// Screen-space Radiance Cascades, pass 0 (one dispatch per cascade): places the probes of the cascade on the visible
// surfaces, where the camera ray through the centre of each tile hits.
#include "RCSCommon.hlsli"

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    RCSCascade c = RS.c[RP.cascade];
    uint n = c.grid.x * c.grid.y;
    if (id.x >= n) return;
    uint2 k = uint2(id.x % c.grid.x, id.x / c.grid.x);
    float3 d = PixelDir(RCSProbePixel(c, k));
    Hit h;
    float4 P = float4(0, 0, 0, 0);
    float3 N = float3(0, 1, 0);
    if (TraceClosest(F.camPos, d, 1e30, MASK_OPAQUE | MASK_GLASS, h)) {
        P = float4(OffsetRay(h.P, h.Ng), 1);   // just above the surface: its rays do not start inside it
        N = h.N;
    }
    gRCS[c.probeOffset + id.x * 2] = P;
    gRCS[c.probeOffset + id.x * 2 + 1] = float4(N, 0);
}
