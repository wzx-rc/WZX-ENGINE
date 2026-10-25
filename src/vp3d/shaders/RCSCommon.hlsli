// Screen-space Radiance Cascades: probes on the visible surfaces (one per screen tile and cascade), their rays
// traced in the whole scene (hardware ray tracing), so off-screen and hidden geometry and lights still count.
// The world-space cascades (RCTrace.hlsl) stay the cache of the further bounces: the hits read their probes.
#include "RCCommon.hlsli"

ConstantBuffer<RCSConstants> RS : register(b4);
RWStructuredBuffer<float4> gRCS : register(u16);   // texels of every cascade, then 2 float4 per probe (P + valid, N)
RWTexture2D<float4> gScreenGI : register(u17);     // per pixel: irradiance / pi (rgb), confidence (a)

// Camera ray through a viewport position in pixels (no jitter, no depth of field).
float3 PixelDir(float2 px) {
    float2 uv = px / float2(F.resolution);
    float2 ndc = float2(2.0 * uv.x - 1.0, 1.0 - 2.0 * uv.y);
    return normalize(F.camFwd + F.camRight * (ndc.x * F.aspect * F.tanHalfFovY) + F.camUp * (ndc.y * F.tanHalfFovY));
}

// Screen position (pixels) of probe k of cascade c: the centre of its tile, kept inside the viewport.
float2 RCSProbePixel(RCSCascade c, uint2 k) {
    return min((float2(k) + 0.5) * c.grid.z, float2(F.resolution) - 0.5);
}

// Probe record: position (w = 1 when the probe sits on a surface) and normal.
void RCSProbe(RCSCascade c, uint2 k, out float4 P, out float3 N) {
    uint i = c.probeOffset + (k.x + k.y * c.grid.x) * 2;
    P = gRCS[i];
    N = gRCS[i + 1].xyz;
}

// Weight of a probe at Pp (normal Np) for a point P with normal N: probes off the tangent plane of P (another
// surface, behind an edge) or facing elsewhere count less. tol: distance scale of the plane test.
float RCSPlaneWeight(float3 P, float3 N, float3 Pp, float3 Np, float tol) {
    float d = abs(dot(Pp - P, N));
    float wn = saturate(dot(N, Np));
    return exp(-d / max(tol, 1e-4)) * (0.05 + wn * wn);
}

// Real spherical harmonics up to order 2 (9 functions) of a unit direction.
void RCSShBasis(float3 d, out float y[9]) {
    y[0] = 0.282095;
    y[1] = 0.488603 * d.y;
    y[2] = 0.488603 * d.z;
    y[3] = 0.488603 * d.x;
    y[4] = 1.092548 * d.x * d.y;
    y[5] = 1.092548 * d.y * d.z;
    y[6] = 0.315392 * (3.0 * d.z * d.z - 1.0);
    y[7] = 1.092548 * d.x * d.z;
    y[8] = 0.546274 * (d.x * d.x - d.y * d.y);
}

// Irradiance / pi for normal N from the radiance SH of probe p of cascade 0 (cosine lobe convolution).
float3 RCSShIrradiance(uint p, float3 N) {
    float v[28];
    uint o = RS.shOffset + p * 7;
    for (uint q = 0; q < 7; ++q) {
        float4 f = gRCS[o + q];
        v[q * 4] = f.x; v[q * 4 + 1] = f.y; v[q * 4 + 2] = f.z; v[q * 4 + 3] = f.w;
    }
    float y[9];
    RCSShBasis(N, y);
    const float A[9] = {3.141593, 2.094395, 2.094395, 2.094395, 0.785398, 0.785398, 0.785398, 0.785398, 0.785398};
    float3 e = 0;
    for (uint k = 0; k < 9; ++k) e += float3(v[k * 3], v[k * 3 + 1], v[k * 3 + 2]) * (A[k] * y[k]);
    return max(e, 0.0) / PI;
}
