#include "ambre_fx.h"
#include <algorithm>
#include <cmath>

namespace atelier::fx {

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
    GradientH(dl, a, b, col::selectedLeft, col::selectedRight, 0);
    dl->AddRectFilled(a, ImVec2(a.x + 2 * s, b.y), col::accent);
}

void TabLine(ImDrawList* dl, ImVec2 a, ImVec2 b, bool active, bool hovered, float s) {
    ImVec2 la(a.x, b.y - 2 * s), lb(b.x, b.y);
    if (active) {
        Glow(dl, la, lb, col::accent, 1 * s, 3 * s, 0.11f);
        dl->AddRectFilled(la, lb, col::accent);
    } else if (hovered) {
        dl->AddRectFilled(ImVec2(a.x, b.y - 1 * s), lb, col::rgb(0x4A5058));
    }
}

void SelectedFrame(ImDrawList* dl, ImVec2 a, ImVec2 b, float s, float rounding) {
    Glow(dl, a, b, col::accent, rounding * s, 5 * s, 0.16f);
    dl->AddRect(a, b, col::accent, rounding * s, 0, 1.5f * s);
}

void Field(ImDrawList* dl, ImVec2 a, ImVec2 b, float s, bool focus, float rounding) {
    dl->AddRectFilled(a, b, col::input, rounding * s);
    dl->AddRect(a, b, focus ? col::accent : col::fieldBorder, rounding * s);
}

ImU32 AxisColor(int axis) { return axis == 0 ? col::axisX : axis == 1 ? col::axisY : col::axisZ; }

void AxisTag(ImDrawList* dl, ImVec2 a, ImVec2 b, int axis, float s) {
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

float Brand(ImDrawList* dl, ImFont* font, ImFont* bold, ImVec2 p, float s) {
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
