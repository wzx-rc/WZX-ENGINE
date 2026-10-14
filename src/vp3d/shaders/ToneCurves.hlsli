// View transforms shared by the path tracer's tonemap pass (Tonemap.hlsl) and the Lit post pass (src/lit).
#ifndef VP3D_TONE_CURVES
#define VP3D_TONE_CURVES

float3 ACESFitted(float3 x) {
    // Stephen Hill's fit of the ACES RRT+ODT.
    const float3x3 inM = float3x3(0.59719, 0.35458, 0.04823, 0.07600, 0.90834, 0.01566, 0.02840, 0.13383, 0.83777);
    const float3x3 outM = float3x3(1.60475, -0.53108, -0.07367, -0.10208, 1.10813, -0.00605, -0.00327, -0.07276, 1.07602);
    x = mul(inM, x);
    float3 a = x * (x + 0.0245786) - 0.000090537;
    float3 b = x * (0.983729 * x + 0.4329510) + 0.238081;
    x = mul(outM, a / b);
    return saturate(x);
}

// AgX (Troy Sobotka), polynomial fit by Benjamin Wrensch. look: 0 base, 1 punchy.
float3 AgXContrast(float3 x) {
    float3 x2 = x * x, x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

float3 AgX(float3 v, uint look) {
    const float3x3 agxIn = float3x3(0.842479062253094, 0.0423282422610123, 0.0423756549057051,
                                    0.0784335999999992, 0.878468636469772, 0.0784336,
                                    0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const float3x3 agxOut = float3x3(1.19687900512017, -0.0528968517574562, -0.0529716355144438,
                                     -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
                                     -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    const float minEv = -12.47393, maxEv = 4.026069;
    v = mul(max(v, 1e-10), agxIn);
    v = clamp(log2(v), minEv, maxEv);
    v = (v - minEv) / (maxEv - minEv);
    v = AgXContrast(v);
    if (look == 1) {
        float luma = dot(v, float3(0.2126, 0.7152, 0.0722));
        v = pow(max(v, 0.0), 1.35);
        v = luma + 1.4 * (v - luma);
    }
    v = mul(v, agxOut);
    return saturate(pow(max(v, 0.0), 2.2));  // back to linear, encoded to sRGB by the caller
}

// Scalar form: compiles with every HLSL compiler (select() needs HLSL 2021, not available to FXC and glslang).
float ToSRGB1(float c) { return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(max(c, 0.0), 1.0 / 2.4) - 0.055; }
float3 ToSRGB(float3 c) { return float3(ToSRGB1(c.x), ToSRGB1(c.y), ToSRGB1(c.z)); }

#endif
