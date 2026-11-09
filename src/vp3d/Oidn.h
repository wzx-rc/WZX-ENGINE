// vp3d - Intel Open Image Denoise (OIDN 2.x, Apache 2.0) for the progressive path tracer of the editor.
//
// OpenImageDenoise.dll is loaded at run time from the folder of the executable (build.bat copies it there from
// third_party\oidn, git-ignored): nothing to link, and an editor built without it just reports OIDN unavailable.
//
// Images: three float3 planes in one D3D12 buffer (colour, albedo, normal: OidnPack.hlsl writes them from the
// accumulation) and one float3 output buffer (read by OidnPack.hlsl's blend). Two ways to hand them to OIDN:
//  - shared: the device is the GPU of the renderer (same LUID) and imports D3D12 memory: the buffers are D3D12
//    resources created shared, OIDN reads and writes them in place (no copy through the CPU);
//  - staged: any other device (CPU, a GPU without import): the input is copied to a readback buffer, written into
//    OIDN's own buffers, and the result comes back through an upload buffer.
// In both cases the caller waits for its GPU work before run() and records the rest of the frame after it.
#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include <string>

namespace vp3d {

class Oidn {
public:
    ~Oidn();
    // OpenImageDenoise.dll next to the executable (cheap check, no load): the option can be offered
    static bool dllPresent();
    // Loads the library and creates the device on first call (the GPU of `device` when OIDN supports it, else the
    // best device OIDN finds, else the CPU). False when OIDN cannot run here (see status()).
    bool ensureDevice(ID3D12Device* device);
    bool ready() const { return m_device != nullptr; }
    bool failed() const { return m_failed; }
    bool shared() const { return m_shared; }
    // Buffers for a w x h image (recreated when the size changes; the filter is committed again)
    bool resize(ID3D12Device* device, uint32_t w, uint32_t h);
    ID3D12Resource* input() const { return m_in.Get(); }          // colour, albedo, normal planes (UAV, float3 each)
    ID3D12Resource* output() const { return m_out.Get(); }        // denoised colour (UAV, float3)
    ID3D12Resource* readback() const { return m_readback.Get(); } // staged: the input copied here before run()
    ID3D12Resource* upload() const { return m_upload.Get(); }     // staged: the output, to copy into output()
    uint64_t inputBytes() const { return m_inBytes; }
    uint64_t outputBytes() const { return m_outBytes; }
    // Denoises the input (the caller's GPU work that wrote it has completed). Synchronous; false on error.
    bool run();
    float lastMs() const { return m_lastMs; }       // time of the last run() (filter execution, copies included)
    int runs() const { return m_runs; }
    const std::string& deviceName() const { return m_deviceName; }   // "CUDA · NVIDIA GeForce RTX ..." / "CPU · ..."
    const std::string& status() const { return m_status; }           // error or state text for the UI

private:
    void release();
    bool check(const char* what);   // reads the device error; false (and status set) on error
    void* m_lib = nullptr;          // HMODULE
    void* m_device = nullptr;       // OIDNDevice
    void* m_filter = nullptr;       // OIDNFilter
    void* m_inBuf = nullptr;        // OIDNBuffer (shared import or OIDN's own)
    void* m_outBuf = nullptr;
    HANDLE m_inHandle = nullptr, m_outHandle = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_in, m_out, m_readback, m_upload;
    uint64_t m_inBytes = 0, m_outBytes = 0;
    uint32_t m_w = 0, m_h = 0;
    bool m_failed = false, m_shared = false, m_importable = false;
    unsigned m_importType = 0;
    float m_lastMs = 0.0f;
    int m_runs = 0;
    std::string m_deviceName, m_status = "not started";
};

} // namespace vp3d
