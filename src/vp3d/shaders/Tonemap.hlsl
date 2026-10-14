// Exposure, view transform (ACES / AgX / AgX Punchy / Standard), vignette, sRGB and selection outline.
#include "Common.hlsli"

struct TonemapConstants { uint denoisedSlot; uint raster; uint tonemapper; uint pad2; };
ConstantBuffer<TonemapConstants> T : register(b1);

#include "ToneCurves.hlsli"

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= F.resolution.x || id.y >= F.resolution.y) return;
    int2 p = int2(id.xy);
    float4 acc = gTex[SLOT_ACCUM][p];
    float spp = max(acc.w, 1.0);
    float3 raw = acc.rgb / spp;
    float4 alb = gTex[SLOT_ALBEDO][p];
    float3 color = raw;
    if (T.raster) {
        color = gTex[T.denoisedSlot][p].rgb;
    } else if (T.denoisedSlot != 0xFFFFFFFFu) {
        float3 den = gTex[T.denoisedSlot][p].rgb * max(alb.rgb, 0.01);
        float keepRaw = saturate((spp - 64.0) / 768.0);
        color = lerp(den, raw, keepRaw);
    }
    color *= F.exposure;

    float2 uv = (float2(p) + 0.5) / float2(F.resolution);
    float2 vc = uv - 0.5;
    vc.x *= F.aspect;
    color *= 1.0 - F.vignette * saturate(dot(vc, vc) * 1.4);

    float3 mapped;
    if (T.tonemapper == 1) mapped = AgX(color, 0);
    else if (T.tonemapper == 2) mapped = AgX(color, 1);
    else if (T.tonemapper == 3) mapped = saturate(color);
    else mapped = ACESFitted(color);
    float3 c = ToSRGB(mapped);

    // Selection outline (yellow, 2px).
    uint sel = F.selectedInstance;
    if (sel != 0 || F.selMoreCount != 0) {
        uint center = (uint)(alb.w + 0.5);
        bool centerSel = IsSelected(center);
        int hits = 0;
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx) {
                int2 q = clamp(p + int2(dx, dy), int2(0, 0), int2(F.resolution) - 1);
                if (IsSelected((uint)(gTex[SLOT_ALBEDO][q].w + 0.5))) hits++;
            }
        if (!centerSel && hits > 0) c = lerp(c, float3(1.0, 0.82, 0.1), 0.95);
        else if (centerSel && hits < 25) c = lerp(c, float3(1.0, 0.86, 0.2), 0.6);
        else if (centerSel) c = lerp(c, float3(1.0, 0.9, 0.4), 0.08);
    }
    gTex[SLOT_OUTPUT][p] = float4(c, 1.0);
}

