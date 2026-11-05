// BrickIt - the temporary brick tray (« Bac à briques temporaire », Wzx 05/11/2026): a floating glass window where
// parts are kept as thumbnails in their own colour, placed freely as in PureRef (no grid), zoomed and panned. A card of
// the library dropped in it, or « Ajouter la sélection » (one thumbnail per selected brick, its colour), adds them; a
// click or a drag out of it gives the part in 3D with its colour, as the library does. Its content and layout are kept
// in %LOCALAPPDATA%\BrickIt\bac.json. The thumbnails are path traced in the studio of the library (Thumbs.cpp), at the
// start of a frame (the renderer free).
#include "BrickApp.h"
#include "app/Json.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace brickit {

bool isGlassPart(const Part& p);   // Thumbs.cpp

std::wstring BrickApp::bacFile() const {
    if (!m_opt.bacFile.empty()) return m_opt.bacFile;
    if (!m_opt.shotPath.empty()) return L"";   // (captures: never the user's tray)
    wchar_t la[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return L"";
    return std::wstring(la) + L"\\BrickIt\\bac.json";
}

void BrickApp::bacAdd(int part, int color, float x, float y) {
    if (part < 0 || part >= (int)m_lib.parts.size()) return;
    BacItem it;
    it.part = part;
    it.color = std::clamp(color, 0, (int)m_lib.palette.size() - 1);
    it.x = x;
    it.y = y;
    m_bac.push_back(it);
    m_bacDirty = true;
}

// one thumbnail per selected brick, in a row after the last ones (Wzx: 10 pieces = 10 thumbnails)
void BrickApp::bacAddSelection() {
    const std::vector<int> s = selection();
    if (s.empty()) { toast("Sélectionne d'abord des briques"); return; }
    float x = 20, y = 20;
    if (!m_bac.empty()) { y = m_bac.back().y; x = m_bac.back().x + 96; }
    const float wrap = m_bacRect.z > 0 ? std::max(120.0f, m_bacRect.z / (m_ui * m_bacZoom) - 110) : 300.0f;   // (the visible width)
    for (int i : s) {
        if (x > wrap) { x = 20; y += 96; }
        bacAdd(m_bricks[i].part, m_bricks[i].color, x, y);
        x += 96;
    }
    char t[80];
    snprintf(t, sizeof(t), "%d pièce%s ajoutée%s au bac", (int)s.size(), s.size() > 1 ? "s" : "", s.size() > 1 ? "s" : "");
    toast(t);
}

// the thumbnails still to render (start of a frame)
void BrickApp::bacRenderPending() {
    bool any = false;
    for (const BacItem& it : m_bac) any |= it.tex == 0;
    if (!any) return;
    if (!m_bacBuilt) {
        m_bacScene = Scene();
        m_bacMat = setupStudio2(m_bacScene, m_bacGlass);
        m_bacScene.geometryVersion = 0x5c000000u;
        m_bacBuilt = true;
    }
    int budget = 6;   // a few per frame
    for (BacItem& it : m_bac) {
        if (it.tex || budget <= 0) continue;
        --budget;
        Part* p = m_lib.get(m_lib.parts[it.part].name);
        if (!p) { it.tex = (ImTextureID)1; continue; }   // (no geometry: an empty card)
        m_bacScene.meshes.clear();
        m_bacScene.objects.clear();
        Material m = it.color < (int)m_colorMaterialOrig.size() ? m_colorMaterialOrig[it.color] : m_bacScene.materials[m_bacMat];
        m.subsurface = 0.0f;   // (the studio: no SSS)
        m_bacScene.materials[m_bacMat] = m;
        Object o;
        o.name = p->name;
        o.mesh = m_bacScene.addMesh(p->name, p->mesh);
        o.material = m_bacMat;
        const int ob = m_bacScene.addObject(o);
        ++m_bacScene.geometryVersion;
        std::vector<uint8_t> rgba;
        const int px = 128;
        if (renderThumb2(m_bacScene, ob, *p, defaultThumbView(), px, rgba)) it.tex = m_gui.createTexture(rgba.data(), px, px);
        else it.tex = (ImTextureID)1;
    }
}

void BrickApp::loadBac() {
    const std::wstring f = bacFile();
    if (f.empty()) return;
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, f.c_str(), L"rb") != 0 || !fp) return;
    std::string text;
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) text.append(buf, n);
    fclose(fp);
    app::Json j;
    if (!app::Json::parse(text, j)) return;
    m_bacOpen = j["open"].type == app::Json::Bool && j["open"].b;
    m_bacRect = ImVec4((float)j["x"].num(m_bacRect.x), (float)j["y"].num(m_bacRect.y), (float)j["w"].num(m_bacRect.z), (float)j["h"].num(m_bacRect.w));
    m_bacZoom = std::clamp((float)j["zoom"].num(1), 0.4f, 3.0f);
    m_bacPan = ImVec2((float)j["panX"].num(0), (float)j["panY"].num(0));
    for (const app::Json& it : j["items"].a) {
        const int part = m_lib.find(it["part"].str());
        if (part < 0) continue;
        int color = 0;
        const int cid = (int)it["color"].num(15);
        for (int k = 0; k < (int)m_lib.palette.size(); ++k)
            if (m_lib.palette[k].id == cid) color = k;
        bacAdd(part, color, (float)it["x"].num(), (float)it["y"].num());
    }
    m_bacDirty = false;
}

void BrickApp::saveBac() {
    m_bacDirty = false;
    const std::wstring f = bacFile();
    if (f.empty()) return;
    CreateDirectoryW(f.substr(0, f.find_last_of(L'\\')).c_str(), nullptr);
    std::string o = "{\n";
    char b[256];
    snprintf(b, sizeof(b), " \"open\": %s, \"x\": %.0f, \"y\": %.0f, \"w\": %.0f, \"h\": %.0f, \"zoom\": %.3f, \"panX\": %.1f, \"panY\": %.1f,\n \"items\": [",
             m_bacOpen ? "true" : "false", m_bacRect.x, m_bacRect.y, m_bacRect.z, m_bacRect.w, m_bacZoom, m_bacPan.x, m_bacPan.y);
    o += b;
    for (size_t i = 0; i < m_bac.size(); ++i) {
        const BacItem& it = m_bac[i];
        snprintf(b, sizeof(b), "%s\n  {\"part\": \"%s\", \"color\": %d, \"x\": %.1f, \"y\": %.1f}", i ? "," : "", m_lib.parts[it.part].name.c_str(), m_lib.palette[it.color].id, it.x, it.y);
        o += b;
    }
    o += "\n ]\n}\n";
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, f.c_str(), L"wb") == 0 && fp) { fwrite(o.data(), 1, o.size(), fp); fclose(fp); }
}

} // namespace brickit
