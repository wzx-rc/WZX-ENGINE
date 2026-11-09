#include "Renderer.h"
#include <algorithm>
#include <unordered_map>
#include "shaders/Shared.h"
#include <cstring>
#include <fstream>
#include <functional>

namespace vp3d {

namespace {

D3D12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE t) {
    D3D12_HEAP_PROPERTIES p = {};
    p.Type = t;
    return p;
}

D3D12_RESOURCE_DESC bufferDesc(uint64_t size, D3D12_RESOURCE_FLAGS flags) {
    D3D12_RESOURCE_DESC d = {};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = size;
    d.Height = 1;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    d.Flags = flags;
    return d;
}

bool readFile(const std::wstring& path, std::vector<char>& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    out.resize((size_t)f.tellg());
    f.seekg(0);
    f.read(out.data(), out.size());
    return true;
}

std::string narrow(const std::wstring& w) {
    std::string s;
    for (wchar_t c : w) s.push_back(c < 128 ? (char)c : '?');
    return s;
}

// Slots of the UAV texture table (must match shaders).
enum TexSlot { TEX_ACCUM = 0, TEX_ALBEDO = 1, TEX_NORMALDEPTH = 2, TEX_DENOISE_A = 3, TEX_DENOISE_B = 4, TEX_OUTPUT = 5 };

} // namespace

Renderer::Buffer Renderer::createBuffer(uint64_t size, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_FLAGS flags,
                                        D3D12_RESOURCE_STATES state) {
    Buffer b;
    b.size = size = std::max<uint64_t>(size, 256);
    auto hp = heapProps(heap);
    auto bd = bufferDesc(size, flags);
    if (heap == D3D12_HEAP_TYPE_UPLOAD) state = D3D12_RESOURCE_STATE_GENERIC_READ;
    if (heap == D3D12_HEAP_TYPE_READBACK) state = D3D12_RESOURCE_STATE_COPY_DEST;
    m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, state, nullptr, IID_PPV_ARGS(&b.res));
    if (heap == D3D12_HEAP_TYPE_UPLOAD) {
        D3D12_RANGE r = {0, 0};
        b.res->Map(0, &r, &b.mapped);
    }
    return b;
}

void Renderer::ensureUpload(Buffer& b, uint64_t size) {
    if (b.res && b.size >= size) return;
    uint64_t cap = std::max<uint64_t>(size * 3 / 2, 4096);
    b = createBuffer(cap, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
}

bool Renderer::init(HWND hwnd, uint32_t width, uint32_t height, const std::wstring& shaderDir, std::string* error) {
    m_shaderDir = shaderDir;
    UINT factoryFlags = 0;
    char dbg[8] = {};
    if (GetEnvironmentVariableA("VP3D_DEBUG", dbg, sizeof(dbg)) > 0) {
        ComPtr<ID3D12Debug> d;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&d)))) {   // (fails without the Windows "Graphics Tools")
            d->EnableDebugLayer();
            factoryFlags = DXGI_CREATE_FACTORY_DEBUG;
        }
    }
    if (FAILED(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&m_factory)))) {
        if (error) *error = "CreateDXGIFactory2 failed";
        return false;
    }
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; m_factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 ad;
        adapter->GetDesc1(&ad);
        if (ad.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&m_device)))) {
            D3D12_FEATURE_DATA_D3D12_OPTIONS5 o5 = {};
            m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &o5, sizeof(o5));
            if (o5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1) {
                m_adapterName = narrow(ad.Description);
                break;
            }
            m_device.Reset();
        }
    }
    if (!m_device) {
        if (error) *error = "No GPU with DirectX Raytracing tier 1.1 found";
        return false;
    }
    if (factoryFlags) m_device.As(&m_infoQueue);   // validation layer running: its messages are kept for the self-test

    D3D12_COMMAND_QUEUE_DESC qd = {};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    m_device->CreateCommandQueue(&qd, IID_PPV_ARGS(&m_queue));
    m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_alloc));
    m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_alloc.Get(), nullptr, IID_PPV_ARGS(&m_cmd));
    m_cmd->Close();
    m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    m_width = std::max(1u, width);
    m_height = std::max(1u, height);
    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = m_width;
    sd.Height = m_height;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = kBackBuffers;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<IDXGISwapChain1> sc1;
    if (FAILED(m_factory->CreateSwapChainForHwnd(m_queue.Get(), hwnd, &sd, nullptr, nullptr, &sc1))) {
        if (error) *error = "CreateSwapChainForHwnd failed";
        return false;
    }
    m_factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    sc1.As(&m_swapChain);
    for (uint32_t i = 0; i < kBackBuffers; ++i) m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i]));

    D3D12_DESCRIPTOR_HEAP_DESC hd = {};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = kViewHeapBase + kMaxViews * kViewHeapStride;  // (extra views last) + shadow map SRV + material textures + DDGI UAVs + history UAVs + HDRI SRV + capture UAVs + probe SRVs + screen GI, sun shadow and reflection views + selection, depth, bevel
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    m_device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_heap));
    m_descSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    {   // no HDRI yet: null texture view and a small zeroed table buffer
        D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
        sv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sv.Texture2D.MipLevels = 1;
        D3D12_CPU_DESCRIPTOR_HANDLE h = m_heap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += (SIZE_T)kEnvHeapSlot * m_descSize;
        m_device->CreateShaderResourceView(nullptr, &sv, h);
        {   // no selection coverage yet (created with the viewport targets)
            D3D12_SHADER_RESOURCE_VIEW_DESC cv = sv;
            cv.Format = DXGI_FORMAT_R8_UNORM;
            D3D12_CPU_DESCRIPTOR_HANDLE ch = m_heap->GetCPUDescriptorHandleForHeapStart();
            ch.ptr += (SIZE_T)kSelCovSlot * m_descSize;
            m_device->CreateShaderResourceView(nullptr, &cv, ch);
            cv.Format = DXGI_FORMAT_R32_FLOAT;
            ch.ptr += m_descSize;   // t9: no depth yet
            m_device->CreateShaderResourceView(nullptr, &cv, ch);
        }
        // no probe grid yet: null views in the probe SRV slots of the raster pass
        // (and the screen GI, sun shadow, reflection and centre ones of the Radiance Cascades modes)
        for (uint32_t k = 0; k < 7; ++k) {
            D3D12_CPU_DESCRIPTOR_HANDLE ph = m_heap->GetCPUDescriptorHandleForHeapStart();
            ph.ptr += (SIZE_T)(kProbeSrvSlot + k) * m_descSize;
            m_device->CreateShaderResourceView(nullptr, &sv, ph);
        }
        D3D12_UNORDERED_ACCESS_VIEW_DESC uv = {};
        uv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        uv.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        D3D12_CPU_DESCRIPTOR_HANDLE uh = m_heap->GetCPUDescriptorHandleForHeapStart();
        for (uint32_t k = 0; k < 5; ++k) {   // screen GI, sun shadow and reflection UAVs (raw, filtered)
            D3D12_CPU_DESCRIPTOR_HANDLE uh2 = uh;
            uh2.ptr += (SIZE_T)(kScreenGIUavSlot + k) * m_descSize;
            m_device->CreateUnorderedAccessView(nullptr, nullptr, &uv, uh2);
        }
        m_envDist = createBuffer(256, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
        memset(m_envDist.mapped, 0, 256);
    }
    {   // 8x MSAA for the AA quality settings of the GI modes (HDR colour and depth formats)
        auto levels = [&](DXGI_FORMAT f) {
            D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS q = {};
            q.Format = f;
            q.SampleCount = 8;
            m_device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &q, sizeof(q));
            return q.NumQualityLevels;
        };
        m_msaa8 = levels(DXGI_FORMAT_R16G16B16A16_FLOAT) > 0 && levels(DXGI_FORMAT_D32_FLOAT) > 0;
    }
    if (!createPipelines(error)) return false;
    if (!createRasterPipelines(error)) return false;

    m_constants = createBuffer(256 * 4, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
    m_giConstants = createBuffer(256, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
    m_rcConstants = createBuffer((sizeof(gpu::RCConstants) + 255) & ~255ull, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE,
                                 D3D12_RESOURCE_STATE_GENERIC_READ);
    m_rcsConstants = createBuffer((sizeof(gpu::RCSConstants) + 255) & ~255ull, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE,
                                  D3D12_RESOURCE_STATE_GENERIC_READ);
    m_srcConstants = createBuffer((sizeof(gpu::SplitRCConstants) + 255) & ~255ull, D3D12_HEAP_TYPE_UPLOAD,
                                  D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);

    D3D12_QUERY_HEAP_DESC qhd = {};
    qhd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    qhd.Count = 64;   // 0 start, 1 end, 2 after DDGI probe rays, 3 after DDGI blend, 4 and 5 around Split RC
    m_device->CreateQueryHeap(&qhd, IID_PPV_ARGS(&m_queryHeap));
    m_queryReadback = createBuffer(512, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    UINT64 freq = 1;
    m_queue->GetTimestampFrequency(&freq);
    m_tsFreq = (double)freq;
    return true;
}

bool Renderer::createPipelines(std::string* error) {
    // Root signature shared by all compute passes.
    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    range.NumDescriptors = kTexCount;
    range.BaseShaderRegister = 0;

    D3D12_DESCRIPTOR_RANGE texRange = {};
    texRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    texRange.NumDescriptors = kMaxTextures;
    texRange.BaseShaderRegister = 0;
    texRange.RegisterSpace = 1;

    D3D12_DESCRIPTOR_RANGE giRange = {};
    giRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    giRange.NumDescriptors = kGITexCount;
    giRange.BaseShaderRegister = 6;

    D3D12_DESCRIPTOR_RANGE histRange = {};
    histRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    histRange.NumDescriptors = 2;
    histRange.BaseShaderRegister = 10;

    D3D12_DESCRIPTOR_RANGE envRange = {};
    envRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    envRange.NumDescriptors = 1;
    envRange.BaseShaderRegister = 7;

    D3D12_DESCRIPTOR_RANGE capRange = {};   // u12..u14 probe captures (raster DDGI)
    capRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    capRange.NumDescriptors = 3;
    capRange.BaseShaderRegister = 12;

    D3D12_DESCRIPTOR_RANGE screenRange = {};   // u17 screen GI texture (screen-space cascades)
    screenRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    screenRange.NumDescriptors = 1;
    screenRange.BaseShaderRegister = 17;

    D3D12_DESCRIPTOR_RANGE sunRange = {};   // u18..u21 ray traced sun shadow, reflections (raw, filtered)
    sunRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    sunRange.NumDescriptors = 4;
    sunRange.BaseShaderRegister = 18;


    D3D12_DESCRIPTOR_RANGE selRange = {};   // t8 coverage of the selection (tonemapper)
    selRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    selRange.NumDescriptors = 2;   // t8 coverage of the selection, t9 viewport depth (ambient occlusion)
    selRange.BaseShaderRegister = 8;

    D3D12_ROOT_PARAMETER p[24] = {};
    p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    p[0].Descriptor.ShaderRegister = 0;
    p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    p[1].Constants.ShaderRegister = 1;
    p[1].Constants.Num32BitValues = 4;
    for (int i = 0; i < 6; ++i) {
        p[2 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        p[2 + i].Descriptor.ShaderRegister = i;
    }
    p[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    p[8].DescriptorTable.NumDescriptorRanges = 1;
    p[8].DescriptorTable.pDescriptorRanges = &range;
    p[9].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    p[9].DescriptorTable.NumDescriptorRanges = 1;
    p[9].DescriptorTable.pDescriptorRanges = &texRange;
    p[10].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;   // b2 DDGI constants
    p[10].Descriptor.ShaderRegister = 2;
    p[11].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;   // u6..u9 DDGI textures
    p[11].DescriptorTable.NumDescriptorRanges = 1;
    p[11].DescriptorTable.pDescriptorRanges = &giRange;
    p[12].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;   // u10, u11 temporal history
    p[12].DescriptorTable.NumDescriptorRanges = 1;
    p[12].DescriptorTable.pDescriptorRanges = &histRange;
    p[13].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;   // t6 HDRI sampling tables
    p[13].Descriptor.ShaderRegister = 6;
    p[14].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;   // t7 HDRI texture
    p[14].DescriptorTable.NumDescriptorRanges = 1;
    p[14].DescriptorTable.pDescriptorRanges = &envRange;
    p[15].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;   // u12 probe captures (raster DDGI)
    p[15].DescriptorTable.NumDescriptorRanges = 1;
    p[15].DescriptorTable.pDescriptorRanges = &capRange;
    p[16].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;   // b3 Radiance Cascades constants
    p[16].Descriptor.ShaderRegister = 3;
    p[17].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;   // u15 Radiance Cascades buffer
    p[17].Descriptor.ShaderRegister = 15;
    p[18].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;   // b4 screen-space cascades constants
    p[18].Descriptor.ShaderRegister = 4;
    p[19].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;   // u16 screen-space cascades buffer
    p[19].Descriptor.ShaderRegister = 16;
    p[20].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;   // u17 screen GI texture
    p[20].DescriptorTable.NumDescriptorRanges = 1;
    p[20].DescriptorTable.pDescriptorRanges = &screenRange;
    p[21].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;   // u18, u19 sun shadow
    p[21].DescriptorTable.NumDescriptorRanges = 1;
    p[21].DescriptorTable.pDescriptorRanges = &sunRange;
    p[22].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;   // u22 Split RC history (previous frame)
    p[22].Descriptor.ShaderRegister = 22;
    p[23].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;   // t8 coverage of the selection
    p[23].DescriptorTable.NumDescriptorRanges = 1;
    p[23].DescriptorTable.pDescriptorRanges = &selRange;

    D3D12_STATIC_SAMPLER_DESC ss = {};   // gLinearWrap (s0) of the path tracer
    ss.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    ss.AddressU = ss.AddressV = ss.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    ss.MaxLOD = D3D12_FLOAT32_MAX;
    ss.ShaderRegister = 0;

    D3D12_ROOT_SIGNATURE_DESC rsd = {};
    rsd.NumParameters = 24;
    rsd.pParameters = p;
    rsd.NumStaticSamplers = 1;
    rsd.pStaticSamplers = &ss;
    ComPtr<ID3DBlob> blob, err;
    if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err))) {
        if (error) *error = err ? (const char*)err->GetBufferPointer() : "root signature";
        return false;
    }
    m_device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_rootSig));

    auto make = [&](const wchar_t* file, ComPtr<ID3D12PipelineState>& pso) -> bool {
        std::vector<char> code;
        if (!readFile(m_shaderDir + L"\\" + file, code)) {
            if (error) *error = "Missing shader: " + narrow(m_shaderDir + L"\\" + file);
            return false;
        }
        D3D12_COMPUTE_PIPELINE_STATE_DESC d = {};
        d.pRootSignature = m_rootSig.Get();
        d.CS = {code.data(), code.size()};
        if (FAILED(m_device->CreateComputePipelineState(&d, IID_PPV_ARGS(&pso)))) {
            if (error) *error = "CreateComputePipelineState failed for " + narrow(file);
            return false;
        }
        return true;
    };
    {   // ambient occlusion of the raster viewport: optional
        std::string ignore;
        std::string* keep = error;
        error = &ignore;
        if (!make(L"SSAO.cso", m_psoSSAO) || !make(L"SSAO_MS.cso", m_psoSSAOMS) || !make(L"SSAOBlur.cso", m_psoSSAOBlur)) { m_psoSSAO.Reset(); m_psoSSAOMS.Reset(); m_psoSSAOBlur.Reset(); }
        if (!make(L"OidnPack.cso", m_psoOidn)) m_psoOidn.Reset();   // Open Image Denoise passes: optional too
        error = keep;
    }
    return make(L"PathTrace.cso", m_psoTrace) && make(L"Denoise.cso", m_psoDenoise) && make(L"Tonemap.cso", m_psoTonemap) && make(L"GIFade.cso", m_psoGIFade) &&
           make(L"DDGIUpdate.cso", m_psoGIUpdate) && make(L"DDGIRaster.cso", m_psoGIRaster) &&
           make(L"DDGIBlend.cso", m_psoGIBlend) && make(L"GIShade.cso", m_psoGIShade) &&
           make(L"RCVisibility.cso", m_psoRCVisibility) && make(L"RCTrace.cso", m_psoRCTrace) &&
           make(L"RCGather.cso", m_psoRCGather) && make(L"RCSProbes.cso", m_psoRCSProbes) &&
           make(L"RCSTrace.cso", m_psoRCSTrace) && make(L"RCSProbeSH.cso", m_psoRCSProbeSH) &&
           make(L"RCSGather.cso", m_psoRCSGather) && make(L"SunShadowTrace.cso", m_psoSunTrace) &&
           make(L"SunShadowFilter.cso", m_psoSunFilter) && make(L"ReflectTrace.cso", m_psoReflTrace) &&
           make(L"ReflectFilter.cso", m_psoReflFilter) && make(L"SRCClear.cso", m_psoSRCClear) &&
           make(L"SRCInsert.cso", m_psoSRCInsert) && make(L"SRCAlloc.cso", m_psoSRCAlloc) &&
           make(L"SRCCount.cso", m_psoSRCCount) && make(L"SRCTrace.cso", m_psoSRCTrace) &&
           make(L"SRCMerge.cso", m_psoSRCMerge) && make(L"SRCIrradiance.cso", m_psoSRCIrradiance) &&
           make(L"SRCFilter.cso", m_psoSRCFilter) &&
           make(L"SRCGather.cso", m_psoSRCGather) && make(L"ResolveTM.cso", m_psoResolve);
}

void Renderer::createTargets(uint32_t w, uint32_t h) {
    m_rtW = w;
    m_rtH = h;
    const DXGI_FORMAT fmts[kTexCount] = {DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT,
                                         DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT,
                                         DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R8G8B8A8_UNORM};
    auto hp = heapProps(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_heap->GetCPUDescriptorHandleForHeapStart();
    for (int i = 0; i < kTexCount; ++i) {
        D3D12_RESOURCE_DESC d = {};
        d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        d.Width = w;
        d.Height = h;
        d.DepthOrArraySize = 1;
        d.MipLevels = 1;
        d.Format = fmts[i];
        d.SampleDesc.Count = 1;
        d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        if (i == TEX_ALBEDO) d.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        m_tex[i].Reset();
        m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                          nullptr, IID_PPV_ARGS(&m_tex[i]));
        D3D12_UNORDERED_ACCESS_VIEW_DESC u = {};
        u.Format = fmts[i];
        u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        D3D12_CPU_DESCRIPTOR_HANDLE hnd = cpu;
        hnd.ptr += (SIZE_T)i * m_descSize;
        m_device->CreateUnorderedAccessView(m_tex[i].Get(), nullptr, &u, hnd);
    }
    // History textures (UAV slots u10, u11): copies of ACCUM and NORMAL, written at the end of each frame.
    const TexSlot histSrc[2] = {TEX_ACCUM, TEX_NORMALDEPTH};
    const uint32_t histHeap = kGIHeapBase + kGITexCount;
    for (int i = 0; i < 2; ++i) {
        D3D12_RESOURCE_DESC d = {};
        d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        d.Width = w;
        d.Height = h;
        d.DepthOrArraySize = 1;
        d.MipLevels = 1;
        d.Format = fmts[histSrc[i]];
        d.SampleDesc.Count = 1;
        d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        m_hist[i].Reset();
        m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                          nullptr, IID_PPV_ARGS(&m_hist[i]));
        D3D12_UNORDERED_ACCESS_VIEW_DESC u = {};
        u.Format = d.Format;
        u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        D3D12_CPU_DESCRIPTOR_HANDLE hnd = cpu;
        hnd.ptr += (SIZE_T)(histHeap + i) * m_descSize;
        m_device->CreateUnorderedAccessView(m_hist[i].Get(), nullptr, &u, hnd);
    }
    m_historyValid = false;
    {   // screen GI texture of the screen-space cascades (UAV u17, SRV t7 of the raster pass)
        D3D12_RESOURCE_DESC d = {};
        d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        d.Width = w;
        d.Height = h;
        d.DepthOrArraySize = 1;
        d.MipLevels = 1;
        d.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        d.SampleDesc.Count = 1;
        d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        m_screenGI.Reset();
        m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                          IID_PPV_ARGS(&m_screenGI));
        D3D12_UNORDERED_ACCESS_VIEW_DESC u = {};
        u.Format = d.Format;
        u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        D3D12_CPU_DESCRIPTOR_HANDLE hnd = cpu;
        hnd.ptr += (SIZE_T)kScreenGIUavSlot * m_descSize;
        m_device->CreateUnorderedAccessView(m_screenGI.Get(), nullptr, &u, hnd);
        D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
        sv.Format = d.Format;
        sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sv.Texture2D.MipLevels = 1;
        hnd = cpu;
        hnd.ptr += (SIZE_T)kScreenGISrvSlot * m_descSize;
        m_device->CreateShaderResourceView(m_screenGI.Get(), &sv, hnd);
        m_screenGIOn = false;
    }
    {   // ray traced sun shadow (raw + depth + normal u18, filtered mask u19, raster t8) and reflections (u20, u21, t9)
        const DXGI_FORMAT fm[4] = {DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT,
                                   DXGI_FORMAT_R16G16B16A16_FLOAT};
        ComPtr<ID3D12Resource>* res[4] = {&m_sunShadowRaw, &m_sunShadow, &m_reflRaw, &m_refl};
        for (int k = 0; k < 4; ++k) {
            D3D12_RESOURCE_DESC d = {};
            d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            d.Width = w;
            d.Height = h;
            d.DepthOrArraySize = 1;
            d.MipLevels = 1;
            d.Format = fm[k];
            d.SampleDesc.Count = 1;
            d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            res[k]->Reset();
            m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                              IID_PPV_ARGS(&(*res[k])));
            D3D12_UNORDERED_ACCESS_VIEW_DESC u = {};
            u.Format = fm[k];
            u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            D3D12_CPU_DESCRIPTOR_HANDLE hnd = cpu;
            hnd.ptr += (SIZE_T)(kSunShadowUavSlot + k) * m_descSize;
            m_device->CreateUnorderedAccessView(res[k]->Get(), nullptr, &u, hnd);
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
        sv.Format = DXGI_FORMAT_R16_FLOAT;
        sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sv.Texture2D.MipLevels = 1;
        D3D12_CPU_DESCRIPTOR_HANDLE hnd = cpu;
        hnd.ptr += (SIZE_T)kSunShadowSrvSlot * m_descSize;
        m_device->CreateShaderResourceView(m_sunShadow.Get(), &sv, hnd);
        sv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        hnd = cpu;
        hnd.ptr += (SIZE_T)kReflSrvSlot * m_descSize;
        m_device->CreateShaderResourceView(m_refl.Get(), &sv, hnd);
        hnd = cpu;   // the raw sun trace: its .y is the depth of the surface the pixel centre sees (raster t10)
        hnd.ptr += (SIZE_T)kCenterSrvSlot * m_descSize;
        m_device->CreateShaderResourceView(m_sunShadowRaw.Get(), &sv, hnd);
    }
    createRasterTargets(w, h);
    m_sampleIndex = 0;
}

void Renderer::shutdown() {
    if (!m_device) return;
    waitIdle();
    if (m_fenceEvent) CloseHandle(m_fenceEvent);
    m_fenceEvent = nullptr;
}

void Renderer::resize(uint32_t width, uint32_t height) {
    if (!m_swapChain || width == 0 || height == 0) return;
    if (width == m_width && height == m_height) return;
    waitIdle();
    for (auto& b : m_backBuffers) b.Reset();
    m_swapChain->ResizeBuffers(kBackBuffers, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    for (uint32_t i = 0; i < kBackBuffers; ++i) m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i]));
    m_width = width;
    m_height = height;
}

void Renderer::beginCommands() {
    m_alloc->Reset();
    m_cmd->Reset(m_alloc.Get(), nullptr);
}

void Renderer::waitIdle() {
    if (!m_queue) return;
    ++m_fenceValue;
    m_queue->Signal(m_fence.Get(), m_fenceValue);
    if (m_fence->GetCompletedValue() < m_fenceValue) {
        m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent);
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
}

void Renderer::executeAndWait() {
    m_cmd->Close();
    ID3D12CommandList* l[] = {m_cmd.Get()};
    m_queue->ExecuteCommandLists(1, l);
    waitIdle();
}

void Renderer::transition(ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b) {
    D3D12_RESOURCE_BARRIER br = {};
    br.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    br.Transition.pResource = r;
    br.Transition.StateBefore = a;
    br.Transition.StateAfter = b;
    br.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_cmd->ResourceBarrier(1, &br);
}

void Renderer::uavBarrier(ID3D12Resource* r) {
    D3D12_RESOURCE_BARRIER br = {};
    br.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    br.UAV.pResource = r;
    m_cmd->ResourceBarrier(1, &br);
}

void Renderer::updateMeshVertices(int mesh, const std::vector<Vertex>& vertices) {
    if (mesh < 0 || mesh >= (int)m_meshes.size() || !m_vb.res) return;
    const MeshGpu& g = m_meshes[mesh];
    if (vertices.size() != g.vertexCount || vertices.empty()) return;
    std::vector<gpu::GpuVertex> verts;
    verts.reserve(vertices.size());
    for (const Vertex& v : vertices) verts.push_back({v.pos, v.nrm, v.uv});
    const uint64_t size = verts.size() * sizeof(gpu::GpuVertex);
    Buffer stage = createBuffer(size, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
    memcpy(stage.mapped, verts.data(), size);
    waitIdle();
    beginCommands();
    const D3D12_RESOURCE_STATES geoRead = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER;
    transition(m_vb.res.Get(), geoRead, D3D12_RESOURCE_STATE_COPY_DEST);
    m_cmd->CopyBufferRegion(m_vb.res.Get(), (uint64_t)g.vertexOffset * sizeof(gpu::GpuVertex), stage.res.Get(), 0, size);
    transition(m_vb.res.Get(), D3D12_RESOURCE_STATE_COPY_DEST, geoRead);
    executeAndWait();
}

void Renderer::setScene(const Scene& scene) {
    waitIdle();
    m_buildVersion = m_tlasVersion = 0;   // the instances point at the old BLAS
    m_historyValid = false;
    m_meshes.clear();
    m_sceneGeometryVersion = scene.geometryVersion;

    // Concatenate all meshes (+ the internal light quad) into one vertex/index buffer.
    std::vector<gpu::GpuVertex> verts;
    std::vector<uint32_t> inds;
    auto addMesh = [&](const MeshData& m) {
        MeshGpu g = {};
        g.indexOffset = (uint32_t)inds.size();
        g.vertexOffset = (uint32_t)verts.size();
        g.indexCount = (uint32_t)m.indices.size();
        g.vertexCount = (uint32_t)m.vertices.size();
        m.bounds(g.bmin, g.bmax);
        g.center = (g.bmin + g.bmax) * 0.5f;
        for (auto& v : m.vertices) g.radius = std::max(g.radius, length(v.pos - g.center));
        for (auto& v : m.vertices) verts.push_back({v.pos, v.nrm, v.uv});
        inds.insert(inds.end(), m.indices.begin(), m.indices.end());
        // wireframe display: the polygon edges of the mesh, else every triangle edge
        g.lineOffset = (uint32_t)inds.size();
        if (!m.edges.empty()) inds.insert(inds.end(), m.edges.begin(), m.edges.end());
        else
            for (size_t t = 0; t + 2 < m.indices.size(); t += 3)
                for (int k = 0; k < 3; ++k) { inds.push_back(m.indices[t + k]); inds.push_back(m.indices[t + (k + 1) % 3]); }
        g.lineCount = (uint32_t)inds.size() - g.lineOffset;
        // hard edges (edge outline): the positions welded, an edge with one face (open border) or two faces at more
        // than 35 degrees (as three.js EdgesGeometry(geo, 35) in the web build)
        g.hardOffset = (uint32_t)inds.size();
        {
            std::unordered_map<uint64_t, uint32_t> weld;
            std::vector<uint32_t> wid(m.vertices.size());
            for (size_t v = 0; v < m.vertices.size(); ++v) {
                const float3& p = m.vertices[v].pos;
                const uint64_t k = ((uint64_t)(std::llround(p.x * 1e3) & 0x1FFFFF) << 42) | ((uint64_t)(std::llround(p.y * 1e3) & 0x1FFFFF) << 21) |
                                   (uint64_t)(std::llround(p.z * 1e3) & 0x1FFFFF);
                wid[v] = weld.emplace(k, (uint32_t)v).first->second;
            }
            struct E { uint32_t a, b; float3 n; int faces; bool hard; };
            std::unordered_map<uint64_t, E> edges;
            const float cosHard = std::cos(radians(35.0f));
            for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
                const uint32_t i0 = m.indices[t], i1 = m.indices[t + 1], i2 = m.indices[t + 2];
                float3 n = cross(m.vertices[i1].pos - m.vertices[i0].pos, m.vertices[i2].pos - m.vertices[i0].pos);
                const float len = length(n);
                if (len < 1e-12f) continue;
                n = n / len;
                const uint32_t tri[3] = {i0, i1, i2};
                for (int k = 0; k < 3; ++k) {
                    uint32_t a = wid[tri[k]], b = wid[tri[(k + 1) % 3]];
                    if (a == b) continue;
                    const uint64_t key = a < b ? ((uint64_t)a << 32) | b : ((uint64_t)b << 32) | a;
                    auto it = edges.find(key);
                    if (it == edges.end()) edges.emplace(key, E{tri[k], tri[(k + 1) % 3], n, 1, false});
                    else { if (std::fabs(dot(it->second.n, n)) < cosHard) it->second.hard = true; ++it->second.faces; }
                }
            }
            for (const auto& e : edges)
                if (e.second.hard || e.second.faces == 1) { inds.push_back(e.second.a); inds.push_back(e.second.b); }
        }
        g.hardCount = (uint32_t)inds.size() - g.hardOffset;
        m_meshes.push_back(g);
    };
    for (auto& m : scene.meshes) addMesh(m);
    m_quadMesh = (int)m_meshes.size();
    addMesh(makeQuadXY());

    uint64_t vbSize = verts.size() * sizeof(gpu::GpuVertex), ibSize = inds.size() * sizeof(uint32_t);
    Buffer stageV = createBuffer(vbSize, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
    Buffer stageI = createBuffer(ibSize, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
    memcpy(stageV.mapped, verts.data(), vbSize);
    memcpy(stageI.mapped, inds.data(), ibSize);
    m_vb = createBuffer(vbSize, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    m_ib = createBuffer(ibSize, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);

    beginCommands();
    m_cmd->CopyBufferRegion(m_vb.res.Get(), 0, stageV.res.Get(), 0, vbSize);
    m_cmd->CopyBufferRegion(m_ib.res.Get(), 0, stageI.res.Get(), 0, ibSize);
    const D3D12_RESOURCE_STATES geoRead = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER;
    transition(m_vb.res.Get(), D3D12_RESOURCE_STATE_COPY_DEST, geoRead);
    transition(m_ib.res.Get(), D3D12_RESOURCE_STATE_COPY_DEST, geoRead);

    std::vector<Buffer> scratch;
    for (auto& g : m_meshes) {
        if (g.indexCount == 0) continue;
        D3D12_RAYTRACING_GEOMETRY_DESC geo = {};
        geo.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        // non-opaque glass instances are reported once to the shadow rays (ShadowTransmittance)
        geo.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE | D3D12_RAYTRACING_GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION;
        geo.Triangles.VertexBuffer.StartAddress = m_vb.va() + (uint64_t)g.vertexOffset * sizeof(gpu::GpuVertex);
        geo.Triangles.VertexBuffer.StrideInBytes = sizeof(gpu::GpuVertex);
        geo.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
        geo.Triangles.VertexCount = g.vertexCount;
        geo.Triangles.IndexBuffer = m_ib.va() + (uint64_t)g.indexOffset * 4;
        geo.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
        geo.Triangles.IndexCount = g.indexCount;

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS in = {};
        in.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        in.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        in.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        in.NumDescs = 1;
        in.pGeometryDescs = &geo;
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
        m_device->GetRaytracingAccelerationStructurePrebuildInfo(&in, &info);
        g.blas = createBuffer(info.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                              D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                              D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
        scratch.push_back(createBuffer(info.ScratchDataSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bd = {};
        bd.Inputs = in;
        bd.DestAccelerationStructureData = g.blas.va();
        bd.ScratchAccelerationStructureData = scratch.back().va();
        m_cmd->BuildRaytracingAccelerationStructure(&bd, 0, nullptr);
    }
    uavBarrier();
    executeAndWait();
    uploadTextures(scene);
    m_sampleIndex = 0;
}

// Descriptor table of the material textures (after the render targets and the shadow map in m_heap).
D3D12_GPU_DESCRIPTOR_HANDLE Renderer::textureTable() const {
    D3D12_GPU_DESCRIPTOR_HANDLE h = m_heap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += (UINT64)(kTexCount + 1) * m_descSize;
    return h;
}

// Uploads Scene::textures (RGBA8 sRGB) with a box-filtered mip chain into the texture table.
void Renderer::uploadTextures(const Scene& scene) {
    m_materialTex.clear();
    D3D12_CPU_DESCRIPTOR_HANDLE base = m_heap->GetCPUDescriptorHandleForHeapStart();
    base.ptr += (SIZE_T)(kTexCount + 1) * m_descSize;
    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    for (uint32_t i = 0; i < kMaxTextures; ++i) {
        D3D12_CPU_DESCRIPTOR_HANDLE h = base;
        h.ptr += (SIZE_T)i * m_descSize;
        m_device->CreateShaderResourceView(nullptr, &sv, h);   // null view
    }
    auto hp = heapProps(D3D12_HEAP_TYPE_DEFAULT);
    for (size_t ti = 0; ti < scene.textures.size() && ti < kMaxTextures; ++ti) {
        const Texture& t = scene.textures[ti];
        if (t.width == 0 || t.height == 0 || t.rgba.size() < (size_t)t.width * t.height * 4) {
            m_materialTex.push_back(nullptr);
            continue;
        }
        // mip chain on the CPU
        std::vector<std::vector<uint8_t>> mips(1, t.rgba);
        std::vector<uint32_t> mw(1, t.width), mh(1, t.height);
        while (mw.back() > 1 || mh.back() > 1) {
            uint32_t w0 = mw.back(), h0 = mh.back(), w1 = std::max(1u, w0 / 2), h1 = std::max(1u, h0 / 2);
            const std::vector<uint8_t>& s = mips.back();
            std::vector<uint8_t> d((size_t)w1 * h1 * 4);
            for (uint32_t y = 0; y < h1; ++y)
                for (uint32_t x = 0; x < w1; ++x) {
                    uint32_t x0 = std::min(x * 2, w0 - 1), x1 = std::min(x * 2 + 1, w0 - 1);
                    uint32_t y0 = std::min(y * 2, h0 - 1), y1 = std::min(y * 2 + 1, h0 - 1);
                    for (int c = 0; c < 4; ++c) {
                        uint32_t sum = s[((size_t)y0 * w0 + x0) * 4 + c] + s[((size_t)y0 * w0 + x1) * 4 + c] +
                                       s[((size_t)y1 * w0 + x0) * 4 + c] + s[((size_t)y1 * w0 + x1) * 4 + c];
                        d[((size_t)y * w1 + x) * 4 + c] = (uint8_t)((sum + 2) / 4);
                    }
                }
            mips.push_back(std::move(d));
            mw.push_back(w1);
            mh.push_back(h1);
        }
        UINT levels = (UINT)mips.size();
        D3D12_RESOURCE_DESC d = {};
        d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        d.Width = t.width;
        d.Height = t.height;
        d.DepthOrArraySize = 1;
        d.MipLevels = (UINT16)levels;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        d.SampleDesc.Count = 1;
        ComPtr<ID3D12Resource> tex;
        if (FAILED(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                     IID_PPV_ARGS(&tex)))) {
            m_materialTex.push_back(nullptr);
            continue;
        }
        std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> fp(levels);
        UINT64 total = 0;
        m_device->GetCopyableFootprints(&d, 0, levels, 0, fp.data(), nullptr, nullptr, &total);
        Buffer stage = createBuffer(total, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
        for (UINT l = 0; l < levels; ++l)
            for (uint32_t y = 0; y < mh[l]; ++y)
                memcpy((uint8_t*)stage.mapped + fp[l].Offset + (size_t)y * fp[l].Footprint.RowPitch,
                       &mips[l][(size_t)y * mw[l] * 4], (size_t)mw[l] * 4);
        beginCommands();
        for (UINT l = 0; l < levels; ++l) {
            D3D12_TEXTURE_COPY_LOCATION dl = {}, sl = {};
            dl.pResource = tex.Get();
            dl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dl.SubresourceIndex = l;
            sl.pResource = stage.res.Get();
            sl.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            sl.PlacedFootprint = fp[l];
            m_cmd->CopyTextureRegion(&dl, 0, 0, 0, &sl, nullptr);
        }
        transition(tex.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        executeAndWait();
        sv.Texture2D.MipLevels = levels;
        D3D12_CPU_DESCRIPTOR_HANDLE h = base;
        h.ptr += (SIZE_T)ti * m_descSize;
        m_device->CreateShaderResourceView(tex.Get(), &sv, h);
        m_materialTex.push_back(tex);
    }
}


namespace {

uint16_t floatToHalf(float f) {
    uint32_t x;
    memcpy(&x, &f, 4);
    uint32_t sign = (x >> 16) & 0x8000;
    int32_t e = (int32_t)((x >> 23) & 0xFF) - 127 + 15;
    uint32_t m = x & 0x7FFFFF;
    if (e >= 31) return (uint16_t)(sign | 0x7BFF);   // clamp to the largest half (no inf)
    if (e <= 0) {
        if (e < -10) return (uint16_t)sign;
        m |= 0x800000;
        return (uint16_t)(sign | ((m >> (14 - e)) + ((m >> (13 - e)) & 1)));
    }
    uint32_t h = sign | ((uint32_t)e << 10) | (m >> 13);
    return (uint16_t)(h + ((m >> 12) & 1));   // round to nearest
}

// Real SH basis (order 2), same assignment as EnvIrradiance in Raster.hlsl.
void shBasis(const float3& d, float y[9]) {
    y[0] = 0.282095f;
    y[1] = 0.488603f * d.y;
    y[2] = 0.488603f * d.z;
    y[3] = 0.488603f * d.x;
    y[4] = 1.092548f * d.x * d.y;
    y[5] = 1.092548f * d.y * d.z;
    y[6] = 0.315392f * (3.0f * d.z * d.z - 1.0f);
    y[7] = 1.092548f * d.x * d.z;
    y[8] = 0.546274f * (d.x * d.x - d.y * d.y);
}

float3 latLongDir(float u, float v) {
    float ph = (u - 0.5f) * 2.0f * kPi, th = v * kPi;
    return float3(std::sin(th) * std::sin(ph), std::cos(th), -std::sin(th) * std::cos(ph));
}

float lum(const float3& c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }

} // namespace

// Uploads the HDRI: RGBA16F texture with a box-filtered mip chain (raster reflections use the mips), the importance
// sampling tables of the path tracer, the irradiance SH and the sun of the image (raster key light).
void Renderer::uploadEnvironment(const EnvImagePtr& img) {
    waitIdle();
    m_envImage = img;
    m_envW = m_envH = 0;
    m_envSunValid = false;
    if (!img || img->width < 2 || img->height < 1 || img->rgb.size() < (size_t)img->width * img->height * 3) return;

    // mip chain (RGB float), level 0 = the image
    struct Level { uint32_t w, h; std::vector<float> rgb; };
    std::vector<Level> mips;
    mips.push_back({img->width, img->height, img->rgb});
    while (mips.back().w > 1 || mips.back().h > 1) {
        const Level& s = mips.back();
        Level d{std::max(1u, s.w / 2), std::max(1u, s.h / 2), {}};
        d.rgb.resize((size_t)d.w * d.h * 3);
        for (uint32_t y = 0; y < d.h; ++y)
            for (uint32_t x = 0; x < d.w; ++x) {
                uint32_t x0 = std::min(x * 2, s.w - 1), x1 = std::min(x * 2 + 1, s.w - 1);
                uint32_t y0 = std::min(y * 2, s.h - 1), y1 = std::min(y * 2 + 1, s.h - 1);
                for (int c = 0; c < 3; ++c)
                    d.rgb[((size_t)y * d.w + x) * 3 + c] =
                        0.25f * (s.rgb[((size_t)y0 * s.w + x0) * 3 + c] + s.rgb[((size_t)y0 * s.w + x1) * 3 + c] +
                                 s.rgb[((size_t)y1 * s.w + x0) * 3 + c] + s.rgb[((size_t)y1 * s.w + x1) * 3 + c]);
            }
        mips.push_back(std::move(d));
    }

    // texture
    UINT levels = (UINT)mips.size();
    D3D12_RESOURCE_DESC d = {};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = img->width;
    d.Height = img->height;
    d.DepthOrArraySize = 1;
    d.MipLevels = (UINT16)levels;
    d.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    d.SampleDesc.Count = 1;
    auto hp = heapProps(D3D12_HEAP_TYPE_DEFAULT);
    m_envTex.Reset();
    if (FAILED(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                 IID_PPV_ARGS(&m_envTex))))
        return;
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> fp(levels);
    UINT64 total = 0;
    m_device->GetCopyableFootprints(&d, 0, levels, 0, fp.data(), nullptr, nullptr, &total);
    Buffer stage = createBuffer(total, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
    for (UINT l = 0; l < levels; ++l) {
        const Level& L = mips[l];
        for (uint32_t y = 0; y < L.h; ++y) {
            uint16_t* row = (uint16_t*)((uint8_t*)stage.mapped + fp[l].Offset + (size_t)y * fp[l].Footprint.RowPitch);
            for (uint32_t x = 0; x < L.w; ++x) {
                const float* p = &L.rgb[((size_t)y * L.w + x) * 3];
                row[x * 4 + 0] = floatToHalf(p[0]);
                row[x * 4 + 1] = floatToHalf(p[1]);
                row[x * 4 + 2] = floatToHalf(p[2]);
                row[x * 4 + 3] = floatToHalf(1.0f);
            }
        }
    }
    beginCommands();
    for (UINT l = 0; l < levels; ++l) {
        D3D12_TEXTURE_COPY_LOCATION dl = {}, sl = {};
        dl.pResource = m_envTex.Get();
        dl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dl.SubresourceIndex = l;
        sl.pResource = stage.res.Get();
        sl.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        sl.PlacedFootprint = fp[l];
        m_cmd->CopyTextureRegion(&dl, 0, 0, 0, &sl, nullptr);
    }
    transition(m_envTex.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    executeAndWait();
    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.Format = d.Format;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = levels;
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += (SIZE_T)kEnvHeapSlot * m_descSize;
    m_device->CreateShaderResourceView(m_envTex.Get(), &sv, h);
    m_envMips = (float)levels;

    // Sampling tables on the first level at most 2048 wide. Each cell takes the largest luminance of its texels and
    // of their neighbours: the bilinear filter spreads the sun over the next texels, and a cell whose pdf would be far
    // below the radiance it returns makes fireflies.
    size_t li = 0;
    while (mips[li].w > 2048 && li + 1 < mips.size()) ++li;
    const Level& S = mips[li];   // (the sun of the image, below, is found at this level)
    const Level& L0 = mips[0];
    const uint32_t W = S.w, H = S.h;
    // The tables themselves on a level at most 256 wide (~130 KB, held by the GPU caches): sampling the 2k tables
    // (16 MB, two binary searches of dependent reads per sample) cost 15 times the rest of a studio frame. Each cell
    // keeps the largest luminance of the texels it covers, so the pdf never drops below the radiance it returns.
    size_t ti = 0;
    while (mips[ti].w > 256 && ti + 1 < mips.size()) ++ti;
    const uint32_t TW = mips[ti].w, TH = mips[ti].h, k = std::max(1u, L0.w / TW);
    std::vector<float> lum0((size_t)L0.w * L0.h);
    for (size_t i = 0; i < lum0.size(); ++i) lum0[i] = std::max(lum(float3(L0.rgb[i * 3], L0.rgb[i * 3 + 1], L0.rgb[i * 3 + 2])), 0.0f);
    std::vector<float> func((size_t)TW * TH), dist((size_t)TW * TH + (size_t)TH * (TW + 1) + TH + 1);
    double sum = 0;
    for (uint32_t y = 0; y < TH; ++y) {
        float st = std::sin((y + 0.5f) / TH * kPi);
        int y0 = std::max(0, (int)(y * k) - 1), y1 = std::min((int)L0.h - 1, (int)(y * k + k));
        for (uint32_t x = 0; x < TW; ++x) {
            float m = 0;
            for (int sy = y0; sy <= y1; ++sy)
                for (int sx = (int)(x * k) - 1; sx <= (int)(x * k + k); ++sx) {
                    int wx = (sx + (int)L0.w) % (int)L0.w;   // the image wraps horizontally
                    m = std::max(m, lum0[(size_t)sy * L0.w + wx]);
                }
            float f = m * st;
            func[(size_t)y * TW + x] = f;
            sum += f;
        }
    }
    if (sum <= 0) {   // black image: uniform tables
        std::fill(func.begin(), func.end(), 1.0f);
        sum = (double)TW * TH;
    }
    // a small floor keeps every direction reachable by the sampling
    float floorV = (float)(sum / ((double)TW * TH)) * 1e-3f;
    sum = 0;
    for (float& f : func) { f += floorV; sum += f; }
    std::copy(func.begin(), func.end(), dist.begin());
    size_t condBase = (size_t)TW * TH, margBase = condBase + (size_t)TH * (TW + 1);
    std::vector<double> rowSum(TH);
    for (uint32_t y = 0; y < TH; ++y) {
        double acc = 0;
        for (uint32_t x = 0; x < TW; ++x) acc += func[(size_t)y * TW + x];
        rowSum[y] = acc;
        double run = 0;
        float* cdf = &dist[condBase + (size_t)y * (TW + 1)];
        cdf[0] = 0;
        for (uint32_t x = 0; x < TW; ++x) {
            run += func[(size_t)y * TW + x];
            cdf[x + 1] = (float)(run / acc);
        }
        cdf[TW] = 1.0f;
    }
    {
        double run = 0;
        float* cdf = &dist[margBase];
        cdf[0] = 0;
        for (uint32_t y = 0; y < TH; ++y) {
            run += rowSum[y];
            cdf[y + 1] = (float)(run / sum);
        }
        cdf[TH] = 1.0f;
    }
    Buffer up = createBuffer(dist.size() * sizeof(float), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
    memcpy(up.mapped, dist.data(), dist.size() * sizeof(float));
    m_envDist = up;   // read directly from the upload heap (a few MB, read through the GPU caches)
    m_envW = TW;
    m_envH = TH;
    m_envPdfScale = (float)((double)TW * TH / sum);

    // Sun of the image: the brightest texel, if it stands well above the mean, and the bright texels around it
    // (6 degrees). The raster viewport uses it as its key light (with the shadow map) and leaves it out of the SH.
    uint32_t bx = 0, by = 0;
    float peak = 0;
    double meanL = 0;
    for (uint32_t y = 0; y < H; ++y)
        for (uint32_t x = 0; x < W; ++x) {
            const float* p = &S.rgb[((size_t)y * W + x) * 3];
            float l = lum(float3(p[0], p[1], p[2]));
            meanL += l;
            if (l > peak) { peak = l; bx = x; by = y; }
        }
    meanL /= (double)W * H;
    float3 sunDir = latLongDir((bx + 0.5f) / W, (by + 0.5f) / H);
    std::vector<uint8_t> isSun((size_t)W * H, 0);
    float3 sunE(0.0f), sunAxis(0.0f);
    const float dOmega0 = (2.0f * kPi / W) * (kPi / H);
    if (peak > 30.0f * (float)meanL) {
        const float cosCone = std::cos(radians(6.0f));
        for (uint32_t y = 0; y < H; ++y) {
            float st = std::sin((y + 0.5f) / H * kPi);
            for (uint32_t x = 0; x < W; ++x) {
                float3 dir = latLongDir((x + 0.5f) / W, (y + 0.5f) / H);
                if (dot(dir, sunDir) < cosCone) continue;
                const float* p = &S.rgb[((size_t)y * W + x) * 3];
                float3 c(p[0], p[1], p[2]);
                if (lum(c) < 0.05f * peak) continue;
                isSun[(size_t)y * W + x] = 1;
                sunE += c * (dOmega0 * st);
                sunAxis += dir * (lum(c) * dOmega0 * st);
            }
        }
        m_envSunValid = lum(sunE) > 0.05f;
        m_envSunDir = normalize(sunAxis);
        m_envSunE = sunE;
    }

    // Irradiance SH (cosine lobe convolution: pi, 2pi/3, pi/4 per band), with and without the sun.
    double sh[9][3] = {}, shSun[9][3] = {};
    for (uint32_t y = 0; y < H; ++y) {
        float st = std::sin((y + 0.5f) / H * kPi);
        for (uint32_t x = 0; x < W; ++x) {
            float3 dir = latLongDir((x + 0.5f) / W, (y + 0.5f) / H);
            const float* p = &S.rgb[((size_t)y * W + x) * 3];
            float b[9];
            shBasis(dir, b);
            double w = dOmega0 * st;
            bool sunTexel = m_envSunValid && isSun[(size_t)y * W + x];
            for (int k = 0; k < 9; ++k)
                for (int c = 0; c < 3; ++c) (sunTexel ? shSun : sh)[k][c] += p[c] * b[k] * w;
        }
    }
    const float band[9] = {kPi, 2 * kPi / 3, 2 * kPi / 3, 2 * kPi / 3, kPi / 4, kPi / 4, kPi / 4, kPi / 4, kPi / 4};
    for (int k = 0; k < 9; ++k)
        for (int c = 0; c < 3; ++c) {
            m_envSH[k][c] = (float)(sh[k][c] * band[k]);
            m_envSHFull[k][c] = (float)((sh[k][c] + shSun[k][c]) * band[k]);
        }
}

void Renderer::buildTLAS(const Scene& scene, bool buildAS) {
    // unchanged scene (Scene::objectsVersion): the instances, materials and lights of the last build stay, the TLAS is
    // built once
    const bool cached = scene.objectsVersion && scene.objectsVersion == m_buildVersion && m_instanceDescs.res;
    if (!cached) buildInstances(scene);
    m_buildVersion = scene.objectsVersion;
    if (!buildAS) return;
    if (cached && scene.objectsVersion == m_tlasVersion && m_tlas.res) return;
    m_tlasVersion = scene.objectsVersion;
    buildAccel();
}

void Renderer::buildInstances(const Scene& scene) {
    m_draws.clear();
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> descs;
    std::vector<gpu::GpuInstance> insts;
    std::vector<gpu::GpuMaterial> mats;
    std::vector<gpu::GpuLight> lights;
    m_objectToInstance.assign(scene.objects.size(), -1);

    for (const Material& m : scene.materials) {
        gpu::GpuMaterial g = {};
        g.baseColor = m.baseColor;
        g.roughness = m.roughness;
        g.emission = m.emission * m.emissionStrength;
        g.metallic = m.metallic;
        g.sssRadius = m.sssRadius;
        g.subsurface = m.subsurface;
        g.transmission = m.transmission;
        g.ior = m.ior;
        g.bevelRadius = m.bevelRadius;
        g.flags = m.flags;
        g.specular = m.specular;
        g.coat = m.coat;
        g.coatRoughness = m.coatRoughness;
        g.baseColorTex = NO_TEXTURE;
        g.roughTex = NO_TEXTURE;
        if (m.baseColorTex >= 0 && m.baseColorTex < (int)m_materialTex.size() && m_materialTex[m.baseColorTex])
            g.baseColorTex = (uint32_t)m.baseColorTex;
        if (m.roughnessTex >= 0 && m.roughnessTex < (int)m_materialTex.size() && m_materialTex[m.roughnessTex] && m.roughTexStrength > 0)
            g.roughTex = (uint32_t)m.roughnessTex;
        g.roughTexScale = m.roughTexScale;
        g.roughTexStrength = m.roughTexStrength;
        mats.push_back(g);
    }
    gpu::GpuMaterial none = {};
    none.baseColorTex = NO_TEXTURE;
    none.roughTex = NO_TEXTURE;
    if (mats.empty()) mats.push_back(none);

    auto addInstance = [&](int meshIdx, const Affine& xf, uint32_t matId, uint32_t lightId, uint32_t mask) -> int {
        if (meshIdx < 0 || meshIdx >= (int)m_meshes.size() || !m_meshes[meshIdx].blas.res) return -1;
        const MeshGpu& g = m_meshes[meshIdx];
        D3D12_RAYTRACING_INSTANCE_DESC d = {};
        memcpy(d.Transform, xf.m, sizeof(d.Transform));
        d.InstanceID = (UINT)insts.size();
        d.InstanceMask = mask;
        d.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
        if (mask == MASK_GLASS) d.Flags |= D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_NON_OPAQUE;
        d.AccelerationStructure = g.blas.va();
        descs.push_back(d);
        insts.push_back({g.indexOffset, g.vertexOffset, matId, lightId});
        uint32_t flags = lightId != NO_LIGHT ? (mask == MASK_LIGHT_VISIBLE ? 2u : 6u) : 0u;
        m_draws.push_back({meshIdx, xf, matId, (uint32_t)insts.size(), flags, 1.0f});
        return (int)insts.size() - 1;
    };

    for (size_t i = 0; i < scene.objects.size(); ++i) {
        const Object& o = scene.objects[i];
        if (!o.visible) continue;
        if (o.ghost) {
            uint32_t gm = (o.material >= 0 && o.material < (int)scene.materials.size()) ? o.material : 0;
            if (o.mesh >= 0 && o.mesh < (int)m_meshes.size()) m_draws.push_back({o.mesh, o.transform(), gm, 0u, 1u, o.ghostAlpha});
            continue;
        }
        uint32_t matId = (o.material >= 0 && o.material < (int)scene.materials.size()) ? o.material : 0;
        bool glass = matId < scene.materials.size() && scene.materials[matId].transmission > 0.5f;
        m_objectToInstance[i] = addInstance(o.mesh, o.transform(), matId, NO_LIGHT, glass ? MASK_GLASS : MASK_OPAQUE);
    }
    for (const AreaLight& l : scene.lights) {
        if (!l.enabled) continue;
        float3 U, V, N;
        l.basis(U, V, N);
        gpu::GpuMaterial lm = {};
        lm.baseColorTex = NO_TEXTURE;
        lm.roughTex = NO_TEXTURE;
        lm.emission = l.color * l.intensity;
        lm.roughness = 1.0f;
        lm.ior = 1.5f;
        uint32_t matId = (uint32_t)mats.size();
        mats.push_back(lm);
        Affine xf = Affine::fromBasis(U * l.width, V * l.height, N, l.position);
        uint32_t lightId = (uint32_t)lights.size();
        int inst = addInstance(m_quadMesh, xf, matId, lightId,
                               l.visibleToCamera ? MASK_LIGHT_VISIBLE : MASK_LIGHT_HIDDEN);
        gpu::GpuLight gl = {};
        gl.corner = l.position - U * (l.width * 0.5f) - V * (l.height * 0.5f);
        gl.edgeU = U * l.width;
        gl.edgeV = V * l.height;
        gl.area = l.width * l.height;
        gl.radiance = l.color * l.intensity;
        gl.instance = (uint32_t)inst;
        lights.push_back(gl);
    }
    m_sunLight = NO_LIGHT;
    // Path tracing: the sun becomes a small area light far away (hidden from the camera), with the same irradiance.
    if (scene.sun.enabled && scene.sun.intensity > 0) {
        const float dist = 2000.0f, size = 20.0f;   // a side of 20 at 2000: about half a degree, as the real sun (sharp shadows)
        AreaLight sl;
        sl.position = normalize(scene.sun.direction) * dist;
        sl.target = float3(0, 0, 0);
        float3 U, V, N;
        sl.basis(U, V, N);
        float3 radiance = scene.sun.color * (scene.sun.intensity * dist * dist / (size * size));
        gpu::GpuMaterial lm = {};
        lm.baseColorTex = NO_TEXTURE;
        lm.roughTex = NO_TEXTURE;
        lm.emission = radiance;
        lm.roughness = 1.0f;
        lm.ior = 1.5f;
        uint32_t matId = (uint32_t)mats.size();
        mats.push_back(lm);
        Affine xf = Affine::fromBasis(U * size, V * size, N, sl.position);
        uint32_t lightId = (uint32_t)lights.size();
        int inst = addInstance(m_quadMesh, xf, matId, lightId, MASK_LIGHT_HIDDEN);
        if (inst >= 0) {
            gpu::GpuLight gl = {};
            gl.corner = sl.position - U * (size * 0.5f) - V * (size * 0.5f);
            gl.edgeU = U * size;
            gl.edgeV = V * size;
            gl.area = size * size;
            gl.radiance = radiance;
            gl.instance = (uint32_t)inst;
            m_sunLight = (uint32_t)lights.size();
            lights.push_back(gl);
        }
    }
    // Emissive objects are lights too: sampled in the cone of their bounding sphere (path tracing), sphere lights in raster.
    for (size_t i = 0; i < scene.objects.size(); ++i) {
        int inst = m_objectToInstance[i];
        if (inst < 0) continue;
        const Object& o = scene.objects[i];
        const Material& m = scene.materials[insts[inst].materialId];
        float3 e = m.emission * m.emissionStrength;
        if (std::max(e.x, std::max(e.y, e.z)) <= 0.0f || m.transmission > 0.5f) continue;
        const MeshGpu& g = m_meshes[o.mesh];
        Affine xf = o.transform();
        float sc = 0;
        for (int c = 0; c < 3; ++c) sc = std::max(sc, length(float3(xf.m[0][c], xf.m[1][c], xf.m[2][c])));
        gpu::GpuLight gl = {};
        gl.type = LIGHT_OBJECT;
        gl.corner = xf.point(g.center);
        gl.area = std::max(g.radius * sc, 1e-4f);
        gl.radiance = e;
        gl.instance = (uint32_t)inst;
        insts[inst].lightId = (uint32_t)lights.size();
        lights.push_back(gl);
    }
    m_lightCount = (uint32_t)lights.size();
    if (lights.empty()) lights.push_back(gpu::GpuLight{});
    if (insts.empty()) insts.push_back(gpu::GpuInstance{});

    ensureUpload(m_instanceDescs, std::max<size_t>(descs.size(), 1) * sizeof(D3D12_RAYTRACING_INSTANCE_DESC));
    ensureUpload(m_instances, insts.size() * sizeof(gpu::GpuInstance));
    ensureUpload(m_materials, mats.size() * sizeof(gpu::GpuMaterial));
    ensureUpload(m_lights, lights.size() * sizeof(gpu::GpuLight));
    if (!descs.empty()) memcpy(m_instanceDescs.mapped, descs.data(), descs.size() * sizeof(descs[0]));
    memcpy(m_instances.mapped, insts.data(), insts.size() * sizeof(insts[0]));
    memcpy(m_materials.mapped, mats.data(), mats.size() * sizeof(mats[0]));
    memcpy(m_lights.mapped, lights.data(), lights.size() * sizeof(lights[0]));

    // Scene bounds (for the raster shadow map), ignoring huge ground planes.
    {
        float3 mn(1e30f), mx(-1e30f);
        for (const Draw& d : m_draws) {
            const MeshGpu& g = m_meshes[d.mesh];
            if (length(g.bmax - g.bmin) > 150.0f || (d.flags & 2u)) continue;
            for (int k = 0; k < 8; ++k) {
                float3 p((k & 1) ? g.bmax.x : g.bmin.x, (k & 2) ? g.bmax.y : g.bmin.y, (k & 4) ? g.bmax.z : g.bmin.z);
                p = d.xf.point(p);
                mn = minv(mn, p);
                mx = maxv(mx, p);
            }
        }
        if (mn.x > mx.x) { mn = float3(-10); mx = float3(10); }
        m_sceneCenter = (mn + mx) * 0.5f;
        m_sceneRadius = std::max(length(mx - mn) * 0.5f, 1.0f);
    }
    m_tlasCount = (uint32_t)descs.size();
}

void Renderer::buildAccel() {

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS in = {};
    in.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    in.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    in.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    in.NumDescs = m_tlasCount;
    in.InstanceDescs = m_instanceDescs.va();
    uint32_t needed = std::max<uint32_t>(m_tlasCount, 1);
    if (!m_tlas.res || needed > m_tlasCapacity) {
        m_tlasCapacity = std::max(needed * 2, 64u);
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS cap = in;
        cap.NumDescs = m_tlasCapacity;
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
        m_device->GetRaytracingAccelerationStructurePrebuildInfo(&cap, &info);
        m_tlas = createBuffer(info.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                              D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                              D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
        m_tlasScratch = createBuffer(info.ScratchDataSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                                     D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bd = {};
    bd.Inputs = in;
    bd.DestAccelerationStructureData = m_tlas.va();
    bd.ScratchAccelerationStructureData = m_tlasScratch.va();
    m_cmd->BuildRaytracingAccelerationStructure(&bd, 0, nullptr);
    uavBarrier(m_tlas.res.Get());
}

// Places a regular probe grid over the scene bounds (about rs.ddgiProbeBudget probes, isotropic spacing)
// and (re)creates the DDGI textures. Probes restart from scratch when the grid changes.
void Renderer::setupProbeGrid(const RenderSettings& rs) {
    int budget = std::max(8, rs.giMethod == 1 ? rs.rcProbeBudget : rs.ddgiProbeBudget);
    // GI bounds (RenderSettings::giBounds*, BrickIt): the grid covers them (grown by the fade), not a huge ground;
    // snapped to 4 units so that a small edit of the scene keeps the grid (and the converged probes)
    const bool bounded = rs.giBoundsFade > 0;
    float3 bmn = rs.giBoundsMin - float3(rs.giBoundsFade), bmx = rs.giBoundsMax + float3(rs.giBoundsFade);
    if (bounded) {
        bmn = float3(std::floor(bmn.x / 4) * 4, std::floor(bmn.y / 4) * 4, std::floor(bmn.z / 4) * 4);
        bmx = float3(std::ceil(bmx.x / 4) * 4, std::ceil(bmx.y / 4) * 4, std::ceil(bmx.z / 4) * 4);
    }
    const uint64_t boundsKey = bounded ? std::hash<std::string>()(std::string((const char*)&bmn, sizeof(bmn)) + std::string((const char*)&bmx, sizeof(bmx))) : 0;
    if (m_giTex[0] && m_giBudget == budget && (bounded ? m_giBoundsKey == boundsKey : (m_giBoundsKey == 0 && m_giGridVersion == m_sceneGeometryVersion))) return;
    m_giBoundsKey = boundsKey;
    float3 mn(1e30f), mx(-1e30f);
    if (bounded) { mn = bmn; mx = bmx; }
    else for (const Draw& d : m_draws) {
        if (d.flags & 2u) continue;   // light quads
        const MeshGpu& g = m_meshes[d.mesh];
        for (int k = 0; k < 8; ++k) {
            float3 p((k & 1) ? g.bmax.x : g.bmin.x, (k & 2) ? g.bmax.y : g.bmin.y, (k & 4) ? g.bmax.z : g.bmin.z);
            p = d.xf.point(p);
            mn = minv(mn, p);
            mx = maxv(mx, p);
        }
    }
    // The grid itself is placed by the code shared with the Lit renderer (same probes in the editor and the builds).
    lit::ProbeGrid grid;
    lit::placeProbeGrid(mn, mx, m_sceneCenter, m_sceneRadius, budget, grid);
    m_giBudget = budget;
    createProbeTextures(grid);
}

// (Re)creates the DDGI textures for a grid: the atlases, the rays and the probe state, with their views. The probes
// restart from scratch (first capture or bake writes them without history).
void Renderer::createProbeTextures(const lit::ProbeGrid& grid) {
    m_giOrigin = grid.origin;
    m_giSpacing = grid.spacing;
    for (int i = 0; i < 3; ++i) m_giCounts[i] = grid.counts[i];
    const uint32_t* n = m_giCounts;
    m_giGridVersion = m_sceneGeometryVersion;
    m_bakeValid = false;
    m_giFrame = 0;
    m_capBase = 0;
    m_capSeen = 0;

    uint32_t total = n[0] * n[1] * n[2];
    m_capWarmup = 4 * (int)((total + kCaptureMax - 1) / kCaptureMax);   // four full passes: the bounces settle
    struct TexDesc { uint32_t w, h; DXGI_FORMAT fmt; };
    const TexDesc td[kGITexCount] = {
        {n[0] * n[1] * (DDGI_IRR_N + 2), n[2] * (DDGI_IRR_N + 2), DXGI_FORMAT_R16G16B16A16_FLOAT},
        {n[0] * n[1] * (DDGI_DIST_N + 2), n[2] * (DDGI_DIST_N + 2), DXGI_FORMAT_R16G16B16A16_FLOAT},
        {DDGI_RAYS, total, DXGI_FORMAT_R32G32B32A32_FLOAT},
        {total, 1, DXGI_FORMAT_R16G16B16A16_FLOAT},
    };
    waitIdle();
    auto hp = heapProps(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_heap->GetCPUDescriptorHandleForHeapStart();
    for (int i = 0; i < kGITexCount; ++i) {
        D3D12_RESOURCE_DESC d = {};
        d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        d.Width = td[i].w;
        d.Height = td[i].h;
        d.DepthOrArraySize = 1;
        d.MipLevels = 1;
        d.Format = td[i].fmt;
        d.SampleDesc.Count = 1;
        d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        m_giTex[i].Reset();
        m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                          nullptr, IID_PPV_ARGS(&m_giTex[i]));
        D3D12_UNORDERED_ACCESS_VIEW_DESC u = {};
        u.Format = td[i].fmt;
        u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        D3D12_CPU_DESCRIPTOR_HANDLE hnd = cpu;
        hnd.ptr += (SIZE_T)(kGIHeapBase + i) * m_descSize;
        m_device->CreateUnorderedAccessView(m_giTex[i].Get(), nullptr, &u, hnd);
    }
    // Read-only views of the irradiance, distance and state atlases for the raster pass (DDGI without ray tracing).
    const int srvOf[3] = {0, 1, 3};
    for (int k = 0; k < 3; ++k) {
        D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
        sv.Format = td[srvOf[k]].fmt;
        sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sv.Texture2D.MipLevels = 1;
        D3D12_CPU_DESCRIPTOR_HANDLE hnd = cpu;
        hnd.ptr += (SIZE_T)(kProbeSrvSlot + k) * m_descSize;
        m_device->CreateShaderResourceView(m_giTex[srvOf[k]].Get(), &sv, hnd);
    }
}

// ---------------------------------------------------------------- baked probes (Lit mode)

void Renderer::startBake(int frames) {
    m_bakeFrames = std::max(1, frames);
    m_bakeValid = false;
    m_giFrame = 0;   // the first bake frame writes the tiles without history
}

uint32_t Renderer::bakeSignature(const Scene& scene) const {
    uint32_t h = sceneSignature(scene) ^ (transformSignature(scene) * 2654435761u) ^ (scene.geometryVersion * 40503u);
    return h ? h : 1u;
}

namespace {

// Reads a 2D texture (one subresource, 16-bit texels of `channels` components) into `out`.
bool readTexture16(ID3D12Device* device, ID3D12Resource* tex, std::vector<uint16_t>& out, uint32_t channels,
                   const std::function<void(ID3D12Resource*, D3D12_PLACED_SUBRESOURCE_FOOTPRINT&, UINT64)>& copy) {
    D3D12_RESOURCE_DESC d = tex->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {};
    UINT64 total = 0;
    device->GetCopyableFootprints(&d, 0, 1, 0, &fp, nullptr, nullptr, &total);
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC bd = {};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = total;
    bd.Height = bd.DepthOrArraySize = bd.MipLevels = 1;
    bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> rb;
    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&rb))))
        return false;
    copy(rb.Get(), fp, total);
    void* p = nullptr;
    D3D12_RANGE r = {0, (SIZE_T)total};
    if (FAILED(rb->Map(0, &r, &p))) return false;
    const uint32_t w = (uint32_t)d.Width, h = d.Height;
    out.resize((size_t)w * h * channels);
    for (uint32_t y = 0; y < h; ++y)
        memcpy(&out[(size_t)y * w * channels], (const uint8_t*)p + fp.Offset + (size_t)y * fp.Footprint.RowPitch, (size_t)w * channels * 2);
    D3D12_RANGE wr = {0, 0};
    rb->Unmap(0, &wr);
    return true;
}

} // namespace

bool Renderer::readBakedProbes(lit::BakedProbes& out) {
    out = lit::BakedProbes();
    if (!m_giTex[0] || !m_bakeValid) return false;
    waitIdle();
    out.grid.origin = m_giOrigin;
    out.grid.spacing = m_giSpacing;
    for (int i = 0; i < 3; ++i) out.grid.counts[i] = m_giCounts[i];
    std::vector<uint16_t>* dst[3] = {&out.irradiance, &out.distance, &out.state};
    const int src[3] = {0, 1, 3};
    for (int k = 0; k < 3; ++k) {
        ID3D12Resource* tex = m_giTex[src[k]].Get();
        bool ok = readTexture16(m_device.Get(), tex, *dst[k], 4, [&](ID3D12Resource* rb, D3D12_PLACED_SUBRESOURCE_FOOTPRINT& fp, UINT64) {
            beginCommands();
            transition(tex, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
            D3D12_TEXTURE_COPY_LOCATION dl = {}, sl = {};
            dl.pResource = rb;
            dl.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dl.PlacedFootprint = fp;
            sl.pResource = tex;
            sl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            m_cmd->CopyTextureRegion(&dl, 0, 0, 0, &sl, nullptr);
            transition(tex, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            executeAndWait();
        });
        if (!ok) { out = lit::BakedProbes(); return false; }
    }
    return out.valid();
}

void Renderer::setBakedProbes(const lit::BakedProbes& probes) {
    if (!probes.valid()) return;
    waitIdle();
    createProbeTextures(probes.grid);
    // Upload the three atlases through one upload buffer each (textures are in the UNORDERED_ACCESS state).
    const std::vector<uint16_t>* src[3] = {&probes.irradiance, &probes.distance, &probes.state};
    const int dst[3] = {0, 1, 3};
    beginCommands();
    std::vector<Buffer> uploads;
    for (int k = 0; k < 3; ++k) {
        ID3D12Resource* tex = m_giTex[dst[k]].Get();
        D3D12_RESOURCE_DESC d = tex->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {};
        UINT64 total = 0;
        m_device->GetCopyableFootprints(&d, 0, 1, 0, &fp, nullptr, nullptr, &total);
        Buffer up = createBuffer(total, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
        const uint32_t w = (uint32_t)d.Width, h = d.Height;
        if (src[k]->size() < (size_t)w * h * 4) continue;
        for (uint32_t y = 0; y < h; ++y)
            memcpy((uint8_t*)up.mapped + fp.Offset + (size_t)y * fp.Footprint.RowPitch, &(*src[k])[(size_t)y * w * 4], (size_t)w * 8);
        transition(tex, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION dl = {}, sl = {};
        dl.pResource = tex;
        dl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        sl.pResource = up.res.Get();
        sl.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        sl.PlacedFootprint = fp;
        m_cmd->CopyTextureRegion(&dl, 0, 0, 0, &sl, nullptr);
        transition(tex, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        uploads.push_back(std::move(up));
    }
    executeAndWait();
    m_bakeFrames = 0;
    m_bakeValid = true;
    m_giFrame = 1;   // history exists: a later live update blends into it
}

// Probe update (trace + blend) then the per-pixel shading into the accumulation texture.
// Expects the compute root signature and the scene SRVs to be bound.
void Renderer::recordDDGI(const RenderSettings& rs, UINT gx, UINT gy, bool shade) {
    setupProbeGrid(rs);
    uint32_t total = giProbeCount();

    // Constants shared with the Lit renderer (LitPrep): grid, random rotation of the ray set, capture batch.
    lit::ProbeGrid grid;
    grid.origin = m_giOrigin;
    grid.spacing = m_giSpacing;
    for (int i = 0; i < 3; ++i) grid.counts[i] = m_giCounts[i];
    lit::DDGIFrame f;
    f.frame = m_giFrame++;
    f.hysteresis = rs.ddgiHysteresis;
    // progressive: a running average while still (1 - 1 / n, from 0.85 up to ddgiHysteresis); after 64 still frames a
    // quarter of the probes per frame (the average goes on at a quarter of the pace, a quarter of the cost)
    uint32_t stride = 1;
    const bool progressive = rs.ddgiProgressive && !rs.ddgiRaster && rs.giMethod == 0;
    const bool hold = progressive && rs.ddgiHold && m_giFrame > 1;   // (probes that exist already)
    if (progressive && !hold) {
        if (m_giStill == 0) m_giStillUpdates = 0;
        stride = m_giStill >= 64 && m_giFrame > 1 ? 4u : 1u;
        f.hysteresis = std::min(rs.ddgiHysteresis, std::max(0.85f, 1.0f - 1.0f / (m_giStillUpdates + 7.0f)));
        m_giStillUpdates += 1.0f / (float)stride;
        ++m_giStill;
    }
    f.temporalMax = (uint32_t)std::max(1, rs.temporalCap);
    f.view = rs.ddgiView;
    f.showProbes = rs.ddgiShowProbes;
    f.rasterCapture = rs.ddgiRaster;
    f.warmup = rs.ddgiRaster && m_capWarmup > 0;
    f.captureBase = m_capBase;
    f.captureSlots = m_capSlots;
    f.captureSeen = rs.ddgiRaster ? std::min(total, m_capSeen + m_capSlots) : total;
    gpu::DDGIConstants c;
    lit::ddgiConstants(grid, f, m_giRng, c);
    if (progressive) c.flags |= 16u;   // (DDGIBlend: no per-texel fast path, the noise would trigger it)
    c.stride = rs.ddgiRaster ? 1u : stride;
    c.energy = std::max(0.0f, rs.ddgiEnergy);
    if (rs.ddgiViewBias >= 0) c.viewBias = rs.ddgiViewBias;
    c.ageCap = progressive ? (float)std::max(0, rs.ddgiAgeCap) : 0.0f;
    if (progressive && !hold && (m_giDrawVersion != m_buildVersion || m_buildVersion == 0)) {
        // the draws that appeared or went away since the last frame (a moved object: both): their box, grown by
        // two probe spacings, is where the probes restart their average
        m_giDrawVersion = m_buildVersion;
        typedef std::pair<uint64_t, std::pair<float3, float3>> KeyBox;
        std::vector<KeyBox> now;
        now.reserve(m_draws.size());
        for (const Draw& d : m_draws) {
            if (d.flags & 2u) continue;   // light quads
            uint64_t k = 1469598103934665603ull;
            auto mixd = [&](const void* p, size_t n) { const unsigned char* c = (const unsigned char*)p; for (size_t i = 0; i < n; ++i) { k ^= c[i]; k *= 1099511628211ull; } };
            mixd(&d.mesh, sizeof(d.mesh));
            mixd(&d.xf, sizeof(d.xf));
            mixd(&d.material, sizeof(d.material));
            const MeshGpu& g = m_meshes[d.mesh];
            float3 mn(1e30f), mx(-1e30f);
            for (int q = 0; q < 8; ++q) {
                const float3 v = d.xf.point(float3((q & 1) ? g.bmax.x : g.bmin.x, (q & 2) ? g.bmax.y : g.bmin.y, (q & 4) ? g.bmax.z : g.bmin.z));
                mn = minv(mn, v);
                mx = maxv(mx, v);
            }
            now.push_back({k, {mn, mx}});
        }
        std::sort(now.begin(), now.end(), [](const KeyBox& x, const KeyBox& y) { return x.first < y.first; });
        float3 dmn(1e30f), dmx(-1e30f);
        if (!m_giDrawBoxes.empty()) {
            size_t i = 0, j = 0;   // sorted merge: what is in one list only changed
            while (i < now.size() || j < m_giDrawBoxes.size()) {
                if (j >= m_giDrawBoxes.size() || (i < now.size() && now[i].first < m_giDrawBoxes[j].first)) {
                    dmn = minv(dmn, now[i].second.first); dmx = maxv(dmx, now[i].second.second); ++i;
                } else if (i >= now.size() || m_giDrawBoxes[j].first < now[i].first) {
                    dmn = minv(dmn, m_giDrawBoxes[j].second.first); dmx = maxv(dmx, m_giDrawBoxes[j].second.second); ++j;
                } else { ++i; ++j; }
            }
        }
        m_giDrawBoxes.swap(now);
        if (dmn.x <= dmx.x) {
            // grown by two probe spacings, and by half its size (a large change reaches further: shadow, bounce)
            const float3 ext = dmx - dmn;
            const float g = std::max(2.0f * m_giSpacing, 0.5f * std::max(ext.x, std::max(ext.y, ext.z)));
            const float3 a = dmn - float3(g), b = dmx + float3(g);
            c.dirtyMin = float4(a.x, a.y, a.z, 1.0f);
            c.dirtyMax = float4(b.x, b.y, b.z, 1.0f);
        }
    }
    if (progressive && !hold && m_giStill == 1 && c.ageCap > 0) {
        // a change of the lights, the environment or the settings: every probe restarts (also those left alone)
        c.dirtyMin = float4(-1e30f, -1e30f, -1e30f, 1.0f);
        c.dirtyMax = float4(1e30f, 1e30f, 1e30f, 1.0f);
    }
    // Screen-space or split cascades: only for the raster view (shade = false), which reads their screen GI texture.
    const bool screen = rs.giMethod == 1 && (rs.rcScreen || rs.rcSplit) && !rs.ddgiRaster && !shade;
    if (screen) c.flags |= 8u;
    if (screen && rs.rcSplit && rs.rcSplitDebug > 0) c.view = rs.rcSplitDebug == 7 ? 4 : 3;   // Split RC inspection: the raster
                                                                                                  // pass shows it as is (7: the ray traced shadow mask)
    memcpy(m_giConstants.mapped, &c, sizeof(c));

    D3D12_GPU_DESCRIPTOR_HANDLE giTable = m_heap->GetGPUDescriptorHandleForHeapStart();
    giTable.ptr += (UINT64)kGIHeapBase * m_descSize;
    m_cmd->SetComputeRootConstantBufferView(10, m_giConstants.va());
    m_cmd->SetComputeRootDescriptorTable(11, giTable);

    m_screenGIOn = false;
    m_rcStats.screenCascades = 0;
    m_rcStats.screenRays = 0;
    if (rs.giMethod == 1 && !rs.ddgiRaster) {
        recordRC(rs);   // the cascades replace the probe rays and the blend (same atlases)
        if (screen && rs.rcSplit) recordSplitRC(rs, gx, gy);
        else if (screen) recordRCScreen(rs, gx, gy);
    } else if (rs.ddgiRaster) {
        // One group per probe: the captured ones integrate their faces (u12..u14) straight into their tiles, no blend pass.
        D3D12_GPU_DESCRIPTOR_HANDLE capTable = m_heap->GetGPUDescriptorHandleForHeapStart();
        capTable.ptr += (UINT64)kCapHeapSlot * m_descSize;
        m_cmd->SetComputeRootDescriptorTable(15, capTable);
        m_cmd->SetPipelineState(m_psoGIRaster.Get());
        m_cmd->Dispatch(total, 1, 1);
        uavBarrier();
        m_cmd->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 2);
        m_cmd->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 3);
    } else if (hold) {   // held: the probes as they are
        m_cmd->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 2);
        m_cmd->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 3);
    } else {
        m_cmd->SetPipelineState(m_psoGIUpdate.Get());
        m_cmd->Dispatch(DDGI_RAYS / 64, total, 1);
        uavBarrier();
        m_cmd->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 2);
        m_cmd->SetPipelineState(m_psoGIBlend.Get());
        m_cmd->Dispatch(total, 1, 1);
        uavBarrier();
        m_cmd->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 3);
    }
    if (shade) {   // ray traced visibility; without it the raster pass reads the probes (recordRaster, probes = true)
        m_cmd->SetPipelineState(m_psoGIShade.Get());
        m_cmd->Dispatch(gx, gy, 1);
        uavBarrier();
    }
    m_giTimed = true;
    // The batch just captured is the one the next frame starts after.
    if (rs.ddgiRaster) {
        m_capSeen = c.captureSeen;
        m_capBase = (m_capBase + m_capSlots) % total;
        if (m_capWarmup > 0) --m_capWarmup;
    }
}

// Radiance Cascades over the probe grid: cascade 0 = the probes (spacing s0), cascade i has its probes every
// s0 * 2^i (centred on the 2x2x2 blocks of the cascade below), dirRes0 * 2^i directions per side and the interval
// [L0 (4^i - 1) / 3, L0 (4^(i+1) - 1) / 3]; the top cascade runs to infinity. The cascades are traced from the top
// down, each one merging with the one above (RCTrace.hlsl), then cascade 0 fills the probe atlases (RCGather.hlsl).
// Expects the compute root signature, the scene SRVs and the DDGI constants and atlases to be bound (recordDDGI).
void Renderer::recordRC(const RenderSettings& rs) {
    gpu::RCConstants rc = {};
    const int count = std::max(1, std::min(rs.rcCascades, RC_MAX_CASCADES));
    const uint32_t dir0 = (uint32_t)std::max(2, std::min(rs.rcDirRes, RC_MAX_DIR0));
    const float s0 = m_giSpacing, L0 = std::max(0.05f, rs.rcInterval) * s0;
    uint32_t offset = 0;
    m_rcStats = RCStats();
    m_rcStats.cascades = count;
    for (int i = 0; i < count; ++i) {
        gpu::RCCascade& c = rc.c[i];
        const uint32_t scale = 1u << i;
        for (int a = 0; a < 3; ++a) {
            uint32_t n = m_giCounts[a];
            // parents cover the children: child k sits at (k + 0.5) / 2^i - 0.5 in parent units
            (&c.counts.x)[a] = i == 0 ? n : (uint32_t)std::max(1, (int)std::floor((n - 0.5f) / scale - 0.5f) + 2);
        }
        c.counts.w = c.counts.x * c.counts.y * c.counts.z;
        c.spacing = s0 * scale;
        c.origin = m_giOrigin + float3(s0 * (scale - 1) * 0.5f);
        c.dirRes = dir0 * scale;
        c.offset = offset;
        c.tStart = L0 * (std::pow(4.0f, (float)i) - 1.0f) / 3.0f;
        c.tEnd = i + 1 == count ? 1e30f : L0 * (std::pow(4.0f, (float)(i + 1)) - 1.0f) / 3.0f;
        const uint64_t texels = (uint64_t)c.counts.w * c.dirRes * c.dirRes;
        offset += (uint32_t)texels;
        m_rcStats.probes[i] = c.counts.w;
        m_rcStats.dirRes[i] = c.dirRes;
        m_rcStats.tStart[i] = c.tStart;
        m_rcStats.tEnd[i] = c.tEnd;
        m_rcStats.rays += texels;
    }
    for (int i = 0; i < count; ++i) {   // parent visibility masks after the texels, one element per probe
        rc.c[i].maskOffset = offset;
        offset += rc.c[i].counts.w;
    }
    rc.cascadeCount = (uint32_t)count;
    rc.frame = m_giFrame;
    rc.flags = (rs.rcBounces ? RC_FLAG_BOUNCES : 0u) | (rs.rcHysteresis > 0 ? RC_FLAG_ANIMATED_NOISE : 0u) |
               (rs.rcBilinearFix ? RC_FLAG_BILINEAR_FIX : 0u);
    rc.lightSamples = (uint32_t)std::max(1, rs.rcLightSamples);
    rc.hysteresis = std::max(0.0f, std::min(rs.rcHysteresis, 0.99f));
    memcpy(m_rcConstants.mapped, &rc, sizeof(rc));

    const uint64_t bytes = (uint64_t)offset * 16;
    if (!m_rcBuffer.res || m_rcBuffer.size < bytes) {
        waitIdle();   // the previous frame may still read the old buffer
        m_rcBuffer = createBuffer(bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    m_cmd->SetComputeRootConstantBufferView(16, m_rcConstants.va());
    m_cmd->SetComputeRootUnorderedAccessView(17, m_rcBuffer.va());
    m_cmd->SetPipelineState(m_psoRCVisibility.Get());
    for (int i = 0; i + 1 < count && !rs.rcBilinearFix; ++i) {   // the bilinear fix traces its own joins
        const uint32_t pass[4] = {(uint32_t)i, 0, 0, 0};
        m_cmd->SetComputeRoot32BitConstants(1, 4, pass, 0);
        m_cmd->Dispatch((rc.c[i].counts.w + 63) / 64, 1, 1);
    }
    uavBarrier(m_rcBuffer.res.Get());
    m_cmd->SetPipelineState(m_psoRCTrace.Get());
    for (int i = count - 1; i >= 0; --i) {
        const uint32_t pass[4] = {(uint32_t)i, 0, 0, 0};
        m_cmd->SetComputeRoot32BitConstants(1, 4, pass, 0);
        const uint64_t texels = (uint64_t)rc.c[i].counts.w * rc.c[i].dirRes * rc.c[i].dirRes;
        m_cmd->Dispatch((UINT)((texels + 63) / 64), 1, 1);
        uavBarrier(m_rcBuffer.res.Get());
    }
    m_cmd->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 2);
    m_cmd->SetPipelineState(m_psoRCGather.Get());
    m_cmd->Dispatch(rc.c[0].counts.w, 1, 1);
    uavBarrier();
    m_cmd->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 3);
}

// Screen-space cascades over the viewport: cascade i has a probe per (spacing * 2^i)^2 pixels on the visible surface,
// (dirRes0 * 2^i)^2 directions and the interval [L0 (4^i - 1) / 3, L0 (4^(i+1) - 1) / 3] (the top one to infinity),
// so every cascade traces about the same number of rays. Cascades are added until one probe covers the viewport.
// Probes (RCSProbes), cascades from the top down (RCSTrace), then the per-pixel gather into the screen GI texture.
// Expects recordRC to have run this frame (its probes are the cache of the further bounces) and its bindings.
void Renderer::recordRCScreen(const RenderSettings& rs, UINT gx, UINT gy) {
    gpu::RCSConstants sc = {};
    const uint32_t W = m_rtW, H = m_rtH;
    const uint32_t s0 = (uint32_t)std::max(2, std::min(rs.rcScreenSpacing, 32));
    const uint32_t d0 = (uint32_t)std::max(2, std::min(rs.rcScreenDirRes, 8));
    const float L0 = std::max(0.01f, rs.rcScreenInterval) * m_giSpacing;
    uint32_t count = 0, texels = 0;
    for (uint32_t i = 0; i < RCS_MAX_CASCADES; ++i) {
        gpu::RCSCascade& c = sc.c[i];
        const uint32_t tile = s0 << i;
        c.grid = {(W + tile - 1) / tile, (H + tile - 1) / tile, tile, d0 << i};
        c.offset = texels;
        c.tStart = L0 * (std::pow(4.0f, (float)i) - 1.0f) / 3.0f;
        c.tEnd = L0 * (std::pow(4.0f, (float)(i + 1)) - 1.0f) / 3.0f;
        texels += c.grid.x * c.grid.y * c.grid.w * c.grid.w;
        ++count;
        // stop when the next cascade would see the whole viewport from one probe, or once the rays cross the scene
        if ((c.grid.x <= 2 && c.grid.y <= 2) || c.tEnd >= 2.0f * m_sceneRadius) break;
    }
    sc.c[count - 1].tEnd = 1e30f;
    uint32_t probes = texels;
    for (uint32_t i = 0; i < count; ++i) {
        sc.c[i].probeOffset = probes;
        probes += sc.c[i].grid.x * sc.c[i].grid.y * 2;
    }
    sc.shOffset = probes;   // radiance SH of the cascade-0 probes (RCSProbeSH), 7 float4 each
    probes += sc.c[0].grid.x * sc.c[0].grid.y * 7;
    sc.cascadeCount = count;
    sc.planeTolerance = std::max(L0, 1e-3f);
    memcpy(m_rcsConstants.mapped, &sc, sizeof(sc));
    m_rcStats.screenCascades = (int)count;
    m_rcStats.screenRays = texels;

    const uint64_t bytes = (uint64_t)probes * 16;
    if (!m_rcsBuffer.res || m_rcsBuffer.size < bytes) {
        waitIdle();
        m_rcsBuffer = createBuffer(bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    D3D12_GPU_DESCRIPTOR_HANDLE screenTable = m_heap->GetGPUDescriptorHandleForHeapStart();
    screenTable.ptr += (UINT64)kScreenGIUavSlot * m_descSize;
    m_cmd->SetComputeRootConstantBufferView(18, m_rcsConstants.va());
    m_cmd->SetComputeRootUnorderedAccessView(19, m_rcsBuffer.va());
    m_cmd->SetComputeRootDescriptorTable(20, screenTable);

    m_cmd->SetPipelineState(m_psoRCSProbes.Get());
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t pass[4] = {i, 0, 0, 0};
        m_cmd->SetComputeRoot32BitConstants(1, 4, pass, 0);
        m_cmd->Dispatch((sc.c[i].grid.x * sc.c[i].grid.y + 63) / 64, 1, 1);
    }
    uavBarrier(m_rcsBuffer.res.Get());
    m_cmd->SetPipelineState(m_psoRCSTrace.Get());
    for (int i = (int)count - 1; i >= 0; --i) {
        const uint32_t pass[4] = {(uint32_t)i, 0, 0, 0};
        m_cmd->SetComputeRoot32BitConstants(1, 4, pass, 0);
        const uint32_t n = sc.c[i].grid.x * sc.c[i].grid.y * sc.c[i].grid.w * sc.c[i].grid.w;
        m_cmd->Dispatch((n + 63) / 64, 1, 1);
        uavBarrier(m_rcsBuffer.res.Get());
    }
    m_cmd->SetPipelineState(m_psoRCSProbeSH.Get());
    m_cmd->Dispatch((sc.c[0].grid.x * sc.c[0].grid.y + 63) / 64, 1, 1);
    uavBarrier(m_rcsBuffer.res.Get());
    m_cmd->SetPipelineState(m_psoRCSGather.Get());
    m_cmd->Dispatch(gx, gy, 1);
    uavBarrier();
    m_screenGIOn = true;
}

// Split Radiance Cascades (SRC*.hlsl), rebuilt every frame, no history: hash tables cleared, probes inserted around
// the visible surfaces and their parents allocated cascade by cascade, the ray budget split into contiguous R2
// segments, one ray per pixel traced and split into the cascades, the cascades merged from the top down, cascade 0
// integrated per probe, then the per-pixel gather into the screen GI texture (raster t7).
// Expects recordRC to have run this frame (its probes light the further bounces) and its bindings.
void Renderer::recordSplitRC(const RenderSettings& rs, UINT gx, UINT gy) {
    gpu::SplitRCConstants sc = {};
    const uint32_t W = m_rtW, H = m_rtH, pixels = W * H;
    const uint32_t count = (uint32_t)std::max(1, std::min(rs.rcSplitCascades, SRC_CASCADES));
    const auto* fc = static_cast<const gpu::FrameConstants*>(m_constants.mapped);
    const float tanHalf = std::max(fc->tanHalfFovY, 1e-3f);
    const float probePx = (float)std::max(2, std::min(rs.rcSplitProbePx, 64));
    // LOD 0 spacing: small (it only matters close to the camera, the LODs take over beyond lodDist). A LOD covers one
    // octave of distance; at its far end a cascade-0 cell is probePx pixels on screen (twice that at its near end).
    // The keys hold cells within +-256 of the camera's: the octave is capped at 120 cells.
    const float s0 = m_sceneRadius / 4096.0f;   // fine enough that close-ups stay near probePx (big probes: slow atomics)
    const float ratio = std::min((float)H / (4.0f * tanHalf * probePx), 120.0f);
    const float t0 = std::max(0.25f, rs.rcSplitInterval) * s0;
    const uint32_t maxProbes0 = std::max(4096u, std::min(65536u, pixels / 24));
    uint32_t words = 16;   // counters
    uint32_t caps[SRC_CASCADES], maxProbes[SRC_CASCADES];
    for (uint32_t i = 0; i < count; ++i) {
        // upper bound: each cascade has about 4x fewer probes on a surface and 4x more directions (2x margin)
        uint32_t bound = i == 0 ? maxProbes0 : std::max(64u, maxProbes0 >> (2 * i - 1));
        bound = std::min(bound, maxProbes0);
        bound = std::min(bound, 65535u * 64u / ((8u << i) * (4u << i)));   // one dispatch per cascade
        uint32_t cap = 1024;   // hash slots for the bound (only keys: small)
        while (cap < bound * 2) cap <<= 1;
        caps[i] = cap;
        // probe records and entries for what the last frame used (x2), not for the bound: the bound's entries reach
        // hundreds of MB, which spill out of video memory beside another 3D app and make every pass 50x slower
        uint32_t& c = m_srcCapProbes[i];
        const uint32_t keys = m_srcKeys[i];
        const uint32_t lo = std::max(16u, 1024u >> (2 * i));   // the same few entries at least, whatever the cascade
        if (c == 0) c = std::max(lo, bound / 4);
        if (keys > c * 9 / 10) c = keys * 2;
        else if (keys * 4 < c) c = std::max(lo, keys * 2);
        c = std::min(c, bound);
        maxProbes[i] = c;
    }
    for (uint32_t i = 0; i < count; ++i) {   // slot keys right after the counters: one clear covers them all
        sc.c[i].keys = words;
        words += caps[i];
    }
    sc.clearWords = words;
    if (rs.giBoundsFade > 0) {
        sc.giMin = float4(rs.giBoundsMin.x, rs.giBoundsMin.y, rs.giBoundsMin.z, rs.giBoundsFade);
        sc.giMax = float4(rs.giBoundsMax.x, rs.giBoundsMax.y, rs.giBoundsMax.z, 1.0f);
    }
    m_rcStats.splitCascades = (int)count;
    for (uint32_t i = 0; i < count; ++i) {
        gpu::SRCCascade& c = sc.c[i];
        c.cap = caps[i];
        c.maxProbes = maxProbes[i];
        c.dirH = 4u << i;
        c.dirW = 2 * c.dirH;
        c.vals = words;
        words += caps[i];
        c.probes = words;
        words += maxProbes[i] * 8;
        c.entries = words;
        words += maxProbes[i] * c.dirW * c.dirH * 4;
        c.spacing = s0 * (float)(1u << i);
        c.tEnd = i + 1 == count ? 1e30f : t0 * std::pow(4.0f, (float)i);
        m_rcStats.splitCapacity[i] = maxProbes[i];
        m_rcStats.splitDirs[i] = c.dirW * c.dirH;
    }
    sc.irr = words;
    words += maxProbes[0] * SRC_IRR_N * SRC_IRR_N * 4;
    sc.hist = words;   // right after the irradiance: both copied to the history in one go
    words += maxProbes[0];
    sc.irrF = sc.irr;
    if (rs.rcSplitFilter) {   // the filtered copy the gather reads
        sc.irrF = words;
        words += maxProbes[0] * SRC_IRR_N * SRC_IRR_N * 4;
    }
    sc.pixels = words;
    words += pixels * 8;
    sc.cascadeCount = count;
    sc.counters = 0;
    sc.lodDist = s0 * ratio;
    sc.lodOverlap = 0.152f;   // the LODs overlap over the last 10 % of the distance (log2(0.9)), blended there
    sc.debug = (uint32_t)std::max(0, rs.rcSplitDebug);
    sc.debugCascade = (uint32_t)std::max(0, rs.rcSplitDebugCascade);
    sc.frame = m_giFrame;
    // History: the frames average only if the rays change, so the accumulation turns the jitter on.
    const bool histOn = rs.rcSplitHistory > 1;
    if (rs.rcSplitAnimate || histOn) {   // R2 jitter of the whole ray set and new light samples, per frame
        const uint32_t n = m_giFrame;
        sc.jitterX = (float)((n * 3242174889u) * (1.0 / 4294967296.0));
        sc.jitterY = (float)((n * 2447445413u) * (1.0 / 4294967296.0));
        sc.flags |= SRC_FLAG_ANIMATE;
    }
    if (histOn) {
        sc.histMax = (uint32_t)rs.rcSplitHistory;
        // a light, a material, the environment or a setting changed (m_sceneSignature), or the draws changed: all of
        // it is dropped; an object that moved drops it around its old and new place
        bool all = !m_srcPrevValid || m_sceneSignature != m_srcPrevSig || m_draws.size() != m_srcPrevXf.size();
        uint32_t moved = 0;
        for (size_t i = 0; i < m_draws.size() && !all; ++i) {
            const Affine& a = m_draws[i].xf;
            const Affine& b = m_srcPrevXf[i];
            if (memcmp(&a, &b, sizeof(Affine)) == 0) continue;
            if (moved == SRC_MAX_MOVED) { all = true; break; }
            const MeshGpu& m = m_meshes[m_draws[i].mesh];
            float3 lo(1e30f), hi(-1e30f);
            for (int c = 0; c < 16; ++c) {   // corners of the mesh box under the old and the new transform
                float3 p((c & 1) ? m.bmax.x : m.bmin.x, (c & 2) ? m.bmax.y : m.bmin.y, (c & 4) ? m.bmax.z : m.bmin.z);
                p = (c & 8) ? b.point(p) : a.point(p);
                lo = float3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z));
                hi = float3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z));
            }
            const float grow = 0.5f * length(hi - lo);   // its shadow and its bounce reach about its size around it
            sc.movedMin[moved] = {lo.x - grow, lo.y - grow, lo.z - grow, 0};
            sc.movedMax[moved] = {hi.x + grow, hi.y + grow, hi.z + grow, 0};
            ++moved;
        }
        sc.movedCount = moved;
        sc.prevValid = all ? 0u : 1u;
        sc.prevCamPos = m_srcPrevCam;
        sc.prevCap = m_srcPrevCap;
        sc.prevIrr = 2 * m_srcPrevCap;
        sc.prevHist = sc.prevIrr + m_srcPrevProbes * SRC_IRR_N * SRC_IRR_N * 4;
    }
    memcpy(m_srcConstants.mapped, &sc, sizeof(sc));
    m_rcStats.splitRays = pixels;
    m_rcStats.splitLodDist = sc.lodDist;
    m_rcStats.splitSpacing = s0;
    for (int i = 0; i < RC_MAX_CASCADES; ++i) m_rcStats.splitProbes[i] = m_srcKeys[i];

    const uint64_t bytes = (uint64_t)words * 4;
    if (!m_srcBuffer.res || m_srcBuffer.size < bytes) {
        waitIdle();
        m_srcBuffer = createBuffer(bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    D3D12_GPU_DESCRIPTOR_HANDLE screenTable = m_heap->GetGPUDescriptorHandleForHeapStart();
    screenTable.ptr += (UINT64)kScreenGIUavSlot * m_descSize;
    m_cmd->SetComputeRootConstantBufferView(18, m_srcConstants.va());
    m_cmd->SetComputeRootUnorderedAccessView(19, m_srcBuffer.va());
    m_cmd->SetComputeRootDescriptorTable(20, screenTable);
    const uint64_t prevBytes = ((uint64_t)caps[0] * 2 + (uint64_t)maxProbes[0] * (SRC_IRR_N * SRC_IRR_N * 4 + 1)) * 4;
    if (histOn && (!m_srcPrev.res || m_srcPrev.size < prevBytes)) {
        waitIdle();
        m_srcPrev = createBuffer(prevBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_srcPrevValid = false;
        sc.prevValid = 0;
        memcpy(m_srcConstants.mapped, &sc, sizeof(sc));
    }
    m_cmd->SetComputeRootUnorderedAccessView(22, m_srcPrev.res ? m_srcPrev.va() : m_srcBuffer.va());
    ID3D12Resource* buf = m_srcBuffer.res.Get();
    auto pass = [&](ID3D12PipelineState* pso, uint32_t ci, uint32_t mode, UINT x, UINT y = 1) {
        const uint32_t c[4] = {ci, mode, 0, 0};
        m_cmd->SetPipelineState(pso);
        m_cmd->SetComputeRoot32BitConstants(1, 4, c, 0);
        m_cmd->Dispatch(std::max(1u, x), y, 1);
        if (m_srcProfile && m_srcProfileN < 58) m_cmd->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 6 + m_srcProfileN++);
        uavBarrier(buf);
    };
    auto groups = [](uint64_t n) { return (UINT)((n + 63) / 64); };
    m_cmd->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 4);
    pass(m_psoSRCClear.Get(), 0, 0, groups(sc.clearWords));
    pass(m_psoSRCInsert.Get(), 0, 0, gx, gy);
    for (uint32_t i = 0; i < count; ++i) pass(m_psoSRCAlloc.Get(), i, 0, groups(caps[i]));
    for (uint32_t i = 0; i < count; ++i)
        pass(m_psoSRCClear.Get(), i, 1, groups((uint64_t)maxProbes[i] * sc.c[i].dirW * sc.c[i].dirH));
    pass(m_psoSRCCount.Get(), 0, 0, groups(pixels));
    for (uint32_t i = 0; i + 1 < count; ++i) pass(m_psoSRCCount.Get(), i, 1, groups(maxProbes[i]));
    for (int i = (int)count - 1; i >= 0; --i) pass(m_psoSRCCount.Get(), (uint32_t)i, 2, groups(maxProbes[i]));
    pass(m_psoSRCTrace.Get(), 0, 0, gx, gy);
    for (int i = (int)count - 1; i >= 0; --i)
        pass(m_psoSRCMerge.Get(), (uint32_t)i, 0, groups((uint64_t)maxProbes[i] * sc.c[i].dirW * sc.c[i].dirH));
    pass(m_psoSRCIrradiance.Get(), 0, 0, groups((uint64_t)maxProbes[0] * SRC_IRR_N * SRC_IRR_N));
    if (rs.rcSplitFilter) pass(m_psoSRCFilter.Get(), 0, 0, groups((uint64_t)maxProbes[0] * SRC_IRR_N * SRC_IRR_N));
    pass(m_psoSRCGather.Get(), 0, 0, gx, gy);
    uavBarrier();
    {   // the counters (probes per cascade), read back in present() for the statistics
        if (!m_srcReadback.res) m_srcReadback = createBuffer(256, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
        transition(buf, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        m_cmd->CopyBufferRegion(m_srcReadback.res.Get(), 0, buf, 0, 64);
        transition(buf, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    if (histOn) {   // this frame's cascade-0 slots, irradiance and history lengths become the next frame's history
        ID3D12Resource* prev = m_srcPrev.res.Get();
        transition(buf, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        transition(prev, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
        m_cmd->CopyBufferRegion(prev, 0, buf, (uint64_t)sc.c[0].keys * 4, (uint64_t)caps[0] * 4);
        m_cmd->CopyBufferRegion(prev, (uint64_t)caps[0] * 4, buf, (uint64_t)sc.c[0].vals * 4, (uint64_t)caps[0] * 4);
        m_cmd->CopyBufferRegion(prev, (uint64_t)caps[0] * 8, buf, (uint64_t)sc.irr * 4,
                                (uint64_t)maxProbes[0] * (SRC_IRR_N * SRC_IRR_N * 4 + 1) * 4);
        transition(buf, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        transition(prev, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_srcPrevCap = caps[0];
        m_srcPrevProbes = maxProbes[0];
        m_srcPrevCam = fc->camPos;
        m_srcPrevSig = m_sceneSignature;
        m_srcPrevXf.resize(m_draws.size());
        for (size_t i = 0; i < m_draws.size(); ++i) m_srcPrevXf[i] = m_draws[i].xf;
        m_srcPrevValid = true;
    } else {
        m_srcPrevValid = false;
    }
    m_cmd->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 5);
    m_splitTimed = true;
    m_screenGIOn = true;
}

// Ray traced sun shadow and reflections of the raster pass. SunShadowTrace writes the visibility of every pixel with
// its depth and normal (the guide of both filters), ReflectTrace the reflections of the glossy pixels, then the two
// bilateral filters. Leaves the filtered sun mask (t8) and reflections (t9) in PIXEL_SHADER_RESOURCE when asked for;
// recordRaster puts them back. Needs this frame's acceleration structure, frame and probe constants; binds the
// compute root signature.
void Renderer::recordScreenTrace(const float3& sunDir, const RenderSettings& rs, bool shadow, bool refl, int areaLight) {
    float3 L = normalize(sunDir);
    float s = std::abs(L.x) + std::abs(L.y) + std::abs(L.z);
    float ox = L.x / s, oy = L.y / s;
    if (L.z < 0) {   // octahedral encoding, as OctDec in the shader
        float nx = (1.0f - std::abs(oy)) * (ox >= 0 ? 1.0f : -1.0f), ny = (1.0f - std::abs(ox)) * (oy >= 0 ? 1.0f : -1.0f);
        ox = nx;
        oy = ny;
    }
    const float tanR = std::tan(radians(std::max(rs.sunAngle, 0.0f)) * 0.5f);
    // without the shadow, one ray per pixel: the pass still writes the depth and normal the reflection filter needs
    // shadow quality: rays per pixel (sun; a lamp, a wide source, twice as many) and filter radius (pixels)
    const int q = std::max(0, std::min(rs.shadowQuality, 4));
    const uint32_t sunRays[5] = {2, 4, 8, 16, 32}, sunRadius[5] = {1, 2, 2, 3, 4}, lampRadius[5] = {2, 3, 4, 6, 8};
    const uint32_t samples = shadow ? (q == 2 ? (uint32_t)std::max(1, std::min(rs.rtShadowSamples, 32)) : sunRays[q]) : 1u;
    uint32_t c[4];
    memcpy(&c[0], &ox, 4);
    memcpy(&c[1], &oy, 4);
    memcpy(&c[2], &tanR, 4);
    c[3] = samples | (sunRadius[q] << 24);
    if (shadow && areaLight >= 0) {   // area light: its index in bits 8..23, the filter radius in 24..31
        const float area = -1.0f;
        memcpy(&c[2], &area, 4);
        c[3] = std::min(samples * 2, 64u) | ((uint32_t)areaLight << 8) | (lampRadius[q] << 24);
    }
    m_cmd->SetComputeRootSignature(m_rootSig.Get());
    m_cmd->SetComputeRootConstantBufferView(0, m_constants.va());
    m_cmd->SetComputeRoot32BitConstants(1, 4, c, 0);
    m_cmd->SetComputeRootShaderResourceView(2, m_tlas.va());
    m_cmd->SetComputeRootShaderResourceView(3, m_vb.va());
    m_cmd->SetComputeRootShaderResourceView(4, m_ib.va());
    m_cmd->SetComputeRootShaderResourceView(5, m_instances.va());
    m_cmd->SetComputeRootShaderResourceView(6, m_materials.va());
    m_cmd->SetComputeRootShaderResourceView(7, m_lights.va());
    m_cmd->SetComputeRootDescriptorTable(8, m_heap->GetGPUDescriptorHandleForHeapStart());
    m_cmd->SetComputeRootDescriptorTable(9, textureTable());
    m_cmd->SetComputeRootConstantBufferView(10, m_giConstants.va());   // probes lighting the reflected surfaces
    D3D12_GPU_DESCRIPTOR_HANDLE giTable = m_heap->GetGPUDescriptorHandleForHeapStart();
    giTable.ptr += (UINT64)kGIHeapBase * m_descSize;
    m_cmd->SetComputeRootDescriptorTable(11, giTable);
    D3D12_GPU_DESCRIPTOR_HANDLE envTable = m_heap->GetGPUDescriptorHandleForHeapStart();
    envTable.ptr += (UINT64)kEnvHeapSlot * m_descSize;
    m_cmd->SetComputeRootShaderResourceView(13, m_envDist.va());
    m_cmd->SetComputeRootDescriptorTable(14, envTable);
    D3D12_GPU_DESCRIPTOR_HANDLE sunTable = m_heap->GetGPUDescriptorHandleForHeapStart();
    sunTable.ptr += (UINT64)kSunShadowUavSlot * m_descSize;
    m_cmd->SetComputeRootDescriptorTable(21, sunTable);
    const UINT gx = (m_rtW + 7) / 8, gy = (m_rtH + 7) / 8;
    m_cmd->SetPipelineState(m_psoSunTrace.Get());
    m_cmd->Dispatch(gx, gy, 1);
    if (refl) {
        const float maxRough = std::max(0.0f, rs.reflMaxRough);
        uint32_t r[4] = {(uint32_t)std::max(1, std::min(rs.reflSamples, 32)), 0, (m_lastProbes && m_giTex[0] && m_giTex[1] && m_giTex[3]) ? 1u : 0u, 0};
        memcpy(&r[1], &maxRough, 4);
        m_cmd->SetComputeRoot32BitConstants(1, 4, r, 0);
        // the raster pass reads the probe atlases as SRVs at this point: back to UAV for the compute pass meanwhile
        ID3D12Resource* atlases[3] = {m_giTex[0].Get(), m_giTex[1].Get(), m_giTex[3].Get()};
        const bool withProbes = m_lastProbes && atlases[0] && atlases[1] && atlases[2];   // (HQ raster: no probe volume)
        if (withProbes) for (ID3D12Resource* a : atlases) transition(a, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_cmd->SetPipelineState(m_psoReflTrace.Get());
        m_cmd->Dispatch(gx, gy, 1);
        if (withProbes) for (ID3D12Resource* a : atlases) transition(a, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    uavBarrier();
    if (shadow) {
        m_cmd->SetPipelineState(m_psoSunFilter.Get());
        m_cmd->Dispatch(gx, gy, 1);
    }
    if (refl) {
        m_cmd->SetPipelineState(m_psoReflFilter.Get());
        m_cmd->Dispatch(gx, gy, 1);
    }
    uavBarrier();
    if (shadow) transition(m_sunShadow.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    transition(m_sunShadowRaw.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);   // t10
    if (refl) transition(m_refl.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

// Copies ACCUM and NORMAL into the history textures, after the frame's compute work and before the copy to the back buffer.
void Renderer::copyHistory() {
    uavBarrier();
    const TexSlot src[2] = {TEX_ACCUM, TEX_NORMALDEPTH};
    for (int i = 0; i < 2; ++i) {
        transition(m_tex[src[i]].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        transition(m_hist[i].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
        m_cmd->CopyResource(m_hist[i].Get(), m_tex[src[i]].Get());
        transition(m_hist[i].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        transition(m_tex[src[i]].Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
}

// Hash of the render settings that change the accumulated radiance (not tone mapping or denoising).
// A change drops the history, otherwise the old image would stay on screen for up to the history length.
uint32_t renderSignature(const RenderSettings& rs) {
    uint32_t h = 2166136261u;
    auto mix = [&](const void* p, size_t n) {
        const unsigned char* b = static_cast<const unsigned char*>(p);
        for (size_t i = 0; i < n; ++i) {
            h ^= b[i];
            h *= 16777619u;
        }
    };
    mix(&rs.raster, sizeof(bool));
    mix(&rs.ddgi, sizeof(bool));
    mix(&rs.historyFixes, sizeof(bool));
    mix(&rs.resetOnMove, sizeof(bool));
    mix(&rs.ddgiView, sizeof(int));
    mix(&rs.ddgiShowProbes, sizeof(bool));
    mix(&rs.ddgiProbeBudget, sizeof(int));
    mix(&rs.ddgiRaster, sizeof(bool));
    mix(&rs.ddgiCaptureProbes, sizeof(int));
    mix(&rs.ddgiHysteresis, sizeof(float));
    mix(&rs.giMethod, sizeof(int));
    mix(&rs.rcProbeBudget, sizeof(int));
    mix(&rs.rcCascades, sizeof(int));
    mix(&rs.rcDirRes, sizeof(int));
    mix(&rs.rcInterval, sizeof(float));
    mix(&rs.rcBounces, sizeof(bool));
    mix(&rs.rcLightSamples, sizeof(int));
    mix(&rs.rcHysteresis, sizeof(float));
    mix(&rs.rcBilinearFix, sizeof(bool));
    mix(&rs.rcScreen, sizeof(bool));
    mix(&rs.rcScreenSpacing, sizeof(int));
    mix(&rs.rcScreenDirRes, sizeof(int));
    mix(&rs.rcScreenInterval, sizeof(float));
    mix(&rs.rcSplit, sizeof(bool));
    mix(&rs.rcSplitProbePx, sizeof(int));
    mix(&rs.rcSplitInterval, sizeof(float));
    mix(&rs.rcSplitCascades, sizeof(int));
    mix(&rs.rcSplitAnimate, sizeof(bool));
    mix(&rs.rcSplitHistory, sizeof(int));
    mix(&rs.rcSplitFilter, sizeof(bool));
    mix(&rs.aaQuality, sizeof(int));
    mix(&rs.shadowQuality, sizeof(int));
    mix(&rs.rtSunShadows, sizeof(bool));
    mix(&rs.sunAngle, sizeof(float));
    mix(&rs.rtShadowSamples, sizeof(int));
    mix(&rs.rtReflections, sizeof(bool));
    mix(&rs.reflSamples, sizeof(int));
    mix(&rs.reflMaxRough, sizeof(float));
    mix(&rs.maxBounces, sizeof(int));
    mix(&rs.bevel, sizeof(bool));
    mix(&rs.bevelScale, sizeof(float));
    mix(&rs.bevelSamples, sizeof(int));
    mix(&rs.fireflyClamp, sizeof(float));
    mix(&rs.filterGlossy, sizeof(float));
    mix(&rs.indirectClamp, sizeof(float));
    mix(&rs.sssScale, sizeof(float));
    mix(&rs.lightScale, sizeof(float));
    return h;
}

// Hash of everything that changes the lighting of a frame except the camera and the object transforms, which
// the reprojection handles. A change here drops the history.
uint32_t Renderer::sceneSig(const Scene& s) {
    if (!s.objectsVersion) return sceneSignature(s);
    if (s.objectsVersion != m_sigVersion) {
        m_sigVersion = s.objectsVersion;
        m_cachedSceneSig = sceneSignature(s);
        m_cachedTransformSig = transformSignature(s);
    }
    return m_cachedSceneSig;
}

uint32_t Renderer::transformSig(const Scene& s) {
    if (!s.objectsVersion) return transformSignature(s);
    sceneSig(s);
    return m_cachedTransformSig;
}

uint32_t Renderer::sceneSignature(const Scene& s) const {
    uint32_t h = 2166136261u;
    auto mix = [&](const void* p, size_t n) {
        const unsigned char* b = static_cast<const unsigned char*>(p);
        for (size_t i = 0; i < n; ++i) {
            h ^= b[i];
            h *= 16777619u;
        }
    };
    for (const Material& m : s.materials) {
        mix(&m.baseColor, sizeof(m.baseColor));
        mix(&m.roughness, sizeof(float));
        mix(&m.metallic, sizeof(float));
        mix(&m.specular, sizeof(float));
        mix(&m.transmission, sizeof(float));
        mix(&m.ior, sizeof(float));
        mix(&m.subsurface, sizeof(float));
        mix(&m.sssRadius, sizeof(m.sssRadius));
        mix(&m.emission, sizeof(m.emission));
        mix(&m.emissionStrength, sizeof(float));
        mix(&m.bevelRadius, sizeof(float));
        mix(&m.coat, sizeof(float));
        mix(&m.coatRoughness, sizeof(float));
        mix(&m.flags, sizeof(uint32_t));
    }
    for (const AreaLight& l : s.lights) {
        mix(&l.position, sizeof(l.position));
        mix(&l.target, sizeof(l.target));
        mix(&l.width, sizeof(float));
        mix(&l.height, sizeof(float));
        mix(&l.color, sizeof(l.color));
        mix(&l.intensity, sizeof(float));
        mix(&l.enabled, sizeof(bool));
        mix(&l.visibleToCamera, sizeof(bool));
    }
    mix(&s.env.top, sizeof(s.env.top));
    mix(&s.env.horizon, sizeof(s.env.horizon));
    mix(&s.env.bottom, sizeof(s.env.bottom));
    mix(&s.env.intensity, sizeof(float));
    const void* hdri = s.env.hdri.get();
    mix(&hdri, sizeof(hdri));
    mix(&s.env.hdriIntensity, sizeof(float));
    mix(&s.env.hdriRotation, sizeof(float));
    mix(&s.env.visibleToCamera, sizeof(bool));
    for (const Object& o : s.objects) {
        mix(&o.material, sizeof(o.material));
        mix(&o.mesh, sizeof(o.mesh));
        mix(&o.visible, sizeof(bool));
        mix(&o.ghost, sizeof(bool));
    }
    mix(&s.gridScale, sizeof(float));
    mix(&s.gridLine, sizeof(uint32_t));
    return h;
}

// Where the objects are and the sun: a change of these is a move, which fades the history (see render).
// Quantized (1e-4): a settling spring must not change it every frame.
uint32_t Renderer::transformSignature(const Scene& s) const {
    uint32_t h = 2166136261u;
    auto mix = [&](const void* p, size_t n) {
        const unsigned char* b = static_cast<const unsigned char*>(p);
        for (size_t i = 0; i < n; ++i) {
            h ^= b[i];
            h *= 16777619u;
        }
    };
    auto mixQ = [&](float v) {
        const int32_t q = (int32_t)std::lround(v * 1e4f);
        mix(&q, sizeof(q));
    };
    for (const Object& o : s.objects) {
        mixQ(o.position.x); mixQ(o.position.y); mixQ(o.position.z);
        mixQ(o.scale.x); mixQ(o.scale.y); mixQ(o.scale.z);
        mixQ(o.rot.x); mixQ(o.rot.y); mixQ(o.rot.z); mixQ(o.rot.w);
        mix(&o.useRot, sizeof(bool));
    }
    mixQ(s.sun.direction.x); mixQ(s.sun.direction.y); mixQ(s.sun.direction.z);
    mixQ(s.sun.intensity);
    mix(&s.sun.enabled, sizeof(bool));
    return h;
}

void Renderer::render(const Scene& scene, const RenderSettings& rs, const ViewRect& vpIn, int selectedObject,
                      bool toBackBuffer) {
    ViewRect vp = vpIn;
    vp.x = std::max(0, std::min(vp.x, (int)m_width - 1));
    vp.y = std::max(0, std::min(vp.y, (int)m_height - 1));
    vp.w = std::max(1, std::min(vp.w, (int)m_width - vp.x));
    vp.h = std::max(1, std::min(vp.h, (int)m_height - vp.y));
    setMsaa((uint32_t)std::max(1, rs.msaaSamples));   // viewport anti-aliasing (rebuilds the MSAA passes when it changes)
    // GI mode, raster while moving: the camera, the objects or the lighting changed -> the raster view of the probes
    // (the probes keep updating), until giSettleFrames still frames; then the ray traced image, faded in from the last
    // raster image over giFadeFrames (no jump)
    if (!m_giInMoveSwitch) {   // (the raster frame drawn below for the switch keeps them)
        m_giKeepRaster = false;
        m_giFadeT = 1.0f;
    }
    const bool giTracedMode = rs.ddgi && !rs.raster && !rs.ddgiRaster && !rs.ddgiRasterView && rs.giMethod == 0 && !rs.bakedProbes;
    if (rs.giRasterWhileMoving && giTracedMode && !m_giInMoveSwitch) {
        uint32_t k = sceneSig(scene) ^ (transformSig(scene) * 2654435761u) ^ (renderSignature(rs) * 40503u);
        auto mixc = [&](const void* p, size_t n) { const unsigned char* c = (const unsigned char*)p; for (size_t i = 0; i < n; ++i) { k ^= c[i]; k *= 16777619u; } };
        const Camera& cam = scene.camera;
        mixc(&cam.target, sizeof(cam.target)); mixc(&cam.yaw, sizeof(float)); mixc(&cam.pitch, sizeof(float));
        mixc(&cam.distance, sizeof(float)); mixc(&cam.fovY, sizeof(float)); mixc(&cam.ortho, sizeof(bool)); mixc(&cam.orthoHeight, sizeof(float));
        mixc(&vp, sizeof(vp));
        if (k != m_giMoveKey) { m_giMoveKey = k; m_giMoveStill = 0; }
        else if (m_giMoveStill < (1 << 20)) ++m_giMoveStill;
        if (m_giMoveStill < std::max(0, rs.giSettleFrames)) {
            RenderSettings r2 = rs;
            r2.ddgiRasterView = true;
            r2.aaQuality = 0;   // (the same MSAA as the traced view: no rebuild of the targets at each switch)
            m_giInMoveSwitch = true;
            m_giKeepRaster = !rs.denoise && m_psoGIFade;   // its image is where the fade starts
            render(scene, r2, vpIn, selectedObject, toBackBuffer);
            m_giInMoveSwitch = false;
            return;
        }
        const int f = m_giMoveStill - std::max(0, rs.giSettleFrames);
        if (!rs.denoise && m_psoGIFade && f < rs.giFadeFrames && m_giMoveStill < (1 << 20))
            m_giFadeT = (f + 1.0f) / (float)std::max(1, rs.giFadeFrames);
    }
    auto selCovTable = [&]() {   // t8 of the tonemapper: coverage of the selection
        D3D12_GPU_DESCRIPTOR_HANDLE g = m_heap->GetGPUDescriptorHandleForHeapStart();
        g.ptr += (UINT64)kSelCovSlot * m_descSize;
        return g;
    };
    if ((uint32_t)vp.w != m_rtW || (uint32_t)vp.h != m_rtH) {
        waitIdle();
        createTargets(vp.w, vp.h);
    }
    if (scene.geometryVersion != m_sceneGeometryVersion) setScene(scene);
    if (scene.env.hdri && scene.env.hdri != m_envImage) uploadEnvironment(scene.env.hdri);
    const bool envMap = scene.env.hdri && scene.env.hdri == m_envImage && m_envW > 0;
    // A move (object or sun) does not wipe the history: it fades to new samples over rs.resetDelay seconds, so the
    // trail of the old place disappears in that time and the noise of the new place is short. Off: nothing happens.
    if (rs.historyFixes && rs.resetOnMove) {
        const uint32_t tsig = transformSig(scene);
        if (tsig != m_transformSignature) {
            if (m_transformSignature != 0 && m_historyValid) {
                m_fadeTotal = std::max(2, (int)std::lround(rs.resetDelay * 60.0f));
                m_fadeFrames = m_fadeTotal;
            }
            m_transformSignature = tsig;
        }
    }
    const uint32_t sig = sceneSig(scene) ^ (renderSignature(rs) * 16777619u);
    // progressive probes: a change of the lights, the environment, the materials or the settings restarts them all
    // (the objects added, removed or moved restart only the probes around them, recordDDGI)
    if (rs.ddgiProgressive && !rs.ddgiHold) {   // (held: the red of a blocked brick or the dim focus restart nothing)
        uint32_t lk = renderSignature(rs);
        auto mixk = [&](const void* p, size_t n) { const unsigned char* c = (const unsigned char*)p; for (size_t i = 0; i < n; ++i) { lk ^= c[i]; lk *= 16777619u; } };
        for (const Material& m : scene.materials) {
            mixk(&m.baseColor, sizeof(m.baseColor)); mixk(&m.roughness, sizeof(float)); mixk(&m.metallic, sizeof(float));
            mixk(&m.transmission, sizeof(float)); mixk(&m.emission, sizeof(m.emission)); mixk(&m.emissionStrength, sizeof(float));
        }
        for (const AreaLight& l : scene.lights) {
            mixk(&l.position, sizeof(l.position)); mixk(&l.target, sizeof(l.target)); mixk(&l.color, sizeof(l.color));
            mixk(&l.intensity, sizeof(float)); mixk(&l.enabled, sizeof(bool));
        }
        const void* hdri = scene.env.hdri.get();
        mixk(&hdri, sizeof(hdri));
        mixk(&scene.env.hdriIntensity, sizeof(float));
        mixk(&scene.env.hdriRotation, sizeof(float));
        mixk(&scene.env.intensity, sizeof(float));
        mixk(&scene.sun.direction, sizeof(scene.sun.direction));
        mixk(&scene.sun.intensity, sizeof(float));
        mixk(&scene.sun.enabled, sizeof(bool));
        if (lk != m_giStillKey) { m_giStillKey = lk; m_giStill = 0; }
    }
    if (sig != m_sceneSignature) {
        m_sceneSignature = sig;
        m_historyValid = false;
    }
    // The raster main pass reads the probes (no image history) in three cases: live probes (Metal iOS: captured in
    // raster, or ray traced with --ddgi-raster-view), a probe bake in Lit mode (ray traced probes updated each frame
    // of the bake), and Lit mode reading a finished or loaded bake (no probe computation at all).
    const bool liveProbes = rs.ddgi && (rs.ddgiRaster || rs.ddgiRasterView) && !rs.raster;
    const bool baking = rs.raster && m_bakeFrames > 0;
    const bool bakedView = rs.raster && rs.bakedProbes && m_bakeValid && !baking;
    const bool giRaster = liveProbes || baking || bakedView;
    const bool useHistory = rs.ddgi && !rs.raster && !giRaster;
    const bool needAS = (!rs.raster && !(liveProbes && rs.ddgiRaster)) || baking || (rs.raster && ((rs.bevel && rs.rasterBevel) || rs.rasterRT));

    // AA quality of the GI raster modes: the sample count of the viewport (Lit and the others keep the MSAA of the
    // Display menu, rs.msaaSamples, set by setMsaa in render())
    if (!rs.raster && giRaster) {
        const uint32_t msaa = (rs.aaQuality >= 1 && m_msaa8) ? 8u : 4u;
        if (msaa != m_msaa || m_msaaTemporal) setMsaa(msaa);
    }

    beginCommands();
    m_giTimed = false;
    m_splitTimed = false;
    m_srcProfileN = 0;
    buildTLAS(scene, needAS);
    m_asThisFrame = needAS;

    const Camera& cam = scene.camera;
    float3 right, up, fwd;
    cam.basis(right, up, fwd);
    gpu::FrameConstants fc = {};
    fc.camPos = cam.eye();
    fc.aperture = cam.apertureRadius();
    fc.camRight = right;
    fc.focusDist = std::max(cam.focusDistance, 1e-3f);
    fc.camUp = up;
    fc.tanHalfFovY = std::tan(cam.fovY * 0.5f);
    fc.camFwd = fwd;
    fc.aspect = (float)vp.w / (float)vp.h;
    fc.orthoHalfH = cam.ortho ? cam.orthoHeight * 0.5f : 0.0f;
    fc.orthoBack = cam.ortho ? orthoBack(cam) : 0.0f;
    fc.resolution = {(uint32_t)vp.w, (uint32_t)vp.h};
    fc.frameIndex = m_frameIndex++;
    fc.sampleIndex = m_sampleIndex;
    fc.maxBounces = (uint32_t)std::max(0, rs.maxBounces);   // 0: primary rays only (profiling)
    fc.lightCount = m_lightCount;
    fc.samplesPerFrame = (uint32_t)std::max(1, rs.samplesPerFrame);
    fc.flags = (cam.dof ? FRAME_FLAG_DOF : 0) | (rs.bevel ? FRAME_FLAG_BEVEL : 0) | (rs.denoise ? FRAME_FLAG_DENOISE : 0) |
               (scene.env.visibleToCamera ? FRAME_FLAG_ENV_VISIBLE : 0) | (rs.ddgi && !rs.raster ? FRAME_FLAG_ID_EDGES : 0) |
               (rs.ddgi && !rs.raster && rs.historyFixes ? FRAME_FLAG_CENTRE_VALID : 0u) |
               (envMap ? FRAME_FLAG_ENV_MAP : 0u);
    fc.envTop = scene.env.top;
    fc.envIntensity = scene.env.intensity;
    fc.envHorizon = scene.env.horizon;
    fc.exposure = rs.exposure;
    fc.envBottom = scene.env.bottom;
    fc.bevelScale = rs.bevelScale;
    int selInst = (selectedObject >= 0 && selectedObject < (int)m_objectToInstance.size()) ? m_objectToInstance[selectedObject] : -1;
    fc.selectedInstance = (uint32_t)(selInst + 1);
    fc.selMoreCount = 0;
    m_selIds.clear();
    if (selInst >= 0) m_selIds.push_back((uint32_t)(selInst + 1));
    fc.selAge = rs.selectionAge;
    fc.selCenterX = rs.selectionCenter.x;
    fc.selCenterY = rs.selectionCenter.y;
    fc.selRadius = rs.selectionRadius;
    fc.selStyle = (uint32_t)rs.selectionStyle;
    fc.selFill = rs.selectionFill;
    fc.selPixelScale = rs.pixelScale;
    for (int o : m_extraSelection) {
        if (fc.selMoreCount >= 32 || o < 0 || o >= (int)m_objectToInstance.size() || m_objectToInstance[o] < 0) continue;
        reinterpret_cast<uint32_t*>(fc.selMore)[fc.selMoreCount++] = (uint32_t)(m_objectToInstance[o] + 1);
        m_selIds.push_back((uint32_t)(m_objectToInstance[o] + 1));
    }
    // the raster pass draws the coverage of the selection whenever there is one (the path traced view gets the overlay pass)
    fc.selCovValid = (!m_selIds.empty() && m_psoSelect && m_psoDepth) ? 1u : 0u;
    fc.traceStride = (rs.traceStride == 4 && m_sampleIndex > 0) ? 4u : 1u;
    fc.fireflyClamp = rs.fireflyClamp;
    fc.sssScale = rs.sssScale;
    fc.gridScale = scene.gridScale;
    fc.gridLine = scene.gridLine;
    fc.denoiseStrength = rs.denoiseStrength;
    fc.denoiseEdgeLuma = rs.denoiseEdgeLuma;
    fc.lightScale = rs.lightScale;
    fc.vignette = rs.vignette;
    fc.bevelSamples = (float)std::max(1, rs.bevelSamples);
    fc.historyValid = (useHistory && m_historyValid && !(rs.ddgi && rs.ddgiShowProbes)) ? 1u : 0u;
    // during a fade the history counts from 2 up to the full length: the old place fades out in resetDelay
    float histCap = (float)std::max(1, rs.temporalCap);
    if (m_fadeFrames > 0) {
        const float t = 1.0f - m_fadeFrames / (float)m_fadeTotal;
        histCap = std::max(2.0f, histCap * t);
        --m_fadeFrames;
    }
    fc.historyCap = histCap;
    fc.prevCamPos = m_prevCam.pos;
    fc.prevCamRight = m_prevCam.right;
    fc.prevCamUp = m_prevCam.up;
    fc.prevCamFwd = m_prevCam.fwd;
    fc.prevTanHalfFovY = m_prevCam.tanHalf;
    fc.prevAspect = m_prevCam.aspect;
    fc.prevOrthoHalfH = m_prevCam.orthoHalfH;
    fc.prevOrthoBack = m_prevCam.orthoBack;
    fc.envW = envMap ? m_envW : 0;
    fc.envH = envMap ? m_envH : 0;
    fc.envRotation = scene.env.hdriRotation;
    fc.envMapIntensity = scene.env.hdriIntensity;
    fc.envPdfScale = m_envPdfScale;
    fc.envMips = m_envMips;
    fc.filterGlossy = rs.filterGlossy;
    fc.indirectClamp = rs.indirectClamp;
    memcpy(m_constants.mapped, &fc, sizeof(fc));
    static_assert(sizeof(fc) <= sizeof(m_lastFc), "m_lastFc holds the frame constants");
    memcpy(m_lastFc, &fc, sizeof(fc));   // the extra views of this frame start from them (renderView)
    m_lastProbes = giRaster;

    ID3D12DescriptorHeap* heaps[] = {m_heap.Get()};
    m_cmd->SetDescriptorHeaps(1, heaps);
    UINT gx = (vp.w + 7) / 8, gy = (vp.h + 7) / 8;
    if (!(rs.raster && !giRaster)) m_taaValid = false;
    if (rs.raster && !giRaster) {
        // MSAA x16 over two frames (m_msaaTemporal): the frames are averaged only while nothing that moves the image
        // changes (camera, objects, view size, display)
        uint64_t sig = 1469598103934665603ull;
        if (m_msaaTemporal) {
            const Camera& c = scene.camera;
            const float k[] = {c.target.x, c.target.y, c.target.z, c.yaw, c.pitch, c.distance, c.fovY, (float)vp.w, (float)vp.h,
                               (float)rs.wireframe, rs.editorGrid, c.ortho ? c.orthoHeight : 0.0f};
            const uint32_t n[2] = {transformSig(scene), scene.geometryVersion};
            auto mix = [&](const void* p, size_t len) {
                const unsigned char* b = static_cast<const unsigned char*>(p);
                for (size_t i = 0; i < len; ++i) { sig ^= b[i]; sig *= 1099511628211ull; }
            };
            mix(k, sizeof(k));
            mix(n, sizeof(n));
        }
        m_cmd->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
        recordRaster(scene, rs, vp);
        if (rs.ssao) recordSSAO(rs, vp, scene);
        m_denoisedSlot = 0xFFFFFFFF;
        m_sampleIndex = (uint32_t)std::max(rs.maxSamples, 1);
        m_cmd->SetComputeRootSignature(m_rootSig.Get());
        m_cmd->SetComputeRootConstantBufferView(0, m_constants.va());
        m_cmd->SetComputeRootDescriptorTable(8, m_heap->GetGPUDescriptorHandleForHeapStart());
        m_cmd->SetPipelineState(m_psoTonemap.Get());
        m_cmd->SetComputeRootDescriptorTable(23, selCovTable());
        const bool blend = m_msaaTemporal && m_taaValid && sig == m_taaSig;
        uint32_t rc[4] = {TEX_DENOISE_A, 1u, (uint32_t)rs.tonemapper, blend ? (uint32_t)TEX_DENOISE_B + 1u : 0u};
        m_cmd->SetComputeRoot32BitConstants(1, 4, rc, 0);
        m_cmd->Dispatch(gx, gy, 1);
        if (m_msaaTemporal) {   // this frame becomes the history of the next one
            ID3D12Resource* cur = m_tex[TEX_DENOISE_A].Get();
            ID3D12Resource* hist = m_tex[TEX_DENOISE_B].Get();
            transition(cur, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
            transition(hist, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
            m_cmd->CopyResource(hist, cur);
            transition(hist, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            transition(cur, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            m_taaSig = sig;
            m_taaValid = true;
            ++m_taaFrame;
        } else m_taaValid = false;
    } else if (giRaster) {
        // Probe captures (raster), probe rays + blend (compute), then the viewport lit by the raster pass reading the
        // probes, and the tonemapper. Nothing here needs ray tracing.
        m_cmd->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
        if (!bakedView) setupProbeGrid(rs);
        if (liveProbes && rs.ddgiRaster) {
            // Warm-up: every probe in a few frames; then the requested batch (at most kCaptureMax per frame).
            const uint32_t slots = m_capWarmup > 0 ? kCaptureMax : (uint32_t)std::max(1, rs.ddgiCaptureProbes);
            ensureCapture(std::min(std::min(giProbeCount(), kCaptureMax), slots));
            recordRaster(scene, rs, vp, true);   // shadow map + cube captures of the next probes
        }
        m_cmd->SetComputeRootSignature(m_rootSig.Get());
        m_cmd->SetComputeRootConstantBufferView(0, m_constants.va());
        // Raster probes have no acceleration structure: the slot gets a valid buffer, unused.
        m_cmd->SetComputeRootShaderResourceView(2, needAS ? m_tlas.va() : m_instances.va());
        m_cmd->SetComputeRootShaderResourceView(3, m_vb.va());
        m_cmd->SetComputeRootShaderResourceView(4, m_ib.va());
        m_cmd->SetComputeRootShaderResourceView(5, m_instances.va());
        m_cmd->SetComputeRootShaderResourceView(6, m_materials.va());
        m_cmd->SetComputeRootShaderResourceView(7, m_lights.va());
        m_cmd->SetComputeRootDescriptorTable(8, m_heap->GetGPUDescriptorHandleForHeapStart());
        m_cmd->SetComputeRootDescriptorTable(9, textureTable());
        D3D12_GPU_DESCRIPTOR_HANDLE histTable = m_heap->GetGPUDescriptorHandleForHeapStart();
        histTable.ptr += (UINT64)(kGIHeapBase + kGITexCount) * m_descSize;
        m_cmd->SetComputeRootDescriptorTable(12, histTable);
        D3D12_GPU_DESCRIPTOR_HANDLE envTable = m_heap->GetGPUDescriptorHandleForHeapStart();
        envTable.ptr += (UINT64)kEnvHeapSlot * m_descSize;
        m_cmd->SetComputeRootShaderResourceView(13, m_envDist.va());
        m_cmd->SetComputeRootDescriptorTable(14, envTable);
        if (baking) {
            // Ray traced probes (as the GI mode), converged over the bake: a longer hysteresis averages the random rays.
            RenderSettings rb = rs;
            rb.ddgiRaster = false;
            rb.ddgiRasterView = false;
            rb.ddgiHysteresis = 0.95f;
            rb.ddgiView = 0;
            rb.ddgiShowProbes = false;
            recordDDGI(rb, gx, gy, false);
            if (--m_bakeFrames == 0) m_bakeValid = true;
        } else if (!bakedView) {
            recordDDGI(rs, gx, gy, false);
            m_bakeValid = false;   // live probes overwrite the atlases
        } else {
            // Baked probes: only the grid constants for the raster pass (no update).
            lit::ProbeGrid grid;
            grid.origin = m_giOrigin;
            grid.spacing = m_giSpacing;
            for (int i = 0; i < 3; ++i) grid.counts[i] = m_giCounts[i];
            lit::DDGIFrame f;
            f.frame = 1;
            f.view = rs.ddgiView;
            f.showProbes = rs.ddgiShowProbes;
            gpu::DDGIConstants c;
            lit::ddgiConstants(grid, f, m_giRng, c);
            memcpy(m_giConstants.mapped, &c, sizeof(c));
        }
        // The raster pass reads the irradiance, distance and state atlases (SRVs t4..t6).
        ID3D12Resource* atlases[4] = {m_giTex[0].Get(), m_giTex[1].Get(), m_giTex[3].Get(), m_screenGI.Get()};   // + screen GI (t7)
        for (ID3D12Resource* r : atlases) transition(r, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        recordRaster(scene, rs, vp, false, true, !(liveProbes && rs.ddgiRaster));   // the capture pass already drew the shadow map
        for (ID3D12Resource* r : atlases) transition(r, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_denoisedSlot = 0xFFFFFFFF;
        ++m_sampleIndex;
        m_cmd->SetComputeRootSignature(m_rootSig.Get());
        m_cmd->SetComputeRootConstantBufferView(0, m_constants.va());
        m_cmd->SetComputeRootDescriptorTable(8, m_heap->GetGPUDescriptorHandleForHeapStart());
        m_cmd->SetPipelineState(m_psoTonemap.Get());
        m_cmd->SetComputeRootDescriptorTable(23, selCovTable());
        uint32_t rc[4] = {TEX_DENOISE_A, 1u, (uint32_t)rs.tonemapper, 0};
        m_cmd->SetComputeRoot32BitConstants(1, 4, rc, 0);
        m_cmd->Dispatch(gx, gy, 1);
        if (m_giKeepRaster) {   // GI mode, raster while moving: this image is kept for the fade to the traced one
            uavBarrier();
            m_cmd->SetPipelineState(m_psoGIFade.Get());
            uint32_t fk[4] = {0u, 0u, 0u, 0u};
            m_cmd->SetComputeRoot32BitConstants(1, 4, fk, 0);
            m_cmd->Dispatch(gx, gy, 1);
        }
    } else {
    bindCompute();

    bool traced = false;
    m_cmd->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
    // Open Image Denoise (rs.oidn): a new accumulation drops its image (the a-trous filter shows the moving view);
    // once the view stayed still a frame, OIDN denoises at the samples of the schedule (oidnDue), its image faded in
    const bool oidnOn = rs.denoise && rs.oidn && !rs.ddgi && m_psoOidn && !m_oidn.failed() && oidnAvailable();
    const uint32_t before = m_sampleIndex;
    if (before == 0 || !oidnOn) { m_oidnShown = false; m_oidnLast = 0; }
    bool oidnRun = false;
    if (oidnOn && before > 0) {
        const uint32_t after = before < (uint32_t)rs.maxSamples ? before + fc.samplesPerFrame : before;
        oidnRun = oidnDue(after, rs);
    }
    if (oidnRun) {   // the image shown by the last frame: where the fade to the new denoised image starts
        const uint64_t bytes = (uint64_t)vp.w * vp.h * 16;
        if (m_oidnFrom.size < bytes)
            m_oidnFrom = createBuffer(bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_cmd->SetPipelineState(m_psoOidn.Get());
        m_cmd->SetComputeRootUnorderedAccessView(22, m_oidnFrom.va());
        const uint32_t from = m_oidnShown ? (0x80000000u | (uint32_t)TEX_DENOISE_A)
                                          : (rs.denoise && m_denoisedSlot != 0xFFFFFFFFu ? m_denoisedSlot : 0xFFFFFFFFu);
        uint32_t c[4] = {1u, from, 0u, 0u};
        m_cmd->SetComputeRoot32BitConstants(1, 4, c, 0);
        m_cmd->Dispatch(gx, gy, 1);
        uavBarrier();
    }
    if (rs.ddgi) {   // ray traced probes and visibility (the raster probes take the giRaster branch above)
        recordDDGI(rs, gx, gy);   // never converges: probes and image history keep updating
        ++m_sampleIndex;
        traced = true;
    } else if (m_sampleIndex < (uint32_t)rs.maxSamples) {
        m_cmd->SetPipelineState(m_psoTrace.Get());
        if (fc.traceStride == 4) m_cmd->Dispatch((vp.w / 2 + 8) / 8, (vp.h / 2 + 8) / 8, 1);
        else m_cmd->Dispatch(gx, gy, 1);
        uavBarrier();
        m_sampleIndex += fc.samplesPerFrame;
        traced = true;
    }
    if (oidnRun) {
        if (runOidn(gx, gy, (uint32_t)vp.w, (uint32_t)vp.h)) {
            m_oidnShown = true;
            m_oidnFade = 0;
        }
        m_oidnLast = m_sampleIndex;
    }

    // The denoised image shown: OIDN's, faded in from the last image; else the edge-aware a-trous denoiser
    // (rs.denoisePasses passes), only when new samples arrived.
    uint32_t& denoisedSlot = m_denoisedSlot;
    uint32_t shownSlot = 0xFFFFFFFFu;   // OIDN: the slot of its image (radiance, not demodulated)
    if (oidnOn && m_oidnShown) {
        m_cmd->SetPipelineState(m_psoOidn.Get());
        m_cmd->SetComputeRootUnorderedAccessView(19, m_oidn.output()->GetGPUVirtualAddress());
        m_cmd->SetComputeRootUnorderedAccessView(22, m_oidnFrom.va());
        const float t = rs.oidnBlendFrames <= 0 ? 1.0f : std::min(1.0f, (m_oidnFade + 1) / (float)rs.oidnBlendFrames);
        if (m_oidnFade < (1 << 20)) ++m_oidnFade;
        uint32_t c[4] = {2u, (uint32_t)TEX_DENOISE_A, 0u, 0u};
        memcpy(&c[2], &t, sizeof(float));
        m_cmd->SetComputeRoot32BitConstants(1, 4, c, 0);
        m_cmd->Dispatch(gx, gy, 1);
        uavBarrier();
        shownSlot = TEX_DENOISE_A;
        denoisedSlot = 0xFFFFFFFFu;   // the a-trous image is gone: redone when it is shown again
    } else if (rs.denoise) {
        if (traced || denoisedSlot == 0xFFFFFFFF) {
            m_cmd->SetPipelineState(m_psoDenoise.Get());
            uint32_t src = TEX_ACCUM, dst = TEX_DENOISE_A;
            for (uint32_t pass = 0; pass < (uint32_t)std::max(1, rs.denoisePasses); ++pass) {
                uint32_t c[4] = {src, dst, 1u << pass, pass == 0 ? 1u : 0u};
                m_cmd->SetComputeRoot32BitConstants(1, 4, c, 0);
                m_cmd->Dispatch(gx, gy, 1);
                uavBarrier();
                src = dst;
                dst = (dst == TEX_DENOISE_A) ? TEX_DENOISE_B : TEX_DENOISE_A;
            }
            denoisedSlot = src;
        }
    } else {
        denoisedSlot = 0xFFFFFFFF;
    }

    // editor overlay (grid, wireframe) drawn by the raster pass into the denoise slot not holding the image
    uint32_t overlay = 0;
    if (m_psoDepth && (rs.editorGrid > 0 || rs.wireframe || !m_selIds.empty() || !m_edgeObjects.empty())) {   // (also draws the coverage of the selection)
        const uint32_t slot = (rs.denoise && (denoisedSlot == TEX_DENOISE_A || shownSlot == TEX_DENOISE_A)) ? TEX_DENOISE_B : TEX_DENOISE_A;
        recordRaster(scene, rs, vp, false, false, false, (int)slot);
        overlay = slot + 1;
    }
    m_cmd->SetPipelineState(m_psoTonemap.Get());
    m_cmd->SetComputeRootDescriptorTable(23, selCovTable());
    const uint32_t tonemapSlot = shownSlot != 0xFFFFFFFFu ? (0x80000000u | shownSlot) : (rs.denoise ? denoisedSlot : 0xFFFFFFFFu);
    uint32_t tc[4] = {tonemapSlot, 0, (uint32_t)rs.tonemapper, overlay};
    m_cmd->SetComputeRoot32BitConstants(1, 4, tc, 0);
    m_cmd->Dispatch(gx, gy, 1);
    if (m_giFadeT < 1.0f) {   // GI mode, raster while moving: from the last raster image to the traced one
        uavBarrier();
        m_cmd->SetPipelineState(m_psoGIFade.Get());
        uint32_t fk[4] = {1u, 0u, 0u, 0u};
        memcpy(&fk[1], &m_giFadeT, sizeof(float));
        m_cmd->SetComputeRoot32BitConstants(1, 4, fk, 0);
        m_cmd->Dispatch(gx, gy, 1);
    }
    }
    if (useHistory) {
        copyHistory();
        m_historyValid = true;
        m_prevCam = {fc.camPos, fc.camRight, fc.camUp, fc.camFwd, fc.tanHalfFovY, fc.aspect, fc.orthoHalfH, fc.orthoBack};
    }
    m_cmd->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
    m_cmd->ResolveQueryData(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, m_splitTimed ? 6 + m_srcProfileN : (m_giTimed ? 4 : 2),
                            m_queryReadback.res.Get(), 0);

    if (!toBackBuffer) {
        executeAndWait();
        return;
    }
    if (m_toTexture) {
        transition(m_tex[TEX_OUTPUT].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        m_cmd->Close();
        ID3D12CommandList* l[] = {m_cmd.Get()};
        m_queue->ExecuteCommandLists(1, l);
        return;
    }

    // Copy the viewport image into the back buffer.
    ID3D12Resource* bb = m_backBuffers[backBufferIndex()].Get();
    ID3D12Resource* out = m_tex[TEX_OUTPUT].Get();
    transition(out, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition(bb, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION dl = {}, sl = {};
    dl.pResource = bb;
    dl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    sl.pResource = out;
    sl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_BOX box = {0, 0, 0, (UINT)vp.w, (UINT)vp.h, 1};
    m_cmd->CopyTextureRegion(&dl, vp.x, vp.y, 0, &sl, &box);
    transition(out, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    transition(bb, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);

    m_cmd->Close();
    ID3D12CommandList* l[] = {m_cmd.Get()};
    m_queue->ExecuteCommandLists(1, l);
}

// Compute root signature and tables of the path tracing passes (also after runOidn reopened the command list).
void Renderer::bindCompute() {
    ID3D12DescriptorHeap* heaps[] = {m_heap.Get()};
    m_cmd->SetDescriptorHeaps(1, heaps);
    m_cmd->SetComputeRootSignature(m_rootSig.Get());
    m_cmd->SetComputeRootConstantBufferView(0, m_constants.va());
    m_cmd->SetComputeRootShaderResourceView(2, m_tlas.va());
    m_cmd->SetComputeRootShaderResourceView(3, m_vb.va());
    m_cmd->SetComputeRootShaderResourceView(4, m_ib.va());
    m_cmd->SetComputeRootShaderResourceView(5, m_instances.va());
    m_cmd->SetComputeRootShaderResourceView(6, m_materials.va());
    m_cmd->SetComputeRootShaderResourceView(7, m_lights.va());
    m_cmd->SetComputeRootDescriptorTable(8, m_heap->GetGPUDescriptorHandleForHeapStart());
    m_cmd->SetComputeRootDescriptorTable(9, textureTable());
    D3D12_GPU_DESCRIPTOR_HANDLE histTable = m_heap->GetGPUDescriptorHandleForHeapStart();
    histTable.ptr += (UINT64)(kGIHeapBase + kGITexCount) * m_descSize;
    m_cmd->SetComputeRootDescriptorTable(12, histTable);   // u10, u11: temporal history
    D3D12_GPU_DESCRIPTOR_HANDLE envTable = m_heap->GetGPUDescriptorHandleForHeapStart();
    envTable.ptr += (UINT64)kEnvHeapSlot * m_descSize;
    m_cmd->SetComputeRootShaderResourceView(13, m_envDist.va());
    m_cmd->SetComputeRootDescriptorTable(14, envTable);
}

bool Renderer::oidnAvailable() {
    static const bool present = Oidn::dllPresent();
    return present;
}

// Samples per pixel at which OIDN denoises: each doubling up to 32 (1 sample needs a still frame first, so 2, 4, 8,
// 16, 32), then each multiple of rs.oidnInterval, and the final image (maxSamples), each once per accumulation.
bool Renderer::oidnDue(uint32_t samples, const RenderSettings& rs) const {
    if (samples == 0 || samples <= m_oidnLast) return false;
    if (samples >= (uint32_t)std::max(1, rs.maxSamples)) return true;
    if (m_oidnLast < 32) return samples >= std::max(1u, 2 * m_oidnLast);
    const uint32_t interval = (uint32_t)std::max(1, rs.oidnInterval);
    return samples / interval > m_oidnLast / interval;
}

// The accumulation packed for OIDN, the frame so far executed and waited for, OIDN run, then the command list
// reopened with the compute bindings (the rest of the frame follows). False when OIDN failed (its status says why).
bool Renderer::runOidn(UINT gx, UINT gy, uint32_t w, uint32_t h) {
    if (!m_oidn.ensureDevice(m_device.Get()) || !m_oidn.resize(m_device.Get(), w, h)) return false;
    m_cmd->SetPipelineState(m_psoOidn.Get());
    m_cmd->SetComputeRootUnorderedAccessView(17, m_oidn.input()->GetGPUVirtualAddress());
    uint32_t c[4] = {0u, 0u, 0u, 0u};
    m_cmd->SetComputeRoot32BitConstants(1, 4, c, 0);
    m_cmd->Dispatch(gx, gy, 1);
    uavBarrier();
    if (!m_oidn.shared()) {
        transition(m_oidn.input(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        m_cmd->CopyBufferRegion(m_oidn.readback(), 0, m_oidn.input(), 0, m_oidn.inputBytes());
        transition(m_oidn.input(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    executeAndWait();
    const bool ok = m_oidn.run();
    beginCommands();
    bindCompute();
    if (ok && !m_oidn.shared()) {
        transition(m_oidn.output(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
        m_cmd->CopyBufferRegion(m_oidn.output(), 0, m_oidn.upload(), 0, m_oidn.outputBytes());
        transition(m_oidn.output(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    return ok;
}

float Renderer::orthoBack(const Camera& cam) const {
    float3 r, u, f;
    cam.basis(r, u, f);
    // the nearest point of the bounding sphere of the scene along the view, from the eye (negative: behind it)
    const float nearest = dot(m_sceneCenter - cam.eye(), f) - m_sceneRadius;
    return std::max(0.0f, -nearest) + m_sceneRadius * 0.05f + 1.0f;
}

void Renderer::renderToTexture(const Scene& scene, const RenderSettings& rs, const ViewRect& vp, int selectedObject) {
    m_toTexture = true;
    render(scene, rs, vp, selectedObject, true);
    m_toTexture = false;
}

void Renderer::present(bool vsync) {
    m_swapChain->Present(vsync ? 1 : 0, 0);
    waitIdle();
    m_drawBufUsed = 0;
    m_drawGrave.clear();
    void* p = nullptr;
    D3D12_RANGE r = {0, 512};
    if (SUCCEEDED(m_queryReadback.res->Map(0, &r, &p))) {
        uint64_t* ts = (uint64_t*)p;
        if (ts[1] > ts[0]) m_gpuMs = (float)((ts[1] - ts[0]) * 1000.0 / m_tsFreq);
        if (m_giTimed && ts[2] >= ts[0] && ts[3] >= ts[2]) {
            m_giUpdateMs = (float)((ts[2] - ts[0]) * 1000.0 / m_tsFreq);
            m_giBlendMs = (float)((ts[3] - ts[2]) * 1000.0 / m_tsFreq);
        }
        if (m_splitTimed && ts[5] >= ts[4]) m_splitMs = (float)((ts[5] - ts[4]) * 1000.0 / m_tsFreq);
        if (m_splitTimed && m_srcProfileN > 0) {   // per pass: clear, insert, alloc x C, clear x C, count..., trace, merge x C, irr, filter, gather
            srcProfile.clear();
            uint64_t prev = ts[4];
            for (uint32_t i = 0; i < m_srcProfileN; ++i) {
                char b[32];
                snprintf(b, sizeof(b), "%.2f ", (ts[6 + i] - prev) * 1000.0 / m_tsFreq);
                srcProfile += b;
                prev = ts[6 + i];
            }
        }
        if (m_splitTimed && m_srcReadback.res) {   // keys inserted per cascade (above the capacity: probes dropped)
            void* q = nullptr;
            D3D12_RANGE rr = {0, 64};
            if (SUCCEEDED(m_srcReadback.res->Map(0, &rr, &q))) {
                for (int i = 0; i < RC_MAX_CASCADES; ++i) m_srcKeys[i] = ((const uint32_t*)q)[i];
                D3D12_RANGE nw = {0, 0};
                m_srcReadback.res->Unmap(0, &nw);
            }
        }
        D3D12_RANGE w = {0, 0};
        m_queryReadback.res->Unmap(0, &w);
    }
}

bool Renderer::readBackBuffer(std::vector<uint8_t>& rgba, uint32_t& w, uint32_t& h) {
    waitIdle();
    ID3D12Resource* bb = m_backBuffers[backBufferIndex()].Get();
    D3D12_RESOURCE_DESC d = bb->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {};
    UINT64 total = 0;
    m_device->GetCopyableFootprints(&d, 0, 1, 0, &fp, nullptr, nullptr, &total);
    if (!m_readback.res || m_readback.size < total)
        m_readback = createBuffer(total, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    beginCommands();
    transition(bb, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION dl = {}, sl = {};
    dl.pResource = m_readback.res.Get();
    dl.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dl.PlacedFootprint = fp;
    sl.pResource = bb;
    sl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    m_cmd->CopyTextureRegion(&dl, 0, 0, 0, &sl, nullptr);
    transition(bb, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
    executeAndWait();
    w = (uint32_t)d.Width;
    h = d.Height;
    void* p = nullptr;
    D3D12_RANGE r = {0, (SIZE_T)total};
    if (FAILED(m_readback.res->Map(0, &r, &p))) return false;
    rgba.resize((size_t)w * h * 4);
    for (uint32_t y = 0; y < h; ++y)
        memcpy(&rgba[(size_t)y * w * 4], (uint8_t*)p + fp.Offset + (size_t)y * fp.Footprint.RowPitch, (size_t)w * 4);
    D3D12_RANGE wr = {0, 0};
    m_readback.res->Unmap(0, &wr);
    return true;
}

bool Renderer::readSelectionCoverage(std::vector<uint8_t>& r8, uint32_t& w, uint32_t& h) {
    if (!m_selCov) return false;
    waitIdle();
    D3D12_RESOURCE_DESC d = m_selCov->GetDesc();
    w = (uint32_t)d.Width;
    h = d.Height;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {};
    UINT64 total = 0;
    m_device->GetCopyableFootprints(&d, 0, 1, 0, &fp, nullptr, nullptr, &total);
    if (!m_readback.res || m_readback.size < total)
        m_readback = createBuffer(total, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    beginCommands();
    transition(m_selCov.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION dl = {}, sl = {};
    dl.pResource = m_readback.res.Get();
    dl.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dl.PlacedFootprint = fp;
    sl.pResource = m_selCov.Get();
    sl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    m_cmd->CopyTextureRegion(&dl, 0, 0, 0, &sl, nullptr);
    transition(m_selCov.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    executeAndWait();
    void* p = nullptr;
    D3D12_RANGE r = {0, (SIZE_T)total};
    if (FAILED(m_readback.res->Map(0, &r, &p))) return false;
    r8.resize((size_t)w * h);
    for (uint32_t y = 0; y < h; ++y) memcpy(&r8[(size_t)y * w], (uint8_t*)p + fp.Offset + (size_t)y * fp.Footprint.RowPitch, w);
    D3D12_RANGE wr = {0, 0};
    m_readback.res->Unmap(0, &wr);
    return true;
}

bool Renderer::takeDebugMessages(std::vector<std::string>& messages, int& errors, int& warnings) {
    errors = warnings = 0;
    if (!m_infoQueue) return false;
    const UINT64 n = m_infoQueue->GetNumStoredMessages();
    for (UINT64 i = 0; i < n; ++i) {
        SIZE_T len = 0;
        if (FAILED(m_infoQueue->GetMessage(i, nullptr, &len)) || len == 0) continue;
        std::vector<char> buf(len);
        D3D12_MESSAGE* m = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
        if (FAILED(m_infoQueue->GetMessage(i, m, &len))) continue;
        const bool err = m->Severity == D3D12_MESSAGE_SEVERITY_ERROR || m->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION;
        if (err) ++errors;
        else if (m->Severity == D3D12_MESSAGE_SEVERITY_WARNING) ++warnings;
        else continue;
        messages.push_back(std::string(err ? "error: " : "warning: ") + std::string(m->pDescription, m->DescriptionByteLength ? m->DescriptionByteLength - 1 : 0));
    }
    m_infoQueue->ClearStoredMessages();
    return true;
}

bool Renderer::readViewport(std::vector<uint8_t>& rgba, uint32_t& w, uint32_t& h) {
    if (!m_tex[TEX_OUTPUT]) return false;
    waitIdle();
    w = m_rtW;
    h = m_rtH;
    D3D12_RESOURCE_DESC d = m_tex[TEX_OUTPUT]->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {};
    UINT64 total = 0;
    m_device->GetCopyableFootprints(&d, 0, 1, 0, &fp, nullptr, nullptr, &total);
    if (!m_readback.res || m_readback.size < total)
        m_readback = createBuffer(total, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    beginCommands();
    transition(m_tex[TEX_OUTPUT].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION dl = {}, sl = {};
    dl.pResource = m_readback.res.Get();
    dl.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dl.PlacedFootprint = fp;
    sl.pResource = m_tex[TEX_OUTPUT].Get();
    sl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    m_cmd->CopyTextureRegion(&dl, 0, 0, 0, &sl, nullptr);
    transition(m_tex[TEX_OUTPUT].Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    executeAndWait();
    void* p = nullptr;
    D3D12_RANGE r = {0, (SIZE_T)total};
    if (FAILED(m_readback.res->Map(0, &r, &p))) return false;
    rgba.resize((size_t)w * h * 4);
    for (uint32_t y = 0; y < h; ++y)
        memcpy(&rgba[(size_t)y * w * 4], (uint8_t*)p + fp.Offset + (size_t)y * fp.Footprint.RowPitch, (size_t)w * 4);
    D3D12_RANGE wr = {0, 0};
    m_readback.res->Unmap(0, &wr);
    return true;
}

} // namespace vp3d


namespace vp3d {

static float halfToFloat(uint16_t h) {
    uint32_t s = (h >> 15) & 1, e = (h >> 10) & 0x1F, m = h & 0x3FF, f;
    if (e == 0) f = (s << 31) | 0;                                  // zero / denormal -> 0
    else if (e == 31) f = (s << 31) | 0x7F800000 | (m << 13);      // inf / nan
    else f = (s << 31) | ((e + 112) << 23) | (m << 13);
    float out;
    memcpy(&out, &f, 4);
    return out;
}

bool Renderer::readCapture(std::vector<float>& rgba, uint32_t& w, uint32_t& h) {
    if (!m_capTex) return false;
    waitIdle();
    D3D12_RESOURCE_DESC d = m_capTex->GetDesc();
    w = (uint32_t)d.Width;
    h = d.Height;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {};
    UINT64 total = 0;
    m_device->GetCopyableFootprints(&d, 0, 1, 0, &fp, nullptr, nullptr, &total);
    Buffer rb = createBuffer(total, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    beginCommands();
    transition(m_capTex.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION dl = {}, sl = {};
    dl.pResource = rb.res.Get();
    dl.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dl.PlacedFootprint = fp;
    sl.pResource = m_capTex.Get();
    sl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    m_cmd->CopyTextureRegion(&dl, 0, 0, 0, &sl, nullptr);
    transition(m_capTex.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    executeAndWait();
    void* p = nullptr;
    D3D12_RANGE r = {0, (SIZE_T)total};
    if (FAILED(rb.res->Map(0, &r, &p))) return false;
    rgba.resize((size_t)w * h * 4);
    for (uint32_t y = 0; y < h; ++y) {
        const uint16_t* row = (const uint16_t*)((const uint8_t*)p + fp.Offset + (size_t)y * fp.Footprint.RowPitch);
        for (uint32_t x = 0; x < w * 4; ++x) rgba[(size_t)y * w * 4 + x] = halfToFloat(row[x]);
    }
    D3D12_RANGE wr = {0, 0};
    rb.res->Unmap(0, &wr);
    return true;
}

} // namespace vp3d
