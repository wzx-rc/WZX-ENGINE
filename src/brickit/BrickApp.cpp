#include "BrickApp.h"
#include "MeshLod.h"
#include "app/Theme.h"
#include "backends/imgui_impl_win32.h"
#include "vp3d/AssetImport.h"
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
    m_ui = m_opt.uiScale > 0 ? m_opt.uiScale : m_dpi;   // fonts and layout at the UI scale (100 % / 150 % tests)
    if (!m_gui.init(m_hwnd, m_renderer, m_ui)) {
        MessageBoxA(m_hwnd, "Dear ImGui initialisation failed", "BrickIt", MB_ICONERROR);
        return false;
    }
    th::loadThemes(m_exeDir + L"\\assets\\themes");
    th::loadSettings(m_exeDir + L"\\brickit_settings.ini");
    th::apply(m_ui);

    if (!m_lib.load(m_exeDir + L"\\assets\\brickit", &err)) {
        MessageBoxA(m_hwnd, err.c_str(), "BrickIt - library", MB_ICONERROR);
        return false;
    }
    m_rs.raster = !opt.pathTrace;
    m_rs.denoise = true;
    m_rs.exposure = m_opt.exposure;   // bright "product shot" look
    m_rs.vignette = 0.06f;
    // path tracing: the denoiser stays useful longer (its strength fades as 1/sqrt(samples)), the sparkles of the
    // glossy coat in the studio lights are clamped and the caustics through the rough bounces filtered
    m_rs.denoiseEdgeLuma = 4.0f;
    m_rs.denoisePasses = 4;
    m_rs.fireflyClamp = 8.0f;
    m_rs.filterGlossy = 0.15f;
    m_rs.bevelScale = 1.0f;
    m_rs.tonemapper = m_opt.tonemapper;   // ACES by default
    m_rs.idPass = false;   // selection: coverage outline only
    m_rs.selectionStyle = 2;   // thin white line at 45 % and a light fill
    if (opt.bounces > 0) m_rs.maxBounces = opt.bounces;
    if (opt.denoise >= 0) m_rs.denoise = opt.denoise != 0;
    if (opt.bevelSpp >= 0) { m_rs.bevel = opt.bevelSpp > 0; m_rs.bevelSamples = std::max(1, opt.bevelSpp); }
    m_lib.loadAll(&err);   // every Technic pack (about 10 MB): the library shows them all, the thumbnails need them
    buildThumbs();
    buildNav();
    m_color = colorIndex(4);   // red
    m_focusDim = opt.focusDim;
    setupStage();
    if (!opt.openPath.empty()) openScene(opt.openPath);
    else if (opt.mechDemo) buildMechDemo();
    else if (opt.demo) buildDemo();
    else if (opt.grid) buildPartGrid();
    else if (opt.bench > 0) buildBench(opt.bench);
    rebuildScene();
    frameAll();
    m_scene.camera.distance *= opt.zoom;
    if (opt.yaw > -999) m_scene.camera.yaw = radians(opt.yaw);
    if (opt.pitch > -999) m_scene.camera.pitch = radians(opt.pitch);
    m_scene.camera.focusDistance = m_scene.camera.distance;
    if (opt.libCategory >= 0 && opt.libCategory < (int)m_nav.size()) m_navSel = opt.libCategory;
    if (opt.selectBrick >= 0 && opt.selectBrick < (int)m_bricks.size()) select(opt.selectBrick);
    if (opt.duplicate) duplicateSelected();   // scripted test of Ctrl+D
    if (opt.mech) {
        mechEnter();
        if (opt.motor != 0 && !m_mm.cmds.empty()) {   // scripted test: the first motor turned by --motor degrees, in steps
            const int steps = 24;
            for (int s = 1; s <= steps; ++s) {
                m_mm.cmds[0].value = radians(opt.motor) * s / steps;
                m_mech.solve(m_mm);
            }
            mechApplyPose();
        }
    }

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
    g.baseColor = float3(0.70f, 0.71f, 0.73f);   // light studio floor
    g.roughness = 0.5f;
    g.flags = m_opt.cleanGround ? 0u : MAT_FLAG_GRID;
    Object o;
    o.name = "Ground";
    o.part = "GROUND";
    o.mesh = m_scene.addMesh("ground", makePlane(m_opt.groundSize));
    o.material = m_scene.addMaterial(g);
    o.selectable = false;
    o.visible = !m_opt.noGround;
    m_scene.addObject(o);
    m_firstBrickObject = (int)m_scene.objects.size();

    // fingerprints: roughness mask shared by the plastic colours (assets/brickit/fingerprints.jpg)
    int fpTex = -1;
    {
        Texture t;
        std::string terr;
        if (loadImage(m_exeDir + L"\\assets\\brickit\\fingerprints.jpg", t, &terr, 2048)) fpTex = m_scene.addTexture(std::move(t));
    }
    // one material per palette colour: ABS plastic (light clear coat, discreet subsurface on the light colours),
    // transparent = tinted glass, metal colours = metal; bevel on every edge (rounded edges and the groove between
    // two bricks that touch, path tracing)
    m_colorMaterial.clear();
    for (const Color& c : m_lib.palette) {
        Material m;
        m.name = c.name;
        float3 srgb = c.srgb;
        if (c.id == 4) srgb = float3(201, 26, 9) * (1.0f / 255.0f);   // LDConfig Red is pure 180,0,0: LEGO Bright Red instead
        m.baseColor = float3(srgbToLinear(srgb.x), srgbToLinear(srgb.y), srgbToLinear(srgb.z));
        m.roughness = 0.24f;
        m.specular = 0.5f;
        m.coat = 0.35f;
        m.coatRoughness = 0.05f;
        m.bevelRadius = 0.035f;
        m.roughnessTex = fpTex;
        m.roughTexScale = 1.0f / 10.0f;   // the map covers 10 cm: prints at the scale of a finger on the bricks
        m.roughTexStrength = m_opt.fingerprints;
        const float lum = 0.2126f * m.baseColor.x + 0.7152f * m.baseColor.y + 0.0722f * m.baseColor.z;
        if (lum > 0.06f && m_opt.sss) {   // light goes into the ABS: more on the light colours (white, yellow, red),
            m.subsurface = std::min(0.35f, 0.15f + lum * 0.4f);   // a diffusion distance of about a millimetre, tinted
            m.sssRadius = (m.baseColor * 0.85f + float3(0.15f)) * 0.14f;
        }
        if (c.group.find("trans") != std::string::npos) {
            m.transmission = 1.0f;
            m.roughness = 0.03f;
            m.coat = 0.0f;
            m.subsurface = 0.0f;
            m.roughTexStrength *= 0.5f;
        } else if (c.group.find("metal") != std::string::npos || c.group.find("chrome") != std::string::npos) {
            m.metallic = 1.0f;
            m.roughness = 0.2f;
            m.coat = 0.0f;
            m.subsurface = 0.0f;
        }
        m_colorMaterial.push_back(m_scene.addMaterial(m));
    }
    // focus mode: the same materials at 50 % for the bricks that are not the focus
    m_dimMaterial.clear();
    for (int ci : m_colorMaterial) {
        Material d = m_scene.materials[ci];
        d.name += " (dim)";
        d.baseColor = d.baseColor * 0.5f;
        d.specular *= 0.5f;
        d.coat *= 0.5f;
        m_dimMaterial.push_back(m_scene.addMaterial(d));
    }
    {
        Material bl;
        bl.name = "Blocked";
        bl.baseColor = float3(0.9f, 0.12f, 0.1f);
        bl.roughness = 0.5f;
        m_blockedMaterial = m_scene.addMaterial(bl);
    }

    // studio HDRI (assets/hdri, kept out of git) else the procedural sky
    std::wstring hdr = m_exeDir + L"\\assets\\hdri\\studio_small_09_2k.hdr";
    auto img = std::make_shared<EnvImage>();
    std::string err;
    if (!m_opt.noHdri && fileExists(hdr) && loadEnvImage(hdr, *img, &err)) m_status = "HDRI studio_small_09_2k";
    else { makeProceduralSky(*img); m_status = "HDRI: procedural sky (studio_small_09_2k.hdr not found)"; }
    m_scene.env.hdri = img;
    m_scene.env.hdriIntensity = 1.0f;
    m_scene.env.visibleToCamera = false;
    m_scene.env.top = float3(0.82f, 0.83f, 0.86f);   // background behind the bricks (the HDRI only lights)
    m_scene.env.horizon = float3(0.90f, 0.90f, 0.91f);
    m_scene.env.bottom = float3(0.6f, 0.6f, 0.62f);

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
    const int mesh = m_scene.addMesh(p->name, p->mesh);
    m_partMesh[part] = mesh;
    // raster levels of detail: clustered (studs melted) then the box
    const float3 ext = p->bmax - p->bmin;
    const float cell = std::max(0.12f, std::max(ext.x, std::max(ext.y, ext.z)) / 40.0f);
    MeshData l1 = simplifyCluster(p->mesh, cell);
    Scene::MeshLod lod;
    if (l1.indices.size() * 2 < p->mesh.indices.size()) lod.lod1 = m_scene.addMesh(p->name + "_lod1", std::move(l1));
    lod.lod2 = m_scene.addMesh(p->name + "_lod2", boxLod(p->bmin, p->bmax));
    if ((int)m_scene.lods.size() <= mesh) m_scene.lods.resize(mesh + 1);
    m_scene.lods[mesh] = lod;
    return mesh;
}

void BrickApp::rebuildScene() {
    m_scene.objects.resize(m_firstBrickObject);
    m_scene.objects.reserve(m_firstBrickObject + m_bricks.size());
    for (const Brick& b : m_bricks) {
        Object o;
        o.mesh = meshOf(b.part);
        if (o.mesh < 0) o.visible = false;
        o.useRot = true;
        m_scene.addObject(o);
    }
    syncObjects();
}

// Transforms and materials of the brick objects: colour, focus mode (the rest at 50 %), the brick in hand red and
// translucent when it cannot be dropped.
void BrickApp::syncObjects() {
    const bool dim = m_focusDim && (m_selected >= 0 || m_holding);
    for (size_t i = 0; i < m_bricks.size(); ++i) {
        Object& o = m_scene.objects[m_firstBrickObject + i];
        const Brick& b = m_bricks[i];
        o.position = b.pos;
        o.rot = b.rot;
        const int c = std::clamp(b.color, 0, (int)m_colorMaterial.size() - 1);
        const bool focus = (int)i == m_selected || (m_holding && (int)i == m_held.brick) ||
                           std::find(m_multi.begin(), m_multi.end(), (int)i) != m_multi.end();
        o.material = dim && !focus ? m_dimMaterial[c] : m_colorMaterial[c];
        o.ghost = false;
        if (m_holding && (int)i == m_held.brick && m_held.blocked) {
            o.material = m_blockedMaterial;
            o.ghost = true;
            o.ghostAlpha = 0.55f;
        }
    }
    ++m_scene.objectsVersion;
    m_renderer.resetAccumulation();
}

void BrickApp::toast(const std::string& text) {
    m_toast = text;
    m_toastTime = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
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
        do b.color = (int)(rng() % m_lib.palette.size());
        while (m_opt.opaque && m_lib.palette[b.color].group != "standard");
        int cell = n % (side * side), layer = n / (side * side);
        b.pos = float3((cell % side) * 3.2f, layer * 0.96f, (cell / side) * 3.2f);
        float a = (rng() % 4) * kPi * 0.5f;
        b.rot = float4(0, std::sin(a * 0.5f), 0, std::cos(a * 0.5f));
        m_bricks.push_back(b);
    }
}

int BrickApp::colorIndex(int ldrawId) const {
    for (int i = 0; i < (int)m_lib.palette.size(); ++i)
        if (m_lib.palette[i].id == ldrawId) return i;
    return 0;
}

// Small showcase on the stud grid (beauty captures): a running bond wall, coloured steps, round and transparent
// bricks, slopes, Technic gears and axles. The bricks touch: the bevel makes the grooves between them.
void BrickApp::buildDemo() {
    auto add = [&](const char* part, int color, float x, float y, float z, float yawDeg = 0) {
        int pi = m_lib.find(part);
        if (pi < 0 || !m_lib.get(part)) return;
        Brick b;
        b.part = pi;
        b.color = colorIndex(color);
        float a = radians(yawDeg);
        b.rot = float4(0, std::sin(a * 0.5f), 0, std::cos(a * 0.5f));
        // (x, y, z) = minimum corner of the turned part
        const Part& p = m_lib.parts[pi];
        const float c = std::cos(a), sn = std::sin(a);
        float3 mn(1e30f);
        for (int k = 0; k < 8; ++k) {
            float3 q((k & 1) ? p.bmax.x : p.bmin.x, (k & 2) ? p.bmax.y : p.bmin.y, (k & 4) ? p.bmax.z : p.bmin.z);
            mn = minv(mn, float3(c * q.x + sn * q.z, q.y, -sn * q.x + c * q.z));
        }
        b.pos = float3(x, y, z) - mn;
        m_bricks.push_back(b);
    };
    const float P = 0.8f, B = 0.96f, L = 0.32f;
    // wall: 2 x 4 bricks in running bond, 5 rows
    const int wallColors[5] = {4, 15, 4, 15, 4};
    for (int r = 0; r < 5; ++r)
        for (int k = 0; k < 4; ++k) {
            float off = (r % 2) ? 2 * P : 0.0f;
            if (r % 2 && k == 3) { add("3003", wallColors[r], 4 * P * k + off, r * B, 0); continue; }
            add("3001", wallColors[r], 4 * P * k + off, r * B, 0);
        }
    add("3003", 15, 0, 1 * B, 0);
    add("3003", 15, 0, 3 * B, 0);
    // slopes on top of the wall
    add("15068", 1, 0, 5 * B, 0);
    add("15068", 1, 2 * P, 5 * B, 0);
    add("3022", 14, 10 * P, 5 * B, 0);
    add("15068", 14, 12 * P, 5 * B, 0);
    add("15068", 14, 14 * P, 5 * B, 0);
    // steps of 2 x 2 bricks
    const int stepColors[4] = {14, 1, 10, 25};
    for (int i = 0; i < 4; ++i)
        for (int h = 0; h <= i; ++h) add("3003", stepColors[i], 18 * P + i * 2 * P, h * B, 0);
    // round, transparent and metal bricks in front
    for (int h = 0; h < 4; ++h) add("3062a", 47, 2 * P, h * B, 4 * P);
    for (int h = 0; h < 3; ++h) add("3062a", 36, 4 * P, h * B, 4 * P);
    add("3941", 15, 6 * P, 0, 4 * P);
    add("3941", 72, 6 * P, B, 4 * P);
    add("30367a", 297, 6 * P, 2 * B, 4 * P);
    add("3022", 0, 9 * P, 0, 4 * P);
    add("3022", 0, 9 * P, L, 4 * P);
    add("15068", 4, 9 * P, 2 * L, 4 * P);
    add("24947", 191, 12 * P, 0, 4 * P);
    // Technic: beam, axles, gears
    add("3707", 71, 0, 0, 8 * P);
    add("3705", 0, 0, 0, 9 * P);
    add("94925", 72, 10 * P, 0, 8 * P);
    add("3647", 72, 13 * P, 0, 8 * P);
    add("32064a", 4, 16 * P, 0, 8 * P);
    add("6541", 14, 19 * P, 0, 8 * P);
    add("60483", 71, 21 * P, 0, 8 * P);
    char buf[64];
    snprintf(buf, sizeof(buf), "Demo : %d briques", (int)m_bricks.size());
    m_status = buf;
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
    if (io.WantCaptureMouse || m_overUi) return;
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
    if (!io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_F)) { frameAll(); changed = true; }
        if (ImGui::IsKeyPressed(ImGuiKey_P)) { m_rs.raster = !m_rs.raster; changed = true; }
        if (ImGui::IsKeyPressed(ImGuiKey_L)) {   // levels of detail on / off (comparison)
            std::swap(m_scene.lods, m_lodsOff);
            changed = true;
        }
    }
    if (changed) { cam.focusDistance = cam.distance; m_renderer.resetAccumulation(); m_lastMove = ImGui::GetTime(); }
}

// --------------------------------------------------------------------------- frame

void BrickApp::frame() {
    LARGE_INTEGER t0;
    QueryPerformanceCounter(&t0);
    if (m_last.QuadPart) m_frameMs = m_frameMs * 0.9 + 0.1 * (t0.QuadPart - m_last.QuadPart) * 1000.0 / m_qpf.QuadPart;
    m_last = t0;

    m_gui.newFrame();
    if (m_frame == 2 && !m_opt.spawnPart.empty()) {   // scripted test: a part in hand at a point of the view
        int pi = m_lib.find(m_opt.spawnPart);
        ImVec2 d = ImGui::GetIO().DisplaySize;
        if (pi >= 0) spawn(pi, d.x * m_opt.spawnX, d.y * m_opt.spawnY);
    }
    if (!m_opt.noUi) drawUi();
    cameraInput();
    if (m_mechOn) mechUpdate((float)ImGui::GetIO().DeltaTime);
    else editInput();
    if (ImGui::IsKeyPressed(ImGuiKey_M) && !ImGui::GetIO().WantTextInput && !ImGui::GetIO().KeyCtrl) { if (m_mechOn) mechExit(); else mechEnter(); }

    ImVec2 ds = ImGui::GetIO().DisplaySize;
    // Path tracing: half resolution while the view moves (and 0.25 s after), full resolution to converge when still.
    const bool moving = ImGui::GetTime() - m_lastMove < 0.25 && m_opt.shotPath.empty();
    const float scale = (!m_rs.raster && moving) ? m_opt.ptMoveScale : 1.0f;
    m_vp = {0, 0, std::max(16, (int)(ds.x * scale)), std::max(16, (int)(ds.y * scale))};
    m_rs.samplesPerFrame = m_rs.raster ? 1 : std::max(1, m_opt.ppf);
    std::vector<int> extra;
    for (int i : m_multi) extra.push_back(m_firstBrickObject + i);
    m_renderer.setExtraSelection(extra);
    const int selObj = (m_selected >= 0 && !m_holding) ? m_firstBrickObject + m_selected : -1;
    m_rs.pixelScale = scale;
    m_renderer.renderToTexture(m_scene, m_rs, m_vp, selObj);
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
            if (!m_opt.savePath.empty()) saveScene(m_opt.savePath);
            // timings next to the capture (benchmarks): bricks, triangles drawn, GPU and CPU ms
            FILE* f = nullptr;
            if (_wfopen_s(&f, (m_opt.shotPath + L".txt").c_str(), L"w") == 0 && f) {
                fprintf(f, "bricks=%d mode=%s gpu_ms=%.2f cpu_ms=%.2f frame_ms=%.2f tris_drawn=%.2fM spp=%u\n", (int)m_bricks.size(),
                        m_rs.raster ? "raster" : "pathtrace", m_renderer.gpuFrameMs(), m_cpuMs, m_frameMs,
                        m_renderer.rasterTriangles() / 1e6, m_renderer.sampleCount());
                fclose(f);
            }
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
        else if (a == L"--zoom") opt.zoom = (float)_wtof(next().c_str());
        else if (a == L"--bounces") opt.bounces = _wtoi(next().c_str());
        else if (a == L"--bevel-spp") opt.bevelSpp = _wtoi(next().c_str());
        else if (a == L"--opaque") opt.opaque = true;
        else if (a == L"--demo") opt.demo = true;
        else if (a == L"--no-sss") opt.sss = false;
        else if (a == L"--no-bevel") opt.bevelSpp = 0;
        else if (a == L"--tonemap") opt.tonemapper = _wtoi(next().c_str());
        else if (a == L"--clean") opt.cleanGround = true;
        else if (a == L"--exposure") opt.exposure = (float)_wtof(next().c_str());
        else if (a == L"--fingerprints") opt.fingerprints = (float)_wtof(next().c_str());
        else if (a == L"--yaw") opt.yaw = (float)_wtof(next().c_str());
        else if (a == L"--pitch") opt.pitch = (float)_wtof(next().c_str());
        else if (a == L"--no-hdri") opt.noHdri = true;
        else if (a == L"--no-ground") opt.noGround = true;
        else if (a == L"--ground") opt.groundSize = (float)_wtof(next().c_str());
        else if (a == L"--denoise") opt.denoise = _wtoi(next().c_str());
        else if (a == L"--ppf") opt.ppf = _wtoi(next().c_str());
        else if (a == L"--no-ui") opt.noUi = true;
        else if (a == L"--ui-scale") opt.uiScale = (float)_wtof(next().c_str());
        else if (a == L"--open") opt.openPath = next();
        else if (a == L"--spawn") { std::wstring w = next(); opt.spawnPart.assign(w.begin(), w.end()); }
        else if (a == L"--at") { opt.spawnX = (float)_wtof(next().c_str()); opt.spawnY = (float)_wtof(next().c_str()); }
        else if (a == L"--select") opt.selectBrick = _wtoi(next().c_str());
        else if (a == L"--cat") opt.libCategory = _wtoi(next().c_str());
        else if (a == L"--focus") opt.focusDim = true;
        else if (a == L"--save") opt.savePath = next();
        else if (a == L"--dup") opt.duplicate = true;
        else if (a == L"--mechdemo") opt.mechDemo = true;
        else if (a == L"--mech") opt.mech = true;
        else if (a == L"--motor") opt.motor = (float)_wtof(next().c_str());
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
