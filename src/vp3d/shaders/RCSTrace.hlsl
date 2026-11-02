// Screen-space Radiance Cascades, pass 1 (one dispatch per cascade, top cascade first): each texel (probe on a
// surface, direction) traces its interval from the probe; what it does not hit is merged from the cascade above:
// the 2x2 finer directions of the 4 parent probes around it on the screen, weighted by screen distance (bilinear)
// and by how well each parent lies on the child's surface (parents on another surface would bring its light).
// Same hit shading as the world cascades (ShadeHit: emission, direct light, the world probes for the next bounces).
// Nothing is accumulated over frames.
#include "RCSCommon.hlsli"

// Merged radiance of the world cascade 0 in direction d around P: the direction texel of the 8 surrounding world probes
// (the active ones), trilinear. Used where no screen parent lies on the child's surface.
float3 WorldRadiance(float3 P, float3 d) {
    RCCascade c = RC.c[0];
    float3 f = clamp((P - c.origin) / c.spacing, 0.0, float3(c.counts.xyz) - 1.0);
    int3 base = max(min(int3(floor(f)), int3(c.counts.xyz) - 2), 0);
    float3 a = saturate(f - float3(base));
    uint2 t = min(uint2((OctEncode(d) * 0.5 + 0.5) * c.dirRes), c.dirRes - 1);
    float3 sum = 0;
    float wsum = 0;
    for (uint i = 0; i < 8; ++i) {
        int3 off = int3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
        uint3 k = uint3(min(base + off, int3(c.counts.xyz) - 1));
        uint p = RCProbeIndex(c, k);
        if (gProbe[int2(p, 0)].x < 0.5) continue;
        float3 tri = lerp(1.0 - a, a, float3(off));
        float w = tri.x * tri.y * tri.z;
        sum += gRC[c.offset + p * c.dirRes * c.dirRes + t.x + t.y * c.dirRes].rgb * w;
        wsum += w;
    }
    return wsum > 0 ? sum / wsum : 0;
}

float3 MergeParents(uint j, uint2 kChild, RCSCascade child, float3 P, float3 N, uint2 t) {
    RCSCascade c = RS.c[j];
    float2 px = RCSProbePixel(child, kChild);
    float2 f = px / c.grid.z - 0.5;
    int2 base = int2(floor(f));
    float2 a = f - float2(base);
    // plane tolerance of the parents: grows with the tile (x2 per cascade), not with the ray length, or parents on
    // another surface far behind (a sunlit wall seen past a ceiling) pass the test in the upper cascades
    float tol = RS.planeTolerance * float(1u << j);
    float3 sum = 0;
    float wsum = 0;
    for (uint i = 0; i < 4; ++i) {
        int2 off = int2(i & 1, i >> 1);
        uint2 k = (uint2)clamp(base + off, int2(0, 0), int2(c.grid.xy) - 1);
        float2 bl = lerp(1.0 - a, a, float2(off));
        float w = bl.x * bl.y;
        float4 Pp;
        float3 Np;
        RCSProbe(c, k, Pp, Np);
        if (Pp.w < 0.5 || w <= 0) continue;
        uint first = c.offset + (k.x + k.y * c.grid.x) * c.grid.w * c.grid.w;
        float3 v = 0;
        for (uint s = 0; s < 4; ++s) {
            uint2 tt = t * 2 + uint2(s & 1, s >> 1);
            v += gRCS[first + tt.x + tt.y * c.grid.w].rgb;
        }
        v *= 0.25;
        float wp = w * RCSPlaneWeight(P, N, Pp.xyz, Np, tol);
        sum += v * wp;
        wsum += wp;
    }
    // few parents on this surface (a narrow face, an edge): the world cascades take over progressively
    float3 world = WorldRadiance(P, RCTexelDir(t, child.grid.w));
    return wsum > 1e-6 ? lerp(world, sum / wsum, saturate(wsum / 0.1)) : world;
}

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint ci = RP.cascade;
    RCSCascade c = RS.c[ci];
    uint dirs = c.grid.w * c.grid.w;
    uint texel = id.x;
    if (texel >= c.grid.x * c.grid.y * dirs) return;
    uint p = texel / dirs, di = texel % dirs;
    uint2 k = uint2(p % c.grid.x, p / c.grid.x);
    uint2 t = uint2(di % c.grid.w, di / c.grid.w);
    float4 P;
    float3 N;
    RCSProbe(c, k, P, N);
    float3 result = 0;
    if (P.w > 0.5) {
        float3 d = RCTexelDir(t, c.grid.w);
        uint rng = Hash(di * 7919u + ci * 104729u + 29u);
        Hit h;
        if (dot(d, N) < -0.2) {
            result = 0;   // well below the surface: the surface itself
        } else if (TraceClosest(P.xyz + d * c.tStart, d, c.tEnd - c.tStart, MASK_OPAQUE, h)) {
            result = h.front ? ShadeHit(h, d, rng) : 0;
        } else if (ci + 1 < RS.cascadeCount) {
            result = MergeParents(ci + 1, k, c, P.xyz, N, t);
        } else {
            result = Environment(d);
        }
    }
    if (any(isnan(result)) || any(isinf(result))) result = 0;
    gRCS[c.offset + texel] = float4(result, 0);
}
