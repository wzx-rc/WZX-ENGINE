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
#define MAT_FLAG_VCOLOR    4u   // prints: a vertex uv with y = 1 carries a colour (x = sRGB 0xRRGGBB) over the base colour

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
    // Roughness mask (fingerprints): texture projected in object space (triplanar, scale = repeats per unit, offset
    // per instance), its dark areas raise the roughness by up to `strength` and lower the specular a little.
    uint roughTex;     float roughTexScale; float roughTexStrength; float pad2;
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

    uint selMoreCount; float selAge; uint selStyle; float selPixelScale;   // extra selected instances (multi-selection), instance id + 1; selection glow: seconds since the selection changed, 1 = blue glow, render pixels per screen pixel
    uint4 selMore[8];

    // Temporal reprojection: camera of the previous frame (the history holds that frame).
    float3 prevCamPos;   float prevTanHalfFovY;
    float3 prevCamRight; float prevAspect;
    float3 prevCamUp;    uint historyValid;     // 1 = history textures hold the previous frame
    float3 prevCamFwd;   float historyCap;      // history length in frames (exponential average window)
    uint selCovValid; uint traceStride; uint gridLine; float selFill;   // selFill: white veil over the selection (selStyle 2, BrickIt); gridLine: Scene::gridLine (RGBA8, 0 = default lines)   // selCovValid: the selection coverage (t8 of the tonemapper) was drawn this frame; traceStride: 1 = every pixel traced, 4 = one pixel of each 2 x 2 block per frame (in turn)
    float denoiseEdgeLuma; float selCenterX; float selCenterY; float selRadius;   // edge sensitivity of the denoiser (lower = keeps shadows); selection glow: screen centre and radius (pixels) of the selection
    // HDRI: lat-long texture (gEnvTex) and its importance sampling tables (gEnvDist, see EnvSample in PathTrace.hlsl)
    uint envW; uint envH; float envRotation; float envMapIntensity;   // table size, rotation (radians), intensity
    float envPdfScale; float envMips; float filterGlossy; float indirectClamp;   // W*H / sum of the table, mip count of gEnvTex; see TracePath
    // Orthographic camera (0 = perspective): half the height of the view (scene units) and how far behind the eye the
    // parallel rays start (the whole scene in front of them); the same for the previous frame (reprojection)
    float orthoHalfH; float orthoBack; float prevOrthoHalfH; float prevOrthoBack;
    float sharpen; float gridFadeX; float gridFadeZ; float gridFadeR;   // gridFade: radial fade of the grid (Scene::gridFade)   // sharpening of the tone mapper (0 = off): the image rendered at a reduced scale, then enlarged
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
                                                          // flags: 1 show probes, 2 raster capture, 4 warm-up (no hysteresis)
    float normalBias; float viewBias; float energy;
    float ageCap;   // progressive probes: a probe updated ageCap times since its last restart is converged and no longer
                    // updated until a change restarts it (0: always updated)
    // Raster probe capture (flags bit 2): the probes of the batch starting at captureBase (mod total) were rendered
    // this frame into the cube captures; probes below captureSeen have been captured at least once.
    uint captureBase; uint captureSeen; uint captureSlots;
    uint stride;   // ray traced probes: only those with (p + frame) % stride == 0 are updated this frame (0, 1: all)
    // progressive probes (flags bit 16): the box of the objects added, removed or moved this frame (w > 0: valid);
    // the probes in it restart their running average (their age, gProbe.z, back to 0), the others keep theirs
    float4 dirtyMin; float4 dirtyMax;
};

// Probe cube faces (raster capture): forward and up per face, right = cross(up, forward). Same order in C++.
// Cube face texture size and the face field of view (90 degrees).
#define DDGI_FACE 16

// Radiance Cascades (world space, 3D). Cascade i: probes every spacing0 * 2^i, dirRes0 * 2^i octahedral directions
// per side (4x the directions of the cascade below), rays over [tStart, tEnd] with intervals 4x longer per cascade.
// Each texel of a cascade holds the radiance merged with the cascades above it (rgb) and the distance to the first
// hit (w, negative = back face). Cascade 0 sits on the DDGI probe grid and fills its atlases (RCGather.hlsl).
#define RC_MAX_CASCADES 6
#define RC_MAX_DIR0 8         // dirRes of cascade 0 at most (groupshared size of the gather pass)

struct RCCascade {
    uint4 counts;             // probes per axis, w = total
    float3 origin; float spacing;   // probe (0,0,0), distance between probes
    uint dirRes; uint offset; float tStart; float tEnd;   // directions per side, first texel in the buffer, interval
    uint maskOffset; uint pad0; uint pad1; uint pad2;     // per probe: which of its 8 parents it sees (RCVisibility)
};

struct RCConstants {
    RCCascade c[RC_MAX_CASCADES];
    uint cascadeCount; uint frame; uint flags; uint lightSamples;   // flags: 1 bounces (feedback), 2 animated noise
    float hysteresis; float pad0; float pad1; float pad2;
};

// Screen-space Radiance Cascades (probes on the visible surfaces, rays traced in the whole scene): cascade i has one
// probe per tile x tile pixels (tile = spacing * 2^i) placed where the tile centre's camera ray hits, dirRes
// = dirRes0 * 2^i octahedral directions, intervals x4 per cascade, the top one to infinity. Every cascade holds about
// the same number of texels. Cascade 0 is gathered per pixel into the screen GI texture read by the raster pass.
#define RCS_MAX_CASCADES 8

struct RCSCascade {
    uint4 grid;                       // probes per row, per column, tile (pixels), directions per side
    uint offset; uint probeOffset; float tStart; float tEnd;   // first texel, first probe record (2 float4 each)
};

struct RCSConstants {
    RCSCascade c[RCS_MAX_CASCADES];
    uint cascadeCount; uint flags; float planeTolerance; uint shOffset;   // planeTolerance: world size of a C0 tile;
                                                                         // shOffset: irradiance SH of the C0 probes (7 float4 each)
};

// Split Radiance Cascades (Freeman, Sannikov, Pitt-Francis 2026): world-space probes kept sparse in a hash table per
// cascade, only around the visible surfaces, with levels of detail that keep a probe about the same size on screen.
// Cascade i: spacing s0 * 2^i, (2 D0 2^i) x (D0 2^i) equal-area directions, interval ]tEnd(i-1), tEnd(i)] with
// tEnd(i) = t0 * 4^i (the top one to infinity); every LOD doubles s0 and t0. Ray splitting: one ray per pixel, from
// the visible surface, deposited into every cascade its hit distance tells about (transparent below the hit cascade,
// the hit radiance in it). Everything lives in one raw buffer (word offsets below) and is rebuilt every frame.
#define SRC_CASCADES 6
#define SRC_MAX_MOVED 8          // boxes of moved objects per frame (more: all the history is dropped)
#define SRC_IRR_N 6              // per cascade-0 probe: 6x6 octahedral irradiance texels (rgb / pi, valid weight)
#define SRC_SCALE 2048.0         // fixed point of the deposited radiance (atomic integer sums)

struct SRCCascade {
    uint cap; uint maxProbes; uint dirW; uint dirH;   // hash slots (power of 2), probe capacity, directions per row, rows
    uint keys; uint vals; uint probes; uint entries;  // word offsets: slot keys, slot probe index, probe records (8 words:
                                                      // cell xyz, lod, rays, offset, cursor, parent), entries (4 words
                                                      // per probe and direction: deposits, then the merged radiance)
    float spacing; float tEnd; float pad0; float pad1;   // at LOD 0
};

struct SplitRCConstants {
    SRCCascade c[SRC_CASCADES];
    uint cascadeCount; uint counters; uint pixels; uint irr;   // word offsets: counters (probes per cascade, then the
                                                               // ray offset of the top cascade), pixel records (8 words),
                                                               // irradiance texels of the cascade-0 probes (4 words each)
    float lodDist; float lodOverlap; float jitterX; float jitterY;   // distance where LOD 1 starts, overlap of the LODs
                                                                     // (fraction of an octave), jitter of the ray set
    uint clearWords; uint flags; uint debug; uint debugCascade;   // words cleared at the start (counters and slot keys),
                                                                  // SRC_FLAG_*, inspection view (SRC_DEBUG_*) and its cascade
    // Accumulation while nothing moves: the cascade-0 irradiance of the previous frame (u22: slot keys, slot probe,
    // irradiance texels, history length per probe), looked up by world cell. Reset where an object moved (boxes), all
    // of it when a light, a material or the environment changed (prevValid 0).
    float3 prevCamPos; uint prevCap;
    uint prevIrr; uint prevHist; uint histMax; uint movedCount;   // word offsets in u22, history cap (0 = off)
    uint hist; uint prevValid; uint frame; uint irrF;             // history lengths of this frame, filtered irradiance
                                                                  // (word offsets in u16; irrF = irr without the filter)
    float4 movedMin[SRC_MAX_MOVED]; float4 movedMax[SRC_MAX_MOVED];
    // GI bounds (RenderSettings::giBounds*): probes only inside the box grown by the fade distance (giMin.w), the GI
    // confidence fading to 0 across it (the raster ambient, analytic sky, takes over: no probe cells to the horizon on
    // a huge ground). giMax.w = 1 when on.
    float4 giMin; float4 giMax;
};

#define SRC_FLAG_ANIMATE 1u      // the ray set and the light samples change every frame
#define SRC_DEBUG_CELLS 1u       // inspection: probe cells of cascade debugCascade (random colour per cell, LOD tint)
#define SRC_DEBUG_PROBES 2u      // irradiance of the nearest cascade-0 probe, no blending (the probes as blocks)
#define SRC_DEBUG_HIT 3u         // cascade where the pixel's ray deposited its radiance (blue c0 ... red top, black sky)
#define SRC_DEBUG_RAYS 4u        // rays received by the pixel's cascade-0 probe (heat map, white = 256)
#define SRC_DEBUG_LOD 5u         // level of detail of the pixel
#define SRC_DEBUG_HISTORY 6u     // frames accumulated (black = none, white = full history)

#define RC_FLAG_BOUNCES 1u
#define RC_FLAG_ANIMATED_NOISE 2u
#define RC_FLAG_BILINEAR_FIX 4u   // merge with one traced segment per parent (RCTrace.hlsl, BilinearFixMerge)

#ifdef __cplusplus
}} // namespace vp3d::gpu
#endif

#endif

