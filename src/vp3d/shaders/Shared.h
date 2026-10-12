// Structures shared between C++ and HLSL. Layouts are kept 16-byte friendly
// (float3 always followed by a scalar) so cbuffer and structured buffer
// packing match the C++ side.
#ifndef VP3D_SHARED_H
#define VP3D_SHARED_H

#ifdef __cplusplus
#include "../Math.h"
namespace vp3d { namespace gpu {
typedef uint32_t uint;
struct uint2 { uint x, y; };
struct uint4 { uint x, y, z, w; };
#endif

// Material flags
#define MAT_FLAG_GRID      1u   // procedural ground grid
#define MAT_FLAG_THIN      2u   // thin-walled transmission (no refraction offset)

// Instance masks
#define MASK_OPAQUE        0x01u
#define MASK_GLASS         0x02u
#define MASK_LIGHT_VISIBLE 0x04u
#define MASK_LIGHT_HIDDEN  0x08u

// Frame flags
#define FRAME_FLAG_DOF       1u
#define FRAME_FLAG_BEVEL     2u
#define FRAME_FLAG_DENOISE   4u
#define FRAME_FLAG_ENV_VISIBLE 8u

#define NO_LIGHT 0xFFFFFFFFu

struct GpuVertex {
    float3 pos;
    float3 nrm;
};

struct GpuInstance {
    uint indexOffset;
    uint vertexOffset;
    uint materialId;
    uint lightId;
};

struct GpuMaterial {
    float3 baseColor;  float roughness;
    float3 emission;   float metallic;
    float3 sssRadius;  float subsurface;
    float transmission; float ior; float bevelRadius; uint flags;
    float specular;    float coat; float coatRoughness; float pad;
};

struct GpuLight {
    float3 corner; float area;
    float3 edgeU;  uint instance;
    float3 edgeV;  uint pad0;
    float3 radiance; float pad1;
};

struct FrameConstants {
    float3 camPos;   float aperture;
    float3 camRight; float focusDist;
    float3 camUp;    float tanHalfFovY;
    float3 camFwd;   float aspect;

    uint2 resolution; uint frameIndex; uint sampleIndex;
    uint maxBounces; uint lightCount; uint samplesPerFrame; uint flags;

    float3 envTop;     float envIntensity;
    float3 envHorizon; float exposure;
    float3 envBottom;  float bevelScale;

    uint selectedInstance; float fireflyClamp; float sssScale; float gridScale;
    float denoiseStrength; float lightScale; float vignette; float bevelSamples;

    uint selMoreCount; uint pad0; uint pad1; uint pad2;   // extra selected instances (multi-selection), instance id + 1
    uint4 selMore[8];
};

#ifdef __cplusplus
}} // namespace vp3d::gpu
#endif

#endif

