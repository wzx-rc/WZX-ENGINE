// Guide images of DLSS Ray Reconstruction (moving view of the progressive mode, Renderer::render, Dlss.cpp), at the
// input resolution (F.resolution of this pass), from the single jittered sample PathTrace.hlsl just wrote:
//  u23 diffuse albedo   base colour x (1 - metallic)   (the sky: 1, its colour passes through)
//  u24 specular albedo  split-sum approximation of the specular reflectance (Karis' fit)
//  u25 normal + roughness   world normal (xyz), roughness (w): DLSS's packed roughness mode
//  u26 depth            linear view depth (along the camera axis)
//  u27 motion vectors   pixels from this frame to where the point was in the previous one (camera motion; the sky
//                       as a point at infinity, so only the camera rotation moves it)
// Roughness and metallic come from the material of the instance seen (not its textures); the albedo of the path
// tracer guides already holds the textured base colour.
#include "Common.hlsli"

RWTexture2D<float4> gDiffAlb : register(u23);
RWTexture2D<float4> gSpecAlb : register(u24);
RWTexture2D<float4> gNormRough : register(u25);
RWTexture2D<float> gDepth : register(u26);
RWTexture2D<float2> gMotion : register(u27);

float3 EnvBRDFApprox(float3 f0, float rough, float nv) {
    const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    float4 r = rough * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * nv)) * r.x + r.y;
    float2 ab = float2(-1.04, 1.04) * a004 + r.zw;
    return f0 * ab.x + ab.y;
}

// Where world point P was seen by the previous camera, in pixels (continuous); `atInfinity`: P is a direction
bool PrevPixel(float3 P, bool atInfinity, out float2 pix) {
    float3 v = atInfinity ? P : P - F.prevCamPos;
    float z = dot(v, F.prevCamFwd);
    pix = 0;
    if (F.prevOrthoHalfH > 0) {
        if (atInfinity) return false;
        float x = dot(v, F.prevCamRight) / (F.prevOrthoHalfH * F.prevAspect);
        float y = dot(v, F.prevCamUp) / F.prevOrthoHalfH;
        pix = (float2(x, -y) * 0.5 + 0.5) * float2(F.resolution);
        return true;
    }
    if (z <= 1e-6) return false;
    float x = dot(v, F.prevCamRight) / (z * F.prevAspect * F.prevTanHalfFovY);
    float y = dot(v, F.prevCamUp) / (z * F.prevTanHalfFovY);
    pix = (float2(x, -y) * 0.5 + 0.5) * float2(F.resolution);
    return true;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= F.resolution.x || id.y >= F.resolution.y) return;
    const int2 p = int2(id.xy);
    const float4 alb = gTex[SLOT_ALBEDO][p];
    const float4 nd = gTex[SLOT_NORMAL][p];
    const uint inst = (uint)(alb.w + 0.5);   // instance + 1, 0 = sky

    // the ray of the pixel centre (the motion vectors are those of the unjittered pixel)
    const float2 centre = float2(p) + 0.5;
    const float2 uv = centre / float2(F.resolution);
    float3 org, dir;
    CameraRay(float2(2.0 * uv.x - 1.0, 1.0 - 2.0 * uv.y), org, dir);
    const bool sky = inst == 0 || nd.w >= 1e5;

    float rough = 1.0, metal = 0.0;
    if (!sky) {
        GpuMaterial m = gMats[gInst[inst - 1].materialId];
        rough = saturate(m.roughness);
        metal = saturate(m.metallic);
    }
    float3 n = normalize(nd.xyz + 1e-6);
    float3 base = saturate(alb.rgb);
    float nv = saturate(dot(n, -dir));
    gDiffAlb[p] = float4(sky ? 1.0.xxx : base * (1.0 - metal), 1.0);
    gSpecAlb[p] = float4(sky ? 0.0.xxx : EnvBRDFApprox(lerp(0.04.xxx, base, metal), rough, nv), 1.0);
    gNormRough[p] = float4(n, rough);

    float depth = sky ? 1e5 : nd.w * max(dot(dir, F.camFwd), 1e-4);
    if (F.orthoHalfH > 0 && !sky) depth = max(nd.w - F.orthoBack, 1e-4);
    gDepth[p] = depth;

    float2 prev;
    const bool ok = sky ? PrevPixel(dir, true, prev) : PrevPixel(org + dir * nd.w, false, prev);
    gMotion[p] = ok ? prev - centre : 0.0.xx;
}
