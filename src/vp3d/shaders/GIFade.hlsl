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
    else gTex[SLOT_OUTPUT][p] = lerp(gTex[4][p], gTex[SLOT_OUTPUT][p], saturate(C.t));
}
