// Split Radiance Cascades, pass 4 (one thread per pixel): ray splitting. The pixel's ray (its index in the R2
// sequence of its probe, mapped to an equal-area direction, flipped into the surface's hemisphere) is traced from the
// visible surface to the first hit. For a hit in the interval of cascade j: the cascades below are told "nothing in
// my interval" (transparent), cascade j gets the hit radiance (opaque), the cascades above nothing. A miss is
// transparent everywhere and brings the sky to the top cascade. Each deposit goes to the probe of the pixel's cell and
// the direction bucket of the ray, as integer atomic sums (radiance in fixed point, count, transparent count).
#include "SRCCommon.hlsli"

void Deposit(uint ci, float3 P, uint lod, float3 d, float3 rad, uint transparent) {
    uint p = FindCell(ci, CellOf(P, ci, lod), lod);
    if (p == SRC_NONE) return;
    uint e = EntryWord(ci, p, DirBucket(d, ci));
    if (!transparent) {
        uint3 q = uint3(min(rad, 512.0) * SRC_SCALE + 0.5);
        if (q.x) SAdd(e, q.x);
        if (q.y) SAdd(e + 1, q.y);
        if (q.z) SAdd(e + 2, q.z);
    }
    SAdd(e + 3, 0x10000u | transparent);
}

uint Split(float3 P, uint lod, float3 d, float tHit, float3 rad) {   // returns the cascade of the radiance
    float scale = float(1u << lod);
    for (uint ci = 0; ci < S.cascadeCount; ++ci) {
        bool top = ci + 1 == S.cascadeCount;
        if (!top && tHit > S.c[ci].tEnd * scale) {
            Deposit(ci, P, lod, d, 0, 1u);
        } else {
            Deposit(ci, P, lod, d, rad, 0u);
            return tHit < 1e29 ? ci : 0xFFu;
        }
    }
    return 0xFFu;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= F.resolution.x || id.y >= F.resolution.y) return;
    uint w = PixelWord(id.xy);
    float lodF = asfloat(SLoad(w + 3));
    if (lodF < 0) return;
    uint4 rec = gS.Load4((w + 4) * 4);
    if (rec.y == SRC_NONE) return;
    float3 P = asfloat(gS.Load3(w * 4));
    float3 N = UnpackNormal(rec.x);
    // R2 sequence in 32-bit fixed point (exact for any index), plus the global jitter
    uint n = SLoad(ProbeWord(0, rec.y, 5)) + rec.z;
    float2 uv = frac(float2(n * 3242174889u, n * 2447445413u) * (1.0 / 4294967296.0) + 0.5 + float2(S.jitterX, S.jitterY));
    float3 d = DecodeDir(uv);
    if (dot(d, N) < 0) d = -d;
    Hit h;
    float tHit = 1e30;
    float3 rad;
    uint rng = Hash(id.x * 9781u + id.y * 6271u + 13u + ((S.flags & SRC_FLAG_ANIMATE) ? S.frame * 7919u : 0u));
    if (TraceClosest(P, d, 1e30, MASK_OPAQUE, h)) {
        tHit = h.t;
        rad = h.front ? ShadeHit(h, d, rng) : 0;
    } else {
        rad = Environment(d);
        // the sun disk of an HDRI, seen by a handful of rays per bucket, would make blotches: capped like the path
        // tracer's fireflies (the raster pass lights the surfaces with the sun as a directional light anyway)
        float l = Luminance(rad);
        if (l > F.fireflyClamp) rad *= F.fireflyClamp / l;
    }
    if (any(isnan(rad)) || any(isinf(rad))) rad = 0;
    uint lod0, lod1;
    float w1;
    PixelLods(lodF, lod0, lod1, w1);
    SStore(w + 7, Split(P, lod0, d, tHit, rad));   // the inspection view of the hit cascade
    if (w1 > 0) Split(P, lod1, d, tHit, rad);
}
