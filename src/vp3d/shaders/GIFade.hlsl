// GI mode, raster while moving (RenderSettings::giRasterWhileMoving): mode 0 keeps the last image of the raster view
// of the probes (output -> denoise B), mode 1 fades from it to the ray traced image once still (output = lerp).
#include "Common.hlsli"

struct FadeConstants { uint mode; float t; uint pad0; uint pad1; };
ConstantBuffer<FadeConstants> C : register(b1);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= F.resolution.x || id.y >= F.resolution.y) return;
    int2 p = int2(id.xy);
    if (C.mode == 0) gTex[4][p] = gTex[SLOT_OUTPUT][p];
    else if (C.mode == 1) gTex[SLOT_OUTPUT][p] = lerp(gTex[4][p], gTex[SLOT_OUTPUT][p], saturate(C.t));
    else {   // mode 2: still image supersampling (RenderSettings::stillAA): the HDR image (slot 3) averaged with the
             // jittered frames before it (slot 0), t = 1 / (n + 1); the average goes back to slot 3 for the tone mapper
        const float4 a = C.t >= 0.999 ? gTex[3][p] : lerp(gTex[0][p], gTex[3][p], saturate(C.t));
        gTex[0][p] = a;
        gTex[3][p] = a;
    }
}
