// Prefab studio (workspace 3): the asset being assembled, its own scene (floor, lights, parts posed by the current
// clip) rendered in Lit (raster, instant), files, undo, playback, picking and the move gizmo. Panels: PrefabUI.inl.
#include "App.h"
#include "vp3d/AssetImport.h"
#include "vp3d/shaders/Shared.h"
#include <commdlg.h>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace app {

namespace fs = std::filesystem;

// Open / save dialog of the studio (mesh, texture, prefab files). Empty = cancelled.
std::wstring pickFileDialog(HWND hwnd, bool save, const wchar_t* filter, const wchar_t* defExt, const std::wstring& initialDir,
                        const std::wstring& initialName) {
    wchar_t buf[MAX_PATH] = L"";
    if (!initialName.empty()) wcsncpy_s(buf, initialName.c_str(), _TRUNCATE);
    OPENFILENAMEW of = {sizeof(of)};
    of.hwndOwner = hwnd;
    of.lpstrFilter = filter;
    of.lpstrFile = buf;
    of.nMaxFile = MAX_PATH;
    of.lpstrDefExt = defExt;
    if (!initialDir.empty()) of.lpstrInitialDir = initialDir.c_str();
    of.Flags = save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST;
    BOOL ok = save ? GetSaveFileNameW(&of) : GetOpenFileNameW(&of);
    return ok ? std::wstring(buf) : std::wstring();
}

namespace {

float3 lin(float r, float g, float b) { return float3(srgbToLinear(r), srgbToLinear(g), srgbToLinear(b)); }

} // namespace

// --------------------------------------------------------------------------- asset, files, undo

void App::prefabInit() {
    PrefabStudio& P = m_pf;
    if (P.initialised) return;
    P.initialised = true;
    P.camera.yaw = radians(28.0f);
    P.camera.pitch = radians(24.0f);
    P.camera.distance = 8.0f;
    P.camera.fovY = radians(32.0f);
    P.camera.target = float3(0, 1.1f, 0);
    if (!m_opt.prefabOpen.empty() && prefabOpen(m_opt.prefabOpen)) {
    } else {
        prefabNew(true);
    }
    if (m_opt.prefabSelect >= 0 && m_opt.prefabSelect < (int)P.asset.parts.size()) P.sel = m_opt.prefabSelect;
    if (m_opt.prefabView >= 0) P.debugView = m_opt.prefabView;
    if (m_opt.prefabTab >= 0) P.inspectorTab = m_opt.prefabTab;
    if (m_opt.prefabClip >= 0 && m_opt.prefabClip < (int)P.asset.clips.size()) P.clip = m_opt.prefabClip;
    if (m_opt.prefabTime >= 0) P.time = m_opt.prefabTime;
    if (m_opt.prefabBottom >= 0) P.bottomTab = m_opt.prefabBottom;
    if (m_opt.prefabLight >= 0) P.lightPreset = m_opt.prefabLight;
    if (!m_opt.prefabRigFbx.empty()) { P.asset = PrefabAsset(); P.asset.clips.push_back(PrefabClip()); rigImportFile(m_opt.prefabRigFbx); P.centerTab = 2; }
    if (m_opt.prefabRig >= 0) {
        if (P.asset.joints.empty()) rigLoadSample();
        P.centerTab = 2;
        P.rigMode = m_opt.prefabRig;
        P.rigTab = P.rigMode == 0 || P.rigMode == 3 ? 0 : P.rigMode == 1 ? 1 : 2;
        if (m_opt.prefabTab >= 0) P.rigTab = m_opt.prefabTab;
        if (m_opt.prefabJoint >= 0 && m_opt.prefabJoint < (int)P.asset.joints.size()) P.selJoint = m_opt.prefabJoint;
        if (m_opt.prefabTime >= 0) P.time = m_opt.prefabTime;
    }
    if (m_opt.prefabDq) for (PrefabSkin& sk : P.asset.skins) sk.dualQuaternion = true;
    if (!m_opt.prefabExportGltf.empty() && !P.asset.skins.empty()) rigExportGltf(P.asset.findPart(P.asset.skins[0].part), m_opt.prefabExportGltf);
    for (size_t c = 0; c < P.asset.clips.size(); ++c)
        if (!m_opt.prefabClipName.empty() && fromUtf8(P.asset.clips[c].name) == m_opt.prefabClipName) P.clip = (int)c;
    if (!m_opt.prefabUv.empty()) {
        P.centerTab = 1;
        swscanf_s(m_opt.prefabUv.c_str(), L"%d,%d,%d,%d,%d", &P.uvSetA, &P.uvSetB, &P.uvCompare, &P.uvFill, &P.uvBackground);
    }
    if (!m_opt.prefabSave.empty() && writeFileSafe(m_opt.prefabSave, prefabToJson(P.asset))) {
        P.path = m_opt.prefabSave;
        P.savedJson = prefabToJson(P.asset);
    }
    if (m_opt.prefabPlace) {
        prefabAddToScene();
        m_workspace = 0;
    }
}

void App::prefabNew(bool demo) {
    PrefabStudio& P = m_pf;
    P.asset = demo ? makeDemoPrefab() : PrefabAsset();
    if (!demo) {
        PrefabClip idle;
        P.asset.clips.push_back(idle);
    }
    P.path.clear();
    P.savedJson = demo ? std::string() : prefabToJson(P.asset);
    P.sel = P.asset.parts.empty() ? -1 : 0;
    P.clip = 0;
    P.time = 0;
    P.playing = false;
    P.keyTrack = P.keyIndex = -1;
    P.undo.clear();
    P.selJoint = P.asset.joints.empty() ? -1 : 0;
    P.skinPart = -1;
    P.poseOverride.clear();
    P.redo.clear();
    P.rebuild = true;
    prefabFrameAll();
}

bool App::prefabDirty() const { return prefabToJson(m_pf.asset) != m_pf.savedJson; }

bool App::prefabSave(bool saveAs) {
    PrefabStudio& P = m_pf;
    std::wstring path = P.path;
    if (path.empty() || saveAs) {
        std::wstring dir = projectRoot() + L"\\game\\prefabs";
        CreateDirectoryW(dir.c_str(), nullptr);
        std::wstring name = fromUtf8(P.asset.name) + kPrefabExt;
        path = pickFileDialog(m_hwnd, true, L"WZX prefab (*.prefab)\0*.prefab\0", L"prefab", P.path.empty() ? dir : fs::path(P.path).parent_path().wstring(), name);
        if (path.empty()) return false;
    }
    std::string text = prefabToJson(P.asset);
    if (!writeFileSafe(path, text)) {
        status(L"Prefab not saved: " + path, 2);
        return false;
    }
    P.path = path;
    P.savedJson = text;
    status(L"Prefab saved: " + fs::path(path).filename().wstring(), 1);
    log("Prefab saved: " + toUtf8(path), 1);
    return true;
}

bool App::prefabOpen(const std::wstring& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { status(L"Prefab not found: " + path, 2); return false; }
    std::stringstream ss;
    ss << f.rdbuf();
    Json j;
    PrefabAsset a;
    std::string err;
    if (!Json::parse(ss.str(), j) || !prefabFromJson(j, a, &err)) {
        status(L"Not a prefab file: " + fs::path(path).filename().wstring(), 2);
        return false;
    }
    PrefabStudio& P = m_pf;
    P.asset = a;
    P.path = path;
    P.savedJson = prefabToJson(a);
    P.sel = a.parts.empty() ? -1 : 0;
    P.clip = 0;
    P.time = 0;
    P.playing = false;
    P.keyTrack = P.keyIndex = -1;
    P.undo.clear();
    P.selJoint = P.asset.joints.empty() ? -1 : 0;
    P.skinPart = -1;
    P.poseOverride.clear();
    P.redo.clear();
    P.rebuild = true;
    prefabFrameAll();
    status(L"Prefab opened: " + fs::path(path).filename().wstring(), 1);
    return true;
}

void App::prefabOpenDialog() {
    std::wstring dir = projectRoot() + L"\\game\\prefabs";
    std::wstring p = pickFileDialog(m_hwnd, false, L"WZX prefab (*.prefab)\0*.prefab\0All files\0*.*\0", L"prefab", dir, L"");
    if (!p.empty()) prefabOpen(p);
}

void App::prefabPushUndo() {
    PrefabStudio& P = m_pf;
    P.undo.push_back(P.asset);
    if (P.undo.size() > 100) P.undo.erase(P.undo.begin());
    P.redo.clear();
}

void App::prefabUndo() {
    PrefabStudio& P = m_pf;
    if (P.undo.empty()) return;
    P.redo.push_back(P.asset);
    P.asset = P.undo.back();
    P.undo.pop_back();
    P.rebuild = true;
    if (P.sel >= (int)P.asset.parts.size()) P.sel = (int)P.asset.parts.size() - 1;
    P.keyTrack = P.keyIndex = -1;
}

void App::prefabRedo() {
    PrefabStudio& P = m_pf;
    if (P.redo.empty()) return;
    P.undo.push_back(P.asset);
    P.asset = P.redo.back();
    P.redo.pop_back();
    P.rebuild = true;
    if (P.sel >= (int)P.asset.parts.size()) P.sel = (int)P.asset.parts.size() - 1;
    P.keyTrack = P.keyIndex = -1;
}

PrefabClip* App::prefabClip() {
    PrefabStudio& P = m_pf;
    return P.clip >= 0 && P.clip < (int)P.asset.clips.size() ? &P.asset.clips[P.clip] : nullptr;
}

// --------------------------------------------------------------------------- editing

void App::prefabAddPart(int kind) {
    PrefabStudio& P = m_pf;
    PrefabPart p;
    if (kind == 100) {
        std::wstring path = pickFileDialog(m_hwnd, false, L"Meshes (*.obj;*.gltf;*.glb;*.fbx)\0*.obj;*.gltf;*.glb;*.fbx\0All files\0*.*\0", L"obj", projectRoot() + L"\\assets", L"");
        if (path.empty()) return;
        p.meshPath = relPath(path);
        p.name = toUtf8(fs::path(path).stem().wstring());
        MeshData m;
        std::string err;
        if (!prefabMeshFor(p, m, &err)) { status(L"Mesh not imported: " + fromUtf8(err), 2); return; }
        // a large file (centimetres) is scaled to about two cells
        float3 mn, mx;
        m.bounds(mn, mx);
        float ext = std::max(mx.x - mn.x, std::max(mx.y - mn.y, mx.z - mn.z));
        if (ext > 8.0f) p.scale = float3(2.0f / ext);
        // its base colour image: the .mtl map, or a *basecolor* image next to it
        ObjInfo info;
        MeshData tmp;
        if (!isMeshFile(path) || fs::path(path).extension() != L".obj") {
        } else if (loadObj(path, tmp, &info, nullptr)) {
            fs::path dir = fs::path(path).parent_path();
            if (!info.diffuseMap.empty() && fs::exists(dir / fromUtf8(info.diffuseMap))) p.texture = relPath((dir / fromUtf8(info.diffuseMap)).wstring());
            else {
                std::error_code ec;
                for (auto& e : fs::directory_iterator(dir, ec)) {
                    std::wstring n = e.path().filename().wstring();
                    for (auto& c : n) c = (wchar_t)towlower(c);
                    if (n.find(L"basecolor") != std::wstring::npos || n.find(L"albedo") != std::wstring::npos || n.find(L"diffuse") != std::wstring::npos) {
                        p.texture = relPath(e.path().wstring());
                        break;
                    }
                }
            }
        }
        if (!p.texture.empty()) p.material.baseColor = float3(1);
    } else if (kind == 0) {
        p.name = "Pivot";
    } else {
        p.prim.type = kind;
        if (kind == PRIM_TORUS) p.prim.radius = 0.35f;
        p.name = primitiveName(kind);
        p.material.baseColor = float3(0.42f, 0.42f, 0.42f);
        p.material.roughness = 0.6f;
    }
    p.material.name = p.name;
    prefabPushUndo();
    // child of the selected pivot, else beside the selection
    if (P.sel >= 0 && P.sel < (int)P.asset.parts.size()) {
        const PrefabPart& s = P.asset.parts[P.sel];
        p.parent = s.isPivot() ? s.id : s.parent;
    }
    std::string base = p.name;
    for (int n = 2; ; ++n) {
        bool used = false;
        for (const PrefabPart& q : P.asset.parts) used |= q.name == p.name;
        if (!used) break;
        p.name = base + " " + std::to_string(n);
    }
    P.asset.addPart(p);
    P.sel = (int)P.asset.parts.size() - 1;
    P.rebuild = true;
    status(fromUtf8("Part added: " + p.name), 1);
}

void App::prefabDuplicate() {
    PrefabStudio& P = m_pf;
    if (P.sel < 0 || P.sel >= (int)P.asset.parts.size()) return;
    prefabPushUndo();
    // the part and its children, with their tracks
    std::vector<int> src{P.sel};
    for (size_t k = 0; k < src.size(); ++k)
        for (int c : P.asset.childrenOf(P.asset.parts[src[k]].id)) src.push_back(c);
    std::map<uint32_t, uint32_t> remap;
    std::vector<PrefabPart> copies;
    for (int i : src) copies.push_back(P.asset.parts[i]);
    for (PrefabPart& c : copies) {
        uint32_t old = c.id;
        c.id = 0;
        uint32_t nid = P.asset.addPart(c);
        remap[old] = nid;
    }
    for (auto& kv : remap) {
        PrefabPart& n = P.asset.parts[P.asset.findPart(kv.second)];
        if (remap.count(n.parent)) n.parent = remap[n.parent];
        if (kv.first == P.asset.parts[P.sel].id) { n.name += " Copy"; n.position.x += 0.5f; }
    }
    for (PrefabClip& clip : P.asset.clips) {
        size_t count = clip.tracks.size();
        for (size_t t = 0; t < count; ++t)
            if (!clip.tracks[t].joint && remap.count(clip.tracks[t].part)) {
                PrefabTrack c = clip.tracks[t];
                c.part = remap[c.part];
                clip.tracks.push_back(c);
            }
    }
    uint32_t first = remap[P.asset.parts[P.sel].id];
    P.sel = P.asset.findPart(first);
    P.rebuild = true;
}

void App::prefabDelete() {
    PrefabStudio& P = m_pf;
    if (P.sel < 0 || P.sel >= (int)P.asset.parts.size()) return;
    prefabPushUndo();
    std::string name = P.asset.parts[P.sel].name;
    P.asset.removePart(P.asset.parts[P.sel].id);
    P.sel = std::min(P.sel, (int)P.asset.parts.size() - 1);
    P.keyTrack = P.keyIndex = -1;
    P.rebuild = true;
    status(fromUtf8("Part deleted: " + name));
}

void App::prefabSetTime(float t) {
    PrefabStudio& P = m_pf;
    PrefabClip* c = prefabClip();
    if (!c) { P.time = 0; return; }
    P.time = clampf(t, 0, c->length);
}

// keys the selected part at the current time (frame-snapped), with its posed value
void App::prefabKeySelected(int channel) {
    PrefabStudio& P = m_pf;
    PrefabClip* c = prefabClip();
    if (!c || P.sel < 0 || P.sel >= (int)P.asset.parts.size()) return;
    prefabPushUndo();
    const float t = std::round(P.time * c->fps) / c->fps;
    const PrefabPart& p = P.asset.parts[P.sel];
    std::vector<PartPose> pose;
    evaluatePose(P.asset, c, P.time, pose);
    for (int ch = 0; ch < CH_COUNT; ++ch) {
        if (channel >= 0 && ch != channel) continue;
        const float3 v = ch == CH_POSITION ? pose[P.sel].position : ch == CH_ROTATION ? pose[P.sel].rotation : pose[P.sel].scale;
        c->track(p.id, ch, true)->setKey(t, v, INTERP_SMOOTH, 0.5f / c->fps);
    }
    status(fromUtf8("Key set: " + p.name), 1);
}

void App::prefabStep(float dt) {
    PrefabStudio& P = m_pf;
    PrefabClip* c = prefabClip();
    if (!c) { P.playing = false; return; }
    if (!P.playing) return;
    P.time += dt * P.speed;
    if (P.time >= c->length) {
        if (c->loop) P.time = std::fmod(P.time, c->length);
        else { P.time = c->length; P.playing = false; }
    }
    if (P.time < 0) P.time = c->loop ? c->length + std::fmod(P.time, c->length) : 0.0f;
}

// --------------------------------------------------------------------------- studio scene

bool App::prefabMeshFor(const PrefabPart& p, MeshData& out, std::string* err) {
    PrefabStudio& P = m_pf;
    if (p.prim.type != PRIM_NONE && p.meshPath.empty()) { out = makePrimitive(p.prim); return true; }
    if (p.meshPath.empty()) return false;
    const std::string key = p.meshPath + (p.recenter ? "|c" : "");
    auto it = P.meshCache.find(key);
    if (it != P.meshCache.end()) { out = it->second; return true; }
    auto e = P.meshErrors.find(p.meshPath);
    if (e != P.meshErrors.end()) { if (err) *err = e->second; return false; }
    MeshData m;
    std::string error;
    std::vector<UvSet> sets;
    if (!loadMeshFile(absPath(p.meshPath), m, &sets, &error)) {
        P.meshErrors[p.meshPath] = error;
        if (err) *err = error;
        log("Prefab mesh not loaded: " + p.meshPath + " (" + error + ")", 2);
        return false;
    }
    float3 shift(0);
    if (p.recenter) {   // pivot at the centre of the base, as the RTS house
        float3 mn, mx;
        m.bounds(mn, mx);
        shift = float3((mn.x + mx.x) * 0.5f, mn.y, (mn.z + mx.z) * 0.5f);
        for (Vertex& v : m.vertices) v.pos -= shift;
    }
    P.meshShift[key] = shift;
    P.meshCache[key] = m;
    P.uvCache[key] = std::move(sets);
    out = std::move(m);
    return true;
}

void App::prefabBuildScene() {
    PrefabStudio& P = m_pf;
    Scene& s = P.scene;
    s = Scene();
    s.name = "Prefab Studio";
    s.unitsPerMeter = 100.0f;
    s.gridScale = 1.0f;
    // floor: the RTS grid (1 unit = 1 cell)
    {
        Material g;
        g.name = "Studio Floor";
        g.baseColor = float3(0.05f, 0.052f, 0.058f);
        g.roughness = 0.75f;
        g.flags = MAT_FLAG_GRID;
        Object o;
        o.name = "Floor";
        o.part = "GROUND";
        o.mesh = s.addMesh("studioFloor", makePlane(80.0f));
        o.material = s.addMaterial(g);
        o.selectable = false;
        P.floorObject = s.addObject(o);
    }
    std::map<std::string, int> textures;
    P.partObject.assign(P.asset.parts.size(), -1);
    P.partUv.assign(P.asset.parts.size(), {});
    P.partRest.assign(P.asset.parts.size(), MeshData());
    P.skinSig.clear();
    P.uvAnalysis.clear();
    for (size_t i = 0; i < P.asset.parts.size(); ++i) {
        const PrefabPart& p = P.asset.parts[i];
        if (p.isPivot()) continue;
        MeshData m;
        std::string err;
        bool ok = prefabMeshFor(p, m, &err);
        if (!ok) m = makeBox({-0.5f, 0, -0.5f}, {0.5f, 1, 0.5f});   // missing file: a placeholder box (red)
        // UV sets: those of the file (glTF can have several), else the one of the vertices
        auto uvs = ok && !p.meshPath.empty() ? P.uvCache.find(p.meshPath + (p.recenter ? "|c" : "")) : P.uvCache.end();
        if (uvs != P.uvCache.end() && !uvs->second.empty() && uvs->second[0].uv.size() == m.vertices.size()) P.partUv[i] = uvs->second;
        else {
            UvSet s0;
            s0.name = "UV0";
            for (const Vertex& v : m.vertices) s0.uv.push_back(float2(v.uv.x, 1.0f - v.uv.y));   // UV-editor convention (v up)
            P.partUv[i].push_back(std::move(s0));
        }
        Object o;
        o.name = p.name;
        o.part = "PREFAB_PART";
        o.mesh = s.addMesh("part" + std::to_string(p.id), std::move(m));
        Material mat = p.material;
        mat.baseColorTex = -1;
        if (!ok) { mat = Material(); mat.baseColor = float3(0.8f, 0.05f, 0.05f); }
        if (ok && !p.texture.empty()) {
            auto t = textures.find(p.texture);
            if (t != textures.end()) mat.baseColorTex = t->second;
            else if ((int)s.textures.size() < (int)MAX_TEXTURES) {
                auto c = P.texCache.find(p.texture);
                Texture tex;
                std::string terr;
                if (c != P.texCache.end()) tex = c->second;
                else if (loadImage(absPath(p.texture), tex, &terr, 2048)) P.texCache[p.texture] = tex;
                else { log("Prefab texture not loaded: " + p.texture, 2); tex = Texture(); }
                if (tex.width) mat.baseColorTex = textures[p.texture] = s.addTexture(std::move(tex));
            }
        }
        o.material = s.addMaterial(mat);
        o.useRot = true;
        P.partObject[i] = s.addObject(o);
        // skinned part: keep the rest mesh (deformed every frame), weights checked against its vertex count
        if (PrefabSkin* sk = P.asset.skinOf(p.id)) {
            const MeshData& rest = s.meshes[s.objects[P.partObject[i]].mesh];
            P.partRest[i] = rest;
            P.weld[p.id].build(rest);
            if (sk->vertexCount != rest.vertices.size()) {   // other primitive parameters or file: bind again
                std::vector<uint32_t> joints = sk->joints;
                Affine bm = sk->bindMesh;
                bindSkin(*sk, P.asset, rest, bm, joints, P.bindOpt, P.weld[p.id]);
                log("Skin of " + p.name + " bound again (the mesh changed)", 2);
            }
        }
    }
    P.meshMin.resize(s.meshes.size());
    P.meshMax.resize(s.meshes.size());
    for (size_t i = 0; i < s.meshes.size(); ++i) s.meshes[i].bounds(P.meshMin[i], P.meshMax[i]);
    P.rebuild = false;
    ++P.buildSerial;
    m_renderer.setScene(s);
}

// lights of the studio world (presets), scaled to the size of the asset
void App::prefabLighting() {
    PrefabStudio& P = m_pf;
    Scene& s = P.scene;
    float3 mn(1e30f), mx(-1e30f);
    for (size_t i = 0; i < P.world.size() && i < P.partObject.size(); ++i) {
        int o = P.partObject[i];
        if (o < 0) continue;
        mn = minv(mn, P.world[i].position);
        mx = maxv(mx, P.world[i].position);
    }
    if (mn.x > mx.x) { mn = float3(-1, 0, -1); mx = float3(1, 2, 1); }
    const float R = std::max(1.5f, length(mx - mn) * 0.5f + 1.0f);
    const float3 c((mn.x + mx.x) * 0.5f, std::max(0.6f, (mn.y + mx.y) * 0.5f), (mn.z + mx.z) * 0.5f);
    s.lights.clear();
    s.sun.enabled = false;
    s.env.hdri.reset();
    Environment& e = s.env;
    e.visibleToCamera = true;
    const float k = P.envIntensity;
    auto area = [&](const char* name, float az, float el, float dist, float size, float3 color, float irradiance) {
        AreaLight l;
        l.name = name;
        float a = radians(az) + P.camera.yaw * 0.0f;
        l.position = c + float3(std::sin(a) * std::cos(radians(el)), std::sin(radians(el)), std::cos(a) * std::cos(radians(el))) * dist;
        l.target = c;
        l.width = l.height = size;
        l.color = color;
        l.intensity = irradiance * dist * dist / (size * size);
        l.visibleToCamera = false;
        s.lights.push_back(l);
    };
    const float dist = 2.4f * R + 3.0f, size = 0.9f * R + 1.0f;
    switch (P.lightPreset) {
    case 1:   // daylight: sun and a blue sky
        e.top = lin(0.42f, 0.58f, 0.85f) * 0.6f; e.horizon = lin(0.75f, 0.82f, 0.9f) * 0.6f; e.bottom = lin(0.25f, 0.24f, 0.22f) * 0.3f;
        e.intensity = 1.0f * k; e.ambient = lin(0.35f, 0.4f, 0.48f) * 0.35f * k;
        s.sun.enabled = true; s.sun.color = float3(1.0f, 0.95f, 0.86f); s.sun.intensity = 3.6f * P.keyIntensity;
        break;
    case 2:   // sunset: low warm sun
        e.top = lin(0.3f, 0.33f, 0.5f) * 0.5f; e.horizon = lin(0.95f, 0.55f, 0.32f) * 0.6f; e.bottom = lin(0.15f, 0.1f, 0.08f) * 0.3f;
        e.intensity = 1.0f * k; e.ambient = lin(0.45f, 0.32f, 0.3f) * 0.25f * k;
        s.sun.enabled = true; s.sun.color = float3(1.0f, 0.58f, 0.3f); s.sun.intensity = 3.0f * P.keyIntensity;
        break;
    case 3:   // night: moonlight, the emissive parts carry the scene
        e.top = lin(0.05f, 0.07f, 0.14f); e.horizon = lin(0.08f, 0.1f, 0.16f); e.bottom = lin(0.02f, 0.02f, 0.03f);
        e.intensity = 1.0f * k; e.ambient = lin(0.3f, 0.36f, 0.55f) * 0.3f * k;
        s.sun.enabled = true; s.sun.color = float3(0.55f, 0.65f, 1.0f); s.sun.intensity = 1.1f * P.keyIntensity;
        break;
    case 4: {   // HDRI: the image of the scene settings (lighting, reflections, background)
        std::wstring path = P.hdri.empty() ? defaultHdri() : P.hdri;
        EnvImagePtr img;
        auto it = m_hdriCache.find(path);
        if (it != m_hdriCache.end()) img = it->second;
        else {
            auto ei = std::make_shared<EnvImage>();
            std::string err;
            bool ok = path == L"sky" ? (makeProceduralSky(*ei), true) : loadEnvImage(path, *ei, &err);
            if (ok) { img = ei; m_hdriCache[path] = img; }
        }
        P.hdri = path;
        e.hdri = img;
        e.hdriIntensity = k;
        e.hdriRotation = radians(P.sunAzimuth);
        e.ambient = float3(0.02f);
        break;
    }
    default:   // studio: key / fill / rim area lights on a neutral gradient
        e.top = lin(0.3f, 0.32f, 0.36f) * 0.55f; e.horizon = lin(0.2f, 0.21f, 0.23f) * 0.55f; e.bottom = lin(0.05f, 0.05f, 0.055f) * 0.5f;
        e.intensity = 0.8f * k; e.ambient = float3(0.06f, 0.062f, 0.068f) * k;
        area("Key", P.sunAzimuth - 75.0f, P.sunElevation, dist, size, float3(1.0f, 0.95f, 0.88f), 2.2f * P.keyIntensity);
        area("Fill", P.sunAzimuth + 30.0f, 18.0f, dist, size * 1.3f, float3(0.8f, 0.88f, 1.0f), 0.7f * P.fillIntensity);
        area("Rim", P.sunAzimuth + 150.0f, 35.0f, dist, size * 0.8f, float3(0.9f, 0.95f, 1.0f), 1.4f * P.rimIntensity);
        break;
    }
    if (s.sun.enabled) {
        float az = radians(P.sunAzimuth), el = radians(P.lightPreset == 2 ? std::min(P.sunElevation, 14.0f) : P.sunElevation);
        s.sun.direction = normalize(float3(std::sin(az) * std::cos(el), std::sin(el), std::cos(az) * std::cos(el)));
        if (P.lightPreset == 1) area("Sky Fill", P.sunAzimuth + 180.0f, 55.0f, dist, size * 1.5f, lin(0.6f, 0.7f, 0.9f), 0.35f * P.fillIntensity);
    }
}

void App::prefabSyncScene() {
    PrefabStudio& P = m_pf;
    Scene& s = P.scene;
    const PrefabClip* clip = prefabClip();
    evaluatePose(P.asset, clip, P.time, P.pose);
    worldTransforms(P.asset, P.pose, P.world);
    // joints: rest, clip, then the Pose mode overrides
    restJointWorld(P.asset, P.jointRest);
    evaluateJoints(P.asset, clip, P.time, P.jointPose);
    for (size_t j = 0; j < P.asset.joints.size(); ++j) {
        auto ov = P.poseOverride.find(P.asset.joints[j].id);
        if (ov != P.poseOverride.end()) P.jointPose[j] = ov->second;
    }
    jointWorld(P.asset, P.jointPose, P.jointNow);
    if (P.centerTab == 2 && P.rigMode == 0) P.jointNow = P.jointRest;   // Skeleton mode: the rig and the skin at the bind pose
    for (size_t i = 0; i < P.asset.parts.size() && i < P.partObject.size(); ++i) {
        int oi = P.partObject[i];
        if (oi < 0 || oi >= (int)s.objects.size()) continue;
        const PrefabPart& p = P.asset.parts[i];
        Object& o = s.objects[oi];
        const PartWorld& w = P.world[i];
        o.position = w.position;
        o.rot = w.rot;
        o.useRot = true;
        // a zero scale (parts growing in a Build clip) is kept just above zero
        o.scale = float3(std::fabs(w.scale.x) < 1e-4f ? 1e-4f : w.scale.x, std::fabs(w.scale.y) < 1e-4f ? 1e-4f : w.scale.y,
                         std::fabs(w.scale.z) < 1e-4f ? 1e-4f : w.scale.z);
        bool visible = p.visible;
        for (uint32_t pid = p.parent; pid && visible; ) {   // a hidden parent hides its children
            int pi = P.asset.findPart(pid);
            if (pi < 0) break;
            visible = P.asset.parts[pi].visible;
            pid = P.asset.parts[pi].parent;
        }
        o.visible = visible;
        // skinned part: the deformed mesh is in world space, the object stays at the origin
        if (const PrefabSkin* sk = P.asset.skinOf(p.id)) {
            if (i < P.partRest.size() && P.partRest[i].vertices.size() == sk->vertexCount && sk->vertexCount) {
                o.position = float3(0);
                o.rot = qIdentity();
                o.scale = float3(1);
                uint64_t sig = 1469598103934665603ull ^ P.weightsVersion;
                auto mix = [&](float f) { uint32_t u; memcpy(&u, &f, 4); sig = (sig ^ u) * 1099511628211ull; };
                for (const PartWorld& jw : P.jointNow) { mix(jw.position.x); mix(jw.position.y); mix(jw.position.z); mix(jw.rot.x); mix(jw.rot.y); mix(jw.rot.z); mix(jw.rot.w); mix(jw.scale.x); mix(jw.scale.y); mix(jw.scale.z); }
                auto it = P.skinSig.find(p.id);
                if (it == P.skinSig.end() || it->second != sig) {
                    P.skinSig[p.id] = sig;
                    MeshData& dst = s.meshes[o.mesh];
                    MeshData out;
                    deformSkin(*sk, P.asset, P.partRest[i], P.jointNow, out);
                    dst.vertices = std::move(out.vertices);
                    if (P.active) m_renderer.updateMeshVertices(o.mesh, dst.vertices);
                }
            }
        }
        Material& m = s.materials[o.material];
        if (!p.meshPath.empty() && P.meshErrors.count(p.meshPath)) continue;   // missing file: the red placeholder stays
        int tex = m.baseColorTex;
        m = p.material;
        m.baseColorTex = p.texture.empty() ? -1 : tex;
        m.emission = p.material.emission * p.material.emissionStrength;
    }
    if (P.floorObject >= 0) {
        Object& f = s.objects[P.floorObject];
        f.visible = P.floorStyle != 2;
        s.materials[f.material].flags = P.floorStyle == 0 && P.showGrid ? MAT_FLAG_GRID : 0u;
    }
    prefabLighting();
    if (P.turntable && !P.orbiting) P.camera.yaw += m_dt * 0.35f;
    s.camera = P.camera;
    s.camera.dof = false;
    s.camera.sensorScale = s.unitsPerMeter;
}

void App::renderPrefab() {
    PrefabStudio& P = m_pf;
    prefabInit();
    prefabStep(m_dt);
    if (!P.active || P.rebuild) {
        prefabBuildScene();
        P.active = true;
        m_previewActive = false;   // the renderer no longer holds the material preview
    }
    prefabSyncScene();
    RenderSettings rs = m_rs;
    rs.raster = true;
    rs.ddgi = false;
    rs.bakedProbes = false;
    rs.wireframe = P.wireframe;
    rs.debugView = P.debugView;
    rs.vignette = 0.0f;
    rs.exposure = std::pow(2.0f, P.exposure);
    if (P.debugView != 0 && P.debugView != 4) { rs.tonemapper = 3; rs.exposure = 1.0f; }
    int selObj = P.showSelection && P.sel >= 0 && P.sel < (int)P.partObject.size() ? P.partObject[P.sel] : -1;
    // a selected pivot outlines its children
    std::vector<int> extra;
    if (P.showSelection && P.sel >= 0 && P.sel < (int)P.asset.parts.size())
        for (size_t i = 0; i < P.asset.parts.size(); ++i)
            if ((int)i != P.sel && P.asset.isAncestor(P.asset.parts[P.sel].id, P.asset.parts[i].id) && P.partObject[i] >= 0) extra.push_back(P.partObject[i]);
    m_renderer.setExtraSelection(extra);
    m_renderer.renderToTexture(P.scene, rs, P.vpPx, selObj);
    m_gui.viewportTexture();
}

void App::prefabFrameAll() {
    PrefabStudio& P = m_pf;
    std::vector<PartPose> pose;
    std::vector<PartWorld> world;
    evaluatePose(P.asset, nullptr, 0, pose);
    worldTransforms(P.asset, pose, world);
    float3 mn(1e30f), mx(-1e30f);
    for (size_t i = 0; i < P.asset.parts.size(); ++i) {
        const PrefabPart& p = P.asset.parts[i];
        if (p.isPivot()) continue;
        MeshData m;
        if (!prefabMeshFor(p, m, nullptr)) continue;
        float3 a, b;
        m.bounds(a, b);
        for (int k = 0; k < 8; ++k) {
            float3 q((k & 1) ? b.x : a.x, (k & 2) ? b.y : a.y, (k & 4) ? b.z : a.z);
            float3 w = world[i].position + qRot(world[i].rot, q * world[i].scale);
            mn = minv(mn, w);
            mx = maxv(mx, w);
        }
    }
    if (mn.x > mx.x) { mn = float3(-1, 0, -1); mx = float3(1, 2, 1); }
    float r = std::max(0.5f, length(mx - mn) * 0.5f);
    P.camera.target = (mn + mx) * 0.5f;
    P.camera.distance = r / std::tan(P.camera.fovY * 0.5f) * 1.15f;
}

// --------------------------------------------------------------------------- UV Editor data

const UvAnalysis& App::prefabUvAnalysis(int part, int set) {
    static const UvAnalysis none;
    PrefabStudio& P = m_pf;
    if (part < 0 || part >= (int)P.partUv.size() || set < 0 || set >= (int)P.partUv[part].size()) return none;
    int oi = part < (int)P.partObject.size() ? P.partObject[part] : -1;
    if (oi < 0 || oi >= (int)P.scene.objects.size()) return none;
    const uint64_t key = ((uint64_t)P.asset.parts[part].id << 40) | ((uint64_t)set << 32) | P.buildSerial;
    auto it = P.uvAnalysis.find(key);
    if (it != P.uvAnalysis.end()) return it->second;
    float3 scale(1);
    if (part < (int)P.world.size()) {
        const float3& w = P.world[part].scale;
        scale = float3(std::max(1e-4f, std::fabs(w.x)), std::max(1e-4f, std::fabs(w.y)), std::max(1e-4f, std::fabs(w.z)));
    }
    UvAnalysis& a = P.uvAnalysis[key];
    analyzeUv(P.scene.meshes[P.scene.objects[oi].mesh], P.partUv[part][set].uv, scale, a);
    return a;
}

ImTextureID App::prefabUvTexture(const std::string& path) {
    PrefabStudio& P = m_pf;
    auto it = P.uvTextures.find(path);
    if (it != P.uvTextures.end()) return it->second;
    ImTextureID id = 0;
    auto t = P.texCache.find(path);
    if (t != P.texCache.end() && t->second.width) {
        // at most 1024 px: a background only
        const Texture& tx = t->second;
        uint32_t step = std::max(1u, std::max(tx.width, tx.height) / 1024);
        uint32_t w = tx.width / step, h = tx.height / step;
        std::vector<uint8_t> px((size_t)w * h * 4);
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x) memcpy(&px[((size_t)y * w + x) * 4], &tx.rgba[((size_t)y * step * tx.width + x * step) * 4], 4);
        id = m_gui.createTexture(px.data(), w, h);
    }
    P.uvTextures[path] = id;
    return id;
}

// --------------------------------------------------------------------------- viewport input

void App::prefabViewInput(bool hovered) {
    PrefabStudio& P = m_pf;
    ImGuiIO& io = ImGui::GetIO();
    Camera& cam = P.camera;
    float mx = io.MousePos.x, my = io.MousePos.y;
    float dx = mx - P.lastX, dy = my - P.lastY;
    P.lastX = mx;
    P.lastY = my;
    auto ray = [&](float px, float py, float3& o, float3& d) {
        float3 r, u, f;
        cam.basis(r, u, f);
        float uu = (px - P.rcView.x) / P.rcView.w, vv = (py - P.rcView.y) / P.rcView.h;
        float t = std::tan(cam.fovY * 0.5f), a = P.rcView.w / P.rcView.h;
        o = cam.eye();
        d = normalize(f + r * ((2 * uu - 1) * a * t) + u * ((1 - 2 * vv) * t));
    };
    if (hovered && io.MouseWheel != 0) cam.distance = clampf(cam.distance * std::pow(0.88f, io.MouseWheel), 0.3f, 400.0f);
    if (hovered && (ImGui::IsMouseClicked(1) || (io.KeyAlt && ImGui::IsMouseClicked(0)))) {
        if (io.KeyShift) P.panning = true;
        else P.orbiting = true;
    }
    if (hovered && ImGui::IsMouseClicked(2)) P.panning = true;
    if (P.orbiting) {
        if (!(io.MouseDown[1] || io.MouseDown[0])) P.orbiting = false;
        else {
            cam.yaw -= dx * 0.0065f / m_dpi;
            cam.pitch = clampf(cam.pitch + dy * 0.0065f / m_dpi, -1.5f, 1.5f);
        }
    }
    if (P.panning) {
        if (!(io.MouseDown[2] || io.MouseDown[1])) P.panning = false;
        else {
            float3 r, u, f;
            cam.basis(r, u, f);
            float k = cam.distance * std::tan(cam.fovY * 0.5f) * 2.0f / P.rcView.h;
            cam.target = cam.target - r * (dx * k) + u * (dy * k);
        }
    }

    if (P.centerTab == 2) return;   // Rig module: the left button is handled by rigViewInput (joints, gizmo, brush)
    // move gizmo: drag along a world axis of the selected part
    const bool selValid = P.sel >= 0 && P.sel < (int)P.world.size();
    if (P.gizmoAxis >= 0) {
        if (!io.MouseDown[0]) {
            P.gizmoAxis = -1;
            P.editing = false;
        } else if (selValid) {
            float3 o, d;
            ray(mx, my, o, d);
            float3 w = P.gizmoOrigin - o;
            float b = dot(P.gizmoDir, d), dd = dot(P.gizmoDir, w), e = dot(d, w), den = 1 - b * b;
            float t = std::fabs(den) < 1e-6f ? 0.0f : (b * e - dd) / den;
            float delta = t - P.gizmoT0;
            if (io.KeyCtrl) delta = std::round(delta * 4.0f) / 4.0f;   // quarter cell steps
            PrefabPart& p = P.asset.parts[P.sel];
            // world delta -> local delta of the part (parent rotation and scale)
            float3 wd = P.gizmoDir * delta;
            float4 prot = qIdentity();
            float3 pscale(1);
            int pi = p.parent ? P.asset.findPart(p.parent) : -1;
            if (pi >= 0 && pi < (int)P.world.size()) { prot = P.world[pi].rot; pscale = P.world[pi].scale; }
            float4 inv(-prot.x, -prot.y, -prot.z, prot.w);
            float3 ld = qRot(inv, wd);
            ld = float3(ld.x / std::max(1e-4f, pscale.x), ld.y / std::max(1e-4f, pscale.y), ld.z / std::max(1e-4f, pscale.z));
            float3 np = P.gizmoStart + ld;
            PrefabClip* c = prefabClip();
            PrefabTrack* tr = c ? c->track(p.id, CH_POSITION, P.autoKey) : nullptr;
            if (tr) tr->setKey(std::round(P.time * c->fps) / c->fps, np, INTERP_SMOOTH, 0.5f / c->fps);
            else p.position = np;
        }
        return;
    }
    if (hovered && ImGui::IsMouseClicked(0) && !io.KeyAlt) {
        if (P.gizmoHover >= 0 && selValid) {
            prefabPushUndo();
            P.editing = true;
            P.gizmoAxis = P.gizmoHover;
            P.gizmoOrigin = P.world[P.sel].position;
            P.gizmoDir = float3(P.gizmoAxis == 0 ? 1.0f : 0.0f, P.gizmoAxis == 1 ? 1.0f : 0.0f, P.gizmoAxis == 2 ? 1.0f : 0.0f);
            P.gizmoStart = P.pose[P.sel].position;
            float3 o, d;
            ray(mx, my, o, d);
            float3 w = P.gizmoOrigin - o;
            float b = dot(P.gizmoDir, d), dd = dot(P.gizmoDir, w), e = dot(d, w), den = 1 - b * b;
            P.gizmoT0 = std::fabs(den) < 1e-6f ? 0.0f : (b * e - dd) / den;
            return;
        }
        float3 o, d;
        ray(mx, my, o, d);
        PickResult pr = pick(P.scene, o, d);
        P.sel = -1;
        for (size_t i = 0; i < P.partObject.size(); ++i)
            if (pr.object >= 0 && P.partObject[i] == pr.object) P.sel = (int)i;
    }
}

void App::prefabShortcuts() {
    PrefabStudio& P = m_pf;
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;
    const bool ctrl = io.KeyCtrl;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) prefabSave(io.KeyShift);
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) prefabOpenDialog();
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z)) prefabUndo();
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y)) prefabRedo();
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) prefabDuplicate();
    if (ctrl) return;
    PrefabClip* c = prefabClip();
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
        if (c && !P.playing && !c->loop && P.time >= c->length - 1e-4f) P.time = 0;
        P.playing = !P.playing;
    }
    if (P.centerTab == 1) {   // UV Editor: F frames the UVs, Home the 0-1 square
        if (ImGui::IsKeyPressed(ImGuiKey_F, false)) P.uvFrame = 2;
        if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) P.uvFrame = 1;
    } else if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) prefabSetTime(0);
    if (ImGui::IsKeyPressed(ImGuiKey_End, false) && c) prefabSetTime(c->length);
    if (c && ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) prefabSetTime(std::round(P.time * c->fps - 1) / c->fps);
    if (c && ImGui::IsKeyPressed(ImGuiKey_RightArrow)) prefabSetTime(std::round(P.time * c->fps + 1) / c->fps);
    if (P.centerTab == 2) {   // Rig module: tools, modes, joint keys and deletion
        if (ImGui::IsKeyPressed(ImGuiKey_W, false)) P.rigTool = 1;
        if (ImGui::IsKeyPressed(ImGuiKey_E, false)) P.rigTool = 2;
        if (ImGui::IsKeyPressed(ImGuiKey_K, false)) prefabKeyJoint();
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) rigDeleteJoint();
        for (int k = 0; k < 4; ++k)
            if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_1 + k), false)) { P.rigMode = k; P.rigTab = k == 0 || k == 3 ? 0 : k == 1 ? 1 : 2; }
        if (ImGui::IsKeyPressed(ImGuiKey_F, false)) prefabFrameAll();
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_K, false)) prefabKeySelected();
    if (P.centerTab == 0 && ImGui::IsKeyPressed(ImGuiKey_F, false)) prefabFrameAll();
    if (ImGui::IsKeyPressed(ImGuiKey_F2, false) && P.sel >= 0 && P.sel < (int)P.asset.parts.size()) {
        P.renameId = P.asset.parts[P.sel].id;
        snprintf(P.renameBuf, sizeof(P.renameBuf), "%s", P.asset.parts[P.sel].name.c_str());
        P.renameFocus = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        if (c && P.keyTrack >= 0 && P.keyTrack < (int)c->tracks.size() && P.keyIndex >= 0 && P.keyIndex < (int)c->tracks[P.keyTrack].keys.size()) {
            prefabPushUndo();
            PrefabTrack& t = c->tracks[P.keyTrack];
            t.keys.erase(t.keys.begin() + P.keyIndex);
            if (t.keys.empty()) c->tracks.erase(c->tracks.begin() + P.keyTrack);
            P.keyTrack = P.keyIndex = -1;
        } else prefabDelete();
    }
    for (int k = 0; k < 6; ++k)
        if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_1 + k), false)) P.debugView = k;
    if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) P.wireframe = (P.wireframe + 1) % 3;
}

// --------------------------------------------------------------------------- to the main scene

// The prefab (current pose) becomes a group of the main scene: one object per part, the mesh files and textures kept
// as paths so the .wzx stores them. The group pivot is at the camera target.
void App::prefabAddToScene() {
    PrefabStudio& P = m_pf;
    if (P.asset.parts.empty()) return;
    pushUndo();
    outlinerIds();
    std::vector<PartPose> pose;
    std::vector<PartWorld> world;
    evaluatePose(P.asset, nullptr, 0, pose);
    worldTransforms(P.asset, pose, world);
    float3 at(m_scene.camera.target.x, 0, m_scene.camera.target.z);
    Object g;
    g.name = uniqueNodeName(P.asset.name);
    g.part = "GROUP";
    g.group = true;
    g.mesh = -1;
    g.position = at;
    g.useRot = true;
    g.id = newNodeId();
    int gi = m_scene.addObject(g);
    std::map<std::string, int> textures;
    for (size_t i = 0; i < P.asset.parts.size(); ++i) {
        const PrefabPart& p = P.asset.parts[i];
        if (p.isPivot()) continue;
        Object o;
        o.name = p.name;
        o.useRot = true;
        o.rot = world[i].rot;
        o.scale = world[i].scale;
        o.position = at + world[i].position;
        Material m = p.material;
        m.emission = p.material.emission * p.material.emissionStrength;
        m.baseColorTex = -1;
        if (!p.meshPath.empty()) {
            MeshData mesh;
            std::string err;
            if (!loadMeshFile(absPath(p.meshPath), mesh, nullptr, &err)) continue;
            const std::string name = "file:" + p.meshPath;
            int mi = m_scene.findMesh(name);
            if (mi < 0) mi = m_scene.addMesh(name, std::move(mesh));
            o.mesh = mi;
            o.part = "MESH";
            auto sh = P.meshShift.find(p.meshPath + (p.recenter ? "|c" : ""));
            if (sh != P.meshShift.end()) o.position = o.position - qRot(o.rot, sh->second * o.scale);
        } else {
            o.prim = p.prim;
            o.part = std::string("PRIM_") + primitiveName(p.prim.type);
            o.mesh = -1;
        }
        if (!p.texture.empty()) {
            auto t = textures.find(p.texture);
            if (t != textures.end()) m.baseColorTex = t->second;
            else {
                Texture tex;
                if (loadImage(absPath(p.texture), tex, nullptr, 2048)) {
                    tex.name = p.texture;
                    m.baseColorTex = textures[p.texture] = m_scene.addTexture(std::move(tex));
                }
            }
        }
        o.material = m_scene.addMaterial(m);
        o.parent = m_scene.objects[gi].id;
        o.id = newNodeId();
        int idx = m_scene.addObject(o);
        if (o.prim.type != PRIM_NONE) syncPrimitiveMesh(m_scene, idx);
    }
    m_renderer.setScene(m_scene);
    P.active = false;   // the renderer holds the main scene again: the studio scene is uploaded at its next frame
    select(gi);
    markDirty();
    status(fromUtf8("Added to the scene: " + P.asset.name), 1);
    log("Prefab placed in the scene: " + P.asset.name, 1);
}

} // namespace app
