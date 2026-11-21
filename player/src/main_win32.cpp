// WZX Player for Windows: a window, a DirectX 12 device through Diligent, the game, its HUD and the Lit renderer.
// Game (studio scene): ZQSD / WASD or arrows, screen edges, wheel, right drag, Q/E or middle drag to turn, left click
// and drag for the game and its houses, the keys of the graph. Esc = quit.
// LookDev scene: left or right drag = orbit, wheel = zoom, F = default view.
// Options: --scene studio|lookdev  --hdri sky|none|file.hdr  --backend d3d12|vk  --data folder (game/ and assets/)
// WZX Live: --live (wait for the editor: it sends the game it edits), --live-dir folder (default
//   %LOCALAPPDATA%/WZX Engine/live-player/game), --live-port 47800, --live-lan (listen on the network, Bonjour; else this PC only)
#include "LiveHost.h"
#include "app/Json.h"
#include "Player.h"
#include "imgui.h"

#include "Graphics/GraphicsEngineD3D12/interface/EngineFactoryD3D12.h"
#if VULKAN_SUPPORTED
#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#endif
#include "Graphics/GraphicsEngine/interface/SwapChain.h"

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <objbase.h>

#include <algorithm>
#include <cfloat>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

using namespace Diligent;

namespace {

std::unique_ptr<player::Player> g_player;   // recreated when WZX Live brings a new game
player::Options g_opt;
IEngineFactory* g_factory = nullptr;
std::string g_shaders;
player::LiveHost g_live;
bool g_liveOn = false;
std::string g_liveDir;
RefCntAutoPtr<IRenderDevice> g_device;
RefCntAutoPtr<IDeviceContext> g_context;
RefCntAutoPtr<ISwapChain> g_swapChain;
bool g_ready = false;
bool g_dragging = false;   // LookDev orbit
POINT g_last = {};
LARGE_INTEGER g_freq = {}, g_prev = {};

std::string utf8(const wchar_t* w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

// Folder of the executable: the shaders, game/ and assets/ are copied next to it.
std::string exeDir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring p = path;
    return utf8(p.substr(0, p.find_last_of(L"\\/")).c_str());
}

ImGuiKey imguiKey(WPARAM vk) {
    if (vk >= 'A' && vk <= 'Z') return (ImGuiKey)(ImGuiKey_A + (vk - 'A'));
    if (vk >= '0' && vk <= '9') return (ImGuiKey)(ImGuiKey_0 + (vk - '0'));
    if (vk >= VK_F1 && vk <= VK_F12) return (ImGuiKey)(ImGuiKey_F1 + (vk - VK_F1));
    switch (vk) {
    case VK_SPACE: return ImGuiKey_Space;
    case VK_RETURN: return ImGuiKey_Enter;
    case VK_ESCAPE: return ImGuiKey_Escape;
    case VK_DELETE: return ImGuiKey_Delete;
    case VK_BACK: return ImGuiKey_Backspace;
    case VK_TAB: return ImGuiKey_Tab;
    case VK_UP: return ImGuiKey_UpArrow;
    case VK_DOWN: return ImGuiKey_DownArrow;
    case VK_LEFT: return ImGuiKey_LeftArrow;
    case VK_RIGHT: return ImGuiKey_RightArrow;
    case VK_SHIFT: return ImGuiKey_LeftShift;
    case VK_CONTROL: return ImGuiKey_LeftCtrl;
    case VK_MENU: return ImGuiKey_LeftAlt;
    default: return ImGuiKey_None;
    }
}

// Move keys by their place on the keyboard (QWERTY scan codes): ZQSD on AZERTY and WASD on QWERTY both work.
float physKey(UINT scan) {
    UINT vk = MapVirtualKeyW(scan, MAPVK_VSC_TO_VK);
    return vk && (GetAsyncKeyState(vk) & 0x8000) ? 1.0f : 0.0f;
}

void render(HWND hwnd) {
    if (!g_swapChain || !g_ready) return;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    float dt = g_prev.QuadPart ? (float)(now.QuadPart - g_prev.QuadPart) / (float)g_freq.QuadPart : 1.0f / 60.0f;
    g_prev = now;
    player::CameraControls cam;
    if (GetForegroundWindow() == hwnd && !ImGui::GetIO().WantTextInput) {
        cam.moveZ = physKey(0x11) - physKey(0x1F);   // W / S
        cam.moveX = physKey(0x20) - physKey(0x1E);   // D / A
        cam.turn = physKey(0x12) - physKey(0x10);    // E / Q
    }
    const SwapChainDesc& d = g_swapChain->GetDesc();
    g_player->frame(g_swapChain->GetCurrentBackBufferRTV(), d.Width, d.Height, std::min(dt, 0.1f), cam);
    if (g_liveOn && g_live.wantsShot()) g_live.captureShot(g_device, g_context, g_swapChain->GetCurrentBackBufferRTV()->GetTexture());
    g_swapChain->Present(1);
}

// (Re)starts the player on a scene: "game" of a data folder, or "empty" (the WZX Live waiting screen). The GPU is
// drained first: the old game's resources go away with its Player.
bool startGame(const std::string& scene, const std::string& dataDir, std::string* err) {
    g_ready = false;
    if (g_player) {
        g_context->Flush();
        g_context->WaitForIdle();
        g_player->shutdown();
        g_player.reset();
    }
    player::Options o = g_opt;
    o.scene = scene;
    o.dataDir = dataDir;
    if (scene == "empty") o.game = false;
    auto p = std::make_unique<player::Player>();
    if (g_liveOn) p->overlay = [] { g_live.drawOverlay(g_opt.uiScale, 0.0f, 0.0f); };
    const bool ok = p->init(g_device, g_context, g_factory, g_swapChain->GetDesc().ColorBufferFormat, g_shaders, o, err);
    if (!ok) {
        p->shutdown();
        return false;
    }
    g_player = std::move(p);
    g_live.setPlaying(scene != "empty");
    g_prev.QuadPart = 0;
    g_ready = true;
    return true;
}

std::string gameTitle(const std::string& dir) {
    std::ifstream f(std::filesystem::u8path(dir) / "game.json", std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    app::Json j;
    app::Json::parse(ss.str(), j);
    return j["title"].str("WZX Game");
}

// WZX Live, once per frame after Present: the log of the graph, a game received, the buttons of the waiting screen.
void liveTick() {
    g_live.pumpGameLog(g_player ? g_player->game() : nullptr);
    std::string err;
    if (g_live.takeReload()) {
        const bool ok = startGame("game", g_liveDir, &err);
        g_live.reloaded(ok, gameTitle(g_liveDir), ok ? g_player->warnings() : std::vector<std::string>(), err);
        if (!ok) startGame("empty", g_liveDir, nullptr);
    }
    switch (g_live.takeAction()) {
    case player::LiveHost::PlayLast:
        if (!startGame("game", g_liveDir, &err)) { g_live.reloaded(false, "", {}, err); startGame("empty", g_liveDir, nullptr); }
        break;
    case player::LiveHost::PlayBundled:
        if (!startGame("game", g_opt.dataDir, &err)) { g_live.reloaded(false, "", {}, err); startGame("empty", g_liveDir, nullptr); }
        break;
    case player::LiveHost::ShowWaiting: startGame("empty", g_liveDir, nullptr); break;
    default: break;
    }
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    const bool gui = g_ready;
    switch (msg) {
    case WM_SIZE:
        if (g_swapChain && LOWORD(lp) > 0 && HIWORD(lp) > 0) g_swapChain->Resize(LOWORD(lp), HIWORD(lp));
        return 0;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN: {
        int b = msg == WM_LBUTTONDOWN ? 0 : (msg == WM_RBUTTONDOWN ? 1 : 2);
        if (gui) ImGui::GetIO().AddMouseButtonEvent(b, true);
        g_dragging = true;
        g_last = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        SetCapture(hwnd);
        return 0;
    }
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP: {
        int b = msg == WM_LBUTTONUP ? 0 : (msg == WM_RBUTTONUP ? 1 : 2);
        if (gui) ImGui::GetIO().AddMouseButtonEvent(b, false);
        g_dragging = false;
        ReleaseCapture();
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (gui) ImGui::GetIO().AddMousePosEvent((float)p.x, (float)p.y);
        if (g_dragging && g_player && !g_player->playing()) g_player->orbit((float)(p.x - g_last.x), (float)(p.y - g_last.y));
        g_last = p;
        return 0;
    }
    case WM_MOUSEWHEEL:
        if (gui) ImGui::GetIO().AddMouseWheelEvent(0, (float)GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA);
        if (g_player && !g_player->playing()) g_player->zoom(GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 0.9f : 1.0f / 0.9f);
        return 0;
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP: {
        const bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
        if (gui) {
            ImGuiIO& io = ImGui::GetIO();
            io.AddKeyEvent(ImGuiMod_Shift, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
            io.AddKeyEvent(ImGuiMod_Ctrl, (GetKeyState(VK_CONTROL) & 0x8000) != 0);
            io.AddKeyEvent(ImGuiMod_Alt, (GetKeyState(VK_MENU) & 0x8000) != 0);
            ImGuiKey k = imguiKey(wp);
            if (k != ImGuiKey_None) io.AddKeyEvent(k, down);
        }
        if (down && wp == VK_ESCAPE) DestroyWindow(hwnd);
        if (down && wp == 'F' && g_player && !g_player->playing()) g_player->resetView();
        if (msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP) break;   // Alt+F4 and the system keys
        return 0;
    }
    case WM_CHAR:
        if (gui && wp > 0 && wp < 0x10000) ImGui::GetIO().AddInputCharacterUTF16((unsigned short)wp);
        return 0;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        if (gui) ImGui::GetIO().AddFocusEvent(msg == WM_SETFOCUS);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);   // WIC decodes the textures (vp3d/AssetImport.cpp)
    player::Options opt;
    opt.dataDir = exeDir();
    std::string backend = "d3d12";
    int livePort = live::kDefaultPort;
    bool liveLan = false;
    std::string liveOld;
    int liveProtocol = 0;
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--hdri" && i + 1 < argc) opt.hdri = utf8(argv[++i]);
        else if (a == L"--backend" && i + 1 < argc) backend = utf8(argv[++i]);
        else if (a == L"--scene" && i + 1 < argc) opt.scene = utf8(argv[++i]);
        else if (a == L"--data" && i + 1 < argc) opt.dataDir = utf8(argv[++i]);
        else if (a == L"--ddgi" && i + 1 < argc) opt.ddgi = _wtoi(argv[++i]) != 0;   // DDGI probes without ray tracing
        else if (a == L"--ddgi-view" && i + 1 < argc) opt.ddgiView = _wtoi(argv[++i]);
        else if (a == L"--probes" && i + 1 < argc) opt.ddgiProbes = _wtoi(argv[++i]);
        else if (a == L"--show-probes" && i + 1 < argc) opt.showProbes = _wtoi(argv[++i]) != 0;
        else if (a == L"--baked" && i + 1 < argc) opt.bakedProbes = _wtoi(argv[++i]) != 0;
        else if (a == L"--live") g_liveOn = true;   // WZX Live: the editor sends the game it edits
        else if (a == L"--live-dir" && i + 1 < argc) g_liveDir = utf8(argv[++i]);
        else if (a == L"--live-port" && i + 1 < argc) livePort = _wtoi(argv[++i]);
        else if (a == L"--live-lan") liveLan = true;   // listen on the network too (Windows firewall prompt), Bonjour
        else if (a == L"--live-simulate-old" && i + 1 < argc) liveOld = utf8(argv[++i]);   // tests: "unit.produce,prefab-clip"
        else if (a == L"--live-simulate-protocol" && i + 1 < argc) liveProtocol = _wtoi(argv[++i]);
    }
    LocalFree(argv);
    if (g_liveOn && g_liveDir.empty()) {   // %LOCALAPPDATA%\WZX Engine\live-player\game
        wchar_t* la = nullptr;
        size_t n = 0;
        _wdupenv_s(&la, &n, L"LOCALAPPDATA");
        g_liveDir = ((la ? std::filesystem::path(la) : std::filesystem::temp_directory_path()) / L"WZX Engine" / L"live-player" / L"game").u8string();
        free(la);
    }
    if (opt.scene == "studio" && std::filesystem::exists(std::filesystem::u8path(opt.dataDir) / "game.json"))
        opt.scene = "game";   // an exported game beside the executable

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"WZXPlayer";
    RegisterClassExW(&wc);
    const float dpi = GetDpiForSystem() / 96.0f;
    RECT r = {0, 0, (LONG)(1600 * dpi), (LONG)(900 * dpi)};
    AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, GetDpiForSystem());
    HWND hwnd = CreateWindowW(L"WZXPlayer", L"WZX Game", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, nullptr);
    opt.uiScale = GetDpiForWindow(hwnd) / 96.0f;
    opt.fontFile = "C:\\Windows\\Fonts\\segoeui.ttf";

    SwapChainDesc sc;
    sc.ColorBufferFormat = TEX_FORMAT_RGBA8_UNORM;   // the post pass writes sRGB values itself
    sc.DepthBufferFormat = TEX_FORMAT_UNKNOWN;       // the Lit renderer has its own MSAA depth
    sc.Usage = SWAP_CHAIN_USAGE_RENDER_TARGET | SWAP_CHAIN_USAGE_COPY_SOURCE;   // WZX Live screenshots read it back
    Win32NativeWindow window{hwnd};
    IEngineFactory* factory = nullptr;
#if VULKAN_SUPPORTED
    if (backend == "vk") {
        IEngineFactoryVk* f = GetEngineFactoryVk();
        EngineVkCreateInfo ci;
        ci.Features.ShaderResourceRuntimeArrays = DEVICE_FEATURE_STATE_OPTIONAL;
        ci.DynamicHeapSize = 32 << 20;   // the DDGI probe captures map the draw constants thousands of times per frame
        ci.DynamicHeapPageSize = 2 << 20;
        f->CreateDeviceAndContextsVk(ci, &g_device, &g_context);
        if (g_device) f->CreateSwapChainVk(g_device, g_context, sc, window, &g_swapChain);
        factory = f;
    }
#endif
    if (!g_device) {
        IEngineFactoryD3D12* f = GetEngineFactoryD3D12();
        EngineD3D12CreateInfo ci;
        ci.Features.ShaderResourceRuntimeArrays = DEVICE_FEATURE_STATE_OPTIONAL;
        ci.DynamicHeapPageSize = 8 << 20;   // the DDGI probe captures map the draw constants thousands of times per frame
        f->CreateDeviceAndContextsD3D12(ci, &g_device, &g_context);
        if (g_device) f->CreateSwapChainD3D12(g_device, g_context, sc, FullScreenModeDesc{}, window, &g_swapChain);
        factory = f;
    }
    if (!g_device || !g_swapChain) {
        MessageBoxW(nullptr, L"No DirectX 12 device.", L"WZX Game", MB_ICONERROR);
        return 1;
    }

    g_opt = opt;
    g_factory = factory;
    g_shaders = exeDir() + "\\shaders";
    std::string err;
    if (g_liveOn) {
        // WZX Live: listen for the editor (127.0.0.1 only unless --live-lan), start on the waiting screen
        player::LiveConfig lc;
        lc.liveDir = g_liveDir;
        lc.stateFile = (std::filesystem::u8path(g_liveDir).parent_path() / "wzx_live_device.json").u8string();
        lc.deviceType = "Desktop";
        lc.model = "PC";
        wchar_t comp[256] = {};
        DWORD n = 256;
        GetComputerNameW(comp, &n);
        lc.name = "WZX Player (" + utf8(comp) + ")";
        lc.built = __DATE__ " " __TIME__;
        lc.port = livePort;
        lc.loopbackOnly = !liveLan;
        lc.advertise = liveLan;
        lc.hasBundledGame = std::filesystem::exists(std::filesystem::u8path(opt.dataDir) / "game.json");
        for (size_t s = 0; s < liveOld.size();) {
            size_t e = liveOld.find(',', s);
            if (e == std::string::npos) e = liveOld.size();
            if (e > s) lc.simulateMissing.push_back(liveOld.substr(s, e - s));
            s = e + 1;
        }
        lc.simulateProtocol = liveProtocol;
        if (!g_live.start(lc, &err)) {
            MessageBoxW(nullptr, std::wstring(err.begin(), err.end()).c_str(), L"WZX Game", MB_ICONERROR);
            return 1;
        }
        SetWindowTextW(hwnd, (L"WZX Game - Live, code " + std::wstring(g_live.code().begin(), g_live.code().end()) + L", port " +
                              std::to_wstring(g_live.port())).c_str());
        opt.scene = "empty";
    }
    if (!startGame(opt.scene, opt.dataDir, &err)) {
        std::wstring w(err.begin(), err.end());
        MessageBoxW(nullptr, w.c_str(), L"WZX Game", MB_ICONERROR);
        return 1;
    }
    QueryPerformanceFrequency(&g_freq);

    ShowWindow(hwnd, show);
    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        } else {
            render(hwnd);
            if (g_liveOn) liveTick();
        }
    }
    g_ready = false;
    g_live.stop();
    g_context->Flush();
    g_context->WaitForIdle();
    if (g_player) g_player->shutdown();
    g_player.reset();
    g_swapChain.Release();
    g_context.Release();
    g_device.Release();
    return 0;
}
