// Real-time path tracer with temporal reprojection: one path per pixel per frame, blended into the history
// of the previous frames (see Temporal.hlsli). The image sharpens while the view is still and stays clean
// while the camera moves, with the same BSDF, lights and bevel as the progressive path tracer.
#define VP3D_NO_MAIN
#include "PathTrace.hlsl"
#include "Temporal.hlsli"

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= F.resolution.x || id.y >= F.resolution.y) return;
    uint2 p = id.xy;
    uint rng = Hash(p.x + p.y * 65521u + Hash(F.frameIndex * 9781u + 1u));

    float3 sum = 0;
    Guides g0 = (Guides)0;
    float3 P0 = F.camPos;
    uint spp = max(F.samplesPerFrame, 1u);
    for (uint s = 0; s < spp; ++s) {
        float2 uv = (float2(p) + Rnd2(rng)) / float2(F.resolution);
        float2 ndc = float2(2.0 * uv.x - 1.0, 1.0 - 2.0 * uv.y);
        float3 dir = normalize(F.camFwd + F.camRight * (ndc.x * F.aspect * F.tanHalfFovY) + F.camUp * (ndc.y * F.tanHalfFovY));
        float3 org = F.camPos;
        if ((F.flags & FRAME_FLAG_DOF) && F.aperture > 0) {
            float3 focal = org + dir * (F.focusDist / dot(dir, F.camFwd));
            float2 lens = ConcentricDisk(Rnd2(rng)) * F.aperture;
            org += F.camRight * lens.x + F.camUp * lens.y;
            dir = normalize(focal - org);
        }
        Guides g = (Guides)0;
        float3 L = TracePath(org, dir, rng, g);
        if (any(isnan(L)) || any(isinf(L))) L = 0;
        float lum = Luminance(L);
        if (lum > F.fireflyClamp) L *= F.fireflyClamp / lum;
        sum += L;
        if (s == 0) {
            g0 = g;
            P0 = org + dir * g.depth;
        }
    }

    // Anchor on the pixel-centre ray at the hit depth: same direction function as the history (Temporal.hlsli).
    float3 Pa = F.camPos + CurPixelDir(float2(p)) * g0.depth;
    gTex[SLOT_ACCUM][p] = TemporalAccumulate(sum / spp, Pa, g0.geom, g0.depth);
    gTex[SLOT_ALBEDO][p] = float4(g0.albedo, (float)g0.id);
    gTex[SLOT_NORMAL][p] = float4(g0.geom, g0.depth);
}
