// vp3d - CPU side of the real-time "Lit" raster pass, independent of the graphics API.
//
// The DirectX 12 viewport (RendererRaster.cpp) and the multi-API Lit renderer (src/lit, Diligent Engine: DirectX 12
// on Windows, Metal through MoltenVK on iOS) feed shaders/Raster.hlsl with the same data. Everything that decides
// what the image looks like (materials, lights, key light and shadow frustum, HDRI mips, SH and sun) is computed
// here once, so that both renderers stay identical (docs/RENDU_PARITE.md).
#pragma once
#include "Scene.h"
#include "shaders/Shared.h"
#include <filesystem>
#include <vector>

namespace vp3d {
namespace lit {

// One mesh inside the shared vertex / index buffers.
struct MeshInfo {
    uint32_t indexOffset = 0, vertexOffset = 0, indexCount = 0, vertexCount = 0;
    float3 bmin, bmax;
    float3 center;
    float radius = 0;   // bounding sphere (emissive objects become sphere lights)
};

// All meshes of the scene, then the internal light quad, concatenated.
struct Geometry {
    std::vector<gpu::GpuVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<MeshInfo> meshes;
    int quadMesh = -1;
};
void buildGeometry(const Scene& scene, Geometry& out);

// Draw flags (Raster.hlsl): 1 ghost (placement preview), 2 light quad seen by the camera, 4 hidden light quad.
struct Draw {
    int mesh = 0;
    Affine xf;
    uint32_t material = 0;
    uint32_t id = 0;        // instance + 1 (object id pass)
    uint32_t flags = 0;
    float alpha = 1.0f;
};

struct SceneData {
    std::vector<gpu::GpuMaterial> materials;   // scene materials, then the light materials
    std::vector<gpu::GpuLight> lights;         // never empty (a zero light when there is none)
    uint32_t lightCount = 0;                   // real lights in `lights`
    uint32_t sunLight = NO_LIGHT;              // the sun's area light (path tracing only, skipped by the raster pass)
    std::vector<Draw> draws;
    float3 center;                             // bounds of the scene without huge ground planes (shadow map)
    float radius = 10.0f;
};
// textureValid[i]: Scene::textures[i] is uploaded and bound at slot i (else the material ignores it).
void buildSceneData(const Scene& scene, const Geometry& geo, const std::vector<bool>& textureValid, SceneData& out);
bool isTransparent(const Scene& scene, const Draw& d);

// HDRI prepared for the raster pass: box-filtered mip chain (blurred reflections), irradiance SH with and without
// the sun of the image, and that sun (key light with the shadow map).
struct Environment {
    const EnvImage* image = nullptr;           // source (identity check)
    std::vector<std::vector<float>> mips;      // RGB float per level, level 0 = the image
    std::vector<uint32_t> mipW, mipH;
    float sh[9][3] = {};                       // irradiance SH without the sun
    float shFull[9][3] = {};                   // irradiance SH with the sun
    float3 sunDir{0, 1, 0}, sunE{0, 0, 0};     // image space direction, irradiance
    bool sunValid = false;
    bool valid() const { return !mips.empty(); }
};
bool prepareEnvironment(const EnvImage& img, Environment& out);

// RGBA8 (sRGB encoded) mip chain of a material texture, box filter on the encoded values.
struct MipChain {
    std::vector<std::vector<uint8_t>> levels;
    std::vector<uint32_t> w, h;
};
void buildTextureMips(const Texture& t, MipChain& out);

uint16_t floatToHalf(float f);

// Constant buffers of shaders/Raster.hlsl (same layout).
struct RasterConstants {
    float viewProj[16];
    float lightViewProj[16];
    float3 camPos; float time;
    float3 camRight; float tanHalfFovY;
    float3 camUp; float aspect;
    float3 camFwd; uint32_t lightCount;
    float3 envTop; float envIntensity;
    float3 envHorizon; float gridScale;
    float3 envBottom; uint32_t keyLight;
    float3 ambient; float shadowTexel;
    float3 keyPos; float lightScale;
    float3 sunDir; float sunOn;
    float3 sunRadiance; float sunFromEnv;
    float shIrr[9][4];
    float envMapOn; float envRotation; float envMapIntensity; float envMips;
    float envVisible; uint32_t sunLight; float giMode, pad3;   // giMode: DDGI probes read by Shade (Raster.hlsl)
    float3 hemiSky{0, 0, 0}; float hemiOn = 0;   // (Raster.hlsl layout)
    float3 hemiGround{0, 0, 0}; float hemiPad = 0;
    float rtShadow = 0, sunTanRadius = 0, rtShadowSamples = 0, rtRefl = 0;   // ray traced sun shadow, reflections (DirectX 12 viewport only)
    float centreValid = 0, rcPad0 = 0, rcPad1 = 0, rcPad2 = 0;
    float3 envSun2Dir{0, 1, 0}; float envSun2On = 0;   // (DirectX 12 viewport only)
    float3 envSun2E{0, 0, 0}; float envSun2Tan = 0;
    float sssScale = 0, giSmooth = 0, sssPad1 = 0, sssPad2 = 0;   // (DirectX 12 viewport only)
};

struct DrawConstants {
    float rows[12];
    uint32_t materialId;
    uint32_t id;
    float alpha;
    uint32_t flags;
};
DrawConstants drawConstants(const Draw& d);

struct FrameSettings {
    float lightScale = 1.0f;
    bool envSun = true;            // RenderSettings::rasterEnvSun
    uint32_t shadowSize = 2048;
    bool ddgi = false;             // Shade reads the DDGI probes (R.giMode): the main pass and the probe captures
};

// Camera, key light and shadow frustum, sun, environment. env: the prepared HDRI of scene.env.hdri, or null.
// castShadows: false when there is no key light (the shadow pass draws nothing).
void computeFrame(const Scene& scene, const SceneData& data, const Environment* env, const FrameSettings& fs,
                  int viewW, int viewH, RasterConstants& out, bool& castShadows);

// ---------------------------------------------------------------- DDGI probe volume (shared by both renderers)
// Regular probe grid over the scene bounds: about `budget` probes, isotropic spacing, probe (0,0,0) at `origin`,
// probe p = x + nx * (y + ny * z). The same grid in the editor and in the Lit builds (docs/RENDU_PARITE.md).
struct ProbeGrid {
    uint32_t counts[3] = {0, 0, 0};
    float3 origin;
    float spacing = 1.0f;
    uint32_t total() const { return counts[0] * counts[1] * counts[2]; }
    // Atlas sizes: irradiance (DDGI_IRR_N + 2 texels per tile), distance (DDGI_DIST_N + 2), rays, probe state.
    uint32_t irrW() const { return counts[0] * counts[1] * (DDGI_IRR_N + 2); }
    uint32_t irrH() const { return counts[2] * (DDGI_IRR_N + 2); }
    uint32_t distW() const { return counts[0] * counts[1] * (DDGI_DIST_N + 2); }
    uint32_t distH() const { return counts[2] * (DDGI_DIST_N + 2); }
};
// Bounds of the drawn objects (light quads excluded), the volume the probes cover.
void probeBounds(const SceneData& data, const Geometry& geo, float3& mn, float3& mx);
// mn / mx: probeBounds; sceneCenter / radius: SceneData::center / radius (the grid stays within 2 radii of the centre).
void placeProbeGrid(const float3& mn, const float3& mx, const float3& sceneCenter, float sceneRadius, int budget, ProbeGrid& out);

// Per-frame DDGI constants (random rotation of the ray set, capture batch, view).
struct DDGIFrame {
    uint32_t frame = 0;          // probe update counter (0: no history yet)
    float hysteresis = 0.97f;
    uint32_t temporalMax = 1;    // image history length (editor, ray traced visibility)
    int view = 0;                // 0 full, 1 indirect only, 2 direct only
    bool showProbes = false;
    bool rasterCapture = false;  // probe rays from raster cube captures
    bool warmup = false;         // raster capture after a grid change: no hysteresis, the whole grid captured in a few frames
    uint32_t captureBase = 0, captureSlots = 0, captureSeen = 0;   // see DDGIConstants
};
void ddgiConstants(const ProbeGrid& grid, const DDGIFrame& f, Rng& rng, gpu::DDGIConstants& out);

// Baked probes: the DDGI atlases computed once in the editor (ray traced probes, converged) and read as they are by
// the Lit renderer on every target, no computation at run time. Texels are RGBA16F (halves), as on the GPU.
struct BakedProbes {
    ProbeGrid grid;
    uint32_t signature = 0;              // scene state the probes were baked from (0 = unknown)
    std::vector<uint16_t> irradiance;    // grid.irrW() x grid.irrH() x 4
    std::vector<uint16_t> distance;      // grid.distW() x grid.distH() x 4
    std::vector<uint16_t> state;         // grid.total() x 4 (x = 1 active)
    bool valid() const { return grid.total() > 0 && irradiance.size() == (size_t)grid.irrW() * grid.irrH() * 4; }
};
bool saveBakedProbes(const std::filesystem::path& path, const BakedProbes& p);
bool loadBakedProbes(const std::filesystem::path& path, BakedProbes& p);

} // namespace lit
} // namespace vp3d
