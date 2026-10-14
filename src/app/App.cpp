#include "App.h"
#include "GameHost.h"
#include "backends/imgui_impl_win32.h"
#include "vp3d/shaders/Shared.h"
#include "atelier/material_ui.h"
#include <shellapi.h>
#include <wincodec.h>
#include <ctime>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace app {

static App* g_app = nullptr;

static LRESULT CALLBACK StaticWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (g_app) return g_app->wndProc(h, m, w, l);
    return DefWindowProcW(h, m, w, l);
}

static bool savePng(const std::wstring& path, std::vector<uint8_t> rgba, uint32_t w, uint32_t h) {
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

// --------------------------------------------------------------------------- init / loop

bool App::init(HINSTANCE inst, const Options& opt) {
    g_app = this;
    m_opt = opt;
    m_quality = opt.quality;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    m_exeDir = exe;
    m_exeDir = m_exeDir.substr(0, m_exeDir.find_last_of(L"\\/"));
    SetCurrentDirectoryW(m_exeDir.c_str());   // window .ini next to the exe

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = StaticWndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    // cube logo of the Atelier top bar (src/app/res/wzx.ico), title bar and taskbar
    wc.hIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wc.lpszClassName = L"WzxEngineEditor";
    RegisterClassExW(&wc);

    UINT sysDpi = GetDpiForSystem();
    m_dpi = sysDpi / 96.0f;
    RECT r = {0, 0, (LONG)(opt.width * m_dpi), (LONG)(opt.height * m_dpi)};
    AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, sysDpi);
    m_hwnd = CreateWindowExW(0, wc.lpszClassName, L"WZX Engine — Editor", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                             r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, nullptr);
    if (!m_hwnd) return false;
    m_dpi = GetDpiForWindow(m_hwnd) / 96.0f;

    RECT cr;
    GetClientRect(m_hwnd, &cr);
    std::string err;
    if (!m_renderer.init(m_hwnd, cr.right - cr.left, cr.bottom - cr.top, m_exeDir + L"\\shaders", &err)) {
        MessageBoxA(m_hwnd, err.c_str(), "WZX Engine - DirectX 12", MB_ICONERROR);
        return false;
    }
    if (!m_gui.init(m_hwnd, m_renderer, m_dpi)) {
        MessageBoxA(m_hwnd, "Dear ImGui initialisation failed", "WZX Engine", MB_ICONERROR);
        return false;
    }
    th::loadThemes(m_exeDir + L"\\assets\\themes");
    th::loadSettings(m_exeDir + L"\\wzx_settings.ini");
    if (opt.light >= 0) th::settings().light = opt.light != 0;
    if (opt.mainColor.x >= 0) th::settings().mainColor = opt.mainColor;
    if (opt.sizes.x > 0) { th::settings().textScale = opt.sizes.x; th::settings().iconScale = opt.sizes.y; th::settings().layoutScale = opt.sizes.z; }
    th::apply(m_dpi);
    m_themeTestOnly = opt.light >= 0 || opt.mainColor.x >= 0 || opt.sizes.x > 0;
    m_showSettings = opt.openSettings;
    m_workspace = opt.workspace;
    if (opt.inspectorTab >= 0 && opt.inspectorTab <= 3) m_atl.inspectorTab = opt.inspectorTab;
    m_matHost.app = this;
    atelier::material::SetPreviewHost(&m_matHost);
    atelier::material::Initialize(toUtf8(m_exeDir + L"\\MaterialLibrary.amat"));
    m_rs.raster = opt.raster;
    m_vpPx.w = (int)(960 * m_dpi);
    m_vpPx.h = (int)(540 * m_dpi);
    m_rs.ddgi = opt.ddgi;
    m_rs.ddgiRaster = opt.ddgiRaster;
    m_rs.ddgiCaptureProbes = opt.ddgiCapture;
    m_rs.ddgiProbeBudget = opt.probes;
    m_rs.ddgiView = opt.ddgiView;
    m_rs.temporalCap = opt.temporalCap;
    m_rs.ddgiHysteresis = opt.hysteresis;
    if (!opt.raster) setRenderMode(opt.ddgi ? 2 : 1);
    ShowWindow(m_hwnd, SW_SHOWDEFAULT);
    UpdateWindow(m_hwnd);

    applyQuality();
    log("WZX Engine started — " + m_renderer.adapterName(), 1);
    loadScene(opt.scene);
    if (opt.view >= 0 && opt.view < (int)m_views.size()) {
        m_activeView = opt.view;
        m_scene.camera = m_views[opt.view].cam;
    }
    if (!opt.openPath.empty()) loadSceneFile(opt.openPath);
    if (!opt.hdri.empty()) setHdri(opt.hdri);
    if (opt.select >= 0 && opt.select < (int)m_scene.objects.size()) m_selected = opt.select;
    if (opt.tool >= 0 && opt.tool <= TOOL_ROTATE) m_tool = (Tool)opt.tool;
    m_game = new GameHost(*this);   // node graph, Play, builds, command API
    m_game->init();
    markDirty();
    return true;
}

int App::run() {
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
    m_renderer.waitIdle();
    delete m_game;
    m_game = nullptr;
    atelier::material::Shutdown();
    m_gui.shutdown();
    m_renderer.shutdown();
    return 0;
}

LRESULT App::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return 1;
    switch (msg) {
    case WM_CLOSE: m_running = false; DestroyWindow(hwnd); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_SIZE:
        m_minimized = wp == SIZE_MINIMIZED;
        if (!m_minimized && m_renderer.device()) {
            UINT w = LOWORD(lp), h = HIWORD(lp);
            if (w > 0 && h > 0 && (w != m_renderer.width() || h != m_renderer.height())) {
                m_renderer.resize(w, h);
                m_gui.onResize();
            }
        }
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mm = (MINMAXINFO*)lp;
        mm->ptMinTrackSize.x = (LONG)(960 * m_dpi);
        mm->ptMinTrackSize.y = (LONG)(540 * m_dpi);
        return 0;
    }
    case WM_DPICHANGED: {
        RECT* r = (RECT*)lp;
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void App::cameraChanged() {
    m_activeView = -1;
    markDirty();
}

void App::markDirty() {
    m_thumbFresh = false;
    m_renderer.resetAccumulation();
    m_interacting = true;
}

// Modes of the viewport: 0 real time (raster), 1 progressive path tracing (exact, denoiser optional),
// 2 real-time GI probes (DDGI).
void App::setRenderMode(int mode) {
    m_rs.raster = mode == 0;
    m_rs.historyFixes = true;
    m_rs.ddgi = mode == 2;
    // progressive: exact, no denoiser by default (the option is in the Render tab); GI: off by default (it blurs shadows)
    m_rs.denoise = false;
    m_quality = mode == 1 ? 1 : 0;
    applyQuality();
    markDirty();
}

int App::renderMode() const {
    if (m_rs.raster) return 0;
    if (m_rs.ddgi) return 2;
    return 1;
}

void App::applyQuality() {
    if (m_quality == 0) {
        m_rs.maxBounces = 4;
        m_rs.maxSamples = 256;
        m_rs.bevelSamples = 2;
    } else {
        m_rs.maxBounces = 10;
        m_rs.maxSamples = 4096;
        m_rs.bevelSamples = 4;
    }
}

void App::log(const std::string& text, int level) {
    time_t t = time(nullptr);
    tm lt;
    localtime_s(&lt, &t);
    char b[16];
    snprintf(b, sizeof(b), "%02d:%02d:%02d", lt.tm_hour, lt.tm_min, lt.tm_sec);
    m_log.push_back({b, text, level});
    if (m_log.size() > 500) m_log.erase(m_log.begin());
}

void App::status(const std::wstring& text, int level) {
    m_status = text;
    m_statusLevel = level;
    m_statusTime = GetTickCount64();
    log(toUtf8(text), level);
}

void App::loadScene(int id) {
    m_sceneId = id;
    if (id == SCENE_CORNELL) buildCornellBox(m_scene);
    else if (id == SCENE_LOOKDEV) buildLookDevScene(m_scene);
    else buildStudioScene(m_scene);
    m_hdriPath.clear();   // the new scene has no HDRI (LookDev gets one below)
    m_selected = -1;
    m_multi.clear();
    m_undo.clear();
    if (m_game) m_game->resetVillage();   // the houses belong to the scene they were placed in
    m_redo.clear();
    m_rs.exposure = id == SCENE_STUDIO ? 1.3f : 1.0f;
    if (id == SCENE_CORNELL) m_scene.env.ambient = float3(0.16f, 0.15f, 0.13f);
    m_rs.vignette = id == SCENE_CORNELL ? 0.10f : 0.18f;
    m_tool = id == SCENE_CORNELL ? TOOL_SELECT : TOOL_MOVE;
    m_gz = GizmoDrag();
    if (id == SCENE_LOOKDEV) setHdri(m_lastHdri.empty() ? defaultHdri() : m_lastHdri);
    m_renderer.setScene(m_scene);
    clearViews();
    for (auto& v : defaultViews(m_scene, id == SCENE_CORNELL)) m_views.push_back({v.name, v.cam, 0});
    m_activeView = 0;
    generateThumbnails();
    m_scene.camera = m_views[0].cam;
    log("Scene loaded: " + m_scene.name, 1);
    markDirty();
}


// --------------------------------------------------------------------------- HDRI

static void listEnvFiles(const std::wstring& dir, bool hdrOnly, std::vector<std::wstring>& out) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring p = dir + L"\\" + fd.cFileName;
        if (!isEnvImageFile(p)) continue;
        std::wstring e = p.substr(p.find_last_of(L'.'));
        for (auto& c : e) c = (wchar_t)towlower(c);
        if (hdrOnly && e != L".hdr" && e != L".exr") continue;   // the inbox also holds reference pictures
        out.push_back(p);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

std::vector<std::wstring> App::hdriFiles() const {
    std::vector<std::wstring> v;
    listEnvFiles(m_exeDir + L"\\..\\..\\01 - INBOX", true, v);
    listEnvFiles(m_exeDir + L"\\assets\\hdri", false, v);
    return v;
}

std::wstring App::defaultHdri() const {
    std::vector<std::wstring> v = hdriFiles();
    return v.empty() ? std::wstring(L"sky") : v.front();
}

bool App::setHdri(const std::wstring& path) {
    if (path.empty()) {
        m_scene.env.hdri.reset();
        m_hdriPath.clear();
        markDirty();
        return true;
    }
    EnvImagePtr img;
    auto it = m_hdriCache.find(path);
    if (it != m_hdriCache.end()) {
        img = it->second;
    } else {
        auto e = std::make_shared<EnvImage>();
        if (path == L"sky") {
            makeProceduralSky(*e);
        } else {
            std::string err;
            if (!loadEnvImage(path, *e, &err)) {
                status(L"HDRI not loaded: " + fromUtf8(err), 2);
                return false;
            }
        }
        img = e;
        m_hdriCache[path] = img;
        char b[256];
        snprintf(b, sizeof(b), "HDRI loaded: %s (%ux%u%s)", img->name.c_str(), img->sourceWidth, img->sourceHeight,
                 img->sourceWidth != img->width ? ", reduced to 4096" : "");
        log(b, 1);
    }
    m_scene.env.hdri = img;
    m_hdriPath = path;
    m_lastHdri = path;
    // LookDev: the sun of the image is turned behind the camera, on its right, so that the spheres are lit from the front.
    if (m_sceneId == SCENE_LOOKDEV) m_scene.env.hdriRotation = lookDevHdriRotation(*img);
    markDirty();
    return true;
}

// --------------------------------------------------------------------------- frame

void App::frame() {
    uint64_t now = GetTickCount64();
    if (m_lastTick) {
        m_frameMs = lerpf(m_frameMs, (float)(now - m_lastTick), 0.1f);
        m_dt = clampf((now - m_lastTick) * 0.001f, 0.001f, 0.1f);
    }
    m_lastTick = now;

    if (m_pendingAction) runPendingAction();
    if (m_pendingScene >= 0) {
        int s = m_pendingScene;
        m_pendingScene = -1;
        loadScene(s);
    }

    m_gui.newFrame();
    m_interacting = false;
    // game layer first: it owns the keys and the graph overlay (drawn after the editor, below)
    bool gameKeys = m_game && m_game->beginFrame();
    if (!gameKeys && m_workspace == 0) handleShortcuts();   // Graph / Material Studio have their own shortcuts
    if (!m_game || !m_game->playerMode()) drawEditor();
    if (m_game) m_game->update(m_dt);
    if (m_turntable) {
        m_scene.camera.yaw += 0.006f;
        cameraChanged();
    }
    if (m_autoFocus && m_scene.camera.dof) m_scene.camera.focusDistance = m_scene.camera.distance;

    // Adaptive samples per frame: 1 while interacting, more when the view is still.
    if (m_interacting) m_adaptiveSpp = 1;
    else {
        float g = m_renderer.gpuFrameMs();
        if (g < 9.0f && m_adaptiveSpp < 8) m_adaptiveSpp++;
        else if (g > 22.0f && m_adaptiveSpp > 1) m_adaptiveSpp--;
    }
    m_rs.samplesPerFrame = m_adaptiveSpp;

    if (m_game && m_game->playerMode()) {   // built game: the 3D view fills the window
        ImVec2 ds = ImGui::GetIO().DisplaySize;
        m_rcView = {0, 0, ds.x, ds.y};
        m_vpPx = {0, 0, std::max(16, (int)ds.x), std::max(16, (int)ds.y)};
        m_viewportVisible = true;
    }
    m_viewportRendered = false;
    if (m_workspace == 2 && m_matHost.want) {
        m_matHost.want = false;
        renderMaterialPreview();
        m_viewportRendered = true;
    } else if (m_viewportVisible) {
        if (m_previewActive) leavePreview();
        std::vector<int> outlined = m_multi;
        if (m_game) m_game->addHoverOutline(outlined);   // the house under the cursor: its halo
        m_renderer.setExtraSelection(outlined);
        m_renderer.renderToTexture(m_scene, m_rs, m_vpPx, m_selected);
        m_gui.viewportTexture();   // refresh the SRV (the texture is recreated when the viewport size changes)
        m_viewportRendered = true;
    }
    // built game: the editor's viewport canvas is not drawn, so the 3D view is the background of the window
    if (m_game && m_game->playerMode() && m_viewportRendered)
        ImGui::GetBackgroundDrawList()->AddImage(m_gui.viewportTexture(), ImVec2(0, 0), ImGui::GetIO().DisplaySize);
    if (m_game) m_game->draw();   // graph editor / toolbar / toasts, over the viewport
    m_gui.render(m_viewportRendered);
    if (m_game) m_game->afterDraw();   // API screenshots: frame complete

    // Refresh the thumbnail of the active bookmark once the path traced image is clean.
    if (m_activeView >= 0 && m_activeView < (int)m_views.size() && !m_thumbFresh && !m_rs.raster && m_renderer.sampleCount() >= 128) {
        m_thumbFresh = true;
        std::vector<uint8_t> img;
        uint32_t w, h;
        if (m_renderer.readViewport(img, w, h)) {
            if (m_views[m_activeView].thumb) m_gui.releaseTexture(m_views[m_activeView].thumb);
            m_views[m_activeView].thumb = makeThumb(img, w, h);
        }
    }

    // Automated capture (command line): window image once the viewport has enough samples.
    if (!m_opt.shotPath.empty() && ((int)m_renderer.sampleCount() >= m_opt.shotSpp || !m_viewportRendered) && ++m_frameCounter > 6) {
        std::vector<uint8_t> img;
        uint32_t w, h;
        bool ok = m_opt.shotViewport ? m_renderer.readViewport(img, w, h) : m_renderer.readBackBuffer(img, w, h);
        if (ok) savePng(m_opt.shotPath, img, w, h);
        m_running = false;
    }
    m_renderer.present(true);
}

// --------------------------------------------------------------------------- camera & picking

void App::cameraRay(float px, float py, float3& o, float3& d) const {
    const Camera& c = m_scene.camera;
    float3 right, up, fwd;
    c.basis(right, up, fwd);
    float u = (px - m_rcView.x) / m_rcView.w, v = (py - m_rcView.y) / m_rcView.h;
    float t = std::tan(c.fovY * 0.5f);
    float aspect = m_rcView.w / m_rcView.h;
    o = c.eye();
    d = normalize(fwd + right * ((2 * u - 1) * aspect * t) + up * ((1 - 2 * v) * t));
}

bool App::project(const float3& p, float2& s) const {
    const Camera& c = m_scene.camera;
    float3 r, u, f;
    c.basis(r, u, f);
    float3 v = p - c.eye();
    float z = dot(v, f);
    if (z < 1e-3f) return false;
    float t = std::tan(c.fovY * 0.5f), a = m_rcView.w / m_rcView.h;
    float x = dot(v, r) / (z * t * a), y = dot(v, u) / (z * t);
    s = float2(m_rcView.x + (0.5f + 0.5f * x) * m_rcView.w, m_rcView.y + (0.5f - 0.5f * y) * m_rcView.h);
    return true;
}

void App::aabbOf(const Object& o, const float3& pos, const float4& rot, float3& mn, float3& mx) {
    float3 a, b;
    m_scene.meshes[o.mesh].bounds(a, b);
    mn = float3(1e30f);
    mx = float3(-1e30f);
    for (int i = 0; i < 8; ++i) {
        float3 p = qRot(rot, float3((i & 1) ? b.x : a.x, (i & 2) ? b.y : a.y, (i & 4) ? b.z : a.z) * o.scale) + pos;
        mn = minv(mn, p);
        mx = maxv(mx, p);
    }
}

void App::setPivotKeepEye(const float3& p) {
    Camera& c = m_scene.camera;
    float3 eye = c.eye();
    float3 v = eye - p;
    float dist = length(v);
    if (dist < 1e-3f) return;
    c.target = p;
    c.distance = dist;
    c.pitch = std::asin(clampf(v.y / dist, -1, 1));
    c.yaw = std::atan2(v.x, v.z);
}

void App::frameSelection() {
    float3 mn(1e30f), mx(-1e30f);
    auto addObj = [&](const Object& o) {
        float3 a, b;
        m_scene.meshes[o.mesh].bounds(a, b);
        Affine xf = o.transform();
        for (int i = 0; i < 8; ++i) {
            float3 p((i & 1) ? b.x : a.x, (i & 2) ? b.y : a.y, (i & 4) ? b.z : a.z);
            p = xf.point(p);
            mn = minv(mn, p);
            mx = maxv(mx, p);
        }
    };
    if (m_selected >= 0)
        for (int i : selectionAll()) addObj(m_scene.objects[i]);
    else
        for (auto& o : m_scene.objects)
            if (o.visible && o.selectable && o.part != "WALL") addObj(o);
    if (mn.x > mx.x) return;
    Camera& c = m_scene.camera;
    c.target = (mn + mx) * 0.5f;
    float radius = length(mx - mn) * 0.5f;
    c.distance = std::max(radius / std::sin(c.fovY * 0.5f) * 1.1f, 1.0f);
    cameraChanged();
}

// Shows / hides the grid drawn on the ground of the scene (material flag of the GROUND object).
void App::toggleGroundGrid() {
    bool found = false;
    for (Object& o : m_scene.objects)
        if (o.part == "GROUND" && o.material >= 0 && o.material < (int)m_scene.materials.size()) {
            m_scene.materials[o.material].flags ^= MAT_FLAG_GRID;
            found = true;
        }
    if (found) markDirty();
    else status(L"No ground grid in this scene", 2);
}

void App::setViewAxis(int axis) {
    Camera& c = m_scene.camera;
    switch (axis) {
    case 0: c.yaw = 0; c.pitch = radians(10); break;            // perspective front
    case 1: c.yaw = 0; c.pitch = 0; break;                       // face
    case 2: c.yaw = kPi; c.pitch = 0; break;                     // arriere
    case 3: c.yaw = -kPi * 0.5f; c.pitch = 0; break;             // gauche
    case 4: c.yaw = kPi * 0.5f; c.pitch = 0; break;              // droite
    case 5: c.yaw = 0.001f; c.pitch = radians(89.0f); break;     // dessus
    }
    cameraChanged();
}

void App::handleShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;
    const bool placing = m_game && m_game->placementMode();   // Del, R and the arrows belong to the houses then
    bool ctrl = io.KeyCtrl;
    auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
    if (ctrl) {
        if (pressed(ImGuiKey_S)) m_pendingAction = 1;
        if (pressed(ImGuiKey_O)) m_pendingAction = 2;
        if (ImGui::IsKeyPressed(ImGuiKey_Z)) undo();
        if (ImGui::IsKeyPressed(ImGuiKey_Y)) redo();
        if (pressed(ImGuiKey_D) && !placing) duplicateSelected();
        return;
    }
    if (pressed(ImGuiKey_F1)) m_pendingScene = SCENE_CORNELL;
    if (pressed(ImGuiKey_F2)) m_pendingScene = SCENE_STUDIO;
    if (pressed(ImGuiKey_F3)) m_pendingScene = SCENE_LOOKDEV;
    if (pressed(ImGuiKey_F12)) saveScreenshot(false);
    if (pressed(ImGuiKey_P)) setRenderMode(m_rs.raster ? 1 : 0);            // viewport <-> progressive path tracing
    if (pressed(ImGuiKey_L)) setRenderMode(m_rs.ddgi ? 1 : 2);              // real-time probe GI (DDGI) <-> progressive path tracing
    if (pressed(ImGuiKey_Delete) && !placing) deleteSelected();
    if (io.WantCaptureKeyboard && !ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow)) return;
    if (pressed(ImGuiKey_Q)) m_tool = TOOL_SELECT;
    if (pressed(ImGuiKey_W) || pressed(ImGuiKey_G)) m_tool = TOOL_MOVE;
    if (pressed(ImGuiKey_E)) m_tool = TOOL_ROTATE;
    if (pressed(ImGuiKey_R) && !placing) m_tool = TOOL_SCALE;
    if (pressed(ImGuiKey_F)) frameSelection();
    if (pressed(ImGuiKey_T)) m_turntable = !m_turntable;
    if (pressed(ImGuiKey_Escape)) select(-1);
    if (m_selected >= 0 && !placing && !ImGui::IsAnyItemActive()) {
        // Arrow keys move along the axis closest to the camera direction.
        float3 r, u, f;
        m_scene.camera.basis(r, u, f);
        auto axisOf = [](float3 v, int& ax, int& az) {
            if (std::fabs(v.x) > std::fabs(v.z)) { ax = v.x > 0 ? 1 : -1; az = 0; }
            else { ax = 0; az = v.z > 0 ? 1 : -1; }
        };
        int rx, rz, fx, fz;
        axisOf(r, rx, rz);
        axisOf(f, fx, fz);
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) nudgeSelected(rx, 0, rz);
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) nudgeSelected(-rx, 0, -rz);
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) nudgeSelected(fx, 0, fz);
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) nudgeSelected(-fx, 0, -fz);
        if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) nudgeSelected(0, 1, 0);
        if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) nudgeSelected(0, -1, 0);
    }
}

// --------------------------------------------------------------------------- editing

void App::pushUndo() {
    m_undo.push_back({m_scene.objects, m_scene.materials, m_selected});
    if (m_undo.size() > 100) m_undo.erase(m_undo.begin());
    m_redo.clear();
}

void App::undo() {
    if (m_undo.empty()) return;
    m_redo.push_back({m_scene.objects, m_scene.materials, m_selected});
    Snapshot s = m_undo.back();
    m_undo.pop_back();
    m_scene.objects = s.objects;
    m_scene.materials = s.materials;
    m_selected = s.selected < (int)m_scene.objects.size() ? s.selected : -1;
    m_multi.clear();
    markDirty();
}

void App::redo() {
    if (m_redo.empty()) return;
    m_undo.push_back({m_scene.objects, m_scene.materials, m_selected});
    Snapshot s = m_redo.back();
    m_redo.pop_back();
    m_scene.objects = s.objects;
    m_scene.materials = s.materials;
    m_selected = s.selected < (int)m_scene.objects.size() ? s.selected : -1;
    m_multi.clear();
    markDirty();
}

void App::select(int idx) {
    if (idx == m_selected && m_multi.empty()) return;
    m_selected = idx;
    m_multi.clear();
    markDirty();
}

void App::addPartFromLibrary(const PartDef& p, const float3* at) {
    pushUndo();
    // Drop the object at the given point, else at the pivot on the ground, or on top of the selection.
    float3 pos = m_scene.camera.target;
    pos.y = 0;
    if (at) pos = *at;
    else if (m_selected >= 0) {
        const Object& s = m_scene.objects[m_selected];
        float3 a, b;
        m_scene.meshes[s.mesh].bounds(a, b);
        pos = s.position;
        pos.y = s.position.y + b.y * s.scale.y;
    }
    if (m_magnet) { pos.x = std::round(pos.x); pos.z = std::round(pos.z); }
    int idx = addPart(m_scene, p.id, p.defaultColor, -1, pos, 0);
    m_selected = idx;
    m_multi.clear();
    if (m_tool == TOOL_SELECT) m_tool = TOOL_MOVE;
    status(L"Object added: " + std::wstring(p.name));
    markDirty();
}

void App::duplicateSelected() {
    if (m_selected < 0) return;
    pushUndo();
    std::vector<int> created;
    for (int i : selectionAll()) {
        Object o = m_scene.objects[i];
        Material m = m_scene.materials[o.material];
        o.material = m_scene.addMaterial(m);
        o.position.x += 8.0f;
        created.push_back(m_scene.addObject(o));
    }
    m_selected = created[0];
    m_multi.assign(created.begin() + 1, created.end());
    status(L"Selection duplicated");
    markDirty();
}

void App::deleteSelected() {
    if (m_selected < 0) return;
    pushUndo();
    std::vector<int> all = selectionAll();
    std::sort(all.begin(), all.end(), [](int a, int b) { return a > b; });
    for (int i : all)
        if (i >= 0 && i < (int)m_scene.objects.size()) m_scene.objects.erase(m_scene.objects.begin() + i);
    m_selected = -1;
    m_multi.clear();
    status(std::to_wstring(all.size()) + L" object(s) deleted");
    markDirty();
}

void App::rotateSelected(float deg) {
    if (m_selected < 0) return;
    pushUndo();
    Object& o = m_scene.objects[m_selected];
    o.yaw += radians(deg);
    // objects turned with the rotate gizmo use a quaternion: turn it as well
    if (o.useRot) o.rot = qNormalize(qMul(qAxisAngle(float3(0, 1, 0), radians(deg)), o.rot));
    markDirty();
}

void App::nudgeSelected(int dx, int dy, int dz) {
    if (m_selected < 0) return;
    pushUndo();
    for (int i : selectionAll()) {
        Object& o = m_scene.objects[i];
        o.position.x += (float)dx;
        o.position.z += (float)dz;
        o.position.y = std::max(0.0f, o.position.y + dy);
    }
    markDirty();
}

void App::setColor(int pi) {
    if (m_selected < 0) return;
    pushUndo();
    const PaletteColor& c = palette()[pi];
    for (int sel : selectionAll()) {
        Material& m = m_scene.materials[m_scene.objects[sel].material];
        m.baseColor = c.linear();
        m.colorId = c.id;
        int preset = m.preset;
        if (c.group == ColorGroup::Transparent) preset = PRESET_TRANSPARENT;
        else if (c.group == ColorGroup::Metallic && preset != PRESET_CHROME && preset != PRESET_BRUSHED) preset = PRESET_PEARL;
        else if (c.group == ColorGroup::Standard && (preset == PRESET_TRANSPARENT || preset == PRESET_PEARL || preset == PRESET_CHROME || preset == PRESET_BRUSHED))
            preset = PRESET_PLASTIC;
        if (preset != m.preset || preset == PRESET_EMISSIVE) applyPreset(m, preset, 1.0f);
    }
    markDirty();
}

void App::setPreset(int preset) {
    if (m_selected < 0) return;
    pushUndo();
    for (int sel : selectionAll()) applyPreset(m_scene.materials[m_scene.objects[sel].material], preset, 1.0f);
    markDirty();
}

// --------------------------------------------------------------------------- output

ImTextureID App::makeThumb(const std::vector<uint8_t>& img, uint32_t w, uint32_t h) {
    const uint32_t tw = 256, th = 144;
    // centre-crop to 16:9 then box filter
    float srcAspect = (float)w / h, dstAspect = (float)tw / th;
    float cw = (float)w, ch = (float)h;
    if (srcAspect > dstAspect) cw = h * dstAspect;
    else ch = w / dstAspect;
    float ox = (w - cw) * 0.5f, oy = (h - ch) * 0.5f;
    std::vector<uint8_t> out(tw * th * 4);
    for (uint32_t y = 0; y < th; ++y)
        for (uint32_t x = 0; x < tw; ++x) {
            int x0 = (int)(ox + x * cw / tw), x1 = std::max(x0 + 1, (int)(ox + (x + 1) * cw / tw));
            int y0 = (int)(oy + y * ch / th), y1 = std::max(y0 + 1, (int)(oy + (y + 1) * ch / th));
            uint32_t acc[3] = {}, n = 0;
            for (int yy = y0; yy < y1 && yy < (int)h; ++yy)
                for (int xx = x0; xx < x1 && xx < (int)w; ++xx) {
                    const uint8_t* p = &img[((size_t)yy * w + xx) * 4];
                    acc[0] += p[0]; acc[1] += p[1]; acc[2] += p[2]; ++n;
                }
            uint8_t* q = &out[((size_t)y * tw + x) * 4];
            n = std::max(n, 1u);
            q[0] = (uint8_t)(acc[0] / n); q[1] = (uint8_t)(acc[1] / n); q[2] = (uint8_t)(acc[2] / n); q[3] = 255;
        }
    return m_gui.createTexture(out.data(), tw, th);
}

void App::clearViews() {
    for (auto& v : m_views)
        if (v.thumb) m_gui.releaseTexture(v.thumb);
    m_views.clear();
}

void App::generateThumbnails() {
    RenderSettings rs = m_rs;
    rs.maxBounces = 5;
    rs.samplesPerFrame = 4;
    rs.maxSamples = 1 << 30;
    rs.bevelSamples = 2;
    rs.raster = false;
    Camera saved = m_scene.camera;
    ViewRect vp;
    vp.w = 512;
    vp.h = 288;
    for (auto& v : m_views) {
        m_scene.camera = v.cam;
        if (m_autoFocus && m_scene.camera.dof) m_scene.camera.focusDistance = m_scene.camera.distance;
        m_renderer.resetAccumulation();
        for (int i = 0; i < 8; ++i) m_renderer.render(m_scene, rs, vp, -1, false);
        std::vector<uint8_t> img;
        uint32_t w, h;
        if (m_renderer.readViewport(img, w, h)) v.thumb = makeThumb(img, w, h);
    }
    m_scene.camera = saved;
    m_renderer.resetAccumulation();
}

void App::saveScreenshot(bool window) {
    std::vector<uint8_t> img;
    uint32_t w, h;
    bool ok = window ? m_renderer.readBackBuffer(img, w, h) : m_renderer.readViewport(img, w, h);
    if (!ok) return;
    std::wstring dir = m_exeDir + L"\\captures";
    CreateDirectoryW(dir.c_str(), nullptr);
    time_t t = time(nullptr);
    tm lt;
    localtime_s(&lt, &t);
    wchar_t name[128];
    swprintf(name, 128, L"\\viewport_%04d%02d%02d_%02d%02d%02d_%uspp.png", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
             lt.tm_hour, lt.tm_min, lt.tm_sec, m_renderer.sampleCount());
    if (savePng(dir + name, img, w, h)) status(L"Capture saved: captures" + std::wstring(name), 1);
}

} // namespace app

// --------------------------------------------------------------------------- entry point

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    app::Options opt;
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        auto next = [&]() -> std::wstring { return i + 1 < argc ? std::wstring(argv[++i]) : std::wstring(); };
        if (a == L"--scene") { std::wstring sc = next(); opt.scene = sc == L"studio" ? app::SCENE_STUDIO : (sc == L"lookdev" ? app::SCENE_LOOKDEV : app::SCENE_CORNELL); }
        else if (a == L"--hdri") opt.hdri = next();
        else if (a == L"--tab") opt.inspectorTab = _wtoi(next().c_str());
        else if (a == L"--raster") opt.raster = _wtoi(next().c_str()) != 0;
        else if (a == L"--open") opt.openPath = next();
        else if (a.size() > 5 && a.find(L".json") != std::wstring::npos) opt.openPath = a;
        else if (a == L"--shot") opt.shotPath = next();
        else if (a == L"--shot-viewport") { opt.shotPath = next(); opt.shotViewport = true; }
        else if (a == L"--spp") opt.shotSpp = _wtoi(next().c_str());
        else if (a == L"--view") opt.view = _wtoi(next().c_str());
        else if (a == L"--select") opt.select = _wtoi(next().c_str());
        else if (a == L"--tool") opt.tool = _wtoi(next().c_str());
        else if (a == L"--quality") opt.quality = _wtoi(next().c_str());
        else if (a == L"--light") opt.light = _wtoi(next().c_str());
        else if (a == L"--settings") opt.openSettings = true;
        else if (a == L"--sizes") swscanf_s(next().c_str(), L"%f,%f,%f", &opt.sizes.x, &opt.sizes.y, &opt.sizes.z);
        else if (a == L"--main-color") swscanf_s(next().c_str(), L"%f,%f,%f", &opt.mainColor.x, &opt.mainColor.y, &opt.mainColor.z);
        else if (a == L"--workspace") { std::wstring w = next(); opt.workspace = w == L"graph" ? 1 : (w == L"material" ? 2 : 0); }
        else if (a == L"--ddgi") opt.ddgi = _wtoi(next().c_str()) != 0;
        else if (a == L"--probes") opt.probes = _wtoi(next().c_str());
        else if (a == L"--ddgi-raster") opt.ddgiRaster = _wtoi(next().c_str()) != 0;
        else if (a == L"--ddgi-capture") opt.ddgiCapture = _wtoi(next().c_str());
        else if (a == L"--ddgi-view") opt.ddgiView = _wtoi(next().c_str());
        else if (a == L"--temporal-cap") opt.temporalCap = _wtoi(next().c_str());
        else if (a == L"--hysteresis") opt.hysteresis = (float)_wtof(next().c_str());
        else if (a == L"--size") {
            std::wstring s = next();
            swscanf_s(s.c_str(), L"%dx%d", &opt.width, &opt.height);
        }
    }
    LocalFree(argv);
    app::App a;
    if (!a.init(inst, opt)) return 1;
    return a.run();
}
