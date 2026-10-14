#include "ImGuiDiligent.h"
#include "imgui.h"

#include "Graphics/GraphicsEngine/interface/Shader.h"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

using namespace Diligent;

namespace player {

namespace {

// Vertex position in pixels to clip space: p * scale + offset (no matrix, so no packing question across APIs).
const char* kShader = R"(
cbuffer Constants { float4 gScaleOffset; };
struct VSIn { float2 pos : ATTRIB0; float2 uv : ATTRIB1; float4 col : ATTRIB2; };
struct PSIn { float4 pos : SV_POSITION; float4 col : COLOR; float2 uv : TEXCOORD; };
void VSMain(in VSIn i, out PSIn o) {
    o.pos = float4(i.pos * gScaleOffset.xy + gScaleOffset.zw, 0.0, 1.0);
    o.col = i.col;
    o.uv = i.uv;
}
Texture2D gTexture;
SamplerState gSampler;
float4 PSMain(in PSIn i) : SV_Target { return i.col * gTexture.Sample(gSampler, i.uv); }
)";

struct BackendTex {
    RefCntAutoPtr<ITexture> tex;
};

} // namespace

bool ImGuiDiligent::init(IRenderDevice* device, IDeviceContext* context, TEXTURE_FORMAT outputFormat, std::string* error) {
    m_device = device;
    m_ctx = context;
    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererName = "wzx_diligent";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;

    auto shader = [&](const char* entry, SHADER_TYPE type, RefCntAutoPtr<IShader>& out) {
        ShaderCreateInfo ci;
        ci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
        ci.Source = kShader;
        ci.EntryPoint = entry;
        ci.Desc.ShaderType = type;
        ci.Desc.Name = entry;
        RefCntAutoPtr<IDataBlob> log;
        m_device->CreateShader(ci, &out, &log);
        if (!out && error) {
            *error = std::string("ImGui shader ") + entry + " failed";
            if (log && log->GetSize()) *error += ":\n" + std::string((const char*)log->GetConstDataPtr(), log->GetSize());
        }
        return out != nullptr;
    };
    RefCntAutoPtr<IShader> vs, ps;
    if (!shader("VSMain", SHADER_TYPE_VERTEX, vs) || !shader("PSMain", SHADER_TYPE_PIXEL, ps)) return false;

    const LayoutElement layout[] = {
        LayoutElement{0, 0, 2, VT_FLOAT32, False},   // pos
        LayoutElement{1, 0, 2, VT_FLOAT32, False},   // uv
        LayoutElement{2, 0, 4, VT_UINT8, True},      // col (RGBA8)
    };
    SamplerDesc linear;
    linear.MinFilter = linear.MagFilter = linear.MipFilter = FILTER_TYPE_LINEAR;
    linear.AddressU = linear.AddressV = linear.AddressW = TEXTURE_ADDRESS_CLAMP;
    const ImmutableSamplerDesc samplers[] = {{SHADER_TYPE_PIXEL, "gSampler", linear}};

    GraphicsPipelineStateCreateInfo ci;
    ci.PSODesc.Name = "ImGui";
    ci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
    ci.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC;
    ci.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
    ci.PSODesc.ResourceLayout.NumImmutableSamplers = 1;
    GraphicsPipelineDesc& g = ci.GraphicsPipeline;
    g.NumRenderTargets = 1;
    g.RTVFormats[0] = outputFormat;
    g.DSVFormat = TEX_FORMAT_UNKNOWN;
    g.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    g.RasterizerDesc.CullMode = CULL_MODE_NONE;
    g.RasterizerDesc.ScissorEnable = True;
    g.DepthStencilDesc.DepthEnable = False;
    g.DepthStencilDesc.DepthWriteEnable = False;
    RenderTargetBlendDesc& b = g.BlendDesc.RenderTargets[0];
    b.BlendEnable = True;
    b.SrcBlend = BLEND_FACTOR_SRC_ALPHA;
    b.DestBlend = BLEND_FACTOR_INV_SRC_ALPHA;
    b.BlendOp = BLEND_OPERATION_ADD;
    b.SrcBlendAlpha = BLEND_FACTOR_ONE;
    b.DestBlendAlpha = BLEND_FACTOR_INV_SRC_ALPHA;
    b.BlendOpAlpha = BLEND_OPERATION_ADD;
    g.InputLayout.LayoutElements = layout;
    g.InputLayout.NumElements = 3;
    ci.pVS = vs;
    ci.pPS = ps;
    m_device->CreateGraphicsPipelineState(ci, &m_pso);
    if (!m_pso) {
        if (error) *error = "ImGui pipeline failed";
        return false;
    }
    m_pso->CreateShaderResourceBinding(&m_srb, true);

    BufferDesc cb;
    cb.Name = "ImGui constants";
    cb.Size = 16;
    cb.Usage = USAGE_DYNAMIC;
    cb.BindFlags = BIND_UNIFORM_BUFFER;
    cb.CPUAccessFlags = CPU_ACCESS_WRITE;
    m_device->CreateBuffer(cb, nullptr, &m_cb);
    if (IShaderResourceVariable* v = m_srb->GetVariableByName(SHADER_TYPE_VERTEX, "Constants")) v->Set(m_cb);
    return m_cb != nullptr;
}

void ImGuiDiligent::shutdown() {
    for (ImTextureData* tex : ImGui::GetPlatformIO().Textures) {
        if (tex->RefCount != 1) continue;
        delete (BackendTex*)tex->BackendUserData;
        tex->BackendUserData = nullptr;
        tex->SetTexID(ImTextureID_Invalid);
        tex->SetStatus(ImTextureStatus_Destroyed);
    }
    *this = ImGuiDiligent();
}

void ImGuiDiligent::updateTexture(ImTextureData* tex) {
    if (tex->Status == ImTextureStatus_WantDestroy) {
        delete (BackendTex*)tex->BackendUserData;   // Diligent keeps the texture alive until the GPU is done with it
        tex->BackendUserData = nullptr;
        tex->SetTexID(ImTextureID_Invalid);
        tex->SetStatus(ImTextureStatus_Destroyed);
        return;
    }
    if (tex->Status == ImTextureStatus_WantCreate) {
        TextureDesc d;
        d.Name = "ImGui texture";
        d.Type = RESOURCE_DIM_TEX_2D;
        d.Width = (Uint32)tex->Width;
        d.Height = (Uint32)tex->Height;
        d.Format = TEX_FORMAT_RGBA8_UNORM;
        d.BindFlags = BIND_SHADER_RESOURCE;
        d.Usage = USAGE_DEFAULT;
        BackendTex* bt = new BackendTex();
        m_device->CreateTexture(d, nullptr, &bt->tex);
        if (!bt->tex) {
            delete bt;
            return;
        }
        tex->BackendUserData = bt;
        tex->SetTexID((ImTextureID)(uintptr_t)bt->tex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
    }
    if (tex->Status != ImTextureStatus_WantCreate && tex->Status != ImTextureStatus_WantUpdates) return;
    BackendTex* bt = (BackendTex*)tex->BackendUserData;
    if (!bt) return;
    const bool full = tex->Status == ImTextureStatus_WantCreate;
    const int x = full ? 0 : tex->UpdateRect.x, y = full ? 0 : tex->UpdateRect.y;
    const int w = full ? tex->Width : tex->UpdateRect.w, h = full ? tex->Height : tex->UpdateRect.h;
    if (w > 0 && h > 0) {
        std::vector<uint8_t> rgba((size_t)w * h * 4);
        for (int r = 0; r < h; ++r) {
            const uint8_t* src = (const uint8_t*)tex->GetPixelsAt(x, y + r);
            uint8_t* dst = rgba.data() + (size_t)r * w * 4;
            if (tex->Format == ImTextureFormat_RGBA32) {
                std::memcpy(dst, src, (size_t)w * 4);
            } else {   // Alpha8: white with the coverage as alpha
                for (int c = 0; c < w; ++c) {
                    dst[c * 4 + 0] = dst[c * 4 + 1] = dst[c * 4 + 2] = 255;
                    dst[c * 4 + 3] = src[c];
                }
            }
        }
        Box box((Uint32)x, (Uint32)(x + w), (Uint32)y, (Uint32)(y + h));
        TextureSubResData sub;
        sub.pData = rgba.data();
        sub.Stride = (Uint64)w * 4;
        m_ctx->UpdateTexture(bt->tex, 0, 0, box, sub, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                             RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }
    tex->SetStatus(ImTextureStatus_OK);
}

void ImGuiDiligent::render(ImDrawData* data, ITextureView* output) {
    if (!m_pso || !data || !output) return;
    if (data->Textures)
        for (ImTextureData* tex : *data->Textures)
            if (tex->Status != ImTextureStatus_OK) updateTexture(tex);
    const float fbW = data->DisplaySize.x * data->FramebufferScale.x, fbH = data->DisplaySize.y * data->FramebufferScale.y;
    if (fbW <= 0 || fbH <= 0 || data->TotalVtxCount == 0) return;

    auto grow = [&](RefCntAutoPtr<IBuffer>& buf, uint32_t& cap, uint32_t need, uint32_t stride, BIND_FLAGS bind, const char* name) {
        if (buf && cap >= need) return;
        cap = std::max(need + need / 2, 5000u);
        BufferDesc d;
        d.Name = name;
        d.Size = (Uint64)cap * stride;
        d.Usage = USAGE_DYNAMIC;
        d.BindFlags = bind;
        d.CPUAccessFlags = CPU_ACCESS_WRITE;
        buf.Release();
        m_device->CreateBuffer(d, nullptr, &buf);
    };
    grow(m_vb, m_vbCount, (uint32_t)data->TotalVtxCount, sizeof(ImDrawVert), BIND_VERTEX_BUFFER, "ImGui vertices");
    grow(m_ib, m_ibCount, (uint32_t)data->TotalIdxCount, sizeof(ImDrawIdx), BIND_INDEX_BUFFER, "ImGui indices");
    if (!m_vb || !m_ib) return;
    {
        MapHelper<ImDrawVert> vtx(m_ctx, m_vb, MAP_WRITE, MAP_FLAG_DISCARD);
        MapHelper<ImDrawIdx> idx(m_ctx, m_ib, MAP_WRITE, MAP_FLAG_DISCARD);
        ImDrawVert* v = vtx;
        ImDrawIdx* i = idx;
        for (const ImDrawList* l : data->CmdLists) {
            std::memcpy(v, l->VtxBuffer.Data, l->VtxBuffer.Size * sizeof(ImDrawVert));
            std::memcpy(i, l->IdxBuffer.Data, l->IdxBuffer.Size * sizeof(ImDrawIdx));
            v += l->VtxBuffer.Size;
            i += l->IdxBuffer.Size;
        }
    }
    {
        // pixels (DisplayPos origin, y down) to clip space (y up)
        MapHelper<float> c(m_ctx, m_cb, MAP_WRITE, MAP_FLAG_DISCARD);
        const float L = data->DisplayPos.x, T = data->DisplayPos.y, W = data->DisplaySize.x, H = data->DisplaySize.y;
        c[0] = 2.0f / W;
        c[1] = -2.0f / H;
        c[2] = -1.0f - L * 2.0f / W;
        c[3] = 1.0f + T * 2.0f / H;
    }

    auto setup = [&]() {
        m_ctx->SetRenderTargets(1, &output, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_ctx->SetViewports(1, nullptr, (Uint32)fbW, (Uint32)fbH);
        IBuffer* vbs[] = {m_vb};
        const Uint64 offsets[] = {0};
        m_ctx->SetVertexBuffers(0, 1, vbs, offsets, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
        m_ctx->SetIndexBuffer(m_ib, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_ctx->SetPipelineState(m_pso);
    };
    setup();

    IShaderResourceVariable* texVar = m_srb->GetVariableByName(SHADER_TYPE_PIXEL, "gTexture");
    const ImVec2 clipOff = data->DisplayPos, clipScale = data->FramebufferScale;
    uint32_t vtxBase = 0, idxBase = 0;
    for (const ImDrawList* l : data->CmdLists) {
        for (const ImDrawCmd& cmd : l->CmdBuffer) {
            if (cmd.UserCallback) {
                if (cmd.UserCallback == ImDrawCallback_ResetRenderState) setup();
                else cmd.UserCallback(l, &cmd);
                continue;
            }
            float x0 = std::max(0.0f, (cmd.ClipRect.x - clipOff.x) * clipScale.x);
            float y0 = std::max(0.0f, (cmd.ClipRect.y - clipOff.y) * clipScale.y);
            float x1 = std::min(fbW, (cmd.ClipRect.z - clipOff.x) * clipScale.x);
            float y1 = std::min(fbH, (cmd.ClipRect.w - clipOff.y) * clipScale.y);
            if (x1 <= x0 || y1 <= y0) continue;
            Rect r((Int32)x0, (Int32)y0, (Int32)x1, (Int32)y1);
            m_ctx->SetScissorRects(1, &r, (Uint32)fbW, (Uint32)fbH);
            ITextureView* view = (ITextureView*)(uintptr_t)cmd.GetTexID();
            if (!view || !texVar) continue;
            texVar->Set(view);
            m_ctx->CommitShaderResources(m_srb, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            DrawIndexedAttribs a;
            a.NumIndices = cmd.ElemCount;
            a.IndexType = sizeof(ImDrawIdx) == 2 ? VT_UINT16 : VT_UINT32;
            a.FirstIndexLocation = idxBase + cmd.IdxOffset;
            a.BaseVertex = vtxBase + cmd.VtxOffset;
            a.Flags = DRAW_FLAG_VERIFY_ALL;
            m_ctx->DrawIndexed(a);
        }
        vtxBase += (uint32_t)l->VtxBuffer.Size;
        idxBase += (uint32_t)l->IdxBuffer.Size;
    }
}

} // namespace player
