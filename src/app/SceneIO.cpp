// Scene file format (.v3d.json).
#include "App.h"
#include "Json.h"
#include <commdlg.h>
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
           << "], \"yaw\": " << yawDeg << ", \"rot\": [" << q.x << ", " << q.y << ", " << q.z << ", " << q.w << "]}";
        first = false;
        ++count;
    }
    js << "\n  ]\n}\n";
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << js.str();
    wchar_t b[128];
    swprintf(b, 128, L"Scène enregistrée (%d objets)", count);
    m_status = b;
    m_statusTime = GetTickCount();
    return true;
}

bool App::loadSceneFile(const std::wstring& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    Json j;
    if (!Json::parse(ss.str(), j) || j["format"].str() != "vp3d-scene") {
        m_status = L"Fichier de scène invalide";
        m_statusTime = GetTickCount();
        return false;
    }
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
        Material& m = m_scene.materials[m_scene.objects[idx].material];
        if (color < 0 && b["rgb"].type == Json::Array)
            m.baseColor = float3(srgbToLinear((float)b["rgb"][0].num()), srgbToLinear((float)b["rgb"][1].num()), srgbToLinear((float)b["rgb"][2].num()));
        int preset = (int)b["material"].num(m.preset);
        if (preset != m.preset) applyPreset(m, preset, 1.0f);
        ++count;
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
    swprintf(b, 128, L"Scène chargée (%d objets)", count);
    m_status = b;
    m_statusTime = GetTickCount();
    markDirty();
    return true;
}

static std::wstring fileDialog(HWND hwnd, bool save) {
    wchar_t buf[MAX_PATH] = L"scene.v3d.json";
    OPENFILENAMEW of = {sizeof(of)};
    of.hwndOwner = hwnd;
    of.lpstrFilter = L"Scène Viewport 3D (*.v3d.json)\0*.v3d.json;*.json\0Tous les fichiers\0*.*\0";
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
    }
}

} // namespace app

