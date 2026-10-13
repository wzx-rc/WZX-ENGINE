// vp3d - small math library shared by the viewport module and apps.
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace vp3d {

constexpr float kPi = 3.14159265358979323846f;
inline float radians(float d) { return d * kPi / 180.0f; }
inline float degrees(float r) { return r * 180.0f / kPi; }
inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

struct float2 {
    float x = 0, y = 0;
    float2() = default;
    constexpr float2(float x_, float y_) : x(x_), y(y_) {}
    float2 operator+(float2 o) const { return {x + o.x, y + o.y}; }
    float2 operator-(float2 o) const { return {x - o.x, y - o.y}; }
    float2 operator*(float s) const { return {x * s, y * s}; }
};

struct float3 {
    float x = 0, y = 0, z = 0;
    float3() = default;
    constexpr float3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    explicit constexpr float3(float s) : x(s), y(s), z(s) {}
    float3 operator+(const float3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    float3 operator-(const float3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    float3 operator*(const float3& o) const { return {x * o.x, y * o.y, z * o.z}; }
    float3 operator*(float s) const { return {x * s, y * s, z * s}; }
    float3 operator/(float s) const { return {x / s, y / s, z / s}; }
    float3 operator-() const { return {-x, -y, -z}; }
    float3& operator+=(const float3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    float3& operator-=(const float3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    float3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    float operator[](int i) const { return (&x)[i]; }
    float& operator[](int i) { return (&x)[i]; }
};
inline float3 operator*(float s, const float3& v) { return v * s; }
inline float dot(const float3& a, const float3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float3 cross(const float3& a, const float3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(const float3& v) { return std::sqrt(dot(v, v)); }
inline float3 normalize(const float3& v) { float l = length(v); return l > 0 ? v / l : float3(0, 1, 0); }
inline float3 minv(const float3& a, const float3& b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }
inline float3 maxv(const float3& a, const float3& b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }
inline float3 lerp(const float3& a, const float3& b, float t) { return a + (b - a) * t; }

struct float4 {
    float x = 0, y = 0, z = 0, w = 0;
    float4() = default;
    constexpr float4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
};

// Quaternions are stored in a float4 (x, y, z, w), identity = (0, 0, 0, 1).
inline float4 qIdentity() { return float4(0, 0, 0, 1); }
inline float4 qAxisAngle(const float3& axis, float angle) {
    float3 a = normalize(axis);
    float s = std::sin(angle * 0.5f);
    return float4(a.x * s, a.y * s, a.z * s, std::cos(angle * 0.5f));
}
inline float4 qMul(const float4& a, const float4& b) {
    return float4(a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                  a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);
}
inline float3 qRot(const float4& q, const float3& v) {
    float3 u(q.x, q.y, q.z);
    float3 t = cross(u, v) * 2.0f;
    return v + t * q.w + cross(u, t);
}
inline float4 qNormalize(const float4& q) {
    float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return l > 0 ? float4(q.x / l, q.y / l, q.z / l, q.w / l) : qIdentity();
}
inline float4 qSlerp(const float4& a, float4 b, float t) {
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0) { b = float4(-b.x, -b.y, -b.z, -b.w); d = -d; }
    if (d > 0.9995f) return qNormalize(float4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t));
    float th = std::acos(d), s = std::sin(th);
    float wa = std::sin((1 - t) * th) / s, wb = std::sin(t * th) / s;
    return float4(a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb);
}
// Quaternion from an orthonormal basis (columns X, Y, Z).
inline float4 qFromBasis(const float3& X, const float3& Y, const float3& Z) {
    float m00 = X.x, m10 = X.y, m20 = X.z, m01 = Y.x, m11 = Y.y, m21 = Y.z, m02 = Z.x, m12 = Z.y, m22 = Z.z;
    float tr = m00 + m11 + m22;
    float4 q;
    if (tr > 0) {
        float s = std::sqrt(tr + 1.0f) * 2;
        q = float4((m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s);
    } else if (m00 > m11 && m00 > m22) {
        float s = std::sqrt(1.0f + m00 - m11 - m22) * 2;
        q = float4(0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s);
    } else if (m11 > m22) {
        float s = std::sqrt(1.0f + m11 - m00 - m22) * 2;
        q = float4((m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s);
    } else {
        float s = std::sqrt(1.0f + m22 - m00 - m11) * 2;
        q = float4((m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s);
    }
    return qNormalize(q);
}
// Keeps orientations exact: every axis of the rotated frame is rounded to the nearest world axis.
inline float4 qSnap90(const float4& q) {
    auto snapAxis = [](float3 v) {
        float ax = std::fabs(v.x), ay = std::fabs(v.y), az = std::fabs(v.z);
        if (ax >= ay && ax >= az) return float3(v.x < 0 ? -1.0f : 1.0f, 0, 0);
        if (ay >= az) return float3(0, v.y < 0 ? -1.0f : 1.0f, 0);
        return float3(0, 0, v.z < 0 ? -1.0f : 1.0f);
    };
    float3 X = snapAxis(qRot(q, float3(1, 0, 0)));
    float3 Y = snapAxis(qRot(q, float3(0, 1, 0)));
    float3 Z = cross(X, Y);   // right-handed frame
    return qFromBasis(X, Y, Z);
}

// sRGB (0-255) to linear helper used for colour palettes.
inline float srgbToLinear(float c) {
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}
inline float3 rgb255(int r, int g, int b) {
    return {srgbToLinear(r / 255.0f), srgbToLinear(g / 255.0f), srgbToLinear(b / 255.0f)};
}

// Affine transform stored as 3 rows of 4 (row-major, translation in column 3),
// identical to the layout D3D12_RAYTRACING_INSTANCE_DESC::Transform expects.
struct Affine {
    float m[3][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}};

    static Affine fromBasis(const float3& X, const float3& Y, const float3& Z, const float3& T) {
        Affine a;
        a.m[0][0] = X.x; a.m[0][1] = Y.x; a.m[0][2] = Z.x; a.m[0][3] = T.x;
        a.m[1][0] = X.y; a.m[1][1] = Y.y; a.m[1][2] = Z.y; a.m[1][3] = T.y;
        a.m[2][0] = X.z; a.m[2][1] = Y.z; a.m[2][2] = Z.z; a.m[2][3] = T.z;
        return a;
    }
    // T * Ry(yaw) * S
    static Affine trs(const float3& t, float yawRad, const float3& s) {
        float c = std::cos(yawRad), sn = std::sin(yawRad);
        return fromBasis(float3(c, 0, -sn) * s.x, float3(0, 1, 0) * s.y, float3(sn, 0, c) * s.z, t);
    }
    // T * R(q) * S (scale applied in the local frame)
    static Affine trsQuat(const float3& t, const float4& q, const float3& s) {
        return fromBasis(qRot(q, float3(1, 0, 0)) * s.x, qRot(q, float3(0, 1, 0)) * s.y, qRot(q, float3(0, 0, 1)) * s.z, t);
    }
    float3 point(const float3& p) const {
        return {m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z + m[0][3],
                m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z + m[1][3],
                m[2][0] * p.x + m[2][1] * p.y + m[2][2] * p.z + m[2][3]};
    }
    float3 vector(const float3& v) const {
        return {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
                m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
                m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z};
    }
    Affine inverse() const {
        // General 3x3 inverse + translation.
        float a = m[0][0], b = m[0][1], c = m[0][2];
        float d = m[1][0], e = m[1][1], f = m[1][2];
        float g = m[2][0], h = m[2][1], i = m[2][2];
        float A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
        float det = a * A + b * B + c * C;
        float id = std::fabs(det) > 1e-20f ? 1.0f / det : 0.0f;
        Affine r;
        r.m[0][0] = A * id; r.m[0][1] = -(b * i - c * h) * id; r.m[0][2] = (b * f - c * e) * id;
        r.m[1][0] = B * id; r.m[1][1] = (a * i - c * g) * id;  r.m[1][2] = -(a * f - c * d) * id;
        r.m[2][0] = C * id; r.m[2][1] = -(a * h - b * g) * id; r.m[2][2] = (a * e - b * d) * id;
        float3 t(m[0][3], m[1][3], m[2][3]);
        float3 it = r.vector(t);
        r.m[0][3] = -it.x; r.m[1][3] = -it.y; r.m[2][3] = -it.z;
        return r;
    }
};

// Small deterministic RNG for scene generation.
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed = 1) : s(seed ? seed : 1) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uniform() { return (next() & 0xFFFFFF) / 16777216.0f; }
    float range(float a, float b) { return a + (b - a) * uniform(); }
};

} // namespace vp3d
