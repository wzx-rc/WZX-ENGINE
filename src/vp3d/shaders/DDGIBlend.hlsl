// DDGI pass 2 (ray traced probes): one thread group per probe. Blends this frame's rays into the irradiance and
// distance octahedral tiles (with hysteresis) and classifies probes stuck inside geometry. The raster probes
// (DDGIRaster.hlsl) integrate their captures into the tiles themselves and do not use this pass.
#include "DDGICommon.hlsli"

groupshared float4 sRay[DDGI_RAYS];
groupshared float3 sDir[DDGI_RAYS];
groupshared uint sBackfaces;

// Tile texel (border included) -> interior texel index whose direction it mirrors (octahedral wrap).
int2 InteriorTexel(int2 t, int n) {
    bool bx = t.x == 0 || t.x == n + 1, by = t.y == 0 || t.y == n + 1;
    int2 r = t;
    if (bx && by) r = int2(t.x == 0 ? n : 1, t.y == 0 ? n : 1);
    else if (by) r = int2(n + 1 - t.x, t.y == 0 ? 1 : n);
    else if (bx) r = int2(t.x == 0 ? 1 : n, n + 1 - t.y);
    return r - 1;
}

float3 TexelDir(int2 t, int n) {
    float2 uv = (float2(InteriorTexel(t, n)) + 0.5) / n;
    return OctDecode(uv * 2.0 - 1.0);
}

[numthreads(DDGI_RAYS, 1, 1)]
void main(uint3 gid : SV_GroupID, uint3 tid3 : SV_GroupThreadID) {
    uint p = gid.x;
    uint tid = tid3.x;
    if (tid == 0) sBackfaces = 0;
    GroupMemoryBarrierWithGroupSync();
    float4 r = gRays[int2(tid, p)];
    sRay[tid] = r;
    sDir[tid] = ProbeRayDir(tid);
    if (r.w < 0) InterlockedAdd(sBackfaces, 1u);
    GroupMemoryBarrierWithGroupSync();

    float h = G.frame == 0 ? 0.0 : G.hysteresis;
    const uint irrTile = DDGI_IRR_N + 2, distTile = DDGI_DIST_N + 2;
    const uint irrCount = irrTile * irrTile, total = irrCount + distTile * distTile;
    for (uint t = tid; t < total; t += DDGI_RAYS) {
        if (t < irrCount) {
            int2 tc = int2(t % irrTile, t / irrTile);
            float3 n = TexelDir(tc, DDGI_IRR_N);
            float3 sum = 0;
            float wsum = 0;
            for (uint i = 0; i < DDGI_RAYS; ++i) {
                if (sRay[i].w < 0) continue;
                float w = max(dot(n, sDir[i]), 0.0);
                sum += sRay[i].rgb * w;
                wsum += w;
            }
            float3 v = wsum > 1e-4 ? sum / wsum : 0;
            int2 px = TileOrigin(p, irrTile) + tc;
            float3 prev = gIrr[px].rgb;
            float hh = h;
            // Large changes (light moved, object moved) converge faster.
            float lp = Luminance(prev), lv = Luminance(v);
            if (abs(lv - lp) > 2.0 * max(min(lv, lp), 0.02)) hh = min(hh, 0.85);
            gIrr[px] = float4(lerp(v, prev, hh), 1);
        } else {
            uint u = t - irrCount;
            int2 tc = int2(u % distTile, u / distTile);
            float3 n = TexelDir(tc, DDGI_DIST_N);
            float2 sum = 0;
            float wsum = 0;
            for (uint i = 0; i < DDGI_RAYS; ++i) {
                float w = pow(max(dot(n, sDir[i]), 0.0), 50.0);
                float d = min(abs(sRay[i].w), G.maxDistance);
                sum += float2(d, d * d) * w;
                wsum += w;
            }
            if (wsum > 1e-6) {
                int2 px = TileOrigin(p, distTile) + tc;
                float2 prev = gDist[px].xy;
                gDist[px] = float4(lerp(sum / wsum, prev, h), 0, 1);
            }
        }
    }
    if (tid == 0) gProbe[int2(p, 0)] = float4(sBackfaces > DDGI_RAYS / 4 ? 0.0 : 1.0, (float)sBackfaces, 0, 1);
}
