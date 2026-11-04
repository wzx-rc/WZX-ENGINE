// DDGI pass 1: trace DDGI_RAYS rays per probe and store radiance + hit distance.
// Hit shading = emission + direct light (1 shadow ray) + previous frame's probe irradiance (infinite bounces).
#include "DDGICommon.hlsli"

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint ray = id.x, p = id.y;
    if (ray >= DDGI_RAYS || p >= G.counts.w) return;
    if (ProbeSkipped(p)) return;   // (an old probe, not updated this frame)
    uint rng = Hash(ray * 7919u + p * 104729u + Hash(G.frame * 31u + 7u));
    float3 o = ProbePos(ProbeCoord(p));
    float3 d = ProbeRayDir(ray);

    float4 result;
    Hit h;
    // Lights are excluded: their contribution reaches the probes through next-event estimation at the hits.
    // Glass (transparent parts): the ray goes through, tinted by the glass and losing what it reflects (up to 3
    // panes); the distance kept for the visibility is that of the first opaque hit (a window is no wall).
    float3 thr = 1.0;
    float tGlass = 0;
    bool any = TraceClosest(o, d, 1e30, MASK_OPAQUE | MASK_GLASS, h);
    [loop] for (int g = 0; g < 3 && any; ++g) {
        GpuInstance gg = gInst[h.inst];
        Surf sg = MakeSurf(gMats[gg.materialId], h);
        if (sg.trans < 0.5) break;
        thr *= sg.base * 0.92;   // (about 8 % reflected at the two faces of a pane)
        tGlass += h.t;
        o = h.P + d * 1e-3;
        any = TraceClosest(o, d, 1e30, MASK_OPAQUE | MASK_GLASS, h);
    }
    if (any) h.t += tGlass;
    if (!any) {
        result = float4(Environment(d) * thr, 1e27);
    } else if (!h.front) {
        result = float4(0, 0, 0, -h.t * 0.2);   // backface: the probe sees the inside of an object
    } else {
        GpuInstance gi = gInst[h.inst];
        Surf s = MakeSurf(gMats[gi.materialId], h);
        float3 albedo = DiffuseAlbedo(s);
        float3 rad = s.emission;
        rad += albedo / PI * (DirectIrradiance(h.P, h.N, h.Ng, rng) + EnvDirectIrradiance(h.P, h.N, h.Ng, rng));
        if (G.frame > 0) rad += albedo * SampleIrradiance(h.P, h.N, -d) / max(G.energy, 1e-3);   // (the bounce without the read scale)
        result = float4(rad * thr, h.t);
    }
    gRays[int2(ray, p)] = result;
}
