#include "Dlss.h"
#include <windows.h>
#ifdef WZX_DLSS
#include <nvsdk_ngx_helpers_dlssd_d3d.h>
#endif
#include <cstdio>
#include <cstring>
#include <string>

namespace vp3d {

#ifdef WZX_DLSS

namespace {
std::string ngxError(NVSDK_NGX_Result r) {
    char b[32];
    snprintf(b, sizeof(b), "0x%08X", (unsigned)r);
    switch (r) {
    case NVSDK_NGX_Result_FAIL_FeatureNotSupported: return "not supported by this GPU";
    case NVSDK_NGX_Result_FAIL_PlatformError: return "platform error";
    case NVSDK_NGX_Result_FAIL_FeatureAlreadyExists: return "feature already exists";
    case NVSDK_NGX_Result_FAIL_FeatureNotFound: return "nvngx_dlssd.dll not found";
    case NVSDK_NGX_Result_FAIL_InvalidParameter: return "invalid parameter";
    case NVSDK_NGX_Result_FAIL_OutOfDate: return "driver or DLSS out of date";
    case NVSDK_NGX_Result_FAIL_OutOfGPUMemory: return "out of GPU memory";
    case NVSDK_NGX_Result_FAIL_UnsupportedFormat: return "unsupported format";
    case NVSDK_NGX_Result_FAIL_UnsupportedParameter: return "unsupported parameter";
    default: return std::string("NGX error ") + b;
    }
}
NVSDK_NGX_PerfQuality_Value quality(int mode) {
    return mode == 1 ? NVSDK_NGX_PerfQuality_Value_DLAA : NVSDK_NGX_PerfQuality_Value_MaxQuality;
}
} // namespace

bool Dlss::compiled() { return true; }

Dlss::~Dlss() { shutdown(m_device); }

bool Dlss::ensure(ID3D12Device* device) {
    if (m_ready) return true;
    if (m_failed) return false;
    m_failed = true;
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe;
    dir = dir.substr(0, dir.find_last_of(L"\\/"));
    if (GetFileAttributesW((dir + L"\\nvngx_dlssd.dll").c_str()) == INVALID_FILE_ATTRIBUTES) {
        m_status = "nvngx_dlssd.dll missing next to the editor";
        return false;
    }
    const wchar_t* paths[] = {dir.c_str()};
    NVSDK_NGX_FeatureCommonInfo info = {};
    info.PathListInfo.Path = paths;
    info.PathListInfo.Length = 1;
    std::wstring data = dir;   // NGX writes its logs there
    NVSDK_NGX_Result r = NVSDK_NGX_D3D12_Init_with_ProjectID("7c1d4b0e-5a2f-4e8b-9d61-3f0a2b8c9e14", NVSDK_NGX_ENGINE_TYPE_CUSTOM, "1.0",
                                                             data.c_str(), device, &info);
    if (NVSDK_NGX_FAILED(r)) { m_status = "NGX: " + ngxError(r) + " (NVIDIA RTX GPU and driver needed)"; return false; }
    m_device = device;
    NVSDK_NGX_Parameter* params = nullptr;
    r = NVSDK_NGX_D3D12_GetCapabilityParameters(&params);
    if (NVSDK_NGX_FAILED(r) || !params) { m_status = "NGX parameters: " + ngxError(r); return false; }
    m_params = params;
    int available = 0, needsDriver = 0;
    NVSDK_NGX_Parameter_GetI(params, NVSDK_NGX_Parameter_SuperSamplingDenoising_NeedsUpdatedDriver, &needsDriver);
    r = NVSDK_NGX_Parameter_GetI(params, NVSDK_NGX_Parameter_SuperSamplingDenoising_Available, &available);
    if (needsDriver) { m_status = "a newer NVIDIA driver is needed for DLSS Ray Reconstruction"; return false; }
    if (NVSDK_NGX_FAILED(r) || !available) {
        int init = 0;
        NVSDK_NGX_Parameter_GetI(params, NVSDK_NGX_Parameter_SuperSamplingDenoising_FeatureInitResult, &init);
        m_status = "Ray Reconstruction unavailable on this GPU (" + ngxError((NVSDK_NGX_Result)init) + ")";
        return false;
    }
    m_failed = false;
    m_ready = true;
    m_status = "ready";
    return true;
}

void Dlss::inputSize(uint32_t outW, uint32_t outH, int mode, uint32_t& inW, uint32_t& inH) {
    inW = outW;
    inH = outH;
    if (!m_ready || mode == 1) return;
    unsigned w = 0, h = 0, maxW = 0, maxH = 0, minW = 0, minH = 0;
    float sharp = 0;
    NVSDK_NGX_Result r = NGX_DLSSD_GET_OPTIMAL_SETTINGS((NVSDK_NGX_Parameter*)m_params, outW, outH, quality(mode), &w, &h,
                                                       &maxW, &maxH, &minW, &minH, &sharp);
    if (!NVSDK_NGX_FAILED(r) && w > 0 && h > 0) { inW = w; inH = h; }
}

bool Dlss::needsFeature(uint32_t inW, uint32_t inH, uint32_t outW, uint32_t outH, int mode) const {
    return m_handle && (inW != m_inW || inH != m_inH || outW != m_outW || outH != m_outH || mode != m_mode);
}

void Dlss::releaseFeature() {
    if (m_handle) NVSDK_NGX_D3D12_ReleaseFeature((NVSDK_NGX_Handle*)m_handle);
    m_handle = nullptr;
}

void Dlss::shutdown(ID3D12Device* device) {
    releaseFeature();
    if (m_params) NVSDK_NGX_D3D12_DestroyParameters((NVSDK_NGX_Parameter*)m_params);
    m_params = nullptr;
    if (m_ready && device) NVSDK_NGX_D3D12_Shutdown1(device);
    m_ready = false;
    m_device = nullptr;
}

bool Dlss::evaluate(ID3D12GraphicsCommandList* cmd, const Eval& e) {
    if (!m_ready) return false;
    if (!m_handle) {
        NVSDK_NGX_DLSSD_Create_Params cp = {};
        cp.InDenoiseMode = NVSDK_NGX_DLSS_Denoise_Mode_DLUnified;
        cp.InRoughnessMode = NVSDK_NGX_DLSS_Roughness_Mode_Packed;   // roughness in the w of the normals
        cp.InUseHWDepth = NVSDK_NGX_DLSS_Depth_Type_Linear;
        cp.InWidth = e.inW;
        cp.InHeight = e.inH;
        cp.InTargetWidth = e.outW;
        cp.InTargetHeight = e.outH;
        cp.InPerfQualityValue = quality(e.mode);
        cp.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR | NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
                                  NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
        NVSDK_NGX_Handle* h = nullptr;
        NVSDK_NGX_Result r = NGX_D3D12_CREATE_DLSSD_EXT(cmd, 1, 1, &h, (NVSDK_NGX_Parameter*)m_params, &cp);
        if (NVSDK_NGX_FAILED(r) || !h) {
            m_status = "Ray Reconstruction could not start: " + ngxError(r);
            m_failed = true;
            m_ready = false;
            return false;
        }
        m_handle = h;
        m_inW = e.inW; m_inH = e.inH; m_outW = e.outW; m_outH = e.outH; m_mode = e.mode;
    }
    NVSDK_NGX_D3D12_DLSSD_Eval_Params ep = {};
    ep.pInColor = e.color;
    ep.pInOutput = e.output;
    ep.pInDiffuseAlbedo = e.diffuseAlbedo;
    ep.pInSpecularAlbedo = e.specularAlbedo;
    ep.pInNormals = e.normalRoughness;
    ep.pInRoughness = e.normalRoughness;   // packed mode: the same image
    ep.pInDepth = e.depth;
    ep.pInMotionVectors = e.motion;
    ep.InJitterOffsetX = -e.jitterX;       // NGX wants the offset that brings the sample back to the pixel centre
    ep.InJitterOffsetY = -e.jitterY;
    ep.InRenderSubrectDimensions.Width = e.inW;
    ep.InRenderSubrectDimensions.Height = e.inH;
    ep.InReset = e.reset ? 1 : 0;
    ep.InMVScaleX = 1.0f;
    ep.InMVScaleY = 1.0f;
    float v[16], p[16];
    memcpy(v, e.worldToView, sizeof(v));
    memcpy(p, e.viewToClip, sizeof(p));
    ep.pInWorldToViewMatrix = v;
    ep.pInViewToClipMatrix = p;
    ep.InFrameTimeDeltaInMsec = e.frameMs;
    NVSDK_NGX_Result r = NGX_D3D12_EVALUATE_DLSSD_EXT(cmd, (NVSDK_NGX_Handle*)m_handle, (NVSDK_NGX_Parameter*)m_params, &ep);
    if (NVSDK_NGX_FAILED(r)) {
        m_status = "Ray Reconstruction failed: " + ngxError(r);
        return false;
    }
    m_status = "running";
    return true;
}

#else   // built without the DLSS SDK

bool Dlss::compiled() { return false; }
Dlss::~Dlss() {}
bool Dlss::ensure(ID3D12Device*) {
    m_failed = true;
    m_status = "editor built without the DLSS SDK";
    return false;
}
void Dlss::inputSize(uint32_t outW, uint32_t outH, int, uint32_t& inW, uint32_t& inH) { inW = outW; inH = outH; }
bool Dlss::needsFeature(uint32_t, uint32_t, uint32_t, uint32_t, int) const { return false; }
void Dlss::releaseFeature() {}
void Dlss::shutdown(ID3D12Device*) {}
bool Dlss::evaluate(ID3D12GraphicsCommandList*, const Eval&) { return false; }

#endif

} // namespace vp3d
