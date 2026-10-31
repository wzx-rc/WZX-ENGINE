// Split Radiance Cascades, pass 5 (one dispatch per cascade, top down; one thread per probe and direction): the
// deposits become the radiance interval R and transparency T of the bucket, merged with the cone of the cascade above
// (the 4 finer directions of the up to 8 parent probes around, sparse trilinear: only the probes that exist, weights
// renormalised): L = R + T * cone. Written in place as float4 (rgb, 1 = valid; 0 = no ray reached this bucket).
#include "SRCCommon.hlsli"

float4 ParentCone(uint pc, int3 k, uint lod, uint2 b) {
    // child centre (k + 0.5) s, parent centres (m + 0.5) 2s: in parent cells, f = k / 2 - 0.25
    float3 f = float3(k) * 0.5 - 0.25;
    int3 base = int3(floor(f));
    float3 a = f - float3(base);
    float3 sum = 0;
    float wsum = 0;
    for (uint i = 0; i < 8; ++i) {
        int3 off = int3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
        uint p = FindCell(pc, base + off, lod);
        if (p == SRC_NONE) continue;
        float3 tri = lerp(1.0 - a, a, float3(off));
        float3 v = 0;
        float n = 0;
        for (uint s = 0; s < 4; ++s) {
            float4 m = asfloat(gS.Load4(EntryWord(pc, p, b * 2 + uint2(s & 1, s >> 1)) * 4));
            v += m.rgb * m.a;
            n += m.a;
        }
        if (n <= 0) continue;
        float wt = tri.x * tri.y * tri.z;
        sum += v / n * wt;
        wsum += wt;
    }
    return wsum > 0 ? float4(sum / wsum, 1) : 0;
}

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint ci = RP.cascade;
    SRCCascade c = S.c[ci];
    uint dirs = c.dirW * c.dirH;
    uint p = id.x / dirs, di = id.x % dirs;
    if (p >= ProbeCount(ci)) return;
    uint e = (c.entries + id.x * 4) * 4;
    uint4 dep = gS.Load4(e);
    uint count = dep.w >> 16;
    if (count == 0) {
        gS.Store4(e, uint4(0, 0, 0, 0));
        return;
    }
    float3 R = float3(dep.xyz) / (SRC_SCALE * count);
    float T = float(dep.w & 0xFFFFu) / count;
    if (T > 0 && ci + 1 < S.cascadeCount) {
        uint4 rec = gS.Load4(ProbeWord(ci, p, 0) * 4);
        float4 up = ParentCone(ci + 1, asint(rec.xyz), rec.w, uint2(di % c.dirW, di / c.dirW));
        R += T * up.rgb;
    }
    gS.Store4(e, uint4(asuint(R), asuint(1.0)));
}
