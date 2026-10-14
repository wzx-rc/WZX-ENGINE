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

#define MAX_TEXTURES       16u  // material textures bound to the raster and path tracing passes
#define NO_TEXTURE         0xFFFFFFFFu

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
#define FRAME_FLAG_ID_EDGES  16u   // denoiser does not blend across objects (DDGI mode: short history)
#define FRAME_FLAG_FLAT_SHADING 32u   // faceted polygons: geometric normals instead of interpolated ones
#define FRAME_FLAG_NO_JITTER 64u      // anti-aliasing off: no sub-pixel jitter
#define FRAME_FLAG_CENTRE_VALID 128u  // stable edges: the pixel-centre surface validates the history
#define FRAME_FLAG_ENV_MAP   512u     // HDRI environment (gEnvTex + gEnvDist) instead of the gradient

#define NO_LIGHT 0xFFFFFFFFu

// Light types (GpuLight::type)
#define LIGHT_RECT   0u   // area light quad: corner, edgeU, edgeV, area
#define LIGHT_OBJECT 1u   // emissive object: corner = centre and area = radius of its bounding sphere, radiance = emission

struct GpuVertex {
    float3 pos;
    float3 nrm;
    float2 uv;
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
    float specular;    float coat; float coatRoughness; uint baseColorTex;   // NO_TEXTURE or index in the texture table
};

struct GpuLight {
    float3 corner; float area;
    float3 edgeU;  uint instance;
    float3 edgeV;  uint type;     // LIGHT_RECT or LIGHT_OBJECT
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

    // Temporal reprojection: camera of the previous frame (the history holds that frame).
    float3 prevCamPos;   float prevTanHalfFovY;
    float3 prevCamRight; float prevAspect;
    float3 prevCamUp;    uint historyValid;     // 1 = history textures hold the previous frame
    float3 prevCamFwd;   float historyCap;      // history length in frames (exponential average window)
    uint pad3; uint pad4; uint pad5; uint pad6;
    float denoiseEdgeLuma; float pad7; float pad8; float pad9;   // edge sensitivity of the denoiser (lower = keeps shadows)
    // HDRI: lat-long texture (gEnvTex) and its importance sampling tables (gEnvDist, see EnvSample in PathTrace.hlsl)
    uint envW; uint envH; float envRotation; float envMapIntensity;   // table size, rotation (radians), intensity
    float envPdfScale; float envMips; float filterGlossy; float indirectClamp;   // W*H / sum of the table, mip count of gEnvTex; see TracePath
};

// DDGI probe volume (real-time GI test). Atlases: irradiance 8x8 octahedral texels per probe, distance
// 16x16, each tile with a 1-texel border. Probe p = x + nx * (y + ny * z) uses tile (p % (nx*ny), p / (nx*ny)).
#define DDGI_RAYS 128
#define DDGI_IRR_N 8
#define DDGI_DIST_N 16

struct DDGIConstants {
    float3 origin;  float hysteresis;      // position of probe (0,0,0)
    float3 spacing; float maxDistance;     // distance between probes, distance clamp
    uint4 counts;                          // xyz probe counts, w = total
    float4 rot0, rot1, rot2;               // per-frame random rotation of the ray set (rows)
    uint frame; uint temporalMax; uint view; uint flags;  // view: 0 full, 1 indirect diffuse only, 2 direct only
    float normalBias; float viewBias; float energy; float pad;
};

#ifdef __cplusplus
}} // namespace vp3d::gpu
#endif

#endif

