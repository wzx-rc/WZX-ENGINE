// Ray traced reflections, step 2 (one thread per pixel): bilateral filter of the raw reflections, wider for rougher
// surfaces (a mirror keeps its own ray), guided by the depth and the normal written by SunShadowTrace.hlsl and by the
// roughness. The result (alpha 1 = traced) is read by the raster pass (t9). No temporal accumulation.
RWTexture2D<float4> gShadowRaw : register(u18);   // y depth, zw normal (octahedral)
RWTexture2D<float4> gReflRaw : register(u20);
RWTexture2D<float4> gRefl : register(u21);

float2 SignNZ2(float2 v) { return float2(v.x >= 0 ? 1.0 : -1.0, v.y >= 0 ? 1.0 : -1.0); }

float3 OctDec(float2 o) {
    float3 n = float3(o, 1.0 - abs(o.x) - abs(o.y));
    if (n.z < 0) n.xy = (1.0 - abs(n.yx)) * SignNZ2(n.xy);
    return normalize(n);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint w, h;
    gReflRaw.GetDimensions(w, h);
    if (id.x >= w || id.y >= h) return;
    float4 c = gReflRaw[id.xy];
    if (c.a < 0) { gRefl[id.xy] = 0; return; }
    if (c.a < 0.04) { gRefl[id.xy] = float4(c.rgb, 1); return; }   // mirror: exact already
    float4 g = gShadowRaw[id.xy];
    float3 n = OctDec(g.zw);
    float sigma = 0.75 + 4.0 * c.a;                                   // pixels
    int radius = (int)min(ceil(2.0 * sigma), 4.0);
    float3 sum = 0;
    float wsum = 0;
    for (int y = -radius; y <= radius; ++y)
        for (int x = -radius; x <= radius; ++x) {
            int2 q = clamp(int2(id.xy) + int2(x, y), int2(0, 0), int2(w, h) - 1);
            float4 s = gReflRaw[q];
            if (s.a < 0) continue;
            float4 gs = gShadowRaw[q];
            float wd = exp(-abs(gs.y - g.y) / (0.02 * g.y + 1e-4));
            float wn = pow(saturate(dot(n, OctDec(gs.zw))), 32.0);
            float wr = exp(-abs(s.a - c.a) * 20.0);
            float ws = exp(-(x * x + y * y) / (2.0 * sigma * sigma));
            float wt = wd * wn * wr * ws;
            sum += s.rgb * wt;
            wsum += wt;
        }
    gRefl[id.xy] = float4(wsum > 0 ? sum / wsum : c.rgb, 1);
}
