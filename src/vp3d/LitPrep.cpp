// CPU side of the real-time "Lit" raster pass (see LitPrep.h). The logic mirrors Renderer::setScene, buildTLAS,
// uploadTextures, uploadEnvironment and recordRaster (DirectX 12 viewport) so that both renderers match.
#include "LitPrep.h"
#include <cstring>

namespace vp3d {
namespace lit {

namespace {

// Real SH basis (order 2), same assignment as EnvIrradiance in Raster.hlsl.
void shBasis(const float3& d, float y[9]) {
    y[0] = 0.282095f;
    y[1] = 0.488603f * d.y;
    y[2] = 0.488603f * d.z;
    y[3] = 0.488603f * d.x;
    y[4] = 1.092548f * d.x * d.y;
    y[5] = 1.092548f * d.y * d.z;
    y[6] = 0.315392f * (3.0f * d.z * d.z - 1.0f);
    y[7] = 1.092548f * d.x * d.z;
    y[8] = 0.546274f * (d.x * d.x - d.y * d.y);
}

float3 latLongDir(float u, float v) {
    float ph = (u - 0.5f) * 2.0f * kPi, th = v * kPi;
    return float3(std::sin(th) * std::sin(ph), std::cos(th), -std::sin(th) * std::cos(ph));
}

float lum(const float3& c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }

void setRows(float* m, int row, const float3& v, float w) {
    m[row * 4 + 0] = v.x;
    m[row * 4 + 1] = v.y;
    m[row * 4 + 2] = v.z;
    m[row * 4 + 3] = w;
}

} // namespace

uint16_t floatToHalf(float f) {
    uint32_t x;
    memcpy(&x, &f, 4);
    uint32_t sign = (x >> 16) & 0x8000;
    int32_t e = (int32_t)((x >> 23) & 0xFF) - 127 + 15;
    uint32_t m = x & 0x7FFFFF;
    if (e >= 31) return (uint16_t)(sign | 0x7BFF);   // clamp to the largest half (no inf)
    if (e <= 0) {
        if (e < -10) return (uint16_t)sign;
        m |= 0x800000;
        return (uint16_t)(sign | ((m >> (14 - e)) + ((m >> (13 - e)) & 1)));
    }
    uint32_t h = sign | ((uint32_t)e << 10) | (m >> 13);
    return (uint16_t)(h + ((m >> 12) & 1));   // round to nearest
}

void buildGeometry(const Scene& scene, Geometry& out) {
    out = Geometry();
    auto addMesh = [&](const MeshData& m) {
        MeshInfo g;
        g.indexOffset = (uint32_t)out.indices.size();
        g.vertexOffset = (uint32_t)out.vertices.size();
        g.indexCount = (uint32_t)m.indices.size();
        g.vertexCount = (uint32_t)m.vertices.size();
        m.bounds(g.bmin, g.bmax);
        g.center = (g.bmin + g.bmax) * 0.5f;
        for (auto& v : m.vertices) g.radius = std::max(g.radius, length(v.pos - g.center));
        for (auto& v : m.vertices) out.vertices.push_back({v.pos, v.nrm, v.uv});
        out.indices.insert(out.indices.end(), m.indices.begin(), m.indices.end());
        out.meshes.push_back(g);
    };
    for (auto& m : scene.meshes) addMesh(m);
    out.quadMesh = (int)out.meshes.size();
    addMesh(makeQuadXY());
}

bool isTransparent(const Scene& scene, const Draw& d) {
    return d.material < scene.materials.size() && scene.materials[d.material].transmission > 0.5f;
}

void buildSceneData(const Scene& scene, const Geometry& geo, const std::vector<bool>& textureValid, SceneData& out) {
    out = SceneData();
    std::vector<gpu::GpuMaterial>& mats = out.materials;
    std::vector<gpu::GpuLight>& lights = out.lights;
    std::vector<int> objectToInstance(scene.objects.size(), -1);
    std::vector<uint32_t> instMaterial;

    for (const Material& m : scene.materials) {
        gpu::GpuMaterial g = {};
        g.baseColor = m.baseColor;
        g.roughness = m.roughness;
        g.emission = m.emission * m.emissionStrength;
        g.metallic = m.metallic;
        g.sssRadius = m.sssRadius;
        g.subsurface = m.subsurface;
        g.transmission = m.transmission;
        g.ior = m.ior;
        g.bevelRadius = m.bevelRadius;
        g.flags = m.flags;
        g.specular = m.specular;
        g.coat = m.coat;
        g.coatRoughness = m.coatRoughness;
        g.baseColorTex = NO_TEXTURE;
        if (m.baseColorTex >= 0 && m.baseColorTex < (int)textureValid.size() && textureValid[m.baseColorTex])
            g.baseColorTex = (uint32_t)m.baseColorTex;
        mats.push_back(g);
    }
    gpu::GpuMaterial none = {};
    none.baseColorTex = NO_TEXTURE;
    if (mats.empty()) mats.push_back(none);

    // Same instance numbering as the DirectX 12 renderer (ids of the selection pass).
    auto addInstance = [&](int meshIdx, const Affine& xf, uint32_t matId, bool light, bool lightVisible) -> int {
        if (meshIdx < 0 || meshIdx >= (int)geo.meshes.size() || geo.meshes[meshIdx].indexCount == 0) return -1;
        instMaterial.push_back(matId);
        uint32_t flags = light ? (lightVisible ? 2u : 6u) : 0u;
        out.draws.push_back({meshIdx, xf, matId, (uint32_t)instMaterial.size(), flags, 1.0f});
        return (int)instMaterial.size() - 1;
    };

    for (size_t i = 0; i < scene.objects.size(); ++i) {
        const Object& o = scene.objects[i];
        if (!o.visible) continue;
        if (o.ghost) {
            uint32_t gm = (o.material >= 0 && o.material < (int)scene.materials.size()) ? o.material : 0;
            if (o.mesh >= 0 && o.mesh < (int)geo.meshes.size()) out.draws.push_back({o.mesh, o.transform(), gm, 0u, 1u, o.ghostAlpha});
            continue;
        }
        uint32_t matId = (o.material >= 0 && o.material < (int)scene.materials.size()) ? o.material : 0;
        objectToInstance[i] = addInstance(o.mesh, o.transform(), matId, false, false);
    }
    for (const AreaLight& l : scene.lights) {
        if (!l.enabled) continue;
        float3 U, V, N;
        l.basis(U, V, N);
        gpu::GpuMaterial lm = {};
        lm.baseColorTex = NO_TEXTURE;
        lm.emission = l.color * l.intensity;
        lm.roughness = 1.0f;
        lm.ior = 1.5f;
        uint32_t matId = (uint32_t)mats.size();
        mats.push_back(lm);
        Affine xf = Affine::fromBasis(U * l.width, V * l.height, N, l.position);
        int inst = addInstance(geo.quadMesh, xf, matId, true, l.visibleToCamera);
        gpu::GpuLight gl = {};
        gl.corner = l.position - U * (l.width * 0.5f) - V * (l.height * 0.5f);
        gl.edgeU = U * l.width;
        gl.edgeV = V * l.height;
        gl.area = l.width * l.height;
        gl.radiance = l.color * l.intensity;
        gl.instance = (uint32_t)inst;
        lights.push_back(gl);
    }
    // The sun as a small, far area light (as in the DirectX 12 renderer, which builds the same list for both modes).
    if (scene.sun.enabled && scene.sun.intensity > 0) {
        const float dist = 2000.0f, size = 20.0f;
        AreaLight sl;
        sl.position = normalize(scene.sun.direction) * dist;
        sl.target = float3(0, 0, 0);
        float3 U, V, N;
        sl.basis(U, V, N);
        float3 radiance = scene.sun.color * (scene.sun.intensity * dist * dist / (size * size));
        gpu::GpuMaterial lm = {};
        lm.baseColorTex = NO_TEXTURE;
        lm.emission = radiance;
        lm.roughness = 1.0f;
        lm.ior = 1.5f;
        uint32_t matId = (uint32_t)mats.size();
        mats.push_back(lm);
        Affine xf = Affine::fromBasis(U * size, V * size, N, sl.position);
        int inst = addInstance(geo.quadMesh, xf, matId, true, false);
        if (inst >= 0) {
            gpu::GpuLight gl = {};
            gl.corner = sl.position - U * (size * 0.5f) - V * (size * 0.5f);
            gl.edgeU = U * size;
            gl.edgeV = V * size;
            gl.area = size * size;
            gl.radiance = radiance;
            gl.instance = (uint32_t)inst;
            lights.push_back(gl);
        }
    }
    // Emissive objects are sphere lights.
    for (size_t i = 0; i < scene.objects.size(); ++i) {
        int inst = objectToInstance[i];
        if (inst < 0) continue;
        const Object& o = scene.objects[i];
        const Material& m = scene.materials[instMaterial[inst]];
        float3 e = m.emission * m.emissionStrength;
        if (std::max(e.x, std::max(e.y, e.z)) <= 0.0f || m.transmission > 0.5f) continue;
        const MeshInfo& g = geo.meshes[o.mesh];
        Affine xf = o.transform();
        float sc = 0;
        for (int c = 0; c < 3; ++c) sc = std::max(sc, length(float3(xf.m[0][c], xf.m[1][c], xf.m[2][c])));
        gpu::GpuLight gl = {};
        gl.type = LIGHT_OBJECT;
        gl.corner = xf.point(g.center);
        gl.area = std::max(g.radius * sc, 1e-4f);
        gl.radiance = e;
        gl.instance = (uint32_t)inst;
        lights.push_back(gl);
    }
    out.lightCount = (uint32_t)lights.size();
    if (lights.empty()) lights.push_back(gpu::GpuLight{});

    // Scene bounds (shadow map), ignoring huge ground planes.
    float3 mn(1e30f), mx(-1e30f);
    for (const Draw& d : out.draws) {
        const MeshInfo& g = geo.meshes[d.mesh];
        if (length(g.bmax - g.bmin) > 150.0f || (d.flags & 2u)) continue;
        for (int k = 0; k < 8; ++k) {
            float3 p((k & 1) ? g.bmax.x : g.bmin.x, (k & 2) ? g.bmax.y : g.bmin.y, (k & 4) ? g.bmax.z : g.bmin.z);
            p = d.xf.point(p);
            mn = minv(mn, p);
            mx = maxv(mx, p);
        }
    }
    if (mn.x > mx.x) { mn = float3(-10); mx = float3(10); }
    out.center = (mn + mx) * 0.5f;
    out.radius = std::max(length(mx - mn) * 0.5f, 1.0f);
}

void buildTextureMips(const Texture& t, MipChain& out) {
    out = MipChain();
    if (t.width == 0 || t.height == 0 || t.rgba.size() < (size_t)t.width * t.height * 4) return;
    out.levels.push_back(t.rgba);
    out.w.push_back(t.width);
    out.h.push_back(t.height);
    while (out.w.back() > 1 || out.h.back() > 1) {
        uint32_t w0 = out.w.back(), h0 = out.h.back(), w1 = std::max(1u, w0 / 2), h1 = std::max(1u, h0 / 2);
        const std::vector<uint8_t>& s = out.levels.back();
        std::vector<uint8_t> d((size_t)w1 * h1 * 4);
        for (uint32_t y = 0; y < h1; ++y)
            for (uint32_t x = 0; x < w1; ++x) {
                uint32_t x0 = std::min(x * 2, w0 - 1), x1 = std::min(x * 2 + 1, w0 - 1);
                uint32_t y0 = std::min(y * 2, h0 - 1), y1 = std::min(y * 2 + 1, h0 - 1);
                for (int c = 0; c < 4; ++c) {
                    uint32_t sum = s[((size_t)y0 * w0 + x0) * 4 + c] + s[((size_t)y0 * w0 + x1) * 4 + c] +
                                   s[((size_t)y1 * w0 + x0) * 4 + c] + s[((size_t)y1 * w0 + x1) * 4 + c];
                    d[((size_t)y * w1 + x) * 4 + c] = (uint8_t)((sum + 2) / 4);
                }
            }
        out.levels.push_back(std::move(d));
        out.w.push_back(w1);
        out.h.push_back(h1);
    }
}

bool prepareEnvironment(const EnvImage& img, Environment& out) {
    out = Environment();
    if (img.width < 2 || img.height < 1 || img.rgb.size() < (size_t)img.width * img.height * 3) return false;
    out.image = &img;

    // mip chain (RGB float), level 0 = the image
    out.mips.push_back(img.rgb);
    out.mipW.push_back(img.width);
    out.mipH.push_back(img.height);
    while (out.mipW.back() > 1 || out.mipH.back() > 1) {
        const std::vector<float>& s = out.mips.back();
        uint32_t sw = out.mipW.back(), shh = out.mipH.back();
        uint32_t dw = std::max(1u, sw / 2), dh = std::max(1u, shh / 2);
        std::vector<float> d((size_t)dw * dh * 3);
        for (uint32_t y = 0; y < dh; ++y)
            for (uint32_t x = 0; x < dw; ++x) {
                uint32_t x0 = std::min(x * 2, sw - 1), x1 = std::min(x * 2 + 1, sw - 1);
                uint32_t y0 = std::min(y * 2, shh - 1), y1 = std::min(y * 2 + 1, shh - 1);
                for (int c = 0; c < 3; ++c)
                    d[((size_t)y * dw + x) * 3 + c] =
                        0.25f * (s[((size_t)y0 * sw + x0) * 3 + c] + s[((size_t)y0 * sw + x1) * 3 + c] +
                                 s[((size_t)y1 * sw + x0) * 3 + c] + s[((size_t)y1 * sw + x1) * 3 + c]);
            }
        out.mips.push_back(std::move(d));
        out.mipW.push_back(dw);
        out.mipH.push_back(dh);
    }

    // Sun and SH on the first level at most 2048 wide (the level of the path tracer's sampling tables).
    size_t li = 0;
    while (out.mipW[li] > 2048 && li + 1 < out.mips.size()) ++li;
    const std::vector<float>& S = out.mips[li];
    const uint32_t W = out.mipW[li], H = out.mipH[li];

    // Sun of the image: the brightest texel, if it stands well above the mean, and the bright texels around it
    // (6 degrees). The raster pass uses it as its key light (with the shadow map) and leaves it out of the SH.
    uint32_t bx = 0, by = 0;
    float peak = 0;
    double meanL = 0;
    for (uint32_t y = 0; y < H; ++y)
        for (uint32_t x = 0; x < W; ++x) {
            const float* p = &S[((size_t)y * W + x) * 3];
            float l = lum(float3(p[0], p[1], p[2]));
            meanL += l;
            if (l > peak) { peak = l; bx = x; by = y; }
        }
    meanL /= (double)W * H;
    float3 sunDir = latLongDir((bx + 0.5f) / W, (by + 0.5f) / H);
    std::vector<uint8_t> isSun((size_t)W * H, 0);
    float3 sunE(0.0f), sunAxis(0.0f);
    const float dOmega0 = (2.0f * kPi / W) * (kPi / H);
    if (peak > 30.0f * (float)meanL) {
        const float cosCone = std::cos(radians(6.0f));
        for (uint32_t y = 0; y < H; ++y) {
            float st = std::sin((y + 0.5f) / H * kPi);
            for (uint32_t x = 0; x < W; ++x) {
                float3 dir = latLongDir((x + 0.5f) / W, (y + 0.5f) / H);
                if (dot(dir, sunDir) < cosCone) continue;
                const float* p = &S[((size_t)y * W + x) * 3];
                float3 c(p[0], p[1], p[2]);
                if (lum(c) < 0.05f * peak) continue;
                isSun[(size_t)y * W + x] = 1;
                sunE += c * (dOmega0 * st);
                sunAxis += dir * (lum(c) * dOmega0 * st);
            }
        }
        out.sunValid = lum(sunE) > 0.05f;
        out.sunDir = normalize(sunAxis);
        out.sunE = sunE;
    }

    // Irradiance SH (cosine lobe convolution: pi, 2pi/3, pi/4 per band), with and without the sun.
    double sh[9][3] = {}, shSun[9][3] = {};
    for (uint32_t y = 0; y < H; ++y) {
        float st = std::sin((y + 0.5f) / H * kPi);
        for (uint32_t x = 0; x < W; ++x) {
            float3 dir = latLongDir((x + 0.5f) / W, (y + 0.5f) / H);
            const float* p = &S[((size_t)y * W + x) * 3];
            float b[9];
            shBasis(dir, b);
            double w = dOmega0 * st;
            bool sunTexel = out.sunValid && isSun[(size_t)y * W + x];
            for (int k = 0; k < 9; ++k)
                for (int c = 0; c < 3; ++c) (sunTexel ? shSun : sh)[k][c] += p[c] * b[k] * w;
        }
    }
    const float band[9] = {kPi, 2 * kPi / 3, 2 * kPi / 3, 2 * kPi / 3, kPi / 4, kPi / 4, kPi / 4, kPi / 4, kPi / 4};
    for (int k = 0; k < 9; ++k)
        for (int c = 0; c < 3; ++c) {
            out.sh[k][c] = (float)(sh[k][c] * band[k]);
            out.shFull[k][c] = (float)((sh[k][c] + shSun[k][c]) * band[k]);
        }
    return true;
}

DrawConstants drawConstants(const Draw& d) {
    DrawConstants dc = {};
    memcpy(dc.rows, d.xf.m, sizeof(dc.rows));
    dc.materialId = d.material;
    dc.id = d.id;
    dc.alpha = d.alpha;
    dc.flags = d.flags;
    return dc;
}

void computeFrame(const Scene& scene, const SceneData& data, const Environment* env, const FrameSettings& fs,
                  int viewW, int viewH, RasterConstants& rc, bool& castShadows) {
    const Camera& cam = scene.camera;
    float3 r, u, f;
    cam.basis(r, u, f);
    float3 eye = cam.eye();
    float t = std::tan(cam.fovY * 0.5f), a = (float)viewW / std::max(viewH, 1);
    float zn = std::max(cam.distance * 0.005f, 0.02f), zf = cam.distance * 40.0f + 2000.0f;
    float A = zf / (zf - zn), B = -zf * zn / (zf - zn);

    rc = RasterConstants();
    setRows(rc.viewProj, 0, r / (t * a), -dot(r, eye) / (t * a));
    setRows(rc.viewProj, 1, u / t, -dot(u, eye) / t);
    setRows(rc.viewProj, 2, f * A, -dot(f, eye) * A + B);
    setRows(rc.viewProj, 3, f, -dot(f, eye));

    // Key light = the sun when enabled, else the strongest enabled area light; it gets the shadow map.
    int key = -1;
    float best = 0;
    int li = 0;
    float3 keyCenter(0, 50, 0);
    float3 c = data.center;
    float R = data.radius * 1.05f;
    bool sun = scene.sun.enabled && scene.sun.intensity > 0;
    // HDRI: without a scene sun, the sun of the image is the key light (shadow map); the rest of the image is
    // the SH ambient. Its highlight already is in the blurred reflections, so this key has no specular term.
    const bool envMap = scene.env.hdri && env && env->valid() && env->image == scene.env.hdri.get();
    const bool envSun = envMap && !sun && env->sunValid && fs.envSun;
    float3 envSunDir = envMap ? env->sunDir : float3(0, 1, 0);
    {
        float cr = std::cos(scene.env.hdriRotation), sr = std::sin(scene.env.hdriRotation);   // image space to world
        envSunDir = float3(envSunDir.x * cr - envSunDir.z * sr, envSunDir.y, envSunDir.z * cr + envSunDir.x * sr);
    }
    if (sun || envSun) {
        keyCenter = c + (envSun ? envSunDir : normalize(scene.sun.direction)) * (R * 3.0f);
    } else {
        for (const AreaLight& l : scene.lights) {
            if (!l.enabled) continue;
            float pw = (l.color.x + l.color.y + l.color.z) * l.intensity * l.width * l.height;
            if (pw > best) { best = pw; key = li; keyCenter = l.position; }
            ++li;
        }
    }
    float3 lz = normalize(c - keyCenter);
    float3 ref = std::fabs(lz.y) > 0.99f ? float3(1, 0, 0) : float3(0, 1, 0);
    float3 lx = normalize(cross(ref, lz)), ly = cross(lz, lx);
    setRows(rc.lightViewProj, 0, lx / R, -dot(lx, c) / R);
    setRows(rc.lightViewProj, 1, ly / R, -dot(ly, c) / R);
    setRows(rc.lightViewProj, 2, lz / (2 * R), (-dot(lz, c) + R) / (2 * R));
    setRows(rc.lightViewProj, 3, float3(0, 0, 0), 1);

    rc.camPos = eye;
    rc.camRight = r;
    rc.camUp = u;
    rc.camFwd = f;
    rc.tanHalfFovY = t;
    rc.aspect = a;
    rc.lightCount = data.lightCount;
    rc.envTop = scene.env.top;
    rc.envHorizon = scene.env.horizon;
    rc.envBottom = scene.env.bottom;
    rc.envIntensity = scene.env.intensity;
    rc.gridScale = scene.gridScale;
    rc.keyLight = key < 0 ? 0xFFFFFFFFu : (uint32_t)key;
    rc.ambient = scene.env.ambient;
    rc.shadowTexel = 1.0f / fs.shadowSize;
    rc.keyPos = keyCenter;
    rc.lightScale = fs.lightScale;
    rc.sunDir = envSun ? envSunDir : normalize(scene.sun.direction);
    rc.sunOn = (sun || envSun) ? 1.0f : 0.0f;
    rc.sunRadiance = envSun ? env->sunE * scene.env.hdriIntensity : scene.sun.color * scene.sun.intensity;
    rc.sunFromEnv = envSun ? 1.0f : 0.0f;
    if (envMap)
        for (int k = 0; k < 9; ++k)
            for (int ch = 0; ch < 3; ++ch) rc.shIrr[k][ch] = envSun ? env->sh[k][ch] : env->shFull[k][ch];
    rc.envMapOn = envMap ? 1.0f : 0.0f;
    rc.envRotation = scene.env.hdriRotation;
    rc.envMapIntensity = scene.env.hdriIntensity;
    rc.envMips = envMap ? (float)env->mips.size() : 1.0f;
    rc.envVisible = scene.env.visibleToCamera ? 1.0f : 0.0f;
    castShadows = key >= 0 || sun || envSun;
}

} // namespace lit
} // namespace vp3d
