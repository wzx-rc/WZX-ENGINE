// DDGI pass 1 (raster, no ray tracing): the ray set of each captured probe is read from its cube capture
// (PSProbe in Raster.hlsl) instead of being traced. One thread group per captured probe, one thread per ray.
// A ray reads the texel of the cube face it points to: radiance and distance, back face, or miss (the environment,
// drawn into the capture by PSProbeBackground: no environment code here, so the pass needs no ray tracing).
// A surface hit gets its diffuse bounce from the previous frame's probes, as the traced hits do (DDGIUpdate.hlsl).
#include "DDGICommon.hlsli"

// Cube captures: face f of capture slot s at columns f * DDGI_FACE, rows s * DDGI_FACE (see Renderer::recordRaster).
RWTexture2D<float4> gCapture : register(u12);       // rgb radiance, alpha distance (negative: back face, 1e27: miss, rgb = environment)
RWTexture2D<float4> gCapAlbedo : register(u13);     // diffuse albedo of the surface
RWTexture2D<float4> gCapNormal : register(u14);     // world normal, alpha 1 on a front face

// Forward and up of the six cube faces (+X -X +Y -Y +Z -Z), same table as Renderer.cpp.
static const float3 kFwd[6] = {float3(1, 0, 0), float3(-1, 0, 0), float3(0, 1, 0), float3(0, -1, 0), float3(0, 0, 1), float3(0, 0, -1)};
static const float3 kUp[6] = {float3(0, 1, 0), float3(0, 1, 0), float3(0, 0, -1), float3(0, 0, 1), float3(0, 1, 0), float3(0, 1, 0)};

[numthreads(DDGI_RAYS, 1, 1)]
void main(uint3 gid : SV_GroupID, uint3 tid3 : SV_GroupThreadID) {
    uint slot = gid.x, ray = tid3.x;
    uint p = (G.captureBase + slot) % G.counts.w;
    float3 d = ProbeRayDir(ray);

    // The face whose axis is the closest to the ray.
    uint f = 0;
    float best = -2.0;
    for (uint k = 0; k < 6; ++k) {
        float c = dot(d, kFwd[k]);
        if (c > best) { best = c; f = k; }
    }
    float3 fwd = kFwd[f], up = kUp[f], right = cross(up, fwd);
    float z = dot(d, fwd);
    float x = dot(d, right) / z, y = dot(d, up) / z;   // face coordinates in [-1, 1] (90 degrees)
    int2 t = int2(clamp(floor((0.5 + 0.5 * x) * DDGI_FACE), 0, DDGI_FACE - 1),
                  clamp(floor((0.5 - 0.5 * y) * DDGI_FACE), 0, DDGI_FACE - 1));
    int2 px = int2(f * DDGI_FACE, slot * DDGI_FACE) + t;
    float4 c = gCapture[px];

    float4 result;
    if (c.w >= 1e26) {
        result = float4(c.rgb, 1e27);   // miss: the environment of the capture background
    } else if (c.w < 0) {
        result = float4(0, 0, 0, c.w);           // back face (inside an object)
    } else {
        float3 rad = c.rgb;
        float4 nrm = gCapNormal[px];
        if (G.frame > 0 && nrm.w > 0.5) {
            // Direction and position of the texel centre (the captured surface is at distance c.w along it).
            float2 tc = float2(t) + 0.5;
            float3 dt = normalize(fwd + right * (tc.x / DDGI_FACE * 2.0 - 1.0) + up * (1.0 - tc.y / DDGI_FACE * 2.0));
            float3 P = ProbePos(ProbeCoord(p)) + dt * c.w;
            rad += gCapAlbedo[px].rgb * SampleIrradiance(P, nrm.xyz, -dt);
        }
        result = float4(rad, c.w);
    }
    gRays[int2(ray, p)] = result;
}
