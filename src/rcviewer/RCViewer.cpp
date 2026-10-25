// RC Viewer: a small standalone 3D viewer to test the Radiance Cascades GI of vp3d (RenderSettings::giMethod = 1)
// against DDGI and the path traced reference, with the usual inspection views. One window: the settings panel on
// the left, the 3D view on the right (orbit / pan / zoom). Built by build.bat next to the editor (RCViewer.exe).
//
// Command line: --scene cornell|lookdev|atrium  --mode 0..6  --view 0|1|2  --probes N  --cascades N  --dirres N
//               --interval x  --hdri file|sky  --size WxH  --shot file.png [--frames N]  --anim 1
//               (modes: 0 RC écran, 1 RC grille, 2 RC ray tracé, 3 DDGI raster, 4 DDGI ray tracé, 5 path, 6 raster)
//               --show-probes 1  --camview N  --hysteresis x  --bilinear 0|1  --sspacing N  --sdirres N  --sinterval x  --lsamples N  --shot-window file.png
#include "app/Content.h"
#include "app/ImGuiLayer.h"
#include "backends/imgui_impl_win32.h"
#include "vp3d/EnvImage.h"
#include "vp3d/MeshGen.h"
#include "vp3d/Renderer.h"
#include "vp3d/shaders/Shared.h"
#include <commdlg.h>
#include <shellapi.h>
#include <wincodec.h>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

using namespace vp3d;

namespace {

enum SceneId { SC_CORNELL = 0, SC_LOOKDEV = 1, SC_ATRIUM = 2, SC_COUNT };
const char* kSceneNames[SC_COUNT] = {"Cornell Box", "LookDev (HDRI)", "Atrium (soleil)"};

// Render modes of the viewer (how RenderSettings is set up, see applyMode).
enum Mode { MODE_RCS_RASTER = 0, MODE_RC_RASTER, MODE_RC_TRACED, MODE_DDGI_RASTER, MODE_DDGI_TRACED, MODE_PATH, MODE_RASTER, MODE_COUNT };
const char* kModeNames[MODE_COUNT] = {
    "RC écran (sondes sur les surfaces) + raster",
    "RC grille 3D + raster (sans temporel)",
    "Radiance Cascades + ombrage ray tracé",
    "DDGI + raster (comparaison)",
    "DDGI + ombrage ray tracé (comparaison)",
    "Path tracing (référence)",
    "Raster sans GI",
};

struct Options {
    int scene = SC_CORNELL;
    int mode = MODE_RCS_RASTER;
    int view = 0;
    int probes = -1, cascades = -1, dirRes = -1;
    float interval = -1;
    std::wstring hdri;
    int width = 1600, height = 900;
    std::wstring shot;
    bool shotWindow = false;   // --shot-window: the whole window instead of the 3D view
    int frames = 60;
    bool anim = false;
    bool showProbes = false;
    int camView = -1;
    float hysteresis = -1;
    int bilinear = -1;
    int sSpacing = -1, sDirRes = -1;
    float sInterval = -1;
    int lightSamples = -1;
};

bool savePng(const std::wstring& path, std::vector<uint8_t> rgba, uint32_t w, uint32_t h) {
    for (size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 255;
    ComPtr<IWICImagingFactory> f;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) return false;
    ComPtr<IWICStream> st;
    f->CreateStream(&st);
    if (FAILED(st->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) return false;
    ComPtr<IWICBitmapEncoder> enc;
    f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
    enc->Initialize(st.Get(), WICBitmapEncoderNoCache);
    ComPtr<IWICBitmapFrameEncode> fr;
    ComPtr<IPropertyBag2> props;
    enc->CreateNewFrame(&fr, &props);
    fr->Initialize(props.Get());
    fr->SetSize(w, h);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppRGBA;
    fr->SetPixelFormat(&fmt);
    if (fmt != GUID_WICPixelFormat32bppRGBA)
        for (size_t i = 0; i + 3 < rgba.size(); i += 4) std::swap(rgba[i], rgba[i + 2]);
    fr->WritePixels(h, w * 4, (UINT)rgba.size(), rgba.data());
    fr->Commit();
    enc->Commit();
    return true;
}

void listEnvFiles(const std::wstring& dir, bool hdrOnly, std::vector<std::wstring>& out) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring p = dir + L"\\" + fd.cFileName;
        if (!isEnvImageFile(p)) continue;
        std::wstring e = p.substr(p.find_last_of(L'.'));
        for (auto& c : e) c = (wchar_t)towlower(c);
        if (hdrOnly && e != L".hdr" && e != L".exr") continue;
        out.push_back(p);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

std::string fileName(const std::wstring& p) {
    size_t s = p.find_last_of(L"\\/");
    return app::toUtf8(s == std::wstring::npos ? p : p.substr(s + 1));
}

// A long closed hall lit by the sun through a slit in the roof, with pillars and coloured banners: almost all the
// light of the hall is bounced light (the Sponza-like case Radiance Cascades are made for).
void buildAtrium(Scene& s) {
    s = Scene();
    s.name = "Atrium";
    s.unitsPerMeter = 100.0f;
    s.gridScale = 5.0f;
    s.env.top = float3(0.32f, 0.48f, 0.85f);
    s.env.horizon = float3(0.60f, 0.70f, 0.85f);
    s.env.bottom = float3(0.10f, 0.09f, 0.08f);
    s.env.intensity = 1.0f;
    s.env.ambient = float3(0.05f, 0.05f, 0.06f);

    auto mat = [&](const char* n, float3 c, float rough) {
        Material m;
        m.name = n;
        m.baseColor = c;
        app::applyPreset(m, app::PRESET_MATTE, 1.0f);
        m.roughness = rough;
        return s.addMaterial(m);
    };
    const int stone = mat("Stone", float3(0.62f, 0.58f, 0.50f), 0.9f);
    const int floorM = mat("Floor", float3(0.45f, 0.42f, 0.38f), 0.8f);
    const int red = mat("Red Banner", float3(0.65f, 0.06f, 0.04f), 0.95f);
    const int green = mat("Green Banner", float3(0.08f, 0.40f, 0.10f), 0.95f);
    const int blue = mat("Blue Banner", float3(0.06f, 0.12f, 0.50f), 0.95f);

    const float L = 60.0f, W = 12.0f, H = 30.0f, T = 1.0f, slit = 3.0f;
    auto box = [&](const char* name, float3 a, float3 b, int m) {
        Object o;
        o.name = name;
        o.part = "WALL";
        o.mesh = s.addMesh(name, makeBox(a, b));
        o.material = m;
        s.addObject(o);
    };
    box("Floor", {-W - T, -T, -L - T}, {W + T, 0, L + T}, floorM);
    box("Left Wall", {-W - T, 0, -L}, {-W, H, L}, stone);
    box("Right Wall", {W, 0, -L}, {W + T, H, L}, stone);
    box("Back Wall", {-W, 0, -L - T}, {W, H, -L}, stone);
    box("Front Wall", {-W, 0, L}, {W, H, L + T}, stone);
    box("Roof Left", {-W - T, H, -L - T}, {-slit, H + T, L + T}, stone);
    box("Roof Right", {slit, H, -L - T}, {W + T, H + T, L + T}, stone);
    // galleries: two rows of pillars and an architrave over each row
    for (int i = 0; i < 8; ++i) {
        float z = -L + 10.0f + i * 14.3f;
        box("Pillar L", {-8.5f, 0, z - 1.2f}, {-6.1f, 20.0f, z + 1.2f}, stone);
        box("Pillar R", {6.1f, 0, z - 1.2f}, {8.5f, 20.0f, z + 1.2f}, stone);
    }
    box("Architrave L", {-W, 20.0f, -L}, {-6.1f, 22.0f, L}, stone);
    box("Architrave R", {6.1f, 20.0f, -L}, {W, 22.0f, L}, stone);
    // banners hanging from the architraves, between the pillars
    const int bannerMats[3] = {red, green, blue};
    for (int i = 0; i < 7; ++i) {
        float z = -L + 17.15f + i * 14.3f;
        box("Banner L", {-6.6f, 8.0f, z - 4.5f}, {-6.3f, 20.0f, z + 4.5f}, bannerMats[i % 3]);
        box("Banner R", {6.3f, 8.0f, z - 4.5f}, {6.6f, 20.0f, z + 4.5f}, bannerMats[(i + 1) % 3]);
    }
    // a few objects on the floor
    auto ball = [&](const char* name, float3 pos, float r, float3 c, int preset, float rough) {
        Material m;
        m.name = name;
        m.baseColor = c;
        app::applyPreset(m, preset, 1.0f);
        m.roughness = rough;
        Object o;
        o.name = name;
        o.part = "SPHERE";
        o.mesh = s.addMesh(name, makeSphere(r, 64, 32));
        o.material = s.addMaterial(m);
        o.position = pos;
        s.addObject(o);
    };
    ball("White Ball", {-2.5f, 3.0f, 8.0f}, 3.0f, float3(0.8f, 0.8f, 0.78f), app::PRESET_MATTE, 0.9f);
    ball("Gold Ball", {2.5f, 2.2f, 2.0f}, 2.2f, float3(1.0f, 0.75f, 0.35f), app::PRESET_CHROME, 0.25f);
    ball("Red Ball", {0.0f, 1.6f, 16.0f}, 1.6f, float3(0.7f, 0.08f, 0.05f), app::PRESET_PLASTIC, 0.4f);

    s.sun.enabled = true;
    s.sun.direction = normalize(float3(0.2f, 1.0f, 0.1f));   // towards the sun: a strip of sunlight on the floor
    s.sun.color = float3(1.0f, 0.92f, 0.80f);
    s.sun.intensity = 8.0f;

    Camera& c = s.camera;
    c.target = {0.0f, 11.0f, 0.0f};   // from the front wall, down the hall
    c.yaw = 0.0f;
    c.pitch = radians(4.0f);
    c.distance = 54.0f;
    c.fovY = radians(55.0f);
    c.dof = false;
    c.focusDistance = 54.0f;
    c.sensorScale = s.unitsPerMeter;
}

class Viewer {
public:
    bool init(HINSTANCE inst, const Options& opt);
    int run();
    LRESULT wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

private:
    void loadScene(int id);
    bool setHdri(const std::wstring& path);
    void applyMode();
    void frame();
    void ui(float panelW);
    void cameraInput(const ImVec2& vpMin, const ImVec2& vpMax);
    void animate(float dt);

    HWND m_hwnd = nullptr;
    float m_dpi = 1.0f;
    bool m_running = true, m_minimized = false;
    std::wstring m_exeDir;
    Options m_opt;
    Renderer m_r;
    app::ImGuiLayer m_gui;
    Scene m_scene;
    Scene m_base;          // the scene as built (animation reference)
    RenderSettings m_rs;
    int m_sceneId = SC_CORNELL;
    int m_mode = MODE_RCS_RASTER;
    std::vector<app::ViewPreset> m_views;
    std::vector<std::wstring> m_hdris;
    std::wstring m_hdriPath;
    bool m_anim = false;
    float m_animTime = 0.0f;
    uint64_t m_lastTick = 0;
    int m_frame = 0;
    float m_fps = 0.0f;
    ViewRect m_vp;
};

Viewer* g_viewer = nullptr;

LRESULT CALLBACK StaticWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (g_viewer) return g_viewer->wndProc(h, m, w, l);
    return DefWindowProcW(h, m, w, l);
}

bool Viewer::init(HINSTANCE inst, const Options& opt) {
    g_viewer = this;
    m_opt = opt;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    m_exeDir = exe;
    m_exeDir = m_exeDir.substr(0, m_exeDir.find_last_of(L"\\/"));
    SetCurrentDirectoryW(m_exeDir.c_str());

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = StaticWndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    wc.lpszClassName = L"WzxRCViewer";
    RegisterClassExW(&wc);
    UINT sysDpi = GetDpiForSystem();
    m_dpi = sysDpi / 96.0f;
    RECT r = {0, 0, (LONG)(opt.width * m_dpi), (LONG)(opt.height * m_dpi)};
    AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, sysDpi);
    m_hwnd = CreateWindowExW(0, wc.lpszClassName, L"WZX — Radiance Cascades Viewer", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                             CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, nullptr);
    if (!m_hwnd) return false;
    m_dpi = GetDpiForWindow(m_hwnd) / 96.0f;
    RECT cr;
    GetClientRect(m_hwnd, &cr);
    std::string err;
    if (!m_r.init(m_hwnd, cr.right - cr.left, cr.bottom - cr.top, m_exeDir + L"\\shaders", &err)) {
        MessageBoxA(m_hwnd, err.c_str(), "RC Viewer - DirectX 12", MB_ICONERROR);
        return false;
    }
    if (!m_gui.init(m_hwnd, m_r, m_dpi)) {
        MessageBoxA(m_hwnd, "Dear ImGui initialisation failed", "RC Viewer", MB_ICONERROR);
        return false;
    }
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 0.0f;
    st.FrameRounding = 3.0f;
    st.GrabRounding = 3.0f;
    st.ScaleAllSizes(m_dpi);

    listEnvFiles(m_exeDir + L"\\..\\..\\01 - INBOX", true, m_hdris);
    listEnvFiles(m_exeDir + L"\\assets\\hdri", false, m_hdris);
    if (m_hdris.empty()) {   // built in a worktree (_worktrees\<name>\build\bin): the HDRIs of the main checkout
        listEnvFiles(m_exeDir + L"\\..\\..\\..\\..\\01 - INBOX", true, m_hdris);
        listEnvFiles(m_exeDir + L"\\..\\..\\..\\..\\assets\\hdri", false, m_hdris);
    }

    // Path tracing quality of the editor's "high" preset (reference mode).
    m_rs.maxBounces = 10;
    m_rs.maxSamples = 4096;
    m_rs.bevelSamples = 4;
    m_rs.denoise = false;
    m_rs.rcProbeBudget = opt.probes > 0 ? opt.probes : 8192;
    if (opt.cascades > 0) m_rs.rcCascades = opt.cascades;
    if (opt.dirRes > 0) m_rs.rcDirRes = opt.dirRes;
    if (opt.interval > 0) m_rs.rcInterval = opt.interval;
    m_rs.ddgiView = opt.view;
    m_mode = opt.mode;
    m_anim = opt.anim;
    m_rs.ddgiShowProbes = opt.showProbes;
    if (opt.hysteresis >= 0) m_rs.rcHysteresis = opt.hysteresis;
    if (opt.bilinear >= 0) m_rs.rcBilinearFix = opt.bilinear != 0;
    if (opt.sSpacing > 0) m_rs.rcScreenSpacing = opt.sSpacing;
    if (opt.sDirRes > 0) m_rs.rcScreenDirRes = opt.sDirRes;
    if (opt.sInterval > 0) m_rs.rcScreenInterval = opt.sInterval;
    if (opt.lightSamples > 0) m_rs.rcLightSamples = opt.lightSamples;
    applyMode();
    ShowWindow(m_hwnd, SW_SHOWDEFAULT);
    UpdateWindow(m_hwnd);
    loadScene(opt.scene);
    if (!opt.hdri.empty()) setHdri(opt.hdri);
    if (opt.camView >= 0 && opt.camView < (int)m_views.size()) m_scene.camera = m_views[opt.camView].cam;
    m_lastTick = GetTickCount64();
    return true;
}

void Viewer::applyMode() {
    m_rs.raster = m_mode == MODE_RASTER;
    m_rs.ddgi = m_mode <= MODE_DDGI_TRACED;
    m_rs.giMethod = (m_mode == MODE_RCS_RASTER || m_mode == MODE_RC_RASTER || m_mode == MODE_RC_TRACED) ? 1 : 0;
    m_rs.rcScreen = m_mode == MODE_RCS_RASTER;
    m_rs.ddgiRaster = false;   // probes always ray traced here (Radiance Cascades need ray tracing)
    m_rs.ddgiRasterView = m_mode == MODE_RCS_RASTER || m_mode == MODE_RC_RASTER || m_mode == MODE_DDGI_RASTER;
    m_r.resetAccumulation();
}

void Viewer::loadScene(int id) {
    m_sceneId = id;
    if (id == SC_LOOKDEV) app::buildLookDevScene(m_scene);
    else if (id == SC_ATRIUM) buildAtrium(m_scene);
    else {
        app::buildCornellBox(m_scene);
        m_scene.env.ambient = float3(0.16f, 0.15f, 0.13f);
        m_scene.camera.dof = false;   // a GI viewer: the whole box in focus
    }
    m_rs.vignette = id == SC_CORNELL ? 0.10f : 0.18f;
    m_hdriPath.clear();
    if (id == SC_LOOKDEV) setHdri(m_hdris.empty() ? std::wstring(L"sky") : m_hdris.front());
    m_views = app::defaultViews(m_scene, id == SC_CORNELL);
    for (auto& v : m_views) v.cam.dof = false;
    m_base = m_scene;
    m_animTime = 0.0f;
    m_r.setScene(m_scene);
    m_r.resetAccumulation();
}

bool Viewer::setHdri(const std::wstring& path) {
    auto e = std::make_shared<EnvImage>();
    if (path == L"sky") {
        makeProceduralSky(*e);
    } else {
        std::string err;
        if (!loadEnvImage(path, *e, &err)) return false;
    }
    m_scene.env.hdri = e;
    m_hdriPath = path;
    if (m_sceneId == SC_LOOKDEV) m_scene.env.hdriRotation = app::lookDevHdriRotation(*e);
    m_base.env = m_scene.env;
    m_r.resetAccumulation();
    return true;
}

// Moving light: the Cornell ceiling light slides from side to side, the HDRI turns, the sun of the atrium turns.
// Radiance Cascades follow at once (nothing is accumulated); DDGI lags behind its hysteresis.
void Viewer::animate(float dt) {
    if (!m_anim) return;
    m_animTime += dt;
    const float t = m_animTime;
    if (m_sceneId == SC_CORNELL && !m_scene.lights.empty()) {
        const AreaLight& b = m_base.lights[0];
        float dx = 9.0f * std::sin(t * 0.8f);
        m_scene.lights[0].position = b.position + float3(dx, 0, 0);
        m_scene.lights[0].target = b.target + float3(dx, 0, 0);
    } else if (m_sceneId == SC_LOOKDEV) {
        m_scene.env.hdriRotation = m_base.env.hdriRotation + t * 0.35f;
    } else if (m_sceneId == SC_ATRIUM) {
        float a = std::sin(t * 0.5f) * 0.6f;
        float3 d = m_base.sun.direction;
        m_scene.sun.direction = normalize(float3(d.x * std::cos(a) - d.z * std::sin(a), d.y, d.x * std::sin(a) + d.z * std::cos(a)));
    }
    m_r.resetAccumulation();
}

int Viewer::run() {
    MSG msg;
    while (m_running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) m_running = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!m_running) break;
        if (m_minimized) { Sleep(30); continue; }
        frame();
    }
    m_r.waitIdle();
    m_gui.shutdown();
    m_r.shutdown();
    return 0;
}

LRESULT Viewer::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return 1;
    switch (msg) {
    case WM_CLOSE: m_running = false; DestroyWindow(hwnd); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_SIZE:
        m_minimized = wp == SIZE_MINIMIZED;
        if (!m_minimized && m_r.device()) {
            UINT w = LOWORD(lp), h = HIWORD(lp);
            if (w > 0 && h > 0 && (w != m_r.width() || h != m_r.height())) {
                m_r.resize(w, h);
                m_gui.onResize();
            }
        }
        return 0;
    case WM_DPICHANGED: {
        RECT* r = (RECT*)lp;
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void Viewer::cameraInput(const ImVec2& vpMin, const ImVec2& vpMax) {
    ImGuiIO& io = ImGui::GetIO();
    Camera& c = m_scene.camera;
    const bool hover = !io.WantCaptureMouse && io.MousePos.x >= vpMin.x && io.MousePos.y >= vpMin.y &&
                       io.MousePos.x < vpMax.x && io.MousePos.y < vpMax.y;
    static bool dragging = false;
    if (hover && (ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1) || ImGui::IsMouseClicked(2))) dragging = true;
    if (!ImGui::IsMouseDown(0) && !ImGui::IsMouseDown(1) && !ImGui::IsMouseDown(2)) dragging = false;
    bool moved = false;
    if (dragging) {
        ImVec2 d = io.MouseDelta;
        if (d.x != 0 || d.y != 0) {
            const bool pan = ImGui::IsMouseDown(2) || (ImGui::IsMouseDown(0) && io.KeyShift);
            if (pan) {
                float3 right, up, fwd;
                c.basis(right, up, fwd);
                float k = c.distance * 2.0f * std::tan(c.fovY * 0.5f) / std::max(1.0f, vpMax.y - vpMin.y);
                c.target = c.target - right * (d.x * k) + up * (d.y * k);
            } else {
                c.yaw -= d.x * 0.006f;
                c.pitch = std::max(-1.5f, std::min(1.5f, c.pitch + d.y * 0.006f));
            }
            moved = true;
        }
    }
    if (hover && io.MouseWheel != 0) {
        c.distance = std::max(0.5f, c.distance * std::pow(0.88f, io.MouseWheel));
        c.focusDistance = c.distance;
        moved = true;
    }
    if (!io.WantCaptureKeyboard) {
        if (ImGui::IsKeyPressed(ImGuiKey_F1)) { loadScene(SC_CORNELL); moved = true; }
        if (ImGui::IsKeyPressed(ImGuiKey_F2)) { loadScene(SC_LOOKDEV); moved = true; }
        if (ImGui::IsKeyPressed(ImGuiKey_F3)) { loadScene(SC_ATRIUM); moved = true; }
        if (ImGui::IsKeyPressed(ImGuiKey_F)) { m_scene.camera = m_views.empty() ? m_base.camera : m_views[0].cam; moved = true; }
        if (ImGui::IsKeyPressed(ImGuiKey_A)) m_anim = !m_anim;
        for (int k = 0; k < MODE_COUNT; ++k)
            if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_1 + k))) { m_mode = k; applyMode(); }
    }
    if (moved) m_r.resetAccumulation();
}

// Settings panel. Every control writes m_rs / m_scene; the renderer drops its history by itself when they change.
void Viewer::ui(float panelW) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(panelW, io.DisplaySize.y));
    ImGui::Begin("Radiance Cascades", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    ImGui::PushItemWidth(-1);

    ImGui::SeparatorText("Scène");
    int sc = m_sceneId;
    if (ImGui::Combo("##scene", &sc, kSceneNames, SC_COUNT)) loadScene(sc);
    if (!m_views.empty()) {
        if (ImGui::BeginCombo("##view", "Points de vue")) {
            for (auto& v : m_views)
                if (ImGui::Selectable(app::toUtf8(v.name).c_str())) { m_scene.camera = v.cam; m_r.resetAccumulation(); }
            ImGui::EndCombo();
        }
    }
    ImGui::Checkbox("Animer la lumière (A)", &m_anim);

    ImGui::SeparatorText("Rendu (touches 1 à 7)");
    int mode = m_mode;
    if (ImGui::Combo("##mode", &mode, kModeNames, MODE_COUNT)) { m_mode = mode; applyMode(); }

    ImGui::SeparatorText("Inspection");
    const bool gi = m_rs.ddgi && !m_rs.raster;
    ImGui::BeginDisabled(!gi);
    const char* views[] = {"Complet", "Indirect seul (GI)", "Direct seul"};
    ImGui::Combo("##giview", &m_rs.ddgiView, views, 3);
    ImGui::Checkbox("Afficher les sondes (cascade 0)", &m_rs.ddgiShowProbes);
    ImGui::EndDisabled();
    const char* wires[] = {"Ombré", "Fil de fer", "Ombré + fil de fer"};
    ImGui::BeginDisabled(!(m_rs.raster || m_rs.ddgiRasterView));
    ImGui::Combo("##wire", &m_rs.wireframe, wires, 3);
    ImGui::EndDisabled();
    ImGui::SliderFloat("##exp", &m_rs.exposure, 0.1f, 4.0f, "Exposition %.2f");
    const char* tms[] = {"ACES", "AgX", "AgX Punchy", "Standard"};
    ImGui::Combo("##tm", &m_rs.tonemapper, tms, 4);
    if (m_mode == MODE_PATH || m_mode == MODE_RC_TRACED || m_mode == MODE_DDGI_TRACED)
        ImGui::Checkbox("Débruiteur (modes ray tracés)", &m_rs.denoise);
    if (m_mode == MODE_PATH) ImGui::Text("%u / %d échantillons", m_r.sampleCount(), m_rs.maxSamples);

    ImGui::SeparatorText("Radiance Cascades");
    ImGui::BeginDisabled(m_rs.giMethod != 1 || !gi);
    ImGui::SliderInt("##budget", &m_rs.rcProbeBudget, 512, 16384, "Sondes cascade 0 : %d");
    ImGui::SliderInt("##casc", &m_rs.rcCascades, 1, RC_MAX_CASCADES, "Cascades : %d");
    ImGui::SliderInt("##dir", &m_rs.rcDirRes, 2, RC_MAX_DIR0, "Directions C0 : %d par côté");
    ImGui::SliderFloat("##int", &m_rs.rcInterval, 0.25f, 4.0f, "Intervalle C0 : %.2f x esp.");
    ImGui::SliderInt("##ls", &m_rs.rcLightSamples, 1, 8, "Échantillons lumière : %d");
    ImGui::Checkbox("Rebonds multiples", &m_rs.rcBounces);
    ImGui::Checkbox("Correction bilinéaire (fusion exacte)", &m_rs.rcBilinearFix);
    ImGui::SliderFloat("##hy", &m_rs.rcHysteresis, 0.0f, 0.95f, m_rs.rcHysteresis > 0 ? "Hystérésis : %.2f" : "Hystérésis : 0 (aucun temporel)");
    ImGui::BeginDisabled(!m_rs.rcScreen);
    ImGui::SliderInt("##sspace", &m_rs.rcScreenSpacing, 2, 16, "Écran : 1 sonde / %d px");
    ImGui::SliderInt("##sdir", &m_rs.rcScreenDirRes, 2, 8, "Écran : directions C0 %d par côté");
    ImGui::SliderFloat("##sint", &m_rs.rcScreenInterval, 0.05f, 2.0f, "Écran : intervalle C0 %.2f x esp.");
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (m_rs.giMethod == 0 && gi) {
        ImGui::SeparatorText("DDGI");
        ImGui::SliderInt("##dbudget", &m_rs.ddgiProbeBudget, 256, 16384, "Sondes : %d");
        ImGui::SliderFloat("##dhy", &m_rs.ddgiHysteresis, 0.5f, 0.995f, "Hystérésis : %.3f");
    }

    ImGui::SeparatorText("Environnement");
    if (m_sceneId == SC_LOOKDEV || m_scene.env.hdri) {
        std::string cur = m_hdriPath.empty() ? "(aucun)" : (m_hdriPath == L"sky" ? "Ciel procédural" : fileName(m_hdriPath));
        if (ImGui::BeginCombo("##hdri", cur.c_str())) {
            if (ImGui::Selectable("Ciel procédural")) setHdri(L"sky");
            for (auto& p : m_hdris)
                if (ImGui::Selectable(fileName(p).c_str())) setHdri(p);
            if (ImGui::Selectable("Charger un fichier...")) {
                wchar_t file[MAX_PATH] = {};
                OPENFILENAMEW ofn = {sizeof(ofn)};
                ofn.hwndOwner = m_hwnd;
                ofn.lpstrFilter = L"HDRI (*.hdr, *.exr, *.jpg, *.png)\0*.hdr;*.exr;*.jpg;*.jpeg;*.png\0";
                ofn.lpstrFile = file;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
                if (GetOpenFileNameW(&ofn)) setHdri(file);
            }
            ImGui::EndCombo();
        }
        ImGui::SliderFloat("##hi", &m_scene.env.hdriIntensity, 0.0f, 4.0f, "Intensité HDRI %.2f");
        ImGui::SliderAngle("##hr", &m_scene.env.hdriRotation, -180.0f, 180.0f, "Rotation %.0f°");
    }
    if (m_scene.sun.enabled) ImGui::SliderFloat("##sun", &m_scene.sun.intensity, 0.0f, 20.0f, "Soleil %.1f");
    if (!m_scene.lights.empty()) ImGui::SliderFloat("##li", &m_scene.lights[0].intensity, 0.0f, 150.0f, "Lumière %.0f");

    ImGui::SeparatorText("Statistiques");
    ImGui::Text("%s", m_r.adapterName().c_str());
    ImGui::Text("%.0f fps   GPU %.2f ms", m_fps, m_r.gpuFrameMs());
    if (gi) {
        ImGui::Text("Sondes : %u (espacement %.2f)", m_r.giProbeCount(), m_r.giSpacing());
        if (m_rs.giMethod == 1) {
            const RCStats& s = m_r.rcStats();
            ImGui::Text("Cascades : trace %.2f ms, intégration %.2f ms", m_r.giUpdateMs(), m_r.giBlendMs());
            ImGui::Text("%.2f M rayons / image (grille)", s.rays / 1e6);
            if (s.screenCascades > 0) ImGui::Text("Écran : %d cascades, %.2f M rayons", s.screenCascades, s.screenRays / 1e6);
            if (ImGui::BeginTable("casc", 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("C");
                ImGui::TableSetupColumn("Sondes");
                ImGui::TableSetupColumn("Dir.");
                ImGui::TableSetupColumn("Intervalle");
                ImGui::TableHeadersRow();
                for (int i = 0; i < s.cascades; ++i) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::Text("%d", i);
                    ImGui::TableNextColumn(); ImGui::Text("%u", s.probes[i]);
                    ImGui::TableNextColumn(); ImGui::Text("%ux%u", s.dirRes[i], s.dirRes[i]);
                    ImGui::TableNextColumn();
                    if (s.tEnd[i] > 1e20f) ImGui::Text("%.1f - inf", s.tStart[i]);
                    else ImGui::Text("%.1f - %.1f", s.tStart[i], s.tEnd[i]);
                }
                ImGui::EndTable();
            }
        } else {
            ImGui::Text("Sondes : rayons %.2f ms, mélange %.2f ms", m_r.giUpdateMs(), m_r.giBlendMs());
        }
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Clic gauche : orbite · Maj+clic / milieu : pan");
    ImGui::TextDisabled("Molette : zoom · F : recadrer · F1-F3 : scènes");
    ImGui::PopItemWidth();
    ImGui::End();
}

void Viewer::frame() {
    uint64_t now = GetTickCount64();
    float dt = std::min(0.1f, (now - m_lastTick) / 1000.0f);
    m_lastTick = now;
    if (dt > 0) m_fps = m_fps * 0.9f + 0.1f / dt;

    m_gui.newFrame();
    ImGuiIO& io = ImGui::GetIO();
    const float panelW = std::floor(330.0f * m_dpi);
    ui(panelW);
    ImVec2 vpMin(panelW, 0), vpMax(io.DisplaySize.x, io.DisplaySize.y);
    cameraInput(vpMin, vpMax);
    animate(dt);

    m_vp = {0, 0, std::max(1, (int)(vpMax.x - vpMin.x)), std::max(1, (int)(vpMax.y - vpMin.y))};
    m_r.renderToTexture(m_scene, m_rs, m_vp, -1);
    ImGui::GetBackgroundDrawList()->AddImage(m_gui.viewportTexture(), vpMin, ImVec2(vpMin.x + m_vp.w, vpMin.y + m_vp.h));
    m_gui.render(true);
    const bool shotNow = !m_opt.shot.empty() && m_frame + 1 >= m_opt.frames;
    std::vector<uint8_t> img;
    uint32_t w = 0, h = 0;
    bool shotOk = false;
    if (shotNow && m_opt.shotWindow) shotOk = m_r.readBackBuffer(img, w, h);   // the whole window, panel included
    m_r.present(m_opt.shot.empty());
    ++m_frame;

    if (shotNow) {
        if (!m_opt.shotWindow) shotOk = m_r.readViewport(img, w, h);
        if (shotOk) savePng(m_opt.shot, img, w, h);
        // timings of the last frame next to the image (shot.png.txt)
        FILE* f = nullptr;
        if (_wfopen_s(&f, (m_opt.shot + L".txt").c_str(), L"w") == 0 && f) {
            const RCStats& s = m_r.rcStats();
            fprintf(f, "mode %d scene %d size %ux%u\ngpu %.3f ms\nprobes %u spacing %.3f\nprobe update %.3f ms, gather %.3f ms\n",
                    m_mode, m_sceneId, w, h, m_r.gpuFrameMs(), m_r.giProbeCount(), m_r.giSpacing(), m_r.giUpdateMs(), m_r.giBlendMs());
            fprintf(f, "rc rays %.3f M, screen %d cascades %.3f M rays\n", s.rays / 1e6, s.screenCascades, s.screenRays / 1e6);
            for (int i = 0; i < s.cascades; ++i)
                fprintf(f, "C%d probes %u dirs %ux%u interval %.2f - %.2f\n", i, s.probes[i], s.dirRes[i], s.dirRes[i], s.tStart[i], s.tEnd[i]);
            fclose(f);
        }
        m_running = false;
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    Options opt;
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        auto next = [&]() -> std::wstring { return i + 1 < argc ? std::wstring(argv[++i]) : std::wstring(); };
        if (a == L"--scene") { std::wstring s = next(); opt.scene = s == L"lookdev" ? SC_LOOKDEV : (s == L"atrium" ? SC_ATRIUM : SC_CORNELL); }
        else if (a == L"--mode") opt.mode = std::max(0, std::min(MODE_COUNT - 1, _wtoi(next().c_str())));
        else if (a == L"--view") opt.view = _wtoi(next().c_str());
        else if (a == L"--probes") opt.probes = _wtoi(next().c_str());
        else if (a == L"--cascades") opt.cascades = _wtoi(next().c_str());
        else if (a == L"--dirres") opt.dirRes = _wtoi(next().c_str());
        else if (a == L"--interval") opt.interval = (float)_wtof(next().c_str());
        else if (a == L"--hdri") opt.hdri = next();
        else if (a == L"--shot") opt.shot = next();
        else if (a == L"--shot-window") { opt.shot = next(); opt.shotWindow = true; }
        else if (a == L"--frames") opt.frames = _wtoi(next().c_str());
        else if (a == L"--anim") opt.anim = _wtoi(next().c_str()) != 0;
        else if (a == L"--show-probes") opt.showProbes = _wtoi(next().c_str()) != 0;
        else if (a == L"--camview") opt.camView = _wtoi(next().c_str());
        else if (a == L"--hysteresis") opt.hysteresis = (float)_wtof(next().c_str());
        else if (a == L"--bilinear") opt.bilinear = _wtoi(next().c_str());
        else if (a == L"--sspacing") opt.sSpacing = _wtoi(next().c_str());
        else if (a == L"--sdirres") opt.sDirRes = _wtoi(next().c_str());
        else if (a == L"--sinterval") opt.sInterval = (float)_wtof(next().c_str());
        else if (a == L"--lsamples") opt.lightSamples = _wtoi(next().c_str());
        else if (a == L"--size") swscanf_s(next().c_str(), L"%dx%d", &opt.width, &opt.height);
    }
    LocalFree(argv);
    Viewer v;
    if (!v.init(inst, opt)) return 1;
    return v.run();
}
