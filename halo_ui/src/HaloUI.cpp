// Halo editor UI, written from scratch to reproduce design/halo/mockup-halo.html (halo.css) element by element.
// Every size below is the CSS pixel value of the mockup, multiplied by the DPI scale k.
#include "HaloUI.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace halo {
namespace {

// --------------------------------------------------------------------------- tokens (halo.css :root)
constexpr ImU32 rgb(unsigned h, float a = 1.0f) {
    return IM_COL32((h >> 16) & 255, (h >> 8) & 255, h & 255, (int)(a * 255.0f + 0.5f));
}
const ImU32 kBase = rgb(0x0D1013), kPanel = rgb(0x13171B), kPanel2 = rgb(0x181D22), kHover = rgb(0x1F252B);
const ImU32 kLine = rgb(0x242B32), kLineHi = rgb(0x333C45);
const ImU32 kText = rgb(0xD5DCE2), kText2 = rgb(0x8B96A1), kText3 = rgb(0x5D6873);
const ImU32 kAccent = rgb(0x6CC4DC), kAccentLo = rgb(0x2B5563), kAccentBg = rgb(0x6CC4DC, 0.10f);
const ImU32 kOk = rgb(0x7FC79A);
const ImU32 kAxis[3] = {rgb(0xD77A7A), rgb(0x84C48F), rgb(0x7EA6D8)};

// --------------------------------------------------------------------------- icons (halo_ui/assets/icons.png)
enum Icon {
    I_MAP, I_WORKFLOW, I_PALETTE, I_PACKAGE, I_UNDO, I_REDO, I_PLAY, I_HAMMER, I_SETTINGS, I_LAYERS, I_SUN, I_VIDEO,
    I_SEARCH, I_TERMINAL, I_CHEV_DOWN, I_CHEV_RIGHT, I_GLOBE, I_EYE, I_POINTER, I_MOVE, I_ROTATE, I_SCALING, I_MAGNET,
    I_CAMERA, I_SPARKLES, I_MORE, I_MOVE3D, I_CLOCK, I_FOLDER_OPEN, I_BOX, I_IMAGE, I_OUTPOST, I_HELMET, I_GRID, I_BRAND,
    I_PLUS, I_ZAP, I_BRANCH, I_HAND, I_MAXIMIZE, I_DROPLET, I_PUZZLE, I_CROSSHAIR, I_SHIELD, I_REPEAT, I_COPY, I_CIRCLE_PLAY,
    I_COUNT   // tools/atlas_names.txt order
};
const int kAtlasSizes[] = {16, 19, 22, 40, 42, 96};   // tools/atlas2.html: one grid per size, cells of size + 4

ImTextureID g_atlas = (ImTextureID)0;
float g_atlasW = 1024, g_atlasH = 896;
Fonts g_f;
ImFont* g_semi = nullptr;   // 500 / 600 weights of Inter -> Segoe UI Semibold
ImFont* g_monoBold = nullptr;

ImDrawList* dl = nullptr;
float k = 1.0f;
inline float P(float v) { return v * k; }

void DrawIcon(Icon icon, float x, float y, float size, ImU32 color) {
    // the atlas size closest above the drawn size (crisp strokes), otherwise the largest one
    const float px = size * k;
    int pick = 5;
    for (int i = 0; i < 6; ++i)
        if (kAtlasSizes[i] >= px - 0.5f) { pick = i; break; }
    float by = 0;
    for (int i = 0; i < pick; ++i) {
        const int c = kAtlasSizes[i] + 4, cols = 1024 / c;
        by += (float)(((I_COUNT + cols - 1) / cols) * c);
    }
    const int S = kAtlasSizes[pick], C = S + 4, cols = 1024 / C;
    const float cx = (float)((icon % cols) * C + 2), cy = by + (float)((icon / cols) * C + 2);
    dl->AddImage(g_atlas, ImVec2(x, y), ImVec2(x + px, y + px), ImVec2(cx / g_atlasW, cy / g_atlasH),
                 ImVec2((cx + S) / g_atlasW, (cy + S) / g_atlasH), color);
}

// text helpers: size in CSS px, letter spacing in em
ImVec2 TextSize(ImFont* f, float size, const char* t, float spacing = 0) {
    ImVec2 z = f->CalcTextSizeA(P(size), FLT_MAX, 0, t);
    if (spacing != 0) z.x += P(size) * spacing * (float)ImTextCountCharsFromUtf8(t, nullptr);
    return z;
}
float DrawText(ImFont* f, float size, float x, float y, ImU32 c, const char* t, float spacing = 0) {
    if (spacing == 0) {
        dl->AddText(f, P(size), ImVec2(x, y), c, t);
        return x + f->CalcTextSizeA(P(size), FLT_MAX, 0, t).x;
    }
    const char* s = t;
    while (*s) {
        unsigned int cp = 0;
        int n = ImTextCharFromUtf8(&cp, s, nullptr);
        dl->AddText(f, P(size), ImVec2(x, y), c, s, s + n);
        x += f->CalcTextSizeA(P(size), FLT_MAX, 0, s, s + n).x + P(size) * spacing;
        s += n;
    }
    return x;
}
// text vertically centred in a box of height h starting at top
float TextMid(ImFont* f, float size, float x, float top, float h, ImU32 c, const char* t, float spacing = 0) {
    return DrawText(f, size, x, top + (h - f->CalcTextSizeA(P(size), FLT_MAX, 0, "Ag").y) / 2, c, t, spacing);
}
std::string Upper(const char* t) {   // ASCII + the accented capitals of the mockup labels
    std::string s = t;
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char ch = (unsigned char)s[i];
        if (ch == 0xC3 && i + 1 < s.size()) {   // à-ÿ -> À-ß range: clear bit 0x20 of the second byte
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

// --------------------------------------------------------------------------- shapes
void Rect(float x, float y, float w, float h, ImU32 fill, float r = 0) { dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), fill, r); }
void Border(float x, float y, float w, float h, ImU32 c, float r = 0) { dl->AddRect(ImVec2(x + 0.5f, y + 0.5f), ImVec2(x + w - 0.5f, y + h - 0.5f), c, r, 0, 1.0f); }
void HLine(float x0, float x1, float y, ImU32 c) { dl->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + 1), c); }
void VLine(float x, float y0, float y1, ImU32 c) { dl->AddRectFilled(ImVec2(x, y0), ImVec2(x + 1, y1), c); }
// clip-path: polygon(0 0, calc(100% - cut) 0, 100% cut, 100% 100%, 0 100%) with its 1 px border
void Chamfer(float x, float y, float w, float h, float cut, ImU32 fill, ImU32 border) {
    ImVec2 p[5] = {{x, y}, {x + w - cut, y}, {x + w, y + cut}, {x + w, y + h}, {x, y + h}};
    if (fill) dl->AddConvexPolyFilled(p, 5, fill);
    if (border) {
        ImVec2 q[5] = {{x + 0.5f, y + 0.5f}, {x + w - cut - 0.2f, y + 0.5f}, {x + w - 0.5f, y + cut + 0.2f}, {x + w - 0.5f, y + h - 0.5f}, {x + 0.5f, y + h - 0.5f}};
        dl->AddPolyline(q, 5, border, ImDrawFlags_Closed, 1.0f);
    }
}
bool Hit(const char* id, float x, float y, float w, float h, bool* hovered = nullptr) {
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    bool q = ImGui::InvisibleButton(id, ImVec2(std::max(1.0f, w), std::max(1.0f, h)));
    if (hovered) *hovered = ImGui::IsItemHovered();
    return q;
}
// line with an alpha ramp along y (the SVG stroke gradient of the viewport grid), in segments
void FadeLine(ImVec2 a, ImVec2 b, float y0, float y1, float a0, float a1, ImU32 base, float width) {
    const int n = 24;
    for (int i = 0; i < n; ++i) {
        float t0 = (float)i / n, t1 = (float)(i + 1) / n;
        ImVec2 p0(a.x + (b.x - a.x) * t0, a.y + (b.y - a.y) * t0), p1(a.x + (b.x - a.x) * t1, a.y + (b.y - a.y) * t1);
        float ym = (p0.y + p1.y) / 2, f = y1 > y0 ? std::clamp((ym - y0) / (y1 - y0), 0.0f, 1.0f) : 1.0f;
        float al = a0 + (a1 - a0) * f;
        if (al <= 0.003f) continue;
        ImU32 c = (base & ~IM_COL32_A_MASK) | ((ImU32)(al * 255) << IM_COL32_A_SHIFT);
        dl->AddLine(p0, p1, c, width);
    }
}

// --------------------------------------------------------------------------- state
struct State {
    int workspace = 0;   // Scène, Graph, Matériaux, Prefab
    int rail = 0;
    int treeSel = 5;
    bool treeOpen[11] = {true, true, true, true, true, true, true, true, false, false, true};
    int tool = 0;
    bool gridSnap = true, magnet = false;
    int drawerTab = 0;
    int assetSel = 0;
    bool sectOpen[4] = {true, true, true, false};
    float pos[3] = {12.0f, 0.0f, -4.5f}, rot[3] = {0, 90, 0}, scl[3] = {1, 1, 1};
    float speed = 3.15f, hp = 40;
    bool selectable = true;
    int faction = 0;
    char search[64] = "", assetSearch[64] = "";
} S;

// --------------------------------------------------------------------------- top bar (.top)
void TopBar(float W) {
    const float h = P(44);
    Rect(0, 0, W, h, kPanel);
    HLine(0, W, h - 1, kLine);
    // brand: 22 px mark, WZX / ENGINE stacked (.brand, width 196)
    float x = P(14);
    DrawIcon(I_BRAND, x, (h - P(22)) / 2, 22, kAccent);
    const float tx = x + P(22 + 10), ty = (h - P(14 + 3 + 9)) / 2;
    DrawText(g_f.hud, 14, tx, ty - P(1), kText, "WZX", 0.22f);
    DrawText(g_f.hud, 9, tx, ty + P(17), kText3, "ENGINE", 0.38f);
    x += P(196 + 18);
    // menus (.menu span: padding 6 9, gap 2)
    const char* menus[] = {"Fichier", "Édition", "Scène", "Build", "Fenêtre", "Aide"};
    for (int i = 0; i < 6; ++i) {
        float w = TextSize(g_f.ui, 13, menus[i]).x + P(18), mh = P(30);
        char id[16];
        snprintf(id, sizeof(id), "menu%d", i);
        bool hv;
        Hit(id, x, (h - mh) / 2, w, mh, &hv);
        if (hv) Rect(x, (h - mh) / 2, w, mh, kHover, P(2));
        TextMid(g_f.ui, 13, x + P(9), (h - mh) / 2, mh, hv ? kText : kText2, menus[i]);
        x += w + P(2);
    }
    x += P(18 + 12 - 2);
    // workspaces (.ws): joined tabs in one bordered strip
    const char* ws[] = {"Scène", "Graph", "Matériaux", "Prefab"};
    const Icon wsIcon[] = {I_MAP, I_WORKFLOW, I_PALETTE, I_PACKAGE};
    float wsW[4], total = 0;
    for (int i = 0; i < 4; ++i) {
        wsW[i] = P(14 + 16 + 7 + 14) + TextSize(g_f.hud, 13, Upper(ws[i]).c_str(), 0.14f).x + (i < 3 ? 1 : 0);
        total += wsW[i];
    }
    const float wh = P(25) + 2, wy = (h - wh) / 2;
    float wx = x;
    for (int i = 0; i < 4; ++i) {
        char id[16];
        snprintf(id, sizeof(id), "ws%d", i);
        bool hv;
        if (Hit(id, wx, wy, wsW[i], wh, &hv)) S.workspace = i;
        const bool on = S.workspace == i;
        if (on) {
            Rect(wx, wy, wsW[i], wh, kPanel2);
            Rect(wx, wy + wh - P(2), wsW[i], P(2), kAccent);
        } else if (hv) Rect(wx, wy, wsW[i], wh, kHover);
        const ImU32 c = on || hv ? kText : kText2;
        DrawIcon(wsIcon[i], wx + P(14), wy + (wh - P(16)) / 2, 16, c);
        TextMid(g_f.hud, 13, wx + P(14 + 16 + 7), wy, wh, c, Upper(ws[i]).c_str(), 0.14f);
        wx += wsW[i];
        if (i < 3) VLine(wx - 1, wy, wy + wh, kLine);
    }
    Border(x, wy, total, wh, kLine);
    // right group (.play): undo, redo, Play F5, Build F7, settings; gap 6
    struct Btn { const char* id; Icon icon; const char* label; int kind; };   // kind 0 icon, 1 primary, 2 text
    const Btn btns[] = {{"undo", I_UNDO, nullptr, 0}, {"redo", I_REDO, nullptr, 0}, {"play", I_PLAY, "Play F5", 1},
                        {"build", I_HAMMER, "Build F7", 2}, {"settings", I_SETTINGS, nullptr, 0}};
    float bw[5], sum = 0;
    for (int i = 0; i < 5; ++i) {
        bw[i] = btns[i].label ? P(12 + 16 + 6 + 12) + TextSize(g_semi, 12, btns[i].label).x : P(28);
        sum += bw[i] + (i < 4 ? P(6) : 0);
    }
    float bx = W - P(14) - sum, by = (h - P(28)) / 2;
    for (int i = 0; i < 5; ++i) {
        const Btn& b = btns[i];
        bool hv;
        Hit(b.id, bx, by, bw[i], P(28), &hv);
        if (b.kind == 0) {
            Rect(bx, by, bw[i], P(28), hv ? kHover : kPanel2, P(2));
            Border(bx, by, bw[i], P(28), kLineHi, P(2));
            DrawIcon(b.icon, bx + (bw[i] - P(16)) / 2, by + P(6), 16, kText);
        } else {
            const ImU32 fill = b.kind == 1 ? (hv ? rgb(0x6CC4DC, 0.18f) : kAccentBg) : (hv ? kHover : kPanel2);
            Chamfer(bx, by, bw[i], P(28), P(6), fill, b.kind == 1 ? kAccentLo : kLineHi);
            const ImU32 c = b.kind == 1 ? kAccent : kText;
            DrawIcon(b.icon, bx + P(12), by + P(6), 16, c);
            TextMid(g_semi, 12, bx + P(12 + 16 + 6), by, P(28), c, b.label);
        }
        bx += bw[i] + P(6);
    }
}

// --------------------------------------------------------------------------- icon rail (.rail)
void Rail(float x, float y, float w, float h) {
    Rect(x, y, w, h, kPanel);
    VLine(x + w - 1, y, y + h, kLine);
    struct Item { Icon icon; const char* tip; };
    const Item items[] = {{I_LAYERS, "Scène"}, {I_HELMET, "Unités"}, {I_OUTPOST, "Bâtiments"}, {I_GRID, "Terrain et grille"},
                          {I_SUN, "Lumières"}, {I_VIDEO, "Caméras"}, {I_SEARCH, "Recherche"}, {I_TERMINAL, "Console"}};
    float iy = y + P(8);
    for (int i = 0; i < 8; ++i) {
        if (i == 6) { iy += P(6); HLine(x + (w - P(20)) / 2, x + (w + P(20)) / 2, iy, kLine); iy += P(1 + 6 + 2); }
        float ry = i == 7 ? y + h - P(8 + 36) : iy;
        float rx = x + (w - P(36)) / 2;
        char id[16];
        snprintf(id, sizeof(id), "rail%d", i);
        bool hv;
        if (Hit(id, rx, ry, P(36), P(36), &hv)) S.rail = i;
        if (hv && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", items[i].tip);
        const bool on = S.rail == i;
        if (on) {
            Rect(rx, ry, P(36), P(36), kPanel2, P(2));
            Rect(x, ry + P(9), P(2), P(36 - 18), kAccent);
        } else if (hv) Rect(rx, ry, P(36), P(36), kHover, P(2));
        DrawIcon(items[i].icon, rx + P(36 - 19) / 2, ry + P(36 - 19) / 2, 19, on || hv ? kText : kText3);
        iy += P(36 + 2);
    }
}

// panel header (.ph): condensed spaced title, HUD corner mark, optional mono count
void PanelHeader(float x, float y, float w, const char* title, const char* count) {
    const float h = P(32);
    TextMid(g_f.hud, 12, x + P(12), y, h, kText2, Upper(title).c_str(), 0.2f);
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + P(8), y + 1), rgb(0x6CC4DC, 0.8f));
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + 1, y + P(8)), rgb(0x6CC4DC, 0.8f));
    if (count) {
        float cw = TextSize(g_f.mono, 11, count).x;
        TextMid(g_f.mono, 11, x + w - P(12) - cw, y, h, kText3, count);
    }
    HLine(x, x + w, y + h - 1, kLine);
}

// search box (.search)
void SearchBox(const char* id, float x, float y, float w, char* buf, size_t len, const char* hint) {
    const float h = P(28);
    Rect(x, y, w, h, kBase, P(2));
    Border(x, y, w, h, ImGui::IsItemActive() ? kAccentLo : kLine, P(2));
    DrawIcon(I_SEARCH, x + P(9), y + (h - P(16)) / 2, 16, kText3);
    ImGui::SetCursorScreenPos(ImVec2(x + P(9 + 16 + 8) - P(4), y));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, 0);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, 0);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, 0);
    ImGui::PushStyleColor(ImGuiCol_Text, kText);
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, kText3);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(P(4), (h - ImGui::GetFontSize()) / 2));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::SetNextItemWidth(w - P(9 + 16 + 8) - P(4));
    ImGui::InputTextWithHint(id, hint, buf, len);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(5);
}

// --------------------------------------------------------------------------- scene panel (left)
void ScenePanel(float x, float y, float w, float h) {
    Rect(x, y, w, h, kPanel);
    VLine(x + w - 1, y, y + h, kLine);
    PanelHeader(x, y, w - 1, "Scène", "Village · 124 objets");
    SearchBox("##scenesearch", x + P(12), y + P(32 + 10), w - P(24), S.search, sizeof(S.search), "Rechercher…");
    struct Row { int indent; int chev; Icon icon; const char* name; bool eye; int parent; };   // chev -1 none, 0 closed, 1 open
    const Row rows[] = {{8, 1, I_GLOBE, "Village", false, -1},       {24, 1, I_OUTPOST, "Bâtiments", true, 0},
                        {40, -1, I_OUTPOST, "QG_001", false, 1},     {40, -1, I_OUTPOST, "Avant-poste_002", false, 1},
                        {24, 1, I_HELMET, "Unités", true, 0},        {40, -1, I_HELMET, "Marine_003", false, 4},
                        {40, -1, I_HELMET, "Marine_004", false, 4},  {40, -1, I_HELMET, "Marine_005", false, 4},
                        {24, 0, I_GRID, "Terrain", false, 0},        {24, 0, I_SUN, "Éclairage", false, 0},
                        {24, -1, I_VIDEO, "Caméra RTS", false, 0}};
    float ry = y + P(32 + 10 + 28 + 6 + 4), rx = x + P(6), rw = w - 1 - P(12);
    dl->PushClipRect(ImVec2(x, ry), ImVec2(x + w, y + h), true);
    for (int i = 0; i < 11; ++i) {
        const Row& r = rows[i];
        // hidden when an ancestor is folded
        bool shown = true;
        for (int p = r.parent; p >= 0; p = rows[p].parent)
            if (rows[p].chev >= 0 && !S.treeOpen[p]) shown = false;
        if (!shown) continue;
        const bool sel = S.treeSel == i;
        char id[16];
        snprintf(id, sizeof(id), "row%d", i);
        bool hv;
        if (Hit(id, rx, ry, rw, P(25), &hv)) {
            if (r.chev >= 0 && ImGui::GetMousePos().x < rx + P(r.indent + 16)) S.treeOpen[i] = !S.treeOpen[i];
            else S.treeSel = i;
        }
        if (sel) {
            Rect(rx, ry, rw, P(25), kAccentBg, P(2));
            Rect(rx, ry, P(2), P(25), kAccent);
        } else if (hv) Rect(rx, ry, rw, P(25), rgb(0x1F252B, 0.6f), P(2));
        float cx = rx + P(r.indent);
        const ImU32 ic = sel ? kAccent : kText3;
        if (r.chev >= 0) {
            DrawIcon(S.treeOpen[i] ? I_CHEV_DOWN : I_CHEV_RIGHT, cx, ry + P(4.5f), 16, ic);
            cx += P(16 + 7);
        }
        DrawIcon(r.icon, cx, ry + P(4.5f), 16, ic);
        cx += P(16 + 7);
        TextMid(g_f.ui, 13, cx, ry, P(25), sel ? kText : kText2, r.name);
        if (r.eye) DrawIcon(I_EYE, rx + rw - P(8 + 16), ry + P(4.5f), 16, rgb(0x5D6873, 0.55f));
        ry += P(25);
    }
    dl->PopClipRect();
}

// --------------------------------------------------------------------------- viewport (.view)
void Viewport(float x, float y, float w, float h) {
    // background: linear-gradient(#1a2026, #0f1317 70%)
    const float y70 = y + h * 0.7f;
    dl->AddRectFilledMultiColor(ImVec2(x, y), ImVec2(x + w, y70), rgb(0x1A2026), rgb(0x1A2026), rgb(0x0F1317), rgb(0x0F1317));
    Rect(x, y70, w, y + h - y70, rgb(0x0F1317));
    dl->PushClipRect(ImVec2(x, y), ImVec2(x + w, y + h), true);
    // svg.grid: viewBox 800 x 560, preserveAspectRatio xMidYMid slice
    const float sc = std::max(w / 800.0f, h / 560.0f), ox = x + (w - 800 * sc) / 2, oy = y + (h - 560 * sc) / 2;
    auto V = [&](float vx, float vy) { return ImVec2(ox + vx * sc, oy + vy * sc); };
    const ImU32 lineCol = rgb(0x9FB3C4);
    // stroke gradient per path (objectBoundingBox): transparent at the top of the path, 16 % at its bottom
    const float rTop = oy + 180 * sc, rBot = oy + 560 * sc;
    FadeLine(V(-400, 560), V(400, 180), rTop, rBot, 0, 0.16f, lineCol, sc);
    FadeLine(V(400, 180), V(1200, 560), rTop, rBot, 0, 0.16f, lineCol, sc);
    for (int vx = -300; vx <= 1100; vx += 100)
        if (vx != 400) FadeLine(V((float)vx, 560), V(400, 180), rTop, rBot, 0, 0.16f, lineCol, sc);
    const float hTop = oy + 230 * sc, hBot = oy + 476 * sc;
    for (float hy : {230.f, 262.f, 300.f, 346.f, 404.f, 476.f}) {
        float f = (oy + hy * sc - hTop) / (hBot - hTop), al = 0.16f * f;
        if (al > 0.003f) dl->AddLine(V(0, hy), V(800, hy), (lineCol & ~IM_COL32_A_MASK) | ((ImU32)(al * 255) << IM_COL32_A_SHIFT), sc);
    }
    // selection disc: radial accent glow (rx 110, ry 34) and dashed-free ring (rx 62, ry 19)
    const ImVec2 c = V(400, 300);
    for (int i = 20; i >= 1; --i) {
        float t = i / 20.0f;
        dl->AddEllipseFilled(c, ImVec2(110 * sc * t, 34 * sc * t), rgb(0x6CC4DC, 0.10f / 20.0f * 1.6f), 0, 48);
    }
    dl->AddEllipse(c, ImVec2(62 * sc, 19 * sc), rgb(0x6CC4DC, 0.45f), 0, 64, sc);
    // unit at 50 % / 46 %: the helmet (96 px) and the four-corner reticle (150 px) with its label
    const ImVec2 u(x + w * 0.5f, y + h * 0.46f);
    DrawIcon(I_HELMET, u.x - P(48), u.y - P(48), 96, rgb(0xC9D2DA));
    const float r0x = u.x - P(75), r0y = u.y - P(75), r1x = u.x + P(75), r1y = u.y + P(75), L = P(14), t = P(1.5f);
    const ImVec2 cs[4] = {{r0x, r0y}, {r1x, r0y}, {r0x, r1y}, {r1x, r1y}};
    for (int i = 0; i < 4; ++i) {
        float dx = (i % 2) ? -1.0f : 1.0f, dy = (i / 2) ? -1.0f : 1.0f;
        float ax = cs[i].x + (dx < 0 ? -t : 0), ay = cs[i].y + (dy < 0 ? -t : 0);
        dl->AddRectFilled(ImVec2(std::min(ax, ax + dx * L), ay), ImVec2(std::max(ax, ax + dx * L) + (dx < 0 ? t : 0), ay + t), kAccent);
        dl->AddRectFilled(ImVec2(ax, std::min(ay, ay + dy * L)), ImVec2(ax + t, std::max(ay, ay + dy * L) + (dy < 0 ? t : 0)), kAccent);
    }
    DrawText(g_f.mono, 10.5f, r1x + P(8), r0y, kAccent, "MARINE_003 · PV 40/100", 0.04f);

    // floating tools (.vtools): pointer, move, rotate, scale | grid snap, magnet
    {
        const Icon icons[] = {I_POINTER, I_MOVE, I_ROTATE, I_SCALING, I_GRID, I_MAGNET};
        const float bw = P(3 + 28 * 4 + 2 * 3 + 2 + 7 + 2 + 28 + 2 + 28 + 3), bh = P(28 + 6);
        const float bx = x + P(10), by = y + P(10);
        Rect(bx, by, bw, bh, rgb(0x13171B, 0.9f), P(2));
        Border(bx, by, bw, bh, kLine, P(2));
        float tx = bx + P(3);
        for (int i = 0; i < 6; ++i) {
            if (i == 4) { VLine(tx + P(3), by + P(3 + 4), by + bh - P(3 + 4), kLine); tx += P(7 + 2); }
            const bool on = i < 4 ? S.tool == i : (i == 4 ? S.gridSnap : S.magnet);
            char id[16];
            snprintf(id, sizeof(id), "vt%d", i);
            bool hv;
            if (Hit(id, tx, by + P(3), P(28), P(28), &hv)) {
                if (i < 4) S.tool = i;
                else if (i == 4) S.gridSnap = !S.gridSnap;
                else S.magnet = !S.magnet;
            }
            if (on) Rect(tx, by + P(3), P(28), P(28), kAccentBg, P(2));
            else if (hv) Rect(tx, by + P(3), P(28), P(28), kHover, P(2));
            DrawIcon(icons[i], tx + P(6), by + P(3 + 6), 16, on ? kAccent : (hv ? kText : kText2));
            tx += P(28 + 2);
        }
    }
    // chips (.vinfo): Perspective, Lit · DDGI, 144 fps, right aligned
    {
        struct Chip { Icon icon; const char* a; const char* b; const char* c; };
        const Chip chips[] = {{I_CAMERA, "Perspective", nullptr, nullptr}, {I_SPARKLES, "Lit · ", "DDGI", nullptr}, {I_COUNT, nullptr, "144", " fps"}};
        float cw[3], sum = 0;
        for (int i = 0; i < 3; ++i) {
            const Chip& ch = chips[i];
            cw[i] = P(10 + 10) + (ch.icon != I_COUNT ? P(16 + 6) : 0) + (ch.a ? TextSize(g_f.ui, 12, ch.a).x : 0) +
                    (ch.b ? TextSize(g_f.mono, 12, ch.b).x : 0) + (ch.c ? TextSize(g_f.ui, 12, ch.c).x : 0);
            sum += cw[i] + (i < 2 ? P(6) : 0);
        }
        float cx = x + w - P(10) - sum, cy = y + P(10);
        for (int i = 0; i < 3; ++i) {
            const Chip& ch = chips[i];
            Rect(cx, cy, cw[i], P(26), rgb(0x13171B, 0.9f), P(2));
            Border(cx, cy, cw[i], P(26), kLine, P(2));
            float tx = cx + P(10);
            if (ch.icon != I_COUNT) { DrawIcon(ch.icon, tx, cy + P(5), 16, kText2); tx += P(16 + 6); }
            if (ch.a) tx = TextMid(g_f.ui, 12, tx, cy, P(26), kText2, ch.a);
            if (ch.b) tx = TextMid(g_f.mono, 12, tx, cy, P(26), kText, ch.b);
            if (ch.c) TextMid(g_f.ui, 12, tx, cy, P(26), kText2, ch.c);
            cx += cw[i] + P(6);
        }
    }
    dl->PopClipRect();
}

// --------------------------------------------------------------------------- bottom drawer (.drawer)
void Drawer(float x, float y, float w, float h) {
    Rect(x, y, w, h, kPanel);
    HLine(x, x + w, y, kLine);
    // tabs (.tabs / .tab): 32 px, padding-left 6
    const char* tabs[] = {"Assets", "Console", "Graph"};
    const Icon tabIcons[] = {I_FOLDER_OPEN, I_TERMINAL, I_WORKFLOW};
    float tx = x + P(6);
    for (int i = 0; i < 3; ++i) {
        std::string up = Upper(tabs[i]);
        float tw = P(14 + 16 + 7 + 14) + TextSize(g_f.hud, 12, up.c_str(), 0.16f).x;
        char id[16];
        snprintf(id, sizeof(id), "tab%d", i);
        bool hv;
        if (Hit(id, tx, y + 1, tw, P(32) - 1, &hv)) S.drawerTab = i;
        const bool on = S.drawerTab == i;
        const ImU32 c = on || hv ? kText : kText3;
        DrawIcon(tabIcons[i], tx + P(14), y + P(8), 16, c);
        TextMid(g_f.hud, 12, tx + P(14 + 16 + 7), y, P(32), c, up.c_str(), 0.16f);
        if (on) Rect(tx + P(10), y + P(32) - P(2), tw - P(20), P(2), kAccent);
        tx += tw;
    }
    HLine(x, x + w, y + P(32) - 1, kLine);
    const float by = y + P(32);
    if (S.drawerTab == 0) {
        // asset tiles (.asset): 96 x 116, chamfered, gap 10, padding 12
        struct A { Icon icon; float size; const char* name; };
        const A assets[] = {{I_HELMET, 42, "Marine"}, {I_OUTPOST, 42, "Avant-poste"}, {I_GRID, 42, "Tuile_hex"}, {I_BOX, 40, "Cube"},
                            {I_PALETTE, 40, "M_Blindage"}, {I_IMAGE, 40, "T_Sol_desert"}, {I_SUN, 40, "HDRI_Aube"}};
        float ax = x + P(12), ay = by + P(12);
        for (int i = 0; i < 7; ++i) {
            if (ax + P(96) > x + w - P(12)) { ax = x + P(12); ay += P(116 + 10); }
            const bool sel = S.assetSel == i;
            char id[16];
            snprintf(id, sizeof(id), "asset%d", i);
            bool hv;
            if (Hit(id, ax, ay, P(96), P(116), &hv)) S.assetSel = i;
            Chamfer(ax, ay, P(96), P(116), P(8), kPanel2, sel ? kAccentLo : (hv ? kLineHi : kLine));
            const float nameH = P(6 + 6) + g_f.ui->CalcTextSizeA(P(11), FLT_MAX, 0, "Ag").y;
            const float thH = P(116) - nameH;
            DrawIcon(assets[i].icon, ax + (P(96) - P(assets[i].size)) / 2, ay + (thH - P(assets[i].size)) / 2, assets[i].size, sel ? kAccent : kText2);
            HLine(ax + 1, ax + P(96) - 1, ay + thH, kLine);
            dl->PushClipRect(ImVec2(ax, ay + thH), ImVec2(ax + P(96) - P(8), ay + P(116)), true);
            DrawText(g_f.ui, 11, ax + P(8), ay + thH + P(6), kText2, assets[i].name);
            dl->PopClipRect();
            ax += P(96 + 10);
        }
    } else if (S.drawerTab == 1) {
        const char* lines[] = {"[16:02:11]  Scène chargée : Village.wzx", "[16:02:11]  DDGI : 2048 sondes", "[16:02:12]  Prêt"};
        float ly = by + P(12);
        for (const char* l : lines) { DrawText(g_f.mono, 11.5f, x + P(14), ly, kText2, l); ly += P(20); }
    } else {
        TextMid(g_f.ui, 13, x + P(14), by + P(12), P(20), kText3, "Le graph de la scène s'ouvre dans l'onglet GRAPH.");
    }
}

// --------------------------------------------------------------------------- inspector (.panel.right)
// section header (.sh): 30 px, icon, title, chevron on the right; returns true when clicked
bool SectionHeader(const char* id, float x, float y, float w, Icon icon, const char* title, Icon chev) {
    bool hv;
    bool q = Hit(id, x, y, w, P(30), &hv);
    DrawIcon(icon, x + P(12), y + P(7), 16, kText2);
    TextMid(g_semi, 13, x + P(12 + 16 + 8), y, P(30), kText, title);
    DrawIcon(chev, x + w - P(12 + 16), y + P(7), 16, hv ? kText2 : kText3);
    return q;
}
// one X / Y / Z field (.f): drag to edit
void AxisField(const char* id, float x, float y, float w, int axis, float* v, const char* fmt) {
    const char* names[] = {"X", "Y", "Z"};
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    ImGui::InvisibleButton(id, ImVec2(w, P(25)));
    const bool active = ImGui::IsItemActive(), hv = ImGui::IsItemHovered();
    if (active) *v += ImGui::GetIO().MouseDelta.x * 0.05f;
    if (hv || active) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    Rect(x, y, w, P(25), kBase, P(2));
    Border(x, y, w, P(25), active ? kAccentLo : (hv ? kLineHi : kLine), P(2));
    float tx = TextMid(g_monoBold, 10, x + P(7), y, P(25), kAxis[axis], names[axis]);
    char buf[32];
    snprintf(buf, sizeof(buf), fmt, *v);
    dl->PushClipRect(ImVec2(x, y), ImVec2(x + w - P(4), y + P(25)), true);
    TextMid(g_f.mono, 11.5f, tx + P(6), y, P(25), kText, buf);
    dl->PopClipRect();
}
// slider (.slider): fill up to t, value text inside; drag to edit
void Slider(const char* id, float x, float y, float w, float* v, float lo, float hi, const char* text) {
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    ImGui::InvisibleButton(id, ImVec2(w, P(25)));
    if (ImGui::IsItemActive()) *v = lo + std::clamp((ImGui::GetMousePos().x - x) / w, 0.0f, 1.0f) * (hi - lo);
    const float t = (*v - lo) / (hi - lo);
    Rect(x, y, w, P(25), kBase, P(2));
    const float fx = x + 1 + (w - 2) * t;
    Rect(x + 1, y + 1, fx - x - 1, P(25) - 2, kAccentBg);
    Rect(fx - P(2), y + 1, P(2), P(25) - 2, kAccent);
    Border(x, y, w, P(25), kLine, P(2));
    TextMid(g_f.mono, 11.5f, x + P(8), y, P(25), kText, text);
}

void Inspector(float x, float y, float w, float h) {
    Rect(x, y, w, h, kPanel);
    VLine(x, y, y + h, kLine);
    PanelHeader(x, y, w, "Inspecteur", nullptr);
    float sy = y + P(32);
    const float labelW = P(88), gap = P(8), px = x + P(12), fieldX = px + labelW + gap, fieldW = x + w - P(12) - fieldX;
    auto label = [&](float ry, float rh, const char* t, ImU32 c = 0) { TextMid(g_f.ui, 13, px, ry, rh, c ? c : kText2, t); };

    // name
    SectionHeader("sh0", x, sy, w, I_HELMET, "Marine_003", I_MORE);
    sy += P(30);
    HLine(x, x + w, sy - 1, kLine);
    // Transform
    if (SectionHeader("sh1", x, sy, w, I_MOVE3D, "Transform", S.sectOpen[1] ? I_CHEV_DOWN : I_CHEV_RIGHT)) S.sectOpen[1] = !S.sectOpen[1];
    sy += P(30);
    if (S.sectOpen[1]) {
        const char* rows[] = {"Position", "Rotation", "Échelle"};
        float* vals[] = {S.pos, S.rot, S.scl};
        const char* fmts[] = {"%.1f", "%.0f", "%.0f"};
        const float cw = (fieldW - P(4) * 2) / 3;
        for (int r = 0; r < 3; ++r) {
            const float ry = sy + P(4);
            label(ry, P(25), rows[r]);
            for (int a = 0; a < 3; ++a) {
                char id[16];
                snprintf(id, sizeof(id), "ax%d%d", r, a);
                AxisField(id, fieldX + a * (cw + P(4)), ry, cw, a, &vals[r][a], fmts[r]);
            }
            sy += P(4 + 25 + 4);
        }
        sy += P(6);   // last row: padding-bottom 10
    }
    HLine(x, x + w, sy - 1, kLine);
    // Unité RTS
    if (SectionHeader("sh2", x, sy, w, I_HELMET, "Unité RTS", S.sectOpen[2] ? I_CHEV_DOWN : I_CHEV_RIGHT)) S.sectOpen[2] = !S.sectOpen[2];
    sy += P(30);
    if (S.sectOpen[2]) {
        float ry = sy + P(4);
        // Faction (.select)
        label(ry, P(25), "Faction");
        {
            bool hv;
            if (Hit("faction", fieldX, ry, fieldW, P(25), &hv)) ImGui::OpenPopup("factionPopup");
            Rect(fieldX, ry, fieldW, P(25), hv ? kHover : kPanel2, P(2));
            Border(fieldX, ry, fieldW, P(25), kLineHi, P(2));
            const char* factions[] = {"Joueur 1", "Joueur 2", "Neutre"};
            TextMid(g_f.ui, 13, fieldX + P(8), ry, P(25), kText, factions[S.faction]);
            DrawIcon(I_CHEV_DOWN, fieldX + fieldW - P(8 + 16), ry + P(4.5f), 16, kText2);
            ImGui::SetNextWindowPos(ImVec2(fieldX, ry + P(27)));
            ImGui::SetNextWindowSize(ImVec2(fieldW, 0));
            if (ImGui::BeginPopup("factionPopup")) {
                for (int i = 0; i < 3; ++i)
                    if (ImGui::Selectable(factions[i], S.faction == i)) S.faction = i;
                ImGui::EndPopup();
            }
        }
        ry += P(33);
        label(ry, P(25), "Vitesse");
        char sp[16];
        snprintf(sp, sizeof(sp), "%.2f", S.speed);
        Slider("speed", fieldX, ry, fieldW, &S.speed, 0, 5.08f, sp);
        ry += P(33);
        label(ry, P(25), "Points de vie");
        char hp[24];
        snprintf(hp, sizeof(hp), "%.0f / 100", S.hp);
        Slider("hp", fieldX, ry, fieldW, &S.hp, 0, 100, hp);
        ry += P(33);
        // Sélectionnable (.toggle 30 x 16)
        label(ry, P(18), "Sélectionnable");
        {
            if (Hit("selectable", fieldX, ry + 1, P(30), P(16))) S.selectable = !S.selectable;
            Rect(fieldX, ry + 1, P(30), P(16), S.selectable ? kAccentLo : kLine, P(2));
            const float kx = S.selectable ? fieldX + P(30 - 2 - 12) : fieldX + P(2);
            Rect(kx, ry + 1 + P(2), P(12), P(12), S.selectable ? kAccent : kText3, P(1));
        }
        ry += P(18 + 8);
        label(ry, P(18), "Coût", kText3);
        ry += P(18 + 4 + 6);
        // costs (.cost)
        {
            float cx = px;
            struct C { Icon icon; const char* t; };
            const C costs[] = {{I_OUTPOST, "50"}, {I_CLOCK, "12 s"}};
            for (const C& c : costs) {
                float cw = P(9 + 16 + 6 + 9) + TextSize(g_f.mono, 11.5f, c.t).x;
                Rect(cx, ry, cw, P(25), kPanel2, P(2));
                Border(cx, ry, cw, P(25), kLine, P(2));
                DrawIcon(c.icon, cx + P(9), ry + P(4.5f), 16, kText2);
                TextMid(g_f.mono, 11.5f, cx + P(9 + 16 + 6), ry, P(25), kText, c.t);
                cx += cw + P(10);
            }
        }
        sy = ry + P(25 + 12);
    }
    HLine(x, x + w, sy - 1, kLine);
    // Grille
    if (SectionHeader("sh3", x, sy, w, I_GRID, "Grille", S.sectOpen[3] ? I_CHEV_DOWN : I_CHEV_RIGHT)) S.sectOpen[3] = !S.sectOpen[3];
    sy += P(30);
    if (S.sectOpen[3]) {
        label(sy + P(4), P(25), "Taille");
        static float cell[3] = {1, 1, 1};
        const float cw = (fieldW - P(4) * 2) / 3;
        for (int a = 0; a < 3; a += 2) AxisField(a ? "gz" : "gx", fieldX + (a / 2) * (cw + P(4)), sy + P(4), cw, a, &cell[a], "%.1f");
        sy += P(4 + 25 + 10);
    }
    HLine(x, x + w, sy - 1, kLine);
}

// --------------------------------------------------------------------------- status bar (.status)
void StatusBar(float x, float y, float w, float h) {
    Rect(x, y, w, h, kBase);
    HLine(x, x + w, y, kLine);
    float tx = x + P(12);
    Rect(tx, y + (h - P(6)) / 2, P(6), P(6), kOk);
    tx += P(6 + 6);
    tx = TextMid(g_f.mono, 11, tx, y, h, kText3, "Prêt") + P(16);
    tx = TextMid(g_f.mono, 11, tx, y, h, kText3, "DirectX 12 · GPU") + P(16);
    TextMid(g_f.mono, 11, tx, y, h, kText3, "Scène : Village.wzx");
    const char* right = "Grille 1.0 m · Snap actif";
    TextMid(g_f.mono, 11, x + w - P(12) - TextSize(g_f.mono, 11, right).x, y, h, kText3, right);
}

} // namespace

void Init(ImTextureID atlas, int atlasW, int atlasH, const Fonts& fonts) {
    g_atlas = atlas;
    g_atlasW = (float)atlasW;
    g_atlasH = (float)atlasH;
    g_f = fonts;
    ImGuiIO& io = ImGui::GetIO();
    g_semi = io.Fonts->Fonts.Size > 3 ? io.Fonts->Fonts[3] : fonts.ui;
    g_monoBold = io.Fonts->Fonts.Size > 4 ? io.Fonts->Fonts[4] : fonts.mono;
}

void Draw(float dpiScale) {
    k = dpiScale;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float W = vp->Size.x, H = vp->Size.y;
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##halo", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar(2);
    dl = ImGui::GetWindowDrawList();
    // grid: 44 top bar, 26 status bar; columns 48 | 264 | 1fr | 300, rows 1fr | 210 (the drawer spans columns 2 and 3)
    const float top = P(44), status = P(26), body = H - top - status;
    const float railW = P(48), leftW = P(264), rightW = P(300), drawerH = P(210);
    const float viewX = railW + leftW, viewW = W - viewX - rightW, rowH = body - drawerH;
    TopBar(W);
    Rail(0, top, railW, body);
    ScenePanel(railW, top, leftW, rowH);
    Viewport(viewX, top, viewW, rowH);
    Drawer(railW, top + rowH, leftW + viewW, drawerH);
    Inspector(W - rightW, top, rightW, body);
    StatusBar(0, H - status, W, status);
    ImGui::End();
}

} // namespace halo
