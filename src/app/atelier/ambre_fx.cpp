#include "ambre_fx.h"
#include "atelier_style.h"
#include <algorithm>
#include <cmath>

namespace atelier::fx {

bool Coral() { return Style().skin == 2; }

void TintCoral(ImU32 (*tint)(ImU32)) {
    ImU32* vars[] = {&coral::shell, &coral::panel, &coral::viewport, &coral::card, &coral::input, &coral::raised, &coral::border,
                     &coral::borderStrong, &coral::text, &coral::muted, &coral::dim, &coral::selection, &coral::selectionBorder,
                     &coral::fill, &coral::thumb, &coral::buttonTop, &coral::buttonBottom, &coral::hoverTop, &coral::hoverBottom};
    static ImU32 base[sizeof(vars) / sizeof(vars[0])];   // pack values, saved before the first recolouring
    static bool saved = false;
    for (size_t i = 0; i < sizeof(vars) / sizeof(vars[0]); ++i) {
        if (!saved) base[i] = *vars[i];
        *vars[i] = tint(base[i]);
    }
    saved = true;
}

const ImVec2 kGlowOffsets[8] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {0.7f, 0.7f}, {-0.7f, 0.7f}, {0.7f, -0.7f}, {-0.7f, -0.7f}};

ImU32 Alpha(ImU32 c, float a) {
    int al = (int)(((c >> IM_COL32_A_SHIFT) & 255) * std::clamp(a, 0.0f, 1.0f) + 0.5f);
    return (c & ~IM_COL32_A_MASK) | ((ImU32)al << IM_COL32_A_SHIFT);
}

namespace {
ImVec4 v4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }
ImVec4 lerp(const ImVec4& a, const ImVec4& b, float t) { return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t); }

struct Stop { float t; ImU32 c; };
// Recolours the vertices [v0, v1) along p0 -> p1 with colour stops; the vertex alpha (anti-aliased fringe) is kept as a factor.
void Shade(ImDrawList* dl, int v0, int v1, ImVec2 p0, ImVec2 p1, const Stop* stops, int n) {
    ImVec2 d(p1.x - p0.x, p1.y - p0.y);
    float len2 = std::max(1e-6f, d.x * d.x + d.y * d.y);
    ImVec4 cs[4];
    for (int i = 0; i < n; ++i) cs[i] = v4(stops[i].c);
    for (int i = v0; i < v1; ++i) {
        ImDrawVert& v = dl->VtxBuffer[i];
        float t = std::clamp(((v.pos.x - p0.x) * d.x + (v.pos.y - p0.y) * d.y) / len2, 0.0f, 1.0f);
        ImVec4 c = cs[n - 1];
        for (int k = 0; k + 1 < n; ++k)
            if (t <= stops[k + 1].t) { c = lerp(cs[k], cs[k + 1], (t - stops[k].t) / std::max(1e-6f, stops[k + 1].t - stops[k].t)); break; }
        c.w *= ((v.col >> IM_COL32_A_SHIFT) & 255) / 255.0f;
        v.col = ImGui::ColorConvertFloat4ToU32(c);
    }
}
} // namespace

void Gradient(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 top, ImU32 bottom, float rounding, ImDrawFlags flags) {
    int v0 = dl->VtxBuffer.Size;
    dl->AddRectFilled(a, b, IM_COL32_WHITE, rounding, flags);
    const Stop st[2] = {{0, top}, {1, bottom}};
    Shade(dl, v0, dl->VtxBuffer.Size, a, ImVec2(a.x, b.y), st, 2);
}

void GradientH(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 left, ImU32 right, float rounding) {
    int v0 = dl->VtxBuffer.Size;
    dl->AddRectFilled(a, b, IM_COL32_WHITE, rounding);
    const Stop st[2] = {{0, left}, {1, right}};
    Shade(dl, v0, dl->VtxBuffer.Size, a, ImVec2(b.x, a.y), st, 2);
}

void Glow(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 color, float rounding, float spread, float strength) {
    int steps = std::clamp((int)std::ceil(spread), 3, 10);
    float w = spread / steps;
    for (int i = 0; i < steps; ++i) {
        float d = (i + 0.5f) * w, k = 1.0f - (float)i / steps;
        dl->AddRect(ImVec2(a.x - d, a.y - d), ImVec2(b.x + d, b.y + d), Alpha(color, strength * k * k), rounding + d, 0, w + 0.35f);
    }
}

void TopEdge(ImDrawList* dl, ImVec2 a, ImVec2 b, float s, ImU32 color, float alpha) {
    dl->AddLine(ImVec2(a.x + 3 * s, a.y + 1.5f * s), ImVec2(b.x - 3 * s, a.y + 1.5f * s), Alpha(color, alpha), std::max(1.0f, 0.6f * s));
}

ImU32 Button(ImDrawList* dl, ImVec2 a, ImVec2 b, Face face, float s, float rounding) {
    if (Coral()) {   // button-secondary / button-build / icon-button-move.svg: 7 px radius, no glow, no light edge
        const float r = 7 * s;
        switch (face) {
        case Face::Active:
            dl->AddRectFilled(a, b, coral::selection, 6 * s);
            dl->AddRect(a, b, coral::selectionBorder, 6 * s);
            return coral::accent;
        case Face::Primary:
            Gradient(dl, a, b, coral::primaryTop, coral::primaryBottom, r);
            dl->AddRect(a, b, coral::primaryBorder, r);
            return coral::primaryText;
        case Face::Hover:
            Gradient(dl, a, b, coral::hoverTop, coral::hoverBottom, r);
            dl->AddRect(a, b, coral::borderStrong, r);
            return coral::text;
        case Face::Disabled:
            dl->AddRectFilled(a, b, Alpha(coral::buttonBottom, 0.6f), r);
            dl->AddRect(a, b, coral::border, r);
            return coral::dim;
        default:
            Gradient(dl, a, b, coral::buttonTop, coral::buttonBottom, r);
            dl->AddRect(a, b, coral::border, r);
            return coral::text;
        }
    }
    const float r = rounding * s;
    switch (face) {
    case Face::Active:
        Glow(dl, a, b, col::accent, r, 6 * s, 0.15f);
        Gradient(dl, a, b, col::activeTop, col::activeBottom, r);
        dl->AddRect(a, b, col::accentBorder, r);
        // inner amber ring with its own small blur (the "#D19B43, 45 %" ring of toolbar-move-active.svg)
        dl->AddRect(ImVec2(a.x + 1 * s, a.y + 1 * s), ImVec2(b.x - 1 * s, b.y - 1 * s), Alpha(col::rgb(0xD19B43), 0.10f), r - s, 0, 2.2f * s);
        dl->AddRect(ImVec2(a.x + 1 * s, a.y + 1 * s), ImVec2(b.x - 1 * s, b.y - 1 * s), Alpha(col::rgb(0xD19B43), 0.45f), r - s, 0, 0.65f * s);
        TopEdge(dl, a, b, s, col::rgb(0xC3BDA5));
        return col::accentHi;
    case Face::Primary:
        Glow(dl, a, b, col::accent, r, 4 * s, 0.09f);
        Gradient(dl, a, b, col::primaryTop, col::primaryBottom, r);
        dl->AddRect(a, b, col::primaryBorder, r);
        TopEdge(dl, a, b, s, IM_COL32_WHITE, 0.35f);
        return col::primaryText;
    case Face::Hover:
        Gradient(dl, a, b, col::hoverTop, col::hoverBottom, r);
        dl->AddRect(a, b, col::rgb(0x4A5058), r);
        TopEdge(dl, a, b, s, col::rgb(0xB5BAC0), 0.18f);
        return col::text;
    case Face::Disabled:
        Gradient(dl, a, b, Alpha(col::buttonTop, 0.6f), Alpha(col::buttonBottom, 0.6f), r);
        dl->AddRect(a, b, Alpha(col::borderBtn, 0.7f), r);
        return col::disabled;
    default:
        Gradient(dl, a, b, col::buttonTop, col::buttonBottom, r);
        dl->AddRect(a, b, col::borderBtn, r);
        TopEdge(dl, a, b, s, col::rgb(0xB5BAC0));
        return col::text;
    }
}

void Panel(ImDrawList* dl, ImVec2 a, ImVec2 b, float s, float headerH, float rounding) {
    if (Coral()) {   // matte card, 8 px radius, no border; the title zone ends with a thin separator
        dl->AddRectFilled(a, b, coral::panel, 8 * s);
        if (headerH > 0) dl->AddLine(ImVec2(a.x, a.y + headerH), ImVec2(b.x, a.y + headerH), coral::border);
        return;
    }
    const float r = rounding * s;
    Gradient(dl, a, b, col::surfaceTop, col::surfaceBottom, r);
    if (headerH > 0) {
        ImVec2 hb(b.x, a.y + headerH);
        Gradient(dl, a, hb, col::headerTop, col::headerBottom, r, ImDrawFlags_RoundCornersTop);
        dl->AddLine(ImVec2(a.x + 1, hb.y), ImVec2(b.x - 1, hb.y), col::border);
    }
    dl->AddRect(a, b, col::border, r);
    TopEdge(dl, a, b, s, col::rgb(0xB5BAC0), 0.10f);
}

void SelectedRow(ImDrawList* dl, ImVec2 a, ImVec2 b, float s) {
    if (Coral()) {   // hierarchy-selected.svg
        dl->AddRectFilled(a, b, coral::selection, 5 * s);
        dl->AddRect(a, b, coral::selectionBorder, 5 * s);
        return;
    }
    GradientH(dl, a, b, col::selectedLeft, col::selectedRight, 0);
    dl->AddRectFilled(a, ImVec2(a.x + 2 * s, b.y), col::accent);
}

void TabLine(ImDrawList* dl, ImVec2 a, ImVec2 b, bool active, bool hovered, float s) {
    ImVec2 la(a.x, b.y - 2 * s), lb(b.x, b.y);
    if (Coral()) {   // coral underline, no glow
        if (active) dl->AddRectFilled(la, lb, coral::accent, 1 * s);
        else if (hovered) dl->AddRectFilled(ImVec2(a.x, b.y - 1 * s), lb, coral::borderStrong);
        return;
    }
    if (active) {
        Glow(dl, la, lb, col::accent, 1 * s, 3 * s, 0.11f);
        dl->AddRectFilled(la, lb, col::accent);
    } else if (hovered) {
        dl->AddRectFilled(ImVec2(a.x, b.y - 1 * s), lb, col::rgb(0x4A5058));
    }
}

void SelectedFrame(ImDrawList* dl, ImVec2 a, ImVec2 b, float s, float rounding) {
    if (Coral()) { dl->AddRect(a, b, coral::accent, 6 * s, 0, 1.2f * s); return; }
    Glow(dl, a, b, col::accent, rounding * s, 5 * s, 0.16f);
    dl->AddRect(a, b, col::accent, rounding * s, 0, 1.5f * s);
}

void Field(ImDrawList* dl, ImVec2 a, ImVec2 b, float s, bool focus, float rounding) {
    if (Coral()) {
        dl->AddRectFilled(a, b, coral::input, 5 * s);
        dl->AddRect(a, b, focus ? coral::selectionBorder : coral::border, 5 * s);
        return;
    }
    dl->AddRectFilled(a, b, col::input, rounding * s);
    dl->AddRect(a, b, focus ? col::accent : col::fieldBorder, rounding * s);
}

ImU32 AxisColor(int axis) {
    if (Coral()) return axis == 0 ? coral::axisX : axis == 1 ? coral::axisY : coral::axisZ;
    return axis == 0 ? col::axisX : axis == 1 ? col::axisY : col::axisZ;
}

void AxisTag(ImDrawList* dl, ImVec2 a, ImVec2 b, int axis, float s) {
    if (Coral()) return;   // vector-position.svg: only the coloured letter
    dl->AddRectFilled(ImVec2(a.x + 1 * s, a.y + 1 * s), ImVec2(a.x + 22 * s, b.y - 1 * s), Alpha(AxisColor(axis), 0.18f), 2 * s);
}

void SliderTrack(ImDrawList* dl, float x0, float x1, float y, float t, bool enabled, bool focus, float s) {
    t = std::clamp(t, 0.0f, 1.0f);
    float hx0 = x0 + 2.5f * s, hx1 = x1 - 2.5f * s, x = hx0 + (hx1 - hx0) * t;
    dl->AddRectFilled(ImVec2(hx0, y - 1.5f * s), ImVec2(hx1, y + 1.5f * s), col::border, 1.5f * s);
    dl->AddRectFilled(ImVec2(hx0, y - 1.5f * s), ImVec2(x, y + 1.5f * s), enabled ? col::accent : col::rgb(0x68717B), 1.5f * s);
    ImVec2 ha(x - 2.5f * s, y - 8 * s), hb(x + 2.5f * s, y + 8 * s);
    dl->AddRectFilled(ha, hb, enabled ? col::thumb : col::disabled, 1 * s);   // matte: no glow, no chrome
    if (focus) dl->AddRect(ImVec2(ha.x - 1.5f * s, ha.y - 1.5f * s), ImVec2(hb.x + 1.5f * s, hb.y + 1.5f * s), col::accent, 2 * s, 0, 1 * s);
}

void Checkbox(ImDrawList* dl, ImVec2 a, ImVec2 b, bool on, bool hovered, float s) {
    if (Coral()) {   // checkbox-checked.svg: filled coral box, dark tick; unchecked: dark box, grey border
        float r = 3 * s, w = b.x - a.x, h = b.y - a.y;
        dl->AddRectFilled(a, b, on ? coral::accent : coral::input, r);
        if (!on) dl->AddRect(a, b, hovered ? col::rgb(0x59616A) : coral::borderStrong, r);
        else {
            ImVec2 pts[3] = {{a.x + w * 0.24f, a.y + h * 0.52f}, {a.x + w * 0.42f, a.y + h * 0.70f}, {a.x + w * 0.77f, a.y + h * 0.31f}};
            dl->AddPolyline(pts, 3, coral::shell, 0, std::max(1.3f, w * 0.10f));
        }
        return;
    }
    float r = 2 * s;
    dl->AddRectFilled(a, b, col::checkBg, r);
    dl->AddRect(a, b, on ? col::accent : (hovered ? col::rgb(0x7A838E) : col::checkOff), r, 0, 1.4f * s);
    if (on) {
        float w = b.x - a.x, h = b.y - a.y;
        ImVec2 pts[3] = {{a.x + w * 0.24f, a.y + h * 0.50f}, {a.x + w * 0.41f, a.y + h * 0.67f}, {a.x + w * 0.76f, a.y + h * 0.31f}};
        dl->AddPolyline(pts, 3, col::accent, 0, std::max(1.2f, w * 0.085f));
    }
}

void BrandMark(ImDrawList* dl, ImVec2 p, float size) {
    // paths of components/brand.svg (32 x 32 units), gradients gold-face and gold-dark
    const float k = size / 32.0f;
    const Stop face[3] = {{0, col::rgb(0xFFF1CA)}, {0.45f, col::rgb(0xECA43D)}, {1, col::rgb(0x98602B)}};
    const Stop dark[3] = {{0, col::rgb(0x7B5226)}, {0.55f, col::rgb(0xCB8632)}, {1, col::rgb(0x4E321B)}};
    struct Poly { int n; float pts[16]; const Stop* grad; ImU32 flat; };
    const Poly polys[] = {
        {6, {16, 1, 28, 8, 22, 12, 16, 8, 10, 12, 4, 8}, face, 0},
        {4, {4, 8, 4, 17, 10, 20, 10, 12}, dark, 0},
        {4, {28, 8, 28, 17, 22, 20, 22, 12}, nullptr, col::rgb(0xA76726)},
        {6, {10, 12, 16, 16, 22, 12, 22, 20, 16, 24, 10, 20}, face, 0},
        {8, {2, 15, 8, 19, 8, 27, 16, 31, 16, 20, 10, 16, 10, 21, 2, 17}, dark, 0},
        {7, {30, 15, 22, 21, 22, 16, 16, 20, 16, 31, 24, 27, 24, 19}, face, 0},
        {4, {2, 15, 10, 11, 10, 16, 8, 19}, nullptr, col::rgb(0xFFD28D)},
        {4, {30, 15, 22, 11, 22, 16, 24, 19}, nullptr, col::rgb(0xC18A43)},
    };
    for (const Poly& pl : polys) {
        ImVec2 pts[8], mn(1e9f, 1e9f), mx(-1e9f, -1e9f);
        for (int i = 0; i < pl.n; ++i) {
            pts[i] = ImVec2(p.x + pl.pts[i * 2] * k, p.y + pl.pts[i * 2 + 1] * k);
            mn = ImVec2(std::min(mn.x, pts[i].x), std::min(mn.y, pts[i].y));
            mx = ImVec2(std::max(mx.x, pts[i].x), std::max(mx.y, pts[i].y));
        }
        int v0 = dl->VtxBuffer.Size;
        dl->AddConcavePolyFilled(pts, pl.n, pl.grad ? IM_COL32_WHITE : pl.flat);
        if (pl.grad) Shade(dl, v0, dl->VtxBuffer.Size, mn, mx, pl.grad, 3);
        dl->AddPolyline(pts, pl.n, col::rgb(0xFFD08A, 150), ImDrawFlags_Closed, std::max(0.6f, 0.55f * k));
    }
}

float SpacedText(ImDrawList* dl, ImFont* font, float size, ImVec2 p, ImU32 color, const char* text, float spacing) {
    float x = p.x;
    for (const char* c = text; *c; ++c) {
        dl->AddText(font, size, ImVec2(x, p.y), color, c, c + 1);
        x += font->CalcTextSizeA(size, 1e9f, 0, c, c + 1).x + spacing;
    }
    return x - spacing;
}

void Shadow(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding, float s) {
    const int steps = 6;
    for (int i = 0; i < steps; ++i) {
        float d = (i + 0.5f) * s, k = 1.0f - (float)i / steps;
        dl->AddRect(ImVec2(a.x - d, a.y - d + 4 * s), ImVec2(b.x + d, b.y + d + 4 * s), IM_COL32(0, 0, 0, (int)(64 * k * k)), rounding + d, 0, 1.35f * s);
    }
    dl->AddRectFilled(ImVec2(a.x, a.y + 4 * s), ImVec2(b.x, b.y + 4 * s), IM_COL32(0, 0, 0, 50), rounding);
}

void FloatBar(ImDrawList* dl, ImVec2 a, ImVec2 b, float s) {
    Shadow(dl, a, b, 8 * s, s);
    dl->AddRectFilled(a, b, coral::panel, 8 * s);
}

void SliderField(ImDrawList* dl, ImFont* font, float textSize, ImVec2 a, ImVec2 b, float t, const char* label, const char* value, bool focus, float s) {
    // row a..b: label (left) and value (right) on the text line, then the bar: the field of the pack at a third of
    // its height (11 px instead of 32), its fill and the matte 5 x 16 handle across it
    t = std::clamp(t, 0.0f, 1.0f);
    const float bh = 11 * s, r = 3.5f * s, cy = (a.y + 16 * s + b.y) / 2;
    ImVec2 ba(a.x, cy - bh / 2), bb(b.x, cy + bh / 2);
    dl->AddRectFilled(ba, bb, coral::input, r);
    float x = ba.x + 1 + (bb.x - ba.x - 2) * t;
    if (x > ba.x + 1.5f) dl->AddRectFilled(ImVec2(ba.x + 1, ba.y + 1), ImVec2(x, bb.y - 1), coral::fill, r - 1, x > bb.x - r ? ImDrawFlags_RoundCornersAll : ImDrawFlags_RoundCornersLeft);
    dl->AddRect(ba, bb, focus ? coral::selectionBorder : coral::border, r);
    float hx = std::clamp(x, ba.x + 3.5f * s, bb.x - 3.5f * s);
    dl->AddRectFilled(ImVec2(hx - 2.5f * s, cy - 8 * s), ImVec2(hx + 2.5f * s, cy + 8 * s), coral::thumb, 1 * s);
    dl->AddText(font, textSize, ImVec2(a.x + 1 * s, a.y), coral::text, label);
    float vw = font->CalcTextSizeA(textSize, 1e9f, 0, value).x;
    dl->AddText(font, textSize, ImVec2(b.x - 1 * s - vw, a.y), coral::text, value);
}

float Brand(ImDrawList* dl, ImFont* font, ImFont* bold, ImVec2 p, float s) {
    if (Coral()) {   // rounded dark tile with the coral cube, "WZX Engine" and a small spaced subtitle
        const float k = 26 * s / 34.0f;
        ImVec2 o(p.x, p.y - 1 * s);
        dl->AddRectFilled(o, ImVec2(o.x + 34 * k, o.y + 34 * k), col::rgb(0x202328), 9 * k);
        auto P = [&](float x, float y) { return ImVec2(o.x + x * k, o.y + y * k); };
        ImVec2 top[4] = {P(17, 5), P(27, 11), P(17, 17), P(7, 11)};
        ImVec2 left[4] = {P(7, 11), P(17, 17), P(17, 29), P(7, 23)};
        ImVec2 right[4] = {P(27, 11), P(17, 17), P(17, 29), P(27, 23)};
        dl->AddConvexPolyFilled(top, 4, col::rgb(0xE8B49B));
        dl->AddConvexPolyFilled(left, 4, col::rgb(0xCE846A));
        dl->AddConvexPolyFilled(right, 4, col::rgb(0xA85F47));
        float x = o.x + 34 * k + 10 * s;
        dl->AddText(bold ? bold : font, 14 * s, ImVec2(x, o.y + 1 * s), coral::text, "WZX Engine");
        float e1 = x + (bold ? bold : font)->CalcTextSizeA(14 * s, 1e9f, 0, "WZX Engine").x;
        float e2 = SpacedText(dl, font, 8.5f * s, ImVec2(x, o.y + 17 * s), coral::dim, "EDITOR", 1.2f * s);
        return std::max(e1, e2);
    }
    const float mark = 24 * s;
    Glow(dl, ImVec2(p.x + 6 * s, p.y + 6 * s), ImVec2(p.x + mark - 6 * s, p.y + mark - 6 * s), col::accent, 6 * s, 6 * s, 0.05f);
    BrandMark(dl, p, mark);
    float x = p.x + mark + 9 * s;
    const float big = 15 * s, small = 12.5f * s;
    x = SpacedText(dl, bold ? bold : font, big, ImVec2(x, p.y + (mark - big) / 2 - 1 * s), col::text, "WZX", 2.0f * s);
    x = SpacedText(dl, font, small, ImVec2(x + 7 * s, p.y + (mark - big) / 2 + 1 * s), col::rgb(0xC9CBD0), "ENGINE", 1.8f * s);
    return x;
}

} // namespace atelier::fx
