// Radiance Cascades, pass 1 (one dispatch per cascade, from the top cascade down to cascade 0): every texel traces
// its direction over the cascade's interval, then merges what the ray did not hit with the cascade above:
//     L_i(p, w) = L_interval + T_interval * mean of the 4 finer directions of w, trilinear over the 8 parent probes
// (only the parents the probe sees, see RCVisibility.hlsl).
// Hits are shaded like the DDGI probe rays (emission + direct light + optionally the previous frame's probe
// irradiance for the further bounces). No temporal blending here: the image of a frame only depends on that frame
// (unless RC.hysteresis > 0 in the gather pass).
#include "RCCommon.hlsli"

// Merged radiance of the cascade above (index j) for direction texel t of cascade j-1, seen from the child probe
// (position P, visibility mask of its parents): the 2x2 finer directions averaged, trilinear over the parents the
// child sees (all of them when it sees none, e.g. a probe stuck inside an object).
float4 ParentRadiance(uint j, float3 P, uint visMask, uint2 t) {
    RCCascade c = RC.c[j];
    int3 base;
    float3 a;
    RCParentCell(c, P, base, a);
    float wvis = 0;
    for (uint v = 0; v < 8; ++v) {
        float3 tri = lerp(1.0 - a, a, float3(int3(v & 1, (v >> 1) & 1, (v >> 2) & 1)));
        if (visMask & (1u << v)) wvis += tri.x * tri.y * tri.z;
    }
    if (wvis < 1e-3) visMask = 0xFFu;
    float3 sum = 0;
    float dist = 0, neg = 0, wsum = 0;
    for (uint i = 0; i < 8; ++i) {
        int3 off = int3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
        int3 k = min(base + off, int3(c.counts.xyz) - 1);
        float3 tri = lerp(1.0 - a, a, float3(off));
        float w = (visMask & (1u << i)) ? tri.x * tri.y * tri.z : 0.0;
        if (w <= 0) continue;
        uint p = RCProbeIndex(c, uint3(k));
        uint first = c.offset + p * c.dirRes * c.dirRes;
        for (uint s = 0; s < 4; ++s) {
            uint2 tt = t * 2 + uint2(s & 1, s >> 1);
            float4 v = gRC[first + tt.x + tt.y * c.dirRes];
            sum += v.rgb * w;
            dist += abs(v.w) * w;
            if (v.w < 0) neg += w;
        }
        wsum += 4.0 * w;
    }
    if (wsum <= 0) return float4(0, 0, 0, 1e27);
    sum /= wsum;
    dist /= wsum;
    return float4(sum, neg > 0.5 * wsum ? -dist : dist);
}

// Merged radiance of one parent probe k (cascade j) for direction texel t of cascade j-1: its 2x2 finer directions.
float4 OneParent(RCCascade c, uint3 k, uint2 t) {
    uint first = c.offset + RCProbeIndex(c, k) * c.dirRes * c.dirRes;
    float3 sum = 0;
    float dist = 0, neg = 0;
    for (uint s = 0; s < 4; ++s) {
        uint2 tt = t * 2 + uint2(s & 1, s >> 1);
        float4 v = gRC[first + tt.x + tt.y * c.dirRes];
        sum += v.rgb;
        dist += abs(v.w);
        if (v.w < 0) neg += 1;
    }
    return float4(sum * 0.25, neg > 2 ? -dist * 0.25 : dist * 0.25);
}

// "Bilinear fix" merge: for each of the 8 parents, the child traces its own segment from the start of its interval
// to the start of that parent's interval (parent position + d * tEnd), so the two intervals join without a gap or
// an overlap: a wall between the child and a parent stops the child's segment instead of being skipped by the
// parent's ray. 8 segments per texel instead of 1 ray, the most accurate merge.
float4 BilinearFixMerge(uint ci, float3 P, float3 d, uint2 t, inout uint rng) {
    RCCascade c = RC.c[ci], pc = RC.c[ci + 1];
    int3 base;
    float3 a;
    RCParentCell(pc, P, base, a);
    float3 S = P + d * c.tStart;
    float3 sum = 0;
    float dist = 0, neg = 0, wsum = 0;
    for (uint i = 0; i < 8; ++i) {
        int3 off = int3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
        float3 tri = lerp(1.0 - a, a, float3(off));
        float w = tri.x * tri.y * tri.z;
        if (w <= 1e-5) continue;
        uint3 k = uint3(min(base + off, int3(pc.counts.xyz) - 1));
        float3 Q = RCProbePos(pc, k) + d * pc.tStart;
        float3 v = Q - S;
        float len = length(v);
        float4 r;
        Hit h;
        if (len > 1e-4 && TraceClosest(S, v / len, len, MASK_OPAQUE, h)) {
            float th = length(h.P - P);
            r = h.front ? float4(ShadeHit(h, v / len, rng), th) : float4(0, 0, 0, -th);
        } else {
            r = OneParent(pc, k, t);
        }
        sum += r.rgb * w;
        dist += abs(r.w) * w;
        if (r.w < 0) neg += w;
        wsum += w;
    }
    if (wsum <= 0) return float4(0, 0, 0, 1e27);
    return float4(sum / wsum, neg > 0.5 * wsum ? -dist / wsum : dist / wsum);
}

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint ci = RP.cascade;
    RCCascade c = RC.c[ci];
    uint dirs = c.dirRes * c.dirRes;
    uint texel = id.x;
    if (texel >= c.counts.w * dirs) return;
    uint p = texel / dirs, di = texel % dirs;
    uint2 t = uint2(di % c.dirRes, di / c.dirRes);
    float3 P = RCProbePos(c, RCProbeCoord(c, p));
    float3 d = RCTexelDir(t, c.dirRes);

    // The same light samples for a direction in every probe: their error varies smoothly in space instead of
    // making a per-probe pattern on the walls.
    uint seed = di * 7919u + ci * 104729u + 17u;
    if (RC.flags & RC_FLAG_ANIMATED_NOISE) seed += Hash(RC.frame * 31u + 7u);
    uint rng = Hash(seed);

    float4 result;
    Hit h;
    // Lights are excluded from the rays (MASK_OPAQUE): they reach the probes through next-event estimation at the
    // hits, as in DDGIUpdate.hlsl, so the shading of the pixel can add their direct light without counting it twice.
    if ((RC.flags & RC_FLAG_BILINEAR_FIX) && ci + 1 < RC.cascadeCount) {
        result = BilinearFixMerge(ci, P, d, t, rng);
    } else if (TraceClosest(P + d * c.tStart, d, c.tEnd - c.tStart, MASK_OPAQUE, h)) {
        float t = c.tStart + h.t;
        result = h.front ? float4(ShadeHit(h, d, rng), t) : float4(0, 0, 0, -t);
    } else if (ci + 1 < RC.cascadeCount) {
        result = ParentRadiance(ci + 1, P, asuint(gRC[c.maskOffset + p].x), t);
    } else {
        result = float4(Environment(d), 1e27);   // top cascade: its rays run to infinity, a miss sees the sky
    }
    // a NaN would spread to every probe merging this texel, then to the probe tiles (black blots)
    if (any(isnan(result.rgb)) || any(isinf(result.rgb))) result.rgb = 0;
    gRC[c.offset + texel] = result;
}
