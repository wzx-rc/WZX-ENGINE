// Editor grid of the raster viewport (options: Grid Options window of the editor): a ground plane (y = 0) with grid
// lines every G.every units and G.subdiv subdivisions between them, larger cells taking over when the lines get too
// dense, the X (z = 0) and Z (x = 0) axes on the ground and the Y axis standing at the origin, with small arrows and
// the letters (drawn by the editor) at their ends. Full screen pass drawn after the opaque objects: SV_Depth puts each
// line at its true depth, so objects in front hide it. Orthographic views: the plane facing the view (front: XY, side:
// YZ, top: the ground), drawn under the objects. Editor only (DirectX 12 viewport, not the players).

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

#define GRID_LINES 1u
#define GRID_SUBDIV 2u
#define GRID_AXES 4u
#define GRID_ARROWS 8u
#define GRID_FADE 16u

// 32 root constants (RendererRaster.cpp, drawGrid)
struct GridConstants {
    float every;       // spacing of the grid lines (scene units)
    float axisLen;     // length of the axes
    float opacity;     // overall opacity
    float pixelAngle;  // view angle of one pixel (radians): line widths in pixels; orthographic: the size of a pixel
    float pixelScale;  // render pixels per screen pixel: the widths are screen pixels
    float subdiv;      // subdivisions between two grid lines (1 = none)
    float extent;      // length and width of the grid, centred on the origin (0 = infinite)
    uint flags;        // GRID_*, bits 8-9 the plane: 0 the ground (XZ), 1 XY, 2 YZ (orthographic front / side views)
    float wGrid, wSub, wAxis;   // line widths (screen pixels)
    float orthoHalfH;  // orthographic camera: half the height of the view (0 = perspective)
    float4 colGrid;    // rgb, a = opacity of the lines
    float4 colSub;
    float4 colX, colY, colZ;
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

// coverage of a line lying at d pixels, width pixels wide (antialiased; under 1 pixel it gets fainter)
float LineCov(float d, float width) { width *= G.pixelScale; return saturate(width * 0.5 + 0.5 - d); }

// World axes: a line, then a small arrow head on the positive side, from kArrowBase of the axis length to its end.
static const float kArrowBase = 0.93, kArrowHalf = 0.018;   // arrow start and half width (fractions of the axis length)

// opacity along an axis (x = distance / axis length): with the arrows, the line fades to 45 % before the arrow and the
// arrow carries the gradient on to its tip; the negative side (and both sides without arrows) fades out at the end
float AxisFade(float x) {
    if (x < 0.0 || !(G.flags & GRID_ARROWS)) return 1.0 - smoothstep(0.85, 1.0, abs(x));
    if (x < kArrowBase) return lerp(1.0, 0.45, smoothstep(0.5, kArrowBase, x));
    return lerp(0.45, 0.25, saturate((x - kArrowBase) / (1.0 - kArrowBase)));
}

// coverage (x) and opacity (y) of an axis lying on the ground: u along it, v across (world units), fwU / fwV their
// size of a pixel
float2 GroundAxis(float u, float v, float fwU, float fwV) {
    float L = G.axisLen, x = u / L;
    float cov = LineCov(abs(v) / fwV, G.wAxis) * (abs(x) <= 1.0 ? 1.0 : 0.0);
    if ((G.flags & GRID_ARROWS) && x > kArrowBase - 0.02 && x < 1.02) {   // flat arrow head on the ground
        float hw = kArrowHalf * L * saturate((1.0 - x) / (1.0 - kArrowBase));
        float outside = max((abs(v) - hw) / fwV, max((u - L) / fwU, (kArrowBase * L - u) / fwU));
        cov = max(cov, saturate(0.5 - outside));
    }
    return float2(cov, AxisFade(x));
}

GridPixel PSGrid(GridOut i) {
    // primary ray: from the eye (perspective), parallel to the view from the plane of the eye (orthographic)
    const bool ortho = G.orthoHalfH > 0;
    float3 rd, ro;
    if (ortho) {
        rd = R.camFwd;
        ro = R.camPos + R.camRight * (i.ndc.x * R.aspect * G.orthoHalfH) + R.camUp * (i.ndc.y * G.orthoHalfH);
    } else {
        rd = normalize(R.camFwd + R.camRight * (i.ndc.x * R.aspect * R.tanHalfFovY) + R.camUp * (i.ndc.y * R.tanHalfFovY));
        ro = R.camPos;
    }
    // the grid plane, through the origin: the ground (XZ), or for an orthographic front / side view the plane facing
    // it (XY, YZ, as in Maya); u and v its axes
    const uint plane = (G.flags >> 8) & 3u;
    const float3 N = plane == 0 ? float3(0, 1, 0) : (plane == 1 ? float3(0, 0, 1) : float3(1, 0, 0));
    const float3 U = plane == 2 ? float3(0, 0, 1) : float3(1, 0, 0);
    const float3 V = plane == 0 ? float3(0, 0, 1) : float3(0, 1, 0);
    const float4 colU = plane == 2 ? G.colZ : G.colX, colV = plane == 0 ? G.colZ : G.colY;
    float4 col = 0;
    float depth = 1;

    // grid plane (the derivatives are taken before any branch)
    float rdn = dot(rd, N);
    rdn = abs(rdn) < 1e-6 ? -1e-6 : rdn;
    const float ron = dot(ro, N);
    float t = -ron / rdn;
    float3 p = ro + rd * clamp(t, ortho ? -1e7 : 0.0, 1e7);
    float2 pp = float2(dot(p, U), dot(p, V));
    float2 fw = max(fwidth(pp), 1e-6);
    if (ortho || (rdn * ron < 0 && t > 0)) {
        float3 c = 0;
        float a = 0;
        const bool inside = G.extent <= 0.0 || (abs(pp.x) <= G.extent * 0.5 && abs(pp.y) <= G.extent * 0.5);
        if (inside && (G.flags & (GRID_LINES | GRID_SUBDIV))) {
            // finest spacing m (the subdivisions); a level k times larger takes over when the lines get ~6 px apart,
            // the finer one fading out (without subdivisions: levels 10 times larger)
            const float subdiv = max(G.subdiv, 1.0);
            const float k = subdiv >= 2.0 ? subdiv : 10.0;
            const float m = G.every / subdiv;
            float lod = max(0.0, log(max(fw.x, fw.y) * 6.0 / m) / log(k));
            float fade = frac(lod);
            float c0 = m * pow(k, floor(lod)), c1 = c0 * k;
            // the finer level holds grid lines once it reaches the spacing of the grid lines, else subdivisions
            const bool grid0 = c0 >= G.every * 0.999;
            const float4 s0 = grid0 ? G.colGrid : G.colSub;
            const float w0 = grid0 ? G.wGrid : G.wSub;
            const bool on0 = (G.flags & (grid0 ? GRID_LINES : GRID_SUBDIV)) != 0;
            float2 d0 = abs(frac(pp / c0 + 0.5) - 0.5) * c0 / fw;
            float2 d1 = abs(frac(pp / c1 + 0.5) - 0.5) * c1 / fw;
            float a0 = on0 ? max(LineCov(d0.x, w0), LineCov(d0.y, w0)) * (1.0 - fade) * s0.a : 0.0;
            float a1 = (G.flags & GRID_LINES) ? max(LineCov(d1.x, G.wGrid), LineCov(d1.y, G.wGrid)) * G.colGrid.a : 0.0;
            if (a1 >= a0) { c = G.colGrid.rgb; a = a1; }
            else { c = s0.rgb; a = a0; }
        }
        if (G.flags & GRID_AXES) {
            float2 au = GroundAxis(pp.x, pp.y, fw.x, fw.y), av = GroundAxis(pp.y, pp.x, fw.y, fw.x);
            if (au.x > 0.01) { c = lerp(c, colU.rgb, au.x); a = lerp(a, au.y * colU.a, au.x); }
            if (av.x > 0.01) { c = lerp(c, colV.rgb, av.x); a = lerp(a, av.y * colV.a, av.x); }
        }
        // fade towards the horizon and far away (else only at the very horizon, where the lines would shimmer)
        a *= (G.flags & GRID_FADE) ? smoothstep(0.0, 0.28, abs(rdn)) : smoothstep(0.0, 0.03, abs(rdn));
        col = float4(c, a * G.opacity);
        // the planes of the front and side views: under everything, as the grids of the orthographic views of Maya; the
        // ground: at its true depth (over a floor, under the objects standing on it)
        depth = (ortho && plane != 0) ? 0.999999 : DepthOf(p);
    }

    // Y axis standing on the ground: the segment (0, 0..axisLen, 0)
    if ((G.flags & GRID_AXES) && plane == 0) {
        float3 w = ro;                               // origin of the axis is (0, 0, 0)
        float b = rd.y, dd = dot(rd, w), e = w.y;    // closest points between the ray and the vertical line
        float den = 1.0 - b * b;
        if (den > 1e-5) {
            float tr = (b * e - dd) / den;           // along the ray
            float s = (e - b * dd) / den;            // height on the axis
            s = clamp(s, 0.0, G.axisLen);
            float3 q = float3(0, s, 0);
            float3 rp = ro + rd * (ortho ? dot(q - ro, rd) : max(dot(q - ro, rd), 0.0));
            float dist = length(rp - q), tq = max(dot(q - ro, rd), 1e-4);
            float pixW = ortho ? G.pixelAngle : tq * G.pixelAngle, x = s / G.axisLen;
            float cov = LineCov(dist / pixW, G.wAxis);
            if ((G.flags & GRID_ARROWS) && x > kArrowBase) {   // arrow head: the outline of a cone, facing the camera
                float hw = kArrowHalf * G.axisLen * saturate((1.0 - x) / (1.0 - kArrowBase));
                cov = max(cov, saturate(0.5 - (dist - hw) / pixW));
            }
            cov *= AxisFade(x);
            float dq = DepthOf(q);
            if (cov > 0.01 && (ortho || tr > 0) && (col.a < 0.01 || dq < depth)) {
                col = float4(G.colY.rgb, cov * G.colY.a * G.opacity);
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
