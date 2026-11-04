// Scene file format .wzx: readable JSON (fixed key order, one value per line, diff friendly). The file holds the
// whole scene: opening it rebuilds everything from the file, no built-in scene underneath. Geometry is never
// stored: primitives are a shape and its parameters, imported meshes the path of their source file (relative to
// the project folder) read again on open. A missing file leaves a red placeholder box that keeps its path.
// Also the document of the editor: current file, modified state (*), Save / Save As / Open / New and the
// "Save changes?" prompt.
// The reader is in WzxRead.cpp (shared with the player). Version 1 reader: unknown keys are ignored and missing keys take their defaults, so fields can be added without
// a version bump. Scene Manager: a top-level "collections" array, and per object / light / sun id, parent, collection,
// order, selectable (objects: group, collapsed); positions stay world values, groups have no geometry.
#include "App.h"
#include "GameHost.h"
#include "CurveEditor.h"
#include "Json.h"
#include "MeshFile.h"
#include "WzxRead.h"
#include "vp3d/AssetImport.h"
#include <algorithm>
#include <charconv>
#include <commdlg.h>
#include <filesystem>
#include <fstream>
#include <functional>
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

float f(const Json& j, float def) { return (float)j.num(def); }
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

const char* const kMeshFile = kWzxMeshFile;   // mesh name prefix of an imported mesh (WzxRead.h)

// Geometry of an object, never the mesh data: "mesh" = path of an imported file, else "shape" = a primitive and
// its parameters. Parametric primitives (size, radius, segments, rings, subdiv) are written here, read in
// App::loadWzxScene (shapeMesh).
void writeGeometry(Writer& w, const Scene& sc, const Object& o, const std::function<std::string(const std::string&)>& path) {
    const std::string mesh = o.mesh >= 0 && o.mesh < (int)sc.meshNames.size() ? sc.meshNames[o.mesh] : std::string();
    if (mesh.rfind(kMeshFile, 0) == 0) {
        w.str("mesh", path(mesh.substr(strlen(kMeshFile))));
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

std::string App::wzxSceneJson(bool withCamera, const std::function<std::string(const std::string&)>& mapPath) const {
    const Scene& sc = m_scene;
    auto path = [&](const std::string& p) { return mapPath ? mapPath(p) : p; };
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
        const Camera& c = perspCamera();   // (the persp camera, also when a pane looks through another one)
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
    else w.str("hdri", path(relPath(m_hdriPath)));
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
        if (sc.objects[i].group || sc.objects[i].camera) continue;   // a group or a camera has no material
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
            w.str("baseColorTexture", path(sc.textures[m.baseColorTex].name));
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
        else if (o.camera) {   // camera object: a transform and its lens, no geometry
            w.beginObject("camera");
            w.deg("fov", o.camFovY);
            w.boolean("orthographic", o.camOrtho);
            w.num("orthoHeight", o.camOrthoHeight);
            w.num("distance", o.camDistance);
            w.num("aspect", o.camAspect);
            w.boolean("render", o.camRender);
            w.endObject();
        } else if (o.curve.valid()) w.value("curve", curveToJson(o.curve));   // curve: its points (CurveEditor.cpp)
        else writeGeometry(w, sc, o, path);
        w.vec("position", o.position);
        w.vec4("rotation", objRot(o));
        w.vec("scale", o.scale);
        if (o.pivot.x != 0 || o.pivot.y != 0 || o.pivot.z != 0) w.vec("pivot", o.pivot);   // (Edit Pivot: the point of the geometry at the position)
        w.boolean("visible", o.visible);
        w.boolean("selectable", o.selectable);
        // Scene Manager: links and order (transforms above are world values), a group has no material
        w.integer("id", (int)o.id);
        w.integer("parent", (int)o.parent);
        w.integer("collection", (int)o.collection);
        w.integer("order", o.order);
        w.boolean("collapsed", o.collapsed);
        int m = (o.group || o.camera) ? -1 : materialOf(o);
        w.integer("material", m >= 0 && m < (int)fileIndex.size() ? fileIndex[m] : -1);
        w.endObject();
    }
    w.endArray();

    Json village;
    village.type = Json::Array;
    if (m_game) Json::parse(m_game->villageJson(), village);
    w.value("village", village);
    Json graph;
    if (m_game) Json::parse(m_game->graphJson(), graph);
    w.value("graph", graph);
    w.str("probes", path("game/probes/" + m_probeKey + ".probes"));
    w.endObject();
    w.out += '\n';
    return w.out;
}

bool App::saveSceneFile(const std::wstring& path) {
    if (!endsWithNoCase(path, L".wzx")) return saveLegacySceneFile(path);   // old format, by extension only
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

    WzxRead rd;
    rd.readCurve = [](const Json& cj, Object& o) { curveFromJson(cj, o.curve); };   // curve objects (CurveEditor.cpp)
    rd.loadMesh = [](const std::wstring& p, MeshData& m, std::string* e) { return loadMeshFile(p, m, nullptr, e); };   // OBJ or glTF
    readWzxScene(j, sc, [this](const std::string& p) { return absPath(p); }, toUtf8(fs::path(path).stem().wstring()), rd);
    m_placeholderMat = rd.placeholderMat;
    for (const std::string& w : rd.warnings) log(w, 2);
    warnings += (int)rd.warnings.size();
    const Json& ej = j["environment"];
    if (ej["hdri"].type == Json::String && !ej["hdri"].s.empty()) {
        const float intensity = sc.env.hdriIntensity, rotation = sc.env.hdriRotation;
        std::wstring hp = absPath(ej["hdri"].s);
        if (!setHdri(hp)) {   // kept in the scene (and the file) so that it comes back when the file is there again
            m_hdriPath = hp;
            log("HDRI not found: " + ej["hdri"].s + " (kept in the scene)", 2);
            ++warnings;
        }
        sc.env.hdriIntensity = intensity;
        sc.env.hdriRotation = rotation;
    }
    const Camera c = sc.camera;

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

    if (m_game) m_game->loadVillage(j["village"]);   // after the objects: the houses go on the grid
    else m_pendingVillage = j["village"];             // opened from the command line, before the game layer
    if (m_game) m_game->loadSceneGraph(j["graph"]);   // the graph of the scene (none: an empty graph)
    else m_pendingGraph = j["graph"];

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
    resetViewCams();   // persp is the camera of the file, top / front / side frame the scene
    loadActiveCamera();

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
    if (!loadMeshFile(path, mesh, nullptr, &err)) {
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
        loadScene(SCENE_STUDIO, true);
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
