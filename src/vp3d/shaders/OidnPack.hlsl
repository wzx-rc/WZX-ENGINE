// Open Image Denoise of the progressive path tracer (Renderer::render, Oidn.cpp): three small passes around OIDN.
//  mode 0  pack: the accumulated colour, albedo and normal into the three float3 planes OIDN reads (u15). The
//          albedo and normal are accumulated by PathTrace.hlsl like the colour (jittered samples): anti-aliased
//          guides, as OIDN wants them.
//  mode 1  snapshot: the image shown by the last frame (radiance before exposure) into the fade start (u22):
//          src = 0x80000000 | slot: OIDN's image in that slot; a slot: the a-trous image (demodulated, as the
//          tonemapper shows it); 0xFFFFFFFF: the raw accumulation.
//  mode 2  blend: lerp(fade start, OIDN's output (u16), t) into the slot `src`, which the tonemapper shows.
#include "Common.hlsli"

struct OidnConstants { uint mode; uint src; float t; uint pad; };
ConstantBuffer<OidnConstants> P : register(b1);

RWByteAddressBuffer gOidnIn : register(u15);    // colour | albedo | normal planes, float3 per pixel
RWByteAddressBuffer gOidnOut : register(u16);   // denoised colour, float3 per pixel
RWByteAddressBuffer gFadeFrom : register(u22);  // fade start, float4 per pixel

float3 Shown(int2 p) {
    float4 acc = gTex[SLOT_ACCUM][p];
    float spp = max(acc.w, 1.0);
    float3 raw = acc.rgb / spp;
    if (P.src == 0xFFFFFFFFu) return raw;
    if (P.src & 0x80000000u) return gTex[P.src & 0xFu][p].rgb;
    float3 den = gTex[P.src][p].rgb * max(gTex[SLOT_ALBEDO][p].rgb, 0.01);   // as Tonemap.hlsl
    return lerp(den, raw, saturate((spp - 64.0) / 768.0));
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= F.resolution.x || id.y >= F.resolution.y) return;
    const int2 p = int2(id.xy);
    const uint pix = id.y * F.resolution.x + id.x;
    const uint plane = F.resolution.x * F.resolution.y * 12;
    if (P.mode == 0) {
        float4 acc = gTex[SLOT_ACCUM][p];
        float3 c = acc.rgb / max(acc.w, 1.0);
        c = min(max(c, 0.0), 65000.0);
        float3 a = saturate(gTex[SLOT_ALBEDO][p].rgb);
        float3 n = clamp(gTex[SLOT_NORMAL][p].xyz, -1.0, 1.0);
        gOidnIn.Store3(pix * 12, asuint(c));
        gOidnIn.Store3(plane + pix * 12, asuint(a));
        gOidnIn.Store3(plane * 2 + pix * 12, asuint(n));
    } else if (P.mode == 1) {
        gFadeFrom.Store4(pix * 16, asuint(float4(Shown(p), 1.0)));
    } else {
        float3 from = asfloat(gFadeFrom.Load3(pix * 16));
        float3 to = asfloat(gOidnOut.Load3(pix * 12));
        float3 c = lerp(from, max(to, 0.0), saturate(P.t));
        if (any(isnan(c)) || any(isinf(c))) c = to;
        gTex[P.src][p] = float4(min(c, 65000.0), 1.0);
    }
}
