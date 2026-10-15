// vp3d - procedural mesh generation (boxes, cylinders, lathes, spheres).
#pragma once
#include "Math.h"
#include <vector>

namespace vp3d {

struct Vertex {
    float3 pos;
    float3 nrm;
    float2 uv;                     // texture coordinates (0 when the mesh has none)
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    void append(const MeshData& o, const Affine& xf = Affine());
    void addTri(uint32_t a, uint32_t b, uint32_t c) { indices.push_back(a); indices.push_back(b); indices.push_back(c); }
    // Quad with a flat normal, corners in CCW order seen from outside.
    void addQuad(const float3& a, const float3& b, const float3& c, const float3& d, const float3& n);
    void bounds(float3& mn, float3& mx) const;
    size_t triangleCount() const { return indices.size() / 3; }
};

// Profile point for lathes: radius r at height y.
struct LathePoint { float r, y; };

MeshData makeBox(const float3& mn, const float3& mx);
MeshData makeQuadXY();  // unit quad [-0.5,0.5]^2 at z=0, normal +Z
MeshData makePlane(float size); // XZ plane at y=0, normal +Y
MeshData makeSphere(float r, int segU = 64, int segV = 32);
MeshData makeCylinder(float r, float y0, float y1, int seg, bool capTop, bool capBottom);
// Revolve a profile (bottom -> top, outside surface) around Y.
// Normals are smoothed between segments whose angle is below smoothDeg.
MeshData makeLathe(const std::vector<LathePoint>& profile, int seg, float smoothDeg = 35.0f);
MeshData makeTorusProfile(float R, float r, int segProfile, std::vector<LathePoint>& out);

MeshData makeShaderBall(float radius);

// Parametric primitives (editor Create > Mesh). Pivot at the centre of the base (y = 0), sizes in scene units.
enum PrimType { PRIM_NONE = 0, PRIM_CUBE, PRIM_SPHERE, PRIM_PLANE, PRIM_CYLINDER, PRIM_TORUS, PRIM_DISK };
struct PrimitiveParams {
    int type = PRIM_NONE;
    float3 size{1, 1, 1};   // cube: width x height x depth; plane: width (x) x depth (z); cylinder: height (y)
    float radius = 0.5f;    // sphere, cylinder, disk; torus: ring radius (centre of the tube)
    float radius2 = 0.15f;  // torus: tube radius
    int segments = 32;      // around the axis: sphere, cylinder, torus, disk
    int rings = 16;         // sphere: pole to pole; torus: around the tube
    int subdiv = 1;         // cube / plane: cuts per side; cylinder: cuts along the height; disk: concentric rings
};
MeshData makePrimitive(const PrimitiveParams& p);

} // namespace vp3d
