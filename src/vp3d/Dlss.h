// vp3d - NVIDIA DLSS Ray Reconstruction (NGX, DLSS SDK) for the moving view of the progressive path tracer.
//
// Built with the SDK when build.bat finds it in third_party\dlss (WZX_DLSS defined, nvsdk_ngx_s.lib linked,
// nvngx_dlssd.dll copied next to the editor); otherwise this is an empty shell that reports DLSS unavailable.
// Runs on NVIDIA RTX GPUs only: anything else (another GPU, an old driver, the DLL missing) fails at ensure() with the
// reason in status(), and the renderer keeps the a-trous filter while the view moves.
#pragma once
#include <d3d12.h>
#include <cstdint>
#include <string>

namespace vp3d {

class Dlss {
public:
    ~Dlss();
    static bool compiled();                       // built with the DLSS SDK
    bool ensure(ID3D12Device* device);            // NGX started and Ray Reconstruction available (once)
    bool ready() const { return m_ready; }
    bool failed() const { return m_failed; }
    const std::string& status() const { return m_status; }
    // Input (render) size for an output size: mode 0 = DLSS's optimal size for its quality mode, 1 = the output size
    // (DLAA). Falls back to the output size.
    void inputSize(uint32_t outW, uint32_t outH, int mode, uint32_t& inW, uint32_t& inH);
    // The feature must be created again (sizes changed): the caller waits for the GPU, then releaseFeature()
    bool needsFeature(uint32_t inW, uint32_t inH, uint32_t outW, uint32_t outH, int mode) const;
    void releaseFeature();
    void shutdown(ID3D12Device* device);

    struct Eval {
        ID3D12Resource *color, *diffuseAlbedo, *specularAlbedo, *normalRoughness, *depth, *motion, *output;
        uint32_t inW, inH, outW, outH;
        int mode;
        float jitterX, jitterY;   // sub-pixel offset of the sample from the pixel centre (input pixels, -0.5..0.5)
        bool reset;               // no usable history (first frame, cut, size change)
        float worldToView[16], viewToClip[16];   // row-major, column vectors (clip = P * V * p)
        float frameMs;
    };
    // Records Ray Reconstruction (creating the feature first if needed). The inputs are in the non-pixel shader
    // resource state, the output in unordered access. NGX changes the descriptor heaps and the root signature of
    // the command list: the caller binds its own again after.
    bool evaluate(ID3D12GraphicsCommandList* cmd, const Eval& e);

private:
    bool m_ready = false, m_failed = false;
    std::string m_status = "not started";
    void* m_params = nullptr;   // NVSDK_NGX_Parameter*
    void* m_handle = nullptr;   // NVSDK_NGX_Handle*
    uint32_t m_inW = 0, m_inH = 0, m_outW = 0, m_outH = 0;
    int m_mode = -1;
    ID3D12Device* m_device = nullptr;
};

} // namespace vp3d
