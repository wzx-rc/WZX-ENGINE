// Shared resources and helpers for all vp3d compute passes.
#include "Shared.h"

ConstantBuffer<FrameConstants> F : register(b0);
RaytracingAccelerationStructure gScene : register(t0);
StructuredBuffer<GpuVertex> gVerts : register(t1);
StructuredBuffer<uint> gIndices : register(t2);
StructuredBuffer<GpuInstance> gInst : register(t3);
StructuredBuffer<GpuMaterial> gMats : register(t4);
StructuredBuffer<GpuLight> gLights : register(t5);

// 0 accum (rgb sum, w = samples) | 1 albedo (rgb, w = instance id + 1) | 2 normal (xyz) + depth (w)
// 3,4 denoise ping-pong (demodulated radiance) | 5 output LDR
RWTexture2D<float4> gTex[6] : register(u0);

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

