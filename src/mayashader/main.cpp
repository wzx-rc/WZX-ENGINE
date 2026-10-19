// WzxShaderGraph - the WZX node graph editor as a Maya shading tool (throwaway prototype).
// The window is embedded in a dockable Maya panel (tools/maya/scripts/wzx_shader_panel.py): Maya starts the exe
// with --embed, reads the window handle printed on stdout and wraps it in a Qt container. Every edit of the graph
// is saved to the graph file and Maya is asked, through its commandPort, to rebuild the matching Arnold / Maya
// nodes (wzx_shader_bridge.sync_file). The right column shows a path traced preview of the material (vp3d).
#include "Preview.h"
#include "MayaLink.h"
#include "Flatten.h"
#include "Equivalences.h"
#include "Library.h"
#include <map>
#include "app/ImGuiLayer.h"
#include "app/Theme.h"
#include "app/AtelierIcons.h"
#include "app/Icons.h"
#include "SettingsWindow.h"
#include "game/NodeEditor.h"
#include "backends/imgui_impl_win32.h"
#include <windows.h>
#include <shellapi.h>
#include <wincodec.h>
#include <cstdio>
#include <string>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

using namespace game;
namespace atl = app::atl;

namespace {

struct Options {
    bool embed = false;
    int port = 7811;
    std::wstring graphPath;
    std::wstring settingsPath;
    std::wstring hdri;
    DWORD mayaPid = 0;
    std::wstring shotPath;
    bool openSettings = false;
    bool openPreview = false;    // the material preview is hidden (and off) by default
    int mode = 0;                // workspace at start: 0 graph, 1 library, 2 equivalences
    std::wstring library, saveLib;   // test: library folder, "Category/Name" saved at start
    int libTab = -1;
    bool autoCheck = false;   // test: Check in Maya once connected
    std::wstring mappings;       // equivalences file (default: tools/maya/mappings/equivalences.json)
    int enterCompound = 0;       // test: compound opened at start
    std::wstring collapse;       // test: node ids (comma separated) moved into a compound at start
    int hoverLink = -1;          // test: link shown as hovered
    int shotFrames = 120;
    bool shotPreview = false;    // --shot-preview: the preview image only
    int previewLights = 1;
    bool acescg = true;          // Maya rendering space of the colours
};

bool savePng(const std::wstring& path, std::vector<uint8_t> rgba, uint32_t w, uint32_t h) {
    for (size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 255;
    vp3d::ComPtr<IWICImagingFactory> f;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) return false;
    vp3d::ComPtr<IWICStream> st;
    f->CreateStream(&st);
    if (FAILED(st->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) return false;
    vp3d::ComPtr<IWICBitmapEncoder> enc;
    f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
    enc->Initialize(st.Get(), WICBitmapEncoderNoCache);
    vp3d::ComPtr<IWICBitmapFrameEncode> fr;
    vp3d::ComPtr<IPropertyBag2> props;
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

vp3d::Renderer g_renderer;
app::ImGuiLayer g_gui;
bool g_running = true;
bool g_minimized = false;

std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return 1;
    switch (msg) {
    case WM_CLOSE: g_running = false; DestroyWindow(hwnd); return 0;
    case WM_DESTROY: g_running = false; PostQuitMessage(0); return 0;
    case WM_MOUSEACTIVATE: SetFocus(hwnd); return MA_ACTIVATE;   // embedded in Maya: a click gives the keyboard focus
    case WM_SIZE:
        g_minimized = wp == SIZE_MINIMIZED;
        if (!g_minimized && g_renderer.device()) {
            UINT w = LOWORD(lp), h = HIWORD(lp);
            if (w > 0 && h > 0 && (w != g_renderer.width() || h != g_renderer.height())) {
                g_renderer.resize(w, h);
                g_gui.onResize();
            }
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

Options parseArgs() {
    Options o;
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        auto next = [&]() -> std::wstring { return i + 1 < argc ? argv[++i] : L""; };
        if (a == L"--embed") o.embed = true;
        else if (a == L"--port") o.port = _wtoi(next().c_str());
        else if (a == L"--graph") o.graphPath = next();
        else if (a == L"--settings") o.settingsPath = next();
        else if (a == L"--hdri") o.hdri = next();
        else if (a == L"--maya-pid") o.mayaPid = (DWORD)_wtoi(next().c_str());
        else if (a == L"--shot") o.shotPath = next();
        else if (a == L"--open-settings") o.openSettings = true;
        else if (a == L"--open-preview") o.openPreview = true;
        else if (a == L"--mode") o.mode = _wtoi(next().c_str());
        else if (a == L"--library") o.library = next();
        else if (a == L"--lib-tab") o.libTab = _wtoi(next().c_str());
        else if (a == L"--auto-check") o.autoCheck = true;
        else if (a == L"--save-lib") o.saveLib = next();
        else if (a == L"--mappings") o.mappings = next();
        else if (a == L"--enter") o.enterCompound = _wtoi(next().c_str());
        else if (a == L"--collapse") o.collapse = next();
        else if (a == L"--hover-link") o.hoverLink = _wtoi(next().c_str());
        else if (a == L"--frames") o.shotFrames = _wtoi(next().c_str());
        else if (a == L"--shot-preview") { o.shotPath = next(); o.shotPreview = true; }
        else if (a == L"--preview-lights") o.previewLights = _wtoi(next().c_str());
        else if (a == L"--rendering-space") o.acescg = next() == L"ACEScg";
    }
    LocalFree(argv);
    return o;
}

// Default graph: Ramp -> aiStandardSurface -> Material Output.
void defaultGraph(Graph& g, const std::string& name) {
    g.clear();
    g.name = name.empty() ? "wzxMaterial" : name;
    int out = g.addNode("out.material", float2(560, 40));
    int sh = g.addNode("shader.standardSurface", float2(260, 0));
    int ramp = g.addNode("tex.ramp", float2(-40, 0));
    auto pin = [&](int id, const char* n, bool output) {
        const Node* nd = g.node(id);
        return output ? nd->def->findOut(n) : nd->def->findIn(n);
    };
    g.connect(ramp, pin(ramp, "Color", true), sh, pin(sh, "Base Color", false));
    g.connect(sh, pin(sh, "Shader", true), out, pin(out, "Surface", false));
}

// The graph surfaces follow the UI theme (panels, cards, text, accent), as one coherent interface.
void applyGraphTheme() {
    const app::ThemePalette& p = app::th::P();
    auto mix = [](ImVec4 a, ImVec4 b, float t) { return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, 1); };
    GraphPalette g;
    g.canvas = app::th::u32(p.bg0);
    g.bar = app::th::u32(p.bar);
    g.panel = app::th::u32(p.bg1);
    g.node = app::th::u32(p.bg2);
    g.raised = app::th::u32(mix(p.bg2, p.hover, 0.5f));
    g.hover = app::th::u32(p.hover);
    g.border = app::th::u32(p.border);
    g.grid = app::th::u32(mix(p.border, p.dim, 0.25f));
    g.text = app::th::u32(p.text);
    g.muted = app::th::u32(p.dim);
    g.accent = app::th::u32(p.accent);
    g.select = app::th::u32(p.select);
    setGraphPalette(g);
}

atl::Icon nodeIcon(const NodeDef& d) {
    if (d.category == "Output") return atl::Icon::Check;
    if (d.category == "Shaders") return atl::Icon::Material;
    if (d.category == "Textures") return d.id == "tex.image" ? atl::Icon::Scene : atl::Icon::Grid;
    if (d.category == "Utilities") return atl::Icon::Settings;
    return atl::Icon::Plus;
}

void drawNodeIconFn(ImDrawList* dl, const std::string& id, ImVec2 p, float size, ImU32 color) {
    const NodeDef* d = findNodeDef(id);
    atl::DrawIcon(dl, d ? nodeIcon(*d) : atl::Icon::Blueprint, p, size, color);
}

} // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    Options opt = parseArgs();
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring exeDir = exe;
    exeDir = exeDir.substr(0, exeDir.find_last_of(L"\\/"));
    SetCurrentDirectoryW(exeDir.c_str());
    if (opt.graphPath.empty()) opt.graphPath = exeDir + L"\\wzxMaterial.graph.json";

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"WzxShaderGraph";
    RegisterClassExW(&wc);

    float dpi = GetDpiForSystem() / 96.0f;
    DWORD style = opt.embed ? WS_POPUP : WS_OVERLAPPEDWINDOW;
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"WZX Shader Graph", style, CW_USEDEFAULT, CW_USEDEFAULT,
                                (int)(1500 * dpi), (int)(820 * dpi), nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;
    if (opt.embed) {
        // Maya reads this line, then reparents the window into its dock (QWindow::fromWinId)
        printf("HWND %llu\n", (unsigned long long)(uintptr_t)hwnd);
        fflush(stdout);
    }

    RECT cr;
    GetClientRect(hwnd, &cr);
    std::string err;
    if (!g_renderer.init(hwnd, std::max(16L, cr.right - cr.left), std::max(16L, cr.bottom - cr.top), exeDir + L"\\shaders", &err)) {
        MessageBoxA(hwnd, err.c_str(), "WZX Shader Graph - DirectX 12", MB_ICONERROR);
        return 1;
    }
    if (!g_gui.init(hwnd, g_renderer, dpi)) return 1;
    ImGui::GetIO().IniFilename = nullptr;
    app::th::loadThemes(exeDir + L"\\assets\\themes");
    // own settings file next to the exe; the first time, the settings of the WZX editor (--settings) are taken over
    const std::wstring settingsFile = exeDir + L"\wzx_settings.ini";
    app::th::loadSettings(GetFileAttributesW(settingsFile.c_str()) == INVALID_FILE_ATTRIBUTES && !opt.settingsPath.empty() ? opt.settingsPath : settingsFile);
    app::th::apply(dpi);
    applyGraphTheme();
    if (!opt.embed) ShowWindow(hwnd, SW_SHOWDEFAULT);

    Graph graph;
    std::string loadErr;
    if (!graph.load(opt.graphPath, &loadErr) || graph.nodes.empty()) {
        std::wstring stem = opt.graphPath.substr(opt.graphPath.find_last_of(L"\\/") + 1);   // <name>.graph.json
        defaultGraph(graph, toUtf8(stem.substr(0, stem.find(L'.'))));
    }
    NodeEditor editor;
    editor.setFonts(g_gui.fontSmall, g_gui.fontBold);
    editor.setNodeIconDrawer(&drawNodeIconFn);
    editor.labels.title = "Shader Graph";
    editor.labels.subtitle = "Arnold";
    editor.labels.description = "Arnold material, mirrored in Maya";
    editor.labels.compile = "Sync Maya";
    editor.labels.compileIcon = 0xE895;   // sync
    editor.labels.run = "Assign";
    editor.labels.runIcon = 0xE8AB;       // apply to selection
    editor.labels.build = "Graph";
    editor.labels.buildIcon = 0xE8A1;
    editor.labels.vectorPin = "Color";
    editor.labels.objectPin = "Shader";
    editor.labels.cleanState = "\xE2\x97\x8F  Synced";
    editor.inspectorPx = 270;
    editor.outputPx = 110;
    editor.frameAll(graph);

    mayashader::MayaLink maya;
    maya.start(opt.port);
    mayashader::Preview preview;
    preview.init(g_renderer, g_gui, opt.hdri);
    preview.setLights(opt.previewLights != 0);
    preview.setAcescg(opt.acescg);

    HANDLE mayaProc = opt.mayaPid ? OpenProcess(SYNCHRONIZE, FALSE, opt.mayaPid) : nullptr;
    std::string lastJson;
    ULONGLONG lastSend = 0;
    bool pendingSync = true;
    bool wasConnected = false;
    int frameCount = 0;
    bool showSettings = opt.openSettings;
    bool wasParented = false;
    bool showPreview = opt.openPreview || opt.shotPreview;   // hidden and off by default

    // compounds opened by double click, from the root (the editor shows the last one)
    std::vector<std::pair<int, std::shared_ptr<Graph>>> path;
    auto current = [&]() -> Graph& { return path.empty() ? graph : *path.back().second; };
    auto pathIds = [&]() { std::vector<int> v; for (auto& p : path) v.push_back(p.first); return v; };
    auto openLevel = [&](size_t depth) { path.resize(depth); editor.resetHistory(); editor.frameAll(current()); };
    graph.refreshDefs();
    if (!opt.collapse.empty()) {   // test: compound made from these nodes, then opened
        std::vector<int> ids;
        for (size_t p = 0; p < opt.collapse.size();) { ids.push_back(_wtoi(opt.collapse.c_str() + p)); p = opt.collapse.find(L",", p); if (p == std::wstring::npos) break; ++p; }
        int c = graph.collapse(ids, "Patterns");
        if (opt.enterCompound < 0) opt.enterCompound = c;
    }
    editor.forceHoverLink = opt.hoverLink;
    if (opt.enterCompound)
        if (Node* n = graph.node(opt.enterCompound))
            if (n->sub) { path.push_back({n->id, n->sub}); editor.frameAll(*n->sub); }

    // flat graph (compounds expanded): what Maya builds and the preview renders
    const std::wstring flatPath = opt.graphPath.substr(0, opt.graphPath.rfind(L".graph.json") != std::wstring::npos
                                                           ? opt.graphPath.rfind(L".graph.json") : opt.graphPath.size()) + L".flat.json";
    Graph flat;

    // preview squares of the cards, rendered on the CPU when the graph changes
    std::map<int, ImTextureID> thumbs;
    std::string thumbKey;
    ULONGLONG thumbAt = 0;
    editor.nodeThumb = [&](const Node& n) -> ImTextureID {
        auto it = thumbs.find(n.id);
        return it == thumbs.end() ? (ImTextureID)0 : it->second;
    };
    // what flows through a link (tooltip): the source square and its value at the centre of the UVs
    editor.linkInfo = [&](const Graph& g, const Link& l) {
        const Node* a = g.node(l.fromNode);
        mayashader::FlatRef r = mayashader::resolveOut(graph, pathIds(), l.fromNode, l.fromPin);
        auto it = thumbs.find(l.fromNode);
        if (it != thumbs.end()) {
            ImGui::Image(it->second, ImVec2(64 * dpi, 64 * dpi));
            ImGui::SameLine();
        }
        ImGui::BeginGroup();
        vp3d::float3 v;
        bool isValue = r.valid() && (r.constant ? (v = r.value.asVec3(), true) : mayashader::sampleValue(flat, r.node, r.pin, v));
        if (!r.valid()) ImGui::TextDisabled("nothing flows (not connected inside)");
        else if (!isValue) {
            const Node* src = r.constant ? nullptr : flat.node(r.node);
            ImGui::Text("Shader: %s", src ? src->def->label.c_str() : "?");
        } else if (a && a->def->outs[l.fromPin].type == PinType::Float) {
            ImGui::Text("Value  %.3f", v.x);
        } else {
            vp3d::float3 d = v;
            if (preview.acescg())   // swatch in display colours (Maya ACEScg values)
                d = vp3d::float3(std::max(0.0f, 1.70505f * v.x - 0.62179f * v.y - 0.08326f * v.z),
                                 std::max(0.0f, -0.13026f * v.x + 1.14080f * v.y - 0.01055f * v.z),
                                 std::max(0.0f, -0.02400f * v.x - 0.12897f * v.y + 1.15297f * v.z));
            auto enc = [](float x) { x = std::min(std::max(x, 0.0f), 1.0f); return x <= 0.0031308f ? x * 12.92f : 1.055f * std::pow(x, 1 / 2.4f) - 0.055f; };
            ImGui::ColorButton("##flow", ImVec4(enc(d.x), enc(d.y), enc(d.z), 1), ImGuiColorEditFlags_NoTooltip, ImVec2(18 * dpi, 18 * dpi));
            ImGui::SameLine();
            ImGui::Text("R %.3f   G %.3f   B %.3f", v.x, v.y, v.z);
        }
        if (r.valid()) ImGui::TextDisabled(r.constant ? "compound input value" : "sampled at UV (0.5, 0.5)");
        ImGui::EndGroup();
    };

    // workspaces: 0 graph, 1 shader library, 2 equivalences dashboard
    int mode = opt.mode;
    auto send = [&](const std::string& line) { return maya.send(line); };
    // equivalences: tools/maya/mappings/equivalences.json of the repository (found from the exe folder)
    std::wstring mappingPath = opt.mappings;
    if (mappingPath.empty()) {
        std::wstring d = exeDir;
        for (int up = 0; up < 6 && mappingPath.empty(); ++up) {
            std::wstring c = d + L"\\tools\\maya\\mappings\\equivalences.json";
            if (GetFileAttributesW(c.c_str()) != INVALID_FILE_ATTRIBUTES) mappingPath = c;
            size_t cut = d.find_last_of(L"\\/");
            if (cut == std::wstring::npos) break;
            d = d.substr(0, cut);
        }
        if (mappingPath.empty()) mappingPath = exeDir + L"\\equivalences.json";
    }
    mayashader::Equivalences equivalences;
    equivalences.load(mappingPath);
    // shader library: folder on the PC and materials of the scene
    const std::wstring graphStem = opt.graphPath.substr(opt.graphPath.find_last_of(L"\\/") + 1);
    const std::string graphKey = toUtf8(graphStem.substr(0, graphStem.find(L'.')));
    mayashader::Library library;
    mayashader::Library::Host host;
    host.send = send;
    host.mayaConnected = [&]() { return maya.connected(); };
    host.graph = [&]() -> const Graph& { return graph; };
    host.openInGraph = [&](const std::string& json) {   // a library material replaces the current one (its name stays)
        std::string keep = graph.name;
        std::string err;
        if (!graph.fromJson(json, &err)) { editor.addMessage(2, "Cannot open the material: " + err); return; }
        graph.name = keep;
        graph.refreshDefs();
        path.clear();
        editor.resetHistory();
        editor.frameAll(graph);
        mode = 0;
    };
    host.currentKey = [&]() { return graphKey; };
    host.acescg = preview.acescg();
    library.init(g_gui, exeDir + L"\\wzx_library.txt", host);
    if (!opt.library.empty()) library.setRoot(opt.library);
    if (opt.libTab >= 0) library.setTab(opt.libTab);
    if (!opt.saveLib.empty()) {   // test: "Category/Name"
        std::string cs = toUtf8(opt.saveLib);
        size_t sl = cs.find('/');
        library.saveMaterial(sl == std::string::npos ? cs : cs.substr(sl + 1), sl == std::string::npos ? "Default" : cs.substr(0, sl), "test");
    }

    MSG msg;
    while (g_running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) g_running = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!g_running) break;
        if (mayaProc && WaitForSingleObject(mayaProc, 0) == WAIT_OBJECT_0) break;   // Maya was closed
        if (opt.embed) {   // the Maya panel was closed: the window lost its parent
            HWND parent = GetParent(hwnd);
            if (parent) wasParented = true;
            else if (wasParented) break;
        }
        if (g_minimized || !IsWindowVisible(hwnd)) { Sleep(50); maya.poll(); continue; }

        g_gui.newFrame();
        ImGui::GetStyle().FontScaleMain = app::th::textSize();
        const float s = dpi * app::th::layoutSize();
        ImGuiViewport* vp = ImGui::GetMainViewport();
        const float sideW = 190 * s;
        const ImVec2 full = vp->WorkSize;

        // node library (left)
        ImGui::SetNextWindowPos(vp->WorkPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(sideW, full.y), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(1.0f);
        const ImGuiWindowFlags fixed = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
        if (ImGui::Begin("##library", nullptr, fixed)) {
            ImGui::AlignTextToFramePadding();
            // workspaces
            {
                const char* names[] = {"Graph", "Library", "Equiv."};
                float bw = (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3;
                for (int m = 0; m < 3; ++m) {
                    if (m) ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Button, mode == m ? app::th::selectV() : ImGui::GetStyleColorVec4(ImGuiCol_Button));
                    if (ImGui::Button(names[m], ImVec2(bw, 0))) mode = m;
                    ImGui::PopStyleColor();
                }
                ImGui::Separator();
            }
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Material");
            ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 30 * s);
            if (ImGui::Button(ICON_SETTINGS "##settings", ImVec2(28 * s, 0))) showSettings = !showSettings;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Settings: theme, colours, sizes, graph");
            // material preview: hidden and off until asked
            ImGui::PushStyleColor(ImGuiCol_Button, showPreview ? app::th::selectV() : ImGui::GetStyleColorVec4(ImGuiCol_Button));
            if (ImGui::Button(showPreview ? "\xEE\x9E\x90  Material Preview: on" : "\xEE\x9E\x90  Material Preview: off", ImVec2(-FLT_MIN, 0)))   // view glyph
                showPreview = !showPreview;
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Path traced sphere above the Inspector (uses the GPU while it renders)");
            // breadcrumb of the opened compounds
            if (!path.empty()) {
                ImGui::TextDisabled("COMPOUND");
                if (ImGui::SmallButton(graph.name.c_str())) openLevel(0);
                for (size_t i = 0; i < path.size(); ++i) {
                    ImGui::SameLine(0, 2);
                    ImGui::TextDisabled(">");
                    ImGui::SameLine(0, 2);
                    ImGui::PushID((int)i);
                    std::string nm = path[i].second->io ? path[i].second->io->name : std::string("Compound");
                    if (ImGui::SmallButton(nm.c_str()) && i + 1 < path.size()) openLevel(i + 1);
                    ImGui::PopID();
                }
                if (ImGui::Button("\xEE\x9C\xAA  Up one level", ImVec2(-FLT_MIN, 0))) openLevel(path.size() - 1);   // back glyph
            }
            char name[128] = {};
            strncpy_s(name, graph.name.c_str(), _TRUNCATE);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputText("##graphname", name, sizeof name, ImGuiInputTextFlags_EnterReturnsTrue) && name[0]) graph.name = name;
            ImGui::Spacing();
            ImGui::TextColored(maya.connected() ? ImVec4(0.45f, 0.8f, 0.5f, 1) : ImVec4(0.85f, 0.6f, 0.3f, 1), "%s",
                               maya.connected() ? "\xE2\x97\x8F Maya connected" : "\xE2\x97\x8F Maya not connected");
            if (!maya.lastReply().empty()) ImGui::TextDisabled("%s", maya.lastReply().c_str());
            ImGui::Separator();
            ImGui::TextDisabled("NODE LIBRARY");
            static char search[96] = {};
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##libsearch", "Search nodes\xE2\x80\xA6", search, sizeof search);
            std::string q = search;
            for (char& c : q) c = (char)tolower((unsigned char)c);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            std::string last;
            for (const NodeDef& d : nodeDefs()) {
                if (d.id == "compound.input" || d.id == "compound.output") continue;   // created with each compound
                std::string label = d.label;
                for (char& c : label) c = (char)tolower((unsigned char)c);
                if (!q.empty() && label.find(q) == std::string::npos) continue;
                if (d.category != last) {
                    ImGui::Spacing();
                    ImGui::TextColored(ImColor(categoryAccentColor(d.category)), "%s", d.category.c_str());
                    last = d.category;
                }
                ImGui::PushID(d.id.c_str());
                ImVec2 rmin = ImGui::GetCursorScreenPos();
                bool clicked = ImGui::Selectable("##row", false, 0, ImVec2(0, 22 * s));
                ImVec2 rmax = ImGui::GetItemRectMax();
                float cy = (rmin.y + rmax.y) * 0.5f;
                atl::DrawIcon(dl, nodeIcon(d), ImVec2(rmin.x + 6 * s, cy - 7 * s), 14 * s, categoryAccentColor(d.category));
                dl->AddText(ImGui::GetFont(), 13 * s, ImVec2(rmin.x + 26 * s, cy - 7 * s), app::th::text(), d.label.c_str());
                atl::DrawIcon(dl, atl::Icon::Plus, ImVec2(rmax.x - 18 * s, cy - 6 * s), 12 * s, app::th::dim());
                if (clicked) editor.addNodeAtView(current(), d);
                if (ImGui::IsItemHovered() && !d.help.empty()) ImGui::SetTooltip("%s", d.help.c_str());
                ImGui::PopID();
            }
            ImGui::Spacing();
            ImGui::TextColored(ImColor(app::th::dim()), "Layout");
            if (ImGui::Button("+ Backdrop  (Ctrl+G)", ImVec2(-FLT_MIN, 0))) editor.addBackdrop(current());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Frame around the selected nodes (or in the middle of the view)");
            if (ImGui::Button("+ Compound  (Ctrl+Shift+G)", ImVec2(-FLT_MIN, 0))) editor.collapseSelection(current());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Moves the selected nodes into a compound (LookDevX): double click to enter it,\nconnect inner pins to its Input / Output + pins to promote them");
        }
        ImGui::End();

        // the opened compounds must still exist (a deleted compound or an undo at an upper level closes them)
        {
            const Graph* g = &graph;
            size_t ok = 0;
            for (; ok < path.size(); ++ok) {
                const Node* n = g->node(path[ok].first);
                if (!n || n->sub != path[ok].second) break;
                g = n->sub.get();
            }
            if (ok < path.size()) openLevel(ok);
        }
        graph.refreshDefs();
        flat = mayashader::flatten(graph);

        // preview squares: rendered again when the graph (or the opened level) changed, at most every 100 ms
        {
            std::string key = flat.toJson() + "|" + std::to_string(path.size()) + "|" + std::to_string(path.empty() ? 0 : path.back().first) +
                              "|" + std::to_string(preview.acescg());
            ULONGLONG t = GetTickCount64();
            if (key != thumbKey && t - thumbAt > 100) {
                thumbKey = key;
                thumbAt = t;
                for (auto& kv : thumbs) g_gui.releaseTexture(kv.second);
                thumbs.clear();
                const int S = (int)(72 * dpi);
                std::vector<uint8_t> px;
                std::vector<int> ids = pathIds();
                for (const Node& n : current().nodes) {
                    if (!n.def || n.type == "compound.input" || n.type == "compound.output") continue;
                    mayashader::FlatRef r;
                    if (n.type == "out.material") r = mayashader::resolveIn(graph, ids, n.id, n.def->findIn("Surface"));
                    else if (!n.def->outs.empty()) r = mayashader::resolveOut(graph, ids, n.id, 0);
                    if (!r.valid()) continue;
                    if (mayashader::renderThumb(flat, r.node, r.pin, r.constant ? &r.value : nullptr, preview.acescg(), S, px))
                        if (ImTextureID tex = g_gui.createTexture(px.data(), S, S)) thumbs[n.id] = tex;
                }
            }
        }

        // graph editor (canvas, output, inspector) with the preview above the inspector
        const float previewH = showPreview ? std::min(full.y * 0.5f, editor.inspectorPx * s + 64 * s) : 0.0f;
        editor.inspectorTop = previewH;
        editor.labels.subtitle = path.empty() ? "Arnold" : "Compound: " + (path.back().second->io ? path.back().second->io->name : std::string());
        editor.setOutline(app::th::settings().graphOutline, app::th::settings().graphOutlineWeight,
                          ImGui::ColorConvertFloat4ToU32(app::th::settings().graphSelectColor));
        editor.status = maya.connected() ? "Maya :" + std::to_string(opt.port) : "waiting for Maya :" + std::to_string(opt.port);
        ImVec2 edPos(vp->WorkPos.x + sideW, vp->WorkPos.y), edSize(std::max(1.0f, full.x - sideW), full.y);
        bool rendered = false;
        if (mode == 1) library.draw(edPos, edSize, s);
        else if (mode == 2) {
            static bool checked = false;
            if (opt.autoCheck && !checked && maya.connected()) checked = equivalences.requestCheck(send);
            equivalences.draw(edPos, edSize, s, send, maya.connected());
        }
        else {
            editor.draw(edPos, edSize, s, current(), nullptr, false);
            // preview (top of the right column): the flat graph, so a compound shows what it really makes
            ImVec2 pvPos(edPos.x + edSize.x - editor.inspectorPx * s, edPos.y);
            rendered = showPreview && preview.draw(flat, pvPos, ImVec2(editor.inspectorPx * s, previewH), s);
        }

        // WZX settings window (theme, colours, sizes, graph outlines), saved next to the exe
        if (showSettings && mayashader::drawSettingsWindow(&showSettings, dpi)) {
            app::th::apply(dpi);
            applyGraphTheme();
            app::th::saveSettings(settingsFile);
        }

        switch (editor.takeAction()) {
        case NodeEditor::Compile: pendingSync = true; lastJson.clear(); break;
        case NodeEditor::TogglePlay: maya.send("import wzx_shader_bridge as _w; _w.assign(" + mayashader::pyStr(toUtf8(opt.graphPath)) + ")"); break;
        case NodeEditor::Build: maya.send("import wzx_shader_bridge as _w; _w.show_in_hypershade(" + mayashader::pyStr(toUtf8(opt.graphPath)) + ")"); break;
        case NodeEditor::Save: pendingSync = true; lastJson.clear(); break;
        case NodeEditor::Arrange: editor.arrangeOverlaps(current()); break;
        case NodeEditor::EnterCompound:
            if (Node* n = current().node(editor.enteredNode()))
                if (n->sub) { path.push_back({n->id, n->sub}); editor.resetHistory(); editor.frameAll(*n->sub); }
            break;
        default: break;
        }

        // Maya sync: the graph file is rewritten and Maya rebuilds its nodes (at most every 60 ms while editing)
        maya.poll();
        if (maya.connected() && !wasConnected) { pendingSync = true; lastJson.clear(); }
        wasConnected = maya.connected();
        graph.refreshDefs();
        std::string json = graph.toJson();
        if (json != lastJson) pendingSync = true;
        ULONGLONG now = GetTickCount64();
        if (pendingSync && now - lastSend > 60) {
            // the editor graph (with its compounds) is kept in the scene; Maya builds the flat one
            flat = mayashader::flatten(graph);
            if (graph.save(opt.graphPath) && flat.save(flatPath)) {
                lastJson = json;
                pendingSync = false;
                lastSend = now;
                if (maya.connected()) {
                    maya.send("import wzx_shader_bridge as _w; _w.sync_file(" + mayashader::pyStr(toUtf8(flatPath)) + ", " +
                              mayashader::pyStr(toUtf8(opt.graphPath)) + ")");
                    editor.clearModified();
                }
            }
        }

        g_gui.render(rendered);
        // test capture (command line): window image after N frames, then quit
        if (!opt.shotPath.empty() && ++frameCount >= opt.shotFrames) {
            std::vector<uint8_t> rgba;
            uint32_t w, h;
            if (opt.shotPreview ? g_renderer.readViewport(rgba, w, h) : g_renderer.readBackBuffer(rgba, w, h)) savePng(opt.shotPath, rgba, w, h);
            g_running = false;
        }
        g_renderer.present(true);
        // idle: the preview has converged and nothing moves, so do not keep the GPU busy inside Maya
        if ((!showPreview || !preview.busy()) && !ImGui::IsAnyItemActive() && !ImGui::GetIO().WantTextInput)
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 30, QS_ALLINPUT);
    }
    g_renderer.waitIdle();
    graph.save(opt.graphPath);
    maya.stop();
    preview.shutdown();
    g_gui.shutdown();
    g_renderer.shutdown();
    if (mayaProc) CloseHandle(mayaProc);
    return 0;
}
