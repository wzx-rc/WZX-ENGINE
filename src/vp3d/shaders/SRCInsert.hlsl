// Split Radiance Cascades, pass 1 (one thread per pixel): the visible surface of the pixel (camera ray), its record,
// and the cascade-0 probe of its cell inserted into the hash table (in both LODs in the overlap of two LODs).
#include "SRCCommon.hlsli"

float3 PixelDir(float2 px) {
    float2 uv = px / float2(F.resolution);
    float2 ndc = float2(2.0 * uv.x - 1.0, 1.0 - 2.0 * uv.y);
    return normalize(F.camFwd + F.camRight * (ndc.x * F.aspect * F.tanHalfFovY) + F.camUp * (ndc.y * F.tanHalfFovY));
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= F.resolution.x || id.y >= F.resolution.y) return;
    uint w = PixelWord(id.xy);
    Hit h;
    if (!TraceClosest(F.camPos, PixelDir(float2(id.xy) + 0.5), 1e30, MASK_OPAQUE | MASK_GLASS, h)) {
        SStore(w + 3, asuint(-1.0));
        return;
    }
    float3 P = OffsetRay(h.P, h.Ng);
    float lodF = LodCoord(P);
    gS.Store4(w * 4, uint4(asuint(P), asuint(lodF)));
    gS.Store4((w + 4) * 4, uint4(PackNormal(h.N), SRC_NONE, 0, 0));
    if (GIBoundsWeight(P) <= 0) return;   // beyond the GI bounds: no probe (the pixel record stays, without a probe)
    uint lod0, lod1;
    float w1;
    PixelLods(lodF, lod0, lod1, w1);
    Insert(0, CellKey(CellOf(P, 0, lod0), 0, lod0));
    if (w1 > 0) Insert(0, CellKey(CellOf(P, 0, lod1), 0, lod1));
}
