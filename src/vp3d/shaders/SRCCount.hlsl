// Split Radiance Cascades, ray budget (Algorithm 3 of the paper). The rays of a probe are a contiguous segment of one
// R2 low-discrepancy sequence, and the segments of the children of a probe are contiguous inside the parent's, so
// the directions are spread evenly in every cascade (random directions leave the many buckets of the upper cascades
// half empty).
// Mode 0 (one thread per pixel): the pixel takes the next ray of its cascade-0 probe.
// Mode 1 (per probe of cascade RP.cascade, bottom up): finds its parent and adds its ray count to the parent's.
// Mode 2 (per probe of cascade RP.cascade, top down): the probe's segment, inside its parent's (or a new one).
#include "SRCCommon.hlsli"

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint ci = RP.cascade, mode = RP.pad0;
    if (mode == 0) {
        uint2 px = uint2(id.x % F.resolution.x, id.x / F.resolution.x);
        if (px.y >= F.resolution.y) return;
        uint w = PixelWord(px);
        float lodF = asfloat(SLoad(w + 3));
        if (lodF < 0) return;
        float3 P = asfloat(gS.Load3(w * 4));
        uint lod = (uint)floor(lodF);
        uint p = FindCell(0, CellOf(P, 0, lod), lod);
        if (p == SRC_NONE) return;
        SStore(w + 5, p);
        SStore(w + 6, SAdd(ProbeWord(0, p, 4), 1u));
        return;
    }
    if (id.x >= ProbeCount(ci)) return;
    uint4 rec = gS.Load4(ProbeWord(ci, id.x, 0) * 4);
    if (mode == 1) {
        uint parent = FindCell(ci + 1, asint(rec.xyz) >> 1, rec.w);
        SStore(ProbeWord(ci, id.x, 7), parent);
        uint rays = SLoad(ProbeWord(ci, id.x, 4));
        if (parent != SRC_NONE && rays > 0) SAdd(ProbeWord(ci + 1, parent, 4), rays);
        return;
    }
    uint rays = SLoad(ProbeWord(ci, id.x, 4));
    uint parent = ci + 1 < S.cascadeCount ? SLoad(ProbeWord(ci, id.x, 7)) : SRC_NONE;
    uint offset = parent != SRC_NONE ? SLoad(ProbeWord(ci + 1, parent, 5)) + SAdd(ProbeWord(ci + 1, parent, 6), rays)
                                     : SAdd(S.counters + SRC_CASCADES, rays);
    SStore(ProbeWord(ci, id.x, 5), offset);
}
