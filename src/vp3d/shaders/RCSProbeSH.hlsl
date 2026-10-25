// Screen-space Radiance Cascades, pass 2 (one thread per cascade-0 probe): projects the probe's merged radiance onto
// order-2 spherical harmonics (9 rgb coefficients, 7 float4), each direction texel weighted by its solid angle. The
// per-pixel gather then evaluates 4 small SH instead of integrating 4 x dirRes^2 texels per pixel.
#include "RCSCommon.hlsli"

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    RCSCascade c = RS.c[0];
    uint n = c.grid.x * c.grid.y;
    if (id.x >= n) return;
    uint D = c.grid.w;
    uint first = c.offset + id.x * D * D;
    float3 sh[9];
    for (uint k = 0; k < 9; ++k) sh[k] = 0;
    for (uint t = 0; t < D * D; ++t) {
        uint2 tt = uint2(t % D, t / D);
        float3 d = RCTexelDir(tt, D);
        float3 L = gRCS[first + t].rgb * RCTexelSolidAngle(tt, D);
        float y[9];
        RCSShBasis(d, y);
        for (uint k = 0; k < 9; ++k) sh[k] += L * y[k];
    }
    float v[28];
    for (uint k = 0; k < 9; ++k) { v[k * 3] = sh[k].x; v[k * 3 + 1] = sh[k].y; v[k * 3 + 2] = sh[k].z; }
    v[27] = 0;
    uint o = RS.shOffset + id.x * 7;
    for (uint q = 0; q < 7; ++q) gRCS[o + q] = float4(v[q * 4], v[q * 4 + 1], v[q * 4 + 2], v[q * 4 + 3]);
}
