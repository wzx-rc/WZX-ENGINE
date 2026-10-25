// Radiance Cascades (world space, 3D): resources and helpers shared by RCTrace.hlsl and RCGather.hlsl.
// The cascades live in one buffer (gRC); cascade 0 is laid on the DDGI probe grid, so its result fills the DDGI
// atlases and every consumer of the probes (GIShade.hlsl, the raster pass, the probe display) reads it unchanged.
#include "DDGICommon.hlsli"

ConstantBuffer<RCConstants> RC : register(b3);
RWStructuredBuffer<float4> gRC : register(u15);   // per cascade: probe * dirRes^2 + direction -> (radiance, distance)

struct RCPassConstants { uint cascade; uint pad0; uint pad1; uint pad2; };
ConstantBuffer<RCPassConstants> RP : register(b1);

float3 RCProbePos(RCCascade c, uint3 k) { return c.origin + c.spacing * float3(k); }

uint3 RCProbeCoord(RCCascade c, uint p) {
    return uint3(p % c.counts.x, (p / c.counts.x) % c.counts.y, p / (c.counts.x * c.counts.y));
}

uint RCProbeIndex(RCCascade c, uint3 k) { return k.x + c.counts.x * (k.y + c.counts.y * k.z); }

// Cell of parent cascade pc around position P: its lowest corner (the 8 parents are base + {0,1}^3, clamped to the
// grid) and the trilinear weights inside it.
void RCParentCell(RCCascade pc, float3 P, out int3 base, out float3 a) {
    float3 f = clamp((P - pc.origin) / pc.spacing, 0.0, float3(pc.counts.xyz) - 1.0);
    base = max(min(int3(floor(f)), int3(pc.counts.xyz) - 2), 0);
    a = saturate(f - float3(base));
}

// Centre direction of octahedral texel t of a dirRes x dirRes square.
float3 RCTexelDir(uint2 t, uint dirRes) {
    float2 uv = (float2(t) + 0.5) / dirRes;
    return OctDecode(uv * 2.0 - 1.0);
}

// Solid angle of the spherical triangle (a, b, c) (Van Oosterom and Strackee).
float TriSolidAngle(float3 a, float3 b, float3 c) {
    float num = abs(dot(a, cross(b, c)));
    float den = 1.0 + dot(a, b) + dot(b, c) + dot(c, a);
    return 2.0 * atan2(num, den);
}

// Solid angle covered by octahedral texel t (the oct map is not equal-area: texels near the edges are smaller).
float RCTexelSolidAngle(uint2 t, uint dirRes) {
    float2 a = float2(t) / dirRes * 2.0 - 1.0, b = float2(t + 1) / dirRes * 2.0 - 1.0;
    float3 p00 = OctDecode(a), p10 = OctDecode(float2(b.x, a.y)), p01 = OctDecode(float2(a.x, b.y)), p11 = OctDecode(b);
    float3 m = RCTexelDir(t, dirRes);
    // four triangles around the centre: exact for texels that straddle a fold of the octahedron as well
    return TriSolidAngle(m, p00, p10) + TriSolidAngle(m, p10, p11) + TriSolidAngle(m, p11, p01) + TriSolidAngle(m, p01, p00);
}
