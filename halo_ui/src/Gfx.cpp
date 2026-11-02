#include "Gfx.h"
#include "backends/imgui_impl_win32.h"
#include <wincodec.h>
#include <algorithm>

static Gfx* g_gfx = nullptr;
static const int kSrvSlots = 64;

void Gfx::srvAlloc(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* c, D3D12_GPU_DESCRIPTOR_HANDLE* g) {
    int s = g_gfx->m_nextSrv++;
    c->ptr = g_gfx->m_srvHeap->GetCPUDescriptorHandleForHeapStart().ptr + (SIZE_T)s * g_gfx->m_srvSize;
    g->ptr = g_gfx->m_srvHeap->GetGPUDescriptorHandleForHeapStart().ptr + (UINT64)s * g_gfx->m_srvSize;
}
void Gfx::srvFree(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE) {}

bool Gfx::init(HWND hwnd) {
    g_gfx = this;
    m_hwnd = hwnd;
    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_dev)))) return false;
    D3D12_COMMAND_QUEUE_DESC qd = {};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(m_dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&m_queue)))) return false;

    RECT rc;
    GetClientRect(hwnd, &rc);
    m_w = std::max<LONG>(1, rc.right - rc.left);
    m_h = std::max<LONG>(1, rc.bottom - rc.top);
    ComPtr<IDXGIFactory4> f;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&f)))) return false;
    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.BufferCount = kFrames;
    sd.Width = m_w;
    sd.Height = m_h;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.SampleDesc.Count = 1;
    ComPtr<IDXGISwapChain1> sc;
    if (FAILED(f->CreateSwapChainForHwnd(m_queue.Get(), hwnd, &sd, nullptr, nullptr, &sc))) return false;
    sc.As(&m_swap);
    f->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    D3D12_DESCRIPTOR_HEAP_DESC rd = {};
    rd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rd.NumDescriptors = kFrames;
    m_dev->CreateDescriptorHeap(&rd, IID_PPV_ARGS(&m_rtvHeap));
    m_rtvSize = m_dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_DESCRIPTOR_HEAP_DESC hd = {};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = kSrvSlots;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    m_dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_srvHeap));
    m_srvSize = m_dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    createRtvs();

    m_dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_alloc));
    m_dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_alloc.Get(), nullptr, IID_PPV_ARGS(&m_cmd));
    m_cmd->Close();
    m_dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    m_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX12_InitInfo ii;
    ii.Device = m_dev.Get();
    ii.CommandQueue = m_queue.Get();
    ii.NumFramesInFlight = kFrames;
    ii.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    ii.DSVFormat = DXGI_FORMAT_UNKNOWN;
    ii.SrvDescriptorHeap = m_srvHeap.Get();
    ii.SrvDescriptorAllocFn = &Gfx::srvAlloc;
    ii.SrvDescriptorFreeFn = &Gfx::srvFree;
    return ImGui_ImplDX12_Init(&ii);
}

void Gfx::createRtvs() {
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (int i = 0; i < kFrames; ++i) {
        m_swap->GetBuffer(i, IID_PPV_ARGS(&m_bb[i]));
        m_dev->CreateRenderTargetView(m_bb[i].Get(), nullptr, h);
        h.ptr += m_rtvSize;
    }
}

void Gfx::waitIdle() {
    if (!m_queue) return;
    m_queue->Signal(m_fence.Get(), ++m_fenceValue);
    if (m_fence->GetCompletedValue() < m_fenceValue) {
        m_fence->SetEventOnCompletion(m_fenceValue, m_event);
        WaitForSingleObject(m_event, INFINITE);
    }
}

void Gfx::resize(UINT w, UINT h) {
    if (!m_swap || w == 0 || h == 0 || (w == m_w && h == m_h)) return;
    waitIdle();
    for (auto& b : m_bb) b.Reset();
    m_swap->ResizeBuffers(kFrames, w, h, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    m_w = w;
    m_h = h;
    createRtvs();
}

void Gfx::shutdown() {
    waitIdle();
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    if (m_event) CloseHandle(m_event);
}

void Gfx::beginFrame() {
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

static void barrier(ID3D12GraphicsCommandList* cmd, ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b) {
    D3D12_RESOURCE_BARRIER br = {};
    br.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    br.Transition.pResource = r;
    br.Transition.StateBefore = a;
    br.Transition.StateAfter = b;
    br.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(1, &br);
}

void Gfx::endFrame(const std::wstring& shotPath) {
    ImGui::Render();
    waitIdle();   // simple: one frame in flight
    m_alloc->Reset();
    m_cmd->Reset(m_alloc.Get(), nullptr);
    UINT i = m_swap->GetCurrentBackBufferIndex();
    ID3D12Resource* bb = m_bb[i].Get();
    barrier(m_cmd.Get(), bb, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += (SIZE_T)i * m_rtvSize;
    const float clear[4] = {0.051f, 0.063f, 0.075f, 1};
    m_cmd->ClearRenderTargetView(rtv, clear, 0, nullptr);
    m_cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    ID3D12DescriptorHeap* heaps[] = {m_srvHeap.Get()};
    m_cmd->SetDescriptorHeaps(1, heaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_cmd.Get());

    ComPtr<ID3D12Resource> readback;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {};
    if (!shotPath.empty()) {
        D3D12_RESOURCE_DESC d = bb->GetDesc();
        UINT64 total = 0;
        m_dev->GetCopyableFootprints(&d, 0, 1, 0, &fp, nullptr, nullptr, &total);
        D3D12_HEAP_PROPERTIES hp = {D3D12_HEAP_TYPE_READBACK};
        D3D12_RESOURCE_DESC bd = {};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bd.Width = total;
        bd.Height = bd.DepthOrArraySize = bd.MipLevels = 1;
        bd.SampleDesc.Count = 1;
        bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        m_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback));
        barrier(m_cmd.Get(), bb, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
        dst.pResource = readback.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = fp;
        src.pResource = bb;
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        m_cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        barrier(m_cmd.Get(), bb, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
    } else {
        barrier(m_cmd.Get(), bb, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    }
    m_cmd->Close();
    ID3D12CommandList* l[] = {m_cmd.Get()};
    m_queue->ExecuteCommandLists(1, l);
    if (readback) {
        waitIdle();
        savePng(readback.Get(), fp.Footprint.RowPitch, m_w, m_h, shotPath);
    }
    m_swap->Present(1, 0);
}

bool Gfx::savePng(ID3D12Resource* rb, UINT pitch, UINT w, UINT h, const std::wstring& path) {
    uint8_t* p = nullptr;
    if (FAILED(rb->Map(0, nullptr, (void**)&p))) return false;
    std::vector<uint8_t> bgra((size_t)w * h * 4);
    for (UINT y = 0; y < h; ++y)
        for (UINT x = 0; x < w; ++x) {
            const uint8_t* s = p + (size_t)y * pitch + x * 4;
            uint8_t* d = &bgra[((size_t)y * w + x) * 4];
            d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = 255;
        }
    rb->Unmap(0, nullptr);
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return false;
    ComPtr<IWICStream> st;
    wic->CreateStream(&st);
    if (FAILED(st->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) return false;
    ComPtr<IWICBitmapEncoder> enc;
    wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
    enc->Initialize(st.Get(), WICBitmapEncoderNoCache);
    ComPtr<IWICBitmapFrameEncode> fr;
    enc->CreateNewFrame(&fr, nullptr);
    fr->Initialize(nullptr);
    fr->SetSize(w, h);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    fr->SetPixelFormat(&fmt);
    fr->WritePixels(h, w * 4, (UINT)bgra.size(), bgra.data());
    fr->Commit();
    enc->Commit();
    return true;
}

ImTextureID Gfx::loadPng(const std::wstring& path, int& w, int& h) {
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return (ImTextureID)0;
    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &dec))) return (ImTextureID)0;
    ComPtr<IWICBitmapFrameDecode> fr;
    dec->GetFrame(0, &fr);
    ComPtr<IWICFormatConverter> conv;
    wic->CreateFormatConverter(&conv);
    conv->Initialize(fr.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);
    UINT uw, uh;
    conv->GetSize(&uw, &uh);
    std::vector<uint8_t> rgba((size_t)uw * uh * 4);
    conv->CopyPixels(nullptr, uw * 4, (UINT)rgba.size(), rgba.data());
    w = (int)uw;
    h = (int)uh;

    D3D12_HEAP_PROPERTIES hp = {D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC d = {};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = uw;
    d.Height = uh;
    d.DepthOrArraySize = d.MipLevels = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    ComPtr<ID3D12Resource> tex, up;
    m_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex));
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {};
    UINT64 total = 0;
    m_dev->GetCopyableFootprints(&d, 0, 1, 0, &fp, nullptr, nullptr, &total);
    D3D12_HEAP_PROPERTIES uhp = {D3D12_HEAP_TYPE_UPLOAD};
    D3D12_RESOURCE_DESC bd = {};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = total;
    bd.Height = bd.DepthOrArraySize = bd.MipLevels = 1;
    bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    m_dev->CreateCommittedResource(&uhp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&up));
    uint8_t* p = nullptr;
    up->Map(0, nullptr, (void**)&p);
    for (UINT y = 0; y < uh; ++y) memcpy(p + fp.Offset + (size_t)y * fp.Footprint.RowPitch, rgba.data() + (size_t)y * uw * 4, (size_t)uw * 4);
    up->Unmap(0, nullptr);
    waitIdle();
    m_alloc->Reset();
    m_cmd->Reset(m_alloc.Get(), nullptr);
    D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
    dst.pResource = tex.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.pResource = up.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = fp;
    m_cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    barrier(m_cmd.Get(), tex.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_cmd->Close();
    ID3D12CommandList* l[] = {m_cmd.Get()};
    m_queue->ExecuteCommandLists(1, l);
    waitIdle();

    D3D12_CPU_DESCRIPTOR_HANDLE c;
    D3D12_GPU_DESCRIPTOR_HANDLE g;
    srvAlloc(nullptr, &c, &g);
    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    m_dev->CreateShaderResourceView(tex.Get(), &sv, c);
    m_textures.push_back(tex);
    return (ImTextureID)g.ptr;
}
