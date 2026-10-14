// Real-time shading without ray tracing (DDGI raster). The visible surface comes from the raster G-buffer
// (PSGBuffer in Raster.hlsl), whose direct light (shadow map, area lights, sun) is already lit there. This pass adds
// the diffuse light of the probes, the sky reflection of the glossy and glass surfaces, and the temporal
// accumulation of GIShade.hlsl. No TLAS is read: glass is approximated (sky seen through a tint), reflections of
// objects are not traced (sky only).
#include "DDGICommon.hlsli"
#include "Temporal.hlsli"

// G-buffer (u15..u18), see Raster.hlsl GBufferOut. Miss: geom.w < 0.
RWTexture2D<float4> gGBDirect : register(u15);
RWTexture2D<float4> gGBNormal : register(u16);
RWTexture2D<float4> gGBGeom : register(u17);
RWTexture2D<float4> gGBInfo : register(u18);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= F.resolution.x || id.y >= F.resolution.y) return;
    float3 dir = CurPixelDir(float2(id.xy));
    float4 gd = gGBDirect[id.xy], gn = gGBNormal[id.xy], gg = gGBGeom[id.xy], gi = gGBInfo[id.xy];

    float3 L, N, Pc;
    float depth;
    float3 albedo;
    float objId;
    if (gg.w < 0) {   // sky
        L = (F.flags & FRAME_FLAG_ENV_VISIBLE) ? Environment(dir) : 0;
        N = -dir;
        depth = 1e6;
        Pc = F.camPos + dir * depth;
        albedo = 1;
        objId = 0;
    } else if (gi.z > 0.5) {   // emissive quad: its radiance, as the ray tracer
        L = gd.rgb;
        N = gn.xyz;
        depth = gd.w;
        Pc = F.camPos + dir * depth;
        albedo = 1;
        objId = gi.w + 1;
    } else {
        N = gn.xyz;
        if (dot(N, -dir) < 0) N = -N;
        depth = gd.w;
        Pc = F.camPos + dir * depth;
        Hit h = (Hit)0;
        h.P = Pc;
        h.N = N;
        h.Ng = N;
        h.uv = gi.xy;
        h.front = gn.w > 0.5;
        Surf s = MakeSurf(gMats[(uint)(gg.w + 0.5)], h);
        float3 V = -dir;
        float NoV = max(dot(N, V), 1e-4);
        float3 F0 = SpecF0(s);
        albedo = s.trans > 0.5 ? 1.0.xxx : lerp(s.base, F0 + 0.2, s.metal);
        objId = gi.w + 1;
        if (s.trans > 0.5) {
            // Glass: reflection of the sky and the sky seen through the tinted surface (no refraction trace).
            float Fr = FresnelDielectric(NoV, 1.0 / s.ior);
            L = s.emission + Fr * EnvironmentRough(reflect(dir, N), s.rough) + (1.0 - Fr) * s.base * Environment(dir);
        } else {
            L = gd.rgb;   // direct light and emission, lit in the raster pass
            float3 kd = s.base * DiffuseWeight(s);
            L += kd * (1.0 - F0) * SampleIrradiance(Pc, N, V);   // diffuse light of the probes
            L += SchlickF(F0, NoV) * (1.0 - s.rough * 0.7) * EnvironmentRough(reflect(dir, N), s.rough);   // sky reflection
        }
    }
    if (any(isnan(L)) || any(isinf(L))) L = 0;
    float lum = Luminance(L);
    if (lum > F.fireflyClamp) L *= F.fireflyClamp / lum;

    gTex[SLOT_ACCUM][id.xy] = TemporalAccumulate(L, Pc, N, depth);
    gTex[SLOT_NORMAL][id.xy] = float4(N, depth);
    gTex[SLOT_ALBEDO][id.xy] = float4(albedo, objId);
}
