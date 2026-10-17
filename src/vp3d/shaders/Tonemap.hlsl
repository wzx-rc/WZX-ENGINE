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

    // Selection: blue glow (selStyle 1, animated by selAge) or yellow outline (2px).
    uint sel = F.selectedInstance;
    if ((sel != 0 || F.selMoreCount != 0) && F.selStyle == 1) {
        bool centerSel = IsSelected((uint)(alb.w + 0.5));
        const float3 glow = float3(0.32, 0.64, 1.0);
        float age = F.selAge;
        float grow = smoothstep(0.0, 0.3, age);           // the halo opens when the object is selected
        float flash = exp(-age * 4.0);                    // a short bright flash
        float pulse = 0.92 + 0.08 * sin(age * 2.6);       // then breathes slowly
        if (!centerSel) {
            // distance (pixels) to the nearest selected pixel, on rings of growing radius
            const float radii[6] = {1.0, 2.0, 3.0, 4.5, 6.5, 9.0};
            float dmin = 100.0;
            [loop] for (int r = 0; r < 6 && dmin > 99.0; ++r) {
                [loop] for (int k = 0; k < 12; ++k) {
                    float an = (k + (r & 1) * 0.5) * 0.5235988;
                    int2 q = clamp(p + int2(round(float2(cos(an), sin(an)) * radii[r])), int2(0, 0), int2(F.resolution) - 1);
                    if (IsSelected((uint)(gTex[SLOT_ALBEDO][q].w + 0.5))) { dmin = radii[r]; break; }
                }
            }
            if (dmin < 99.0) {
                float edge = dmin <= 1.0 ? 1.0 : 0.0;
                float halo = exp(-(dmin - 1.0) / (0.8 + 3.4 * grow)) * (0.7 + 0.8 * flash) * pulse;
                c = lerp(c, glow, saturate(max(edge * 0.9, halo)));
                c += glow * edge * (0.25 + 0.5 * flash);
            }
        } else {
            // inside: the silhouette pixels light up, a faint blue tint on the rest (stronger during the flash)
            bool rim = false;
            [unroll] for (int k = 0; k < 4; ++k) {
                int2 o = k == 0 ? int2(1, 0) : k == 1 ? int2(-1, 0) : k == 2 ? int2(0, 1) : int2(0, -1);
                int2 q = clamp(p + o, int2(0, 0), int2(F.resolution) - 1);
                if (!IsSelected((uint)(gTex[SLOT_ALBEDO][q].w + 0.5))) rim = true;
            }
            if (rim) c = lerp(c, float3(0.78, 0.9, 1.0), 0.85);
            else c = lerp(c, glow, 0.07 + 0.22 * flash);
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

