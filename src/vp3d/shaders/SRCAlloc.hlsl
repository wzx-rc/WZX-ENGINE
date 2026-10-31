// Split Radiance Cascades, pass 2 (one dispatch per cascade, bottom up; one thread per hash slot): every key inserted
// in cascade RP.cascade gets a probe record, and inserts its parent cell (floor(k / 2), same LOD) into the cascade
// above, so a cascade has a probe wherever one of its children has one.
#include "SRCCommon.hlsli"

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint ci = RP.cascade;
    SRCCascade c = S.c[ci];
    if (id.x >= c.cap) return;
    uint key = SLoad(c.keys + id.x);
    if (key == 0u) return;
    uint p = SAdd(S.counters + ci, 1u);
    if (p >= c.maxProbes) {   // over the capacity: the cell has no probe (its pixels fall back to their neighbours)
        SStore(c.vals + id.x, SRC_NONE);
        return;
    }
    SStore(c.vals + id.x, p);
    int3 k;
    uint lod;
    KeyCell(key, ci, k, lod);
    uint b = ProbeWord(ci, p, 0);
    gS.Store4(b * 4, uint4(asuint(k), lod));
    gS.Store4((b + 4) * 4, uint4(0, 0, 0, SRC_NONE));
    if (ci + 1 < S.cascadeCount) Insert(ci + 1, CellKey(k >> 1, ci + 1, lod));
}
