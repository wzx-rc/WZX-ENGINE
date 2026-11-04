// Minimal Win32 + DirectX 12 host for Dear ImGui: device, swap chain, one texture upload, PNG capture of a frame.
#pragma once
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include "imgui.h"
#include "backends/imgui_impl_dx12.h"

using Microsoft::WRL::ComPtr;

class Gfx {
public:
    static const int kFrames = 2;
    bool init(HWND hwnd);
    void shutdown();
    void resize(UINT w, UINT h);
    void beginFrame();
    // renders the ImGui draw data and presents; shotPath non empty: also saves the frame as PNG
    void endFrame(const std::wstring& shotPath = L"");
    // loads a PNG (WIC) into an ImGui texture; size returned in w, h
    ImTextureID loadPng(const std::wstring& path, int& w, int& h);

private:
    void createRtvs();
    void waitIdle();
    bool savePng(ID3D12Resource* readback, UINT pitch, UINT w, UINT h, const std::wstring& path);
    static void srvAlloc(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE*, D3D12_GPU_DESCRIPTOR_HANDLE*);
    static void srvFree(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE);

    HWND m_hwnd = nullptr;
    ComPtr<ID3D12Device> m_dev;
    ComPtr<ID3D12CommandQueue> m_queue;
    ComPtr<IDXGISwapChain3> m_swap;
    ComPtr<ID3D12Resource> m_bb[kFrames];
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap, m_srvHeap;
    ComPtr<ID3D12CommandAllocator> m_alloc;
    ComPtr<ID3D12GraphicsCommandList> m_cmd;
    ComPtr<ID3D12Fence> m_fence;
    std::vector<ComPtr<ID3D12Resource>> m_textures;
    HANDLE m_event = nullptr;
    UINT64 m_fenceValue = 0;
    UINT m_rtvSize = 0, m_srvSize = 0, m_w = 0, m_h = 0;
    int m_nextSrv = 0;
};
