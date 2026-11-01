// BrickIt - raster levels of detail of the parts: vertex clustering (studs and tubes melt into a few triangles) and
// the bounding box for the far bricks.
#pragma once
#include "vp3d/MeshGen.h"

namespace brickit {

// Vertex clustering on a grid of `cell` units. Vertices are merged per cell and per main direction of their normal
// (6 buckets), so the faces of a brick keep sharp edges. Degenerate and repeated triangles are dropped.
vp3d::MeshData simplifyCluster(const vp3d::MeshData& m, float cell);
// Box over the bounds, flat faces (12 triangles).
vp3d::MeshData boxLod(const vp3d::float3& mn, const vp3d::float3& mx);

} // namespace brickit
