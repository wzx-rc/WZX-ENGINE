// Lightweight raster viewport: forward PBR (GGX + Lambert), area lights
// approximated as soft point lights, one PCF shadow map for the key light,
// hemisphere ambient with a cheap cavity term, procedural ground grid.
#include "Shared.h"

struct RasterConstants {
    row_major float4x4 viewProj;
    row_major float4x4 lightViewProj;
    float3 camPos; float time;
    float3 camRight; float tanHalfFovY;
    float3 camUp; float aspect;
    float3 camFwd; uint lightCount;
    float3 envTop; float envIntensity;
    float3 envHorizon; float gridScale;
    float3 envBottom; uint keyLight;
    float3 ambient; float shadowTexel;
    float3 keyPos; float lightScale;
    float3 sunDir; float sunOn;          // direction towards the sun, 1 when the sun is the key light
    float3 sunRadiance; float sunFromEnv;   // sunFromEnv: the key is the sun of the HDRI (its highlight is in the reflections)
    float4 shIrr[9];                     // HDRI irradiance (cosine-convolved SH, without the sun), image space
    float envMapOn; float envRotation; float envMapIntensity; float envMips;
    float envVisible; uint sunLight; float giMode; float pad3;   // sunLight: index of the path tracer's sun quad (skipped here); giMode: DDGI probes (see Shade)
};
ConstantBuffer<RasterConstants> R : register(b0);

struct DrawConstants {
    float4 row0;
    float4 row1;
    float4 row2;
    uint materialId;
    uint id;
    float alpha;
    uint flags;
};
ConstantBuffer<DrawConstants> D : register(b1);

StructuredBuffer<GpuMaterial> gMats : register(t0);
Texture2D<float> gShadow : register(t1);
StructuredBuffer<GpuLight> gLights : register(t2);
SamplerComparisonState gShadowCmp : register(s0);
#ifdef VP3D_ATTRIB_SEMANTICS
// Lit (Diligent): FXC (shader model 5) has no register spaces, t0..t3 would alias the buffers above. t4..t19 are free.
Texture2D<float4> gTextures[MAX_TEXTURES] : register(t4);
#else
Texture2D<float4> gTextures[MAX_TEXTURES] : register(t0, space1);
#endif
SamplerState gLinearWrap : register(s1);
Texture2D<float4> gEnvTex : register(t3);   // HDRI (lat-long, with mips)

static const float PI = 3.14159265;

// DDGI without ray tracing (VP3D_DDGI_PROBES, R.giMode > 0): the diffuse ambient of Shade is read from the probe volume
// (DDGIProbe.hlsli, same code as the compute passes) instead of the hemisphere / HDRI terms. Lit (Diligent, FXC) has no
// register spaces: t4..t19 hold the material textures there, so the atlases move to t20..t22.
#ifdef VP3D_DDGI_PROBES
#define DDGI_PROBES_READONLY
#ifdef VP3D_ATTRIB_SEMANTICS
#define DDGI_PROBE_REG_IRR t20
#define DDGI_PROBE_REG_DIST t21
#define DDGI_PROBE_REG_STATE t22
#endif
#include "DDGIProbe.hlsli"
#endif
#define DRAW_GHOST 1u
#define DRAW_LIGHT 2u

// Vertex input semantics. The multi-API Lit renderer (src/lit, Diligent Engine) maps vertex inputs by ATTRIBn
// semantics and defines VP3D_ATTRIB_SEMANTICS; the DirectX 12 viewport keeps the named ones.
#ifdef VP3D_ATTRIB_SEMANTICS
#define SEM_POSITION ATTRIB0
#define SEM_NORMAL ATTRIB1
#define SEM_TEXCOORD ATTRIB2
#else
#define SEM_POSITION POSITION
#define SEM_NORMAL NORMAL
#define SEM_TEXCOORD TEXCOORD
#endif

struct VSIn {
    float3 pos : SEM_POSITION;
    float3 nrm : SEM_NORMAL;
    float2 uv : SEM_TEXCOORD;
};

struct VSOut {
    float4 pos : SV_Position;
    float3 wpos : WORLDPOS;
    float3 nrm : NORMAL;
    float2 uv : TEXCOORD;
};

float3 ToWorld(float3 p) {
    float4 h = float4(p, 1);
    return float3(dot(D.row0, h), dot(D.row1, h), dot(D.row2, h));
}

VSOut VSMain(VSIn i) {
    VSOut o;
    o.wpos = ToWorld(i.pos);
    float3 n = float3(dot(D.row0.xyz, i.nrm), dot(D.row1.xyz, i.nrm), dot(D.row2.xyz, i.nrm));
    o.nrm = normalize(n);
    o.uv = i.uv;
    o.pos = mul(R.viewProj, float4(o.wpos, 1));
    return o;
}

// Wireframe display: the edge moved 0.2 % of its distance towards the camera, in front of its own faces.
VSOut VSWire(VSIn i) {
    VSOut o = VSMain(i);
    o.wpos += (R.camPos - o.wpos) * 0.002;
    o.pos = mul(R.viewProj, float4(o.wpos, 1));
    return o;
}

float4 VSShadow(VSIn i) : SV_Position {
    return mul(R.lightViewProj, float4(ToWorld(i.pos), 1));
}

// ---------------------------------------------------------------- background

struct BgOut {
    float4 pos : SV_Position;
    float2 ndc : NDC;
};

BgOut VSBackground(uint id : SV_VertexID) {
    BgOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.ndc = uv * float2(2, -2) + float2(-1, 1);
    o.pos = float4(o.ndc, 1, 1);
    return o;
}

float3 Gradient(float3 d) {
    float y = d.y;
    float3 c = y > 0 ? lerp(R.envHorizon, R.envTop, pow(saturate(y), 0.6))
                     : lerp(R.envHorizon, R.envBottom, pow(saturate(-y), 0.4));
    return c * R.envIntensity;
}

// World direction to the image space of the HDRI (turned by -envRotation around +Y), same mapping as the path tracer.
float3 ToEnvSpace(float3 d) {
    float c = cos(R.envRotation), s = sin(R.envRotation);
    return float3(d.x * c + d.z * s, d.y, d.z * c - d.x * s);
}

float3 EnvMap(float3 d, float lod) {
    float3 e = ToEnvSpace(d);
    float2 uv = float2(frac(atan2(e.x, -e.z) * (0.5 / PI) + 0.5), clamp(acos(clamp(e.y, -1.0, 1.0)) / PI, 0.0005, 0.9995));
    return gEnvTex.SampleLevel(gLinearWrap, uv, lod).rgb * R.envMapIntensity;
}

// Diffuse irradiance of the HDRI from its SH projection (order 2).
float3 EnvIrradiance(float3 n) {
    float3 e = ToEnvSpace(n);
    float x = e.x, y = e.y, z = e.z;
    float3 c = R.shIrr[0].rgb * 0.282095
             + R.shIrr[1].rgb * (0.488603 * y) + R.shIrr[2].rgb * (0.488603 * z) + R.shIrr[3].rgb * (0.488603 * x)
             + R.shIrr[4].rgb * (1.092548 * x * y) + R.shIrr[5].rgb * (1.092548 * y * z)
             + R.shIrr[6].rgb * (0.315392 * (3.0 * z * z - 1.0)) + R.shIrr[7].rgb * (1.092548 * x * z)
             + R.shIrr[8].rgb * (0.546274 * (x * x - y * y));
    return max(c, 0.0) * R.envMapIntensity;
}

float3 Environment(float3 d) { return R.envMapOn > 0 ? EnvMap(d, 0) : Gradient(d); }

float4 PSBackground(BgOut i) : SV_Target {
    float3 d = normalize(R.camFwd + R.camRight * (i.ndc.x * R.aspect * R.tanHalfFovY) + R.camUp * (i.ndc.y * R.tanHalfFovY));
    return float4(R.envMapOn > 0 && R.envVisible > 0 ? EnvMap(d, 0) : Gradient(d), 1);
}

// Split-sum environment BRDF, analytic fit (Karis, mobile).
float3 EnvBRDFApprox(float3 f0, float rough, float NoV) {
    const float4 c0 = float4(-1, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1, 0.0425, 1.04, -0.04);
    float4 r = rough * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    float2 AB = float2(-1.04, 1.04) * a004 + r.zw;
    return f0 * AB.x + AB.y;
}

// DDGI probe capture (PSProbe, VSProbe): view-projection of the cube face and the probe position (root constants b3).
struct ProbeFaceConstants {
    row_major float4x4 viewProj;
    float3 origin; float pad;
};
ConstantBuffer<ProbeFaceConstants> PF : register(b3);

// ---------------------------------------------------------------- shading

float D_GGX(float NoH, float a) {
    float a2 = a * a;
    float d = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d + 1e-7);
}

float V_Smith(float NoV, float NoL, float a) {
    float a2 = a * a;
    float gv = NoL * sqrt(NoV * NoV * (1 - a2) + a2);
    float gl = NoV * sqrt(NoL * NoL * (1 - a2) + a2);
    return 0.5 / max(gv + gl, 1e-5);
}

float3 Fresnel(float3 f0, float c) { return f0 + (1 - f0) * pow(1 - saturate(c), 5); }

float Shadow(float3 wpos, float3 n, float bias) {
    float4 lp = mul(R.lightViewProj, float4(wpos + n * 0.03, 1));
    float3 p = lp.xyz / lp.w;
    float2 uv = float2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
    if (any(uv < 0) || any(uv > 1) || p.z > 1) return 1;
    const float2 taps[12] = {
        float2(-0.326, -0.406), float2(-0.840, -0.074), float2(-0.696, 0.457), float2(-0.203, 0.621),
        float2(0.962, -0.195), float2(0.473, -0.480), float2(0.519, 0.767), float2(0.185, -0.893),
        float2(0.507, 0.064), float2(0.896, 0.412), float2(-0.322, -0.933), float2(-0.792, -0.598)};
    float s = 0;
    float radius = 2.5 * R.shadowTexel;
    [unroll] for (int k = 0; k < 12; ++k)
        s += gShadow.SampleCmpLevelZero(gShadowCmp, uv + taps[k] * radius, p.z - bias);
    return s / 12.0;
}

float3 GridColor(float3 base, float3 wpos, inout float3 emission) {
    float g = R.gridScale;
    float2 p = wpos.xz / g;
    float2 w = fwidth(p);
    float2 d = abs(frac(p + 0.5) - 0.5) / max(w, 1e-4);
    float minor = 1 - saturate(min(d.x, d.y));   // thin lines (about 1 pixel), like the editor grid
    float2 pm = p / 10.0;
    float2 dm = abs(frac(pm + 0.5) - 0.5) / max(fwidth(pm), 1e-4);
    float major = 1 - saturate(min(dm.x, dm.y) - 0.3);
    float fade = saturate(1 - length(wpos - R.camPos) / (g * 160));
    base = lerp(base, base * 2.2 + 0.015, max(minor * 0.4, major * 0.8) * fade);
    float ax = 1 - saturate(abs(wpos.z) / max(fwidth(wpos.z), 1e-4) - 0.2);
    float az = 1 - saturate(abs(wpos.x) / max(fwidth(wpos.x), 1e-4) - 0.2);
    base = lerp(base, float3(0.5, 0.06, 0.05), ax);
    emission += float3(0.25, 0.02, 0.02) * ax;
    base = lerp(base, float3(0.06, 0.12, 0.6), az);
    emission += float3(0.02, 0.05, 0.3) * az;
    return base;
}

// capture: the probe capture (PSProbe). In DDGI mode its ambient is left out: the bounce is added from the probes
// by the ray pass (DDGIRaster.hlsl), so the camera view and the probes see the same light, once.
float4 Shade(VSOut i, bool ghost, float shadowBias, bool capture) {
    GpuMaterial m = gMats[D.materialId];
    float3 N = normalize(i.nrm);
    float3 V = normalize((capture ? PF.origin : R.camPos) - i.wpos);   // the probe is the viewer of its capture
    if (dot(N, V) < 0) N = -N;
    float3 base = m.baseColor;
    float3 emission = m.emission;
    if (m.baseColorTex != NO_TEXTURE) base *= gTextures[NonUniformResourceIndex(m.baseColorTex)].Sample(gLinearWrap, i.uv).rgb;
    if (m.flags & MAT_FLAG_GRID) base = GridColor(base, i.wpos, emission);
    if (D.flags & DRAW_LIGHT) return float4(m.emission * R.lightScale, 1);

    float rough = max(m.roughness, 0.04);
    float a = rough * rough;
    float metal = m.metallic;
    float trans = m.transmission;
    float3 f0 = lerp(0.08 * m.specular.xxx, base, metal);
    float3 diffuseCol = base * (1 - metal);
    if (trans > 0.5) { diffuseCol = base * 0.18; f0 = 0.05; }
    float NoV = saturate(dot(N, V)) + 1e-4;

    float3 color = 0;
    for (uint li = 0; li < R.lightCount; ++li) {
        GpuLight l = gLights[li];
        if (l.type == LIGHT_OBJECT) {   // emissive object: a sphere light (no shadow)
            float3 Lv = l.corner - i.wpos;
            float d2 = dot(Lv, Lv);
            float3 L = Lv * rsqrt(d2);
            float NoL = saturate(dot(N, L));
            if (NoL <= 0) continue;
            float r2 = l.area * l.area;
            float3 E = l.radiance * PI * r2 / max(d2, r2);
            float3 H = normalize(V + L);
            float aL = saturate(a + l.area / (2.0 * sqrt(d2)));
            float3 F = Fresnel(f0, saturate(dot(V, H)));
            float3 spec = D_GGX(saturate(dot(N, H)), aL) * V_Smith(NoV, NoL, aL) * F * (a * a) / max(aL * aL, 1e-4);
            color += (diffuseCol / PI * (1 - F) + spec) * E * NoL;
            continue;
        }
        // The sun is drawn once, by the directional term below (with the shadow map): its area light copy, made
        // for the path tracer, is skipped (it doubled the sun and left the shadows half lit).
        if (li == R.sunLight) continue;
        float3 c = l.corner + (l.edgeU + l.edgeV) * 0.5;
        float3 Ln = normalize(cross(l.edgeU, l.edgeV));
        float3 Lv = c - i.wpos;
        float d2 = dot(Lv, Lv);
        float3 L = Lv * rsqrt(d2);
        float cosL = saturate(dot(-L, Ln));
        float NoL = saturate(dot(N, L));
        if (NoL <= 0 || cosL <= 0) continue;
        float size = sqrt(l.area);
        float3 E = l.radiance * R.lightScale * l.area * cosL / max(d2, l.area * 0.5);
        float shadow = (li == R.keyLight) ? Shadow(i.wpos, N, shadowBias) : 1.0;
        float3 H = normalize(V + L);
        float NoH = saturate(dot(N, H));
        float aL = saturate(a + size / (3.0 * sqrt(d2)));   // widen the lobe for the light's size
        float3 F = Fresnel(f0, saturate(dot(V, H)));
        float3 spec = D_GGX(NoH, aL) * V_Smith(NoV, NoL, aL) * F * (a * a) / max(aL * aL, 1e-4);
        float3 diff = diffuseCol / PI * (1 - F);
        if (m.coat > 0) {
            float ac = max(m.coatRoughness * m.coatRoughness, 0.002);
            float acL = saturate(ac + size / (3.0 * sqrt(d2)));
            spec += D_GGX(NoH, acL) * V_Smith(NoV, NoL, acL) * 0.04 * m.coat * (ac * ac) / max(acL * acL, 1e-4);
        }
        color += (diff + spec) * E * NoL * shadow;
    }
    if (R.sunOn > 0) {
        float3 L = R.sunDir;
        float NoL = saturate(dot(N, L));
        if (NoL > 0) {
            float3 H = normalize(V + L);
            float NoH = saturate(dot(N, H));
            float3 F = Fresnel(f0, saturate(dot(V, H)));
            float3 spec = R.sunFromEnv > 0 ? 0 : D_GGX(NoH, a) * V_Smith(NoV, NoL, a) * F;
            float3 diff = diffuseCol / PI * (1 - F);
            color += (diff + spec) * R.sunRadiance * R.lightScale * NoL * Shadow(i.wpos, N, shadowBias);
        }
    }
    // Hemisphere ambient + reflection of the environment, darkened in cavities facing down.
    float cavity = lerp(0.55, 1.0, saturate(i.wpos.y * 0.6 + 0.5 + N.y * 0.3));
    // Ambient of Lit: diffuse and glossy parts kept apart (the DDGI probes replace the diffuse part only).
    float3 ambDiffuse, ambGlossy;
    float3 kdAmb;
    if (R.envMapOn > 0) {
        // HDRI: SH irradiance (diffuse) and the blurred mips of the image along the reflection (glossy).
        float3 Rd = reflect(-V, N);
        float3 refl = EnvMap(Rd, sqrt(rough) * max(R.envMips - 2.0, 0.0));
        float3 Fe = EnvBRDFApprox(f0, rough, NoV);
        kdAmb = diffuseCol * (1 - Fe);
        ambDiffuse = kdAmb / PI * EnvIrradiance(N) * cavity;
        ambGlossy = Fe * refl * cavity;
    } else {
        float3 sky = Environment(N) + R.ambient * (0.65 + 0.35 * N.y);
        float3 refl = Environment(reflect(-V, N)) + R.ambient;
        float3 Fa = Fresnel(f0, NoV) * (1 - rough * 0.7);
        kdAmb = diffuseCol * (1 - Fa);
        ambDiffuse = diffuseCol * sky * cavity;
        ambGlossy = Fa * refl * cavity;
    }
#ifdef VP3D_DDGI_PROBES
    if (R.giMode > 0) {
        // Diffuse GI from the probes instead of the ambient (view 1: indirect only, 2: direct only, as GIShade.hlsl).
        // Where no probe answers (inside geometry, outside the volume) the ambient of Lit stays. The glossy part is
        // the one of Lit (environment reflection): the probes are too coarse for reflections.
        if (G.view == 1 && !capture) color = 0;
        if (!capture && G.view != 2) {
            float conf;
            float3 irr = SampleIrradiance(i.wpos, N, V, conf);
            color += lerp(ambDiffuse, kdAmb * irr, conf) + ambGlossy;
        }
    } else
#endif
    color += ambDiffuse + ambGlossy;
    color += emission;
    float alpha = 1;
    if (trans > 0.5) alpha = 0.55 + 0.4 * Fresnel(0.04, NoV).x;
    if (ghost) {
        float rim = pow(1 - NoV, 2);
        color = lerp(color, float3(1.0, 0.92, 0.6), 0.35) + rim * float3(1.0, 0.85, 0.3);
        alpha = D.alpha;
    }
    return float4(color, alpha);
}

float4 PSMain(VSOut i) : SV_Target {
#ifdef VP3D_DDGI_PROBES
    // Debug (G.flags bit 1): the probes drawn as small spheres shaded with their irradiance (red = disabled), as GIShade.
    if (R.giMode > 0 && (G.flags & 1u)) {
        float3 dir = normalize(i.wpos - R.camPos);
        float best = length(i.wpos - R.camPos);
        float r = min(G.spacing.x, min(G.spacing.y, G.spacing.z)) * 0.1;
        uint hitProbe = 0xFFFFFFFFu;
        for (uint p = 0; p < G.counts.w; ++p) {
            float3 oc = R.camPos - ProbePos(ProbeCoord(p));
            float b = dot(oc, dir), c = dot(oc, oc) - r * r;
            float disc = b * b - c;
            if (disc <= 0) continue;
            float t = -b - sqrt(disc);
            if (t > 0 && t < best) { best = t; hitProbe = p; }
        }
        if (hitProbe != 0xFFFFFFFFu) {
            float3 n = normalize(R.camPos + dir * best - ProbePos(ProbeCoord(hitProbe)));
            float3 c = gProbe[int2(hitProbe, 0)].x > 0.5 ? SampleTile(gIrr, hitProbe, DDGI_IRR_N, n).rgb : float3(0.6, 0.02, 0.02);
            return float4(c, 1);
        }
    }
#endif
    return Shade(i, false, 0.0015, false);
}
float4 PSGhost(VSOut i) : SV_Target { return Shade(i, true, 0.0015, false); }
// id in w; the world normal in rgb lets the selection glow find the creases of the selected object (Tonemap.hlsl)
float4 PSId(VSOut i) : SV_Target { return float4(normalize(i.nrm) * 0.5 + 0.5, (float)D.id); }
// selection mask (coverage of the selected objects, see Renderer::recordSelectionMask)
float4 PSSelect(VSOut i) : SV_Target { return 1.0; }
// Editor wireframe display: flat lines, grey level in D.alpha (blended, no depth write).
// Flag 8 (Wireframe Override): faint light lines with a facing ratio gradient, clear where the surface faces the
// camera and fading towards the silhouettes (where the lines crowd).
float4 PSWire(VSOut i) : SV_Target {
    if (D.flags & 8u) {
        float facing = abs(dot(normalize(i.nrm), normalize(R.camPos - i.wpos)));
        return float4(D.alpha.xxx, 0.02 + 0.24 * facing * facing);
    }
    return float4(D.alpha.xxx, 0.75);
}

// ---------------------------------------------------------------- DDGI probe capture
// One 90-degree cube face per draw: the view-projection of the face and the probe position (32-bit root constants b3).
// The face is lit like the main pass; alpha = distance to the probe (negative for back faces, as the ray tracer).

VSOut VSProbe(VSIn i) {
    VSOut o;
    o.wpos = ToWorld(i.pos);
    float3 n = float3(dot(D.row0.xyz, i.nrm), dot(D.row1.xyz, i.nrm), dot(D.row2.xyz, i.nrm));
    o.nrm = normalize(n);
    o.uv = i.uv;
    o.pos = mul(PF.viewProj, float4(o.wpos, 1));
    return o;
}

struct ProbeOut {
    float4 radiance : SV_Target0;   // rgb: lit surface (direct, emission, sky), alpha: distance to the probe
    float4 albedo : SV_Target1;     // diffuse albedo: the probe bounce is added by the ray pass (DDGIRaster.hlsl)
    float4 normal : SV_Target2;     // world normal, alpha 1 on a front face
};

// Background of a probe face: the environment in the texels no geometry covers, so the ray pass (DDGIRaster.hlsl) reads
// misses from the capture itself (the Lit renderer has no environment code outside Raster.hlsl). Alpha 1e27 = miss.
// The face basis comes from its view-projection: rows 0, 1 and 3 are right, up and forward (90 degrees).
ProbeOut PSProbeBackground(BgOut i) {
    float3 d = normalize(PF.viewProj[3].xyz + PF.viewProj[0].xyz * i.ndc.x + PF.viewProj[1].xyz * i.ndc.y);
    ProbeOut o;
    o.radiance = float4(Environment(d), 1e27);
    o.albedo = 0;
    o.normal = 0;
    return o;
}

ProbeOut PSProbe(VSOut i, bool front : SV_IsFrontFace) {
    ProbeOut o;
    float d = length(i.wpos - PF.origin);
    if (!front) {   // back face: the probe sees the inside of an object
        o.radiance = float4(0, 0, 0, -d * 0.2);
        o.albedo = 0;
        o.normal = 0;
        return o;
    }
    GpuMaterial m = gMats[D.materialId];
    float3 base = m.baseColor;
    if (m.baseColorTex != NO_TEXTURE) base *= gTextures[NonUniformResourceIndex(m.baseColorTex)].Sample(gLinearWrap, i.uv).rgb;
    // Shadow bias of the probes: the main pass bias (0.0015) leaves acne on surfaces grazed by the key light.
    o.radiance = float4(Shade(i, false, 0.01, true).rgb, d);
    o.albedo = float4(base * (1.0 - m.transmission) * (1.0 - 0.6 * m.metallic), 1);
    o.normal = float4(normalize(i.nrm), 1);
    return o;
}
