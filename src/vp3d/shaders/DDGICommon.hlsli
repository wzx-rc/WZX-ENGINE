// DDGI (Dynamic Diffuse Global Illumination) probe volume: shared resources and helpers.
// Follows Majercik et al. 2019 / RTXGI: per-probe octahedral irradiance and distance moments,
// Chebyshev visibility test, rays rotated randomly each frame, temporal hysteresis.
//
// DDGI_NO_RT: the passes that need no ray tracing (DDGIRaster.hlsl, DDGIBlend.hlsl) compiled without the path
// tracer, for the multi-API Lit renderer (src/lit: DirectX 12, Vulkan, Metal).
#ifdef DDGI_NO_RT
#include "Shared.h"
static const float PI = 3.14159265358979;
float Luminance(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }
#else
#define VP3D_NO_MAIN
#include "PathTrace.hlsl"
#endif

// Probe atlases (u6 irradiance, u7 distance, u9 state) and their sampling: DDGIProbe.hlsli (also used by Raster.hlsl).
#include "DDGIProbe.hlsli"
RWTexture2D<float4> gRays : register(u8);   // probe rays (rgb radiance, hit distance)

float3 SphericalFibonacci(uint i, uint n) {
    const float PHI = 1.6180339887;
    float phi = 2.0 * PI * frac(i * (PHI - 1.0));
    float cosT = 1.0 - (2.0 * i + 1.0) / n;
    float sinT = sqrt(saturate(1.0 - cosT * cosT));
    return float3(cos(phi) * sinT, sin(phi) * sinT, cosT);
}

// Ray i of this frame's set: a spherical Fibonacci direction turned by the frame's random rotation.
float3 ProbeRayDir(uint i) {
    float3 d = SphericalFibonacci(i, DDGI_RAYS);
    return normalize(float3(dot(G.rot0.xyz, d), dot(G.rot1.xyz, d), dot(G.rot2.xyz, d)));
}

#ifndef DDGI_NO_RT
// Lambert-only next-event estimation (one light sample), returns radiance leaving towards any direction
// divided by albedo / pi, i.e. the direct irradiance term E.
float3 DirectIrradiance(float3 P, float3 N, float3 Ng, inout uint rng) {
    float3 L, Le;
    float dist, lpdf;
    uint li;
    if (!SampleLight(P, rng, L, dist, Le, lpdf, li)) return 0;
    float c = dot(N, L);
    if (c <= 0 || dot(L, Ng) <= 0) return 0;
    float3 T = LightVisibility(li, OffsetRay(P, Ng), L, dist);
    if (all(T <= 0)) return 0;
    return Le * T * c / lpdf;
}

// HDRI sun / sky as a light for the probes and the GI shading: one sample of the image (importance sampled), with
// the shadow ray through glass. Without it the image only reaches the probes through their random escape rays,
// which almost never hit the small sun disc, so the floor stays dark and the shadows of the sun are blurred.
float3 EnvDirectIrradiance(float3 P, float3 N, float3 Ng, inout uint rng) {
    if (!EnvEnabled()) return 0;
    float pdf;
    float3 L = EnvSample(Rnd2(rng), pdf);
    if (pdf <= 0) return 0;
    float c = dot(N, L);
    if (c <= 0 || dot(L, Ng) <= 0) return 0;
    float3 T = ShadowTransmittance(OffsetRay(P, Ng), L, 1e30);
    if (all(T <= 0)) return 0;
    return EnvMap(L, 0) * T * c / pdf;
}

// Albedo used for the diffuse transport in the probes (metals and SSS approximated as diffuse).
float3 DiffuseAlbedo(Surf s) {
    return s.base * (1.0 - s.trans) * (1.0 - 0.6 * s.metal);
}
#endif
