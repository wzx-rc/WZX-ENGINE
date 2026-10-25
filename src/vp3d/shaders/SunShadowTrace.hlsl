// Ray traced sun shadow of the raster pass, step 1 (one thread per pixel): the camera ray of the pixel, then a few
// shadow rays inside the sun's disc. Writes the raw visibility with the depth and the normal that guide the filter
// (SunShadowFilter.hlsl). The raster pass reads the filtered mask instead of its shadow map (Raster.hlsl, t8).
//
// The shadow rays start on the smooth surface the shading normals describe, not on the flat triangle ("Hacking the
// shadow terminator", Hanika 2021): otherwise a tessellated sphere shadows itself in a dotted line along the
// day / night boundary, where the smooth normal still faces the sun but the neighbouring facet hides it.
#include "Common.hlsli"

struct SunPass { float2 sunOct; float tanRadius; uint samples; };   // sun direction (octahedral), disc, rays
ConstantBuffer<SunPass> S : register(b1);
RWTexture2D<float4> gShadowRaw : register(u18);   // x visibility, y depth along the camera ray (0 = sky), zw normal

float2 SignNZ2(float2 v) { return float2(v.x >= 0 ? 1.0 : -1.0, v.y >= 0 ? 1.0 : -1.0); }

float2 OctEnc(float3 n) {
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    return n.z >= 0 ? n.xy : (1.0 - abs(n.yx)) * SignNZ2(n.xy);
}

float3 OctDec(float2 o) {
    float3 n = float3(o, 1.0 - abs(o.x) - abs(o.y));
    if (n.z < 0) n.xy = (1.0 - abs(n.yx)) * SignNZ2(n.xy);
    return normalize(n);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= F.resolution.x || id.y >= F.resolution.y) return;
    float2 uv = (float2(id.xy) + 0.5) / float2(F.resolution);
    float2 ndc = float2(2.0 * uv.x - 1.0, 1.0 - 2.0 * uv.y);
    float3 dir = normalize(F.camFwd + F.camRight * (ndc.x * F.aspect * F.tanHalfFovY) + F.camUp * (ndc.y * F.tanHalfFovY));

    RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
    RayDesc r;
    r.Origin = F.camPos; r.Direction = dir; r.TMin = 0.0; r.TMax = 1e30;
    q.TraceRayInline(gScene, RAY_FLAG_NONE, MASK_OPAQUE | MASK_GLASS, r);
    q.Proceed();
    if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) {
        gShadowRaw[id.xy] = float4(1, 0, 0, 0);
        return;
    }
    float t = q.CommittedRayT();
    uint inst = q.CommittedInstanceID(), prim = q.CommittedPrimitiveIndex();
    float2 bc = q.CommittedTriangleBarycentrics();
    float3x4 o2w = q.CommittedObjectToWorld3x4();
    float3x4 w2o = q.CommittedWorldToObject3x4();

    GpuInstance gi = gInst[inst];
    uint b = gi.indexOffset + prim * 3;
    GpuVertex v0 = gVerts[gIndices[b] + gi.vertexOffset];
    GpuVertex v1 = gVerts[gIndices[b + 1] + gi.vertexOffset];
    GpuVertex v2 = gVerts[gIndices[b + 2] + gi.vertexOffset];
    float w0 = 1.0 - bc.x - bc.y;
    // object space: the hit point moved onto the smooth surface (Hanika), then back to world space
    float3 p = v0.pos * w0 + v1.pos * bc.x + v2.pos * bc.y;
    float3 n0 = normalize(v0.nrm), n1 = normalize(v1.nrm), n2 = normalize(v2.nrm);
    float3 t0 = p - v0.pos, t1 = p - v1.pos, t2 = p - v2.pos;
    t0 -= min(dot(t0, n0), 0.0) * n0;
    t1 -= min(dot(t1, n1), 0.0) * n1;
    t2 -= min(dot(t2, n2), 0.0) * n2;
    float3 ps = w0 * (v0.pos + t0) + bc.x * (v1.pos + t1) + bc.y * (v2.pos + t2);
    float3 P = mul(o2w, float4(ps, 1));
    float3 ng = normalize(mul(cross(v1.pos - v0.pos, v2.pos - v0.pos), (float3x3)w2o));
    float3 ns = normalize(mul(n0 * w0 + n1 * bc.x + n2 * bc.y, (float3x3)w2o));
    if (dot(ng, dir) > 0) ng = -ng;   // the side seen by the camera
    if (dot(ns, ng) < 0) ns = -ns;

    float3 L = OctDec(S.sunOct);
    float vis = 0;
    if (dot(ns, L) > 0) {
        float3 tx = normalize(abs(L.y) < 0.99 ? cross(L, float3(0, 1, 0)) : cross(L, float3(1, 0, 0)));
        float3 ty = cross(L, tx);
        float3 o = P + ng * (1e-4 * (1.0 + max(abs(P.x), max(abs(P.y), abs(P.z)))));
        // one rotation per pixel (interleaved gradient noise) of a Vogel spiral: the filter averages the neighbours
        float rot = 6.2831853 * frac(52.9829189 * frac(dot(float2(id.xy) + 0.5, float2(0.06711056, 0.00583715))));
        uint n = max(S.samples, 1u);
        for (uint k = 0; k < n; ++k) {
            float rr = sqrt((k + 0.5) / n), th = k * 2.3999632 + rot;
            float3 d = normalize(L + (tx * cos(th) + ty * sin(th)) * (rr * S.tanRadius));
            RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> sq;
            RayDesc sr;
            sr.Origin = o; sr.Direction = d; sr.TMin = 0.0; sr.TMax = 1e30;
            sq.TraceRayInline(gScene, RAY_FLAG_NONE, MASK_OPAQUE, sr);
            sq.Proceed();
            if (sq.CommittedStatus() != COMMITTED_TRIANGLE_HIT) vis += 1.0;
        }
        vis /= n;
    }
    gShadowRaw[id.xy] = float4(vis, t, OctEnc(ns));
}
