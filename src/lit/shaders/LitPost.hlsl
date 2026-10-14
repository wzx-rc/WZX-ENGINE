// Lit post pass: exposure, vignette, view transform and sRGB encoding of the resolved HDR image.
// Same operations, in the same order, as the raster branch of src/vp3d/shaders/Tonemap.hlsl (without the
// editor's selection outline).
#include "ToneCurves.hlsli"

cbuffer PostConstants {
    float gExposure;
    float gVignette;
    float gAspect;
    uint gTonemapper;   // 0 ACES, 1 AgX, 2 AgX Punchy, 3 Standard
    float2 gInvSize;    // 1 / image size
    float2 gPad;
};

Texture2D<float4> gHdr;

struct PostOut {
    float4 pos : SV_Position;
};

PostOut VSPost(uint id : SV_VertexID) {
    PostOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}

float4 PSPost(PostOut i) : SV_Target {
    int2 p = int2(i.pos.xy);
    float3 color = gHdr.Load(int3(p, 0)).rgb * gExposure;

    float2 uv = (float2(p) + 0.5) * gInvSize;
    float2 vc = uv - 0.5;
    vc.x *= gAspect;
    color *= 1.0 - gVignette * saturate(dot(vc, vc) * 1.4);

    float3 mapped;
    if (gTonemapper == 1) mapped = AgX(color, 0);
    else if (gTonemapper == 2) mapped = AgX(color, 1);
    else if (gTonemapper == 3) mapped = saturate(color);
    else mapped = ACESFitted(color);
    return float4(ToSRGB(mapped), 1.0);
}
