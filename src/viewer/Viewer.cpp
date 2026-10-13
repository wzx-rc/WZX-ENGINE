// DDGI Viewer: minimal study viewer for the probe-based real-time GI.
// Cornell box with spheres, orbit camera, DDGI / path tracing / raster switch, probe display,
// animated sphere and light to watch the probes react, live GPU timings.
//
// Mouse: left or right drag = orbit, middle drag = pan, wheel = zoom.
// Keys: 1 DDGI, 2 path tracing (temporal) | V GI view (full / indirect / direct) | P probes
//       +/- probe count | H hysteresis | A animate sphere | L animate light | R reset view | F12 capture
#include "vp3d/Renderer.h"
#include "vp3d/UI.h"
#include <shellapi.h>
#include <wincodec.h>
#include <windowsx.h>
#include <cstdio>
#include <string>
#include <vector>

using namespace vp3d;

namespace {

struct Viewer {
    HWND hwnd = nullptr;
    Renderer renderer;
    UI ui;
    Scene scene;
    RenderSettings rs;
    Camera homeCam;
    float dpi = 1.0f;
    bool running = true, minimized = false;
    int mode = 0;                 // 0 DDGI, 1 path tracing, 2 raster
    bool animSphere = false, animLight = false;
    float animT = 0.0f;
    int animObject = -1;
    float3 animBase, lightBase;
    float lastMx = 0, lastMy = 0;
    uint64_t lastTick = 0;
    float frameMs = 16.0f;
    // Automated capture: --shot file.png --frames N
    std::wstring shotPath;
    int shotFrames = 120, frameCount = 0;
    bool vsync = true;
    double gpuSum = 0;            // mean GPU time over the second half of an automated run
    int gpuN = 0;

    void buildScene();
    void applyMode();
    void frame();
    void drawHud();
    bool saveCapture(const std::wstring& path);
    LRESULT wndProc(HWND, UINT, WPARAM, LPARAM);
};

Viewer* g_viewer = nullptr;

LRESULT CALLBACK StaticWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    return g_viewer ? g_viewer->wndProc(h, m, w, l) : DefWindowProcW(h, m, w, l);
}

Material makeMat(const char* name, float3 color, float rough, float metal = 0.0f, float spec = 0.5f) {
    Material m;
    m.name = name;
    m.baseColor = color;
    m.roughness = rough;
    m.metallic = metal;
    m.specular = spec;
    return m;
}

// Classic Cornell box (cm units, open front) with spheres of a few materials.
void Viewer::buildScene() {
    Scene& s = scene;
    s = Scene();
    s.name = "Cornell Box spheres";
    s.unitsPerMeter = 100.0f;
    s.env.top = s.env.horizon = s.env.bottom = float3(0, 0, 0);
    s.env.intensity = 0.0f;

    int white = s.addMaterial(makeMat("Blanc", float3(0.74f, 0.73f, 0.70f), 0.95f, 0, 0.25f));
    int red = s.addMaterial(makeMat("Rouge", float3(0.72f, 0.045f, 0.03f), 0.95f, 0, 0.25f));
    int green = s.addMaterial(makeMat("Vert", float3(0.10f, 0.52f, 0.025f), 0.95f, 0, 0.25f));
    const float H = 55.5f, W = 27.75f, T = 1.0f, D = 30.0f;
    auto wall = [&](const char* name, float3 a, float3 b, int m) {
        Object o;
        o.name = name;
        o.mesh = s.addMesh(name, makeBox(a, b));
        o.material = m;
        s.addObject(o);
    };
    wall("Sol", {-W - T, -T, -W - T}, {W + T, 0, W + D}, white);
    wall("Plafond", {-W - T, H, -W - T}, {W + T, H + T, W + D}, white);
    wall("Fond", {-W - T, 0, -W - T}, {W + T, H, -W}, white);
    wall("Gauche", {-W - T, 0, -W}, {-W, H, W + D}, red);
    wall("Droite", {W, 0, -W}, {W + T, H, W + D}, green);

    int sphere = s.addMesh("sphere", makeSphere(1.0f, 96, 48));
    auto ball = [&](const char* name, float3 pos, float r, const Material& m) {
        Object o;
        o.name = name;
        o.mesh = sphere;
        o.material = s.addMaterial(m);
        o.position = pos;
        o.scale = float3(r);
        return s.addObject(o);
    };
    ball("Diffuse blanche", {-12.0f, 11.0f, -8.0f}, 11.0f, makeMat("Diffus", float3(0.80f, 0.80f, 0.78f), 0.9f, 0, 0.3f));
    ball("Miroir", {11.0f, 9.0f, -12.0f}, 9.0f, makeMat("Chrome", float3(0.95f, 0.93f, 0.88f), 0.04f, 1.0f));
    ball("Bleue", {13.0f, 6.0f, 10.0f}, 6.0f, makeMat("Bleu", float3(0.05f, 0.18f, 0.75f), 0.85f, 0, 0.3f));
    ball("Jaune brillante", {-6.0f, 5.0f, 14.0f}, 5.0f, makeMat("Jaune", float3(0.85f, 0.55f, 0.04f), 0.25f, 0, 0.6f));
    ball("Or rugueux", {2.0f, 4.0f, 2.0f}, 4.0f, makeMat("Or", float3(1.0f, 0.71f, 0.29f), 0.35f, 1.0f));
    animObject = ball("Orange (animée)", {0.0f, 32.0f, 0.0f}, 5.0f, makeMat("Orange", float3(0.9f, 0.25f, 0.02f), 0.9f, 0, 0.3f));
    animBase = s.objects[animObject].position;

    AreaLight l;
    l.name = "Plafonnier";
    l.position = {0, H - 0.05f, -2.0f};
    l.target = {0, 0, -2.0f};
    l.width = 14.0f;
    l.height = 11.0f;
    l.color = float3(1.0f, 0.93f, 0.82f);
    l.intensity = 48.0f;
    s.lights.push_back(l);
    lightBase = l.position;

    Camera& c = s.camera;
    c.target = {0, H * 0.47f, 0};
    c.yaw = 0.0f;
    c.pitch = 0.0f;
    c.distance = 98.0f;
    c.fovY = radians(37.0f);
    homeCam = c;
}

void Viewer::applyMode() {
    rs.raster = false;
    rs.ddgi = mode == 0;
    rs.temporal = mode == 1;
    rs.maxBounces = 6;
    rs.maxSamples = 4096;
    renderer.resetAccumulation();
}

void Viewer::frame() {
    uint64_t now = GetTickCount64();
    float dt = lastTick ? std::min((now - lastTick) * 0.001f, 0.1f) : 0.016f;
    if (lastTick) frameMs = lerpf(frameMs, (float)(now - lastTick), 0.1f);
    lastTick = now;

    RECT cr;
    GetClientRect(hwnd, &cr);
    int pw = std::max(16, (int)(cr.right - cr.left)), ph = std::max(16, (int)(cr.bottom - cr.top));
    ui.newFrame(dpi, pw / dpi, ph / dpi);

    // Camera
    bool moved = false;
    Camera& c = scene.camera;
    float dx = ui.mx - lastMx, dy = ui.my - lastMy;
    lastMx = ui.mx;
    lastMy = ui.my;
    if ((ui.mouseDown[0] || ui.mouseDown[1]) && (dx != 0 || dy != 0)) {
        c.yaw -= dx * 0.006f;
        c.pitch = clampf(c.pitch + dy * 0.006f, -1.5f, 1.5f);
        moved = true;
    }
    if (ui.mouseDown[2] && (dx != 0 || dy != 0)) {
        float3 r, u, f;
        c.basis(r, u, f);
        float k = c.distance * 0.0015f;
        c.target = c.target - r * (dx * k) + u * (dy * k);
        moved = true;
    }
    if (ui.wheel != 0) {
        c.distance = clampf(c.distance * std::pow(0.88f, ui.wheel), 5.0f, 400.0f);
        moved = true;
    }

    // Keys
    if (ui.keyPressed('1')) { mode = 0; applyMode(); }
    if (ui.keyPressed('2')) { mode = 1; applyMode(); }
    if (ui.keyPressed('V')) { rs.ddgiView = (rs.ddgiView + 1) % 3; moved = true; }
    if (ui.keyPressed('P')) { rs.ddgiShowProbes = !rs.ddgiShowProbes; moved = true; }
    if (ui.keyPressed(VK_ADD) || ui.keyPressed(VK_OEM_PLUS)) rs.ddgiProbeBudget = std::min(rs.ddgiProbeBudget * 2, 16384);
    if (ui.keyPressed(VK_SUBTRACT) || ui.keyPressed(VK_OEM_MINUS)) rs.ddgiProbeBudget = std::max(rs.ddgiProbeBudget / 2, 64);
    if (ui.keyPressed('H')) rs.ddgiHysteresis = rs.ddgiHysteresis > 0.95f ? 0.9f : 0.97f;
    if (ui.keyPressed('A')) animSphere = !animSphere;
    if (ui.keyPressed('L')) animLight = !animLight;
    if (ui.keyPressed('R')) { c = homeCam; moved = true; }
    if (ui.keyPressed(VK_F12)) saveCapture(L"ddgi_viewer_capture.png");
    if (ui.keyPressed(VK_ESCAPE)) running = false;

    // Animation: a sphere bouncing across the box, the ceiling light sliding.
    if (animSphere || animLight) {
        animT += dt;
        if (animSphere) {
            Object& o = scene.objects[animObject];
            o.position = animBase + float3(std::sin(animT * 0.9f) * 16.0f, std::sin(animT * 1.7f) * 12.0f, std::cos(animT * 0.9f) * 10.0f);
        }
        if (animLight) {
            AreaLight& l = scene.lights[0];
            l.position = lightBase + float3(std::sin(animT * 0.6f) * 9.0f, 0, std::cos(animT * 0.6f) * 9.0f);
            l.target = l.position - float3(0, 50, 0);
        }
        moved = true;
    }
    if (moved) renderer.resetAccumulation();

    ViewRect vp;
    vp.x = 0;
    vp.y = 0;
    vp.w = pw;
    vp.h = ph;
    renderer.render(scene, rs, vp, -1);
    ui.beginDraw(renderer.backBufferIndex());
    drawHud();
    ui.end();

    ++frameCount;
    if (frameCount > shotFrames / 2) { gpuSum += renderer.gpuFrameMs(); ++gpuN; }
    if (!shotPath.empty() && frameCount >= shotFrames) {
        saveCapture(shotPath);
        if (FILE* f = _wfopen((shotPath + L".txt").c_str(), L"w")) {
            fprintf(f, "%ux%u mode=%d probes=%u gpu_ms_mean=%.3f update_ms=%.3f blend_ms=%.3f\n", renderer.width(), renderer.height(), mode,
                    renderer.giProbeCount(), gpuN ? gpuSum / gpuN : 0.0, renderer.giUpdateMs(), renderer.giBlendMs());
            fclose(f);
        }
        running = false;
    }
    renderer.present(vsync);
}

void Viewer::drawHud() {
    const wchar_t* modes[] = {L"GI sondes (DDGI)", L"Path tracing", L"Raster"};
    const wchar_t* views[] = {L"complet", L"indirect seul", L"direct seul"};
    std::vector<std::wstring> lines;
    wchar_t b[200];
    swprintf(b, 200, L"%s", modes[mode]);
    lines.push_back(b);
    swprintf(b, 200, L"GPU %.2f ms   ·   image %.1f ms", renderer.gpuFrameMs(), frameMs);
    lines.push_back(b);
    if (mode == 0) {
        swprintf(b, 200, L"Sondes %u (espacement %.1f cm)", renderer.giProbeCount(), renderer.giSpacing());
        lines.push_back(b);
        swprintf(b, 200, L"Mise à jour %.2f ms  +  fusion %.2f ms", renderer.giUpdateMs(), renderer.giBlendMs());
        lines.push_back(b);
        swprintf(b, 200, L"Vue : %s   ·   hystérésis %.2f", views[rs.ddgiView], rs.ddgiHysteresis);
        lines.push_back(b);
    } else if (mode == 1) {
        swprintf(b, 200, L"Historique temporel : %d images", rs.temporalCap);
        lines.push_back(b);
    }
    swprintf(b, 200, L"Animation : sphère %s · lumière %s", animSphere ? L"oui" : L"non", animLight ? L"oui" : L"non");
    lines.push_back(b);
    float y = 12, h = 22.0f * lines.size() + 14;
    ui.fill(Rect(12, y, 330, h), Color::hex(0x101317, 0.82f), 6);
    for (size_t i = 0; i < lines.size(); ++i)
        ui.text(lines[i], Rect(24, y + 8 + 22.0f * i, 310, 20), i == 0 ? Color::hex(0x6CB4FF) : Color::hex(0xE6E8EB), 13, i == 0);

    const wchar_t* help =
        L"1 DDGI   2 Path tracing temporel   ·   V vue GI   P sondes   +/- nombre de sondes   H hystérésis   ·   "
        L"A anime la sphère   L anime la lumière   R vue   F12 capture";
    ui.fill(Rect(12, ui.height - 38, ui.width - 24, 26), Color::hex(0x101317, 0.75f), 6);
    ui.text(help, Rect(22, ui.height - 35, ui.width - 44, 20), Color::hex(0xB9BEC6), 12);
}

bool Viewer::saveCapture(const std::wstring& path) {
    std::vector<uint8_t> rgba;
    uint32_t w, h;
    if (!renderer.readBackBuffer(rgba, w, h)) return false;
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

LRESULT Viewer::wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CLOSE: running = false; DestroyWindow(h); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_SIZE:
        minimized = wp == SIZE_MINIMIZED;
        if (!minimized && renderer.device()) {
            UINT w = LOWORD(lp), hh = HIWORD(lp);
            if (w > 0 && hh > 0 && (w != renderer.width() || hh != renderer.height())) {
                ui.releaseTargets();
                renderer.resize(w, hh);
                ui.createTargets(renderer.swapChain(), Renderer::kBackBuffers);
                renderer.resetAccumulation();
            }
        }
        return 0;
    case WM_MOUSEMOVE: ui.onMouseMove((float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp)); return 0;
    case WM_LBUTTONDOWN: SetCapture(h); ui.onMouseButton(0, true); return 0;
    case WM_LBUTTONUP: ReleaseCapture(); ui.onMouseButton(0, false); return 0;
    case WM_RBUTTONDOWN: SetCapture(h); ui.onMouseButton(1, true); return 0;
    case WM_RBUTTONUP: ReleaseCapture(); ui.onMouseButton(1, false); return 0;
    case WM_MBUTTONDOWN: SetCapture(h); ui.onMouseButton(2, true); return 0;
    case WM_MBUTTONUP: ReleaseCapture(); ui.onMouseButton(2, false); return 0;
    case WM_MOUSEWHEEL: ui.onWheel(GET_WHEEL_DELTA_WPARAM(wp) / (float)WHEEL_DELTA); return 0;
    case WM_KEYDOWN: case WM_SYSKEYDOWN: ui.onKey((int)wp, true); if (msg == WM_SYSKEYDOWN && wp != VK_F4) return 0; break;
    case WM_KEYUP: case WM_SYSKEYUP: ui.onKey((int)wp, false); return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    Viewer v;
    g_viewer = &v;
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        auto next = [&]() -> std::wstring { return i + 1 < argc ? std::wstring(argv[++i]) : std::wstring(); };
        if (a == L"--shot") v.shotPath = next();
        else if (a == L"--frames") v.shotFrames = _wtoi(next().c_str());
        else if (a == L"--mode") v.mode = _wtoi(next().c_str());
        else if (a == L"--probes-visible") v.rs.ddgiShowProbes = _wtoi(next().c_str()) != 0;
        else if (a == L"--view") v.rs.ddgiView = _wtoi(next().c_str());
        else if (a == L"--probes") v.rs.ddgiProbeBudget = _wtoi(next().c_str());
        else if (a == L"--anim") v.animSphere = v.animLight = _wtoi(next().c_str()) != 0;
        else if (a == L"--light-anim") v.animLight = _wtoi(next().c_str()) != 0;
        else if (a == L"--cap") v.rs.temporalCap = _wtoi(next().c_str());
        else if (a == L"--vsync") v.vsync = _wtoi(next().c_str()) != 0;
    }
    LocalFree(argv);

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe;
    dir = dir.substr(0, dir.find_last_of(L"\\/"));

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = StaticWndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"DDGIViewerWindow";
    RegisterClassExW(&wc);
    UINT sysDpi = GetDpiForSystem();
    RECT r = {0, 0, (LONG)(1280 * sysDpi / 96), (LONG)(800 * sysDpi / 96)};
    AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, sysDpi);
    v.hwnd = CreateWindowExW(0, wc.lpszClassName, L"DDGI Viewer - WZX ENGINE", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                             r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, nullptr);
    if (!v.hwnd) return 1;
    v.dpi = GetDpiForWindow(v.hwnd) / 96.0f;
    RECT cr;
    GetClientRect(v.hwnd, &cr);
    std::string err;
    if (!v.renderer.init(v.hwnd, cr.right - cr.left, cr.bottom - cr.top, dir + L"\\shaders", &err)) {
        MessageBoxA(v.hwnd, err.c_str(), "DDGI Viewer", MB_ICONERROR);
        return 1;
    }
    if (!v.ui.init(v.renderer.device(), v.renderer.queue())) return 1;
    v.ui.createTargets(v.renderer.swapChain(), Renderer::kBackBuffers);
    v.buildScene();
    v.renderer.setScene(v.scene);
    v.applyMode();
    ShowWindow(v.hwnd, SW_SHOWDEFAULT);

    MSG msg;
    while (v.running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) v.running = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!v.running) break;
        if (v.minimized) { Sleep(30); continue; }
        v.frame();
    }
    v.renderer.waitIdle();
    v.ui.shutdown();
    v.renderer.shutdown();
    return 0;
}
