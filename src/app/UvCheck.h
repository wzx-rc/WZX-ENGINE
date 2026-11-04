// UV checks of a mesh for one UV set (UV Editor of the Prefab studio): UDIM tiles, islands, flipped and zero-area
// triangles, overlap, coverage of the 0-1 square, texel density and stretch. UVs use the usual convention: u right,
// v up, the 0-1 square is UDIM tile 1001 (tile = 1001 + floor(u) + 10 * floor(v), u tiles 0..9, v >= 0).
#pragma once
#include "vp3d/Scene.h"
#include <cmath>
#include <map>
#include <vector>

namespace app {

using namespace vp3d;

enum UvTriFlag : uint8_t { UVF_FLIPPED = 1, UVF_OVERLAP = 2, UVF_ZERO = 4, UVF_OUTSIDE = 8, UVF_BADTILE = 16 };

struct UvAnalysis {
    bool valid = false;
    int tris = 0, zeroArea = 0, flipped = 0, outside01 = 0, badTile = 0, overlapTris = 0, islands = 0;
    std::map<int, int> tiles;            // UDIM tile -> triangles (tile of the triangle centre); -1 = outside the UDIM range
    float coverage01 = 0;                // part of the 0-1 square covered
    float overlap = 0;                   // part of the covered texels covered twice or more (all tiles)
    float density = 0;                   // median texel density: pixels per scene unit (1 cell) on a 1024 texture
    float densitySpread = 1;             // 90th / 10th percentile of the density (1 = uniform)
    float stretchAvg = 1, stretchMax = 1;   // anisotropy of the mapping (1 = no stretch), area weighted average
    float2 uvMin, uvMax;
    std::vector<uint8_t> flags;          // per triangle, UvTriFlag
    std::vector<int> island;             // per triangle
    std::vector<float> densityLog;       // per triangle: log2(density / median)
    std::vector<float> stretch;          // per triangle
    std::vector<uint32_t> edges;         // unique edges (vertex index pairs, seams counted once per side)
};

// uv: one per vertex of m. scale: world scale of the part (texel density in scene units).
void analyzeUv(const MeshData& m, const std::vector<float2>& uv, const float3& scale, UvAnalysis& out);

inline int udimTile(float u, float v) {
    int tu = (int)std::floor(u), tv = (int)std::floor(v);
    return tu >= 0 && tu <= 9 && tv >= 0 && tv <= 99 ? 1001 + tu + 10 * tv : -1;
}

} // namespace app
