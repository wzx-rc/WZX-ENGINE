// Split Radiance Cascades, clear. Mode 0 (RP.pad0): the counters and the slot keys of every hash table (start of
// the frame). Mode 1: the deposits of the probes of cascade RP.cascade (after SRCAlloc, one thread per entry).
#include "SRCCommon.hlsli"

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (RP.pad0 == 0) {
        if (id.x < S.clearWords) SStore(id.x, 0u);
        return;
    }
    uint ci = RP.cascade;
    uint dirs = S.c[ci].dirW * S.c[ci].dirH;
    if (id.x / dirs >= ProbeCount(ci)) return;
    gS.Store4((S.c[ci].entries + id.x * 4) * 4, uint4(0, 0, 0, 0));
}
