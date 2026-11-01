#include "Mech.h"
#include <sstream>
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <regex>
#include <set>
#include <unordered_map>

namespace brickit {

using namespace vp3d;

// --------------------------------------------------------------------------- affine helpers

static Affine mul(const Affine& a, const Affine& b) {
    Affine r;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j) {
            float s = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
            if (j == 3) s += a.m[i][3];
            r.m[i][j] = s;
        }
    }
    return r;
}

// rotation of angle v around the line (p, u)
static Affine rotAbout(const float3& p, const float3& u, float v) {
    float4 q = qAxisAngle(u, v);
    Affine r = Affine::fromBasis(qRot(q, float3(1, 0, 0)), qRot(q, float3(0, 1, 0)), qRot(q, float3(0, 0, 1)), float3(0));
    float3 t = p - r.vector(p);
    r.m[0][3] = t.x; r.m[1][3] = t.y; r.m[2][3] = t.z;
    return r;
}

static bool parallel(const float3& a, const float3& b) { return std::fabs(std::fabs(dot(a, b)) - 1.0f) < 1e-3f; }
static bool onLine(const float3& p, const float3& p0, const float3& u, float tol = 0.05f) {
    float3 d = p - p0;
    d = d - u * dot(d, u);
    return dot(d, d) < tol * tol;
}

// --------------------------------------------------------------------------- connectors of a part

const MechMeta& Mech::meta(int part) {
    if ((int)m_meta.size() != (int)m_lib.parts.size()) { m_meta.assign(m_lib.parts.size(), MechMeta()); m_metaDone.assign(m_lib.parts.size(), false); }
    MechMeta& m = m_meta[part];
    if (m_metaDone[part]) return m;
    m_metaDone[part] = true;
    const Part& p = m_lib.parts[part];
    {   // turntable halves: "Technic Turntable 28 Tooth Bottom" / "... Top", "Turntable 5 x 5 Base" / "... Top"
        const std::string t = p.title.empty() ? p.label : p.title;
        if (t.find("Turntable") != std::string::npos) {
            std::string kind;
            int role = 0;
            std::istringstream ws(t);
            std::string w;
            while (ws >> w) {
                if (w == "Top") role = 2;
                else if (w == "Bottom" || w == "Base") role = 1;
                else kind += w + " ";
            }
            if (role && p.loaded) {
                m.pairRole = role;
                m.pairKind = kind;
                m.pairC = float3((p.bmin.x + p.bmax.x) * 0.5f, (p.bmin.y + p.bmax.y) * 0.5f, (p.bmin.z + p.bmax.z) * 0.5f);
                m.pairAxis = float3(0, 1, 0);
                m.pairLo = p.bmin.y - m.pairC.y;
                m.pairHi = p.bmax.y - m.pairC.y;
            }
        }
    }
    for (size_t i = 0; i < p.voxels.size(); ++i) {
        if (p.conn[i] == CONN_STUD) m.top.push_back(p.voxels[i]);
        else if (p.conn[i] == CONN_ANTISTUD) m.bottom.push_back(p.voxels[i]);
    }
    // pin / pin hole / axle / axle hole voxels on one line form one connector (axis = the line); holes are split per
    // 0.8 cm (one Technic hole width)
    struct Kind { uint8_t conn; bool hole, cross; };
    const Kind kinds[4] = {{CONN_PINHOLE, true, false}, {CONN_AXLEHOLE, true, true}, {CONN_PIN, false, false}, {CONN_AXLE, false, true}};
    for (const Kind& k : kinds) {
        std::vector<float3> pts;
        for (size_t i = 0; i < p.voxels.size(); ++i)
            if (p.conn[i] == k.conn) pts.push_back(p.voxels[i]);
        std::vector<bool> used(pts.size(), false);
        for (size_t a = 0; a < pts.size(); ++a) {
            if (used[a]) continue;
            std::vector<size_t> line{a};
            used[a] = true;
            int axis = -1;
            for (bool grow = true; grow;) {
                grow = false;
                for (size_t b = 0; b < pts.size() && !grow; ++b) {
                    if (used[b]) continue;
                    for (size_t c : line) {
                        float d[3] = {std::fabs(pts[b].x - pts[c].x), std::fabs(pts[b].y - pts[c].y), std::fabs(pts[b].z - pts[c].z)};
                        int nd = 0, kk = -1;
                        for (int j = 0; j < 3; ++j)
                            if (d[j] > 0.02f) { ++nd; kk = j; }
                        if (nd != 1 || d[kk] > 0.5f || (axis >= 0 && kk != axis)) continue;
                        axis = kk;
                        line.push_back(b);
                        used[b] = true;
                        grow = true;
                        break;
                    }
                }
            }
            if (axis < 0) axis = 2;   // a lone voxel: the usual Technic hole axis
            float lo = 1e30f, hi = -1e30f;
            for (size_t i : line) { float v = (&pts[i].x)[axis]; lo = std::min(lo, v); hi = std::max(hi, v); }
            float3 dir(0);
            (&dir.x)[axis] = 1;
            float3 base = pts[line[0]];
            const float len = hi - lo;
            if (!k.hole) {
                (&base.x)[axis] = (lo + hi) * 0.5f;
                m.males.push_back({base, dir, std::max(len * 0.5f, 0.2f), k.cross});
            } else {
                int n = std::max(1, (int)std::lround(len / 0.8f));
                float w = len / n;
                for (int s = 0; s < n; ++s) {
                    float3 q = base;
                    (&q.x)[axis] = lo + w * (s + 0.5f);
                    m.holes.push_back({q, dir, std::max(w * 0.5f, 0.2f), k.cross});
                }
            }
        }
    }
    // gears: "N Tooth" in the title, turning around the hole nearest to the centre of the part
    static const std::regex gearRe("Gear\\s+(\\d+)\\s+Tooth", std::regex::icase), toothRe("(\\d+)\\s+Tooth", std::regex::icase);
    if (!m.holes.empty() && std::regex_search(p.label, gearRe) && p.label.find("Rack") == std::string::npos) {
        float3 bc = (p.bmin + p.bmax) * 0.5f;
        auto hd = [&](const MechConn& h) {
            float3 d = h.p - bc;
            d = d - h.axis * dot(d, h.axis);
            return length(d);
        };
        const MechConn* h0 = &m.holes[0];
        for (const MechConn& h : m.holes)
            if (hd(h) < hd(*h0) - 1e-4f) h0 = &h;
        std::set<int> seen;
        for (std::sregex_iterator it(p.label.begin(), p.label.end(), toothRe), end; it != end; ++it) {
            int t = std::stoi((*it)[1].str());
            if (!seen.insert(t).second) continue;
            // centre: the hole along the axis, the middle of the part across it (some LDCad hole voxels sit off-centre)
            float3 c = bc;
            for (int j = 0; j < 3; ++j)
                if ((&h0->axis.x)[j] > 0.5f) (&c.x)[j] = (&h0->p.x)[j];
            float3 ex(1, 0, 0);
            float best = -1;
            for (const Vertex& v : p.mesh.vertices) {
                float3 r = v.pos - c;
                r = r - h0->axis * dot(r, h0->axis);
                float l = length(r);
                if (l > best) { best = l; ex = l > 1e-5f ? r * (1.0f / l) : float3(1, 0, 0); }
            }
            m.gears.push_back({t, c, h0->axis, ex});
        }
        MechConn keep = *h0;
        if (!m.gears.empty()) keep.p = m.gears[0].c;   // the axle goes through the centre
        m.holes.assign(1, keep);
    }
    return m;
}

// --------------------------------------------------------------------------- detection: bodies, joints, gears, tree

bool Mech::build(const std::vector<Brick>& bricks, MechModel& M) {
    M = MechModel();
    const int n = (int)bricks.size();
    if (!n) return false;
    std::vector<int> uf(n);
    for (int i = 0; i < n; ++i) uf[i] = i;
    std::function<int(int)> find = [&](int i) { while (uf[i] != i) { uf[i] = uf[uf[i]]; i = uf[i]; } return i; };
    auto unite = [&](int a, int b) { a = find(a); b = find(b); if (a != b) uf[b] = a; };
    auto wPos = [&](int i, const float3& v) { return qRot(bricks[i].rot, v) + bricks[i].pos; };
    auto wDir = [&](int i, const float3& v) { return normalize(qRot(bricks[i].rot, v)); };
    auto key = [](const float3& v) { return ((int64_t)std::lround(v.x * 50) * 73856093) ^ ((int64_t)std::lround(v.y * 50) * 19349663) ^ ((int64_t)std::lround(v.z * 50) * 83492791); };

    // 0) turntable halves (same family, same axis, touching along it) = pivot (web step 3; the two halves are not
    // fused by their studs: the top turns on the base)
    struct Rev { int i, j; float3 p, u; };
    std::vector<Rev> revs;
    std::set<std::pair<int, int>> pairs;
    for (int i = 0; i < n; ++i) {
        const MechMeta& c = meta(bricks[i].part);
        if (c.pairRole != 1) continue;
        const float3 pc = wPos(i, c.pairC), u = wDir(i, c.pairAxis);
        for (int j = 0; j < n; ++j) {
            const MechMeta& ct = meta(bricks[j].part);
            if (j == i || ct.pairRole != 2 || ct.pairKind != c.pairKind) continue;
            const float3 tc = wPos(j, ct.pairC), ut = wDir(j, ct.pairAxis);
            if (!parallel(u, ut) || !onLine(tc, pc, u)) continue;
            // extents along the axis must touch or overlap (the web compares the two centres within 1 mm, which two
            // halves of different heights never satisfy)
            const float d = dot(tc - pc, u);
            const float aLo = c.pairLo, aHi = c.pairHi;
            const float s = dot(ut, u) > 0 ? 1.0f : -1.0f;
            const float bLo = d + std::min(s * ct.pairLo, s * ct.pairHi), bHi = d + std::max(s * ct.pairLo, s * ct.pairHi);
            if (bLo > aHi + 0.1f || bHi < aLo - 0.1f) continue;
            revs.push_back({i, j, pc, u});
            pairs.insert({std::min(i, j), std::max(i, j)});
        }
    }
    // 1) a stud of one brick in an anti-stud of another = rigid
    std::unordered_map<int64_t, std::vector<int>> tops;
    for (int i = 0; i < n; ++i)
        for (const float3& v : meta(bricks[i].part).top) tops[key(wPos(i, v))].push_back(i);
    for (int i = 0; i < n; ++i)
        for (const float3& v : meta(bricks[i].part).bottom) {
            auto it = tops.find(key(wPos(i, v)));
            if (it != tops.end())
                for (int j : it->second)
                    if (j != i && !pairs.count({std::min(i, j), std::max(i, j)})) unite(i, j);
        }
    // 2) axles and pins through holes
    struct WH { int i; float3 p, u; bool cross; float half; };
    std::vector<WH> holes, males;
    for (int i = 0; i < n; ++i) {
        const MechMeta& mt = meta(bricks[i].part);
        for (const MechConn& h : mt.holes) holes.push_back({i, wPos(i, h.p), wDir(i, h.axis), h.cross, h.half});
        for (const MechConn& h : mt.males) males.push_back({i, wPos(i, h.p), wDir(i, h.axis), h.cross, h.half});
    }
    for (const WH& m : males) {
        std::vector<const WH*> hit;
        for (const WH& h : holes) {
            if (h.i == m.i || !parallel(h.u, m.u)) continue;
            float t = dot(h.p - m.p, m.u);
            if (std::fabs(t) <= m.half - 0.15f + 1e-4f && onLine(h.p, m.p, m.u)) hit.push_back(&h);
        }
        if (hit.empty()) continue;
        if (m.cross) {   // axle: rigid in a cross hole, pivot in a round hole
            for (const WH* h : hit) {
                if (h->cross) unite(m.i, h->i);
                else revs.push_back({m.i, h->i, h->p, m.u});
            }
        } else {   // pin: rides with the first part, pivot between it and the others
            unite(m.i, hit[0]->i);
            for (size_t a = 1; a < hit.size(); ++a) revs.push_back({hit[0]->i, hit[a]->i, hit[a]->p, m.u});
        }
    }
    // 3) two non-coaxial pivots between the same two bodies lock them
    std::map<std::pair<int, int>, std::vector<Rev>> groups;
    for (int guard = 0; guard < 50; ++guard) {
        groups.clear();
        for (const Rev& r : revs) {
            int a = find(r.i), b = find(r.j);
            if (a == b) continue;
            groups[{std::min(a, b), std::max(a, b)}].push_back(r);
        }
        bool changed = false;
        for (auto& g : groups) {
            const Rev& r0 = g.second[0];
            for (const Rev& r : g.second)
                if (!parallel(r.u, r0.u) || !onLine(r.p, r0.p, r0.u)) { unite(r0.i, r0.j); changed = true; break; }
        }
        if (!changed) break;
    }
    // bodies
    std::map<int, int> bodyOf;
    std::vector<float> minY;
    std::vector<bool> structural;
    M.brickBody.assign(n, 0);
    for (int i = 0; i < n; ++i) {
        int r = find(i);
        auto it = bodyOf.find(r);
        int b;
        if (it == bodyOf.end()) { b = (int)M.bodyBricks.size(); bodyOf[r] = b; M.bodyBricks.push_back(0); minY.push_back(1e30f); structural.push_back(false); }
        else b = it->second;
        M.brickBody[i] = b;
        ++M.bodyBricks[b];
        const Part& p = m_lib.parts[bricks[i].part];
        for (int c = 0; c < 8; ++c) {
            float3 v((c & 1) ? p.bmax.x : p.bmin.x, (c & 2) ? p.bmax.y : p.bmin.y, (c & 4) ? p.bmax.z : p.bmin.z);
            minY[b] = std::min(minY[b], wPos(i, v).y);
        }
        const MechMeta& mt = meta(bricks[i].part);
        if (!mt.top.empty() || !mt.bottom.empty()) structural[b] = true;
    }
    const int nb = (int)M.bodyBricks.size();
    M.bodyCount = nb;
    std::vector<MechJoint> joints;
    for (auto& g : groups) {
        const Rev& r0 = g.second[0];
        int a = M.brickBody[r0.i], b = M.brickBody[r0.j];
        if (a != b) joints.push_back({a, b, r0.p, normalize(r0.u), r0.p + normalize(r0.u)});
    }
    // gears and meshes
    for (int i = 0; i < n; ++i)
        for (const MechGear& g : meta(bricks[i].part).gears)
            M.gears.push_back({i, M.brickBody[i], g.teeth, wPos(i, g.c), wDir(i, g.axis), wDir(i, g.ex), 0.0f, (int)(&g - meta(bricks[i].part).gears.data())});
    for (size_t x = 0; x < M.gears.size(); ++x)
        for (size_t y = x + 1; y < M.gears.size(); ++y) {
            const MechGearW &A = M.gears[x], &B = M.gears[y];
            if (A.body == B.body || !parallel(A.u, B.u)) continue;
            float3 d = B.c - A.c;
            float ax = dot(d, A.u);
            if (std::fabs(ax) > 0.3f) continue;
            float dist = length(d - A.u * ax), want = (A.teeth + B.teeth) * 0.05f;
            char buf[160];
            if (std::fabs(dist - want) < 0.105f) M.meshes.push_back({(int)x, (int)y});
            else if (std::fabs(dist - want) < 0.8f) {
                snprintf(buf, sizeof(buf), "Engrenages %d/%d pas en prise : entraxe %.2f cm, il faut %.2f cm", A.teeth, B.teeth, dist, want);
                M.warnings.push_back(buf);
            }
        }

    // tooth phases: from the biggest gear of each train, every meshing gear turned so its teeth fall between the teeth
    // of the gear it meshes with (spin around its own axis, applied to its brick at rest)
    {
        const int ng = (int)M.gears.size();
        std::vector<std::vector<int>> gadj0(ng);
        for (auto& ms : M.meshes) { gadj0[ms.first].push_back(ms.second); gadj0[ms.second].push_back(ms.first); }
        auto angAbout = [](const float3& dir, const float3& ex, const float3& u) { return std::atan2(dot(dir, cross(u, ex)), dot(dir, ex)); };
        auto frac = [](float v) { return v - std::floor(v); };
        std::vector<bool> vis(ng, false);
        std::vector<int> order(ng);
        for (int i = 0; i < ng; ++i) order[i] = i;
        std::sort(order.begin(), order.end(), [&](int a, int b) { return M.gears[a].teeth > M.gears[b].teeth; });
        for (int s0 : order) {
            if (vis[s0]) continue;
            vis[s0] = true;
            M.gears[s0].spin = 0;
            std::vector<int> st{s0};
            while (!st.empty()) {
                int a = st.back();
                st.pop_back();
                const MechGearW& A = M.gears[a];
                for (int b : gadj0[a]) {
                    if (vis[b]) continue;
                    MechGearW& B = M.gears[b];
                    const float3 u = A.u, d = B.c - A.c;
                    const float sgn = dot(B.u, u) < 0 ? -1.0f : 1.0f;
                    const float aA = angAbout(d, A.ex, u), aB = angAbout(d * -1.0f, B.ex, u);
                    const float eA = frac((aA - A.spin) * A.teeth / (2 * kPi));
                    const float phiBu = aB + (eA - 0.5f) * 2 * kPi / B.teeth;
                    B.spin = sgn * phiBu;
                    vis[b] = true;
                    st.push_back(b);
                }
            }
        }
        for (const MechGearW& g : M.gears) {
            if (g.spin == 0) continue;
            const MechGear& lg = meta(bricks[g.brick].part).gears[g.local];
            M.spins.push_back({g.brick, rotAbout(lg.c, lg.axis, g.spin)});
        }
    }

    // kinematic tree. Ground: per connected group, a structural body (studs), the lowest, the biggest
    std::vector<int> comp(nb);
    for (int i = 0; i < nb; ++i) comp[i] = i;
    std::function<int(int)> cf = [&](int i) { while (comp[i] != i) { comp[i] = comp[comp[i]]; i = comp[i]; } return i; };
    for (const MechJoint& j : joints) comp[cf(j.a)] = cf(j.b);
    std::map<int, int> rootOf;
    for (int i = 0; i < nb; ++i) {
        int c = cf(i);
        auto it = rootOf.find(c);
        if (it == rootOf.end()) { rootOf[c] = i; continue; }
        int r = it->second;
        bool better = structural[i] > structural[r] ||
                      (structural[i] == structural[r] && (minY[i] < minY[r] - 0.05f || (std::fabs(minY[i] - minY[r]) <= 0.05f && M.bodyBricks[i] > M.bodyBricks[r])));
        if (better) it->second = i;
    }
    for (auto& kv : rootOf) M.roots.push_back(kv.second);
    // spanning tree: pivots of gear bodies first; the joints left close loops
    std::vector<bool> hasGear(nb, false);
    for (const MechGearW& g : M.gears) hasGear[g.body] = true;
    std::vector<int> order(joints.size());
    for (size_t k = 0; k < joints.size(); ++k) order[k] = (int)k;
    std::stable_sort(order.begin(), order.end(), [&](int x, int y) {
        int px = hasGear[joints[x].a] || hasGear[joints[x].b] ? 0 : 1, py = hasGear[joints[y].a] || hasGear[joints[y].b] ? 0 : 1;
        return px < py;
    });
    std::vector<int> tf(nb);
    for (int i = 0; i < nb; ++i) tf[i] = i;
    std::function<int(int)> tfind = [&](int i) { while (tf[i] != i) { tf[i] = tf[tf[i]]; i = tf[i]; } return i; };
    std::vector<bool> inTree(joints.size(), false);
    for (int k : order) {
        int a = tfind(joints[k].a), b = tfind(joints[k].b);
        if (a != b) { tf[a] = b; inTree[k] = true; }
        else M.closures.push_back(joints[k]);
    }
    std::vector<std::vector<int>> adj(nb);
    for (size_t k = 0; k < joints.size(); ++k)
        if (inTree[k]) { adj[joints[k].a].push_back((int)k); adj[joints[k].b].push_back((int)k); }
    std::vector<int> tjOf(nb, -1);
    std::vector<bool> seenB(nb, false);
    for (int r : M.roots) {
        std::vector<int> queue{r};
        seenB[r] = true;
        for (size_t qi = 0; qi < queue.size(); ++qi) {
            int p = queue[qi];
            for (int k : adj[p]) {
                const MechJoint& j = joints[k];
                int c = j.a == p ? j.b : j.a;
                if (seenB[c]) continue;
                seenB[c] = true;
                queue.push_back(c);
                M.tree.push_back({j, p, c, (int)M.tree.size()});
                tjOf[c] = (int)M.tree.size() - 1;
            }
        }
    }
    // a part turning on an axle that itself turns in the same line: its angle is measured from the part carrying the axle
    for (MechTreeJ& t : M.tree)
        for (int tp = tjOf[t.parent]; tp >= 0 && parallel(M.tree[tp].j.u, t.j.u) && onLine(t.j.p, M.tree[tp].j.p, M.tree[tp].j.u); tp = tjOf[t.parent])
            t.parent = M.tree[tp].parent;
    const int nq = (int)M.tree.size();
    M.qmode.assign(nq, MechModel::QMode());
    // gear relations q_b = k q_a (spur gears: opposite direction, ratio of teeth)
    struct Rel { int a, b; float k; };
    std::vector<Rel> rel;
    for (auto& ms : M.meshes) {
        const MechGearW &A = M.gears[ms.first], &B = M.gears[ms.second];
        int tA = tjOf[A.body], tB = tjOf[B.body];
        if (tA < 0 || tB < 0 || M.tree[tA].parent != M.tree[tB].parent || !parallel(M.tree[tA].j.u, A.u) || !parallel(M.tree[tB].j.u, B.u)) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Engrenages %d/%d : axes non portés par la même pièce, ignorés", A.teeth, B.teeth);
            M.warnings.push_back(buf);
            continue;
        }
        float s = dot(M.tree[tA].j.u, M.tree[tB].j.u) < 0 ? -1.0f : 1.0f;
        rel.push_back({tA, tB, -((float)A.teeth / B.teeth) * s});
    }
    std::map<int, std::vector<std::pair<int, float>>> gadj;
    for (const Rel& r : rel) { gadj[r.a].push_back({r.b, r.k}); gadj[r.b].push_back({r.a, 1.0f / r.k}); }
    auto teethOf = [&](int qi) {
        int t = 1 << 30;
        for (const MechGearW& g : M.gears)
            if (g.body == M.tree[qi].child) t = std::min(t, g.teeth);
        return t;
    };
    std::set<int> done;
    for (auto& kv : gadj) {
        int start = kv.first;
        if (done.count(start)) continue;
        std::map<int, float> kk{{start, 1.0f}};
        std::vector<int> st{start}, compQ;
        bool bad = false;
        while (!st.empty()) {
            int x = st.back();
            st.pop_back();
            compQ.push_back(x);
            for (auto& e : gadj[x]) {
                float v = kk[x] * e.second;
                auto it = kk.find(e.first);
                if (it == kk.end()) { kk[e.first] = v; st.push_back(e.first); }
                else if (std::fabs(it->second - v) > 1e-4f) bad = true;
            }
        }
        for (int x : compQ) done.insert(x);
        if (bad) {
            for (int x : compQ) M.qmode[x].kind = 3;
            M.warnings.push_back("Train d'engrenages bloqué (rapports incompatibles)");
            continue;
        }
        int master = compQ[0];
        for (int x : compQ)
            if (teethOf(x) < teethOf(master)) master = x;
        float km = kk[master];
        MechCmd c;
        c.name = "Moteur";
        c.qi = master;
        for (int x : compQ)
            if (x != master) { M.qmode[x] = {2, master, kk[x] / km, -1}; c.ratio.push_back({x, kk[x] / km}); }
        M.qmode[master] = {1, -1, 0, (int)M.cmds.size()};
        M.cmds.push_back(c);
    }
    // an axle carrying gears that turns alone in a fixed part also gets a motor
    for (int qi = 0; qi < nq; ++qi)
        if (M.qmode[qi].kind == 0 && hasGear[M.tree[qi].child]) {
            MechCmd c;
            c.name = "Axe";
            c.qi = qi;
            M.qmode[qi] = {1, -1, 0, (int)M.cmds.size()};
            M.cmds.push_back(c);
        }
    int motors = 0;
    for (MechCmd& c : M.cmds) if (c.name == "Moteur") ++motors;
    int k = 0;
    static const ImGuiKey kp[8] = {ImGuiKey_A, ImGuiKey_Z, ImGuiKey_E, ImGuiKey_R, ImGuiKey_T, ImGuiKey_Y, ImGuiKey_U, ImGuiKey_I};
    static const ImGuiKey km_[8] = {ImGuiKey_Q, ImGuiKey_S, ImGuiKey_D, ImGuiKey_F, ImGuiKey_G, ImGuiKey_H, ImGuiKey_J, ImGuiKey_K};
    for (MechCmd& c : M.cmds) {
        if (motors > 1 && c.name == "Moteur") c.name = "Moteur " + std::to_string(k + 1);
        c.keyPlus = k < 8 ? (int)kp[k] : 0;
        c.keyMinus = k < 8 ? (int)km_[k] : 0;
        ++k;
    }
    M.q.assign(nq, 0.0);
    M.mats.assign(nb, Affine());
    fk(M, M.q, M.mats);
    // degrees of freedom: independent joint variables minus the rank of the loop equations
    std::vector<int> vars;
    for (int i = 0; i < nq; ++i)
        if (M.qmode[i].kind == 0 || M.qmode[i].kind == 1) vars.push_back(i);
    const int rows = (int)M.closures.size() * 6;
    std::vector<double> r0(rows), r1(rows);
    std::vector<std::vector<double>> J(vars.size(), std::vector<double>(rows));
    std::vector<double> q(nq, 0.0);
    std::vector<Affine> mats(nb);
    applyGears(M, q); fk(M, q, mats); closureRes(M, mats, r0.data());
    for (size_t c = 0; c < vars.size(); ++c) {
        q[vars[c]] = 1e-3;
        applyGears(M, q); fk(M, q, mats); closureRes(M, mats, r1.data());
        for (int r = 0; r < rows; ++r) J[c][r] = (r1[r] - r0[r]) / 1e-3;
        q[vars[c]] = 0;
    }
    int rank = 0;
    for (int r = 0; r < rows && rank < (int)vars.size(); ++r) {
        int best = -1;
        double bv = 1e-5;
        for (int c = rank; c < (int)vars.size(); ++c)
            if (std::fabs(J[c][r]) > bv) { bv = std::fabs(J[c][r]); best = c; }
        if (best < 0) continue;
        std::swap(J[rank], J[best]);
        for (int c = rank + 1; c < (int)vars.size(); ++c) {
            double f = J[c][r] / J[rank][r];
            for (int kq = r; kq < rows; ++kq) J[c][kq] -= f * J[rank][kq];
        }
        ++rank;
    }
    M.dof = (int)vars.size() - rank;
    return true;
}

// --------------------------------------------------------------------------- kinematics

void Mech::fk(const MechModel& m, const std::vector<double>& q, std::vector<Affine>& mats) const {
    for (int r : m.roots) mats[r] = Affine();
    for (const MechTreeJ& t : m.tree) mats[t.child] = mul(mats[t.parent], rotAbout(t.j.p, t.j.u, (float)q[t.qi]));
}

void Mech::applyGears(const MechModel& m, std::vector<double>& q) const {
    for (size_t i = 0; i < q.size(); ++i) {
        const MechModel::QMode& md = m.qmode[i];
        if (md.kind == 2) q[i] = md.k * q[md.master];
        else if (md.kind == 3) q[i] = 0;
    }
}

int Mech::closureRes(const MechModel& m, const std::vector<Affine>& mats, double* out) const {
    int k = 0;
    for (const MechJoint& c : m.closures)
        for (const float3& pt : {c.p, c.p2}) {
            float3 a = mats[c.a].point(pt), b = mats[c.b].point(pt);
            out[k++] = a.x - b.x;
            out[k++] = a.y - b.y;
            out[k++] = a.z - b.z;
        }
    return k;
}

static std::vector<double> solveLinear(std::vector<std::vector<double>> A, std::vector<double> b) {
    const int n = (int)b.size();
    for (int i = 0; i < n; ++i) {
        int p = i;
        for (int r = i + 1; r < n; ++r)
            if (std::fabs(A[r][i]) > std::fabs(A[p][i])) p = r;
        std::swap(A[i], A[p]);
        std::swap(b[i], b[p]);
        double d = A[i][i] != 0 ? A[i][i] : 1e-12;
        for (int r = i + 1; r < n; ++r) {
            double f = A[r][i] / d;
            if (f == 0) continue;
            for (int c = i; c < n; ++c) A[r][c] -= f * A[i][c];
            b[r] -= f * b[i];
        }
    }
    std::vector<double> x(n, 0.0);
    for (int i = n - 1; i >= 0; --i) {
        double s = b[i];
        for (int c = i + 1; c < n; ++c) s -= A[i][c] * x[c];
        x[i] = s / (A[i][i] != 0 ? A[i][i] : 1e-12);
    }
    return x;
}

// Commands set their joints, the free joints are solved (Levenberg-Marquardt) so that every loop stays closed, pulled
// towards the previous pose. On failure the previous pose stays (a stop).
bool Mech::solve(MechModel& m, const Drag* drag) {
    const int nq = (int)m.q.size();
    std::vector<double> prev = m.q;
    std::vector<int> freeV;
    for (int i = 0; i < nq; ++i) {
        const MechModel::QMode& md = m.qmode[i];
        if (md.kind == 1 && !drag) m.q[i] = m.cmds[md.cmd].value;
        else if (md.kind == 0 || md.kind == 1) freeV.push_back(i);   // a drag moves the commands too
    }
    const double WD = 0.35;
    const int rowsC = (int)m.closures.size() * 6, nf = (int)freeV.size(), rowsD = drag ? 3 : 0, rows = rowsC + rowsD + nf;
    const double LAM = 0.01, TOL = 0.004;
    std::vector<Affine> mats(m.bodyCount);
    std::vector<double> xRef(nf);
    for (int k = 0; k < nf; ++k) xRef[k] = prev[freeV[k]];
    auto evalR = [&](const std::vector<double>& x, std::vector<double>& out) {
        for (int k = 0; k < nf; ++k) m.q[freeV[k]] = x[k];
        applyGears(m, m.q);
        fk(m, m.q, mats);
        int k = closureRes(m, mats, out.data());
        if (drag) {
            float3 p = mats[drag->body].point(drag->local);
            out[k++] = (p.x - drag->target.x) * WD;
            out[k++] = (p.y - drag->target.y) * WD;
            out[k++] = (p.z - drag->target.z) * WD;
        }
        for (int j = 0; j < nf; ++j) out[k++] = LAM * (x[j] - xRef[j]);
        double s = 0;
        for (int r = 0; r < rows; ++r) s += out[r] * out[r];
        return s;
    };
    auto closureMax = [&](const std::vector<double>& r) { double mx = 0; for (int k = 0; k < rowsC; ++k) mx = std::max(mx, std::fabs(r[k])); return mx; };
    std::vector<double> x = xRef, r(rows), rj(rows);
    double cost = evalR(x, r);
    if (nf > 0) {
        double mu = 1e-3;
        std::vector<std::vector<double>> J(nf, std::vector<double>(rows));
        for (int it = 0; it < 30; ++it) {
            if (!drag && closureMax(r) < 1e-4) break;
            for (int c = 0; c < nf; ++c) {
                std::vector<double> xs = x;
                xs[c] += 1e-6;
                evalR(xs, rj);
                for (int k = 0; k < rows; ++k) J[c][k] = (rj[k] - r[k]) / 1e-6;
            }
            std::vector<std::vector<double>> A(nf, std::vector<double>(nf, 0.0));
            std::vector<double> g(nf, 0.0);
            for (int a = 0; a < nf; ++a) {
                for (int k = 0; k < rows; ++k) g[a] += J[a][k] * r[k];
                for (int b = a; b < nf; ++b) {
                    double t = 0;
                    for (int k = 0; k < rows; ++k) t += J[a][k] * J[b][k];
                    A[a][b] = A[b][a] = t;
                }
            }
            bool improved = false;
            for (int tries = 0; tries < 8 && !improved; ++tries) {
                std::vector<std::vector<double>> Am = A;
                for (int i = 0; i < nf; ++i) Am[i][i] = Am[i][i] * (1 + mu) + 1e-9;
                std::vector<double> ng(nf);
                for (int i = 0; i < nf; ++i) ng[i] = -g[i];
                std::vector<double> dx = solveLinear(Am, ng), xn(nf), rn(rows);
                for (int k = 0; k < nf; ++k) xn[k] = x[k] + dx[k];
                double cn = evalR(xn, rn);
                if (cn < cost) { x = xn; r = rn; cost = cn; mu = std::max(mu * 0.3, 1e-9); improved = true; }
                else mu *= 10;
            }
            if (!improved || cost < 1e-14) break;
        }
    }
    evalR(x, r);
    if (closureMax(r) > TOL) {
        m.q = prev;
        applyGears(m, m.q);
        fk(m, m.q, m.mats);
        return false;
    }
    applyGears(m, m.q);
    fk(m, m.q, m.mats);
    if (drag)
        for (int i : freeV)
            if (m.qmode[i].kind == 1) m.cmds[m.qmode[i].cmd].value = (float)m.q[i];
    return true;
}

Affine Mech::brickMatrix(const MechModel& m, const Brick& b, int brick) const {
    Affine a = mul(m.mats[m.brickBody[brick]], Affine::trsQuat(b.pos, b.rot, float3(1)));
    for (auto& s : m.spins)
        if (s.first == brick) a = mul(a, s.second);
    return a;
}

} // namespace brickit
