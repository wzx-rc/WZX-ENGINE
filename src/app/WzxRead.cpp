// Reader of the .wzx scene format, shared by the editor and the player (see WzxRead.h, format in SceneWzx.cpp).
#include "WzxRead.h"
#include "vp3d/AssetImport.h"
#include "vp3d/MeshGen.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace app {

const char* const kWzxMeshFile = "file:";

namespace {

const double kDegPerRad = 180.0 / 3.14159265358979323846;

// snprintf rather than std::to_chars: floating point to_chars needs iOS 16.3 and the player targets iOS 15
std::string fnum(float v) {
    if (!std::isfinite(v)) v = 0.0f;
    char b[32];
    snprintf(b, sizeof(b), "%.9g", (double)v);
    return b;
}
float3 vec3(const Json& j, const float3& def) {
    if (j.type != Json::Array || j.a.size() < 3) return def;
    return float3((float)j[0].num(def.x), (float)j[1].num(def.y), (float)j[2].num(def.z));
}
float f(const Json& j, float def) { return (float)j.num(def); }
float angle(const Json& j, float defRadians) { return j.type == Json::Number ? (float)(j.n / kDegPerRad) : defRadians; }
bool flag(const Json& j, bool def) { return j.type == Json::Bool ? j.b : def; }
MeshData placeholderBox() { return makeBox({-3, 0, -3}, {3, 6, 3}); }

} // namespace

void readWzxScene(const Json& j, Scene& sc, const std::function<std::wstring(const std::string&)>& absPath,
                  const std::string& fallbackName, WzxRead& out) {
    auto warn = [&](const std::string& s) { out.warnings.push_back(s); };

    const Json& sj = j["scene"];
    sc.name = sj["name"].str(fallbackName);
    sc.unitsPerMeter = f(sj["unitsPerMeter"], 100.0f);
    sc.gridScale = f(sj["gridScale"], 1.0f);

    const Json& ej = j["environment"];
    Environment& e = sc.env;
    e.top = vec3(ej["skyTop"], e.top);
    e.horizon = vec3(ej["skyHorizon"], e.horizon);
    e.bottom = vec3(ej["skyBottom"], e.bottom);
    e.intensity = f(ej["skyIntensity"], e.intensity);
    e.ambient = vec3(ej["ambient"], e.ambient);
    e.visibleToCamera = flag(ej["visible"], true);
    e.hdriIntensity = f(ej["hdriIntensity"], 1.0f);
    e.hdriRotation = angle(ej["hdriRotation"], 0.0f);

    const Json& sun = j["sun"];
    sc.sun.enabled = flag(sun["enabled"], false);
    sc.sun.direction = vec3(sun["direction"], sc.sun.direction);
    sc.sun.color = vec3(sun["color"], sc.sun.color);
    sc.sun.intensity = f(sun["intensity"], sc.sun.intensity);
    sc.sun.id = (uint32_t)std::max(0.0, sun["id"].num(0));
    sc.sun.collection = (uint32_t)std::max(0.0, sun["collection"].num(0));
    sc.sun.order = (int)sun["order"].num(0);
    sc.sun.selectable = flag(sun["selectable"], true);

    // Scene Manager collections (before the objects: their members refer to them)
    for (const Json& cj : j["collections"].a) {
        Collection c;
        c.id = (uint32_t)std::max(0.0, cj["id"].num(0));
        c.name = cj["name"].str("Collection");
        c.color = (int)cj["color"].num(0);
        c.collapsed = flag(cj["collapsed"], false);
        c.order = (int)cj["order"].num(0);
        if (c.id) sc.collections.push_back(c);
    }

    for (const Json& lj : j["lights"].a) {
        AreaLight l;
        l.name = lj["name"].str(l.name);
        l.position = vec3(lj["position"], l.position);
        l.target = vec3(lj["target"], l.target);
        l.width = f(lj["width"], l.width);
        l.height = f(lj["height"], l.height);
        l.color = vec3(lj["color"], l.color);
        l.intensity = f(lj["intensity"], l.intensity);
        l.visibleToCamera = flag(lj["visible"], l.visibleToCamera);
        l.enabled = flag(lj["enabled"], l.enabled);
        l.id = (uint32_t)std::max(0.0, lj["id"].num(0));
        l.parent = (uint32_t)std::max(0.0, lj["parent"].num(0));
        l.collection = (uint32_t)std::max(0.0, lj["collection"].num(0));
        l.order = (int)lj["order"].num(0);
        l.selectable = flag(lj["selectable"], true);
        sc.lights.push_back(l);
    }

    // textures: one per file; a missing image is a small magenta texture that keeps its path
    std::map<std::string, int> textures;
    auto texture = [&](const std::string& rel) {
        auto it = textures.find(rel);
        if (it != textures.end()) return it->second;
        Texture t;
        std::string err;
        if (!loadImage(absPath(rel), t, &err, 2048)) {
            warn("Texture not found: " + rel + " (kept in the scene)");
            t = Texture();
            t.width = t.height = 2;
            for (int k = 0; k < 4; ++k) t.rgba.insert(t.rgba.end(), {255, 0, 255, 255});
        }
        t.name = rel;
        return textures[rel] = sc.addTexture(std::move(t));
    };
    for (const Json& mj : j["materials"].a) {
        Material m;
        m.name = mj["name"].str(m.name);
        m.baseColor = vec3(mj["baseColor"], m.baseColor);
        if (mj["baseColorTexture"].type == Json::String) m.baseColorTex = texture(mj["baseColorTexture"].s);
        m.roughness = f(mj["roughness"], m.roughness);
        m.metallic = f(mj["metallic"], m.metallic);
        m.specular = f(mj["specular"], m.specular);
        m.transmission = f(mj["transmission"], m.transmission);
        m.ior = f(mj["ior"], m.ior);
        m.subsurface = f(mj["subsurface"], m.subsurface);
        m.sssRadius = vec3(mj["subsurfaceRadius"], m.sssRadius);
        m.emission = vec3(mj["emission"], m.emission);
        m.emissionStrength = f(mj["emissionStrength"], m.emissionStrength);
        m.coat = f(mj["coat"], m.coat);
        m.coatRoughness = f(mj["coatRoughness"], m.coatRoughness);
        m.bevelRadius = f(mj["bevelRadius"], m.bevelRadius);
        m.flags = (uint32_t)mj["flags"].num(0);
        m.preset = (int)mj["preset"].num(0);
        m.colorId = (int)mj["color"].num(-1);
        sc.addMaterial(m);
    }
    const int fileMaterials = (int)sc.materials.size();

    // meshes: shared by every object of the same shape or file
    std::map<std::string, int> meshes;
    std::map<int, bool> missing;   // mesh index -> placeholder of a missing file
    auto shapeMesh = [&](const Json& s) {
        const std::string prim = s["primitive"].str();
        if (const PartDef* p = findPart(prim)) return partMesh(sc, *p);
        std::string key;
        MeshData mesh;
        if (prim == "plane") {
            float size = f(s["size"], 400.0f);
            key = "plane " + fnum(size);
            if (!meshes.count(key)) mesh = makePlane(size);
        } else if (prim == "box") {
            float3 mn = vec3(s["min"], float3(-3, 0, -3)), mx = vec3(s["max"], float3(3, 6, 3));
            key = "box " + fnum(mn.x) + " " + fnum(mn.y) + " " + fnum(mn.z) + " " + fnum(mx.x) + " " + fnum(mx.y) + " " + fnum(mx.z);
            if (!meshes.count(key)) mesh = makeBox(mn, mx);
        } else {
            key = "unknown " + prim;
            if (!meshes.count(key)) {
                warn("Unknown primitive: " + prim + " (replaced by a box)");
                mesh = placeholderBox();
            }
        }
        auto it = meshes.find(key);
        if (it != meshes.end()) return it->second;
        return meshes[key] = sc.addMesh(prim == "plane" ? "ground" : prim, std::move(mesh));
    };
    auto fileMesh = [&](const std::string& rel) {
        const std::string key = kWzxMeshFile + rel;
        auto it = meshes.find(key);
        if (it != meshes.end()) return it->second;
        MeshData mesh;
        std::string err;
        bool ok = out.loadMesh ? out.loadMesh(absPath(rel), mesh, &err) : loadObj(absPath(rel), mesh, nullptr, &err);
        if (!ok) {
            warn("Mesh file not found: " + rel + " (red placeholder, the path is kept)");
            mesh = placeholderBox();
        }
        int idx = sc.addMesh(key, std::move(mesh));
        if (!ok) missing[idx] = true;
        return meshes[key] = idx;
    };

    for (const Json& oj : j["objects"].a) {
        Object o;
        o.name = oj["name"].str("Object");
        o.part = oj["type"].str();
        const Json& shape = oj["shape"];
        for (int t = PRIM_CUBE; t <= PRIM_DISK; ++t)   // parametric primitive: its mesh is built after addObject
            if (shape["primitive"].str() == primitiveName(t)) {
                PrimitiveParams& p = o.prim;
                p.type = t;
                p.size = vec3(shape["size"], p.size);
                p.radius = f(shape["radius"], p.radius);
                p.radius2 = f(shape["radius2"], p.radius2);
                p.segments = (int)shape["segments"].num(p.segments);
                p.rings = (int)shape["rings"].num(p.rings);
                p.subdiv = (int)shape["subdiv"].num(p.subdiv);
                o.mesh = -1;
            }
        o.group = flag(oj["group"], false);   // Scene Manager group node: a transform only
        const Json& cj = oj["camera"];
        o.camera = !o.group && cj.type == Json::Object;   // camera object: a transform and its lens
        if (o.camera) {
            o.mesh = -1;
            if (o.part.empty()) o.part = "CAMERA";
            o.camFovY = angle(cj["fov"], o.camFovY);
            o.camOrtho = flag(cj["orthographic"], false);
            o.camOrthoHeight = std::max(f(cj["orthoHeight"], o.camOrthoHeight), 1e-3f);
            o.camDistance = std::max(f(cj["distance"], o.camDistance), 0.01f);
            o.camAspect = std::clamp(f(cj["aspect"], o.camAspect), 0.1f, 10.0f);
            o.camRender = flag(cj["render"], false);
        }
        o.prefab = oj["prefab"].str();   // live prefab instance: a transform, its parts come from the file (PrefabInstance.h)
        if (!o.prefab.empty()) {
            o.mesh = -1;
            o.part = "PREFAB";
            o.group = false;
            o.clip = oj["clip"].str();
        } else if (o.group) { o.mesh = -1; if (o.part.empty()) o.part = "GROUP"; }
        else if (o.camera) {}
        else if (oj["curve"].type == Json::Object) {   // curve object: its points, no mesh
            if (out.readCurve) out.readCurve(oj["curve"], o);
            o.mesh = -1;
            if (o.part.empty()) o.part = "CURVE";
        }
        else if (o.prim.type == PRIM_NONE) o.mesh = oj["mesh"].type == Json::String ? fileMesh(oj["mesh"].s) : shapeMesh(shape);
        o.position = vec3(oj["position"], o.position);
        const Json& r = oj["rotation"];
        if (r.type == Json::Array && r.a.size() == 4) {
            o.rot = float4((float)r[0].num(), (float)r[1].num(), (float)r[2].num(), (float)r[3].num(1));
            float len = std::sqrt(o.rot.x * o.rot.x + o.rot.y * o.rot.y + o.rot.z * o.rot.z + o.rot.w * o.rot.w);
            if (std::fabs(len - 1.0f) > 1e-4f) o.rot = qNormalize(o.rot);   // hand-edited file
        }
        o.useRot = true;
        o.scale = vec3(oj["scale"], o.scale);
        o.pivot = vec3(oj["pivot"], o.pivot);
        o.visible = flag(oj["visible"], true);
        o.selectable = flag(oj["selectable"], true);
        o.id = (uint32_t)std::max(0.0, oj["id"].num(0));   // Scene Manager (0 / missing: a new id is given)
        o.parent = (uint32_t)std::max(0.0, oj["parent"].num(0));
        o.collection = (uint32_t)std::max(0.0, oj["collection"].num(0));
        o.order = (int)oj["order"].num(0);
        o.collapsed = flag(oj["collapsed"], false);
        int m = (int)oj["material"].num(-1);
        if (o.group || o.camera || !o.prefab.empty()) {
            o.material = 0;   // never used (no geometry)
            sc.addObject(o);
            continue;
        }
        if (m < 0 || m >= fileMaterials) {
            Material def;
            def.name = o.name;
            m = sc.addMaterial(def);
        }
        o.material = m;
        if (missing.count(o.mesh)) {   // red placeholder: its own red material, the file keeps the object's material
            Material red;
            red.name = "Missing mesh";
            red.baseColor = float3(srgbToLinear(0.95f), srgbToLinear(0.1f), srgbToLinear(0.08f));
            red.roughness = 0.6f;
            o.material = sc.addMaterial(red);
            out.placeholderMat[o.material] = m;
        }
        int idx = sc.addObject(o);
        if (o.prim.type != PRIM_NONE) syncPrimitiveMesh(sc, idx);
    }

    const Json& cj = j["camera"];
    Camera& c = sc.camera;
    c.target = vec3(cj["target"], c.target);
    c.yaw = angle(cj["yaw"], c.yaw);
    c.pitch = angle(cj["pitch"], c.pitch);
    c.distance = f(cj["distance"], c.distance);
    c.fovY = angle(cj["fov"], c.fovY);
    c.dof = flag(cj["dof"], c.dof);
    c.fStop = f(cj["fStop"], c.fStop);
    c.focalLengthMm = f(cj["focalLength"], c.focalLengthMm);
    c.focusDistance = f(cj["focusDistance"], c.focusDistance);
    c.sensorScale = f(cj["sensorScale"], sc.unitsPerMeter);
}

std::string jsonText(const Json& j) {
    switch (j.type) {
    case Json::Bool: return j.b ? "true" : "false";
    case Json::Number: {
        if (j.n == std::floor(j.n) && std::fabs(j.n) < 1e15) return std::to_string((long long)j.n);
        char b[32];
        snprintf(b, sizeof(b), "%.17g", j.n);
        return b;
    }
    case Json::String: {
        std::string o = "\"";
        for (unsigned char c : j.s) {
            if (c == '"' || c == '\\') { o.push_back('\\'); o.push_back((char)c); }
            else if (c == '\n') o += "\\n";
            else if (c == '\t') o += "\\t";
            else if (c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
            else o.push_back((char)c);
        }
        return o + "\"";
    }
    case Json::Array: {
        std::string o = "[";
        for (size_t k = 0; k < j.a.size(); ++k) o += (k ? "," : "") + jsonText(j.a[k]);
        return o + "]";
    }
    case Json::Object: {
        std::string o = "{";
        bool first = true;
        for (const auto& kv : j.o) {
            Json key;
            key.type = Json::String;
            key.s = kv.first;
            o += (first ? "" : ",") + jsonText(key) + ":" + jsonText(kv.second);
            first = false;
        }
        return o + "}";
    }
    default: return "null";
    }
}

std::vector<std::string> wzxSceneFiles(const Json& j) {
    std::vector<std::string> files;
    std::set<std::string> seen;
    auto add = [&](const Json& v) {
        if (v.type == Json::String && !v.s.empty() && v.s != "sky" && seen.insert(v.s).second) files.push_back(v.s);
    };
    add(j["environment"]["hdri"]);
    for (const Json& o : j["objects"].a) { add(o["mesh"]); add(o["prefab"]); }   // (the meshes of a prefab: see prefabFiles)
    for (const Json& m : j["materials"].a) add(m["baseColorTexture"]);
    add(j["probes"]);
    return files;
}

} // namespace app
