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

    // Selection: thin blue glow with a light running along the contour (selStyle 1, animated by selAge = seconds since
    // the selection changed) or the yellow outline (2px).
    uint sel = F.selectedInstance;
    if ((sel != 0 || F.selMoreCount != 0) && F.selStyle == 1) {
        bool centerSel = IsSelected((uint)(alb.w + 0.5));
        const float3 glow = float3(0.24, 0.56, 1.0), core = float3(0.80, 0.93, 1.0);
        float age = F.selAge;
        float grow = smoothstep(0.0, 0.5, age);           // the glow opens when the object is selected
        float flash = exp(-age * 3.0);                    // and flashes once
        // light running along the contour: two soft spots turning around the centre of the selection
        float2 cv = float2(p) - float2(F.selCenterX, F.selCenterY);
        float run = 0.5 + 0.5 * cos(atan2(cv.y, cv.x) * 2.0 - age * 1.8);
        run = 0.3 + 0.7 * run * run;
        if (!centerSel) {
            // distance (pixels) to the nearest selected pixel, on rings of growing radius
            const float radii[4] = {1.0, 2.0, 3.0, 4.0};
            float dmin = 100.0;
            [loop] for (int r = 0; r < 4 && dmin > 99.0; ++r) {
                [loop] for (int k = 0; k < 12; ++k) {
                    float an = (k + (r & 1) * 0.5) * 0.5235988;
                    int2 q = clamp(p + int2(round(float2(cos(an), sin(an)) * radii[r])), int2(0, 0), int2(F.resolution) - 1);
                    if (IsSelected((uint)(gTex[SLOT_ALBEDO][q].w + 0.5))) { dmin = radii[r]; break; }
                }
            }
            if (dmin < 99.0) {
                // glow: linear falloff over about 3 pixels, brighter where the running light passes
                float lin = saturate(1.0 - (dmin - 1.0) / (1.0 + 2.0 * grow));
                c += glow * lin * (0.22 + 0.38 * run) * (1.0 + 0.8 * flash);
                // thin core line right on the silhouette, lit by the running light
                if (dmin <= 1.0) c = lerp(c, core, 0.3 + 0.5 * run);
            }
        } else {
            // inside: the creases of the object (normal breaks, written by the raster id pass) glow thinly with the
            // running light, a faint tint elsewhere
            float crease = 0.0;
            if (T.raster) {
                float3 n0 = alb.rgb * 2.0 - 1.0;
                [unroll] for (int k = 0; k < 8; ++k) {
                    int2 o = k == 0 ? int2(1, 0) : k == 1 ? int2(-1, 0) : k == 2 ? int2(0, 1) : k == 3 ? int2(0, -1)
                           : k == 4 ? int2(2, 0) : k == 5 ? int2(-2, 0) : k == 6 ? int2(0, 2) : int2(0, -2);
                    float4 a = gTex[SLOT_ALBEDO][clamp(p + o, int2(0, 0), int2(F.resolution) - 1)];
                    if (abs(a.w - alb.w) > 0.5) continue;
                    float e = 1.0 - saturate((dot(n0, a.rgb * 2.0 - 1.0) - 0.55) / 0.3);
                    crease = max(crease, k < 4 ? e : e * 0.3);
                }
            }
            c += glow * crease * (0.2 + 0.4 * run) * (1.0 + flash);
            c = lerp(c, core, crease * 0.2 * run);
            c = lerp(c, glow, 0.03 + 0.08 * flash);
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

