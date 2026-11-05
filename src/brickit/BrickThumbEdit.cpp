// BrickIt - Edit thumbnail (DEV): a right click on a brick of the scene or on a card of the library opens a menu;
// « Modifier la miniature » opens a viewer with the part alone in the studio of the thumbnails (v2), orbit (left drag),
// zoom (wheel), pan (right / middle drag); « Enregistrer comme miniature » renders it (transparent background, contact
// shadow) into %LOCALAPPDATA%\BrickIt\thumb_overrides\<id>.png with its view (<id>.view), used first by the library.
#include "BrickApp.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace brickit {

float acesInverse(float target8, float exposure);   // BrickPreview.cpp
bool isGlassPart(const Part& p);                     // Thumbs.cpp
bool encodePng(const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h, std::vector<uint8_t>& out);

std::wstring BrickApp::thumbOverrideDir() const {
    if (!m_opt.thumbDir.empty()) return m_opt.thumbDir;
    wchar_t la[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return L"";
    return std::wstring(la) + L"\\BrickIt\\thumb_overrides";
}

// the image of an override: padded to 1.4 x (the cards zoom the thumbnails x 1.4, made with that margin)
ImTextureID BrickApp::overrideTexture(const std::vector<uint8_t>& rgba, int w, int h) {
    const int W = (int)std::lround(w * 1.4f), H = (int)std::lround(h * 1.4f), ox = (W - w) / 2, oy = (H - h) / 2;
    std::vector<uint8_t> big((size_t)W * H * 4, 0);
    for (int y = 0; y < h; ++y) memcpy(&big[((size_t)(y + oy) * W + ox) * 4], &rgba[(size_t)y * w * 4], (size_t)w * 4);
    return m_gui.createTexture(big.data(), (uint32_t)W, (uint32_t)H);
}

void BrickApp::loadThumbOverrides() {
    const std::wstring dir = thumbOverrideDir();
    if (dir.empty()) return;
    WIN32_FIND_DATAW fd;
    HANDLE hf = FindFirstFileW((dir + L"\\*.png").c_str(), &fd);
    if (hf == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring fn = fd.cFileName;
        std::string id(fn.begin(), fn.end() - 4);
        const int part = m_lib.find(id);
        if (part < 0) continue;
        FILE* f = nullptr;
        if (_wfopen_s(&f, (dir + L"\\" + fn).c_str(), L"rb") != 0 || !f) continue;
        std::vector<uint8_t> data;
        fseek(f, 0, SEEK_END);
        data.resize((size_t)ftell(f));
        fseek(f, 0, SEEK_SET);
        const bool ok = fread(data.data(), 1, data.size(), f) == data.size();
        fclose(f);
        std::vector<uint8_t> rgba;
        int w = 0, h = 0;
        if (ok && decodeImage(data.data(), data.size(), rgba, w, h) && w > 0 && h > 0) m_thumbOverride[part] = overrideTexture(rgba, w, h);
    } while (FindNextFileW(hf, &fd));
    FindClose(hf);
}

bool BrickApp::readThumbView(int part, ThumbView& v) {
    v = defaultThumbView();
    const std::string& id = m_lib.parts[part].name;
    FILE* f = nullptr;
    if (_wfopen_s(&f, (thumbOverrideDir() + L"\\" + std::wstring(id.begin(), id.end()) + L".view").c_str(), L"r") != 0 || !f) return false;
    const bool ok = fscanf_s(f, "%f %f %f %f %f", &v.yaw, &v.pitch, &v.zoom, &v.panX, &v.panY) == 5;
    fclose(f);
    if (!ok) v = defaultThumbView();
    return ok;
}

void BrickApp::openThumbEditor(int part) {
    if (part < 0 || !m_lib.get(m_lib.parts[part].name)) return;
    m_teditPart = part;
    readThumbView(part, m_teView);
    m_teObj = -1;
}

// the viewer of the editor: the main renderer draws it into the viewport texture (the main view frozen meanwhile)
void BrickApp::renderThumbEditor() {
    if (m_teditPart < 0) return;
    if (!m_teBuilt) {
        m_te = Scene();
        m_teMat = setupStudio2(m_te, m_teGlass);
        m_te.geometryVersion = 0x58000000u;
        m_teBuilt = true;
        m_teObj = -1;
    }
    const Part& p = m_lib.parts[m_teditPart];
    if (m_teObj < 0 || m_te.objects[m_teObj].name != p.name) {
        m_te.meshes.clear();
        m_te.objects.clear();
        Object o;
        o.name = p.name;
        o.mesh = m_te.addMesh(p.name, p.mesh);
        o.material = isGlassPart(p) ? m_teGlass : m_teMat;
        m_teObj = m_te.addObject(o);
        ++m_te.geometryVersion;
    }
    const float bg[3] = {acesInverse(0x2a, 1.12f), acesInverse(0x2b, 1.12f), acesInverse(0x30, 1.12f)};   // the card colour
    m_te.env.top = m_te.env.horizon = m_te.env.bottom = float3(bg[0], bg[1], bg[2]);
    fitThumbCamera(m_te.camera, p, m_teView);
    ++m_te.objectsVersion;
    RenderSettings rs = thumbSettings();
    rs.pixelScale = 1;
    const int side = (int)std::max(64.0f, m_teRect.z - m_teRect.x);
    m_renderer.renderToTexture(m_te, rs, ViewRect{0, 0, side, side}, -1);
}

// « Enregistrer comme miniature »: at the start of the next frame (the renderer is free: no frame being recorded)
void BrickApp::saveThumbOverride() {
    m_teSave = false;
    if (m_teditPart < 0 || m_teObj < 0) return;
    const Part& p = m_lib.parts[m_teditPart];
    std::vector<uint8_t> rgba, png;
    const int px = 256;
    const Environment keep = m_te.env;
    // rendered at the size of the viewer (the output image of the renderer keeps its size: the UI shows it), then
    // reduced to 256 px (box filter, premultiplied)
    const int side = (int)std::max(64.0f, m_teRect.z - m_teRect.x);
    std::vector<uint8_t> big;
    bool ok = renderThumb2(m_te, m_teObj, p, m_teView, side, big);
    if (ok) {
        rgba.assign((size_t)px * px * 4, 0);
        for (int y = 0; y < px; ++y)
            for (int x = 0; x < px; ++x) {
                const int x0 = x * side / px, x1 = std::max(x0 + 1, (x + 1) * side / px), y0 = y * side / px, y1 = std::max(y0 + 1, (y + 1) * side / px);
                float acc[4] = {0, 0, 0, 0};
                int n = 0;
                for (int yy = y0; yy < y1; ++yy)
                    for (int xx = x0; xx < x1; ++xx, ++n) {
                        const uint8_t* q = &big[((size_t)yy * side + xx) * 4];
                        const float a = q[3] / 255.0f;
                        for (int c = 0; c < 3; ++c) acc[c] += q[c] * a;
                        acc[3] += a;
                    }
                uint8_t* o = &rgba[((size_t)y * px + x) * 4];
                for (int c = 0; c < 3; ++c) o[c] = acc[3] > 0 ? (uint8_t)std::min(255.0f, acc[c] / acc[3] + 0.5f) : 0;
                o[3] = (uint8_t)std::min(255.0f, acc[3] / n * 255.0f + 0.5f);
            }
        ok = encodePng(rgba, px, px, png);
    }
    m_te.env = keep;
    const std::wstring dir = thumbOverrideDir();
    CreateDirectoryW((dir.substr(0, dir.find_last_of(L'\\'))).c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);
    const std::wstring base = dir + L"\\" + std::wstring(p.name.begin(), p.name.end());
    FILE* f = nullptr;
    bool saved = false;
    if (ok && _wfopen_s(&f, (base + L".png").c_str(), L"wb") == 0 && f) { saved = fwrite(png.data(), 1, png.size(), f) == png.size(); fclose(f); }
    if (saved && _wfopen_s(&f, (base + L".view").c_str(), L"w") == 0 && f) {
        fprintf(f, "%.5f %.5f %.4f %.4f %.4f\n", m_teView.yaw, m_teView.pitch, m_teView.zoom, m_teView.panX, m_teView.panY);
        fclose(f);
    }
    if (saved) {
        auto it = m_thumbOverride.find(m_teditPart);
        if (it != m_thumbOverride.end()) m_gui.releaseTexture(it->second);
        m_thumbOverride[m_teditPart] = overrideTexture(rgba, px, px);
        toast("Miniature enregistrée (thumb_overrides)");
    } else toast("Miniature non enregistrée");
}

} // namespace brickit
