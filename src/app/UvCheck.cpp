// UV checks (see UvCheck.h).
#include "UvCheck.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace app {

namespace {

// vertices welded by position and UV (a seam keeps two ids, a hard normal edge does not)
struct Weld {
    std::vector<uint32_t> id;
    explicit Weld(const MeshData& m, const std::vector<float2>& uv) {
        std::unordered_map<uint64_t, uint32_t> map;
        id.resize(m.vertices.size());
        auto q = [](float x, float k) { return (int64_t)std::llround(x * k); };
        for (size_t i = 0; i < m.vertices.size(); ++i) {
            const float3& p = m.vertices[i].pos;
            uint64_t h = 1469598103934665603ull;
            for (int64_t v : {q(p.x, 1e5f), q(p.y, 1e5f), q(p.z, 1e5f), q(uv[i].x, 1e6f), q(uv[i].y, 1e6f)}) h = (h ^ (uint64_t)v) * 1099511628211ull;
            auto it = map.find(h);
            if (it == map.end()) it = map.emplace(h, (uint32_t)i).first;
            id[i] = it->second;
        }
    }
};

int findRoot(std::vector<int>& p, int x) {
    while (p[x] != x) { p[x] = p[p[x]]; x = p[x]; }
    return x;
}

} // namespace

void analyzeUv(const MeshData& m, const std::vector<float2>& uv, const float3& scale, UvAnalysis& A) {
    A = UvAnalysis();
    if (uv.size() != m.vertices.size() || m.indices.size() < 3) return;
    const size_t T = m.indices.size() / 3;
    A.valid = true;
    A.tris = (int)T;
    A.flags.assign(T, 0);
    A.island.assign(T, 0);
    A.densityLog.assign(T, 0);
    A.stretch.assign(T, 1);
    A.uvMin = float2(1e30f, 1e30f);
    A.uvMax = float2(-1e30f, -1e30f);
    for (const float2& t : uv) {
        A.uvMin = float2(std::min(A.uvMin.x, t.x), std::min(A.uvMin.y, t.y));
        A.uvMax = float2(std::max(A.uvMax.x, t.x), std::max(A.uvMax.y, t.y));
    }
    Weld weld(m, uv);
    // islands: triangles joined by a shared welded edge (two corners in common); unique edges
    std::vector<int> parent(T);
    for (size_t i = 0; i < T; ++i) parent[i] = (int)i;
    std::unordered_map<uint64_t, uint32_t> edgeSeen;   // edge -> first triangle
    edgeSeen.reserve(T * 2);
    std::vector<float> density(T, 0), weights(T, 0);
    double stretchSum = 0, areaSum = 0;
    for (size_t t = 0; t < T; ++t) {
        uint32_t i[3] = {m.indices[t * 3], m.indices[t * 3 + 1], m.indices[t * 3 + 2]};
        uint32_t w[3] = {weld.id[i[0]], weld.id[i[1]], weld.id[i[2]]};
        for (int k = 0; k < 3; ++k) {
            uint32_t lo = std::min(w[k], w[(k + 1) % 3]), hi = std::max(w[k], w[(k + 1) % 3]);
            uint64_t key = ((uint64_t)lo << 32) | hi;
            auto ins = edgeSeen.emplace(key, (uint32_t)t);
            if (ins.second) { A.edges.push_back(i[k]); A.edges.push_back(i[(k + 1) % 3]); }
            else {
                int ra = findRoot(parent, (int)ins.first->second), rb = findRoot(parent, (int)t);
                if (ra != rb) parent[ra] = rb;
            }
        }
        const float2 &a = uv[i[0]], &b = uv[i[1]], &c = uv[i[2]];
        float uvArea = 0.5f * ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x));
        if (std::fabs(uvArea) < 1e-10f) { A.flags[t] |= UVF_ZERO; ++A.zeroArea; }
        else if (uvArea < 0) { A.flags[t] |= UVF_FLIPPED; ++A.flipped; }
        bool out = false;
        for (const float2* p : {&a, &b, &c}) out |= p->x < -1e-4f || p->y < -1e-4f || p->x > 1.0001f || p->y > 1.0001f;
        if (out) { A.flags[t] |= UVF_OUTSIDE; ++A.outside01; }
        int tile = udimTile((a.x + b.x + c.x) / 3, (a.y + b.y + c.y) / 3);
        if (tile < 0) { A.flags[t] |= UVF_BADTILE; ++A.badTile; }
        ++A.tiles[tile];
        // texel density and stretch: the 3D triangle (part scale applied) in its own 2D frame
        float3 p0 = m.vertices[i[0]].pos * scale, p1 = m.vertices[i[1]].pos * scale, p2 = m.vertices[i[2]].pos * scale;
        float3 e1 = p1 - p0, e2 = p2 - p0;
        float3 n = cross(e1, e2);
        float area3 = 0.5f * length(n);
        if (area3 > 1e-12f && std::fabs(uvArea) > 1e-12f) {
            density[t] = std::sqrt(std::fabs(uvArea) / area3) * 1024.0f;
            weights[t] = area3;
            float3 X = normalize(e1), Y = normalize(cross(normalize(n), X));
            float bx = length(e1), cx = dot(e2, X), cy = dot(e2, Y);
            // J * [[bx, cx], [0, cy]] = [du1 du2]
            float du1x = b.x - a.x, du1y = b.y - a.y, du2x = c.x - a.x, du2y = c.y - a.y;
            float det = bx * cy;
            if (std::fabs(det) > 1e-14f) {
                float j00 = du1x / bx, j10 = du1y / bx;
                float j01 = (du2x - j00 * cx) / cy, j11 = (du2y - j10 * cx) / cy;
                float E = (j00 + j11) * 0.5f, F = (j00 - j11) * 0.5f, G = (j10 + j01) * 0.5f, H = (j10 - j01) * 0.5f;
                float Q = std::sqrt(E * E + H * H), R = std::sqrt(F * F + G * G);
                float s1 = Q + R, s2 = std::fabs(Q - R);
                float st = std::min(20.0f, s1 / std::max(s2, 1e-6f));
                A.stretch[t] = st;
                A.stretchMax = std::max(A.stretchMax, st);
                stretchSum += st * area3;
                areaSum += area3;
            }
        }
    }
    if (areaSum > 0) A.stretchAvg = (float)(stretchSum / areaSum);
    // islands: one id per root
    std::unordered_map<int, int> rootId;
    for (size_t t = 0; t < T; ++t) {
        int r = findRoot(parent, (int)t);
        auto it = rootId.find(r);
        if (it == rootId.end()) it = rootId.emplace(r, (int)rootId.size()).first;
        A.island[t] = it->second;
    }
    A.islands = (int)rootId.size();
    // density: median and spread (area weighted)
    {
        std::vector<std::pair<float, float>> d;
        for (size_t t = 0; t < T; ++t) if (density[t] > 0) d.push_back({density[t], weights[t]});
        if (!d.empty()) {
            std::sort(d.begin(), d.end());
            double total = 0;
            for (auto& e : d) total += e.second;
            auto pct = [&](double p) { double acc = 0; for (auto& e : d) { acc += e.second; if (acc >= total * p) return e.first; } return d.back().first; };
            A.density = pct(0.5);
            A.densitySpread = pct(0.9) / std::max(1e-6f, pct(0.1));
            for (size_t t = 0; t < T; ++t) A.densityLog[t] = density[t] > 0 ? std::log2(density[t] / A.density) : 0.0f;
        }
    }
    // coverage and overlap: each triangle rasterised in the tile of its centre (256 x 256 texels per tile, 32 tiles at most)
    {
        const int N = 256;
        std::map<int, std::vector<uint8_t>> grids;
        std::vector<int> tileOf(T);
        auto raster = [&](size_t t, auto&& visit) {
            const float2 &a0 = uv[m.indices[t * 3]], &b0 = uv[m.indices[t * 3 + 1]], &c0 = uv[m.indices[t * 3 + 2]];
            float ox = std::floor((a0.x + b0.x + c0.x) / 3), oy = std::floor((a0.y + b0.y + c0.y) / 3);
            float2 a((a0.x - ox) * N, (a0.y - oy) * N), b((b0.x - ox) * N, (b0.y - oy) * N), c((c0.x - ox) * N, (c0.y - oy) * N);
            float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
            if (std::fabs(area) < 1e-9f) return;
            float sgn = area > 0 ? 1.0f : -1.0f;
            int x0 = std::max(0, (int)std::floor(std::min(a.x, std::min(b.x, c.x)))), x1 = std::min(N - 1, (int)std::ceil(std::max(a.x, std::max(b.x, c.x))));
            int y0 = std::max(0, (int)std::floor(std::min(a.y, std::min(b.y, c.y)))), y1 = std::min(N - 1, (int)std::ceil(std::max(a.y, std::max(b.y, c.y))));
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) {
                    float qx = x + 0.5f, qy = y + 0.5f;
                    float w0 = sgn * ((b.x - a.x) * (qy - a.y) - (b.y - a.y) * (qx - a.x));
                    float w1 = sgn * ((c.x - b.x) * (qy - b.y) - (c.y - b.y) * (qx - b.x));
                    float w2 = sgn * ((a.x - c.x) * (qy - c.y) - (a.y - c.y) * (qx - c.x));
                    if (w0 > 1e-6f && w1 > 1e-6f && w2 > 1e-6f) visit(y * N + x);   // strict: shared edges are not counted twice
                }
        };
        for (size_t t = 0; t < T; ++t) {
            const float2 &a = uv[m.indices[t * 3]], &b = uv[m.indices[t * 3 + 1]], &c = uv[m.indices[t * 3 + 2]];
            int key = (int)std::floor((a.x + b.x + c.x) / 3) * 1000 + (int)std::floor((a.y + b.y + c.y) / 3);
            tileOf[t] = key;
            auto it = grids.find(key);
            if (it == grids.end()) {
                if (grids.size() >= 32) { tileOf[t] = INT32_MIN; continue; }
                it = grids.emplace(key, std::vector<uint8_t>((size_t)N * N, 0)).first;
            }
            std::vector<uint8_t>& g = it->second;
            raster(t, [&](int px) { if (g[px] < 255) ++g[px]; });
        }
        for (size_t t = 0; t < T; ++t) {
            if (tileOf[t] == INT32_MIN) continue;
            std::vector<uint8_t>& g = grids[tileOf[t]];
            int over = 0, total = 0;
            raster(t, [&](int px) { ++total; over += g[px] > 1; });
            if (over >= 2 || (total > 0 && over * 4 >= total && over > 0 && total >= 2)) { A.flags[t] |= UVF_OVERLAP; ++A.overlapTris; }
        }
        size_t covered = 0, over = 0;
        for (auto& kv : grids) {
            size_t c = 0;
            for (uint8_t v : kv.second) { c += v > 0; over += v > 1; }
            covered += c;
            if (kv.first == 0) A.coverage01 = (float)c / (N * N);
        }
        A.overlap = covered ? (float)over / covered : 0.0f;
    }
}

} // namespace app
