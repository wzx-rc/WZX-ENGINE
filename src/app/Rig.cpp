// Rig module core (see Rig.h).
#include "Rig.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace app {

void WeldGraph::build(const MeshData& m) {
    group.assign(m.vertices.size(), 0);
    members.clear();
    neighbours.clear();
    std::unordered_map<uint64_t, uint32_t> map;
    auto q = [](float x) { return (int64_t)std::llround(x * 1e5f); };
    for (size_t i = 0; i < m.vertices.size(); ++i) {
        const float3& p = m.vertices[i].pos;
        uint64_t h = 1469598103934665603ull;
        for (int64_t v : {q(p.x), q(p.y), q(p.z)}) h = (h ^ (uint64_t)v) * 1099511628211ull;
        auto it = map.find(h);
        if (it == map.end()) {
            it = map.emplace(h, (uint32_t)members.size()).first;
            members.emplace_back();
        }
        group[i] = it->second;
        members[it->second].push_back((uint32_t)i);
    }
    neighbours.assign(members.size(), {});
    for (size_t t = 0; t + 2 < m.indices.size(); t += 3)
        for (int k = 0; k < 3; ++k) {
            uint32_t a = group[m.indices[t + k]], b = group[m.indices[t + (k + 1) % 3]];
            if (a == b) continue;
            auto& na = neighbours[a];
            if (std::find(na.begin(), na.end(), b) == na.end()) na.push_back(b);
            auto& nb = neighbours[b];
            if (std::find(nb.begin(), nb.end(), a) == nb.end()) nb.push_back(a);
        }
}

void restJointWorld(const PrefabAsset& a, std::vector<PartWorld>& out) {
    std::vector<PartPose> pose;
    evaluateJoints(a, nullptr, 0, pose);
    jointWorld(a, pose, out);
}

namespace {

float distSegment(const float3& p, const float3& a, const float3& b) {
    float3 ab = b - a;
    float l2 = dot(ab, ab);
    float t = l2 > 1e-12f ? clampf(dot(p - a, ab) / l2, 0, 1) : 0.0f;
    return length(a + ab * t - p);
}

// keeps the n largest weights of a row, then normalises it
void keepLargest(float* row, size_t count, int n) {
    if ((int)count > n) {
        std::vector<std::pair<float, size_t>> v;
        for (size_t k = 0; k < count; ++k) v.push_back({row[k], k});
        std::sort(v.begin(), v.end(), [](auto& x, auto& y) { return x.first > y.first; });
        for (size_t k = (size_t)n; k < v.size(); ++k) row[v[k].second] = 0;
    }
    float sum = 0;
    for (size_t k = 0; k < count; ++k) sum += row[k];
    if (sum > 1e-8f)
        for (size_t k = 0; k < count; ++k) row[k] /= sum;
}

} // namespace

void bindSkin(PrefabSkin& s, const PrefabAsset& a, const MeshData& mesh, const Affine& meshWorld, const std::vector<uint32_t>& joints,
              const SkinBindOptions& opt, const WeldGraph& graph) {
    std::vector<PartWorld> rest;
    restJointWorld(a, rest);
    const size_t J = joints.size();
    s.joints = joints;
    s.locked.assign(J, 0);
    s.bindJoint.resize(J);
    s.bindMesh = meshWorld;
    s.vertexCount = (uint32_t)mesh.vertices.size();
    s.maxInfluences = opt.maxInfluences;
    s.weights.assign((size_t)s.vertexCount * J, 0.0f);
    if (!J) return;
    // bones: each influence is the segment from its joint to each of its children (a leaf: towards its parent's
    // direction, half the parent bone long; alone: a point)
    std::vector<std::vector<std::pair<float3, float3>>> bones(J);
    for (size_t k = 0; k < J; ++k) {
        int ji = a.findJoint(joints[k]);
        if (ji < 0) continue;
        s.bindJoint[k] = toAffine(rest[ji]);
        const float3 p = rest[ji].position;
        std::vector<int> kids = a.jointChildren(joints[k]);
        for (int c : kids) bones[k].push_back({p, rest[c].position});
        if (kids.empty()) {
            int pi = a.joints[ji].parent ? a.findJoint(a.joints[ji].parent) : -1;
            if (pi >= 0) {
                float3 d = p - rest[pi].position;
                bones[k].push_back({p, p + d * 0.5f});
            } else bones[k].push_back({p, p});
        }
    }
    // distances on the welded points (seams get the same weights)
    const size_t G = graph.members.size();
    std::vector<float> gw(G * J, 0.0f);
    for (size_t g = 0; g < G; ++g) {
        float3 p = meshWorld.point(mesh.vertices[graph.members[g][0]].pos);
        float* row = &gw[g * J];
        size_t best = 0;
        float bestD = 1e30f;
        for (size_t k = 0; k < J; ++k) {
            float d = 1e30f;
            for (auto& b : bones[k]) d = std::min(d, distSegment(p, b.first, b.second));
            row[k] = d;
            if (d < bestD) { bestD = d; best = k; }
        }
        if (opt.method == 0) {
            for (size_t k = 0; k < J; ++k) row[k] = k == best ? 1.0f : 0.0f;
        } else {
            for (size_t k = 0; k < J; ++k) row[k] = 1.0f / (std::pow(std::max(row[k], 1e-4f), opt.falloff));
            keepLargest(row, J, std::max(1, opt.maxInfluences));
        }
    }
    // smoothing over the surface, then the influence limit again
    for (int pass = 0; pass < (opt.method == 0 ? 0 : opt.smoothPasses); ++pass) {
        std::vector<float> next = gw;
        for (size_t g = 0; g < G; ++g) {
            const auto& nb = graph.neighbours[g];
            if (nb.empty()) continue;
            for (size_t k = 0; k < J; ++k) {
                float avg = 0;
                for (uint32_t n : nb) avg += gw[n * J + k];
                avg /= (float)nb.size();
                next[g * J + k] = gw[g * J + k] * 0.5f + avg * 0.5f;
            }
        }
        gw.swap(next);
    }
    for (size_t g = 0; g < G; ++g) {
        keepLargest(&gw[g * J], J, std::max(1, opt.maxInfluences));
        for (uint32_t v : graph.members[g])
            for (size_t k = 0; k < J; ++k) s.w(v, k) = gw[g * J + k];
    }
}

void deformSkin(const PrefabSkin& s, const PrefabAsset& a, const MeshData& rest, const std::vector<PartWorld>& jointNow, MeshData& out) {
    out.vertices = rest.vertices;
    const size_t J = s.joints.size();
    std::vector<Affine> M(J);
    for (size_t k = 0; k < J; ++k) {
        int ji = a.findJoint(s.joints[k]);
        Affine now = ji >= 0 && ji < (int)jointNow.size() ? toAffine(jointNow[ji]) : s.bindJoint[k];
        M[k] = mul(mul(now, s.bindJoint[k].inverse()), s.bindMesh);
    }
    const bool ok = s.vertexCount == rest.vertices.size();
    if (s.dualQuaternion && ok) {
        // D_k = J_k(t) * bind_k^-1 (rigid: rotation + translation) as unit dual quaternions, blended per vertex;
        // the mesh is first placed by bindMesh (its scale stays out of the blend)
        struct DQ { float4 r, d; };
        std::vector<DQ> dq(J);
        for (size_t k = 0; k < J; ++k) {
            int ji = a.findJoint(s.joints[k]);
            Affine now = ji >= 0 && ji < (int)jointNow.size() ? toAffine(jointNow[ji]) : s.bindJoint[k];
            Affine D = mul(now, s.bindJoint[k].inverse());
            float3 X(D.m[0][0], D.m[1][0], D.m[2][0]), Y(D.m[0][1], D.m[1][1], D.m[2][1]), Z(D.m[0][2], D.m[1][2], D.m[2][2]);
            float4 q = qFromBasis(normalize(X), normalize(Y), normalize(Z));
            float3 t(D.m[0][3], D.m[1][3], D.m[2][3]);
            // d = 0.5 * (t, 0) * q
            float4 tq(t.x, t.y, t.z, 0);
            float4 d = qMul(tq, q);
            dq[k] = {q, float4(d.x * 0.5f, d.y * 0.5f, d.z * 0.5f, d.w * 0.5f)};
        }
        for (size_t v = 0; v < rest.vertices.size(); ++v) {
            const Vertex& src = rest.vertices[v];
            float4 br(0, 0, 0, 0), bd(0, 0, 0, 0);
            float sum = 0;
            int first = -1;
            for (size_t k = 0; k < J; ++k) {
                float w = s.w(v, k);
                if (w <= 0) continue;
                if (first < 0) first = (int)k;
                const float4& r0 = dq[(size_t)first].r;
                const float sign = dq[k].r.x * r0.x + dq[k].r.y * r0.y + dq[k].r.z * r0.z + dq[k].r.w * r0.w < 0 ? -1.0f : 1.0f;
                float ws = w * sign;
                br = float4(br.x + dq[k].r.x * ws, br.y + dq[k].r.y * ws, br.z + dq[k].r.z * ws, br.w + dq[k].r.w * ws);
                bd = float4(bd.x + dq[k].d.x * ws, bd.y + dq[k].d.y * ws, bd.z + dq[k].d.z * ws, bd.w + dq[k].d.w * ws);
                sum += w;
            }
            float3 p0 = s.bindMesh.point(src.pos), n0 = s.bindMesh.vector(src.nrm);
            float len = std::sqrt(br.x * br.x + br.y * br.y + br.z * br.z + br.w * br.w);
            if (sum < 1e-6f || len < 1e-8f) { out.vertices[v].pos = p0; out.vertices[v].nrm = normalize(n0); continue; }
            br = float4(br.x / len, br.y / len, br.z / len, br.w / len);
            bd = float4(bd.x / len, bd.y / len, bd.z / len, bd.w / len);
            float3 rv(br.x, br.y, br.z), dv(bd.x, bd.y, bd.z);
            float3 tr = (dv * br.w - rv * bd.w + cross(rv, dv)) * 2.0f;
            out.vertices[v].pos = qRot(br, p0) + tr;
            out.vertices[v].nrm = normalize(qRot(br, n0));
        }
        return;
    }
    for (size_t v = 0; v < rest.vertices.size(); ++v) {
        const Vertex& src = rest.vertices[v];
        float3 p(0), n(0);
        float sum = 0;
        if (ok)
            for (size_t k = 0; k < J; ++k) {
                float w = s.w(v, k);
                if (w <= 0) continue;
                p += M[k].point(src.pos) * w;
                n += M[k].vector(src.nrm) * w;
                sum += w;
            }
        if (sum < 1e-6f) {
            p = s.bindMesh.point(src.pos);
            n = s.bindMesh.vector(src.nrm);
        } else if (sum < 0.999f || sum > 1.001f) {
            p = p / sum;
        }
        out.vertices[v].pos = p;
        out.vertices[v].nrm = normalize(n);
    }
}

void normalizeWeights(PrefabSkin& s) {
    const size_t J = s.joints.size();
    for (uint32_t v = 0; v < s.vertexCount; ++v) {
        float sum = 0;
        for (size_t k = 0; k < J; ++k) sum += std::max(0.0f, s.w(v, k));
        if (sum > 1e-8f)
            for (size_t k = 0; k < J; ++k) s.w(v, k) = std::max(0.0f, s.w(v, k)) / sum;
    }
}

void pruneWeights(PrefabSkin& s, float threshold) {
    const size_t J = s.joints.size();
    for (uint32_t v = 0; v < s.vertexCount; ++v) {
        float keep = 0;
        for (size_t k = 0; k < J; ++k) if (s.w(v, k) >= threshold) keep += s.w(v, k);
        if (keep <= 1e-8f) continue;   // every weight is small: the vertex keeps them
        for (size_t k = 0; k < J; ++k) if (s.w(v, k) < threshold) s.w(v, k) = 0;
    }
    normalizeWeights(s);
}

void limitInfluences(PrefabSkin& s, int maxInfluences) {
    const size_t J = s.joints.size();
    for (uint32_t v = 0; v < s.vertexCount; ++v) keepLargest(&s.weights[v * J], J, std::max(1, maxInfluences));
    s.maxInfluences = maxInfluences;
}

void setVertexWeight(PrefabSkin& s, uint32_t v, int influence, float value, int fallback) {
    const size_t J = s.joints.size();
    if (influence < 0 || influence >= (int)J || v >= s.vertexCount) return;
    auto locked = [&](size_t k) { return k < s.locked.size() && s.locked[k]; };
    if (locked((size_t)influence)) return;
    float lockedSum = 0, others = 0;
    for (size_t k = 0; k < J; ++k) {
        if ((int)k == influence) continue;
        if (locked(k)) lockedSum += s.w(v, k);
        else others += s.w(v, k);
    }
    const float room = std::max(0.0f, 1.0f - lockedSum);
    value = clampf(value, 0.0f, room);
    const float rest = room - value;
    if (others > 1e-6f) {
        for (size_t k = 0; k < J; ++k)
            if ((int)k != influence && !locked(k)) s.w(v, k) *= rest / others;
    } else if (rest > 1e-6f) {
        // nobody else to take the rest: the fallback influence (parent joint), else the first unlocked one
        int target = fallback >= 0 && fallback < (int)J && fallback != influence && !locked((size_t)fallback) ? fallback : -1;
        for (size_t k = 0; k < J && target < 0; ++k)
            if ((int)k != influence && !locked(k)) target = (int)k;
        if (target >= 0) s.w(v, (size_t)target) = rest;
        else value = room;   // only this influence can carry the weight
    }
    s.w(v, (size_t)influence) = value;
}

void smoothWeights(PrefabSkin& s, const WeldGraph& g, int passes, float strength, const std::vector<float>* mask) {
    const size_t J = s.joints.size(), G = g.members.size();
    if (!J || g.group.size() != s.vertexCount) return;
    std::vector<float> gw(G * J, 0.0f);
    for (size_t p = 0; p < G; ++p)
        for (size_t k = 0; k < J; ++k) gw[p * J + k] = s.w(g.members[p][0], k);
    for (int pass = 0; pass < passes; ++pass) {
        std::vector<float> next = gw;
        for (size_t p = 0; p < G; ++p) {
            float f = strength * (mask ? (*mask)[g.members[p][0]] : 1.0f);
            if (f <= 0 || g.neighbours[p].empty()) continue;
            float lockedSum = 0, avgSum = 0;
            std::vector<float> avg(J, 0.0f);
            for (size_t k = 0; k < J; ++k) {
                if (k < s.locked.size() && s.locked[k]) { lockedSum += gw[p * J + k]; continue; }
                for (uint32_t n : g.neighbours[p]) avg[k] += gw[n * J + k];
                avg[k] /= (float)g.neighbours[p].size();
                avgSum += avg[k];
            }
            const float room = std::max(0.0f, 1.0f - lockedSum);
            float sum = 0;
            for (size_t k = 0; k < J; ++k) {
                if (k < s.locked.size() && s.locked[k]) continue;
                float target = avgSum > 1e-8f ? avg[k] / avgSum * room : gw[p * J + k];
                next[p * J + k] = lerpf(gw[p * J + k], target, clampf(f, 0, 1));
                sum += next[p * J + k];
            }
            if (sum > 1e-8f)
                for (size_t k = 0; k < J; ++k)
                    if (!(k < s.locked.size() && s.locked[k])) next[p * J + k] *= room / sum;
        }
        gw.swap(next);
    }
    for (size_t p = 0; p < G; ++p)
        for (uint32_t v : g.members[p])
            for (size_t k = 0; k < J; ++k) s.w(v, k) = gw[p * J + k];
}

void paintWeights(PrefabSkin& s, const WeldGraph& g, int influence, int mode, float value, const std::vector<float>& factor, int fallback) {
    if (influence < 0 || influence >= (int)s.joints.size() || factor.size() != s.vertexCount) return;
    if (mode == PAINT_SMOOTH) {
        smoothWeights(s, g, 1, 1.0f, &factor);
        return;
    }
    for (size_t p = 0; p < g.members.size(); ++p) {
        uint32_t v0 = g.members[p][0];
        float f = factor[v0];
        if (f <= 0) continue;
        float cur = s.w(v0, (size_t)influence), target = cur;
        if (mode == PAINT_ADD) target = cur + value * f;
        else if (mode == PAINT_SUBTRACT) target = cur - value * f;
        else target = lerpf(cur, value, clampf(f, 0, 1));
        for (uint32_t v : g.members[p]) setVertexWeight(s, v, influence, clampf(target, 0, 1), fallback);
    }
}

std::string mirrorJointName(const std::string& n) {
    static const std::pair<const char*, const char*> pairs[] = {
        {"Left", "Right"}, {"left", "right"}, {"_L", "_R"}, {"_l", "_r"}, {"L_", "R_"}, {"l_", "r_"}, {".L", ".R"}, {".l", ".r"}};
    for (auto& p : pairs) {
        for (int dir = 0; dir < 2; ++dir) {
            std::string a = dir ? p.second : p.first, b = dir ? p.first : p.second;
            size_t at = n.find(a);
            if (at != std::string::npos) return n.substr(0, at) + b + n.substr(at + a.size());
        }
    }
    return n;
}

int mirrorWeights(PrefabSkin& s, const PrefabAsset& a, const MeshData& mesh, bool positiveToNegative) {
    const size_t J = s.joints.size();
    if (mesh.vertices.size() != s.vertexCount || !J) return 0;
    // influence -> mirrored influence (by name)
    std::vector<int> swap(J);
    for (size_t k = 0; k < J; ++k) {
        swap[k] = (int)k;
        int ji = a.findJoint(s.joints[k]);
        if (ji < 0) continue;
        std::string m = mirrorJointName(a.joints[ji].name);
        for (size_t q = 0; q < J; ++q) {
            int qi = a.findJoint(s.joints[q]);
            if (qi >= 0 && a.joints[qi].name == m) swap[k] = (int)q;
        }
    }
    // mesh in bind (world) space
    std::vector<float3> P(s.vertexCount);
    for (uint32_t v = 0; v < s.vertexCount; ++v) P[v] = s.bindMesh.point(mesh.vertices[v].pos);
    std::vector<uint32_t> source;
    for (uint32_t v = 0; v < s.vertexCount; ++v)
        if (positiveToNegative ? P[v].x > 1e-4f : P[v].x < -1e-4f) source.push_back(v);
    int done = 0;
    std::vector<float> old = s.weights;
    for (uint32_t v = 0; v < s.vertexCount; ++v) {
        if (positiveToNegative ? P[v].x >= -1e-4f : P[v].x <= 1e-4f) continue;   // only the target side
        float3 m(-P[v].x, P[v].y, P[v].z);
        uint32_t best = UINT32_MAX;
        float bd = 1e30f;
        for (uint32_t q : source) {
            float3 d = P[q] - m;
            float dd = dot(d, d);
            if (dd < bd) { bd = dd; best = q; }
        }
        if (best == UINT32_MAX) continue;
        for (size_t k = 0; k < J; ++k) s.w(v, k) = 0;
        for (size_t k = 0; k < J; ++k) s.w(v, (size_t)swap[k]) += old[best * J + k];
        ++done;
    }
    return done;
}

} // namespace app
