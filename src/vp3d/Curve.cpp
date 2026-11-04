// 3D curves: evaluation, conversions and primitives (see Curve.h).
#include "Curve.h"
#include <algorithm>
#include <cmath>

namespace vp3d {

const char* curveTypeName(int type) {
    switch (type) {
    case CURVE_LINEAR: return "Linear";
    case CURVE_CV: return "CV Curve";
    case CURVE_EP: return "EP Curve";
    case CURVE_BEZIER: return "Bezier";
    default: return "Curve";
    }
}

const char* curveShapeName(int shape) {
    switch (shape) {
    case CSHAPE_LINE: return "Line";
    case CSHAPE_CIRCLE: return "Circle";
    case CSHAPE_SQUARE: return "Square";
    case CSHAPE_ARC: return "Arc";
    case CSHAPE_HELIX: return "Helix";
    case CSHAPE_SPIRAL: return "Spiral";
    case CSHAPE_STAR: return "Star";
    default: return "Curve";
    }
}

int curveDegree(const CurveData& c) {
    if (c.type == CURVE_LINEAR) return 1;
    int n = c.count();
    if (c.type == CURVE_CV && !c.closed) return std::max(1, std::min(3, n - 1));
    return n >= 3 || c.type == CURVE_BEZIER ? 3 : 1;
}

int curveSpans(const CurveData& c) {
    int n = c.count();
    if (n < 2) return 0;
    if (c.type == CURVE_CV) {
        if (c.closed && n >= 3) return n;
        return c.closed ? n : n - curveDegree(c);   // closed with 2 points: a back and forth line
    }
    return c.closed ? n : n - 1;
}

namespace {

float3 bezierPoint(const float3& p0, const float3& p1, const float3& p2, const float3& p3, float t) {
    float u = 1 - t;
    return p0 * (u * u * u) + p1 * (3 * u * u * t) + p2 * (3 * u * t * t) + p3 * (t * t * t);
}

// point i of the curve with the ends of an open curve reflected (Catmull-Rom neighbours)
float3 neighbour(const CurveData& c, int i) {
    int n = c.count();
    if (c.closed) return c.points[((i % n) + n) % n];
    if (i < 0) return c.points[0] * 2.0f - c.points[std::min(1, n - 1)];
    if (i >= n) return c.points[n - 1] * 2.0f - c.points[std::max(0, n - 2)];
    return c.points[i];
}

// Catmull-Rom segment i -> i+1 as Bezier control points (EP curves and smooth Bezier handles)
void catmullSpan(const CurveData& c, int i, float3& b1, float3& b2) {
    float3 p0 = neighbour(c, i - 1), p1 = neighbour(c, i), p2 = neighbour(c, i + 1), p3 = neighbour(c, i + 2);
    b1 = p1 + (p2 - p0) * (1.0f / 6.0f);
    b2 = p2 - (p3 - p1) * (1.0f / 6.0f);
}

// open clamped B-spline of degree d (de Boor), parameter u in [0, n - d]
float3 deBoor(const std::vector<float3>& P, int d, float u) {
    const int n = (int)P.size();
    auto knot = [&](int i) { return (float)std::clamp(i - d, 0, n - d); };
    int k = std::clamp((int)std::floor(u) + d, d, n - 1);
    float3 q[4];
    for (int j = 0; j <= d; ++j) q[j] = P[j + k - d];
    for (int r = 1; r <= d; ++r)
        for (int j = d; j >= r; --j) {
            float a0 = knot(j + k - d), a1 = knot(j + 1 + k - r);
            float alpha = a1 > a0 ? (u - a0) / (a1 - a0) : 0.0f;
            q[j] = q[j - 1] * (1 - alpha) + q[j] * alpha;
        }
    return q[d];
}

float3 periodicBspline(const CurveData& c, int i, float t) {
    int n = c.count();
    auto P = [&](int k) { return c.points[((k % n) + n) % n]; };
    float t2 = t * t, t3 = t2 * t;
    float b0 = (1 - t) * (1 - t) * (1 - t) / 6.0f, b1 = (3 * t3 - 6 * t2 + 4) / 6.0f;
    float b2 = (-3 * t3 + 3 * t2 + 3 * t + 1) / 6.0f, b3 = t3 / 6.0f;
    return P(i - 1) * b0 + P(i) * b1 + P(i + 1) * b2 + P(i + 2) * b3;
}

} // namespace

void sampleCurve(const CurveData& c, std::vector<float3>& out, std::vector<int>* spanOf) {
    out.clear();
    if (spanOf) spanOf->clear();
    const int n = c.count();
    if (n == 0) return;
    if (n == 1) { out.push_back(c.points[0]); if (spanOf) spanOf->push_back(0); return; }
    const int spans = curveSpans(c);
    const int steps = std::clamp(c.steps, 1, 256);
    auto add = [&](const float3& p, int span) { out.push_back(p); if (spanOf) spanOf->push_back(span); };
    const bool linear = c.type == CURVE_LINEAR || curveDegree(c) == 1;
    if (linear && c.type != CURVE_BEZIER) {
        for (int i = 0; i < n; ++i) add(c.points[i], std::min(i, spans - 1));
        if (c.closed) add(c.points[0], spans - 1);
        return;
    }
    if (c.type == CURVE_CV && !c.closed) {
        const int d = curveDegree(c);
        for (int sp = 0; sp < spans; ++sp)
            for (int k = 0; k < steps; ++k) add(deBoor(c.points, d, sp + k / (float)steps), sp);
        add(c.points[n - 1], spans - 1);
        return;
    }
    for (int sp = 0; sp < spans; ++sp) {
        float3 p0 = c.points[sp], p3 = c.points[(sp + 1) % n], b1, b2;
        if (c.type == CURVE_BEZIER && (int)c.hout.size() == n && (int)c.hin.size() == n) {
            b1 = c.hout[sp];
            b2 = c.hin[(sp + 1) % n];
        } else catmullSpan(c, sp, b1, b2);
        for (int k = 0; k < steps; ++k) {
            float t = k / (float)steps;
            add(c.type == CURVE_CV ? periodicBspline(c, sp, t) : bezierPoint(p0, b1, b2, p3, t), sp);
        }
    }
    if (c.type == CURVE_CV) add(periodicBspline(c, 0, 0.0f), spans - 1);   // closed CV curve
    else add(c.closed ? c.points[0] : c.points[n - 1], spans - 1);
}

float curveLength(const CurveData& c) {
    std::vector<float3> s;
    sampleCurve(c, s);
    float l = 0;
    for (size_t i = 1; i < s.size(); ++i) l += length(s[i] - s[i - 1]);
    return l;
}

void curveBounds(const CurveData& c, float3& mn, float3& mx) {
    mn = float3(1e30f);
    mx = float3(-1e30f);
    std::vector<float3> s;
    sampleCurve(c, s);
    for (const float3& p : s) { mn = minv(mn, p); mx = maxv(mx, p); }
    for (const float3& p : c.points) { mn = minv(mn, p); mx = maxv(mx, p); }
    if (c.type == CURVE_BEZIER) {
        for (const float3& p : c.hin) { mn = minv(mn, p); mx = maxv(mx, p); }
        for (const float3& p : c.hout) { mn = minv(mn, p); mx = maxv(mx, p); }
    }
    if (mn.x > mx.x) mn = mx = float3(0);
}

void bezierFixArrays(CurveData& c) {
    const size_t n = c.points.size();
    bool fresh = c.hin.size() != n || c.hout.size() != n;
    c.hin.resize(n);
    c.hout.resize(n);
    c.broken.resize(n, 0);
    if (fresh) bezierAutoHandlesAll(c);
}

void bezierAutoHandles(CurveData& c, int i) {
    const int n = c.count();
    if (i < 0 || i >= n || (int)c.hin.size() != n || (int)c.hout.size() != n) return;
    float3 t = (neighbour(c, i + 1) - neighbour(c, i - 1)) * (1.0f / 6.0f);
    if (n == 1) t = float3(0);
    c.hout[i] = c.points[i] + t;
    c.hin[i] = c.points[i] - t;
}

void bezierAutoHandlesAll(CurveData& c) {
    c.hin.resize(c.points.size());
    c.hout.resize(c.points.size());
    c.broken.resize(c.points.size(), 0);
    for (int i = 0; i < c.count(); ++i) bezierAutoHandles(c, i);
}

void convertCurve(CurveData& c, int type) {
    if (type <= CURVE_NONE || type >= CURVE_TYPE_COUNT || type == c.type) return;
    c.type = type;
    c.shape.type = CSHAPE_NONE;
    if (type == CURVE_BEZIER) {
        c.hin.clear();
        c.hout.clear();
        c.broken.clear();
        bezierFixArrays(c);
    } else {
        c.hin.clear();
        c.hout.clear();
        c.broken.clear();
    }
}

void reverseCurve(CurveData& c) {
    std::reverse(c.points.begin(), c.points.end());
    if (c.type == CURVE_BEZIER) {
        std::reverse(c.hin.begin(), c.hin.end());
        std::reverse(c.hout.begin(), c.hout.end());
        std::reverse(c.broken.begin(), c.broken.end());
        std::swap(c.hin, c.hout);
    }
    c.shape.type = CSHAPE_NONE;
}

void rebuildCurve(CurveData& c, int n) {
    n = std::max(n, c.closed ? 3 : 2);
    CurveData dense = c;
    dense.steps = 64;
    std::vector<float3> s;
    sampleCurve(dense, s);
    if (s.size() < 2) return;
    std::vector<float> acc(s.size(), 0.0f);
    for (size_t i = 1; i < s.size(); ++i) acc[i] = acc[i - 1] + length(s[i] - s[i - 1]);
    const float total = acc.back();
    if (total <= 1e-6f) return;
    std::vector<float3> pts;
    const int div = c.closed ? n : n - 1;
    size_t j = 0;
    for (int k = 0; k < n; ++k) {
        float target = total * k / div;
        while (j + 1 < acc.size() - 1 && acc[j + 1] < target) ++j;
        float seg = acc[j + 1] - acc[j];
        float t = seg > 1e-9f ? (target - acc[j]) / seg : 0.0f;
        pts.push_back(lerp(s[j], s[j + 1], clampf(t, 0, 1)));
    }
    c.points = pts;
    c.shape.type = CSHAPE_NONE;
    if (c.type == CURVE_BEZIER) bezierAutoHandlesAll(c);
}

void insertCurvePoint(CurveData& c, int after, const float3& p) {
    int at = std::clamp(after + 1, 0, c.count());
    c.points.insert(c.points.begin() + at, p);
    if (c.type == CURVE_BEZIER) {
        c.hin.insert(c.hin.begin() + std::min(at, (int)c.hin.size()), p);
        c.hout.insert(c.hout.begin() + std::min(at, (int)c.hout.size()), p);
        c.broken.insert(c.broken.begin() + std::min(at, (int)c.broken.size()), (uint8_t)0);
        bezierFixArrays(c);
        bezierAutoHandles(c, at);
    }
    c.shape.type = CSHAPE_NONE;
}

void eraseCurvePoint(CurveData& c, int i) {
    if (i < 0 || i >= c.count()) return;
    c.points.erase(c.points.begin() + i);
    if (i < (int)c.hin.size()) c.hin.erase(c.hin.begin() + i);
    if (i < (int)c.hout.size()) c.hout.erase(c.hout.begin() + i);
    if (i < (int)c.broken.size()) c.broken.erase(c.broken.begin() + i);
    if (c.closed && c.count() < 3) c.closed = false;
    c.shape.type = CSHAPE_NONE;
}

void buildCurveShape(CurveData& c) {
    const CurveShapeParams& p = c.shape;
    std::vector<float3> pts;
    auto ring = [](float r, float a, float y) { return float3(r * std::cos(a), y, r * std::sin(a)); };
    int type = CURVE_EP;
    bool closed = false;
    const int sec = std::max(p.sections, 3);
    switch (p.type) {
    case CSHAPE_LINE:
        type = CURVE_LINEAR;
        pts = {float3(-p.width * 0.5f, 0, 0), float3(p.width * 0.5f, 0, 0)};
        break;
    case CSHAPE_CIRCLE: {
        // CV circle: the cubic B-spline passes inside its CVs, they are pushed out so that the curve has the radius
        type = CURVE_CV;
        closed = true;
        float th = 2 * kPi / sec, k = 3.0f / (2.0f + std::cos(th));
        for (int i = 0; i < sec; ++i) pts.push_back(ring(p.radius * k, i * th, 0));
        break;
    }
    case CSHAPE_SQUARE: {
        type = CURVE_LINEAR;
        closed = true;
        float w = p.width * 0.5f, d = p.depth * 0.5f;
        pts = {float3(-w, 0, -d), float3(w, 0, -d), float3(w, 0, d), float3(-w, 0, d)};
        break;
    }
    case CSHAPE_ARC: {
        int n = std::max(p.sections, 2);
        float sw = radians(clampf(p.sweep, 1.0f, 360.0f));
        for (int i = 0; i <= n; ++i) pts.push_back(ring(p.radius, sw * i / n, 0));
        break;
    }
    case CSHAPE_HELIX: {
        int n = std::max(2, (int)std::lround(sec * std::max(p.turns, 0.1f)));
        for (int i = 0; i <= n; ++i) {
            float t = i / (float)n;
            pts.push_back(ring(p.radius, t * p.turns * 2 * kPi, t * p.height));
        }
        break;
    }
    case CSHAPE_SPIRAL: {
        int n = std::max(2, (int)std::lround(sec * std::max(p.turns, 0.1f)));
        for (int i = 0; i <= n; ++i) {
            float t = i / (float)n;
            pts.push_back(ring(lerpf(p.radius2, p.radius, t), t * p.turns * 2 * kPi, 0));
        }
        break;
    }
    case CSHAPE_STAR: {
        type = CURVE_LINEAR;
        closed = true;
        int b = std::max(p.sections, 3);
        for (int i = 0; i < 2 * b; ++i) pts.push_back(ring((i & 1) ? p.radius2 : p.radius, kPi * 0.5f + i * kPi / b, 0));
        break;
    }
    default: return;
    }
    c.type = type;
    c.closed = closed;
    c.points = pts;
    c.hin.clear();
    c.hout.clear();
    c.broken.clear();
}

CurveData makeCurveShape(int shape) {
    CurveData c;
    c.shape.type = shape;
    if (shape == CSHAPE_STAR) { c.shape.sections = 5; c.shape.radius2 = 1.0f; }
    if (shape == CSHAPE_ARC) c.shape.sections = 6;
    if (shape == CSHAPE_SPIRAL) c.shape.sections = 12;
    if (shape == CSHAPE_HELIX) c.shape.sections = 12;
    buildCurveShape(c);
    return c;
}


CurveData filletCurve(const CurveData& in, const std::vector<int>& pts, float radius) {
    CurveData c = in;
    const int n = c.count();
    if (radius <= 0 || pts.empty() || n < 3) return c;
    if (c.type == CURVE_LINEAR) {   // straight segments: Bezier handles on the chords
        c.type = CURVE_BEZIER;
        c.hin.assign(n, float3(0));
        c.hout.assign(n, float3(0));
        c.broken.assign(n, 1);
        for (int i = 0; i < n; ++i) {
            const float3 p = c.points[i];
            const bool hasPrev = c.closed || i > 0, hasNext = c.closed || i < n - 1;
            c.hin[i] = hasPrev ? p + (c.points[(i - 1 + n) % n] - p) * (1.0f / 3.0f) : p;
            c.hout[i] = hasNext ? p + (c.points[(i + 1) % n] - p) * (1.0f / 3.0f) : p;
        }
    } else if (c.type != CURVE_BEZIER) convertCurve(c, CURVE_BEZIER);
    bezierFixArrays(c);
    std::vector<char> sel(n, 0);
    for (int i : pts) if (i >= 0 && i < n) sel[i] = 1;
    // segment i -> i+1 straight: both handles on the chord
    auto straight = [&](int i) {
        int j = (i + 1) % n;
        float3 d = c.points[j] - c.points[i];
        float L = length(d);
        if (L < 1e-6f) return true;
        d = d / L;
        auto on = [&](const float3& h, const float3& base) { float3 v = h - base; float l = length(v); return l < 1e-6f || length(cross(v / l, d)) < 1e-3f; };
        return on(c.hout[i], c.points[i]) && on(c.hin[j], c.points[j]);
    };
    struct Fil { bool on = false; float3 p1, p2, in1, out1, in2, out2; };
    std::vector<Fil> f(n);
    for (int i = 0; i < n; ++i) {
        if (!sel[i] || (!c.closed && (i == 0 || i == n - 1))) continue;
        const int ip = (i - 1 + n) % n, inx = (i + 1) % n;
        const float3 p = c.points[i];
        float3 a = c.hin[i] - p, b = c.hout[i] - p;
        if (length(a) < 1e-6f) a = c.points[ip] - p;
        if (length(b) < 1e-6f) b = c.points[inx] - p;
        a = normalize(a);
        b = normalize(b);
        const float th = std::acos(clampf(dot(a, b), -1.0f, 1.0f));   // angle between the two segments
        if (th < 1e-3f || th > kPi - 1e-3f) continue;               // folded back or straight: nothing to round
        const float dp = length(c.points[ip] - p), dn = length(c.points[inx] - p);
        const bool pEnd = sel[ip] && (c.closed || ip > 0), nEnd = sel[inx] && (c.closed || inx < n - 1);
        const float lim = std::min(dp * (pEnd ? 0.5f : 0.98f), dn * (nEnd ? 0.5f : 0.98f));
        const float t = std::min(radius / std::tan(th * 0.5f), lim);   // distance from the corner to the arc ends
        const float r = t * std::tan(th * 0.5f);
        const float k = 4.0f / 3.0f * std::tan((kPi - th) * 0.25f) * r;  // Bezier handle of a circular arc
        Fil& F = f[i];
        F.on = true;
        F.p1 = p + a * t;
        F.p2 = p + b * t;
        F.in1 = F.p1 + a * ((dp - t) / 3.0f);
        F.out1 = F.p1 - a * k;
        F.in2 = F.p2 - b * k;
        F.out2 = F.p2 + b * ((dn - t) / 3.0f);
    }
    CurveData out = c;
    out.points.clear();
    out.hin.clear();
    out.hout.clear();
    out.broken.clear();
    std::vector<char> outStraight;   // the segment leaving this new point lies on an original straight segment
    for (int i = 0; i < n; ++i) {
        const bool st = (c.closed || i < n - 1) && straight(i);
        if (f[i].on) {
            out.points.push_back(f[i].p1); out.hin.push_back(f[i].in1); out.hout.push_back(f[i].out1); out.broken.push_back(0); outStraight.push_back(0);
            out.points.push_back(f[i].p2); out.hin.push_back(f[i].in2); out.hout.push_back(f[i].out2); out.broken.push_back(0); outStraight.push_back(st);
        } else {
            out.points.push_back(c.points[i]); out.hin.push_back(c.hin[i]); out.hout.push_back(c.hout[i]); out.broken.push_back(c.broken[i]); outStraight.push_back(st);
        }
    }
    const int m = out.count();
    for (int k = 0; k < m; ++k) {
        if (!outStraight[k] || (!out.closed && k == m - 1)) continue;
        const int j = (k + 1) % m;
        out.hout[k] = out.points[k] + (out.points[j] - out.points[k]) * (1.0f / 3.0f);
        out.hin[j] = out.points[j] + (out.points[k] - out.points[j]) * (1.0f / 3.0f);
    }
    out.type = CURVE_BEZIER;
    out.shape.type = CSHAPE_NONE;
    return out;
}

CurveData toBezierCurve(const CurveData& c) {
    CurveData b = c;
    b.type = CURVE_BEZIER;
    b.shape.type = CSHAPE_NONE;
    if (c.type == CURVE_BEZIER) { bezierFixArrays(b); return b; }
    const int n = c.count();
    b.points.clear();
    b.hin.clear();
    b.hout.clear();
    b.broken.clear();
    if (n == 0) return b;
    const int spans = curveSpans(c);
    if (n == 1 || spans == 0) { b.points = c.points; bezierFixArrays(b); return b; }
    // Bezier controls of span sp: straight (linear), Catmull-Rom (EP), or the cubic of the B-spline span found from
    // four of its points (exact: the span is a polynomial of degree 3 at most)
    auto span = [&](int sp, float3& p0, float3& p1, float3& p2, float3& p3) {
        const bool linear = c.type == CURVE_LINEAR || curveDegree(c) == 1;
        if (linear || c.type == CURVE_EP) {
            p0 = c.points[sp];
            p3 = c.points[(sp + 1) % n];
            if (linear) { p1 = p0 + (p3 - p0) * (1.0f / 3.0f); p2 = p0 + (p3 - p0) * (2.0f / 3.0f); }
            else catmullSpan(c, sp, p1, p2);
            return;
        }
        auto at = [&](float t) { return c.closed ? periodicBspline(c, sp, t) : deBoor(c.points, curveDegree(c), sp + t); };
        p0 = at(0.0f);
        p3 = at(1.0f);
        const float3 q1 = at(1.0f / 3.0f) * 27.0f - p0 * 8.0f - p3, q2 = at(2.0f / 3.0f) * 27.0f - p0 - p3 * 8.0f;
        p1 = (q1 * 2.0f - q2) * (1.0f / 18.0f);
        p2 = (q2 * 2.0f - q1) * (1.0f / 18.0f);
    };
    for (int sp = 0; sp < spans; ++sp) {
        float3 p0, p1, p2, p3;
        span(sp, p0, p1, p2, p3);
        if (b.points.empty()) { b.points.push_back(p0); b.hin.push_back(p0); b.hout.push_back(p1); }
        else b.hout.back() = p1;
        const bool wraps = c.closed && sp == spans - 1;   // the last span of a closed curve ends on the first anchor
        if (wraps) b.hin[0] = p2;
        else { b.points.push_back(p3); b.hin.push_back(p2); b.hout.push_back(p3); }
    }
    b.closed = c.closed;
    b.broken.assign(b.points.size(), 0);
    if (c.type == CURVE_LINEAR || curveDegree(c) == 1) std::fill(b.broken.begin(), b.broken.end(), (uint8_t)1);
    return b;
}

CurveData transformCurve(const CurveData& c, const Affine& xf) {
    CurveData r = c;
    for (float3& p : r.points) p = xf.point(p);
    for (float3& p : r.hin) p = xf.point(p);
    for (float3& p : r.hout) p = xf.point(p);
    return r;
}

} // namespace vp3d
