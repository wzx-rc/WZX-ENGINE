#include "MeshGen.h"

namespace vp3d {

void MeshData::append(const MeshData& o, const Affine& xf) {
    uint32_t base = (uint32_t)vertices.size();
    for (const Vertex& v : o.vertices)
        vertices.push_back({xf.point(v.pos), normalize(xf.vector(v.nrm))});
    for (uint32_t i : o.indices) indices.push_back(base + i);
    for (uint32_t i : o.edges) edges.push_back(base + i);
}

void MeshData::quadEdges() {
    edges.clear();
    std::vector<uint64_t> seen;
    auto add = [&](uint32_t a, uint32_t b) {
        if (a == b) return;
        if (a > b) std::swap(a, b);
        seen.push_back((uint64_t)a << 32 | b);
    };
    auto shares = [](const uint32_t* t, uint32_t a, uint32_t b) {
        bool ha = t[0] == a || t[1] == a || t[2] == a, hb = t[0] == b || t[1] == b || t[2] == b;
        return ha && hb;
    };
    size_t triCount = indices.size() / 3;
    for (size_t t = 0; t < triCount; t += 2) {
        const uint32_t* A = &indices[t * 3];
        if (t + 1 >= triCount) { add(A[0], A[1]); add(A[1], A[2]); add(A[2], A[0]); break; }
        const uint32_t* B = &indices[t * 3 + 3];
        for (const uint32_t* T : {A, B}) {
            const uint32_t* O = T == A ? B : A;
            for (int k = 0; k < 3; ++k) {
                uint32_t a = T[k], b = T[(k + 1) % 3];
                if (!shares(O, a, b)) add(a, b);   // the shared edge is the diagonal of the quad
            }
        }
    }
    std::sort(seen.begin(), seen.end());
    seen.erase(std::unique(seen.begin(), seen.end()), seen.end());
    edges.reserve(seen.size() * 2);
    for (uint64_t e : seen) { edges.push_back((uint32_t)(e >> 32)); edges.push_back((uint32_t)e); }
}

void MeshData::addQuad(const float3& a, const float3& b, const float3& c, const float3& d, const float3& n) {
    uint32_t base = (uint32_t)vertices.size();
    vertices.push_back({a, n});
    vertices.push_back({b, n});
    vertices.push_back({c, n});
    vertices.push_back({d, n});
    addTri(base, base + 1, base + 2);
    addTri(base, base + 2, base + 3);
}

void MeshData::bounds(float3& mn, float3& mx) const {
    mn = float3(1e30f);
    mx = float3(-1e30f);
    for (const Vertex& v : vertices) { mn = minv(mn, v.pos); mx = maxv(mx, v.pos); }
}

MeshData makeBox(const float3& a, const float3& b) {
    MeshData m;
    // -X / +X
    m.addQuad({a.x, a.y, a.z}, {a.x, a.y, b.z}, {a.x, b.y, b.z}, {a.x, b.y, a.z}, {-1, 0, 0});
    m.addQuad({b.x, a.y, b.z}, {b.x, a.y, a.z}, {b.x, b.y, a.z}, {b.x, b.y, b.z}, {1, 0, 0});
    // -Y / +Y
    m.addQuad({a.x, a.y, a.z}, {b.x, a.y, a.z}, {b.x, a.y, b.z}, {a.x, a.y, b.z}, {0, -1, 0});
    m.addQuad({a.x, b.y, b.z}, {b.x, b.y, b.z}, {b.x, b.y, a.z}, {a.x, b.y, a.z}, {0, 1, 0});
    // -Z / +Z
    m.addQuad({b.x, a.y, a.z}, {a.x, a.y, a.z}, {a.x, b.y, a.z}, {b.x, b.y, a.z}, {0, 0, -1});
    m.addQuad({a.x, a.y, b.z}, {b.x, a.y, b.z}, {b.x, b.y, b.z}, {a.x, b.y, b.z}, {0, 0, 1});
    m.quadEdges();
    return m;
}

MeshData makeQuadXY() {
    MeshData m;
    m.addQuad({-0.5f, -0.5f, 0}, {0.5f, -0.5f, 0}, {0.5f, 0.5f, 0}, {-0.5f, 0.5f, 0}, {0, 0, 1});
    return m;
}

MeshData makePlane(float s) {
    MeshData m;
    float h = s * 0.5f;
    m.addQuad({-h, 0, h}, {h, 0, h}, {h, 0, -h}, {-h, 0, -h}, {0, 1, 0});
    m.quadEdges();
    return m;
}

MeshData makeSphere(float r, int segU, int segV) {
    MeshData m;
    for (int j = 0; j <= segV; ++j) {
        float v = (float)j / segV;
        float th = v * kPi;
        for (int i = 0; i <= segU; ++i) {
            float u = (float)i / segU;
            float ph = u * 2.0f * kPi;
            float3 n(std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph));
            m.vertices.push_back({n * r, n});
        }
    }
    for (int j = 0; j < segV; ++j)
        for (int i = 0; i < segU; ++i) {
            uint32_t a = j * (segU + 1) + i, b = a + segU + 1;
            m.addTri(a, b, a + 1);
            m.addTri(a + 1, b, b + 1);
        }
    m.quadEdges();
    return m;
}

MeshData makeCylinder(float r, float y0, float y1, int seg, bool capTop, bool capBottom) {
    std::vector<LathePoint> p;
    if (capBottom) p.push_back({0, y0});
    p.push_back({r, y0});
    p.push_back({r, y1});
    if (capTop) p.push_back({0, y1});
    return makeLathe(p, seg, 30.0f);
}

MeshData makeLathe(const std::vector<LathePoint>& prof, int seg, float smoothDeg) {
    MeshData m;
    size_t n = prof.size();
    if (n < 2) return m;
    bool closed = std::fabs(prof[0].r - prof[n - 1].r) < 1e-6f && std::fabs(prof[0].y - prof[n - 1].y) < 1e-6f;
    size_t segCount = n - 1;
    std::vector<float2> sn(segCount);
    std::vector<bool> valid(segCount);
    for (size_t i = 0; i < segCount; ++i) {
        float dr = prof[i + 1].r - prof[i].r, dy = prof[i + 1].y - prof[i].y;
        float l = std::sqrt(dr * dr + dy * dy);
        valid[i] = l > 1e-7f;
        sn[i] = valid[i] ? float2(dy / l, -dr / l) : float2(0, 1);
    }
    float cosSmooth = std::cos(radians(smoothDeg));
    auto prevValid = [&](long i) -> long {
        for (long k = 1; k <= (long)segCount; ++k) {
            long j = i - k;
            if (j < 0) { if (!closed) return -1; j += (long)segCount; }
            if (valid[j]) return j;
        }
        return -1;
    };
    auto nextValid = [&](long i) -> long {
        for (long k = 1; k <= (long)segCount; ++k) {
            long j = i + k;
            if (j >= (long)segCount) { if (!closed) return -1; j -= (long)segCount; }
            if (valid[j]) return j;
        }
        return -1;
    };
    auto blend = [&](float2 a, long other) -> float2 {
        if (other < 0) return a;
        float2 b = sn[other];
        if (a.x * b.x + a.y * b.y < cosSmooth) return a;
        float2 s = a + b;
        float l = std::sqrt(s.x * s.x + s.y * s.y);
        return l > 1e-6f ? s * (1.0f / l) : a;
    };
    for (size_t i = 0; i < segCount; ++i) {
        if (!valid[i]) continue;
        float2 n0 = blend(sn[i], prevValid((long)i));
        float2 n1 = blend(sn[i], nextValid((long)i));
        uint32_t base = (uint32_t)m.vertices.size();
        for (int k = 0; k <= seg; ++k) {
            float a = 2.0f * kPi * k / seg;
            float c = std::cos(a), s = std::sin(a);
            m.vertices.push_back({{prof[i].r * c, prof[i].y, prof[i].r * s}, normalize(float3(n0.x * c, n0.y, n0.x * s))});
            m.vertices.push_back({{prof[i + 1].r * c, prof[i + 1].y, prof[i + 1].r * s}, normalize(float3(n1.x * c, n1.y, n1.x * s))});
        }
        for (int k = 0; k < seg; ++k) {
            uint32_t a0 = base + k * 2, a1 = a0 + 1, b0 = a0 + 2, b1 = a0 + 3;
            m.addTri(a0, a1, b1);
            m.addTri(a0, b1, b0);
        }
    }
    m.quadEdges();
    return m;
}

MeshData makeTorusProfile(float R, float r, int segProfile, std::vector<LathePoint>& out) {
    out.clear();
    for (int i = 0; i <= segProfile; ++i) {
        float t = -kPi * 0.5f + 2.0f * kPi * i / segProfile;
        out.push_back({R + r * std::cos(t), r * std::sin(t)});
    }
    out.back() = out.front();
    return MeshData();
}

MeshData makeShaderBall(float R) {
    MeshData m;
    // Pedestal: disc with a raised lip, like a material preview ball.
    std::vector<LathePoint> base = {
        {0, 0}, {R * 1.05f, 0}, {R * 1.1f, R * 0.04f}, {R * 1.1f, R * 0.1f}, {R * 1.05f, R * 0.14f},
        {R * 0.98f, R * 0.14f}, {R * 0.95f, R * 0.08f}, {R * 0.55f, R * 0.08f}, {R * 0.45f, R * 0.16f}, {0, R * 0.16f}};
    m.append(makeLathe(base, 96, 35.0f));
    float cy = R * 1.12f;
    m.append(makeSphere(R, 96, 48), Affine::fromBasis({1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, cy, 0}));
    // Side rings (the classic shader-ball cut-outs)
    std::vector<LathePoint> torus;
    makeTorusProfile(R * 0.56f, R * 0.07f, 24, torus);
    MeshData ring = makeLathe(torus, 72, 60.0f);
    float off = std::sqrt(R * R - (R * 0.56f) * (R * 0.56f)) - R * 0.01f;
    Affine toX = Affine::fromBasis({0, -1, 0}, {1, 0, 0}, {0, 0, 1}, {off, cy, 0});
    Affine toNegX = Affine::fromBasis({0, 1, 0}, {-1, 0, 0}, {0, 0, 1}, {-off, cy, 0});
    m.append(ring, toX);
    m.append(ring, toNegX);
    return m;
}

// --------------------------------------------------------------------------- parametric primitives

// Grid of nu x nv quads from o along u and v, flat normal n (cross(u, v) points along n).
static void addGrid(MeshData& m, const float3& o, const float3& u, const float3& v, const float3& n, int nu, int nv) {
    uint32_t base = (uint32_t)m.vertices.size();
    for (int j = 0; j <= nv; ++j)
        for (int i = 0; i <= nu; ++i) {
            float fu = (float)i / nu, fv = (float)j / nv;
            m.vertices.push_back({o + u * fu + v * fv, n, float2(fu, fv)});
        }
    for (int j = 0; j < nv; ++j)
        for (int i = 0; i < nu; ++i) {
            uint32_t a = base + j * (nu + 1) + i, b = a + 1, c = b + nu + 1, d = a + nu + 1;
            m.addTri(a, b, c);
            m.addTri(a, c, d);
        }
}

MeshData makePrimitive(const PrimitiveParams& p) {
    const float w = std::max(p.size.x, 1e-3f), h = std::max(p.size.y, 1e-3f), d = std::max(p.size.z, 1e-3f);
    const float r = std::max(p.radius, 1e-3f);
    const int cuts = std::clamp(p.subdiv, 1, 64);
    MeshData m;
    switch (p.type) {
    case PRIM_CUBE: {
        const float ax = -w / 2, bx = w / 2, az = -d / 2, bz = d / 2;
        addGrid(m, {bx, 0, bz}, {0, 0, -d}, {0, h, 0}, {1, 0, 0}, cuts, cuts);    // +X
        addGrid(m, {ax, 0, az}, {0, 0, d}, {0, h, 0}, {-1, 0, 0}, cuts, cuts);    // -X
        addGrid(m, {ax, h, bz}, {w, 0, 0}, {0, 0, -d}, {0, 1, 0}, cuts, cuts);    // +Y
        addGrid(m, {ax, 0, az}, {w, 0, 0}, {0, 0, d}, {0, -1, 0}, cuts, cuts);    // -Y
        addGrid(m, {ax, 0, bz}, {w, 0, 0}, {0, h, 0}, {0, 0, 1}, cuts, cuts);     // +Z
        addGrid(m, {bx, 0, az}, {-w, 0, 0}, {0, h, 0}, {0, 0, -1}, cuts, cuts);   // -Z
        break;
    }
    case PRIM_PLANE:
        addGrid(m, {-w / 2, 0, d / 2}, {w, 0, 0}, {0, 0, -d}, {0, 1, 0}, cuts, cuts);
        break;
    case PRIM_SPHERE:
        m.append(makeSphere(r, std::clamp(p.segments, 3, 256), std::clamp(p.rings, 2, 128)), Affine::trs(float3(0, r, 0), 0.0f, float3(1, 1, 1)));
        for (size_t i = 0; i + 2 < m.indices.size(); i += 3) std::swap(m.indices[i + 1], m.indices[i + 2]);   // makeSphere winds clockwise
        break;
    case PRIM_CYLINDER: {
        std::vector<LathePoint> prof;
        prof.push_back({0, 0});
        for (int k = 0; k <= cuts; ++k) prof.push_back({r, h * k / cuts});
        prof.push_back({0, h});
        m = makeLathe(prof, std::clamp(p.segments, 3, 256), 30.0f);
        break;
    }
    case PRIM_TORUS: {
        // lying flat (axis Y), resting on y = 0
        const float R = r, t = std::max(p.radius2, 1e-3f);
        const int nu = std::clamp(p.segments, 3, 256), nv = std::clamp(p.rings, 3, 128);
        for (int i = 0; i <= nu; ++i) {
            float a = 2.0f * kPi * i / nu, ca = std::cos(a), sa = std::sin(a);
            for (int j = 0; j <= nv; ++j) {
                float b = 2.0f * kPi * j / nv, cb = std::cos(b), sb = std::sin(b);
                float3 n(cb * ca, sb, cb * sa);
                m.vertices.push_back({float3(R * ca, t, R * sa) + n * t, n, float2((float)i / nu, (float)j / nv)});
            }
        }
        for (int i = 0; i < nu; ++i)
            for (int j = 0; j < nv; ++j) {
                uint32_t a = i * (nv + 1) + j, b = a + nv + 1;
                m.addTri(a, a + 1, b + 1);
                m.addTri(a, b + 1, b);
            }
        break;
    }
    case PRIM_DISK: {
        std::vector<LathePoint> prof;   // outside in: the lathe then faces up
        for (int k = cuts; k >= 0; --k) prof.push_back({r * k / cuts, 0});
        m = makeLathe(prof, std::clamp(p.segments, 3, 256), 30.0f);
        break;
    }
    }
    m.quadEdges();   // every primitive is built as triangle pairs: the wireframe shows its quads
    return m;
}

} // namespace vp3d
