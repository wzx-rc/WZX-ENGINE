#include "App.h"
#include <shellapi.h>
#include <wincodec.h>
#include <windowsx.h>
#include <ctime>

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
    if (fmt != GUID_WICPixelFormat32bppRGBA) {
        // swap to BGRA
        for (size_t i = 0; i + 3 < rgba.size(); i += 4) std::swap(rgba[i], rgba[i + 2]);
    }
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

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = StaticWndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.lpszClassName = L"Viewport3DWindow";
    RegisterClassExW(&wc);

    UINT sysDpi = GetDpiForSystem();
    m_dpi = sysDpi / 96.0f;
    RECT r = {0, 0, (LONG)(opt.width * m_dpi), (LONG)(opt.height * m_dpi)};
    AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, sysDpi);
    m_hwnd = CreateWindowExW(0, wc.lpszClassName, L"Viewport 3D", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                             r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, nullptr);
    if (!m_hwnd) return false;
    m_dpi = GetDpiForWindow(m_hwnd) / 96.0f;

    RECT cr;
    GetClientRect(m_hwnd, &cr);
    std::string err;
    if (!m_renderer.init(m_hwnd, cr.right - cr.left, cr.bottom - cr.top, m_exeDir + L"\\shaders", &err)) {
        MessageBoxA(m_hwnd, err.c_str(), "Viewport 3D - DirectX 12", MB_ICONERROR);
        return false;
    }
    if (!m_ui.init(m_renderer.device(), m_renderer.queue())) {
        MessageBoxA(m_hwnd, "Direct2D / D3D11On12 initialisation failed", "Viewport 3D", MB_ICONERROR);
        return false;
    }
    m_ui.createTargets(m_renderer.swapChain(), Renderer::kBackBuffers);
    resetPanels();
    loadPrefs();
    m_rs.raster = opt.raster;
    m_rs.ddgi = opt.ddgi;
    m_rs.ddgiProbeBudget = opt.probes;
    m_rs.ddgiView = opt.ddgiView;
    m_rs.ddgiTemporal = opt.temporal;
    m_rs.ddgiHysteresis = opt.hysteresis;
    if (opt.ddgi) m_rs.raster = false;
    ShowWindow(m_hwnd, SW_SHOWDEFAULT);
    UpdateWindow(m_hwnd);

    applyQuality();
    layout();
    if (opt.bounces > 0) m_rs.maxBounces = opt.bounces;
    loadScene(opt.scene);
    if (opt.view >= 0 && opt.view < (int)m_views.size()) {
        m_activeView = opt.view;
        m_scene.camera = m_views[opt.view].cam;
    }
    if (!opt.openPath.empty()) loadSceneFile(opt.openPath);
    if (opt.select >= 0 && opt.select < (int)m_scene.objects.size()) m_selected = opt.select;
    if (opt.tool >= 0 && opt.tool <= TOOL_ROTATE) m_tool = (Tool)opt.tool;
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
    m_ui.shutdown();
    m_renderer.shutdown();
    return 0;
}

LRESULT App::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CLOSE: m_running = false; DestroyWindow(hwnd); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_SIZE:
        m_minimized = wp == SIZE_MINIMIZED;
        if (!m_minimized && m_renderer.device()) {
            UINT w = LOWORD(lp), h = HIWORD(lp);
            if (w > 0 && h > 0 && (w != m_renderer.width() || h != m_renderer.height())) {
                m_ui.releaseTargets();
                m_renderer.resize(w, h);
                m_ui.createTargets(m_renderer.swapChain(), Renderer::kBackBuffers);
                markDirty();
                if (m_renderer.device()) frame();
            }
        }
        return 0;
    case WM_DPICHANGED: {
        m_dpi = HIWORD(wp) / 96.0f;
        RECT* r = (RECT*)lp;
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mm = (MINMAXINFO*)lp;
        mm->ptMinTrackSize.x = (LONG)(1100 * m_dpi);
        mm->ptMinTrackSize.y = (LONG)(700 * m_dpi);
        return 0;
    }
    case WM_MOUSEMOVE: m_ui.onMouseMove((float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp)); return 0;
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: SetCapture(hwnd); m_ui.onMouseButton(0, true); return 0;
    case WM_LBUTTONUP: ReleaseCapture(); m_ui.onMouseButton(0, false); return 0;
    case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: SetCapture(hwnd); m_ui.onMouseButton(1, true); return 0;
    case WM_RBUTTONUP: ReleaseCapture(); m_ui.onMouseButton(1, false); return 0;
    case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: SetCapture(hwnd); m_ui.onMouseButton(2, true); return 0;
    case WM_MBUTTONUP: ReleaseCapture(); m_ui.onMouseButton(2, false); return 0;
    case WM_MOUSEWHEEL: m_ui.onWheel(GET_WHEEL_DELTA_WPARAM(wp) / (float)WHEEL_DELTA); return 0;
    case WM_CHAR: m_ui.onChar((wchar_t)wp); return 0;
    case WM_KEYDOWN: case WM_SYSKEYDOWN:
        m_ui.onKey((int)wp, true);
        if (msg == WM_SYSKEYDOWN && wp != VK_F4) return 0;
        break;
    case WM_KEYUP: case WM_SYSKEYUP: m_ui.onKey((int)wp, false); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void App::layout() {
    RECT cr;
    GetClientRect(m_hwnd, &cr);
    m_W = (cr.right - cr.left) / m_dpi;
    m_H = (cr.bottom - cr.top) / m_dpi;
    const float topH = 40, toolH = 38;
    m_rcTop = Rect(0, 0, m_W, topH);
    m_rcTool = Rect(0, topH, m_W, toolH);
    layoutPanels(topH + toolH);   // docked / floating panels + the viewport rectangle
    m_vpPx.x = (int)std::lround(m_rcView.x * m_dpi);
    m_vpPx.y = (int)std::lround(m_rcView.y * m_dpi);
    m_vpPx.w = std::max(16, (int)std::lround(m_rcView.w * m_dpi));
    m_vpPx.h = std::max(16, (int)std::lround(m_rcView.h * m_dpi));
    // Keep the DIP rectangle exactly on the pixel grid of the rendered image.
    m_rcView = Rect(m_vpPx.x / m_dpi, m_vpPx.y / m_dpi, m_vpPx.w / m_dpi, m_vpPx.h / m_dpi);
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

void App::loadScene(int id) {
    m_sceneId = id;
    if (id == SCENE_CORNELL) buildCornellBox(m_scene);
    else buildStudioScene(m_scene);
    m_selected = -1;
    m_multi.clear();
    m_undo.clear();
    m_redo.clear();
    m_rs.exposure = id == SCENE_CORNELL ? 1.0f : 1.3f;
    if (id == SCENE_CORNELL) m_scene.env.ambient = float3(0.16f, 0.15f, 0.13f);
    m_rs.vignette = id == SCENE_CORNELL ? 0.10f : 0.18f;
    m_snapMode = 0;
    m_tool = id == SCENE_CORNELL ? TOOL_SELECT : m_lastTool;
    m_gz = GizmoDrag();
    m_renderer.setScene(m_scene);
    m_views.clear();
    for (auto& v : defaultViews(m_scene, id == SCENE_CORNELL)) m_views.push_back({v.name, v.cam, nullptr});
    m_activeView = 0;
    generateThumbnails();
    m_scene.camera = m_views[0].cam;
    markDirty();
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
    layout();
    m_ui.newFrame(m_dpi, m_W, m_H);
    m_interacting = false;
    panelInput();
    handleShortcuts();
    viewportInput();
    savePrefs();
    if (m_turntable) {
        m_scene.camera.yaw += 0.006f;
        cameraChanged();
    }
    if (m_autoFocus && m_scene.camera.dof) m_scene.camera.focusDistance = m_scene.camera.distance;

    // Adaptive samples per frame: 1 while interacting, more when the view is still.
    if (m_interacting || m_opt.motion) m_adaptiveSpp = 1;
    else {
        float g = m_renderer.gpuFrameMs();
        if (g < 9.0f && m_adaptiveSpp < 8) m_adaptiveSpp++;
        else if (g > 22.0f && m_adaptiveSpp > 1) m_adaptiveSpp--;
    }
    m_rs.samplesPerFrame = m_adaptiveSpp;

    m_renderer.setExtraSelection(m_multi);
    m_renderer.render(m_scene, m_rs, m_vpPx, m_selected);
    m_ui.beginDraw(m_renderer.backBufferIndex());
    drawUI();
    m_ui.end();

    // Refresh the thumbnail of the active bookmark once the image is clean.
    if (m_activeView >= 0 && m_activeView < (int)m_views.size() && !m_thumbFresh && !m_rs.raster && m_renderer.sampleCount() >= 128) {
        m_thumbFresh = true;
        std::vector<uint8_t> img;
        uint32_t w, h;
        if (m_renderer.readViewport(img, w, h)) m_views[m_activeView].thumb = makeThumb(img, w, h);
    }

    // Automated capture (command line).
    if (m_opt.benchPath.empty() && !m_opt.shotPath.empty() && (int)m_renderer.sampleCount() >= m_opt.shotSpp && ++m_frameCounter > 3) {
        std::vector<uint8_t> img;
        uint32_t w, h;
        bool ok = m_opt.shotWindow ? m_renderer.readBackBuffer(img, w, h) : m_renderer.readViewport(img, w, h);
        if (ok) savePng(m_opt.shotPath, img, w, h);
        m_running = false;
    }
    m_renderer.present(m_opt.vsync);
    if (!m_opt.benchPath.empty()) benchFrame();
}

// Benchmark mode: GPU timings of each frame and the mean absolute change of the displayed image between
// consecutive frames (flicker, 8-bit sRGB luma, in % of full scale). Writes a CSV and a summary, then exits.
void App::benchFrame() {
    std::vector<uint8_t> img;
    uint32_t w = 0, h = 0;
    if (!m_renderer.readViewport(img, w, h)) return;
    float flicker = 0;
    if (m_benchPrev.size() == img.size()) {
        double s = 0;
        for (size_t i = 0; i + 3 < img.size(); i += 4) {
            float a = 0.2126f * img[i] + 0.7152f * img[i + 1] + 0.0722f * img[i + 2];
            float b = 0.2126f * m_benchPrev[i] + 0.7152f * m_benchPrev[i + 1] + 0.0722f * m_benchPrev[i + 2];
            s += std::fabs(a - b);
        }
        flicker = (float)(s / (img.size() / 4) / 255.0 * 100.0);
    }
    m_benchPrev = img;
    m_bench.push_back({m_renderer.gpuFrameMs(), m_rs.ddgi ? m_renderer.giUpdateMs() : 0.0f, m_rs.ddgi ? m_renderer.giBlendMs() : 0.0f, flicker});
    if (m_opt.motion) m_renderer.resetAccumulation();
    if ((int)m_bench.size() < m_opt.benchFrames) return;

    FILE* f = _wfopen(m_opt.benchPath.c_str(), L"w");
    if (f) {
        fprintf(f, "frame,gpu_ms,gi_update_ms,gi_blend_ms,flicker_pct\n");
        for (size_t i = 0; i < m_bench.size(); ++i)
            fprintf(f, "%zu,%.4f,%.4f,%.4f,%.4f\n", i, m_bench[i].gpu, m_bench[i].giUpdate, m_bench[i].giBlend, m_bench[i].flicker);
        // Summary over the second half (warm-up excluded).
        size_t a = m_bench.size() / 2;
        auto stat = [&](float BenchRow::*m, float& mean, float& p95) {
            std::vector<float> v;
            for (size_t i = a; i < m_bench.size(); ++i) v.push_back(m_bench[i].*m);
            std::sort(v.begin(), v.end());
            double s = 0;
            for (float x : v) s += x;
            mean = v.empty() ? 0 : (float)(s / v.size());
            p95 = v.empty() ? 0 : v[std::min(v.size() - 1, (size_t)(v.size() * 0.95))];
        };
        float gm, gp, um, up, bm, bp, fm, fp;
        stat(&BenchRow::gpu, gm, gp);
        stat(&BenchRow::giUpdate, um, up);
        stat(&BenchRow::giBlend, bm, bp);
        stat(&BenchRow::flicker, fm, fp);
        fprintf(f, "# mode=%s motion=%d res=%ux%u probes=%u spacing=%.3f frames=%zu\n", m_rs.ddgi ? "ddgi" : "pathtrace",
                (int)m_opt.motion, w, h, m_rs.ddgi ? m_renderer.giProbeCount() : 0u, m_renderer.giSpacing(), m_bench.size());
        fprintf(f, "# gpu_ms mean=%.3f p95=%.3f | gi_update_ms mean=%.3f p95=%.3f | gi_blend_ms mean=%.3f p95=%.3f | flicker_pct mean=%.3f p95=%.3f\n",
                gm, gp, um, up, bm, bp, fm, fp);
        fclose(f);
    }
    if (!m_opt.shotPath.empty()) savePng(m_opt.shotPath, img, w, h);
    m_running = false;
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
        float3 p = qRot(rot, float3((i & 1) ? b.x : a.x, (i & 2) ? b.y : a.y, (i & 4) ? b.z : a.z)) + pos;
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
    if (m_selected >= 0) addObj(m_scene.objects[m_selected]);
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

bool App::overViewportWidgets(float x, float y) const {
    // must match the widgets drawn by drawViewportOverlay()
    const Rect& v = m_rcView;
    Rect bar(v.x + 8, v.y + 8, 444, 30);
    Rect stats(v.r() - 178, v.y + 8, 170, 116);
    Rect axis(v.x + 8, v.b() - 96, 88, 88);
    Rect snap(v.r() - 150, v.b() - 38, 142, 30);
    return bar.contains(x, y) || stats.contains(x, y) || axis.contains(x, y) || snap.contains(x, y);
}

void App::viewportInput() {
    UI& ui = m_ui;
    Camera& cam = m_scene.camera;
    bool inView = m_rcView.contains(ui.mx, ui.my) && !ui.blocked() && !ui.popupOpen() && !overViewportWidgets(ui.mx, ui.my) && !overPanels(ui.mx, ui.my) && m_gripActive < 0;
    float dx = ui.mx - m_lastX, dy = ui.my - m_lastY;
    m_lastX = ui.mx;
    m_lastY = ui.my;
    bool alt = ui.keyDown(VK_MENU);
    bool shift = ui.keyDown(VK_SHIFT);

    if (inView && ui.wheel != 0) {
        cam.distance *= std::pow(0.88f, ui.wheel);
        cam.distance = clampf(cam.distance, 0.5f, 2000.0f);
        cameraChanged();
    }
    if (inView && (ui.mousePressed[1] || (alt && ui.mousePressed[0]))) {
        if (shift) m_panning = true;
        else m_orbiting = true;
    }
    if (inView && ui.mousePressed[2]) m_panning = true;
    if (m_orbiting) {
        if (!(ui.mouseDown[1] || ui.mouseDown[0])) m_orbiting = false;
        else if (dx != 0 || dy != 0) {
            cam.yaw -= dx * 0.0065f;
            cam.pitch = clampf(cam.pitch + dy * 0.0065f, -1.55f, 1.55f);
            cameraChanged();
        }
    }
    if (m_panning) {
        if (!(ui.mouseDown[2] || ui.mouseDown[1])) m_panning = false;
        else if (dx != 0 || dy != 0) {
            float3 right, up, fwd;
            cam.basis(right, up, fwd);
            float k = cam.distance * std::tan(cam.fovY * 0.5f) * 2.0f / m_rcView.h;
            cam.target = cam.target - right * (dx * k) + up * (dy * k);
            cameraChanged();
        }
    }

    // Move / rotate manipulators of the selected object.
    bool gzBusy = gizmoInput(inView);
    marqueeInput(inView);
    if (m_marq.active) gzBusy = true;

    // Left button: selection / tools / focus.
    if (!gzBusy && inView && ui.mousePressed[0] && !alt) {
        float3 o, d;
        cameraRay(ui.mx, ui.my, o, d);
        PickResult pr = pick(m_scene, o, d);
        if (ui.mouseDoubleClick) {
            if (pr.object >= 0 || pr.t < 1e29f) {
                float3 p = pr.object >= 0 ? pr.position : o + d * cam.distance;
                setPivotKeepEye(p);
                cam.focusDistance = cam.distance;
                m_status = L"Pivot et mise au point sur le point cliqué";
                m_statusTime = GetTickCount();
                cameraChanged();
            }
        } else if (pr.object >= 0) {
            if (m_tool == TOOL_SELECT && shift) {
                // Shift+click: add / remove from the selection
                if (m_selected < 0) select(pr.object);
                else if (pr.object == m_selected) { if (!m_multi.empty()) { m_selected = m_multi.front(); m_multi.erase(m_multi.begin()); } else m_selected = -1; markDirty(); }
                else {
                    auto it = std::find(m_multi.begin(), m_multi.end(), pr.object);
                    if (it != m_multi.end()) m_multi.erase(it); else m_multi.push_back(pr.object);
                    markDirty();
                }
                return;
            }
            select(pr.object);
            if (m_tool == TOOL_MOVE) {
                pushUndo();
                m_dragging = true;
                m_dragMoved = false;
                m_dragPlaneY = pr.position.y;
                float3 op = m_scene.objects[pr.object].position;
                m_dragOffset = float3(op.x - pr.position.x, 0, op.z - pr.position.z);
            } else if (m_tool == TOOL_ROTATE) {
                rotateSelected(shift ? -90.0f : 90.0f);
            }
        } else if (m_tool == TOOL_SELECT) {
            // empty space in select mode: start a selection rectangle (a simple click clears the selection)
            m_marq.active = true;
            m_marq.moved = false;
            m_marq.x0 = ui.mx;
            m_marq.y0 = ui.my;
        } else {
            select(-1);
        }
    }
    if (m_dragging) {
        if (!ui.mouseDown[0]) {
            m_dragging = false;
            if (!m_dragMoved && !m_undo.empty()) m_undo.pop_back();
        } else if (m_selected >= 0 && (dx != 0 || dy != 0)) {
            float3 o, d;
            cameraRay(ui.mx, ui.my, o, d);
            if (std::fabs(d.y) > 1e-4f) {
                float t = (m_dragPlaneY - o.y) / d.y;
                if (t > 0) {
                    float3 p = o + d * t + m_dragOffset;
                    Object& ob = m_scene.objects[m_selected];
                    float3 np(p.x, ob.position.y, p.z);
                    if (m_magnet && m_snapMode == 0) {
                        np.x = std::round(np.x);
                        np.z = std::round(np.z);
                    }
                    if (np.x != ob.position.x || np.z != ob.position.z) {
                        ob.position = np;
                        m_dragMoved = true;
                        markDirty();
                    }
                }
            }
        }
    }
}

void App::handleShortcuts() {
    UI& ui = m_ui;
    if (ui.textFocused()) return;
    bool ctrl = ui.keyDown(VK_CONTROL);
    if (ui.keyPressed('P')) { m_rs.raster = !m_rs.raster; markDirty(); }
    if (ui.keyPressed('L')) { m_rs.raster = false; m_rs.ddgi = !m_rs.ddgi; markDirty(); }   // DDGI probe GI test
    if (ctrl && ui.keyPressed('S')) m_pendingAction = 1;
    if (ctrl && ui.keyPressed('O')) m_pendingAction = 2;
    if (ctrl && ui.keyPressed('Z')) undo();
    if (ctrl && ui.keyPressed('Y')) redo();
    if (ctrl && ui.keyPressed('D')) duplicateSelected();
    if (!ctrl) {
        if (ui.keyPressed('G') || ui.keyPressed('W') || ui.keyPressed('2')) m_tool = TOOL_MOVE;
        if (ui.keyPressed('R') || ui.keyPressed('E') || ui.keyPressed('3')) m_tool = TOOL_ROTATE;
        if (ui.keyPressed('Q')) m_tool = TOOL_SELECT;
        if (ui.keyPressed('F')) frameSelection();
        if (ui.keyPressed('T')) m_turntable = !m_turntable;
    }
    if (ui.keyPressed(VK_DELETE)) deleteSelected();
    if (ui.keyPressed(VK_ESCAPE)) select(-1);
    if (ui.keyPressed(VK_F12)) saveScreenshot(false);
    if (ui.keyPressed(VK_F1)) loadScene(SCENE_CORNELL);
    if (ui.keyPressed(VK_F2)) loadScene(SCENE_STUDIO);
    if (m_selected >= 0) {
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
        if (ui.keyPressed(VK_RIGHT)) nudgeSelected(rx, 0, rz);
        if (ui.keyPressed(VK_LEFT)) nudgeSelected(-rx, 0, -rz);
        if (ui.keyPressed(VK_UP)) nudgeSelected(fx, 0, fz);
        if (ui.keyPressed(VK_DOWN)) nudgeSelected(-fx, 0, -fz);
        if (ui.keyPressed(VK_PRIOR)) nudgeSelected(0, 1, 0);
        if (ui.keyPressed(VK_NEXT)) nudgeSelected(0, -1, 0);
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
    markDirty();
}

void App::select(int idx) {
    if (idx == m_selected && m_multi.empty()) return;
    m_selected = idx;
    m_multi.clear();
    markDirty();
}

void App::addPartFromLibrary(const PartDef& p) {
    pushUndo();
    // Drop the object at the pivot, on the ground or on top of the selection.
    float3 pos = m_scene.camera.target;
    pos.y = 0;
    if (m_selected >= 0) {
        const Object& s = m_scene.objects[m_selected];
        float3 a, b;
        m_scene.meshes[s.mesh].bounds(a, b);
        pos = s.position;
        pos.y = s.position.y + b.y;
    }
    int idx = addPart(m_scene, p.id, p.defaultColor, -1, pos, 0);
    m_selected = idx;
    m_multi.clear();
    m_tool = TOOL_MOVE;
    m_status = L"Objet ajouté : déplacez-le avec la souris, PgUp/PgDn pour la hauteur";
    m_statusTime = GetTickCount();
    markDirty();
}

void App::duplicateSelected() {
    if (m_selected < 0) return;
    pushUndo();
    Object o = m_scene.objects[m_selected];
    Material m = m_scene.materials[o.material];
    o.material = m_scene.addMaterial(m);
    o.position.x += 8.0f;
    m_selected = m_scene.addObject(o);
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
    Object& o = m_scene.objects[m_selected];
    o.position.x += (float)dx;
    o.position.z += (float)dz;
    o.position.y = std::max(0.0f, o.position.y + dy);
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

ComPtr<ID2D1Bitmap1> App::makeThumb(const std::vector<uint8_t>& img, uint32_t w, uint32_t h) {
    const uint32_t tw = 192, th = 108;
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
    return m_ui.createBitmap(out.data(), tw, th);
}

void App::generateThumbnails() {
    RenderSettings rs = m_rs;
    rs.maxBounces = 5;
    rs.samplesPerFrame = 4;
    rs.maxSamples = 1 << 30;
    rs.bevelSamples = 2;
    Camera saved = m_scene.camera;
    for (auto& v : m_views) {
        m_scene.camera = v.cam;
        if (m_autoFocus && m_scene.camera.dof) m_scene.camera.focusDistance = m_scene.camera.distance;
        m_renderer.resetAccumulation();
        for (int i = 0; i < 8; ++i) m_renderer.render(m_scene, rs, m_vpPx, -1, false);
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
    std::wstring path = dir + name;
    if (savePng(path, img, w, h)) {
        m_status = L"Capture enregistrée : captures" + std::wstring(name);
        m_statusTime = GetTickCount();
    }
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
        if (a == L"--scene") opt.scene = next() == L"studio" ? app::SCENE_STUDIO : app::SCENE_CORNELL;
        else if (a == L"--raster") opt.raster = _wtoi(next().c_str()) != 0;
        else if (a == L"--open") opt.openPath = next();
        else if (a.size() > 5 && a.find(L".json") != std::wstring::npos) opt.openPath = a;
        else if (a == L"--shot") opt.shotPath = next();
        else if (a == L"--shot-viewport") { opt.shotPath = next(); opt.shotWindow = false; }
        else if (a == L"--spp") opt.shotSpp = _wtoi(next().c_str());
        else if (a == L"--view") opt.view = _wtoi(next().c_str());
        else if (a == L"--select") opt.select = _wtoi(next().c_str());
        else if (a == L"--tool") opt.tool = _wtoi(next().c_str());
        else if (a == L"--quality") opt.quality = _wtoi(next().c_str());
        else if (a == L"--ddgi") opt.ddgi = _wtoi(next().c_str()) != 0;
        else if (a == L"--probes") opt.probes = _wtoi(next().c_str());
        else if (a == L"--ddgi-view") opt.ddgiView = _wtoi(next().c_str());
        else if (a == L"--temporal") opt.temporal = _wtoi(next().c_str());
        else if (a == L"--hysteresis") opt.hysteresis = (float)_wtof(next().c_str());
        else if (a == L"--bench") opt.benchPath = next();
        else if (a == L"--frames") opt.benchFrames = _wtoi(next().c_str());
        else if (a == L"--motion") opt.motion = _wtoi(next().c_str()) != 0;
        else if (a == L"--bounces") opt.bounces = _wtoi(next().c_str());
        else if (a == L"--vsync") opt.vsync = _wtoi(next().c_str()) != 0;
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





