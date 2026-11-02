// Spartan theme: the editor rebuilt from the Halo mockups (design/halo/mockup-halo-espaces.html) and the standalone
// reproduction halo_ui/src/HaloUI.cpp, wired to the engine. Every size is a CSS pixel of the mockup times k (DPI and the
// layout size setting). The colours come from the Spartan settings (main, background, second: atelier::fx::halo).
#include "App.h"
#include "GameHost.h"
#include "atelier/ambre_fx.h"
#include "atelier/atelier_style.h"
#include "vp3d/AssetImport.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace app {
namespace {

namespace hc = atelier::fx::halo;

// --------------------------------------------------------------------------- tokens (live: Settings > Spartan colours)
ImU32 cBase() { return hc::shell; }
ImU32 cPanel() { return hc::panel; }
ImU32 cPanel2() { return hc::raised; }
ImU32 cHover() { return hc::hover; }
ImU32 cLine() { return hc::border; }
ImU32 cLineHi() { return hc::borderHi; }
ImU32 cText() { return hc::text; }
ImU32 cText2() { return hc::muted; }
ImU32 cText3() { return hc::dim; }
ImU32 cAccent() { return hc::accent; }
ImU32 cAccentLo() { return hc::accentLo; }
ImU32 cAccentBg() { return hc::accentBg; }
ImU32 cSecond() { return hc::second; }
ImU32 alpha(ImU32 c, float a) { return atelier::fx::Alpha(c, a); }
const ImU32 kOk = IM_COL32(127, 199, 154, 255), kWarn = IM_COL32(214, 166, 74, 255);
const ImU32 kAxis[3] = {IM_COL32(215, 122, 122, 255), IM_COL32(132, 196, 143, 255), IM_COL32(126, 166, 216, 255)};

// --------------------------------------------------------------------------- icons (halo_ui/tools/atlas_names.txt order)
enum Icon {
    I_MAP, I_WORKFLOW, I_PALETTE, I_PACKAGE, I_UNDO, I_REDO, I_PLAY, I_HAMMER, I_SETTINGS, I_LAYERS, I_SUN, I_VIDEO,
    I_SEARCH, I_TERMINAL, I_CHEV_DOWN, I_CHEV_RIGHT, I_GLOBE, I_EYE, I_POINTER, I_MOVE, I_ROTATE, I_SCALING, I_MAGNET,
    I_CAMERA, I_SPARKLES, I_MORE, I_MOVE3D, I_CLOCK, I_FOLDER_OPEN, I_BOX, I_IMAGE, I_OUTPOST, I_HELMET, I_GRID, I_BRAND,
    I_PLUS, I_ZAP, I_BRANCH, I_HAND, I_MAXIMIZE, I_DROPLET, I_PUZZLE, I_CROSSHAIR, I_SHIELD, I_REPEAT, I_COPY, I_CIRCLE_PLAY,
    I_COUNT
};
const int kAtlasSizes[] = {16, 19, 22, 40, 42, 96};

struct Ctx {
    ImDrawList* dl = nullptr;
    float k = 1;
    ImTextureID atlas = 0;
    float aw = 1024, ah = 896;
    ImFont *ui = nullptr, *semi = nullptr, *hud = nullptr, *mono = nullptr, *monoBold = nullptr;
} C;
inline float P(float v) { return v * C.k; }
// font size for ImGui widgets (PushFont): the style's FontScaleMain multiplies it, the text drawn by hand is not scaled
inline float FP(float v) { return P(v) / std::max(0.01f, ImGui::GetStyle().FontScaleMain); }

void DrawIcon(int icon, float x, float y, float size, ImU32 color) {
    if (!C.atlas) return;
    const float px = size * C.k;
    int pick = 5;
    for (int i = 0; i < 6; ++i)
        if (kAtlasSizes[i] >= px - 0.5f) { pick = i; break; }
    float by = 0;
    for (int i = 0; i < pick; ++i) {
        const int c = kAtlasSizes[i] + 4, cols = 1024 / c;
        by += (float)(((I_COUNT + cols - 1) / cols) * c);
    }
    const int S = kAtlasSizes[pick], cell = S + 4, cols = 1024 / cell;
    const float cx = (float)((icon % cols) * cell + 2), cy = by + (float)((icon / cols) * cell + 2);
    C.dl->AddImage(C.atlas, ImVec2(x, y), ImVec2(x + px, y + px), ImVec2(cx / C.aw, cy / C.ah), ImVec2((cx + S) / C.aw, (cy + S) / C.ah), color);
}

// text: size in CSS px, letter spacing in em
float TextW(ImFont* f, float size, const char* t, float spacing = 0) {
    float w = f->CalcTextSizeA(P(size), FLT_MAX, 0, t).x;
    if (spacing != 0) w += P(size) * spacing * (float)ImTextCountCharsFromUtf8(t, nullptr);
    return w;
}
float Text(ImFont* f, float size, float x, float y, ImU32 c, const char* t, float spacing = 0) {
    if (spacing == 0) {
        C.dl->AddText(f, P(size), ImVec2(x, y), c, t);
        return x + f->CalcTextSizeA(P(size), FLT_MAX, 0, t).x;
    }
    for (const char* s = t; *s;) {
        unsigned int cp = 0;
        int n = ImTextCharFromUtf8(&cp, s, nullptr);
        C.dl->AddText(f, P(size), ImVec2(x, y), c, s, s + n);
        x += f->CalcTextSizeA(P(size), FLT_MAX, 0, s, s + n).x + P(size) * spacing;
        s += n;
    }
    return x;
}
float TextMid(ImFont* f, float size, float x, float top, float h, ImU32 c, const char* t, float spacing = 0) {
    return Text(f, size, x, top + (h - f->CalcTextSizeA(P(size), FLT_MAX, 0, "Ag").y) / 2, c, t, spacing);
}
std::string Upper(const std::string& s) {   // ASCII and the accented letters of the French labels (UTF-8 C3 xx)
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char ch = (unsigned char)s[i];
        if (ch == 0xC3 && i + 1 < s.size()) {
            unsigned char nx = (unsigned char)s[i + 1];
            out += (char)ch;
            out += (char)(nx >= 0xA0 && nx <= 0xBE ? nx - 0x20 : nx);
            ++i;
            continue;
        }
        out += (char)std::toupper(ch);
    }
    return out;
}
std::string Fit(ImFont* f, float size, const std::string& s, float w) {
    if (f->CalcTextSizeA(P(size), FLT_MAX, 0, s.c_str()).x <= w) return s;
    std::string t = s;
    while (!t.empty() && f->CalcTextSizeA(P(size), FLT_MAX, 0, (t + "…").c_str()).x > w) t.pop_back();
    return t + "…";
}

// shapes
void Rect(float x, float y, float w, float h, ImU32 fill, float r = 0) { C.dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), fill, r); }
void Border(float x, float y, float w, float h, ImU32 c, float r = 0) { C.dl->AddRect(ImVec2(x + 0.5f, y + 0.5f), ImVec2(x + w - 0.5f, y + h - 0.5f), c, r, 0, 1.0f); }
void HLine(float x0, float x1, float y, ImU32 c) { C.dl->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + 1), c); }
void VLine(float x, float y0, float y1, ImU32 c) { C.dl->AddRectFilled(ImVec2(x, y0), ImVec2(x + 1, y1), c); }
void Chamfer(float x, float y, float w, float h, float cut, ImU32 fill, ImU32 border) { atelier::fx::Chamfer(C.dl, ImVec2(x, y), ImVec2(x + w, y + h), cut, fill, border); }
void CornerTick(float x, float y) {
    C.dl->AddRectFilled(ImVec2(x, y), ImVec2(x + P(8), y + 1), alpha(cAccent(), 0.8f));
    C.dl->AddRectFilled(ImVec2(x, y), ImVec2(x + 1, y + P(8)), alpha(cAccent(), 0.8f));
}
bool Hit(const char* id, float x, float y, float w, float h, bool* hovered = nullptr, ImGuiButtonFlags flags = 0) {
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    bool q = ImGui::InvisibleButton(id, ImVec2(std::max(1.0f, w), std::max(1.0f, h)), flags);
    if (hovered) *hovered = ImGui::IsItemHovered();
    return q;
}
void Tip(const char* t) { if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", t); }
void FadeLine(ImVec2 a, ImVec2 b, float y0, float y1, float a1, ImU32 base, float width) {
    const int n = 24;
    for (int i = 0; i < n; ++i) {
        float t0 = (float)i / n, t1 = (float)(i + 1) / n;
        ImVec2 p0(a.x + (b.x - a.x) * t0, a.y + (b.y - a.y) * t0), p1(a.x + (b.x - a.x) * t1, a.y + (b.y - a.y) * t1);
        float f = y1 > y0 ? std::clamp(((p0.y + p1.y) / 2 - y0) / (y1 - y0), 0.0f, 1.0f) : 1.0f;
        if (a1 * f > 0.003f) C.dl->AddLine(p0, p1, alpha(base, a1 * f), width);
    }
}

// panel header (.ph): condensed spaced title, HUD corner mark, optional mono count on the right
void PanelHeader(float x, float y, float w, const char* title, const std::string& count = "") {
    const float h = P(32);
    TextMid(C.hud, 12, x + P(12), y, h, cText2(), Upper(title).c_str(), 0.2f);
    CornerTick(x, y);
    if (!count.empty()) TextMid(C.mono, 11, x + w - P(12) - TextW(C.mono, 11, count.c_str()), y, h, cText3(), count.c_str());
    HLine(x, x + w, y + h - 1, cLine());
}
// small icon button (.btn.icon 28 x 28) or flat tool (.vtools .t)
bool IconButton(const char* id, float x, float y, float sz, int icon, bool on, bool flat, const char* tip, bool enabled = true) {
    bool hv;
    ImGui::BeginDisabled(!enabled);
    bool q = Hit(id, x, y, sz, sz, &hv);
    ImGui::EndDisabled();
    if (flat) {
        if (on) Rect(x, y, sz, sz, cAccentBg(), P(2));
        else if (hv) Rect(x, y, sz, sz, cHover(), P(2));
    } else {
        Rect(x, y, sz, sz, hv ? cHover() : cPanel2(), P(2));
        Border(x, y, sz, sz, on ? cAccentLo() : cLineHi(), P(2));
    }
    const ImU32 c = !enabled ? cText3() : on ? cAccent() : hv ? cText() : (flat ? cText2() : cText());
    DrawIcon(icon, x + (sz - P(16)) / 2, y + (sz - P(16)) / 2, 16, c);
    if (tip) Tip(tip);
    return q && enabled;
}
// text button (.btn), chamfered; kind 0 normal, 1 main colour (Play), 2 second colour (Build)
bool TextButton(const char* id, float x, float y, float w, float h, int icon, const char* label, int kind, const char* tip = nullptr, bool enabled = true) {
    bool hv;
    ImGui::BeginDisabled(!enabled);
    bool q = Hit(id, x, y, w, h, &hv);
    ImGui::EndDisabled();
    ImU32 fill = hv ? cHover() : cPanel2(), border = cLineHi(), col = cText();
    if (kind == 1) { fill = hv ? alpha(cAccent(), 0.2f) : cAccentBg(); border = cAccentLo(); col = cAccent(); }
    if (kind == 2) { fill = hv ? alpha(cSecond(), 0.18f) : hc::secondBg; border = hc::secondLo; col = cSecond(); }
    if (!enabled) col = cText3();
    Chamfer(x, y, w, h, P(6), fill, border);
    float tx = x + P(12);
    if (icon >= 0) { DrawIcon(icon, tx, y + (h - P(16)) / 2, 16, col); tx += P(16 + 6); }
    TextMid(C.semi, 12, tx, y, h, col, label);
    if (tip) Tip(tip);
    return q && enabled;
}
float TextButtonW(int icon, const char* label) { return P(12 + 12) + (icon >= 0 ? P(16 + 6) : 0) + TextW(C.semi, 12, label); }

// search field (.search): icon + ImGui input with a hint, frame drawn here
void SearchField(const char* id, float x, float y, float w, char* buf, size_t len, const char* hint, bool focus = false) {
    const float h = P(28);
    Rect(x, y, w, h, cBase(), P(2));
    DrawIcon(I_SEARCH, x + P(9), y + (h - P(16)) / 2, 16, cText3());
    ImGui::PushFont(C.ui, FP(13));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, 0);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, 0);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, 0);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(cText()));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImGui::ColorConvertU32ToFloat4(cText3()));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(P(4), std::max(0.0f, (h - ImGui::GetFontSize()) / 2)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::SetCursorScreenPos(ImVec2(x + P(9 + 16 + 4), y));
    ImGui::SetNextItemWidth(w - P(9 + 16 + 4 + 4));
    if (focus) ImGui::SetKeyboardFocusHere();
    ImGui::InputTextWithHint(id, hint, buf, len);
    const bool active = ImGui::IsItemActive();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(5);
    ImGui::PopFont();
    Border(x, y, w, h, active ? cAccentLo() : cLine(), P(2));
}

// section header (.sh)
bool SectionHeader(const char* id, float x, float y, float w, int icon, const char* title, int chev) {
    bool hv;
    bool q = Hit(id, x, y, w, P(30), &hv);
    if (hv) Rect(x, y, w, P(30), alpha(cHover(), 0.5f));
    DrawIcon(icon, x + P(12), y + P(7), 16, cText2());
    TextMid(C.semi, 13, x + P(12 + 16 + 8), y, P(30), cText(), title);
    if (chev >= 0) DrawIcon(chev, x + w - P(12 + 16), y + P(7), 16, hv ? cText2() : cText3());
    return q;
}
// X / Y / Z drag field (.f); returns true while changed, activated set on the first frame of an edit
bool AxisField(const char* id, float x, float y, float w, int axis, float* v, float speed, const char* fmt, bool& activated) {
    const char* names[] = {"X", "Y", "Z"};
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, 0);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, 0);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, 0);
    ImGui::PushStyleColor(ImGuiCol_Text, 0);   // the value is drawn below with the mono font (shown while typing)
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(P(22), std::max(0.0f, (P(25) - ImGui::GetFontSize()) / 2)));
    ImGui::SetNextItemWidth(w);
    const ImGuiID gid = ImGui::GetID(id);
    const bool typing = ImGui::TempInputIsActive(gid);
    if (typing) ImGui::PopStyleColor(), ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(cText()));
    bool c = ImGui::DragFloat(id, v, speed, 0, 0, fmt, ImGuiSliderFlags_NoRoundToFormat);
    activated |= ImGui::IsItemActivated();
    const bool active = ImGui::IsItemActive(), hv = ImGui::IsItemHovered();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    Tip("Glisser pour modifier · Ctrl+clic pour saisir");
    if (!typing) {
        Rect(x, y, w, P(25), cBase(), P(2));
        float tx = TextMid(C.monoBold, 10, x + P(7), y, P(25), kAxis[axis], names[axis]);
        char buf[32];
        snprintf(buf, sizeof(buf), fmt, *v);
        C.dl->PushClipRect(ImVec2(x, y), ImVec2(x + w - P(3), y + P(25)), true);
        TextMid(C.mono, 11.5f, tx + P(6), y, P(25), cText(), buf);
        C.dl->PopClipRect();
    }
    Border(x, y, w, P(25), active || typing ? cAccentLo() : (hv ? cLineHi() : cLine()), P(2));
    return c;
}
// slider (.slider): fill up to the value, value text inside; drag sets the value
bool SliderBar(const char* id, float x, float y, float w, float* v, float lo, float hi, const char* fmt, bool& activated) {
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    ImGui::InvisibleButton(id, ImVec2(w, P(25)));
    activated |= ImGui::IsItemActivated();
    bool c = false;
    if (ImGui::IsItemActive()) {
        float nv = lo + std::clamp((ImGui::GetMousePos().x - x) / w, 0.0f, 1.0f) * (hi - lo);
        c = nv != *v;
        *v = nv;
    }
    const bool hv = ImGui::IsItemHovered();
    const float t = hi > lo ? std::clamp((*v - lo) / (hi - lo), 0.0f, 1.0f) : 0;
    Rect(x, y, w, P(25), cBase(), P(2));
    const float fx = x + 1 + (w - 2) * t;
    Rect(x + 1, y + 1, fx - x - 1, P(25) - 2, cAccentBg());
    Rect(std::max(x + 1, fx - P(2)), y + 1, P(2), P(25) - 2, cAccent());
    Border(x, y, w, P(25), hv || ImGui::IsItemActive() ? cLineHi() : cLine(), P(2));
    char buf[32];
    snprintf(buf, sizeof(buf), fmt, *v);
    TextMid(C.mono, 11.5f, x + P(8), y, P(25), cText(), buf);
    return c;
}
// toggle (.toggle 30 x 16)
bool Toggle(const char* id, float x, float y, bool* v) {
    bool q = Hit(id, x, y, P(30), P(16));
    if (q) *v = !*v;
    Rect(x, y, P(30), P(16), *v ? cAccentLo() : cLine(), P(2));
    Rect(*v ? x + P(30 - 2 - 12) : x + P(2), y + P(2), P(12), P(12), *v ? cAccent() : cText3(), P(1));
    return q;
}
// select (.select): value + chevron; opens a popup listing the options
bool Select(const char* id, float x, float y, float w, int* value, const std::vector<std::string>& options) {
    bool hv;
    if (Hit(id, x, y, w, P(25), &hv)) ImGui::OpenPopup(id);
    Rect(x, y, w, P(25), hv ? cHover() : cPanel2(), P(2));
    Border(x, y, w, P(25), cLineHi(), P(2));
    const std::string cur = *value >= 0 && *value < (int)options.size() ? options[*value] : std::string("—");
    C.dl->PushClipRect(ImVec2(x, y), ImVec2(x + w - P(28), y + P(25)), true);
    TextMid(C.ui, 13, x + P(8), y, P(25), cText(), cur.c_str());
    C.dl->PopClipRect();
    DrawIcon(I_CHEV_DOWN, x + w - P(8 + 16), y + P(4.5f), 16, cText2());
    bool changed = false;
    ImGui::SetNextWindowPos(ImVec2(x, y + P(27)));
    ImGui::SetNextWindowSizeConstraints(ImVec2(w, 0), ImVec2(w, P(320)));
    if (ImGui::BeginPopup(id)) {
        ImGui::PushFont(C.ui, FP(13));
        for (int i = 0; i < (int)options.size(); ++i)
            if (ImGui::Selectable(options[i].c_str(), *value == i)) { *value = i; changed = true; }
        ImGui::PopFont();
        ImGui::EndPopup();
    }
    return changed;
}
// property row label (.prop: 88 px label column)
void PropLabel(float x, float y, float h, const char* t, ImU32 c = 0) { TextMid(C.ui, 13, x, y, h, c ? c : cText2(), t); }

float3 eulerYXZ(const float4& q) {
    float m02 = 2 * (q.x * q.z + q.w * q.y), m12 = 2 * (q.y * q.z - q.w * q.x), m22 = 1 - 2 * (q.x * q.x + q.y * q.y);
    float m10 = 2 * (q.x * q.y + q.w * q.z), m11 = 1 - 2 * (q.x * q.x + q.z * q.z);
    float3 e(degrees(std::asin(clampf(-m12, -1, 1))), degrees(std::atan2(m02, m22)), degrees(std::atan2(m10, m11)));
    auto clean = [](float a) { return std::fabs(a) < 0.005f ? 0.0f : a; };
    return float3(clean(e.x), clean(e.y), clean(e.z));
}
float4 quatYXZ(const float3& deg) {
    float4 qy = qAxisAngle(float3(0, 1, 0), radians(deg.y)), qx = qAxisAngle(float3(1, 0, 0), radians(deg.x));
    float4 qz = qAxisAngle(float3(0, 0, 1), radians(deg.z));
    return qNormalize(qMul(qMul(qy, qx), qz));
}

// content of the asset drawer: primitives, material presets, scenes (same catalogue as the classic Content Browser)
struct Asset { int kind; int index; std::string name; };
std::vector<Asset> Assets() {
    std::vector<Asset> a;
    for (size_t i = 0; i < parts().size(); ++i) a.push_back({0, (int)i, "SM_" + toUtf8(parts()[i].name)});
    for (size_t i = 0; i < presetNames().size(); ++i) a.push_back({1, (int)i, "M_" + toUtf8(presetNames()[i])});
    a.push_back({2, SCENE_CORNELL, "Cornell Box"});
    a.push_back({2, SCENE_STUDIO, "Village"});
    a.push_back({2, SCENE_LOOKDEV, "LookDev"});
    return a;
}

} // namespace

// =========================================================================== frame

void App::spDraw(float X, float Y, float W, float H) {
    // resources: icon atlas, CSS-sized fonts
    if (!m_sp.loaded) {
        m_sp.loaded = true;
        vp3d::Texture t;
        if (vp3d::loadImage(m_exeDir + L"\\assets\\ui\\spartan_icons.png", t) && t.width) {
            m_sp.icons = m_gui.createTexture(t.rgba.data(), t.width, t.height);
            m_sp.iconsW = (float)t.width;
            m_sp.iconsH = (float)t.height;
        } else log("Spartan: assets/ui/spartan_icons.png not found (icons hidden)", 2);
    }
    C.atlas = m_sp.icons;
    C.aw = m_sp.iconsW;
    C.ah = m_sp.iconsH;
    C.k = m_dpi * th::layoutSize();
    C.ui = m_gui.spUi; C.semi = m_gui.spSemi; C.hud = m_gui.spHud; C.mono = m_gui.spMono; C.monoBold = m_gui.spMonoBold;
    C.dl = ImGui::GetWindowDrawList();
    ImGui::SetWindowFontScale(1.0f);   // the classic layout scales the window font; Spartan sizes its fonts itself
    ImGui::PushFont(C.ui, FP(13));

    // grid of the mockup: 44 top bar, 26 status bar; columns 48 | 264 | 1fr | 300; rows 1fr | 210 (the drawer spans 2 and 3)
    const float top = Y + P(44), status = P(26), body = H - P(44) - status;
    Rect(X, Y, W, H, cBase());
    spTopBar(X, Y, W, P(44));
    m_atelierTop = P(44);   // the game layer draws the Graph workspace below the top bar
    spStatus(X, Y + H - status, W, status);
    if (m_workspace == 0) {
        AtelierState& A = m_atl;
        const float railW = P(48);
        A.leftW = std::clamp(A.leftW, 200.0f, std::max(200.0f, W / C.k - 760));
        A.rightW = std::clamp(A.rightW, 260.0f, std::max(260.0f, W / C.k - 760));
        A.bottomH = std::clamp(A.bottomH, 150.0f, std::max(150.0f, body / C.k - 220));
        const float leftW = P(A.leftW), rightW = P(A.rightW), drawerH = P(A.bottomH);
        const float viewX = X + railW + leftW, viewW = W - railW - leftW - rightW, rowH = body - drawerH;
        spRail(X, top, railW, body);
        spScenePanel(X + railW, top, leftW, rowH);
        spViewport(viewX, top, viewW, rowH);
        spDrawer(X + railW, top + rowH, leftW + viewW, drawerH);
        spInspector(X + W - rightW, top, rightW, body);
        C.dl = ImGui::GetWindowDrawList();
        // splitters on the panel borders
        auto split = [&](const char* id, float x, float y, float w, float h, float& v, bool vert, bool rev) {
            bool hv;
            Hit(id, x, y, w, h, &hv);
            if (hv || ImGui::IsItemActive()) {
                ImGui::SetMouseCursor(vert ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
                Rect(vert ? x + w / 2 - 1 : x, vert ? y : y + h / 2 - 1, vert ? 2 : w, vert ? h : 2, alpha(cAccent(), 0.5f));
            }
            if (ImGui::IsItemActive()) v += (vert ? ImGui::GetIO().MouseDelta.x : ImGui::GetIO().MouseDelta.y) / C.k * (rev ? -1 : 1);
        };
        split("spSplitL", viewX - P(3), top, P(6), rowH, A.leftW, true, false);
        split("spSplitR", X + W - rightW - P(3), top, P(6), body, A.rightW, true, true);
        split("spSplitB", X + railW, top + rowH - P(3), leftW + viewW, P(6), A.bottomH, false, true);
    } else if (m_workspace == 2) {
        spMaterials(X, top, W, body);
    } else if (m_workspace == 3) {
        spPrefab(X, top, W, body);
    }
    // toast (status messages)
    if (!m_status.empty() && GetTickCount64() - m_statusTime < 3500) {
        std::string t = toUtf8(m_status);
        const float tw = TextW(C.ui, 13, t.c_str()) + P(28), tx = X + (W - tw) / 2, ty = Y + H - status - P(48);
        Rect(tx, ty, tw, P(30), alpha(cPanel(), 0.96f), P(2));
        Border(tx, ty, tw, P(30), m_statusLevel == 2 ? alpha(kWarn, 0.6f) : cAccentLo(), P(2));
        TextMid(C.ui, 13, tx + P(14), ty, P(30), m_statusLevel == 2 ? kWarn : cText(), t.c_str());
    }
    ImGui::PopFont();
}

// --------------------------------------------------------------------------- top bar (.top)
void App::spTopBar(float x0, float y0, float W, float h) {
    Rect(x0, y0, W, h, cPanel());
    HLine(x0, x0 + W, y0 + h - 1, cLine());
    // brand (.brand, 196 px)
    float x = x0 + P(14);
    DrawIcon(I_BRAND, x, y0 + (h - P(22)) / 2, 22, cAccent());
    const float tx = x + P(22 + 10), ty = y0 + (h - P(26)) / 2;
    Text(C.hud, 14, tx, ty - P(1), cText(), "WZX", 0.22f);
    Text(C.hud, 9, tx, ty + P(17), cText3(), "ENGINE", 0.38f);
    x += P(196 + 18);
    // menus (the items are the classic ones)
    const char* menus[] = {"Fichier", "Édition", "Créer", "Affichage", "Outils", "Aide"};
    for (int mi = 0; mi < 6; ++mi) {
        const float w = TextW(C.ui, 13, menus[mi]) + P(18), mh = P(30), my = y0 + (h - mh) / 2;
        std::string id = "atlmenu" + std::to_string(mi);
        bool hv;
        if (Hit(id.c_str(), x, my, w, mh, &hv)) ImGui::OpenPopup(id.c_str());
        const bool open = ImGui::IsPopupOpen(id.c_str());
        if (hv || open) Rect(x, my, w, mh, cHover(), P(2));
        TextMid(C.ui, 13, x + P(9), my, mh, hv || open ? cText() : cText2(), menus[mi]);
        ImGui::SetNextWindowPos(ImVec2(x, my + mh + P(2)));
        if (ImGui::BeginPopup(id.c_str())) {
            ImGui::PushFont(C.ui, FP(13));
            atlMenuItems(mi);
            ImGui::PopFont();
            ImGui::EndPopup();
        }
        x += w + P(2);
    }
    x += P(18 + 12 - 2);
    // workspaces (.ws): joined tabs
    const char* ws[] = {"Scène", "Graph", "Matériaux", "Prefab"};
    const int wsIcon[] = {I_MAP, I_WORKFLOW, I_PALETTE, I_PACKAGE};
    float wsW[4], total = 0;
    for (int i = 0; i < 4; ++i) { wsW[i] = P(14 + 16 + 7 + 14) + TextW(C.hud, 13, Upper(ws[i]).c_str(), 0.14f); total += wsW[i]; }
    const float wh = P(27), wy = y0 + (h - wh) / 2;
    float wx = x;
    for (int i = 0; i < 4; ++i) {
        char id[16];
        snprintf(id, sizeof(id), "spWs%d", i);
        bool hv;
        if (Hit(id, wx, wy, wsW[i], wh, &hv)) m_workspace = i;
        const bool on = m_workspace == i;
        if (on) { Rect(wx, wy, wsW[i], wh, cPanel2()); Rect(wx, wy + wh - P(2), wsW[i], P(2), cAccent()); }
        else if (hv) Rect(wx, wy, wsW[i], wh, cHover());
        const ImU32 c = on || hv ? cText() : cText2();
        DrawIcon(wsIcon[i], wx + P(14), wy + (wh - P(16)) / 2, 16, c);
        TextMid(C.hud, 13, wx + P(14 + 16 + 7), wy, wh, c, Upper(ws[i]).c_str(), 0.14f);
        wx += wsW[i];
        if (i < 3) VLine(wx - 1, wy, wy + wh, cLine());
    }
    Border(x, wy, total, wh, cLine());
    // right (.play): save, undo, redo, Play F5, Build F7, settings
    const bool playing = m_game && m_game->playing();
    const char* playLabel = playing ? "Stop F5" : "Play F5";
    float bx = x0 + W - P(14), by = y0 + (h - P(28)) / 2;
    auto left = [&](float w) { bx -= w; float r = bx; bx -= P(6); return r; };
    if (IconButton("spSettings", left(P(28)), by, P(28), I_SETTINGS, m_showSettings, false, "Réglages : thème, couleurs, tailles")) m_showSettings = !m_showSettings;
    float bw = TextButtonW(I_HAMMER, "Build F7");
    if (TextButton("spBuild", left(bw), by, bw, P(28), I_HAMMER, "Build F7", 2, "Build (F7) : choisir un profil de plateforme, puis Build ou Build and Run", m_game != nullptr)) m_game->openBuildWindow();
    bw = TextButtonW(I_PLAY, playLabel);
    if (TextButton("spPlay", left(bw), by, bw, P(28), I_PLAY, playLabel, 1, playing ? "Stop (F5) : la scène revient à son état d'avant Play" : "Play (F5) : teste le jeu dans le viewport avec son graph", m_game != nullptr)) m_game->requestPlay();
    if (IconButton("spRedo", left(P(28)), by, P(28), I_REDO, false, false, "Rétablir (Ctrl+Y)", !m_redo.empty())) redo();
    if (IconButton("spUndo", left(P(28)), by, P(28), I_UNDO, false, false, "Annuler (Ctrl+Z)", !m_undo.empty())) undo();
}

// --------------------------------------------------------------------------- icon rail (.rail): the inspector pages
void App::spRail(float x, float y, float w, float h) {
    Rect(x, y, w, h, cPanel());
    VLine(x + w - 1, y, y + h, cLine());
    AtelierState& A = m_atl;
    auto item = [&](const char* id, int icon, bool on, const char* tip, float ry) {
        const float rx = x + (w - P(36)) / 2;
        bool hv;
        bool q = Hit(id, rx, ry, P(36), P(36), &hv);
        if (on) { Rect(rx, ry, P(36), P(36), cPanel2(), P(2)); Rect(x, ry + P(9), P(2), P(18), cAccent()); }
        else if (hv) Rect(rx, ry, P(36), P(36), cHover(), P(2));
        DrawIcon(icon, rx + P(8.5f), ry + P(8.5f), 19, on || hv ? cText() : cText3());
        Tip(tip);
        return q;
    };
    float ry = y + P(8);
    const int pages[] = {I_LAYERS, I_SUN, I_CAMERA, I_GLOBE};
    const char* tips[] = {"Objet : transform, matériau, réglages de l'objet sélectionné", "Lumières : lumières et soleil",
                          "Rendu : caméra, moteur de rendu, illumination globale", "Monde : ciel, environnement, grille"};
    for (int i = 0; i < 4; ++i) {
        char id[16];
        snprintf(id, sizeof(id), "spRail%d", i);
        if (item(id, pages[i], A.inspectorTab == i, tips[i], ry)) A.inspectorTab = i;
        ry += P(38);
    }
    ry += P(4);
    HLine(x + (w - P(20)) / 2, x + (w + P(20)) / 2, ry, cLine());
    ry += P(9);
    if (item("spRailScenes", I_MAP, false, "Ouvrir une scène", ry)) ImGui::OpenPopup("spScenes");
    ImGui::SetNextWindowPos(ImVec2(x + w + P(4), ry));
    if (ImGui::BeginPopup("spScenes")) {
        ImGui::PushFont(C.ui, FP(13));
        const char* scenes[] = {"Cornell Box", "Village", "LookDev"};
        for (int i = 0; i < 3; ++i)
            if (ImGui::MenuItem(scenes[i], nullptr, m_sceneId == i) && m_sceneId != i) m_pendingScene = i;
        ImGui::PopFont();
        ImGui::EndPopup();
    }
    ry += P(38);
    if (item("spRailSearch", I_SEARCH, false, "Rechercher dans la scène", ry)) A.searchFocus = true;
    if (item("spRailConsole", I_TERMINAL, m_sp.drawerTab == 1, "Console", y + h - P(8 + 36))) m_sp.drawerTab = m_sp.drawerTab == 1 ? 0 : 1;
}

// --------------------------------------------------------------------------- scene panel: the Scene Manager rows
void App::spScenePanel(float x, float y, float w, float h) {
    AtelierState& A = m_atl;
    outlinerIds();
    Rect(x, y, w, h, cPanel());
    VLine(x + w - 1, y, y + h, cLine());
    size_t count = m_scene.lights.size() + 1;
    for (const Object& o : m_scene.objects) count += !o.ghost && !(o.part == "House" && o.grab == -2);
    PanelHeader(x, y, w - 1, "Scène", Upper(m_scene.name.empty() ? std::string("Scène") : m_scene.name) + " · " + std::to_string(count) + " OBJETS");
    SearchField("##spSceneSearch", x + P(12), y + P(42), w - P(24), A.sceneSearch, sizeof(A.sceneSearch), "Rechercher…", A.searchFocus);
    A.searchFocus = false;
    // add buttons, under the search
    const float by = y + P(42 + 28 + 8), half = (w - P(24) - P(6)) / 2;
    float abw = half;
    if (TextButton("spAddObj", x + P(12), by, abw, P(26), I_PLUS, "Objet", 0, "Ajouter une primitive (ou glisser un asset du tiroir)")) ImGui::OpenPopup("spNewObject");
    if (ImGui::BeginPopup("spNewObject")) {
        ImGui::PushFont(C.ui, FP(13));
        for (const PartDef& p : parts())
            if (ImGui::MenuItem(toUtf8(p.name).c_str())) addPartFromLibrary(p);
        ImGui::PopFont();
        ImGui::EndPopup();
    }
    if (TextButton("spAddCol", x + P(12) + half + P(6), by, abw, P(26), I_FOLDER_OPEN, "Collection", 0, "Nouvelle collection (reçoit la sélection)")) {
        pushUndo();
        uint32_t c = addCollection("Collection", (int)(m_scene.collections.size() % 10));
        std::vector<uint32_t> sel = selectedNodeIds();
        if (!sel.empty()) { size_t depth = m_undo.size(); moveNodes(sel, 2, c); if (m_undo.size() > depth) m_undo.pop_back(); }
        startRename(c);
    }
    // rows (.tree / .row): 25 px, indent 16 per level
    const float listY = by + P(26 + 8);
    ImGui::SetCursorScreenPos(ImVec2(x + P(6), listY));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("spTree", ImVec2(w - 1 - P(12), std::max(P(20), y + h - listY - P(6))), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();
    C.dl = ImGui::GetWindowDrawList();
    const float rx = ImGui::GetCursorScreenPos().x, rw = ImGui::GetContentRegionAvail().x;
    float ry = ImGui::GetCursorScreenPos().y;
    const bool ctrl = ImGui::GetIO().KeyCtrl, shift = ImGui::GetIO().KeyShift;
    std::vector<OutlinerRow> rows;
    outlinerRows(rows, A.sceneSearch);
    static uint32_t ctxId = 0;
    // fixed first row: the scene camera
    {
        bool hv;
        if (Hit("spCamRow", rx, ry, rw, P(25), &hv)) { select(-1); A.inspectorTab = 2; }
        if (hv) Rect(rx, ry, rw, P(25), alpha(cHover(), 0.6f), P(2));
        DrawIcon(I_VIDEO, rx + P(8 + 16 + 7), ry + P(4.5f), 16, cText3());
        TextMid(C.ui, 13, rx + P(8 + 16 + 7 + 16 + 7), ry, P(25), cText2(), "Caméra de scène");
        Tip("La caméra de la scène (ses réglages : page Rendu du rail)");
        ry += P(25);
    }
    for (size_t ri = 0; ri < rows.size(); ++ri) {
        const OutlinerRow& row = rows[ri];
        const NodeRef n = row.node;
        const bool isCol = n.kind == NODE_COLLECTION, sel = nodeSelected(n), vis = nodeVisible(n), locked = nodeLocked(n);
        char id[32];
        snprintf(id, sizeof(id), "spRow%u", row.id);
        ImGui::PushID(id);
        bool hv;
        bool q = Hit("row", rx, ry, rw - P(44), P(25), &hv);
        const bool dbl = hv && ImGui::IsMouseDoubleClicked(0);
        if (hv && ImGui::IsMouseReleased(1)) { ctxId = row.id; ImGui::OpenPopup("spRowCtx"); if (!sel && (isCol || !locked)) selectNodes({n}, n); }
        // drag the row onto the viewport or another row: same payload as the classic Scene Manager
        if (ImGui::BeginDragDropSource()) {
            m_outDrag = sel ? selectedNodeIds() : std::vector<uint32_t>{row.id};
            int one = 1;
            ImGui::SetDragDropPayload("WZX_NODE", &one, sizeof(one));
            ImGui::TextUnformatted(nodeName(n).c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("WZX_NODE")) { (void)pl; moveNodes(m_outDrag, 2, row.id); m_outDrag.clear(); }
            ImGui::EndDragDropTarget();
        }
        if (sel) { Rect(rx, ry, rw, P(25), cAccentBg(), P(2)); Rect(rx, ry, P(2), P(25), cAccent()); }
        else if (hv) Rect(rx, ry, rw, P(25), alpha(cHover(), 0.6f), P(2));
        float cx = rx + P(8) + P(16) * row.depth;
        const ImU32 ic = sel ? cAccent() : cText3();
        if (row.hasChildren || isCol) DrawIcon(row.open ? I_CHEV_DOWN : I_CHEV_RIGHT, cx, ry + P(4.5f), 16, ic);
        cx += P(16 + 7);
        int icon = I_BOX;
        if (isCol) icon = I_FOLDER_OPEN;
        else if (n.kind == NODE_OBJECT) {
            const Object& o = m_scene.objects[n.index];
            icon = o.group ? I_LAYERS : (o.part == "House" ? I_OUTPOST : I_BOX);
        } else if (n.kind == NODE_LIGHT) icon = I_ZAP;
        else if (n.kind == NODE_SUN) icon = I_SUN;
        DrawIcon(icon, cx, ry + P(4.5f), 16, vis ? ic : alpha(ic, 0.5f));
        cx += P(16 + 7);
        if (m_renameId == row.id) {
            ImGui::SetCursorScreenPos(ImVec2(cx - P(4), ry + P(1)));
            ImGui::SetNextItemWidth(std::max(P(40), rx + rw - P(48) - cx));
            if (m_renameFocus) { ImGui::SetKeyboardFocusHere(); m_renameFocus = false; }
            bool enter = ImGui::InputText("##rename", m_renameBuf, sizeof(m_renameBuf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            if (enter || ImGui::IsItemDeactivated()) {
                const bool cancel = !enter && ImGui::IsKeyPressed(ImGuiKey_Escape, false);
                if (!cancel && m_renameBuf[0] && nodeName(n) != m_renameBuf) { pushUndo(); setNodeName(n, m_renameBuf); }
                m_renameMulti.clear();
                m_renameId = 0;
            }
        } else {
            C.dl->PushClipRect(ImVec2(cx, ry), ImVec2(rx + rw - P(48), ry + P(25)), true);
            const ImU32 nc = !vis ? cText3() : sel ? cText() : (locked ? IM_COL32(150, 204, 156, 255) : cText2());
            TextMid(isCol ? C.semi : C.ui, 13, cx, ry, P(25), nc, nodeName(n).c_str());
            C.dl->PopClipRect();
        }
        // eye / lock on the right (.vis, opacity .5)
        bool he, hl;
        if (Hit("eye", rx + rw - P(44), ry, P(20), P(25), &he)) { pushUndo(); setNodeVisible(n, !vis); }
        Tip(vis ? "Masquer" : "Afficher");
        DrawIcon(I_EYE, rx + rw - P(42), ry + P(4.5f), 16, alpha(cText3(), vis ? (he ? 1.0f : 0.55f) : 0.25f));
        if (Hit("lock", rx + rw - P(22), ry, P(20), P(25), &hl)) { pushUndo(); setNodeLocked(n, !locked); }
        Tip(locked ? "Déverrouiller" : "Verrouiller (non sélectionnable)");
        DrawIcon(I_SHIELD, rx + rw - P(20), ry + P(4.5f), 16, locked ? cAccent() : alpha(cText3(), hl ? 0.9f : 0.3f));
        ImGui::PopID();
        if (dbl && n.kind != NODE_SUN) startRename(row.id);
        else if (q && m_renameId != row.id) {
            const bool onArrow = ImGui::GetMousePos().x < rx + P(8) + P(16) * row.depth + P(16 + 4) && (row.hasChildren || isCol);
            if (onArrow) {
                if (isCol) m_scene.collections[n.index].collapsed = row.open;
                else if (n.kind == NODE_OBJECT) m_scene.objects[n.index].collapsed = row.open;
            } else if (!isCol && locked) status(L"Verrouillé : déverrouillez-le pour le sélectionner", 2);
            else if (ctrl) { toggleNodeSelected(n); m_outAnchor = row.id; }
            else if (shift && m_outAnchor) {
                int a = -1, b = (int)ri;
                for (size_t k2 = 0; k2 < rows.size(); ++k2) if (rows[k2].id == m_outAnchor) a = (int)k2;
                if (a < 0) a = b;
                std::vector<NodeRef> range;
                for (int k2 = std::min(a, b); k2 <= std::max(a, b); ++k2)
                    if (rows[k2].node.kind != NODE_COLLECTION && !nodeLocked(rows[k2].node)) range.push_back(rows[k2].node);
                selectNodes(range, n);
            } else { selectNodes({n}, n); m_outAnchor = row.id; }
        }
        ry += P(25);
    }
    // empty area: click clears the selection, drops an asset
    {
        const float rest = std::max(ImGui::GetWindowPos().y + ImGui::GetWindowHeight() - ry, P(40));
        bool hv;
        if (Hit("spTreeEmpty", rx, ry, rw, rest, &hv) && !ctrl && !shift) select(-1);
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("WZX_ASSET")) addPartFromLibrary(parts()[*(const int*)pl->Data]);
            if (ImGui::AcceptDragDropPayload("WZX_NODE")) { moveNodes(m_outDrag, 3, 0); m_outDrag.clear(); }
            ImGui::EndDragDropTarget();
        }
    }
    if (ImGui::BeginPopup("spRowCtx")) {
        ImGui::PushFont(C.ui, FP(13));
        NodeRef n = findNode(ctxId);
        if (!n.valid()) ImGui::CloseCurrentPopup();
        else {
            ImGui::TextDisabled("%s", nodeName(n).c_str());
            if (ImGui::MenuItem("Cadrer", "F")) frameSelection();
            if (n.kind != NODE_SUN && ImGui::MenuItem("Renommer", "F2")) startRename(ctxId);
            if (n.kind == NODE_OBJECT && ImGui::MenuItem("Dupliquer", "Ctrl+D")) duplicateSelected();
            if ((n.kind == NODE_OBJECT || n.kind == NODE_LIGHT) && ImGui::MenuItem("Grouper", "Ctrl+G")) groupSelection();
            if (nodeParent(n) && ImGui::MenuItem("Détacher du parent")) moveNodes(selectedNodeIds(), 3, 0);
            ImGui::Separator();
            if (n.kind != NODE_SUN && ImGui::MenuItem("Supprimer", "Suppr")) deleteSelected();
        }
        ImGui::PopFont();
        ImGui::EndPopup();
    }
    ImGui::EndChild();
    C.dl = ImGui::GetWindowDrawList();
}

// --------------------------------------------------------------------------- viewport (.view)
void App::spViewport(float x, float y, float w, float h) {
    Rect(x, y, w, h, IM_COL32(15, 19, 23, 255));
    viewportCanvas(ImVec2(x, y), ImVec2(w, h));   // picture, input, gizmo
    C.dl = ImGui::GetWindowDrawList();
    C.dl->PushClipRect(ImVec2(x, y), ImVec2(x + w, y + h), true);
    ImGuiIO& io = ImGui::GetIO();
    if (m_marq.active && m_marq.moved) {
        ImVec2 a(std::min(m_marq.x0, io.MousePos.x), std::min(m_marq.y0, io.MousePos.y)), b(std::max(m_marq.x0, io.MousePos.x), std::max(m_marq.y0, io.MousePos.y));
        C.dl->AddRectFilled(a, b, alpha(cAccent(), 0.10f));
        C.dl->AddRect(a, b, alpha(cAccent(), 0.6f));
    }
    // selection reticle (four corner brackets) and its label
    if (m_selected >= 0 && m_selected < (int)m_scene.objects.size()) {
        const Object& so = m_scene.objects[m_selected];
        float3 a, b;
        aabbOf(so, so.position, objRot(so), a, b);
        ImVec2 mn(1e9f, 1e9f), mx(-1e9f, -1e9f);
        bool ok = true;
        for (int i = 0; i < 8 && ok; ++i) {
            float2 sp;
            ok = project(float3(i & 1 ? b.x : a.x, i & 2 ? b.y : a.y, i & 4 ? b.z : a.z), sp);
            mn = ImVec2(std::min(mn.x, sp.x), std::min(mn.y, sp.y));
            mx = ImVec2(std::max(mx.x, sp.x), std::max(mx.y, sp.y));
        }
        if (ok && mx.x - mn.x < w * 1.2f) {
            const float pad = P(8);
            atelier::fx::Reticle(C.dl, ImVec2(mn.x - pad, mn.y - pad), ImVec2(mx.x + pad, mx.y + pad), P(14), cAccent(), P(1.5f));
            std::string tag = Upper(so.name);
            Text(C.mono, 10.5f, mx.x + pad + P(8), mn.y - pad, cAccent(), tag.c_str(), 0.04f);
        }
    }
    // orientation axes, bottom left
    {
        float3 cr, cu, cf;
        m_scene.camera.basis(cr, cu, cf);
        ImVec2 o(x + P(34), y + h - P(34));
        const char* names[] = {"X", "Y", "Z"};
        for (int a = 0; a < 3; ++a) {
            float3 av(a == 0 ? 1.0f : 0.0f, a == 1 ? 1.0f : 0.0f, a == 2 ? 1.0f : 0.0f);
            ImVec2 e(o.x + dot(av, cr) * P(22), o.y - dot(av, cu) * P(22));
            C.dl->AddLine(o, e, kAxis[a], P(1.5f));
            Text(C.monoBold, 10, e.x + (e.x >= o.x ? P(3) : P(-9)), e.y - P(6), kAxis[a], names[a]);
        }
    }
    // path tracing progress
    if (!m_rs.raster) {
        const float prog = clampf((float)m_renderer.sampleCount() / m_rs.maxSamples, 0, 1);
        Rect(x + w / 2 - P(120), y + h - P(10), P(240), P(2), alpha(cBase(), 0.8f));
        Rect(x + w / 2 - P(120), y + h - P(10), P(240) * prog, P(2), prog >= 1 ? kOk : cAccent());
    }
    C.dl->PopClipRect();
    // floating tools (.vtools): select, move, rotate, scale | grid snap (magnet), ground grid, local axes
    {
        bool gridOn = false;
        for (const Object& o : m_scene.objects)
            if (o.part == "GROUND" && o.material >= 0 && o.material < (int)m_scene.materials.size()) gridOn = (m_scene.materials[o.material].flags & 1u) != 0;
        const float bw = P(3 + 28 * 4 + 2 * 3 + 2 + 7 + 2 + 28 * 3 + 2 * 2 + 3), bh = P(34), bx = x + P(10), by = y + P(10);
        Rect(bx, by, bw, bh, alpha(cPanel(), 0.92f), P(2));
        Border(bx, by, bw, bh, cLine(), P(2));
        float tx = bx + P(3);
        const int icons[] = {I_POINTER, I_MOVE, I_ROTATE, I_SCALING};
        const char* tips[] = {"Sélection (Q)", "Déplacer (W)", "Tourner par pas de 90° (E)", "Échelle (R)"};
        for (int i = 0; i < 4; ++i) {
            char id[16];
            snprintf(id, sizeof(id), "spTool%d", i);
            if (IconButton(id, tx, by + P(3), P(28), icons[i], (int)m_tool == i, true, tips[i])) m_tool = (Tool)i;
            tx += P(30);
        }
        VLine(tx + P(3), by + P(7), by + bh - P(7), cLine());
        tx += P(9);
        if (IconButton("spSnap", tx, by + P(3), P(28), I_MAGNET, m_magnet, true, "Accrochage : 1 cm / 90° / x0.1")) m_magnet = !m_magnet;
        tx += P(30);
        if (IconButton("spGrid", tx, by + P(3), P(28), I_GRID, gridOn, true, "Grille au sol")) toggleGroundGrid();
        tx += P(30);
        if (IconButton("spSpace", tx, by + P(3), P(28), I_GLOBE, m_localSpace, true, m_localSpace ? "Axes du gizmo : objet (local)" : "Axes du gizmo : scène (monde)")) m_localSpace = !m_localSpace;
    }
    // chips (.vinfo), right to left: fps / statistics, render mode, display, view, frame, village placement
    {
        float cx = x + w - P(10);
        const float cy = y + P(10), ch = P(26);
        auto chip = [&](const char* id, int icon, const std::string& a, const std::string& b, bool on) {
            const float cw = P(10 + 10) + (icon >= 0 ? P(16 + 6) : 0) + TextW(C.ui, 12, a.c_str()) + (b.empty() ? 0 : TextW(C.mono, 12, b.c_str()));
            cx -= cw;
            const float x0 = cx;
            cx -= P(6);
            bool hv;
            bool q = Hit(id, x0, cy, cw, ch, &hv);
            Rect(x0, cy, cw, ch, hv ? alpha(cHover(), 0.95f) : alpha(cPanel(), 0.92f), P(2));
            Border(x0, cy, cw, ch, on ? cAccentLo() : cLine(), P(2));
            float tx = x0 + P(10);
            if (icon >= 0) { DrawIcon(icon, tx, cy + P(5), 16, on ? cAccent() : cText2()); tx += P(16 + 6); }
            tx = TextMid(C.ui, 12, tx, cy, ch, cText2(), a.c_str());
            if (!b.empty()) TextMid(C.mono, 12, tx, cy, ch, cText(), b.c_str());
            return std::make_pair(q, x0);
        };
        char fps[16];
        snprintf(fps, sizeof(fps), "%.0f", 1000.0f / std::max(m_frameMs, 0.1f));
        if (chip("spFps", -1, "", std::string(fps) + " fps", m_showStats).first) m_showStats = !m_showStats;
        Tip("Statistiques du viewport");
        static const char* modes[] = {"Lit", "Metal iOS", "Progressive", "GI"};
        static const char* modeTag[] = {"Raster", "iOS", "Path", "DDGI"};
        const int mode = renderMode();
        auto rm = chip("spMode", I_SPARKLES, std::string(modes[std::clamp(mode, 0, 3)]) + " · ", modeTag[std::clamp(mode, 0, 3)], false);
        if (rm.first) ImGui::OpenPopup("spModePopup");
        Tip("Rendu : Lit (raster temps réel), Metal iOS (sondes GI sans ray tracing), Progressive (path tracing), GI (sondes DDGI)");
        ImGui::SetNextWindowPos(ImVec2(rm.second, cy + ch + P(2)));
        if (ImGui::BeginPopup("spModePopup")) {
            for (int i = 0; i < 4; ++i) if (ImGui::Selectable(modes[i], mode == i)) setRenderMode(i);
            ImGui::EndPopup();
        }
        static const char* displays[] = {"Ombré", "Filaire", "Filaire + ombré"};
        auto dp = chip("spDisplay", I_BOX, displays[std::clamp(m_rs.wireframe, 0, 2)], "", m_rs.wireframe != 0);
        if (dp.first) ImGui::OpenPopup("spDisplayPopup");
        ImGui::SetNextWindowPos(ImVec2(dp.second, cy + ch + P(2)));
        if (ImGui::BeginPopup("spDisplayPopup")) {
            for (int i = 0; i < 3; ++i) if (ImGui::Selectable(displays[i], m_rs.wireframe == i)) { m_rs.wireframe = i; markDirty(); }
            ImGui::EndPopup();
        }
        static const char* views[] = {"Perspective", "Face", "Arrière", "Gauche", "Droite", "Dessus"};
        auto vw = chip("spView", I_CAMERA, views[std::clamp(m_viewMode, 0, 5)], "", false);
        if (vw.first) ImGui::OpenPopup("spViewPopup");
        ImGui::SetNextWindowPos(ImVec2(vw.second, cy + ch + P(2)));
        if (ImGui::BeginPopup("spViewPopup")) {
            for (int i = 0; i < 6; ++i) if (ImGui::Selectable(views[i], m_viewMode == i)) { m_viewMode = i; setViewAxis(i); }
            ImGui::EndPopup();
        }
        if (chip("spFrame", I_MAXIMIZE, "Cadrer", "", false).first) frameSelection();
        Tip("Cadrer la sélection (F)");
        const bool placing = m_game && m_game->placementMode();
        if (chip("spVillage", I_OUTPOST, "Village", "", placing).first && m_game) m_game->togglePlacement();
        Tip("Village RTS : placer des maisons sur la grille (hors Play)");
        // statistics box under the chips
        if (m_showStats) {
            size_t objs = 0;
            for (auto& o : m_scene.objects) if (o.visible && o.part != "WALL" && o.part != "GROUND") ++objs;
            const float sw = P(176), sx = x + w - P(10) - sw, sy = cy + ch + P(8);
            Rect(sx, sy, sw, P(108), alpha(cPanel(), 0.92f), P(2));
            Border(sx, sy, sw, P(108), cLine(), P(2));
            CornerTick(sx, sy);
            char v[5][32];
            snprintf(v[0], 32, "%.1f ms", m_frameMs);
            snprintf(v[1], 32, "%.2f ms", m_renderer.gpuFrameMs());
            snprintf(v[2], 32, "%zu", m_scene.triangleCount());
            snprintf(v[3], 32, "%zu", objs);
            snprintf(v[4], 32, "%zu", m_scene.lights.size());
            const char* names[] = {"Image", "GPU", "Triangles", "Objets", "Lumières"};
            for (int i = 0; i < 5; ++i) {
                const float ly = sy + P(8 + 19 * i);
                TextMid(C.ui, 12, sx + P(10), ly, P(18), cText3(), names[i]);
                TextMid(C.mono, 11.5f, sx + sw - P(10) - TextW(C.mono, 11.5f, v[i]), ly, P(18), cText(), v[i]);
            }
        }
    }
}

// --------------------------------------------------------------------------- bottom drawer (.drawer): Assets, Console, Graph
void App::spDrawer(float x, float y, float w, float h) {
    Rect(x, y, w, h, cPanel());
    HLine(x, x + w, y, cLine());
    const char* tabs[] = {"Assets", "Console", "Graph"};
    const int icons[] = {I_FOLDER_OPEN, I_TERMINAL, I_WORKFLOW};
    float tx = x + P(6);
    for (int i = 0; i < 3; ++i) {
        const std::string up = Upper(tabs[i]);
        const float tw = P(14 + 16 + 7 + 14) + TextW(C.hud, 12, up.c_str(), 0.16f);
        char id[16];
        snprintf(id, sizeof(id), "spTab%d", i);
        bool hv;
        if (Hit(id, tx, y + 1, tw, P(32) - 1, &hv)) { if (i == 2) m_workspace = 1; else m_sp.drawerTab = i; }
        const bool on = m_sp.drawerTab == i;
        const ImU32 c = on || hv ? cText() : cText3();
        DrawIcon(icons[i], tx + P(14), y + P(8), 16, c);
        TextMid(C.hud, 12, tx + P(14 + 16 + 7), y, P(32), c, up.c_str(), 0.16f);
        if (on) Rect(tx + P(10), y + P(30), tw - P(20), P(2), cAccent());
        tx += tw;
    }
    HLine(x, x + w, y + P(32) - 1, cLine());
    AtelierState& A = m_atl;
    const float by = y + P(32);
    if (m_sp.drawerTab == 0) {
        // search and folder filter on the right of the tab row
        const char* folders[] = {"Tout", "Primitives", "Matériaux", "Scènes"};
        const float sw = std::min(P(220), std::max(P(100), x + w - tx - P(150)));
        SearchField("##spAssetSearch", x + w - P(10) - sw, y + P(2), sw, A.assetSearch, sizeof(A.assetSearch), "Rechercher…");
        std::vector<std::string> fo(folders, folders + 4);
        Select("##spFolder", x + w - P(10) - sw - P(8) - P(120), y + P(3.5f), P(120), &A.folder, fo);
        // tiles (.asset 96 x 116, chamfered), drag onto the viewport
        std::vector<Asset> assets = Assets();
        ImGui::SetCursorScreenPos(ImVec2(x, by));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("spAssets", ImVec2(w, y + h - by), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
        ImGui::PopStyleVar();
        C.dl = ImGui::GetWindowDrawList();
        float ax = x + P(12), ay = ImGui::GetCursorScreenPos().y + P(12);
        for (size_t i = 0; i < assets.size(); ++i) {
            const Asset& a = assets[i];
            if (A.folder != 0 && a.kind != A.folder - 1) continue;
            if (A.assetSearch[0]) {
                std::string n = Upper(a.name), q = Upper(A.assetSearch);
                if (n.find(q) == std::string::npos) continue;
            }
            if (ax + P(96) > x + w - P(12)) { ax = x + P(12); ay += P(116 + 10); }
            char id[16];
            snprintf(id, sizeof(id), "spAsset%zu", i);
            bool hv;
            if (Hit(id, ax, ay, P(96), P(116), &hv)) m_contentAsset = (int)i;
            if (hv && ImGui::IsMouseDoubleClicked(0)) {
                if (a.kind == 0) addPartFromLibrary(parts()[a.index]);
                else if (a.kind == 1) { if (m_selected >= 0) setPreset(a.index); else status(L"Sélectionnez d'abord un objet", 2); }
                else m_pendingScene = a.index;
            }
            if (a.kind != 2 && ImGui::BeginDragDropSource()) {
                int payload = a.index;
                ImGui::SetDragDropPayload(a.kind == 0 ? "WZX_ASSET" : "WZX_MATERIAL", &payload, sizeof(int));
                ImGui::TextUnformatted(a.name.c_str());
                ImGui::EndDragDropSource();
            }
            Tip(a.kind == 0 ? "Double-clic : ajouter · glisser dans le viewport" : a.kind == 1 ? "Double-clic : appliquer à la sélection · glisser sur un objet" : "Double-clic : ouvrir la scène");
            const bool sel = m_contentAsset == (int)i;
            Chamfer(ax, ay, P(96), P(116), P(8), cPanel2(), sel ? cAccentLo() : (hv ? cLineHi() : cLine()));
            const float nameH = P(27), thH = P(116) - nameH;
            drawAssetThumb(C.dl, a.kind, a.index, ImVec2(ax + P(18), ay + P(12)), ImVec2(ax + P(78), ay + thH - P(10)), m_dpi);
            HLine(ax + 1, ax + P(96) - 1, ay + thH, cLine());
            C.dl->PushClipRect(ImVec2(ax, ay + thH), ImVec2(ax + P(96) - P(6), ay + P(116)), true);
            TextMid(C.ui, 11, ax + P(8), ay + thH, nameH, sel ? cText() : cText2(), a.name.c_str());
            C.dl->PopClipRect();
            ax += P(96 + 10);
        }
        ImGui::SetCursorScreenPos(ImVec2(x, ay + P(116 + 12)));
        ImGui::Dummy(ImVec2(1, 1));
        ImGui::EndChild();
        C.dl = ImGui::GetWindowDrawList();
    } else {
        // console: the output log
        if (TextButton("spClearLog", x + w - P(10) - TextButtonW(-1, "Effacer"), y + P(3), TextButtonW(-1, "Effacer"), P(26), -1, "Effacer", 0)) m_log.clear();
        ImGui::SetCursorScreenPos(ImVec2(x + P(14), by + P(8)));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("spLog", ImVec2(w - P(28), y + h - by - P(12)), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
        ImGui::PopStyleVar();
        C.dl = ImGui::GetWindowDrawList();
        float lx = ImGui::GetCursorScreenPos().x, ly = ImGui::GetCursorScreenPos().y;
        for (const LogLine& l : m_log) {
            float ex = Text(C.mono, 11.5f, lx, ly, cText3(), l.time.c_str());
            Text(C.mono, 11.5f, ex + P(12), ly, l.level == 2 ? kWarn : (l.level == 1 ? kOk : cText2()), l.text.c_str());
            ly += P(20);
        }
        ImGui::SetCursorScreenPos(ImVec2(lx, ly + P(4)));
        ImGui::Dummy(ImVec2(1, 1));
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
        C.dl = ImGui::GetWindowDrawList();
    }
}

// --------------------------------------------------------------------------- inspector (.panel.right)
void App::spInspector(float x, float y, float w, float h) {
    AtelierState& A = m_atl;
    Rect(x, y, w, h, cPanel());
    VLine(x, y, y + h, cLine());
    const char* pages[] = {"Inspecteur", "Lumières", "Rendu", "Monde"};
    A.inspectorTab = std::clamp(A.inspectorTab, 0, 3);
    PanelHeader(x + 1, y, w - 1, pages[A.inspectorTab]);
    const float top = y + P(32);
    if (A.inspectorTab != 0) {   // the classic pages, drawn with the Spartan colours
        atlInspectorPage(A.inspectorTab, x + P(12), top + P(10), w - P(24), y + h - top - P(16));
        C.dl = ImGui::GetWindowDrawList();
        return;
    }
    Object* o = m_selected >= 0 && m_selected < (int)m_scene.objects.size() ? &m_scene.objects[m_selected] : nullptr;
    if (!o) {
        TextMid(C.semi, 13, x + P(12), top + P(8), P(24), cText(), m_scene.name.empty() ? "Scène" : m_scene.name.c_str());
        TextMid(C.ui, 13, x + P(12), top + P(32), P(20), cText3(), "Aucun objet sélectionné");
        const float bx = x + P(12), byy = top + P(64), bw = w - P(24);
        Rect(bx, byy, bw, P(84), cBase(), P(2));
        Border(bx, byy, bw, P(84), cLine(), P(2));
        CornerTick(bx, byy);
        Text(C.hud, 11, bx + P(12), byy + P(10), cAccent(), "SÉLECTION", 0.2f);
        Text(C.ui, 12, bx + P(12), byy + P(30), cText2(), "Clic dans le viewport ou la liste");
        Text(C.ui, 12, bx + P(12), byy + P(48), cText2(), "Maj+clic ajoute à la sélection");
        return;
    }
    ImGui::SetCursorScreenPos(ImVec2(x + 1, top));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("spInspector", ImVec2(w - 1, y + h - top), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();
    C.dl = ImGui::GetWindowDrawList();
    const float ix = ImGui::GetCursorScreenPos().x, iw = ImGui::GetContentRegionAvail().x;
    float sy = ImGui::GetCursorScreenPos().y;
    const float px = ix + P(12), fieldX = px + P(88 + 8), fieldW = ix + iw - P(12) - fieldX, cw = (fieldW - P(8)) / 3;
    bool act = false;
    // name row
    SectionHeader("spName", ix, sy, iw, o->part == "House" ? I_OUTPOST : I_BOX, o->name.c_str(), I_MORE);
    if (ImGui::IsItemClicked()) ImGui::OpenPopup("spObjMenu");
    if (ImGui::BeginPopup("spObjMenu")) {
        if (ImGui::MenuItem("Renommer", "F2")) startRename(o->id);
        if (ImGui::MenuItem("Dupliquer", "Ctrl+D")) duplicateSelected();
        if (ImGui::MenuItem("Cadrer", "F")) frameSelection();
        ImGui::Separator();
        if (ImGui::MenuItem("Supprimer", "Suppr")) deleteSelected();
        ImGui::EndPopup();
    }
    sy += P(30);
    HLine(ix, ix + iw, sy - 1, cLine());
    if (m_selected < 0) { ImGui::EndChild(); C.dl = ImGui::GetWindowDrawList(); return; }
    o = &m_scene.objects[m_selected];
    // Transform
    if (SectionHeader("spSecT", ix, sy, iw, I_MOVE3D, "Transform", m_sp.sect[0] ? I_CHEV_DOWN : I_CHEV_RIGHT)) m_sp.sect[0] = !m_sp.sect[0];
    sy += P(30);
    if (m_sp.sect[0]) {
        float3 e = eulerYXZ(objRot(*o));
        const char* rows[] = {"Position", "Rotation", "Échelle"};
        for (int r = 0; r < 3; ++r) {
            const float ry = sy + P(4);
            PropLabel(px, ry, P(25), rows[r]);
            for (int a = 0; a < 3; ++a) {
                char id[16];
                snprintf(id, sizeof(id), "##spT%d%d", r, a);
                const float fx = fieldX + a * (cw + P(4));
                if (r == 0) { if (AxisField(id, fx, ry, cw, a, &o->position.x + a, 0.1f, "%.1f", act)) markDirty(); }
                else if (r == 1) { if (AxisField(id, fx, ry, cw, a, &e.x + a, 0.5f, "%.0f", act)) { o->rot = quatYXZ(e); o->useRot = true; o->yaw = radians(e.y); markDirty(); } }
                else if (AxisField(id, fx, ry, cw, a, &o->scale.x + a, 0.01f, "%.2f", act)) { o->scale = maxv(o->scale, float3(0.01f)); markDirty(); }
            }
            sy += P(33);
        }
        sy += P(6);
    }
    HLine(ix, ix + iw, sy - 1, cLine());
    // Matériau
    if (SectionHeader("spSecM", ix, sy, iw, I_PALETTE, "Matériau", m_sp.sect[1] ? I_CHEV_DOWN : I_CHEV_RIGHT)) m_sp.sect[1] = !m_sp.sect[1];
    sy += P(30);
    if (m_sp.sect[1] && o->material >= 0 && o->material < (int)m_scene.materials.size()) {
        Material& m = m_scene.materials[o->material];
        std::vector<std::string> names;
        for (auto& n : presetNames()) names.push_back(toUtf8(n));
        int preset = m.preset;
        float ry = sy + P(4);
        PropLabel(px, ry, P(25), "Préréglage");
        if (Select("##spPreset", fieldX, ry, fieldW, &preset, names)) setPreset(preset);
        ry += P(33);
        PropLabel(px, ry, P(25), "Couleur");
        {
            float3& c = m.baseColor;
            ImGui::SetCursorScreenPos(ImVec2(fieldX, ry));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(P(7), std::max(0.0f, (P(25) - ImGui::GetFontSize()) / 2)));
            ImGui::SetNextItemWidth(fieldW);
            float col[3] = {c.x, c.y, c.z};
            if (ImGui::ColorEdit3("##spColor", col, ImGuiColorEditFlags_DisplayHex)) { c = float3(col[0], col[1], col[2]); markDirty(); }
            act |= ImGui::IsItemActivated();
            ImGui::PopStyleVar();
        }
        ry += P(33);
        PropLabel(px, ry, P(25), "Métal");
        if (SliderBar("##spMetal", fieldX, ry, fieldW, &m.metallic, 0, 1, "%.2f", act)) markDirty();
        ry += P(33);
        PropLabel(px, ry, P(25), "Rugosité");
        if (SliderBar("##spRough", fieldX, ry, fieldW, &m.roughness, 0, 1, "%.2f", act)) markDirty();
        sy = ry + P(25 + 12);
    }
    HLine(ix, ix + iw, sy - 1, cLine());
    // Unité RTS (houses of the village) and Grille: what the scene knows of them
    if (SectionHeader("spSecV", ix, sy, iw, I_GRID, "Grille", m_sp.sect[2] ? I_CHEV_DOWN : I_CHEV_RIGHT)) m_sp.sect[2] = !m_sp.sect[2];
    sy += P(30);
    if (m_sp.sect[2]) {
        float ry = sy + P(4);
        PropLabel(px, ry, P(18), "Accrochage");
        Toggle("##spMagnet", fieldX, ry + P(1), &m_magnet);
        ry += P(26);
        PropLabel(px, ry, P(18), "Visible");
        bool vis = o->visible;
        if (Toggle("##spVisible", fieldX, ry + P(1), &vis)) { pushUndo(); o->visible = vis; markDirty(); }
        sy = ry + P(18 + 12);
    }
    HLine(ix, ix + iw, sy - 1, cLine());
    // the rest of the classic Actor page (primitive parameters, lights of the object...) under "Avancé"
    if (SectionHeader("spSecA", ix, sy, iw, I_SETTINGS, "Avancé", m_sp.sect[3] ? I_CHEV_DOWN : I_CHEV_RIGHT)) m_sp.sect[3] = !m_sp.sect[3];
    sy += P(30);
    if (act) pushUndo();
    ImGui::SetCursorScreenPos(ImVec2(ix, sy));
    ImGui::Dummy(ImVec2(1, 1));
    ImGui::EndChild();
    C.dl = ImGui::GetWindowDrawList();
    if (m_sp.sect[3]) {
        const float ay = std::min(sy, y + h - P(60));
        if (ay < y + h - P(80)) atlInspectorPage(0, x + P(12), ay + P(8), w - P(24), y + h - ay - P(14));
        C.dl = ImGui::GetWindowDrawList();
    }
}

// --------------------------------------------------------------------------- status bar (.status)
void App::spStatus(float x, float y, float w, float h) {
    Rect(x, y, w, h, cBase());
    HLine(x, x + w, y, cLine());
    const bool busy = !m_rs.raster && m_renderer.sampleCount() < (uint32_t)m_rs.maxSamples;
    float tx = x + P(12);
    Rect(tx, y + (h - P(6)) / 2, P(6), P(6), busy ? kWarn : kOk);
    tx += P(12);
    tx = TextMid(C.mono, 11, tx, y, h, cText3(), busy ? "Rendu" : "Prêt") + P(16);
    tx = TextMid(C.mono, 11, tx, y, h, cText3(), ("DirectX 12 · " + m_renderer.adapterName()).c_str()) + P(16);
    TextMid(C.mono, 11, tx, y, h, cText3(), ("Scène : " + (m_scene.name.empty() ? std::string("sans titre") : m_scene.name)).c_str());
    size_t objs = 0;
    for (auto& o : m_scene.objects) if (o.visible && o.part != "WALL" && o.part != "GROUND") ++objs;
    const char* tools[] = {"Sélection", "Déplacer", "Tourner", "Échelle"};
    std::string right = std::string(tools[std::clamp((int)m_tool, 0, 3)]) + " · " + (m_magnet ? "Snap actif" : "Snap coupé") + " · " +
                        (m_localSpace ? "Local" : "Monde") + " · " + std::to_string(objs) + " objets · " + std::to_string(m_scene.triangleCount()) + " tris";
    TextMid(C.mono, 11, x + w - P(12) - TextW(C.mono, 11, right.c_str()), y, h, cText3(), right.c_str());
}

// --------------------------------------------------------------------------- shared pieces of the Matériaux / Prefab workspaces
namespace {
// rail of a workspace: icons with one active, console at the bottom
int WorkspaceRail(const char* key, float x, float y, float w, float h, const int* icons, const char* const* tips, int n, int active) {
    Rect(x, y, w, h, cPanel());
    VLine(x + w - 1, y, y + h, cLine());
    int clicked = -1;
    float ry = y + P(8);
    for (int i = 0; i < n; ++i) {
        const float rx = x + (w - P(36)) / 2;
        char id[32];
        snprintf(id, sizeof(id), "%sRail%d", key, i);
        bool hv;
        if (Hit(id, rx, ry, P(36), P(36), &hv)) clicked = i;
        const bool on = active == i;
        if (on) { Rect(rx, ry, P(36), P(36), cPanel2(), P(2)); Rect(x, ry + P(9), P(2), P(18), cAccent()); }
        else if (hv) Rect(rx, ry, P(36), P(36), cHover(), P(2));
        DrawIcon(icons[i], rx + P(8.5f), ry + P(8.5f), 19, on || hv ? cText() : cText3());
        Tip(tips[i]);
        ry += P(38);
    }
    return clicked;
}
// drawer tabs (.tabs); returns the clicked tab or -1
int DrawerTabs(const char* key, float x, float y, float w, const char* const* tabs, const int* icons, int n, int active) {
    float tx = x + P(6);
    int clicked = -1;
    for (int i = 0; i < n; ++i) {
        const std::string up = Upper(tabs[i]);
        const float tw = P(14 + 16 + 7 + 14) + TextW(C.hud, 12, up.c_str(), 0.16f);
        char id[32];
        snprintf(id, sizeof(id), "%sTab%d", key, i);
        bool hv;
        if (Hit(id, tx, y + 1, tw, P(32) - 1, &hv)) clicked = i;
        const bool on = active == i;
        const ImU32 c = on || hv ? cText() : cText3();
        DrawIcon(icons[i], tx + P(14), y + P(8), 16, c);
        TextMid(C.hud, 12, tx + P(14 + 16 + 7), y, P(32), c, up.c_str(), 0.16f);
        if (on) Rect(tx + P(10), y + P(30), tw - P(20), P(2), cAccent());
        tx += tw;
    }
    HLine(x, x + w, y + P(32) - 1, cLine());
    return clicked;
}
ImU32 ToU32(const float3& c, float k = 1) {
    auto g = [](float v) { return (int)(std::pow(std::clamp(v, 0.0f, 1.0f), 1 / 2.2f) * 255.0f + 0.5f); };   // linear -> sRGB
    return IM_COL32(g(c.x * k), g(c.y * k), g(c.z * k), 255);
}
// shaded preview sphere: concentric discs from the rim to the highlight (base colour, metal, roughness)
void PreviewSphere(ImVec2 c, float r, const float3& base, float metal, float rough) {
    C.dl->AddEllipseFilled(ImVec2(c.x, c.y + r + P(14)), ImVec2(r * 1.15f, P(20)), IM_COL32(0, 0, 0, 110), 0, 48);
    const ImVec2 hl(c.x - r * 0.32f, c.y - r * 0.36f);
    const int n = 48;
    const float spec = 0.25f + 0.75f * (1 - rough);
    for (int i = 0; i <= n; ++i) {
        const float t = (float)i / n;   // 0 rim -> 1 highlight
        const ImVec2 p(c.x + (hl.x - c.x) * t * 0.9f, c.y + (hl.y - c.y) * t * 0.9f);
        const float rr = r * (1 - t * 0.92f);
        const float diffuse = 0.08f + 0.9f * std::pow(t, 0.8f);
        float3 col = base * (diffuse * (1 - 0.6f * metal));
        const float hi = std::pow(t, 6.0f + 20.0f * (1 - rough)) * spec;
        const float3 tint = metal > 0.5f ? base : float3(1, 1, 1);
        col = col + tint * hi * (0.6f + 0.6f * metal);
        C.dl->AddCircleFilled(p, rr, ToU32(col), 64);
    }
    C.dl->AddCircle(c, r, IM_COL32(255, 255, 255, 14), 96, 1.0f);
}
} // namespace

// --------------------------------------------------------------------------- Matériaux: the materials of the scene
void App::spMaterials(float X, float y, float W, float h) {
    const float railW = P(48), leftW = P(264), rightW = P(300), drawerH = P(210);
    const float viewX = X + railW + leftW, viewW = W - railW - leftW - rightW, rowH = h - drawerH;
    const int railIcons[] = {I_PALETTE, I_IMAGE, I_SUN, I_SEARCH};
    const char* railTips[] = {"Matériaux de la scène", "Textures", "Éclairage d'aperçu", "Rechercher"};
    int rc = WorkspaceRail("spMat", X, y, railW, h, railIcons, railTips, 4, m_sp.matTab == 1 ? 1 : 0);
    if (rc == 1) m_sp.matTab = 1;
    else if (rc == 0) m_sp.matTab = 0;
    // library: the scene materials with their colour
    std::vector<Material>& mats = m_scene.materials;
    m_sp.matSel = mats.empty() ? -1 : std::clamp(m_sp.matSel, 0, (int)mats.size() - 1);
    float lx = X + railW;
    Rect(lx, y, leftW, rowH, cPanel());
    VLine(lx + leftW - 1, y, y + rowH, cLine());
    PanelHeader(lx, y, leftW - 1, "Matériaux", std::to_string(mats.size()));
    static char matSearch[64] = "";
    SearchField("##spMatSearch", lx + P(12), y + P(42), leftW - P(24), matSearch, sizeof(matSearch), "Rechercher…");
    const float listY = y + P(42 + 28 + 8);
    ImGui::SetCursorScreenPos(ImVec2(lx + P(6), listY));
    ImGui::BeginChild("spMatList", ImVec2(leftW - 1 - P(12), y + rowH - listY - P(46)), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    C.dl = ImGui::GetWindowDrawList();
    {
        const float rx = ImGui::GetCursorScreenPos().x, rw = ImGui::GetContentRegionAvail().x;
        float ry = ImGui::GetCursorScreenPos().y;
        for (size_t i = 0; i < mats.size(); ++i) {
            if (matSearch[0] && Upper(mats[i].name).find(Upper(matSearch)) == std::string::npos) continue;
            char id[24];
            snprintf(id, sizeof(id), "spMat%zu", i);
            bool hv;
            if (Hit(id, rx, ry, rw, P(25), &hv)) m_sp.matSel = (int)i;
            const bool sel = m_sp.matSel == (int)i;
            if (sel) { Rect(rx, ry, rw, P(25), cAccentBg(), P(2)); Rect(rx, ry, P(2), P(25), cAccent()); }
            else if (hv) Rect(rx, ry, rw, P(25), alpha(cHover(), 0.6f), P(2));
            Rect(rx + P(10), ry + P(5.5f), P(14), P(14), ToU32(mats[i].baseColor), P(2));
            Border(rx + P(10), ry + P(5.5f), P(14), P(14), cLineHi(), P(2));
            // users of the material
            int users = 0;
            for (const Object& o : m_scene.objects) users += o.material == (int)i;
            TextMid(C.ui, 13, rx + P(10 + 14 + 9), ry, P(25), sel ? cText() : cText2(), Fit(C.ui, 13, mats[i].name, rw - P(80)).c_str());
            const std::string u = std::to_string(users);
            TextMid(C.mono, 10.5f, rx + rw - P(8) - TextW(C.mono, 10.5f, u.c_str()), ry, P(25), cText3(), u.c_str());
            ry += P(25);
        }
        ImGui::SetCursorScreenPos(ImVec2(rx, ry));
        ImGui::Dummy(ImVec2(1, 1));
    }
    ImGui::EndChild();
    C.dl = ImGui::GetWindowDrawList();
    {
        const float bw = (leftW - P(24) - P(6)) / 2, by = y + rowH - P(38);
        if (TextButton("spMatNew", lx + P(12), by, bw, P(28), I_PLUS, "Nouveau", 0)) {
            pushUndo();
            Material m;
            m.name = "M_Nouveau_" + std::to_string(mats.size() + 1);
            mats.push_back(m);
            m_sp.matSel = (int)mats.size() - 1;
            markDirty();
        }
        if (TextButton("spMatDup", lx + P(12) + bw + P(6), by, bw, P(28), I_COPY, "Dupliquer", 0, nullptr, m_sp.matSel >= 0)) {
            pushUndo();
            Material m = mats[m_sp.matSel];
            m.name += "_copie";
            mats.push_back(m);
            m_sp.matSel = (int)mats.size() - 1;
            markDirty();
        }
    }
    // preview stage
    {
        Rect(viewX, y, viewW, rowH, IM_COL32(18, 23, 27, 255));
        C.dl->AddRectFilledMultiColor(ImVec2(viewX, y), ImVec2(viewX + viewW, y + rowH * 0.6f), IM_COL32(34, 42, 49, 255), IM_COL32(34, 42, 49, 255), IM_COL32(18, 23, 27, 255), IM_COL32(18, 23, 27, 255));
        if (m_sp.matSel >= 0) {
            const Material& m = mats[m_sp.matSel];
            const float r = std::min(viewW, rowH) * 0.3f;
            const ImVec2 c(viewX + viewW / 2, y + rowH * 0.47f);
            if (m_sp.matShape == 0) PreviewSphere(c, r, m.baseColor, m.metallic, m.roughness);
            else {   // cube: three shaded faces
                const float s2 = r * 0.85f;
                ImVec2 top[4] = {{c.x, c.y - s2}, {c.x + s2, c.y - s2 * 0.5f}, {c.x, c.y}, {c.x - s2, c.y - s2 * 0.5f}};
                ImVec2 lf[4] = {{c.x - s2, c.y - s2 * 0.5f}, {c.x, c.y}, {c.x, c.y + s2}, {c.x - s2, c.y + s2 * 0.5f}};
                ImVec2 rt[4] = {{c.x, c.y}, {c.x + s2, c.y - s2 * 0.5f}, {c.x + s2, c.y + s2 * 0.5f}, {c.x, c.y + s2}};
                C.dl->AddConvexPolyFilled(top, 4, ToU32(m.baseColor, 1.0f));
                C.dl->AddConvexPolyFilled(lf, 4, ToU32(m.baseColor, 0.55f));
                C.dl->AddConvexPolyFilled(rt, 4, ToU32(m.baseColor, 0.3f));
            }
        }
        // shape switch, bottom centre (.mat-shapes)
        const float sw = P(3 + 28 * 2 + 2 + 3), sx = viewX + (viewW - sw) / 2, sy = y + rowH - P(14 + 34);
        Rect(sx, sy, sw, P(34), alpha(cPanel(), 0.92f), P(2));
        Border(sx, sy, sw, P(34), cLine(), P(2));
        if (IconButton("spShapeS", sx + P(3), sy + P(3), P(28), I_GLOBE, m_sp.matShape == 0, true, "Sphère")) m_sp.matShape = 0;
        if (IconButton("spShapeC", sx + P(33), sy + P(3), P(28), I_BOX, m_sp.matShape == 1, true, "Cube")) m_sp.matShape = 1;
        // chips
        float cx = viewX + viewW - P(10);
        const std::string n = m_sp.matSel >= 0 ? mats[m_sp.matSel].name : "";
        const float cw = P(20 + 22) + TextW(C.ui, 12, n.c_str());
        cx -= cw;
        Rect(cx, y + P(10), cw, P(26), alpha(cPanel(), 0.92f), P(2));
        Border(cx, y + P(10), cw, P(26), cLine(), P(2));
        DrawIcon(I_PALETTE, cx + P(10), y + P(15), 16, cText2());
        TextMid(C.ui, 12, cx + P(32), y + P(10), P(26), cText2(), n.c_str());
    }
    // inspector: the selected material, edited live on the scene
    {
        const float ix = X + W - rightW;
        Rect(ix, y, rightW, h, cPanel());
        VLine(ix, y, y + h, cLine());
        PanelHeader(ix + 1, y, rightW - 1, "Matériau");
        if (m_sp.matSel >= 0) {
            Material& m = mats[m_sp.matSel];
            bool act = false;
            const float px = ix + P(12), fieldX = px + P(96), fieldW = ix + rightW - P(12) - fieldX;
            float sy = y + P(32);
            Rect(ix + P(12), sy + P(8), P(14), P(14), ToU32(m.baseColor), P(2));
            TextMid(C.semi, 13, ix + P(36), sy, P(30), cText(), m.name.c_str());
            sy += P(30);
            HLine(ix, ix + rightW, sy - 1, cLine());
            if (SectionHeader("spMsS", ix, sy, rightW, I_DROPLET, "Surface", m_sp.sect[4] ? I_CHEV_DOWN : I_CHEV_RIGHT)) m_sp.sect[4] = !m_sp.sect[4];
            sy += P(30);
            if (m_sp.sect[4]) {
                float ry = sy + P(4);
                std::vector<std::string> names;
                for (auto& pn : presetNames()) names.push_back(toUtf8(pn));
                int preset = m.preset;
                PropLabel(px, ry, P(25), "Préréglage");
                if (Select("##spMPreset", fieldX, ry, fieldW, &preset, names)) { pushUndo(); m.preset = preset; markDirty(); }
                ry += P(33);
                PropLabel(px, ry, P(25), "Couleur");
                ImGui::SetCursorScreenPos(ImVec2(fieldX, ry));
                ImGui::SetNextItemWidth(fieldW);
                float col[3] = {m.baseColor.x, m.baseColor.y, m.baseColor.z};
                if (ImGui::ColorEdit3("##spMColor", col, ImGuiColorEditFlags_DisplayHex)) { m.baseColor = float3(col[0], col[1], col[2]); markDirty(); }
                act |= ImGui::IsItemActivated();
                ry += P(33);
                struct Sl { const char* label; float* v; };
                Sl sl[] = {{"Métal", &m.metallic}, {"Rugosité", &m.roughness}, {"Spéculaire", &m.specular}, {"Vernis", &m.coat}};
                for (int i = 0; i < 4; ++i) {
                    char id[16];
                    snprintf(id, sizeof(id), "##spMs%d", i);
                    PropLabel(px, ry, P(25), sl[i].label);
                    if (SliderBar(id, fieldX, ry, fieldW, sl[i].v, 0, 1, "%.2f", act)) markDirty();
                    ry += P(33);
                }
                sy = ry + P(4);
            }
            HLine(ix, ix + rightW, sy - 1, cLine());
            if (SectionHeader("spMsE", ix, sy, rightW, I_ZAP, "Émission", m_sp.sect[5] ? I_CHEV_DOWN : I_CHEV_RIGHT)) m_sp.sect[5] = !m_sp.sect[5];
            sy += P(30);
            if (m_sp.sect[5]) {
                float ry = sy + P(4);
                PropLabel(px, ry, P(25), "Couleur");
                ImGui::SetCursorScreenPos(ImVec2(fieldX, ry));
                ImGui::SetNextItemWidth(fieldW);
                float col[3] = {m.emission.x, m.emission.y, m.emission.z};
                if (ImGui::ColorEdit3("##spMEm", col, ImGuiColorEditFlags_DisplayHex)) { m.emission = float3(col[0], col[1], col[2]); markDirty(); }
                act |= ImGui::IsItemActivated();
                ry += P(33);
                PropLabel(px, ry, P(25), "Intensité");
                if (SliderBar("##spMEmS", fieldX, ry, fieldW, &m.emissionStrength, 0, 20, "%.1f", act)) markDirty();
                sy = ry + P(25 + 12);
            }
            HLine(ix, ix + rightW, sy - 1, cLine());
            if (SectionHeader("spMsT", ix, sy, rightW, I_IMAGE, "Transmission", m_sp.sect[6] ? I_CHEV_DOWN : I_CHEV_RIGHT)) m_sp.sect[6] = !m_sp.sect[6];
            sy += P(30);
            if (m_sp.sect[6]) {
                float ry = sy + P(4);
                PropLabel(px, ry, P(25), "Transmission");
                if (SliderBar("##spMTr", fieldX, ry, fieldW, &m.transmission, 0, 1, "%.2f", act)) markDirty();
                ry += P(33);
                PropLabel(px, ry, P(25), "IOR");
                if (SliderBar("##spMIor", fieldX, ry, fieldW, &m.ior, 1, 2.5f, "%.2f", act)) markDirty();
                sy = ry + P(25 + 12);
            }
            HLine(ix, ix + rightW, sy - 1, cLine());
            if (act) pushUndo();
        }
    }
    // drawer: presets (click applies to the selected material), textures, console
    {
        const float dx = X + railW, dw = leftW + viewW, dy = y + rowH;
        Rect(dx, dy, dw, drawerH, cPanel());
        HLine(dx, dx + dw, dy, cLine());
        const char* tabs[] = {"Préréglages", "Textures", "Console"};
        const int icons[] = {I_SPARKLES, I_IMAGE, I_TERMINAL};
        int t = DrawerTabs("spMatD", dx, dy, dw, tabs, icons, 3, m_sp.matTab);
        if (t >= 0) m_sp.matTab = t;
        float ax = dx + P(12), ay = dy + P(32 + 12);
        if (m_sp.matTab == 0) {
            for (size_t i = 0; i < presetNames().size(); ++i) {
                if (ax + P(96) > dx + dw - P(12)) break;
                char id[24];
                snprintf(id, sizeof(id), "spPre%zu", i);
                bool hv;
                if (Hit(id, ax, ay, P(96), P(116), &hv) && m_sp.matSel >= 0) { pushUndo(); mats[m_sp.matSel].preset = (int)i; markDirty(); }
                Tip("Clic : appliquer au matériau sélectionné");
                const bool sel = m_sp.matSel >= 0 && mats[m_sp.matSel].preset == (int)i;
                Chamfer(ax, ay, P(96), P(116), P(8), cPanel2(), sel ? cAccentLo() : (hv ? cLineHi() : cLine()));
                drawAssetThumb(C.dl, 1, (int)i, ImVec2(ax + P(18), ay + P(12)), ImVec2(ax + P(78), ay + P(80)), m_dpi);
                HLine(ax + 1, ax + P(96) - 1, ay + P(89), cLine());
                C.dl->PushClipRect(ImVec2(ax, ay + P(89)), ImVec2(ax + P(90), ay + P(116)), true);
                TextMid(C.ui, 11, ax + P(8), ay + P(89), P(27), sel ? cText() : cText2(), toUtf8(presetNames()[i]).c_str());
                C.dl->PopClipRect();
                ax += P(106);
            }
        } else if (m_sp.matTab == 1) {
            if (m_scene.textures.empty()) TextMid(C.ui, 13, ax, ay, P(20), cText3(), "Aucune texture dans la scène.");
            for (size_t i = 0; i < m_scene.textures.size(); ++i) {
                if (ax + P(96) > dx + dw - P(12)) break;
                Chamfer(ax, ay, P(96), P(116), P(8), cPanel2(), cLine());
                DrawIcon(I_IMAGE, ax + P(27), ay + P(24), 42, cText2());
                HLine(ax + 1, ax + P(96) - 1, ay + P(89), cLine());
                C.dl->PushClipRect(ImVec2(ax, ay + P(89)), ImVec2(ax + P(90), ay + P(116)), true);
                TextMid(C.ui, 11, ax + P(8), ay + P(89), P(27), cText2(), m_scene.textures[i].name.c_str());
                C.dl->PopClipRect();
                ax += P(106);
            }
        } else {
            float ly = ay;
            for (size_t i = m_log.size() > 7 ? m_log.size() - 7 : 0; i < m_log.size(); ++i) {
                float ex = Text(C.mono, 11.5f, dx + P(14), ly, cText3(), m_log[i].time.c_str());
                Text(C.mono, 11.5f, ex + P(12), ly, m_log[i].level == 2 ? kWarn : cText2(), m_log[i].text.c_str());
                ly += P(20);
            }
        }
    }
}

// --------------------------------------------------------------------------- Prefab: layout of the mockup (UI only for now:
// the prefab system lives on its own branch; the components and values here are the mockup's)
void App::spPrefab(float X, float y, float W, float h) {
    const float railW = P(48), leftW = P(264), rightW = P(300), drawerH = P(210);
    const float viewX = X + railW + leftW, viewW = W - railW - leftW - rightW, rowH = h - drawerH;
    const int railIcons[] = {I_PACKAGE, I_HELMET, I_OUTPOST, I_PUZZLE};
    const char* railTips[] = {"Prefabs", "Unités", "Bâtiments", "Composants"};
    static int railSel = 0;
    int rc = WorkspaceRail("spPf", X, y, railW, h, railIcons, railTips, 4, railSel);
    if (rc >= 0) railSel = rc;
    // components of the prefab
    const float lx = X + railW;
    Rect(lx, y, leftW, rowH, cPanel());
    VLine(lx + leftW - 1, y, y + rowH, cLine());
    const char* prefabs[] = {"Marine", "Marine_Lourd", "Marine_Sniper", "Avant-poste", "QG", "Tourelle"};
    PanelHeader(lx, y, leftW - 1, "Prefab", Upper(std::string(prefabs[m_sp.prefabSel]) + ".prefab"));
    static char pfSearch[64] = "";
    SearchField("##spPfSearch", lx + P(12), y + P(42), leftW - P(24), pfSearch, sizeof(pfSearch), "Rechercher…");
    Text(C.hud, 11, lx + P(12), y + P(82), cText3(), "COMPOSANTS", 0.2f);
    struct Comp { int icon; const char* name; bool toggle; };
    const Comp comps[] = {{I_MOVE3D, "Transform", false}, {I_BOX, "Mesh · SM_Marine", true}, {I_SHIELD, "Collision · Capsule", true},
                          {I_HELMET, "Unité RTS", true}, {I_CROSSHAIR, "Arme · Fusil", true}, {I_SPARKLES, "FX · Tir", true}, {I_GRID, "Empreinte grille", true}};
    float ry = y + P(100);
    for (int i = 0; i < 7; ++i) {
        char id[16];
        snprintf(id, sizeof(id), "spComp%d", i);
        bool hv;
        if (Hit(id, lx, ry, leftW - 1 - P(44), P(30), &hv)) m_sp.prefabComp = i;
        const bool sel = m_sp.prefabComp == i;
        if (sel) { Rect(lx, ry, leftW - 1, P(30), cAccentBg()); Rect(lx, ry, P(2), P(30), cAccent()); }
        else if (hv) Rect(lx, ry, leftW - 1, P(30), alpha(cHover(), 0.6f));
        DrawIcon(comps[i].icon, lx + P(12), ry + P(7), 16, sel ? cAccent() : cText3());
        TextMid(C.ui, 13, lx + P(36), ry, P(30), sel ? cText() : cText2(), comps[i].name);
        if (comps[i].toggle) {
            char tid[16];
            snprintf(tid, sizeof(tid), "spCompT%d", i);
            const float tx = lx + leftW - P(12 + 26), ty = ry + P(8);
            if (Hit(tid, tx, ty, P(26), P(14))) m_sp.prefabOn[i] = !m_sp.prefabOn[i];
            Rect(tx, ty, P(26), P(14), m_sp.prefabOn[i] ? cAccentLo() : cLine(), P(2));
            Rect(m_sp.prefabOn[i] ? tx + P(14) : tx + P(2), ty + P(2), P(10), P(10), m_sp.prefabOn[i] ? cAccent() : cText3(), P(1));
        }
        HLine(lx, lx + leftW - 1, ry + P(30) - 1, cLine());
        ry += P(30);
    }
    TextButton("spAddComp", lx + P(12), ry + P(10), leftW - P(24), P(28), I_PLUS, "Ajouter un composant", 0, "Bientôt : les composants viennent du système de prefabs");
    // preview
    {
        Rect(viewX, y, viewW, rowH, IM_COL32(15, 19, 23, 255));
        C.dl->AddRectFilledMultiColor(ImVec2(viewX, y), ImVec2(viewX + viewW, y + rowH * 0.7f), IM_COL32(26, 32, 38, 255), IM_COL32(26, 32, 38, 255), IM_COL32(15, 19, 23, 255), IM_COL32(15, 19, 23, 255));
        C.dl->PushClipRect(ImVec2(viewX, y), ImVec2(viewX + viewW, y + rowH), true);
        const float sc = std::max(viewW / 800.0f, rowH / 560.0f), ox = viewX + (viewW - 800 * sc) / 2, oy = y + (rowH - 560 * sc) / 2;
        auto V = [&](float vx, float vy) { return ImVec2(ox + vx * sc, oy + vy * sc); };
        const ImU32 lc = IM_COL32(159, 179, 196, 255);
        for (int vx = -300; vx <= 1100; vx += 200) FadeLine(V((float)vx, 560), V(400, 220), oy + 220 * sc, oy + 560 * sc, 0.14f, lc, sc);
        for (float hy : {300.f, 350.f, 420.f, 510.f}) C.dl->AddLine(V(0, hy), V(800, hy), alpha(lc, 0.14f * (hy - 220) / 340), sc);
        const ImVec2 fc(viewX + viewW / 2, y + rowH * 0.66f);
        C.dl->AddEllipseFilled(fc, ImVec2(P(210), P(60)), alpha(cAccent(), 0.06f), 0, 64);
        C.dl->AddEllipse(fc, ImVec2(P(210), P(60)), alpha(cAccent(), 0.3f), 0, 64, 1.0f);
        const int icon = m_sp.prefabSel >= 3 ? (m_sp.prefabSel == 5 ? I_CROSSHAIR : I_OUTPOST) : I_HELMET;
        DrawIcon(icon, viewX + viewW / 2 - P(95), y + rowH * 0.46f - P(95), 190, IM_COL32(201, 210, 218, 255));
        const ImVec2 g(viewX + viewW / 2, y + rowH * 0.62f);
        C.dl->AddLine(g, ImVec2(g.x + P(66), g.y + P(22)), kAxis[0], P(2));
        C.dl->AddLine(g, ImVec2(g.x, g.y - P(80)), kAxis[1], P(2));
        C.dl->AddLine(g, ImVec2(g.x - P(58), g.y + P(23)), kAxis[2], P(2));
        C.dl->PopClipRect();
        const char* chips[] = {"Orbite", "Empreinte 1 × 1", "2 480 tris"};
        float cx = viewX + viewW - P(10);
        for (int i = 2; i >= 0; --i) {
            const float cw = P(20) + TextW(C.ui, 12, chips[i]);
            cx -= cw;
            Rect(cx, y + P(10), cw, P(26), alpha(cPanel(), 0.92f), P(2));
            Border(cx, y + P(10), cw, P(26), cLine(), P(2));
            TextMid(C.ui, 12, cx + P(10), y + P(10), P(26), cText2(), chips[i]);
            cx -= P(6);
        }
    }
    // inspector of the component
    {
        const float ix = X + W - rightW;
        Rect(ix, y, rightW, h, cPanel());
        VLine(ix, y, y + h, cLine());
        PanelHeader(ix + 1, y, rightW - 1, "Composant");
        float sy = y + P(32);
        SectionHeader("spPfName", ix, sy, rightW, comps[m_sp.prefabComp].icon, comps[m_sp.prefabComp].name, I_MORE);
        sy += P(30);
        HLine(ix, ix + rightW, sy - 1, cLine());
        static float dmg = 12, range = 8, rate = 1.2f;
        static int proj = 0;
        static bool air = true;
        bool act = false;
        const float px = ix + P(12), fieldX = px + P(96), fieldW = ix + rightW - P(12) - fieldX;
        SectionHeader("spPfTir", ix, sy, rightW, I_ZAP, "Tir", I_CHEV_DOWN);
        sy += P(30);
        float ry = sy + P(4);
        PropLabel(px, ry, P(25), "Dégâts");
        SliderBar("##spDmg", fieldX, ry, fieldW, &dmg, 0, 40, "%.0f", act);
        ry += P(33);
        PropLabel(px, ry, P(25), "Portée");
        SliderBar("##spRange", fieldX, ry, fieldW, &range, 0, 15, "%.1f m", act);
        ry += P(33);
        PropLabel(px, ry, P(25), "Cadence");
        SliderBar("##spRate", fieldX, ry, fieldW, &rate, 0, 3, "%.1f /s", act);
        ry += P(33);
        PropLabel(px, ry, P(25), "Projectile");
        Select("##spProj", fieldX, ry, fieldW, &proj, {"Balle traçante", "Plasma", "Grenade"});
        ry += P(33);
        PropLabel(px, ry, P(18), "Cible air");
        Toggle("##spAir", fieldX, ry + P(1), &air);
        sy = ry + P(18 + 12);
        HLine(ix, ix + rightW, sy - 1, cLine());
        SectionHeader("spPfVar", ix, sy, rightW, I_COPY, "Surcharges de variante", -1);
        Text(C.mono, 10, ix + rightW - P(28), sy + P(9), cSecond(), "2");
        sy += P(30);
        HLine(ix, ix + rightW, sy - 1, cLine());
    }
    // drawer: variants
    {
        const float dx = X + railW, dw = leftW + viewW, dy = y + rowH;
        Rect(dx, dy, dw, drawerH, cPanel());
        HLine(dx, dx + dw, dy, cLine());
        const char* tabs[] = {"Variantes", "Prefabs", "Console"};
        const int icons[] = {I_COPY, I_PACKAGE, I_TERMINAL};
        int t = DrawerTabs("spPfD", dx, dy, dw, tabs, icons, 3, m_sp.prefabTab);
        if (t >= 0) m_sp.prefabTab = t;
        float ax = dx + P(12);
        const float ay = dy + P(32 + 12);
        const int picons[] = {I_HELMET, I_HELMET, I_HELMET, I_OUTPOST, I_OUTPOST, I_CROSSHAIR};
        for (int i = 0; i < 6; ++i) {
            if (ax + P(96) > dx + dw - P(12)) break;
            char id[16];
            snprintf(id, sizeof(id), "spPf%d", i);
            bool hv;
            if (Hit(id, ax, ay, P(96), P(116), &hv)) m_sp.prefabSel = i;
            const bool sel = m_sp.prefabSel == i;
            Chamfer(ax, ay, P(96), P(116), P(8), cPanel2(), sel ? cAccentLo() : (hv ? cLineHi() : cLine()));
            DrawIcon(picons[i], ax + P(27), ay + P(24), 42, sel ? cAccent() : cText2());
            HLine(ax + 1, ax + P(96) - 1, ay + P(89), cLine());
            TextMid(C.ui, 11, ax + P(8), ay + P(89), P(27), sel ? cText() : cText2(), prefabs[i]);
            ax += P(106);
        }
    }
}

} // namespace app
