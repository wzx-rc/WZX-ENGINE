// BrickIt - part thumbnails: every part rendered once by the engine (raster, same studio lighting and plastic as the
// viewport), packed in atlases of 21 x 21 cells and cached on disk (cache/thumbs_<px>_<count>.bin next to the exe).
#include "BrickApp.h"
#include "vp3d/AssetImport.h"
#include "vp3d/shaders/Shared.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace brickit {

static const uint32_t kThumbMagic = 0x42544831;   // "BTH1"

void BrickApp::buildThumbs() {
    const int px = m_thumbPx, cols = m_thumbCols, perAtlas = cols * cols;
    const int atlasPx = px * cols;
    const int n = (int)m_lib.parts.size();
    m_thumbSlot.assign(n, -1);
    int slots = 0;
    for (int i = 0; i < n; ++i)
        if (m_lib.parts[i].loaded) m_thumbSlot[i] = slots++;
    const int atlases = (slots + perAtlas - 1) / perAtlas;
    std::vector<std::vector<uint8_t>> pix(atlases, std::vector<uint8_t>((size_t)atlasPx * atlasPx * 4, 0));

    wchar_t name[128];
    swprintf_s(name, L"\\cache\\thumbs6_%d_%d.bin", px, slots);   // 5: transparent background (alpha)   // 2: neutral grey studio of the web
    const std::wstring cacheDir = m_exeDir + L"\\cache", cacheFile = m_exeDir + name;
    bool cached = false;
    FILE* f = nullptr;
    if (_wfopen_s(&f, cacheFile.c_str(), L"rb") == 0 && f) {
        uint32_t magic = 0, count = 0;
        if (fread(&magic, 4, 1, f) == 1 && fread(&count, 4, 1, f) == 1 && magic == kThumbMagic && (int)count == slots) {
            cached = true;
            for (auto& a : pix) cached &= fread(a.data(), 1, a.size(), f) == a.size();
        }
        fclose(f);
    }

    if (!cached) {
        // one scene with every part (all hidden but the one rendered), uploaded once
        Scene ts;
        ts.unitsPerMeter = 100.0f;
        ts.env = m_scene.env;
        {
            auto img = std::make_shared<EnvImage>();
            std::wstring hdr = m_exeDir + L"\\assets\\hdri\\studio_small_09_2k.hdr";
            std::string err;
            if (!loadEnvImage(hdr, *img, &err)) makeProceduralSky(*img);
            ts.env.hdri = img;
        }
        ts.env.hdriIntensity = 0.75f;   // fill; the key and the rim do the shape
        ts.env.visibleToCamera = false;
        ts.objectsVersion = 1;
        ts.env.top = float3(0.93f, 0.94f, 0.96f);   // light card background
        ts.env.horizon = float3(0.96f, 0.96f, 0.97f);
        ts.env.bottom = float3(0.88f, 0.89f, 0.91f);
        // warm key light and cool rim light (the studio of the web thumbnails)
        AreaLight key;
        key.position = float3(30, 45, 40);
        key.width = key.height = 25;
        key.color = float3(1.0f, 0.93f, 0.84f);
        key.intensity = 7.5f;
        key.visibleToCamera = false;
        ts.lights.push_back(key);
        AreaLight rim;
        rim.position = float3(-35, 25, -40);
        rim.width = rim.height = 20;
        rim.color = float3(0.78f, 0.86f, 1.0f);
        rim.intensity = 5.0f;
        rim.visibleToCamera = false;
        ts.lights.push_back(rim);
        Material m;
        m.baseColor = float3(srgbToLinear(0xc4 / 255.0f), srgbToLinear(0xc4 / 255.0f), srgbToLinear(0xc1 / 255.0f));   // neutral grey
        m.bevelRadius = 0.04f;
        m.roughness = 0.24f;
        m.coat = 0.35f;
        m.coatRoughness = 0.05f;
        const int mat = ts.addMaterial(m);
        std::vector<int> objOf(n, -1);
        for (int i = 0; i < n; ++i) {
            if (m_thumbSlot[i] < 0) continue;
            Object o;
            o.mesh = ts.addMesh(m_lib.parts[i].name, m_lib.parts[i].mesh);
            o.material = mat;
            o.visible = false;
            objOf[i] = ts.addObject(o);
        }
        ts.geometryVersion = 0x40000000u;   // never equal to the viewport scene's
        RenderSettings rs;
        rs.raster = true;
        rs.exposure = 0.92f;
        rs.tonemapper = 0;   // ACES
        rs.idPass = false;
        rs.vignette = 0.0f;
        rs.msaaSamples = 4;
        ViewRect vp{0, 0, px, px};
        for (int i = 0; i < n; ++i) {
            if (objOf[i] < 0) continue;
            const Part& p = m_lib.parts[i];
            ts.objects[objOf[i]].visible = true;
            Camera& c = ts.camera;
            c.fovY = radians(24.0f);
            c.yaw = std::atan2(1.0f, 1.3f);                       // view direction (1, 0.8, 1.3)
            c.pitch = std::atan2(0.8f, std::sqrt(1.0f + 1.69f));
            c.target = (p.bmin + p.bmax) * 0.5f;
            float r = std::max(length(p.bmax - p.bmin) * 0.5f, 0.2f);
            c.distance = r / std::sin(c.fovY * 0.5f) * 1.4f;   // margin: the cards zoom the image x1.4 (as the web)
            // the part over a black then a white background: alpha = 1 - (white - black), colour = black / alpha
            std::vector<uint8_t> img, imgW;
            uint32_t w = 0, h = 0, w2 = 0, h2 = 0;
            ts.env.top = ts.env.horizon = ts.env.bottom = float3(0);
            ++ts.objectsVersion;
            m_renderer.render(ts, rs, vp, -1, false);
            bool ok = m_renderer.readViewport(img, w, h);
            ts.env.top = ts.env.horizon = ts.env.bottom = float3(1000);
            ++ts.objectsVersion;
            m_renderer.render(ts, rs, vp, -1, false);
            ok = ok && m_renderer.readViewport(imgW, w2, h2);
            ts.objects[objOf[i]].visible = false;
            if (!ok || (int)w != px || (int)h != px || imgW.size() != img.size()) continue;
            for (size_t q = 0; q + 3 < img.size(); q += 4) {
                int diff = 0;
                for (int ch = 0; ch < 3; ++ch) diff = std::max(diff, (int)imgW[q + ch] - (int)img[q + ch]);
                int a = 255 - std::clamp(diff, 0, 255);
                for (int ch = 0; ch < 3; ++ch) img[q + ch] = a > 0 ? (uint8_t)std::min(255, img[q + ch] * 255 / a) : 0;
                img[q + 3] = (uint8_t)a;
            }
            const int s = m_thumbSlot[i], a = s / perAtlas, cell = s % perAtlas;
            const int ox = (cell % cols) * px, oy = (cell / cols) * px;
            for (int y = 0; y < px; ++y)
                memcpy(&pix[a][((size_t)(oy + y) * atlasPx + ox) * 4], &img[(size_t)y * px * 4], (size_t)px * 4);
        }
        CreateDirectoryW(cacheDir.c_str(), nullptr);
        if (_wfopen_s(&f, cacheFile.c_str(), L"wb") == 0 && f) {
            uint32_t hdr[2] = {kThumbMagic, (uint32_t)slots};
            fwrite(hdr, 4, 2, f);
            for (auto& a : pix) fwrite(a.data(), 1, a.size(), f);
            fclose(f);
        }
    }
    for (auto& a : pix) m_thumbAtlas.push_back(m_gui.createTexture(a.data(), atlasPx, atlasPx));
}

bool BrickApp::thumbUv(int part, ImTextureID& tex, ImVec2& uv0, ImVec2& uv1) const {
    if (part < 0 || part >= (int)m_thumbSlot.size() || m_thumbSlot[part] < 0) return false;
    const int perAtlas = m_thumbCols * m_thumbCols, s = m_thumbSlot[part];
    const int a = s / perAtlas, cell = s % perAtlas;
    if (a >= (int)m_thumbAtlas.size() || !m_thumbAtlas[a]) return false;
    tex = m_thumbAtlas[a];
    const float k = 1.0f / m_thumbCols;
    uv0 = ImVec2((cell % m_thumbCols) * k, (cell / m_thumbCols) * k);
    uv1 = ImVec2(uv0.x + k, uv0.y + k);
    return true;
}

} // namespace brickit
