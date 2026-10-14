// WZX Player for Windows: a window, a DirectX 12 device through Diligent, the Lit renderer.
// Mouse: left or right drag = orbit, wheel = zoom, F = default view, Esc = quit.
// Options: --hdri sky|none|file.hdr  --backend d3d12|vk
#include "Player.h"

#include "Graphics/GraphicsEngineD3D12/interface/EngineFactoryD3D12.h"
#if VULKAN_SUPPORTED
#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#endif
#include "Graphics/GraphicsEngine/interface/SwapChain.h"

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>

#include <string>

using namespace Diligent;

namespace {

player::Player g_player;
RefCntAutoPtr<IRenderDevice> g_device;
RefCntAutoPtr<IDeviceContext> g_context;
RefCntAutoPtr<ISwapChain> g_swapChain;
bool g_dragging = false;
POINT g_last = {};

std::string utf8(const wchar_t* w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

// Folder of the executable: the shaders are copied next to it.
std::string exeDir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring p = path;
    return utf8(p.substr(0, p.find_last_of(L"\\/")).c_str());
}

void render() {
    if (!g_swapChain) return;
    const SwapChainDesc& d = g_swapChain->GetDesc();
    g_player.frame(g_swapChain->GetCurrentBackBufferRTV(), d.Width, d.Height);
    g_swapChain->Present(1);
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_SIZE:
        if (g_swapChain && LOWORD(lp) > 0 && HIWORD(lp) > 0) g_swapChain->Resize(LOWORD(lp), HIWORD(lp));
        return 0;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
        g_dragging = true;
        g_last = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        SetCapture(hwnd);
        return 0;
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
        g_dragging = false;
        ReleaseCapture();
        return 0;
    case WM_MOUSEMOVE:
        if (g_dragging) {
            POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            g_player.orbit((float)(p.x - g_last.x), (float)(p.y - g_last.y));
            g_last = p;
        }
        return 0;
    case WM_MOUSEWHEEL:
        g_player.zoom(GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 0.9f : 1.0f / 0.9f);
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) DestroyWindow(hwnd);
        if (wp == 'F') g_player.resetView();
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    player::Options opt;
    std::string backend = "d3d12";
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--hdri" && i + 1 < argc) opt.hdri = utf8(argv[++i]);
        else if (a == L"--backend" && i + 1 < argc) backend = utf8(argv[++i]);
    }
    LocalFree(argv);

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"WZXPlayer";
    RegisterClassExW(&wc);
    RECT r = {0, 0, 1600, 900};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowW(L"WZXPlayer", L"WZX Game", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, nullptr);

    SwapChainDesc sc;
    sc.ColorBufferFormat = TEX_FORMAT_RGBA8_UNORM;   // the post pass writes sRGB values itself
    sc.DepthBufferFormat = TEX_FORMAT_UNKNOWN;       // the Lit renderer has its own MSAA depth
    Win32NativeWindow window{hwnd};
    IEngineFactory* factory = nullptr;
#if VULKAN_SUPPORTED
    if (backend == "vk") {
        IEngineFactoryVk* f = GetEngineFactoryVk();
        EngineVkCreateInfo ci;
        ci.Features.ShaderResourceRuntimeArrays = DEVICE_FEATURE_STATE_OPTIONAL;
        f->CreateDeviceAndContextsVk(ci, &g_device, &g_context);
        if (g_device) f->CreateSwapChainVk(g_device, g_context, sc, window, &g_swapChain);
        factory = f;
    }
#endif
    if (!g_device) {
        IEngineFactoryD3D12* f = GetEngineFactoryD3D12();
        EngineD3D12CreateInfo ci;
        ci.Features.ShaderResourceRuntimeArrays = DEVICE_FEATURE_STATE_OPTIONAL;
        f->CreateDeviceAndContextsD3D12(ci, &g_device, &g_context);
        if (g_device) f->CreateSwapChainD3D12(g_device, g_context, sc, FullScreenModeDesc{}, window, &g_swapChain);
        factory = f;
    }
    if (!g_device || !g_swapChain) {
        MessageBoxW(nullptr, L"No DirectX 12 device.", L"WZX Game", MB_ICONERROR);
        return 1;
    }

    std::string err;
    if (!g_player.init(g_device, g_context, factory, g_swapChain->GetDesc().ColorBufferFormat, exeDir() + "\\shaders", opt, &err)) {
        std::wstring w(err.begin(), err.end());
        MessageBoxW(nullptr, w.c_str(), L"WZX Game", MB_ICONERROR);
        return 1;
    }

    ShowWindow(hwnd, show);
    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        } else {
            render();
        }
    }
    g_context->Flush();
    g_player.shutdown();
    g_swapChain.Release();
    g_context.Release();
    g_device.Release();
    return 0;
}
