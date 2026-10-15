// Old scene file format (.v3d.json): still written for the Build and the player, read as an import by the editor
// (the editor saves .wzx, see SceneWzx.cpp).
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

bool App::saveLegacySceneFile(const std::wstring& path) {
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
    js << "  \"objects\": [";
    bool first = true;
    int count = 0;
    for (const Object& o : m_scene.objects) {
        if (!o.visible || o.ghost || !findPart(o.part)) continue;
        const Material& m = m_scene.materials[o.material];
        float4 q = objRot(o);
        float yawDeg = degrees(std::atan2(-qRot(q, float3(1, 0, 0)).z, qRot(q, float3(1, 0, 0)).x));
        js << (first ? "\n" : ",\n") << "    {\"part\": \"" << jsonEscape(o.part) << "\", \"color\": " << m.colorId
           << ", \"rgb\": [" << linToSrgb(m.baseColor.x) << ", " << linToSrgb(m.baseColor.y) << ", " << linToSrgb(m.baseColor.z) << "]"
           << ", \"material\": " << m.preset << ", \"pos\": [" << o.position.x << ", " << o.position.y << ", " << o.position.z
           << "], \"yaw\": " << yawDeg << ", \"rot\": [" << q.x << ", " << q.y << ", " << q.z << ", " << q.w << "], \"name\": \"" << jsonEscape(o.name) << "\"}";
        first = false;
        ++count;
    }
    js << "\n  ],\n  \"village\": " << (m_game ? m_game->villageJson() : std::string("[]")) << "\n}\n";
    if (!writeFileSafe(path, js.str())) return false;
    wchar_t b[128];
    swprintf(b, 128, L"Scene saved (%d objects)", count);
    status(b, 1);
    return true;
}

bool App::loadLegacySceneFile(const Json& j, const std::wstring& path) {
    loadScene(SCENE_STUDIO);
    m_undo.clear();
    int count = 0;
    for (const Json& b : j["objects"].a) {
        std::string part = b["part"].str();
        int color = (int)b["color"].num(-1);
        float3 pos((float)b["pos"][0].num(), (float)b["pos"][1].num(), (float)b["pos"][2].num());
        float yaw = (float)b["yaw"].num();
        bool hasRot = b["rot"].type == Json::Array && b["rot"].a.size() == 4;
        int idx = addPart(m_scene, part, color >= 0 ? color : 1, -1, pos, yaw);
        if (idx < 0) continue;
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
    else m_pendingVillage = j["village"];             // opened from the command line, before the game layer
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

} // namespace app

