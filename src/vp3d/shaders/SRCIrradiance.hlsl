// Split Radiance Cascades, pass 6 (one thread per cascade-0 probe and irradiance texel): cosine integral of the
// probe's merged directions for the texel's normal (6x6 octahedral map), over the directions that received rays,
// renormalised; the valid weight says how much of the hemisphere they cover.
// Accumulation while nothing moves (S.histMax > 0): the same world cell in the previous frame (looked up with the
// previous camera, the keys being relative to it) is averaged in over up to histMax frames, unless an object moved
// near it this frame or the lighting changed (S.prevValid 0). The camera moving keeps the history: it is world space.
#include "SRCCommon.hlsli"

uint FindPrev(uint key) {
    if (key == SRC_NONE || S.prevCap == 0) return SRC_NONE;
    uint slot = Hash(key) & (S.prevCap - 1);
    for (uint n = 0; n < 64; ++n) {
        uint k = gPrev.Load(slot * 4);
        if (k == key) return gPrev.Load((S.prevCap + slot) * 4);
        if (k == 0u) return SRC_NONE;
        slot = (slot + 1) & (S.prevCap - 1);
    }
    return SRC_NONE;
}

bool NearMove(float3 P, float r) {
    for (uint i = 0; i < S.movedCount; ++i)
        if (all(P > S.movedMin[i].xyz - r) && all(P < S.movedMax[i].xyz + r)) return true;
    return false;
}

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
    uint hist = 1;
    if (S.histMax > 1 && S.prevValid) {
        uint4 rec = gS.Load4(ProbeWord(0, p, 0) * 4);
        int3 k = asint(rec.xyz);
        uint lod = rec.w;
        float s = SpacingAt(0, lod);
        // a moved object changes the light around it over a few cells (its shadow, its bounce): no history there
        if (!NearMove((float3(k) + 0.5) * s, s * 4.0)) {
            uint pp = FindPrev(CellKeyFrom(S.prevCamPos, k, 0, lod));
            if (pp != SRC_NONE) {
                uint h = gPrev.Load((S.prevHist + pp) * 4);
                float4 old = asfloat(gPrev.Load4((S.prevIrr + (pp * T + t) * 4) * 4));
                hist = min(h + 1, S.histMax);
                r = lerp(old, r, 1.0 / hist);
            }
        }
    }
    gS.Store4((S.irr + id.x * 4) * 4, asuint(r));
    if (t == 0) SStore(S.hist + p, hist);
}
