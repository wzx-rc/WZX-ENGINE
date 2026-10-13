// Game layer: Play mode, node graph editor hookup, builds, player mode and the local command API.
#include "App.h"
#include "GameHost.h"
#include "atelier/ui_icons.h"
#include <unordered_map>
#include <shellapi.h>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace app {

namespace fs = std::filesystem;
using namespace game;

static const int kApiPort = 7878;

static std::string num(float v) {
    char b[32];
    snprintf(b, sizeof b, "%g", v);
    return b;
}

static std::string vec(const float3& v) { return "[" + num(v.x) + ", " + num(v.y) + ", " + num(v.z) + "]"; }

static std::string jstr(const std::string& s) { return "\"" + jsonEscape(s) + "\""; }

static bool readVec(const Json& j, float3& out) {
    if (j.type != Json::Array || j.a.size() < 3) return false;
    out = float3((float)j[0].num(), (float)j[1].num(), (float)j[2].num());
    return true;
}

static float linToSrgb1(float c) {
    c = clampf(c, 0, 1);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

static std::string readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Icon of each node type, from the Atelier icon set (the game layer only names the type).
static void drawNodeIcon(ImDrawList* dl, const std::string& id, ImVec2 p, float size, ImU32 color) {
    using atelier::Icon;
    static const std::unordered_map<std::string, Icon> kIcons = {
        {"event.start", Icon::Play},          {"event.tick", Icon::Refresh},        {"event.clickObject", Icon::Pin},
        {"event.clickGround", Icon::Pin},     {"event.custom", Icon::Link},         {"event.key", Icon::Monitor},
        {"flow.branch", Icon::Blueprint},     {"flow.sequence", Icon::List},        {"flow.delay", Icon::Animation},
        {"flow.forEachPart", Icon::Cube},     {"flow.doOnce", Icon::Check},         {"flow.fireCustom", Icon::Link},
        {"action.setPosition", Icon::Move},   {"action.moveTo", Icon::ArrowRight},  {"action.rotate", Icon::Rotate},
        {"action.spawn", Icon::Cube},         {"action.destroy", Icon::Trash},      {"action.setColor", Icon::Material},
        {"action.setScale", Icon::Scale},     {"action.select", Icon::Pin},         {"action.print", Icon::List},
        {"action.setVar", Icon::Save},        {"action.addVar", Icon::Plus},        {"data.float", Icon::Plus},
        {"data.bool", Icon::Check},           {"data.vec3", Icon::Move},            {"data.string", Icon::List},
        {"data.getVar", Icon::Pin},           {"data.selected", Icon::Cube},        {"data.findObject", Icon::Search},
        {"data.getPosition", Icon::Move},     {"data.partOf", Icon::Cube},          {"data.isValid", Icon::Check},
        {"data.makeVec3", Icon::Move},        {"data.breakVec3", Icon::Move},       {"data.distance", Icon::Scale},
        {"data.time", Icon::Animation},       {"math.add", Icon::Plus},             {"math.sub", Icon::ArrowLeft},
        {"math.mul", Icon::Scale},            {"math.div", Icon::Scale},            {"math.greater", Icon::Check},
        {"math.less", Icon::ArrowLeft},       {"math.equal", Icon::Check},          {"math.strEqual", Icon::Check},
        {"math.and", Icon::Check},            {"math.or", Icon::Check},             {"math.not", Icon::Undo},
        {"math.random", Icon::Refresh},       {"math.addVec3", Icon::Move},         {"math.scaleVec3", Icon::Scale},
        {"math.concat", Icon::List},          {"rts.snap", Icon::Grid},             {"rts.cellFree", Icon::Grid},
        {"rts.camera", Icon::Camera},         {"rts.count", Icon::Cube},
    };
    auto it = kIcons.find(id);
    atelier::DrawIcon(dl, it == kIcons.end() ? Icon::Blueprint : it->second, p, size, color);
}

// --------------------------------------------------------------------------- setup

GameHost::~GameHost() { m_server.stop(); }

void GameHost::init() {
    m_editor.setFonts(m_app.m_gui.fontSmall, m_app.m_gui.fontBold);
    m_editor.setNodeIconDrawer(&drawNodeIcon);
    fs::path exe(m_app.m_exeDir);
    std::error_code ec;
    if (fs::exists(exe / "game.json")) {
        // Built game: everything sits next to the executable.
        m_player = true;
        m_projectDir = m_gameDir = exe.wstring();
        Json cfg;
        Json::parse(readFile(exe / "game.json"), cfg);
        m_graphPath = (exe / fromUtf8(cfg["graph"].str("graph.json"))).wstring();
        m_playerFps = cfg["showFps"].b;
        m_playerPathTracing = cfg["pathTracing"].b;
        fs::path scene = exe / fromUtf8(cfg["scene"].str("scene.v3d.json"));
        if (fs::exists(scene)) m_app.loadSceneFile(scene.wstring());
        m_app.m_rs.raster = true;
        m_app.m_tool = TOOL_SELECT;
        std::wstring title = fromUtf8(cfg["title"].str("WZX Game"));
        SetWindowTextW(m_app.m_hwnd, title.c_str());
        HWND h = m_app.m_hwnd;
        if (cfg["fullscreen"].b) {
            MONITORINFO mi = {sizeof(mi)};
            GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
            SetWindowLongPtrW(h, GWL_STYLE, WS_POPUP | WS_VISIBLE);
            SetWindowPos(h, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED);
        } else {
            int w = (int)cfg["width"].num(1600), hh = (int)cfg["height"].num(900);
            RECT r = {0, 0, (LONG)(w * m_app.m_dpi), (LONG)(hh * m_app.m_dpi)};
            AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, (UINT)(m_app.m_dpi * 96));
            SetWindowPos(h, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER);
        }
        std::string err;
        if (!loadGraph(&err)) toast(L"Graph not found: " + fromUtf8(err));
        if (cfg["commandApi"].b)
            for (int p = kApiPort; p < kApiPort + 10 && !m_server.running(); ++p) m_server.start(p);
        play();
        return;
    }
    // Editor: project root = two levels above build\bin when the sources are there.
    fs::path root = exe.parent_path().parent_path();
    if (!fs::exists(root / "src")) root = exe;
    m_projectDir = root.wstring();
    m_gameDir = (root / "game").wstring();
    fs::create_directories(m_gameDir, ec);
    m_graphPath = (fs::path(m_gameDir) / "main.graph.json").wstring();
    if (!loadGraph()) {
        defaultGraph();
        saveGraph();
    }
    for (int p = kApiPort; p < kApiPort + 10 && !m_server.running(); ++p) m_server.start(p);
    if (m_server.running())
        toast(L"Command API: http://127.0.0.1:" + std::to_wstring(m_server.port()) + L"   ·   F3 graph   ·   F5 Play   ·   F7 Build");
}

bool GameHost::saveGraph(std::string* err) {
    if (!m_graph.save(m_graphPath)) { if (err) *err = "écriture impossible"; return false; }
    m_editor.clearModified();
    return true;
}

bool GameHost::loadGraph(std::string* err) {
    if (!fs::exists(m_graphPath)) { if (err) *err = "fichier absent"; return false; }
    return m_graph.load(m_graphPath, err);
}

// Example graph: select a unit by clicking it, click the ground to send it to a grid cell,
// R turns it, C spawns a cube on a free cell.
void GameHost::defaultGraph() {
    Graph& g = m_graph;
    g.clear();
    g.groups = {
        Group{"INITIALISE ACTOR", -30, -50, 560, 330, "Scene"},
        Group{"GAMEPLAY LOGIC", -30, 290, 1330, 390, "Flow Control"},
        Group{"DATA & PARAMETERS", -30, 690, 1330, 560, "Math"},
    };
    auto L = [&](int a, const char* out, int b, const char* in) {
        g.connect(a, g.node(a)->def->findOut(out), b, g.node(b)->def->findIn(in));
    };
    auto set = [&](int n, const char* pin, const Value& v) {
        Node* nd = g.node(n);
        int i = nd->def->findIn(pin);
        if (i >= 0) { Value t = v; t.type = nd->def->ins[i].type; nd->inputs[i] = t; }
    };
    int start = g.addNode("event.start", float2(0, 0));
    int hello = g.addNode("action.print", float2(260, 0));
    set(hello, "Text", Value::Str("Click a unit, then the ground to move it. R: rotate, C: spawn a cube."));
    L(start, "", hello, "");

    int click = g.addNode("event.clickObject", float2(0, 140));
    int sel = g.addNode("action.select", float2(260, 140));
    L(click, "", sel, "");
    L(click, "Object", sel, "Object");

    int ground = g.addNode("event.clickGround", float2(0, 320));
    int selected = g.addNode("data.selected", float2(0, 440));
    int valid = g.addNode("data.isValid", float2(230, 470));
    int branch = g.addNode("flow.branch", float2(480, 320));
    int snap = g.addNode("rts.snap", float2(230, 560));
    int move = g.addNode("action.moveTo", float2(760, 320));
    int done = g.addNode("action.print", float2(1040, 380));
    set(done, "Text", Value::Str("Unit arrived"));
    L(ground, "", branch, "");
    L(selected, "Object", valid, "Object");
    L(valid, "Valid", branch, "Condition");
    L(branch, "True", move, "");
    L(selected, "Object", move, "Object");
    L(ground, "Point", snap, "Position");
    L(snap, "Position", move, "Target");
    L(move, "Arrived", done, "");

    int keyR = g.addNode("event.key", float2(0, 720));
    set(keyR, "Key", Value::Str("R"));
    int rot = g.addNode("action.rotate", float2(260, 720));
    L(keyR, "", rot, "");
    L(selected, "Object", rot, "Object");

    int keyC = g.addNode("event.key", float2(0, 860));
    set(keyC, "Key", Value::Str("C"));
    int rnd = g.addNode("math.random", float2(0, 960));
    set(rnd, "Min", Value::Float(-30));
    set(rnd, "Max", Value::Float(30));
    int mk = g.addNode("data.makeVec3", float2(230, 960));
    L(rnd, "Value", mk, "X");
    L(rnd, "Value", mk, "Z");
    int snap2 = g.addNode("rts.snap", float2(500, 1000));
    L(mk, "Vector", snap2, "Position");
    int spawn = g.addNode("action.spawn", float2(760, 860));
    L(keyC, "", spawn, "");
    L(snap2, "Position", spawn, "Position");
    int count = g.addNode("action.addVar", float2(1040, 860));
    set(count, "Name", Value::Str("cubes"));
    L(spawn, "", count, "");
}

// --------------------------------------------------------------------------- play

void GameHost::play() {
    if (m_playing) return;
    Scene& s = m_app.m_scene;
    m_saveObjects = s.objects;
    m_saveMaterials = s.materials;
    m_saveSelected = m_app.m_selected;
    m_saveTool = m_app.m_tool;
    m_app.m_tool = TOOL_SELECT;
    m_app.m_multi.clear();
    m_app.m_gz = GizmoDrag();
    m_playing = true;
    m_rt.start(&m_graph, this);
    m_app.markDirty();
}

void GameHost::stop() {
    if (!m_playing) return;
    m_rt.stop();
    m_playing = false;
    m_rtsCam = false;
    Scene& s = m_app.m_scene;
    s.objects = m_saveObjects;
    s.materials = m_saveMaterials;
    m_app.m_selected = m_saveSelected < (int)s.objects.size() ? m_saveSelected : -1;
    m_app.m_multi.clear();
    m_app.m_tool = (Tool)m_saveTool;
    m_app.markDirty();
}

void GameHost::update(float dt) {
    if (m_playing) m_rt.tick(dt);
    if (m_playing && m_rtsCam) updateRtsCamera(dt);
}

void GameHost::setRtsCamera(bool on) {
    if (on && !m_rtsCam) {
        const vp3d::Camera& c = m_app.m_scene.camera;
        m_cam.reset(c.target, c.yaw, std::min(c.distance, 14.0f));   // the RTS starting view (prototype default)
    }
    m_rtsCam = on;
    m_rmbDrag = false;
}

bool GameHost::groundUnderMouse(float3& p) const {
    ImVec2 m = ImGui::GetIO().MousePos;
    float3 o, d;
    m_app.cameraRay(m.x, m.y, o, d);
    if (std::fabs(d.y) < 1e-5f) return false;
    float t = -o.y / d.y;
    if (t <= 0) return false;
    p = o + d * t;
    return true;
}

// RTS camera (node "Caméra RTS"): keys, screen edges, wheel zoom to the cursor, right drag, middle turn.
void GameHost::updateRtsCamera(float dt) {
    ImGuiIO& io = ImGui::GetIO();
    const ScreenRect& v = m_app.m_rcView;
    const float e = 6 * m_app.m_dpi;
    bool inView = v.contains(io.MousePos.x, io.MousePos.y) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
    game::CameraInput in;
    if (!io.WantTextInput) {
        // physical positions (QWERTY scan codes), so ZQSD on AZERTY and WASD on QWERTY both work;
        // turning uses the QWERTY Q/E positions (A/E on AZERTY)
        auto k = [](ImGuiKey key) { return ImGui::IsKeyDown(key) ? 1.0f : 0.0f; };
        auto phys = [](UINT scan) { UINT vk = MapVirtualKeyW(scan, MAPVK_VSC_TO_VK); return vk && (GetAsyncKeyState(vk) & 0x8000) ? 1.0f : 0.0f; };
        in.moveZ = phys(0x11) + k(ImGuiKey_UpArrow) - phys(0x1F) - k(ImGuiKey_DownArrow);   // W / S
        in.moveX = phys(0x20) + k(ImGuiKey_RightArrow) - phys(0x1E) - k(ImGuiKey_LeftArrow); // D / A
        in.turn = phys(0x12) - phys(0x10);                                                   // E / Q
    }
    in.fast = io.KeyShift;
    if (inView) {
        if (io.MousePos.x < v.x + e) in.moveX -= 1;
        if (io.MousePos.x > v.r() - e) in.moveX += 1;
        if (io.MousePos.y < v.y + e) in.moveZ += 1;
        if (io.MousePos.y > v.b() - e) in.moveZ -= 1;
        in.wheel = io.MouseWheel;
        float3 g;
        if (groundUnderMouse(g)) { in.hasCursorGround = true; in.cursorGround = g; }
    }
    m_cam.update(in, dt);
    if (inView && ImGui::IsMouseDown(1)) {
        float3 g;
        if (groundUnderMouse(g)) {
            if (!m_rmbDrag) { m_rmbDrag = true; m_rmbGrab = g; }
            else m_cam.dragTo(m_rmbGrab, g);
        }
    } else {
        m_rmbDrag = false;
    }
    if (inView && ImGui::IsMouseDown(2)) m_cam.addYaw(io.MouseDelta.x * 0.01f);
    m_cam.apply(m_app.m_scene.camera);
    m_app.cameraChanged();
}

// --------------------------------------------------------------------------- World

int GameHost::objectCount() const { return (int)m_app.m_scene.objects.size(); }

bool GameHost::objectAlive(int i) const {
    return i >= 0 && i < objectCount() && m_app.m_scene.objects[i].visible && !m_app.m_scene.objects[i].ghost;
}

std::string GameHost::objectName(int i) const { return objectAlive(i) ? m_app.m_scene.objects[i].name : ""; }
std::string GameHost::objectPart(int i) const { return objectAlive(i) ? m_app.m_scene.objects[i].part : ""; }
float3 GameHost::getPosition(int i) const { return objectAlive(i) ? m_app.m_scene.objects[i].position : float3(0, 0, 0); }

void GameHost::setPosition(int i, float3 p) {
    if (!objectAlive(i)) return;
    m_app.m_scene.objects[i].position = p;
    m_app.markDirty();
}

void GameHost::rotateY(int i, float deg) {
    if (!objectAlive(i)) return;
    Object& o = m_app.m_scene.objects[i];
    o.yaw += radians(deg);
    if (o.useRot) o.rot = qNormalize(qMul(qAxisAngle(float3(0, 1, 0), radians(deg)), o.rot));
    m_app.markDirty();
}

void GameHost::setScale(int i, float3 s) {
    if (!objectAlive(i)) return;
    m_app.m_scene.objects[i].scale = s;
    m_app.markDirty();
}

void GameHost::setColor(int i, float3 rgb) {
    if (!objectAlive(i)) return;
    Material& m = m_app.m_scene.materials[m_app.m_scene.objects[i].material];
    m.baseColor = float3(srgbToLinear(clampf(rgb.x, 0, 1)), srgbToLinear(clampf(rgb.y, 0, 1)), srgbToLinear(clampf(rgb.z, 0, 1)));
    m.colorId = -1;
    m_app.markDirty();
}

std::string GameHost::uniqueName(const std::string& base) const {
    auto used = [&](const std::string& n) {
        for (const Object& o : m_app.m_scene.objects) if (o.name == n) return true;
        return false;
    };
    if (!used(base)) return base;
    for (int k = 2;; ++k) {
        std::string n = base + " " + std::to_string(k);
        if (!used(n)) return n;
    }
}

int GameHost::spawn(const std::string& part, float3 pos, float3 rgb) {
    const PartDef* p = findPart(part);
    if (!p) return -1;
    int idx = addPart(m_app.m_scene, p->id, p->defaultColor, -1, pos, 0);
    if (idx < 0) return -1;
    m_app.m_scene.objects[idx].name.clear();   // the new object must not collide with itself
    m_app.m_scene.objects[idx].name = uniqueName(toUtf8(p->name));
    setColor(idx, rgb);
    m_app.markDirty();
    return idx;
}

void GameHost::destroy(int i) {
    if (!objectAlive(i)) return;
    Object& o = m_app.m_scene.objects[i];
    o.visible = false;
    o.selectable = false;
    if (m_app.m_selected == i) m_app.m_selected = -1;
    m_app.markDirty();
}

int GameHost::selected() const { return m_app.m_selected; }

void GameHost::select(int i) {
    m_app.m_selected = objectAlive(i) ? i : -1;
    m_app.m_multi.clear();
    m_app.markDirty();
}

void GameHost::message(const std::string& text) { toast(fromUtf8(text)); }

void GameHost::toast(const std::wstring& s) {
    m_toasts.push_back({s, GetTickCount64()});
    if (m_toasts.size() > 5) m_toasts.erase(m_toasts.begin());
}

// --------------------------------------------------------------------------- per-frame input

// Game clicks and keys (Play in the editor, or the built game).
void GameHost::playInput() {
    ImGuiIO& io = ImGui::GetIO();
    const ScreenRect& v = m_app.m_rcView;
    if (ImGui::IsMouseClicked(0) && v.contains(io.MousePos.x, io.MousePos.y) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        float3 o, d;
        m_app.cameraRay(io.MousePos.x, io.MousePos.y, o, d);
        PickResult pr = pick(m_app.m_scene, o, d);
        if (pr.object >= 0 && objectAlive(pr.object) && !m_app.m_scene.objects[pr.object].part.empty()) {
            m_rt.fireClickObject(pr.object, pr.position);
        } else if (std::fabs(d.y) > 1e-5f) {
            float t = -o.y / d.y;
            if (t > 0) m_rt.fireClickGround(o + d * t);
        }
    }
    if (io.WantTextInput) return;
    for (int k = ImGuiKey_A; k <= ImGuiKey_Z; ++k)
        if (ImGui::IsKeyPressed((ImGuiKey)k, true)) m_rt.fireKey(std::string(1, (char)('A' + k - ImGuiKey_A)));
    for (int k = ImGuiKey_0; k <= ImGuiKey_9; ++k)
        if (ImGui::IsKeyPressed((ImGuiKey)k, true)) m_rt.fireKey(std::string(1, (char)('0' + k - ImGuiKey_0)));
    for (int k = ImGuiKey_F1; k <= ImGuiKey_F12; ++k)
        if (ImGui::IsKeyPressed((ImGuiKey)k, true)) m_rt.fireKey("F" + std::to_string(k - ImGuiKey_F1 + 1));
    static const struct { ImGuiKey key; const char* name; } kKeys[] = {
        {ImGuiKey_Space, "ESPACE"}, {ImGuiKey_Enter, "ENTREE"}, {ImGuiKey_Escape, "ECHAP"}, {ImGuiKey_Delete, "SUPPR"},
        {ImGuiKey_Tab, "TAB"}, {ImGuiKey_UpArrow, "HAUT"}, {ImGuiKey_DownArrow, "BAS"}, {ImGuiKey_LeftArrow, "GAUCHE"},
        {ImGuiKey_RightArrow, "DROITE"},
    };
    for (auto& k : kKeys) if (ImGui::IsKeyPressed(k.key, true)) m_rt.fireKey(k.name);
}

bool GameHost::beginFrame() {
    handleRequests();
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput) {
        if (!m_player) {
            if (ImGui::IsKeyPressed(ImGuiKey_F3, false)) {
                setGraphOpen(!showGraph());


            }
            if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) m_playing ? stop() : play();
            if (ImGui::IsKeyPressed(ImGuiKey_F7, false)) m_pendingToolbar = 3;
        } else {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) PostMessageW(m_app.m_hwnd, WM_CLOSE, 0, 0);
            if (m_playerPathTracing && ImGui::IsKeyPressed(ImGuiKey_P, false)) { m_app.m_rs.raster = !m_app.m_rs.raster; m_app.markDirty(); }
        }
    }
    // toolbar buttons (drawn in draw(), applied here on the next frame)
    switch (m_pendingToolbar) {
    case 1: m_playing ? stop() : play(); break;
    case 2: setGraphOpen(true); break;
    case 3: {
        std::wstring out;
        std::string err;
        if (build("", false, out, err)) {
            toast(L"Build finished: " + out);
            m_editor.addMessage(3, "Build finished: " + toUtf8(out));
        } else {
            toast(L"Build failed: " + fromUtf8(err));
            m_editor.addMessage(2, "Build failed: " + err);
        }
        break;
    }
    default: break;
    }
    m_pendingToolbar = 0;

    if (graphOpen()) return true;   // the graph editor owns the keys
    if (m_playing) {
        playInput();
        return true;
    }
    return m_player;
}

// --------------------------------------------------------------------------- drawing

// The Graph tab of the editor: node library on the left, the node editor on the canvas.
void GameHost::drawGraphWorkspace() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float s = m_app.m_dpi;
    const float top = vp->WorkPos.y + m_app.m_atelierTop;   // below the editor top bar
    const float sideW = 230 * s;
    const float h = std::max(1.0f, vp->WorkPos.y + vp->WorkSize.y - top);
    const ImVec2 sidePos(vp->WorkPos.x, top);
    const ImVec2 canvasPos(vp->WorkPos.x + sideW, top);
    const ImVec2 canvasSize(std::max(1.0f, vp->WorkSize.x - sideW), h);

    // node library: graphs, then the nodes grouped by category (click = add at the centre of the view)
    ImGui::SetNextWindowPos(sidePos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(sideW, h), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(1.0f);
    const ImGuiWindowFlags sideFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("##nodelibrary", nullptr, sideFlags)) {
        ImGui::TextUnformatted("My Blueprint");
        ImGui::SameLine(sideW - 64 * s);
        ImGui::TextDisabled("ASSET");
        ImGui::Separator();
        ImGui::TextDisabled("GRAPHS");
        ImGui::Selectable("Game Graph", true);
        ImGui::Spacing();
        ImGui::TextDisabled("NODE LIBRARY");
        ImGui::SameLine(sideW - 64 * s);
        ImGui::TextDisabled("%d nodes", (int)nodeDefs().size());
        static char search[96] = {};
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##libsearch", "Search nodes…", search, sizeof search);
        std::string q = search;
        for (char& c : q) c = (char)tolower((unsigned char)c);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        std::string last;
        for (const NodeDef& d : nodeDefs()) {
            std::string label = d.label;
            for (char& c : label) c = (char)tolower((unsigned char)c);
            if (!q.empty() && label.find(q) == std::string::npos) continue;
            if (d.category != last) {
                ImGui::Spacing();
                ImGui::TextColored(ImColor(categoryAccentColor(d.category)), "%s", d.category.c_str());
                last = d.category;
            }
            // row: highlight and click from a selectable; icon, name and the "+" drawn on it (Atelier library)
            ImGui::PushID(d.id.c_str());
            ImVec2 rmin = ImGui::GetCursorScreenPos();
            bool clicked = ImGui::Selectable("##row", false, 0, ImVec2(0, 22 * s));
            ImVec2 rmax = ImGui::GetItemRectMax();
            float cy = (rmin.y + rmax.y) * 0.5f;
            drawNodeIcon(dl, d.id, ImVec2(rmin.x + 6 * s, cy - 7 * s), 14 * s, categoryAccentColor(d.category));
            dl->AddText(ImGui::GetFont(), 13 * s, ImVec2(rmin.x + 26 * s, cy - 7 * s), IM_COL32(201, 208, 216, 255), d.label.c_str());
            atelier::DrawIcon(dl, atelier::Icon::Plus, ImVec2(rmax.x - 18 * s, cy - 6 * s), 12 * s, IM_COL32(132, 143, 155, 255));
            if (clicked) m_editor.addNodeAtView(m_graph, d);
            if (ImGui::IsItemHovered() && !d.help.empty()) ImGui::SetTooltip("%s", d.help.c_str());
            ImGui::PopID();
        }
    }
    ImGui::End();

    if (!m_graphFramed) { m_graphFramed = true; m_editor.frameAll(m_graph); }
    m_editor.setOutline(th::settings().graphOutline, th::settings().graphOutlineWeight);
    m_editor.draw(canvasPos, canvasSize, s, m_graph, m_playing ? &m_rt : nullptr, m_playing);
    switch (m_editor.takeAction()) {
    case NodeEditor::TogglePlay: m_playing ? stop() : play(); break;
    case NodeEditor::Compile: compileGraph(); break;
    case NodeEditor::Save: {
        std::string err;
        if (saveGraph(&err)) m_editor.addMessage(3, "Graph saved: " + toUtf8(m_graphPath));
        else m_editor.addMessage(2, "Save failed: " + err);
        break;
    }
    case NodeEditor::Close: setGraphOpen(false); break;
    case NodeEditor::Build: m_pendingToolbar = 3; break;
    default: break;
    }
}

bool GameHost::showGraph() const { return m_app.m_workspace == 1 && !m_player; }
void GameHost::setGraphOpen(bool on) { m_app.m_workspace = on ? 1 : 0; }

// Compile: saves the graph, checks its execution flow and lists the results in the Output panel.
void GameHost::compileGraph() {
    std::string err;
    if (!saveGraph(&err)) {
        m_editor.addMessage(2, "Compile failed: the graph could not be saved (" + err + ")");
        return;
    }
    std::vector<Diagnostic> diags = m_graph.validate();
    int errors = 0, warnings = 0;
    for (const Diagnostic& d : diags) {
        errors += d.level == 2;
        warnings += d.level == 1;
    }
    m_editor.setDiagnostics(diags);
    m_editor.clearModified();
    std::string text = std::string(errors ? "Compile failed" : "Compile successful") + " · " + std::to_string(m_graph.nodes.size()) +
                       " nodes · " + std::to_string(m_graph.links.size()) + " links";
    if (warnings) text += " · " + std::to_string(warnings) + (warnings == 1 ? " warning" : " warnings");
    if (errors) text += " · " + std::to_string(errors) + (errors == 1 ? " error" : " errors");
    m_editor.addMessage(errors ? 2 : 3, text);
}

void GameHost::draw() {
    const ScreenRect& v = m_app.m_rcView;
    if (m_player) {
        if (m_playerFps) {
            char b[64];
            snprintf(b, sizeof b, "%.0f FPS", 1000.0f / std::max(m_app.m_frameMs, 0.1f));
            ImGui::GetForegroundDrawList()->AddText(ImVec2(v.x + 10, v.y + 6), IM_COL32(170, 170, 170, 255), b);
        }
        drawToasts();
        return;
    }
    if (showGraph()) {
        drawGraphWorkspace();
    } else {
        if (m_playing) {
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            fg->AddRect(ImVec2(v.x + 1, v.y + 1), ImVec2(v.x + v.w - 1, v.y + v.h - 1), IM_COL32(0x3F, 0xB9, 0x50, 255), 0, 0, 2.5f);
            ImVec2 tl(v.x + v.w * 0.5f - 120, v.y + 10), br(tl.x + 240, tl.y + 26);
            fg->AddRectFilled(tl, br, IM_COL32(0x3F, 0xB9, 0x50, 230), 5);
            const char* tag = "PLAY  ·  F5 to stop";
            ImVec2 ts = ImGui::CalcTextSize(tag);
            fg->AddText(ImVec2(tl.x + (240 - ts.x) * 0.5f, tl.y + (26 - ts.y) * 0.5f), IM_COL32(8, 19, 11, 255), tag);
        }
        drawToolbar();
    }
    drawToasts();
}

// Floating bar at the bottom of the view, outside the graph editor.
void GameHost::drawToolbar() {
    const ScreenRect& v = m_app.m_rcView;
    float d = m_app.m_dpi;
    ImGui::SetNextWindowPos(ImVec2(v.x + v.w * 0.5f, v.y + v.h - 12 * d), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.85f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize;
    if (ImGui::Begin("##gamebar", nullptr, flags)) {
        if (ImGui::Button(m_playing ? "Stop  F5" : "Play  F5")) m_pendingToolbar = 1;
        ImGui::SameLine();
        if (ImGui::Button("Build  F7")) m_pendingToolbar = 3;
    }
    ImGui::End();
}

void GameHost::drawToasts() {
    uint64_t now = GetTickCount64();
    m_toasts.erase(std::remove_if(m_toasts.begin(), m_toasts.end(), [&](const Toast& t) { return now - t.t > 5000; }), m_toasts.end());
    const ScreenRect& v = m_app.m_rcView;
    float d = m_app.m_dpi;
    float y = v.y + (showGraph() ? 46 : 48) * d;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove;
    int i = 0;
    for (const Toast& t : m_toasts) {
        float a = clampf((5000.0f - (now - t.t)) / 600.0f, 0, 1);
        ImGui::SetNextWindowPos(ImVec2(v.x + v.w * 0.5f, y), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, a);
        ImGui::SetNextWindowBgAlpha(0.88f);
        std::string id = "##toast" + std::to_string(i++);
        if (ImGui::Begin(id.c_str(), nullptr, flags)) ImGui::TextUnformatted(toUtf8(t.text).c_str());
        ImGui::End();
        ImGui::PopStyleVar();
        y += 36 * d;
    }
}

void GameHost::afterDraw() {
    if (!m_shotReq) return;
    m_app.m_status.clear();
    m_app.saveScreenshot(m_shotWindow);   // window: true = whole window, UI included
    bool saved = !m_app.m_status.empty();
    m_shotReq->response = saved ? "{\"ok\": true, \"status\": " + jstr(toUtf8(m_app.m_status)) + ", \"dir\": " +
                                      jstr(toUtf8(m_app.m_exeDir + L"\\captures")) + "}"
                                : "{\"ok\": false, \"error\": \"capture impossible\"}";
    m_shotReq->status = saved ? 200 : 500;
    m_server.complete(m_shotReq);
    m_shotReq.reset();
}

// --------------------------------------------------------------------------- builds

std::vector<BuildProfile> GameHost::loadProfiles() {
    fs::path file = fs::path(m_gameDir) / "build_profiles.json";
    if (!fs::exists(file)) {
        std::ofstream f(file, std::ios::binary);
        f << "{\n  \"profiles\": [\n"
             "    {\"name\": \"windows-release\", \"title\": \"WZX Game\", \"width\": 1600, \"height\": 900, \"fullscreen\": false, "
             "\"pathTracing\": false, \"showFps\": false, \"commandApi\": false},\n"
             "    {\"name\": \"windows-debug\", \"title\": \"WZX Game (debug)\", \"width\": 1600, \"height\": 900, \"fullscreen\": false, "
             "\"pathTracing\": true, \"showFps\": true, \"commandApi\": true},\n"
             "    {\"name\": \"windows-fullscreen\", \"title\": \"WZX Game\", \"width\": 1920, \"height\": 1080, \"fullscreen\": true, "
             "\"pathTracing\": false, \"showFps\": false, \"commandApi\": false}\n"
             "  ]\n}\n";
    }
    std::vector<BuildProfile> out;
    Json j;
    if (Json::parse(readFile(file), j))
        for (const Json& p : j["profiles"].a) {
            BuildProfile b;
            b.name = p["name"].str(b.name);
            b.title = p["title"].str(b.title);
            b.width = (int)p["width"].num(b.width);
            b.height = (int)p["height"].num(b.height);
            b.fullscreen = p["fullscreen"].b;
            b.pathTracing = p["pathTracing"].b;
            b.showFps = p["showFps"].b;
            b.commandApi = p["commandApi"].b;
            out.push_back(b);
        }
    if (out.empty()) out.push_back(BuildProfile());
    return out;
}

bool GameHost::build(const std::string& profileName, bool run, std::wstring& outDir, std::string& err) {
    if (m_player) { err = "pas de build depuis un jeu exporté"; return false; }
    if (m_playing) stop();
    std::vector<BuildProfile> profiles = loadProfiles();
    const BuildProfile* bp = &profiles[0];
    if (!profileName.empty()) {
        bp = nullptr;
        for (const BuildProfile& p : profiles) if (p.name == profileName) bp = &p;
        if (!bp) { err = "profil inconnu : " + profileName; return false; }
    }
    std::string exeName;
    for (char c : bp->title) exeName.push_back(strchr("\\/:*?\"<>|", c) ? '_' : c);
    if (exeName.empty()) exeName = "Game";
    exeName += ".exe";

    fs::path out = fs::path(m_projectDir) / "builds" / fromUtf8(bp->name);
    fs::path exeDir(m_app.m_exeDir);
    std::error_code ec;
    fs::create_directories(out, ec);
    if (ec) { err = "création du dossier impossible : " + ec.message(); return false; }
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    fs::copy_file(self, out / fromUtf8(exeName), fs::copy_options::overwrite_existing, ec);
    if (ec) { err = "copie de l'exécutable impossible (le jeu exporté est-il ouvert ?) : " + ec.message(); return false; }
    fs::copy(exeDir / "shaders", out / "shaders", fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    if (ec) { err = "copie des shaders impossible : " + ec.message(); return false; }
    if (fs::exists(exeDir / "assets")) {
        fs::copy(exeDir / "assets", out / "assets", fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        if (ec) { err = "copie des assets impossible : " + ec.message(); return false; }
    }
    std::wstring savedStatus = m_app.m_status;
    if (!m_app.saveSceneFile((out / "scene.v3d.json").wstring())) { err = "écriture de la scène impossible"; return false; }
    m_app.m_status = savedStatus;
    if (!m_graph.save((out / "graph.json").wstring())) { err = "écriture du graphe impossible"; return false; }
    time_t t = time(nullptr);
    tm lt;
    localtime_s(&lt, &t);
    char date[64];
    strftime(date, sizeof date, "%Y-%m-%d %H:%M:%S", &lt);
    {
        std::ofstream f(out / "game.json", std::ios::binary);
        f << "{\n  \"format\": \"wzx-game\",\n  \"profile\": " << jstr(bp->name) << ",\n  \"title\": " << jstr(bp->title)
          << ",\n  \"built\": " << jstr(date) << ",\n  \"width\": " << bp->width << ",\n  \"height\": " << bp->height
          << ",\n  \"fullscreen\": " << (bp->fullscreen ? "true" : "false") << ",\n  \"pathTracing\": " << (bp->pathTracing ? "true" : "false")
          << ",\n  \"showFps\": " << (bp->showFps ? "true" : "false") << ",\n  \"commandApi\": " << (bp->commandApi ? "true" : "false")
          << ",\n  \"scene\": \"scene.v3d.json\",\n  \"graph\": \"graph.json\"\n}\n";
        if (!f) { err = "écriture de game.json impossible"; return false; }
    }
    outDir = out.wstring();
    m_lastBuild = toUtf8(outDir);
    if (run) ShellExecuteW(nullptr, L"open", (out / fromUtf8(exeName)).c_str(), nullptr, out.c_str(), SW_SHOWNORMAL);
    return true;
}

// --------------------------------------------------------------------------- command API

std::string GameHost::sceneJson() const {
    const Scene& s = m_app.m_scene;
    const Camera& c = s.camera;
    std::ostringstream js;
    js << "{\"ok\": true, \"scene\": " << jstr(s.name) << ", \"units\": \"cm\", \"playing\": " << (m_playing ? "true" : "false")
       << ", \"selected\": " << m_app.m_selected << ",\n \"camera\": {\"target\": " << vec(c.target) << ", \"yaw\": " << num(degrees(c.yaw))
       << ", \"pitch\": " << num(degrees(c.pitch)) << ", \"distance\": " << num(c.distance) << "},\n \"parts\": [";
    for (size_t i = 0; i < parts().size(); ++i) js << (i ? ", " : "") << jstr(parts()[i].id);
    js << "],\n \"objects\": [";
    for (size_t i = 0; i < s.objects.size(); ++i) {
        const Object& o = s.objects[i];
        const Material& m = s.materials[o.material];
        float4 q = m_app.objRot(o);
        float3 xa = qRot(q, float3(1, 0, 0));
        js << (i ? ",\n  " : "\n  ") << "{\"index\": " << i << ", \"name\": " << jstr(o.name) << ", \"part\": " << jstr(o.part)
           << ", \"pos\": " << vec(o.position) << ", \"yaw\": " << num(degrees(std::atan2(-xa.z, xa.x))) << ", \"scale\": " << vec(o.scale)
           << ", \"color\": " << vec(float3(linToSrgb1(m.baseColor.x), linToSrgb1(m.baseColor.y), linToSrgb1(m.baseColor.z)))
           << ", \"visible\": " << (o.visible ? "true" : "false") << "}";
    }
    js << "\n ]}";
    return js.str();
}

int GameHost::resolveObject(const Json& t) const {
    if (t.type == Json::Number) {
        int i = (int)t.n;
        return i >= 0 && i < objectCount() ? i : -1;
    }
    if (t.type == Json::String)
        for (int i = 0; i < objectCount(); ++i) if (m_app.m_scene.objects[i].name == t.s) return i;
    return -1;
}

static const char* kHelp =
    "{\"ok\": true, \"usage\": \"POST /cmd avec un objet JSON {\\\"cmd\\\": ..., ...}. GET /scene = scene.get.\", \"commands\": {"
    "\"state\": \"{}\", "
    "\"scene.get\": \"{} -> objets, camera, selection\", "
    "\"scene.new\": \"{} -> scene Studio vide\", "
    "\"scene.save\": \"{path}\", \"scene.load\": \"{path}\", "
    "\"object.add\": \"{part: CUBE|SPHERE|BALL, pos:[x,y,z], yaw?, color?:[r,g,b] 0-1, name?} -> index\", "
    "\"object.set\": \"{target: index|nom, pos?, yaw?, rotate?, scale?, color?, name?, visible?}\", "
    "\"object.delete\": \"{target}\", \"object.select\": \"{target|null}\", "
    "\"camera.set\": \"{target?:[x,y,z], yaw?, pitch?, distance?}\", "
    "\"play\": \"{}\", \"stop\": \"{}\", "
    "\"input.key\": \"{key: R|ESPACE|...} (pendant Play)\", \"input.click\": \"{target} ou {pos:[x,y,z]} (pendant Play)\", "
    "\"graph.get\": \"{}\", \"graph.set\": \"{graph: {...}}\", \"graph.clear\": \"{}\", \"graph.nodeTypes\": \"{}\", "
    "\"graph.addNode\": \"{type, x?, y?, in?: {broche: valeur}} -> id\", \"graph.removeNode\": \"{id}\", "
    "\"graph.connect\": \"{from:[id, sortie], to:[id, entree]} (nom ou index de broche)\", "
    "\"graph.disconnect\": \"{id, pin, output}\", \"graph.setInput\": \"{id, pin, value}\", "
    "\"graph.save\": \"{}\", \"graph.load\": \"{}\", \"graph.show\": \"{open: true|false, output?: 0|1|2}\", \"graph.compile\": \"{} -> diagnostics\", "
    "\"build\": \"{profile?, run?} -> dossier\", \"build.profiles\": \"{}\", "
    "\"screenshot\": \"{window?: true = fenetre entiere} -> fichier PNG\", \"log\": \"{} -> journal du graphe\"}}";

void GameHost::handleRequests() {
    for (int guard = 0; guard < 16; ++guard) {
        std::shared_ptr<CommandRequest> r = m_server.next();
        if (!r) return;
        int status = 200;
        std::string path = r->path.substr(0, r->path.find('?'));
        if (path == "/" || path == "/help") {
            r->response = kHelp;
        } else if (path == "/scene") {
            r->response = sceneJson();
        } else if (path == "/cmd") {
            Json j;
            if (!Json::parse(r->body, j) || j.type != Json::Object) {
                status = 400;
                r->response = "{\"ok\": false, \"error\": \"corps JSON invalide\"}";
            } else if (j["cmd"].str() == "screenshot") {
                // taken in afterDraw(), once this frame (UI included) is complete
                m_shotReq = r;
                m_shotWindow = j["window"].b;
                continue;
            } else {
                r->response = command(j, status);
            }
        } else {
            status = 404;
            r->response = "{\"ok\": false, \"error\": \"utiliser POST /cmd, GET /scene ou GET /help\"}";
        }
        r->status = status;
        m_server.complete(r);
    }
}

std::string GameHost::command(const Json& j, int& status) {
    App& a = m_app;
    Scene& s = a.m_scene;
    std::string cmd = j["cmd"].str();
    auto fail = [&](const std::string& e) { status = 400; return "{\"ok\": false, \"error\": " + jstr(e) + "}"; };
    auto ok = [](const std::string& extra = "") { return "{\"ok\": true" + extra + "}"; };
    auto pathArg = [&](const char* def) {
        fs::path p = fromUtf8(j["path"].str(def));
        if (p.is_relative()) p = fs::path(m_projectDir) / p;
        return p;
    };

    if (cmd == "help") return kHelp;
    if (cmd == "state")
        return ok(", \"playing\": " + std::string(m_playing ? "true" : "false") + ", \"graphOpen\": " + (showGraph() ? "true" : "false") +
                  ", \"selected\": " + std::to_string(a.m_selected) + ", \"objects\": " + std::to_string(s.objects.size()) +
                  ", \"graphNodes\": " + std::to_string(m_graph.nodes.size()) + ", \"graphPath\": " + jstr(toUtf8(m_graphPath)) +
                  ", \"projectDir\": " + jstr(toUtf8(m_projectDir)) + ", \"lastBuild\": " + jstr(m_lastBuild) +
                  ", \"mouse\": [" + num(ImGui::GetIO().MousePos.x / m_app.m_dpi) + ", " + num(ImGui::GetIO().MousePos.y / m_app.m_dpi) + "], \"viewport\": [" + num(m_app.m_rcView.x) + ", " + num(m_app.m_rcView.y) + ", " + num(m_app.m_rcView.w) + ", " + num(m_app.m_rcView.h) + "]");
    if (cmd == "scene.get") return sceneJson();
    if (cmd == "scene.new") {
        if (m_playing) stop();
        a.loadScene(SCENE_STUDIO);
        return ok();
    }
    if (cmd == "scene.save") {
        fs::path p = pathArg("game/scene.v3d.json");
        if (!a.saveSceneFile(p.wstring())) return fail("écriture impossible : " + toUtf8(p.wstring()));
        return ok(", \"path\": " + jstr(toUtf8(p.wstring())));
    }
    if (cmd == "scene.load") {
        if (m_playing) stop();
        fs::path p = pathArg("game/scene.v3d.json");
        if (!a.loadSceneFile(p.wstring())) return fail("chargement impossible : " + toUtf8(p.wstring()));
        return ok(", \"objects\": " + std::to_string(s.objects.size()));
    }
    if (cmd == "object.add") {
        std::string part = j["part"].str("CUBE");
        const PartDef* p = findPart(part);
        if (!p) return fail("type inconnu : " + part + " (CUBE, SPHERE, BALL)");
        float3 pos(0, 0, 0);
        readVec(j["pos"], pos);
        if (!m_playing) a.pushUndo();
        int idx = addPart(s, p->id, p->defaultColor, -1, pos, (float)j["yaw"].num(0));
        Object& o = s.objects[idx];
        o.name.clear();
        o.name = uniqueName(j["name"].str(toUtf8(p->name)));
        o.rot = qAxisAngle(float3(0, 1, 0), o.yaw);
        o.useRot = true;
        float3 c;
        if (readVec(j["color"], c)) setColor(idx, c);
        a.markDirty();
        return ok(", \"index\": " + std::to_string(idx) + ", \"name\": " + jstr(o.name));
    }
    if (cmd == "object.set") {
        int i = resolveObject(j["target"]);
        if (i < 0) return fail("objet introuvable");
        if (!m_playing) a.pushUndo();
        Object& o = s.objects[i];
        float3 v;
        if (readVec(j["pos"], v)) o.position = v;
        if (j["yaw"].type == Json::Number) {
            o.yaw = radians((float)j["yaw"].n);
            o.rot = qAxisAngle(float3(0, 1, 0), o.yaw);
            o.useRot = true;
        }
        if (j["rotate"].type == Json::Number) rotateY(i, (float)j["rotate"].n);
        if (readVec(j["scale"], v)) o.scale = v;
        else if (j["scale"].type == Json::Number) o.scale = float3((float)j["scale"].n);
        if (readVec(j["color"], v)) setColor(i, v);
        if (j["name"].type == Json::String) o.name = j["name"].s;
        if (j["visible"].type == Json::Bool) o.visible = j["visible"].b;
        a.markDirty();
        return ok(", \"index\": " + std::to_string(i));
    }
    if (cmd == "object.delete") {
        int i = resolveObject(j["target"]);
        if (i < 0) return fail("objet introuvable");
        if (m_playing) { destroy(i); return ok(); }
        a.pushUndo();
        s.objects.erase(s.objects.begin() + i);
        a.m_selected = -1;
        a.m_multi.clear();
        a.markDirty();
        return ok();
    }
    if (cmd == "object.select") {
        int i = resolveObject(j["target"]);
        a.m_selected = i;
        a.m_multi.clear();
        a.markDirty();
        return ok(", \"selected\": " + std::to_string(i));
    }
    if (cmd == "camera.set") {
        Camera& c = s.camera;
        float3 v;
        if (readVec(j["target"], v)) c.target = v;
        if (j["yaw"].type == Json::Number) c.yaw = radians((float)j["yaw"].n);
        if (j["pitch"].type == Json::Number) c.pitch = radians(clampf((float)j["pitch"].n, -89, 89));
        if (j["distance"].type == Json::Number) c.distance = clampf((float)j["distance"].n, 0.5f, 2000);
        a.cameraChanged();
        return ok();
    }
    if (cmd == "play") { play(); return ok(", \"playing\": true"); }
    if (cmd == "stop") { stop(); return ok(", \"playing\": false"); }
    if (cmd == "input.key") {
        if (!m_playing) return fail("Play n'est pas lancé");
        std::string k = j["key"].str();
        for (char& c : k) if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        m_rt.fireKey(k);
        return ok();
    }
    if (cmd == "input.click") {
        if (!m_playing) return fail("Play n'est pas lancé");
        float3 p;
        if (j["target"].type != Json::Null) {
            int i = resolveObject(j["target"]);
            if (i < 0) return fail("objet introuvable");
            m_rt.fireClickObject(i, s.objects[i].position);
        } else if (readVec(j["pos"], p)) {
            m_rt.fireClickGround(p);
        } else {
            return fail("préciser target ou pos");
        }
        return ok();
    }
    // graph
    if (cmd == "graph.get") return "{\"ok\": true, \"path\": " + jstr(toUtf8(m_graphPath)) + ", \"graph\": " + m_graph.toJson() + "}";
    if (cmd == "graph.nodeTypes") {
        std::ostringstream js;
        js << "{\"ok\": true, \"types\": [";
        bool first = true;
        for (const NodeDef& d : nodeDefs()) {
            js << (first ? "\n  " : ",\n  ") << "{\"type\": " << jstr(d.id) << ", \"label\": " << jstr(d.label) << ", \"category\": "
               << jstr(d.category) << ", \"help\": " << jstr(d.help) << ", \"in\": [";
            for (size_t i = 0; i < d.ins.size(); ++i)
                js << (i ? ", " : "") << "{\"name\": " << jstr(d.ins[i].name) << ", \"type\": \"" << pinTypeName(d.ins[i].type) << "\"}";
            js << "], \"out\": [";
            for (size_t i = 0; i < d.outs.size(); ++i)
                js << (i ? ", " : "") << "{\"name\": " << jstr(d.outs[i].name) << ", \"type\": \"" << pinTypeName(d.outs[i].type) << "\"}";
            js << "]}";
            first = false;
        }
        js << "\n]}";
        return js.str();
    }
    if (cmd == "graph.set" || cmd == "graph.clear" || cmd == "graph.load") {
        if (m_playing) stop();
        std::string err;
        if (cmd == "graph.clear") m_graph.clear();
        else if (cmd == "graph.load") { if (!loadGraph(&err)) return fail(err); }
        else {
            // re-serialise the "graph" member (the parser keeps no source text)
            const Json& g = j["graph"];
            if (g.type != Json::Object) return fail("champ graph manquant");
            Graph tmp;
            std::ostringstream js;
            std::function<void(const Json&)> write = [&](const Json& v) {
                switch (v.type) {
                case Json::Null: js << "null"; break;
                case Json::Bool: js << (v.b ? "true" : "false"); break;
                case Json::Number: js << num((float)v.n); break;
                case Json::String: js << jstr(v.s); break;
                case Json::Array: { js << "["; for (size_t i = 0; i < v.a.size(); ++i) { if (i) js << ","; write(v.a[i]); } js << "]"; break; }
                case Json::Object: {
                    js << "{";
                    bool f = true;
                    for (auto& kv : v.o) { if (!f) js << ","; js << jstr(kv.first) << ":"; write(kv.second); f = false; }
                    js << "}";
                    break;
                }
                }
            };
            write(g);
            if (!tmp.fromJson(js.str(), &err)) return fail(err);
            m_graph = tmp;
        }
        m_editor.frameAll(m_graph);
        return ok(", \"nodes\": " + std::to_string(m_graph.nodes.size()));
    }
    if (cmd == "graph.addNode") {
        std::string type = j["type"].str();
        int id = m_graph.addNode(type, float2((float)j["x"].num(0), (float)j["y"].num(0)));
        if (!id) return fail("type de noeud inconnu : " + type + " (voir graph.nodeTypes)");
        Node* n = m_graph.node(id);
        for (auto& kv : j["in"].o) {
            int pi = n->def->findIn(kv.first);
            if (pi < 0) continue;
            Value& v = n->inputs[pi];
            const Json& x = kv.second;
            if (x.type == Json::Number) { v.f = (float)x.n; v.b = x.n != 0; v.v = float3((float)x.n); v.s = num((float)x.n); }
            else if (x.type == Json::Bool) { v.b = x.b; v.f = x.b ? 1.0f : 0.0f; }
            else if (x.type == Json::String) v.s = x.s;
            else readVec(x, v.v);
        }
        return ok(", \"id\": " + std::to_string(id));
    }
    if (cmd == "graph.removeNode") {
        int id = (int)j["id"].num(0);
        if (!m_graph.node(id)) return fail("noeud introuvable");
        m_graph.removeNode(id);
        return ok();
    }
    auto pinIndex = [&](const Node* n, const Json& p, bool output) -> int {
        if (p.type == Json::Number) return (int)p.n;
        return output ? n->def->findOut(p.str()) : n->def->findIn(p.str());
    };
    if (cmd == "graph.connect") {
        int a1 = (int)j["from"][0].num(0), b1 = (int)j["to"][0].num(0);
        const Node* na = m_graph.node(a1);
        const Node* nb = m_graph.node(b1);
        if (!na || !nb) return fail("noeud introuvable");
        int pa = pinIndex(na, j["from"][1], true), pb = pinIndex(nb, j["to"][1], false);
        std::string err;
        if (!m_graph.connect(a1, pa, b1, pb, &err)) return fail(err);
        return ok();
    }
    if (cmd == "graph.disconnect") {
        int id = (int)j["id"].num(0);
        const Node* n = m_graph.node(id);
        if (!n) return fail("noeud introuvable");
        bool out = j["output"].b;
        int p = pinIndex(n, j["pin"], out);
        if (p < 0) return fail("broche introuvable");
        m_graph.disconnectPin(id, p, out);
        return ok();
    }
    if (cmd == "graph.setInput") {
        Node* n = m_graph.node((int)j["id"].num(0));
        if (!n) return fail("noeud introuvable");
        int p = pinIndex(n, j["pin"], false);
        if (p < 0 || p >= (int)n->inputs.size()) return fail("broche introuvable");
        Value& v = n->inputs[p];
        const Json& x = j["value"];
        switch (v.type) {
        case PinType::Float: v.f = (float)x.num(v.f); break;
        case PinType::Bool: v.b = x.type == Json::Bool ? x.b : x.num(0) != 0; break;
        case PinType::Vec3: if (!readVec(x, v.v) && x.type == Json::Number) v.v = float3((float)x.n); break;
        case PinType::String: v.s = x.type == Json::String ? x.s : num((float)x.num()); break;
        default: return fail("cette broche n'a pas de valeur");
        }
        ++m_graph.version;
        return ok();
    }
    if (cmd == "graph.save") {
        std::string err;
        if (!saveGraph(&err)) return fail(err);
        return ok(", \"path\": " + jstr(toUtf8(m_graphPath)));
    }
    if (cmd == "graph.show") {
        setGraphOpen(j["open"].type == Json::Bool ? j["open"].b : true);
        if (j["output"].type == Json::Number) m_editor.setOutputTab((int)j["output"].num());   // 0 Output, 1 Compiler Results, 2 Execution Trace
        return ok();
    }
    if (cmd == "graph.compile") {
        compileGraph();
        return ok();
    }
    // build / output
    if (cmd == "build.profiles") {
        std::ostringstream js;
        js << "{\"ok\": true, \"file\": " << jstr(toUtf8((fs::path(m_gameDir) / "build_profiles.json").wstring())) << ", \"profiles\": [";
        auto ps = loadProfiles();
        for (size_t i = 0; i < ps.size(); ++i)
            js << (i ? ", " : "") << "{\"name\": " << jstr(ps[i].name) << ", \"title\": " << jstr(ps[i].title) << ", \"fullscreen\": "
               << (ps[i].fullscreen ? "true" : "false") << ", \"pathTracing\": " << (ps[i].pathTracing ? "true" : "false")
               << ", \"showFps\": " << (ps[i].showFps ? "true" : "false") << ", \"commandApi\": " << (ps[i].commandApi ? "true" : "false") << "}";
        js << "]}";
        return js.str();
    }
    if (cmd == "build") {
        std::wstring out;
        std::string err;
        if (!build(j["profile"].str(), j["run"].b, out, err)) return fail(err);
        toast(L"Build finished: " + out);
        return ok(", \"path\": " + jstr(toUtf8(out)));
    }
    if (cmd == "log") {
        std::ostringstream js;
        js << "{\"ok\": true, \"log\": [";
        for (size_t i = 0; i < m_rt.log.size(); ++i) js << (i ? ", " : "") << jstr(m_rt.log[i]);
        js << "], \"variables\": {";
        bool f = true;
        for (auto& kv : m_rt.variables()) { js << (f ? "" : ", ") << jstr(kv.first) << ": " << valueToJson(kv.second); f = false; }
        js << "}}";
        return js.str();
    }
    return fail("commande inconnue : " + cmd + " (GET /help pour la liste)");
}

} // namespace app
