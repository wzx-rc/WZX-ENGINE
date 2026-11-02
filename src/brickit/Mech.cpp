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

static bool parallel(const float3& a, const float3& b) { return std::fabs(std::fabs(dot(a, b)) - 1.0f) < 2e-3f; }
static bool onLine(const float3& p, const float3& p0, const float3& u, float tol = 0.08f) {
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
        if (k.hole) {
            // a hole given by its two ends on opposite faces of the part (beams: one point on each face, 0.8 apart, the
            // same spacing as the next hole along the beam): paired across the part first, along that axis
            for (size_t a = 0; a < pts.size(); ++a) {
                if (used[a]) continue;
                for (size_t b = a + 1; b < pts.size(); ++b) {
                    if (used[b]) continue;
                    float d[3] = {std::fabs(pts[b].x - pts[a].x), std::fabs(pts[b].y - pts[a].y), std::fabs(pts[b].z - pts[a].z)};
                    int nd = 0, kk = -1;
                    for (int j = 0; j < 3; ++j)
                        if (d[j] > 0.02f) { ++nd; kk = j; }
                    if (nd != 1 || d[kk] < 0.3f || d[kk] > 1.7f) continue;
                    const float lo = (&p.bmin.x)[kk], hi = (&p.bmax.x)[kk];
                    const float va = (&pts[a].x)[kk], vb = (&pts[b].x)[kk];
                    auto onFace = [&](float v) { return std::fabs(v - lo) < 0.06f || std::fabs(v - hi) < 0.06f; };
                    if (!onFace(va) || !onFace(vb)) continue;
                    float3 c = (pts[a] + pts[b]) * 0.5f, dir(0);
                    (&dir.x)[kk] = 1;
                    const float len = std::fabs(vb - va);
                    const int nh = std::max(1, (int)std::lround(len / 0.8f));
                    for (int sgm = 0; sgm < nh; ++sgm) {
                        float3 q = c;
                        (&q.x)[kk] = std::min(va, vb) + len / nh * (sgm + 0.5f);
                        m.holes.push_back({q, dir, std::max(len / nh * 0.5f, 0.2f), k.cross});
                    }
                    used[a] = used[b] = true;
                    for (size_t e = 0; e < pts.size(); ++e) {   // the points between the two ends belong to that hole
                        if (used[e]) continue;
                        float3 de = pts[e] - pts[a];
                        float t = (&de.x)[kk];
                        (&de.x)[kk] = 0;
                        if (length(de) < 0.02f && t * (vb - va) > 0 && std::fabs(t) < std::fabs(vb - va)) used[e] = true;
                    }
                    break;
                }
            }
        }
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
    // Technic beams: the converter types some holes (bent liftarms 32526 / 32140 / 32271..., the end hole of the beam
    // 11) as anti-studs on a face, at the hole centre: each one is also a round pin hole across the part
    if (m.top.empty() && (p.label.find("Technic Beam") != std::string::npos || p.label.find("Liftarm") != std::string::npos) && p.loaded) {
        bool added = false;
        for (const float3& v : m.bottom) {
            int kk = -1;
            for (int j = 0; j < 3; ++j) {
                const float lo = (&p.bmin.x)[j], hi = (&p.bmax.x)[j];
                if (hi - lo <= 1.7f && (std::fabs((&v.x)[j] - lo) < 0.06f || std::fabs((&v.x)[j] - hi) < 0.06f)) { kk = j; break; }
            }
            if (kk < 0) continue;
            float3 c = v, dir(0);
            (&c.x)[kk] = ((&p.bmin.x)[kk] + (&p.bmax.x)[kk]) * 0.5f;
            (&dir.x)[kk] = 1;
            bool dup = false;
            for (const MechConn& h : m.holes) dup |= length(h.p - c) < 0.1f;
            if (dup) continue;
            m.holes.push_back({c, dir, ((&p.bmax.x)[kk] - (&p.bmin.x)[kk]) * 0.5f, false});
            added = true;
        }
        if (added)   // in order along the part, as the holes found from their pin voxels
            std::stable_sort(m.holes.begin(), m.holes.end(), [](const MechConn& a, const MechConn& b) {
                return a.p.z < b.p.z - 0.05f || (std::fabs(a.p.z - b.p.z) <= 0.05f && (a.p.x < b.p.x - 0.05f || (std::fabs(a.p.x - b.p.x) <= 0.05f && a.p.y < b.p.y - 0.05f)));
            });
    }
    // a "male" line that runs across the part's own holes (beams: the middles of the holes typed as pins) is no male
    m.males.erase(std::remove_if(m.males.begin(), m.males.end(), [&](const MechConn& ml) {
        int crossed = 0;
        for (const MechConn& h : m.holes) {
            if (parallel(h.axis, ml.axis)) continue;
            float3 d = h.p - ml.p;
            d = d - ml.axis * dot(d, ml.axis);
            if (length(d) < 0.08f && std::fabs(dot(h.p - ml.p, ml.axis)) <= ml.half + 0.05f) ++crossed;
        }
        return crossed >= 2;
    }), m.males.end());
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
            const bool bevel = p.label.find("Bevel") != std::string::npos || p.label.find("Crown") != std::string::npos;
            m.gears.push_back({t, c, h0->axis, ex, bevel ? 1 : 0, 0.0f});
        }
        MechConn keep = *h0;
        if (!m.gears.empty()) keep.p = m.gears[0].c;   // the axle goes through the centre
        m.holes.assign(1, keep);
    }
    // worm gears ("Worm Gear", not Duplo): one start, turning on their axle hole, radius = the farthest vertex
    if (!m.holes.empty() && m.gears.empty() && p.label.find("Worm Gear") != std::string::npos && p.label.find("Duplo") == std::string::npos) {
        const MechConn& h0 = m.holes[0];
        float3 c = (p.bmin + p.bmax) * 0.5f, ex(1, 0, 0);
        for (int j = 0; j < 3; ++j)
            if ((&h0.axis.x)[j] > 0.5f) (&c.x)[j] = (&h0.p.x)[j] * 0 + (&c.x)[j];
        float best = 0;
        for (const Vertex& v : p.mesh.vertices) {
            float3 r = v.pos - c;
            r = r - h0.axis * dot(r, h0.axis);
            float l = length(r);
            if (l > best) { best = l; ex = l > 1e-5f ? r * (1.0f / l) : float3(1, 0, 0); }
        }
        m.gears.push_back({1, c, h0.axis, ex, 2, best});
    }
    // racks ("Gear Rack", not the steering rack): a row of teeth along the longest side of the part
    if (m.gears.empty() && p.label.find("Gear Rack") != std::string::npos && p.loaded) {
        const float3 ext = p.bmax - p.bmin;
        int ax = 0;
        for (int j = 1; j < 3; ++j) if ((&ext.x)[j] > (&ext.x)[ax]) ax = j;
        float3 dir(0);
        (&dir.x)[ax] = 1;
        m.gears.push_back({0, (p.bmin + p.bmax) * 0.5f, dir, float3(0), 3, (&ext.x)[ax] * 0.5f});
    }
    // linear actuators: body and rod halves, axis = the longest side (from the mesh: the converter gives no slide)
    {
        const std::string& t = p.label;
        int role = 0;
        std::string kind;
        if (t.find("Shock Absorber") != std::string::npos && t.find("Cylinder") != std::string::npos) { role = 1; kind = "shock"; }
        else if (t.find("Shock Absorber") != std::string::npos && t.find("Piston Rod") != std::string::npos) { role = 2; kind = "shock"; }
        else if (t.find("Linear Actuator") != std::string::npos && t.find("Axle Actuator") != std::string::npos) { role = 1; kind = "linear"; }
        else if (t.find("Linear Actuator") != std::string::npos && t.find("Piston") != std::string::npos) { role = 2; kind = "linear"; }
        if (role && p.loaded) {
            const float3 ext = p.bmax - p.bmin;
            int ax = 0;
            for (int j = 1; j < 3; ++j) if ((&ext.x)[j] > (&ext.x)[ax]) ax = j;
            m.cylRole = role;
            m.cylKind = kind;
            m.cylC = (p.bmin + p.bmax) * 0.5f;
            m.cylAxis = float3(0);
            (&m.cylAxis.x)[ax] = 1;
            m.cylLen = (&ext.x)[ax];
            // the axis goes through the end pin hole (across the axis), not the box centre: a side tab shifts the box
            // (2909: 0.45 cm), and an actuator pinned by its ends was never seen coaxial with its rod
            int best = -1;
            float bv = 0;
            for (size_t i = 0; i < m.holes.size(); ++i) {
                if (std::fabs(dot(m.holes[i].axis, m.cylAxis)) > 0.1f) continue;
                const float v = std::fabs(dot(m.holes[i].p - m.cylC, m.cylAxis));
                if (best < 0 || v > bv) { best = (int)i; bv = v; }
            }
            if (best >= 0) m.cylC = m.holes[best].p + m.cylAxis * dot(m.cylC - m.holes[best].p, m.cylAxis);
        }
    }
    // toothed turntables ("Turntable 28 Tooth Top"): a gear on the turntable axis, driven at 90 degrees
    {
        static const std::regex ttRe("Turntable\\s+(\\d+)\\s+Tooth", std::regex::icase);
        std::smatch mt;
        if (m.gears.empty() && m.pairRole == 2 && std::regex_search(p.label, mt, ttRe)) {
            float3 ex(1, 0, 0);
            m.gears.push_back({std::stoi(mt[1].str()), m.pairC, m.pairAxis, ex, 1, 0.0f});
        }
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
    // (any orientation: studs on the side, upside down, SNOT; a stud and an anti-stud within 0.06 cm, searched in the
    // 27 cells of 0.1 cm around, robust to the small errors of position of turned parts)
    auto cellKey = [](int x, int y, int z) { return ((int64_t)x * 73856093) ^ ((int64_t)y * 19349663) ^ ((int64_t)z * 83492791); };
    struct TopPt { float3 p; int i; };
    std::unordered_map<int64_t, std::vector<TopPt>> tops;
    for (int i = 0; i < n; ++i)
        for (const float3& v : meta(bricks[i].part).top) {
            const float3 w = wPos(i, v);
            tops[cellKey((int)std::floor(w.x * 10), (int)std::floor(w.y * 10), (int)std::floor(w.z * 10))].push_back({w, i});
        }
    for (int i = 0; i < n; ++i)
        for (const float3& v : meta(bricks[i].part).bottom) {
            const float3 w = wPos(i, v);
            const int cx = (int)std::floor(w.x * 10), cy = (int)std::floor(w.y * 10), cz = (int)std::floor(w.z * 10);
            for (int dz = -1; dz <= 1; ++dz)
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        auto it = tops.find(cellKey(cx + dx, cy + dy, cz + dz));
                        if (it == tops.end()) continue;
                        for (const TopPt& t : it->second)
                            if (t.i != i && length(t.p - w) < 0.06f && !pairs.count({std::min(i, t.i), std::max(i, t.i)})) unite(i, t.i);
                    }
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
            M.gears.push_back({i, M.brickBody[i], g.teeth, wPos(i, g.c), wDir(i, g.axis), wDir(i, g.ex), 0.0f, (int)(&g - meta(bricks[i].part).gears.data()), g.kind, g.radius});
    for (size_t x = 0; x < M.gears.size(); ++x)
        for (size_t y = x + 1; y < M.gears.size(); ++y) {
            const MechGearW &A = M.gears[x], &B = M.gears[y];
            if (A.body == B.body) continue;
            if (A.kind == 3 || B.kind == 3) {   // rack and pinion: the pinion axis across the rack, its centre over the row
                if (A.kind == 3 && B.kind == 3) continue;
                const MechGearW& R = A.kind == 3 ? A : B;
                const MechGearW& P = A.kind == 3 ? B : A;
                if (P.kind == 2 || std::fabs(dot(P.u, R.u)) > 0.05f) continue;
                const float3 d = P.c - R.c;
                const float t = dot(d, R.u);
                const float3 perp = d - R.u * t;
                const float side = std::fabs(dot(perp, P.u)), dist = length(perp - P.u * dot(perp, P.u));
                const float rp = P.teeth * 0.05f;
                if (std::fabs(t) < R.radius && side < 0.4f && dist > rp - 0.1f && dist < rp + 0.6f) M.meshes.push_back({(int)(&P - M.gears.data()), (int)(&R - M.gears.data())});
                continue;
            }
            if (!parallel(A.u, B.u)) {   // 90 degrees: bevel / crown gears (axes crossing), worm and wheel (axes apart)
                if (std::fabs(dot(A.u, B.u)) > 0.05f) continue;
                // closest points of the two axis lines
                const float3 w0 = A.c - B.c;
                const float b = dot(A.u, B.u), dd = dot(A.u, w0), e = dot(B.u, w0), den = 1 - b * b;
                const float sA = (b * e - dd) / den, sB = (e - b * dd) / den;
                const float3 PA = A.c + A.u * sA, PB = B.c + B.u * sB;
                const float gap = length(PA - PB);
                if (A.kind == 2 || B.kind == 2) {
                    const MechGearW& W = A.kind == 2 ? A : B;
                    const MechGearW& G = A.kind == 2 ? B : A;
                    if (G.kind == 2) continue;
                    const float want = G.teeth * 0.05f + W.radius;
                    if (std::fabs(gap - want) < 0.2f && std::fabs(dot(W.c - G.c, G.u)) < 0.35f) M.meshes.push_back({(int)x, (int)y});
                } else if ((A.kind == 1 || B.kind == 1) && gap < 0.36f) {   // (a plate apart, 0.32 cm, still engages: teeth 0.4 to 0.8 cm wide)
                    const float3 P = (PA + PB) * 0.5f;
                    if (std::fabs(length(A.c - P) - B.teeth * 0.05f) < 0.35f && std::fabs(length(B.c - P) - A.teeth * 0.05f) < 0.35f) M.meshes.push_back({(int)x, (int)y});
                }
                continue;
            }
            if (A.kind == 2 || B.kind == 2) continue;
            float3 d = B.c - A.c;
            float ax = dot(d, A.u);
            if (std::fabs(ax) > 0.3f) continue;
            float dist = length(d - A.u * ax), want = (A.teeth + B.teeth) * 0.05f;
            char buf[160];
            if (std::fabs(dist - want) < 0.105f) M.meshes.push_back({(int)x, (int)y});
            // (near but not in mesh: no warning, only real blocks are reported)
        }

    // slides. A rack meshed with a pinion and held by nothing slides along its row, guided by the part carrying the
    // pinion axle; a linear actuator: its rod slides in its body (coaxial halves of the same kind)
    {
        std::vector<int> jointsOf(nb, 0);
        for (const MechJoint& j : joints) { ++jointsOf[j.a]; ++jointsOf[j.b]; }
        for (auto& ms : M.meshes) {
            const MechGearW& P = M.gears[ms.first];
            const MechGearW& R = M.gears[ms.second];
            if (R.kind != 3 || jointsOf[R.body] > 0) continue;
            int S = -1;
            for (const MechJoint& j : joints) if (j.type == 0 && (j.a == P.body || j.b == P.body)) S = j.a == P.body ? j.b : j.a;
            if (S < 0 || S == R.body) continue;
            const float t0 = dot(P.c - R.c, R.u), half = std::max(0.0f, R.radius - 0.4f);
            MechJoint j{S, R.body, R.c, R.u, R.c + R.u};
            j.type = 1;
            j.lo = t0 - half;   // the rack moves by q: the pinion, at t0 - q along it, stays on the teeth
            j.hi = t0 + half;
            joints.push_back(j);
            ++jointsOf[R.body];
        }
        for (int i = 0; i < n; ++i) {
            const MechMeta& cb = meta(bricks[i].part);
            if (cb.cylRole != 1) continue;
            const float3 cB = wPos(i, cb.cylC), uB = wDir(i, cb.cylAxis);
            for (int k = 0; k < n; ++k) {
                const MechMeta& cr = meta(bricks[k].part);
                if (k == i || cr.cylRole != 2 || cr.cylKind != cb.cylKind || M.brickBody[k] == M.brickBody[i]) continue;
                const float3 cR = wPos(k, cr.cylC), uR = wDir(k, cr.cylAxis);
                if (!parallel(uB, uR) || !onLine(cR, cB, uB, 0.15f)) continue;
                const float t = dot(cR - cB, uB), reach = (cb.cylLen + cr.cylLen) * 0.5f;
                if (std::fabs(t) > reach - 0.2f) continue;   // the rod must go into the body
                // travel (no slide data in LDraw): a third of the shorter half each way, at most 2.4 cm, rod kept in
                const float stroke = std::min(2.4f, std::max(0.4f, std::min(cb.cylLen, cr.cylLen) / 3));
                // axis from the body towards the rod: a positive command lengthens the actuator
                const float3 uo = t < 0 ? uB * -1.0f : uB;
                MechJoint j{M.brickBody[i], M.brickBody[k], cB, uo, cB + uo};
                j.type = 1;
                j.lo = -stroke;
                j.hi = stroke;
                joints.push_back(j);
            }
        }
    }
    // tooth phases: from the biggest gear of each train, every meshing gear turned so its teeth fall between the teeth
    // of the gear it meshes with (spin around its own axis, applied to its brick at rest)
    {
        const int ng = (int)M.gears.size();
        std::vector<std::vector<int>> gadj0(ng);
        for (auto& ms : M.meshes)
            if (parallel(M.gears[ms.first].u, M.gears[ms.second].u)) { gadj0[ms.first].push_back(ms.second); gadj0[ms.second].push_back(ms.first); }
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
        int px = joints[x].type == 1 ? -1 : (hasGear[joints[x].a] || hasGear[joints[x].b] ? 0 : 1), py = joints[y].type == 1 ? -1 : (hasGear[joints[y].a] || hasGear[joints[y].b] ? 0 : 1);
        return px < py;
    });
    std::vector<int> tf(nb);
    for (int i = 0; i < nb; ++i) tf[i] = i;
    std::function<int(int)> tfind = [&](int i) { while (tf[i] != i) { tf[i] = tf[tf[i]]; i = tf[i]; } return i; };
    std::vector<bool> inTree(joints.size(), false);
    for (int k : order) {
        int a = tfind(joints[k].a), b = tfind(joints[k].b);
        if (a != b) { tf[a] = b; inTree[k] = true; }
        else if (joints[k].type == 1) M.warnings.push_back("Vérin dans une boucle : non piloté");
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
                MechJoint jj = j;
                if (jj.type == 1 && jj.a != p) { jj.u = jj.u * -1.0f; std::swap(jj.lo, jj.hi); jj.lo = -jj.lo; jj.hi = -jj.hi; }
                M.tree.push_back({jj, p, c, (int)M.tree.size()});
                tjOf[c] = (int)M.tree.size() - 1;
            }
        }
    }
    // a part turning on an axle that itself turns in the same line: its angle is measured from the part carrying the axle
    for (MechTreeJ& t : M.tree)
        for (int tp = tjOf[t.parent]; t.j.type == 0 && tp >= 0 && M.tree[tp].j.type == 0 && parallel(M.tree[tp].j.u, t.j.u) && onLine(t.j.p, M.tree[tp].j.p, M.tree[tp].j.u); tp = tjOf[t.parent])
            t.parent = M.tree[tp].parent;
    const int nq = (int)M.tree.size();
    M.qmode.assign(nq, MechModel::QMode());
    // gear meshes: constraints of the solver (any supports). sigma from the velocities of the two gears at the contact.
    for (auto& ms : M.meshes) {
        const MechGearW &A = M.gears[ms.first], &B = M.gears[ms.second];
        float sigma = 1.0f;
        if (B.kind == 3) {
            const float3 foot = B.c + B.u * dot(A.c - B.c, B.u);
            float3 toward = foot - A.c;
            toward = toward - A.u * dot(toward, A.u);
            const float l = length(toward);
            const float3 K = A.c + (l > 1e-5f ? toward * (A.teeth * 0.05f / l) : float3(0));
            sigma = dot(cross(A.u, K - A.c), B.u) > 0 ? 1.0f : -1.0f;
        } else if (A.kind != 2 && B.kind != 2) {
            float3 dA = (B.c - A.c) - A.u * dot(B.c - A.c, A.u);
            const float l = length(dA);
            if (l > 1e-5f) {
                const float3 K = A.c + dA * (A.teeth * 0.05f / l);
                sigma = dot(cross(A.u, K - A.c), cross(B.u, K - B.c)) > 0 ? -1.0f : 1.0f;
            }
        }
        MechModel::GearCon g{ms.first, ms.second, sigma, 0.0};
        std::vector<Affine> rest(nb);
        g.c0 = gearPhase(M, g, rest);
        M.gearCons.push_back(g);
    }
    // commands: per gear train (gears linked by meshes), the pivot of its smallest moving gear (a worm first)
    {
        const int ng = (int)M.gears.size();
        std::vector<int> gc(ng);
        for (int i = 0; i < ng; ++i) gc[i] = i;
        std::function<int(int)> gfind = [&](int i) { while (gc[i] != i) { gc[i] = gc[gc[i]]; i = gc[i]; } return i; };
        for (auto& ms : M.meshes) gc[gfind(ms.first)] = gfind(ms.second);
        for (int i = 0; i < ng; ++i)   // gears on one body (one axle) belong to one train
            for (int j = i + 1; j < ng; ++j)
                if (M.gears[i].body == M.gears[j].body) gc[gfind(i)] = gfind(j);
        std::map<int, std::vector<int>> trains;
        for (auto& ms : M.meshes) { trains[gfind(ms.first)]; }
        for (int i = 0; i < ng; ++i)
            if (trains.count(gfind(i))) trains[gfind(i)].push_back(i);
        for (auto& kv : trains) {
            int master = -1;
            for (int gi : kv.second) {
                const MechGearW& G = M.gears[gi];
                const int t = tjOf[G.body];
                if (G.kind == 3 || t < 0 || M.qmode[t].kind != 0 || M.tree[t].j.type != 0) continue;
                if (master < 0 || G.teeth < M.gears[master].teeth) master = gi;
            }
            if (master < 0) {   // every gear of the train sits on a fixed part
                char buf[96];
                snprintf(buf, sizeof(buf), "Engrenages fixes : aucun ne peut tourner (%d dents)", M.gears[kv.second[0]].teeth);
                M.warnings.push_back(buf);
                continue;
            }
            MechCmd c;
            c.name = "Moteur";
            c.qi = tjOf[M.gears[master].body];
            M.qmode[c.qi] = {1, -1, 0, (int)M.cmds.size()};
            M.cmds.push_back(c);
        }
    }
    // an axle carrying gears that turns alone in a fixed part also gets a motor
    for (int qi = 0; qi < nq; ++qi)
        if (M.qmode[qi].kind == 0 && hasGear[M.tree[qi].child] && M.tree[qi].j.type == 0) {
            bool meshed = false;
            for (auto& ms : M.meshes) meshed |= M.gears[ms.first].body == M.tree[qi].child || M.gears[ms.second].body == M.tree[qi].child;
            if (meshed) continue;
            MechCmd c;
            c.name = "Axe";
            c.qi = qi;
            M.qmode[qi] = {1, -1, 0, (int)M.cmds.size()};
            M.cmds.push_back(c);
        }
    {   // linear actuators: one command each (cm), its travel as range; a slide of a rack is driven by its pinion
        int k = 0;
        for (int qi = 0; qi < nq; ++qi) {
            const MechTreeJ& t = M.tree[qi];
            if (t.j.type != 1 || M.qmode[qi].kind != 0 || hasGear[t.child]) continue;
            MechCmd c;
            c.name = "Vérin " + std::to_string(++k);
            c.qi = qi;
            c.type = 1;
            c.lo = t.j.lo;
            c.hi = t.j.hi;
            c.speed = 1.0f;
            M.qmode[qi] = {1, -1, 0, (int)M.cmds.size()};
            M.cmds.push_back(c);
        }
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
    // ratios of each command, measured: a small turn of the command alone, the other pivots solved
    for (size_t ci = 0; ci < M.cmds.size(); ++ci) {
        MechModel t = M;
        for (MechCmd& c : t.cmds) c.value = 0;
        const float dv = 0.02f;
        t.cmds[ci].value = dv;
        if (!solve(t)) {
            M.warnings.push_back("Train d'engrenages bloqué (rapports incompatibles) : " + M.cmds[ci].name);
            continue;
        }
        for (int qi = 0; qi < nq; ++qi) {
            if (qi == M.cmds[ci].qi || !hasGear[M.tree[qi].child]) continue;
            const double k = t.q[qi] / dv;
            if (std::fabs(k) > 1e-3) M.cmds[ci].ratio.push_back({qi, (float)k});
        }
    }
    // degrees of freedom: independent joint variables minus the rank of the loop equations
    std::vector<int> vars;
    for (int i = 0; i < nq; ++i)
        if (M.qmode[i].kind == 0 || M.qmode[i].kind == 1) vars.push_back(i);
    const int rows = (int)M.closures.size() * 6 + (int)M.gearCons.size();
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
    for (const MechTreeJ& t : m.tree)
        mats[t.child] = mul(mats[t.parent], t.j.type == 1 ? Affine::fromBasis(float3(1, 0, 0), float3(0, 1, 0), float3(0, 0, 1), t.j.u * (float)q[t.qi])
                                                          : rotAbout(t.j.p, t.j.u, (float)q[t.qi]));
}

void Mech::applyGears(const MechModel& m, std::vector<double>& q) const {
    for (size_t i = 0; i < q.size(); ++i) {
        const MechModel::QMode& md = m.qmode[i];
        if (md.kind == 2) q[i] = md.k * q[md.master];
        else if (md.kind == 3) q[i] = 0;
    }
}

// Tooth phase of a mesh: the angle of each gear around its axis, from the direction of the other gear, times its teeth.
double Mech::gearPhase(const MechModel& m, const MechModel::GearCon& g, const std::vector<Affine>& mats) {
    const MechGearW &A = m.gears[g.ga], &B = m.gears[g.gb];
    if (B.kind == 3) {   // pinion A and rack B: the pinion's phase and the pitch (pi x module 0.1 cm) of the rack
        const Affine &MA = mats[A.body], &MB = mats[B.body];
        const float3 cA = MA.point(A.c), cB = MB.point(B.c);
        const float3 uA = normalize(MA.point(A.c + A.u) - cA), dB = normalize(MB.point(B.c + B.u) - cB);
        const float3 eA = MA.point(A.c + A.ex) - cA;
        float3 toward = (cB + dB * dot(cA - cB, dB)) - cA;   // to the foot of the pinion on the row
        toward = toward - uA * dot(toward, uA);
        const float3 e = eA - uA * dot(eA, uA);
        const double aA = std::atan2(dot(cross(toward, e), uA), dot(toward, e));
        const double sAlong = dot(cA - cB, dB);
        return A.teeth * aA + g.sigma * (2 * 3.14159265358979 / (3.14159265358979 * 0.1)) * sAlong;
    }
    const Affine &MA = mats[A.body], &MB = mats[B.body];
    const float3 cA = MA.point(A.c), cB = MB.point(B.c);
    const float3 uA = normalize(MA.point(A.c + A.u) - cA), uB = normalize(MB.point(B.c + B.u) - cB);
    const float3 eA = MA.point(A.c + A.ex) - cA, eB = MB.point(B.c + B.ex) - cB;
    auto ang = [](float3 from, float3 to, const float3& u) {
        from = from - u * dot(from, u);
        to = to - u * dot(to, u);
        return (double)std::atan2(dot(cross(from, to), u), dot(from, to));
    };
    const double aA = ang(cB - cA, eA, uA), aB = ang(cA - cB, eB, uB);
    return A.teeth * aA + g.sigma * B.teeth * aB;
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
    for (const MechModel::GearCon& g : m.gearCons) {   // (modulo one tooth: the teeth repeat)
        double e = gearPhase(m, g, mats) - g.c0;
        e -= 2 * 3.14159265358979 * std::round(e / (2 * 3.14159265358979));
        out[k++] = e * 0.5;   // (weighted above the pull towards the previous pose of the solver: exact ratios)
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
    const int rowsC = residualRows(m), nf = (int)freeV.size(), rowsD = drag ? 3 : 0, rows = rowsC + rowsD + nf;
    const double LAM = 0.01, TOL = 0.004;
    std::vector<Affine> mats(m.bodyCount);
    std::vector<double> xRef(nf);
    for (int k = 0; k < nf; ++k) xRef[k] = prev[freeV[k]];
    auto evalR = [&](const std::vector<double>& x, std::vector<double>& out) {
        for (int k = 0; k < nf; ++k) m.q[freeV[k]] = x[k];
        applyGears(m, m.q);
        fk(m, m.q, mats);
        int k = closureRes(m, mats, out.data());
        for (const MechTreeJ& t : m.tree)   // travel of the slides (a stop beyond)
            if (t.j.type == 1) { const double v = m.q[t.qi]; out[k++] = v > t.j.hi ? v - t.j.hi : (v < t.j.lo ? t.j.lo - v : 0.0); }
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
                // (step 1e-4: the poses are single precision floats, a 1e-6 step only measured their rounding)
                xs[c] += 1e-4;
                evalR(xs, rj);
                for (int k = 0; k < rows; ++k) J[c][k] = (rj[k] - r[k]) / 1e-4;
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
