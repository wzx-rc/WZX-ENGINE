// Shared resources and helpers for all vp3d compute passes.
#include "Shared.h"

ConstantBuffer<FrameConstants> F : register(b0);
RaytracingAccelerationStructure gScene : register(t0);
StructuredBuffer<GpuVertex> gVerts : register(t1);
StructuredBuffer<uint> gIndices : register(t2);
StructuredBuffer<GpuInstance> gInst : register(t3);
StructuredBuffer<GpuMaterial> gMats : register(t4);
StructuredBuffer<GpuLight> gLights : register(t5);
StructuredBuffer<float> gEnvDist : register(t6);   // HDRI importance sampling tables (see EnvSample)
Texture2D<float4> gEnvTex : register(t7);          // HDRI, lat-long, linear, with mips

// 0 accum (rgb sum, w = samples) | 1 albedo (rgb, w = instance id + 1) | 2 normal (xyz) + depth (w)
// 3,4 denoise ping-pong (demodulated radiance) | 5 output LDR
RWTexture2D<float4> gTex[6] : register(u0);

// Material textures (sRGB views, sampled as linear) and their sampler.
Texture2D<float4> gTextures[MAX_TEXTURES] : register(t0, space1);
SamplerState gLinearWrap : register(s0);

static const float PI = 3.14159265358979;
static const uint SLOT_ACCUM = 0;
static const uint SLOT_ALBEDO = 1;
static const uint SLOT_NORMAL = 2;
static const uint SLOT_OUTPUT = 5;

// Selection test: the primary selected instance or one of the extra ones (multi-selection).
bool IsSelected(uint id) {
    if (id == 0) return false;
    if (id == F.selectedInstance) return true;
    for (uint i = 0; i < F.selMoreCount && i < 32; ++i)
        if (F.selMore[i >> 2][i & 3] == id) return true;
    return false;
}

float Luminance(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }

// Primary ray through the image point ndc (-1..1, y up): from the eye (perspective), or parallel to the view from
// F.orthoBack behind the eye (orthographic camera, F.orthoHalfH > 0: the whole scene lies in front of the rays).
void CameraRay(float2 ndc, out float3 org, out float3 dir) {
    if (F.orthoHalfH > 0) {
        org = F.camPos + F.camRight * (ndc.x * F.aspect * F.orthoHalfH) + F.camUp * (ndc.y * F.orthoHalfH) - F.camFwd * F.orthoBack;
        dir = F.camFwd;
    } else {
        org = F.camPos;
        dir = normalize(F.camFwd + F.camRight * (ndc.x * F.aspect * F.tanHalfFovY) + F.camUp * (ndc.y * F.tanHalfFovY));
    }
}

// Distance at which a perspective camera would see P as this camera does (pixel footprint): line widths and fading
// of the procedural ground grid.
float ViewDistance(float3 P) { return F.orthoHalfH > 0 ? F.orthoHalfH / max(F.tanHalfFovY, 1e-3) : length(P - F.camPos); }

uint Hash(uint x) {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

uint Pcg(inout uint s) {
    s = s * 747796405u + 2891336453u;
    uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
    return (w >> 22u) ^ w;
}

float Rnd(inout uint s) { return (Pcg(s) >> 8) * (1.0 / 16777216.0); }
float2 Rnd2(inout uint s) { return float2(Rnd(s), Rnd(s)); }

