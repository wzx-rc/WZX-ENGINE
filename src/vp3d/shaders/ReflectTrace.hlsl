// Ray traced reflections of the raster pass (one thread per pixel). The camera ray finds the pixel's surface; a smooth
// surface traces its mirror ray, a rough one a few GGX rays (visible normals, a low-discrepancy set turned per pixel,
// averaged by ReflectFilter.hlsl). What the rays hit is lit like the probe rays: emission, direct light and the
// probes (the world cascades or DDGI) for the rest; a miss sees the environment. The raster pass then uses this
// radiance with its Fresnel / roughness weight instead of the environment reflection (Raster.hlsl, t9).
// Glossy surfaces only (metals and roughness under RP.maxRough); other pixels are marked (alpha -1) and keep the
// reflection of the raster pass. No temporal accumulation.
#include "DDGICommon.hlsli"

struct ReflPass { uint samples; float maxRough; uint probes; uint pad1; };   // probes: 0 = no probe volume (HQ raster)
ConstantBuffer<ReflPass> RP : register(b1);
RWTexture2D<float4> gReflRaw : register(u20);   // rgb radiance, a roughness (-1: not traced)

float3 ShadeReflHit(Hit h, float3 d, uint rng) {   // rng by value: every hit uses the same sample set
    Surf s = MakeSurf(gMats[gInst[h.inst].materialId], h);
    float3 albedo = DiffuseAlbedo(s);
    // 4 light samples, the same for every pixel (seed fixed per pass): a mirror shows a smooth reflected surface, not
    // the per-pixel speckle of one random light or HDRI sample (no temporal accumulation to average it away)
    float3 e = 0;
    for (uint k = 0; k < 4; ++k) e += DirectIrradiance(h.P, h.N, h.Ng, rng) + EnvDirectIrradiance(h.P, h.N, h.Ng, rng);
    e *= 0.25;
    // the rest of the light: the probes, or without probes the environment irradiance (no bounce)
    float3 rest = RP.probes != 0 ? SampleIrradiance(h.P, h.N, -d) : 0.0;
    return s.emission + albedo / PI * e + albedo * rest;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= F.resolution.x || id.y >= F.resolution.y) return;
    float2 uv = (float2(id.xy) + 0.5) / float2(F.resolution);
    float2 ndc = float2(2.0 * uv.x - 1.0, 1.0 - 2.0 * uv.y);
    float3 dir = normalize(F.camFwd + F.camRight * (ndc.x * F.aspect * F.tanHalfFovY) + F.camUp * (ndc.y * F.tanHalfFovY));
    Hit h;
    if (!TraceClosest(F.camPos, dir, 1e30, MASK_OPAQUE | MASK_GLASS, h)) {
        gReflRaw[id.xy] = float4(0, 0, 0, -1);
        return;
    }
    Surf s = MakeSurf(gMats[gInst[h.inst].materialId], h);
    // a clear coat (lacquered plastic) reflects as its coat: nearly a mirror (one exact ray, no noise)
    const float er = s.coat > 0 ? min(s.rough, s.coatRough) : s.rough;
    if (s.trans > 0.5 || (s.metal < 0.5 && er > RP.maxRough)) {
        gReflRaw[id.xy] = float4(0, 0, 0, -1);
        return;
    }
    float3 V = -dir, N = h.N;
    float3 o = OffsetRay(h.P, h.Ng);
    uint rng0 = Hash(26699u);   // light samples of the hits: the same set for all pixels and frames (see ShadeReflHit)
    float3 sum = 0;
    float n = 0;
    if (er < 0.06) {   // mirror: one exact ray
        float3 L = reflect(-V, N);
        Hit r;
        sum = TraceClosest(o, L, 1e30, MASK_OPAQUE, r) ? (r.front ? ShadeReflHit(r, L, rng0) : 0) : Environment(L);
        n = 1;
    } else {
        float rot = frac(52.9829189 * frac(dot(float2(id.xy) + 0.5, float2(0.06711056, 0.00583715))));
        uint count = max(RP.samples, 1u);
        for (uint k = 0; k < count; ++k) {
            float2 u = frac(float2(k * 0.7548777 + rot, k * 0.5698403 + rot * 0.618034));   // R2 set, turned per pixel
            float3 H = SampleGGXHalf(N, V, Alpha(er), u);
            float3 L = reflect(-V, H);
            if (dot(L, h.Ng) <= 0) continue;
            Hit r;
            sum += TraceClosest(o, L, 1e30, MASK_OPAQUE, r) ? (r.front ? ShadeReflHit(r, L, rng0) : 0) : Environment(L);
            n += 1;
        }
    }
    float3 c = n > 0 ? sum / n : 0;
    if (any(isnan(c)) || any(isinf(c))) c = 0;
    float lum = Luminance(c);
    if (lum > F.fireflyClamp) c *= F.fireflyClamp / lum;
    gReflRaw[id.xy] = float4(c, er);
}
