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
    // DDGI probe volume without ray tracing (as the editor's --ddgi-raster 1): probes captured in raster, read by
    // the main pass instead of the hemisphere ambient. Same code, same probes as the editor.
    bool ddgi = false;
    int ddgiProbeBudget = 2048;  // probes over the scene bounds
    int ddgiCaptureProbes = 32;  // probes captured per frame
    float ddgiHysteresis = 0.97f;
    int ddgiView = 0;            // 0 full, 1 indirect only, 2 direct only
    bool showProbes = false;     // the probes as small spheres shaded with their irradiance
    // Baked probes (setBakedProbes): the main pass reads them when no live DDGI runs. No computation at run time.
    bool bakedProbes = true;
    // Rounded edges without ray tracing (the iPad): the normal bends over the edges of the box of each mesh (its outer
    // faces, the top at the level of its biggest upward face: under the studs), over the material bevel radius times
    // this scale. 0 = off. Cheap: a few instructions in the main pass.
    float bevel = 0.0f;
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
    uint32_t drawCalls() const { return m_drawCalls; }   // instanced draw calls of the last frame (all passes)
    // Probes baked by the editor (vp3d::lit::loadBakedProbes): read by the main pass as they are (Settings::bakedProbes).
    void setBakedProbes(const vp3d::lit::BakedProbes& probes);
    bool hasBakedProbes() const { return m_baked; }

private:
    bool createPipelines(std::string* error);
    void uploadGeometry(const vp3d::Scene& scene);
    void uploadEnvironment(const vp3d::Scene& scene);
    void createTargets(uint32_t w, uint32_t h);
    void updateBuffer(RefCntAutoPtr<Diligent::IBuffer>& buf, const void* data, uint32_t size, uint32_t stride,
                      const char* name);
    void bindFrameResources();
    // Instancing: the draw constants of the frame are uploaded once (m_drawBuf), sorted by mesh; drawRuns draws the
    // runs of consecutive draws of one mesh that pass the filter with one instanced call each.
    void sortDraws();
    template <class F> void drawRuns(F&& pass);
    void drawRange(int mesh, uint32_t first, uint32_t count);
    // DDGI: probe grid and atlases (on geometry or budget change), cube captures, then the two compute passes.
    void ensureProbes(const vp3d::Scene& scene, const Settings& settings);
    void recordProbes(const vp3d::Scene& scene, const Settings& settings);

    RefCntAutoPtr<Diligent::IRenderDevice> m_device;
    RefCntAutoPtr<Diligent::IDeviceContext> m_ctx;
    RefCntAutoPtr<Diligent::IShaderSourceInputStreamFactory> m_shaderSource;
    Diligent::TEXTURE_FORMAT m_outFormat = Diligent::TEX_FORMAT_UNKNOWN;
    uint32_t m_msaa = 4;
    static constexpr uint32_t kShadowSize = 2048;

    // pipelines (shadow, background, opaque, transparent, ghost, post, DDGI probe capture and its background, DDGI
    // compute passes: rays from the captures, blend) and their resource bindings
    enum { P_SHADOW, P_BG, P_MAIN, P_TRANS, P_GHOST, P_POST, P_PROBE, P_PROBE_BG, C_RAYS, C_BLEND, P_COUNT };
    RefCntAutoPtr<Diligent::IPipelineState> m_pso[P_COUNT];
    RefCntAutoPtr<Diligent::IShaderResourceBinding> m_srb[P_COUNT];

    // scene
    uint32_t m_geometryVersion = 0;
    const vp3d::Scene* m_geometryScene = nullptr;
    vp3d::lit::Geometry m_geo;
    vp3d::lit::SceneData m_data;
    RefCntAutoPtr<Diligent::IBuffer> m_vb, m_ib, m_materials, m_lights;
    RefCntAutoPtr<Diligent::IBuffer> m_rasterCB, m_drawCB, m_postCB;
    RefCntAutoPtr<Diligent::IBuffer> m_drawBuf;        // DrawConstants of the frame, sorted by mesh (gDraws)
    std::vector<uint32_t> m_drawOrder;                 // sorted slot -> index in m_data.draws
    // the draw constants of Lit: those of the DirectX 12 viewport, then the box of the bevel (Raster.hlsl, Lit only)
    struct LitDraw {
        vp3d::lit::DrawConstants d;
        float bmin[4];   // w: 1 = a box (the bevel applies)
        float bmax[4];
    };
    std::vector<LitDraw> m_drawSorted;
    std::vector<float> m_bevelBox;   // per mesh: min xyz, max xyz, valid (7 floats)
    uint32_t m_drawCalls = 0;
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

    // DDGI probe volume: grid, atlases (irradiance, distance, rays, state), cube captures of the probes of a batch
    vp3d::lit::ProbeGrid m_grid;
    uint32_t m_gridVersion = 0;
    int m_gridBudget = 0;
    uint32_t m_giFrame = 0, m_capBase = 0, m_capSeen = 0, m_capSlots = 0;
    int m_capWarmup = 0;   // frames left of the warm-up after a grid change (1024 probes per frame, no hysteresis)
    bool m_baked = false;  // the atlases hold baked probes (setBakedProbes)
    static constexpr uint32_t kCaptureMax = 256;   // per frame: each draw of a face maps the dynamic draw constants (256 B each)
    vp3d::Rng m_giRng{12345};
    RefCntAutoPtr<Diligent::ITexture> m_giIrr, m_giDist, m_giRays, m_giProbe;
    RefCntAutoPtr<Diligent::ITexture> m_capTex, m_capAlbedo, m_capNormal, m_capDepth;
    RefCntAutoPtr<Diligent::IBuffer> m_giCB, m_probeCB;   // DDGIConstants (G), ProbeFaceConstants (PF)
    static constexpr uint32_t kProbeFace = 16;   // texels per cube face of a probe capture (Renderer::kProbeFace)
};

} // namespace lit
