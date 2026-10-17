// Real-time shading with the DDGI probe volume:
// primary ray + direct light (1 light sample, 1 shadow ray) + diffuse GI from the probes
// + one glossy reflection ray whose hit is shaded with direct light and probe GI.
// The result is blended into the accumulation texture with a capped history (exponential average),
// so the image keeps updating when the probes or the scene change.
#include "DDGICommon.hlsli"
#include "Temporal.hlsli"

// Radiance leaving a secondary hit (reflection ray), diffuse only.
float3 ShadeSecondary(Hit h, float3 V, inout uint rng) {
    GpuInstance gi = gInst[h.inst];
    if (IsRectLight(gi)) return 0;   // handled by the caller (MIS with the light sampling)
    Surf s = MakeSurf(gMats[gi.materialId], h);
    float3 a = DiffuseAlbedo(s);
    float3 c = h.front ? s.emission : 0;
    c += a / PI * (DirectIrradiance(h.P, h.N, h.Ng, rng) + EnvDirectIrradiance(h.P, h.N, h.Ng, rng));
    c += a * SampleIrradiance(h.P, h.N, V);
    return c;
}

float3 ShadePixel(float3 o, float3 d, inout uint rng, inout Guides g, float3 ul, float2 ur) {
    Hit h;
    if (!TraceClosest(o, d, 1e30, 0xFF & ~MASK_LIGHT_HIDDEN, h)) {
        g.albedo = 1; g.normal = -d; g.geom = -d; g.depth = 1e6; g.id = 0;
        return (F.flags & FRAME_FLAG_ENV_VISIBLE) ? Environment(d) : 0;
    }
    GpuInstance gi = gInst[h.inst];
    g.depth = h.t;
    g.id = h.inst + 1;
    if (IsRectLight(gi)) {
        g.albedo = 1; g.normal = h.N; g.geom = h.Ng;
        return h.front ? gLights[gi.lightId].radiance * F.lightScale : 0;
    }
    Surf s = MakeSurf(gMats[gi.materialId], h);
    if ((F.flags & FRAME_FLAG_BEVEL) && s.bevel > 0 && h.front)
        h.N = BevelNormal(h, s.bevel * F.bevelScale, (uint)F.bevelSamples, rng);
    float3 V = -d;
    if (dot(h.N, V) <= 0) h.N = h.Ng;
    g.albedo = s.trans > 0.5 ? 1.0.xxx : lerp(s.base, SpecF0(s) + 0.2, s.metal);
    g.normal = h.N;
    g.geom = h.Ng;

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
        uint li;
        if (SampleLightU(h.P, ul, L, dist, Le, lpdf, li) && dot(L, h.Ng) > 0) {
            float NoL = dot(N, L);
            float3 T = NoL > 0 ? LightVisibility(li, OffsetRay(h.P, h.Ng), L, dist) : 0;
            if (any(T > 0)) {
                float3 H = normalize(V + L);
                float NoH = saturate(dot(N, H)), VoH = saturate(dot(V, H));
                float D = D_GGX(NoH, a);
                float3 Fr = SchlickF(F0, VoH);
                float3 spec = D * G2_Smith(NoL, max(NoV, 1e-4), a) * Fr / (4.0 * max(NoV, 1e-4));
                float pdfSpec = D * G1_Smith(max(NoV, 1e-4), a) / (4.0 * max(NoV, 1e-4));
                c += (kd * (1.0 - Fr) / PI * NoL + spec * PowerHeuristic(lpdf, pdfSpec)) * Le * T / lpdf;
            }
        }
    }

    // HDRI direct light (diffuse only: its glossy part comes from the reflection ray, see below)
    if (showDirect && EnvEnabled()) {
        float elpdf;
        float3 EL = EnvSample(ur, elpdf);
        if (elpdf > 0 && dot(EL, h.Ng) > 0 && dot(N, EL) > 0) {
            float3 T = ShadowTransmittance(OffsetRay(h.P, h.Ng), EL, 1e30);
            c += kd * (1.0 - F0) / PI * dot(N, EL) * EnvMap(EL, 0) * T / elpdf;
        }
    }

    // Diffuse GI from the probes.
    if (showIndirect) c += kd * (1.0 - F0) * SampleIrradiance(h.P, N, V);

    // Glass: the mirror reflection and a refracted ray through the object (smooth dielectric, a few interfaces).
    if (s.trans > 0.5 && showIndirect) {
        float Fr0 = FresnelDielectric(max(NoV, 1e-4), 1.0 / s.ior);
        float3 Rd = reflect(-V, N);
        Hit rh;
        if (!TraceClosest(OffsetRay(h.P, h.Ng), Rd, 1e30, 0xFF, rh)) c += Fr0 * EnvironmentRough(Rd, s.rough);
        else if (!IsRectLight(gInst[rh.inst])) c += Fr0 * ShadeSecondary(rh, -Rd, rng);
        float3 thr = (1.0 - Fr0) * s.base;
        float3 o3 = OffsetRay(h.P, -h.Ng);
        float3 d3 = refract(-V, N, 1.0 / s.ior);
        for (uint k = 0; k < 6 && dot(d3, d3) > 1e-6 && max(thr.x, max(thr.y, thr.z)) > 1e-3; ++k) {
            Hit e;
            if (!TraceClosest(o3, d3, 1e30, 0xFF, e)) { c += thr * EnvironmentRough(d3, 0.0); break; }
            GpuInstance ei = gInst[e.inst];
            if (IsRectLight(ei)) { if (e.front) c += thr * gLights[ei.lightId].radiance * F.lightScale; break; }
            GpuMaterial em = gMats[ei.materialId];
            if (em.transmission > 0.5) {
                float eta = e.front ? 1.0 / em.ior : em.ior;
                float3 t = refract(d3, e.N, eta);
                if (dot(t, t) < 1e-6) { d3 = reflect(d3, e.N); o3 = OffsetRay(e.P, e.Ng); continue; }   // total internal reflection
                thr *= 1.0 - FresnelDielectric(saturate(dot(-d3, e.N)), eta);
                if (e.front) thr *= em.baseColor;
                o3 = OffsetRay(e.P, -e.Ng);
                d3 = normalize(t);
                continue;
            }
            c += thr * ShadeSecondary(e, -d3, rng);
            break;
        }
    }

    // Glossy reflections (VNDF sampling), each hit shaded with direct light + probe GI. Glossy surfaces take several
    // rays: a bright reflection of the light is hit in only some frames, so a single ray flickers.
    if (s.trans < 0.5 && Luminance(F0) > 0.01) {
        uint nRefl = s.rough < 0.5 ? 4u : 1u;
        for (uint k = 0; k < nRefl; ++k) {
            float2 uk = frac(ur + float2((float)k / nRefl, frac((float)k * 0.618034)));
            float3 Hm = SampleGGXHalf(N, V, a, uk);
            float3 L = reflect(-V, Hm);
            float NoL = dot(N, L);
            if (NoL > 0 && dot(L, h.Ng) > 0) {
                float VoH = saturate(dot(V, Hm));
                float3 w = SchlickF(F0, VoH) * G2_Smith(NoL, max(NoV, 1e-4), a) / max(G1_Smith(max(NoV, 1e-4), a), 1e-4) / (float)nRefl;
                float pdfSpec = D_GGX(saturate(dot(N, Hm)), a) * G1_Smith(max(NoV, 1e-4), a) / (4.0 * max(NoV, 1e-4));
                Hit r;
                float3 o2 = OffsetRay(h.P, h.Ng);
                if (!TraceClosest(o2, L, 1e30, 0xFF, r)) {
                    if (showIndirect) c += w * EnvironmentRough(L, s.rough);
                } else {
                    GpuInstance ri = gInst[r.inst];
                    if (IsRectLight(ri)) {
                        if (showDirect && r.front)
                            c += w * gLights[ri.lightId].radiance * F.lightScale * PowerHeuristic(pdfSpec, LightPdf(ri.lightId, o2, L, r.t));
                    } else if (showIndirect) {
                        c += w * ShadeSecondary(r, -L, rng);
                    }
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
    float2 jitter = (F.sampleIndex == 0 || (F.flags & FRAME_FLAG_NO_JITTER)) ? float2(0.5, 0.5) : Rnd2(rng);
    float2 uv = (float2(id.xy) + jitter) / float2(F.resolution);
    float2 ndc = float2(2.0 * uv.x - 1.0, 1.0 - 2.0 * uv.y);
    float3 org, dir;
    CameraRay(ndc, org, dir);

    Guides g = (Guides)0;
    // Stratified over frames: a low-discrepancy sequence with a per-pixel shift. The light sample and the glossy
    // reflection direction then converge in a few frames instead of flickering with random numbers.
    uint hp0 = Hash(id.x + id.y * 65521u);
    uint hp1 = Hash(hp0 ^ 0x9e3779b9u);
    uint hp2 = Hash(hp1 ^ 0x85ebca6bu);
    float3 shift = float3(hp0, hp1, hp2) * (1.0 / 4294967296.0);
    float fr = (float)F.frameIndex;
    float3 ul = frac(0.5 + fr * float3(0.8191725134, 0.6710436067, 0.5497004779) + shift);
    float2 ur = frac(0.5 + fr * float2(0.7548776662, 0.5698402910) + shift.yz);
    float3 L = ShadePixel(org, dir, rng, g, ul, ur);
    if (any(isnan(L)) || any(isinf(L))) L = 0;
    float lum = Luminance(L);
    if (lum > F.fireflyClamp) L *= F.fireflyClamp / lum;

    // Debug: probes drawn as small spheres showing their irradiance (red = disabled, inside geometry).
    if (G.flags & 1u) {
        float r = min(G.spacing.x, min(G.spacing.y, G.spacing.z)) * 0.1;
        float best = g.depth;
        uint hitProbe = 0xFFFFFFFFu;
        for (uint p = 0; p < G.counts.w; ++p) {
            float3 oc = org - ProbePos(ProbeCoord(p));
            float b = dot(oc, dir), c = dot(oc, oc) - r * r;
            float disc = b * b - c;
            if (disc <= 0) continue;
            float t = -b - sqrt(disc);
            if (t > 0 && t < best) { best = t; hitProbe = p; }
        }
        if (hitProbe != 0xFFFFFFFFu) {
            float3 n = normalize(org + dir * best - ProbePos(ProbeCoord(hitProbe)));
            L = gProbe[int2(hitProbe, 0)].x > 0.5 ? SampleTile(gIrr, hitProbe, DDGI_IRR_N, n).rgb : float3(0.6, 0.02, 0.02);
            g.albedo = 1; g.normal = n; g.depth = best; g.id = 65000;
        }
    }

    // Temporal accumulation with reprojection (Temporal.hlsli). The guides are the current frame's.
    // Stable edges (flag): the pixel-centre surface validates the history, see PathTrace.hlsl (main).
    if (F.flags & FRAME_FLAG_CENTRE_VALID) {
        float3 orgc, dirc;
        CurPixelRay(float2(id.xy), orgc, dirc);
        Hit hc;
        bool okc = TraceClosest(orgc, dirc, 1e30, 0xFF & ~MASK_LIGHT_HIDDEN, hc);
        float depthC = okc ? hc.t : 1e6;
        float3 geomC = okc ? hc.Ng : -dirc;
        float3 Pc = orgc + dirc * depthC;
        // A jittered sample that hit another object than the pixel centre is not averaged in: the history stands.
        // Otherwise a silhouette pixel alternates between the two objects from one frame to the next (flicker).
        if (g.id != (okc ? hc.inst + 1 : 0)) {
            float3 mh;
            float nh;
            FetchHistory(Pc, geomC, depthC, mh, nh);
            if (nh > 0) L = mh;
        }
        gTex[SLOT_ACCUM][id.xy] = TemporalAccumulate(L, Pc, geomC, depthC);
        gTex[SLOT_NORMAL][id.xy] = float4(geomC, depthC);
    } else {
        float3 orgp, dirp;
        CurPixelRay(float2(id.xy), orgp, dirp);
        gTex[SLOT_ACCUM][id.xy] = TemporalAccumulate(L, orgp + dirp * g.depth, g.geom, g.depth);
        gTex[SLOT_NORMAL][id.xy] = float4(g.geom, g.depth);
    }
    gTex[SLOT_ALBEDO][id.xy] = float4(g.albedo, (float)g.id);
}
