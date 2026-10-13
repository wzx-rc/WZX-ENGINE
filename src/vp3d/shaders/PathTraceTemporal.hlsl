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

    // Pixel-centre primary surface: it validates the history. A jittered sample on an edge may hit the other
    // surface, which would drop the history every frame. The jittered samples are still averaged in (anti-aliasing).
    float3 dirc = CurPixelDir(float2(p));
    Hit hc;
    bool okc = TraceClosest(F.camPos, dirc, 1e30, 0xFF & ~MASK_LIGHT_HIDDEN, hc);
    float depthC = okc ? hc.t : 1e6;
    float3 geomC = okc ? hc.Ng : -dirc;
    float3 Pa = F.camPos + dirc * depthC;

    float3 sum = 0, first = 0;
    float kept = 0;
    Guides g0 = (Guides)0;
    float3 P0 = F.camPos;
    uint spp = max(F.samplesPerFrame, 1u);
    // Stable edges: a jittered sample that hit another object than the pixel centre is left out, so the edge of an
    // object does not bleed its colour into the neighbour (halo) and does not reset the history either.
    const bool centreValid = (F.flags & FRAME_FLAG_CENTRE_VALID) != 0;
    const uint centreId = okc ? hc.inst + 1 : 0;
    for (uint s = 0; s < spp; ++s) {
        float2 jit = (F.flags & FRAME_FLAG_NO_JITTER) ? float2(0.5, 0.5) : Rnd2(rng);
        float2 uv = (float2(p) + jit) / float2(F.resolution);
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
        if (s == 0) {
            g0 = g;
            P0 = org + dir * g.depth;
            first = L;
        }
        if (!centreValid || g.id == centreId) {
            sum += L;
            kept += 1.0;
        }
    }
    if (kept == 0) {   // every sample hit another object: keep the first one
        sum = first;
        kept = 1.0;
    }

    // Stable edges (flag): the pixel-centre surface validates the history. Otherwise the jittered sample does:
    // a sub-pixel edge then resets the history, which avoids the halos behind moving objects but flickers on edges.
    if (F.flags & FRAME_FLAG_CENTRE_VALID) {
        gTex[SLOT_ACCUM][p] = TemporalAccumulate(sum / kept, Pa, geomC, depthC);
        gTex[SLOT_NORMAL][p] = float4(geomC, depthC);
    } else {
        gTex[SLOT_ACCUM][p] = TemporalAccumulate(sum / kept, F.camPos + CurPixelDir(float2(p)) * g0.depth, g0.geom, g0.depth);
        gTex[SLOT_NORMAL][p] = float4(g0.geom, g0.depth);
    }
    gTex[SLOT_ALBEDO][p] = float4(g0.albedo, (float)g0.id);
}
