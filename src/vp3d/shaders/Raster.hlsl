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
    float envVisible; uint sunLight; float giMode; float orthoHalfH;   // sunLight: index of the path tracer's sun quad (skipped here); giMode: DDGI probes (see Shade); orthoHalfH: orthographic camera (half the view height, 0 = perspective)
    float3 hemiSky; float hemiOn;        // hemisphere fill (three.js HemisphereLight): irradiance from the sky / the ground, x intensity
    float3 hemiGround; float bevelOn;    // bevelOn: scale of the material bevel radius in the raster view (0 = off, needs the TLAS)
    float rtShadow; float sunTanRadius; float rtShadowSamples; float rtRefl;   // ray traced sun shadow (t12), reflections (t13)
    float centreValid; float rtLights; float rtAO; uint gridLine;
    float3 envSun2Dir; float envSun2On;  // HQ: the light of the HDRI (its brightest area) as a second, ray traced soft light
    float3 envSun2E; float envSun2Tan;   // beside the scene sun (out of the SH and the probes); its cone (tan half angle)   // centreValid: t14 is this frame's; rtLights: area
                                         // lights shadowed by rays (TLAS, space2); rtAO: ambient occluded by rays; rtProbes:
                                         // gridLine: Scene::gridLine (RGBA8 colour of the ground grid lines, 0 = default)
};
ConstantBuffer<RasterConstants> R : register(b0);

// Direction from a surface point towards the viewer (orthographic camera: the opposite of the view direction).
float3 ToViewer(float3 wpos) { return R.orthoHalfH > 0 ? -R.camFwd : normalize(R.camPos - wpos); }
// Distance at which a perspective view would see wpos as this one does (pixel footprint of the procedural grid).
float ViewDist(float3 wpos) { return R.orthoHalfH > 0 ? R.orthoHalfH / max(R.tanHalfFovY, 1e-3) : length(wpos - R.camPos); }

struct DrawConstants {
    float4 row0;
    float4 row1;
    float4 row2;
    uint materialId;
    uint id;
    float alpha;
    uint flags;
};
#ifdef VP3D_ATTRIB_SEMANTICS
ConstantBuffer<DrawConstants> D : register(b1);
#define DRAW_VS(iid)
#define DRAW_PS(v)
#else
// Instanced draws (DirectX 12 viewport): the per-object constants of the frame sit in a buffer (t7), sorted by mesh;
// a draw covers a run of instances starting at gDrawBase. D is set at the top of each entry point.
cbuffer DrawBase : register(b1) { uint gDrawBase; };
StructuredBuffer<DrawConstants> gDraws : register(t7);
static DrawConstants D;
#define DRAW_VS(iid) D = gDraws[gDrawBase + (iid)]
#define DRAW_PS(v) D = gDraws[(v).inst]
#endif

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
#define FP_SAMPLE(t, uv) gTextures[NonUniformResourceIndex(t)].Sample(gLinearWrap, (uv))
#include "Fingerprint.hlsli"
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
#ifndef VP3D_ATTRIB_SEMANTICS
// Screen-space Radiance Cascades (DDGIConstants flags bit 8): the indirect irradiance of each pixel, computed by
// RCSGather.hlsl (DirectX 12 viewport only).
#define VP3D_SCREEN_GI
Texture2D<float4> gScreenGI : register(t11);
#endif
#endif
// Bevel of the raster view (DirectX 12 only, shader model 6.5): rounded edges on every edge of every part, as the
// bevel of the path tracer (PathTrace.hlsl, BevelNormal), from inline ray queries against the same TLAS restricted to
// the instance itself. Real time version: 8 short rays along the surface (4 from just inside it: convex edges, 4 from
// just outside: concave edges) find the neighbouring face within the radius and its distance; the normal turns towards
// the bisector of the two faces as on a fillet (half way at the edge, unchanged at the radius). Deterministic (no
// noise, no accumulation needed); skipped when the radius is under half a pixel.
#ifndef VP3D_ATTRIB_SEMANTICS
RaytracingAccelerationStructure gBvScene : register(t0, space2);
StructuredBuffer<GpuVertex> gBvVerts : register(t1, space2);
StructuredBuffer<uint> gBvIdx : register(t2, space2);
StructuredBuffer<GpuInstance> gBvInst : register(t3, space2);

void BvBasis(float3 n, out float3 t, out float3 b) {
    float s = n.z >= 0 ? 1.0 : -1.0;
    float a = -1.0 / (s + n.z);
    float c = n.x * n.y * a;
    t = float3(1.0 + s * n.x * n.x * a, s * c, -s * n.x);
    b = float3(c, s + n.y * n.y * a, -n.y);
}

// geometric normal of a triangle of an instance, in world space
float3 BvFaceNormal(uint inst, uint prim, float3x4 o2w) {
    GpuInstance gi = gBvInst[inst];
    uint b = gi.indexOffset + prim * 3;
    float3 p0 = gBvVerts[gBvIdx[b + 0] + gi.vertexOffset].pos, p1 = gBvVerts[gBvIdx[b + 1] + gi.vertexOffset].pos, p2 = gBvVerts[gBvIdx[b + 2] + gi.vertexOffset].pos;
    float3 a = mul((float3x3)o2w, p1 - p0), c = mul((float3x3)o2w, p2 - p0);
    return normalize(cross(a, c));
}

float3 RasterBevel(float3 P, float3 Ng, float3 N, uint inst, float radius) {
    float3 T, B;
    BvBasis(Ng, T, B);
    const float eps = radius * 0.06;
    float3 sum = N;
    float3 seen[4];
    uint nSeen = 0;
    [unroll] for (uint k = 0; k < 8; ++k) {
        const bool inside = k < 4;   // inside: the ray leaves the part through the next face (convex edge)
        float3 dir = (k & 1) ? ((k & 2) ? -B : B) : ((k & 2) ? -T : T);
        RayQuery<RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
        RayDesc r;
        r.Origin = P + Ng * (inside ? -eps : eps); r.Direction = dir; r.TMin = 0.0; r.TMax = radius * 1.5;
        q.TraceRayInline(gBvScene, RAY_FLAG_NONE, MASK_OPAQUE | MASK_GLASS, r);
        while (q.Proceed()) {
            if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE && q.CandidateInstanceID() == inst)
                q.CommitNonOpaqueTriangleHit();
        }
        if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) continue;
        float3 nh = BvFaceNormal(inst, q.CommittedPrimitiveIndex(), q.CommittedObjectToWorld3x4());
        if (dot(nh, dir) < 0) nh = -nh;   // outward: the ray from inside leaves along it, the ray from outside meets it
        if (!inside) nh = -nh;
        if (dot(nh, Ng) > 0.9) continue;   // the same (curved) surface
        float3 hp = r.Origin + dir * q.CommittedRayT();
        float d = abs(dot(hp - P, nh));   // distance to the plane of the next face = to the edge
        if (d >= radius) continue;
        bool dup = false;
        [unroll] for (uint s2 = 0; s2 < 4; ++s2) if (s2 < nSeen && dot(seen[s2], nh) > 0.98) dup = true;
        if (dup || nSeen >= 4) continue;
        seen[nSeen++] = nh;
        float t = 1.0 - d / radius;
        sum += nh * (t * t * (3.0 - 2.0 * t));   // smooth: the bisector (N + nh) at the edge
    }
    return normalize(sum);
}

// Visibility from P towards T (the TLAS of the frame, opaque parts only; the light quads are not opaque).
float BvVisible(float3 P, float3 Ng, float3 T) {
    float3 o = P + Ng * (2e-3 * (1.0 + max(abs(P.x), max(abs(P.y), abs(P.z)))));
    float3 d = T - o;
    float len = length(d);
    RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
    RayDesc r;
    r.Origin = o; r.Direction = d / max(len, 1e-6); r.TMin = 0.0; r.TMax = len * 0.999;
    q.TraceRayInline(gBvScene, RAY_FLAG_NONE, MASK_OPAQUE, r);
    q.Proceed();
    return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? 0.0 : 1.0;
}

float BvIGN(float2 pix) { return frac(52.9829189 * frac(dot(pix, float2(0.06711056, 0.00583715)))); }

// Visibility of a distant light of angular radius atan(tanR) around L: 4 rays in its cone (turned per pixel).
float BvConeShadow(float3 P, float3 Ng, float3 L, float tanR, float2 pix) {
    float3 T, B;
    BvBasis(L, T, B);
    float rot = BvIGN(pix) * 6.2831853, vis = 0;
    [unroll] for (uint k = 0; k < 4; ++k) {
        float r = sqrt((k + 0.5) / 4.0) * tanR, a = rot + k * 2.39996;
        float3 d = normalize(L + T * (r * cos(a)) + B * (r * sin(a)));
        vis += BvVisible(P, Ng, P + d * 1e4);
    }
    return vis * 0.25;
}

// Shadow of a rectangle light: two rays to stratified points of the rectangle (turned per pixel).
float BvAreaShadow(float3 P, float3 Ng, GpuLight l, float2 pix) {
    float r = BvIGN(pix);
    float2 a = frac(float2(0.25, 0.25) + r * float2(0.7548777, 0.5698403)), b = frac(a + 0.5);
    return 0.5 * (BvVisible(P, Ng, l.corner + l.edgeU * a.x + l.edgeV * a.y) + BvVisible(P, Ng, l.corner + l.edgeU * b.x + l.edgeV * b.y));
}

// Ambient occlusion of the environment: 2 cosine rays up to `range` (a hit at distance t occludes 1 - t / range).
float BvAO(float3 P, float3 Ng, float range, float2 pix) {
    float3 T, B;
    BvBasis(Ng, T, B);
    float rot = BvIGN(pix);
    float occ = 0;
    [unroll] for (uint k = 0; k < 2; ++k) {
        float2 u = frac(float2((k + 0.5) / 2.0, k * 0.618034 + rot));
        float rr = sqrt(u.x), ph = 6.2831853 * (u.y + rot);
        float3 d = normalize(T * (rr * cos(ph)) + B * (rr * sin(ph)) + Ng * sqrt(max(0.0, 1.0 - u.x)));
        float3 o = P + Ng * (2e-3 * (1.0 + max(abs(P.x), max(abs(P.y), abs(P.z)))));
        RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
        RayDesc r;
        r.Origin = o; r.Direction = d; r.TMin = 0.0; r.TMax = range;
        q.TraceRayInline(gBvScene, RAY_FLAG_NONE, MASK_OPAQUE, r);
        q.Proceed();
        if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) occ += 1.0 - q.CommittedRayT() / range;
    }
    return 1.0 - occ / 2.0;
}
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
    uint iid : SV_InstanceID;
};

struct VSOut {
    float4 pos : SV_Position;
    float3 wpos : WORLDPOS;
    float3 nrm : NORMAL;
    float2 uv : TEXCOORD;
    float3 opos : OBJPOS;   // object space position and normal (roughness mask projection)
    float3 onrm : OBJNRM;
#ifndef VP3D_ATTRIB_SEMANTICS
    nointerpolation uint inst : INSTIDX;   // index in gDraws
#endif
};

float3 ToWorld(float3 p) {
    float4 h = float4(p, 1);
    return float3(dot(D.row0, h), dot(D.row1, h), dot(D.row2, h));
}

VSOut VSMain(VSIn i) {
    DRAW_VS(i.iid);
    VSOut o;
#ifndef VP3D_ATTRIB_SEMANTICS
    o.inst = gDrawBase + i.iid;
#endif
    o.wpos = ToWorld(i.pos);
    float3 n = float3(dot(D.row0.xyz, i.nrm), dot(D.row1.xyz, i.nrm), dot(D.row2.xyz, i.nrm));
    o.nrm = normalize(n);
    o.uv = i.uv;
    o.opos = i.pos;
    o.onrm = i.nrm;
    o.pos = mul(R.viewProj, float4(o.wpos, 1));
    return o;
}

// Wireframe display: the edge moved 0.2 % of its distance towards the camera, in front of its own faces (orthographic
// camera: a small fraction of the view height).
VSOut VSWire(VSIn i) {
    VSOut o = VSMain(i);
    o.wpos += R.orthoHalfH > 0 ? -R.camFwd * (R.orthoHalfH * 0.004) : (R.camPos - o.wpos) * 0.002;
    o.pos = mul(R.viewProj, float4(o.wpos, 1));
    return o;
}

float4 VSShadow(VSIn i) : SV_Position {
    DRAW_VS(i.iid);
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
    // (orthographic camera: every pixel looks the same way)
    float3 d = R.orthoHalfH > 0 ? R.camFwd : normalize(R.camFwd + R.camRight * (i.ndc.x * R.aspect * R.tanHalfFovY) + R.camUp * (i.ndc.y * R.tanHalfFovY));
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

#if defined(VP3D_RT_SHADOWS) && !defined(VP3D_ATTRIB_SEMANTICS)
// Sun shadow ray traced per pixel (DirectX 12 viewport, R.rtShadow): computed before this pass by SunShadowTrace.hlsl
// (rays inside the sun's disc, from the smooth surface) and SunShadowFilter.hlsl (bilateral filter), read here
// instead of the shadow map: physical penumbra, no shadow map resolution, no temporal.
Texture2D<float> gSunShadow : register(t12);
Texture2D<float4> gRefl : register(t13);   // ray traced reflections (alpha 1 where traced), DirectX 12 viewport
Texture2D<float4> gCentre : register(t14);   // raw sun trace: .y = depth of the surface the pixel centre sees
#endif

// Pixel to read the traced screen textures from (sun shadow, screen GI, reflections). They are computed for the
// surface at each pixel centre, but with MSAA an edge pixel also shades the other surface it covers: that fragment
// would get the values of its neighbour across the edge (white or dark dotted lines along the edges). It takes the
// pixel of the 3x3 around whose centre surface matches its own: same depth and same normal (where a block stands on
// the floor both surfaces have the same depth along the contact line; only the normal tells them apart).
#if defined(VP3D_RT_SHADOWS) && !defined(VP3D_ATTRIB_SEMANTICS)
float CentreMismatch(int2 q, float d, float3 n) {
    float4 c = gCentre.Load(int3(q, 0));
    float3 cn = float3(c.zw, 1.0 - abs(c.z) - abs(c.w));   // octahedral normal (as SunShadowTrace's OctEnc)
    if (cn.z < 0) cn.xy = (1.0 - abs(cn.yx)) * float2(cn.x >= 0 ? 1.0 : -1.0, cn.y >= 0 ? 1.0 : -1.0);
    cn = normalize(cn);
    return abs(c.y - d) / (0.01 * d) + (1.0 - dot(n, cn)) * 20.0;
}
#endif

int2 TracedPixel(float2 pos, float3 wpos, float3 n) {
    int2 p = int2(pos);
#if defined(VP3D_RT_SHADOWS) && !defined(VP3D_ATTRIB_SEMANTICS)
    if (R.centreValid <= 0) return p;
    float d = length(wpos - R.camPos);
    float best = CentreMismatch(p, d, n);
    if (best < 1.0) return p;
    uint w, h;
    gCentre.GetDimensions(w, h);
    int2 bp = p;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            int2 q = clamp(p + int2(x, y), int2(0, 0), int2(w, h) - 1);
            float e = CentreMismatch(q, d, n);
            if (e < best) { best = e; bp = q; }
        }
    return bp;
#else
    return p;
#endif
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
    float fade = saturate(1 - ViewDist(wpos) / (g * 160));
    if (R.gridLine != 0u) {   // the colour of the lines (Scene::gridLine), its opacity in the top byte
        float3 lc = pow(float3(R.gridLine & 255u, (R.gridLine >> 8) & 255u, (R.gridLine >> 16) & 255u) / 255.0, 2.2);
        base = lerp(base, lc, max(minor * 0.4, major * 0.8) * fade * ((R.gridLine >> 24) / 255.0));
    } else
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
    float3 V = capture ? normalize(PF.origin - i.wpos) : ToViewer(i.wpos);   // the probe is the viewer of its capture
    if (dot(N, V) < 0) N = -N;
#ifndef VP3D_ATTRIB_SEMANTICS
    if (R.bevelOn > 0 && m.bevelRadius > 0 && !capture && !ghost) {   // rounded edges, as the path tracer
        float3 Ng = normalize(cross(ddx(i.wpos), ddy(i.wpos)));
        if (dot(Ng, N) < 0) Ng = -Ng;
        const float rad = m.bevelRadius * R.bevelOn;
        if (rad > 0.5 * max(length(ddx(i.wpos)), length(ddy(i.wpos))))   // at least half a pixel wide
            N = RasterBevel(i.wpos, Ng, N, D.id - 1, rad);
    }
#endif
    float3 base = m.baseColor;
    float3 emission = m.emission;
    if (m.baseColorTex != NO_TEXTURE) base *= gTextures[NonUniformResourceIndex(m.baseColorTex)].Sample(gLinearWrap, i.uv).rgb;
    if (m.flags & MAT_FLAG_GRID) base = GridColor(base, i.wpos, emission);
    if (D.flags & DRAW_LIGHT) return float4(m.emission * R.lightScale, 1);

    float rough = max(m.roughness, 0.04);
    float specular = m.specular, coatRough = m.coatRoughness;
    if (m.roughTex != NO_TEXTURE) {   // fingerprints (same mask as the path tracer: seed = instance index)
        float fp = FingerprintMask(m.roughTex, m.roughTexScale, i.opos, i.onrm, D.id - 1) * m.roughTexStrength * 0.5;   // half: no path traced reflections to soften it
        rough = saturate(rough + fp);
        specular *= 1.0 - 0.35 * fp;
        coatRough = saturate(coatRough + fp * 1.2);
    }
    float a = rough * rough;
    float metal = m.metallic;
    float trans = m.transmission;
    float3 f0 = lerp(0.08 * specular.xxx, base, metal);
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
        float shadow = 1.0;
        if (li == R.keyLight) {
#if defined(VP3D_RT_SHADOWS) && !defined(VP3D_ATTRIB_SEMANTICS)
            // no sun: the ray traced mask is the key area light's (SunShadowTrace, rays over the lamp's rectangle)
            if (R.rtShadow > 0 && R.sunOn <= 0 && !capture) shadow = gSunShadow.Load(int3(TracedPixel(i.pos.xy, i.wpos, N), 0));
            else
#endif
            shadow = Shadow(i.wpos, N, shadowBias);
        }
#ifndef VP3D_ATTRIB_SEMANTICS
        else if (R.rtLights > 0 && !capture) {   // the other area lights shadowed by rays too (HQ)
            float3 Ngs = normalize(i.nrm);   // (the interpolated normal: the screen derivatives are unstable at grazing angles)
            if (dot(Ngs, V) < 0) Ngs = -Ngs;
            shadow = BvAreaShadow(i.wpos, Ngs, l, i.pos.xy);
        }
#endif
        float3 H = normalize(V + L);
        float NoH = saturate(dot(N, H));
        float aL = saturate(a + size / (3.0 * sqrt(d2)));   // widen the lobe for the light's size
        float3 F = Fresnel(f0, saturate(dot(V, H)));
        float3 spec = D_GGX(NoH, aL) * V_Smith(NoV, NoL, aL) * F * (a * a) / max(aL * aL, 1e-4);
        float3 diff = diffuseCol / PI * (1 - F);
        if (m.coat > 0) {
            float ac = max(coatRough * coatRough, 0.002);
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
            float sunVis;
#if defined(VP3D_RT_SHADOWS) && !defined(VP3D_ATTRIB_SEMANTICS)
            // the ray traced, filtered visibility of this pixel (SunShadowTrace / SunShadowFilter)
            if (R.rtShadow > 0 && !capture) sunVis = gSunShadow.Load(int3(TracedPixel(i.pos.xy, i.wpos, N), 0));
            else
#endif
            sunVis = Shadow(i.wpos, N, shadowBias);
            color += (diff + spec) * R.sunRadiance * R.lightScale * NoL * sunVis;
        }
    }
    // HQ: the light of the HDRI (studio softbox...) beside the scene sun, its soft shadow traced (it is out of the SH)
    float envSun2Vis = 0, envSun2NoL = 0;
#ifndef VP3D_ATTRIB_SEMANTICS
    if (R.envSun2On > 0 && !capture) {
        envSun2NoL = saturate(dot(N, R.envSun2Dir));
        if (envSun2NoL > 0) {
            float3 Ngs = normalize(i.nrm);
            if (dot(Ngs, V) < 0) Ngs = -Ngs;
            envSun2Vis = BvConeShadow(i.wpos, Ngs, R.envSun2Dir, R.envSun2Tan, i.pos.xy);
            color += diffuseCol / PI * R.envSun2E * envSun2NoL * envSun2Vis;   // (its highlight is in the reflections)
        }
    }
#endif
    // Hemisphere ambient + reflection of the environment, darkened in cavities facing down.
    float cavity = lerp(0.55, 1.0, saturate(i.wpos.y * 0.6 + 0.5 + N.y * 0.3));
    // Ambient of Lit: diffuse and glossy parts kept apart (the DDGI probes replace the diffuse part only).
    float3 ambDiffuse, ambGlossy;
    float3 kdAmb;
    // sky radiance the glossy ambient assumes: the brighter of the sky over the hemisphere of N and the sky around the
    // reflection (a ceiling seen from below reflects the zenith); the probes tell how much of it really arrives
    float3 skyN;
    float3 kSpec;  // weight of the glossy ambient (Fresnel), for the reflection of the probe light
    if (R.envMapOn > 0) {
        // HDRI: SH irradiance (diffuse) and the blurred mips of the image along the reflection (glossy).
        float3 Rd = reflect(-V, N);
        float3 refl = EnvMap(Rd, sqrt(rough) * max(R.envMips - 2.0, 0.0));
        float3 Fe = EnvBRDFApprox(f0, rough, NoV);
        kdAmb = diffuseCol * (1 - Fe);
        ambDiffuse = kdAmb / PI * EnvIrradiance(N) * cavity;
        ambGlossy = Fe * refl * cavity;
        float3 skyA = EnvIrradiance(N) / PI, skyB = EnvIrradiance(Rd) / PI;
        skyN = dot(skyA, float3(0.2126, 0.7152, 0.0722)) > dot(skyB, float3(0.2126, 0.7152, 0.0722)) ? skyA : skyB;
        kSpec = Fe;
    } else {
        float3 sky = Environment(N) + R.ambient * (0.65 + 0.35 * N.y);
        float3 refl = Environment(reflect(-V, N)) + R.ambient;
        float3 Fa = Fresnel(f0, NoV) * (1 - rough * 0.7);
        kdAmb = diffuseCol * (1 - Fa);
        ambDiffuse = diffuseCol * sky * cavity;
        ambGlossy = Fa * refl * cavity;
        skyN = dot(sky, float3(0.2126, 0.7152, 0.0722)) > dot(refl, float3(0.2126, 0.7152, 0.0722)) ? sky : refl;
        kSpec = EnvBRDFApprox(f0, rough, NoV);   // the split-sum fit: a rough surface seen at grazing angle reflects little
    }
    if (R.hemiOn > 0) ambDiffuse += diffuseCol / PI * lerp(R.hemiGround, R.hemiSky, 0.5 + 0.5 * N.y);
#ifndef VP3D_ATTRIB_SEMANTICS
    float rtAOk = 1.0;   // the traced occlusion, also applied to the probe light (GI)
    if (R.rtAO > 0 && !capture) {   // the environment occluded by the parts, as the path tracer sees it (HQ)
        float3 Nga = normalize(i.nrm);
        if (dot(Nga, V) < 0) Nga = -Nga;
        float ao = BvAO(i.wpos, Nga, R.rtAO, i.pos.xy);
        ambDiffuse *= ao;
        rtAOk = ao;
        ambGlossy *= lerp(1.0, ao, 0.6);
    }
#endif
#ifdef VP3D_DDGI_PROBES
    if (R.giMode > 0) {
        // Diffuse GI from the probes instead of the ambient (view 1: indirect only, 2: direct only, as GIShade.hlsl).
        // Where no probe answers (inside geometry, outside the volume) the ambient of Lit stays.
        // Glossy part: the environment reflection, occluded by the ratio of the probe irradiance to the sky irradiance
        // for the same normal (indoors the probes see much less than the open sky: the floor no longer reflects it).
        if (G.view == 1 && !capture) color = 0;
        if (!capture && G.view != 2) {
            float conf;
            float3 irr = SampleIrradiance(i.wpos, N, V, conf);
#ifdef VP3D_SCREEN_GI
            if (G.flags & 8u) {   // screen-space cascades where they answer, the probe volume elsewhere
                float4 s = gScreenGI.Load(int3(TracedPixel(i.pos.xy, i.wpos, N), 0));
                irr = lerp(irr, s.rgb, s.a);
                conf = max(conf, s.a);
            }
#endif
            const float3 lw = float3(0.2126, 0.7152, 0.0722);
            float occ = saturate(dot(irr, lw) / max(dot(skyN, lw), 1e-4));
            occ *= occ;   // squared: grazing reflections of the sky indoors are occluded harder
            // what the occluded sky no longer gives, the room gives: the probe light, reflected (metals keep a reflection)
            float3 glossy = ambGlossy * occ + kSpec * irr * cavity * (1.0 - occ);
            float3 glossyAmb = lerp(ambGlossy, glossy, conf);
#if defined(VP3D_RT_SHADOWS) && !defined(VP3D_ATTRIB_SEMANTICS)
            if (R.rtRefl > 0) {   // ray traced reflection of this pixel (ReflectTrace / ReflectFilter), Fresnel-weighted
                float4 rr = gRefl.Load(int3(TracedPixel(i.pos.xy, i.wpos, N), 0));
                if (rr.a > 0.5) glossyAmb = kSpec * rr.rgb;
            }
#endif
            // the HDRI light seen by the probes is drawn above with its traced shadow: out of their irradiance;
            if (R.envSun2On > 0) irr = max(irr - R.envSun2E * (envSun2NoL * envSun2Vis / PI), 0.0);
            color += lerp(ambDiffuse, kdAmb * irr, conf) + glossyAmb;
        }
    } else
#endif
    {
#if defined(VP3D_RT_SHADOWS) && !defined(VP3D_ATTRIB_SEMANTICS)
        if (R.rtRefl > 0 && !capture) {   // ray traced reflection without probes (HQ): the parts around, then the HDRI
            float4 rr = gRefl.Load(int3(TracedPixel(i.pos.xy, i.wpos, N), 0));
            if (rr.a > 0.5) ambGlossy = kSpec * rr.rgb;
        }
#endif
        color += ambDiffuse + ambGlossy;
    }
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
    DRAW_PS(i);
#ifdef VP3D_DDGI_PROBES
    // Debug (G.flags bit 1): the probes drawn as small spheres shaded with their irradiance (red = disabled), as GIShade.
    if (R.giMode > 0 && (G.flags & 1u)) {
        float3 eyeP = R.orthoHalfH > 0 ? i.wpos + R.camFwd * -1000.0 : R.camPos;   // (orthographic: from far along the view)
        float3 dir = normalize(i.wpos - eyeP);
        float best = length(i.wpos - eyeP);
        float r = min(G.spacing.x, min(G.spacing.y, G.spacing.z)) * 0.1;
        uint hitProbe = 0xFFFFFFFFu;
        for (uint p = 0; p < G.counts.w; ++p) {
            float3 oc = eyeP - ProbePos(ProbeCoord(p));
            float b = dot(oc, dir), c = dot(oc, oc) - r * r;
            float disc = b * b - c;
            if (disc <= 0) continue;
            float t = -b - sqrt(disc);
            if (t > 0 && t < best) { best = t; hitProbe = p; }
        }
        if (hitProbe != 0xFFFFFFFFu) {
            float3 n = normalize(eyeP + dir * best - ProbePos(ProbeCoord(hitProbe)));
            float3 c = gProbe[int2(hitProbe, 0)].x > 0.5 ? SampleTile(gIrr, hitProbe, DDGI_IRR_N, n).rgb : float3(0.6, 0.02, 0.02);
            return float4(c, 1);
        }
    }
#endif
#ifdef VP3D_SCREEN_GI
    // Split RC inspection views (view 3): the screen GI texture holds the debug colour itself
    if (R.giMode > 0 && G.view == 3) return float4(gScreenGI.Load(int3(i.pos.xy, 0)).rgb, 1);
#endif
#if defined(VP3D_RT_SHADOWS) && !defined(VP3D_ATTRIB_SEMANTICS)
    if (R.giMode > 0 && G.view == 4) return float4(gSunShadow.Load(int3(i.pos.xy, 0)).xxx, 1);   // inspection: shadow mask
#endif
    return Shade(i, false, 0.0015, false);
}
float4 PSGhost(VSOut i) : SV_Target {
    DRAW_PS(i);
    return Shade(i, true, 0.0015, false);
}
// id in w; the world normal in rgb lets the selection glow find the creases of the selected object (Tonemap.hlsl)
float4 PSId(VSOut i) : SV_Target {
    DRAW_PS(i);
    return float4(normalize(i.nrm) * 0.5 + 0.5, (float)D.id);
}
// selection mask (coverage of the selected objects, see Renderer::recordSelectionMask)
float4 PSSelect(VSOut i) : SV_Target { return 1.0; }
// Editor wireframe display: flat lines, grey level in D.alpha (blended, no depth write).
// Flag 8 (Wireframe Override): faint light lines with a facing ratio gradient, clear where the surface faces the
// camera and fading towards the silhouettes (where the lines crowd).
float4 PSWire(VSOut i) : SV_Target {
    DRAW_PS(i);
    if (D.flags & 8u) {
        float facing = abs(dot(normalize(i.nrm), ToViewer(i.wpos)));
        return float4(D.alpha.xxx, 0.02 + 0.24 * facing * facing);
    }
    return float4(D.alpha.xxx, 0.75);
}

// ---------------------------------------------------------------- DDGI probe capture
// One 90-degree cube face per draw: the view-projection of the face and the probe position (32-bit root constants b3).
// The face is lit like the main pass; alpha = distance to the probe (negative for back faces, as the ray tracer).

VSOut VSProbe(VSIn i) {
    DRAW_VS(i.iid);
    VSOut o;
#ifndef VP3D_ATTRIB_SEMANTICS
    o.inst = gDrawBase + i.iid;
#endif
    o.wpos = ToWorld(i.pos);
    float3 n = float3(dot(D.row0.xyz, i.nrm), dot(D.row1.xyz, i.nrm), dot(D.row2.xyz, i.nrm));
    o.nrm = normalize(n);
    o.uv = i.uv;
    o.opos = i.pos;
    o.onrm = i.nrm;
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
    DRAW_PS(i);
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
