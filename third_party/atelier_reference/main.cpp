// Native Windows / DirectX 11 host for the Atelier Dear ImGui editor.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <iterator>
#include <cstring>
#include <cstdlib>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "editor_ui.h"
#include "material_renderer.h"

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

namespace {
ComPtr<ID3D11Device> device;
ComPtr<ID3D11DeviceContext> context;
ComPtr<IDXGISwapChain> swapChain;
ComPtr<ID3D11RenderTargetView> renderTarget;
UINT resizeWidth = 0, resizeHeight = 0;
bool captureRequested = false;
fs::path executableDirectory;

void Log(const std::string& message) {
    std::error_code error;
    fs::create_directories(executableDirectory / L"outputs", error);
    std::ofstream out(executableDirectory / L"outputs" / L"host.log", std::ios::app);
    out << message << '\n';
    OutputDebugStringA(("Atelier: " + message + "\n").c_str());
}

bool CreateRenderTarget() {
    ComPtr<ID3D11Texture2D> backbuffer;
    return SUCCEEDED(swapChain->GetBuffer(0, IID_PPV_ARGS(&backbuffer))) &&
           SUCCEEDED(device->CreateRenderTargetView(backbuffer.Get(), nullptr, &renderTarget));
}

bool CreateDevice(HWND window) {
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 2;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = window;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL actual{};
    HRESULT result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE,
        nullptr, 0, levels, 2, D3D11_SDK_VERSION, &desc, &swapChain, &device, &actual, &context);
    if (FAILED(result)) {
        Log("Hardware DirectX 11 device unavailable; using Windows WARP.");
        result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP,
            nullptr, 0, levels, 2, D3D11_SDK_VERSION, &desc, &swapChain, &device, &actual, &context);
    }
    return SUCCEEDED(result) && CreateRenderTarget();
}

bool LoadTexture(const fs::path& filename, ComPtr<ID3D11ShaderResourceView>& view,
                 int& width, int& height) {
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateDecoderFromFilename(filename.c_str(), nullptr, GENERIC_READ,
            WICDecodeMetadataCacheOnLoad, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)) ||
        FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return false;
    UINT imageWidth = 0, imageHeight = 0;
    if (FAILED(converter->GetSize(&imageWidth, &imageHeight)) || !imageWidth || !imageHeight) return false;
    const UINT stride = imageWidth * 4;
    std::vector<unsigned char> pixels(size_t(stride) * imageHeight);
    if (FAILED(converter->CopyPixels(nullptr, stride, UINT(pixels.size()), pixels.data()))) return false;
    D3D11_TEXTURE2D_DESC textureDesc{};
    textureDesc.Width = imageWidth;
    textureDesc.Height = imageHeight;
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage = D3D11_USAGE_IMMUTABLE;
    textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{ pixels.data(), stride, 0 };
    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(device->CreateTexture2D(&textureDesc, &data, &texture)) ||
        FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, &view))) return false;
    width = static_cast<int>(imageWidth);
    height = static_cast<int>(imageHeight);
    return true;
}

bool SaveCapture(const fs::path& filename) {
    ComPtr<ID3D11Texture2D> backbuffer, staging;
    if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&backbuffer)))) return false;
    D3D11_TEXTURE2D_DESC desc{};
    backbuffer->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &staging))) return false;
    context->CopyResource(staging.Get(), backbuffer.Get());
    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map))) return false;
    std::vector<unsigned char> pixels(size_t(desc.Width) * desc.Height * 4);
    for (UINT y = 0; y < desc.Height; ++y)
        memcpy(pixels.data() + size_t(y) * desc.Width * 4,
            static_cast<const unsigned char*>(map.pData) + size_t(y) * map.RowPitch, size_t(desc.Width) * 4);
    context->Unmap(staging.Get(), 0);
    // WIC's native PNG encoder accepts BGRA; the swap chain stores RGBA.
    for (size_t pixel = 0; pixel < pixels.size(); pixel += 4)
        std::swap(pixels[pixel], pixels[pixel + 2]);

    std::error_code error;
    if (filename.has_parent_path()) fs::create_directories(filename.parent_path(), error);
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromFilename(filename.c_str(), GENERIC_WRITE)) ||
        FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(&frame, nullptr)) ||
        FAILED(frame->Initialize(nullptr)) ||
        FAILED(frame->SetSize(desc.Width, desc.Height))) return false;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(frame->SetPixelFormat(&format)) || format != GUID_WICPixelFormat32bppBGRA ||
        FAILED(frame->WritePixels(desc.Height, desc.Width * 4, UINT(pixels.size()), pixels.data())) ||
        FAILED(frame->Commit()) || FAILED(encoder->Commit())) return false;
    Log("Screenshot saved: " + filename.u8string() + " (" + std::to_string(desc.Width) + "x" + std::to_string(desc.Height) + ")");
    return true;
}
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
LRESULT WINAPI WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam)) return true;
    switch (message) {
    case WM_SIZE:
        if (wparam != SIZE_MINIMIZED) {
            resizeWidth = LOWORD(lparam); resizeHeight = HIWORD(lparam);
        }
        return 0;
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO*>(lparam)->ptMinTrackSize = POINT{ 1120, 700 };
        return 0;
    case WM_SYSCOMMAND:
        if ((wparam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_KEYDOWN:
        if (wparam == VK_F12) captureRequested = true;
        break;
    case WM_DPICHANGED:
        if (const RECT* suggested = reinterpret_cast<const RECT*>(lparam))
            SetWindowPos(window, nullptr, suggested->left, suggested->top,
                suggested->right - suggested->left, suggested->bottom - suggested->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    wchar_t executablePath[32768]{};
    GetModuleFileNameW(nullptr, executablePath, DWORD(std::size(executablePath)));
    executableDirectory = fs::path(executablePath).parent_path();
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    int frameLimit = 0, clientWidth = 1600, clientHeight = 900;
    fs::path screenshot;
    for (int i = 1; i < argc; ++i) {
        const std::wstring argument = argv[i];
        if (argument == L"--screenshot" && i + 1 < argc) screenshot = fs::absolute(argv[++i]);
        else if (argument == L"--frames" && i + 1 < argc) frameLimit = std::max(1, _wtoi(argv[++i]));
        else if (argument == L"--width" && i + 1 < argc) clientWidth = std::max(800, _wtoi(argv[++i]));
        else if (argument == L"--height" && i + 1 < argc) clientHeight = std::max(460, _wtoi(argv[++i]));
        else if (argument == L"--smoke-test") {
            SetEnvironmentVariableA("ATELIER_SMOKE_TEST", "1");
            _putenv_s("ATELIER_SMOKE_TEST", "1");
        }
        else if (argument == L"--scene") {
            SetEnvironmentVariableA("ATELIER_START_SCENE", "1");
            _putenv_s("ATELIER_START_SCENE", "1");
        }
        else if (argument == L"--graph") {
            SetEnvironmentVariableA("ATELIER_START_GRAPH", "1");
            _putenv_s("ATELIER_START_GRAPH", "1");
        }
        else if (argument == L"--materials") {
            SetEnvironmentVariableA("ATELIER_START_MATERIAL", "1");
            _putenv_s("ATELIER_START_MATERIAL", "1");
        }
        else if (argument == L"--material-test") {
            SetEnvironmentVariableA("ATELIER_MATERIAL_TEST", "1");
            _putenv_s("ATELIER_MATERIAL_TEST", "1");
        }
        else if (argument == L"--graph-test") {
            SetEnvironmentVariableA("ATELIER_GRAPH_TEST", "1");
            _putenv_s("ATELIER_GRAPH_TEST", "1");
        }
    }
    LocalFree(argv);
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ImGui_ImplWin32_EnableDpiAwareness();
    WNDCLASSEXW windowClass{ sizeof(WNDCLASSEXW), CS_CLASSDC, WindowProc, 0, 0, instance,
        LoadIconW(nullptr, IDI_APPLICATION), LoadCursorW(nullptr, IDC_ARROW), nullptr, nullptr,
        L"AtelierImGuiWindow", nullptr };
    RegisterClassExW(&windowClass);
    const DWORD windowStyle = WS_OVERLAPPEDWINDOW;
    RECT bounds{ 0, 0, clientWidth, clientHeight };
    AdjustWindowRectEx(&bounds, windowStyle, FALSE, 0);
    const int outerWidth = bounds.right - bounds.left;
    const int outerHeight = bounds.bottom - bounds.top;
    HWND window = CreateWindowExW(0, windowClass.lpszClassName, L"Atelier - Material Studio",
        windowStyle, std::max(0, (GetSystemMetrics(SM_CXSCREEN) - outerWidth) / 2),
        std::max(0, (GetSystemMetrics(SM_CYSCREEN) - outerHeight) / 2), outerWidth, outerHeight,
        nullptr, nullptr, instance, nullptr);
    if (window) {
        const BOOL darkTitleBar = TRUE;
        DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkTitleBar, sizeof(darkTitleBar));
    }
    if (!window || !CreateDevice(window)) {
        MessageBoxW(window, L"Unable to initialize the DirectX 11 renderer.", L"Atelier", MB_ICONERROR);
        Log("ERROR: DirectX 11 initialization failed.");
        if (window) DestroyWindow(window);
        UnregisterClassW(windowClass.lpszClassName, instance);
        if (SUCCEEDED(comResult)) CoUninitialize();
        return 1;
    }
    ComPtr<ID3D11ShaderResourceView> referenceView;
    int referenceWidth = 0, referenceHeight = 0;
    fs::path referencePath = executableDirectory / L"assets" / L"reference.jpg";
    if (!fs::exists(referencePath)) referencePath = fs::current_path() / L"assets" / L"reference.jpg";
    if (!LoadTexture(referencePath, referenceView, referenceWidth, referenceHeight)) {
        MessageBoxW(window, L"assets/reference.jpg is missing or unreadable.", L"Atelier", MB_ICONERROR);
        Log("ERROR: Reference texture could not be loaded: " + referencePath.u8string());
        DestroyWindow(window);
        if (SUCCEEDED(comResult)) CoUninitialize();
        return 2;
    }
    // An optional clean scene image separates viewport interaction from the
    // concept's baked toolbar, metrics and transform handles.
    ComPtr<ID3D11ShaderResourceView> viewportView;
    int viewportWidth = 0, viewportHeight = 0;
    fs::path viewportPath = executableDirectory / L"assets" / L"viewport-clean.png";
    if (!fs::exists(viewportPath)) viewportPath = fs::current_path() / L"assets" / L"viewport-clean.png";
    if (fs::exists(viewportPath)) {
        if (LoadTexture(viewportPath, viewportView, viewportWidth, viewportHeight))
            Log("Clean viewport loaded: " + viewportPath.u8string() + " (" +
                std::to_string(viewportWidth) + "x" + std::to_string(viewportHeight) + ")");
        else
            Log("WARNING: Optional clean viewport image could not be loaded; using concept reference.");
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX11_Init(device.Get(), context.Get());
    if (!atelier::material::InitializeRenderer(device.Get(), context.Get()))
        Log("WARNING: Material preview renderer initialization failed: " + atelier::material::GetRendererStatus());
    else
        Log("Material preview renderer: " + atelier::material::GetRendererStatus());
    atelier::SetSceneFile((executableDirectory / L"Ancient_Coast.atelier").u8string());
    atelier::Initialize({ referenceView.Get(), referenceWidth, referenceHeight },
        { viewportView.Get(), viewportWidth, viewportHeight });
    ShowWindow(window, showCommand == SW_HIDE ? SW_SHOW : showCommand);
    UpdateWindow(window);
    Log("Started " + std::to_string(clientWidth) + "x" + std::to_string(clientHeight) +
        "; reference " + std::to_string(referenceWidth) + "x" + std::to_string(referenceHeight));
    bool done = false, automaticCaptureDone = false;
    int frameNumber = 0, returnCode = 0;
    const int captureFrame = frameLimit ? std::min(10, frameLimit) : 10;
    while (!done) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
            if (message.message == WM_QUIT) done = true;
        }
        if (done) break;
        if (IsIconic(window)) { Sleep(10); continue; }
        if (resizeWidth && resizeHeight) {
            renderTarget.Reset();
            const HRESULT resized = swapChain->ResizeBuffers(0, resizeWidth, resizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            resizeWidth = resizeHeight = 0;
            if (FAILED(resized) || !CreateRenderTarget()) { returnCode = 3; break; }
        }
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        atelier::Draw();
        ImGui::Render();
        ID3D11RenderTargetView* target = renderTarget.Get();
        context->OMSetRenderTargets(1, &target, nullptr);
        const float clearColor[4] = { 0.075f, 0.085f, 0.095f, 1.0f };
        context->ClearRenderTargetView(target, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        ++frameNumber;
        if (!screenshot.empty() && !automaticCaptureDone && frameNumber >= captureFrame) {
            automaticCaptureDone = true;
            if (!SaveCapture(screenshot)) { Log("ERROR: Screenshot encoding failed."); returnCode = 4; }
        }
        if (captureRequested) {
            captureRequested = false;
            if (!SaveCapture(executableDirectory / L"outputs" / L"capture.png")) Log("ERROR: F12 screenshot failed.");
        }
        HRESULT presented = swapChain->Present(1, 0);
        if (FAILED(presented)) { Log("ERROR: DirectX presentation failed."); returnCode = 5; break; }
        if (frameLimit && frameNumber >= frameLimit) done = true;
    }
    atelier::Shutdown();
    atelier::material::ShutdownRenderer();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    viewportView.Reset();
    referenceView.Reset();
    renderTarget.Reset();
    swapChain.Reset();
    context.Reset();
    device.Reset();
    DestroyWindow(window);
    UnregisterClassW(windowClass.lpszClassName, instance);
    if (SUCCEEDED(comResult)) CoUninitialize();
    Log("Exited after " + std::to_string(frameNumber) + " frames; code " + std::to_string(returnCode));
    return returnCode;
}
