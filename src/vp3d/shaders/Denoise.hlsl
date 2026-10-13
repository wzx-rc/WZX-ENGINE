// Edge-aware a-trous wavelet filter on albedo-demodulated radiance,
// guided by normals, depth and luminance. Strength fades as samples accumulate.
#include "Common.hlsli"

struct PassConstants { uint src; uint dst; uint stepSize; uint first; };
ConstantBuffer<PassConstants> P : register(b1);

float3 LoadColor(int2 p) {
    if (P.first) {
        float4 a = gTex[SLOT_ACCUM][p];
        float3 alb = max(gTex[SLOT_ALBEDO][p].rgb, 0.01);
        return a.rgb / max(a.w, 1.0) / alb;
    }
    return gTex[P.src][p].rgb;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= F.resolution.x || id.y >= F.resolution.y) return;
    int2 p = int2(id.xy);
    float spp = max(gTex[SLOT_ACCUM][p].w, 1.0);
    float3 c = LoadColor(p);
    float4 nd = gTex[SLOT_NORMAL][p];
    float3 n = normalize(nd.xyz + 1e-6);
    float z = nd.w;
    float lc = Luminance(c);
    bool idEdges = (F.flags & FRAME_FLAG_ID_EDGES) != 0;
    float objId = gTex[SLOT_ALBEDO][p].w;

    const float kernel[3] = {3.0 / 8.0, 1.0 / 4.0, 1.0 / 16.0};
    float sigmaL = F.denoiseStrength * 4.0 / sqrt(spp);
    float step = (float)P.stepSize;

    float3 sum = 0;
    float wsum = 0;
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            int2 q = p + int2(dx, dy) * (int)P.stepSize;
            if (q.x < 0 || q.y < 0 || q.x >= (int)F.resolution.x || q.y >= (int)F.resolution.y) continue;
            float3 cq = LoadColor(q);
            float4 ndq = gTex[SLOT_NORMAL][q];
            float wn = pow(saturate(dot(n, normalize(ndq.xyz + 1e-6))), 64.0);
            float wz = exp(-abs(z - ndq.w) / (0.01 * z * step + 1e-4));
            float lq = Luminance(cq);
            float wl = exp(-abs(lc - lq) / (sigmaL * max(max(lc, lq), 0.02) + 1e-4));
            float w = kernel[abs(dx)] * kernel[abs(dy)] * wn * wz * wl;
            if (idEdges && abs(gTex[SLOT_ALBEDO][q].w - objId) > 0.5) w = 0;
            sum += cq * w;
            wsum += w;
        }
    }
    gTex[P.dst][p] = float4(wsum > 0 ? sum / wsum : c, 1.0);
}
