// Editor grid of the raster viewport: an infinite ground plane (y = 0) with thin lines that fade with distance and
// switch to ten times larger cells when they get too dense, the X (red) and Z (blue) axes on the ground and the
// Y axis (green) standing at the origin. Full screen pass drawn after the opaque objects: SV_Depth puts each line at
// its true depth, so objects in front hide it. Editor only (DirectX 12 viewport, not the players).

// First members of RasterConstants (Raster.hlsl, RendererRaster.cpp): the camera.
struct RasterCamera {
    row_major float4x4 viewProj;
    row_major float4x4 lightViewProj;
    float3 camPos; float time;
    float3 camRight; float tanHalfFovY;
    float3 camUp; float aspect;
    float3 camFwd; uint lightCount;
};
ConstantBuffer<RasterCamera> R : register(b0);

struct GridConstants {
    float cell;        // smallest cell (scene units)
    float axisLen;     // length of the Y axis
    float opacity;     // line opacity
    float pixelAngle;  // view angle of one pixel (radians): line widths in pixels
};
ConstantBuffer<GridConstants> G : register(b3);

struct GridOut {
    float4 pos : SV_Position;
    float2 ndc : NDC;
};

GridOut VSGrid(uint id : SV_VertexID) {
    GridOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.ndc = uv * float2(2, -2) + float2(-1, 1);
    o.pos = float4(o.ndc, 0, 1);
    return o;
}

struct GridPixel {
    float4 color : SV_Target;
    float depth : SV_Depth;
};

float DepthOf(float3 p) {
    float4 c = mul(R.viewProj, float4(p, 1));
    return c.z / c.w;
}

// coverage of a line lying at d pixels (about 1 pixel wide, antialiased)
float LineCov(float d, float width) { return saturate(width * 0.5 + 0.5 - d); }

GridPixel PSGrid(GridOut i) {
    float3 rd = normalize(R.camFwd + R.camRight * (i.ndc.x * R.aspect * R.tanHalfFovY) + R.camUp * (i.ndc.y * R.tanHalfFovY));
    float3 ro = R.camPos;
    float4 col = 0;
    float depth = 1;

    // ground plane (the derivatives are taken before any branch)
    float rdy = abs(rd.y) < 1e-6 ? -1e-6 : rd.y;
    float t = -ro.y / rdy;
    float3 p = ro + rd * clamp(t, 0.0, 1e7);
    float2 fw = max(fwidth(p.xz), 1e-6);
    if (rdy * ro.y < 0 && t > 0) {
        // level: cells at least ~8 pixels apart; the finer level fades out as it gets denser
        float lod = max(0.0, log10(max(fw.x, fw.y) * 8.0 / G.cell));
        float fade = frac(lod);
        float c0 = G.cell * pow(10.0, floor(lod)), c1 = c0 * 10.0;
        float2 d0 = abs(frac(p.xz / c0 + 0.5) - 0.5) * c0 / fw;
        float2 d1 = abs(frac(p.xz / c1 + 0.5) - 0.5) * c1 / fw;
        float minor = max(LineCov(d0.x, 1.0), LineCov(d0.y, 1.0)) * (1.0 - fade);
        float major = max(LineCov(d1.x, 1.0), LineCov(d1.y, 1.0));
        float a = max(minor * 0.2, major * 0.4) * G.opacity;
        float3 c = float3(0.58, 0.63, 0.72);
        // axes on the ground, X (z = 0) red and Z (x = 0) blue, as long as the Y axis (labels at the ends: AtelierUI)
        float ax = LineCov(abs(p.z) / fw.y, 1.4) * (1.0 - smoothstep(0.85, 1.0, abs(p.x) / G.axisLen));
        float az = LineCov(abs(p.x) / fw.x, 1.4) * (1.0 - smoothstep(0.85, 1.0, abs(p.z) / G.axisLen));
        if (ax > 0.01) { c = lerp(c, float3(0.93, 0.36, 0.36), ax); a = max(a, ax * 0.9); }
        if (az > 0.01) { c = lerp(c, float3(0.36, 0.56, 0.98), az); a = max(a, az * 0.9); }
        // fade towards the horizon and far away
        float horizon = smoothstep(0.0, 0.28, abs(rd.y));
        a *= horizon;
        col = float4(c, a);
        depth = DepthOf(p);
    }

    // Y axis: the segment (0, 0..axisLen, 0)
    {
        float3 w = ro;                               // origin of the axis is (0, 0, 0)
        float b = rd.y, dd = dot(rd, w), e = w.y;    // closest points between the ray and the vertical line
        float den = 1.0 - b * b;
        if (den > 1e-5) {
            float tr = (b * e - dd) / den;           // along the ray
            float s = (e - b * dd) / den;            // height on the axis
            s = clamp(s, 0.0, G.axisLen);
            float3 q = float3(0, s, 0);
            float3 rp = ro + rd * max(dot(q - ro, rd), 0.0);
            float dist = length(rp - q), tq = max(dot(q - ro, rd), 1e-4);
            float cov = LineCov(dist / (tq * G.pixelAngle), 1.4) * (1.0 - smoothstep(0.85, 1.0, s / G.axisLen));
            float dq = DepthOf(q);
            if (cov > 0.01 && tr > 0 && (col.a < 0.01 || dq < depth)) {
                col = float4(float3(0.42, 0.86, 0.5), cov * 0.95);
                depth = dq;
            }
        }
    }
    if (col.a < 0.004) discard;
    GridPixel o;
    o.color = col;
    o.depth = saturate(depth - 2e-6);   // a hair in front: drawn over a floor lying at y = 0
    return o;
}
