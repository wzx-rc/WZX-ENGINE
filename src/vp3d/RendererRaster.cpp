// Light raster viewport of vp3d::Renderer (no ray tracing): shadow map + forward PBR.
#include "Renderer.h"
#include "shaders/Shared.h"
#include <algorithm>
#include <cstring>
#include <functional>
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
    float envVisible; uint32_t sunLight; float giMode, orthoHalfH;   // giMode: DDGI probes read by Shade (Raster.hlsl); orthoHalfH: orthographic camera
    float3 hemiSky; float hemiOn;
    float3 hemiGround; float bevelOn;
    float rtShadow, sunTanRadius, rtShadowSamples, rtRefl;   // ray traced sun shadow (mask in t12), reflections (t13)
    float centreValid, rtLights, rtAO; uint32_t gridLine;   // centreValid: t14 holds this frame's pixel-centre depth
    float3 envSun2Dir; float envSun2On;                     // HQ: the light of the HDRI as a second soft light
    float3 envSun2E; float envSun2Tan;
    float sssScale, debugView, sssPad1, sssPad2;            // subsurface of the raster view (Raster.hlsl, 0 = off); debugView: RenderSettings::debugView (Prefab studio)
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

// vertex layout of the raster passes (gpu::GpuVertex)
const D3D12_INPUT_ELEMENT_DESC kRasterLayout[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
};

// common state of the raster pipelines: solid triangles, no culling, depth test, single sample
D3D12_GRAPHICS_PIPELINE_STATE_DESC rasterDesc(ID3D12RootSignature* root) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d = {};
    d.pRootSignature = root;
    d.InputLayout = {kRasterLayout, 3};
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
}

// alpha blending, no depth writes
void blendOn(D3D12_GRAPHICS_PIPELINE_STATE_DESC& d) {
    auto& b = d.BlendState.RenderTarget[0];
    b.BlendEnable = TRUE;
    b.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    b.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    b.BlendOp = D3D12_BLEND_OP_ADD;
    b.SrcBlendAlpha = D3D12_BLEND_ONE;
    b.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    b.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    d.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
}

} // namespace

bool Renderer::createRasterPipelines(std::string* error, bool msaaOnly) {
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

    // One table from kProbeSrvSlot: t4..t6 probe atlases (DDGI without ray tracing), then t11 screen GI (screen-space
    // cascades), t12 ray traced sun shadow, t13 reflections, t14 pixel-centre depth of the traced passes (edge fix);
    // (t7 is the buffer of the instanced draws)
    D3D12_DESCRIPTOR_RANGE probeRanges[2] = {};
    probeRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    probeRanges[0].NumDescriptors = 3;
    probeRanges[0].BaseShaderRegister = 4;
    probeRanges[0].OffsetInDescriptorsFromTableStart = 0;
    probeRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    probeRanges[1].NumDescriptors = 4;
    probeRanges[1].BaseShaderRegister = 11;
    probeRanges[1].OffsetInDescriptorsFromTableStart = 3;

    D3D12_DESCRIPTOR_RANGE bevelRange = {};   // t0..t3 space2: the scene of the raster bevel (one table: the root
    bevelRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;   // signature is at 63 of its 64 DWORDs)
    bevelRange.NumDescriptors = 4;
    bevelRange.BaseShaderRegister = 0;
    bevelRange.RegisterSpace = 2;

    D3D12_ROOT_PARAMETER p[12] = {};
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
    p[7].Constants.Num32BitValues = 32;   // probe face (20 values) or editor grid (32)
    p[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;   // t4..t6 probe atlases, t11..t14, pixel
    p[8].DescriptorTable.NumDescriptorRanges = 2;
    p[8].DescriptorTable.pDescriptorRanges = probeRanges;
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
    p[10].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;   // t7 instanced draw constants (gDraws), vertex + pixel
    p[10].Descriptor.ShaderRegister = 7;
    p[11].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;   // raster bevel scene, pixel
    p[11].DescriptorTable.NumDescriptorRanges = 1;
    p[11].DescriptorTable.pDescriptorRanges = &bevelRange;
    p[11].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rsd.NumParameters = 12;
    rsd.pParameters = p;
    rsd.NumStaticSamplers = 2;
    rsd.pStaticSamplers = samplers;
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> blob, err;
    if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err))) {
        if (error) *error = err ? (const char*)err->GetBufferPointer() : "raster root signature";
        return false;
    }
    if (!msaaOnly || !m_rasterRoot) m_device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_rasterRoot));

    auto load = [&](const wchar_t* name, std::vector<char>& code) {
        if (readBlob(m_shaderDir + L"\\" + name, code)) return true;
        if (error) {
            std::wstring w = name;
            *error = "Missing shader: " + std::string(w.begin(), w.end());
        }
        return false;
    };
    RasterShaders& shaders = m_rasterShaders;
    std::vector<char> vsShadow, vsProbe, psId, psProbe, psProbeBg;
    if (!load(L"Raster_VSMain.cso", shaders.vsMain) || !load(L"Raster_VSWire.cso", shaders.vsWire) || !load(L"Raster_VSShadow.cso", vsShadow) ||
        !load(L"Raster_VSBackground.cso", shaders.vsBg) || !load(L"Raster_VSProbe.cso", vsProbe) || !load(L"Raster_PSMain.cso", shaders.psMain) ||
        !load(L"Raster_PSGhost.cso", shaders.psGhost) || !load(L"Raster_PSId.cso", psId) || !load(L"Raster_PSWire.cso", shaders.psWire) ||
        !load(L"Raster_PSBackground.cso", shaders.psBg) || !load(L"Raster_PSProbe.cso", psProbe) || !load(L"Raster_PSProbeBackground.cso", psProbeBg) ||
        !load(L"Raster_PSSelect.cso", shaders.psSelect))
        return false;
    // editor ground grid (EditorGrid.hlsl): optional
    if (!readBlob(m_shaderDir + L"\\EditorGrid_VSGrid.cso", shaders.vsGrid) || !readBlob(m_shaderDir + L"\\EditorGrid_PSGrid.cso", shaders.psGrid)) {
        shaders.vsGrid.clear();
        shaders.psGrid.clear();
    }

    // sample counts of the viewport anti-aliasing (MSAA) supported for the colour and depth formats
    m_msaaMask = 1u << 1;
    for (uint32_t n = 2; n <= 16; n *= 2) {
        bool ok = true;
        for (DXGI_FORMAT fmt : {DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_D32_FLOAT}) {
            D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS q = {};
            q.Format = fmt;
            q.SampleCount = n;
            if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &q, sizeof(q))) || q.NumQualityLevels == 0) ok = false;
        }
        if (ok) m_msaaMask |= 1u << n;
    }

    auto make = [&](D3D12_GRAPHICS_PIPELINE_STATE_DESC& d, ComPtr<ID3D12PipelineState>& pso, const char* what) {
        if (FAILED(m_device->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&pso)))) {
            if (error) *error = std::string("CreateGraphicsPipelineState failed: ") + what;
            return false;
        }
        return true;
    };

    // Shadow (depth only)
    auto d = rasterDesc(m_rasterRoot.Get());
    d.VS = {vsShadow.data(), vsShadow.size()};
    d.RasterizerState.DepthBias = 200;
    d.RasterizerState.SlopeScaledDepthBias = 2.0f;
    d.RasterizerState.MultisampleEnable = FALSE;
    d.NumRenderTargets = 0;
    if (!make(d, m_psoShadow, "shadow")) return false;

    // Viewport passes (multisampled, m_msaa samples)
    if (!createMsaaPipelines(error)) return false;
    if (msaaOnly) return true;

    // Object id pass (single sample) into the albedo texture (w = id)
    d = rasterDesc(m_rasterRoot.Get());
    d.VS = {shaders.vsMain.data(), shaders.vsMain.size()};
    d.PS = {psId.data(), psId.size()};
    d.NumRenderTargets = 1;
    d.RTVFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT;
    d.RasterizerState.MultisampleEnable = FALSE;
    if (!make(d, m_psoId, "id")) return false;

    // DDGI probe capture: cube faces (single sample), lit like the main pass, distance in alpha
    d = rasterDesc(m_rasterRoot.Get());
    d.VS = {vsProbe.data(), vsProbe.size()};
    d.PS = {psProbe.data(), psProbe.size()};
    d.NumRenderTargets = 3;   // radiance + distance, albedo, normal
    for (int k = 0; k < 3; ++k) d.RTVFormats[k] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    d.RasterizerState.MultisampleEnable = FALSE;
    if (!make(d, m_psoProbe, "probe")) return false;
    // Background of the probe faces: the environment where no geometry is drawn (misses of the ray pass)
    d.VS = {shaders.vsBg.data(), shaders.vsBg.size()};
    d.PS = {psProbeBg.data(), psProbeBg.size()};
    d.InputLayout = {nullptr, 0};
    d.DepthStencilState.DepthEnable = FALSE;
    d.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    if (!make(d, m_psoProbeBg, "probe background")) return false;

    // Heaps and the shadow map (RTV 0-1 viewport and id, 2-4 probe capture, 5 selection mask; DSV 0-1 as before, 2 shadow,
    // 3 capture)
    D3D12_DESCRIPTOR_HEAP_DESC hd = {};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.NumDescriptors = 6;
    m_device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_rtvHeap));
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    hd.NumDescriptors = 4;
    m_device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_dsvHeap));
    m_rtvSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    m_dsvSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    // extra views (renderView): 3 RTVs and 2 DSVs each, and their own command list
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.NumDescriptors = kMaxViews * 3;
    m_device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_viewRtvHeap));
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    hd.NumDescriptors = kMaxViews * 2;
    m_device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_viewDsvHeap));
    m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_viewAlloc));
    m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_viewAlloc.Get(), nullptr, IID_PPV_ARGS(&m_viewCmd));
    m_viewCmd->Close();

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

    m_rasterCB = createBuffer(2048, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
    return true;
}

// Passes drawn into the multisampled viewport target, with m_msaa samples (viewport anti-aliasing): background, objects,
// depth only (overlay over the path traced view), transparent, ghost, wireframe lines and the editor grid. Rebuilt when
// the sample count changes (setMsaa).
bool Renderer::createMsaaPipelines(std::string* error) {
    const RasterShaders& sh = m_rasterShaders;
    auto make = [&](D3D12_GRAPHICS_PIPELINE_STATE_DESC& d, ComPtr<ID3D12PipelineState>& pso, const char* what) {
        pso.Reset();
        if (FAILED(m_device->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&pso)))) {
            if (error) *error = std::string("CreateGraphicsPipelineState failed: ") + what;
            return false;
        }
        return true;
    };
    auto ms = [&]() {
        auto x = rasterDesc(m_rasterRoot.Get());
        x.NumRenderTargets = 1;
        x.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        x.SampleDesc.Count = m_msaa;
        return x;
    };
    auto d = ms();
    d.VS = {sh.vsBg.data(), sh.vsBg.size()};
    d.PS = {sh.psBg.data(), sh.psBg.size()};
    d.InputLayout = {nullptr, 0};
    d.DepthStencilState.DepthEnable = FALSE;
    d.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    if (!make(d, m_psoBg, "background")) return false;
    d = ms();
    d.VS = {sh.vsMain.data(), sh.vsMain.size()};
    d.PS = {sh.psMain.data(), sh.psMain.size()};
    if (!make(d, m_psoMain, "main")) return false;
    {   // depth only (editor overlay over the path traced image: the objects hide the grid and the back lines)
        auto z = d;
        z.BlendState.RenderTarget[0].RenderTargetWriteMask = 0;
        z.PS = {nullptr, 0};
        if (!make(z, m_psoDepth, "depth")) return false;
    }
    blendOn(d);
    if (!make(d, m_psoTrans, "transparent")) return false;
    d.PS = {sh.psGhost.data(), sh.psGhost.size()};
    if (!make(d, m_psoGhost, "ghost")) return false;
    // Wireframe display: polygon edges as lines blended over the view; VSWire pulls them slightly towards the camera
    // so that they win over their own faces (a depth bias also let the hidden back edges through)
    d.VS = {sh.vsWire.data(), sh.vsWire.size()};
    d.PS = {sh.psWire.data(), sh.psWire.size()};
    d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    d.RasterizerState.AntialiasedLineEnable = TRUE;
    if (!make(d, m_psoWire, "wireframe")) return false;
    // Selection mask: the selected objects, 1 in their visible samples (depth test against the viewport depth, no write)
    d = ms();
    d.VS = {sh.vsMain.data(), sh.vsMain.size()};
    d.PS = {sh.psSelect.data(), sh.psSelect.size()};
    d.RTVFormats[0] = DXGI_FORMAT_R8_UNORM;
    d.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    if (!make(d, m_psoSelect, "selection mask")) return false;
    // Editor ground grid: full screen, blended, at the depth of its lines
    m_psoGrid.Reset();
    if (!sh.vsGrid.empty()) {
        d = ms();
        d.VS = {sh.vsGrid.data(), sh.vsGrid.size()};
        d.PS = {sh.psGrid.data(), sh.psGrid.size()};
        d.InputLayout = {nullptr, 0};
        blendOn(d);
        if (!make(d, m_psoGrid, "editor grid")) return false;
    }
    return true;
}

// The viewport colour into a texture slot (UAV): a resolve of its samples, a plain copy when the anti-aliasing is off
// (ResolveSubresource needs a multisampled source).
void Renderer::resolveViewport(ID3D12Resource* color, ID3D12Resource* dst) {
    const bool ms = m_msaa > 1;
    const D3D12_RESOURCE_STATES src = ms ? D3D12_RESOURCE_STATE_RESOLVE_SOURCE : D3D12_RESOURCE_STATE_COPY_SOURCE;
    const D3D12_RESOURCE_STATES dstState = ms ? D3D12_RESOURCE_STATE_RESOLVE_DEST : D3D12_RESOURCE_STATE_COPY_DEST;
    transition(color, D3D12_RESOURCE_STATE_RENDER_TARGET, src);
    transition(dst, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, dstState);
    if (ms) m_cmd->ResolveSubresource(dst, 0, color, 0, DXGI_FORMAT_R16G16B16A16_FLOAT);
    else m_cmd->CopyResource(dst, color);
    transition(dst, dstState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    transition(color, src, D3D12_RESOURCE_STATE_RENDER_TARGET);
}

// Samples per pixel of the viewport anti-aliasing (1 = off). The GPU draws the largest supported count up to n; twice
// that count is reached over two frames (temporal, see recordRaster), e.g. x16 as 8 samples x 2 frames. Waits for the
// GPU, then rebuilds the multisampled pipelines and targets when the hardware count changes.
void Renderer::setMsaa(uint32_t n) {
    n = std::max(1u, std::min(n, 16u));
    uint32_t hw = n;
    while (hw > 1 && !msaaSupported(hw)) hw /= 2;
    const bool temporal = hw > 1 && hw * 2 == n;
    if (temporal != m_msaaTemporal) m_taaValid = false;
    m_msaaTemporal = temporal;
    if (hw == m_msaa) return;
    waitIdle();
    m_taaValid = false;
    m_msaa = hw;
    createMsaaPipelines(nullptr);
    if (m_rtW && m_rtH) createRasterTargets(m_rtW, m_rtH);
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
    d.SampleDesc.Count = m_msaa;
    d.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE cc = {};
    cc.Format = d.Format;
    m_msaaColor.Reset();
    m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_RENDER_TARGET, &cc, IID_PPV_ARGS(&m_msaaColor));
    {   // its SRV for the tonemapped resolve (compute, t7 of that pass)
        D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
        sv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
        sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        D3D12_CPU_DESCRIPTOR_HANDLE h = m_heap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += (SIZE_T)kMsaaSrvSlot * m_descSize;
        m_device->CreateShaderResourceView(m_msaaColor.Get(), &sv, h);
    }
    // MSAA depth (typeless: also read as R32 by the ambient occlusion, SRV t9)
    d.Format = DXGI_FORMAT_R32_TYPELESS;
    d.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE dc = {};
    dc.Format = DXGI_FORMAT_D32_FLOAT;
    dc.DepthStencil.Depth = 1.0f;
    m_msaaDepth.Reset();
    m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_DEPTH_WRITE, &dc, IID_PPV_ARGS(&m_msaaDepth));
    d.Format = DXGI_FORMAT_D32_FLOAT;
    // id depth (single sample)
    d.SampleDesc.Count = 1;
    m_idDepth.Reset();
    m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_DEPTH_WRITE, &dc, IID_PPV_ARGS(&m_idDepth));

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    m_device->CreateRenderTargetView(m_msaaColor.Get(), nullptr, rtv);
    rtv.ptr += m_rtvSize;
    m_device->CreateRenderTargetView(m_tex[kTexAlbedo].Get(), nullptr, rtv);
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
    {
        D3D12_DEPTH_STENCIL_VIEW_DESC dv = {};
        dv.Format = DXGI_FORMAT_D32_FLOAT;
        dv.ViewDimension = m_msaa > 1 ? D3D12_DSV_DIMENSION_TEXTURE2DMS : D3D12_DSV_DIMENSION_TEXTURE2D;
        m_device->CreateDepthStencilView(m_msaaDepth.Get(), &dv, dsv);
        D3D12_SHADER_RESOURCE_VIEW_DESC zs = {};
        zs.Format = DXGI_FORMAT_R32_FLOAT;
        zs.ViewDimension = m_msaa > 1 ? D3D12_SRV_DIMENSION_TEXTURE2DMS : D3D12_SRV_DIMENSION_TEXTURE2D;
        zs.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        if (m_msaa <= 1) zs.Texture2D.MipLevels = 1;
        D3D12_CPU_DESCRIPTOR_HANDLE zh = m_heap->GetCPUDescriptorHandleForHeapStart();
        zh.ptr += (SIZE_T)kDepthSrvSlot * m_descSize;
        m_device->CreateShaderResourceView(m_msaaDepth.Get(), &zs, zh);
    }
    dsv.ptr += m_dsvSize;
    m_device->CreateDepthStencilView(m_idDepth.Get(), nullptr, dsv);

    // selection mask (samples of the viewport) and its coverage per pixel, read by the tonemapper (SRV t8)
    d.Format = DXGI_FORMAT_R8_UNORM;
    d.SampleDesc.Count = m_msaa;
    d.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE mc = {};
    mc.Format = DXGI_FORMAT_R8_UNORM;
    m_selMask.Reset();
    m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_RENDER_TARGET, &mc, IID_PPV_ARGS(&m_selMask));
    d.SampleDesc.Count = 1;
    d.Flags = D3D12_RESOURCE_FLAG_NONE;
    m_selCov.Reset();
    m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr,
                                      IID_PPV_ARGS(&m_selCov));
    rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += 5 * (SIZE_T)m_rtvSize;
    m_device->CreateRenderTargetView(m_selMask.Get(), nullptr, rtv);
    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.Format = DXGI_FORMAT_R8_UNORM;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE sh = m_heap->GetCPUDescriptorHandleForHeapStart();
    sh.ptr += (SIZE_T)kSelCovSlot * m_descSize;
    m_device->CreateShaderResourceView(m_selCov.Get(), &sv, sh);
}

uint32_t Renderer::allocDraws(uint32_t count) {
    const uint64_t need = (m_drawBufUsed + count) * sizeof(DrawConstants);
    if (!m_drawBuf.res || need > m_drawBuf.size) {
        uint64_t size = std::max<uint64_t>((uint64_t)count * sizeof(DrawConstants) * 2, 1u << 20);
        if (m_drawBuf.res) {
            size = std::max(size, m_drawBuf.size * 2);
            m_drawGrave.push_back(m_drawBuf);   // still read by the commands recorded this frame
        }
        m_drawBuf = createBuffer(size, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
        m_drawBufUsed = 0;
    }
    uint32_t first = (uint32_t)m_drawBufUsed;
    m_drawBufUsed += count;
    return first;
}

// Coverage of the selection: the selected objects drawn into the mask with the samples (and depth) of the viewport,
// then resolved (the average of the samples) into m_selCov for the tonemapper. Its outline is then as smooth as the
// edges of the image (Tonemap.hlsl).
void Renderer::recordSelectionMask(const std::function<void(const Draw&)>& drawOne) {
    if (m_selIds.empty() || !m_psoSelect) return;
    const ExtraView* xv = m_rtView;
    ID3D12Resource* selMask = xv ? xv->selMask.Get() : m_selMask.Get();
    ID3D12Resource* selCov = xv ? xv->selCov.Get() : m_selCov.Get();
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = xv ? m_viewRtvHeap->GetCPUDescriptorHandleForHeapStart() : m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += (xv ? xv->index * 3 + 2 : 5) * (SIZE_T)m_rtvSize;
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = xv ? m_viewDsvHeap->GetCPUDescriptorHandleForHeapStart() : m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
    if (xv) dsv.ptr += (SIZE_T)(xv->index * 2) * m_dsvSize;
    m_cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    const float zero[4] = {0, 0, 0, 0};
    m_cmd->ClearRenderTargetView(rtv, zero, 0, nullptr);
    m_cmd->SetPipelineState(m_psoSelect.Get());
    m_cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    for (const Draw& d : m_draws)
        if (!(d.flags & 5u) && std::find(m_selIds.begin(), m_selIds.end(), d.id) != m_selIds.end()) drawOne(d);
    const bool ms = m_msaa > 1;
    const D3D12_RESOURCE_STATES src = ms ? D3D12_RESOURCE_STATE_RESOLVE_SOURCE : D3D12_RESOURCE_STATE_COPY_SOURCE;
    const D3D12_RESOURCE_STATES dst = ms ? D3D12_RESOURCE_STATE_RESOLVE_DEST : D3D12_RESOURCE_STATE_COPY_DEST;
    transition(selMask, D3D12_RESOURCE_STATE_RENDER_TARGET, src);
    transition(selCov, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, dst);
    if (ms) m_cmd->ResolveSubresource(selCov, 0, selMask, 0, DXGI_FORMAT_R8_UNORM);
    else m_cmd->CopyResource(selCov, selMask);
    transition(selCov, dst, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    transition(selMask, src, D3D12_RESOURCE_STATE_RENDER_TARGET);
}

void Renderer::recordRaster(const Scene& scene, const RenderSettings& rs, const ViewRect& vp, bool probeCapture, bool probes,
                            bool shadow, int overlaySlot) {
    const bool overlay = overlaySlot >= 0;   // editor overlay only: grid and wireframe lines, for the path traced view
    if (overlay) shadow = false;
    const ExtraView* xv = m_rtView;   // an extra view (renderView): its camera, targets and constants
    const Camera& cam = (xv && m_rtCam) ? *m_rtCam : scene.camera;
    Buffer& rcb = xv ? const_cast<Buffer&>(xv->cb) : m_rasterCB;
    float3 r, u, f;
    cam.basis(r, u, f);
    float3 eye = cam.eye();
    float t = std::tan(cam.fovY * 0.5f), a = (float)vp.w / vp.h;
    RasterConstants rc = {};
    if (cam.ortho) {
        // orthographic: x, y scaled by the view size, linear depth from orthoBack behind the eye (the whole scene in front)
        const float hh = cam.orthoHeight * 0.5f, hw = hh * a;
        const float zn = -orthoBack(cam), zf = std::max(dot(m_sceneCenter - eye, f) + m_sceneRadius * 2.0f, 1.0f) + cam.distance;
        setRows(rc.viewProj, 0, r / hw, -dot(r, eye) / hw);
        setRows(rc.viewProj, 1, u / hh, -dot(u, eye) / hh);
        setRows(rc.viewProj, 2, f / (zf - zn), (-dot(f, eye) - zn) / (zf - zn));
        setRows(rc.viewProj, 3, float3(0, 0, 0), 1);
        rc.orthoHalfH = hh;
    } else {
        float zn = std::max(cam.distance * 0.005f, 0.02f), zf = cam.distance * 40.0f + 2000.0f;
        float A = zf / (zf - zn), B = -zf * zn / (zf - zn);
        setRows(rc.viewProj, 0, r / (t * a), -dot(r, eye) / (t * a));
        setRows(rc.viewProj, 1, u / t, -dot(u, eye) / t);
        setRows(rc.viewProj, 2, f * A, -dot(f, eye) * A + B);
        setRows(rc.viewProj, 3, f, -dot(f, eye));
    }

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
    // HQ (rasterRT): beside the scene sun, the light of the HDRI as a second light with a traced soft shadow (it is
    // left out of the SH); its cone: the 6 degrees of its extraction (uploadEnvironment)
    const bool envSun2 = envMap && sun && m_envSunValid && rs.rasterRT && !probeCapture && m_tlas.res && m_tlasVersion == m_buildVersion;
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
    rc.gridLine = scene.gridLine;
    rc.keyLight = key < 0 ? 0xFFFFFFFFu : (uint32_t)key;
    rc.ambient = scene.env.ambient;
    rc.hemiSky = scene.env.hemiSky * scene.env.hemiIntensity;
    rc.hemiGround = scene.env.hemiGround * scene.env.hemiIntensity;
    rc.hemiOn = scene.env.hemiIntensity > 0 ? 1.0f : 0.0f;
    const bool tlasOk = m_tlas.res && m_tlasVersion == m_buildVersion;
    rc.bevelOn = (rs.bevel && rs.rasterBevel && tlasOk) ? rs.bevelScale : 0.0f;   // raster bevel (TLAS built for it)
    rc.rtLights = (rs.rasterRT && tlasOk && !probeCapture) ? 1.0f : 0.0f;
    rc.rtAO = (rs.rasterRT && tlasOk && !probeCapture) ? std::max(0.0f, rs.rasterAO) : 0.0f;
    rc.envSun2On = envSun2 ? 1.0f : 0.0f;
    rc.envSun2Dir = envSunDir;
    rc.envSun2E = envSun2 ? m_envSunE * scene.env.hdriIntensity : float3(0.0f);
    rc.envSun2Tan = std::tan(radians(6.0f));
    rc.sssScale = std::max(0.0f, rs.rasterSSS);
    rc.shadowTexel = 1.0f / kShadowSize;
    rc.keyPos = keyCenter;
    rc.lightScale = rs.lightScale;
    rc.sunDir = envSun ? envSunDir : normalize(scene.sun.direction);
    rc.sunOn = (sun || envSun) ? 1.0f : 0.0f;
    rc.sunRadiance = envSun ? m_envSunE * scene.env.hdriIntensity : scene.sun.color * scene.sun.intensity;
    rc.sunFromEnv = envSun ? 1.0f : 0.0f;
    for (int k = 0; k < 9; ++k)
        for (int ch = 0; ch < 3; ++ch) rc.shIrr[k][ch] = (envSun || envSun2) ? m_envSH[k][ch] : m_envSHFull[k][ch];
    rc.envMapOn = envMap ? 1.0f : 0.0f;
    rc.envRotation = scene.env.hdriRotation;
    rc.envMapIntensity = scene.env.hdriIntensity;
    rc.envMips = m_envMips;
    rc.envVisible = scene.env.visibleToCamera ? 1.0f : 0.0f;
    rc.sunLight = m_sunLight;
    rc.giMode = (probes || probeCapture) ? 1.0f : 0.0f;   // probes read by the main pass; the capture drops its ambient
    // sun shadow traced per pixel when this frame has an acceleration structure (GI modes)
    rc.rtShadow = (rs.rtSunShadows && m_asThisFrame && m_tlas.res) ? 1.0f : 0.0f;
    rc.sunTanRadius = std::tan(radians(std::max(rs.sunAngle, 0.0f)) * 0.5f);
    rc.rtShadowSamples = (float)std::max(1, std::min(rs.rtShadowSamples, 32));
    // reflections traced per pixel on glossy surfaces: GI modes (the hits read the probes), not the probe captures
    const bool reflOn = rs.rtReflections && (probes || rs.rasterRT) && !probeCapture && !xv && m_asThisFrame && m_tlas.res;   // (main view only)
    rc.rtRefl = reflOn ? 1.0f : 0.0f;
    // ray traced sun shadow and reflections: computed now (compute passes), read by the main pass (t8, t9)
    // the mask is the sun's, or without a sun the key area light's (a lamp in a room: the Cornell box)
    const int areaKey = (rc.sunOn > 0 || key < 0) ? -1 : key;
    const bool sunMask = rc.rtShadow > 0 && !probeCapture && !xv && (rc.sunOn > 0 || areaKey >= 0);
    // the traced passes also give the depth of each pixel centre: the edge fix of the main pass (Raster.hlsl, t10)
    rc.centreValid = (sunMask || reflOn) ? 1.0f : 0.0f;
    // The id pass reads a copy without jitter (offset 768); the viewport passes may be jittered: MSAA x16 on a GPU with
    // 8 samples alternates two sub-pixel offsets of its pattern, frame after frame, and the tonemapper averages the last
    // two frames while nothing moves (16 different samples per pixel).
    rc.debugView = probeCapture ? 0.0f : (float)rs.debugView;
    static_assert(sizeof(RasterConstants) <= 768, "two copies in m_rasterCB (768 + size <= 1536: the frame constants of an extra view)");
    memcpy((uint8_t*)rcb.mapped + 768, &rc, sizeof(rc));
    if (m_msaaTemporal && !overlay && !probeCapture && !probes && !xv) {
        const float o = (m_taaFrame & 1) ? 1.0f / 32.0f : -1.0f / 32.0f;   // pixels: the two frames are 1/16 px apart
        const float jx = 2.0f * o / (float)vp.w, jy = -2.0f * o / (float)vp.h;
        for (int k = 0; k < 4; ++k) {
            rc.viewProj[k] += jx * rc.viewProj[12 + k];
            rc.viewProj[4 + k] += jy * rc.viewProj[12 + k];
        }
    }
    memcpy(rcb.mapped, &rc, sizeof(rc));
    if (sunMask || reflOn) recordScreenTrace(rc.sunDir, rs, sunMask, reflOn, areaKey);

    m_cmd->SetGraphicsRootSignature(m_rasterRoot.Get());
    {   // raster bevel: the scene for the inline ray queries (views written when the bevel is on; read nowhere when
        // rc.bevelOn = 0, the table then points at whatever the slots hold)
        D3D12_CPU_DESCRIPTOR_HANDLE h = m_heap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += (SIZE_T)kBevelSrvSlot * m_descSize;
        if (rc.bevelOn > 0 || rc.rtLights > 0) {
            D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
            sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            sv.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
            sv.RaytracingAccelerationStructure.Location = m_tlas.va();
            m_device->CreateShaderResourceView(nullptr, &sv, h);
            auto structured = [&](ID3D12Resource* res, UINT64 bytes, UINT stride, UINT slot) {
                D3D12_SHADER_RESOURCE_VIEW_DESC b = {};
                b.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                b.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                b.Format = DXGI_FORMAT_UNKNOWN;
                b.Buffer.NumElements = (UINT)(bytes / stride);
                b.Buffer.StructureByteStride = stride;
                D3D12_CPU_DESCRIPTOR_HANDLE hh = h;
                hh.ptr += (SIZE_T)slot * m_descSize;
                m_device->CreateShaderResourceView(res, &b, hh);
            };
            structured(m_vb.res.Get(), m_vb.size, sizeof(gpu::GpuVertex), 1);
            structured(m_ib.res.Get(), m_ib.size, 4, 2);
            structured(m_instances.res.Get(), m_instances.size, sizeof(gpu::GpuInstance), 3);
        }
        D3D12_GPU_DESCRIPTOR_HANDLE g = m_heap->GetGPUDescriptorHandleForHeapStart();
        g.ptr += (UINT64)kBevelSrvSlot * m_descSize;
        m_cmd->SetGraphicsRootDescriptorTable(11, g);
    }
    m_cmd->SetGraphicsRootConstantBufferView(0, rcb.va());
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

    // Instancing: the constants of every draw go to m_drawBuf once, sorted by mesh (counting sort), and each pass draws
    // runs of consecutive draws of the same mesh with one DrawIndexedInstanced.
    m_cmd->SetGraphicsRootShaderResourceView(10, 0);
    m_rasterTris = 0;
    const uint32_t nDraws = (uint32_t)m_draws.size();
    std::vector<uint32_t> order(nDraws), slotOf(nDraws);
    // Level of detail (Scene::lods, by the size of the object on screen) and frustum culling of the camera passes.
    std::vector<int> drawMesh(nDraws);
    std::vector<uint8_t> inView(nDraws, 1);
    {
        const float pixK = (float)vp.h / (2.0f * t);   // screen pixels per unit at distance 1 (perspective)
        const float ta = t * a, kx = std::sqrt(1.0f + ta * ta), ky = std::sqrt(1.0f + t * t);
        const bool persp = !cam.ortho;
        const bool lodOn = !scene.lods.empty() && !probeCapture;
        for (uint32_t k = 0; k < nDraws; ++k) {
            const Draw& d = m_draws[k];
            int mesh = d.mesh;
            if (persp && !(d.flags & 2u)) {
                const MeshGpu& g = m_meshes[d.mesh];
                float3 c = d.xf.point(g.center);
                float sc = std::sqrt(std::max(d.xf.m[0][0] * d.xf.m[0][0] + d.xf.m[1][0] * d.xf.m[1][0] + d.xf.m[2][0] * d.xf.m[2][0],
                                              d.xf.m[0][1] * d.xf.m[0][1] + d.xf.m[1][1] * d.xf.m[1][1] + d.xf.m[2][1] * d.xf.m[2][1]));
                float R = g.radius * sc;
                float3 v = c - eye;
                float z = dot(v, f);
                if (z < -R || std::fabs(dot(v, r)) > z * ta + R * kx || std::fabs(dot(v, u)) > z * t + R * ky) inView[k] = 0;
                if (lodOn && d.mesh < (int)scene.lods.size()) {
                    const Scene::MeshLod& l = scene.lods[d.mesh];
                    float px = 2.0f * R * pixK / std::max(z, 1e-3f);
                    if (px < scene.lodPixels2 && l.lod2 >= 0) mesh = l.lod2;
                    else if (px < scene.lodPixels1 && l.lod1 >= 0) mesh = l.lod1;
                }
            }
            drawMesh[k] = mesh < (int)m_meshes.size() ? mesh : d.mesh;
        }
    }
    {
        std::vector<uint32_t> start(m_meshes.size() + 1, 0);
        for (uint32_t k = 0; k < nDraws; ++k) ++start[drawMesh[k] + 1];
        for (size_t k = 1; k < start.size(); ++k) start[k] += start[k - 1];
        for (uint32_t k = 0; k < nDraws; ++k) order[start[drawMesh[k]]++] = k;
    }
    const uint32_t drawBase = nDraws ? allocDraws(nDraws * (rs.wireframe ? 2u : 1u)) : 0;
    if (nDraws) {
        DrawConstants* dst = (DrawConstants*)m_drawBuf.mapped + drawBase;
        for (uint32_t k = 0; k < nDraws; ++k) {
            const Draw& d = m_draws[order[k]];
            DrawConstants& dc = dst[k];
            memcpy(dc.rows, d.xf.m, sizeof(dc.rows));
            dc.materialId = d.material;
            dc.id = d.id;
            dc.alpha = d.alpha;
            dc.flags = d.flags;
            slotOf[order[k]] = drawBase + k;
        }
        m_cmd->SetGraphicsRootShaderResourceView(10, m_drawBuf.va());
    }
    auto drawOne = [&](const Draw& d) {
        const MeshGpu& g = m_meshes[d.mesh];
        uint32_t slot = slotOf[&d - m_draws.data()];
        m_cmd->SetGraphicsRoot32BitConstants(1, 1, &slot, 0);
        m_cmd->DrawIndexedInstanced(g.indexCount, 1, g.indexOffset, (INT)g.vertexOffset, 0);
    };
    // every draw passing the filter (and in the view when cull is set), batched by mesh (level of detail)
    auto drawAll = [&](const std::function<bool(const Draw&)>& pass, bool cull = true) {
        uint32_t k = 0;
        auto ok = [&](uint32_t j) { return (!cull || inView[order[j]]) && pass(m_draws[order[j]]); };
        while (k < nDraws) {
            if (!ok(k)) { ++k; continue; }
            const int mesh = drawMesh[order[k]];
            uint32_t e = k + 1;
            while (e < nDraws && drawMesh[order[e]] == mesh && ok(e)) ++e;
            const MeshGpu& g = m_meshes[mesh];
            uint32_t slot = drawBase + k;
            m_cmd->SetGraphicsRoot32BitConstants(1, 1, &slot, 0);
            m_cmd->DrawIndexedInstanced(g.indexCount, e - k, g.indexOffset, (INT)g.vertexOffset, 0);
            if (cull) m_rasterTris += (uint64_t)(g.indexCount / 3) * (e - k);
            k = e;
        }
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
        drawAll([&](const Draw& d) { return !(d.flags & 7u) && !isTransparent(d); }, false);
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
                drawAll([&](const Draw& d) { return !(d.flags & 7u) && !isTransparent(d); }, false);
            }
        }
        for (ID3D12Resource* r : capTargets) transition(r, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        return;
    }

    // 2) main MSAA pass (an extra view: its own targets, 3 RTVs and 2 DSVs per view)
    D3D12_CPU_DESCRIPTOR_HANDLE rtvBase = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    if (xv) {
        rtvBase = m_viewRtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtvBase.ptr += (SIZE_T)(xv->index * 3) * m_rtvSize;
        dsvBase = m_viewDsvHeap->GetCPUDescriptorHandleForHeapStart();
        dsvBase.ptr += (SIZE_T)(xv->index * 2) * m_dsvSize;
    }
    m_cmd->OMSetRenderTargets(1, &rtvBase, FALSE, &dsvBase);
    m_cmd->ClearDepthStencilView(dsvBase, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    D3D12_VIEWPORT mvp = {0, 0, (float)vp.w, (float)vp.h, 0, 1};
    D3D12_RECT msc = {0, 0, (LONG)vp.w, (LONG)vp.h};
    m_cmd->RSSetViewports(1, &mvp);
    m_cmd->RSSetScissorRects(1, &msc);
    if (overlay) {
        const float clear0[4] = {0, 0, 0, 0};
        m_cmd->ClearRenderTargetView(rtvBase, clear0, 0, nullptr);
    } else {
        m_cmd->SetPipelineState(m_psoBg.Get());
        m_cmd->DrawInstanced(3, 1, 0, 0);
    }
    // Wireframe only: the ground grid stays shaded, the other objects become lines with every edge visible.
    auto isGround = [&](const Draw& d) {
        return d.material < scene.materials.size() && (scene.materials[d.material].flags & MAT_FLAG_GRID);
    };
    // editor grid after the opaque objects (they hide it), before the transparent ones
    auto drawGrid = [&]() {
        if (!m_psoGrid || rs.editorGrid <= 0) return;
        const EditorGridStyle& g = rs.grid;
        // orthographic: the grid in the plane facing the view (Maya), a pixel of a fixed size
        uint32_t plane = 0;
        if (cam.ortho) plane = std::fabs(f.y) >= std::max(std::fabs(f.x), std::fabs(f.z)) ? 0u : (std::fabs(f.z) >= std::fabs(f.x) ? 1u : 2u);
        const uint32_t flags = g.flags | (plane << 8);
        float gc[32] = {rs.editorGrid, rs.editorGridAxis, g.opacity, cam.ortho ? cam.orthoHeight / std::max(1, vp.h) : 2.0f * t / std::max(1, vp.h),
                        std::max(rs.pixelScale, 1.0f), (float)std::max(1, g.subdivisions), std::max(g.extent, 0.0f), 0.0f,
                        g.widthGrid, g.widthSub, g.widthAxis, cam.ortho ? cam.orthoHeight * 0.5f : 0.0f};
        memcpy(&gc[7], &flags, sizeof(uint32_t));   // GridConstants::flags (uint)
        const float4* cols[5] = {&g.colorGrid, &g.colorSub, &g.colorX, &g.colorY, &g.colorZ};
        for (int k = 0; k < 5; ++k) memcpy(&gc[12 + 4 * k], cols[k], sizeof(float4));
        m_cmd->SetGraphicsRoot32BitConstants(7, 32, gc, 0);
        m_cmd->SetPipelineState(m_psoGrid.Get());
        m_cmd->DrawInstanced(3, 1, 0, 0);
    };
    if (overlay) {   // depth of the opaque objects, then the grid
        m_cmd->SetPipelineState(m_psoDepth.Get());
        drawAll([&](const Draw& d) { return !(d.flags & 5u) && !isTransparent(d); });
        drawGrid();
    } else if (rs.wireframe == 1) {
        m_cmd->SetPipelineState(m_psoMain.Get());
        drawAll([&](const Draw& d) { return !(d.flags & 5u) && isGround(d); });
        drawGrid();
    } else {
    m_cmd->SetPipelineState(m_psoMain.Get());
    drawAll([&](const Draw& d) { return !(d.flags & 5u) && !isTransparent(d); });
    drawGrid();
    m_cmd->SetPipelineState(m_psoTrans.Get());
    drawAll([&](const Draw& d) { return !(d.flags & 5u) && isTransparent(d); });
    m_cmd->SetPipelineState(m_psoGhost.Get());
    drawAll([&](const Draw& d) { return d.flags & 1u; });
    }
    if (rs.wireframe) {   // light lines alone, dark lines over the shaded view
        m_cmd->SetPipelineState(m_psoWire.Get());
        m_cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
        const uint32_t wireBase = drawBase + nDraws;
        for (const Draw& d : m_draws) {
            if ((d.flags & 5u) || isGround(d)) continue;
            const MeshGpu& g = m_meshes[d.mesh];
            const uint32_t slot = wireBase + (uint32_t)(&d - m_draws.data());
            DrawConstants& dc = ((DrawConstants*)m_drawBuf.mapped)[slot];
            memcpy(dc.rows, d.xf.m, sizeof(dc.rows));
            dc.materialId = d.material;
            dc.id = d.id;
            dc.alpha = rs.wireframe == 1 ? 0.75f : 0.62f;   // line grey level
            dc.flags = d.flags | (rs.wireframe == 2 ? 8u : 0u);   // over the shaded view: faint light lines (PSWire)
            m_cmd->SetGraphicsRoot32BitConstants(1, 1, &slot, 0);
            m_cmd->DrawIndexedInstanced(g.lineCount, 1, g.lineOffset, (INT)g.vertexOffset, 0);
        }
        m_cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    }
    if (!m_edgeObjects.empty() && !probeCapture && m_psoWire) {   // edge outline: the hard edges of the objects given
        std::vector<uint32_t> ids;
        for (int o : m_edgeObjects)
            if (o >= 0 && o < (int)m_objectToInstance.size() && m_objectToInstance[o] >= 0) ids.push_back((uint32_t)(m_objectToInstance[o] + 1));
        const uint32_t edgeBase = ids.empty() ? 0 : allocDraws((uint32_t)ids.size());
        uint32_t n = 0;
        m_cmd->SetPipelineState(m_psoWire.Get());
        m_cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
        for (const Draw& d : m_draws) {
            if ((d.flags & 5u) || n >= ids.size() || std::find(ids.begin(), ids.end(), d.id) == ids.end()) continue;
            const MeshGpu& g = m_meshes[d.mesh];
            if (!g.hardCount) continue;
            const uint32_t slot = edgeBase + n++;
            DrawConstants& dc = ((DrawConstants*)m_drawBuf.mapped)[slot];
            memcpy(dc.rows, d.xf.m, sizeof(dc.rows));
            dc.materialId = d.material;
            dc.id = d.id;
            dc.alpha = m_edgeOpacity;
            dc.flags = d.flags | 16u | (m_edgeRed ? 32u : 0u);   // PSWire: the outline colour
            m_cmd->SetGraphicsRoot32BitConstants(1, 1, &slot, 0);
            m_cmd->DrawIndexedInstanced(g.hardCount, 1, g.hardOffset, (INT)g.vertexOffset, 0);
        }
        m_cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    }

    recordSelectionMask(drawOne);   // coverage of the selection (smooth outline)
    if (overlay) {   // the overlay (premultiplied alpha) goes to its slot, composited by the tonemapper
        resolveViewport(m_msaaColor.Get(), m_tex[overlaySlot].Get());
        return;
    }
    // 3) resolve into the HDR slot read by the tonemapper
    ID3D12Resource* idTex = xv ? xv->id.Get() : m_tex[kTexAlbedo].Get();
    if (!xv && rs.aaQuality >= 2 && !rs.raster && probes && !probeCapture) {
        // AAA: resolve in tonemapped space (ResolveTM.hlsl), compute: MSAA colour as t7, the HDR slot as u3
        transition(m_msaaColor.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        m_cmd->SetComputeRootSignature(m_rootSig.Get());
        m_cmd->SetComputeRootDescriptorTable(8, m_heap->GetGPUDescriptorHandleForHeapStart());
        D3D12_GPU_DESCRIPTOR_HANDLE msSrv = m_heap->GetGPUDescriptorHandleForHeapStart();
        msSrv.ptr += (UINT64)kMsaaSrvSlot * m_descSize;
        m_cmd->SetComputeRootDescriptorTable(14, msSrv);
        m_cmd->SetPipelineState(m_psoResolve.Get());
        m_cmd->Dispatch((vp.w + 7) / 8, (vp.h + 7) / 8, 1);
        uavBarrier(m_tex[kTexDenoiseA].Get());
        transition(m_msaaColor.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    } else {
        resolveViewport(xv ? xv->color.Get() : m_msaaColor.Get(), xv ? xv->hdr.Get() : m_tex[kTexDenoiseA].Get());
    }

    // 4) object ids (selection outline)
    D3D12_CPU_DESCRIPTOR_HANDLE idRtv = rtvBase;
    idRtv.ptr += m_rtvSize;
    D3D12_CPU_DESCRIPTOR_HANDLE idDsv = dsvBase;
    idDsv.ptr += m_dsvSize;
    transition(idTex, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RENDER_TARGET);
    const float zero[4] = {0, 0, 0, 0};
    m_cmd->OMSetRenderTargets(1, &idRtv, FALSE, &idDsv);
    m_cmd->ClearRenderTargetView(idRtv, zero, 0, nullptr);
    m_cmd->ClearDepthStencilView(idDsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    m_cmd->SetPipelineState(m_psoId.Get());
    m_cmd->SetGraphicsRootConstantBufferView(0, rcb.va() + 768);   // ids without the anti-aliasing jitter
    if (rs.idPass) drawAll([&](const Draw& d) { return !(d.flags & 7u); });
    transition(idTex, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (sunMask) transition(m_sunShadow.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (sunMask || reflOn) transition(m_sunShadowRaw.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (reflOn) transition(m_refl.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

// --------------------------------------------------------------------------- extra views

// Targets of an extra view: the multisampled colour, depth and selection mask (m_msaa samples), the selection
// coverage, the HDR image and the ids (single sample), the RGBA8 image of the UI; their RTVs, DSVs and the table of
// the tonemapper (u0..u5 as the main view's slots, the ones it does not use left null, then t8).
void Renderer::createViewTargets(ExtraView& v, uint32_t w, uint32_t h) {
    v.w = w;
    v.h = h;
    v.samples = m_msaa;
    v.outReadable = false;
    auto hp = defaultHeap();
    auto make = [&](ComPtr<ID3D12Resource>& r, DXGI_FORMAT fmt, uint32_t samples, D3D12_RESOURCE_FLAGS flags,
                    D3D12_RESOURCE_STATES state, const D3D12_CLEAR_VALUE* clear) {
        D3D12_RESOURCE_DESC d = {};
        d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        d.Width = w;
        d.Height = h;
        d.DepthOrArraySize = 1;
        d.MipLevels = 1;
        d.Format = fmt;
        d.SampleDesc.Count = samples;
        d.Flags = flags;
        r.Reset();
        m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, state, clear, IID_PPV_ARGS(&r));
    };
    D3D12_CLEAR_VALUE cc = {}, dc = {}, mc = {}, ic = {};
    cc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    dc.Format = DXGI_FORMAT_D32_FLOAT;
    dc.DepthStencil.Depth = 1.0f;
    mc.Format = DXGI_FORMAT_R8_UNORM;
    ic.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    const auto rt = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, ds = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, ua = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    make(v.color, DXGI_FORMAT_R16G16B16A16_FLOAT, m_msaa, rt, D3D12_RESOURCE_STATE_RENDER_TARGET, &cc);
    make(v.depth, DXGI_FORMAT_D32_FLOAT, m_msaa, ds, D3D12_RESOURCE_STATE_DEPTH_WRITE, &dc);
    make(v.selMask, DXGI_FORMAT_R8_UNORM, m_msaa, rt, D3D12_RESOURCE_STATE_RENDER_TARGET, &mc);
    make(v.selCov, DXGI_FORMAT_R8_UNORM, 1, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr);
    make(v.hdr, DXGI_FORMAT_R16G16B16A16_FLOAT, 1, ua, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr);
    make(v.id, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, rt | ua, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, &ic);
    make(v.idDepth, DXGI_FORMAT_D32_FLOAT, 1, ds, D3D12_RESOURCE_STATE_DEPTH_WRITE, &dc);
    make(v.out, DXGI_FORMAT_R8G8B8A8_UNORM, 1, ua, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_viewRtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += (SIZE_T)(v.index * 3) * m_rtvSize;
    for (ID3D12Resource* r : {v.color.Get(), v.id.Get(), v.selMask.Get()}) {
        m_device->CreateRenderTargetView(r, nullptr, rtv);
        rtv.ptr += m_rtvSize;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_viewDsvHeap->GetCPUDescriptorHandleForHeapStart();
    dsv.ptr += (SIZE_T)(v.index * 2) * m_dsvSize;
    m_device->CreateDepthStencilView(v.depth.Get(), nullptr, dsv);
    dsv.ptr += m_dsvSize;
    m_device->CreateDepthStencilView(v.idDepth.Get(), nullptr, dsv);

    D3D12_CPU_DESCRIPTOR_HANDLE base = m_heap->GetCPUDescriptorHandleForHeapStart();
    base.ptr += (SIZE_T)(kViewHeapBase + v.index * kViewHeapStride) * m_descSize;
    const DXGI_FORMAT fmts[kTexCount] = {DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT,
                                         DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R8G8B8A8_UNORM};
    ID3D12Resource* res[kTexCount] = {nullptr, v.id.Get(), nullptr, v.hdr.Get(), nullptr, v.out.Get()};
    for (int k = 0; k < kTexCount; ++k) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC u = {};
        u.Format = fmts[k];
        u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        D3D12_CPU_DESCRIPTOR_HANDLE hnd = base;
        hnd.ptr += (SIZE_T)k * m_descSize;
        m_device->CreateUnorderedAccessView(res[k], nullptr, &u, hnd);
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.Format = DXGI_FORMAT_R8_UNORM;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE ch = base;
    ch.ptr += (SIZE_T)kTexCount * m_descSize;
    m_device->CreateShaderResourceView(v.selCov.Get(), &sv, ch);
    if (!v.cb.res) v.cb = createBuffer(2048, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
}

// The extra views of a frame are recorded into m_viewCmd (m_cmd points to it meanwhile: the raster helpers record
// into m_cmd), sent after the main list. present() waits for the GPU at the end of each frame, so the allocator is free.
void Renderer::beginViews() {
    if (m_viewsOpen || !m_viewCmd) return;
    m_viewAlloc->Reset();
    m_viewCmd->Reset(m_viewAlloc.Get(), nullptr);
    std::swap(m_cmd, m_viewCmd);
    ID3D12DescriptorHeap* heaps[] = {m_heap.Get()};
    m_cmd->SetDescriptorHeaps(1, heaps);
    m_viewsOpen = true;
    m_viewsShadow = true;   // (the main view may be path traced: no shadow map drawn this frame)
}

void Renderer::endViews() {
    if (!m_viewsOpen) return;
    m_cmd->Close();
    ID3D12CommandList* l[] = {m_cmd.Get()};
    m_queue->ExecuteCommandLists(1, l);
    std::swap(m_cmd, m_viewCmd);
    m_viewsOpen = false;
}

void Renderer::renderView(int slot, const Scene& scene, const Camera& cam, const RenderSettings& rs, uint32_t w, uint32_t h,
                          float2 selectionCenter, float selectionRadius) {
    if (!m_viewsOpen || slot < 0 || slot >= kMaxViews || !m_psoMain || !m_vb.res) return;
    if (scene.geometryVersion != m_sceneGeometryVersion) return;   // (the main render of the frame takes the scene first)
    w = std::max(16u, w);
    h = std::max(16u, h);
    if ((int)m_extra.size() <= slot) m_extra.resize(slot + 1);
    ExtraView& v = m_extra[slot];
    v.index = slot;
    if (v.w != w || v.h != h || v.samples != m_msaa || !v.out) createViewTargets(v, w, h);
    if (v.outReadable) {
        transition(v.out.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        v.outReadable = false;
    }
    ViewRect vp;
    vp.x = 0;
    vp.y = 0;
    vp.w = (int)w;
    vp.h = (int)h;
    m_rtView = &v;
    m_rtCam = &cam;
    recordRaster(scene, rs, vp, false, m_lastProbes, m_viewsShadow);
    m_viewsShadow = false;
    m_rtView = nullptr;
    m_rtCam = nullptr;

    // tonemapper: the frame constants of the main view with this camera, size and selection centre
    gpu::FrameConstants fc;
    memcpy(&fc, m_lastFc, sizeof(fc));
    float3 r, u, f;
    cam.basis(r, u, f);
    fc.camPos = cam.eye();
    fc.camRight = r;
    fc.camUp = u;
    fc.camFwd = f;
    fc.tanHalfFovY = std::tan(cam.fovY * 0.5f);
    fc.aspect = (float)w / (float)h;
    fc.orthoHalfH = cam.ortho ? cam.orthoHeight * 0.5f : 0.0f;
    fc.orthoBack = cam.ortho ? orthoBack(cam) : 0.0f;
    fc.resolution = {w, h};
    fc.selCenterX = selectionCenter.x;
    fc.selCenterY = selectionCenter.y;
    fc.selRadius = selectionRadius;
    fc.selPixelScale = 1.0f;
    fc.selCovValid = (!m_selIds.empty() && m_psoSelect) ? 1u : 0u;
    memcpy((uint8_t*)v.cb.mapped + 1536, &fc, sizeof(fc));
    m_cmd->SetComputeRootSignature(m_rootSig.Get());
    m_cmd->SetComputeRootConstantBufferView(0, v.cb.va() + 1536);
    D3D12_GPU_DESCRIPTOR_HANDLE table = m_heap->GetGPUDescriptorHandleForHeapStart();
    table.ptr += (UINT64)(kViewHeapBase + slot * kViewHeapStride) * m_descSize;
    m_cmd->SetComputeRootDescriptorTable(8, table);
    D3D12_GPU_DESCRIPTOR_HANDLE cov = table;
    cov.ptr += (UINT64)kTexCount * m_descSize;
    m_cmd->SetComputeRootDescriptorTable(23, cov);
    m_cmd->SetPipelineState(m_psoTonemap.Get());
    uint32_t tc[4] = {(uint32_t)kTexDenoiseA, 1u, (uint32_t)rs.tonemapper, 0u};
    m_cmd->SetComputeRoot32BitConstants(1, 4, tc, 0);
    m_cmd->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
    transition(v.out.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    v.outReadable = true;
}

ID3D12Resource* Renderer::viewOutput(int slot) const {
    return slot >= 0 && slot < (int)m_extra.size() ? m_extra[slot].out.Get() : nullptr;
}

bool Renderer::readView(int slot, std::vector<uint8_t>& rgba, uint32_t& w, uint32_t& h) {
    if (m_viewsOpen || slot < 0 || slot >= (int)m_extra.size() || !m_extra[slot].out) return false;
    ExtraView& v = m_extra[slot];
    waitIdle();
    D3D12_RESOURCE_DESC d = v.out->GetDesc();
    w = (uint32_t)d.Width;
    h = d.Height;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {};
    UINT64 total = 0;
    m_device->GetCopyableFootprints(&d, 0, 1, 0, &fp, nullptr, nullptr, &total);
    if (!m_readback.res || m_readback.size < total)
        m_readback = createBuffer(total, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    const D3D12_RESOURCE_STATES st = v.outReadable ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    beginCommands();
    transition(v.out.Get(), st, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION dl = {}, sl = {};
    dl.pResource = m_readback.res.Get();
    dl.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dl.PlacedFootprint = fp;
    sl.pResource = v.out.Get();
    sl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    m_cmd->CopyTextureRegion(&dl, 0, 0, 0, &sl, nullptr);
    transition(v.out.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, st);
    executeAndWait();
    void* p = nullptr;
    D3D12_RANGE rr = {0, (SIZE_T)total};
    if (FAILED(m_readback.res->Map(0, &rr, &p))) return false;
    rgba.resize((size_t)w * h * 4);
    for (uint32_t y = 0; y < h; ++y) memcpy(&rgba[(size_t)y * w * 4], (uint8_t*)p + fp.Offset + (size_t)y * fp.Footprint.RowPitch, (size_t)w * 4);
    D3D12_RANGE wr = {0, 0};
    m_readback.res->Unmap(0, &wr);
    return true;
}

// Ambient occlusion of the raster viewport (SSAO.hlsl): occlusion from the viewport depth into the alpha of the
// resolved HDR slot, then a 4 x 4 blur multiplied into its colour, before the tonemapper reads it.
void Renderer::recordSSAO(const RenderSettings& rs, const ViewRect& vp, const Scene& scene) {
    ID3D12PipelineState* pso = m_msaa > 1 ? m_psoSSAOMS.Get() : m_psoSSAO.Get();
    if (!pso || !m_psoSSAOBlur || !m_msaaDepth || scene.camera.ortho) return;
    const Camera& cam = scene.camera;
    const float zn = std::max(cam.distance * 0.005f, 0.02f), zf = cam.distance * 40.0f + 2000.0f;   // (recordRaster)
    struct { uint32_t slot; float A, B, radius; } c = {(uint32_t)kTexDenoiseA, zf / (zf - zn), -zf * zn / (zf - zn), rs.ssaoRadius};
    transition(m_msaaDepth.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    m_cmd->SetComputeRootSignature(m_rootSig.Get());
    m_cmd->SetComputeRootConstantBufferView(0, m_constants.va());
    m_cmd->SetComputeRootDescriptorTable(8, m_heap->GetGPUDescriptorHandleForHeapStart());
    D3D12_GPU_DESCRIPTOR_HANDLE t8 = m_heap->GetGPUDescriptorHandleForHeapStart();
    t8.ptr += (UINT64)kSelCovSlot * m_descSize;
    m_cmd->SetComputeRootDescriptorTable(23, t8);
    m_cmd->SetComputeRoot32BitConstants(1, 4, &c, 0);
    m_cmd->SetPipelineState(pso);
    m_cmd->Dispatch((vp.w + 7) / 8, (vp.h + 7) / 8, 1);
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    b.UAV.pResource = m_tex[kTexDenoiseA].Get();
    m_cmd->ResourceBarrier(1, &b);
    m_cmd->SetPipelineState(m_psoSSAOBlur.Get());
    m_cmd->Dispatch((vp.w + 7) / 8, (vp.h + 7) / 8, 1);
    m_cmd->ResourceBarrier(1, &b);
    transition(m_msaaDepth.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
}

} // namespace vp3d

