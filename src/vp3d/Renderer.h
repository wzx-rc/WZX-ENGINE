// vp3d - DirectX 12 + DXR (inline ray tracing) progressive path tracer.
//
// Usage:
//   Renderer r; r.init(hwnd, w, h, shaderDir);
//   r.setScene(scene);                      // when geometry changes
//   each frame: r.render(scene, settings, viewportRect, selected);
//               ... draw overlay (UI) on backBuffer(backBufferIndex()) ...
//               r.present();
#pragma once
#include "Scene.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <string>
#include <vector>

namespace vp3d {

using Microsoft::WRL::ComPtr;

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
    float bevelScale = 1.0f;
    int bevelSamples = 4;
    float fireflyClamp = 25.0f;
    float sssScale = 1.0f;
    float lightScale = 1.0f;
    float vignette = 0.12f;
    bool raster = false;
    int tonemapper = 0;           // 0 ACES (Hill fit), 1 AgX, 2 AgX Punchy, 3 Standard (clip)          // light raster viewport instead of path tracing
    // Temporal path tracing: one path per pixel per frame, blended into the history of the previous frames.
    // Off = progressive accumulation (reference, reset on every change).
    bool temporal = false;
    // Stable history (interactive, GI): the history is dropped when an object moves, edges are validated on the
    // pixel centre, the denoiser strength is bounded. Off = the previous behaviour (marks behind moved objects).
    bool historyFixes = true;
    // Drop the history when an object moves (and light changes). Off = keep it: less noise, but marks behind moved objects.
    bool resetOnMove = true;
    float resetDelay = 2.0f;   // seconds: the history fades to new samples over this time after a move
    // Real-time GI test: DDGI probe volume instead of path tracing (when raster is false).
    bool ddgi = false;
    int ddgiProbeBudget = 2048;   // target probe count over the scene bounds
    float ddgiHysteresis = 0.97f;
    int temporalCap = 1024;        // history length in frames (exponential average window): 256 converges, 64 stays noisier
    int ddgiView = 0;             // 0 full, 1 indirect only, 2 direct only
    bool ddgiShowProbes = false;  // draw the probes as small spheres shaded with their irradiance
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
    // Reads the current back buffer (call after the overlay finished, before present()).
    bool readBackBuffer(std::vector<uint8_t>& rgba, uint32_t& w, uint32_t& h);
    void present(bool vsync);
    void waitIdle();

    void resetAccumulation() { m_sampleIndex = 0; }
    uint32_t sampleCount() const { return m_sampleIndex; }
    float gpuFrameMs() const { return m_gpuMs; }
    float giUpdateMs() const { return m_giUpdateMs; }     // DDGI probe ray tracing
    float giBlendMs() const { return m_giBlendMs; }       // DDGI probe blending
    uint32_t giProbeCount() const { return m_giCounts[0] * m_giCounts[1] * m_giCounts[2]; }
    float giSpacing() const { return m_giSpacing; }
    bool converged(const RenderSettings& rs) const { return m_sampleIndex >= (uint32_t)rs.maxSamples; }

    // Reads back the last tonemapped viewport image (RGBA8).
    bool readViewport(std::vector<uint8_t>& rgba, uint32_t& w, uint32_t& h);

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
    bool createRasterPipelines(std::string* error);
    void createRasterTargets(uint32_t w, uint32_t h);
    void recordRaster(const Scene& scene, const RenderSettings& rs, const ViewRect& vp);
    void createTargets(uint32_t w, uint32_t h);
    void uploadTextures(const Scene& scene);
    void uploadEnvironment(const EnvImagePtr& img);   // HDRI texture, sampling tables, SH and sun (when the image changes)
    D3D12_GPU_DESCRIPTOR_HANDLE textureTable() const;
    void setupProbeGrid(const RenderSettings& rs);
    void recordDDGI(const RenderSettings& rs, UINT gx, UINT gy);
    void copyHistory();
    uint32_t sceneSignature(const Scene& scene) const;
    uint32_t transformSignature(const Scene& scene) const;
    uint32_t m_transformSignature = 0;
    int m_fadeFrames = 0, m_fadeTotal = 1;   // history fading after an object moved
    Buffer createBuffer(uint64_t size, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state);
    void ensureUpload(Buffer& b, uint64_t size);
    void buildTLAS(const Scene& scene, bool buildAS);
    void beginCommands();
    void executeAndWait();
    void transition(ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b);
    void uavBarrier(ID3D12Resource* r = nullptr);

    ComPtr<IDXGIFactory6> m_factory;
    ComPtr<ID3D12Device5> m_device;
    ComPtr<ID3D12CommandQueue> m_queue;
    ComPtr<ID3D12CommandAllocator> m_alloc;
    ComPtr<ID3D12GraphicsCommandList4> m_cmd;
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
    ComPtr<ID3D12PipelineState> m_psoGIUpdate, m_psoGIBlend, m_psoGIShade;
    ComPtr<ID3D12PipelineState> m_psoTraceTemporal;
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
    std::vector<ComPtr<ID3D12Resource>> m_materialTex;
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
    uint32_t m_giCounts[3] = {0, 0, 0};
    float3 m_giOrigin;
    float m_giSpacing = 1.0f;
    uint32_t m_giGridVersion = 0;
    int m_giBudget = 0;
    uint32_t m_giFrame = 0;
    float m_giUpdateMs = 0.0f, m_giBlendMs = 0.0f;
    bool m_giTimed = false;
    Rng m_giRng;

    // Temporal history: copies of ACCUM and NORMAL of the previous frame, and the camera they were rendered with.
    ComPtr<ID3D12Resource> m_hist[2];
    struct PrevCamera { float3 pos, right, up, fwd; float tanHalf = 1.0f, aspect = 1.0f; } m_prevCam;
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
    static constexpr uint32_t kShadowSize = 2048, kMsaa = 4;
    ComPtr<ID3D12RootSignature> m_rasterRoot;
    ComPtr<ID3D12PipelineState> m_psoShadow, m_psoBg, m_psoMain, m_psoTrans, m_psoGhost, m_psoId;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap, m_dsvHeap;
    uint32_t m_rtvSize = 0, m_dsvSize = 0;
    ComPtr<ID3D12Resource> m_msaaColor, m_msaaDepth, m_idDepth, m_shadowMap;
    Buffer m_rasterCB;
};

} // namespace vp3d


