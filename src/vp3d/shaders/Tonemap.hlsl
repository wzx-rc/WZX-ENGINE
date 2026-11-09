// Exposure, view transform (ACES / AgX / AgX Punchy / Standard), vignette, sRGB and selection outline.
#include "Common.hlsli"

// auxSlot (1 + slot, 0 = none): path tracing, the editor overlay (grid, wireframe) drawn over the image; raster, the
// previous frame of MSAA x16 over two frames, averaged with this one (Renderer::render)
struct TonemapConstants { uint denoisedSlot; uint raster; uint tonemapper; uint auxSlot; };
ConstantBuffer<TonemapConstants> T : register(b1);

#include "ToneCurves.hlsli"

// coverage of the selection per pixel: the selected objects drawn with the MSAA samples of the viewport and resolved
// (Renderer::recordSelectionMask), valid when F.selCovValid
Texture2D<float> gSelCov : register(t8);
float SelCov(int2 q) { return gSelCov.Load(int3(clamp(q, int2(0, 0), int2(F.resolution) - 1), 0)); }

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
        if (T.auxSlot != 0) color = 0.5 * (color + gTex[T.auxSlot - 1][p].rgb);   // the other sub-pixel offset
    } else if (T.denoisedSlot != 0xFFFFFFFFu && (T.denoisedSlot & 0x80000000u)) {
        color = gTex[T.denoisedSlot & 0xFu][p].rgb;   // Open Image Denoise: the radiance itself (OidnPack.hlsl)
    } else if (T.denoisedSlot != 0xFFFFFFFFu) {
        float3 den = gTex[T.denoisedSlot][p].rgb * max(alb.rgb, 0.01);
        float keepRaw = saturate((spp - 64.0) / 768.0);
        color = lerp(den, raw, keepRaw);
    }
    // editor overlay (grid, wireframe) drawn by the raster pass over the path traced image, premultiplied alpha
    if (!T.raster && T.auxSlot != 0) {
        float4 ov = gTex[T.auxSlot - 1][p];
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
    if (!T.raster && (F.flags & FRAME_FLAG_SPP_MAP)) {   // samples per pixel (adaptive sampling): blue few ... red as many as the frame
        float t = saturate(acc.w / max((float)min(F.sampleIndex, F.maxSamplesPx), 1.0));
        float3 heat = saturate(float3(1.5 - abs(4.0 * t - 3.0), 1.5 - abs(4.0 * t - 2.0), 1.5 - abs(4.0 * t - 1.0)));
        c = lerp(heat, c, 0.25);   // a little of the image, to recognise the scene
    }

    // Selection (selStyle 1): a soft blue glow hugging the silhouette on both sides (a neon edge, no hard line), its
    // light flowing slowly along the contour (sawtooth around the selection centre, selAge = seconds since the
    // selection changed), antialiased like the image (coverage of the selection, MSAA). The creases, the vertex glints
    // and the floor reflection are drawn by the editor (SelectionFx.cpp). selStyle 0: a thin yellow line.
    uint sel = F.selectedInstance;
    if ((sel != 0 || F.selMoreCount != 0) && F.selStyle == 1) {
        const float ps = max(F.selPixelScale, 1.0);   // render pixels per screen pixel (viewport supersampling)
        const float3 glow = float3(0.30, 0.62, 1.0);
        float2 cv = float2(p) - float2(F.selCenterX, F.selCenterY);
        float k = 0.75 + 0.25 * frac(atan2(cv.y, cv.x) * (4.0 / 6.2831853) - F.selAge * 0.6);
        float outer = 0.0, inner = 0.0, cover = 0.0;   // glow outside / inside the silhouette, coverage of this pixel
        if (F.selCovValid != 0) {
            // Smooth: the coverage of the selection per pixel (resolved MSAA mask, as smooth as the image edges).
            // Distances (render pixels) to the covered and to the uncovered side, estimated from the coverage of the
            // neighbours: a neighbour r pixels away covered at c has its edge about r + 0.5 - c away.
            cover = SelCov(p);
            float sOut = cover > 0.001 ? 0.5 - cover : 100.0, sIn = cover < 0.999 ? cover - 0.5 : 100.0;
            // every pixel of the disc of radius 3 screen pixels, at its true distance (no rounded directions: no steps)
            const int R = (int)ceil(3.0 * ps);
            const float inR = ps + 0.5;
            [loop] for (int dy = -R; dy <= R; ++dy)
                [loop] for (int dx = -R; dx <= R; ++dx) {
                    const float len = length(float2(dx, dy));
                    if ((dx == 0 && dy == 0) || len > R + 0.5) continue;
                    const float cq = SelCov(p + int2(dx, dy));
                    if (cq > 0.001) sOut = min(sOut, len + 0.5 - cq);
                    if (len <= inR && cq < 0.999) sIn = min(sIn, len + 0.5 - (1.0 - cq));
                }
            sOut /= ps;
            sIn /= ps;
            outer = (1.0 - cover) * (sOut < 3.0 ? exp(-max(sOut - 0.5, 0.0) * 1.1) : 0.0);
            inner = cover * saturate(1.5 - sIn);
        } else {
            // without the mask (no raster pass this frame): the ids of the pixels, hard edges
            bool inside = IsSelected((uint)(alb.w + 0.5));
            float dmin = 100.0;
            const int maxR = (int)ceil((inside ? 1.0 : 3.0) * ps);
            [loop] for (int r = 1; r <= maxR && dmin > 99.0; ++r)
                [loop] for (int n = 0; n < 12; ++n) {
                    float an = (n + (r & 1) * 0.5) * 0.5235988;
                    int2 q = clamp(p + int2(round(float2(cos(an), sin(an)) * r)), int2(0, 0), int2(F.resolution) - 1);
                    if (IsSelected((uint)(gTex[SLOT_ALBEDO][q].w + 0.5)) != inside) { dmin = (float)r; break; }
                }
            float ds = dmin < 99.0 ? dmin / ps : 100.0;   // screen pixels
            cover = inside ? 1.0 : 0.0;
            if (!inside && ds < 99.0) outer = exp(-(ds - 1.0) * 1.1);
            if (inside && ds < 99.0) inner = 1.0;
        }
        c += glow * (outer * 0.55 + inner * 0.32) * k;   // bright next to the edge, gone after ~3 px; the edge of the object
    } else if (sel != 0 || F.selMoreCount != 0) {
        // selStyle 0: a thin yellow line on the silhouette (2: thin white line at 45 %, BrickIt) (about 1.3 screen pixels), antialiased by the coverage of the
        // selection (MSAA) when there is one, a faint tint inside
        const float ps = max(F.selPixelScale, 1.0);
        const float3 yellow = float3(1.0, 0.82, 0.1);
        float edge = 0.0, cover = 0.0;
        if (F.selCovValid != 0) {
            cover = SelCov(p);
            // signed distance to the edge (render pixels, > 0 outside), from the coverage of the pixel and its neighbours
            float sOut = cover > 0.001 ? 0.5 - cover : 100.0, sIn = cover < 0.999 ? cover - 0.5 : 100.0;
            const int R = (int)ceil(2.0 * ps);
            [loop] for (int dy = -R; dy <= R; ++dy)
                [loop] for (int dx = -R; dx <= R; ++dx) {
                    const float len = length(float2(dx, dy));
                    if ((dx == 0 && dy == 0) || len > R + 0.5) continue;
                    const float cq = SelCov(p + int2(dx, dy));
                    if (cq > 0.001) sOut = min(sOut, len + 0.5 - cq);
                    if (cq < 0.999) sIn = min(sIn, len + 0.5 - (1.0 - cq));
                }
            const float d = cover <= 0.001 ? sOut : (cover >= 0.999 ? sIn : abs(0.5 - cover));
            edge = saturate(1.0 - d / (1.3 * ps));
        } else {
            // without the mask: the ids, a one pixel line outside the silhouette
            bool inside = IsSelected((uint)(alb.w + 0.5));
            cover = inside ? 1.0 : 0.0;
            if (!inside)
                for (int dy = -1; dy <= 1 && edge == 0.0; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        int2 q = clamp(p + int2(dx, dy), int2(0, 0), int2(F.resolution) - 1);
                        if (IsSelected((uint)(gTex[SLOT_ALBEDO][q].w + 0.5))) { edge = 1.0; break; }
                    }
        }
        if (F.selStyle == 2) {   // BrickIt: a faint white fill (its edges are drawn by the edge outline, as the web build)
            c = lerp(c, 1.0.xxx, saturate(F.selFill) * cover);   // (Réglages › Voile de sélection)
        } else {
            c = lerp(c, float3(1.0, 0.9, 0.4), 0.08 * cover);
            c = lerp(c, yellow, 0.95 * edge);
        }
    }
    gTex[SLOT_OUTPUT][p] = float4(c, 1.0);
}

