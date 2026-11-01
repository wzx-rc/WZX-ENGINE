// BrickIt - instruction manual of the web builder: step n shows the first n bricks in the order they were placed, the
// nth one highlighted (emissive 0xffc21a x 0.3), on the paper colour 0xf4f2ea, camera 28 degrees along (1, 0.75, 1) at
// radius / sin 14 x 1.08, 960 x 600. The imported PDF (read vertically) is rendered by the Windows PDF renderer and
// kept next to the exe so it comes back at the next launch (the web keeps it in IndexedDB).
#include "BrickApp.h"
#include <algorithm>
#include <cmath>
#include <commdlg.h>

namespace brickit {

float acesInverse(float target8, float exposure);   // BrickPreview.cpp

static const int MW = 960, MH = 600;

void BrickApp::manualDirty() {
    for (auto& [n, t] : m_stepTex) if (t) m_gui.releaseTexture(t);
    m_stepTex.clear();
}

// Renders the step images asked by the manual this frame (at most two per frame), before the main view.
void BrickApp::renderSteps() {
    int budget = 2;
    for (int n : m_stepWanted) {
        if (budget <= 0) break;
        if (n < 1 || n > (int)m_bricks.size() || m_stepTex.count(n)) continue;
        --budget;
        // scene: the first n bricks, no ground, no ghost, paper background, the nth brick highlighted
        std::vector<std::pair<int, bool>> vis;
        for (int i = 0; i < (int)m_scene.objects.size(); ++i) vis.push_back({i, m_scene.objects[i].visible});
        for (int i = 0; i < m_firstBrickObject; ++i) m_scene.objects[i].visible = false;
        for (int i = 0; i < (int)m_bricks.size(); ++i) m_scene.objects[m_firstBrickObject + i].visible = i < n;
        if (m_ghostObject >= 0) m_scene.objects[m_ghostObject].visible = false;
        Object& cur = m_scene.objects[m_firstBrickObject + n - 1];
        const int baseMat = cur.material;
        int hi;
        auto it = m_hiMaterial.find(baseMat);
        if (it == m_hiMaterial.end()) {
            Material h = m_scene.materials[baseMat];
            h.emission = float3(srgbToLinear(0xff / 255.0f), srgbToLinear(0xc2 / 255.0f), srgbToLinear(0x1a / 255.0f));
            h.emissionStrength = 0.3f;
            hi = m_scene.addMaterial(h);
            m_hiMaterial[baseMat] = hi;
        } else hi = it->second;
        cur.material = hi;
        const Environment env = m_scene.env;
        const Camera cam = m_scene.camera;
        const float bg[3] = {acesInverse(0xf4, 1.0f), acesInverse(0xf2, 1.0f), acesInverse(0xea, 1.0f)};
        m_scene.env.top = m_scene.env.horizon = m_scene.env.bottom = float3(bg[0], bg[1], bg[2]);
        float3 mn(1e30f), mx(-1e30f);
        for (int i = 0; i < n; ++i) {
            Box b = m_build.aabbOf(m_bricks[i].part, m_bricks[i].rot, m_bricks[i].pos);
            mn = minv(mn, b.mn);
            mx = maxv(mx, b.mx);
        }
        Camera& c = m_scene.camera;
        c.fovY = radians(28.0f);
        c.target = (mn + mx) * 0.5f;
        c.yaw = std::atan2(1.0f, 1.0f);
        c.pitch = std::atan2(0.75f, std::sqrt(2.0f));
        const float r = std::max(length(mx - mn) * 0.5f, 2.2f);
        c.distance = r / std::sin(radians(14.0f)) * 1.08f;
        c.focusDistance = c.distance;
        ++m_scene.objectsVersion;
        RenderSettings rs = m_rs;
        rs.raster = true;
        rs.exposure = 1.0f;
        rs.tonemapper = 0;
        rs.editorGrid = 0;
        rs.vignette = 0;
        rs.idPass = false;
        rs.pixelScale = 1;
        rs.msaaSamples = 4;
        m_renderer.renderToTexture(m_scene, rs, ViewRect{0, 0, MW, MH}, -1);
        std::vector<uint8_t> img;
        uint32_t w = 0, h = 0;
        if (m_renderer.readViewport(img, w, h) && w && h) m_stepTex[n] = m_gui.createTexture(img.data(), w, h);
        else m_stepTex[n] = 0;
        // restore
        cur.material = baseMat;
        for (auto& [i, v] : vis) m_scene.objects[i].visible = v;
        m_scene.env = env;
        m_scene.camera = cam;
        ++m_scene.objectsVersion;
        m_renderer.resetAccumulation();
    }
    m_stepWanted.clear();
}

ImTextureID BrickApp::stepImage(int n) {
    auto it = m_stepTex.find(n);
    if (it != m_stepTex.end()) return it->second;
    if (!m_holding) m_stepWanted.push_back(n);   // (the scene is not final while a brick is in hand)
    return 0;
}

// ---------- PDF
void BrickApp::pdfOpen(const std::wstring& path, const std::string& name, bool save) {
    if (!m_pdf) m_pdf = std::make_unique<PdfReader>();
    std::wstring src = path;
    if (save) {   // a copy next to the exe: reopened at the next launch
        CreateDirectoryW((m_exeDir + L"\\manual_pdf").c_str(), nullptr);
        const std::wstring dst = m_exeDir + L"\\manual_pdf\\current.pdf";
        if (CopyFileW(path.c_str(), dst.c_str(), FALSE)) src = dst;
    }
    for (auto& t : m_pdfTex) if (t.tex) m_gui.releaseTexture(t.tex);
    m_pdfTex.clear();
    m_pdfSizes.clear();
    m_pdfName = name;
    m_pdfOpening = true;
    m_pdfMsg = "Ouverture du PDF…";
    m_pdfOn = true;   // view.hidden = false; pdfmode
    m_pdf->open(src);
    savePrefs();
}

// Results of the worker: page sizes once opened, then the rendered pages (textures).
void BrickApp::pdfPump() {
    if (!m_pdf) return;
    if (m_pdfOpening) {
        std::string err;
        if (m_pdf->failed(&err)) { m_pdfOpening = false; m_pdfMsg = "PDF illisible : " + err; return; }
        if (m_pdf->ready()) {
            m_pdfOpening = false;
            m_pdfMsg.clear();
            m_pdfSizes = m_pdf->sizes();
            m_pdfTex.assign(m_pdfSizes.size(), PdfTex());
            m_pdfPage = std::clamp(m_pdfPage, 1, std::max(1, (int)m_pdfSizes.size()));
            m_pdfScrollTo = m_pdfPage;   // requestAnimationFrame(() => scrollPdfTo(mpdf.page, false))
            m_pdfScrollSmooth = false;
            toast("PDF importé : " + std::to_string(m_pdfSizes.size()) + " pages");
        }
    }
    PdfReader::Image img;
    while (m_pdf->take(img)) {
        if (img.page < 0 || img.page >= (int)m_pdfTex.size() || img.rgba.empty()) continue;
        PdfTex& t = m_pdfTex[img.page];
        if (t.tex) m_gui.releaseTexture(t.tex);
        t.tex = m_gui.createTexture(img.rgba.data(), img.w, img.h);
        t.w = img.w;
        t.h = img.h;
        t.asked = false;
    }
}

void BrickApp::pdfDialog() {
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {sizeof(ofn)};
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"PDF (*.pdf)\0*.pdf\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;
    if (m_pManual.collapsed) m_pManual.collapsed = false;
    m_pdfPage = 1;
    std::wstring p = file;
    std::wstring n = p.substr(p.find_last_of(L"\\/") + 1);
    pdfOpen(p, std::string(n.begin(), n.end()), true);
}

} // namespace brickit
