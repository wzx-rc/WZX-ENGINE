// DDGI pass 1: trace DDGI_RAYS rays per probe and store radiance + hit distance.
// Hit shading = emission + direct light (1 shadow ray) + previous frame's probe irradiance (infinite bounces).
#include "DDGICommon.hlsli"

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint ray = id.x, p = id.y;
    if (ray >= DDGI_RAYS || p >= G.counts.w) return;
    uint rng = Hash(ray * 7919u + p * 104729u + Hash(G.frame * 31u + 7u));
    float3 o = ProbePos(ProbeCoord(p));
    float3 d = ProbeRayDir(ray);

    float4 result;
    Hit h;
    // Lights are excluded: their contribution reaches the probes through next-event estimation at the hits.
    if (!TraceClosest(o, d, 1e30, MASK_OPAQUE, h)) {
        result = float4(Environment(d), 1e27);
    } else if (!h.front) {
        result = float4(0, 0, 0, -h.t * 0.2);   // backface: the probe sees the inside of an object
    } else {
        GpuInstance gi = gInst[h.inst];
        Surf s = MakeSurf(gMats[gi.materialId], h);
        float3 albedo = DiffuseAlbedo(s);
        float3 rad = s.emission;
        rad += albedo / PI * (DirectIrradiance(h.P, h.N, h.Ng, rng) + EnvDirectIrradiance(h.P, h.N, h.Ng, rng));
        if (G.frame > 0) rad += albedo * SampleIrradiance(h.P, h.N, -d);
        result = float4(rad, h.t);
    }
    gRays[int2(ray, p)] = result;
}
