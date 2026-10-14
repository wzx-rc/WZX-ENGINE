// Dear ImGui drawn with Diligent Engine (DirectX 12 on Windows, Metal through MoltenVK on iOS): the HUD of the game.
// Supports the dynamic font atlas of ImGui 1.92 (ImGuiBackendFlags_RendererHasTextures).
#pragma once
#include "Common/interface/RefCntAutoPtr.hpp"
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"

#include <string>

struct ImDrawData;
struct ImTextureData;

namespace player {

class ImGuiDiligent {
public:
    bool init(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context, Diligent::TEXTURE_FORMAT outputFormat,
              std::string* error);
    void shutdown();
    // Draws over output (already holding the 3D image).
    void render(ImDrawData* data, Diligent::ITextureView* output);

private:
    void updateTexture(ImTextureData* tex);

    Diligent::RefCntAutoPtr<Diligent::IRenderDevice> m_device;
    Diligent::RefCntAutoPtr<Diligent::IDeviceContext> m_ctx;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> m_pso;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> m_srb;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> m_vb, m_ib, m_cb;
    uint32_t m_vbCount = 0, m_ibCount = 0;
};

} // namespace player
