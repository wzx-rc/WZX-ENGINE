// Scene file format (.v3d.json).
#include "App.h"
#include "GameHost.h"
#include "Json.h"
#include <commdlg.h>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace app {

static float linToSrgb(float c) {
    c = clampf(c, 0, 1);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

bool App::saveSceneFile(const std::wstring& path) {
    std::ostringstream js;
    js.setf(std::ios::fixed);
    js.precision(4);
    const Camera& c = m_scene.camera;
    js << "{\n  \"format\": \"vp3d-scene\",\n  \"version\": 1,\n  \"units\": \"cm\",\n";
    js << "  \"camera\": {\"target\": [" << c.target.x << ", " << c.target.y << ", " << c.target.z << "], \"yaw\": "
       << degrees(c.yaw) << ", \"pitch\": " << degrees(c.pitch) << ", \"distance\": " << c.distance
       << ", \"fov\": " << degrees(c.fovY) << "},\n";
    if (!m_hdriPath.empty())
        js << "  \"environment\": {\"hdri\": \"" << jsonEscape(toUtf8(m_hdriPath)) << "\", \"intensity\": " << m_scene.env.hdriIntensity
           << ", \"rotation\": " << degrees(m_scene.env.hdriRotation) << ", \"visible\": " << (m_scene.env.visibleToCamera ? "true" : "false") << "},\n";
    // Scene Manager: collections, outliner fields of the sun and the lights (by index), then per object
    outlinerIds();
    auto b01 = [](bool v) { return v ? "true" : "false"; };
    js << "  \"collections\": [";
    for (size_t i = 0; i < m_scene.collections.size(); ++i) {
        const Collection& c = m_scene.collections[i];
        js << (i ? ",\n" : "\n") << "    {\"id\": " << c.id << ", \"name\": \"" << jsonEscape(c.name) << "\", \"color\": " << c.color
           << ", \"collapsed\": " << b01(c.collapsed) << ", \"order\": " << c.order << "}";
    }
    js << "\n  ],\n";
    const SunLight& sun = m_scene.sun;
    js << "  \"sun\": {\"id\": " << sun.id << ", \"collection\": " << sun.collection << ", \"order\": " << sun.order
       << ", \"selectable\": " << b01(sun.selectable) << "},\n";
    js << "  \"lights\": [";
    for (size_t i = 0; i < m_scene.lights.size(); ++i) {
        const AreaLight& l = m_scene.lights[i];
        js << (i ? ",\n" : "\n") << "    {\"id\": " << l.id << ", \"parent\": " << l.parent << ", \"collection\": " << l.collection
           << ", \"order\": " << l.order << ", \"selectable\": " << b01(l.selectable) << ", \"enabled\": " << b01(l.enabled)
           << ", \"name\": \"" << jsonEscape(l.name) << "\", \"pos\": [" << l.position.x << ", " << l.position.y << ", " << l.position.z
           << "], \"target\": [" << l.target.x << ", " << l.target.y << ", " << l.target.z << "]}";
    }
    js << "\n  ],\n";
    js << "  \"objects\": [";
    bool first = true;
    int count = 0;
    for (const Object& o : m_scene.objects) {
        if (o.ghost || (!o.group && !findPart(o.part))) continue;
        float4 q = objRot(o);
        float yawDeg = degrees(std::atan2(-qRot(q, float3(1, 0, 0)).z, qRot(q, float3(1, 0, 0)).x));
        js << (first ? "\n" : ",\n") << "    {\"part\": \"" << jsonEscape(o.part) << "\"";
        if (o.group) js << ", \"group\": true";
        else {
            const Material& m = m_scene.materials[o.material];
            js << ", \"color\": " << m.colorId << ", \"rgb\": [" << linToSrgb(m.baseColor.x) << ", " << linToSrgb(m.baseColor.y) << ", "
               << linToSrgb(m.baseColor.z) << "]" << ", \"material\": " << m.preset;
        }
        js << ", \"pos\": [" << o.position.x << ", " << o.position.y << ", " << o.position.z
           << "], \"yaw\": " << yawDeg << ", \"rot\": [" << q.x << ", " << q.y << ", " << q.z << ", " << q.w << "], \"scale\": ["
           << o.scale.x << ", " << o.scale.y << ", " << o.scale.z << "], \"name\": \"" << jsonEscape(o.name) << "\""
           << ", \"id\": " << o.id << ", \"parent\": " << o.parent << ", \"collection\": " << o.collection << ", \"order\": " << o.order
           << ", \"visible\": " << b01(o.visible) << ", \"selectable\": " << b01(o.selectable) << ", \"collapsed\": " << b01(o.collapsed) << "}";
        first = false;
        ++count;
    }
    js << "\n  ],\n  \"village\": " << (m_game ? m_game->villageJson() : std::string("[]")) << "\n}\n";
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << js.str();
    wchar_t b[128];
    swprintf(b, 128, L"Scene saved (%d objects)", count);
    status(b, 1);
    return true;
}

bool App::loadSceneFile(const std::wstring& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    Json j;
    if (!Json::parse(ss.str(), j) || j["format"].str() != "vp3d-scene") {
        status(L"Invalid scene file", 2);
        return false;
    }
    loadScene(SCENE_STUDIO);
    m_undo.clear();
    int count = 0;
    // Scene Manager (files of an older version have none of these: every field keeps its default)
    auto flag = [](const Json& v, bool def) { return v.type == Json::Bool ? v.b : def; };
    auto u32 = [](const Json& v) { return (uint32_t)std::max(0.0, v.num(0)); };
    for (const Json& c : j["collections"].a) {
        Collection col;
        col.id = u32(c["id"]);
        col.name = c["name"].str("Collection");
        col.color = (int)c["color"].num(0);
        col.collapsed = flag(c["collapsed"], false);
        col.order = (int)c["order"].num(0);
        if (col.id) m_scene.collections.push_back(col);
    }
    if (j["sun"].type == Json::Object) {
        const Json& sj = j["sun"];
        m_scene.sun.id = u32(sj["id"]);
        m_scene.sun.collection = u32(sj["collection"]);
        m_scene.sun.order = (int)sj["order"].num(0);
        m_scene.sun.selectable = flag(sj["selectable"], true);
    }
    for (size_t i = 0; i < j["lights"].a.size() && i < m_scene.lights.size(); ++i) {
        const Json& lj = j["lights"][i];
        AreaLight& l = m_scene.lights[i];
        l.id = u32(lj["id"]);
        l.parent = u32(lj["parent"]);
        l.collection = u32(lj["collection"]);
        l.order = (int)lj["order"].num(0);
        l.selectable = flag(lj["selectable"], true);
        l.enabled = flag(lj["enabled"], l.enabled);
        if (lj["name"].type == Json::String) l.name = lj["name"].s;
        if (lj["pos"].type == Json::Array) l.position = float3((float)lj["pos"][0].num(), (float)lj["pos"][1].num(), (float)lj["pos"][2].num());
        if (lj["target"].type == Json::Array) l.target = float3((float)lj["target"][0].num(), (float)lj["target"][1].num(), (float)lj["target"][2].num());
    }
    auto outlinerFields = [&](Object& ob, const Json& b) {
        ob.id = u32(b["id"]);
        ob.parent = u32(b["parent"]);
        ob.collection = u32(b["collection"]);
        ob.order = (int)b["order"].num(0);
        ob.visible = flag(b["visible"], true);
        ob.selectable = flag(b["selectable"], true);
        ob.collapsed = flag(b["collapsed"], false);
        if (b["scale"].type == Json::Array && b["scale"].a.size() == 3)
            ob.scale = maxv(float3((float)b["scale"][0].num(1), (float)b["scale"][1].num(1), (float)b["scale"][2].num(1)), float3(0.01f));
    };
    for (const Json& b : j["objects"].a) {
        std::string part = b["part"].str();
        int color = (int)b["color"].num(-1);
        float3 pos((float)b["pos"][0].num(), (float)b["pos"][1].num(), (float)b["pos"][2].num());
        float yaw = (float)b["yaw"].num();
        bool hasRot = b["rot"].type == Json::Array && b["rot"].a.size() == 4;
        if (flag(b["group"], false)) {   // group node: a transform only
            Object g;
            g.part = "GROUP";
            g.group = true;
            g.mesh = -1;
            g.material = 0;
            g.position = pos;
            g.useRot = true;
            g.rot = hasRot ? qNormalize(float4((float)b["rot"][0].num(), (float)b["rot"][1].num(), (float)b["rot"][2].num(), (float)b["rot"][3].num(0)))
                           : qAxisAngle(float3(0, 1, 0), radians(yaw));
            g.name = b["name"].str("Group");
            outlinerFields(g, b);
            m_scene.addObject(g);
            continue;
        }
        int idx = addPart(m_scene, part, color >= 0 ? color : 1, -1, pos, yaw);
        if (idx < 0) continue;
        outlinerFields(m_scene.objects[idx], b);
        {
            Object& ob = m_scene.objects[idx];
            if (hasRot) {
                ob.rot = qNormalize(float4((float)b["rot"][0].num(), (float)b["rot"][1].num(), (float)b["rot"][2].num(), (float)b["rot"][3].num(0)));
                ob.useRot = true;
            } else {
                ob.rot = qAxisAngle(float3(0, 1, 0), radians(yaw));
                ob.useRot = true;
            }
        }
        if (b["name"].type == Json::String) m_scene.objects[idx].name = b["name"].s;
        Material& m = m_scene.materials[m_scene.objects[idx].material];
        if (color < 0 && b["rgb"].type == Json::Array)
            m.baseColor = float3(srgbToLinear((float)b["rgb"][0].num()), srgbToLinear((float)b["rgb"][1].num()), srgbToLinear((float)b["rgb"][2].num()));
        int preset = (int)b["material"].num(m.preset);
        if (preset != m.preset) applyPreset(m, preset, 1.0f);
        ++count;
    }
    if (m_game) m_game->loadVillage(j["village"]);   // after the objects: the houses go on the grid
    const Json& env = j["environment"];
    if (env.type == Json::Object && env["hdri"].type == Json::String && setHdri(fromUtf8(env["hdri"].s))) {
        m_scene.env.hdriIntensity = (float)env["intensity"].num(1);
        m_scene.env.hdriRotation = radians((float)env["rotation"].num(0));
        m_scene.env.visibleToCamera = env["visible"].type != Json::Bool || env["visible"].b;
    }
    const Json& c = j["camera"];
    if (c.type == Json::Object) {
        Camera& cam = m_scene.camera;
        cam.target = float3((float)c["target"][0].num(), (float)c["target"][1].num(), (float)c["target"][2].num());
        cam.yaw = radians((float)c["yaw"].num(38));
        cam.pitch = radians((float)c["pitch"].num(30));
        cam.distance = (float)c["distance"].num(26);
        cam.fovY = radians((float)c["fov"].num(32));
    }
    wchar_t b[128];
    swprintf(b, 128, L"Scene loaded (%d objects)", count);
    status(b, 1);
    {   // baked probes of this file: game/probes/<file name without .v3d.json>.probes
        std::wstring name = std::filesystem::path(path).filename().wstring();
        size_t dot = name.find(L'.');
        if (dot != std::wstring::npos) name = name.substr(0, dot);
        m_probeKey = toUtf8(name);
        loadBakedProbesFile();
    }
    markDirty();
    return true;
}

static std::wstring fileDialog(HWND hwnd, bool save) {
    wchar_t buf[MAX_PATH] = L"scene.v3d.json";
    OPENFILENAMEW of = {sizeof(of)};
    of.hwndOwner = hwnd;
    of.lpstrFilter = L"WZX scene (*.v3d.json)\0*.v3d.json;*.json\0All files\0*.*\0";
    of.lpstrFile = buf;
    of.nMaxFile = MAX_PATH;
    of.lpstrDefExt = L"json";
    of.Flags = save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST;
    BOOL ok = save ? GetSaveFileNameW(&of) : GetOpenFileNameW(&of);
    return ok ? std::wstring(buf) : std::wstring();
}

void App::runPendingAction() {
    int a = m_pendingAction;
    m_pendingAction = 0;
    if (a == 1) {
        std::wstring p = fileDialog(m_hwnd, true);
        if (!p.empty()) saveSceneFile(p);
    } else if (a == 2) {
        std::wstring p = fileDialog(m_hwnd, false);
        if (!p.empty()) loadSceneFile(p);
    } else if (a == 3) {
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

