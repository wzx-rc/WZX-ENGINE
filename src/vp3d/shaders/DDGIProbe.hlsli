// DDGI probe volume: reading the probes (irradiance and distance atlases, probe state).
// Shared by the compute passes (DDGICommon.hlsli, read-write atlases u6 u7 u9) and the raster pass
// (Raster.hlsl, read-only atlases t4 t5 t6 with DDGI_PROBES_READONLY defined), so the editor viewport, the
// Windows build and the iOS build read the probes with the same code. Needs PI and the DDGIConstants struct.
#ifndef DDGI_PROBE_HLSLI
#define DDGI_PROBE_HLSLI

ConstantBuffer<DDGIConstants> G : register(b2);

#ifdef DDGI_PROBES_READONLY
#define DDGI_TEX Texture2D<float4>
// Registers of the read-only atlases: t4..t6 by default (DirectX 12 viewport); the including shader may choose others.
#ifndef DDGI_PROBE_REG_IRR
#define DDGI_PROBE_REG_IRR t4
#define DDGI_PROBE_REG_DIST t5
#define DDGI_PROBE_REG_STATE t6
#endif
Texture2D<float4> gIrr : register(DDGI_PROBE_REG_IRR);      // irradiance atlas (rgb)
Texture2D<float4> gDist : register(DDGI_PROBE_REG_DIST);    // distance atlas (mean, mean^2)
Texture2D<float4> gProbe : register(DDGI_PROBE_REG_STATE);  // probe state (x = 1 active, 0 inside geometry)
#else
#define DDGI_TEX RWTexture2D<float4>
RWTexture2D<float4> gIrr : register(u6);
RWTexture2D<float4> gDist : register(u7);
RWTexture2D<float4> gProbe : register(u9);
#endif

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
float4 SampleTile(DDGI_TEX tex, uint p, uint n, float3 dir) {
    float2 uv = OctEncode(dir) * 0.5 + 0.5;                 // [0,1] over the interior
    float2 pos = float2(TileOrigin(p, n + 2)) + 1.0 + uv * n - 0.5;
    int2 i0 = int2(floor(pos));
    float2 f = pos - float2(i0);
    float4 a = tex[i0], b = tex[i0 + int2(1, 0)], c = tex[i0 + int2(0, 1)], d = tex[i0 + int2(1, 1)];
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

// Irradiance (cosine-weighted mean radiance, i.e. E / pi) at P with normal N, seen from direction V
// (V points from P towards the viewer). Multiply by the diffuse albedo to get outgoing radiance.
// confidence: 1 when the surrounding probes answer, 0 when none does (all inside geometry or occluded) or when P lies
// outside the probe volume, fading over half a spacing. The raster pass falls back to its own ambient there.
float3 SampleIrradiance(float3 P, float3 N, float3 V, out float confidence) {
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
    // Outside the volume: the border probes answer (clamped cell), but their light is not that of the open space
    // beyond; the fallback takes over half a spacing past the last probes.
    float3 relP = (P - G.origin) / G.spacing;
    float3 outside = max(max(-relP, relP - float3(G.counts.xyz) + 1.0), 0.0);
    float inside = saturate(1.0 - 2.0 * max(outside.x, max(outside.y, outside.z)));
    confidence = saturate(wsum / 0.25) * inside;
    return wsum > 0 ? sum / wsum * G.energy : 0;
}

float3 SampleIrradiance(float3 P, float3 N, float3 V) {
    float c;
    return SampleIrradiance(P, N, V, c);
}

#endif
