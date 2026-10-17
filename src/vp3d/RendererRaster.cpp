// Light raster viewport of vp3d::Renderer (no ray tracing): shadow map + forward PBR.
#include "Renderer.h"
#include "shaders/Shared.h"
#include <cstring>
#include <fstream>

namespace vp3d {

namespace {

constexpr int kTexAlbedo = 1;
constexpr int kTexDenoiseA = 3;

struct RasterConstants {
    float viewProj[16];
    float lightViewProj[16];
    float3 camPos; float time;
    float3 camRight; float tanHalfFovY;
    float3 camUp; float aspect;
    float3 camFwd; uint32_t lightCount;
    float3 envTop; float envIntensity;
    float3 envHorizon; float gridScale;
    float3 envBottom; uint32_t keyLight;
    float3 ambient; float shadowTexel;
    float3 keyPos; float lightScale;
    float3 sunDir; float sunOn;
    float3 sunRadiance; float sunFromEnv;
    float shIrr[9][4];
    float envMapOn; float envRotation; float envMapIntensity; float envMips;
    float envVisible; uint32_t sunLight; float giMode, pad3;   // giMode: DDGI probes read by Shade (Raster.hlsl)
};

struct DrawConstants {
    float rows[12];
    uint32_t materialId;
    uint32_t id;
    float alpha;
    uint32_t flags;
};

bool readBlob(const std::wstring& path, std::vector<char>& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    out.resize((size_t)f.tellg());
    f.seekg(0);
    f.read(out.data(), out.size());
    return true;
}

D3D12_HEAP_PROPERTIES defaultHeap() {
    D3D12_HEAP_PROPERTIES p = {};
    p.Type = D3D12_HEAP_TYPE_DEFAULT;
    return p;
}

void setRows(float* m, int row, const float3& v, float w) {
    m[row * 4 + 0] = v.x;
    m[row * 4 + 1] = v.y;
    m[row * 4 + 2] = v.z;
    m[row * 4 + 3] = w;
}

// Cube faces of a probe capture (+X -X +Y -Y +Z -Z): forward and up, right = cross(up, forward). Same as DDGIRaster.hlsl.
const float3 kProbeFwd[6] = {float3(1, 0, 0), float3(-1, 0, 0), float3(0, 1, 0), float3(0, -1, 0), float3(0, 0, 1), float3(0, 0, -1)};
const float3 kProbeUp[6] = {float3(0, 1, 0), float3(0, 1, 0), float3(0, 0, -1), float3(0, 0, 1), float3(0, 1, 0), float3(0, 1, 0)};

} // namespace

bool Renderer::createRasterPipelines(std::string* error) {
    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 1;

    D3D12_DESCRIPTOR_RANGE texRange = {};
    texRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    texRange.NumDescriptors = kMaxTextures;
    texRange.BaseShaderRegister = 0;
    texRange.RegisterSpace = 1;

    D3D12_DESCRIPTOR_RANGE envRange = {};
    envRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    envRange.NumDescriptors = 1;
    envRange.BaseShaderRegister = 3;

    D3D12_DESCRIPTOR_RANGE probeRange = {};   // t4..t6 probe atlases (DDGI without ray tracing)
    probeRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    probeRange.NumDescriptors = 3;
    probeRange.BaseShaderRegister = 4;

    D3D12_ROOT_PARAMETER p[10] = {};
    p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    p[0].Descriptor.ShaderRegister = 0;
    p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    p[1].Constants.ShaderRegister = 1;
    p[1].Constants.Num32BitValues = 16;
    p[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    p[2].Descriptor.ShaderRegister = 0;
    p[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    p[3].Descriptor.ShaderRegister = 2;
    p[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    p[4].DescriptorTable.NumDescriptorRanges = 1;
    p[4].DescriptorTable.pDescriptorRanges = &range;
    p[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    p[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    p[5].DescriptorTable.NumDescriptorRanges = 1;
    p[5].DescriptorTable.pDescriptorRanges = &texRange;
    p[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    p[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;   // t3 HDRI
    p[6].DescriptorTable.NumDescriptorRanges = 1;
    p[6].DescriptorTable.pDescriptorRanges = &envRange;
    p[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    p[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;   // b3 probe face (DDGI capture), vertex + pixel
    p[7].Constants.ShaderRegister = 3;
    p[7].Constants.Num32BitValues = 20;
    p[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;   // t4..t6 probe atlases, pixel
    p[8].DescriptorTable.NumDescriptorRanges = 1;
    p[8].DescriptorTable.pDescriptorRanges = &probeRange;
    p[8].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    p[9].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;   // b2 DDGI constants, pixel
    p[9].Descriptor.ShaderRegister = 2;
    p[9].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC ss = {};
    ss.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    ss.AddressU = ss.AddressV = ss.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    ss.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    ss.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    ss.MaxLOD = D3D12_FLOAT32_MAX;
    ss.ShaderRegister = 0;
    ss.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC samplers[2] = {ss, {}};
    D3D12_STATIC_SAMPLER_DESC& ts = samplers[1];   // material textures: anisotropic, wrap
    ts.Filter = D3D12_FILTER_ANISOTROPIC;
    ts.MaxAnisotropy = 8;
    ts.AddressU = ts.AddressV = ts.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    ts.MaxLOD = D3D12_FLOAT32_MAX;
    ts.ShaderRegister = 1;
    ts.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsd = {};
    rsd.NumParameters = 10;
    rsd.pParameters = p;
    rsd.NumStaticSamplers = 2;
    rsd.pStaticSamplers = samplers;
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> blob, err;
    if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err))) {
        if (error) *error = err ? (const char*)err->GetBufferPointer() : "raster root signature";
        return false;
    }
    m_device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_rasterRoot));

    auto load = [&](const wchar_t* name, std::vector<char>& code) {
        if (readBlob(m_shaderDir + L"\\" + name, code)) return true;
        if (error) {
            std::wstring w = name;
            *error = "Missing shader: " + std::string(w.begin(), w.end());
        }
        return false;
    };
    std::vector<char> vsMain, vsWire, vsShadow, vsBg, vsProbe, psMain, psGhost, psId, psWire, psBg, psProbe, psProbeBg;
    if (!load(L"Raster_VSMain.cso", vsMain) || !load(L"Raster_VSWire.cso", vsWire) || !load(L"Raster_VSShadow.cso", vsShadow) || !load(L"Raster_VSBackground.cso", vsBg) ||
        !load(L"Raster_VSProbe.cso", vsProbe) || !load(L"Raster_PSMain.cso", psMain) || !load(L"Raster_PSGhost.cso", psGhost) ||
        !load(L"Raster_PSId.cso", psId) || !load(L"Raster_PSWire.cso", psWire) || !load(L"Raster_PSBackground.cso", psBg) || !load(L"Raster_PSProbe.cso", psProbe) ||
        !load(L"Raster_PSProbeBackground.cso", psProbeBg))
        return false;

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    auto base = [&]() {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC d = {};
        d.pRootSignature = m_rasterRoot.Get();
        d.InputLayout = {layout, 3};
        d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        d.RasterizerState.DepthClipEnable = TRUE;
        d.RasterizerState.MultisampleEnable = TRUE;
        d.RasterizerState.AntialiasedLineEnable = FALSE;
        d.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        d.DepthStencilState.DepthEnable = TRUE;
        d.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        d.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        d.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        d.SampleMask = UINT_MAX;
        d.SampleDesc.Count = 1;
        return d;
    };
    auto blendOn = [](D3D12_GRAPHICS_PIPELINE_STATE_DESC& d) {
        auto& b = d.BlendState.RenderTarget[0];
        b.BlendEnable = TRUE;
        b.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        b.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        b.BlendOp = D3D12_BLEND_OP_ADD;
        b.SrcBlendAlpha = D3D12_BLEND_ONE;
        b.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        b.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        d.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    };
    auto make = [&](D3D12_GRAPHICS_PIPELINE_STATE_DESC& d, ComPtr<ID3D12PipelineState>& pso, const char* what) {
        if (FAILED(m_device->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&pso)))) {
            if (error) *error = std::string("CreateGraphicsPipelineState failed: ") + what;
            return false;
        }
        return true;
    };

    // Shadow (depth only)
    auto d = base();
    d.VS = {vsShadow.data(), vsShadow.size()};
    d.RasterizerState.DepthBias = 200;
    d.RasterizerState.SlopeScaledDepthBias = 2.0f;
    d.RasterizerState.MultisampleEnable = FALSE;
    d.NumRenderTargets = 0;
    if (!make(d, m_psoShadow, "shadow")) return false;

    // Main MSAA passes
    auto ms = [&]() {
        auto x = base();
        x.NumRenderTargets = 1;
        x.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        x.SampleDesc.Count = kMsaa;
        return x;
    };
    d = ms();
    d.VS = {vsBg.data(), vsBg.size()};
    d.PS = {psBg.data(), psBg.size()};
    d.InputLayout = {nullptr, 0};
    d.DepthStencilState.DepthEnable = FALSE;
    d.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    if (!make(d, m_psoBg, "background")) return false;
    d = ms();
    d.VS = {vsMain.data(), vsMain.size()};
    d.PS = {psMain.data(), psMain.size()};
    if (!make(d, m_psoMain, "main")) return false;
    blendOn(d);
    if (!make(d, m_psoTrans, "transparent")) return false;
    d.PS = {psGhost.data(), psGhost.size()};
    if (!make(d, m_psoGhost, "ghost")) return false;
    // Wireframe display: polygon edges as lines blended over the view; VSWire pulls them slightly towards the camera
    // so that they win over their own faces (a depth bias also let the hidden back edges through)
    d.VS = {vsWire.data(), vsWire.size()};
    d.PS = {psWire.data(), psWire.size()};
    d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    d.RasterizerState.AntialiasedLineEnable = TRUE;
    if (!make(d, m_psoWire, "wireframe")) return false;

    // Object id pass (single sample) into the albedo texture (w = id)
    d = base();
    d.VS = {vsMain.data(), vsMain.size()};
    d.PS = {psId.data(), psId.size()};
    d.NumRenderTargets = 1;
    d.RTVFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT;
    d.RasterizerState.MultisampleEnable = FALSE;
    if (!make(d, m_psoId, "id")) return false;

    // DDGI probe capture: cube faces (single sample), lit like the main pass, distance in alpha
    d = base();
    d.VS = {vsProbe.data(), vsProbe.size()};
    d.PS = {psProbe.data(), psProbe.size()};
    d.NumRenderTargets = 3;   // radiance + distance, albedo, normal
    for (int k = 0; k < 3; ++k) d.RTVFormats[k] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    d.RasterizerState.MultisampleEnable = FALSE;
    if (!make(d, m_psoProbe, "probe")) return false;
    // Background of the probe faces: the environment where no geometry is drawn (misses of the ray pass)
    d.VS = {vsBg.data(), vsBg.size()};
    d.PS = {psProbeBg.data(), psProbeBg.size()};
    d.InputLayout = {nullptr, 0};
    d.DepthStencilState.DepthEnable = FALSE;
    d.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    if (!make(d, m_psoProbeBg, "probe background")) return false;

    // Editor ground grid (EditorGrid.hlsl): full screen, blended, at the depth of its lines (optional shaders)
    std::vector<char> vsGrid, psGrid;
    if (readBlob(m_shaderDir + L"\\EditorGrid_VSGrid.cso", vsGrid) && readBlob(m_shaderDir + L"\\EditorGrid_PSGrid.cso", psGrid)) {
        d = ms();
        d.VS = {vsGrid.data(), vsGrid.size()};
        d.PS = {psGrid.data(), psGrid.size()};
        d.InputLayout = {nullptr, 0};
        blendOn(d);
        if (!make(d, m_psoGrid, "editor grid")) return false;
    }

    // Heaps and the shadow map (RTV 0-1 viewport and id, 2-4 probe capture; DSV 0-1 as before, 2 shadow, 3 capture)
    D3D12_DESCRIPTOR_HEAP_DESC hd = {};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.NumDescriptors = 5;
    m_device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_rtvHeap));
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    hd.NumDescriptors = 4;
    m_device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_dsvHeap));
    m_rtvSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    m_dsvSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    D3D12_RESOURCE_DESC sd = {};
    sd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    sd.Width = sd.Height = kShadowSize;
    sd.DepthOrArraySize = 1;
    sd.MipLevels = 1;
    sd.Format = DXGI_FORMAT_R32_TYPELESS;
    sd.SampleDesc.Count = 1;
    sd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE cv = {};
    cv.Format = DXGI_FORMAT_D32_FLOAT;
    cv.DepthStencil.Depth = 1.0f;
    auto hp = defaultHeap();
    m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &sd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &cv,
                                      IID_PPV_ARGS(&m_shadowMap));
    D3D12_DEPTH_STENCIL_VIEW_DESC dv = {};
    dv.Format = DXGI_FORMAT_D32_FLOAT;
    dv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    D3D12_CPU_DESCRIPTOR_HANDLE dh = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
    dh.ptr += 2 * (SIZE_T)m_dsvSize;
    m_device->CreateDepthStencilView(m_shadowMap.Get(), &dv, dh);
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = DXGI_FORMAT_R32_FLOAT;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE sh = m_heap->GetCPUDescriptorHandleForHeapStart();
    sh.ptr += (SIZE_T)kTexCount * m_descSize;
    m_device->CreateShaderResourceView(m_shadowMap.Get(), &srv, sh);

    m_rasterCB = createBuffer(1024, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
    return true;
}

// Cube captures of the DDGI probes (see recordRaster): slots probes of kProbeFace texels per face, six faces wide.
void Renderer::ensureCapture(uint32_t slots) {
    if (m_capTex && m_capSlots == slots) return;
    waitIdle();
    m_capSlots = slots;
    const uint32_t F = kProbeFace;
    auto hp = defaultHeap();
    D3D12_RESOURCE_DESC d = {};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = 6 * F;
    d.Height = slots * F;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    d.SampleDesc.Count = 1;
    d.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    // Radiance (with distance), albedo and normal: render targets of the capture and UAVs u12..u14 of the ray pass.
    ComPtr<ID3D12Resource>* targets[3] = {&m_capTex, &m_capAlbedo, &m_capNormal};
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += 2 * (SIZE_T)m_rtvSize;
    for (uint32_t k = 0; k < 3; ++k) {
        targets[k]->Reset();
        m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                          IID_PPV_ARGS(targets[k]->GetAddressOf()));
        D3D12_UNORDERED_ACCESS_VIEW_DESC u = {};
        u.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        D3D12_CPU_DESCRIPTOR_HANDLE hnd = m_heap->GetCPUDescriptorHandleForHeapStart();
        hnd.ptr += (SIZE_T)(kCapHeapSlot + k) * m_descSize;
        m_device->CreateUnorderedAccessView(targets[k]->Get(), nullptr, &u, hnd);
        m_device->CreateRenderTargetView(targets[k]->Get(), nullptr, rtv);
        rtv.ptr += m_rtvSize;
    }
    d.Format = DXGI_FORMAT_D32_FLOAT;
    d.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    m_capDepth.Reset();
    m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_DEPTH_WRITE, nullptr,
                                      IID_PPV_ARGS(&m_capDepth));
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
    dsv.ptr += 3 * (SIZE_T)m_dsvSize;
    m_device->CreateDepthStencilView(m_capDepth.Get(), nullptr, dsv);
}

void Renderer::createRasterTargets(uint32_t w, uint32_t h) {
    auto hp = defaultHeap();
    D3D12_RESOURCE_DESC d = {};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = w;
    d.Height = h;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;

    // MSAA colour
    d.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    d.SampleDesc.Count = kMsaa;
    d.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE cc = {};
    cc.Format = d.Format;
    m_msaaColor.Reset();
    m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_RENDER_TARGET, &cc, IID_PPV_ARGS(&m_msaaColor));
    // MSAA depth
    d.Format = DXGI_FORMAT_D32_FLOAT;
    d.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE dc = {};
    dc.Format = DXGI_FORMAT_D32_FLOAT;
    dc.DepthStencil.Depth = 1.0f;
    m_msaaDepth.Reset();
    m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_DEPTH_WRITE, &dc, IID_PPV_ARGS(&m_msaaDepth));
    // id depth (single sample)
    d.SampleDesc.Count = 1;
    m_idDepth.Reset();
    m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_DEPTH_WRITE, &dc, IID_PPV_ARGS(&m_idDepth));

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    m_device->CreateRenderTargetView(m_msaaColor.Get(), nullptr, rtv);
    rtv.ptr += m_rtvSize;
    m_device->CreateRenderTargetView(m_tex[kTexAlbedo].Get(), nullptr, rtv);
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
    m_device->CreateDepthStencilView(m_msaaDepth.Get(), nullptr, dsv);
    dsv.ptr += m_dsvSize;
    m_device->CreateDepthStencilView(m_idDepth.Get(), nullptr, dsv);
}

void Renderer::recordRaster(const Scene& scene, const RenderSettings& rs, const ViewRect& vp, bool probeCapture, bool probes,
                            bool shadow) {
    const Camera& cam = scene.camera;
    float3 r, u, f;
    cam.basis(r, u, f);
    float3 eye = cam.eye();
    float t = std::tan(cam.fovY * 0.5f), a = (float)vp.w / vp.h;
    float zn = std::max(cam.distance * 0.005f, 0.02f), zf = cam.distance * 40.0f + 2000.0f;
    float A = zf / (zf - zn), B = -zf * zn / (zf - zn);

    RasterConstants rc = {};
    setRows(rc.viewProj, 0, r / (t * a), -dot(r, eye) / (t * a));
    setRows(rc.viewProj, 1, u / t, -dot(u, eye) / t);
    setRows(rc.viewProj, 2, f * A, -dot(f, eye) * A + B);
    setRows(rc.viewProj, 3, f, -dot(f, eye));

    // Key light = the sun when enabled, else the strongest enabled area light; it gets the shadow map.
    int key = -1;
    float best = 0;
    int li = 0;
    float3 keyCenter(0, 50, 0), keyDir(0, -1, 0);   // keyDir: axis of the key area light (its normal)
    float3 c = m_sceneCenter;
    float R = m_sceneRadius * 1.05f;
    bool sun = scene.sun.enabled && scene.sun.intensity > 0;
    // HDRI: without a scene sun, the sun of the image is the key light (shadow map); the rest of the image is
    // the SH ambient. Its highlight already is in the blurred reflections, so this key has no specular term.
    const bool envMap = scene.env.hdri && scene.env.hdri == m_envImage && m_envW > 0;
    const bool envSun = envMap && !sun && m_envSunValid && rs.rasterEnvSun;
    float3 envSunDir = m_envSunDir;
    {
        float cr = std::cos(scene.env.hdriRotation), sr = std::sin(scene.env.hdriRotation);   // image space to world
        envSunDir = float3(m_envSunDir.x * cr - m_envSunDir.z * sr, m_envSunDir.y, m_envSunDir.z * cr + m_envSunDir.x * sr);
    }
    if (sun || envSun) {
        keyCenter = c + (envSun ? envSunDir : normalize(scene.sun.direction)) * (R * 3.0f);
    } else {
        for (const AreaLight& l : scene.lights) {
            if (!l.enabled) continue;
            float pw = (l.color.x + l.color.y + l.color.z) * l.intensity * l.width * l.height;
            if (pw > best) { best = pw; key = li; keyCenter = l.position; keyDir = normalize(l.target - l.position); }
            ++li;
        }
    }
    // An area light looks along its own normal: aimed at the scene centre it would look sideways when the bounds are
    // off-centre (a floor running towards the camera) and the walls would cut the shadow map diagonally.
    float3 lz = key >= 0 ? keyDir : normalize(c - keyCenter);
    float3 ref = std::fabs(lz.y) > 0.99f ? float3(1, 0, 0) : float3(0, 1, 0);
    float3 lx = normalize(cross(ref, lz)), ly = cross(lz, lx);
    setRows(rc.lightViewProj, 0, lx / R, -dot(lx, c) / R);
    setRows(rc.lightViewProj, 1, ly / R, -dot(ly, c) / R);
    // Depth from the light itself to the far side of the scene: what lies behind a local light (the ceiling above a
    // ceiling lamp) is clipped instead of shadowing the whole scene. The sun sits at 3 R, so its range is 4 R.
    const float zRange = dot(lz, c - keyCenter) + R;
    setRows(rc.lightViewProj, 2, lz / zRange, -dot(lz, keyCenter) / zRange);
    setRows(rc.lightViewProj, 3, float3(0, 0, 0), 1);

    rc.camPos = eye;
    rc.camRight = r;
    rc.camUp = u;
    rc.camFwd = f;
    rc.tanHalfFovY = t;
    rc.aspect = a;
    rc.lightCount = m_lightCount;
    rc.envTop = scene.env.top;
    rc.envHorizon = scene.env.horizon;
    rc.envBottom = scene.env.bottom;
    rc.envIntensity = scene.env.intensity;
    rc.gridScale = scene.gridScale;
    rc.keyLight = key < 0 ? 0xFFFFFFFFu : (uint32_t)key;
    rc.ambient = scene.env.ambient;
    rc.shadowTexel = 1.0f / kShadowSize;
    rc.keyPos = keyCenter;
    rc.lightScale = rs.lightScale;
    rc.sunDir = envSun ? envSunDir : normalize(scene.sun.direction);
    rc.sunOn = (sun || envSun) ? 1.0f : 0.0f;
    rc.sunRadiance = envSun ? m_envSunE * scene.env.hdriIntensity : scene.sun.color * scene.sun.intensity;
    rc.sunFromEnv = envSun ? 1.0f : 0.0f;
    for (int k = 0; k < 9; ++k)
        for (int ch = 0; ch < 3; ++ch) rc.shIrr[k][ch] = envSun ? m_envSH[k][ch] : m_envSHFull[k][ch];
    rc.envMapOn = envMap ? 1.0f : 0.0f;
    rc.envRotation = scene.env.hdriRotation;
    rc.envMapIntensity = scene.env.hdriIntensity;
    rc.envMips = m_envMips;
    rc.envVisible = scene.env.visibleToCamera ? 1.0f : 0.0f;
    rc.sunLight = m_sunLight;
    rc.giMode = (probes || probeCapture) ? 1.0f : 0.0f;   // probes read by the main pass; the capture drops its ambient
    memcpy(m_rasterCB.mapped, &rc, sizeof(rc));

    m_cmd->SetGraphicsRootSignature(m_rasterRoot.Get());
    m_cmd->SetGraphicsRootConstantBufferView(0, m_rasterCB.va());
    m_cmd->SetGraphicsRootShaderResourceView(2, m_materials.va());
    m_cmd->SetGraphicsRootShaderResourceView(3, m_lights.va());
    D3D12_GPU_DESCRIPTOR_HANDLE shadowSrv = m_heap->GetGPUDescriptorHandleForHeapStart();
    shadowSrv.ptr += (UINT64)kTexCount * m_descSize;
    m_cmd->SetGraphicsRootDescriptorTable(4, shadowSrv);
    m_cmd->SetGraphicsRootDescriptorTable(5, textureTable());
    D3D12_GPU_DESCRIPTOR_HANDLE envSrv = m_heap->GetGPUDescriptorHandleForHeapStart();
    envSrv.ptr += (UINT64)kEnvHeapSlot * m_descSize;
    m_cmd->SetGraphicsRootDescriptorTable(6, envSrv);
    D3D12_GPU_DESCRIPTOR_HANDLE probeSrv = m_heap->GetGPUDescriptorHandleForHeapStart();
    probeSrv.ptr += (UINT64)kProbeSrvSlot * m_descSize;
    m_cmd->SetGraphicsRootDescriptorTable(8, probeSrv);   // null views until a probe grid exists; read only when giMode > 0
    m_cmd->SetGraphicsRootConstantBufferView(9, m_giConstants.va());

    D3D12_VERTEX_BUFFER_VIEW vbv = {m_vb.va(), (UINT)m_vb.size, sizeof(gpu::GpuVertex)};
    D3D12_INDEX_BUFFER_VIEW ibv = {m_ib.va(), (UINT)m_ib.size, DXGI_FORMAT_R32_UINT};
    m_cmd->IASetVertexBuffers(0, 1, &vbv);
    m_cmd->IASetIndexBuffer(&ibv);
    m_cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    auto drawOne = [&](const Draw& d) {
        const MeshGpu& g = m_meshes[d.mesh];
        DrawConstants dc = {};
        memcpy(dc.rows, d.xf.m, sizeof(dc.rows));
        dc.materialId = d.material;
        dc.id = d.id;
        dc.alpha = d.alpha;
        dc.flags = d.flags;
        m_cmd->SetGraphicsRoot32BitConstants(1, 16, &dc, 0);
        m_cmd->DrawIndexedInstanced(g.indexCount, 1, g.indexOffset, (INT)g.vertexOffset, 0);
    };
    auto isTransparent = [&](const Draw& d) {
        return d.material < scene.materials.size() && scene.materials[d.material].transmission > 0.5f;
    };

    // 1) shadow map
    D3D12_CPU_DESCRIPTOR_HANDLE dsvBase = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
    if (shadow) {
    transition(m_shadowMap.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    D3D12_CPU_DESCRIPTOR_HANDLE shadowDsv = dsvBase;
    shadowDsv.ptr += 2 * (SIZE_T)m_dsvSize;
    m_cmd->OMSetRenderTargets(0, nullptr, FALSE, &shadowDsv);
    m_cmd->ClearDepthStencilView(shadowDsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    D3D12_VIEWPORT svp = {0, 0, (float)kShadowSize, (float)kShadowSize, 0, 1};
    D3D12_RECT ssc = {0, 0, (LONG)kShadowSize, (LONG)kShadowSize};
    m_cmd->RSSetViewports(1, &svp);
    m_cmd->RSSetScissorRects(1, &ssc);
    m_cmd->SetPipelineState(m_psoShadow.Get());
    if (key >= 0 || sun || envSun)
        for (const Draw& d : m_draws)
            if (!(d.flags & 7u) && !isTransparent(d)) drawOne(d);
    transition(m_shadowMap.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    // Probe capture (DDGI raster): the six cube faces of each probe of the batch, lit like the main pass, in one
    // target: kProbeFace texels per face, faces side by side, one row of faces per probe slot. Alpha = distance.
    if (probeCapture) {
        const uint32_t F = kProbeFace;
        const uint32_t total = giProbeCount(), nxy = m_giCounts[0] * m_giCounts[1];
        const float zn = std::max(m_giSpacing * 0.01f, 1e-3f), zf = std::max(m_sceneRadius * 20.0f, m_giSpacing * 100.0f);
        const float pA = zf / (zf - zn), pB = -zf * zn / (zf - zn);
        D3D12_CPU_DESCRIPTOR_HANDLE capRtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        capRtv.ptr += 2 * (SIZE_T)m_rtvSize;
        D3D12_CPU_DESCRIPTOR_HANDLE capDsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
        capDsv.ptr += 3 * (SIZE_T)m_dsvSize;
        ID3D12Resource* capTargets[3] = {m_capTex.Get(), m_capAlbedo.Get(), m_capNormal.Get()};
        for (ID3D12Resource* r : capTargets) transition(r, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_cmd->OMSetRenderTargets(3, &capRtv, TRUE, &capDsv);   // the three targets are consecutive in the RTV heap
        const float miss[4] = {0, 0, 0, 1e27f};   // no surface: the ray takes the environment
        const float none[4] = {0, 0, 0, 0};
        m_cmd->ClearRenderTargetView(capRtv, miss, 0, nullptr);
        D3D12_CPU_DESCRIPTOR_HANDLE capRtv1 = capRtv, capRtv2 = capRtv;
        capRtv1.ptr += m_rtvSize;
        capRtv2.ptr += 2 * (SIZE_T)m_rtvSize;
        m_cmd->ClearRenderTargetView(capRtv1, none, 0, nullptr);
        m_cmd->ClearRenderTargetView(capRtv2, none, 0, nullptr);
        m_cmd->ClearDepthStencilView(capDsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        for (uint32_t s = 0; s < m_capSlots; ++s) {
            const uint32_t p = (m_capBase + s) % total;
            const float3 o = float3((float)(p % m_giCounts[0]), (float)((p / m_giCounts[0]) % m_giCounts[1]), (float)(p / nxy)) *
                                 m_giSpacing + m_giOrigin;
            for (uint32_t f = 0; f < 6; ++f) {
                const float3 fwd = kProbeFwd[f], up = kProbeUp[f], right = cross(up, fwd);
                float pc[20] = {};   // view-projection of the face (rows), probe position
                setRows(pc, 0, right, -dot(right, o));
                setRows(pc, 1, up, -dot(up, o));
                setRows(pc, 2, fwd * pA, -dot(fwd, o) * pA + pB);
                setRows(pc, 3, fwd, -dot(fwd, o));
                pc[16] = o.x; pc[17] = o.y; pc[18] = o.z;
                m_cmd->SetGraphicsRoot32BitConstants(7, 20, pc, 0);
                D3D12_VIEWPORT fvp = {(float)(f * F), (float)(s * F), (float)F, (float)F, 0, 1};
                D3D12_RECT fsc = {(LONG)(f * F), (LONG)(s * F), (LONG)((f + 1) * F), (LONG)((s + 1) * F)};
                m_cmd->RSSetViewports(1, &fvp);
                m_cmd->RSSetScissorRects(1, &fsc);
                m_cmd->SetPipelineState(m_psoProbeBg.Get());   // environment first, geometry over it
                m_cmd->DrawInstanced(3, 1, 0, 0);
                m_cmd->SetPipelineState(m_psoProbe.Get());
                for (const Draw& d : m_draws)
                    if (!(d.flags & 7u) && !isTransparent(d)) drawOne(d);
            }
        }
        for (ID3D12Resource* r : capTargets) transition(r, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        return;
    }

    // 2) main MSAA pass
    D3D12_CPU_DESCRIPTOR_HANDLE rtvBase = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    m_cmd->OMSetRenderTargets(1, &rtvBase, FALSE, &dsvBase);
    m_cmd->ClearDepthStencilView(dsvBase, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    D3D12_VIEWPORT mvp = {0, 0, (float)vp.w, (float)vp.h, 0, 1};
    D3D12_RECT msc = {0, 0, (LONG)vp.w, (LONG)vp.h};
    m_cmd->RSSetViewports(1, &mvp);
    m_cmd->RSSetScissorRects(1, &msc);
    m_cmd->SetPipelineState(m_psoBg.Get());
    m_cmd->DrawInstanced(3, 1, 0, 0);
    // Wireframe only: the ground grid stays shaded, the other objects become lines with every edge visible.
    auto isGround = [&](const Draw& d) {
        return d.material < scene.materials.size() && (scene.materials[d.material].flags & MAT_FLAG_GRID);
    };
    // editor grid after the opaque objects (they hide it), before the transparent ones
    auto drawGrid = [&]() {
        if (!m_psoGrid || rs.editorGrid <= 0) return;
        float gc[4] = {rs.editorGrid, rs.editorGridAxis, 1.0f, 2.0f * t / std::max(1, vp.h)};
        m_cmd->SetGraphicsRoot32BitConstants(7, 4, gc, 0);
        m_cmd->SetPipelineState(m_psoGrid.Get());
        m_cmd->DrawInstanced(3, 1, 0, 0);
    };
    if (rs.wireframe == 1) {
        m_cmd->SetPipelineState(m_psoMain.Get());
        for (const Draw& d : m_draws)
            if (!(d.flags & 5u) && isGround(d)) drawOne(d);
        drawGrid();
    } else {
    m_cmd->SetPipelineState(m_psoMain.Get());
    for (const Draw& d : m_draws)
        if (!(d.flags & 5u) && !isTransparent(d)) drawOne(d);
    drawGrid();
    m_cmd->SetPipelineState(m_psoTrans.Get());
    for (const Draw& d : m_draws)
        if (!(d.flags & 5u) && isTransparent(d)) drawOne(d);
    m_cmd->SetPipelineState(m_psoGhost.Get());
    for (const Draw& d : m_draws)
        if (d.flags & 1u) drawOne(d);
    }
    if (rs.wireframe) {   // light lines alone, dark lines over the shaded view
        m_cmd->SetPipelineState(m_psoWire.Get());
        m_cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
        for (const Draw& d : m_draws) {
            if ((d.flags & 5u) || isGround(d)) continue;
            const MeshGpu& g = m_meshes[d.mesh];
            DrawConstants dc = {};
            memcpy(dc.rows, d.xf.m, sizeof(dc.rows));
            dc.materialId = d.material;
            dc.id = d.id;
            dc.alpha = rs.wireframe == 1 ? 0.75f : 0.02f;   // line grey level
            dc.flags = d.flags;
            m_cmd->SetGraphicsRoot32BitConstants(1, 16, &dc, 0);
            m_cmd->DrawIndexedInstanced(g.lineCount, 1, g.lineOffset, (INT)g.vertexOffset, 0);
        }
        m_cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    }

    // 3) resolve into the HDR slot read by the tonemapper
    transition(m_msaaColor.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
    transition(m_tex[kTexDenoiseA].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RESOLVE_DEST);
    m_cmd->ResolveSubresource(m_tex[kTexDenoiseA].Get(), 0, m_msaaColor.Get(), 0, DXGI_FORMAT_R16G16B16A16_FLOAT);
    transition(m_tex[kTexDenoiseA].Get(), D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    transition(m_msaaColor.Get(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    // 4) object ids (selection outline)
    D3D12_CPU_DESCRIPTOR_HANDLE idRtv = rtvBase;
    idRtv.ptr += m_rtvSize;
    D3D12_CPU_DESCRIPTOR_HANDLE idDsv = dsvBase;
    idDsv.ptr += m_dsvSize;
    transition(m_tex[kTexAlbedo].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RENDER_TARGET);
    const float zero[4] = {0, 0, 0, 0};
    m_cmd->OMSetRenderTargets(1, &idRtv, FALSE, &idDsv);
    m_cmd->ClearRenderTargetView(idRtv, zero, 0, nullptr);
    m_cmd->ClearDepthStencilView(idDsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    m_cmd->SetPipelineState(m_psoId.Get());
    for (const Draw& d : m_draws)
        if (!(d.flags & 7u)) drawOne(d);
    transition(m_tex[kTexAlbedo].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

} // namespace vp3d

