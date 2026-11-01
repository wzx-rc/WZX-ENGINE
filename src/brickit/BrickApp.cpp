#include "BrickApp.h"
#include "app/Theme.h"
#include "backends/imgui_impl_win32.h"
#include "vp3d/shaders/Shared.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <random>
#include <shellapi.h>
#include <wincodec.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace brickit {

namespace th = app::th;
static BrickApp* g_app = nullptr;

static LRESULT CALLBACK StaticWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (g_app) return g_app->wndProc(h, m, w, l);
    return DefWindowProcW(h, m, w, l);
}

static bool fileExists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

bool BrickApp::savePng(const std::wstring& path, std::vector<uint8_t> rgba, uint32_t w, uint32_t h) {
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

bool BrickApp::init(HINSTANCE inst, const Options& opt) {
    g_app = this;
    m_opt = opt;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    QueryPerformanceFrequency(&m_qpf);

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
    wc.hIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
    wc.lpszClassName = L"WzxBrickIt";
    RegisterClassExW(&wc);

    UINT sysDpi = GetDpiForSystem();
    m_dpi = sysDpi / 96.0f;
    RECT r = {0, 0, (LONG)(opt.width * m_dpi), (LONG)(opt.height * m_dpi)};
    AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, sysDpi);
    m_hwnd = CreateWindowExW(0, wc.lpszClassName, L"BrickIt — WZX Engine", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                             r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, nullptr);
    if (!m_hwnd) return false;
    m_dpi = GetDpiForWindow(m_hwnd) / 96.0f;

    RECT cr;
    GetClientRect(m_hwnd, &cr);
    std::string err;
    if (!m_renderer.init(m_hwnd, cr.right - cr.left, cr.bottom - cr.top, m_exeDir + L"\\shaders", &err)) {
        MessageBoxA(m_hwnd, err.c_str(), "BrickIt - DirectX 12", MB_ICONERROR);
        return false;
    }
    if (!m_gui.init(m_hwnd, m_renderer, m_dpi)) {
        MessageBoxA(m_hwnd, "Dear ImGui initialisation failed", "BrickIt", MB_ICONERROR);
        return false;
    }
    th::loadThemes(m_exeDir + L"\\assets\\themes");
    th::loadSettings(m_exeDir + L"\\brickit_settings.ini");
    th::apply(m_dpi);

    if (!m_lib.load(m_exeDir + L"\\assets\\brickit", &err)) {
        MessageBoxA(m_hwnd, err.c_str(), "BrickIt - library", MB_ICONERROR);
        return false;
    }
    m_rs.raster = !opt.pathTrace;
    m_rs.denoise = true;
    m_rs.exposure = 1.0f;
    m_rs.tonemapper = 1;   // AgX
    setupStage();
    if (opt.grid) buildPartGrid();
    else if (opt.bench > 0) buildBench(opt.bench);
    rebuildScene();
    frameAll();

    ShowWindow(m_hwnd, SW_SHOWDEFAULT);
    UpdateWindow(m_hwnd);
    return true;
}

int BrickApp::run() {
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
    m_gui.shutdown();
    m_renderer.shutdown();
    return 0;
}

LRESULT BrickApp::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
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
    case WM_DPICHANGED: {
        RECT* r = (RECT*)lp;
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// --------------------------------------------------------------------------- scene

void BrickApp::setupStage() {
    m_scene = Scene();
    m_scene.name = "BrickIt";
    m_scene.unitsPerMeter = 100.0f;   // cm
    m_scene.gridScale = 0.8f;         // one stud
    m_partMesh.assign(m_lib.parts.size(), -1);

    Material g;
    g.name = "Ground";
    g.baseColor = float3(0.62f, 0.63f, 0.65f);
    g.roughness = 0.55f;
    g.flags = MAT_FLAG_GRID;
    Object o;
    o.name = "Ground";
    o.part = "GROUND";
    o.mesh = m_scene.addMesh("ground", makePlane(2000.0f));
    o.material = m_scene.addMaterial(g);
    o.selectable = false;
    m_scene.addObject(o);
    m_firstBrickObject = (int)m_scene.objects.size();

    // one material per palette colour: ABS plastic (light clear coat), transparent = tinted glass, metal colours = metal
    m_colorMaterial.clear();
    for (const Color& c : m_lib.palette) {
        Material m;
        m.name = c.name;
        m.baseColor = float3(srgbToLinear(c.srgb.x), srgbToLinear(c.srgb.y), srgbToLinear(c.srgb.z));
        m.roughness = 0.25f;
        m.specular = 0.5f;
        m.coat = 0.25f;
        m.coatRoughness = 0.08f;
        m.bevelRadius = 0.025f;
        if (c.group.find("trans") != std::string::npos) {
            m.transmission = 1.0f;
            m.roughness = 0.04f;
            m.coat = 0.0f;
        } else if (c.group.find("metal") != std::string::npos || c.group.find("chrome") != std::string::npos) {
            m.metallic = 1.0f;
            m.roughness = 0.22f;
            m.coat = 0.0f;
        }
        m_colorMaterial.push_back(m_scene.addMaterial(m));
    }

    // studio HDRI (assets/hdri, kept out of git) else the procedural sky
    std::wstring hdr = m_exeDir + L"\\assets\\hdri\\studio_small_09_2k.hdr";
    auto img = std::make_shared<EnvImage>();
    std::string err;
    if (fileExists(hdr) && loadEnvImage(hdr, *img, &err)) m_status = "HDRI studio_small_09_2k";
    else { makeProceduralSky(*img); m_status = "HDRI: procedural sky (studio_small_09_2k.hdr not found)"; }
    m_scene.env.hdri = img;
    m_scene.env.hdriIntensity = 1.6f;
    m_scene.env.visibleToCamera = false;
    m_scene.env.top = float3(0.80f, 0.82f, 0.86f);
    m_scene.env.horizon = float3(0.86f, 0.87f, 0.89f);
    m_scene.env.bottom = float3(0.5f, 0.5f, 0.52f);

    Camera& cam = m_scene.camera;
    cam.yaw = radians(35.0f);
    cam.pitch = radians(42.0f);
    cam.fovY = radians(30.0f);
    cam.distance = 40.0f;
}

int BrickApp::meshOf(int part) {
    if (part < 0 || part >= (int)m_partMesh.size()) return -1;
    if (m_partMesh[part] >= 0) return m_partMesh[part];
    Part* p = m_lib.get(m_lib.parts[part].name);
    if (!p) return -1;
    m_partMesh[part] = m_scene.addMesh(p->name, p->mesh);
    return m_partMesh[part];
}

void BrickApp::rebuildScene() {
    m_scene.objects.resize(m_firstBrickObject);
    m_scene.objects.reserve(m_firstBrickObject + m_bricks.size());
    for (const Brick& b : m_bricks) {
        Object o;
        o.mesh = meshOf(b.part);
        if (o.mesh < 0) o.visible = false;
        o.material = m_colorMaterial.empty() ? 0 : m_colorMaterial[std::clamp(b.color, 0, (int)m_colorMaterial.size() - 1)];
        o.position = b.pos;
        o.rot = b.rot;
        o.useRot = true;
        m_scene.addObject(o);
    }
    m_renderer.resetAccumulation();
}

void BrickApp::syncObjects() {
    for (size_t i = 0; i < m_bricks.size(); ++i) {
        Object& o = m_scene.objects[m_firstBrickObject + i];
        o.position = m_bricks[i].pos;
        o.rot = m_bricks[i].rot;
        o.material = m_colorMaterial[std::clamp(m_bricks[i].color, 0, (int)m_colorMaterial.size() - 1)];
    }
    m_renderer.resetAccumulation();
}

// Every part of the library on the ground, sorted by category then name, in rows of growing size.
void BrickApp::buildPartGrid() {
    std::string err;
    m_lib.loadAll(&err);
    std::vector<int> order;
    for (int i = 0; i < (int)m_lib.parts.size(); ++i)
        if (m_lib.parts[i].loaded) order.push_back(i);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const Part& A = m_lib.parts[a];
        const Part& B = m_lib.parts[b];
        if (A.base != B.base) return A.base;
        if (A.sub != B.sub) return A.sub < B.sub;
        return A.label < B.label;
    });
    // shelf packing: rows of about the same width (the footprint of the whole set as a square)
    double area = 0;
    float widest = 0;
    for (int i : order) {
        float3 e = m_lib.parts[i].bmax - m_lib.parts[i].bmin;
        area += (e.x + 0.8) * (e.z + 0.8);
        widest = std::max(widest, e.x);
    }
    const float rowWidth = std::max((float)std::sqrt(area) * 1.15f, widest);
    float x = 0, z = 0, rowDepth = 0;
    const float gap = 0.8f;
    const int standard[] = {3, 4, 1, 14, 0, 2, 10, 6};   // a few palette colours in turn
    for (size_t k = 0; k < order.size(); ++k) {
        const Part& p = m_lib.parts[order[k]];
        float3 ext = p.bmax - p.bmin;
        if (x > 0 && x + ext.x > rowWidth) { x = 0; z += rowDepth + gap; rowDepth = 0; }
        Brick b;
        b.part = order[k];
        b.color = standard[k % 8] % std::max<int>(1, (int)m_lib.palette.size());
        b.pos = float3(x - p.bmin.x, -p.bmin.y, z - p.bmin.z);
        m_bricks.push_back(b);
        x += ext.x + gap;
        rowDepth = std::max(rowDepth, ext.z);
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "%d parts (%d base + %d Technic)", (int)order.size(), 50, (int)order.size() - 50);
    m_status = buf;
}

// count random bricks of the base set stacked on a stud grid (benchmark scenes).
void BrickApp::buildBench(int count) {
    std::mt19937 rng(1234);
    std::vector<int> pool;
    for (int i = 0; i < (int)m_lib.parts.size(); ++i)
        if (m_lib.parts[i].base && m_lib.parts[i].loaded) pool.push_back(i);
    const int side = (int)std::ceil(std::sqrt(count / 6.0));
    for (int n = 0; n < count; ++n) {
        Brick b;
        b.part = pool[rng() % pool.size()];
        b.color = (int)(rng() % m_lib.palette.size());
        int cell = n % (side * side), layer = n / (side * side);
        b.pos = float3((cell % side) * 3.2f, layer * 0.96f, (cell / side) * 3.2f);
        float a = (rng() % 4) * kPi * 0.5f;
        b.rot = float4(0, std::sin(a * 0.5f), 0, std::cos(a * 0.5f));
        m_bricks.push_back(b);
    }
}

void BrickApp::frameAll() {
    float3 mn(1e30f), mx(-1e30f);
    for (const Brick& b : m_bricks) {
        const Part& p = m_lib.parts[b.part];
        if (!p.loaded) continue;
        mn = minv(mn, b.pos + p.bmin);
        mx = maxv(mx, b.pos + p.bmax);
    }
    if (mn.x > mx.x) { mn = float3(-5, 0, -5); mx = float3(5, 3, 5); }
    Camera& cam = m_scene.camera;
    cam.target = (mn + mx) * 0.5f;
    float r = length(mx - mn) * 0.5f;
    cam.distance = std::max(4.0f, r / std::tan(cam.fovY * 0.5f) * 1.05f);
    cam.focusDistance = cam.distance;
}

// --------------------------------------------------------------------------- input

void BrickApp::cameraInput() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) return;
    Camera& cam = m_scene.camera;
    bool changed = false;
    const bool orbit = ImGui::IsMouseDown(ImGuiMouseButton_Right) || (io.KeyAlt && ImGui::IsMouseDown(ImGuiMouseButton_Left));
    const bool pan = ImGui::IsMouseDown(ImGuiMouseButton_Middle) || (io.KeyShift && ImGui::IsMouseDown(ImGuiMouseButton_Right));
    if (pan && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) {
        float3 r, u, f;
        cam.basis(r, u, f);
        float k = cam.distance * std::tan(cam.fovY * 0.5f) * 2.0f / std::max(1.0f, io.DisplaySize.y);
        cam.target = cam.target - r * (io.MouseDelta.x * k) + u * (io.MouseDelta.y * k);
        changed = true;
    } else if (orbit && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) {
        cam.yaw -= io.MouseDelta.x * 0.006f;
        cam.pitch = clampf(cam.pitch + io.MouseDelta.y * 0.006f, radians(-5.0f), radians(89.0f));
        changed = true;
    }
    if (io.MouseWheel != 0) {
        cam.distance = std::max(1.0f, cam.distance * std::pow(0.88f, io.MouseWheel));
        changed = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F)) { frameAll(); changed = true; }
    if (ImGui::IsKeyPressed(ImGuiKey_P)) { m_rs.raster = !m_rs.raster; changed = true; }
    if (changed) { cam.focusDistance = cam.distance; m_renderer.resetAccumulation(); }
}

// --------------------------------------------------------------------------- frame

void BrickApp::drawUi() {
    const float s = m_dpi;
    ImGui::SetNextWindowPos(ImVec2(10 * s, 10 * s), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGui::Begin("##stats", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
    ImGui::Text("BrickIt natif  ·  %s", m_rs.raster ? "Raster (MSAA)" : "Path tracing");
    ImGui::Separator();
    ImGui::Text("Briques      %d", (int)m_bricks.size());
    size_t tris = 0;
    for (const Brick& b : m_bricks) tris += m_lib.parts[b.part].mesh.indices.size() / 3;
    ImGui::Text("Triangles    %.2f M", tris / 1e6);
    ImGui::Text("GPU          %.2f ms", m_renderer.gpuFrameMs());
    ImGui::Text("CPU          %.2f ms", m_cpuMs);
    ImGui::Text("Image        %.1f ms (%.0f i/s)", m_frameMs, m_frameMs > 0 ? 1000.0 / m_frameMs : 0.0);
    if (!m_rs.raster) ImGui::Text("Échantillons %u", m_renderer.sampleCount());
    if (!m_status.empty()) ImGui::TextDisabled("%s", m_status.c_str());
    ImGui::End();
}

void BrickApp::frame() {
    LARGE_INTEGER t0;
    QueryPerformanceCounter(&t0);
    if (m_last.QuadPart) m_frameMs = m_frameMs * 0.9 + 0.1 * (t0.QuadPart - m_last.QuadPart) * 1000.0 / m_qpf.QuadPart;
    m_last = t0;

    m_gui.newFrame();
    cameraInput();
    drawUi();

    ImVec2 ds = ImGui::GetIO().DisplaySize;
    m_vp = {0, 0, std::max(16, (int)ds.x), std::max(16, (int)ds.y)};
    m_rs.samplesPerFrame = m_rs.raster ? 1 : 2;
    m_renderer.renderToTexture(m_scene, m_rs, m_vp, -1);
    ImGui::GetBackgroundDrawList()->AddImage(m_gui.viewportTexture(), ImVec2(0, 0), ds);
    LARGE_INTEGER t1;
    QueryPerformanceCounter(&t1);
    m_cpuMs = m_cpuMs * 0.9 + 0.1 * (t1.QuadPart - t0.QuadPart) * 1000.0 / m_qpf.QuadPart;
    m_gui.render(true);
    ++m_frame;

    if (!m_opt.shotPath.empty()) {
        bool ready = m_rs.raster ? m_frame >= m_opt.shotFrames : (int)m_renderer.sampleCount() >= m_opt.shotSpp;
        if (ready && m_frame > 6) {
            std::vector<uint8_t> img;
            uint32_t w, h;
            if (m_renderer.readBackBuffer(img, w, h)) savePng(m_opt.shotPath, img, w, h);
            m_running = false;
        }
    }
    m_renderer.present(m_opt.shotPath.empty());
}

} // namespace brickit

// --------------------------------------------------------------------------- entry point

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    brickit::Options opt;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        auto next = [&]() -> std::wstring { return i + 1 < argc ? std::wstring(argv[++i]) : std::wstring(); };
        if (a == L"--shot") opt.shotPath = next();
        else if (a == L"--spp") opt.shotSpp = _wtoi(next().c_str());
        else if (a == L"--frames") opt.shotFrames = _wtoi(next().c_str());
        else if (a == L"--grid") opt.grid = true;
        else if (a == L"--bench") opt.bench = _wtoi(next().c_str());
        else if (a == L"--pt") opt.pathTrace = true;
        else if (a == L"--size") {
            std::wstring s = next();
            swscanf_s(s.c_str(), L"%dx%d", &opt.width, &opt.height);
        }
    }
    LocalFree(argv);
    brickit::BrickApp app;
    if (!app.init(inst, opt)) return 1;
    return app.run();
}
