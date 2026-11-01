// Screen-space ambient occlusion of the raster viewport (the high quality mode of the BrickIt web builder uses three.js
// SSAOPass: kernel radius 0.6 scene units, its result blurred 4 x 4 and multiplied into the image).
// main: occlusion of each pixel from the viewport depth (hemisphere kernel of 16 samples around the view-space normal,
// rotated per pixel on a 4 x 4 pattern), stored in the alpha of the resolved HDR slot. blur: 4 x 4 box of that alpha,
// multiplied into the colour (the tonemapper then reads the slot as usual).
#include "Common.hlsli"

struct SsaoConstants { uint slot; float A; float B; float radius; };   // depth = A + B / z (view distance z)
ConstantBuffer<SsaoConstants> S : register(b1);
#ifdef SSAO_MS
Texture2DMS<float> gDepth : register(t9);
float LoadDepth(int2 q) { return gDepth.Load(clamp(q, int2(0, 0), int2(F.resolution) - 1), 0); }
#else
Texture2D<float> gDepth : register(t9);
float LoadDepth(int2 q) { return gDepth.Load(int3(clamp(q, int2(0, 0), int2(F.resolution) - 1), 0)); }
#endif

float ViewZ(float d) { return S.B / max(d - S.A, 1e-7); }
float3 ViewPos(int2 q, float d) {
    float2 uv = (float2(q) + 0.5) / float2(F.resolution);
    float2 ndc = float2(uv.x * 2 - 1, 1 - uv.y * 2);
    float z = ViewZ(d);
    return float3(ndc.x * F.tanHalfFovY * F.aspect * z, ndc.y * F.tanHalfFovY * z, z);
}
float3 PosAt(int2 q) { return ViewPos(q, LoadDepth(q)); }

#ifndef SSAO_BLUR
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= F.resolution.x || id.y >= F.resolution.y) return;
    int2 p = int2(id.xy);
    float4 c = gTex[S.slot][p];
    float d = LoadDepth(p);
    if (d >= 0.99999) { gTex[S.slot][p] = float4(c.rgb, 1); return; }   // background
    float3 P = ViewPos(p, d);
    // normal from the nearer neighbour on each axis (no smearing across silhouettes)
    float3 l = PosAt(p + int2(-1, 0)), r = PosAt(p + int2(1, 0)), u = PosAt(p + int2(0, -1)), dn = PosAt(p + int2(0, 1));
    float3 dx = abs(r.z - P.z) < abs(P.z - l.z) ? r - P : P - l;
    float3 dy = abs(dn.z - P.z) < abs(P.z - u.z) ? dn - P : P - u;
    float3 N = normalize(cross(dx, dy));
    if (dot(N, P) > 0) N = -N;   // facing the camera
    // per pixel rotation of the kernel (4 x 4 interleaved pattern, blurred away by the 4 x 4 box)
    float ang = (float)(((p.x & 3) * 4 + (p.y & 3)) * 7 % 16) / 16.0 * 6.2831853;
    float3 tsel = abs(N.y) < 0.9 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 T = normalize(cross(tsel, N)), B = cross(N, T);
    float ca = cos(ang), sa = sin(ang);
    float3 T2 = T * ca + B * sa, B2 = B * ca - T * sa;
    float occ = 0;
    const int K = 16;
    [unroll] for (int k = 0; k < K; ++k) {
        // spiral hemisphere kernel, denser near the centre (SSAOPass: scale lerp(0.1, 1, (k / K)^2))
        float fk = (k + 0.5) / K;
        float phi = k * 2.39996323;
        float cz = sqrt(1 - fk);
        float rr = sqrt(fk);
        float3 h = float3(cos(phi) * rr, sin(phi) * rr, cz);
        float sc = lerp(0.1, 1.0, fk * fk);
        float3 sp = P + (T2 * h.x + B2 * h.y + N * h.z) * (S.radius * sc);
        if (sp.z <= 0.01) continue;
        float2 sn = float2(sp.x / (sp.z * F.tanHalfFovY * F.aspect), sp.y / (sp.z * F.tanHalfFovY));
        int2 sq = int2((sn.x * 0.5 + 0.5) * F.resolution.x, (0.5 - sn.y * 0.5) * F.resolution.y);
        float sz = ViewZ(LoadDepth(sq));
        float range = saturate(S.radius / max(abs(P.z - sz), 1e-4));
        occ += (sz <= sp.z - 0.02 * S.radius ? 1.0 : 0.0) * range;
    }
    float ao = 1.0 - occ / K;
    gTex[S.slot][p] = float4(c.rgb, ao);
}
#else
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= F.resolution.x || id.y >= F.resolution.y) return;
    int2 p = int2(id.xy);
    float sum = 0;
    [unroll] for (int y = -2; y < 2; ++y)
        [unroll] for (int x = -2; x < 2; ++x) sum += gTex[S.slot][clamp(p + int2(x, y), int2(0, 0), int2(F.resolution) - 1)].a;
    float4 c = gTex[S.slot][p];
    gTex[S.slot][p] = float4(c.rgb * (sum / 16.0), c.a);   // (alpha kept: the neighbours read it in this same pass)
}
#endif
