// Radiance Cascades, pass 0 (one dispatch per cascade below the top one): which of its 8 parent probes each probe
// sees. A parent behind a wall (or inside an object) looks at the other side of that wall: its radiance must not be
// merged into the child's, or the light leaks through thin walls and the rooms go dark near their walls (the parents
// outside see the black outside). One shadow ray per parent and per probe, shared by all the probe's directions.
#include "RCCommon.hlsli"

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint ci = RP.cascade;
    RCCascade c = RC.c[ci];
    uint p = id.x;
    if (p >= c.counts.w || ci + 1 >= RC.cascadeCount) return;
    RCCascade pc = RC.c[ci + 1];
    float3 P = RCProbePos(c, RCProbeCoord(c, p));
    int3 base;
    float3 a;
    RCParentCell(pc, P, base, a);
    uint mask = 0;
    for (uint i = 0; i < 8; ++i) {
        int3 k = min(base + int3(i & 1, (i >> 1) & 1, (i >> 2) & 1), int3(pc.counts.xyz) - 1);
        float3 v = RCProbePos(pc, uint3(k)) - P;
        float len = length(v);
        if (len < 1e-4 || !Occluded(P, v / len, len * 0.999)) mask |= 1u << i;
    }
    gRC[c.maskOffset + p] = float4(asfloat(mask), 0, 0, 0);
}
