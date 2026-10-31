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
    float4 e = LodIrradiance(P, N, lod0);
    if (w1 > 0) {
        float4 e1 = LodIrradiance(P, N, lod1);
        if (e1.a > 0) e = e.a > 0 ? lerp(e, e1, w1) : e1;
    }
    gScreenGI[id.xy] = e;
}
