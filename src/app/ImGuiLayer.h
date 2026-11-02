// Dear ImGui glue for the editor: Win32 + DirectX 12 backends, fonts (Segoe UI + Segoe Fluent Icons),
// the editor theme and small GPU textures for thumbnails.
#pragma once
#include "imgui.h"
#include "vp3d/Renderer.h"
#include <windows.h>
#include <vector>

struct ImGui_ImplDX12_InitInfo;

namespace app {

using vp3d::ComPtr;

class ImGuiLayer {
public:
    bool init(HWND hwnd, vp3d::Renderer& renderer, float dpiScale);
    void shutdown();
    void newFrame();
    // Records and submits the UI over the current back buffer (also returns the viewport texture to UAV state).
    void render(bool viewportTextureInUse);
    void onResize();   // after the swapchain was resized

    // The 3D viewport image (renderer output texture) as an ImGui texture. Call after the 3D frame was rendered.
    ImTextureID viewportTexture();
    // Static RGBA8 texture (thumbnails). Returns 0 on failure.
    ImTextureID createTexture(const uint8_t* rgba, uint32_t w, uint32_t h);
    void releaseTexture(ImTextureID id);

    ImFont* fontBold = nullptr;
    ImFont* fontSmall = nullptr;
    ImFont* fontHud = nullptr;    // Bahnschrift: titles of the Halo theme
    // Spartan theme, sized like CSS pixels: Segoe UI (Inter), Segoe UI Semibold, Bahnschrift (Barlow Condensed), Consolas (JetBrains Mono)
    ImFont* spUi = nullptr;
    ImFont* spSemi = nullptr;
    ImFont* spHud = nullptr;
    ImFont* spMono = nullptr;
    ImFont* spMonoBold = nullptr;
    float dpi = 1.0f;

private:
    struct Tex { ComPtr<ID3D12Resource> res; int slot = -1; };
    void createRtvs();
    bool allocSlot(int& slot);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu(int slot) const;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu(int slot) const;
    static void srvAlloc(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* c, D3D12_GPU_DESCRIPTOR_HANDLE* g);
    static void srvFree(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE c, D3D12_GPU_DESCRIPTOR_HANDLE g);

    vp3d::Renderer* m_r = nullptr;
    ComPtr<ID3D12DescriptorHeap> m_srvHeap, m_rtvHeap;
    UINT m_srvSize = 0, m_rtvSize = 0;
    std::vector<bool> m_used;
    int m_viewportSlot = -1;
    ComPtr<ID3D12CommandAllocator> m_alloc;
    ComPtr<ID3D12GraphicsCommandList> m_cmd;
    std::vector<Tex> m_textures;
};

} // namespace app
