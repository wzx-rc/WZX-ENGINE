#include "Scene.h"

namespace vp3d {

void AreaLight::basis(float3& U, float3& V, float3& N) const {
    N = normalize(target - position);
    float3 ref = std::fabs(N.y) > 0.99f ? float3(0, 0, 1) : float3(0, 1, 0);
    U = normalize(cross(ref, N));
    V = cross(N, U);
}

float3 Camera::eye() const {
    float cp = std::cos(pitch);
    return target + float3(cp * std::sin(yaw), std::sin(pitch), cp * std::cos(yaw)) * distance;
}

void Camera::basis(float3& right, float3& up, float3& fwd) const {
    fwd = normalize(target - eye());
    right = normalize(cross(fwd, float3(0, 1, 0)));
    up = cross(right, fwd);
}

float Camera::apertureRadius() const {
    if (!dof) return 0.0f;
    float diameterMeters = (focalLengthMm / std::max(fStop, 0.5f)) * 0.001f;
    return 0.5f * diameterMeters * sensorScale;
}

size_t Scene::triangleCount() const {
    size_t n = 0;
    for (const Object& o : objects)
        if (o.visible && o.mesh >= 0 && o.mesh < (int)meshes.size()) n += meshes[o.mesh].triangleCount();
    return n;
}

static bool rayTri(const float3& o, const float3& d, const float3& a, const float3& b, const float3& c, float& t) {
    float3 e1 = b - a, e2 = c - a;
    float3 p = cross(d, e2);
    float det = dot(e1, p);
    if (std::fabs(det) < 1e-12f) return false;
    float inv = 1.0f / det;
    float3 s = o - a;
    float u = dot(s, p) * inv;
    if (u < 0 || u > 1) return false;
    float3 q = cross(s, e1);
    float v = dot(d, q) * inv;
    if (v < 0 || u + v > 1) return false;
    float tt = dot(e2, q) * inv;
    if (tt <= 1e-6f || tt >= t) return false;
    t = tt;
    return true;
}

static bool rayBox(const float3& o, const float3& d, const float3& mn, const float3& mx, float tmax) {
    float t0 = 0, t1 = tmax;
    for (int i = 0; i < 3; ++i) {
        float inv = 1.0f / (std::fabs(d[i]) > 1e-12f ? d[i] : 1e-12f);
        float a = (mn[i] - o[i]) * inv, b = (mx[i] - o[i]) * inv;
        if (a > b) std::swap(a, b);
        t0 = std::max(t0, a);
        t1 = std::min(t1, b);
        if (t0 > t1) return false;
    }
    return true;
}

PickResult pick(const Scene& s, const float3& origin, const float3& dir) {
    PickResult r;
    for (size_t oi = 0; oi < s.objects.size(); ++oi) {
        const Object& ob = s.objects[oi];
        if (!ob.visible || !ob.selectable || ob.ghost || ob.mesh < 0 || ob.mesh >= (int)s.meshes.size()) continue;
        const MeshData& m = s.meshes[ob.mesh];
        Affine inv = ob.transform().inverse();
        float3 lo = inv.point(origin);
        float3 ld = inv.vector(dir);  // not normalised: t stays in world units
        float3 mn, mx;
        m.bounds(mn, mx);
        if (!rayBox(lo, ld, mn - float3(1e-4f), mx + float3(1e-4f), r.t)) continue;
        float t = r.t;
        int hitTri = -1;
        for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
            if (rayTri(lo, ld, m.vertices[m.indices[i]].pos, m.vertices[m.indices[i + 1]].pos,
                       m.vertices[m.indices[i + 2]].pos, t))
                hitTri = (int)i;
        }
        if (hitTri >= 0) {
            r.t = t;
            r.object = (int)oi;
            r.position = origin + dir * t;
            const float3& a = m.vertices[m.indices[hitTri]].pos;
            const float3& b = m.vertices[m.indices[hitTri + 1]].pos;
            const float3& c = m.vertices[m.indices[hitTri + 2]].pos;
            float3 n = normalize(ob.transform().vector(cross(b - a, c - a)));
            if (dot(n, dir) > 0) n = -n;
            r.normal = n;
        }
    }
    return r;
}

} // namespace vp3d
