// DDGI (Dynamic Diffuse Global Illumination) probe volume: shared resources and helpers.
// Follows Majercik et al. 2019 / RTXGI: per-probe octahedral irradiance and distance moments,
// Chebyshev visibility test, rays rotated randomly each frame, temporal hysteresis.
#define VP3D_NO_MAIN
#include "PathTrace.hlsl"

ConstantBuffer<DDGIConstants> G : register(b2);
// u6 irradiance atlas (rgb), u7 distance atlas (mean, mean^2), u8 probe rays (rgb radiance, hit distance),
// u9 probe state (x = 1 active, 0 inside geometry)
RWTexture2D<float4> gIrr : register(u6);
RWTexture2D<float4> gDist : register(u7);
RWTexture2D<float4> gRays : register(u8);
RWTexture2D<float4> gProbe : register(u9);

float2 SignNZ(float2 v) { return float2(v.x >= 0 ? 1.0 : -1.0, v.y >= 0 ? 1.0 : -1.0); }

// Unit vector <-> octahedral coordinates in [-1, 1]^2.
float2 OctEncode(float3 n) {
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    return n.z >= 0 ? n.xy : (1.0 - abs(n.yx)) * SignNZ(n.xy);
}

float3 OctDecode(float2 o) {
    float3 n = float3(o, 1.0 - abs(o.x) - abs(o.y));
    if (n.z < 0) n.xy = (1.0 - abs(n.yx)) * SignNZ(n.xy);
    return normalize(n);
}

float3 SphericalFibonacci(uint i, uint n) {
    const float PHI = 1.6180339887;
    float phi = 2.0 * PI * frac(i * (PHI - 1.0));
    float cosT = 1.0 - (2.0 * i + 1.0) / n;
    float sinT = sqrt(saturate(1.0 - cosT * cosT));
    return float3(cos(phi) * sinT, sin(phi) * sinT, cosT);
}

float3 ProbeRayDir(uint i) {
    float3 d = SphericalFibonacci(i, DDGI_RAYS);
    return normalize(float3(dot(G.rot0.xyz, d), dot(G.rot1.xyz, d), dot(G.rot2.xyz, d)));
}

uint3 ProbeCoord(uint p) {
    uint nxy = G.counts.x * G.counts.y;
    return uint3(p % G.counts.x, (p / G.counts.x) % G.counts.y, p / nxy);
}

uint ProbeIndex(uint3 c) { return c.x + G.counts.x * (c.y + G.counts.y * c.z); }
float3 ProbePos(uint3 c) { return G.origin + G.spacing * float3(c); }

// Top-left texel of probe p's tile (tile size = interior + 2 border texels).
int2 TileOrigin(uint p, uint tileSize) {
    uint nxy = G.counts.x * G.counts.y;
    return int2((p % nxy) * tileSize, (p / nxy) * tileSize);
}

// Manual bilinear sample of an octahedral tile (atlases are UAVs, no sampler).
float4 SampleTile(RWTexture2D<float4> tex, uint p, uint n, float3 dir) {
    float2 uv = OctEncode(dir) * 0.5 + 0.5;                 // [0,1] over the interior
    float2 pos = float2(TileOrigin(p, n + 2)) + 1.0 + uv * n - 0.5;
    int2 i0 = int2(floor(pos));
    float2 f = pos - float2(i0);
    float4 a = tex[i0], b = tex[i0 + int2(1, 0)], c = tex[i0 + int2(0, 1)], d = tex[i0 + int2(1, 1)];
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

// Irradiance (cosine-weighted mean radiance, i.e. E / pi) at P with normal N, seen from direction V
// (V points from P towards the viewer). Multiply by the diffuse albedo to get outgoing radiance.
float3 SampleIrradiance(float3 P, float3 N, float3 V) {
    float minSpacing = min(G.spacing.x, min(G.spacing.y, G.spacing.z));
    float3 biased = P + (N * G.normalBias + V * G.viewBias) * minSpacing;
    float3 rel = (biased - G.origin) / G.spacing;
    int3 maxC = int3(G.counts.xyz) - 1;
    int3 base = clamp(int3(floor(rel)), int3(0, 0, 0), maxC);
    float3 alpha = saturate(rel - float3(base));

    float3 sum = 0;
    float wsum = 0;
    for (uint i = 0; i < 8; ++i) {
        int3 off = int3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
        uint3 c = (uint3)clamp(base + off, int3(0, 0, 0), maxC);
        uint p = ProbeIndex(c);
        if (gProbe[int2(p, 0)].x < 0.5) continue;            // probe stuck inside geometry
        float3 pp = ProbePos(c);
        float3 toProbe = normalize(pp - P);
        float3 tri = lerp(1.0 - alpha, alpha, float3(off));
        float w = 1.0;
        // Smooth backface test: probes behind the surface count less.
        float wrap = (dot(toProbe, N) + 1.0) * 0.5;
        w *= wrap * wrap + 0.2;
        // Chebyshev visibility from the distance moments.
        float3 pb = biased - pp;
        float d = length(pb);
        float2 m = SampleTile(gDist, p, DDGI_DIST_N, pb / max(d, 1e-4)).xy;
        if (d > m.x) {
            float var = abs(m.x * m.x - m.y);
            float diff = d - m.x;
            float cheb = var / (var + diff * diff);
            w *= max(cheb * cheb * cheb, 0.0);
        }
        w = max(w, 1e-6);
        const float crush = 0.2;
        if (w < crush) w *= w * w / (crush * crush);
        w *= tri.x * tri.y * tri.z;
        sum += SampleTile(gIrr, p, DDGI_IRR_N, N).rgb * w;
        wsum += w;
    }
    return wsum > 0 ? sum / wsum * G.energy : 0;
}

// Lambert-only next-event estimation (one light sample), returns radiance leaving towards any direction
// divided by albedo / pi, i.e. the direct irradiance term E.
float3 DirectIrradiance(float3 P, float3 N, float3 Ng, inout uint rng) {
    float3 L, Le;
    float dist, lpdf;
    uint li;
    if (!SampleLight(P, rng, L, dist, Le, lpdf, li)) return 0;
    float c = dot(N, L);
    if (c <= 0 || dot(L, Ng) <= 0) return 0;
    float3 T = LightVisibility(li, OffsetRay(P, Ng), L, dist);
    if (all(T <= 0)) return 0;
    return Le * T * c / lpdf;
}

// HDRI sun / sky as a light for the probes and the GI shading: one sample of the image (importance sampled), with
// the shadow ray through glass. Without it the image only reaches the probes through their random escape rays,
// which almost never hit the small sun disc, so the floor stays dark and the shadows of the sun are blurred.
float3 EnvDirectIrradiance(float3 P, float3 N, float3 Ng, inout uint rng) {
    if (!EnvEnabled()) return 0;
    float pdf;
    float3 L = EnvSample(Rnd2(rng), pdf);
    if (pdf <= 0) return 0;
    float c = dot(N, L);
    if (c <= 0 || dot(L, Ng) <= 0) return 0;
    float3 T = ShadowTransmittance(OffsetRay(P, Ng), L, 1e30);
    if (all(T <= 0)) return 0;
    return EnvMap(L, 0) * T * c / pdf;
}

// Albedo used for the diffuse transport in the probes (metals and SSS approximated as diffuse).
float3 DiffuseAlbedo(Surf s) {
    return s.base * (1.0 - s.trans) * (1.0 - 0.6 * s.metal);
}
