// vp3d - scene description consumed by the renderer.
#pragma once
#include "MeshGen.h"
#include "EnvImage.h"
#include <string>
#include <vector>

namespace vp3d {

struct Material {
    std::string name = "Material";
    float3 baseColor{0.8f, 0.8f, 0.8f};
    float roughness = 0.5f;
    float metallic = 0.0f;
    float specular = 0.5f;
    float transmission = 0.0f;
    float ior = 1.5f;
    float subsurface = 0.0f;
    float3 sssRadius{1.0f, 0.35f, 0.15f};
    float3 emission{0, 0, 0};
    float emissionStrength = 0.0f;
    float bevelRadius = 0.0f;      // in scene units, 0 = off
    float coat = 0.0f;
    float coatRoughness = 0.05f;
    uint32_t flags = 0;            // MAT_FLAG_*
    int baseColorTex = -1;         // index in Scene::textures multiplied with baseColor (-1 = none)
    int preset = 0;                // app-defined preset id (UI only)
    int colorId = -1;              // app-defined palette colour id (UI only)
};

struct Object {
    std::string name;
    std::string part;              // library part id (UI only)
    PrimitiveParams prim;          // parametric primitive (Create > Mesh): the mesh is built from it; type PRIM_NONE otherwise
    int mesh = 0;
    int material = 0;
    float3 position{0, 0, 0};
    float yaw = 0.0f;              // radians around +Y
    float4 rot{0, 0, 0, 1};        // free orientation (quaternion), used instead of yaw when useRot is set
    bool useRot = false;
    float3 scale{1, 1, 1};
    bool visible = true;
    bool selectable = true;
    bool ghost = false;            // drawn translucent in the raster viewport only (placement preview)
    float ghostAlpha = 0.35f;
    int grab = -1;                 // app-defined grab/snap column (-1 = default)
    // Small world-space tilt (radians around X and Z) applied around a local pivot (animation).
    float tiltX = 0.0f, tiltZ = 0.0f;
    float3 tiltPivot{0, 0, 0};

    Affine transform() const {
        Affine m = useRot ? Affine::trsQuat(position, rot, scale) : Affine::trs(position, yaw, scale);
        if (tiltX == 0.0f && tiltZ == 0.0f) return m;
        float3 w = m.point(tiltPivot);
        float cx = std::cos(tiltX), sx = std::sin(tiltX), cz = std::cos(tiltZ), sz = std::sin(tiltZ);
        // R = Rx(tiltX) * Rz(tiltZ)
        float3 c0(cz, sz * cx, sz * sx), c1(-sz, cz * cx, cz * sx), c2(0, -sx, cx);
        Affine r;
        for (int i = 0; i < 3; ++i) {
            float3 col(m.m[0][i], m.m[1][i], m.m[2][i]);
            float3 rc = c0 * col.x + c1 * col.y + c2 * col.z;
            r.m[0][i] = rc.x; r.m[1][i] = rc.y; r.m[2][i] = rc.z;
        }
        float3 t(m.m[0][3] - w.x, m.m[1][3] - w.y, m.m[2][3] - w.z);
        float3 rt = c0 * t.x + c1 * t.y + c2 * t.z + w;
        r.m[0][3] = rt.x; r.m[1][3] = rt.y; r.m[2][3] = rt.z;
        return r;
    }
};

// Rectangular area light (one-sided, emits along +normal, normal = towards target).
struct AreaLight {
    std::string name = "Light";
    float3 position{0, 5, 0};
    float3 target{0, 0, 0};
    float width = 1.0f, height = 1.0f;
    float3 color{1, 1, 1};
    float intensity = 10.0f;
    bool visibleToCamera = true;
    bool enabled = true;

    void basis(float3& U, float3& V, float3& N) const;
};

// Image used by materials (RGBA8, sRGB encoded, mip 0 only: the renderer builds the mip chain).
struct Texture {
    std::string name;
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> rgba;
};

// Distant sun (directional light). Raster: key light with the shadow map. Path tracing: a small, far area light.
struct SunLight {
    bool enabled = false;
    float3 direction{0.4f, 0.8f, 0.3f};   // towards the sun
    float3 color{1.0f, 0.93f, 0.82f};
    float intensity = 3.2f;               // irradiance on a surface facing the sun
};

struct Environment {
    float3 top{0.05f, 0.05f, 0.06f};
    float3 horizon{0.08f, 0.08f, 0.09f};
    float3 bottom{0.02f, 0.02f, 0.02f};
    float intensity = 1.0f;
    bool visibleToCamera = true;
    float3 ambient{0.06f, 0.06f, 0.065f};  // extra fill used by the raster viewport (no GI there)
    // HDRI (lat-long image): replaces the gradient for lighting, reflections and the background when set.
    EnvImagePtr hdri;
    float hdriIntensity = 1.0f;
    float hdriRotation = 0.0f;             // radians around +Y
};

struct Camera {
    float3 target{0, 0, 0};
    float yaw = 0.6f;        // radians
    float pitch = 0.35f;     // radians
    float distance = 10.0f;
    float fovY = radians(35.0f);
    bool dof = false;
    float fStop = 2.8f;
    float focalLengthMm = 50.0f;  // used for aperture size together with fStop
    float focusDistance = 10.0f;
    float sensorScale = 1.0f;     // scene units per meter, for physically scaled aperture

    float3 eye() const;
    void basis(float3& right, float3& up, float3& fwd) const;
    float apertureRadius() const; // in scene units
};

struct Scene {
    std::string name;
    std::vector<MeshData> meshes;
    std::vector<std::string> meshNames;
    std::vector<Material> materials;
    std::vector<Object> objects;
    std::vector<AreaLight> lights;
    std::vector<Texture> textures;   // material textures, see Material::baseColorTex
    SunLight sun;                // distant sun: the RTS play turns it on (off by default)
    Environment env;
    Camera camera;               // default camera
    float gridScale = 1.0f;      // grid spacing for MAT_FLAG_GRID
    float unitsPerMeter = 1.0f;  // used for DOF and bevel defaults
    uint32_t geometryVersion = 1; // bump when meshes change

    int addMesh(const std::string& n, MeshData m) {
        meshes.push_back(std::move(m));
        meshNames.push_back(n);
        ++geometryVersion;
        return (int)meshes.size() - 1;
    }
    int findMesh(const std::string& n) const {
        for (size_t i = 0; i < meshNames.size(); ++i) if (meshNames[i] == n) return (int)i;
        return -1;
    }
    int addTexture(Texture t) {
        textures.push_back(std::move(t));
        ++geometryVersion;
        return (int)textures.size() - 1;
    }
    int addMaterial(const Material& m) { materials.push_back(m); return (int)materials.size() - 1; }
    int addObject(const Object& o) { objects.push_back(o); return (int)objects.size() - 1; }
    size_t triangleCount() const;
};

// Ray / scene picking on the CPU (used for selection and focus picking).
struct PickResult {
    int object = -1;
    float t = 1e30f;
    float3 position;
    float3 normal;
};
PickResult pick(const Scene& s, const float3& origin, const float3& dir);

} // namespace vp3d
