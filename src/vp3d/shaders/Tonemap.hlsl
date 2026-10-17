// Exposure, view transform (ACES / AgX / AgX Punchy / Standard), vignette, sRGB and selection outline.
#include "Common.hlsli"

struct TonemapConstants { uint denoisedSlot; uint raster; uint tonemapper; uint overlaySlot; };   // overlaySlot: 1 + slot of the editor overlay (grid, wireframe) drawn over the path traced image, 0 = none
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
    // editor overlay (grid, wireframe) drawn by the raster pass over the path traced image, premultiplied alpha
    if (T.overlaySlot != 0) {
        float4 ov = gTex[T.overlaySlot - 1][p];
        color = color * (1.0 - ov.a) + ov.rgb;
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

    // Selection (selStyle 1): a 2 px blue line outside the silhouette whose brightness flows linearly along the
    // contour (a sawtooth turning around the centre of the selection, selAge = seconds since the selection changed).
    // The white glow on the outline vertices and the reflection on the floor are drawn by the editor (SelectionFx.cpp).
    // selStyle 0: the yellow outline (2px).
    uint sel = F.selectedInstance;
    if ((sel != 0 || F.selMoreCount != 0) && F.selStyle == 1) {
        if (!IsSelected((uint)(alb.w + 0.5))) {
            bool near = false;
            [loop] for (int r = 1; r <= 2 && !near; ++r)
                [loop] for (int k = 0; k < 12 && !near; ++k) {
                    float an = (k + (r & 1) * 0.5) * 0.5235988;
                    int2 q = clamp(p + int2(round(float2(cos(an), sin(an)) * r)), int2(0, 0), int2(F.resolution) - 1);
                    near = IsSelected((uint)(gTex[SLOT_ALBEDO][q].w + 0.5));
                }
            if (near) {
                float2 cv = float2(p) - float2(F.selCenterX, F.selCenterY);
                float flow = frac(atan2(cv.y, cv.x) * (4.0 / 6.2831853) - F.selAge * 0.6);   // 4 ramps around the contour
                c = lerp(c, float3(0.22, 0.56, 1.0), 0.45 + 0.5 * flow);
                c += float3(0.10, 0.22, 0.40) * flow;
            }
        }
    } else if (sel != 0 || F.selMoreCount != 0) {
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

