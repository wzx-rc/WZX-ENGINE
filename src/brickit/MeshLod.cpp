#include "MeshLod.h"
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace brickit {

using namespace vp3d;

static int normalBucket(const float3& n) {
    float ax = std::fabs(n.x), ay = std::fabs(n.y), az = std::fabs(n.z);
    if (ax >= ay && ax >= az) return n.x >= 0 ? 0 : 1;
    if (ay >= az) return n.y >= 0 ? 2 : 3;
    return n.z >= 0 ? 4 : 5;
}

MeshData simplifyCluster(const MeshData& m, float cell) {
    MeshData out;
    if (m.vertices.empty() || cell <= 0) return m;
    float3 mn, mx;
    m.bounds(mn, mx);
    struct Acc { float3 p, n; float w = 0; };
    std::unordered_map<uint64_t, uint32_t> cluster;
    std::vector<Acc> acc;
    std::vector<uint32_t> remap(m.vertices.size());
    const float inv = 1.0f / cell;
    for (size_t i = 0; i < m.vertices.size(); ++i) {
        const Vertex& v = m.vertices[i];
        uint64_t x = (uint64_t)std::floor((v.pos.x - mn.x) * inv + 0.5f), y = (uint64_t)std::floor((v.pos.y - mn.y) * inv + 0.5f),
                 z = (uint64_t)std::floor((v.pos.z - mn.z) * inv + 0.5f);
        uint64_t key = (x & 0xFFFFF) | ((y & 0xFFFFF) << 20) | ((z & 0x3FFF) << 40) | ((uint64_t)normalBucket(v.nrm) << 54);
        auto it = cluster.find(key);
        uint32_t c;
        if (it == cluster.end()) { c = (uint32_t)acc.size(); cluster.emplace(key, c); acc.push_back({}); }
        else c = it->second;
        acc[c].p = acc[c].p + v.pos;
        acc[c].n = acc[c].n + v.nrm;
        acc[c].w += 1.0f;
        remap[i] = c;
    }
    out.vertices.resize(acc.size());
    for (size_t c = 0; c < acc.size(); ++c) {
        float3 p = acc[c].p * (1.0f / acc[c].w);
        // keep the outer box exact: a coordinate within half a cell of the bounds snaps to it
        for (int k = 0; k < 3; ++k) {
            float* pk = &p.x + k;
            const float lo = (&mn.x)[k], hi = (&mx.x)[k];
            if (*pk - lo < cell * 0.5f) *pk = lo;
            if (hi - *pk < cell * 0.5f) *pk = hi;
        }
        out.vertices[c] = {p, normalize(acc[c].n), {0, 0}};
    }
    std::unordered_set<uint64_t> seen;
    for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        uint32_t a = remap[m.indices[t]], b = remap[m.indices[t + 1]], c = remap[m.indices[t + 2]];
        if (a == b || b == c || a == c) continue;
        // same triangle twice (any rotation): one is enough
        uint32_t lo = a < b ? (a < c ? a : c) : (b < c ? b : c);
        uint32_t r0 = a, r1 = b, r2 = c;
        while (r0 != lo) { uint32_t tmp = r0; r0 = r1; r1 = r2; r2 = tmp; }
        uint64_t key = ((uint64_t)r0 * 73856093ull) ^ ((uint64_t)r1 * 19349663ull << 1) ^ ((uint64_t)r2 * 83492791ull << 2);
        if (!seen.insert(key).second) continue;
        out.addTri(a, b, c);
    }
    if (out.indices.empty()) return boxLod(mn, mx);
    return out;
}

MeshData boxLod(const float3& mn, const float3& mx) { return makeBox(mn, mx); }

} // namespace brickit
