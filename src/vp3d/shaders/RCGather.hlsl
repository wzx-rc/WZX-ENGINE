// Radiance Cascades, pass 2: one thread group per probe of cascade 0 (= the DDGI probe grid). The merged radiance
// of its directions is integrated into the probe's irradiance tile (cosine weighted, each direction weighted by the
// solid angle of its octahedral texel) and its distance tile, and the probe is classified (inside geometry or not).
// Same atlases as DDGIBlend.hlsl, so GIShade.hlsl and the raster pass read cascade 0 like DDGI probes.
// Without hysteresis (RC.hysteresis = 0, the default) the tiles are rewritten from scratch every frame.
#include "RCCommon.hlsli"

groupshared float4 sVal[RC_MAX_DIR0 * RC_MAX_DIR0];
groupshared float4 sDir[RC_MAX_DIR0 * RC_MAX_DIR0];   // xyz direction, w solid angle
groupshared uint sBackfaces, sOwnBack, sFrontHits;   // all back faces, back / front faces hit by cascade 0 itself

// Tile texel (border included) -> interior texel index whose direction it mirrors (octahedral wrap), as DDGIBlend.
int2 InteriorTexel(int2 t, int n) {
    bool bx = t.x == 0 || t.x == n + 1, by = t.y == 0 || t.y == n + 1;
    int2 r = t;
    if (bx && by) r = int2(t.x == 0 ? n : 1, t.y == 0 ? n : 1);
    else if (by) r = int2(n + 1 - t.x, t.y == 0 ? 1 : n);
    else if (bx) r = int2(t.x == 0 ? 1 : n, n + 1 - t.y);
    return r - 1;
}

float3 TileTexelDir(int2 t, int n) {
    float2 uv = (float2(InteriorTexel(t, n)) + 0.5) / n;
    return OctDecode(uv * 2.0 - 1.0);
}

[numthreads(64, 1, 1)]
void main(uint3 gid : SV_GroupID, uint3 tid3 : SV_GroupThreadID) {
    uint p = gid.x;
    uint tid = tid3.x;
    RCCascade c0 = RC.c[0];
    uint dirRes = min(c0.dirRes, RC_MAX_DIR0);
    uint dirs = dirRes * dirRes;
    if (tid == 0) { sBackfaces = 0; sOwnBack = 0; sFrontHits = 0; }
    GroupMemoryBarrierWithGroupSync();
    if (tid < dirs) {
        uint2 t = uint2(tid % dirRes, tid / dirRes);
        float4 v = gRC[c0.offset + p * dirs + tid];
        sVal[tid] = v;
        sDir[tid] = float4(RCTexelDir(t, dirRes), RCTexelSolidAngle(t, dirRes));
        if (v.w < 0) InterlockedAdd(sBackfaces, 1u);
        if (v.w < 0 && -v.w <= c0.tEnd) InterlockedAdd(sOwnBack, 1u);
        else if (v.w > 0 && v.w <= c0.tEnd) InterlockedAdd(sFrontHits, 1u);
    }
    GroupMemoryBarrierWithGroupSync();

    float h = (RC.hysteresis > 0 && G.frame > 0) ? RC.hysteresis : 0.0;
    // Sharpness of the distance lobe: DDGI uses 50 for 128 rays; fewer directions need a wider lobe to leave no gap.
    float sharp = max(4.0, 50.0 * dirs / 128.0);
    const uint irrTile = DDGI_IRR_N + 2, distTile = DDGI_DIST_N + 2;
    const uint irrCount = irrTile * irrTile, total = irrCount + distTile * distTile;
    for (uint t = tid; t < total; t += 64) {
        if (t < irrCount) {
            int2 tc = int2(t % irrTile, t / irrTile);
            float3 n = TileTexelDir(tc, DDGI_IRR_N);
            float3 sum = 0;
            float wsum = 0;
            for (uint i = 0; i < dirs; ++i) {
                if (sVal[i].w < 0) continue;
                float w = max(dot(n, sDir[i].xyz), 0.0) * sDir[i].w;
                sum += sVal[i].rgb * w;
                wsum += w;
            }
            float3 v = wsum > 1e-6 ? sum / wsum : 0;
            int2 px = TileOrigin(p, irrTile) + tc;
            gIrr[px] = float4(h > 0 ? lerp(v, gIrr[px].rgb, h) : v, 1);
        } else {
            uint u = t - irrCount;
            int2 tc = int2(u % distTile, u / distTile);
            float3 n = TileTexelDir(tc, DDGI_DIST_N);
            float2 sum = 0;
            float wsum = 0, best = -2;
            float bestD = G.maxDistance;
            for (uint i = 0; i < dirs; ++i) {
                float cs = dot(n, sDir[i].xyz);
                float d = min(abs(sVal[i].w), G.maxDistance);
                float w = pow(max(cs, 0.0), sharp) * sDir[i].w;
                sum += float2(d, d * d) * w;
                wsum += w;
                if (cs > best) { best = cs; bestD = d; }
            }
            float2 m = wsum > 1e-8 ? sum / wsum : float2(bestD, bestD * bestD);
            int2 px = TileOrigin(p, distTile) + tc;
            gDist[px] = float4(h > 0 ? lerp(m, gDist[px].xy, h) : m, 0, 1);
        }
    }
    // Inside geometry: many back faces (merged ones included), or more back faces than front faces among the short
    // rays of cascade 0 itself (a probe inside a thin wall only hits the wall's back faces across it; along the wall
    // its rays hit nothing).
    if (tid == 0) {
        bool inside = sBackfaces * 4 > dirs || sOwnBack > sFrontHits;
        gProbe[int2(p, 0)] = float4(inside ? 0.0 : 1.0, (float)sBackfaces, 0, 1);
    }
}
