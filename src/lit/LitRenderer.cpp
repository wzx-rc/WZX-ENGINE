// Lit renderer on Diligent Engine (see LitRenderer.h). The passes follow Renderer::recordRaster
// (src/vp3d/RendererRaster.cpp): shadow map, background, opaque, transparent, ghosts, MSAA resolve, then the
// raster branch of the tonemap pass.
#include "LitRenderer.h"

#include "Graphics/GraphicsEngine/interface/Shader.h"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"

#include <cstring>

using namespace Diligent;

namespace lit {

namespace {

struct PostConstants {
    float exposure, vignette, aspect;
    uint32_t tonemapper;
    float invW, invH, pad0, pad1;
};

RefCntAutoPtr<IBuffer> makeDynamicCB(IRenderDevice* device, uint32_t size, const char* name) {
    BufferDesc d;
    d.Name = name;
    d.Size = size;
    d.Usage = USAGE_DYNAMIC;
    d.BindFlags = BIND_UNIFORM_BUFFER;
    d.CPUAccessFlags = CPU_ACCESS_WRITE;
    RefCntAutoPtr<IBuffer> b;
    device->CreateBuffer(d, nullptr, &b);
    return b;
}

RefCntAutoPtr<ITexture> makeTexture2D(IRenderDevice* device, const char* name, uint32_t w, uint32_t h, TEXTURE_FORMAT fmt,
                                      BIND_FLAGS bind, uint32_t samples = 1, uint32_t mips = 1,
                                      const TextureData* data = nullptr) {
    TextureDesc d;
    d.Name = name;
    d.Type = RESOURCE_DIM_TEX_2D;
    d.Width = w;
    d.Height = h;
    d.Format = fmt;
    d.BindFlags = bind;
    d.SampleCount = samples;
    d.MipLevels = mips;
    d.Usage = data ? USAGE_IMMUTABLE : USAGE_DEFAULT;
    if (bind & BIND_DEPTH_STENCIL) {
        d.ClearValue.Format = fmt;
        d.ClearValue.DepthStencil.Depth = 1.0f;
    }
    RefCntAutoPtr<ITexture> t;
    device->CreateTexture(d, data, &t);
    return t;
}

void setVar(IShaderResourceBinding* srb, SHADER_TYPE stage, const char* name, IDeviceObject* obj) {
    if (IShaderResourceVariable* v = srb->GetVariableByName(stage, name)) v->Set(obj);
}

} // namespace

bool LitRenderer::init(IRenderDevice* device, IDeviceContext* context, IEngineFactory* factory, const std::string& shaderDirs,
                       TEXTURE_FORMAT outputFormat, std::string* error) {
    m_device = device;
    m_ctx = context;
    m_outFormat = outputFormat;
    factory->CreateDefaultShaderSourceStreamFactory(shaderDirs.c_str(), &m_shaderSource);
    if (!m_shaderSource) {
        if (error) *error = "Shader folder not found: " + shaderDirs;
        return false;
    }

    // MSAA 4x as the DirectX 12 viewport, lower when the GPU cannot do it on RGBA16F.
    const TextureFormatInfoExt& ci = device->GetTextureFormatInfoExt(TEX_FORMAT_RGBA16_FLOAT);
    const TextureFormatInfoExt& di = device->GetTextureFormatInfoExt(TEX_FORMAT_D32_FLOAT);
    m_msaa = 4;
    while (m_msaa > 1 && (!(ci.SampleCounts & m_msaa) || !(di.SampleCounts & m_msaa))) m_msaa >>= 1;

    m_rasterCB = makeDynamicCB(device, sizeof(vp3d::lit::RasterConstants), "Lit raster constants");
    m_drawCB = makeDynamicCB(device, sizeof(vp3d::lit::DrawConstants), "Lit draw constants");
    m_postCB = makeDynamicCB(device, sizeof(PostConstants), "Lit post constants");

    // 1x1 white texture: unused material texture slots and the HDRI when there is none
    const uint32_t white = 0xFFFFFFFFu;
    TextureSubResData sub;
    sub.pData = &white;
    sub.Stride = 4;
    TextureData td{&sub, 1};
    m_white = makeTexture2D(device, "Lit white", 1, 1, TEX_FORMAT_RGBA8_UNORM_SRGB, BIND_SHADER_RESOURCE, 1, 1, &td);
    m_textures.assign(MAX_TEXTURES, m_white);

    m_shadow = makeTexture2D(device, "Lit shadow map", kShadowSize, kShadowSize, TEX_FORMAT_D32_FLOAT,
                             BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE);
    if (!m_white || !m_shadow || !m_rasterCB || !m_drawCB || !m_postCB) {
        if (error) *error = "Lit: resource creation failed";
        return false;
    }
    return createPipelines(error);
}

void LitRenderer::shutdown() {
    if (m_ctx) {
        m_ctx->Flush();
        m_ctx->WaitForIdle();
    }
    *this = LitRenderer();
}

bool LitRenderer::createPipelines(std::string* error) {
    // Raster.hlsl as the editor compiles it, with ATTRIBn vertex semantics (Diligent's input mapping). Outside
    // Direct3D (glslang has no NonUniformResourceIndex) the material texture index stays a plain index: it is the
    // same for the whole draw anyway.
    std::vector<ShaderMacro> macros = {{"VP3D_ATTRIB_SEMANTICS", "1"}};
    if (!m_device->GetDeviceInfo().IsD3DDevice()) macros.push_back({"NonUniformResourceIndex(x)", "(x)"});

    auto shader = [&](const char* file, const char* entry, SHADER_TYPE type, RefCntAutoPtr<IShader>& out) {
        ShaderCreateInfo ci;
        ci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
        ci.pShaderSourceStreamFactory = m_shaderSource;
        ci.FilePath = file;
        ci.EntryPoint = entry;
        ci.Desc.ShaderType = type;
        ci.Desc.Name = entry;
        ci.Macros = {macros.data(), (Uint32)macros.size()};
        // Raster.hlsl declares its matrices row_major; glslang drops that qualifier inside ConstantBuffer<T> structs,
        // so row-major is also made the default packing.
        ci.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
        RefCntAutoPtr<IDataBlob> log;
        m_device->CreateShader(ci, &out, &log);
        if (!out && error) {
            *error = std::string("Shader ") + file + " / " + entry + " failed";
            if (log && log->GetSize()) *error += ":\n" + std::string((const char*)log->GetConstDataPtr(), log->GetSize());
        }
        return out != nullptr;
    };
    RefCntAutoPtr<IShader> vsMain, vsShadow, vsBg, psMain, psGhost, psBg, vsPost, psPost;
    if (!shader("Raster.hlsl", "VSMain", SHADER_TYPE_VERTEX, vsMain) ||
        !shader("Raster.hlsl", "VSShadow", SHADER_TYPE_VERTEX, vsShadow) ||
        !shader("Raster.hlsl", "VSBackground", SHADER_TYPE_VERTEX, vsBg) ||
        !shader("Raster.hlsl", "PSMain", SHADER_TYPE_PIXEL, psMain) ||
        !shader("Raster.hlsl", "PSGhost", SHADER_TYPE_PIXEL, psGhost) ||
        !shader("Raster.hlsl", "PSBackground", SHADER_TYPE_PIXEL, psBg) ||
        !shader("LitPost.hlsl", "VSPost", SHADER_TYPE_VERTEX, vsPost) ||
        !shader("LitPost.hlsl", "PSPost", SHADER_TYPE_PIXEL, psPost))
        return false;

    const LayoutElement layout[] = {
        LayoutElement{0, 0, 3, VT_FLOAT32, False},   // position
        LayoutElement{1, 0, 3, VT_FLOAT32, False},   // normal
        LayoutElement{2, 0, 2, VT_FLOAT32, False},   // uv
    };

    // Static samplers of RendererRaster.cpp: shadow comparison (border white) and anisotropic wrap.
    SamplerDesc shadowCmp;
    shadowCmp.MinFilter = shadowCmp.MagFilter = FILTER_TYPE_COMPARISON_LINEAR;
    shadowCmp.MipFilter = FILTER_TYPE_COMPARISON_POINT;
    shadowCmp.AddressU = shadowCmp.AddressV = shadowCmp.AddressW = TEXTURE_ADDRESS_BORDER;
    shadowCmp.BorderColor[0] = shadowCmp.BorderColor[1] = shadowCmp.BorderColor[2] = shadowCmp.BorderColor[3] = 1.0f;
    shadowCmp.ComparisonFunc = COMPARISON_FUNC_LESS_EQUAL;
    SamplerDesc linearWrap;
    linearWrap.MinFilter = linearWrap.MagFilter = linearWrap.MipFilter = FILTER_TYPE_ANISOTROPIC;
    linearWrap.MaxAnisotropy = 8;
    linearWrap.AddressU = linearWrap.AddressV = linearWrap.AddressW = TEXTURE_ADDRESS_WRAP;
    const ImmutableSamplerDesc samplers[] = {
        {SHADER_TYPE_PIXEL, "gShadowCmp", shadowCmp},
        {SHADER_TYPE_PIXEL, "gLinearWrap", linearWrap},
    };

    auto base = [&](GraphicsPipelineStateCreateInfo& ci, const char* name) {
        ci.PSODesc.Name = name;
        ci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
        ci.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC;
        ci.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
        ci.PSODesc.ResourceLayout.NumImmutableSamplers = 2;
        GraphicsPipelineDesc& g = ci.GraphicsPipeline;
        g.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        g.RasterizerDesc.FillMode = FILL_MODE_SOLID;
        g.RasterizerDesc.CullMode = CULL_MODE_NONE;
        g.RasterizerDesc.DepthClipEnable = True;
        g.DepthStencilDesc.DepthEnable = True;
        g.DepthStencilDesc.DepthWriteEnable = True;
        g.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;
        g.DSVFormat = TEX_FORMAT_D32_FLOAT;
        g.InputLayout.LayoutElements = layout;
        g.InputLayout.NumElements = 3;
    };
    auto msaaColor = [&](GraphicsPipelineStateCreateInfo& ci) {
        ci.GraphicsPipeline.NumRenderTargets = 1;
        ci.GraphicsPipeline.RTVFormats[0] = TEX_FORMAT_RGBA16_FLOAT;
        ci.GraphicsPipeline.SmplDesc.Count = (Uint8)m_msaa;
    };
    auto blendOn = [](GraphicsPipelineStateCreateInfo& ci) {
        RenderTargetBlendDesc& b = ci.GraphicsPipeline.BlendDesc.RenderTargets[0];
        b.BlendEnable = True;
        b.SrcBlend = BLEND_FACTOR_SRC_ALPHA;
        b.DestBlend = BLEND_FACTOR_INV_SRC_ALPHA;
        b.BlendOp = BLEND_OPERATION_ADD;
        b.SrcBlendAlpha = BLEND_FACTOR_ONE;
        b.DestBlendAlpha = BLEND_FACTOR_INV_SRC_ALPHA;
        b.BlendOpAlpha = BLEND_OPERATION_ADD;
        ci.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = False;
    };
    auto make = [&](GraphicsPipelineStateCreateInfo& ci, int slot) {
        m_device->CreateGraphicsPipelineState(ci, &m_pso[slot]);
        if (!m_pso[slot]) {
            if (error) *error = std::string("Lit pipeline failed: ") + ci.PSODesc.Name;
            return false;
        }
        m_pso[slot]->CreateShaderResourceBinding(&m_srb[slot], true);
        return true;
    };

    {   // shadow (depth only)
        GraphicsPipelineStateCreateInfo ci;
        base(ci, "Lit shadow");
        ci.GraphicsPipeline.RasterizerDesc.DepthBias = 200;
        ci.GraphicsPipeline.RasterizerDesc.SlopeScaledDepthBias = 2.0f;
        ci.GraphicsPipeline.NumRenderTargets = 0;
        ci.pVS = vsShadow;
        if (!make(ci, P_SHADOW)) return false;
    }
    {   // background
        GraphicsPipelineStateCreateInfo ci;
        base(ci, "Lit background");
        msaaColor(ci);
        ci.GraphicsPipeline.InputLayout = {};
        ci.GraphicsPipeline.DepthStencilDesc.DepthEnable = False;
        ci.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = False;
        ci.pVS = vsBg;
        ci.pPS = psBg;
        if (!make(ci, P_BG)) return false;
    }
    {   // opaque, transparent, ghost
        GraphicsPipelineStateCreateInfo ci;
        base(ci, "Lit main");
        msaaColor(ci);
        ci.pVS = vsMain;
        ci.pPS = psMain;
        if (!make(ci, P_MAIN)) return false;
        blendOn(ci);
        ci.PSODesc.Name = "Lit transparent";
        if (!make(ci, P_TRANS)) return false;
        ci.PSODesc.Name = "Lit ghost";
        ci.pPS = psGhost;
        if (!make(ci, P_GHOST)) return false;
    }
    {   // post (exposure, vignette, view transform)
        GraphicsPipelineStateCreateInfo ci;
        base(ci, "Lit post");
        ci.PSODesc.ResourceLayout.ImmutableSamplers = nullptr;
        ci.PSODesc.ResourceLayout.NumImmutableSamplers = 0;
        ci.GraphicsPipeline.InputLayout = {};
        ci.GraphicsPipeline.DepthStencilDesc.DepthEnable = False;
        ci.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = False;
        ci.GraphicsPipeline.DSVFormat = TEX_FORMAT_UNKNOWN;
        ci.GraphicsPipeline.NumRenderTargets = 1;
        ci.GraphicsPipeline.RTVFormats[0] = m_outFormat;
        ci.pVS = vsPost;
        ci.pPS = psPost;
        if (!make(ci, P_POST)) return false;
    }
    return true;
}

void LitRenderer::updateBuffer(RefCntAutoPtr<IBuffer>& buf, const void* data, uint32_t size, uint32_t stride, const char* name) {
    if (!buf || buf->GetDesc().Size < size) {
        BufferDesc d;
        d.Name = name;
        uint64_t count = std::max<uint64_t>((uint64_t)size * 2 / stride, 64);   // room to grow, whole elements
        d.Size = count * stride;
        d.Usage = USAGE_DEFAULT;
        d.BindFlags = BIND_SHADER_RESOURCE;
        d.Mode = BUFFER_MODE_STRUCTURED;
        d.ElementByteStride = stride;
        buf.Release();
        m_device->CreateBuffer(d, nullptr, &buf);
    }
    m_ctx->UpdateBuffer(buf, 0, size, data, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

void LitRenderer::uploadGeometry(const vp3d::Scene& scene) {
    m_geometryVersion = scene.geometryVersion;
    m_geometryScene = &scene;
    vp3d::lit::buildGeometry(scene, m_geo);

    auto immutable = [&](const char* name, const void* data, uint64_t size, BIND_FLAGS bind, RefCntAutoPtr<IBuffer>& out) {
        out.Release();
        if (size == 0) return;
        BufferDesc d;
        d.Name = name;
        d.Size = size;
        d.Usage = USAGE_IMMUTABLE;
        d.BindFlags = bind;
        BufferData bd{data, size};
        m_device->CreateBuffer(d, &bd, &out);
    };
    immutable("Lit vertices", m_geo.vertices.data(), m_geo.vertices.size() * sizeof(vp3d::gpu::GpuVertex), BIND_VERTEX_BUFFER, m_vb);
    immutable("Lit indices", m_geo.indices.data(), m_geo.indices.size() * sizeof(uint32_t), BIND_INDEX_BUFFER, m_ib);

    // Material textures (RGBA8 sRGB, box-filtered mips), at most MAX_TEXTURES as in the DirectX 12 renderer.
    m_textures.assign(MAX_TEXTURES, m_white);
    m_textureValid.assign(scene.textures.size(), false);
    for (size_t ti = 0; ti < scene.textures.size() && ti < MAX_TEXTURES; ++ti) {
        vp3d::lit::MipChain mips;
        vp3d::lit::buildTextureMips(scene.textures[ti], mips);
        if (mips.levels.empty()) continue;
        std::vector<TextureSubResData> subs(mips.levels.size());
        for (size_t l = 0; l < subs.size(); ++l) {
            subs[l].pData = mips.levels[l].data();
            subs[l].Stride = (Uint64)mips.w[l] * 4;
        }
        TextureData td{subs.data(), (Uint32)subs.size()};
        RefCntAutoPtr<ITexture> t = makeTexture2D(m_device, scene.textures[ti].name.c_str(), mips.w[0], mips.h[0],
                                                  TEX_FORMAT_RGBA8_UNORM_SRGB, BIND_SHADER_RESOURCE, 1,
                                                  (uint32_t)subs.size(), &td);
        if (!t) continue;
        m_textures[ti] = t;
        m_textureValid[ti] = true;
    }
}

void LitRenderer::uploadEnvironment(const vp3d::Scene& scene) {
    m_envSource = scene.env.hdri.get();
    m_envUploaded = true;
    m_envTex.Release();
    m_env = vp3d::lit::Environment();
    if (!m_envSource || !vp3d::lit::prepareEnvironment(*m_envSource, m_env)) return;

    // RGBA16F with the box-filtered mip chain (blurred reflections read the mips)
    std::vector<std::vector<uint16_t>> half(m_env.mips.size());
    std::vector<TextureSubResData> subs(m_env.mips.size());
    for (size_t l = 0; l < m_env.mips.size(); ++l) {
        const std::vector<float>& rgb = m_env.mips[l];
        size_t n = (size_t)m_env.mipW[l] * m_env.mipH[l];
        half[l].resize(n * 4);
        for (size_t i = 0; i < n; ++i) {
            half[l][i * 4 + 0] = vp3d::lit::floatToHalf(rgb[i * 3 + 0]);
            half[l][i * 4 + 1] = vp3d::lit::floatToHalf(rgb[i * 3 + 1]);
            half[l][i * 4 + 2] = vp3d::lit::floatToHalf(rgb[i * 3 + 2]);
            half[l][i * 4 + 3] = vp3d::lit::floatToHalf(1.0f);
        }
        subs[l].pData = half[l].data();
        subs[l].Stride = (Uint64)m_env.mipW[l] * 8;
    }
    TextureData td{subs.data(), (Uint32)subs.size()};
    m_envTex = makeTexture2D(m_device, "Lit HDRI", m_env.mipW[0], m_env.mipH[0], TEX_FORMAT_RGBA16_FLOAT,
                             BIND_SHADER_RESOURCE, 1, (uint32_t)subs.size(), &td);
    if (!m_envTex) m_env = vp3d::lit::Environment();
}

void LitRenderer::createTargets(uint32_t w, uint32_t h) {
    m_w = w;
    m_h = h;
    m_msaaColor = makeTexture2D(m_device, "Lit MSAA colour", w, h, TEX_FORMAT_RGBA16_FLOAT, BIND_RENDER_TARGET, m_msaa);
    m_msaaDepth = makeTexture2D(m_device, "Lit MSAA depth", w, h, TEX_FORMAT_D32_FLOAT, BIND_DEPTH_STENCIL, m_msaa);
    m_hdr = makeTexture2D(m_device, "Lit HDR", w, h, TEX_FORMAT_RGBA16_FLOAT, BIND_RENDER_TARGET | BIND_SHADER_RESOURCE);
}

void LitRenderer::bindFrameResources() {
    std::vector<IDeviceObject*> texViews(MAX_TEXTURES);
    for (uint32_t i = 0; i < MAX_TEXTURES; ++i) texViews[i] = m_textures[i]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    ITextureView* env = (m_envTex ? m_envTex : m_white)->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    ITextureView* shadow = m_shadow->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    for (int p = P_SHADOW; p <= P_GHOST; ++p) {
        IShaderResourceBinding* srb = m_srb[p];
        for (SHADER_TYPE st : {SHADER_TYPE_VERTEX, SHADER_TYPE_PIXEL}) {
            setVar(srb, st, "R", m_rasterCB);
            setVar(srb, st, "D", m_drawCB);
        }
        setVar(srb, SHADER_TYPE_PIXEL, "gMats", m_materials->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
        setVar(srb, SHADER_TYPE_PIXEL, "gLights", m_lights->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
        setVar(srb, SHADER_TYPE_PIXEL, "gEnvTex", env);
        if (p != P_SHADOW && p != P_BG) setVar(srb, SHADER_TYPE_PIXEL, "gShadow", shadow);
        if (IShaderResourceVariable* v = srb->GetVariableByName(SHADER_TYPE_PIXEL, "gTextures"))
            v->SetArray(texViews.data(), 0, MAX_TEXTURES);
    }
    setVar(m_srb[P_POST], SHADER_TYPE_PIXEL, "PostConstants", m_postCB);
    setVar(m_srb[P_POST], SHADER_TYPE_PIXEL, "gHdr", m_hdr->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
}

void LitRenderer::render(const vp3d::Scene& scene, const Settings& settings, ITextureView* output, uint32_t width,
                         uint32_t height) {
    if (!m_device || !output || width == 0 || height == 0) return;
    if (m_geometryScene != &scene || m_geometryVersion != scene.geometryVersion) uploadGeometry(scene);
    if (!m_envUploaded || m_envSource != scene.env.hdri.get()) uploadEnvironment(scene);
    if (width != m_w || height != m_h) createTargets(width, height);
    if (!m_vb || !m_ib || !m_msaaColor || !m_hdr) return;

    // Materials, lights and draws every frame (objects move during play), as Renderer::buildTLAS.
    vp3d::lit::buildSceneData(scene, m_geo, m_textureValid, m_data);
    updateBuffer(m_materials, m_data.materials.data(), (uint32_t)(m_data.materials.size() * sizeof(vp3d::gpu::GpuMaterial)),
                 sizeof(vp3d::gpu::GpuMaterial), "Lit materials");
    updateBuffer(m_lights, m_data.lights.data(), (uint32_t)(m_data.lights.size() * sizeof(vp3d::gpu::GpuLight)),
                 sizeof(vp3d::gpu::GpuLight), "Lit lights");

    vp3d::lit::FrameSettings fs;
    fs.lightScale = settings.lightScale;
    fs.envSun = settings.envSun;
    fs.shadowSize = kShadowSize;
    bool castShadows = false;
    {
        MapHelper<vp3d::lit::RasterConstants> rc(m_ctx, m_rasterCB, MAP_WRITE, MAP_FLAG_DISCARD);
        vp3d::lit::computeFrame(scene, m_data, m_env.valid() ? &m_env : nullptr, fs, (int)width, (int)height, *rc, castShadows);
    }
    {
        MapHelper<PostConstants> pc(m_ctx, m_postCB, MAP_WRITE, MAP_FLAG_DISCARD);
        *pc = PostConstants{settings.exposure, settings.vignette, (float)width / height, (uint32_t)settings.tonemapper,
                            1.0f / width, 1.0f / height, 0, 0};
    }
    bindFrameResources();

    IBuffer* vbs[] = {m_vb};
    const Uint64 offsets[] = {0};
    m_ctx->SetVertexBuffers(0, 1, vbs, offsets, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
    m_ctx->SetIndexBuffer(m_ib, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    auto drawOne = [&](const vp3d::lit::Draw& d) {
        const vp3d::lit::MeshInfo& g = m_geo.meshes[d.mesh];
        {
            MapHelper<vp3d::lit::DrawConstants> dc(m_ctx, m_drawCB, MAP_WRITE, MAP_FLAG_DISCARD);
            *dc = vp3d::lit::drawConstants(d);
        }
        DrawIndexedAttribs a;
        a.NumIndices = g.indexCount;
        a.IndexType = VT_UINT32;
        a.FirstIndexLocation = g.indexOffset;
        a.BaseVertex = g.vertexOffset;
        a.Flags = DRAW_FLAG_VERIFY_ALL;
        m_ctx->DrawIndexed(a);
    };
    auto usePipeline = [&](int p) {
        m_ctx->SetPipelineState(m_pso[p]);
        m_ctx->CommitShaderResources(m_srb[p], RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    };

    // 1) shadow map
    ITextureView* shadowDsv = m_shadow->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
    m_ctx->SetRenderTargets(0, nullptr, shadowDsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_ctx->ClearDepthStencil(shadowDsv, CLEAR_DEPTH_FLAG, 1.0f, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_ctx->SetViewports(1, nullptr, kShadowSize, kShadowSize);
    if (castShadows) {
        usePipeline(P_SHADOW);
        for (const vp3d::lit::Draw& d : m_data.draws)
            if (!(d.flags & 7u) && !vp3d::lit::isTransparent(scene, d)) drawOne(d);
    }

    // 2) main MSAA pass: background, opaque, transparent, ghosts
    ITextureView* rtv = m_msaaColor->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    ITextureView* dsv = m_msaaDepth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
    m_ctx->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_ctx->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1.0f, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_ctx->SetViewports(1, nullptr, width, height);
    usePipeline(P_BG);
    DrawAttribs bg;
    bg.NumVertices = 3;
    m_ctx->Draw(bg);
    usePipeline(P_MAIN);
    for (const vp3d::lit::Draw& d : m_data.draws)
        if (!(d.flags & 5u) && !vp3d::lit::isTransparent(scene, d)) drawOne(d);
    usePipeline(P_TRANS);
    for (const vp3d::lit::Draw& d : m_data.draws)
        if (!(d.flags & 5u) && vp3d::lit::isTransparent(scene, d)) drawOne(d);
    usePipeline(P_GHOST);
    for (const vp3d::lit::Draw& d : m_data.draws)
        if (d.flags & 1u) drawOne(d);

    // 3) resolve
    if (m_msaa > 1) {
        ResolveTextureSubresourceAttribs ra;
        ra.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        ra.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        m_ctx->ResolveTextureSubresource(m_msaaColor, m_hdr, ra);
    } else {
        CopyTextureAttribs ca(m_msaaColor, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, m_hdr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_ctx->CopyTexture(ca);
    }

    // 4) post into the output
    m_ctx->SetRenderTargets(1, &output, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_ctx->SetViewports(1, nullptr, width, height);
    usePipeline(P_POST);
    DrawAttribs post;
    post.NumVertices = 3;
    m_ctx->Draw(post);
}

} // namespace lit
