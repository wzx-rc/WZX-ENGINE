// Ray traced sun shadow, step 2 (one thread per pixel): 5 x 5 bilateral filter of the raw visibility, guided by the
// depth and the normal of each pixel (SunShadowTrace.hlsl), so the few rays per pixel average with the neighbours on
// the same surface into a smooth penumbra, without blurring across object edges. No temporal accumulation.
RWTexture2D<float4> gShadowRaw : register(u18);
RWTexture2D<float> gShadowMask : register(u19);
struct SunPass { float2 sunOct; float tanRadius; uint samples; };   // tanRadius < 0: an area light (wider penumbra)
ConstantBuffer<SunPass> S : register(b1);

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
    // the penumbra of an area light spans many pixels: a wider kernel for it (radius set by the shadow quality)
    // depth slope of this pixel's surface (the smaller one-sided difference, so an edge next to it does not count):
    // neighbours are compared with the depth the plane predicts, else a floor seen at a grazing angle (its depth
    // changes by more than 2 % per pixel) would reject all its neighbours and keep its noise
    int2 ip = int2(id.xy);
    float dxa = gShadowRaw[min(ip + int2(1, 0), int2(w, h) - 1)].y - c.y, dxb = c.y - gShadowRaw[max(ip - int2(1, 0), 0)].y;
    float dya = gShadowRaw[min(ip + int2(0, 1), int2(w, h) - 1)].y - c.y, dyb = c.y - gShadowRaw[max(ip - int2(0, 1), 0)].y;
    float gx = abs(dxa) < abs(dxb) ? dxa : dxb, gy = abs(dya) < abs(dyb) ? dya : dyb;
    const int r = clamp(int(S.samples >> 24), 1, 8);   // shadow quality (Renderer::recordScreenTrace)
    const float sig2 = 2.0 * (0.75 * r) * (0.75 * r);
    for (int y = -r; y <= r; ++y)
        for (int x = -r; x <= r; ++x) {
            int2 q = clamp(int2(id.xy) + int2(x, y), int2(0, 0), int2(w, h) - 1);
            float4 s = gShadowRaw[q];
            if (s.y <= 0) continue;
            float wd = exp(-abs(s.y - (c.y + gx * x + gy * y)) / (0.02 * c.y));   // same plane: within 2 % of the depth
            float wn = pow(saturate(dot(n, OctDec(s.zw))), 16.0);           // and the same orientation
            float ws = exp(-(x * x + y * y) / sig2);                        // gaussian, sigma 0.75 radius
            float wt = wd * wn * ws;
            sum += s.x * wt;
            wsum += wt;
        }
    gShadowMask[id.xy] = wsum > 0 ? sum / wsum : c.x;
}
