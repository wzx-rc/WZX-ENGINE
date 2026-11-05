// BrickIt - part thumbnails: every part rendered once by the engine (raster, same studio lighting and plastic as the
// viewport), packed in atlases of 21 x 21 cells and cached on disk (cache/thumbs_<px>_<count>.bin next to the exe).
#include "BrickApp.h"
#include <wincodec.h>
#include <wrl/client.h>
#include "vp3d/AssetImport.h"
#include "vp3d/shaders/Shared.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace brickit {

static const uint32_t kThumbMagic = 0x42544831;   // "BTH1"

// The studio of the web thumbnails (outils/thumbs.mjs), also used by the 3D viewer of the part sheet: HDRI fill, warm
// key and cool rim area lights, neutral grey plastic with a bevel. Returns the material.
int BrickApp::setupStudio(Scene& ts) {
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
    m.flags |= MAT_FLAG_VCOLOR;   // the prints
    m.roughness = 0.24f;
    m.coat = 0.35f;
    m.coatRoughness = 0.05f;
    return ts.addMaterial(m);
}

// PNG (or any WIC format) in memory -> RGBA8.
bool BrickApp::decodeImage(const uint8_t* data, size_t size, std::vector<uint8_t>& rgba, int& w, int& h) {
    using Microsoft::WRL::ComPtr;
    static ComPtr<IWICImagingFactory> fac;
    if (!fac) {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fac)))) return false;
    }
    ComPtr<IWICStream> st;
    ComPtr<IWICBitmapDecoder> dec;
    ComPtr<IWICBitmapFrameDecode> fr;
    ComPtr<IWICFormatConverter> cv;
    if (FAILED(fac->CreateStream(&st)) || FAILED(st->InitializeFromMemory((BYTE*)data, (DWORD)size))) return false;
    if (FAILED(fac->CreateDecoderFromStream(st.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &dec)) || FAILED(dec->GetFrame(0, &fr))) return false;
    if (FAILED(fac->CreateFormatConverter(&cv)) || FAILED(cv->Initialize(fr.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return false;
    UINT uw = 0, uh = 0;
    cv->GetSize(&uw, &uh);
    w = (int)uw;
    h = (int)uh;
    rgba.resize((size_t)uw * uh * 4);
    return SUCCEEDED(cv->CopyPixels(nullptr, uw * 4, (UINT)rgba.size(), rgba.data()));
}

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
    const bool v3 = GetFileAttributesW((m_exeDir + L"\\assets\\brickit\\thumbs_v3.pack").c_str()) != INVALID_FILE_ATTRIBUTES;   // the thumbnails v3 (--build-full-thumbs)
    swprintf_s(name, v3 ? L"\\cache\\thumbs8v3_%d_%d.bin" : L"\\cache\\thumbs7web_%d_%d.bin", px, slots);   // 7: the thumbnails of the web (thumbs_web.pack)   // 5: transparent background (alpha)   // 2: neutral grey studio of the web
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
        // 1) the very images of the web (assets/brickit/thumbs_web.pack, tools/brickit/extract_web_thumbs.py)
        std::vector<bool> done(n, false);
        {
            std::vector<uint8_t> pack;
            FILE* pf = nullptr;
            if (_wfopen_s(&pf, (m_exeDir + (v3 ? L"\\assets\\brickit\\thumbs_v3.pack" : L"\\assets\\brickit\\thumbs_web.pack")).c_str(), L"rb") == 0 && pf) {
                fseek(pf, 0, SEEK_END);
                pack.resize((size_t)ftell(pf));
                fseek(pf, 0, SEEK_SET);
                if (fread(pack.data(), 1, pack.size(), pf) != pack.size()) pack.clear();
                fclose(pf);
            }
            size_t o = 8;
            const uint32_t count = pack.size() >= 8 && memcmp(pack.data(), "BTW1", 4) == 0 ? *(const uint32_t*)&pack[4] : 0;
            for (uint32_t e = 0; e < count && o + 2 <= pack.size(); ++e) {
                const uint16_t nl = *(const uint16_t*)&pack[o];
                o += 2;
                if (o + nl + 4 > pack.size()) break;
                const std::string pname((const char*)&pack[o], nl);
                o += nl;
                const uint32_t pl = *(const uint32_t*)&pack[o];
                o += 4;
                if (o + pl > pack.size()) break;
                const int i = m_lib.find(pname);
                std::vector<uint8_t> rgba;
                int w = 0, h = 0;
                if (i >= 0 && m_thumbSlot[i] >= 0 && decodeImage(&pack[o], pl, rgba, w, h) && w > 0 && h > 0) {
                    // object-fit: contain in the square cell (240 x 192 -> px x 0.8 px, centred), box filter
                    const float k = std::min((float)px / w, (float)px / h);
                    const int dw = std::max(1, (int)std::lround(w * k)), dh = std::max(1, (int)std::lround(h * k));
                    const int s = m_thumbSlot[i], a = s / perAtlas, cell = s % perAtlas;
                    const int ox = (cell % cols) * px + (px - dw) / 2, oy = (cell / cols) * px + (px - dh) / 2;
                    for (int y = 0; y < dh; ++y)
                        for (int x = 0; x < dw; ++x) {
                            const int x0 = x * w / dw, x1 = std::max(x0 + 1, (x + 1) * w / dw), y0 = y * h / dh, y1 = std::max(y0 + 1, (y + 1) * h / dh);
                            float acc[4] = {0, 0, 0, 0}, cnt = 0;
                            for (int yy = y0; yy < y1; ++yy)
                                for (int xx = x0; xx < x1; ++xx) {
                                    const uint8_t* q = &rgba[((size_t)yy * w + xx) * 4];
                                    const float al = q[3] / 255.0f;
                                    for (int ch = 0; ch < 3; ++ch) acc[ch] += q[ch] * al;   // premultiplied while filtering
                                    acc[3] += al;
                                    ++cnt;
                                }
                            uint8_t* d = &pix[a][((size_t)(oy + y) * atlasPx + ox + x) * 4];
                            for (int ch = 0; ch < 3; ++ch) d[ch] = acc[3] > 0 ? (uint8_t)std::min(255.0f, acc[ch] / acc[3] + 0.5f) : 0;
                            d[3] = (uint8_t)std::min(255.0f, acc[3] / cnt * 255 + 0.5f);
                        }
                    done[i] = true;
                }
                o += pl;
            }
        }
        // 2) the parts the web has no image for: rendered in the studio of the web thumbnails
        // one scene with every part (all hidden but the one rendered), uploaded once
        Scene ts;
        const int mat = setupStudio(ts);
        std::vector<int> objOf(n, -1);
        for (int i = 0; i < n; ++i) {
            if (m_thumbSlot[i] < 0 || done[i]) continue;
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

bool BrickApp::thumbUv(int part, ImTextureID& tex, ImVec2& uv0, ImVec2& uv1) {
    auto ov = m_thumbOverride.find(part);   // Edit thumbnail (DEV): the saved image first
    if (ov != m_thumbOverride.end()) { tex = ov->second; uv0 = ImVec2(0, 0); uv1 = ImVec2(1, 1); return true; }
    if (part < 0 || part >= (int)m_thumbSlot.size() || m_thumbSlot[part] < 0) {   // the full library: on demand
        if (!lazyThumb(part, tex)) return false;
        uv0 = ImVec2(0, 0);
        uv1 = ImVec2(1, 1);
        return true;
    }
    const int perAtlas = m_thumbCols * m_thumbCols, s = m_thumbSlot[part];
    const int a = s / perAtlas, cell = s % perAtlas;
    if (a >= (int)m_thumbAtlas.size() || !m_thumbAtlas[a]) return false;
    tex = m_thumbAtlas[a];
    const float k = 1.0f / m_thumbCols;
    uv0 = ImVec2((cell % m_thumbCols) * k, (cell / m_thumbCols) * k);
    uv1 = ImVec2(uv0.x + k, uv0.y + k);
    return true;
}

// ------------------------------------------------------------------------------------------------- full library
// The thumbnails of the whole LDraw library (21 000 parts): rendered once by --build-full-thumbs into
// assets/brickit/full/thumbs.pack (PNG per part, "BTF1"), then decoded on demand, a few per frame, for the cards on
// screen (one texture each, the least recently used released beyond 900).

bool encodePng(const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h, std::vector<uint8_t>& out) {
    ComPtr<IWICImagingFactory> f;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) return false;
    ComPtr<IStream> mem;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &mem))) return false;
    ComPtr<IWICBitmapEncoder> enc;
    f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
    if (!enc || FAILED(enc->Initialize(mem.Get(), WICBitmapEncoderNoCache))) return false;
    ComPtr<IWICBitmapFrameEncode> fr;
    ComPtr<IPropertyBag2> props;
    enc->CreateNewFrame(&fr, &props);
    fr->Initialize(props.Get());
    fr->SetSize(w, h);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppRGBA;
    fr->SetPixelFormat(&fmt);
    std::vector<uint8_t> px = rgba;
    if (fmt != GUID_WICPixelFormat32bppRGBA)
        for (size_t i = 0; i + 3 < px.size(); i += 4) std::swap(px[i], px[i + 2]);
    fr->WritePixels(h, w * 4, (UINT)px.size(), px.data());
    fr->Commit();
    enc->Commit();
    STATSTG st = {};
    mem->Stat(&st, STATFLAG_NONAME);
    out.resize((size_t)st.cbSize.QuadPart);
    LARGE_INTEGER zero = {};
    mem->Seek(zero, STREAM_SEEK_SET, nullptr);
    ULONG got = 0;
    mem->Read(out.data(), (ULONG)out.size(), &got);
    return got == out.size();
}

// ------------------------------------------------------------------------------------------------- thumbnails v2
// Wzx (05/11/2026): the same look for every part: the grey of the studio (glass parts stay glass, prints kept), a
// consistent 3/4 view, the part framed close (its box fills ~85 % of the image), key + fill + rim lights and a soft
// contact shadow under it, on a transparent background. A view can be given (Edit thumbnail, DEV).

bool isGlassPart(const Part& p) {
    std::string l = p.label;
    for (char& c : l) c = (char)std::tolower((unsigned char)c);
    for (const char* k : {"glass", "windscreen", "windshield", "canopy", "lens", "transparent"})
        if (l.find(k) != std::string::npos) return true;
    return false;
}

ThumbView BrickApp::defaultThumbView() { return ThumbView{std::atan2(1.0f, 1.3f), radians(27.0f), 1.0f, 0.0f, 0.0f}; }

int BrickApp::setupStudio2(Scene& ts, int& glassMat) {
    const int mat = setupStudio(ts);
    // the rig of the first thumbnails (web, outils/thumbs.mjs, Wzx 05/11/2026: « le lighting d'avant »): a warm key
    // from the front left and above, a cool rim behind, the room as fill; placed around each part (renderThumb2)
    ts.lights[0].color = float3(1.0f, 0.957f, 0.91f);   // 0xfff4e8
    ts.lights[1].color = float3(0.84f, 0.89f, 1.0f);    // 0xd6e4ff
    ts.env.hdriIntensity = thumbLook().env;
    Material g;
    g.name = "thumb glass";
    g.baseColor = float3(srgbToLinear(0.90f), srgbToLinear(0.93f), srgbToLinear(0.95f));
    g.transmission = 1.0f;
    g.roughness = 0.03f;
    g.specular = 0.5f;
    glassMat = ts.addMaterial(g);
    return mat;
}

// One part alone in ts (object obj, visible), rendered over black then white (alpha), with its contact shadow.
// the look of the thumbnails (tunable for the validation sheets: BT_THUMB="key rim env exposure bevel")
ThumbLook BrickApp::thumbLook() {
    ThumbLook l;
    char b[128];
    if (GetEnvironmentVariableA("BT_THUMB", b, sizeof(b)) > 0) sscanf_s(b, "%f %f %f %f %f %d %d", &l.key, &l.rim, &l.env, &l.exposure, &l.bevel, &l.spp, &l.bounces);
    return l;
}

RenderSettings BrickApp::thumbSettings() {
    RenderSettings rs;
    rs.raster = true;
    rs.exposure = thumbLook().exposure;
    rs.bevel = rs.rasterBevel = true;   // the rounded edges of the plastic (traced in the raster view)
    rs.bevelScale = 1.0f;
    rs.bevelSamples = 8;
    rs.rasterRT = true;                 // the key and the rim shadowed by rays (soft self shadows)
    rs.tonemapper = 0;
    rs.idPass = false;
    rs.vignette = 0.0f;
    rs.msaaSamples = 4;
    return rs;
}

// the camera of a thumbnail: the view's angles, the box of the part within 85 % of the frame, then zoom and pan
void BrickApp::fitThumbCamera(Camera& c, const Part& p, const ThumbView& v) {
    c.fovY = radians(20.0f);
    c.yaw = v.yaw;
    c.pitch = v.pitch;
    c.target = (p.bmin + p.bmax) * 0.5f;
    c.distance = 1.0f;
    float3 r, u, f;
    c.basis(r, u, f);
    const float t = std::tan(c.fovY * 0.5f) * 0.85f;
    float d = 0.5f;
    for (int k = 0; k < 8; ++k) {
        const float3 q((k & 1) ? p.bmax.x : p.bmin.x, (k & 2) ? p.bmax.y : p.bmin.y, (k & 4) ? p.bmax.z : p.bmin.z);
        const float3 o = q - c.target;
        const float z = dot(o, f);
        d = std::max(d, std::max(std::fabs(dot(o, r)), std::fabs(dot(o, u))) / t - z);
    }
    c.distance = d / std::max(v.zoom, 0.05f);
    const float ht = std::tan(c.fovY * 0.5f);
    c.target = c.target + r * (v.panX * c.distance * ht) + u * (v.panY * c.distance * ht);
    c.focusDistance = c.distance;
}

// the lights around the part (the web rig: key (-2.2, 4, 2.6) x radius, rim (3, 1.5, -3)), sizes in proportion
void BrickApp::placeThumbLights(Scene& ts, const Part& p) {
    const ThumbLook lk = thumbLook();
    const float3 ctr = (p.bmin + p.bmax) * 0.5f;
    const float rad = std::max(length(p.bmax - p.bmin) * 0.5f, 0.2f);
    ts.lights[0].position = ctr + float3(-2.2f, 4.0f, 2.6f) * rad;
    ts.lights[0].width = ts.lights[0].height = rad * 2.2f;
    ts.lights[0].intensity = lk.key;
    ts.lights[1].position = ctr + float3(3.0f, 1.5f, -3.0f) * rad;
    ts.lights[1].width = ts.lights[1].height = rad * 2.0f;
    ts.lights[1].intensity = lk.rim;
    for (AreaLight& l : ts.lights) l.target = ctr;
    for (size_t k = 2; k < ts.lights.size(); ++k) ts.lights[k].enabled = false;
    ts.env.hdriIntensity = lk.env;
    for (Material& m : ts.materials) if (m.transmission < 0.5f) m.bevelRadius = lk.bevel;
}

bool BrickApp::renderThumb2(Scene& ts, int obj, const Part& p, const ThumbView& v, int px, std::vector<uint8_t>& out) {
    const RenderSettings rs = thumbSettings();
    Camera& c = ts.camera;
    fitThumbCamera(c, p, v);
    placeThumbLights(ts, p);
    const ViewRect vp{0, 0, px, px};
    std::vector<uint8_t> img, imgW;
    uint32_t w = 0, h = 0, w2 = 0, h2 = 0;
    // path tracing (Wzx, 05/11/2026: « fait en rendu PathTracing »): the samples accumulated, denoised; else the raster
    ThumbLook lk = thumbLook();
    if (isGlassPart(p) || (obj >= 0 && obj < (int)ts.objects.size() && ts.materials[ts.objects[obj].material].transmission > 0.5f)) lk.spp = 0;   // (glass: the refraction of the black / white backgrounds breaks the alpha in path tracing)
    RenderSettings prs = rs;
    if (lk.spp > 0) {
        prs.raster = false;
        prs.maxBounces = lk.bounces;
        prs.samplesPerFrame = std::min(lk.spp, 16);
        prs.maxSamples = lk.spp;
        prs.traceStride = 1;
        prs.denoise = true;
        prs.denoisePasses = 3;
        prs.denoiseEdgeLuma = 4.0f;
        prs.fireflyClamp = 8.0f;
        prs.bevel = true;
        prs.bevelSamples = 4;
        prs.vignette = 0.0f;
    }
    auto pass = [&](float bg, std::vector<uint8_t>& out8, uint32_t& ow, uint32_t& oh) {
        ts.env.top = ts.env.horizon = ts.env.bottom = float3(bg);
        ++ts.objectsVersion;
        if (lk.spp <= 0) { m_renderer.render(ts, prs, vp, -1, false); return m_renderer.readViewport(out8, ow, oh); }
        m_renderer.resetAccumulation();
        for (int k = 0; k < 64 && (int)m_renderer.sampleCount() < lk.spp; ++k) m_renderer.render(ts, prs, vp, -1, false);
        return m_renderer.readViewport(out8, ow, oh);
    };
    bool ok = pass(0.0f, img, w, h);
    ok = ok && pass(1000.0f, imgW, w2, h2);
    if (!ok || (int)w != px || (int)h != px || imgW.size() != img.size()) return false;
    // contact shadow: a soft ellipse under the footprint of the box
    float ex0 = 1e9f, ex1 = -1e9f, ey0 = 1e9f, ey1 = -1e9f;
    for (int k = 0; k < 4; ++k) {
        const float3 q((k & 1) ? p.bmax.x : p.bmin.x, p.bmin.y, (k & 2) ? p.bmax.z : p.bmin.z);
        float2 ndc;
        float depth;
        if (!c.project(q, 1.0f, ndc, &depth)) continue;
        const float sx = (ndc.x * 0.5f + 0.5f) * px, sy = (0.5f - ndc.y * 0.5f) * px;
        ex0 = std::min(ex0, sx); ex1 = std::max(ex1, sx); ey0 = std::min(ey0, sy); ey1 = std::max(ey1, sy);
    }
    const float ecx = (ex0 + ex1) * 0.5f, ecy = (ey0 + ey1) * 0.5f + px * 0.01f;
    const float erx = std::max(2.0f, (ex1 - ex0) * 0.5f), ery = std::max(1.5f, (ey1 - ey0) * 0.5f);
    out.resize(img.size());
    for (int y = 0; y < px; ++y)
        for (int x = 0; x < px; ++x) {
            const size_t q = ((size_t)y * px + x) * 4;
            int diff = 0;
            for (int ch = 0; ch < 3; ++ch) diff = std::max(diff, (int)imgW[q + ch] - (int)img[q + ch]);
            const float a = 1.0f - std::clamp(diff, 0, 255) / 255.0f;
            const float dx = (x + 0.5f - ecx) / erx, dy = (y + 0.5f - ecy) / ery;
            const float sa = ex1 > ex0 ? 0.30f * std::exp(-(dx * dx + dy * dy) * 1.6f) : 0.0f;   // shadow (black)
            const float oa = a + sa * (1.0f - a);
            for (int ch = 0; ch < 3; ++ch) {
                const float col = a > 0 ? img[q + ch] / a : 0.0f;   // (un-premultiplied over black)
                out[q + ch] = (uint8_t)std::clamp(oa > 0 ? col * a / oa + 0.5f : 0.0f, 0.0f, 255.0f);
            }
            out[q + 3] = (uint8_t)std::clamp(oa * 255.0f + 0.5f, 0.0f, 255.0f);
        }
    return true;
}

static bool writeFile(const std::wstring& path, const std::vector<uint8_t>& data) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return false;
    const bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    fclose(f);
    return ok;
}

// --thumb-sample <dir> <part,part,...>: the new thumbnails of a few parts, 256 and 128 px PNG (validation sheet)
int BrickApp::thumbSample() {
    Scene ts;
    int glass = -1;
    const int mat = setupStudio2(ts, glass);
    ts.geometryVersion = 0x70000000u;
    std::vector<std::string> names;
    {
        std::string cur;
        for (char ch : m_opt.thumbSampleParts + ",") { if (ch == ',') { if (!cur.empty()) names.push_back(cur); cur.clear(); } else cur += ch; }
    }
    std::vector<std::pair<int, int>> objs;
    for (const std::string& n : names) {
        const int i = m_lib.find(n);
        if (i < 0 || !m_lib.get(m_lib.parts[i].name)) continue;
        Object o;
        o.mesh = ts.addMesh(m_lib.parts[i].name, m_lib.parts[i].mesh);
        o.material = isGlassPart(m_lib.parts[i]) ? glass : mat;
        o.visible = false;
        objs.push_back({i, ts.addObject(o)});
    }
    ++ts.geometryVersion;
    int count = 0;
    LARGE_INTEGER q0, q1, qf;
    QueryPerformanceFrequency(&qf);
    QueryPerformanceCounter(&q0);
    for (auto [i, ob] : objs) {
        ts.objects[ob].visible = true;
        for (int px : {256, 128}) {
            std::vector<uint8_t> rgba, png;
            if (!renderThumb2(ts, ob, m_lib.parts[i], defaultThumbView(), px, rgba) || !encodePng(rgba, px, px, png)) continue;
            const std::string& nm = m_lib.parts[i].name;
            writeFile(m_opt.thumbSampleDir + L"\\" + std::wstring(nm.begin(), nm.end()) + (px == 256 ? L"_256.png" : L"_128.png"), png);
            ++count;
        }
        ts.objects[ob].visible = false;
    }
    QueryPerformanceCounter(&q1);
    FILE* tf = nullptr;   // the time per part (both sizes), for the estimate of the whole library
    if (_wfopen_s(&tf, (m_opt.thumbSampleDir + L"\\timing.txt").c_str(), L"w") == 0 && tf) {
        fprintf(tf, "parts=%d images=%d total_s=%.2f per_part_ms=%.1f spp=%d\n", (int)objs.size(), count, (q1.QuadPart - q0.QuadPart) / (double)qf.QuadPart,
                objs.empty() ? 0.0 : (q1.QuadPart - q0.QuadPart) * 1000.0 / qf.QuadPart / objs.size(), thumbLook().spp);
        fclose(tf);
    }
    return count;
}

// --build-full-thumbs <pack>: the thumbnails v3 (Wzx, validated 05/11/2026: path traced, tight 3/4 framing, the light of
// the first thumbnails, bevels, contact shadow) of every part: the base and Technic parts into <pack dir>\thumbs_base_v3.pack
// ("BTW1", read by buildThumbs: copy it to assets\brickit\thumbs_v3.pack), the full library into <pack> ("BTF1"). Each
// image is padded to 1.4 x (the cards zoom the thumbnails x 1.4). Resumable: every image is first written into
// <pack dir>\thumbs_v3_parts\<id>.png and skipped when it is there; the packs are assembled at the end. Progress and
// failures in <pack>.txt.
static void padThumb(const std::vector<uint8_t>& rgba, int px, std::vector<uint8_t>& out, int& W) {
    W = (int)std::lround(px * 1.4f);
    const int o = (W - px) / 2;
    out.assign((size_t)W * W * 4, 0);
    for (int y = 0; y < px; ++y) memcpy(&out[((size_t)(y + o) * W + o) * 4], &rgba[(size_t)y * px * 4], (size_t)px * 4);
}

static bool readAll(const std::wstring& path, std::vector<uint8_t>& data) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return false;
    fseek(f, 0, SEEK_END);
    data.resize((size_t)ftell(f));
    fseek(f, 0, SEEK_SET);
    const bool ok = fread(data.data(), 1, data.size(), f) == data.size();
    fclose(f);
    return ok;
}

int BrickApp::buildFullThumbs() {
    const int px = m_thumbPx;
    const std::wstring packPath = m_opt.fullThumbs, dir = packPath.substr(0, packPath.find_last_of(L"\\/"));
    const std::wstring tmp = dir + L"\\thumbs_v3_parts";
    CreateDirectoryW(tmp.c_str(), nullptr);
    Scene ts;
    int glass = -1;
    const int mat = setupStudio2(ts, glass);
    ts.geometryVersion = 0x60000000u;
    LARGE_INTEGER q0, q1, qf;
    QueryPerformanceFrequency(&qf);
    QueryPerformanceCounter(&q0);
    int done = 0, skipped = 0, failed = 0;
    std::string failures;
    auto log = [&](bool final) {
        FILE* lf = nullptr;
        if (_wfopen_s(&lf, (packPath + L".txt").c_str(), L"w") != 0 || !lf) return;
        QueryPerformanceCounter(&q1);
        fprintf(lf, "%s rendered=%d resumed=%d failed=%d elapsed_s=%.0f\n%s", final ? "done" : "running", done, skipped, failed,
                (q1.QuadPart - q0.QuadPart) / (double)qf.QuadPart, failures.c_str());
        fclose(lf);
    };
    auto pngOf = [&](const Part& p) { return tmp + L"\\" + std::wstring(p.name.begin(), p.name.end()) + L".png"; };
    // the parts of one set (loaded), rendered into the temporary folder
    auto renderSet = [&](const std::vector<int>& set) {
        ts.meshes.clear();
        ts.objects.clear();
        std::vector<std::pair<int, int>> objs;
        for (int i : set) {
            const Part& p = m_lib.parts[i];
            if (!p.loaded || GetFileAttributesW(pngOf(p).c_str()) != INVALID_FILE_ATTRIBUTES) { if (p.loaded) ++skipped; continue; }
            Object o;
            o.mesh = ts.addMesh(p.name, p.mesh);
            o.material = isGlassPart(p) ? glass : mat;
            o.visible = false;
            objs.push_back({i, ts.addObject(o)});
        }
        if (objs.empty()) return;
        ++ts.geometryVersion;
        for (auto [i, ob] : objs) {
            const Part& p = m_lib.parts[i];
            ts.objects[ob].visible = true;
            std::vector<uint8_t> rgba, big, png;
            int W = 0;
            bool ok = renderThumb2(ts, ob, p, defaultThumbView(), px, rgba);
            if (ok) { padThumb(rgba, px, big, W); ok = encodePng(big, W, W, png); }
            ts.objects[ob].visible = false;
            if (!ok) { ++failed; failures += "failed " + p.name + "\n"; continue; }
            FILE* f = nullptr;
            if (_wfopen_s(&f, pngOf(p).c_str(), L"wb") == 0 && f) { fwrite(png.data(), 1, png.size(), f); fclose(f); ++done; }
            else { ++failed; failures += "unwritten " + p.name + "\n"; }
        }
    };
    // 1) base + Technic (loaded at start)
    std::vector<int> base;
    for (int i = 0; i < (int)m_lib.parts.size(); ++i)
        if (m_lib.parts[i].loaded && (m_lib.parts[i].pack < m_lib.technicPacks)) base.push_back(i);
    renderSet(base);
    log(false);
    // 2) the full library, pack by pack (the geometry freed after each)
    for (int pk = m_lib.technicPacks; pk < m_lib.packCount; ++pk) {
        m_lib.loadPack(pk);
        std::vector<int> set;
        for (int i = 0; i < (int)m_lib.parts.size(); ++i)
            if (m_lib.parts[i].pack == pk) set.push_back(i);
        renderSet(set);
        m_lib.unloadPack(pk);
        if (pk % 10 == 0) log(false);
    }
    // 3) the packs, from the images
    auto assemble = [&](const char* magic, const std::vector<int>& set, const std::wstring& out) {
        std::vector<uint8_t> pack = {(uint8_t)magic[0], (uint8_t)magic[1], (uint8_t)magic[2], (uint8_t)magic[3], 0, 0, 0, 0};
        uint32_t count = 0;
        for (int i : set) {
            const Part& p = m_lib.parts[i];
            std::vector<uint8_t> png;
            if (!readAll(pngOf(p), png)) continue;
            const uint16_t nl = (uint16_t)p.name.size();
            const uint32_t pl = (uint32_t)png.size();
            pack.insert(pack.end(), (const uint8_t*)&nl, (const uint8_t*)&nl + 2);
            pack.insert(pack.end(), p.name.begin(), p.name.end());
            pack.insert(pack.end(), (const uint8_t*)&pl, (const uint8_t*)&pl + 4);
            pack.insert(pack.end(), png.begin(), png.end());
            ++count;
        }
        memcpy(&pack[4], &count, 4);
        FILE* f = nullptr;
        if (_wfopen_s(&f, out.c_str(), L"wb") == 0 && f) { fwrite(pack.data(), 1, pack.size(), f); fclose(f); }
        return (int)count;
    };
    std::vector<int> fullSet;
    for (int i = 0; i < (int)m_lib.parts.size(); ++i)
        if (m_lib.parts[i].pack >= m_lib.technicPacks) fullSet.push_back(i);
    assemble("BTW1", base, dir + L"\\thumbs_base_v3.pack");
    const int n = assemble("BTF1", fullSet, packPath);
    log(true);
    return n;
}

// the index of the pack (read once, the images stay in memory compressed)
void BrickApp::loadFullThumbIndex() {
    FILE* f = nullptr;
    if (m_lib.fullDir.empty() || _wfopen_s(&f, (m_lib.fullDir + L"\\thumbs.pack").c_str(), L"rb") != 0 || !f) return;
    fseek(f, 0, SEEK_END);
    m_fullThumbData.resize((size_t)ftell(f));
    fseek(f, 0, SEEK_SET);
    if (fread(m_fullThumbData.data(), 1, m_fullThumbData.size(), f) != m_fullThumbData.size()) m_fullThumbData.clear();
    fclose(f);
    const std::vector<uint8_t>& d = m_fullThumbData;
    if (d.size() < 8 || memcmp(d.data(), "BTF1", 4) != 0) { m_fullThumbData.clear(); return; }
    const uint32_t count = *(const uint32_t*)&d[4];
    size_t o = 8;
    m_fullThumbAt.assign(m_lib.parts.size(), {0, 0});
    for (uint32_t e = 0; e < count && o + 2 <= d.size(); ++e) {
        const uint16_t nl = *(const uint16_t*)&d[o];
        o += 2;
        if (o + nl + 4 > d.size()) break;
        const int i = m_lib.find(std::string((const char*)&d[o], nl));
        o += nl;
        const uint32_t pl = *(const uint32_t*)&d[o];
        o += 4;
        if (o + pl > d.size()) break;
        if (i >= 0) m_fullThumbAt[i] = {(uint32_t)o, pl};
        o += pl;
    }
}

bool BrickApp::lazyThumb(int part, ImTextureID& tex) {
    if (part < 0 || part >= (int)m_fullThumbAt.size() || m_fullThumbAt[part].second == 0) return false;
    auto it = m_lazyThumbs.find(part);
    if (it != m_lazyThumbs.end()) { it->second.second = m_frame; tex = it->second.first; return true; }
    if (m_lazyThumbBudget <= 0) return false;   // a few decoded per frame: the others show next frames
    --m_lazyThumbBudget;
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    if (!decodeImage(&m_fullThumbData[m_fullThumbAt[part].first], m_fullThumbAt[part].second, rgba, w, h) || w <= 0 || h <= 0) return false;
    if (m_lazyThumbs.size() >= 900) {   // the least recently used
        auto old = m_lazyThumbs.begin();
        for (auto q = m_lazyThumbs.begin(); q != m_lazyThumbs.end(); ++q)
            if (q->second.second < old->second.second) old = q;
        m_gui.releaseTexture(old->second.first);
        m_lazyThumbs.erase(old);
    }
    tex = m_gui.createTexture(rgba.data(), (uint32_t)w, (uint32_t)h);
    m_lazyThumbs[part] = {tex, m_frame};
    return true;
}

} // namespace brickit
