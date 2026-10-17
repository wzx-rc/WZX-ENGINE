// Temporal reprojection for the real-time renderers. The includer must include Common.hlsli first (F, gTex).
//
// ACCUM stores (mean * n, n), like the progressive path, so the denoiser and tonemapper are unchanged.
// The history is a copy of ACCUM and of NORMAL (geometric normal xyz, depth w) of the previous frame, written by
// the renderer after each frame. The current frame reads it at the reprojected pixel, so there is no read/write race.
RWTexture2D<float4> gHistAccum : register(u10);
RWTexture2D<float4> gHistNormal : register(u11);

// Primary ray through the pixel centre. CurPixelRay and PrevPixelRay use the same arithmetic on the current and the
// previous camera, so a static camera gives bit-identical anchors and reprojects exactly. Orthographic cameras
// (orthoHalfH > 0): parallel rays from orthoBack behind the eye (CameraRay, Common.hlsli).
void CurPixelRay(float2 p, out float3 org, out float3 dir) {
    float2 uv = (p + 0.5) / float2(F.resolution);
    CameraRay(float2(2.0 * uv.x - 1.0, 1.0 - 2.0 * uv.y), org, dir);
}

void PrevPixelRay(float2 p, out float3 org, out float3 dir) {
    float2 uv = (p + 0.5) / float2(F.resolution);
    float2 ndc = float2(2.0 * uv.x - 1.0, 1.0 - 2.0 * uv.y);
    if (F.prevOrthoHalfH > 0) {
        org = F.prevCamPos + F.prevCamRight * (ndc.x * F.prevAspect * F.prevOrthoHalfH) + F.prevCamUp * (ndc.y * F.prevOrthoHalfH) -
              F.prevCamFwd * F.prevOrthoBack;
        dir = F.prevCamFwd;
        return;
    }
    org = F.prevCamPos;
    dir = normalize(F.prevCamFwd + F.prevCamRight * (ndc.x * F.prevAspect * F.prevTanHalfFovY) + F.prevCamUp * (ndc.y * F.prevTanHalfFovY));
}

// Pixel of the previous frame that shows world point P. False when it is outside the screen or behind the camera.
bool ReprojectPixel(float3 P, out int2 q) {
    float3 v = P - F.prevCamPos;
    float z = dot(v, F.prevCamFwd);
    q = int2(0, 0);
    float x, y;
    if (F.prevOrthoHalfH > 0) {
        x = dot(v, F.prevCamRight) / (F.prevOrthoHalfH * F.prevAspect);
        y = dot(v, F.prevCamUp) / F.prevOrthoHalfH;
    } else {
        if (z <= 1e-4) return false;
        x = dot(v, F.prevCamRight) / (z * F.prevAspect * F.prevTanHalfFovY);
        y = dot(v, F.prevCamUp) / (z * F.prevTanHalfFovY);
    }
    float2 uv = float2(x, -y) * 0.5 + 0.5;
    q = int2(floor(uv * float2(F.resolution)));
    return all(q >= 0) && all(q < int2(F.resolution));
}

// True when the history pixel q stored the same surface as the current sample (P, N, depth).
bool SameSurface(int2 q, float3 P, float3 N, float depth) {
    float4 hn = gHistNormal[q];
    bool bgNow = depth > 1e5, bgPrev = hn.w > 1e5;
    if (bgNow || bgPrev) return bgNow && bgPrev;   // sky / background only matches sky
    float3 po, pd;
    PrevPixelRay(float2(q), po, pd);
    float3 Pprev = po + pd * hn.w;
    return length(Pprev - P) < 0.01 * max(depth, 1e-3) && dot(N, hn.xyz) > 0.9;
}

// Mean and length of the history for the surface (P, N). The reprojected pixel is tried first, then its
// 4-neighbours: a silhouette pixel often stored the other surface, and a neighbour may hold the right one.
// Length 0 when no history matches.
void FetchHistory(float3 P, float3 N, float depth, out float3 mean, out float count) {
    mean = 0;
    count = 0;
    if (!F.historyValid) return;
    int2 q0;
    if (!ReprojectPixel(P, q0)) return;
    static const int2 offs[5] = {int2(0, 0), int2(1, 0), int2(-1, 0), int2(0, 1), int2(0, -1)};
    [unroll] for (int i = 0; i < 5; ++i) {
        int2 q = q0 + offs[i];
        if (any(q < 0) || any(q >= int2(F.resolution))) continue;
        float4 h = gHistAccum[q];
        if (h.w <= 0 || !SameSurface(q, P, N, depth)) continue;
        count = min(h.w, F.historyCap);
        mean = h.rgb / h.w;
        return;
    }
}

// Blends this frame's radiance L of the primary surface (P, N, depth) into the history.
// Returns the value to store in ACCUM.
float4 TemporalAccumulate(float3 L, float3 P, float3 N, float depth) {
    float3 mean;
    float n;
    FetchHistory(P, N, depth, mean, n);
    float3 m = n > 0 ? lerp(mean, L, 1.0 / (n + 1.0)) : L;
    float nNew = min(n + 1.0, F.historyCap);
    return float4(m * nNew, nNew);
}
