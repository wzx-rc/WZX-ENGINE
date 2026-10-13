// The standalone host contains only the inspector, its document model and the
// official Dear ImGui Win32 / DirectX 11 backends.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "inspector.h"

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
namespace ai = atelier::inspector;

namespace {
ComPtr<ID3D11Device> device;
ComPtr<ID3D11DeviceContext> context;
ComPtr<IDXGISwapChain> swapChain;
ComPtr<ID3D11RenderTargetView> renderTarget;
UINT resizeWidth = 0, resizeHeight = 0;
bool captureRequested = false;
float dpiScale = 1.f, requestedDpiScale = 1.f;
float scaleOverride = 0.f;
UINT windowDpi = 96;
fs::path executableDirectory;
constexpr DWORD windowStyle = WS_OVERLAPPEDWINDOW;

RECT WindowBounds(int width, int height) {
    RECT bounds{ 0, 0, width, height };
    // UI scale can be fixed for a reference capture while the native frame
    // still follows the actual monitor DPI.
    AdjustWindowRectExForDpi(&bounds, windowStyle, FALSE, 0, windowDpi);
    return bounds;
}

void Log(const std::string& text) {
    std::error_code error;
    fs::create_directories(executableDirectory / L"outputs", error);
    std::ofstream(executableDirectory / L"outputs" / L"host.log", std::ios::app) << text << '\n';
    OutputDebugStringA(("Atelier Inspector: " + text + "\n").c_str());
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
        Log("Using the Windows WARP software renderer.");
        result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP,
            nullptr, 0, levels, 2, D3D11_SDK_VERSION, &desc, &swapChain, &device, &actual, &context);
    }
    return SUCCEEDED(result) && CreateRenderTarget();
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
            static_cast<const unsigned char*>(map.pData) + size_t(y) * map.RowPitch,
            size_t(desc.Width) * 4);
    context->Unmap(staging.Get(), 0);
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
    Log("Screenshot " + filename.u8string() + " (" + std::to_string(desc.Width) + "x" + std::to_string(desc.Height) + ")");
    return true;
}

void ConfigureScale(float scale) {
    dpiScale = scale;
    ai::ApplyTheme(scale);
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    wchar_t windowsDirectory[MAX_PATH]{};
    GetWindowsDirectoryW(windowsDirectory, MAX_PATH);
    const fs::path fontPath = fs::path(windowsDirectory) / L"Fonts" / L"segoeui.ttf";
    if (fs::exists(fontPath)) io.Fonts->AddFontFromFileTTF(fontPath.u8string().c_str(), 12.f * scale);
    if (io.Fonts->Fonts.empty()) {
        ImFontConfig fallback;
        fallback.SizePixels = 12.f * scale;
        io.Fonts->AddFontDefault(&fallback);
    }
    ImGui::GetStyle().FontScaleMain = 1.f;
    ImGui::GetStyle().FontScaleDpi = 1.f;
}

std::string JsonEscape(const std::string& text) {
    std::string out;
    for (const char c : text) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else out += c;
    }
    return out;
}

void ChooseDocument(HWND window, ai::InspectorState& state, bool saveAs) {
    std::vector<wchar_t> filename(32768, L'\0');
    const fs::path current = state.storagePath.empty()
        ? executableDirectory / L"state.json" : fs::u8path(state.storagePath);
    const std::wstring initialFilename = current.wstring();
    std::copy_n(initialFilename.c_str(), std::min(initialFilename.size(), filename.size() - 1), filename.data());
    const std::wstring initialDirectory = current.parent_path().wstring();
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window;
    dialog.lpstrFilter = L"Inspector documents (*.json)\0*.json\0All files (*.*)\0*.*\0\0";
    dialog.lpstrFile = filename.data();
    dialog.nMaxFile = DWORD(filename.size());
    dialog.lpstrInitialDir = initialDirectory.c_str();
    dialog.lpstrTitle = saveAs ? L"Save inspector document" : L"Open inspector document";
    dialog.lpstrDefExt = L"json";
    dialog.Flags = OFN_EXPLORER | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST |
        (saveAs ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    const BOOL selected = saveAs ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
    if (selected) {
        const std::string path = fs::path(filename.data()).u8string();
        if (saveAs) state.Save(path); else state.Load(path);
    } else if (CommDlgExtendedError() != 0) state.status = "The file chooser could not be opened.";
}

bool DumpControls(const fs::path& filename, const ai::InspectorState& state, float width, float height) {
    std::error_code error;
    if (filename.has_parent_path()) fs::create_directories(filename.parent_path(), error);
    std::ofstream report(filename);
    if (!report) return false;
    report << "{\n  \"clientWidth\":" << width << ",\"clientHeight\":" << height
           << ",\"dpiScale\":" << dpiScale << ",\"tab\":" << state.tab
           << ",\"pickerOpen\":" << (state.pickerOpen ? "true" : "false") << ",\n  \"controls\":[\n";
    for (size_t i = 0; i < state.controls.size(); ++i) {
        const auto& rect = state.controls[i];
        report << "    {\"id\":\"" << JsonEscape(rect.id) << "\",\"x\":" << rect.x << ",\"y\":" << rect.y
               << ",\"width\":" << rect.width << ",\"height\":" << rect.height
               << ",\"enabled\":" << (rect.enabled ? "true" : "false") << "}"
               << (i + 1 < state.controls.size() ? "," : "") << '\n';
    }
    report << "  ]\n}\n";
    return bool(report);
}

// The UI test uses the same Dear ImGui mouse event queue as interactive input.
// Every press and release occupies a separate frame; assertions run after the
// widget has processed both events. No document field is set by the test.
struct UiTest {
    struct Check { std::string name; bool passed; std::string detail; };
    fs::path directory;
    std::vector<Check> checks;
    ai::Document initial;
    std::array<float, 4> draftBeforeMode{};
    float editedRoughness = 0.f;
    ImVec2 cursor{ -100.f, -100.f };
    bool done = false;
    int frame = 0;

    void Record(const std::string& name, bool passed, const std::string& detail = {}) {
        checks.push_back({ name, passed, detail });
        Log(std::string(passed ? "PASS: " : "FAIL: ") + name + (detail.empty() ? "" : " (" + detail + ")"));
    }
    void Press(ai::InspectorState& state, const char* id, float fraction = .5f) {
        if (const auto* rect = state.FindControl(id); rect && rect->enabled && rect->width > 0 && rect->height > 0) {
            cursor = ImVec2(rect->x + rect->width * fraction, rect->y + rect->height * .5f);
            ImGui::GetIO().AddMousePosEvent(cursor.x, cursor.y);
            ImGui::GetIO().AddMouseButtonEvent(0, true);
        } else Record(std::string("Control available: ") + id, false, "Missing, disabled, or empty recorded rectangle");
    }
    void Release() { ImGui::GetIO().AddMouseButtonEvent(0, false); }
    void BeforeFrame(ai::InspectorState& state) {
        ++frame;
        auto& io = ImGui::GetIO();
        io.AddFocusEvent(true);
        io.AddMousePosEvent(cursor.x, cursor.y);
        switch (frame) {
        case 8: Press(state, "roughness.slider", .75f); break;
        case 9: Release(); break;
        case 14: Press(state, "roughness.reset"); break;
        case 15: Release(); break;
        case 20: Press(state, "undo"); break;
        case 21: Release(); break;
        case 26: Press(state, "undo"); break;
        case 27: Release(); break;
        case 32: Press(state, "baseColor.swatch"); break;
        case 33: Release(); break;
        case 36: Press(state, "picker.palette.1"); break;
        case 37: Release(); break;
        case 40: Press(state, "picker.cancel"); break;
        case 41: Release(); break;
        case 46: Press(state, "baseColor.swatch"); break;
        case 47: Release(); break;
        case 50: Press(state, "picker.palette.1"); break;
        case 51: Release(); break;
        case 54: Press(state, "picker.apply"); break;
        case 55: Release(); break;
        case 60:
            io.AddKeyEvent(ImGuiMod_Ctrl, true);
            io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
            io.AddKeyEvent(ImGuiKey_Z, true);
            break;
        case 61:
            io.AddKeyEvent(ImGuiKey_Z, false);
            io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
            io.AddKeyEvent(ImGuiMod_Ctrl, false);
            break;
        case 66: Press(state, "tab.2"); break;
        case 67: Release(); break;
        case 72: Press(state, "tab.1"); break;
        case 73: Release(); break;
        case 78: Press(state, "tab.0"); break;
        case 79: Release(); break;
        case 84: Press(state, "roughness.value"); break;
        case 85: Release(); break;
        case 86:
            io.AddKeyEvent(ImGuiMod_Ctrl, true);
            io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
            io.AddKeyEvent(ImGuiKey_A, true);
            break;
        case 87:
            io.AddKeyEvent(ImGuiKey_A, false);
            io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
            io.AddKeyEvent(ImGuiMod_Ctrl, false);
            break;
        case 88: io.AddInputCharactersUTF8("0.640"); break;
        case 90: Press(state, "tab.0"); break;
        case 91: Release(); break;
        case 94: Press(state, "undo"); break;
        case 95: Release(); break;
        case 98: Press(state, "baseColor.swatch"); break;
        case 99: Release(); break;
        case 102: Press(state, "picker.mode.hsv"); break;
        case 103: Release(); break;
        case 106: Press(state, "picker.cancel"); break;
        case 107: Release(); break;
        }
    }
    void AfterFrame(ai::InspectorState& state) {
        switch (frame) {
        case 5:
            initial = state.value;
            Record("Default capture", SaveCapture(directory / L"default.png")); break;
        case 11:
            editedRoughness = state.value.roughness;
            Record("Slider edits roughness", std::fabs(editedRoughness - initial.roughness) > .1f,
                std::to_string(initial.roughness) + " -> " + std::to_string(editedRoughness));
            break;
        case 17:
            Record("Reset restores default roughness", std::fabs(state.value.roughness - state.defaults.roughness) < .001f); break;
        case 23:
            Record("Undo restores value before reset", std::fabs(state.value.roughness - editedRoughness) < .001f); break;
        case 29:
            Record("Undo restores initial roughness", std::fabs(state.value.roughness - initial.roughness) < .001f); break;
        case 35:
            Record("Swatch opens picker", state.pickerOpen);
            Record("Picker capture", SaveCapture(directory / L"picker.png")); break;
        case 39:
            Record("Palette edits draft", state.pickerDraft != initial.baseColor); break;
        case 43:
            Record("Cancel closes picker", !state.pickerOpen);
            Record("Cancel preserves document color", state.value.baseColor == initial.baseColor); break;
        case 57:
            Record("Apply closes picker", !state.pickerOpen);
            Record("Apply commits chosen color", state.value.baseColor != initial.baseColor); break;
        case 63:
            Record("Keyboard Ctrl+Z restores color before Apply", state.value.baseColor == initial.baseColor); break;
        case 69:
            Record("Texture tab click", state.tab == 2);
            Record("Texture capture", SaveCapture(directory / L"texture.png")); break;
        case 75:
            Record("Stage tab click", state.tab == 1);
            Record("Stage capture", SaveCapture(directory / L"stage.png")); break;
        case 82:
            Record("Material tab click", state.tab == 0);
            Record("History returns to initial document", ai::Equal(state.value, initial));
            break;
        case 93:
            Record("Numeric entry commits 0.640 on blur", std::fabs(state.value.roughness - .64f) < .001f,
                "roughness=" + std::to_string(state.value.roughness));
            break;
        case 97:
            Record("Undo restores roughness before numeric entry", std::fabs(state.value.roughness - initial.roughness) < .001f);
            break;
        case 101:
            Record("Picker reopens for color mode check", state.pickerOpen);
            draftBeforeMode = state.pickerDraft;
            break;
        case 105:
            Record("HSV mode click changes display mode", state.pickerMode == 1);
            Record("RGB to HSV mode preserves draft color", state.pickerDraft == draftBeforeMode);
            break;
        case 110:
            Record("Cancel closes HSV picker", !state.pickerOpen);
            Record("Numeric and picker tests restore initial document", ai::Equal(state.value, initial));
            WriteReport(); done = true; break;
        }
    }
    bool Passed() const {
        return !checks.empty() && std::all_of(checks.begin(), checks.end(), [](const Check& check) { return check.passed; });
    }
    void WriteReport() {
        std::error_code error;
        fs::create_directories(directory, error);
        std::ofstream report(directory / L"ui-test.json");
        report << "{\n  \"passed\": " << (Passed() ? "true" : "false")
               << ",\n  \"frames\": " << frame
               << ",\n  \"input\": \"Dear ImGui mouse and keyboard events through rendered controls\",\n  \"checks\": [\n";
        for (size_t i = 0; i < checks.size(); ++i) {
            const auto& check = checks[i];
            report << "    {\"name\":\"" << JsonEscape(check.name) << "\",\"passed\":"
                   << (check.passed ? "true" : "false") << ",\"detail\":\"" << JsonEscape(check.detail) << "\"}"
                   << (i + 1 < checks.size() ? "," : "") << '\n';
        }
        report << "  ]\n}\n";
    }
};
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
LRESULT WINAPI WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam)) return true;
    switch (message) {
    case WM_SIZE:
        if (wparam != SIZE_MINIMIZED) { resizeWidth = LOWORD(lparam); resizeHeight = HIWORD(lparam); }
        return 0;
    case WM_GETMINMAXINFO: {
        const RECT minimum = WindowBounds(int(std::ceil(342.f * requestedDpiScale)),
            int(std::ceil(480.f * requestedDpiScale)));
        reinterpret_cast<MINMAXINFO*>(lparam)->ptMinTrackSize =
            POINT{ minimum.right - minimum.left, minimum.bottom - minimum.top };
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((wparam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_KEYDOWN:
        if (wparam == VK_F12) captureRequested = true;
        break;
    case WM_DPICHANGED: {
        windowDpi = HIWORD(wparam);
        requestedDpiScale = scaleOverride > 0.f ? scaleOverride : float(windowDpi) / 96.f;
        const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
        SetWindowPos(window, nullptr, suggested->left, suggested->top,
            suggested->right - suggested->left, suggested->bottom - suggested->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    wchar_t executablePath[32768]{};
    GetModuleFileNameW(nullptr, executablePath, DWORD(std::size(executablePath)));
    executableDirectory = fs::path(executablePath).parent_path();
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    int frameLimit = 0, clientWidth = 0, clientHeight = 0, initialTab = 0;
    bool hidden = false, openPicker = false;
    fs::path screenshot, selfTestPath, uiTestDirectory, controlsPath;
    for (int i = 1; i < argc; ++i) {
        const std::wstring argument = argv[i];
        if (argument == L"--screenshot" && i + 1 < argc) screenshot = fs::absolute(argv[++i]);
        else if (argument == L"--self-test" && i + 1 < argc) selfTestPath = fs::absolute(argv[++i]);
        else if (argument == L"--ui-test" && i + 1 < argc) uiTestDirectory = fs::absolute(argv[++i]);
        else if (argument == L"--dump-controls" && i + 1 < argc) controlsPath = fs::absolute(argv[++i]);
        else if (argument == L"--frames" && i + 1 < argc) frameLimit = std::max(1, _wtoi(argv[++i]));
        else if (argument == L"--width" && i + 1 < argc) clientWidth = std::max(342, _wtoi(argv[++i]));
        else if (argument == L"--height" && i + 1 < argc) clientHeight = std::max(480, _wtoi(argv[++i]));
        else if ((argument == L"--scale" || argument == L"--dpi-scale") && i + 1 < argc) {
            const float requested = float(_wtof(argv[++i]));
            if (std::isfinite(requested) && requested > 0.f) scaleOverride = std::clamp(requested, .5f, 4.f);
        }
        else if (argument == L"--tab" && i + 1 < argc) initialTab = std::clamp(_wtoi(argv[++i]), 0, 2);
        else if (argument == L"--picker") openPicker = true;
        else if (argument == L"--hidden") hidden = true;
    }
    LocalFree(argv);
    if (!selfTestPath.empty()) {
        std::error_code error;
        fs::create_directories(selfTestPath.parent_path(), error);
        return ai::RunModelTests(selfTestPath.u8string()) ? 0 : 10;
    }
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ImGui_ImplWin32_EnableDpiAwareness();
    const HMONITOR monitor = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    const float monitorScale = ImGui_ImplWin32_GetDpiScaleForMonitor(monitor);
    windowDpi = UINT(std::lround(monitorScale * 96.f));
    dpiScale = requestedDpiScale = scaleOverride > 0.f ? scaleOverride : monitorScale;
    MONITORINFO monitorInfo{ sizeof(MONITORINFO) };
    GetMonitorInfoW(monitor, &monitorInfo);
    const RECT workArea = monitorInfo.rcWork;
    const RECT frameBounds = WindowBounds(0, 0);
    const int frameWidth = frameBounds.right - frameBounds.left;
    const int frameHeight = frameBounds.bottom - frameBounds.top;
    if (!clientWidth) clientWidth = std::min<int>(int(std::ceil(342.f * dpiScale)),
        workArea.right - workArea.left - frameWidth);
    if (!clientHeight) clientHeight = std::min<int>(int(std::ceil(920.f * dpiScale)),
        workArea.bottom - workArea.top - frameHeight);
    WNDCLASSEXW windowClass{ sizeof(WNDCLASSEXW), CS_CLASSDC, WindowProc, 0, 0, instance,
        LoadIconW(nullptr, IDI_APPLICATION), LoadCursorW(nullptr, IDC_ARROW), nullptr, nullptr,
        L"AtelierInspectorWindow", nullptr };
    RegisterClassExW(&windowClass);
    const RECT bounds = WindowBounds(clientWidth, clientHeight);
    const int outerWidth = bounds.right - bounds.left, outerHeight = bounds.bottom - bounds.top;
    HWND window = CreateWindowExW(0, windowClass.lpszClassName, L"Atelier Inspector",
        windowStyle, workArea.left + std::max<LONG>(0, (workArea.right - workArea.left - outerWidth) / 2),
        workArea.top + std::max<LONG>(0, (workArea.bottom - workArea.top - outerHeight) / 2), outerWidth, outerHeight,
        nullptr, nullptr, instance, nullptr);
    if (!window || !CreateDevice(window)) {
        Log("ERROR: DirectX 11 initialization failed.");
        if (window) DestroyWindow(window);
        if (SUCCEEDED(comResult)) CoUninitialize();
        return 1;
    }
    const BOOL darkTitleBar = TRUE;
    DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkTitleBar, sizeof(darkTitleBar));
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX11_Init(device.Get(), context.Get());
    ConfigureScale(dpiScale);
    ai::InspectorState state;
    state.storagePath = (executableDirectory / L"state.json").u8string();
    state.tab = initialTab;
    if (openPicker) state.OpenPicker();
    UiTest uiTest;
    const bool testingUi = !uiTestDirectory.empty();
    if (testingUi) {
        uiTest.directory = uiTestDirectory;
        if (!frameLimit) frameLimit = 120;
        io.ConfigDebugIgnoreFocusLoss = true;
    }
    if (hidden && !frameLimit) frameLimit = 30;
    if (!hidden) { ShowWindow(window, SW_SHOW); UpdateWindow(window); }
    Log("Started " + std::to_string(clientWidth) + "x" + std::to_string(clientHeight) +
        (hidden ? " hidden" : " visible") + "; DPI " + std::to_string(dpiScale));
    bool done = false, captured = false, dumpedControls = false;
    int frameNumber = 0, returnCode = 0;
    const int captureFrame = frameLimit ? std::min(10, frameLimit) : 10;
    while (!done) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
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
        if (std::fabs(requestedDpiScale - dpiScale) > .001f) ConfigureScale(requestedDpiScale);
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        if (testingUi) uiTest.BeforeFrame(state);
        ImGui::NewFrame();
        if (io.KeyCtrl) {
            if (ImGui::IsKeyPressed(ImGuiKey_S, false)) {
                if (io.KeyShift) ChooseDocument(window, state, true);
                else state.Save(state.storagePath.empty() ? (executableDirectory / L"state.json").u8string() : state.storagePath);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_O, false)) ChooseDocument(window, state, false);
            if (!io.WantTextInput) {
                if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
                    if (io.KeyShift) state.Redo(); else state.Undo();
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) state.Redo();
            }
        }
        ai::DrawInspector(state, io.DisplaySize.x, io.DisplaySize.y);
        ImGui::Render();
        ID3D11RenderTargetView* target = renderTarget.Get();
        context->OMSetRenderTargets(1, &target, nullptr);
        const float clearColor[4] = { .02745f, .0353f, .0471f, 1.f };
        context->ClearRenderTargetView(target, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        ++frameNumber;
        if (!screenshot.empty() && !captured && frameNumber >= captureFrame) {
            captured = true;
            if (!SaveCapture(screenshot)) { Log("ERROR: PNG capture failed."); returnCode = 4; }
        }
        if (!controlsPath.empty() && !dumpedControls && frameNumber >= captureFrame) {
            dumpedControls = true;
            if (!DumpControls(controlsPath, state, io.DisplaySize.x, io.DisplaySize.y)) {
                state.status = "Control rectangles could not be saved.";
                returnCode = 6;
            }
        }
        if (captureRequested) {
            captureRequested = false;
            if (!SaveCapture(executableDirectory / L"outputs" / L"capture.png")) state.status = "Screenshot could not be saved.";
            else state.status = "Screenshot saved to outputs/capture.png";
        }
        if (testingUi) {
            uiTest.AfterFrame(state);
            if (uiTest.done) { returnCode = uiTest.Passed() ? 0 : 11; done = true; }
        }
        const HRESULT presented = swapChain->Present(hidden ? 0 : 1, 0);
        if (FAILED(presented)) { Log("ERROR: DirectX presentation failed."); returnCode = 5; break; }
        if (frameLimit && frameNumber >= frameLimit) done = true;
    }
    if (testingUi && !uiTest.done) {
        uiTest.Record("UI test completed before shutdown", false);
        uiTest.WriteReport();
        returnCode = 11;
    }
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    renderTarget.Reset(); swapChain.Reset(); context.Reset(); device.Reset();
    DestroyWindow(window);
    UnregisterClassW(windowClass.lpszClassName, instance);
    if (SUCCEEDED(comResult)) CoUninitialize();
    Log("Exited after " + std::to_string(frameNumber) + " frames; code " + std::to_string(returnCode));
    return returnCode;
}
