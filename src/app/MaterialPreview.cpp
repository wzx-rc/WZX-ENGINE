// Material Studio preview: the Atelier material is mapped to an engine material and rendered by vp3d
// (path tracing) on the preview shape, with the studio lights and environment of the preview settings.
#include "App.h"
#include "vp3d/shaders/Shared.h"
#include <commdlg.h>
#include <wincodec.h>
#include <sstream>

namespace app {

namespace amat = atelier::material;

static float srgbToLin(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }

// --------------------------------------------------------------------------- host callbacks (called by material_ui)

void App::MaterialPreview::requestPreview(const atelier::material::Material& m, const amat::PreviewSettings& s, int w, int h) {
    material = m;
    settings = s;
    width = std::max(16, w);
    height = std::max(16, h);
    want = true;
}

void* App::MaterialPreview::previewTexture() { return (void*)(intptr_t)app->m_gui.viewportTexture(); }

bool App::MaterialPreview::exportPreview(const std::wstring& path) {
    std::vector<uint8_t> img;
    uint32_t w, h;
    if (!app->m_renderer.readViewport(img, w, h)) return false;
    std::wstring dir = path.substr(0, path.find_last_of(L"\\/"));
    if (!dir.empty()) CreateDirectoryW(dir.c_str(), nullptr);
    // PNG through WIC
    for (size_t i = 3; i < img.size(); i += 4) img[i] = 255;
    ComPtr<IWICImagingFactory> f;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) return false;
    ComPtr<IWICStream> st;
    f->CreateStream(&st);
    if (FAILED(st->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) return false;
    ComPtr<IWICBitmapEncoder> enc;
    f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
    enc->Initialize(st.Get(), WICBitmapEncoderNoCache);
    ComPtr<IWICBitmapFrameEncode> fr;
    ComPtr<IPropertyBag2> props;
    enc->CreateNewFrame(&fr, &props);
    fr->Initialize(props.Get());
    fr->SetSize(w, h);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppRGBA;
    fr->SetPixelFormat(&fmt);
    if (fmt != GUID_WICPixelFormat32bppRGBA)
        for (size_t i = 0; i + 3 < img.size(); i += 4) std::swap(img[i], img[i + 2]);
    fr->WritePixels(h, w * 4, (UINT)img.size(), img.data());
    fr->Commit();
    enc->Commit();
    return true;
}

amat::PreviewTexture App::MaterialPreview::loadTexture(const std::string& utf8Path) {
    auto it = app->m_textureCache.find(utf8Path);
    if (it != app->m_textureCache.end()) return it->second;
    amat::PreviewTexture t;
    ComPtr<IWICImagingFactory> f;
    ComPtr<IWICBitmapDecoder> dec;
    ComPtr<IWICBitmapFrameDecode> fr;
    ComPtr<IWICFormatConverter> conv;
    std::wstring wpath = fromUtf8(utf8Path);
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f))) &&
        SUCCEEDED(f->CreateDecoderFromFilename(wpath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) &&
        SUCCEEDED(dec->GetFrame(0, &fr)) && SUCCEEDED(f->CreateFormatConverter(&conv)) &&
        SUCCEEDED(conv->Initialize(fr.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
        UINT w = 0, h = 0;
        conv->GetSize(&w, &h);
        // thumbnails only: keep at most 512 px
        UINT step = std::max(1u, std::max(w, h) / 512);
        UINT tw = w / step, th = h / step;
        std::vector<uint8_t> full((size_t)w * h * 4), px((size_t)tw * th * 4);
        conv->CopyPixels(nullptr, w * 4, (UINT)full.size(), full.data());
        for (UINT y = 0; y < th; ++y)
            for (UINT x = 0; x < tw; ++x) memcpy(&px[((size_t)y * tw + x) * 4], &full[((size_t)y * step * w + x * step) * 4], 4);
        ImTextureID id = app->m_gui.createTexture(px.data(), tw, th);
        if (id) { t.id = (void*)(intptr_t)id; t.width = (int)tw; t.height = (int)th; }
    }
    app->m_textureCache[utf8Path] = t;
    return t;
}

std::string App::MaterialPreview::pickTextureFile() {
    wchar_t buf[MAX_PATH] = L"";
    OPENFILENAMEW of = {sizeof(of)};
    of.hwndOwner = app->m_hwnd;
    of.lpstrFilter = L"Images (*.png;*.jpg;*.jpeg;*.bmp;*.tif)\0*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff\0All files\0*.*\0";
    of.lpstrFile = buf;
    of.nMaxFile = MAX_PATH;
    of.Flags = OFN_FILEMUSTEXIST;
    return GetOpenFileNameW(&of) ? toUtf8(buf) : std::string();
}

// --------------------------------------------------------------------------- preview scene

void App::buildPreviewScene(int shape) {
    Scene& s = m_previewScene;
    s = Scene();
    s.name = "Material Preview";
    s.unitsPerMeter = 1.0f;
    s.gridScale = 0.25f;
    MeshData mesh;
    float lift = 0;
    switch ((amat::Shape)shape) {
    case amat::Shape::Sphere: mesh = makeSphere(1.0f, 96, 48); lift = 1.0f; break;
    case amat::Shape::Cube: mesh = makeBox({-0.8f, 0, -0.8f}, {0.8f, 1.6f, 0.8f}); break;
    case amat::Shape::Torus: {
        std::vector<LathePoint> prof;
        makeTorusProfile(0.7f, 0.3f, 32, prof);
        mesh = makeLathe(prof, 96, 60.0f);
        lift = 0.3f;
        break;
    }
    case amat::Shape::Plane: mesh = makeBox({-1.2f, 0, -1.2f}, {1.2f, 0.04f, 1.2f}); break;
    default: mesh = makeShaderBall(0.9f); break;
    }
    // floor with grid
    {
        Material g;
        g.name = "Preview Floor";
        g.baseColor = float3(0.05f, 0.055f, 0.06f);
        g.roughness = 0.7f;
        g.flags = MAT_FLAG_GRID;
        Object o;
        o.name = "Floor";
        o.part = "GROUND";
        o.mesh = s.addMesh("previewFloor", makePlane(40.0f));
        o.material = s.addMaterial(g);
        o.selectable = false;
        s.addObject(o);
    }
    Material m;
    m.name = "Preview Material";
    Object o;
    o.name = "Preview";
    o.part = "PREVIEW";
    o.mesh = s.addMesh("previewShape", mesh);
    o.material = s.addMaterial(m);
    o.position = float3(0, lift, 0);
    s.addObject(o);
    m_previewShape = shape;
    m_previewKey.clear();
}

void App::leavePreview() {
    if (!m_previewActive) return;
    m_previewActive = false;
    m_renderer.setScene(m_scene);
    markDirty();
}

void App::renderMaterialPreview() {
    MaterialPreview& P = m_matHost;
    const atelier::material::Material& am = P.material;
    const amat::PreviewSettings& ps = P.settings;
    if (m_previewShape != (int)ps.shape || !m_previewActive) {
        if (m_previewShape != (int)ps.shape || m_previewScene.objects.empty()) buildPreviewScene((int)ps.shape);
        m_renderer.setScene(m_previewScene);
        m_previewActive = true;
        m_previewKey.clear();
    }
    Scene& s = m_previewScene;
    // material
    Material& m = s.materials[s.objects[1].material];
    m.baseColor = float3(srgbToLin(am.baseColor[0]), srgbToLin(am.baseColor[1]), srgbToLin(am.baseColor[2]));
    m.roughness = am.roughness;
    m.metallic = am.metallic;
    m.ior = am.ior;
    m.coat = am.clearcoat;
    m.coatRoughness = am.clearcoatRoughness;
    m.emission = float3(srgbToLin(am.emissionColor[0]), srgbToLin(am.emissionColor[1]), srgbToLin(am.emissionColor[2]));
    m.emissionStrength = am.emissionStrength * 4.0f;
    m.transmission = am.shader == amat::Shader::Transparent ? clampf(1.0f - am.opacity, 0, 1) : 0.0f;
    m.specular = 0.5f;
    s.materials[s.objects[0].material].flags = ps.grid ? MAT_FLAG_GRID : 0u;
    // environment
    float ei = ps.intensity;
    switch (ps.environment) {
    case amat::Environment::Sunset: s.env.top = float3(0.25f, 0.30f, 0.45f); s.env.horizon = float3(0.95f, 0.55f, 0.30f); s.env.bottom = float3(0.10f, 0.07f, 0.05f); break;
    case amat::Environment::Warehouse: s.env.top = float3(0.45f, 0.45f, 0.42f); s.env.horizon = float3(0.30f, 0.29f, 0.27f); s.env.bottom = float3(0.08f, 0.08f, 0.08f); break;
    default: s.env.top = float3(0.32f, 0.34f, 0.38f); s.env.horizon = float3(0.18f, 0.19f, 0.21f); s.env.bottom = float3(0.04f, 0.04f, 0.045f); break;
    }
    s.env.intensity = 0.6f * ei;
    s.env.visibleToCamera = true;
    // key / fill / rim lights, turned with the environment rotation
    s.lights.clear();
    float rot = radians(ps.environmentRotation);
    auto light = [&](const char* name, float az, float el, float dist, float size, float3 color, float k) {
        AreaLight l;
        l.name = name;
        float a = az + rot;
        l.position = float3(std::sin(a) * std::cos(el) * dist, 0.9f + std::sin(el) * dist, std::cos(a) * std::cos(el) * dist);
        l.target = float3(0, 0.9f, 0);
        l.width = l.height = size;
        l.color = color;
        l.intensity = k;
        l.visibleToCamera = false;
        s.lights.push_back(l);
    };
    light("Key", radians(-40.0f), radians(35.0f), 5.0f, 2.2f, float3(1.0f, 0.95f, 0.88f), 7.0f * ps.keyIntensity * ei);
    light("Fill", radians(55.0f), radians(15.0f), 5.0f, 2.8f, float3(0.80f, 0.88f, 1.0f), 4.0f * ps.fillIntensity * ei);
    light("Rim", radians(170.0f), radians(30.0f), 5.0f, 1.6f, float3(0.90f, 0.95f, 1.0f), 9.0f * ps.rimIntensity * ei);
    // camera
    Camera& c = s.camera;
    c.target = float3(0, 0.9f, 0);
    c.yaw = radians(ps.yaw);
    c.pitch = radians(ps.pitch);
    c.distance = ps.distance;
    c.fovY = radians(32.0f);
    c.dof = false;
    c.sensorScale = 1.0f;

    RenderSettings rs = m_rs;
    rs.raster = false;
    rs.maxBounces = 6;
    rs.maxSamples = 1024;
    rs.exposure = std::pow(2.0f, ps.exposure);
    rs.vignette = 0.0f;
    rs.samplesPerFrame = 2;
    // restart the accumulation when anything visible changed
    std::ostringstream key;
    key << (int)ps.shape << ' ' << (int)ps.environment << ' ' << ps.yaw << ' ' << ps.pitch << ' ' << ps.distance << ' ' << ps.exposure << ' '
        << ps.environmentRotation << ' ' << ps.intensity << ' ' << ps.keyIntensity << ' ' << ps.fillIntensity << ' ' << ps.rimIntensity << ' '
        << ps.grid << ' ' << am.baseColor[0] << ' ' << am.baseColor[1] << ' ' << am.baseColor[2] << ' ' << am.roughness << ' ' << am.metallic << ' '
        << am.ior << ' ' << am.clearcoat << ' ' << am.clearcoatRoughness << ' ' << am.emissionStrength << ' ' << am.emissionColor[0] << ' '
        << am.emissionColor[1] << ' ' << am.emissionColor[2] << ' ' << am.opacity << ' ' << (int)am.shader << ' ' << P.width << ' ' << P.height;
    if (key.str() != m_previewKey) {
        m_previewKey = key.str();
        m_renderer.resetAccumulation();
    }
    ViewRect vp;
    vp.w = P.width;
    vp.h = P.height;
    m_renderer.renderToTexture(s, rs, vp, -1);
    m_gui.viewportTexture();
}

} // namespace app
