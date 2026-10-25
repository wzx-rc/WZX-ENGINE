// Screen-space Radiance Cascades, pass 2 (one thread per pixel): the indirect irradiance of the pixel's surface from
// the 4 cascade-0 probes around it (bilinear on the screen, weighted by surface plane and normal), each integrated
// over its directions with the cosine and the solid angle of every direction texel. Written as irradiance / pi, the
// unit of the probe atlases, into the screen GI texture the raster pass reads instead of the probe volume.
#include "RCSCommon.hlsli"

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= F.resolution.x || id.y >= F.resolution.y) return;
    float2 px = float2(id.xy) + 0.5;
    float3 d = PixelDir(px);
    Hit h;
    if (!TraceClosest(F.camPos, d, 1e30, MASK_OPAQUE | MASK_GLASS, h)) {
        gScreenGI[id.xy] = 0;
        return;
    }
    float3 P = OffsetRay(h.P, h.Ng), N = h.N;
    RCSCascade c = RS.c[0];
    uint D = c.grid.w;
    float2 f = px / c.grid.z - 0.5;
    int2 base = int2(floor(f));
    float2 a = f - float2(base);
    float3 sum = 0;
    float wsum = 0;
    for (uint i = 0; i < 4; ++i) {
        int2 off = int2(i & 1, i >> 1);
        uint2 k = (uint2)clamp(base + off, int2(0, 0), int2(c.grid.xy) - 1);
        float2 bl = lerp(1.0 - a, a, float2(off));
        float4 Pp;
        float3 Np;
        RCSProbe(c, k, Pp, Np);
        if (Pp.w < 0.5) continue;
        float w = bl.x * bl.y * RCSPlaneWeight(P, N, Pp.xyz, Np, RS.planeTolerance);
        if (w <= 1e-6) continue;
        uint first = c.offset + (k.x + k.y * c.grid.x) * D * D;
        float3 e = 0;
        float ce = 0;
        for (uint t = 0; t < D * D; ++t) {
            uint2 tt = uint2(t % D, t / D);
            float cs = dot(RCTexelDir(tt, D), N);
            if (cs <= 0) continue;
            float wd = cs * RCTexelSolidAngle(tt, D);
            e += gRCS[first + t].rgb * wd;
            ce += wd;
        }
        if (ce > 0) {
            sum += e / ce * w;
            wsum += w;
        }
    }
    gScreenGI[id.xy] = wsum > 0 ? float4(sum / wsum, saturate(wsum * 8.0)) : 0;
}
