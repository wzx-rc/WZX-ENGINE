// Rig module core: skin deformation (linear blend skinning on the CPU), automatic binding, weight tools (normalise,
// prune, smooth, paint). Weights live in PrefabSkin (Prefab.h): dense, one row per vertex, one column per influence.
#pragma once
#include "Prefab.h"
#include <vector>

namespace app {

// Vertices welded by position (a UV or normal seam is one point for the weights) and the neighbours of each point.
struct WeldGraph {
    std::vector<uint32_t> group;                    // vertex -> point
    std::vector<std::vector<uint32_t>> members;     // point -> vertices
    std::vector<std::vector<uint32_t>> neighbours;  // point -> points sharing an edge
    void build(const MeshData& m);
};

struct SkinBindOptions {
    int method = 1;            // 0 closest bone (rigid), 1 smooth (distance falloff)
    int maxInfluences = 4;
    float falloff = 4.0f;      // smooth: weight = 1 / distance^falloff
    int smoothPasses = 2;      // smooth: weight smoothing passes over the mesh after the falloff
};

// The rest (bind) world transforms of every joint.
void restJointWorld(const PrefabAsset& a, std::vector<PartWorld>& out);

// Binds the part's mesh (local vertices, placed by meshWorld) to the given joints at their rest pose.
void bindSkin(PrefabSkin& s, const PrefabAsset& a, const MeshData& mesh, const Affine& meshWorld, const std::vector<uint32_t>& joints,
              const SkinBindOptions& opt, const WeldGraph& graph);

// Deformed mesh in world space (positions and normals) for the current joint world transforms (by joint index of the
// asset). Vertices without weight follow bindMesh.
void deformSkin(const PrefabSkin& s, const PrefabAsset& a, const MeshData& rest, const std::vector<PartWorld>& jointNow, MeshData& out);

// Weight tools. Locked influences keep their weight (PrefabSkin::locked).
void normalizeWeights(PrefabSkin& s);
void pruneWeights(PrefabSkin& s, float threshold);        // below threshold -> 0, then normalise
void limitInfluences(PrefabSkin& s, int maxInfluences);   // keeps the largest weights per vertex
void smoothWeights(PrefabSkin& s, const WeldGraph& g, int passes, float strength, const std::vector<float>* mask = nullptr);
// Sets the weight of an influence on a vertex; the other unlocked influences are scaled so that the sum stays 1
// (all zero: the rest goes to `fallback` when it is an unlocked influence, else the first unlocked one).
void setVertexWeight(PrefabSkin& s, uint32_t v, int influence, float value, int fallback);

enum PaintMode { PAINT_ADD = 0, PAINT_SUBTRACT, PAINT_REPLACE, PAINT_SMOOTH };
// One brush stamp: factor[v] = brush strength at each vertex (0 = untouched).
void paintWeights(PrefabSkin& s, const WeldGraph& g, int influence, int mode, float value, const std::vector<float>& factor, int fallback);
// Copies the weights from one side to the other (mirror on X of the rest mesh): for each vertex on the target side,
// the closest vertex on the mirrored side gives its weights, the influences swapped by name (Left/Right, _L/_R, l_/r_).
int mirrorWeights(PrefabSkin& s, const PrefabAsset& a, const MeshData& mesh, bool positiveToNegative);

} // namespace app
