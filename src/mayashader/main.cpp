// WzxShaderGraph - the WZX node graph editor as a Maya shading tool (throwaway prototype).
// The window is embedded in a dockable Maya panel (tools/maya/scripts/wzx_shader_panel.py): Maya starts the exe
// with --embed, reads the window handle printed on stdout and wraps it in a Qt container. Every edit of the graph
// is saved to the graph file and Maya is asked, through its commandPort, to rebuild the matching Arnold / Maya
// nodes (wzx_shader_bridge.sync_file). The right column shows a path traced preview of the material (vp3d).
#include "Preview.h"
#include "MayaLink.h"
#include "app/ImGuiLayer.h"
#include "app/Theme.h"
#include "app/AtelierIcons.h"
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
    app::th::loadSettings(opt.settingsPath.empty() ? exeDir + L"\\wzx_settings.ini" : opt.settingsPath);
    app::th::apply(dpi);
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
    bool wasParented = false;

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
        const float s = dpi;
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
            ImGui::TextUnformatted("Material");
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
                if (clicked) editor.addNodeAtView(graph, d);
                if (ImGui::IsItemHovered() && !d.help.empty()) ImGui::SetTooltip("%s", d.help.c_str());
                ImGui::PopID();
            }
        }
        ImGui::End();

        // graph editor (canvas, output, inspector) with the preview above the inspector
        const float previewH = std::min(full.y * 0.5f, editor.inspectorPx * s + 64 * s);
        editor.inspectorTop = previewH;
        editor.setOutline(app::th::settings().graphOutline, app::th::settings().graphOutlineWeight,
                          ImGui::ColorConvertFloat4ToU32(app::th::settings().graphSelectColor));
        editor.status = maya.connected() ? "Maya :" + std::to_string(opt.port) : "waiting for Maya :" + std::to_string(opt.port);
        ImVec2 edPos(vp->WorkPos.x + sideW, vp->WorkPos.y), edSize(std::max(1.0f, full.x - sideW), full.y);
        editor.draw(edPos, edSize, s, graph, nullptr, false);

        // preview (top of the right column)
        ImVec2 pvPos(edPos.x + edSize.x - editor.inspectorPx * s, edPos.y);
        bool rendered = preview.draw(graph, pvPos, ImVec2(editor.inspectorPx * s, previewH), s);

        switch (editor.takeAction()) {
        case NodeEditor::Compile: pendingSync = true; lastJson.clear(); break;
        case NodeEditor::TogglePlay: maya.send("import wzx_shader_bridge as _w; _w.assign(" + mayashader::pyStr(toUtf8(opt.graphPath)) + ")"); break;
        case NodeEditor::Build: maya.send("import wzx_shader_bridge as _w; _w.show_in_hypershade(" + mayashader::pyStr(toUtf8(opt.graphPath)) + ")"); break;
        case NodeEditor::Save: pendingSync = true; lastJson.clear(); break;
        case NodeEditor::Arrange: editor.arrangeOverlaps(graph); break;
        default: break;
        }

        // Maya sync: the graph file is rewritten and Maya rebuilds its nodes (at most every 60 ms while editing)
        maya.poll();
        if (maya.connected() && !wasConnected) { pendingSync = true; lastJson.clear(); }
        wasConnected = maya.connected();
        std::string json = graph.toJson();
        if (json != lastJson) pendingSync = true;
        ULONGLONG now = GetTickCount64();
        if (pendingSync && now - lastSend > 60) {
            if (graph.save(opt.graphPath)) {
                lastJson = json;
                pendingSync = false;
                lastSend = now;
                if (maya.connected()) {
                    maya.send("import wzx_shader_bridge as _w; _w.sync_file(" + mayashader::pyStr(toUtf8(opt.graphPath)) + ")");
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
        if (!preview.busy() && !ImGui::IsAnyItemActive() && !ImGui::GetIO().WantTextInput)
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
