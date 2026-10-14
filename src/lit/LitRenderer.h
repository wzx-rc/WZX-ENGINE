// Lit - the real-time raster renderer of WZX games on every target, built on Diligent Engine
// (DirectX 12 on Windows, Metal through MoltenVK on iOS, Vulkan for the Linux test build).
//
// It draws a vp3d::Scene with the editor's own raster shaders (src/vp3d/shaders/Raster.hlsl) and the CPU
// preparation shared with the DirectX 12 viewport (vp3d/LitPrep.h): shadow map, forward PBR with MSAA,
// HDRI ambient and reflections, then exposure, vignette and the view transform.
//
// Usage:
//   LitRenderer r; r.init(device, context, factory, shaderDirs, outputFormat);
//   each frame: r.render(scene, settings, outputRtv, width, height);
#pragma once
#include "vp3d/LitPrep.h"

#include "Common/interface/RefCntAutoPtr.hpp"
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "Graphics/GraphicsEngine/interface/EngineFactory.h"
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"

#include <string>
#include <vector>

namespace lit {

using Diligent::RefCntAutoPtr;

struct Settings {
    float exposure = 1.0f;
    float vignette = 0.12f;
    int tonemapper = 0;          // 0 ACES, 1 AgX, 2 AgX Punchy, 3 Standard
    float lightScale = 1.0f;
    bool envSun = true;          // the sun of the HDRI is the shadowed key light
};

class LitRenderer {
public:
    // shaderDirs: semicolon-separated folders holding Raster.hlsl, Shared.h, ToneCurves.hlsli and LitPost.hlsl.
    // outputFormat: format of the render targets passed to render() (non-sRGB: the post pass encodes sRGB itself).
    bool init(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context, Diligent::IEngineFactory* factory,
              const std::string& shaderDirs, Diligent::TEXTURE_FORMAT outputFormat, std::string* error = nullptr);
    void shutdown();

    // Renders the scene into output (width x height). Geometry and textures are uploaded again when
    // Scene::geometryVersion changes, the HDRI when Scene::env.hdri changes; objects and materials every frame.
    void render(const vp3d::Scene& scene, const Settings& settings, Diligent::ITextureView* output, uint32_t width,
                uint32_t height);

    uint32_t msaaSamples() const { return m_msaa; }

private:
    bool createPipelines(std::string* error);
    void uploadGeometry(const vp3d::Scene& scene);
    void uploadEnvironment(const vp3d::Scene& scene);
    void createTargets(uint32_t w, uint32_t h);
    void updateBuffer(RefCntAutoPtr<Diligent::IBuffer>& buf, const void* data, uint32_t size, uint32_t stride,
                      const char* name);
    void bindFrameResources();

    RefCntAutoPtr<Diligent::IRenderDevice> m_device;
    RefCntAutoPtr<Diligent::IDeviceContext> m_ctx;
    RefCntAutoPtr<Diligent::IShaderSourceInputStreamFactory> m_shaderSource;
    Diligent::TEXTURE_FORMAT m_outFormat = Diligent::TEX_FORMAT_UNKNOWN;
    uint32_t m_msaa = 4;
    static constexpr uint32_t kShadowSize = 2048;

    // pipelines (shadow, background, opaque, transparent, ghost, post) and their resource bindings
    enum { P_SHADOW, P_BG, P_MAIN, P_TRANS, P_GHOST, P_POST, P_COUNT };
    RefCntAutoPtr<Diligent::IPipelineState> m_pso[P_COUNT];
    RefCntAutoPtr<Diligent::IShaderResourceBinding> m_srb[P_COUNT];

    // scene
    uint32_t m_geometryVersion = 0;
    const vp3d::Scene* m_geometryScene = nullptr;
    vp3d::lit::Geometry m_geo;
    vp3d::lit::SceneData m_data;
    RefCntAutoPtr<Diligent::IBuffer> m_vb, m_ib, m_materials, m_lights;
    RefCntAutoPtr<Diligent::IBuffer> m_rasterCB, m_drawCB, m_postCB;
    std::vector<RefCntAutoPtr<Diligent::ITexture>> m_textures;   // MAX_TEXTURES slots (white when unused)
    std::vector<bool> m_textureValid;
    RefCntAutoPtr<Diligent::ITexture> m_white;

    // environment
    const vp3d::EnvImage* m_envSource = nullptr;
    bool m_envUploaded = false;
    vp3d::lit::Environment m_env;
    RefCntAutoPtr<Diligent::ITexture> m_envTex;

    // targets
    uint32_t m_w = 0, m_h = 0;
    RefCntAutoPtr<Diligent::ITexture> m_shadow, m_msaaColor, m_msaaDepth, m_hdr;
};

} // namespace lit
