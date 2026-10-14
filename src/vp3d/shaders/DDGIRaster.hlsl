// DDGI probe update from the raster cube captures (no ray tracing): one thread group per probe, the captured probes of
// the batch integrate their six faces (PSProbe in Raster.hlsl, environment under the geometry) straight into their
// irradiance and distance tiles, with the hysteresis. Every texel of the capture is a sample weighted by its solid
// angle: no random ray set, no rotation, so the result is smooth and the same from one capture to the next (a sparse
// ray set gave each batch its own bias, visible as bands on large floors). Replaces DDGIBlend.hlsl for these probes.
// Each texel is lit as the raster pass lights it, plus the diffuse bounce read from the previous probes (albedo and
// normal of the capture), as the traced rays do in DDGIUpdate.hlsl.
#include "DDGICommon.hlsli"

// Cube captures: face f of capture slot s at columns f * DDGI_FACE, rows s * DDGI_FACE (see Renderer::recordRaster).
RWTexture2D<float4> gCapture : register(u12);       // rgb radiance, alpha distance (negative: back face, 1e27: miss, rgb = environment)
RWTexture2D<float4> gCapAlbedo : register(u13);     // diffuse albedo of the surface
RWTexture2D<float4> gCapNormal : register(u14);     // world normal, alpha 1 on a front face

// Forward and up of the six cube faces (+X -X +Y -Y +Z -Z), same table as Renderer.cpp.
static const float3 kFwd[6] = {float3(1, 0, 0), float3(-1, 0, 0), float3(0, 1, 0), float3(0, -1, 0), float3(0, 0, 1), float3(0, 0, -1)};
static const float3 kUp[6] = {float3(0, 1, 0), float3(0, 1, 0), float3(0, 0, -1), float3(0, 0, 1), float3(0, 1, 0), float3(0, 1, 0)};

#define CAP_TEXELS (6 * DDGI_FACE * DDGI_FACE)
#define THREADS 256

groupshared float4 sTex[CAP_TEXELS];   // rgb radiance with the bounce, alpha: distance (negative back face), 1e27 miss
groupshared uint sBackfaces;

// Direction of the centre of texel t (face, column, row) and its solid angle.
float3 TexelDirection(uint t, out float omega) {
    uint f = t / (DDGI_FACE * DDGI_FACE), u = t % (DDGI_FACE * DDGI_FACE);
    float2 tc = float2(u % DDGI_FACE, u / DDGI_FACE) + 0.5;
    float x = tc.x / DDGI_FACE * 2.0 - 1.0, y = 1.0 - tc.y / DDGI_FACE * 2.0;
    float3 fwd = kFwd[f], up = kUp[f], right = cross(up, fwd);
    float r2 = 1.0 + x * x + y * y;
    omega = (4.0 / (DDGI_FACE * DDGI_FACE)) / (r2 * sqrt(r2));
    return normalize(fwd + right * x + up * y);
}

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

[numthreads(THREADS, 1, 1)]
void main(uint3 gid : SV_GroupID, uint3 tid3 : SV_GroupThreadID) {
    const uint p = gid.x, tid = tid3.x, total = G.counts.w;
    if (p >= total) return;
    const uint slot = (p + total - G.captureBase) % total;
    if (slot >= G.captureSlots || p >= G.captureSeen) {
        // Not captured this frame. A probe never captured yet is inactive (its tiles hold no data).
        if (G.frame == 0 && tid == 0) gProbe[int2(p, 0)] = float4(0, 0, 0, 1);
        return;
    }
    // First capture of this probe: no history yet. Later captures blend over a cycle of total / slots frames.
    const bool fresh = G.captureSeen < total && p + G.captureSlots >= G.captureSeen;
    float h = max(pow(G.hysteresis, ceil((float)total / max(G.captureSlots, 1u))), 0.5);
    if (G.frame == 0 || fresh || (G.flags & 4u) != 0u) h = 0.0;   // warm-up after a grid change: every capture replaces the tile
    const float3 origin = ProbePos(ProbeCoord(p));

    // 1) Every texel of the six faces: radiance (environment on a miss) plus the diffuse bounce from the probes.
    if (tid == 0) sBackfaces = 0;
    GroupMemoryBarrierWithGroupSync();
    for (uint t = tid; t < CAP_TEXELS; t += THREADS) {
        uint f = t / (DDGI_FACE * DDGI_FACE), u = t % (DDGI_FACE * DDGI_FACE);
        int2 px = int2(f * DDGI_FACE + u % DDGI_FACE, slot * DDGI_FACE + u / DDGI_FACE);
        float4 c = gCapture[px];
        float4 r;
        if (c.w >= 1e26) {
            r = float4(c.rgb, 1e27);   // miss: the environment of the capture background
        } else if (c.w < 0) {
            r = float4(0, 0, 0, c.w);  // back face (inside an object)
            InterlockedAdd(sBackfaces, 1u);
        } else {
            float3 rad = c.rgb;
            float4 nrm = gCapNormal[px];
            if (G.frame > 0 && nrm.w > 0.5) {
                float omega;
                float3 dt = TexelDirection(t, omega);
                rad += gCapAlbedo[px].rgb * SampleIrradiance(origin + dt * c.w, nrm.xyz, -dt);
            }
            r = float4(rad, c.w);
        }
        sTex[t] = r;
    }
    GroupMemoryBarrierWithGroupSync();

    // 2) The tiles: cosine-weighted mean radiance (irradiance / pi) and distance moments, each texel of the capture
    // weighted by its solid angle. Back faces count for the distance only.
    const uint irrTile = DDGI_IRR_N + 2, distTile = DDGI_DIST_N + 2;
    const uint irrCount = irrTile * irrTile, tiles = irrCount + distTile * distTile;
    for (uint k = tid; k < tiles; k += THREADS) {
        if (k < irrCount) {
            int2 tc = int2(k % irrTile, k / irrTile);
            float3 n = TexelDir(tc, DDGI_IRR_N);
            float3 sum = 0;
            float wsum = 0;
            for (uint t = 0; t < CAP_TEXELS; ++t) {
                float4 s = sTex[t];
                if (s.w < 0) continue;
                float omega;
                float w = max(dot(n, TexelDirection(t, omega)), 0.0) * omega;
                sum += s.rgb * w;
                wsum += w;
            }
            float3 v = wsum > 1e-6 ? sum / wsum : 0;
            int2 at = TileOrigin(p, irrTile) + tc;
            float3 prev = gIrr[at].rgb;
            float hh = h;
            // Large changes (light moved, object moved) converge faster.
            float lp = Luminance(prev), lv = Luminance(v);
            if (abs(lv - lp) > 2.0 * max(min(lv, lp), 0.02)) hh = min(hh, 0.85);
            gIrr[at] = float4(lerp(v, prev, hh), 1);
        } else {
            uint u = k - irrCount;
            int2 tc = int2(u % distTile, u / distTile);
            float3 n = TexelDir(tc, DDGI_DIST_N);
            float2 sum = 0;
            float wsum = 0;
            for (uint t = 0; t < CAP_TEXELS; ++t) {
                float omega;
                float w = pow(max(dot(n, TexelDirection(t, omega)), 0.0), 50.0) * omega;
                float d = min(abs(sTex[t].w), G.maxDistance);
                sum += float2(d, d * d) * w;
                wsum += w;
            }
            if (wsum > 1e-9) {
                int2 at = TileOrigin(p, distTile) + tc;
                float2 prev = gDist[at].xy;
                gDist[at] = float4(lerp(sum / wsum, prev, h), 0, 1);
            }
        }
    }
    if (tid == 0) gProbe[int2(p, 0)] = float4(sBackfaces > CAP_TEXELS / 4 ? 0.0 : 1.0, (float)sBackfaces, 0, 1);
}
