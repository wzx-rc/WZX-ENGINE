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
    float3 sunRadiance; float pad;
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
Texture2D<float4> gTextures[MAX_TEXTURES] : register(t0, space1);
SamplerState gLinearWrap : register(s1);

static const float PI = 3.14159265;
#define DRAW_GHOST 1u
#define DRAW_LIGHT 2u

struct VSIn {
    float3 pos : POSITION;
    float3 nrm : NORMAL;
    float2 uv : TEXCOORD;
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

float3 Environment(float3 d) {
    float y = d.y;
    float3 c = y > 0 ? lerp(R.envHorizon, R.envTop, pow(saturate(y), 0.6))
                     : lerp(R.envHorizon, R.envBottom, pow(saturate(-y), 0.4));
    return c * R.envIntensity;
}

float4 PSBackground(BgOut i) : SV_Target {
    float3 d = normalize(R.camFwd + R.camRight * (i.ndc.x * R.aspect * R.tanHalfFovY) + R.camUp * (i.ndc.y * R.tanHalfFovY));
    return float4(Environment(d), 1);
}

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

float Shadow(float3 wpos, float3 n) {
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
        s += gShadow.SampleCmpLevelZero(gShadowCmp, uv + taps[k] * radius, p.z - 0.0015);
    return s / 12.0;
}

float3 GridColor(float3 base, float3 wpos, inout float3 emission) {
    float g = R.gridScale;
    float2 p = wpos.xz / g;
    float2 w = fwidth(p);
    float2 d = abs(frac(p + 0.5) - 0.5) / max(w, 1e-4);
    float minor = 1 - saturate(min(d.x, d.y) - 0.5);
    float2 pm = p / 10.0;
    float2 dm = abs(frac(pm + 0.5) - 0.5) / max(fwidth(pm), 1e-4);
    float major = 1 - saturate(min(dm.x, dm.y) - 0.8);
    float fade = saturate(1 - length(wpos - R.camPos) / (g * 160));
    base = lerp(base, base * 2.2 + 0.015, max(minor * 0.55, major) * fade);
    float ax = 1 - saturate(abs(wpos.z) / max(fwidth(wpos.z), 1e-4) - 1.0);
    float az = 1 - saturate(abs(wpos.x) / max(fwidth(wpos.x), 1e-4) - 1.0);
    base = lerp(base, float3(0.5, 0.06, 0.05), ax);
    emission += float3(0.25, 0.02, 0.02) * ax;
    base = lerp(base, float3(0.06, 0.12, 0.6), az);
    emission += float3(0.02, 0.05, 0.3) * az;
    return base;
}

float4 Shade(VSOut i, bool ghost) {
    GpuMaterial m = gMats[D.materialId];
    float3 N = normalize(i.nrm);
    float3 V = normalize(R.camPos - i.wpos);
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
        float shadow = (li == R.keyLight) ? Shadow(i.wpos, N) : 1.0;
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
            float3 spec = D_GGX(NoH, a) * V_Smith(NoV, NoL, a) * F;
            float3 diff = diffuseCol / PI * (1 - F);
            color += (diff + spec) * R.sunRadiance * R.lightScale * NoL * Shadow(i.wpos, N);
        }
    }
    // Hemisphere ambient + reflection of the environment, darkened in cavities facing down.
    float3 sky = Environment(N) + R.ambient * (0.65 + 0.35 * N.y);
    float3 refl = Environment(reflect(-V, N)) + R.ambient;
    float3 Fa = Fresnel(f0, NoV) * (1 - rough * 0.7);
    float cavity = lerp(0.55, 1.0, saturate(i.wpos.y * 0.6 + 0.5 + N.y * 0.3));
    color += (diffuseCol * sky + Fa * refl) * cavity;
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

float4 PSMain(VSOut i) : SV_Target { return Shade(i, false); }
float4 PSGhost(VSOut i) : SV_Target { return Shade(i, true); }
float4 PSId(VSOut i) : SV_Target { return float4(0, 0, 0, (float)D.id); }
