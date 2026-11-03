// vp3d - DirectX 12 + DXR (inline ray tracing) progressive path tracer.
//
// Usage:
//   Renderer r; r.init(hwnd, w, h, shaderDir);
//   r.setScene(scene);                      // when geometry changes
//   each frame: r.render(scene, settings, viewportRect, selected);
//               ... draw overlay (UI) on backBuffer(backBufferIndex()) ...
//               r.present();
#pragma once
#include "LitPrep.h"
#include "Scene.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <functional>
#include <string>
#include <vector>
#include <utility>

namespace vp3d {

using Microsoft::WRL::ComPtr;

// Look of the editor grid (EditorGrid.hlsl), set by the Grid Options window of the editor.
struct EditorGridStyle {
    enum Flags : uint32_t { Lines = 1, Subdivisions = 2, Axes = 4, Arrows = 8, Fade = 16 };
    uint32_t flags = Lines | Subdivisions | Axes | Arrows | Fade;
    int subdivisions = 1;             // lines between two grid lines + 1 (1 = none)
    float extent = 0.0f;              // length and width of the grid (0 = infinite)
    float widthGrid = 1.0f, widthSub = 1.0f, widthAxis = 1.4f;   // screen pixels
    float opacity = 1.0f;
    float4 colorGrid{0.58f, 0.63f, 0.72f, 0.40f};   // rgb, a = opacity of the lines
    float4 colorSub{0.58f, 0.63f, 0.72f, 0.20f};
    float4 colorX{0.93f, 0.36f, 0.36f, 0.90f}, colorY{0.42f, 0.86f, 0.50f, 0.95f}, colorZ{0.36f, 0.56f, 0.98f, 0.90f};
};

struct RenderSettings {
    int maxBounces = 6;
    int samplesPerFrame = 1;
    int maxSamples = 4096;
    float exposure = 1.0f;
    bool denoise = true;
    float denoiseStrength = 1.0f;
    int denoisePasses = 3;          // a-trous passes (steps 1, 2, 4, ...): more = smoother, blurrier
    float denoiseEdgeLuma = 0.5f;   // luminance edge sensitivity: lower = the filter stops at shadow edges
    bool bevel = true;
    bool rasterBevel = false;       // the bevel in the raster view too (inline ray queries: builds the TLAS in raster)
    // Raster view closer to the path tracer (BrickIt HQ; builds the TLAS): every area light shadowed by rays, the
    // environment occluded by rays over rasterAO (scene units, 0 = off), reflections traced (reflSamples rays) without
    // probes. Off by default (Viewport3D unchanged).
    bool rasterRT = false;
    float rasterAO = 0.0f;
    float bevelScale = 1.0f;
    int bevelSamples = 4;
    float fireflyClamp = 25.0f;
    float filterGlossy = 0.0f;     // minimum roughness of glossy surfaces seen after a rough bounce (0 = exact; >0 removes caustic fireflies)
    float indirectClamp = 0.0f;    // caps each light contribution after a rough bounce (0 = off; temporal: fewer sparkles)
    float sssScale = 1.0f;
    float lightScale = 1.0f;
    float vignette = 0.12f;
    bool raster = false;
    int msaaSamples = 4;            // viewport anti-aliasing of the raster passes: MSAA sample count (1 off, 4, 8, 16)
    // Editor overlays of the raster viewport: ground grid (smallest cell in scene units, 0 = off, see EditorGrid.hlsl),
    // length of its Y axis, and the selection glow (style 1 = blue glow, 0 = yellow outline; age in seconds animates it).
    float editorGrid = 0.0f;
    float editorGridAxis = 10.0f;
    EditorGridStyle grid;
    int selectionStyle = 1;
    float selectionAge = 10.0f;
    float2 selectionCenter{0, 0};   // screen centre (viewport pixels) and radius of the selection: the glow light runs around it
    float selectionRadius = 100.0f;
    float pixelScale = 1.0f;        // render pixels per screen pixel (viewport supersampling): lines and the glow keep their size
    // raster: screen-space ambient occlusion (radius in scene units), multiplied into the image before the tone mapping
    bool ssao = false;
    float ssaoRadius = 0.6f;
    int tonemapper = 0;           // 0 ACES (Hill fit), 1 AgX, 2 AgX Punchy, 3 Standard (clip)          // light raster viewport instead of path tracing
    // Stable history (interactive, GI): the history is dropped when an object moves, edges are validated on the
    // pixel centre, the denoiser strength is bounded. Off = the previous behaviour (marks behind moved objects).
    bool historyFixes = true;
    bool rasterEnvSun = true;      // raster: the sun of the HDRI is a shadowed directional light (else all the image is ambient)
    // Drop the history when an object moves (and light changes). Off = keep it: less noise, but marks behind moved objects.
    bool resetOnMove = true;
    float resetDelay = 2.0f;   // seconds: the history fades to new samples over this time after a move
    // Real-time GI test: DDGI probe volume instead of path tracing (when raster is false).
    bool ddgi = false;
    int ddgiProbeBudget = 2048;   // target probe count over the scene bounds
    float ddgiHysteresis = 0.97f;
    // progressive probes: the hysteresis grows from 0.85 to ddgiHysteresis while nothing changes (a running average:
    // converged and still), back to 0.85 when the lights, the environment or an object change; no fast path per texel
    bool ddgiProgressive = false;
    float ddgiViewBias = -1.0f;   // >= 0: the view bias of the probe lookup (spacings) instead of the default 0.24
    float ddgiEnergy = 1.0f;   // scale of the probe light where it is read (not in the bounces): no probe restart
    int temporalCap = 1024;        // history length in frames (exponential average window): 256 converges, 64 stays noisier
    int ddgiView = 0;             // 0 full, 1 indirect only, 2 direct only
    bool ddgiShowProbes = false;  // draw the probes as small spheres shaded with their irradiance
    // DDGI rays from a raster capture of each probe (cube faces lit by the raster pass) instead of ray tracing.
    bool ddgiRaster = false;
    int ddgiCaptureProbes = 32;   // probes captured per frame (the whole grid is refreshed in total / this frames)
    // Diagnostic: the viewport is lit by the raster pass reading the probes (as ddgiRaster) while the probes themselves
    // keep their ray traced rays. Separates the capture error from the visibility error.
    bool ddgiRasterView = false;
    // Lit mode: the main pass reads baked probes (bakeSignature / startBake / setBakedProbes) instead of its ambient.
    bool bakedProbes = false;
    // Raster modes (Lit, Metal iOS): 0 shaded, 1 wireframe only, 2 wireframe over the shaded view (override).
    int wireframe = 0;
    // Raster: object id pass (normal + id per pixel, used by the editor's id outline). Off: the target is only cleared
    // (the selection coverage still gives the outline), one geometry pass less.
    bool idPass = true;
    // Path tracing: 1 = every pixel each frame; 4 = one pixel of each 2 x 2 block per frame, in turn (a quarter of the
    // rays per frame, same convergence over time; the first frame after a reset traces every pixel)
    int traceStride = 1;
    // How the probes of the GI modes are computed: 0 DDGI (random rays, hysteresis), 1 Radiance Cascades (the
    // cascades merged every frame, no temporal accumulation). Radiance Cascades need ray tracing (not ddgiRaster).
    int giMethod = 0;
    int rcProbeBudget = 8192;     // probes of cascade 0 (the DDGI grid of this mode)
    int rcCascades = 4;           // cascade count (the top one traces to infinity)
    int rcDirRes = 8;             // directions per side of cascade 0 (8 = 64 directions, x4 per cascade), 2..8
    float rcInterval = 3.0f;      // ray length of cascade 0, in probe spacings (x4 per cascade): covers the
                                  // probe visibility range (DDGI maxDistance 2.6 spacings), so the distances are exact
    bool rcBounces = true;        // further bounces: the hits read the previous frame's probes (else one bounce)
    int rcLightSamples = 2;       // light samples per hit (fixed per texel: stable, no flicker)
    float rcHysteresis = 0.0f;    // 0 = no temporal accumulation; > 0 blends the frames (the light samples then move)
    bool rcBilinearFix = false;   // merge with one traced segment per parent probe (8x the merge rays; not better here)
    // Screen-space cascades (needs giMethod = 1 and the raster view): probes on the visible surfaces, one per
    // rcScreenSpacing x rcScreenSpacing pixels in cascade 0; the world cascades stay the cache of the further bounces.
    bool rcScreen = false;
    int rcScreenSpacing = 8;      // pixels between the cascade-0 probes (x2 per cascade)
    int rcScreenDirRes = 8;       // directions per side of cascade 0 (x2 per cascade)
    float rcScreenInterval = 0.25f;   // ray length of cascade 0, in world probe spacings (x4 per cascade)
    // Split Radiance Cascades (needs giMethod = 1 and the raster view; replaces rcScreen): sparse world probes around
    // the visible surfaces (hash tables, LODs from the camera distance), one ray per pixel split into the cascades by
    // its hit distance, nothing accumulated over frames. The world cascades stay the cache of the further bounces.
    bool rcSplit = false;
    int rcSplitProbePx = 12;      // size of a cascade-0 probe cell on screen, in pixels (LODs keep it so with distance)
    float rcSplitInterval = 2.0f; // end of the cascade-0 interval, in cascade-0 spacings (x4 per cascade)
    int rcSplitCascades = 4;      // 1..6, the top one to infinity
    bool rcSplitAnimate = false;  // jitter the ray set every frame (moving noise; off = a still image stays still)
    // Accumulation while nothing moves: up to rcSplitHistory frames per cascade-0 probe (world space, so the camera can
    // move), dropped around an object that moves and everywhere when a light or a material changes. 0 or 1 = off.
    int rcSplitHistory = 16;
    bool rcSplitFilter = true;    // each cascade-0 probe averaged with its 6 neighbour cells (less noise, a cell of blur)
    int rcSplitDebug = 0;         // inspection view: 0 off, SRC_DEBUG_* (cells, probes, hit cascade, rays, LOD, history)
    int rcSplitDebugCascade = 0;  // cascade shown by the cells view
    // Raster modes: the sun shadow traced per pixel against the scene (needs the acceleration structure of the frame,
    // i.e. a GI mode) instead of the shadow map. sunAngle: angular diameter of the sun (degrees), its penumbra.
    bool rtSunShadows = true;
    float sunAngle = 0.53f;
    int rtShadowSamples = 8;
    // Raster modes with GI: reflections traced per pixel on metals and glossy surfaces (roughness under reflMaxRough):
    // one mirror ray, or reflSamples GGX rays filtered spatially, instead of the environment reflection.
    bool rtReflections = true;
    int reflSamples = 4;
    float reflMaxRough = 0.5f;
    // GI raster modes (Metal iOS, Split RC...; Lit keeps 4x): 0 MSAA 4x, 1 MSAA 8x, 2 AAA = MSAA 8x resolved in
    // tonemapped space (bright / dark edges stay smooth instead of stair-stepping on the bright side)
    int aaQuality = 0;
    // Ray traced shadows (sun and lamp) of the raster GI modes: 0 low ... 2 medium (8 sun / 16 lamp rays, the former
    // default) ... 4 AAA (32 / 64 rays, wider filter)
    int shadowQuality = 2;
    // Split RC: GI bounds (an app with a huge ground: BrickIt), probes only in the box grown by giBoundsFade, the GI
    // fading out across it. giBoundsFade 0 = off (the whole visible scene).
    float3 giBoundsMin{0, 0, 0}, giBoundsMax{0, 0, 0};
    float giBoundsFade = 0.0f;
};

// What the Radiance Cascades of the last frame looked like (viewer statistics).
struct RCStats {
    int cascades = 0;
    uint32_t probes[RC_MAX_CASCADES] = {}, dirRes[RC_MAX_CASCADES] = {};
    float tStart[RC_MAX_CASCADES] = {}, tEnd[RC_MAX_CASCADES] = {};
    uint64_t rays = 0;            // texels traced per frame (all cascades)
    int screenCascades = 0;       // screen-space cascades (rcScreen)
    uint64_t screenRays = 0;
    int splitCascades = 0;        // Split RC: cascades, probe capacity and directions per probe of each, rays (pixels)
    uint32_t splitCapacity[RC_MAX_CASCADES] = {}, splitDirs[RC_MAX_CASCADES] = {};
    uint64_t splitRays = 0;
    float splitLodDist = 0, splitSpacing = 0;   // where LOD 1 starts, cascade-0 spacing at LOD 0
    uint32_t splitProbes[RC_MAX_CASCADES] = {};   // probes allocated per cascade (read back from the previous frame)
};

struct ViewRect { int x = 0, y = 0, w = 1, h = 1; };

class Renderer {
public:
    static constexpr uint32_t kBackBuffers = 2;

    bool init(HWND hwnd, uint32_t width, uint32_t height, const std::wstring& shaderDir, std::string* error = nullptr);
    void shutdown();
    void resize(uint32_t width, uint32_t height);   // swapchain resize; release external refs first

    void setScene(const Scene& scene);              // upload geometry and build BLAS
    // Path trace one frame into the viewport rectangle of the current back buffer.
    // Leaves the back buffer in RENDER_TARGET state (an overlay is expected to transition it to PRESENT).
    // With toBackBuffer = false the image is only rendered offscreen (e.g. thumbnails) and the call waits.
    void setExtraSelection(const std::vector<int>& objects) { m_extraSelection = objects; }   // multi-selection outline
    void render(const Scene& scene, const RenderSettings& rs, const ViewRect& vp, int selectedObject,
                bool toBackBuffer = true);
    // Same, but the image stays in outputTexture(), left in PIXEL_SHADER_RESOURCE state so an editor UI can draw it.
    // The caller must transition it back to UNORDERED_ACCESS before the next render (see outputTexture()).
    void renderToTexture(const Scene& scene, const RenderSettings& rs, const ViewRect& vp, int selectedObject);
    ID3D12Resource* outputTexture() const { return m_tex[kTexCount - 1].Get(); }   // RGBA8, size = last viewport
    // Viewport anti-aliasing (MSAA): the sample count in use, and whether the GPU supports n samples (1, 2, 4, 8, 16).
    // RenderSettings::msaaSamples picks it (the largest supported count up to the request).
    uint32_t msaaSamples() const { return m_msaa * (m_msaaTemporal ? 2u : 1u); }   // effective samples per pixel
    bool msaaTemporal() const { return m_msaaTemporal; }                               // x16 as 8 samples x 2 frames
    bool msaaSupported(uint32_t n) const { return n <= 1 || (n <= 16 && (m_msaaMask & (1u << n)) != 0); }
    void setMsaa(uint32_t n);
    // Extra views (the other panes of a split layout, the preview of a camera): the raster view (Lit, with the editor
    // grid, wireframe and selection glow) of another camera, each into its own image. Between beginViews and endViews,
    // after renderToTexture of the frame (same scene and settings); the images are then readable by the UI.
    void beginViews();
    void renderView(int slot, const Scene& scene, const Camera& cam, const RenderSettings& rs, uint32_t w, uint32_t h,
                    float2 selectionCenter, float selectionRadius);
    void endViews();
    ID3D12Resource* viewOutput(int slot) const;   // RGBA8 image of the extra view (null before its first render)
    bool readView(int slot, std::vector<uint8_t>& rgba, uint32_t& w, uint32_t& h);   // self-test
    static constexpr int kMaxViews = 20;
    // Reads the current back buffer (call after the overlay finished, before present()).
    bool readBackBuffer(std::vector<uint8_t>& rgba, uint32_t& w, uint32_t& h);
    void present(bool vsync);
    void waitIdle();

    void resetAccumulation() { m_sampleIndex = 0; }
    uint32_t sampleCount() const { return m_sampleIndex; }
    float gpuFrameMs() const { return m_gpuMs; }
    uint64_t rasterTriangles() const { return m_rasterTris; }   // triangles of the camera passes of the last raster frame (after LOD and culling)
    float giUpdateMs() const { return m_giUpdateMs; }     // DDGI probe ray tracing
    float giBlendMs() const { return m_giBlendMs; }       // DDGI probe blending
    float splitMs() const { return m_splitMs; }           // Split Radiance Cascades (all passes, gather included)
    uint32_t giProbeCount() const { return m_giCounts[0] * m_giCounts[1] * m_giCounts[2]; }
    float giSpacing() const { return m_giSpacing; }
    const RCStats& rcStats() const { return m_rcStats; }   // Radiance Cascades of the last GI frame
    bool converged(const RenderSettings& rs) const { return m_sampleIndex >= (uint32_t)rs.maxSamples; }

    // Reads back the last tonemapped viewport image (RGBA8).
    bool readViewport(std::vector<uint8_t>& rgba, uint32_t& w, uint32_t& h);
    // Debug: reads back the probe cube captures of the last frame (raster DDGI), radiance rgb + distance, as floats.
    bool readCapture(std::vector<float>& rgba, uint32_t& w, uint32_t& h);
    // Self-test: the coverage of the selection of the last raster frame (R8, 255 = every sample of the pixel selected).
    bool readSelectionCoverage(std::vector<uint8_t>& r8, uint32_t& w, uint32_t& h);
    // Validation layer (VP3D_DEBUG set and the Windows "Graphics Tools" installed): false when it is not running.
    // Otherwise the messages stored since the last call, errors and corruptions first counted in `errors`.
    bool takeDebugMessages(std::vector<std::string>& messages, int& errors, int& warnings);

    // Baked probes (Lit mode). A bake runs the ray traced probe update for `frames` frames on the current scene, the
    // raster pass reading the probes meanwhile; readBakedProbes then returns the atlases to save them with the scene.
    // setBakedProbes loads a saved bake. Any live probe update (Metal iOS mode) or grid change invalidates the bake.
    void startBake(int frames = 120);
    bool baking() const { return m_bakeFrames > 0; }
    int bakeFramesLeft() const { return m_bakeFrames; }
    bool hasBakedProbes() const { return m_bakeValid; }
    uint32_t bakeSignature(const Scene& scene) const;   // what the bake depends on: geometry, materials, lights, transforms
    bool readBakedProbes(lit::BakedProbes& out);
    void setBakedProbes(const lit::BakedProbes& probes);

    ID3D12Device5* device() const { return m_device.Get(); }
    ID3D12CommandQueue* queue() const { return m_queue.Get(); }
    IDXGISwapChain3* swapChain() const { return m_swapChain.Get(); }
    uint32_t backBufferIndex() const { return m_swapChain->GetCurrentBackBufferIndex(); }
    ID3D12Resource* backBuffer(uint32_t i) const { return m_backBuffers[i].Get(); }
    uint32_t width() const { return m_width; }
    uint32_t height() const { return m_height; }
    const std::string& adapterName() const { return m_adapterName; }

private:
    struct Buffer {
        ComPtr<ID3D12Resource> res;
        void* mapped = nullptr;
        uint64_t size = 0;
        D3D12_GPU_VIRTUAL_ADDRESS va() const { return res ? res->GetGPUVirtualAddress() : 0; }
    };
    struct MeshGpu {
        uint32_t indexOffset, vertexOffset, indexCount, vertexCount;
        uint32_t lineOffset = 0, lineCount = 0;   // wireframe line list, stored in m_ib after the triangles
        Buffer blas;
        float3 bmin, bmax;
        float3 center; float radius = 0;   // bounding sphere (emissive objects: light sampling cone)
    };
    struct Draw {
        int mesh;
        Affine xf;
        uint32_t material;
        uint32_t id;
        uint32_t flags;
        float alpha;
    };
    std::vector<Draw> m_draws;
    float3 m_sceneCenter;
    float m_sceneRadius = 10;

    bool createPipelines(std::string* error);
    // msaaOnly: rebuild only the multisampled pipelines (sample count change), keeping the root signature and heaps
    bool createRasterPipelines(std::string* error, bool msaaOnly = false);
    bool createMsaaPipelines(std::string* error);   // the multisampled viewport passes (m_msaa samples)
    void resolveViewport(ID3D12Resource* src, ID3D12Resource* dst);   // the multisampled viewport colour into a texture
    void recordSelectionMask(const std::function<void(const Draw&)>& drawOne);   // coverage of the selection (t8)
    void createRasterTargets(uint32_t w, uint32_t h);
    // probeCapture: shadow map, then the cube captures of the next probes (m_capSlots) instead of the viewport.
    // probes: DDGI without ray tracing, the main pass reads the probe atlases (SRVs t4..t6) instead of the ambient.
    // shadow: draw the shadow map first (false when the capture pass of the same frame already drew it).
    void recordRaster(const Scene& scene, const RenderSettings& rs, const ViewRect& vp, bool probeCapture = false,
                      bool probes = false, bool shadow = true, int overlaySlot = -1);   // overlaySlot: grid + wireframe only, into that texture slot (path traced view)
    void ensureCapture(uint32_t slots);
    void createTargets(uint32_t w, uint32_t h);
    void uploadTextures(const Scene& scene);
    void uploadEnvironment(const EnvImagePtr& img);   // HDRI texture, sampling tables, SH and sun (when the image changes)
    D3D12_GPU_DESCRIPTOR_HANDLE textureTable() const;
    void setupProbeGrid(const RenderSettings& rs);
    void createProbeTextures(const lit::ProbeGrid& grid);
    uint64_t m_giBoundsKey = 0;   // GI bounds the probe grid was placed on (0 = the scene bounds)
    void recordDDGI(const RenderSettings& rs, UINT gx, UINT gy, bool shade = true);   // shade: GIShade (ray traced visibility)
    void recordRC(const RenderSettings& rs);   // Radiance Cascades: trace + merge every cascade, then fill the probe atlases
    void recordRCScreen(const RenderSettings& rs, UINT gx, UINT gy);   // screen-space cascades + per-pixel gather
    void recordSplitRC(const RenderSettings& rs, UINT gx, UINT gy);    // Split Radiance Cascades + per-pixel gather
    void copyHistory();
    uint32_t sceneSignature(const Scene& scene) const;
    uint32_t transformSignature(const Scene& scene) const;
    // Signatures cached while Scene::objectsVersion is unchanged (nonzero).
    uint32_t sceneSig(const Scene& scene);
    uint32_t transformSig(const Scene& scene);
    uint32_t m_sigVersion = 0, m_cachedSceneSig = 0, m_cachedTransformSig = 0;
    uint32_t m_buildVersion = 0, m_tlasVersion = 0;   // objectsVersion of the per-object data / of the TLAS
    uint32_t m_transformSignature = 0;
    int m_fadeFrames = 0, m_fadeTotal = 1;   // history fading after an object moved
    Buffer createBuffer(uint64_t size, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state);
    void ensureUpload(Buffer& b, uint64_t size);
    void buildTLAS(const Scene& scene, bool buildAS);
    void buildInstances(const Scene& scene);   // draws, instances, materials, lights, scene bounds
    void buildAccel();                         // TLAS over the instances of the last buildInstances
    uint32_t m_tlasCount = 0;
    void beginCommands();
    void executeAndWait();
    void transition(ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b);
    void uavBarrier(ID3D12Resource* r = nullptr);

    ComPtr<IDXGIFactory6> m_factory;
    ComPtr<ID3D12Device5> m_device;
    ComPtr<ID3D12InfoQueue> m_infoQueue;   // validation layer messages (VP3D_DEBUG), see takeDebugMessages
    ComPtr<ID3D12CommandQueue> m_queue;
    ComPtr<ID3D12CommandAllocator> m_alloc;
    ComPtr<ID3D12GraphicsCommandList4> m_cmd;
    // Extra views (renderView): their own targets, images and constants, the pipelines of the main view. They are
    // recorded into a second command list, sent after the main one (m_cmd points to it between beginViews and endViews).
    struct ExtraView {
        int index = 0;
        uint32_t w = 0, h = 0, samples = 0;
        ComPtr<ID3D12Resource> color, depth, selMask, selCov, hdr, id, idDepth, out;
        Buffer cb;                 // raster constants (0, 512 without jitter), frame constants of the tonemapper (1024)
        bool outReadable = false;  // out is in the pixel shader resource state (drawn by the UI)
    };
    std::vector<ExtraView> m_extra;
    ComPtr<ID3D12CommandAllocator> m_viewAlloc;
    ComPtr<ID3D12GraphicsCommandList4> m_viewCmd;
    ComPtr<ID3D12DescriptorHeap> m_viewRtvHeap, m_viewDsvHeap;   // per view: 3 RTVs (colour, ids, selection), 2 DSVs
    bool m_viewsOpen = false;
    bool m_viewsShadow = false;   // the shadow map still to draw for the extra views of this frame (once: it does not depend on the camera)
    const ExtraView* m_rtView = nullptr;   // recordRaster draws into this extra view (null: the main viewport)
    const Camera* m_rtCam = nullptr;       // ... with this camera
    alignas(16) unsigned char m_lastFc[1024] = {};   // the main view's gpu::FrameConstants of this frame (exposure, selection...)
    bool m_lastProbes = false;             // its raster pass read the probes (Metal iOS, baked)
    void createViewTargets(ExtraView& v, uint32_t w, uint32_t h);
    ComPtr<ID3D12Fence> m_fence;
    uint64_t m_fenceValue = 0;
    HANDLE m_fenceEvent = nullptr;
    ComPtr<IDXGISwapChain3> m_swapChain;
    ComPtr<ID3D12Resource> m_backBuffers[kBackBuffers];
    uint32_t m_width = 0, m_height = 0;
    std::string m_adapterName;
    std::wstring m_shaderDir;

    ComPtr<ID3D12RootSignature> m_rootSig;
    ComPtr<ID3D12PipelineState> m_psoTrace, m_psoDenoise, m_psoTonemap;
    ComPtr<ID3D12PipelineState> m_psoGIUpdate, m_psoGIBlend, m_psoGIShade, m_psoGIRaster;
    ComPtr<ID3D12PipelineState> m_psoRCVisibility, m_psoRCTrace, m_psoRCGather;
    ComPtr<ID3D12PipelineState> m_psoRCSProbes, m_psoRCSTrace, m_psoRCSProbeSH, m_psoRCSGather;
    ComPtr<ID3D12PipelineState> m_psoSRCClear, m_psoSRCInsert, m_psoSRCAlloc, m_psoSRCCount, m_psoSRCTrace, m_psoSRCMerge,
        m_psoSRCIrradiance, m_psoSRCFilter, m_psoSRCGather;
    ComPtr<ID3D12PipelineState> m_psoSunTrace, m_psoSunFilter, m_psoReflTrace, m_psoReflFilter;
    ComPtr<ID3D12DescriptorHeap> m_heap;
    uint32_t m_descSize = 0;

    // Render targets (UAV textures), see shaders for the slot meaning.
    static constexpr int kTexCount = 6;
    // Material textures: SRVs after the render targets and the shadow map in m_heap (unused slots = null views).
    static constexpr uint32_t kMaxTextures = 16;
    // DDGI and history UAVs sit after the texture table (heap offset kTexCount + 1 + kMaxTextures)
    static constexpr uint32_t kGIHeapBase = kTexCount + 1 + kMaxTextures;
    // HDRI SRV after the DDGI and history UAVs
    static constexpr uint32_t kEnvHeapSlot = kGIHeapBase + 4 + 2;
    // UAVs of the probe cube captures (raster DDGI: radiance, albedo, normal), after the HDRI SRV
    static constexpr uint32_t kCapHeapSlot = kEnvHeapSlot + 1;
    // SRVs of the probe atlases (irradiance, distance, state) read by the raster pass, after the capture UAVs
    static constexpr uint32_t kProbeSrvSlot = kCapHeapSlot + 3;
    // Screen GI texture (screen-space cascades): its SRV right after the probe SRVs (raster t11), then its UAV (u17)
    // and the ray traced sun shadow: its mask SRV (raster t12), then its raw and filtered UAVs (u18, u19)
    static constexpr uint32_t kScreenGISrvSlot = kProbeSrvSlot + 3, kSunShadowSrvSlot = kProbeSrvSlot + 4;
    static constexpr uint32_t kReflSrvSlot = kProbeSrvSlot + 5;   // raster t13: ray traced reflections (filtered)
    // UAVs: screen GI (u17), then u18..u21: sun shadow raw, filtered, reflections raw, filtered
    static constexpr uint32_t kCenterSrvSlot = kProbeSrvSlot + 6;   // raster t14: the raw sun trace (pixel-centre depth)
    static constexpr uint32_t kScreenGIUavSlot = kProbeSrvSlot + 7, kSunShadowUavSlot = kProbeSrvSlot + 8;
    static constexpr uint32_t kMsaaSrvSlot = kSunShadowUavSlot + 4;   // the MSAA colour, read by the tonemapped resolve
    // coverage of the selection (resolved MSAA mask of the selected objects) read by the tonemapper (t8)
    static constexpr uint32_t kSelCovSlot = kMsaaSrvSlot + 1;
    // extra views (renderView): per view, the u0..u5 table of the tonemapper then its selection coverage (t8)
    // depth of the viewport read by the ambient occlusion (t9, right after t8)
    static constexpr uint32_t kDepthSrvSlot = kSelCovSlot + 1;
    // scene of the raster bevel (t0..t3 space2 of the raster pass: TLAS, vertices, indices, instances)
    static constexpr uint32_t kBevelSrvSlot = kDepthSrvSlot + 1;
    static constexpr uint32_t kViewHeapBase = kBevelSrvSlot + 4, kViewHeapStride = kTexCount + 1;
    static constexpr uint32_t kProbeFace = 16;   // texels per cube face of a probe capture
    std::vector<ComPtr<ID3D12Resource>> m_materialTex;
    ComPtr<ID3D12PipelineState> m_psoSSAO, m_psoSSAOMS, m_psoSSAOBlur;   // ambient occlusion of the raster viewport (optional)
    void recordSSAO(const RenderSettings& rs, const ViewRect& vp, const Scene& scene);
    ComPtr<ID3D12Resource> m_tex[kTexCount];
    uint32_t m_rtW = 0, m_rtH = 0;

    // Scene
    Buffer m_vb, m_ib;
    std::vector<MeshGpu> m_meshes;
    int m_quadMesh = -1;
    Buffer m_instances, m_materials, m_lights, m_instanceDescs, m_constants;
    Buffer m_tlas, m_tlasScratch;
    uint32_t m_tlasCapacity = 0;
    uint32_t m_lightCount = 0;
    uint32_t m_sunLight = 0xFFFFFFFFu;   // the sun's area light (path tracing; the raster pass skips it)
    std::vector<int> m_objectToInstance;
    std::vector<int> m_extraSelection;
    uint32_t m_sceneGeometryVersion = 0;

    bool m_toTexture = false;
    uint32_t m_frameIndex = 0;
    uint32_t m_sampleIndex = 0;
    uint32_t m_denoisedSlot = 0xFFFFFFFF;

    ComPtr<ID3D12QueryHeap> m_queryHeap;
    Buffer m_queryReadback;
    double m_tsFreq = 1.0;
    float m_gpuMs = 0.0f;
    Buffer m_readback;

    // DDGI probe volume: irradiance atlas, distance atlas, ray buffer, probe state (UAVs u6..u9)
    static constexpr int kGITexCount = 4;
    ComPtr<ID3D12Resource> m_giTex[kGITexCount];
    Buffer m_giConstants;
    // Raster probe capture: cube faces of m_capSlots probes (kProbeFace texels each), depth, first probe of the batch
    ComPtr<ID3D12Resource> m_capTex, m_capAlbedo, m_capNormal, m_capDepth;
    uint32_t m_capSlots = 0, m_capBase = 0, m_capSeen = 0;
    // Warm-up after a grid change: frames left during which 1024 probes are captured per frame without hysteresis, so
    // the indirect light settles in a fraction of a second instead of several cycles of 32 probes per frame.
    int m_capWarmup = 0;
    int m_bakeFrames = 0;        // frames left of a probe bake (ray traced probes in Lit mode)
    bool m_bakeValid = false;    // the atlases hold a bake (finished or loaded) the Lit main pass can read
    static constexpr uint32_t kCaptureMax = 1024;   // probes per capture (16 texel rows each, textures stop at 16384)
    ComPtr<ID3D12PipelineState> m_psoProbe, m_psoProbeBg;
    uint32_t m_giCounts[3] = {0, 0, 0};
    float3 m_giOrigin;
    float m_giSpacing = 1.0f;
    uint32_t m_giGridVersion = 0;
    int m_giBudget = 0;
    uint32_t m_giFrame = 0;
    uint32_t m_giStill = 0, m_giStillKey = 0;   // frames since the lighting / the objects last changed (ddgiProgressive)
    float m_giStillUpdates = 0;                 // updates of each probe since then (a quarter per frame once still)
    // progressive probes: the draws of the last frame (key: mesh, transform, material) with their world box, to find
    // the box of what was added, removed or moved (the probes there restart, the others stay converged)
    std::vector<std::pair<uint64_t, std::pair<float3, float3>>> m_giDrawBoxes;
    uint32_t m_giDrawVersion = 0;
    float m_giUpdateMs = 0.0f, m_giBlendMs = 0.0f;
    bool m_giTimed = false;
    Rng m_giRng;
    // Radiance Cascades: all cascades in one buffer (u15), their constants (b3)
    Buffer m_rcBuffer, m_rcConstants;
    Buffer m_rcsBuffer, m_rcsConstants;          // screen-space cascades (u16, b4)
    Buffer m_srcBuffer, m_srcConstants;          // Split Radiance Cascades (u16 as a raw buffer, b4)
    float m_splitMs = 0.0f;
    bool m_splitTimed = false;                   // the frame wrote the Split RC timestamps (4, 5)
    // Split RC history: the previous frame's cascade-0 slots, irradiance and history lengths (u22), its layout and camera,
    // the signature that drops it (lighting, materials, settings), the transforms of the draws (moves drop it locally)
    Buffer m_srcPrev, m_srcReadback;            // history; the counters read back (probes per cascade)
    uint32_t m_srcKeys[RC_MAX_CASCADES] = {};   // keys of the last frame read back (recordRC resets m_rcStats)
    uint32_t m_srcCapProbes[RC_MAX_CASCADES] = {};   // probe capacity per cascade, following the keys of the last frames
    bool m_srcProfile = GetEnvironmentVariableA("VP3D_SRC_PROFILE", nullptr, 0) > 0;   // a timestamp after each pass
    uint32_t m_srcProfileN = 0;
public:
    std::string srcProfile;   // last frame's per-pass times (VP3D_SRC_PROFILE)
private:
    uint32_t m_srcPrevCap = 0, m_srcPrevProbes = 0, m_srcPrevSig = 0;
    bool m_srcPrevValid = false;
    float3 m_srcPrevCam;
    std::vector<Affine> m_srcPrevXf;
    ComPtr<ID3D12Resource> m_screenGI;           // per pixel indirect irradiance (RCSGather.hlsl)
    ComPtr<ID3D12Resource> m_sunShadowRaw, m_sunShadow;   // ray traced sun visibility: raw (+ depth, normal), filtered
    ComPtr<ID3D12Resource> m_reflRaw, m_refl;             // ray traced reflections: raw (rgb, roughness), filtered
    // SunShadowTrace (+ ReflectTrace) then the filters; shadow / reflections: what the raster pass reads this frame
    // areaLight >= 0: the shadow mask is the one of that area light (its GPU light index) instead of the sun
    void recordScreenTrace(const float3& sunDir, const RenderSettings& rs, bool shadow, bool refl, int areaLight = -1);
    bool m_screenGIOn = false;                   // the last frame filled it (raster reads it)
    bool m_asThisFrame = false;                  // the TLAS was built this frame (ray traced shadows of the raster pass)
    RCStats m_rcStats;

    // Temporal history: copies of ACCUM and NORMAL of the previous frame, and the camera they were rendered with.
    ComPtr<ID3D12Resource> m_hist[2];
    struct PrevCamera { float3 pos, right, up, fwd; float tanHalf = 1.0f, aspect = 1.0f, orthoHalfH = 0.0f, orthoBack = 0.0f; } m_prevCam;
    // orthographic camera: how far behind its eye the rays start so that the whole scene lies in front of them
    float orthoBack(const Camera& cam) const;
    bool m_historyValid = false;
    uint32_t m_sceneSignature = 0;

    // HDRI environment (Scene::env.hdri), uploaded when the image changes
    EnvImagePtr m_envImage;
    ComPtr<ID3D12Resource> m_envTex;
    Buffer m_envDist;                 // importance sampling tables (see EnvSample in PathTrace.hlsl)
    uint32_t m_envW = 0, m_envH = 0;  // size of the tables
    float m_envPdfScale = 0, m_envMips = 1;
    float m_envSH[9][3] = {};         // irradiance SH without the sun (raster)
    float m_envSHFull[9][3] = {};     // irradiance SH with the sun
    float3 m_envSunDir{0, 1, 0}, m_envSunE{0, 0, 0};   // sun of the HDRI (image space), its irradiance
    bool m_envSunValid = false;

    // Raster viewport
    static constexpr uint32_t kShadowSize = 2048;
    uint32_t m_msaa = 4;              // samples of the viewport anti-aliasing (setMsaa)
    uint32_t m_msaaMask = 2;          // bit n set: n samples supported by the GPU
    // MSAA beyond the GPU (x16 with 8 samples): m_msaa samples over two frames with alternating sub-pixel offsets,
    // averaged by the tonemapper while the view and the objects do not move (history in the TEX_DENOISE_B slot)
    bool m_msaaTemporal = false;
    uint32_t m_taaFrame = 0;
    uint64_t m_taaSig = 0;
    bool m_taaValid = false;
    // shaders of the multisampled passes, kept to rebuild their pipelines when the sample count changes
    struct RasterShaders { std::vector<char> vsMain, vsWire, vsBg, psMain, psGhost, psWire, psBg, psSelect, vsGrid, psGrid; } m_rasterShaders;
    // Selection outline: the selected objects drawn into a mask with the samples of the viewport (m_selMask), resolved
    // into their coverage per pixel (m_selCov, SRV kSelCovSlot): the tonemapper draws a glow as smooth as the edges.
    ComPtr<ID3D12Resource> m_selMask, m_selCov;
    ComPtr<ID3D12PipelineState> m_psoSelect;
    std::vector<uint32_t> m_selIds;   // instance ids + 1 of the selection (as FrameConstants)
    bool m_msaa8 = false;           // the GPU supports 8x on the HDR colour format (Split RC / GI modes, aaQuality)
    ComPtr<ID3D12PipelineState> m_psoResolve;   // tonemapped MSAA resolve (ResolveTM.hlsl)
    ComPtr<ID3D12RootSignature> m_rasterRoot;
    ComPtr<ID3D12PipelineState> m_psoShadow, m_psoBg, m_psoMain, m_psoTrans, m_psoGhost, m_psoId, m_psoWire, m_psoGrid, m_psoDepth;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap, m_dsvHeap;
    uint32_t m_rtvSize = 0, m_dsvSize = 0;
    ComPtr<ID3D12Resource> m_msaaColor, m_msaaDepth, m_idDepth, m_shadowMap;
    Buffer m_rasterCB;
    // Instanced raster draws: the per-object constants (64 bytes each) of every raster pass of the frame, appended to an
    // upload buffer read by the vertex and pixel shaders (t7). Reset by present() (the GPU is idle then); a buffer that
    // gets too small mid-frame is kept alive in m_drawGrave until then.
    Buffer m_drawBuf;
    uint64_t m_drawBufUsed = 0;
    std::vector<Buffer> m_drawGrave;
    uint32_t allocDraws(uint32_t count);
    uint64_t m_rasterTris = 0;   // first slot of count consecutive DrawConstants in m_drawBuf
};

} // namespace vp3d


