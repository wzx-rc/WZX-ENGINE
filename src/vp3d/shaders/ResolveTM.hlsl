// MSAA resolve in tonemapped space (AAA anti-aliasing of the raster GI modes): each sample is weighted by
// 1 / (1 + its brightest channel) before the average, and the average mapped back (Karis 2014). A plain resolve averages
// HDR values, so an edge between a bright and a dark surface stays as bright as the bright side's sample count allows
// and the tonemapper turns it into a hard stair; with the weights the edge fades evenly once tonemapped.
Texture2DMS<float4> gMsaa : register(t7);
RWTexture2D<float4> gHdr : register(u3);   // the slot the tonemapper reads (TEX_DENOISE_A)

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint w, h, n;
    gMsaa.GetDimensions(w, h, n);
    if (id.x >= w || id.y >= h) return;
    float4 sum = 0;
    float wsum = 0;
    for (uint s = 0; s < n; ++s) {
        float4 c = gMsaa.Load(int2(id.xy), s);
        float k = 1.0 / (1.0 + max(c.r, max(c.g, c.b)));
        sum += c * k;
        wsum += k;
    }
    float4 r = sum / max(wsum, 1e-6);
    gHdr[id.xy] = r;
}
