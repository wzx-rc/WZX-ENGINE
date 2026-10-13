// vp3d - procedural mesh generation (boxes, cylinders, lathes, spheres).
#pragma once
#include "Math.h"
#include <vector>

namespace vp3d {

struct Vertex {
    float3 pos;
    float3 nrm;
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

} // namespace vp3d
