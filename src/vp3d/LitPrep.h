// vp3d - CPU side of the real-time "Lit" raster pass, independent of the graphics API.
//
// The DirectX 12 viewport (RendererRaster.cpp) and the multi-API Lit renderer (src/lit, Diligent Engine: DirectX 12
// on Windows, Metal through MoltenVK on iOS) feed shaders/Raster.hlsl with the same data. Everything that decides
// what the image looks like (materials, lights, key light and shadow frustum, HDRI mips, SH and sun) is computed
// here once, so that both renderers stay identical (docs/RENDU_PARITE.md).
#pragma once
#include "Scene.h"
#include "shaders/Shared.h"
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
    float envVisible; uint32_t sunLight; float pad2, pad3;
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
};

// Camera, key light and shadow frustum, sun, environment. env: the prepared HDRI of scene.env.hdri, or null.
// castShadows: false when there is no key light (the shadow pass draws nothing).
void computeFrame(const Scene& scene, const SceneData& data, const Environment* env, const FrameSettings& fs,
                  int viewW, int viewH, RasterConstants& out, bool& castShadows);

} // namespace lit
} // namespace vp3d
