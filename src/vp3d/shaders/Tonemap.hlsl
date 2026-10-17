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

    // Selection (selStyle 1): a soft blue glow hugging the silhouette on both sides (a neon edge, no hard line), its
    // light flowing slowly along the contour (sawtooth around the selection centre, selAge = seconds since the
    // selection changed), and the creases of the object lit thinly (raster: normals of the id pass). The vertex glints
    // and the floor reflection are drawn by the editor (SelectionFx.cpp). selStyle 0: the yellow outline (2px).
    uint sel = F.selectedInstance;
    if ((sel != 0 || F.selMoreCount != 0) && F.selStyle == 1) {
        const float ps = max(F.selPixelScale, 1.0);   // render pixels per screen pixel (viewport supersampling)
        const float3 glow = float3(0.30, 0.62, 1.0);
        float2 cv = float2(p) - float2(F.selCenterX, F.selCenterY);
        float k = 0.75 + 0.25 * frac(atan2(cv.y, cv.x) * (4.0 / 6.2831853) - F.selAge * 0.6);
        bool inside = IsSelected((uint)(alb.w + 0.5));
        // distance (render pixels) to the other side of the silhouette: up to 3 screen pixels outside, 1 inside
        float dmin = 100.0;
        const int maxR = (int)ceil((inside ? 1.0 : 3.0) * ps);
        [loop] for (int r = 1; r <= maxR && dmin > 99.0; ++r)
            [loop] for (int n = 0; n < 12; ++n) {
                float an = (n + (r & 1) * 0.5) * 0.5235988;
                int2 q = clamp(p + int2(round(float2(cos(an), sin(an)) * r)), int2(0, 0), int2(F.resolution) - 1);
                if (IsSelected((uint)(gTex[SLOT_ALBEDO][q].w + 0.5)) != inside) { dmin = (float)r; break; }
            }
        float ds = dmin < 99.0 ? dmin / ps : 100.0;   // screen pixels
        if (!inside) {
            if (ds < 99.0) c += glow * exp(-(ds - 1.0) * 1.1) * 0.55 * k;   // bright next to the edge, gone after ~3 px
        } else {
            if (ds < 99.0) c += glow * 0.32 * k;   // the edge pixels of the object glow too
            if (T.raster) {   // creases (normal breaks inside the object)
                float3 n0 = alb.rgb * 2.0 - 1.0;
                float crease = 0.0;
                const int o = (int)round(ps);
                [unroll] for (int m = 0; m < 4; ++m) {
                    int2 d = m == 0 ? int2(o, 0) : m == 1 ? int2(-o, 0) : m == 2 ? int2(0, o) : int2(0, -o);
                    float4 a = gTex[SLOT_ALBEDO][clamp(p + d, int2(0, 0), int2(F.resolution) - 1)];
                    if (abs(a.w - alb.w) > 0.5) continue;
                    crease = max(crease, 1.0 - saturate((dot(n0, a.rgb * 2.0 - 1.0) - 0.55) / 0.3));
                }
                c += glow * crease * 0.3 * k;
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

