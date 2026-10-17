#include "ImGuiLayer.h"
#include "backends/imgui_impl_dx12.h"
#include "backends/imgui_impl_win32.h"

namespace app {

static const int kSrvSlots = 256;
static ImGuiLayer* g_layer = nullptr;

// --------------------------------------------------------------------------- descriptors

bool ImGuiLayer::allocSlot(int& slot) {
    for (int i = 0; i < kSrvSlots; ++i)
        if (!m_used[i]) { m_used[i] = true; slot = i; return true; }
    return false;
}

D3D12_CPU_DESCRIPTOR_HANDLE ImGuiLayer::cpu(int slot) const {
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += (SIZE_T)slot * m_srvSize;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE ImGuiLayer::gpu(int slot) const {
    D3D12_GPU_DESCRIPTOR_HANDLE h = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += (UINT64)slot * m_srvSize;
    return h;
}

void ImGuiLayer::srvAlloc(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* c, D3D12_GPU_DESCRIPTOR_HANDLE* g) {
    int s = 0;
    g_layer->allocSlot(s);
    *c = g_layer->cpu(s);
    *g = g_layer->gpu(s);
}

void ImGuiLayer::srvFree(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE c, D3D12_GPU_DESCRIPTOR_HANDLE) {
    int s = (int)((c.ptr - g_layer->m_srvHeap->GetCPUDescriptorHandleForHeapStart().ptr) / g_layer->m_srvSize);
    if (s >= 0 && s < kSrvSlots) g_layer->m_used[s] = false;
}

void ImGuiLayer::createRtvs() {
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (uint32_t i = 0; i < vp3d::Renderer::kBackBuffers; ++i) {
        m_r->device()->CreateRenderTargetView(m_r->backBuffer(i), nullptr, h);
        h.ptr += m_rtvSize;
    }
}

// --------------------------------------------------------------------------- init

bool ImGuiLayer::init(HWND hwnd, vp3d::Renderer& r, float dpiScale) {
    g_layer = this;
    m_r = &r;
    dpi = dpiScale;
    ID3D12Device* dev = r.device();

    D3D12_DESCRIPTOR_HEAP_DESC hd = {};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = kSrvSlots;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_srvHeap)))) return false;
    m_srvSize = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_used.assign(kSrvSlots, false);
    allocSlot(m_viewportSlot);

    D3D12_DESCRIPTOR_HEAP_DESC rd = {};
    rd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rd.NumDescriptors = vp3d::Renderer::kBackBuffers;
    if (FAILED(dev->CreateDescriptorHeap(&rd, IID_PPV_ARGS(&m_rtvHeap)))) return false;
    m_rtvSize = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    createRtvs();

    if (FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_alloc)))) return false;
    if (FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_alloc.Get(), nullptr, IID_PPV_ARGS(&m_cmd)))) return false;
    m_cmd->Close();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    io.IniFilename = "wzx_editor_layout.ini";

    // Fonts: Segoe UI (regular / semibold / small) with Segoe Fluent Icons merged for the icon glyphs.
    float size = 15.0f * dpi;
    auto addFont = [&](const char* file, float px) -> ImFont* {
        ImFont* f = io.Fonts->AddFontFromFileTTF(file, px);
        if (!f) return nullptr;
        static const ImWchar iconRange[] = {0xE700, 0xF8FF, 0};
        ImFontConfig cfg;
        cfg.MergeMode = true;
        cfg.GlyphOffset = ImVec2(0, 2.0f * dpi);
        if (!io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\SegoeIcons.ttf", px, &cfg, iconRange))
            io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segmdl2.ttf", px, &cfg, iconRange);
        return f;
    };
    if (!addFont("C:\\Windows\\Fonts\\segoeui.ttf", size)) io.Fonts->AddFontDefault();
    fontBold = addFont("C:\\Windows\\Fonts\\seguisb.ttf", size);
    fontSmall = addFont("C:\\Windows\\Fonts\\segoeui.ttf", 13.0f * dpi);
    if (!fontBold) fontBold = io.Fonts->Fonts[0];
    if (!fontSmall) fontSmall = io.Fonts->Fonts[0];


    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX12_InitInfo ii;
    ii.Device = dev;
    ii.CommandQueue = r.queue();
    ii.NumFramesInFlight = vp3d::Renderer::kBackBuffers;
    ii.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    ii.DSVFormat = DXGI_FORMAT_UNKNOWN;
    ii.SrvDescriptorHeap = m_srvHeap.Get();
    ii.SrvDescriptorAllocFn = &ImGuiLayer::srvAlloc;
    ii.SrvDescriptorFreeFn = &ImGuiLayer::srvFree;
    return ImGui_ImplDX12_Init(&ii);
}

void ImGuiLayer::shutdown() {
    if (!ImGui::GetCurrentContext()) return;
    m_r->waitIdle();
    m_textures.clear();
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

void ImGuiLayer::onResize() { createRtvs(); }

void ImGuiLayer::newFrame() {
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    if (beforeNewFrame) beforeNewFrame();
    ImGui::NewFrame();
}

// --------------------------------------------------------------------------- textures

ImTextureID ImGuiLayer::viewportTexture() {
    ID3D12Resource* t = m_r->outputTexture();
    if (!t) return (ImTextureID)0;
    D3D12_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Texture2D.MipLevels = 1;
    m_r->device()->CreateShaderResourceView(t, &sd, cpu(m_viewportSlot));
    return (ImTextureID)gpu(m_viewportSlot).ptr;
}

ImTextureID ImGuiLayer::createTexture(const uint8_t* rgba, uint32_t w, uint32_t h) {
    ID3D12Device* dev = m_r->device();
    Tex t;
    if (!allocSlot(t.slot)) return (ImTextureID)0;
    D3D12_HEAP_PROPERTIES hp = {D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC d = {};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = w;
    d.Height = h;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&t.res)))) {
        m_used[t.slot] = false;
        return (ImTextureID)0;
    }
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {};
    UINT64 total = 0;
    dev->GetCopyableFootprints(&d, 0, 1, 0, &fp, nullptr, nullptr, &total);
    D3D12_HEAP_PROPERTIES up = {D3D12_HEAP_TYPE_UPLOAD};
    D3D12_RESOURCE_DESC bd = {};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = total;
    bd.Height = 1;
    bd.DepthOrArraySize = 1;
    bd.MipLevels = 1;
    bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> upload;
    if (FAILED(dev->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)))) {
        m_used[t.slot] = false;
        return (ImTextureID)0;
    }
    uint8_t* p = nullptr;
    upload->Map(0, nullptr, (void**)&p);
    for (uint32_t y = 0; y < h; ++y) memcpy(p + fp.Offset + (size_t)y * fp.Footprint.RowPitch, rgba + (size_t)y * w * 4, (size_t)w * 4);
    upload->Unmap(0, nullptr);

    m_r->waitIdle();
    m_alloc->Reset();
    m_cmd->Reset(m_alloc.Get(), nullptr);
    D3D12_TEXTURE_COPY_LOCATION dl = {}, sl = {};
    dl.pResource = t.res.Get();
    dl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    sl.pResource = upload.Get();
    sl.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    sl.PlacedFootprint = fp;
    m_cmd->CopyTextureRegion(&dl, 0, 0, 0, &sl, nullptr);
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = t.res.Get();
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_cmd->ResourceBarrier(1, &b);
    m_cmd->Close();
    ID3D12CommandList* l[] = {m_cmd.Get()};
    m_r->queue()->ExecuteCommandLists(1, l);
    m_r->waitIdle();

    D3D12_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Texture2D.MipLevels = 1;
    dev->CreateShaderResourceView(t.res.Get(), &sd, cpu(t.slot));
    ImTextureID id = (ImTextureID)gpu(t.slot).ptr;
    m_textures.push_back(t);
    return id;
}

void ImGuiLayer::releaseTexture(ImTextureID id) {
    for (size_t i = 0; i < m_textures.size(); ++i)
        if ((ImTextureID)gpu(m_textures[i].slot).ptr == id) {
            m_r->waitIdle();
            m_used[m_textures[i].slot] = false;
            m_textures.erase(m_textures.begin() + i);
            return;
        }
}

// --------------------------------------------------------------------------- frame

void ImGuiLayer::render(bool viewportTextureInUse) {
    ImGui::Render();
    m_alloc->Reset();
    m_cmd->Reset(m_alloc.Get(), nullptr);
    uint32_t bbi = m_r->backBufferIndex();
    ID3D12Resource* bb = m_r->backBuffer(bbi);
    auto barrier = [&](ID3D12Resource* res, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b) {
        D3D12_RESOURCE_BARRIER br = {};
        br.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        br.Transition.pResource = res;
        br.Transition.StateBefore = a;
        br.Transition.StateAfter = b;
        br.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_cmd->ResourceBarrier(1, &br);
    };
    barrier(bb, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += (SIZE_T)bbi * m_rtvSize;
    const float clear[4] = {0.055f, 0.062f, 0.075f, 1.0f};
    m_cmd->ClearRenderTargetView(rtv, clear, 0, nullptr);
    m_cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    ID3D12DescriptorHeap* heaps[] = {m_srvHeap.Get()};
    m_cmd->SetDescriptorHeaps(1, heaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_cmd.Get());
    if (viewportTextureInUse && m_r->outputTexture())
        barrier(m_r->outputTexture(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    barrier(bb, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    m_cmd->Close();
    ID3D12CommandList* l[] = {m_cmd.Get()};
    m_r->queue()->ExecuteCommandLists(1, l);
}

} // namespace app
