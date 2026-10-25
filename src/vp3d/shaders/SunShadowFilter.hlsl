// Ray traced sun shadow, step 2 (one thread per pixel): 5 x 5 bilateral filter of the raw visibility, guided by the
// depth and the normal of each pixel (SunShadowTrace.hlsl), so the few rays per pixel average with the neighbours on
// the same surface into a smooth penumbra, without blurring across object edges. No temporal accumulation.
RWTexture2D<float4> gShadowRaw : register(u18);
RWTexture2D<float> gShadowMask : register(u19);

float2 SignNZ2(float2 v) { return float2(v.x >= 0 ? 1.0 : -1.0, v.y >= 0 ? 1.0 : -1.0); }

float3 OctDec(float2 o) {
    float3 n = float3(o, 1.0 - abs(o.x) - abs(o.y));
    if (n.z < 0) n.xy = (1.0 - abs(n.yx)) * SignNZ2(n.xy);
    return normalize(n);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint w, h;
    gShadowRaw.GetDimensions(w, h);
    if (id.x >= w || id.y >= h) return;
    float4 c = gShadowRaw[id.xy];
    if (c.y <= 0) { gShadowMask[id.xy] = 1; return; }   // sky
    float3 n = OctDec(c.zw);
    float sum = 0, wsum = 0;
    for (int y = -2; y <= 2; ++y)
        for (int x = -2; x <= 2; ++x) {
            int2 q = clamp(int2(id.xy) + int2(x, y), int2(0, 0), int2(w, h) - 1);
            float4 s = gShadowRaw[q];
            if (s.y <= 0) continue;
            float wd = exp(-abs(s.y - c.y) / (0.02 * c.y));                // same surface: depth within 2 %
            float wn = pow(saturate(dot(n, OctDec(s.zw))), 16.0);           // and the same orientation
            float ws = exp(-(x * x + y * y) / 4.5);                         // gaussian, sigma 1.5 px
            float wt = wd * wn * ws;
            sum += s.x * wt;
            wsum += wt;
        }
    gShadowMask[id.xy] = wsum > 0 ? sum / wsum : c.x;
}
