// Halo editor: standalone Dear ImGui reproduction of design/halo/mockup-halo.html.
// Usage: HaloEditor.exe [--size 1600x900] [--shot capture.png]   (--shot saves the 10th frame and quits)
#include "Gfx.h"
#include "HaloUI.h"
#include "backends/imgui_impl_win32.h"
#include <shellapi.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static Gfx g_gfx;
static bool g_ready = false;

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (ImGui_ImplWin32_WndProcHandler(h, m, w, l)) return 1;
    switch (m) {
    case WM_SIZE:
        if (g_ready && w != SIZE_MINIMIZED) g_gfx.resize(LOWORD(l), HIWORD(l));
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// em-based sizing: ImGui sizes a font by ascent - descent; CSS sizes it by the em square. ExtraSizeScale makes
// "13 px" in ImGui the same glyph size as font-size: 13px in the mockup.
static float emScale(const wchar_t* path) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"rb") || !f) return 1.0f;
    std::vector<uint8_t> d;
    fseek(f, 0, SEEK_END);
    d.resize((size_t)ftell(f));
    fseek(f, 0, SEEK_SET);
    fread(d.data(), 1, d.size(), f);
    fclose(f);
    auto u16 = [&](size_t o) { return (uint16_t)((d[o] << 8) | d[o + 1]); };
    auto u32 = [&](size_t o) { return (uint32_t)((d[o] << 24) | (d[o + 1] << 16) | (d[o + 2] << 8) | d[o + 3]); };
    if (d.size() < 12) return 1.0f;
    size_t base = 0;
    if (u32(0) == 0x74746366) base = u32(12);   // 'ttcf': first font of the collection
    int n = u16(base + 4), upem = 0, asc = 0, desc = 0;
    for (int i = 0; i < n; ++i) {
        size_t r = base + 12 + i * 16;
        uint32_t tag = u32(r), off = u32(r + 8);
        if (tag == 0x68656164) upem = u16(off + 18);                                                  // 'head'
        if (tag == 0x68686561) { asc = (int16_t)u16(off + 4); desc = (int16_t)u16(off + 6); }         // 'hhea'
    }
    if (!upem || asc - desc <= 0) return 1.0f;
    return (float)(asc - desc) / (float)upem;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    int cw = 1600, ch = 900;
    std::wstring shot;
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--size" && i + 1 < argc) swscanf_s(argv[++i], L"%dx%d", &cw, &ch);
        else if (a == L"--shot" && i + 1 < argc) shot = argv[++i];
    }
    // assets next to the exe
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe;
    dir = dir.substr(0, dir.find_last_of(L"\\/"));

    WNDCLASSEXW wc = {sizeof(wc), CS_CLASSDC, WndProc, 0, 0, inst, nullptr, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr, L"HaloEditor", nullptr};
    RegisterClassExW(&wc);
    // client size in CSS pixels times the monitor scale
    const float dpi = GetDpiForSystem() / 96.0f;
    RECT rc = {0, 0, (LONG)(cw * dpi), (LONG)(ch * dpi)};
    AdjustWindowRectExForDpi(&rc, WS_OVERLAPPEDWINDOW, FALSE, 0, GetDpiForSystem());
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"WZX Engine — Halo", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, inst, nullptr);
    // dark title bar
    BOOL dark = TRUE;
    using DwmSet = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    if (HMODULE dwm = LoadLibraryW(L"dwmapi.dll"))
        if (auto set = (DwmSet)GetProcAddress(dwm, "DwmSetWindowAttribute")) set(hwnd, 20, &dark, sizeof(dark));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.PopupRounding = 2;
    st.PopupBorderSize = 1;
    st.Colors[ImGuiCol_PopupBg] = ImVec4(0.094f, 0.114f, 0.133f, 0.99f);
    st.Colors[ImGuiCol_Border] = ImVec4(0.141f, 0.169f, 0.196f, 1);
    st.Colors[ImGuiCol_Header] = ImVec4(0.424f, 0.769f, 0.863f, 0.14f);
    st.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.122f, 0.145f, 0.169f, 1);
    st.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.424f, 0.769f, 0.863f, 0.3f);
    st.Colors[ImGuiCol_InputTextCursor] = ImVec4(0.424f, 0.769f, 0.863f, 1);

    // fonts: 0 Segoe UI (Inter), 1 Bahnschrift (Barlow Condensed), 2 Consolas (JetBrains Mono), 3 Segoe UI Semibold, 4 Consolas Bold
    auto add = [&](const wchar_t* w, const char* a) -> ImFont* {
        ImFontConfig cfg;
        cfg.ExtraSizeScale = emScale(w);
        static const ImWchar ranges[] = {0x0020, 0x00FF, 0x2010, 0x2027, 0};   // Latin-1, dashes, middle dot, ellipsis
        cfg.GlyphRanges = ranges;
        ImFont* f = io.Fonts->AddFontFromFileTTF(a, 13.0f * dpi, &cfg);
        return f ? f : io.Fonts->AddFontDefault();
    };
    halo::Fonts fonts;
    fonts.ui = add(L"C:\\Windows\\Fonts\\segoeui.ttf", "C:\\Windows\\Fonts\\segoeui.ttf");
    fonts.hud = add(L"C:\\Windows\\Fonts\\bahnschrift.ttf", "C:\\Windows\\Fonts\\bahnschrift.ttf");
    fonts.mono = add(L"C:\\Windows\\Fonts\\consola.ttf", "C:\\Windows\\Fonts\\consola.ttf");
    add(L"C:\\Windows\\Fonts\\seguisb.ttf", "C:\\Windows\\Fonts\\seguisb.ttf");
    add(L"C:\\Windows\\Fonts\\consolab.ttf", "C:\\Windows\\Fonts\\consolab.ttf");
    io.FontDefault = fonts.ui;

    if (!g_gfx.init(hwnd)) {
        MessageBoxW(hwnd, L"DirectX 12 initialisation failed", L"Halo", MB_ICONERROR);
        return 1;
    }
    g_ready = true;
    int aw = 0, ah = 0;
    ImTextureID atlas = g_gfx.loadPng(dir + L"\\assets\\icons.png", aw, ah);
    halo::Init(atlas, aw, ah, fonts);
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    int frame = 0;
    bool run = true;
    while (run) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) run = false;
        }
        if (!run) break;
        g_gfx.beginFrame();
        halo::Draw(dpi);
        const bool capture = !shot.empty() && ++frame == 10;
        g_gfx.endFrame(capture ? shot : L"");
        if (capture) break;
    }
    g_gfx.shutdown();
    ImGui::DestroyContext();
    DestroyWindow(hwnd);
    return 0;
}
