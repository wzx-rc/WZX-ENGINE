// vp3d progressive path tracer (inline ray tracing, DXR 1.1).
// Features: GGX/Lambert BSDF with VNDF sampling, area lights with MIS,
// rough dielectrics, random-walk subsurface scattering, Cycles-style bevel
// normals, thin-lens depth of field, procedural ground grid.
#include "Common.hlsli"

struct Hit {
    float t;
    float3 P;
    float3 N;    // shading normal, facing the incoming ray side
    float3 Ng;   // geometric normal, facing the incoming ray side
    uint inst;
    bool front;  // ray hit the outward side
    float2 uv;   // texture coordinates of the hit point
};

struct Surf {
    float3 base;
    float rough;
    float metal;
    float spec;
    float trans;
    float ior;
    float sss;
    float3 sssR;
    float3 emission;
    float bevel;
    float coat;
    float coatRough;
    uint flags;
};

// ---------------------------------------------------------------------------
// Geometry

float3 OffsetRay(float3 p, float3 n) {
    float s = 1e-4 * (1.0 + max(abs(p.x), max(abs(p.y), abs(p.z))));
    return p + n * s;
}

void FetchTriangle(uint inst, uint prim, out GpuVertex v0, out GpuVertex v1, out GpuVertex v2) {
    GpuInstance gi = gInst[inst];
    uint b = gi.indexOffset + prim * 3;
    v0 = gVerts[gIndices[b + 0] + gi.vertexOffset];
    v1 = gVerts[gIndices[b + 1] + gi.vertexOffset];
    v2 = gVerts[gIndices[b + 2] + gi.vertexOffset];
}

// Interpolated object normal transformed to world space (outward, unflipped).
float3 WorldNormal(uint inst, uint prim, float2 bc, float3x4 w2o, out float3 ngOut) {
    GpuVertex v0, v1, v2;
    FetchTriangle(inst, prim, v0, v1, v2);
    float3 ns = v0.nrm * (1.0 - bc.x - bc.y) + v1.nrm * bc.x + v2.nrm * bc.y;
    float3 ng = cross(v1.pos - v0.pos, v2.pos - v0.pos);
    float3x3 m = (float3x3)w2o;
    ns = normalize(mul(ns, m));
    ng = normalize(mul(ng, m));
    if (dot(ng, ns) < 0) ng = -ng;
    if (F.flags & FRAME_FLAG_FLAT_SHADING) ns = ng;
    ngOut = ng;
    return ns;
}

bool TraceClosest(float3 o, float3 d, float tmax, uint mask, out Hit h) {
    RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
    RayDesc r;
    r.Origin = o; r.Direction = d; r.TMin = 0.0; r.TMax = tmax;
    q.TraceRayInline(gScene, RAY_FLAG_NONE, mask, r);
    q.Proceed();
    h = (Hit)0;
    if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) return false;
    h.t = q.CommittedRayT();
    h.inst = q.CommittedInstanceID();
    float3 ng;
    float3 ns = WorldNormal(h.inst, q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics(),
                            q.CommittedWorldToObject3x4(), ng);
    h.front = dot(ng, d) < 0;
    if (!h.front) { ng = -ng; ns = -ns; }
    if (dot(ns, -d) <= 0.0) ns = ng;
    h.P = o + d * h.t;
    h.N = ns;
    {
        GpuVertex v0, v1, v2;
        FetchTriangle(h.inst, q.CommittedPrimitiveIndex(), v0, v1, v2);
        float2 bc = q.CommittedTriangleBarycentrics();
        h.uv = v0.uv * (1.0 - bc.x - bc.y) + v1.uv * bc.x + v2.uv * bc.y;
    }
    h.Ng = ng;
    return true;
}

bool Occluded(float3 o, float3 d, float tmax) {
    RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;
    RayDesc r;
    r.Origin = o; r.Direction = d; r.TMin = 0.0; r.TMax = tmax;
    q.TraceRayInline(gScene, RAY_FLAG_NONE, MASK_OPAQUE, r);
    q.Proceed();
    return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}

void Basis(float3 n, out float3 t, out float3 b) {
    float s = n.z >= 0 ? 1.0 : -1.0;
    float a = -1.0 / (s + n.z);
    float c = n.x * n.y * a;
    t = float3(1.0 + s * n.x * n.x * a, s * c, -s * n.x);
    b = float3(c, s + n.y * n.y * a, -n.y);
}

float3 CosineHemisphere(float3 n, float2 u) {
    float r = sqrt(u.x), phi = 2.0 * PI * u.y;
    float3 t, b;
    Basis(n, t, b);
    return normalize(t * (r * cos(phi)) + b * (r * sin(phi)) + n * sqrt(max(0.0, 1.0 - u.x)));
}

float3 UniformSphere(float2 u) {
    float z = 1.0 - 2.0 * u.x;
    float r = sqrt(max(0.0, 1.0 - z * z));
    float phi = 2.0 * PI * u.y;
    return float3(r * cos(phi), r * sin(phi), z);
}

float2 ConcentricDisk(float2 u) {
    float2 o = 2.0 * u - 1.0;
    if (o.x == 0 && o.y == 0) return 0;
    float r, th;
    if (abs(o.x) > abs(o.y)) { r = o.x; th = (PI / 4.0) * (o.y / o.x); }
    else { r = o.y; th = PI / 2.0 - (PI / 4.0) * (o.x / o.y); }
    return r * float2(cos(th), sin(th));
}

// ---------------------------------------------------------------------------
// Bevel shader: estimates a rounded-edge normal by probing the same object
// around the shading point along three axes (similar to Cycles' Bevel node).

float3 BevelNormal(Hit h, float radius, uint samples, inout uint rng) {
    float3 Ng = h.Ng;
    float3 T, B;
    Basis(Ng, T, B);
    float3 sum = 0;
    for (uint i = 0; i < samples; ++i) {
        float u = Rnd(rng);
        float3 axis, a1, a2;
        if (u < 0.5) { axis = Ng; a1 = T; a2 = B; }
        else if (u < 0.75) { axis = T; a1 = B; a2 = Ng; }
        else { axis = B; a1 = Ng; a2 = T; }
        float2 dsk = ConcentricDisk(Rnd2(rng)) * radius;
        float3 o = h.P + a1 * dsk.x + a2 * dsk.y + axis * radius;

        RayQuery<RAY_FLAG_FORCE_NON_OPAQUE> q;
        RayDesc r;
        r.Origin = o; r.Direction = -axis; r.TMin = 0.0; r.TMax = 2.0 * radius;
        q.TraceRayInline(gScene, RAY_FLAG_NONE, MASK_OPAQUE | MASK_GLASS, r);
        while (q.Proceed()) {
            if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE && q.CandidateInstanceID() == h.inst)
                q.CommitNonOpaqueTriangleHit();
        }
        if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) continue;
        float3 hp = o - axis * q.CommittedRayT();
        float d = length(hp - h.P);
        if (d > radius) continue;
        float3 ng;
        float3 nh = WorldNormal(h.inst, q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics(),
                                q.CommittedWorldToObject3x4(), ng);
        if (!h.front) nh = -nh;
        float pN = 0.5 * abs(dot(Ng, nh)), pT = 0.25 * abs(dot(T, nh)), pB = 0.25 * abs(dot(B, nh));
        float w = (1.0 - d / radius) / max(pN + pT + pB, 1e-4);
        sum += nh * w;
    }
    if (dot(sum, sum) < 1e-12) return h.N;
    float3 n = normalize(sum);
    // Keep the original smooth shading contribution for curved surfaces.
    n = normalize(n + h.N * 0.15);
    return dot(n, Ng) > 0.05 ? n : h.N;
}

// ---------------------------------------------------------------------------
// Materials

Surf MakeSurf(GpuMaterial m, Hit h) {
    Surf s;
    s.base = m.baseColor;
    if (m.baseColorTex != NO_TEXTURE)
        s.base *= gTextures[NonUniformResourceIndex(m.baseColorTex)].SampleLevel(gLinearWrap, h.uv, 0).rgb;
    s.rough = m.roughness;
    s.metal = m.metallic;
    s.spec = m.specular;
    s.trans = m.transmission;
    s.ior = m.ior;
    s.sss = m.subsurface;
    s.sssR = m.sssRadius;
    s.emission = m.emission;
    s.bevel = m.bevelRadius;
    s.coat = m.coat;
    s.coatRough = m.coatRoughness;
    s.flags = m.flags;
    if (m.flags & MAT_FLAG_GRID) {
        float g = F.gridScale;
        float dist = length(h.P - F.camPos);
        float lw = max(0.02, 0.0009 * dist / g);
        float2 p = h.P.xz / g;
        float2 d = abs(frac(p + 0.5) - 0.5);
        float2 dm = abs(frac(p / 10.0 + 0.5) - 0.5) * 10.0;
        float minor = (min(d.x, d.y) < lw) ? 1.0 : 0.0;
        float major = (min(dm.x, dm.y) < lw * 1.6) ? 1.0 : 0.0;
        float fade = saturate(1.0 - dist / (g * 160.0));
        s.base = lerp(s.base, s.base * 2.2 + 0.015, max(minor * 0.55, major) * fade);
        float axisW = lw * 1.8 * g;
        if (abs(h.P.z) < axisW) { s.base = float3(0.5, 0.06, 0.05); s.emission += float3(0.25, 0.02, 0.02); }
        if (abs(h.P.x) < axisW) { s.base = float3(0.06, 0.12, 0.6); s.emission += float3(0.02, 0.05, 0.3); }
    }
    return s;
}

float3 SchlickF(float3 f0, float c) {
    float t = pow(saturate(1.0 - c), 5.0);
    return f0 + (1.0 - f0) * t;
}

float D_GGX(float NoH, float a) {
    float a2 = a * a;
    float d = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d + 1e-12);
}

float G1_Smith(float NoX, float a) {
    float a2 = a * a;
    return 2.0 * NoX / (NoX + sqrt(a2 + (1.0 - a2) * NoX * NoX));
}

float G2_Smith(float NoL, float NoV, float a) {
    float a2 = a * a;
    float l = NoV * sqrt(a2 + (1.0 - a2) * NoL * NoL);
    float v = NoL * sqrt(a2 + (1.0 - a2) * NoV * NoV);
    return 2.0 * NoL * NoV / (l + v + 1e-12);
}

// Heitz 2018 visible normal sampling, local frame (z = normal).
float3 SampleVNDF(float3 Ve, float a, float2 u) {
    float3 Vh = normalize(float3(a * Ve.x, a * Ve.y, Ve.z));
    float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    float3 T1 = lensq > 0 ? float3(-Vh.y, Vh.x, 0) * rsqrt(lensq) : float3(1, 0, 0);
    float3 T2 = cross(Vh, T1);
    float r = sqrt(u.x), phi = 2.0 * PI * u.y;
    float t1 = r * cos(phi), t2 = r * sin(phi);
    float s = 0.5 * (1.0 + Vh.z);
    t2 = (1.0 - s) * sqrt(max(0.0, 1.0 - t1 * t1)) + s * t2;
    float3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0, 1.0 - t1 * t1 - t2 * t2)) * Vh;
    return normalize(float3(a * Nh.x, a * Nh.y, max(1e-6, Nh.z)));
}

float3 SampleGGXHalf(float3 N, float3 V, float a, float2 u) {
    float3 T, B;
    Basis(N, T, B);
    float3 Vl = float3(dot(V, T), dot(V, B), dot(V, N));
    float3 Hl = SampleVNDF(Vl, a, u);
    return normalize(T * Hl.x + B * Hl.y + N * Hl.z);
}

float Alpha(float r) { return max(r * r, 0.0015); }
float3 SpecF0(Surf s) { return lerp(0.08 * s.spec.xxx, s.base, s.metal); }
float DiffuseWeight(Surf s) { return (1.0 - s.metal) * (1.0 - s.trans); }

float CoatProbability(Surf s) { return s.coat > 0 ? 0.2 * s.coat : 0.0; }

float SpecularProbability(Surf s, float NoV) {
    float ws = Luminance(SchlickF(SpecF0(s), NoV)) + s.coat * 0.04;
    float wd = Luminance(s.base) * DiffuseWeight(s);
    if (wd <= 0.0) return 1.0;
    return clamp(ws / (ws + wd), 0.08, 0.92);
}

// Returns f * cos(theta_l); pdf is the solid-angle pdf of the lobe mixture.
float3 EvalBSDF(Surf s, float3 N, float3 V, float3 L, out float pdf) {
    pdf = 0;
    float NoL = dot(N, L), NoV = dot(N, V);
    if (NoL <= 0 || NoV <= 0) return 0;
    float3 H = normalize(V + L);
    float NoH = saturate(dot(N, H)), VoH = saturate(dot(V, H));
    float a = Alpha(s.rough);
    float D = D_GGX(NoH, a);
    float3 Fr = SchlickF(SpecF0(s), VoH);
    float3 spec = D * G2_Smith(NoL, NoV, a) * Fr / (4.0 * NoL * NoV);
    float3 diff = s.base * DiffuseWeight(s) * (1.0 - s.sss) / PI * (1.0 - Fr);
    float pSpec = SpecularProbability(s, NoV);
    float pdfSpec = D * G1_Smith(NoV, a) / (4.0 * NoV);
    float pdfDiff = NoL / PI;
    pdf = pSpec * pdfSpec + (1.0 - pSpec) * (1.0 - s.sss) * pdfDiff;
    float3 coat = 0;
    if (s.coat > 0) {
        float ac = Alpha(s.coatRough);
        float Dc = D_GGX(NoH, ac);
        float pCoat = CoatProbability(s);
        pdf = pCoat * Dc * G1_Smith(NoV, ac) / (4.0 * NoV) + (1.0 - pCoat) * pdf;
        float Fc = SchlickF(0.04.xxx, VoH).x * s.coat;
        coat = Dc * G2_Smith(NoL, NoV, ac) * Fc / (4.0 * NoL * NoV);
        spec *= 1.0 - Fc;
        diff *= 1.0 - Fc;
    }
    return (diff + spec + coat) * NoL;
}

float FresnelDielectric(float cosI, float eta) {
    float sinT2 = eta * eta * (1.0 - cosI * cosI);
    if (sinT2 > 1.0) return 1.0;
    float cosT = sqrt(1.0 - sinT2);
    float rs = (eta * cosI - cosT) / (eta * cosI + cosT);
    float rp = (cosI - eta * cosT) / (cosI + eta * cosT);
    return 0.5 * (rs * rs + rp * rp);
}

float PowerHeuristic(float a, float b) {
    float a2 = a * a;
    return a2 / (a2 + b * b + 1e-20);
}

// ---------------------------------------------------------------------------
// Lights & environment

// Gradient sky of the scene (no HDRI, or the background when the HDRI is hidden from the camera).
float3 Gradient(float3 d) {
    float y = d.y;
    float3 c = y > 0 ? lerp(F.envHorizon, F.envTop, pow(saturate(y), 0.6))
                     : lerp(F.envHorizon, F.envBottom, pow(saturate(-y), 0.4));
    return c * F.envIntensity;
}

bool EnvEnabled() { return (F.flags & FRAME_FLAG_ENV_MAP) != 0 && F.envW > 0; }

// Lat-long mapping of the HDRI: -Z is the centre of the image, +Y the top row, envRotation turns it around +Y.
float2 EnvDirToUV(float3 d) {
    float u = atan2(d.x, -d.z) * (0.5 / PI) + 0.5 - F.envRotation * (0.5 / PI);
    return float2(frac(u), acos(clamp(d.y, -1.0, 1.0)) / PI);
}

float3 EnvUVToDir(float2 uv) {
    float phi = (uv.x - 0.5) * 2.0 * PI + F.envRotation;
    float th = uv.y * PI;
    float s = sin(th);
    return float3(s * sin(phi), cos(th), -s * cos(phi));
}

float3 EnvMap(float3 d, float lod) {
    float2 uv = EnvDirToUV(d);
    uv.y = clamp(uv.y, 0.0005, 0.9995);   // the sampler wraps: keep the poles off the opposite row
    return gEnvTex.SampleLevel(gLinearWrap, uv, lod).rgb * F.envMapIntensity;
}

float3 Environment(float3 d) { return EnvEnabled() ? EnvMap(d, 0) : Gradient(d); }

// HDRI seen by a glossy ray without next-event estimation (GI probes): a mip blurred by the roughness, so that the
// sun does not make fireflies. Approximate: the reflections of rough surfaces are a little too soft.
float3 EnvironmentRough(float3 d, float rough) {
    return EnvEnabled() ? EnvMap(d, sqrt(saturate(rough)) * max(F.envMips - 3.0, 0.0)) : Gradient(d);
}

// What the camera sees where no object is hit (the HDRI can be hidden from the camera and still light the scene).
float3 Background(float3 d) {
    if (EnvEnabled() && !(F.flags & FRAME_FLAG_ENV_VISIBLE)) return Gradient(d);
    return Environment(d);
}

// Importance sampling of the HDRI. gEnvDist holds, for a W x H table of luminance * sin(theta):
// the table [W*H], then one CDF per row [H * (W+1)], then the CDF of the rows [H+1].
uint SearchCdf(uint base, uint n, float u) {
    uint lo = 0, hi = n;
    while (hi - lo > 1) {
        uint mid = (lo + hi) >> 1;
        if (gEnvDist[base + mid] <= u) lo = mid;
        else hi = mid;
    }
    return lo;
}

float EnvPdfUV(uint col, uint row, float sinTheta) {
    if (sinTheta <= 1e-6) return 0;
    return gEnvDist[row * F.envW + col] * F.envPdfScale / (2.0 * PI * PI * sinTheta);
}

// Direction towards the bright parts of the HDRI and its solid-angle pdf.
float3 EnvSample(float2 u, out float pdf) {
    uint W = F.envW, H = F.envH;
    uint marg = W * H + H * (W + 1);
    uint row = SearchCdf(marg, H, u.y);
    float r0 = gEnvDist[marg + row], r1 = gEnvDist[marg + row + 1];
    float dv = saturate((u.y - r0) / max(r1 - r0, 1e-12));
    uint cdf = W * H + row * (W + 1);
    uint col = SearchCdf(cdf, W, u.x);
    float c0 = gEnvDist[cdf + col], c1 = gEnvDist[cdf + col + 1];
    float du = saturate((u.x - c0) / max(c1 - c0, 1e-12));
    float2 uv = float2((col + du) / W, (row + dv) / H);
    pdf = EnvPdfUV(col, row, sin(uv.y * PI));
    return EnvUVToDir(uv);
}

float EnvPdf(float3 d) {
    float2 uv = EnvDirToUV(d);
    uint col = min((uint)(uv.x * F.envW), F.envW - 1), row = min((uint)(uv.y * F.envH), F.envH - 1);
    return EnvPdfUV(col, row, sqrt(saturate(1.0 - d.y * d.y)));
}

// Light seen through glass along a shadow ray: 0 when an opaque object blocks it, otherwise the product of the
// Fresnel transmission (and tint) of each glass surface crossed. A glass ball then casts a light shadow with a
// dark rim (grazing angles reflect), instead of no shadow at all. Glass instances are non-opaque in the TLAS.
float3 ShadowTransmittance(float3 o, float3 d, float tmax) {
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;
    RayDesc r;
    r.Origin = o; r.Direction = d; r.TMin = 0.0; r.TMax = tmax;
    q.TraceRayInline(gScene, RAY_FLAG_NONE, MASK_OPAQUE | MASK_GLASS, r);
    float3 T = 1;
    while (q.Proceed()) {
        if (q.CandidateType() != CANDIDATE_NON_OPAQUE_TRIANGLE) continue;
        uint inst = q.CandidateInstanceID();
        GpuMaterial m = gMats[gInst[inst].materialId];
        float3 ng;
        float3 n = WorldNormal(inst, q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics(),
                               q.CandidateWorldToObject3x4(), ng);
        bool entering = dot(n, d) < 0;
        T *= 1.0 - FresnelDielectric(abs(dot(n, d)), entering ? 1.0 / m.ior : m.ior);
        if (entering) T *= m.baseColor;
        if (max(T.x, max(T.y, T.z)) < 1e-3) q.Abort();
    }
    if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT || max(T.x, max(T.y, T.z)) < 1e-3) return 0;
    return T;
}

// Sampling of light li from P with the random numbers u (x picks nothing here). Rect lights: a point on the quad.
// Emissive objects: a direction in the cone of their bounding sphere (the shadow ray must then reach the object).
bool SampleOneLight(uint li, float3 P, float2 u, out float3 L, out float dist, out float3 Le, out float pdf) {
    GpuLight l = gLights[li];
    L = 0; dist = 0; Le = 0; pdf = 0;
    if (l.type == LIGHT_OBJECT) {
        float3 w = l.corner - P;
        float d2 = dot(w, w), r2 = l.area * l.area;
        if (d2 <= r2 * 1.0001) return false;
        float dc = sqrt(d2);
        w /= dc;
        float sin2 = r2 / d2;
        float cosMax = sqrt(max(0.0, 1.0 - sin2));
        float oneMinus = sin2 / (1.0 + cosMax);   // 1 - cosMax without cancellation for small, far objects
        float cosT = 1.0 - u.x * oneMinus;
        float sinT = sqrt(max(0.0, 1.0 - cosT * cosT));
        float phi = 2.0 * PI * u.y;
        float3 t, b;
        Basis(w, t, b);
        L = normalize(t * (sinT * cos(phi)) + b * (sinT * sin(phi)) + w * cosT);
        dist = dc + l.area;
        pdf = 1.0 / (2.0 * PI * oneMinus) / F.lightCount;
        Le = l.radiance;
        return true;
    }
    float3 X = l.corner + l.edgeU * u.x + l.edgeV * u.y;
    float3 d = X - P;
    dist = length(d);
    L = d / dist;
    float3 Nl = normalize(cross(l.edgeU, l.edgeV));
    float cosL = dot(-L, Nl);
    if (cosL <= 1e-4) return false;
    pdf = dist * dist / (cosL * l.area) / F.lightCount;
    Le = l.radiance * F.lightScale;
    return true;
}

bool SampleLight(float3 P, inout uint rng, out float3 L, out float dist, out float3 Le, out float pdf, out uint li) {
    L = 0; dist = 0; Le = 0; pdf = 0; li = 0;
    if (F.lightCount == 0) return false;
    li = min((uint)(Rnd(rng) * F.lightCount), F.lightCount - 1);
    return SampleOneLight(li, P, Rnd2(rng), L, dist, Le, pdf);
}

// Same as SampleLight, with the random numbers given by the caller (stratified over frames).
bool SampleLightU(float3 P, float3 us, out float3 L, out float dist, out float3 Le, out float pdf, out uint li) {
    L = 0; dist = 0; Le = 0; pdf = 0; li = 0;
    if (F.lightCount == 0) return false;
    li = min((uint)(us.x * F.lightCount), F.lightCount - 1);
    return SampleOneLight(li, P, us.yz, L, dist, Le, pdf);
}

// Instance of an area light quad (an emissive object is shaded as a surface, its light id only serves the sampling).
bool IsRectLight(GpuInstance gi) { return gi.lightId != NO_LIGHT && gLights[gi.lightId].type == LIGHT_RECT; }

// Pdf with which SampleLight picks the direction dir from o towards light lightId, hit at distance t.
float LightPdf(uint lightId, float3 o, float3 dir, float t) {
    GpuLight l = gLights[lightId];
    if (l.type == LIGHT_OBJECT) {
        float3 w = l.corner - o;
        float d2 = dot(w, w), r2 = l.area * l.area;
        if (d2 <= r2 * 1.0001) return 0;
        float sin2 = r2 / d2;
        float oneMinus = sin2 / (1.0 + sqrt(max(0.0, 1.0 - sin2)));
        return 1.0 / (2.0 * PI * oneMinus) / F.lightCount;
    }
    float3 Nl = normalize(cross(l.edgeU, l.edgeV));
    float cosL = abs(dot(dir, Nl));
    return t * t / (max(cosL, 1e-4) * l.area) / F.lightCount;
}

// Shadow test of a light sample from o: the light gets through glass (tinted), an emissive object must be the
// first opaque surface hit. Returns the transmittance (0 = blocked).
float3 LightVisibility(uint li, float3 o, float3 L, float dist) {
    GpuLight l = gLights[li];
    if (l.type == LIGHT_OBJECT) {
        Hit e;
        if (!TraceClosest(o, L, dist, MASK_OPAQUE, e) || e.inst != l.instance || !e.front) return 0;
        return ShadowTransmittance(o, L, e.t * 0.999);
    }
    return ShadowTransmittance(o, L, dist * 0.999);
}

// Next-event estimation from a surface point (returns MIS-weighted radiance): one light sample, plus one HDRI
// sample when the environment is an image (each strategy weighted against the BSDF sampling).
float3 DirectLight(Surf s, Hit h, float3 V, bool lambertOnly, inout uint rng) {
    float3 sum = 0;
    float3 o = OffsetRay(h.P, h.Ng);
    for (uint k = 0; k < 2; ++k) {
        float3 L, Le;
        float dist, lpdf;
        uint li = 0;
        if (k == 0) {
            if (!SampleLight(h.P, rng, L, dist, Le, lpdf, li)) continue;
        } else {
            if (!EnvEnabled()) break;
            L = EnvSample(Rnd2(rng), lpdf);
            if (lpdf <= 0) continue;
            dist = 1e30;
            Le = EnvMap(L, 0);
        }
        if (dot(L, h.Ng) <= 0) continue;
        float bpdf;
        float3 f;
        if (lambertOnly) {
            float c = dot(h.N, L);
            if (c <= 0) continue;
            f = c / PI;
            bpdf = c / PI;
        } else {
            f = EvalBSDF(s, h.N, V, L, bpdf);
        }
        if (all(f <= 0)) continue;
        float3 T = k == 0 ? LightVisibility(li, o, L, dist) : ShadowTransmittance(o, L, 1e30);
        if (all(T <= 0)) continue;
        sum += f * Le * T / lpdf * PowerHeuristic(lpdf, bpdf);
    }
    return sum;
}

// ---------------------------------------------------------------------------
// Random-walk subsurface scattering. On success `h` becomes the exit point,
// with normals facing outwards.

bool RandomWalk(inout Hit h, Surf s, inout float3 thr, inout uint rng) {
    float3 radius = max(s.sssR * F.sssScale, 1e-4);
    float3 sigmaT = 1.0 / radius;
    float3 A = saturate(s.base);
    float3 k = 4.09712 + 4.20863 * A - sqrt(9.59217 + 41.6808 * A + 17.7126 * A * A);
    float3 alpha = saturate(1.0 - k * k);
    float3 sigmaS = sigmaT * alpha;

    float3 pos = OffsetRay(h.P, -h.Ng);
    float3 dir = CosineHemisphere(-h.Ng, Rnd2(rng));
    for (uint i = 0; i < 96; ++i) {
        uint c = min((uint)(Rnd(rng) * 3.0), 2u);
        float st = c == 0 ? sigmaT.x : (c == 1 ? sigmaT.y : sigmaT.z);
        float t = -log(max(1.0 - Rnd(rng), 1e-7)) / st;
        Hit e;
        if (TraceClosest(pos, dir, t, MASK_OPAQUE, e)) {
            float3 T = exp(-sigmaT * e.t);
            float pdf = (T.x + T.y + T.z) / 3.0;
            thr *= T / max(pdf, 1e-8);
            // e normals face the ray (inwards); flip to outward.
            e.N = -e.N;
            e.Ng = -e.Ng;
            e.front = true;
            h = e;
            return true;
        }
        pos += dir * t;
        float3 T = exp(-sigmaT * t);
        float pdf = dot(sigmaT * T, 1.0 / 3.0);
        thr *= sigmaS * T / max(pdf, 1e-8);
        dir = UniformSphere(Rnd2(rng));
        if (i > 8) {
            float p = saturate(max(thr.x, max(thr.y, thr.z)));
            if (Rnd(rng) > p) return false;
            thr /= max(p, 1e-4);
        }
    }
    return false;
}

// ---------------------------------------------------------------------------

struct Guides {
    float3 albedo;
    float3 normal;   // shading normal (guide for the denoiser)
    float depth;
    uint id;
    float3 geom;     // geometric normal of the primary hit (stable under bevel jitter; used by reprojection)
};

// Indirect clamp: after a rough bounce, one light contribution is capped to F.indirectClamp (biased: removes the
// sparkles of rare bright paths, such as the sun reflected by the chrome ball onto the floor). 0 = off.
float3 Indirect(float3 c, bool roughSeen) {
    if (!roughSeen || F.indirectClamp <= 0) return c;
    float l = Luminance(c);
    return l > F.indirectClamp ? c * (F.indirectClamp / l) : c;
}

float3 TracePath(float3 o, float3 d, inout uint rng, inout Guides g) {
    float3 radiance = 0, thr = 1;
    bool specPrev = true;
    float prevPdf = 0;
    // Light that reaches a surface through glass is already counted by its next-event estimation (shadow rays
    // cross glass, see ShadowTransmittance): a path that crossed glass after such a surface does not add it again.
    bool neeDone = false, viaGlass = false;
    bool roughSeen = false;   // a rough (diffuse-like) bounce happened: see filterGlossy below
    uint mask = 0xFF & ~MASK_LIGHT_HIDDEN;
    for (uint bounce = 0; bounce <= F.maxBounces; ++bounce) {
        Hit h;
        if (!TraceClosest(o, d, 1e30, mask, h)) {
            float3 env = bounce == 0 ? Background(d) : Environment(d);
            float w = 1.0;
            if (EnvEnabled() && bounce > 0) {   // the HDRI is also sampled by next-event estimation: MIS
                if (viaGlass && neeDone) w = 0.0;
                else if (!specPrev) w = PowerHeuristic(prevPdf, EnvPdf(d));
            }
            radiance += Indirect(thr * env * w, roughSeen);
            if (bounce == 0) { g.albedo = 1; g.normal = -d; g.geom = -d; g.depth = 1e6; g.id = 0; }
            break;
        }
        GpuInstance gi = gInst[h.inst];
        GpuMaterial gm = gMats[gi.materialId];
        bool objectLight = gi.lightId != NO_LIGHT && gLights[gi.lightId].type == LIGHT_OBJECT;

        if (gi.lightId != NO_LIGHT && !objectLight) {
            if (h.front && !(viaGlass && neeDone)) {
                float3 Le = gLights[gi.lightId].radiance * F.lightScale;
                float w = specPrev ? 1.0 : PowerHeuristic(prevPdf, LightPdf(gi.lightId, o, d, h.t));
                radiance += Indirect(thr * Le * w, roughSeen);
            }
            if (bounce == 0) { g.albedo = 1; g.normal = h.N; g.geom = h.Ng; g.depth = h.t; g.id = h.inst + 1; }
            break;
        }

        Surf s = MakeSurf(gm, h);
        // Filter glossy: after a rough bounce, mirror-like surfaces are blurred to at least F.filterGlossy. The
        // caustics they would cast (sun seen in the chrome ball from the floor) are spread instead of fireflies.
        if (roughSeen && F.filterGlossy > 0) {
            s.rough = max(s.rough, F.filterGlossy);
            s.coatRough = max(s.coatRough, F.filterGlossy);
        }
        if (h.front) {
            // emissive objects are lights too (next-event estimation): MIS weight of the BSDF sampling
            float w = 1.0;
            if (objectLight && bounce > 0) {
                if (viaGlass && neeDone) w = 0.0;
                else if (!specPrev) w = PowerHeuristic(prevPdf, LightPdf(gi.lightId, o, d, h.t));
            }
            radiance += Indirect(thr * s.emission * w, roughSeen);
        }

        if ((F.flags & FRAME_FLAG_BEVEL) && s.bevel > 0 && h.front) {
            uint ns = bounce == 0 ? (uint)F.bevelSamples : 1u;
            h.N = BevelNormal(h, s.bevel * F.bevelScale, ns, rng);
        }
        float3 V = -d;
        if (dot(h.N, V) <= 0) h.N = h.Ng;

        if (bounce == 0) {
            g.albedo = s.trans > 0.5 ? 1.0.xxx : lerp(s.base, SpecF0(s) + 0.2, s.metal);
            g.normal = h.N;
            g.geom = h.Ng;
            g.depth = h.t;
            g.id = h.inst + 1;
        }
        if (bounce == F.maxBounces) break;

        // --- Dielectric (glass / transparent plastic)
        if (s.trans > 0.5) {
            float eta = h.front ? 1.0 / s.ior : s.ior;
            float3 H = s.rough > 0.02 ? SampleGGXHalf(h.N, V, Alpha(s.rough), Rnd2(rng)) : h.N;
            float cosI = saturate(dot(V, H));
            float Fr = FresnelDielectric(cosI, eta);
            float3 L;
            bool reflected = Rnd(rng) < Fr;
            if (!reflected) {
                L = refract(-V, H, eta);
                if (dot(L, L) < 1e-6) reflected = true;
            }
            if (reflected) {
                L = reflect(-V, H);
                if (dot(L, h.Ng) <= 0) break;
                o = OffsetRay(h.P, h.Ng);
            } else {
                if (dot(L, h.Ng) >= 0) break;
                o = OffsetRay(h.P, -h.Ng);
                if (h.front) thr *= s.base;
            }
            d = normalize(L);
            specPrev = true;
            viaGlass = true;
            mask = 0xFF;
            continue;
        }

        // --- Next-event estimation
        radiance += Indirect(thr * DirectLight(s, h, V, false, rng), roughSeen);
        neeDone = true;
        viaGlass = false;
        if (s.rough > 0.3 || DiffuseWeight(s) > 0.5) roughSeen = true;

        // --- Sample the BSDF
        float NoV = dot(h.N, V);
        float pSpec = SpecularProbability(s, NoV);
        float3 L;
        float u = Rnd(rng);
        if (u >= pSpec && s.sss > 0 && Rnd(rng) < s.sss) {
            // Subsurface: enter, random walk, exit.
            float3 Fv = SchlickF(SpecF0(s), NoV);
            thr *= DiffuseWeight(s) * (1.0 - Fv) / (1.0 - pSpec);
            if (!RandomWalk(h, s, thr, rng)) break;
            Surf exitSurf = s;
            radiance += Indirect(thr * DirectLight(exitSurf, h, h.N, true, rng), true);   // the random walk is a rough bounce
            L = CosineHemisphere(h.N, Rnd2(rng));
            prevPdf = max(dot(h.N, L), 1e-4) / PI;
        } else {
            float pCoat = CoatProbability(s);
            if (Rnd(rng) < pCoat) {
                float3 H = SampleGGXHalf(h.N, V, Alpha(s.coatRough), Rnd2(rng));
                L = reflect(-V, H);
            } else if (u < pSpec) {
                float3 H = SampleGGXHalf(h.N, V, Alpha(s.rough), Rnd2(rng));
                L = reflect(-V, H);
            } else {
                L = CosineHemisphere(h.N, Rnd2(rng));
            }
            if (dot(L, h.Ng) <= 0) break;
            float pdf;
            float3 f = EvalBSDF(s, h.N, V, L, pdf);
            if (pdf <= 0) break;
            thr *= f / pdf;
            prevPdf = pdf;
        }
        specPrev = false;
        o = OffsetRay(h.P, h.Ng);
        d = L;
        mask = 0xFF;

        if (bounce >= 3) {
            float p = clamp(max(thr.x, max(thr.y, thr.z)), 0.05, 0.95);
            if (Rnd(rng) > p) break;
            thr /= p;
        }
    }
    return radiance;
}

#ifndef VP3D_NO_MAIN   // the DDGI shaders include this file for its helpers
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= F.resolution.x || id.y >= F.resolution.y) return;
    uint rng = Hash(id.x + id.y * 65521u + Hash(F.frameIndex * 9781u + 1u));

    float3 sum = 0;
    Guides gAcc = (Guides)0;
    float3 albedoSum = 0, normalSum = 0;
    float depthSum = 0;
    uint primaryId = 0;

    for (uint sIdx = 0; sIdx < F.samplesPerFrame; ++sIdx) {
        float2 jitter = (F.sampleIndex == 0 && sIdx == 0) ? float2(0.5, 0.5) : Rnd2(rng);
        float2 uv = (float2(id.xy) + jitter) / float2(F.resolution);
        float2 ndc = float2(2.0 * uv.x - 1.0, 1.0 - 2.0 * uv.y);
        float3 dir = normalize(F.camFwd + F.camRight * (ndc.x * F.aspect * F.tanHalfFovY) + F.camUp * (ndc.y * F.tanHalfFovY));
        float3 org = F.camPos;
        if ((F.flags & FRAME_FLAG_DOF) && F.aperture > 0) {
            float3 focal = org + dir * (F.focusDist / dot(dir, F.camFwd));
            float2 lens = ConcentricDisk(Rnd2(rng)) * F.aperture;
            org += F.camRight * lens.x + F.camUp * lens.y;
            dir = normalize(focal - org);
        }
        Guides g = (Guides)0;
        float3 L = TracePath(org, dir, rng, g);
        if (any(isnan(L)) || any(isinf(L))) L = 0;
        float lum = Luminance(L);
        if (lum > F.fireflyClamp) L *= F.fireflyClamp / lum;
        sum += L;
        albedoSum += g.albedo;
        normalSum += g.normal;
        depthSum += g.depth;
        if (sIdx == 0) primaryId = g.id;
    }

    float n = (float)F.samplesPerFrame;
    float4 acc = F.sampleIndex == 0 ? 0 : gTex[SLOT_ACCUM][id.xy];
    gTex[SLOT_ACCUM][id.xy] = acc + float4(sum, n);

    float prevN = (float)F.sampleIndex;
    float4 alb = gTex[SLOT_ALBEDO][id.xy];
    float4 nd = gTex[SLOT_NORMAL][id.xy];
    if (F.sampleIndex == 0) {
        alb = float4(albedoSum / n, (float)primaryId);
        nd = float4(normalSum / n, depthSum / n);
    } else {
        float k = n / (prevN + n);
        alb.rgb = lerp(alb.rgb, albedoSum / n, k);
        nd = lerp(nd, float4(normalSum / n, depthSum / n), k);
    }
    gTex[SLOT_ALBEDO][id.xy] = alb;
    gTex[SLOT_NORMAL][id.xy] = nd;
}
#endif
