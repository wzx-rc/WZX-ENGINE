// Curve operations (see Curve.h): frames along a curve, tube mesh swept along it, booleans of closed planar curves.
#include "Curve.h"
#include "MeshGen.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace vp3d {

// --------------------------------------------------------------------------- frames

namespace {

float3 rotateAround(const float3& v, const float3& axis, float angle) {
    return qRot(qAxisAngle(axis, angle), v);
}

// a corner: the polyline turns by more than 20 degrees at a point (linear curves, Boolean tips, Bezier corners)
const float kCornerCos = 0.9397f;

void dedupe(std::vector<float3>& v) {
    std::vector<float3> o;
    for (const float3& p : v) if (o.empty() || length(p - o.back()) > 1e-6f) o.push_back(p);
    v.swap(o);
}

// directions of the segments before and after point i (closed: wraps around, the last point repeats the first)
void segmentDirs(const std::vector<float3>& P, bool closed, int i, float3& tin, float3& tout) {
    const int n = (int)P.size();
    const int m = closed ? n - 1 : n;
    if (closed) {
        const int k = i % m;
        tin = normalize(P[k] - P[(k - 1 + m) % m]);
        tout = normalize(P[(k + 1) % m] - P[k]);
        return;
    }
    tout = i + 1 < n ? normalize(P[i + 1] - P[i]) : normalize(P[i] - P[i - 1]);
    tin = i > 0 ? normalize(P[i] - P[i - 1]) : tout;
}

// Rotation minimizing frames along a polyline (closed: the last point repeats the first). The forward vector is the
// bisector of the two segments at each point: at a corner it is the normal of the mitre plane.
void polyFrames(const std::vector<float3>& P, bool closed, float angleDeg, std::vector<float3>& fwd, std::vector<float3>& up,
                std::vector<float3>& side, std::vector<float>& arc) {
    const int n = (int)P.size();
    fwd.resize(n);
    for (int i = 0; i < n; ++i) {
        float3 a, b;
        segmentDirs(P, closed, i, a, b);
        const float3 s = a + b;
        fwd[i] = length(s) > 1e-6f ? normalize(s) : b;
    }
    // first up vector: +Y made perpendicular to the tangent (+X when the curve starts vertically)
    float3 u0 = float3(0, 1, 0) - fwd[0] * dot(float3(0, 1, 0), fwd[0]);
    if (length(u0) < 1e-3f) u0 = float3(1, 0, 0) - fwd[0] * dot(float3(1, 0, 0), fwd[0]);
    up.resize(n);
    up[0] = normalize(u0);
    // double reflection (Wang, Juttler, Zheng, Liu 2008)
    for (int i = 0; i + 1 < n; ++i) {
        const float3 v1 = P[i + 1] - P[i];
        const float c1 = dot(v1, v1);
        float3 rL = up[i], tL = fwd[i];
        if (c1 > 1e-12f) {
            rL = up[i] - v1 * (2.0f / c1 * dot(v1, up[i]));
            tL = fwd[i] - v1 * (2.0f / c1 * dot(v1, fwd[i]));
        }
        const float3 v2 = fwd[i + 1] - tL;
        const float c2 = dot(v2, v2);
        float3 r = c2 > 1e-12f ? rL - v2 * (2.0f / c2 * dot(v2, rL)) : rL;
        r = r - fwd[i + 1] * dot(r, fwd[i + 1]);
        up[i + 1] = length(r) > 1e-6f ? normalize(r) : up[i];
    }
    arc.assign(n, 0.0f);
    for (int i = 1; i < n; ++i) arc[i] = arc[i - 1] + length(P[i] - P[i - 1]);
    // closed curve: the frames come back turned by some angle; that twist is spread along the curve
    if (closed && n > 1 && arc.back() > 0) {
        const float3 a = up[n - 1], b = up[0], t = fwd[0];
        const float twist = std::atan2(dot(cross(a, b), t), dot(a, b));
        for (int i = 1; i < n; ++i) up[i] = normalize(rotateAround(up[i], fwd[i], twist * arc[i] / arc.back()));
        up[n - 1] = up[0];
    }
    if (angleDeg != 0.0f)
        for (int i = 0; i < n; ++i) up[i] = normalize(rotateAround(up[i], fwd[i], radians(angleDeg)));
    side.resize(n);
    for (int i = 0; i < n; ++i) side[i] = normalize(cross(up[i], fwd[i]));   // X = Y x Z: side, up, forward
}

bool isClosedPolyline(const CurveData& c, const std::vector<float3>& P) {
    return c.closed && P.size() > 3 && length(P.front() - P.back()) < 1e-4f;
}

// Points of the tube along the curve, by c.mesh.distribution: 0 the curve samples (Display Smoothness), 1 evenly
// spaced (mesh.segments in all, corners kept), 2 adaptive (a ring each time the curve has turned by mesh.adaptiveAngle,
// corners kept, straight parts in one segment).
std::vector<float3> tubePoints(const CurveData& c, std::vector<char>& corner) {
    // corners are found on a dense sampling of the curve (a coarse sampling of a smooth curve also turns a lot
    // between two points, it is not a corner)
    std::vector<float3> s;
    CurveData dense = c;
    dense.steps = std::clamp(c.steps * 4, 32, 256);
    sampleCurve(dense, s);
    dedupe(s);
    const bool closed = isClosedPolyline(c, s);
    if (closed) s.back() = s.front();
    const int n = (int)s.size();
    std::vector<int> cuts{0};
    std::vector<float3> corners;
    for (int i = 0; i < n && n >= 3; ++i) {
        if (!closed && (i == 0 || i == n - 1)) continue;
        if (closed && i == n - 1) continue;   // the seam is point 0
        float3 a, b;
        segmentDirs(s, closed, i, a, b);
        if (dot(a, b) < kCornerCos) {
            corners.push_back(s[i]);
            if (i > 0) cuts.push_back(i);
        }
    }
    cuts.push_back(n - 1);
    auto isCorner = [&](const float3& p) {
        for (const float3& q : corners) if (length(p - q) < 1e-5f) return true;
        return false;
    };
    std::vector<float3> out;
    if (c.mesh.distribution == 0 || n < 3) {   // the curve samples (Display Smoothness)
        sampleCurve(c, out);
        dedupe(out);
        if (isClosedPolyline(c, out)) out.back() = out.front();
    } else {
        std::vector<float> arc(n, 0.0f);
        for (int i = 1; i < n; ++i) arc[i] = arc[i - 1] + length(s[i] - s[i - 1]);
        out.push_back(s[0]);
        for (size_t r = 0; r + 1 < cuts.size(); ++r) {
            const int i0 = cuts[r], i1 = cuts[r + 1];
            if (c.mesh.distribution == 1) {   // evenly spaced, the count shared by length
                const float L = arc[i1] - arc[i0];
                const int cnt = std::max(1, (int)std::lround(std::max(c.mesh.segments, 1) * L / std::max(arc.back(), 1e-6f)));
                int k = i0;
                for (int j = 1; j < cnt; ++j) {
                    const float target = arc[i0] + L * j / cnt;
                    while (k + 1 < i1 && arc[k + 1] < target) ++k;
                    const float seg = arc[k + 1] - arc[k];
                    out.push_back(lerp(s[k], s[k + 1], seg > 1e-9f ? clampf((target - arc[k]) / seg, 0, 1) : 0.0f));
                }
            } else {   // adaptive: a point each time the direction has turned by the tolerance
                const float tol = radians(std::clamp(c.mesh.adaptiveAngle, 0.5f, 45.0f));
                float turned = 0;
                for (int i = i0 + 1; i < i1; ++i) {
                    float3 a, b;
                    segmentDirs(s, closed, i, a, b);
                    turned += std::acos(clampf(dot(a, b), -1, 1));
                    if (turned >= tol) { out.push_back(s[i]); turned = 0; }
                }
            }
            out.push_back(s[i1]);
        }
    }
    corner.assign(out.size(), 0);
    for (size_t i = 0; i < out.size(); ++i) corner[i] = isCorner(out[i]) ? 1 : 0;
    if (!closed && !corner.empty()) corner.front() = corner.back() = 0;   // the ends of an open curve: caps
    return out;
}

} // namespace

void curveFrames(const CurveData& c, std::vector<float3>& pos, std::vector<float3>& fwd, std::vector<float3>& up,
                 std::vector<float3>& side, std::vector<float>* arcLength) {
    fwd.clear();
    up.clear();
    side.clear();
    sampleCurve(c, pos);
    dedupe(pos);
    if (pos.size() < 2) { pos.clear(); return; }
    const bool closed = isClosedPolyline(c, pos);
    if (closed) pos.back() = pos.front();
    std::vector<float> arc;
    polyFrames(pos, closed, c.mesh.angle, fwd, up, side, arc);
    if (arcLength) *arcLength = arc;
}

// --------------------------------------------------------------------------- tube mesh

MeshData makeCurveTube(const CurveData& c) {
    MeshData m;
    std::vector<char> cornerAt;
    const std::vector<float3> P = tubePoints(c, cornerAt);
    const int n = (int)P.size();
    if (n < 2) return m;
    const bool closed = isClosedPolyline(c, P);
    std::vector<float3> T, U, S;
    std::vector<float> arc;
    polyFrames(P, closed, c.mesh.angle, T, U, S, arc);
    const int sides = std::clamp(c.mesh.sides, 3, 64);
    const float r = std::max(c.mesh.radius, 1e-4f);
    const float circ = 2.0f * kPi * r;
    // rings of sides + 1 vertices (the last repeats the first for the texture seam). A corner gets two rings at the
    // same place, on its mitre plane (the section stretched along the bend), each with the normals of its own side.
    std::vector<int> ringIn(n), ringOut(n);
    int rings = 0;
    auto addRing = [&](int i, const float3& normalAxis, bool corner, const float3& tin, const float3& tout) {
        const float cosHalf = std::max(dot(T[i], tin), 0.2f);
        float3 bend = tout - tin;
        bend = bend - T[i] * dot(bend, T[i]);
        const bool bent = corner && length(bend) > 1e-6f;
        if (bent) bend = normalize(bend);
        for (int k = 0; k <= sides; ++k) {
            const float a = 2.0f * kPi * k / sides;
            float3 off = (U[i] * std::cos(a) + S[i] * std::sin(a)) * r;
            if (bent) off = off + bend * (dot(off, bend) * (1.0f / cosHalf - 1.0f));
            float3 nrm = off - normalAxis * dot(off, normalAxis);
            nrm = length(nrm) > 1e-9f ? normalize(nrm) : normalize(off);
            m.vertices.push_back({P[i] + off, nrm, float2(k / (float)sides, arc[i] / circ)});
        }
        return rings++;
    };
    for (int i = 0; i < n; ++i) {
        float3 tin, tout;
        segmentDirs(P, closed, i, tin, tout);
        const bool end = !closed && (i == 0 || i == n - 1);
        const bool corner = !end && cornerAt[i] && dot(tin, tout) < 0.99999f;   // a corner of the curve itself
        if (corner) {
            ringIn[i] = addRing(i, tin, true, tin, tout);
            ringOut[i] = addRing(i, tout, true, tin, tout);
        } else ringIn[i] = ringOut[i] = addRing(i, T[i], false, tin, tout);
    }
    auto vid = [&](int ring, int k) { return (uint32_t)(ring * (sides + 1) + k); };
    // winding: the first face must look outwards (its normal along the vertex normal)
    bool flip = false;
    {
        const float3 a = m.vertices[vid(ringOut[0], 0)].pos, b = m.vertices[vid(ringOut[0], 1)].pos, d = m.vertices[vid(ringIn[1], 0)].pos;
        flip = dot(cross(b - a, d - a), m.vertices[vid(ringOut[0], 0)].nrm) > 0;
    }
    for (int i = 0; i + 1 < n; ++i) {
        const int r0 = ringOut[i], r1 = ringIn[i + 1];
        for (int k = 0; k < sides; ++k) {
            const uint32_t a = vid(r0, k), b = vid(r0, k + 1), cc = vid(r1, k + 1), d = vid(r1, k);
            if (flip) { m.addTri(a, d, cc); m.addTri(a, cc, b); }
            else { m.addTri(a, b, cc); m.addTri(a, cc, d); }
            m.edges.push_back(a); m.edges.push_back(d);   // wireframe: along the curve
        }
    }
    for (int i = 0; i < n; ++i)   // wireframe: one ring per point
        for (int k = 0; k < sides; ++k) { m.edges.push_back(vid(ringOut[i], k)); m.edges.push_back(vid(ringOut[i], k + 1)); }
    // caps of an open curve: flat discs facing out
    if (!closed && c.mesh.caps)
        for (int e = 0; e < 2; ++e) {
            const int i = e == 0 ? 0 : n - 1;
            const float3 nrm = e == 0 ? -T[i] : T[i];
            const uint32_t center = (uint32_t)m.vertices.size();
            m.vertices.push_back({P[i], nrm, float2(0.5f, 0.5f)});
            for (int k = 0; k <= sides; ++k) {
                const float a = 2.0f * kPi * k / sides;
                const float3 d = U[i] * std::cos(a) + S[i] * std::sin(a);
                m.vertices.push_back({P[i] + d * r, nrm, float2(0.5f + 0.5f * std::cos(a), 0.5f + 0.5f * std::sin(a))});
            }
            for (int k = 0; k < sides; ++k) {
                const uint32_t a = center + 1 + k, b = center + 2 + k;
                const float3 fa = m.vertices[a].pos - P[i], fb = m.vertices[b].pos - P[i];
                if (dot(cross(fa, fb), nrm) > 0) m.addTri(center, a, b);
                else m.addTri(center, b, a);
            }
        }
    return m;
}

// --------------------------------------------------------------------------- booleans

namespace {

struct V2 {
    double x = 0, y = 0;
    V2() = default;
    V2(double x_, double y_) : x(x_), y(y_) {}
    V2 operator+(const V2& o) const { return {x + o.x, y + o.y}; }
    V2 operator-(const V2& o) const { return {x - o.x, y - o.y}; }
    V2 operator*(double s) const { return {x * s, y * s}; }
};
double dot2(const V2& a, const V2& b) { return a.x * b.x + a.y * b.y; }
double cross2(const V2& a, const V2& b) { return a.x * b.y - a.y * b.x; }
double len2(const V2& a) { return std::sqrt(dot2(a, a)); }

struct Cub { V2 p[4]; };

V2 cubAt(const Cub& c, double t) {
    const double u = 1 - t;
    return c.p[0] * (u * u * u) + c.p[1] * (3 * u * u * t) + c.p[2] * (3 * u * t * t) + c.p[3] * (t * t * t);
}
V2 cubD(const Cub& c, double t) {
    const double u = 1 - t;
    return (c.p[1] - c.p[0]) * (3 * u * u) + (c.p[2] - c.p[1]) * (6 * u * t) + (c.p[3] - c.p[2]) * (3 * t * t);
}
void cubSplit(const Cub& c, double t, Cub& l, Cub& r) {
    auto mix = [&](const V2& a, const V2& b) { return a + (b - a) * t; };
    const V2 a = mix(c.p[0], c.p[1]), b = mix(c.p[1], c.p[2]), d = mix(c.p[2], c.p[3]);
    const V2 e = mix(a, b), f = mix(b, d), g = mix(e, f);
    l.p[0] = c.p[0]; l.p[1] = a; l.p[2] = e; l.p[3] = g;
    r.p[0] = g; r.p[1] = f; r.p[2] = d; r.p[3] = c.p[3];
}
Cub cubReverse(const Cub& c) { Cub r; for (int i = 0; i < 4; ++i) r.p[i] = c.p[3 - i]; return r; }

// polyline of a closed loop of cubics: points, and for each point its cubic and parameter
struct Flat {
    std::vector<V2> pt;
    std::vector<int> seg;
    std::vector<double> t;
};
Flat flatten(const std::vector<Cub>& loop, int per) {
    Flat f;
    for (int i = 0; i < (int)loop.size(); ++i)
        for (int k = 0; k < per; ++k) {
            f.pt.push_back(cubAt(loop[i], k / (double)per));
            f.seg.push_back(i);
            f.t.push_back(k / (double)per);
        }
    return f;   // closed: the segment after the last point goes back to the first
}

double signedArea(const std::vector<Cub>& loop) {
    Flat f = flatten(loop, 32);
    double a = 0;
    for (size_t i = 0; i < f.pt.size(); ++i) a += cross2(f.pt[i], f.pt[(i + 1) % f.pt.size()]);
    return a * 0.5;
}

// winding number of p around a closed polyline (non-zero: inside)
int winding(const std::vector<V2>& poly, const V2& p) {
    int w = 0;
    const size_t n = poly.size();
    for (size_t i = 0; i < n; ++i) {
        const V2& a = poly[i];
        const V2& b = poly[(i + 1) % n];
        if (a.y <= p.y) {
            if (b.y > p.y && cross2(b - a, p - a) > 0) ++w;
        } else if (b.y <= p.y && cross2(b - a, p - a) < 0) --w;
    }
    return w;
}

// closest distance from p to a closed polyline, with the direction of that polyline segment
double closest(const std::vector<V2>& poly, const V2& p, V2& dir) {
    double best = 1e300;
    const size_t n = poly.size();
    for (size_t i = 0; i < n; ++i) {
        const V2 a = poly[i], b = poly[(i + 1) % n], ab = b - a;
        const double l2 = dot2(ab, ab);
        const double t = l2 > 0 ? std::clamp(dot2(p - a, ab) / l2, 0.0, 1.0) : 0.0;
        const double d = len2(a + ab * t - p);
        if (d < best) { best = d; dir = ab; }
    }
    return best;
}

struct Hit { int ia; double ta; int ib; double tb; V2 p; };

} // namespace

float curveArea(const CurveData& c) {
    std::vector<float3> s;
    sampleCurve(c, s);
    if (s.size() < 3) return 0.0f;
    // Newell normal: the area of a planar polygon is half the length of its normal
    float3 n(0);
    for (size_t i = 0; i < s.size(); ++i) {
        const float3& p = s[i];
        const float3& q = s[(i + 1) % s.size()];
        n.x += (p.y - q.y) * (p.z + q.z);
        n.y += (p.z - q.z) * (p.x + q.x);
        n.z += (p.x - q.x) * (p.y + q.y);
    }
    return 0.5f * length(n);
}

bool curveBoolean(const CurveData& ca, const CurveData& cb, int op, std::vector<CurveData>& out, std::string& error) {
    out.clear();
    if (!ca.valid() || !cb.valid() || !ca.closed || !cb.closed || ca.count() < 2 || cb.count() < 2) {
        error = "The Boolean needs two closed curves";
        return false;
    }
    // plane of the first curve (Newell normal of its samples)
    std::vector<float3> sa, sb;
    sampleCurve(ca, sa);
    sampleCurve(cb, sb);
    float3 nrm(0);
    for (size_t i = 0; i < sa.size(); ++i) {
        const float3& p = sa[i];
        const float3& q = sa[(i + 1) % sa.size()];
        nrm.x += (p.y - q.y) * (p.z + q.z);
        nrm.y += (p.z - q.z) * (p.x + q.x);
        nrm.z += (p.x - q.x) * (p.y + q.y);
    }
    if (length(nrm) < 1e-9f) { error = "The first curve encloses no area"; return false; }
    nrm = normalize(nrm);
    float3 mn(1e30f), mx(-1e30f);
    for (const float3& p : sa) { mn = minv(mn, p); mx = maxv(mx, p); }
    for (const float3& p : sb) { mn = minv(mn, p); mx = maxv(mx, p); }
    const double size = std::max(1e-6, (double)length(mx - mn));
    const float3 O = sa[0];
    for (const float3& p : sa) if (std::fabs(dot(p - O, nrm)) > 1e-3 * size) { error = "The first curve is not flat"; return false; }
    for (const float3& p : sb) if (std::fabs(dot(p - O, nrm)) > 1e-3 * size) { error = "The two curves must lie in the same plane"; return false; }
    const float3 ref = std::fabs(nrm.y) > 0.9f ? float3(1, 0, 0) : float3(0, 1, 0);
    const float3 U = normalize(ref - nrm * dot(ref, nrm)), V = cross(nrm, U);
    auto to2 = [&](const float3& p) { const float3 d = p - O; return V2(dot(d, U), dot(d, V)); };
    auto to3 = [&](const V2& p) { return O + U * (float)p.x + V * (float)p.y; };

    // exact cubic loops, counter-clockwise in the plane
    auto loopOf = [&](const CurveData& c) {
        const CurveData b = toBezierCurve(c);
        std::vector<Cub> v;
        const int n = b.count();
        for (int i = 0; i < n; ++i) {
            Cub q;
            q.p[0] = to2(b.points[i]);
            q.p[1] = to2(b.hout[i]);
            q.p[2] = to2(b.hin[(i + 1) % n]);
            q.p[3] = to2(b.points[(i + 1) % n]);
            if (len2(q.p[3] - q.p[0]) + len2(q.p[1] - q.p[0]) + len2(q.p[2] - q.p[3]) > 1e-12 * size) v.push_back(q);
        }
        if (signedArea(v) < 0) {
            std::reverse(v.begin(), v.end());
            for (Cub& q : v) q = cubReverse(q);
        }
        return v;
    };
    std::vector<Cub> A = loopOf(ca), B = loopOf(cb);
    if (A.empty() || B.empty() || std::fabs(signedArea(A)) < 1e-12 * size * size || std::fabs(signedArea(B)) < 1e-12 * size * size) {
        error = "A curve encloses no area";
        return false;
    }

    // crossings: polylines first, then each one refined on the cubics (Newton)
    const int per = 32;
    const Flat fa = flatten(A, per), fb = flatten(B, per);
    std::vector<Hit> hits;
    const double epsMerge = 1e-6 * size;
    auto addHit = [&](int ia, double ta, int ib, double tb) {
        for (int it = 0; it < 12; ++it) {   // A(ta) = B(tb)
            const V2 f = cubAt(A[ia], ta) - cubAt(B[ib], tb);
            if (len2(f) < 1e-13 * size) break;
            const V2 da = cubD(A[ia], ta), db = cubD(B[ib], tb) * -1.0;
            const double det = cross2(da, db);
            if (std::fabs(det) < 1e-18) break;
            const double nta = std::clamp(ta - cross2(f, db) / det, 0.0, 1.0), ntb = std::clamp(tb - cross2(da, f) / det, 0.0, 1.0);
            ta = nta;
            tb = ntb;
        }
        Hit h{ia, ta, ib, tb, cubAt(A[ia], ta)};
        for (const Hit& o : hits) if (len2(o.p - h.p) < epsMerge) return;
        hits.push_back(h);
    };
    const size_t na = fa.pt.size(), nb = fb.pt.size();
    for (size_t i = 0; i < na; ++i) {
        const V2 a0 = fa.pt[i], a1 = fa.pt[(i + 1) % na], da = a1 - a0;
        const double tA0 = fa.t[i], tA1 = (i + 1) % na == 0 || fa.seg[(i + 1) % na] != fa.seg[i] ? 1.0 : fa.t[(i + 1) % na];
        for (size_t j = 0; j < nb; ++j) {
            const V2 b0 = fb.pt[j], b1 = fb.pt[(j + 1) % nb], db = b1 - b0;
            if (std::max(a0.x, a1.x) < std::min(b0.x, b1.x) || std::max(b0.x, b1.x) < std::min(a0.x, a1.x) ||
                std::max(a0.y, a1.y) < std::min(b0.y, b1.y) || std::max(b0.y, b1.y) < std::min(a0.y, a1.y)) continue;
            const double tB0 = fb.t[j], tB1 = (j + 1) % nb == 0 || fb.seg[(j + 1) % nb] != fb.seg[j] ? 1.0 : fb.t[(j + 1) % nb];
            const double d = cross2(da, db);
            const double la = len2(da), lb = len2(db);
            if (std::fabs(d) > 1e-9 * la * lb) {
                const double s = cross2(b0 - a0, db) / d, u = cross2(b0 - a0, da) / d;
                if (s >= 0 && s < 1 && u >= 0 && u < 1) addHit(fa.seg[i], tA0 + (tA1 - tA0) * s, fb.seg[j], tB0 + (tB1 - tB0) * u);
            } else if (std::fabs(cross2(b0 - a0, da)) < 1e-7 * la * size) {
                // collinear overlap: its ends are vertices of the result
                const double l2 = la * la;
                const double s0 = l2 > 0 ? dot2(b0 - a0, da) / l2 : -1;
                if (s0 > 0 && s0 < 1) addHit(fa.seg[i], tA0 + (tA1 - tA0) * s0, fb.seg[j], tB0);
                const double u0 = lb > 0 ? dot2(a0 - b0, db) / (lb * lb) : -1;
                if (u0 > 0 && u0 < 1) addHit(fa.seg[i], tA0, fb.seg[j], tB0 + (tB1 - tB0) * u0);
            }
        }
    }

    // pieces: each cubic split at its crossings
    struct Piece { Cub c; int from; int s = -1, e = -1; int cls = 0; };
    std::vector<Piece> pieces;
    auto cut = [&](const std::vector<Cub>& loop, int from) {
        for (int i = 0; i < (int)loop.size(); ++i) {
            std::vector<double> ts;
            for (const Hit& h : hits) {
                const double t = from == 0 ? (h.ia == i ? h.ta : -1) : (h.ib == i ? h.tb : -1);
                if (t > 1e-9 && t < 1 - 1e-9) ts.push_back(t);
            }
            std::sort(ts.begin(), ts.end());
            Cub rest = loop[i];
            double done = 0;
            for (double t : ts) {
                if (t - done < 1e-9) continue;
                Cub l, r;
                cubSplit(rest, (t - done) / (1 - done), l, r);
                pieces.push_back({l, from});
                rest = r;
                done = t;
            }
            pieces.push_back({rest, from});
        }
    };
    cut(A, 0);
    cut(B, 1);
    // shared end points: merged within a tolerance, the pieces snapped on them
    std::vector<V2> verts;
    auto vertexOf = [&](const V2& p) {
        for (size_t k = 0; k < verts.size(); ++k) if (len2(verts[k] - p) < epsMerge * 10) return (int)k;
        verts.push_back(p);
        return (int)verts.size() - 1;
    };
    for (Piece& pc : pieces) {
        pc.s = vertexOf(pc.c.p[0]);
        pc.e = vertexOf(pc.c.p[3]);
        pc.c.p[0] = verts[pc.s];
        pc.c.p[3] = verts[pc.e];
    }
    // classification of each piece against the other loop: 1 outside, 2 inside, 3 on it (same way), 4 on it (opposite)
    const Flat da = flatten(A, 64), db = flatten(B, 64);
    for (Piece& pc : pieces) {
        if (pc.s == pc.e && len2(pc.c.p[1] - pc.c.p[0]) + len2(pc.c.p[2] - pc.c.p[0]) < epsMerge) { pc.cls = 0; continue; }   // degenerate
        const std::vector<V2>& other = pc.from == 0 ? db.pt : da.pt;
        const V2 mid = cubAt(pc.c, 0.5);
        V2 t = cubD(pc.c, 0.5);
        if (len2(t) < 1e-15) t = pc.c.p[3] - pc.c.p[0];
        V2 dir;
        // on the other border when closer than its polyline error (a short piece next to a crossing is not "on")
        const double onTol = std::min(1e-4 * size, 0.1 * len2(pc.c.p[3] - pc.c.p[0]) + 1e-9 * size);
        if (closest(other, mid, dir) < onTol) pc.cls = dot2(t, dir) > 0 ? 3 : 4;
        else pc.cls = winding(other, mid) != 0 ? 2 : 1;
    }
    // pieces kept by the operation (B reversed for a subtraction: it becomes the inside border of the hole)
    std::vector<Piece> keep;
    for (const Piece& pc : pieces) {
        const bool a = pc.from == 0;
        bool k = false, rev = false;
        if (op == CURVE_UNION) k = a ? (pc.cls == 1 || pc.cls == 3) : pc.cls == 1;
        else if (op == CURVE_INTERSECT) k = a ? (pc.cls == 2 || pc.cls == 3) : pc.cls == 2;
        else { k = a ? (pc.cls == 1 || pc.cls == 4) : pc.cls == 2; rev = !a; }
        if (!k) continue;
        Piece q = pc;
        if (rev) { q.c = cubReverse(pc.c); std::swap(q.s, q.e); }
        keep.push_back(q);
    }
    // loops: each piece is followed by the one that starts where it ends
    std::multimap<int, int> starts;
    for (int i = 0; i < (int)keep.size(); ++i) starts.insert({keep[i].s, i});
    std::vector<char> used(keep.size(), 0);
    for (int first = 0; first < (int)keep.size(); ++first) {
        if (used[first]) continue;
        std::vector<int> chain{first};
        used[first] = 1;
        bool closedLoop = false;
        for (size_t guard = 0; guard <= keep.size(); ++guard) {
            const int end = keep[chain.back()].e;
            if (end == keep[first].s) { closedLoop = true; break; }
            int next = -1;
            auto range = starts.equal_range(end);
            for (auto it = range.first; it != range.second; ++it) if (!used[it->second]) { next = it->second; break; }
            if (next < 0) break;
            used[next] = 1;
            chain.push_back(next);
        }
        if (!closedLoop) continue;   // a broken chain (degenerate touching): left out
        CurveData r;
        r.type = CURVE_BEZIER;
        r.closed = true;
        r.steps = ca.steps;
        const int m = (int)chain.size();
        for (int k = 0; k < m; ++k) {
            const Cub& q = keep[chain[k]].c;
            r.points.push_back(to3(q.p[0]));
            r.hout.push_back(to3(q.p[1]));
            r.hin.push_back(float3(0));
        }
        for (int k = 0; k < m; ++k) r.hin[(k + 1) % m] = to3(keep[chain[k]].c.p[2]);
        // straight everywhere: a linear curve without the points in the middle of straight lines
        bool straight = true;
        for (int k = 0; k < m && straight; ++k) {
            const Cub& q = keep[chain[k]].c;
            const V2 d = q.p[3] - q.p[0];
            const double l = len2(d);
            straight = l > 0 && std::fabs(cross2(q.p[1] - q.p[0], d)) / l < 1e-6 * size && std::fabs(cross2(q.p[2] - q.p[0], d)) / l < 1e-6 * size;
        }
        if (straight) {
            r.type = CURVE_LINEAR;
            r.hin.clear();
            r.hout.clear();
            std::vector<float3> pts;
            for (int k = 0; k < m; ++k) {
                const float3 p = r.points[k], a = r.points[(k - 1 + m) % m], b = r.points[(k + 1) % m];
                const float3 u = normalize(p - a), w = normalize(b - p);
                if (length(cross(u, w)) > 1e-5f || dot(u, w) < 0) pts.push_back(p);
            }
            r.points = pts;
        } else {
            r.broken.assign(m, 1);
            for (int k = 0; k < m; ++k) {   // smooth where the two handles are aligned
                const float3 vi = r.hin[k] - r.points[k], vo = r.hout[k] - r.points[k];
                if (length(vi) > 1e-6f && length(vo) > 1e-6f && dot(normalize(vi), normalize(vo)) < -0.9999f) r.broken[k] = 0;
            }
        }
        if (r.count() >= 3) out.push_back(r);
    }
    if (out.empty()) {
        error = op == CURVE_INTERSECT ? "The two curves do not overlap" : "The result is empty";
        return false;
    }
    return true;
}

} // namespace vp3d
