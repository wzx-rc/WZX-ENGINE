// Split Radiance Cascades, pass 6b (one thread per cascade-0 probe and irradiance texel): spatial filter of the
// probes. A probe that caught too few rays towards a small bright source (an emissive ball) stands out as a dark or
// bright cell; each texel is averaged with the same texel of the probes of the 6 neighbouring cells (same LOD), weighted
// by their valid coverage, the probe itself counting double. Reads the (accumulated) irradiance, writes the filtered
// copy the gather reads; the history keeps the unfiltered one, so the blur does not build up over frames.
#include "SRCCommon.hlsli"

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const uint T = SRC_IRR_N * SRC_IRR_N;
    uint p = id.x / T, t = id.x % T;
    if (p >= ProbeCount(0)) return;
    float4 self = asfloat(gS.Load4((S.irr + id.x * 4) * 4));
    float4 acc = float4(self.rgb * self.a, self.a) * 2.0;
    float wsum = 2.0;
    uint4 rec = gS.Load4(ProbeWord(0, p, 0) * 4);
    int3 k = asint(rec.xyz);
    const int3 offs[6] = {int3(1, 0, 0), int3(-1, 0, 0), int3(0, 1, 0), int3(0, -1, 0), int3(0, 0, 1), int3(0, 0, -1)};
    for (uint i = 0; i < 6; ++i) {
        uint q = FindCell(0, k + offs[i], rec.w);
        if (q == SRC_NONE) continue;
        float4 v = asfloat(gS.Load4((S.irr + (q * T + t) * 4) * 4));
        acc += float4(v.rgb * v.a, v.a);
        wsum += 1.0;
    }
    float4 r = acc.a > 1e-6 ? float4(acc.rgb / acc.a, acc.a / wsum) : self;
    gS.Store4((S.irrF + id.x * 4) * 4, asuint(r));
}
