// Depth of field and bloom of the raster views (RenderSettings::dof, bloom), after the tone mapper.
// mode 0: the depth of each pixel centre, one camera ray against the TLAS (slot 1 .x, 1e30 when it sees nothing)
// mode 1: depth of field: a disc of 24 samples whose radius grows with the distance to the focus (circle of
//         confusion), the sharper samples in front kept out of a blurred background; output -> slot 4
// mode 2: bloom: the bright parts of slot 4 gathered over 3 rings (up to ~3 % of the height), added; slot 4 -> output
#include "Common.hlsli"

struct PostConstants { uint mode; float dofPx; float bloom; float focus; };
ConstantBuffer<PostConstants> P : register(b1);

float CocPx(float d) {   // radius of the blur (pixels) of a surface at distance d
    if (d > 1e20) d = P.focus * 50.0;
    return min(P.dofPx * abs(d - P.focus) / max(d, 1e-3), 24.0);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= F.resolution.x || id.y >= F.resolution.y) return;
    const int2 p = int2(id.xy);
    const int2 mx = int2(F.resolution) - 1;
    if (P.mode == 0) {
        float2 ndc = (float2(p) + 0.5) / float2(F.resolution) * float2(2, -2) + float2(-1, 1);
        float3 o, d;
        CameraRay(ndc, o, d);
        RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
        RayDesc r;
        r.Origin = o; r.Direction = d; r.TMin = 0.0; r.TMax = 1e30;
        q.TraceRayInline(gScene, RAY_FLAG_NONE, MASK_OPAQUE | MASK_GLASS, r);
        q.Proceed();
        gTex[1][p] = float4(q.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? q.CommittedRayT() : 1e30, 0, 0, 0);
        return;
    }
    if (P.mode == 1) {
        const float4 c0 = gTex[SLOT_OUTPUT][p];
        const float r0 = P.dofPx > 0 ? CocPx(gTex[1][p].x) : 0;
        if (r0 < 0.5) { gTex[4][p] = c0; return; }
        float3 acc = c0.rgb;
        float wsum = 1;
        [loop] for (int k = 0; k < 24; ++k) {   // golden angle spiral over the disc
            const float t = (k + 0.5) / 24.0;
            const float a = k * 2.39996323;
            const float2 off = float2(cos(a), sin(a)) * sqrt(t) * r0;
            const int2 q = clamp(p + int2(round(off)), int2(0, 0), mx);
            const float rq = CocPx(gTex[1][q].x);
            // a sample counts when its own blur reaches this pixel (no sharp foreground smeared over the background)
            const float w = saturate(rq - length(off) + 1.0);
            acc += gTex[SLOT_OUTPUT][q].rgb * w;
            wsum += w;
        }
        gTex[4][p] = float4(acc / wsum, 1);
        return;
    }
    // mode 2: bloom over slot 4, into the output
    const float3 c = gTex[4][p].rgb;
    if (P.bloom <= 0) { gTex[SLOT_OUTPUT][p] = float4(c, 1); return; }
    const float h = (float)F.resolution.y;
    float3 glow = 0;
    float wsum = 0;
    [unroll] for (int ring = 0; ring < 3; ++ring) {
        const float rad = h * (0.006 + 0.012 * ring);
        const float wr = 1.0 / (1.0 + ring);
        [unroll] for (int k = 0; k < 12; ++k) {
            const float a = (k + 0.5 * ring) * 0.523598776;
            const int2 q = clamp(p + int2(round(float2(cos(a), sin(a)) * rad)), int2(0, 0), mx);
            const float3 s = gTex[4][q].rgb;
            glow += max(s - 0.75, 0.0) * wr;   // the bright parts only (sRGB, after the tone mapping)
            wsum += wr;
        }
    }
    gTex[SLOT_OUTPUT][p] = float4(saturate(c + glow / wsum * P.bloom * 4.0), 1);
}
