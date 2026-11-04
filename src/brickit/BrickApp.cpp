#include "BrickApp.h"
#include <chrono>
#include <commdlg.h>
#include "app/Content.h"
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
    // never larger than the work area of the screen (1600 x 900 at 200 % is 3200 x 1800 px: wider than a 3072 px
    // screen, the header tools, the colours and the library ended up off screen); interactive runs open maximised then
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
    const int workW = mi.rcWork.right - mi.rcWork.left, workH = mi.rcWork.bottom - mi.rcWork.top;
    int winW = r.right - r.left, winH = r.bottom - r.top;
    const bool tooBig = winW > workW || winH > workH;
    if (tooBig && opt.shotPath.empty()) { winW = std::min(winW, workW); winH = std::min(winH, workH); }
    m_hwnd = CreateWindowExW(0, wc.lpszClassName, L"BrickIt — WZX Engine", WS_OVERLAPPEDWINDOW, opt.shotPath.empty() && tooBig ? mi.rcWork.left : CW_USEDEFAULT,
                             opt.shotPath.empty() && tooBig ? mi.rcWork.top : CW_USEDEFAULT, winW, winH, nullptr, nullptr, inst, nullptr);
    if (!m_hwnd) return false;
    m_startMaximized = tooBig && opt.shotPath.empty();
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
    if (opt.shotPath.empty()) {   // .loading of the web while the library and the thumbnails load
        ShowWindow(m_hwnd, m_startMaximized ? SW_SHOWMAXIMIZED : SW_SHOWDEFAULT);
        for (int f = 0; f < 2; ++f) {
            m_gui.newFrame();
            ImDrawList* dl = ImGui::GetBackgroundDrawList();
            const ImVec2 ds = ImGui::GetIO().DisplaySize;
            dl->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(0x13, 0x14, 0x17, 255));
            const char* t = "Chargement de la bibliothèque BrickIt…";
            const float px = 14 * m_ui * 1.22f;
            const ImVec2 sz = ImGui::GetFont()->CalcTextSizeA(px, 1e9f, 0, t);
            dl->AddText(ImGui::GetFont(), px, ImVec2((ds.x - sz.x) * 0.5f, (ds.y - sz.y) * 0.5f), IM_COL32(0x9a, 0x9c, 0xa4, 255), t);
            m_gui.render(false);
            m_renderer.present(true);
        }
    }

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
    m_rs.traceStride = opt.stride > 0 ? opt.stride : (opt.shotPath.empty() ? 4 : 1);   // interactive: a quarter of the pixels per frame
    m_rs.maxBounces = 4;   // plastic and a light floor: 4 bounces look the same as 6, a third cheaper
    if (opt.bounces >= 0) m_rs.maxBounces = opt.bounces;
    if (opt.denoise >= 0) m_rs.denoise = opt.denoise != 0;
    if (opt.bevelSpp >= 0) { m_rs.bevel = opt.bevelSpp > 0; m_rs.bevelSamples = std::max(1, opt.bevelSpp); }
    m_lib.loadAll(&err);   // every Technic pack (about 10 MB): the library shows them all, the thumbnails need them
    if (!opt.fullThumbs.empty()) {   // the thumbnails of the full library, then exit
        const int n = buildFullThumbs();
        FILE* lf = nullptr;
        if (_wfopen_s(&lf, (opt.fullThumbs + L".txt").c_str(), L"w") == 0 && lf) { fprintf(lf, "%d thumbnails\n", n); fclose(lf); }
        return false;
    }
    buildThumbs();
    loadFullThumbIndex();
    buildNav();
    if (opt.shotPath.empty()) loadPrefs();   // captures stay reproducible
    if (m_uiScaleUser != 1.0f) { m_ui *= m_uiScaleUser; th::apply(m_ui); }   // settings: UI scale over the screen scale
    if (opt.dev) m_dev = true;
    devLoadAll();   // DEV replacements kept next to the exe
    if (opt.manual) m_pManual.visible = true;
    if (opt.settings) { m_popup = 5; m_popupAt = ImVec2(1e6f, 34 * m_ui); }
    if (opt.quality >= 0) { m_quality = opt.quality; m_rs.msaaSamples = m_quality ? 4 : 1; }
    if (opt.aa >= 0) m_rs.aaQuality = std::clamp(opt.aa, 0, 2);
    if (opt.shadows >= 0) m_rs.shadowQuality = std::clamp(opt.shadows, 0, 4);
    if (opt.rc) setRC(true);
    if (opt.hdriRot >= 0) m_hdriRotDeg = opt.hdriRot;
    if (opt.hdriInt >= 0) m_hdriInt = opt.hdriInt;
    if (opt.gridColor >= 0) { m_gridRgb = float3(((opt.gridColor >> 16) & 255) / 255.0f, ((opt.gridColor >> 8) & 255) / 255.0f, (opt.gridColor & 255) / 255.0f); if (m_gridAlpha <= 0) m_gridAlpha = 0.6f; }
    if (opt.gridAlpha >= 0) m_gridAlpha = opt.gridAlpha;
    if (opt.softGI >= 0) m_softGI = opt.softGI != 0;
    if (opt.giStrength >= 0) m_softGIStrength = opt.giStrength;
    if (!opt.mouseTest.empty()) m_gui.beforeNewFrame = [this] { mouseTestInput(); };
    if (opt.floatTray) { m_pTray.floating = true; m_pTray.x = opt.floatX; m_pTray.y = opt.floatY; }
    if (opt.optsRight) m_optsRight = true;
    devApplyAll();
    m_color = colorIndex(14);   // yellow, as the web (state.colorId: 14)
    m_focusDim = opt.focusDim;
    setupStage();
    applyLook();
    const bool test = opt.mechDemo || opt.demo || opt.grid || opt.bench > 0;
    bool keepCamera = false;
    if (!opt.openPath.empty()) { openScene(opt.openPath); keepCamera = true; }
    else if (!opt.writeHouse.empty()) buildHouse();
    else if (opt.mechDemo) buildMechDemo();
    else if (opt.demo) buildDemo();
    else if (opt.grid) buildPartGrid();
    else if (opt.bench > 0) buildBench(opt.bench);
    else if (restoreAutosave()) keepCamera = true;   // the scene of the last session (vp3d-builder-scene)
    else { sampleScene(); keepCamera = true; }
    rebuildScene();
    if (opt.cornell) {   // the very scene of WZX Engine (app::buildCornellBox, its main view, the settings of App::loadScene)
        m_scene = Scene();
        app::buildCornellBox(m_scene);
        m_scene.env.ambient = float3(0.16f, 0.15f, 0.13f);
        m_scene.camera = app::defaultViews(m_scene, true)[0].cam;
        m_bricks.clear();
        m_firstBrickObject = (int)m_scene.objects.size();
        m_ghostObject = -1;
        m_rimLight = -1;
        m_partMesh.assign(m_partMesh.size(), -1);
        m_rs.exposure = 1.0f;
        m_rs.vignette = 0.10f;
        m_cornell = true;
        keepCamera = true;
    }
    if (test || !keepCamera) frameAll();
    m_scene.camera.distance *= opt.zoom;
    if (opt.yaw > -999) m_scene.camera.yaw = radians(opt.yaw);
    if (opt.pitch > -999) m_scene.camera.pitch = radians(opt.pitch);
    m_scene.camera.focusDistance = m_scene.camera.distance;
    if (opt.libCategory >= 0 && opt.libCategory < (int)m_nav.size()) {   // --cat: 0 recents, 1 favourites, families, Technic sub-families
        const NavItem& n = m_nav[opt.libCategory];
        if (n.kind == 3) { m_fam = "Technic"; m_cat = "Technic"; m_sub = n.key; }
        else m_fam = n.key;
    }
    if (opt.mode >= 0) m_mode = opt.mode;
    if (opt.pivotEdit) m_pivotEdit = true;
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
        FILE* f = nullptr;   // diagnostic of the mechanism (bodies, joints, gears, commands) next to the exe
        if (_wfopen_s(&f, (m_exeDir + L"\\mech_diag.txt").c_str(), L"w") == 0 && f) {
            fprintf(f, "bodies=%d roots=%zu joints=%zu gears=%zu meshes=%zu cmds=%zu\n", m_mm.bodyCount, m_mm.roots.size(), m_mm.tree.size(), m_mm.gears.size(), m_mm.meshes.size(), m_mm.cmds.size());
            for (int b = 0; b < m_mm.bodyCount; ++b) {
                bool root = false;
                for (int r : m_mm.roots) root |= r == b;
                fprintf(f, "body %d%s:", b, root ? " (root)" : "");
                for (size_t i = 0; i < m_bricks.size(); ++i)
                    if (m_mm.brickBody[i] == b) fprintf(f, " #%zu %s", i, m_lib.parts[m_bricks[i].part].name.c_str());
                fprintf(f, "\n");
            }
            for (const MechTreeJ& t : m_mm.tree) fprintf(f, "joint q%d: parent body %d -> child body %d, axis (%.2f %.2f %.2f) at (%.2f %.2f %.2f)\n", t.qi, t.parent, t.child, t.j.u.x, t.j.u.y, t.j.u.z, t.j.p.x, t.j.p.y, t.j.p.z);
            for (const MechGearW& g : m_mm.gears) fprintf(f, "gear %d teeth on body %d (brick #%d) centre (%.2f %.2f %.2f)\n", g.teeth, g.body, g.brick, g.c.x, g.c.y, g.c.z);
            for (auto& ms : m_mm.meshes) fprintf(f, "mesh: gear %d (%d t) <-> gear %d (%d t)\n", ms.first, m_mm.gears[ms.first].teeth, ms.second, m_mm.gears[ms.second].teeth);
            for (const MechCmd& c : m_mm.cmds) {
                fprintf(f, "cmd %s q%d ratios:", c.name.c_str(), c.qi);
                for (auto& r : c.ratio) fprintf(f, " q%d x %.4f", r.first, r.second);
                fprintf(f, "\n");
            }
            for (const std::string& w : m_mm.warnings) fprintf(f, "warning: %s\n", w.c_str());
            fclose(f);
        }
    }

    if (opt.mechTests) { runMechTests(); return false; }   // (quits: the results are in mech_tests.txt)
    ShowWindow(m_hwnd, m_startMaximized ? SW_SHOWMAXIMIZED : SW_SHOWDEFAULT);
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
    if (userSession()) { savePrefs(); autosave(); }
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
    {   // cool rim of the web: DirectionalLight(0xc0d4ff, 1.3) from (26, 22, -30), no shadow; a far area light whose
        // irradiance at the target is 1.3 (raster only, the path traced studio has its own lights)
        AreaLight rim;
        rim.name = "Rim";
        const float3 tgt(0.4f, 1.2f, 0.4f), dir = normalize(float3(26, 22, -30));
        const float D = 80.0f, W = 8.0f;
        rim.position = tgt + dir * D;
        rim.target = tgt;
        rim.width = rim.height = W;
        rim.color = float3(srgbToLinear(0xc0 / 255.0f), srgbToLinear(0xd4 / 255.0f), 1.0f);
        rim.intensity = 1.3f * D * D / (W * W);
        rim.visibleToCamera = false;
        m_scene.lights.push_back(rim);
        m_rimLight = (int)m_scene.lights.size() - 1;
    }

    // one material per palette colour: ABS plastic (light clear coat, discreet subsurface on the light colours),
    // transparent = tinted glass, metal colours = metal; bevel on every edge (rounded edges and the groove between
    // two bricks that touch, path tracing)
    m_colorMaterial.clear();
    m_tintMaterial.clear();
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
        m.bevelRadius = 0.035f;   // no fingerprint mask (Wzx: none anywhere)
        m.flags |= 4u;   // MAT_FLAG_VCOLOR: the prints of the printed parts (their colour per vertex)
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
        } else if (c.group.find("metal") != std::string::npos || c.group.find("chrome") != std::string::npos) {
            m.metallic = 1.0f;
            m.roughness = 0.2f;
            m.coat = 0.0f;
            m.subsurface = 0.0f;
        }
        m_colorMaterial.push_back(m_scene.addMaterial(m));
    }
    m_colorMaterialOrig.clear();
    for (int ci : m_colorMaterial) m_colorMaterialOrig.push_back(m_scene.materials[ci]);
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
    if (m_opt.giLookdev) setLookdev(true);
    {
        Material gm;   // ghostMat of the web: 0xffe08a, emissive 0x6b5a1a
        gm.name = "Ghost";
        gm.baseColor = float3(1.0f, 0.75f, 0.25f);
        gm.emission = float3(0.15f, 0.10f, 0.01f);
        gm.emissionStrength = 1.0f;
        m_ghostMaterial = m_scene.addMaterial(gm);
        Material gr;   // blocked: 0xff6b6b, emissive 0x5a1010
        gr.name = "Ghost blocked";
        gr.baseColor = float3(1.0f, 0.15f, 0.15f);
        gr.emission = float3(0.10f, 0.005f, 0.005f);
        gr.emissionStrength = 1.0f;
        m_ghostBlockedMaterial = m_scene.addMaterial(gr);
    }
    {
        Material bl;
        bl.name = "Blocked";
        bl.baseColor = float3(0.9f, 0.12f, 0.1f);
        bl.roughness = 0.5f;
        m_blockedMaterial = m_scene.addMaterial(bl);
    }

    // HDRI of Réglages > Lumière (the studio of the web at the first start: assets/hdri, kept out of git), else the
    // procedural sky
    buildHdriList();
    if (m_hdriPath == L"*") m_hdriPath = m_exeDir + L"\\assets\\hdri\\studio_small_09_2k.hdr";
    if (!m_opt.hdri.empty()) {   // --hdri: a name of the list ("ciel": the procedural sky) or a path
        std::wstring want = m_opt.hdri;
        for (auto& c : want) c = (wchar_t)towlower(c);
        if (want == L"ciel") m_hdriPath = L"";
        else if (fileExists(m_opt.hdri)) m_hdriPath = m_opt.hdri;
        else for (const std::wstring& p : m_hdriPaths) { std::wstring l = p; for (auto& c : l) c = (wchar_t)towlower(c); if (!p.empty() && l.find(want) != std::wstring::npos) { m_hdriPath = p; break; } }
    }
    setHdri(m_hdriPath);
    m_scene.env.hdriIntensity = 1.0f;
    m_scene.env.visibleToCamera = false;
    m_scene.env.top = float3(0.82f, 0.83f, 0.86f);   // background behind the bricks (the HDRI only lights)
    m_scene.env.horizon = float3(0.90f, 0.90f, 0.91f);
    m_scene.env.bottom = float3(0.6f, 0.6f, 0.62f);

    Camera& cam = m_scene.camera;
    cam.yaw = radians(38.0f);     // the camera of the web: fov 32, yaw 38, pitch 32
    cam.pitch = radians(32.0f);
    cam.fovY = radians(32.0f);
    cam.distance = 40.0f;
}

// Real time: the stage of the web builder (background and fog 0x111214, ground 0x0e0f11 with the stud grid, warm key
// light 3.4 from (-28, 34, 30) with shadows, environment at 0.55, ACES exposure 1), in every mode: the bright studio of
// the path tracing (captures 03b) was replaced by this dark stage at Wzx's request (03/11/2026).
// Radiance Cascades: the Split RC mode of WZX Engine (App::setRenderMode(4)): DDGI volume + raster view of the probes,
// probes computed by the Split Radiance Cascades (giMethod 1), rebuilt every frame, no temporal blend.
void BrickApp::setRC(bool on) {
    m_rcMode = on;
    m_rs.ddgi = on;
    m_rs.ddgiRaster = false;
    m_rs.ddgiRasterView = on;
    m_rs.giMethod = on ? 1 : 0;
    m_rs.rcSplit = on;
    m_rs.rcScreen = false;
    m_rs.rcHysteresis = 0.0f;
    if (on) { m_rs.raster = false; m_rs.historyFixes = true; m_rs.denoise = false; }
}

// --------------------------------------------------------------------------- Réglages > Lumière

// GI reference house (Wzx): plain bricks only, one light grey; a floor of plates, four walls one stud thick (running
// bond of 1 x 2 / 1 x 4), a door and two windows (light coming in, soft shadow inside), a roof of plates with an
// overhang, a step in front. Saved as a BrickIt scene (--write-house file), opened with Dossier > Ouvrir.
void BrickApp::buildHouse() {
    m_bricks.clear();
    int grey = 0;
    for (size_t c = 0; c < m_lib.palette.size(); ++c) if (m_lib.palette[c].name == "Light Bluish Grey" || m_lib.palette[c].name == "Light Bluish Gray") { grey = (int)c; break; }
    const float4 q0(0, 0, 0, 1), q90 = qAxisAngle(float3(0, 1, 0), kPi * 0.5f);
    // a part with its box min at (x, y, z) (studs from the origin, y in cm), its long side along X (q0 asked) or Z
    // (q90 asked), whatever the orientation of the LDraw file
    auto put = [&](const char* name, const float4& want, float sx, float y, float sz) {
        const int pi = m_lib.find(name);
        if (pi < 0) return;
        m_lib.get(name);
        const Box bq = m_build.aabbOf(pi, q0, float3(0));
        const bool longX = (bq.mx.x - bq.mn.x) >= (bq.mx.z - bq.mn.z);
        const bool wantX = want.y == 0.0f;
        const float4 rot = longX == wantX ? q0 : q90;
        const Box b0 = m_build.aabbOf(pi, rot, float3(0));
        Brick b;
        b.part = pi;
        b.color = grey;
        b.rot = rot;
        b.pos = float3(sx * kPitch, y, sz * kPitch) - b0.mn;
        m_bricks.push_back(b);
    };
    const int W = 16, D = 12;          // outside of the walls, studs
    const float plate = 0.32f, brick = 0.96f;
    // floor: 2 x 4 plates over (W + 2) x (D + 2) studs, from (-1, -1)
    for (int x = -1; x < W + 1; x += 2)
        for (int z = -1; z < D + 1; z += 4) put(z + 4 <= D + 1 ? "3020" : "3022", q90, (float)x, 0.0f, (float)z);
    const float y0 = plate;
    const int rows = 8;
    // a wall row along x (z fixed) or along z (x fixed), from a to b (studs, b excluded), skipping the openings
    auto rowX = [&](int z, int a, int b, float y, int shift, const std::vector<std::pair<int, int>>& holes) {
        int x = a + shift;
        if (shift) put("3005", q0, (float)a, y, (float)z);   // (a 1 x 1 at the start: running bond)
        while (x < b) {
            bool hole = false;
            for (auto& h : holes) hole |= x >= h.first && x < h.second;
            if (hole) { ++x; continue; }
            int len = 4;
            for (auto& h : holes) if (x < h.first) len = std::min(len, h.first - x);
            len = std::min(len, b - x);
            const char* part = len >= 4 ? "3010" : (len >= 2 ? "3004" : "3005");
            const int l = len >= 4 ? 4 : (len >= 2 ? 2 : 1);
            put(part, q0, (float)x, y, (float)z);
            x += l;
        }
    };
    auto rowZ = [&](int x, int a, int b, float y, int shift, const std::vector<std::pair<int, int>>& holes) {
        int z = a + shift;
        if (shift) put("3005", q90, (float)x, y, (float)a);
        while (z < b) {
            bool hole = false;
            for (auto& h : holes) hole |= z >= h.first && z < h.second;
            if (hole) { ++z; continue; }
            int len = 4;
            for (auto& h : holes) if (z < h.first) len = std::min(len, h.first - z);
            len = std::min(len, b - z);
            const char* part = len >= 4 ? "3010" : (len >= 2 ? "3004" : "3005");
            const int l = len >= 4 ? 4 : (len >= 2 ? 2 : 1);
            put(part, q90, (float)x, y, (float)z);
            z += l;
        }
    };
    for (int r = 0; r < rows; ++r) {
        const float y = y0 + r * brick;
        const int s = r & 1;
        // front (z = 0): a door of 3 studs, rows 0..4
        std::vector<std::pair<int, int>> door, win, none;
        if (r < 5) door.push_back({7, 10});
        if (r >= 2 && r < 5) win.push_back({4, 7});
        rowX(0, 0, W, y, s, door);
        rowX(D - 1, 0, W, y, s, win);   // back: a window
        std::vector<std::pair<int, int>> sideWin;
        if (r >= 2 && r < 5) sideWin.push_back({4, 7});
        rowZ(0, 1, D - 1, y, s, none);
        rowZ(W - 1, 1, D - 1, y, s, sideWin);   // right side: a window
    }
    // roof: plates over (W + 2) x (D + 2) studs (one stud of overhang), two layers
    const float yr = y0 + rows * brick;
    for (int layer = 0; layer < 2; ++layer)
        for (int x = -1; x < W + 1; x += 2)
            for (int z = -1; z < D + 1; z += 4) put(z + 4 <= D + 1 ? "3020" : "3022", q90, (float)x, yr + layer * plate, (float)z);
    // a step in front of the door
    put("3010", q0, 6.0f, plate, -1.0f);
    put("3010", q0, 6.0f, plate, -2.0f);
    rebuildScene();
    frameAll();
    const std::string text = m_build.toJson(m_scene.camera);
    FILE* f = nullptr;
    if (_wfopen_s(&f, m_opt.writeHouse.c_str(), L"wb") == 0 && f) { fwrite(text.data(), 1, text.size(), f); fclose(f); }
}

// GI look development (--gi-lookdev, Réglages › Lumière › Lookdev GI): every colour the same light grey, rough, no
// specular and no coat, a light SSS (the lighting alone, in HQ, RC and PT); off: the palette materials back. The
// dimmed copies (focus mode) follow.
void BrickApp::setLookdev(bool on) {
    m_giLookdev = on;
    for (size_t i = 0; i < m_colorMaterial.size() && i < m_colorMaterialOrig.size(); ++i) {
        Material m = m_colorMaterialOrig[i];
        if (on) {
            m.baseColor = float3(srgbToLinear(0.78f), srgbToLinear(0.79f), srgbToLinear(0.80f));
            m.roughness = 0.9f;
            m.specular = 0.0f;
            m.coat = 0.0f;
            m.metallic = 0.0f;
            m.transmission = 0.0f;
            m.subsurface = 0.12f;
            m.sssRadius = float3(0.06f, 0.06f, 0.06f);
        }
        m_scene.materials[m_colorMaterial[i]] = m;
        if (i < m_dimMaterial.size()) {
            Material d = m;
            d.name += " (dim)";
            d.baseColor = d.baseColor * 0.5f;
            d.specular *= 0.5f;
            d.coat *= 0.5f;
            m_scene.materials[m_dimMaterial[i]] = d;
        }
    }
    ++m_scene.objectsVersion;   // (the materials are uploaded again, the accumulations restart)
    m_renderer.resetAccumulation();
}

void BrickApp::openTestHouse() {
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir(exe);
    dir = dir.substr(0, dir.find_last_of(L"\\/"));   // build\bin
    const std::wstring path = dir + L"\\..\\..\\docs\\brickit\\tests\\maison_gi.v3d.json";
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) { toast("Maison test introuvable (docs\\brickit\\tests\\maison_gi.v3d.json)"); return; }
    openScene(path);
}

void BrickApp::buildHdriList() {
    m_hdriPaths = {L""};   // the procedural sky
    WIN32_FIND_DATAW fd;
    for (const wchar_t* ext : {L"*.hdr", L"*.exr"}) {
        HANDLE h = FindFirstFileW((m_exeDir + L"\\assets\\hdri\\" + ext).c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) m_hdriPaths.push_back(m_exeDir + L"\\assets\\hdri\\" + fd.cFileName);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    for (const std::wstring& c : m_hdriCustom)
        if (std::find(m_hdriPaths.begin(), m_hdriPaths.end(), c) == m_hdriPaths.end() && fileExists(c)) m_hdriPaths.push_back(c);
}

// Loaded once (cached): switching back is instant. The renderer rebuilds the importance table of the path tracer,
// the SH and the mips of the raster view when the image changes (Renderer::uploadEnvironment).
void BrickApp::setHdri(const std::wstring& path) {
    const auto t0 = std::chrono::steady_clock::now();
    vp3d::EnvImagePtr img;
    auto it = m_hdriCache.find(path);
    if (it != m_hdriCache.end()) img = it->second;
    else {
        auto e = std::make_shared<EnvImage>();
        std::string err;
        if (path.empty() || m_opt.noHdri || !fileExists(path) || !loadEnvImage(path, *e, &err)) {
            if (!path.empty()) m_status = "HDRI illisible, ciel procédural : " + err;
            makeProceduralSky(*e);
        }
        img = e;
        m_hdriCache[path] = img;
    }
    m_hdriPath = path;
    m_scene.env.hdri = m_opt.noEnv ? nullptr : img;   // --no-env: no environment image at all (profiling)
    m_hdriLoadMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
    m_renderer.resetAccumulation();
}

void BrickApp::loadHdriDialog() {
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {sizeof(ofn)};
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"HDRI (*.hdr, *.exr)\0*.hdr;*.exr\0Tous les fichiers\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;
    const std::wstring p = file;
    if (std::find(m_hdriCustom.begin(), m_hdriCustom.end(), p) == m_hdriCustom.end()) m_hdriCustom.push_back(p);
    buildHdriList();
    setHdri(p);
    toast("HDRI chargée");
    savePrefs();
}

uint32_t BrickApp::gridLineValue() const {
    if (m_gridAlpha <= 0.0f) return 0u;
    auto b = [](float v) { return (uint32_t)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f); };
    return b(m_gridRgb.x) | (b(m_gridRgb.y) << 8) | (b(m_gridRgb.z) << 16) | (std::max(1u, b(m_gridAlpha)) << 24);
}

void BrickApp::applyLook() {
    if (m_cornell) { m_renderer.resetAccumulation(); return; }   // (the scene keeps its own look)
    auto lin = [](int hex) { return float3(srgbToLinear(((hex >> 16) & 255) / 255.0f), srgbToLinear(((hex >> 8) & 255) / 255.0f), srgbToLinear((hex & 255) / 255.0f)); };
    Material& g = m_scene.materials[0];
    {   // every mode (Wzx: the dark floor of the Lit views in Radiance Cascades and path tracing too)
        m_scene.env.top = m_scene.env.horizon = m_scene.env.bottom = lin(0x111214);
        m_scene.env.hdriIntensity = m_hdriInt;   // Réglages > Lumière (0.55 by default, the web's)
        m_scene.env.hdriRotation = radians(m_hdriRotDeg);
        m_scene.gridLine = gridLineValue();
        g.baseColor = lin(0x0e0f11);
        g.roughness = 0.8f;
        m_scene.sun.enabled = true;
        m_scene.sun.direction = normalize(float3(-28, 34, 30));
        m_scene.sun.color = lin(0xfff1e0);
        m_scene.sun.intensity = 3.4f;
        m_scene.env.hemiSky = lin(0x9aa3b8);   // HemisphereLight(0x9aa3b8, 0x1a1a1c, 0.25)
        m_scene.env.hemiGround = lin(0x1a1a1c);
        m_scene.env.hemiIntensity = 0.25f;
        if (m_rimLight >= 0) m_scene.lights[m_rimLight].enabled = true;
        m_rs.exposure = 1.0f;
        m_rs.tonemapper = 0;
    }
    ++m_scene.objectsVersion;
    m_renderer.resetAccumulation();
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
    if (m_cornell) return;
    m_scene.objects.resize(m_firstBrickObject);
    m_scene.objects.reserve(m_firstBrickObject + m_bricks.size());
    for (const Brick& b : m_bricks) {
        Object o;
        o.mesh = meshOf(b.part);
        if (o.mesh < 0) o.visible = false;
        o.useRot = true;
        m_scene.addObject(o);
    }
    {   // ghost of the brick in hand (placement preview), after the bricks
        Object g;
        g.ghost = true;
        g.ghostAlpha = 0.32f;
        g.visible = false;
        g.selectable = false;
        g.useRot = true;
        g.material = m_ghostMaterial;
        m_ghostObject = m_scene.addObject(g);
    }
    m_settles.clear();
    syncObjects();
}

// Transforms and materials of the brick objects: colour, focus mode (the rest at 50 %), the brick in hand red and
// translucent when it cannot be dropped.
void BrickApp::syncObjects() {
    if (m_cornell) return;
    const bool dim = m_focusDim && (m_selected >= 0 || m_holding);
    for (size_t i = 0; i < m_bricks.size(); ++i) {
        Object& o = m_scene.objects[m_firstBrickObject + i];
        const Brick& b = m_bricks[i];
        o.visible = o.mesh >= 0 && !brickHidden((int)i) && !readHidden((int)i);   // a group hidden or another isolated (outliner), a step not read yet
        o.position = b.pos;
        o.rot = b.rot;
        const int c = std::clamp(b.color, 0, (int)m_colorMaterial.size() - 1);
        const bool focus = (int)i == m_selected || (m_holding && (int)i == m_held.brick) ||
                           std::find(m_multi.begin(), m_multi.end(), (int)i) != m_multi.end();
        o.material = dim && !focus ? m_dimMaterial[c] : m_colorMaterial[c];
        if (m_mechOn && m_mechTint && i < m_mm.brickBody.size()) {   // Mechanical mode: one colour per body (golden angle hues)
            const int body = m_mm.brickBody[i];
            while ((int)m_tintMaterial.size() <= body) {
                const int k = (int)m_tintMaterial.size();
                float h = std::fmod(k * 0.618034f, 1.0f) * 6.0f, x = 1.0f - std::fabs(std::fmod(h, 2.0f) - 1.0f);
                float3 rgb = h < 1 ? float3(1, x, 0) : h < 2 ? float3(x, 1, 0) : h < 3 ? float3(0, 1, x) : h < 4 ? float3(0, x, 1) : h < 5 ? float3(x, 0, 1) : float3(1, 0, x);
                Material t;
                t.name = "Body tint";
                t.baseColor = float3(srgbToLinear(0.075f + rgb.x * 0.85f), srgbToLinear(0.075f + rgb.y * 0.85f), srgbToLinear(0.075f + rgb.z * 0.85f));
                t.roughness = 0.45f;
                m_tintMaterial.push_back(m_scene.addMaterial(t));
            }
            o.material = m_tintMaterial[body];
        }
        o.ghost = false;
        if (m_showVox && (int)i == focusBrick() && !m_mechOn) {   // voxels shown: the brick at 30 % so its dots show through
            o.ghost = true;
            o.ghostAlpha = 0.3f;
        }
        if (m_holding && (int)i == m_held.brick && m_held.blocked && !m_animOn) {   // no preview: the brick itself turns red
            o.material = m_blockedMaterial;
            o.ghost = true;
            o.ghostAlpha = 0.55f;
        }
    }
    ++m_scene.objectsVersion;
    m_renderer.resetAccumulation();
    if (!m_holding && !m_mechOn) autosave();
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
    if (!m_opt.shotPath.empty()) return;   // a capture (--shot): the camera of the command line, whatever the mouse does
    Camera& cam = m_scene.camera;
    bool changed = false;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) m_leftOrbit = false;
    // left drag on empty space (and in the Move / Rotate modes) orbits, as the web
    const bool orbit = ImGui::IsMouseDown(ImGuiMouseButton_Right) || (ImGui::IsMouseDown(ImGuiMouseButton_Left) && (io.KeyAlt || m_leftOrbit));
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
        if (ImGui::IsKeyPressed(ImGuiKey_Home)) { frameAll(); changed = true; }   // (native: F is the focus mode, as the web)
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_P)) { setRC(false); m_rs.raster = !m_rs.raster; applyLook(); changed = true; }
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
    if (m_frame == 2) {   // the PDF of the manual: imported by a test, else the one of the last session
        if (!m_opt.pdfPath.empty()) { std::wstring p = m_opt.pdfPath; std::wstring n = p.substr(p.find_last_of(L"\\/") + 1); pdfOpen(p, std::string(n.begin(), n.end()), false); }
        else if (GetFileAttributesW((m_exeDir + L"\\manual_pdf\\current.pdf").c_str()) != INVALID_FILE_ATTRIBUTES && m_opt.shotPath.empty()) pdfOpen(m_exeDir + L"\\manual_pdf\\current.pdf", m_pdfName, false);
    }
    pdfPump();
    if (m_prefsDirty && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) { m_prefsDirty = false; savePrefs(); }
    mouseTestFrame();
    if (m_frame == 1 && !m_opt.ttBase.empty()) {   // self-test: a turntable base and its top on one axis -> one pivot
        Part* b = m_lib.get(m_opt.ttBase);
        Part* t = m_lib.get(m_opt.ttTop);
        FILE* f = nullptr;
        if (b && t && _wfopen_s(&f, (m_exeDir + L"\\mech_test.txt").c_str(), L"w") == 0 && f) {
            m_bricks.clear();
            Brick bb, tb;
            bb.part = m_lib.find(m_opt.ttBase);
            tb.part = m_lib.find(m_opt.ttTop);
            bb.color = tb.color = m_color;
            bb.pos = float3(0, -b->bmin.y, 0);
            const float3 cb = (b->bmin + b->bmax) * 0.5f, ct = (t->bmin + t->bmax) * 0.5f;
            tb.pos = float3(cb.x - ct.x, bb.pos.y + b->bmax.y - t->bmin.y, cb.z - ct.z);
            m_bricks.push_back(bb);
            m_bricks.push_back(tb);
            rebuildScene();
            MechModel mm;
            const bool ok = m_mech.build(m_bricks, mm);
            fprintf(f, "base=%s top=%s ok=%d bodies=%d joints=%zu cmds=%zu warnings=%zu\n", m_opt.ttBase.c_str(), m_opt.ttTop.c_str(), ok, mm.bodyCount, mm.tree.size(), mm.cmds.size(), mm.warnings.size());
            for (auto& w : mm.warnings) fprintf(f, "  %s\n", w.c_str());
            fclose(f);
        }
    }
    if (m_frame == 3 && !m_opt.mayaOut.empty()) mayaExportScene(m_opt.mayaOut);   // scripted test: Maya export
    if (m_frame == 3 && !m_opt.devRoundtrip.empty()) {   // self-test: OBJ export of a part, re-import, fit (offset and scale must be identity)
        int pi = m_lib.find(m_opt.devRoundtrip);
        Part* pp = pi >= 0 ? m_lib.get(m_opt.devRoundtrip) : nullptr;
        FILE* f = nullptr;
        if (pp && _wfopen_s(&f, (m_exeDir + L"\\dev_roundtrip.txt").c_str(), L"w") == 0 && f) {
            std::string obj = devObjText(pi, pp->mesh, pp->ldrawOrigin, true);
            MeshData g;
            std::map<std::string, std::string> hd;
            bool ok = devParseObj(obj, g, hd);
            DevReview r;
            if (ok) devFit(pi, g, r);
            fprintf(f, "part=%s ok=%d bytes=%zu tris_in=%zu tris_out=%zu header=%s scale=%g offset=%g,%g,%g swapped=%d note=%s\n", pp->name.c_str(), ok, obj.size(),
                    pp->mesh.indices.size() / 3, g.indices.size() / 3, hd["brickit-part"].c_str(), r.scale, r.offset.x, r.offset.y, r.offset.z, r.swapped, r.note.c_str());
            fclose(f);
        }
    }
    if (m_frame == 24 && !m_opt.infoPart.empty()) m_infoPart = m_lib.find(m_opt.infoPart);   // scripted test: part sheet
    if (!m_opt.noUi) drawUi();
    cameraInput();
    if (m_mechOn) mechUpdate((float)ImGui::GetIO().DeltaTime);
    else editInput();
    updateAnim(std::min(0.05f, (float)ImGui::GetIO().DeltaTime));
    if (ImGui::IsKeyPressed(ImGuiKey_M) && !ImGui::GetIO().WantTextInput && !ImGui::GetIO().KeyCtrl) { if (m_mechOn) mechExit(); else mechEnter(); }

    ImVec2 ds = ImGui::GetIO().DisplaySize;
    // Path tracing: half resolution while the view moves (and 0.25 s after), full resolution to converge when still.
    const bool moving = ImGui::GetTime() - m_lastMove < 0.25 && m_opt.shotPath.empty();
    const float scale = (pathTraced() && moving) ? m_opt.ptMoveScale : 1.0f;
    // the stage: between the top bar and the tray (no UI: the whole window)
    if (m_opt.noUi || m_uiRects.size() < 2) m_stage = ImVec4(0, 0, ds.x, ds.y);
    else m_stage = ImVec4(0, m_uiRects[0].w, ds.x, std::max(m_uiRects[0].w + 16, m_uiRects[1].y));
    const float sw = m_stage.z - m_stage.x, sh = m_stage.w - m_stage.y;
    m_vp = {0, 0, std::max(16, (int)(sw * scale)), std::max(16, (int)(sh * scale))};
    if (m_rs.raster || m_rcMode) m_rs.samplesPerFrame = 1;
    else if (!m_opt.shotPath.empty() || m_opt.ppf > 1) m_rs.samplesPerFrame = std::max(1, m_opt.ppf);   // captures: fixed
    else {   // adaptive, as WZX Engine (App::frame): 1 sample and 1 pixel in 4 while the view moves; still: every pixel,
             // one more sample per frame while the GPU stays under 9 ms (up to 8), one less above 22 ms
        if (moving) { m_adaptiveSpp = 1; m_rs.traceStride = m_opt.stride > 0 ? m_opt.stride : 4; }
        else {
            const float g = m_renderer.gpuFrameMs();
            if (g < 9.0f && m_adaptiveSpp < 8) ++m_adaptiveSpp;
            else if (g > 22.0f && m_adaptiveSpp > 1) --m_adaptiveSpp;
            m_rs.traceStride = m_opt.stride > 0 ? m_opt.stride : 1;
        }
        m_rs.samplesPerFrame = m_adaptiveSpp;
    }
    // HQ closer to the path tracing (Wzx): every light shadowed by rays, the environment occluded by rays (1.2 cm), the
    // parts and the HDRI reflected by traced rays on the glossy plastic (inline ray queries on the TLAS of the bevel)
    m_rs.rasterRT = m_rs.raster && m_quality == 1;
    m_rs.rasterAO = m_opt.hqAO >= 0 ? m_opt.hqAO : (m_ssaoOn ? 1.2f : 0.0f);   // Réglages > Occlusion HDRI (HQ), on by default
    m_rs.reflSamples = m_opt.hqRefl >= 0 ? m_opt.hqRefl : 1;
    if (m_opt.hqRefl == 0) m_rs.rtReflections = false;
    m_rs.reflMaxRough = 0.35f;
    m_rs.ssao = m_rs.raster && m_quality == 1 && m_ssaoOn && !m_rs.rasterRT;   // (the traced occlusion replaces the SSAOPass of the web)
    m_rs.ssaoRadius = 0.6f;
    m_rs.rasterBevel = m_quality == 1 || m_rcMode;
    // subsurface of the real time views (HQ, RC): the light wrapped past the terminator, tinted (Réglages › Lumière)
    m_rs.rasterSSS = (m_quality == 1 || m_rcMode) ? (m_opt.hqSSS >= 0 ? m_opt.hqSSS : m_hqSSSStrength) : 0.0f;
    // Radiance Cascades: GI bounds = the bricks + a margin, fading out over the same distance; the ground beyond gets
    // the analytic sky ambient (else its probe cells reached the horizon and overflowed the hash tables when the camera
    // tilts: light and dark squares). Recomputed when the scene changes.
    if (m_rcMode && m_giBoundsVersion != m_scene.objectsVersion) {
        m_giBoundsVersion = m_scene.objectsVersion;
        float3 lo(1e30f), hi(-1e30f);
        for (const Brick& b : m_bricks) {
            const Part& p = m_lib.parts[b.part];
            for (int c = 0; c < 8; ++c) {
                const float3 v((c & 1) ? p.bmax.x : p.bmin.x, (c & 2) ? p.bmax.y : p.bmin.y, (c & 4) ? p.bmax.z : p.bmin.z);
                const float3 wv = qRot(b.rot, v) + b.pos;
                lo = float3(std::min(lo.x, wv.x), std::min(lo.y, wv.y), std::min(lo.z, wv.z));
                hi = float3(std::max(hi.x, wv.x), std::max(hi.y, wv.y), std::max(hi.z, wv.z));
            }
        }
        if (m_bricks.empty()) { lo = float3(-8, 0, -8); hi = float3(8, 8, 8); }
        const float size = std::max(hi.x - lo.x, std::max(hi.y - lo.y, hi.z - lo.z));
        const float margin = std::max(6.0f, size * 0.35f);
        m_rs.giBoundsMin = lo - float3(margin);
        m_rs.giBoundsMax = hi + float3(margin);
        m_rs.giBoundsFade = margin;
    }
    if (!m_rcMode || m_opt.noGiBounds) m_rs.giBoundsFade = 0.0f;   // rounded edges in the real time views: HQ and RC (inline ray queries, about 1 ms)
    std::vector<int> extra;
    for (int i : m_multi) extra.push_back(m_firstBrickObject + i);
    m_renderer.setExtraSelection(extra);
    {   // edge outline of the selected / held brick and the rest of the selection (thin white lines, red when blocked)
        std::vector<int> edge;
        const int fb = focusBrick();
        if (m_reading) for (int i : readStepBricks()) edge.push_back(m_firstBrickObject + i);   // the parts of the step read
        else {
            if (fb >= 0 && fb < (int)m_bricks.size() && !m_mechOn) edge.push_back(m_firstBrickObject + fb);
            if (!m_mechOn) for (int i : m_multi) edge.push_back(m_firstBrickObject + i);
        }
        // (Réglages › Lumière: Contour / Voile de sélection; red stays readable when the brick in hand is blocked)
        m_renderer.setEdgeOutline(edge, m_holding && m_held.blocked, m_holding && m_held.blocked ? std::max(0.35f, m_selOutline) : m_selOutline);
        m_rs.selectionFill = m_selFill;
    }
    const int selObj = (m_selected >= 0 && !m_holding) ? m_firstBrickObject + m_selected : -1;
    m_rs.pixelScale = scale;
    // part sheet open: the main view keeps its last image, the renderer draws the 3D viewer of the part
    const bool sheet = m_infoPart >= 0 && !m_dev;
    if (sheet && !m_frozenTex && m_frame > 0) {
        std::vector<uint8_t> img;
        uint32_t w = 0, h = 0;
        if (m_renderer.readViewport(img, w, h) && w && h) m_frozenTex = m_gui.createTexture(img.data(), w, h);
    }
    if (!sheet && m_frozenTex) {
        m_gui.releaseTexture(m_frozenTex);
        m_frozenTex = 0;
        m_renderer.resetAccumulation();
    }
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(0x13, 0x14, 0x17, 255));
    if (m_frozenTex) {
        renderPreview();
        ImGui::GetBackgroundDrawList()->AddImage(m_frozenTex, ImVec2(m_stage.x, m_stage.y), ImVec2(m_stage.z, m_stage.w));
    } else {
        renderSteps();   // images of the manual asked by the UI this frame
        RenderSettings frs = m_rs;
        if (m_rs.raster && m_quality == 1 && m_softGI && !m_rcMode) {
            // HQ + GI douce: the raster view of the HQ reads the probes (DDGI, ray traced in the volume of the bricks,
            // long temporal average in world space: stable, converged while nothing moves)
            frs.raster = false;
            frs.ddgi = true;
            frs.ddgiRaster = false;
            frs.ddgiRasterView = true;
            frs.giMethod = 0;
            frs.rcSplit = false;
            frs.rcScreen = false;
            frs.ddgiHysteresis = 0.997f;   // (reached after ~300 still frames, 0.85 right after a change)
            frs.ddgiProgressive = true;
            frs.ddgiEnergy = m_softGIStrength;
            // a small view bias: under a roof overhang the lookup no longer reaches the probes out in the open
            // (maison_gi: gap to the PT 12.8 -> 10.9 %, under the overhang of the door view +55 -> +31 %)
            frs.ddgiViewBias = 0.10f;
            frs.ddgiProbeBudget = m_opt.softGIProbes > 0 ? m_opt.softGIProbes : m_softGIProbes;
            frs.ddgiShowProbes = m_opt.showProbes >= 0 ? m_opt.showProbes != 0 : m_showProbes;   // the probes as small spheres with their light (Réglages › Lumière)
            // a brick carried (picked, duplicated, from the library), the gizmo or a mechanism dragged: the probes are
            // held, the scene does not refresh around the brick in the air; released, one restart around where it was
            // and where it is (Wzx)
            // converged probes are left alone: only those around what changed are computed again (Wzx)
            frs.ddgiAgeCap = m_opt.giAgeCap;
            frs.ddgiHold = m_softGIHold && m_opt.giHold != 0 && (m_holding || m_gizmoDragging || m_mechDragging || m_groupDrag || m_testCarry);
            frs.samplesPerFrame = 1;
            frs.historyFixes = true;
            // the bounds of the probe grid: a change recreates the probes (all of the GI restarts). Held while a brick is
            // carried (else a brick lifted over the roof grew them: the whole scene refreshed, Wzx), and kept while the
            // bricks stay inside them (their margin included) with less than a margin of empty room on each axis
            if ((m_giBoundsVersionSoft != m_scene.objectsVersion || frs.giBoundsFade <= 0) && !(frs.ddgiHold && m_softMargin > 0)) {
                m_giBoundsVersionSoft = m_scene.objectsVersion;
                float3 lo(1e30f), hi(-1e30f);
                for (const Brick& b : m_bricks) {
                    const Part& p = m_lib.parts[b.part];
                    for (int c = 0; c < 8; ++c) {
                        const float3 v((c & 1) ? p.bmax.x : p.bmin.x, (c & 2) ? p.bmax.y : p.bmin.y, (c & 4) ? p.bmax.z : p.bmin.z);
                        const float3 wv = qRot(b.rot, v) + b.pos;
                        lo = float3(std::min(lo.x, wv.x), std::min(lo.y, wv.y), std::min(lo.z, wv.z));
                        hi = float3(std::max(hi.x, wv.x), std::max(hi.y, wv.y), std::max(hi.z, wv.z));
                    }
                }
                if (m_bricks.empty()) { lo = float3(-8, 0, -8); hi = float3(8, 8, 8); }
                const float size = std::max(hi.x - lo.x, std::max(hi.y - lo.y, hi.z - lo.z));
                const float margin = std::max(4.0f, size * 0.25f) * m_opt.softGIMargin;
                float3 nlo = lo - float3(margin * 0.5f, 0.0f, margin * 0.5f);
                nlo.y = std::min(lo.y, -0.5f);   // the probes start just under the ground
                const float3 nhi = hi + float3(margin * 0.5f);
                const float3 a = m_softLo, z = m_softHi;
                const bool fits = m_softMargin > 0 && lo.x >= a.x && lo.y >= a.y && lo.z >= a.z && hi.x <= z.x && hi.y <= z.y && hi.z <= z.z;
                const bool tight = fits && (nlo.x - a.x) + (z.x - nhi.x) < margin && (nlo.y - a.y) + (z.y - nhi.y) < margin &&
                                   (nlo.z - a.z) + (z.z - nhi.z) < margin;
                if (!tight) { m_softMargin = margin; m_softLo = nlo; m_softHi = nhi; }
            }
            frs.giBoundsMin = m_softLo;
            frs.giBoundsMax = m_softHi;
            frs.giBoundsFade = m_softMargin * 0.5f;
        }
        m_renderer.renderToTexture(m_scene, frs, m_vp, selObj);
        ImGui::GetBackgroundDrawList()->AddImage(m_gui.viewportTexture(), ImVec2(m_stage.x, m_stage.y), ImVec2(m_stage.z, m_stage.w));
        drawVoxels();          // over the 3D view, under the panels
        drawHoldOverlay();
        drawPivots();
        drawGizmo();   // over the pivot markers (renderOrder 1010 > 1003 in the web)
        drawMechOverlay();
        drawDevOverlay();
    }
    LARGE_INTEGER t1;
    QueryPerformanceCounter(&t1);
    m_cpuMs = m_cpuMs * 0.9 + 0.1 * (t1.QuadPart - t0.QuadPart) * 1000.0 / m_qpf.QuadPart;
    m_gui.render(true);
    ++m_frame;
    // GI hold test: a brick carried in the air (swinging), then put down on the ground 3.5 cm in front of where it was
    if (!m_opt.groupTest.empty() && m_frame == 5) groupTest();
    if (!m_opt.importPath.empty() && m_frame == 3) {   // an LDraw model, then the page asked
        const auto t0 = GetTickCount64();
        importLDraw(m_opt.importPath);
        const auto ms = GetTickCount64() - t0;
        if (m_opt.readStep > 0) readStepTo(m_opt.readStep);
        else if (m_opt.readStep == 0) { stopReading(); frameAll(); }
        if (!m_opt.importReport.empty()) {
            FILE* rf = nullptr;
            if (_wfopen_s(&rf, m_opt.importReport.c_str(), L"wb") == 0 && rf) { fprintf(rf, "import_ms=%llu\n%s", (unsigned long long)ms, m_importReport.c_str()); fclose(rf); }
        }
    }
    if (m_opt.giCarryBrick >= 0 && m_opt.giCarryBrick < (int)m_bricks.size()) {
        static float3 home;
        Brick& b = m_bricks[m_opt.giCarryBrick];
        if (m_frame == m_opt.giCarryFrom) home = b.pos;
        if (m_frame >= m_opt.giCarryFrom && m_frame < m_opt.giCarryTo) {
            const float t = (m_frame - m_opt.giCarryFrom) * 0.05f;
            b.pos = home + float3(3.0f * std::sin(t), 2.5f + std::sin(t * 1.7f), -3.0f + 2.0f * std::cos(t));
            m_testCarry = true;
            rebuildScene();
        } else if (m_frame == m_opt.giCarryTo) {
            b.pos = float3(home.x, 0.0f, home.z - 3.5f);
            m_testCarry = false;
            rebuildScene();
        }
    }
    // GI reactivity test: a brick taken out, then put back (the frames the probes need to settle, in the captures)
    if (m_opt.giEditBrick >= 0 && m_opt.giEditBrick < (int)m_bricks.size() + 1) {
        static Brick removed;
        if (m_frame == m_opt.giEditOff && m_opt.giEditBrick < (int)m_bricks.size()) {
            removed = m_bricks[m_opt.giEditBrick];
            m_bricks.erase(m_bricks.begin() + m_opt.giEditBrick);
            rebuildScene();
        } else if (m_frame == m_opt.giEditOn && m_opt.giEditOn > m_opt.giEditOff) {
            m_bricks.insert(m_bricks.begin() + m_opt.giEditBrick, removed);
            rebuildScene();
        }
    }

    if (!m_opt.shotPath.empty()) {
        bool ready = (m_rs.raster || m_rcMode) ? m_frame >= m_opt.shotFrames : (int)m_renderer.sampleCount() >= m_opt.shotSpp;
        if (ready && m_frame > 6) {
            std::vector<uint8_t> img;
            uint32_t w, h;
            if (m_renderer.readBackBuffer(img, w, h)) savePng(m_opt.shotPath, img, w, h);
            if (!m_opt.savePath.empty()) saveScene(m_opt.savePath);
            // timings next to the capture (benchmarks): bricks, triangles drawn, GPU and CPU ms
            // picking cost: 64 rays over the view (the boxes are already cached by the first one)
            double pickMs = 0;
            {
                ImVec2 ds = ImGui::GetIO().DisplaySize;
                pickBrick(ds.x * 0.5f, ds.y * 0.5f);
                LARGE_INTEGER p0, p1;
                QueryPerformanceCounter(&p0);
                for (int k = 0; k < 64; ++k) pickBrick(ds.x * ((k % 8) + 0.5f) / 8, ds.y * ((k / 8) + 0.5f) / 8);
                QueryPerformanceCounter(&p1);
                pickMs = (p1.QuadPart - p0.QuadPart) * 1000.0 / m_qpf.QuadPart / 64;
            }
            FILE* f = nullptr;
            if (_wfopen_s(&f, (m_opt.shotPath + L".txt").c_str(), L"w") == 0 && f) {
                fprintf(f, "bricks=%d mode=%s gpu_ms=%.2f cpu_ms=%.2f frame_ms=%.2f tris_drawn=%.2fM spp=%u pick_ms=%.3f gi_update_ms=%.3f gi_blend_ms=%.3f\n", (int)m_bricks.size(),
                        m_rcMode ? "radiance_cascades" : (m_rs.raster ? "raster" : "pathtrace"), m_renderer.gpuFrameMs(), m_cpuMs, m_frameMs,
                        m_renderer.rasterTriangles() / 1e6, m_renderer.sampleCount(), pickMs, m_renderer.giUpdateMs(), m_renderer.giBlendMs());
                fclose(f);
            }
            m_running = false;
        }
    }
    m_renderer.present(m_opt.shotPath.empty());
    if (!m_opt.ptTrace.empty()) {   // convergence: samples accumulated and the cost of each frame
        static FILE* tf = nullptr;
        if (!tf) _wfopen_s(&tf, m_opt.ptTrace.c_str(), L"w");
        if (tf) { fprintf(tf, "%d %u %.2f %.2f %d %d\n", m_frame, m_renderer.sampleCount(), m_renderer.gpuFrameMs(), m_frameMs, m_vp.w, m_vp.h); fflush(tf); }
    }
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
        else if (a == L"--rc") opt.rc = true;   // Radiance Cascades mode
        else if (a == L"--no-gi-bounds") opt.noGiBounds = true;
        else if (a == L"--hdri") opt.hdri = next();
        else if (a == L"--hdri-rot") opt.hdriRot = (float)_wtof(next().c_str());
        else if (a == L"--hdri-int") opt.hdriInt = (float)_wtof(next().c_str());
        else if (a == L"--grid-color") opt.gridColor = (int)wcstol(next().c_str(), nullptr, 16);
        else if (a == L"--grid-alpha") opt.gridAlpha = (float)_wtof(next().c_str());
        else if (a == L"--soft-gi") opt.softGI = _wtoi(next().c_str());
        else if (a == L"--gi-strength") opt.giStrength = (float)_wtof(next().c_str());
        else if (a == L"--soft-gi-margin") opt.softGIMargin = (float)_wtof(next().c_str());
        else if (a == L"--show-probes") opt.showProbes = _wtoi(next().c_str());
        else if (a == L"--soft-gi-probes") opt.softGIProbes = _wtoi(next().c_str());
        else if (a == L"--build-full-thumbs") opt.fullThumbs = next();
        else if (a == L"--import") opt.importPath = next();
        else if (a == L"--read-step") opt.readStep = _wtoi(next().c_str());
        else if (a == L"--import-report") opt.importReport = next();
        else if (a == L"--group-test") opt.groupTest = next();
        else if (a == L"--gi-hold") opt.giHold = _wtoi(next().c_str());
        else if (a == L"--hq-sss") opt.hqSSS = (float)_wtof(next().c_str());
        else if (a == L"--gi-age-cap") opt.giAgeCap = _wtoi(next().c_str());
        else if (a == L"--gi-carry") { opt.giCarryBrick = _wtoi(next().c_str()); opt.giCarryFrom = _wtoi(next().c_str()); opt.giCarryTo = _wtoi(next().c_str()); }
        else if (a == L"--gi-edit") { opt.giEditBrick = _wtoi(next().c_str()); opt.giEditOff = _wtoi(next().c_str()); opt.giEditOn = _wtoi(next().c_str()); }
        else if (a == L"--write-house") opt.writeHouse = next();
        else if (a == L"--gi-lookdev") opt.giLookdev = true;
        else if (a == L"--hq-ao") opt.hqAO = (float)_wtof(next().c_str());   // (measures: traced occlusion range, 0 = off)
        else if (a == L"--hq-refl") opt.hqRefl = _wtoi(next().c_str());   // (measures: traced reflection rays, 0 = off)   // (comparison: the GI over the whole visible ground)
        else if (a == L"--aa") opt.aa = _wtoi(next().c_str());
        else if (a == L"--shadows") opt.shadows = _wtoi(next().c_str());
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
        else if (a == L"--yaw") opt.yaw = (float)_wtof(next().c_str());
        else if (a == L"--pitch") opt.pitch = (float)_wtof(next().c_str());
        else if (a == L"--no-hdri") opt.noHdri = true;
        else if (a == L"--no-env") opt.noEnv = true;
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
        else if (a == L"--mode") opt.mode = _wtoi(next().c_str());
        else if (a == L"--info") { std::wstring w = next(); opt.infoPart.assign(w.begin(), w.end()); }
        else if (a == L"--pivot-edit") opt.pivotEdit = true;
        else if (a == L"--dev") opt.dev = true;
        else if (a == L"--pdf") opt.pdfPath = next();
        else if (a == L"--manual") opt.manual = true;
        else if (a == L"--float-tray") { opt.floatTray = true; opt.floatX = (float)_wtof(next().c_str()); opt.floatY = (float)_wtof(next().c_str()); }
        else if (a == L"--opts-right") opt.optsRight = true;
        else if (a == L"--cornell") opt.cornell = true;
        else if (a == L"--mechtests") opt.mechTests = true;   // the Cornell box of WZX Engine (comparison of the renderers)
        else if (a == L"--pt-trace") opt.ptTrace = next();   // per frame: samples, ms (convergence)
        else if (a == L"--settings") opt.settings = true;
        else if (a == L"--quality") opt.quality = _wtoi(next().c_str());   // 0 Rapide, 1 HQ
        else if (a == L"--mouse-test") { std::wstring w = next(); opt.mouseTest.assign(w.begin(), w.end()); }
        else if (a == L"--turntable-test") { std::wstring w1 = next(), w2 = next(); opt.ttBase.assign(w1.begin(), w1.end()); opt.ttTop.assign(w2.begin(), w2.end()); }
        else if (a == L"--maya") opt.mayaOut = next();
        else if (a == L"--dev-roundtrip") { std::wstring w = next(); opt.devRoundtrip.assign(w.begin(), w.end()); }   // 0 select, 1 visor, 2 move, 3 rotate
        else if (a == L"--cat") opt.libCategory = _wtoi(next().c_str());
        else if (a == L"--focus") opt.focusDim = true;
        else if (a == L"--save") opt.savePath = next();
        else if (a == L"--dup") opt.duplicate = true;
        else if (a == L"--mechdemo") opt.mechDemo = true;
        else if (a == L"--mech") opt.mech = true;
        else if (a == L"--stride") opt.stride = _wtoi(next().c_str());
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
