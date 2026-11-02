// Split Radiance Cascades, pass 7 (one thread per pixel): the pixel's indirect irradiance from the cascade-0 probes
// around it (sparse trilinear over the 8 cells, each probe's 6x6 octahedral irradiance map sampled bilinearly for the
// pixel's normal), blended between two LODs in their overlap. Written as irradiance / pi (the probe atlas unit) into
// the screen GI texture read by the raster pass.
#include "SRCCommon.hlsli"

float4 IrrTexel(uint p, int2 t) {
    const int N = SRC_IRR_N;
    if (t.x < 0) { t.x = 0; t.y = N - 1 - t.y; } else if (t.x >= N) { t.x = N - 1; t.y = N - 1 - t.y; }
    if (t.y < 0) { t.y = 0; t.x = N - 1 - t.x; } else if (t.y >= N) { t.y = N - 1; t.x = N - 1 - t.x; }
    return asfloat(gS.Load4((S.irr + (p * N * N + t.x + t.y * N) * 4) * 4));
}

float4 ProbeIrradiance(uint p, float3 n) {
    float2 uv = (OctEncode(n) * 0.5 + 0.5) * SRC_IRR_N - 0.5;
    int2 b = int2(floor(uv));
    float2 a = uv - float2(b);
    float4 r = 0;
    for (uint i = 0; i < 4; ++i) {
        int2 off = int2(i & 1, i >> 1);
        float2 bl = lerp(1.0 - a, a, float2(off));
        float4 v = IrrTexel(p, b + off);
        r += float4(v.rgb * v.a, v.a) * (bl.x * bl.y);
    }
    return r.a > 1e-6 ? float4(r.rgb / r.a, r.a) : 0;
}

float4 LodIrradiance(float3 P, float3 N, uint lod) {
    float3 f = P / SpacingAt(0, lod) - 0.5;
    int3 base = int3(floor(f));
    float3 a = f - float3(base);
    float3 sum = 0;
    float wsum = 0, tsum = 0;
    for (uint i = 0; i < 8; ++i) {
        int3 off = int3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
        uint p = FindCell(0, base + off, lod);
        if (p == SRC_NONE) continue;
        float3 tri = lerp(1.0 - a, a, float3(off));
        float4 e = ProbeIrradiance(p, N);
        float w = tri.x * tri.y * tri.z * e.a;
        sum += e.rgb * w;
        wsum += w;
        tsum += tri.x * tri.y * tri.z;
    }
    return wsum > 1e-6 ? float4(sum / wsum, saturate(wsum / max(tsum, 1e-6) * 2.0)) : 0;
}

float3 Heat(float x) {   // black, blue, cyan, green, yellow, red, white
    x = saturate(x) * 6.0;
    const float3 c[7] = {float3(0, 0, 0), float3(0, 0, 1), float3(0, 1, 1), float3(0, 1, 0), float3(1, 1, 0),
                         float3(1, 0, 0), float3(1, 1, 1)};
    uint i = min((uint)x, 5u);
    return lerp(c[i], c[i + 1], x - i);
}

float3 KeyColor(uint key) {
    uint h = Hash(key * 2654435761u + 7u);
    return float3(h & 255u, (h >> 8) & 255u, (h >> 16) & 255u) / 255.0 * 0.75 + 0.25;
}

// Inspection views (S.debug): written as the colour itself, the raster pass shows it as it is (view 3).
float3 Inspect(uint2 px, uint w, float3 P, float3 N, float lodF, uint lod0) {
    uint mode = S.debug;
    if (mode == SRC_DEBUG_CELLS) {
        uint ci = min(S.debugCascade, S.cascadeCount - 1);
        int3 k = CellOf(P, ci, lod0);
        uint p = FindCell(ci, k, lod0);
        float3 col = p == SRC_NONE ? float3(1, 0, 1) : KeyColor(CellKey(k, ci, lod0));
        // cell borders: dark lines where the position is near a face of the cell
        float3 f = frac(P / SpacingAt(ci, lod0));
        float edge = min(min(min(f.x, 1 - f.x), min(f.y, 1 - f.y)), min(f.z, 1 - f.z));
        return col * (edge < 0.04 ? 0.25 : 1.0);
    }
    if (mode == SRC_DEBUG_PROBES) {
        uint p = FindCell(0, CellOf(P, 0, lod0), lod0);
        return p == SRC_NONE ? float3(1, 0, 1) : ProbeIrradiance(p, N).rgb;
    }
    if (mode == SRC_DEBUG_HIT) {
        uint c = SLoad(w + 7);
        if (c == 0xFFu) return 0;
        return Heat((c + 1.0) / max(S.cascadeCount, 1u) * (5.0 / 6.0));
    }
    if (mode == SRC_DEBUG_RAYS) {
        uint p = SLoad(w + 5);
        return p == SRC_NONE ? float3(1, 0, 1) : Heat(SLoad(ProbeWord(0, p, 4)) / 256.0);
    }
    if (mode == SRC_DEBUG_LOD) return KeyColor(lod0 + 1) * (0.6 + 0.4 * frac(lodF));
    if (mode == SRC_DEBUG_HISTORY) {
        uint p = FindCell(0, CellOf(P, 0, lod0), lod0);
        return p == SRC_NONE ? float3(1, 0, 1) : Heat(SLoad(S.hist + p) / float(max(S.histMax, 1u)));
    }
    return 0;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= F.resolution.x || id.y >= F.resolution.y) return;
    uint w = PixelWord(id.xy);
    float lodF = asfloat(SLoad(w + 3));
    if (lodF < 0) {
        gScreenGI[id.xy] = 0;
        return;
    }
    float3 P = asfloat(gS.Load3(w * 4));
    float3 N = UnpackNormal(SLoad(w + 4));
    uint lod0, lod1;
    float w1;
    PixelLods(lodF, lod0, lod1, w1);
    if (S.debug) {
        gScreenGI[id.xy] = float4(Inspect(id.xy, w, P, N, lodF, lod0), 1);
        return;
    }
    float4 e = LodIrradiance(P, N, lod0);
    if (w1 > 0) {
        float4 e1 = LodIrradiance(P, N, lod1);
        if (e1.a > 0) e = e.a > 0 ? lerp(e, e1, w1) : e1;
    }
    gScreenGI[id.xy] = e;
}
