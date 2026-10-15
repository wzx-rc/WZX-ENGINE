// Scene file format .wzx: readable JSON (fixed key order, one value per line, diff friendly). The file holds the
// whole scene: opening it rebuilds everything from the file, no built-in scene underneath. Geometry is never
// stored: primitives are a shape and its parameters, imported meshes the path of their source file (relative to
// the project folder) read again on open. A missing file leaves a red placeholder box that keeps its path.
// Also the document of the editor: current file, modified state (*), Save / Save As / Open / New and the
// "Save changes?" prompt.
// Version 1 reader: unknown keys are ignored and missing keys take their defaults, so fields can be added without
// a version bump. Scene Manager: a top-level "collections" array, and per object / light / sun id, parent, collection,
// order, selectable (objects: group, collapsed); positions stay world values, groups have no geometry.
#include "App.h"
#include "GameHost.h"
#include "Json.h"
#include "vp3d/AssetImport.h"
#include <algorithm>
#include <charconv>
#include <commdlg.h>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace app {

namespace fs = std::filesystem;

namespace {

const double kDegPerRad = 180.0 / 3.14159265358979323846;

// Floats: the shortest text that reads back to the same float, so save, open and save again give the same file.
std::string fnum(float v) {
    if (!std::isfinite(v)) v = 0.0f;
    char b[32];
    auto r = std::to_chars(b, b + sizeof(b), v);
    return std::string(b, r.ptr);
}

// Angles (radians in the scene) are written in degrees with 6 significant digits: the conversion back and forth
// stays far below the last digit, so the text is stable too.
std::string fdeg(float radiansValue) {
    char b[32];
    snprintf(b, sizeof(b), "%.6g", (double)radiansValue * kDegPerRad);
    return b;
}

std::string escape(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o.push_back('\\'); o.push_back((char)c); }
        else if (c == '\n') o += "\\n";
        else if (c == '\t') o += "\\t";
        else if (c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
        else o.push_back((char)c);
    }
    return o;
}

// Pretty JSON writer: keys in the order they are written, small vectors on one line.
class Writer {
public:
    std::string out;
    void beginObject(const char* key = nullptr) { item(key); out += '{'; m_first.push_back(true); }
    void endObject() { close('}'); }
    void beginArray(const char* key = nullptr) { item(key); out += '['; m_first.push_back(true); }
    void endArray() { close(']'); }
    void raw(const char* key, const std::string& v) { item(key); out += v; }
    void str(const char* key, const std::string& v) { raw(key, "\"" + escape(v) + "\""); }
    void num(const char* key, float v) { raw(key, fnum(v)); }
    void deg(const char* key, float r) { raw(key, fdeg(r)); }
    void integer(const char* key, int v) { raw(key, std::to_string(v)); }
    void boolean(const char* key, bool v) { raw(key, v ? "true" : "false"); }
    void vec(const char* key, const float3& v) { raw(key, "[" + fnum(v.x) + ", " + fnum(v.y) + ", " + fnum(v.z) + "]"); }
    void vec4(const char* key, const float4& v) { raw(key, "[" + fnum(v.x) + ", " + fnum(v.y) + ", " + fnum(v.z) + ", " + fnum(v.w) + "]"); }
    // Any parsed value (the village): object keys in alphabetical order (std::map), numbers as integers when they are.
    void value(const char* key, const Json& j) {
        switch (j.type) {
        case Json::Object:
            beginObject(key);
            for (const auto& kv : j.o) value(kv.first.c_str(), kv.second);
            endObject();
            break;
        case Json::Array:
            if (!j.a.empty() && std::all_of(j.a.begin(), j.a.end(), [](const Json& e) { return e.type == Json::Number; })) {
                std::string line = "[";   // numbers on one line, as the vectors
                for (size_t k = 0; k < j.a.size(); ++k) line += (k ? ", " : "") + number(j.a[k].n);
                raw(key, line + "]");
                break;
            }
            beginArray(key);
            for (const Json& e : j.a) value(nullptr, e);
            endArray();
            break;
        case Json::String: str(key, j.s); break;
        case Json::Bool: boolean(key, j.b); break;
        case Json::Number: raw(key, number(j.n)); break;
        default: raw(key, "null"); break;
        }
    }

private:
    std::vector<bool> m_first;
    static std::string number(double n) {
        return n == std::floor(n) && std::fabs(n) < 1e15 ? std::to_string((long long)n) : fnum((float)n);
    }
    void newline() { out += '\n'; out.append(m_first.size() * 2, ' '); }
    void item(const char* key) {
        if (!m_first.empty()) {
            if (!m_first.back()) out += ',';
            m_first.back() = false;
            newline();
        }
        if (key) { out += '"'; out += escape(key); out += "\": "; }
    }
    void close(char c) {
        bool empty = m_first.back();
        m_first.pop_back();
        if (!empty) newline();
        out += c;
    }
};

float3 vec3(const Json& j, const float3& def) {
    if (j.type != Json::Array || j.a.size() < 3) return def;
    return float3((float)j[0].num(def.x), (float)j[1].num(def.y), (float)j[2].num(def.z));
}
float f(const Json& j, float def) { return (float)j.num(def); }
float angle(const Json& j, float defRadians) { return j.type == Json::Number ? (float)(j.n / kDegPerRad) : defRadians; }
bool flag(const Json& j, bool def) { return j.type == Json::Bool ? j.b : def; }

bool endsWithNoCase(const std::wstring& s, const wchar_t* suffix) {
    size_t n = wcslen(suffix);
    return s.size() >= n && _wcsicmp(s.c_str() + s.size() - n, suffix) == 0;
}

} // namespace

// Writes next to the file then replaces it: a failed write never leaves half a scene.
bool writeFileSafe(const std::wstring& path, const std::string& text) {
    std::wstring tmp = path + L".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << text;
        f.flush();
        if (!f) { f.close(); DeleteFileW(tmp.c_str()); return false; }
    }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

namespace {

const char* kMeshFile = "file:";   // mesh name prefix of an imported mesh: "file:<path relative to the project>"
MeshData placeholderBox() { return makeBox({-3, 0, -3}, {3, 6, 3}); }

// Geometry of an object, never the mesh data: "mesh" = path of an imported file, else "shape" = a primitive and
// its parameters. Parametric primitives (size, radius, segments, rings, subdiv) are written here, read in
// App::loadWzxScene (shapeMesh).
void writeGeometry(Writer& w, const Scene& sc, const Object& o) {
    const std::string mesh = o.mesh >= 0 && o.mesh < (int)sc.meshNames.size() ? sc.meshNames[o.mesh] : std::string();
    if (mesh.rfind(kMeshFile, 0) == 0) {
        w.str("mesh", mesh.substr(strlen(kMeshFile)));
        return;
    }
    w.beginObject("shape");
    const PartDef* part = findPart(o.part);
    if (o.prim.type != PRIM_NONE) {   // parametric primitive (Create > Mesh): its mesh is rebuilt from these
        const PrimitiveParams& p = o.prim;
        w.str("primitive", primitiveName(p.type));
        w.vec("size", p.size);
        w.num("radius", p.radius);
        w.num("radius2", p.radius2);
        w.integer("segments", p.segments);
        w.integer("rings", p.rings);
        w.integer("subdiv", p.subdiv);
    } else if (part && mesh == part->id) {   // library part (fixed size)
        w.str("primitive", part->id);
    } else {
        // built-in geometry of the test scenes (floors, walls, blocks): a plane or a box, from its bounds
        float3 mn(0), mx(0);
        if (o.mesh >= 0 && o.mesh < (int)sc.meshes.size()) sc.meshes[o.mesh].bounds(mn, mx);
        if (mn.y == mx.y && mx.x - mn.x == mx.z - mn.z) {
            w.str("primitive", "plane");
            w.num("size", mx.x - mn.x);
        } else {
            w.str("primitive", "box");
            w.vec("min", mn);
            w.vec("max", mx);
        }
    }
    w.endObject();
}

std::wstring sceneDialog(HWND hwnd, bool save, const std::wstring& current) {
    wchar_t buf[MAX_PATH] = L"";
    std::wstring dir;
    if (!current.empty()) {
        fs::path p(current);
        dir = p.parent_path().wstring();
        std::wstring name = p.filename().wstring();
        if (save) {
            size_t dot = name.find(L'.');
            name = (dot == std::wstring::npos ? name : name.substr(0, dot)) + L".wzx";
            wcsncpy_s(buf, name.c_str(), _TRUNCATE);
        }
    } else if (save) {
        wcscpy_s(buf, L"scene.wzx");
    }
    OPENFILENAMEW of = {sizeof(of)};
    of.hwndOwner = hwnd;
    of.lpstrFilter = save ? L"WZX scene (*.wzx)\0*.wzx\0"
                          : L"Scenes (*.wzx, *.v3d.json)\0*.wzx;*.v3d.json;*.json\0WZX scene (*.wzx)\0*.wzx\0Old scene (*.v3d.json)\0*.v3d.json;*.json\0All files\0*.*\0";
    of.lpstrFile = buf;
    of.nMaxFile = MAX_PATH;
    of.lpstrDefExt = L"wx";
    if (!dir.empty()) of.lpstrInitialDir = dir.c_str();
    of.Flags = save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST;
    BOOL ok = save ? GetSaveFileNameW(&of) : GetOpenFileNameW(&of);
    return ok ? std::wstring(buf) : std::wstring();
}

} // namespace

// --------------------------------------------------------------------------- paths

std::wstring App::projectRoot() const {
    fs::path exe(m_exeDir);
    if (fs::exists(exe / "game.json")) return exe.wstring();   // built game: next to the executable
    fs::path up = exe.parent_path().parent_path();             // editor: two levels above build\bin
    return fs::exists(up / "src") ? up.wstring() : exe.wstring();
}

// Path written in the file: relative to the project folder (forward slashes), absolute when outside of it.
std::string App::relPath(const std::wstring& path) const {
    if (path.empty() || path == L"sky") return toUtf8(path);
    std::error_code ec;
    fs::path p = fs::weakly_canonical(fs::path(path), ec);
    if (ec) p = fs::path(path);
    fs::path root = fs::weakly_canonical(fs::path(projectRoot()), ec);
    fs::path r = p.lexically_relative(root);
    if (!r.empty() && r.native().rfind(L"..", 0) != 0) return toUtf8(r.generic_wstring());
    return toUtf8(p.generic_wstring());
}

std::wstring App::absPath(const std::string& path) const {
    if (path.empty() || path == "sky") return fromUtf8(path);
    fs::path p(fromUtf8(path));
    if (p.is_relative()) p = fs::path(projectRoot()) / p;
    return p.make_preferred().wstring();
}

// --------------------------------------------------------------------------- write

std::string App::wzxSceneJson(bool withCamera) const {
    const Scene& sc = m_scene;
    Writer w;
    w.beginObject();
    w.str("format", "wzx-scene");
    w.integer("version", 1);
    w.str("units", "cm");

    w.beginObject("scene");
    w.str("name", sc.name);
    w.num("unitsPerMeter", sc.unitsPerMeter);
    w.num("gridScale", sc.gridScale);
    w.endObject();

    // Scene Manager collections: folders of the list, no effect on the scene (members: "collection" of the root items)
    w.beginArray("collections");
    for (const Collection& c : sc.collections) {
        w.beginObject();
        w.integer("id", (int)c.id);
        w.str("name", c.name);
        w.integer("color", c.color);
        w.boolean("collapsed", c.collapsed);
        w.integer("order", c.order);
        w.endObject();
    }
    w.endArray();

    if (withCamera) {   // left out of the modified test: moving the view does not modify the scene
        const Camera& c = sc.camera;
        w.beginObject("camera");
        w.vec("target", c.target);
        w.deg("yaw", c.yaw);
        w.deg("pitch", c.pitch);
        w.num("distance", c.distance);
        w.deg("fov", c.fovY);
        w.boolean("dof", c.dof);
        w.num("fStop", c.fStop);
        w.num("focalLength", c.focalLengthMm);
        w.num("focusDistance", c.focusDistance);
        w.num("sensorScale", c.sensorScale);
        w.endObject();
    }

    const Environment& e = sc.env;
    w.beginObject("environment");
    if (m_hdriPath.empty()) w.raw("hdri", "null");
    else w.str("hdri", relPath(m_hdriPath));
    w.num("hdriIntensity", e.hdriIntensity);
    w.deg("hdriRotation", e.hdriRotation);
    w.boolean("visible", e.visibleToCamera);
    w.vec("ambient", e.ambient);
    w.vec("skyTop", e.top);
    w.vec("skyHorizon", e.horizon);
    w.vec("skyBottom", e.bottom);
    w.num("skyIntensity", e.intensity);
    w.endObject();

    w.beginObject("sun");
    w.boolean("enabled", sc.sun.enabled);
    w.vec("direction", sc.sun.direction);
    w.vec("color", sc.sun.color);
    w.num("intensity", sc.sun.intensity);
    w.integer("id", (int)sc.sun.id);
    w.integer("collection", (int)sc.sun.collection);
    w.integer("order", sc.sun.order);
    w.boolean("selectable", sc.sun.selectable);
    w.endObject();

    w.beginArray("lights");
    for (const AreaLight& l : sc.lights) {
        w.beginObject();
        w.str("name", l.name);
        w.vec("position", l.position);
        w.vec("target", l.target);
        w.num("width", l.width);
        w.num("height", l.height);
        w.vec("color", l.color);
        w.num("intensity", l.intensity);
        w.boolean("visible", l.visibleToCamera);
        w.boolean("enabled", l.enabled);
        w.integer("id", (int)l.id);
        w.integer("parent", (int)l.parent);
        w.integer("collection", (int)l.collection);
        w.integer("order", l.order);
        w.boolean("selectable", l.selectable);
        w.endObject();
    }
    w.endArray();

    const RenderSettings& rs = m_rs;
    w.beginObject("render");
    w.integer("mode", renderMode());
    w.integer("quality", m_quality);
    w.num("exposure", rs.exposure);
    w.integer("tonemapper", rs.tonemapper);
    w.num("vignette", rs.vignette);
    w.integer("maxBounces", rs.maxBounces);
    w.integer("maxSamples", rs.maxSamples);
    w.boolean("denoise", rs.denoise);
    w.num("denoiseStrength", rs.denoiseStrength);
    w.integer("denoisePasses", rs.denoisePasses);
    w.num("denoiseEdgeLuma", rs.denoiseEdgeLuma);
    w.boolean("bevel", rs.bevel);
    w.num("bevelScale", rs.bevelScale);
    w.num("fireflyClamp", rs.fireflyClamp);
    w.num("filterGlossy", rs.filterGlossy);
    w.num("indirectClamp", rs.indirectClamp);
    w.num("sssScale", rs.sssScale);
    w.num("lightScale", rs.lightScale);
    w.boolean("hdriSun", rs.rasterEnvSun);
    w.boolean("bakedProbes", rs.bakedProbes);
    w.endObject();

    // The objects saved: everything but the placement previews and the village houses (rebuilt from "village").
    // Materials: the ones used by those objects, in scene order, referenced by their index in the file.
    std::vector<int> saved;
    for (int i = 0; i < (int)sc.objects.size(); ++i) {
        const Object& o = sc.objects[i];
        if (o.ghost || o.part == "House") continue;
        saved.push_back(i);
    }
    auto materialOf = [&](const Object& o) {
        auto it = m_placeholderMat.find(o.material);   // a placeholder is red, its own material is kept for the file
        return it != m_placeholderMat.end() ? it->second : o.material;
    };
    std::vector<int> fileIndex(sc.materials.size(), -1), order;
    for (int i : saved) {
        if (sc.objects[i].group) continue;   // a group has no material
        int m = materialOf(sc.objects[i]);
        if (m >= 0 && m < (int)sc.materials.size() && fileIndex[m] < 0) {
            fileIndex[m] = 0;
            order.push_back(m);
        }
    }
    std::sort(order.begin(), order.end());
    for (int k = 0; k < (int)order.size(); ++k) fileIndex[order[k]] = k;

    w.beginArray("materials");
    for (int mi : order) {
        const Material& m = sc.materials[mi];
        w.beginObject();
        w.str("name", m.name);
        w.vec("baseColor", m.baseColor);
        if (m.baseColorTex >= 0 && m.baseColorTex < (int)sc.textures.size() && !sc.textures[m.baseColorTex].name.empty())
            w.str("baseColorTexture", sc.textures[m.baseColorTex].name);
        w.num("roughness", m.roughness);
        w.num("metallic", m.metallic);
        w.num("specular", m.specular);
        w.num("transmission", m.transmission);
        w.num("ior", m.ior);
        w.num("subsurface", m.subsurface);
        w.vec("subsurfaceRadius", m.sssRadius);
        w.vec("emission", m.emission);
        w.num("emissionStrength", m.emissionStrength);
        w.num("coat", m.coat);
        w.num("coatRoughness", m.coatRoughness);
        w.num("bevelRadius", m.bevelRadius);
        w.integer("flags", (int)m.flags);
        w.integer("preset", m.preset);
        w.integer("color", m.colorId);
        w.endObject();
    }
    w.endArray();

    w.beginArray("objects");
    for (int i : saved) {
        const Object& o = sc.objects[i];
        w.beginObject();
        w.str("name", o.name);
        w.str("type", o.part);
        if (o.group) w.boolean("group", true);   // group node: a transform only, no geometry
        else writeGeometry(w, sc, o);
        w.vec("position", o.position);
        w.vec4("rotation", objRot(o));
        w.vec("scale", o.scale);
        w.boolean("visible", o.visible);
        w.boolean("selectable", o.selectable);
        // Scene Manager: links and order (transforms above are world values), a group has no material
        w.integer("id", (int)o.id);
        w.integer("parent", (int)o.parent);
        w.integer("collection", (int)o.collection);
        w.integer("order", o.order);
        w.boolean("collapsed", o.collapsed);
        int m = o.group ? -1 : materialOf(o);
        w.integer("material", m >= 0 && m < (int)fileIndex.size() ? fileIndex[m] : -1);
        w.endObject();
    }
    w.endArray();

    Json village;
    village.type = Json::Array;
    if (m_game) Json::parse(m_game->villageJson(), village);
    w.value("village", village);
    w.str("probes", "game/probes/" + m_probeKey + ".probes");
    w.endObject();
    w.out += '\n';
    return w.out;
}

bool App::saveSceneFile(const std::wstring& path) {
    if (!endsWithNoCase(path, L".wzx")) return saveLegacySceneFile(path);   // the Build and the player read .v3d.json
    if (!writeFileSafe(path, wzxSceneJson(true))) return false;
    status(L"Scene saved: " + fs::path(path).filename().wstring(), 1);
    return true;
}

// --------------------------------------------------------------------------- read

bool App::loadSceneFile(const std::wstring& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        status(L"Scene not found: " + path, 2);
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    Json j;
    if (!Json::parse(ss.str(), j) || j.type != Json::Object) {
        status(L"Invalid scene file (not JSON): " + fs::path(path).filename().wstring(), 2);
        return false;
    }
    const std::string format = j["format"].str();
    if (format == "vp3d-scene") {   // scene saved before .wzx: imported, Save writes a .wzx
        if (!loadLegacySceneFile(j, path)) return false;
        m_docPath.clear();
        m_docName = fs::path(path).filename().wstring() + L" (import)";
        m_savedSig.clear();   // not saved as .wzx yet: modified
        updateDocState(true);
        return true;
    }
    if (format != "wzx-scene") {
        status(L"Unknown scene format: " + fs::path(path).filename().wstring(), 2);
        return false;
    }
    const int version = (int)j["version"].num(0);
    if (version < 1 || version > 1) {
        status(L"Scene made by a newer version of WZX Engine (format version " + std::to_wstring(version) + L"): " +
                   fs::path(path).filename().wstring(), 2);
        return false;
    }
    loadWzxScene(j, path);
    m_docPath = path;
    m_docName = fs::path(path).filename().wstring();
    markSaved();
    return true;
}

void App::loadWzxScene(const Json& j, const std::wstring& path) {
    // a clean editor state, as loadScene, but nothing of a built-in scene
    m_sceneId = -1;
    m_scene = Scene();
    m_placeholderMat.clear();
    m_hdriPath.clear();
    m_selected = -1;
    m_multi.clear();
    m_outSel.clear();
    m_renameId = 0;
    m_hierCache.clear();   // new scene: no parent moved (the ids of the file may match ids of the old scene)
    m_undo.clear();
    m_redo.clear();
    m_gz = GizmoDrag();
    if (m_game) m_game->resetVillage();
    Scene& sc = m_scene;
    int warnings = 0;

    const Json& sj = j["scene"];
    sc.name = sj["name"].str(toUtf8(fs::path(path).stem().wstring()));
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
    if (ej["hdri"].type == Json::String && !ej["hdri"].s.empty()) {
        std::wstring hp = absPath(ej["hdri"].s);
        if (!setHdri(hp)) {   // kept in the scene (and the file) so that it comes back when the file is there again
            m_hdriPath = hp;
            log("HDRI not found: " + ej["hdri"].s + " (kept in the scene)", 2);
            ++warnings;
        }
    }
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
            log("Texture not found: " + rel + " (kept in the scene)", 2);
            ++warnings;
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
                log("Unknown primitive: " + prim + " (replaced by a box)", 2);
                ++warnings;
                mesh = placeholderBox();
            }
        }
        auto it = meshes.find(key);
        if (it != meshes.end()) return it->second;
        return meshes[key] = sc.addMesh(prim == "plane" ? "ground" : prim, std::move(mesh));
    };
    auto fileMesh = [&](const std::string& rel) {
        const std::string key = kMeshFile + rel;
        auto it = meshes.find(key);
        if (it != meshes.end()) return it->second;
        MeshData mesh;
        std::string err;
        bool ok = loadObj(absPath(rel), mesh, nullptr, &err);
        if (!ok) {
            log("Mesh file not found: " + rel + " (red placeholder, the path is kept)", 2);
            ++warnings;
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
        if (o.group) { o.mesh = -1; if (o.part.empty()) o.part = "GROUP"; }
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
        o.visible = flag(oj["visible"], true);
        o.selectable = flag(oj["selectable"], true);
        o.id = (uint32_t)std::max(0.0, oj["id"].num(0));   // Scene Manager (0 / missing: a new id is given)
        o.parent = (uint32_t)std::max(0.0, oj["parent"].num(0));
        o.collection = (uint32_t)std::max(0.0, oj["collection"].num(0));
        o.order = (int)oj["order"].num(0);
        o.collapsed = flag(oj["collapsed"], false);
        int m = (int)oj["material"].num(-1);
        if (o.group) {
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
            m_placeholderMat[o.material] = m;
        }
        int idx = sc.addObject(o);
        if (o.prim.type != PRIM_NONE) syncPrimitiveMesh(sc, idx);
    }

    const Json& rj = j["render"];
    if (rj.type == Json::Object) {
        setRenderMode((int)rj["mode"].num(renderMode()));
        m_quality = (int)rj["quality"].num(m_quality);
        applyQuality();
        RenderSettings& rs = m_rs;
        rs.exposure = f(rj["exposure"], rs.exposure);
        rs.tonemapper = (int)rj["tonemapper"].num(rs.tonemapper);
        rs.vignette = f(rj["vignette"], rs.vignette);
        rs.maxBounces = (int)rj["maxBounces"].num(rs.maxBounces);
        rs.maxSamples = (int)rj["maxSamples"].num(rs.maxSamples);
        rs.denoise = flag(rj["denoise"], rs.denoise);
        rs.denoiseStrength = f(rj["denoiseStrength"], rs.denoiseStrength);
        rs.denoisePasses = (int)rj["denoisePasses"].num(rs.denoisePasses);
        rs.denoiseEdgeLuma = f(rj["denoiseEdgeLuma"], rs.denoiseEdgeLuma);
        rs.bevel = flag(rj["bevel"], rs.bevel);
        rs.bevelScale = f(rj["bevelScale"], rs.bevelScale);
        rs.fireflyClamp = f(rj["fireflyClamp"], rs.fireflyClamp);
        rs.filterGlossy = f(rj["filterGlossy"], rs.filterGlossy);
        rs.indirectClamp = f(rj["indirectClamp"], rs.indirectClamp);
        rs.sssScale = f(rj["sssScale"], rs.sssScale);
        rs.lightScale = f(rj["lightScale"], rs.lightScale);
        rs.rasterEnvSun = flag(rj["hdriSun"], rs.rasterEnvSun);
        rs.bakedProbes = flag(rj["bakedProbes"], rs.bakedProbes);
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

    if (m_game) m_game->loadVillage(j["village"]);   // after the objects: the houses go on the grid
    else m_pendingVillage = j["village"];             // opened from the command line, before the game layer

    m_renderer.setScene(m_scene);
    // baked probes: the path of the file, else the name of the scene file
    std::string probes = j["probes"].str();
    fs::path key = probes.empty() ? fs::path(path).filename() : fs::path(fromUtf8(probes)).filename();
    std::wstring k = key.wstring();
    m_probeKey = toUtf8(k.substr(0, k.find(L'.')));
    loadBakedProbesFile();
    clearViews();
    for (auto& v : defaultViews(m_scene, false)) m_views.push_back({v.name, v.cam, 0});
    m_activeView = 0;
    generateThumbnails();
    m_scene.camera = c;

    log("Scene loaded: " + toUtf8(fs::path(path).filename().wstring()) + " (" + std::to_string(sc.objects.size()) + " objects)", 1);
    if (warnings) status(L"Scene loaded with " + std::to_wstring(warnings) + L" warning(s): see the log", 2);
    else status(L"Scene loaded: " + fs::path(path).filename().wstring(), 1);
    markDirty();
}

// --------------------------------------------------------------------------- mesh import

// An OBJ mesh as a new object: the file stays outside the scene, the scene keeps its path.
void App::importMeshFile(const std::wstring& path) {
    MeshData mesh;
    std::string err;
    if (!loadObj(path, mesh, nullptr, &err)) {
        status(L"Mesh not imported: " + fromUtf8(err), 2);
        return;
    }
    pushUndo();
    const std::string rel = relPath(path);
    int mi = m_scene.findMesh(kMeshFile + rel);
    if (mi < 0) mi = m_scene.addMesh(kMeshFile + rel, std::move(mesh));
    Material m;
    m.name = toUtf8(fs::path(path).stem().wstring());
    applyPreset(m, PRESET_MATTE, 1.0f);
    Object o;
    o.name = m.name;
    o.part = "MESH";
    o.mesh = mi;
    o.material = m_scene.addMaterial(m);
    o.position = float3(m_scene.camera.target.x, 0, m_scene.camera.target.z);
    o.useRot = true;
    m_selected = m_scene.addObject(o);
    m_multi.clear();
    m_renderer.setScene(m_scene);
    status(L"Mesh imported: " + fs::path(path).filename().wstring(), 1);
    markDirty();
}

// --------------------------------------------------------------------------- document

void App::markSaved() {
    m_savedSig = wzxSceneJson(false);
    m_docDirty = false;
    updateTitle();
}

// Modified = the scene differs from the saved file (camera aside): an undo back to the saved state clears the *.
void App::updateDocState(bool force) {
    if (m_game && (m_game->playerMode() || m_game->playing())) return;   // Play edits the scene, then gives it back
    uint64_t now = GetTickCount64();
    if (!force && now - m_docCheckTick < 300) return;
    m_docCheckTick = now;
    m_docDirty = wzxSceneJson(false) != m_savedSig;
    updateTitle();
}

void App::updateTitle() {
    if (m_game && m_game->playerMode()) return;
    std::wstring t = m_docName + (m_docDirty ? L"*" : L"") + L" — WZX Engine";
    if (t == m_title) return;
    m_title = t;
    SetWindowTextW(m_hwnd, t.c_str());
}

bool App::saveDocument(bool saveAs) {
    std::wstring p = m_docPath;
    if (saveAs || p.empty()) {
        p = sceneDialog(m_hwnd, true, m_docPath);
        if (p.empty()) return false;
        if (!endsWithNoCase(p, L".wzx")) p += L".wzx";
    }
    if (!saveSceneFile(p)) {
        status(L"Scene not saved: " + p, 2);
        return false;
    }
    m_docPath = p;
    m_docName = fs::path(p).filename().wstring();
    markSaved();
    return true;
}

// New, Open, a built-in scene or closing: asks "Save changes?" first when the scene is modified.
void App::requestDocAction(int action) {
    if (m_game && m_game->playing()) {
        status(L"Stop Play first", 2);
        return;
    }
    updateDocState(true);
    if (m_docDirty) m_confirmAction = action;
    else runDocAction(action);
}

void App::runDocAction(int action) {
    if (action == DOC_OPEN) {
        std::wstring p = sceneDialog(m_hwnd, false, m_docPath);
        if (!p.empty()) loadSceneFile(p);
    } else if (action == DOC_NEW) {
        loadScene(SCENE_STUDIO);
    } else if (action == DOC_CLOSE) {
        m_forceClose = true;
        PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
    } else if (action >= DOC_SCENE) {
        loadScene(action - DOC_SCENE);
    }
}

void App::drawSaveChangesPopup() {
    const char* id = "Save changes?##doc";
    if (m_confirmAction && !ImGui::IsPopupOpen(id)) ImGui::OpenPopup(id);
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) return;
    const char* what = m_confirmAction == DOC_CLOSE ? "closing" : (m_confirmAction == DOC_OPEN ? "opening another scene" : "creating a new scene");
    if (m_confirmAction >= DOC_SCENE) what = "loading a built-in scene";
    ImGui::Text("The scene \"%s\" has been modified.", toUtf8(m_docName).c_str());
    ImGui::Text("Save the changes before %s?", what);
    ImGui::Spacing();
    float bw = 110 * m_dpi;
    auto finish = [&](bool save, bool proceed) {
        if (proceed) {
            m_docNext = m_confirmAction;
            m_docSaveFirst = save;
            m_pendingAction = PA_DOC_NEXT;   // after the frame: file dialogs and loading stay out of the ImGui frame
        }
        m_confirmAction = 0;
        ImGui::CloseCurrentPopup();
    };
    if (ImGui::Button("Save", ImVec2(bw, 0))) finish(true, true);
    ImGui::SameLine();
    if (ImGui::Button("Don't save", ImVec2(bw, 0))) finish(false, true);
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) finish(false, false);
    ImGui::EndPopup();
}

void App::runPendingAction() {
    int a = m_pendingAction;
    m_pendingAction = 0;
    if (a == PA_SAVE) saveDocument(false);
    else if (a == PA_SAVE_AS) saveDocument(true);
    else if (a == PA_OPEN) requestDocAction(DOC_OPEN);
    else if (a == PA_NEW) requestDocAction(DOC_NEW);
    else if (a == PA_DOC_NEXT) {
        if (!m_docSaveFirst || saveDocument(false)) runDocAction(m_docNext);
        m_docNext = 0;
    } else if (a == PA_IMPORT_MESH) {
        wchar_t buf[MAX_PATH] = L"";
        OPENFILENAMEW of = {sizeof(of)};
        of.hwndOwner = m_hwnd;
        of.lpstrFilter = L"OBJ meshes (*.obj)\0*.obj\0All files\0*.*\0";
        of.lpstrFile = buf;
        of.nMaxFile = MAX_PATH;
        of.Flags = OFN_FILEMUSTEXIST;
        if (GetOpenFileNameW(&of)) importMeshFile(buf);
    } else if (a == PA_HDRI) {
        wchar_t buf[MAX_PATH] = L"";
        OPENFILENAMEW of = {sizeof(of)};
        of.hwndOwner = m_hwnd;
        of.lpstrFilter = L"HDR images (*.hdr, *.exr)\0*.hdr;*.exr;*.pic\0Images (*.jpg, *.png, *.tif)\0*.jpg;*.jpeg;*.png;*.bmp;*.tif;*.tiff\0All files\0*.*\0";
        of.lpstrFile = buf;
        of.nMaxFile = MAX_PATH;
        of.Flags = OFN_FILEMUSTEXIST;
        if (GetOpenFileNameW(&of)) setHdri(buf);
    }
}

} // namespace app
