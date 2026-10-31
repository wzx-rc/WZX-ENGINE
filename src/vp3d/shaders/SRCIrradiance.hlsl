// Split Radiance Cascades, pass 6 (one thread per cascade-0 probe and irradiance texel): cosine integral of the
// probe's merged directions for the texel's normal (6x6 octahedral map), over the directions that received rays,
// renormalised; the valid weight says how much of the hemisphere they cover.
#include "SRCCommon.hlsli"

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const uint T = SRC_IRR_N * SRC_IRR_N;
    uint p = id.x / T, t = id.x % T;
    if (p >= ProbeCount(0)) return;
    float3 n = OctDecode((float2(t % SRC_IRR_N, t / SRC_IRR_N) + 0.5) / SRC_IRR_N * 2.0 - 1.0);
    SRCCascade c = S.c[0];
    float3 sum = 0;
    float wv = 0, wall = 0;
    for (uint y = 0; y < c.dirH; ++y) {
        for (uint x = 0; x < c.dirW; ++x) {
            float3 d = DecodeDir((float2(x, y) + 0.5) / float2(c.dirW, c.dirH));
            float cs = max(dot(n, d), 0.0);
            wall += cs;
            float4 m = asfloat(gS.Load4(EntryWord(0, p, uint2(x, y)) * 4));
            sum += m.rgb * (cs * m.a);
            wv += cs * m.a;
        }
    }
    float4 r = wv > 1e-6 ? float4(sum / wv, wv / max(wall, 1e-6)) : 0;
    gS.Store4((S.irr + id.x * 4) * 4, asuint(r));
}
