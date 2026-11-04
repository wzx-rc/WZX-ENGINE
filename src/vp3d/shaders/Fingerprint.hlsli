// Roughness mask of a material (GpuMaterial::roughTex, fingerprints): triplanar projection in object space, a random
// offset per object (seed) so the bricks do not all show the same prints. Returns 0 (clean) .. 1 (print).
// The includer defines FP_SAMPLE(tex, uv) (texture table of its stage) before including this file.
float FingerprintMask(uint tex, float scale, float3 po, float3 no, uint seed) {
    uint h = seed * 747796405u + 2891336453u;
    h = ((h >> ((h >> 28u) + 4u)) ^ h) * 277803737u;
    h = (h >> 22u) ^ h;
    float2 off = float2(h & 0xFFFFu, h >> 16) * (1.0 / 65536.0);
    float3 w = pow(abs(no), 4.0);
    w /= max(w.x + w.y + w.z, 1e-5);
    float3 p = po * scale;
    float vx = FP_SAMPLE(tex, p.zy + off).g;
    float vy = FP_SAMPLE(tex, p.xz + off.yx).g;
    float vz = FP_SAMPLE(tex, p.xy + off * 1.7).g;
    float v = vx * w.x + vy * w.y + vz * w.z;   // linear (sRGB texture): clean ~0.75, prints ~0.2
    return saturate((0.78 - v) * 3.5);
}
