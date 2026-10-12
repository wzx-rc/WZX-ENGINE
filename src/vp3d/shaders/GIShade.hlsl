// Real-time shading with the DDGI probe volume:
// primary ray + direct light (1 light sample, 1 shadow ray) + diffuse GI from the probes
// + one glossy reflection ray whose hit is shaded with direct light and probe GI.
// The result is blended into the accumulation texture with a capped history (exponential average),
// so the image keeps updating when the probes or the scene change.
#include "DDGICommon.hlsli"

// Radiance leaving a secondary hit (reflection ray), diffuse only.
float3 ShadeSecondary(Hit h, float3 V, inout uint rng) {
    GpuInstance gi = gInst[h.inst];
    if (gi.lightId != NO_LIGHT) return 0;   // handled by the caller (MIS with the light sampling)
    Surf s = MakeSurf(gMats[gi.materialId], h);
    float3 a = DiffuseAlbedo(s);
    float3 c = h.front ? s.emission : 0;
    c += a / PI * DirectIrradiance(h.P, h.N, h.Ng, rng);
    c += a * SampleIrradiance(h.P, h.N, V);
    return c;
}

float3 ShadePixel(float3 o, float3 d, inout uint rng, inout Guides g) {
    Hit h;
    if (!TraceClosest(o, d, 1e30, 0xFF & ~MASK_LIGHT_HIDDEN, h)) {
        g.albedo = 1; g.normal = -d; g.depth = 1e6; g.id = 0;
        return (F.flags & FRAME_FLAG_ENV_VISIBLE) ? Environment(d) : 0;
    }
    GpuInstance gi = gInst[h.inst];
    g.depth = h.t;
    g.id = h.inst + 1;
    if (gi.lightId != NO_LIGHT) {
        g.albedo = 1; g.normal = h.N;
        return h.front ? gLights[gi.lightId].radiance * F.lightScale : 0;
    }
    Surf s = MakeSurf(gMats[gi.materialId], h);
    if ((F.flags & FRAME_FLAG_BEVEL) && s.bevel > 0 && h.front)
        h.N = BevelNormal(h, s.bevel * F.bevelScale, (uint)F.bevelSamples, rng);
    float3 V = -d;
    if (dot(h.N, V) <= 0) h.N = h.Ng;
    g.albedo = s.trans > 0.5 ? 1.0.xxx : lerp(s.base, SpecF0(s) + 0.2, s.metal);
    g.normal = h.N;

    float3 N = h.N;
    float NoV = saturate(dot(N, V));
    float a = Alpha(s.rough);
    float3 F0 = SpecF0(s);
    // Diffuse albedo (SSS shaded as diffuse). Like the path tracer, the Fresnel loss uses the half vector
    // for direct light; for the probe term (no single direction) the normal-incidence F0 is used.
    float3 kd = s.base * DiffuseWeight(s);
    bool showDirect = G.view != 1, showIndirect = G.view != 2;

    float3 c = h.front ? s.emission : 0;

    // Direct light: diffuse + specular, the specular part MIS-weighted against the reflection ray.
    if (showDirect) {
        float3 L, Le;
        float dist, lpdf;
        if (SampleLight(h.P, rng, L, dist, Le, lpdf) && dot(L, h.Ng) > 0) {
            float NoL = dot(N, L);
            if (NoL > 0 && !Occluded(OffsetRay(h.P, h.Ng), L, dist * 0.999)) {
                float3 H = normalize(V + L);
                float NoH = saturate(dot(N, H)), VoH = saturate(dot(V, H));
                float D = D_GGX(NoH, a);
                float3 Fr = SchlickF(F0, VoH);
                float3 spec = D * G2_Smith(NoL, max(NoV, 1e-4), a) * Fr / (4.0 * max(NoV, 1e-4));
                float pdfSpec = D * G1_Smith(max(NoV, 1e-4), a) / (4.0 * max(NoV, 1e-4));
                c += (kd * (1.0 - Fr) / PI * NoL + spec * PowerHeuristic(lpdf, pdfSpec)) * Le / lpdf;
            }
        }
    }

    // Diffuse GI from the probes.
    if (showIndirect) c += kd * (1.0 - F0) * SampleIrradiance(h.P, N, V);

    // One glossy reflection ray (VNDF sampling), its hit shaded with direct light + probe GI.
    if (s.trans < 0.5 && Luminance(F0) > 0.01) {
        float3 Hm = SampleGGXHalf(N, V, a, Rnd2(rng));
        float3 L = reflect(-V, Hm);
        float NoL = dot(N, L);
        if (NoL > 0 && dot(L, h.Ng) > 0) {
            float VoH = saturate(dot(V, Hm));
            float3 w = SchlickF(F0, VoH) * G2_Smith(NoL, max(NoV, 1e-4), a) / max(G1_Smith(max(NoV, 1e-4), a), 1e-4);
            float pdfSpec = D_GGX(saturate(dot(N, Hm)), a) * G1_Smith(max(NoV, 1e-4), a) / (4.0 * max(NoV, 1e-4));
            Hit r;
            float3 o2 = OffsetRay(h.P, h.Ng);
            if (!TraceClosest(o2, L, 1e30, 0xFF, r)) {
                if (showIndirect) c += w * Environment(L);
            } else {
                GpuInstance ri = gInst[r.inst];
                if (ri.lightId != NO_LIGHT) {
                    if (showDirect && r.front)
                        c += w * gLights[ri.lightId].radiance * F.lightScale * PowerHeuristic(pdfSpec, LightPdf(ri.lightId, L, r.t));
                } else if (showIndirect) {
                    c += w * ShadeSecondary(r, -L, rng);
                }
            }
        }
    }
    return c;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= F.resolution.x || id.y >= F.resolution.y) return;
    uint rng = Hash(id.x + id.y * 65521u + Hash(F.frameIndex * 9781u + 1u));
    float2 jitter = F.sampleIndex == 0 ? float2(0.5, 0.5) : Rnd2(rng);
    float2 uv = (float2(id.xy) + jitter) / float2(F.resolution);
    float2 ndc = float2(2.0 * uv.x - 1.0, 1.0 - 2.0 * uv.y);
    float3 dir = normalize(F.camFwd + F.camRight * (ndc.x * F.aspect * F.tanHalfFovY) + F.camUp * (ndc.y * F.tanHalfFovY));

    Guides g = (Guides)0;
    float3 L = ShadePixel(F.camPos, dir, rng, g);
    if (any(isnan(L)) || any(isinf(L))) L = 0;
    float lum = Luminance(L);
    if (lum > F.fireflyClamp) L *= F.fireflyClamp / lum;

    // Capped temporal history: n frames since the last reset, at most temporalMax.
    float n = (float)min(F.sampleIndex, G.temporalMax);
    float4 acc = gTex[SLOT_ACCUM][id.xy];
    float3 prev = F.sampleIndex == 0 ? L : acc.rgb / max(acc.w, 1.0);
    float k = 1.0 / (n + 1.0);
    float3 avg = lerp(prev, L, k);
    gTex[SLOT_ACCUM][id.xy] = float4(avg * (n + 1.0), n + 1.0);

    float4 alb = gTex[SLOT_ALBEDO][id.xy];
    float4 nd = gTex[SLOT_NORMAL][id.xy];
    if (F.sampleIndex == 0) {
        alb = float4(g.albedo, (float)g.id);
        nd = float4(g.normal, g.depth);
    } else {
        alb.rgb = lerp(alb.rgb, g.albedo, k);
        alb.w = (float)g.id;
        nd = lerp(nd, float4(g.normal, g.depth), k);
    }
    gTex[SLOT_ALBEDO][id.xy] = alb;
    gTex[SLOT_NORMAL][id.xy] = nd;
}
