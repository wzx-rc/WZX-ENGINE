#include "GameUI.h"
#include <windows.h>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace vp3d {

static std::string utf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}

static ImU32 col(const Color& c, float alpha = 1.0f) {
    auto q = [](float v) { return (int)std::lround(std::min(std::max(v, 0.0f), 1.0f) * 255.0f); };
    return IM_COL32(q(c.r), q(c.g), q(c.b), q(c.a * alpha));
}

static ImGuiKey imKey(int vk) {
    if (vk >= 'A' && vk <= 'Z') return (ImGuiKey)(ImGuiKey_A + (vk - 'A'));
    if (vk >= '0' && vk <= '9') return (ImGuiKey)(ImGuiKey_0 + (vk - '0'));
    if (vk >= VK_F1 && vk <= VK_F12) return (ImGuiKey)(ImGuiKey_F1 + (vk - VK_F1));
    switch (vk) {
    case VK_SPACE: return ImGuiKey_Space;
    case VK_RETURN: return ImGuiKey_Enter;
    case VK_ESCAPE: return ImGuiKey_Escape;
    case VK_DELETE: return ImGuiKey_Delete;
    case VK_BACK: return ImGuiKey_Backspace;
    case VK_TAB: return ImGuiKey_Tab;
    case VK_LEFT: return ImGuiKey_LeftArrow;
    case VK_RIGHT: return ImGuiKey_RightArrow;
    case VK_UP: return ImGuiKey_UpArrow;
    case VK_DOWN: return ImGuiKey_DownArrow;
    }
    return ImGuiKey_None;
}

uint32_t UI::hashId(const char* s) {
    uint32_t h = 2166136261u;
    while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
    return h ? h : 1;
}

// --------------------------------------------------------------------------- input

bool UI::keyPressed(int vk) const {
    if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU) return false;
    ImGuiKey k = imKey(vk);
    return k != ImGuiKey_None && ImGui::IsKeyPressed(k, true);   // repeats, like the former UI (backspace, arrows)
}

bool UI::keyDown(int vk) const {
    const ImGuiIO& io = ImGui::GetIO();
    if (vk == VK_SHIFT) return io.KeyShift;
    if (vk == VK_CONTROL) return io.KeyCtrl;
    if (vk == VK_MENU) return io.KeyAlt;
    ImGuiKey k = imKey(vk);
    return k != ImGuiKey_None && ImGui::IsKeyDown(k);
}

// --------------------------------------------------------------------------- frame

void UI::newFrame(float dpiScale, float w, float h) {
    dpi = dpiScale > 0 ? dpiScale : 1.0f;
    width = w;
    height = h;
    const ImGuiIO& io = ImGui::GetIO();
    mx = io.MousePos.x / dpi;
    my = io.MousePos.y / dpi;
    for (int i = 0; i < 3; ++i) {
        mouseDown[i] = ImGui::IsMouseDown(i);
        mousePressed[i] = ImGui::IsMouseClicked(i);
        mouseReleased[i] = ImGui::IsMouseReleased(i);
    }
    mouseDoubleClick = ImGui::IsMouseDoubleClicked(0);
    wheel = io.MouseWheel;

    // Characters typed this frame (control keys are read with keyPressed).
    m_chars.clear();
    for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
        ImWchar c = io.InputQueueCharacters[i];
        if (c) m_chars.push_back((wchar_t)c);
    }

    m_blocked = false;
    if (m_popupId) {
        Rect pr = popupRect();
        if (pr.contains(mx, my)) m_blocked = true;
        else if (mousePressed[0] || mousePressed[1]) {
            // A click outside closes the popup; still let it reach the anchor (toggle) logic.
            if (!m_popupAnchor.contains(mx, my)) {
                m_popupId = 0;
                consumeClick();
            }
        }
    }
    if (m_activeId && !mouseDown[0]) m_activeId = 0;
    m_tooltip.clear();
}

void UI::end() {
    while (m_clipDepth > 0) popClip();   // a widget that forgot its popClip must not leak into the next frame
    drawPopup();
    if (!m_tooltip.empty()) {
        float tw = textWidth(m_tooltip, 12) + 16;
        Rect r(mx + 14, my + 18, tw, 24);
        if (r.r() > width) r.x = width - r.w - 4;
        fill(r, Color::hex(0x0E0F11, 0.95f), 4);
        stroke(r, theme.border, 4);
        text(m_tooltip, r.inset(8, 0), theme.text, 12);
    }
}

// --------------------------------------------------------------------------- drawing

void UI::fill(const Rect& r, const Color& c, float radius) {
    dl()->AddRectFilled(ImVec2(px(r.x), px(r.y)), ImVec2(px(r.r()), px(r.b())), col(c), px(radius));
}

void UI::stroke(const Rect& r, const Color& c, float radius, float w) {
    Rect rr = r.inset(w * 0.5f);
    dl()->AddRect(ImVec2(px(rr.x), px(rr.y)), ImVec2(px(rr.r()), px(rr.b())), col(c), px(radius), 0, px(w));
}

void UI::line(float x0, float y0, float x1, float y1, const Color& c, float w) {
    dl()->AddLine(ImVec2(px(x0), px(y0)), ImVec2(px(x1), px(y1)), col(c), px(w));
}

void UI::circle(float cx, float cy, float r, const Color& c, bool filled, float w) { ellipse(cx, cy, r, r, c, filled, w); }

void UI::ellipse(float cx, float cy, float rx, float ry, const Color& c, bool filled, float w) {
    const int N = 48;
    ImVec2 pts[N];
    for (int i = 0; i < N; ++i) {
        float a = 6.2831853f * i / N;
        pts[i] = ImVec2(px(cx + std::cos(a) * rx), px(cy + std::sin(a) * ry));
    }
    if (filled) dl()->AddConvexPolyFilled(pts, N, col(c));
    else dl()->AddPolyline(pts, N, col(c), ImDrawFlags_Closed, px(w));
}

void UI::polygon(const float2* pts, int n, const Color& c, bool filled, float w) {
    if (n < 2) return;
    std::vector<ImVec2> p(n);
    for (int i = 0; i < n; ++i) p[i] = ImVec2(px(pts[i].x), px(pts[i].y));
    if (filled) dl()->AddConvexPolyFilled(p.data(), n, col(c));
    else dl()->AddPolyline(p.data(), n, col(c), ImDrawFlags_Closed, px(w));
}

void UI::text(const std::wstring& s, const Rect& r, const Color& c, float size, bool bold, Align align) {
    if (s.empty()) return;
    std::string u = utf8(s);
    ImFont* f = bold ? m_bold : m_regular;
    if (!f) f = ImGui::GetFont();
    float fpx = px(size);
    ImVec2 ts = f->CalcTextSizeA(fpx, FLT_MAX, 0, u.c_str());
    float x = px(r.x);
    if (align == Align::Center) x = px(r.cx()) - ts.x * 0.5f;
    else if (align == Align::Right) x = px(r.r()) - ts.x;
    float y = px(r.cy()) - ts.y * 0.5f;
    ImVec4 clip(px(r.x), px(r.y), px(r.r()), px(r.b()));
    dl()->AddText(f, fpx, ImVec2(x, y), col(c), u.c_str(), nullptr, 0.0f, &clip);
}

float UI::textWidth(const std::wstring& s, float size, bool bold) {
    if (s.empty()) return 0;
    std::string u = utf8(s);
    ImFont* f = bold ? m_bold : m_regular;
    if (!f) f = ImGui::GetFont();
    return f->CalcTextSizeA(px(size), FLT_MAX, 0, u.c_str()).x / dpi;
}

void UI::icon(wchar_t glyph, const Rect& r, const Color& c, float size) {
    text(std::wstring(1, glyph), r, c, size, false, Align::Center);
}

void UI::pushClip(const Rect& r) {
    dl()->PushClipRect(ImVec2(px(r.x), px(r.y)), ImVec2(px(r.r()), px(r.b())), true);
    ++m_clipDepth;
}

void UI::popClip() {
    if (m_clipDepth <= 0) return;
    dl()->PopClipRect();
    --m_clipDepth;
}

// --------------------------------------------------------------------------- widgets

bool UI::button(const Rect& r, const std::wstring& label, bool active, wchar_t glyph) {
    bool h = hover(r);
    fill(r, active ? theme.accentDim : (h ? theme.cardHover : theme.card), 4);
    stroke(r, active ? theme.accent.withAlpha(0.7f) : theme.border, 4);
    if (glyph) {
        float tw = textWidth(label, 12.5f);
        float total = tw + 22;
        float x0 = r.cx() - total * 0.5f;
        icon(glyph, Rect(x0, r.y, 16, r.h), active ? theme.accent : theme.text, 13);
        text(label, Rect(x0 + 22, r.y, tw + 2, r.h), theme.text, 12.5f);
    } else {
        text(label, r, active ? theme.accent : theme.text, 12.5f, false, Align::Center);
    }
    return clicked(r);
}

bool UI::iconButton(const Rect& r, wchar_t glyph, bool active, float size, const wchar_t* tip) {
    bool h = hover(r);
    if (active) fill(r, theme.accentDim, 4);
    else if (h) fill(r, theme.cardHover, 4);
    icon(glyph, r, active ? theme.accent.scaled(1.25f) : (h ? theme.text : theme.textDim.scaled(1.1f)), size);
    if (h && tip) tooltip(tip);
    return clicked(r);
}

bool UI::toolButton(const Rect& r, wchar_t glyph, const std::wstring& label, bool active, bool enabled, bool dropArrow, bool boxed) {
    bool h = enabled && hover(r);
    if (active && boxed) {
        fill(r, theme.accent.withAlpha(0.10f), 6);
        stroke(r, theme.accent, 6, 1.5f);
    } else if (h) {
        fill(r, theme.cardHover, 6);
    }
    Color c = !enabled ? theme.textFaint : (active ? theme.accent : theme.text);
    float iw = r.w;
    if (dropArrow) iw -= 14;
    icon(glyph, Rect(r.x, r.y + 6, iw, 26), c, 19);
    text(label, Rect(r.x, r.y + 33, iw, 20), active ? theme.accent : (enabled ? theme.textDim.scaled(1.25f) : theme.textFaint),
         12, false, Align::Center);
    if (dropArrow) icon(0xE70D, Rect(r.r() - 18, r.y + 8, 14, 22), theme.textDim, 9);
    return enabled && clicked(r);
}

bool UI::tab(const Rect& r, const std::wstring& label, bool active) {
    bool h = hover(r);
    if (active) fill(r, theme.panel);
    else if (h) fill(r, theme.cardHover.withAlpha(0.5f));
    text(label, r, active ? theme.text : (h ? theme.text : theme.textDim), 12.5f, active, Align::Center);
    if (active) fill(Rect(r.x, r.b() - 2, r.w, 2), theme.accent);
    return clicked(r);
}

bool UI::slider(const char* id, const Rect& r, const std::wstring& label, float& v, float mn, float mxv,
                const wchar_t* fmt, float labelW, bool logScale) {
    uint32_t h = hashId(id);
    text(label, Rect(r.x, r.y, labelW - 6, r.h), theme.textDim, 12);
    Rect f(r.x + labelW, r.y + 3, r.w - labelW, r.h - 6);
    bool hov = hover(f);
    fill(f, theme.field, 4);
    auto toT = [&](float x) {
        if (logScale) return (std::log(std::max(x, 1e-6f)) - std::log(mn)) / (std::log(mxv) - std::log(mn));
        return (x - mn) / (mxv - mn);
    };
    auto fromT = [&](float t) {
        if (logScale) return std::exp(std::log(mn) + t * (std::log(mxv) - std::log(mn)));
        return mn + t * (mxv - mn);
    };
    float t = std::min(std::max(toT(v), 0.0f), 1.0f);
    if (t > 0.002f) fill(Rect(f.x, f.y, std::max(f.w * t, 6.0f), f.h), theme.accent.withAlpha(m_activeId == h ? 0.42f : 0.28f), 4);
    stroke(f, hov || m_activeId == h ? theme.accent.withAlpha(0.6f) : theme.border, 4);
    wchar_t buf[64];
    swprintf(buf, 64, fmt, v);
    text(buf, f.inset(8, 0), theme.text, 12, false, Align::Right);
    bool changed = false;
    if (clicked(f)) m_activeId = h;
    if (m_activeId == h && mouseDown[0]) {
        float nt = std::min(std::max((mx - f.x) / f.w, 0.0f), 1.0f);
        float nv = fromT(nt);
        if (nv != v) { v = nv; changed = true; }
    }
    if (hov && wheel != 0 && !mouseDown[0]) {
        float nt = std::min(std::max(t + (wheel > 0 ? 0.02f : -0.02f), 0.0f), 1.0f);
        v = fromT(nt);
        changed = true;
    }
    return changed;
}

bool UI::sliderInt(const char* id, const Rect& r, const std::wstring& label, int& v, int mn, int mxv, float labelW) {
    float f = (float)v;
    bool c = slider(id, r, label, f, (float)mn, (float)mxv, L"%.0f", labelW);
    int nv = (int)std::lround(f);
    if (c && nv != v) { v = nv; return true; }
    return false;
}

bool UI::toggle(const Rect& r, const std::wstring& label, bool& v) {
    text(label, Rect(r.x, r.y, r.w - 44, r.h), theme.textDim, 12);
    Rect sw(r.r() - 32, r.cy() - 8, 32, 16);
    bool h = hover(r);
    fill(sw, v ? theme.accent : (h ? theme.cardHover : theme.card), 8);
    if (!v) stroke(sw, theme.border, 8);
    float kx = v ? sw.r() - 8 : sw.x + 8;
    circle(kx, sw.cy(), 5.5f, v ? Color::hex(0xFFFFFF) : theme.textDim);
    if (clicked(r)) { v = !v; return true; }
    return false;
}

bool UI::checkbox(const Rect& r, const std::wstring& label, bool& v) {
    text(label, Rect(r.x, r.y, r.w - 30, r.h), theme.textDim, 12);
    Rect b(r.r() - 18, r.cy() - 8, 16, 16);
    bool h = hover(r);
    if (v) {
        fill(b, theme.accent, 3);
        line(b.x + 3.5f, b.cy(), b.x + 6.5f, b.b() - 4, Color::hex(0xFFFFFF), 1.8f);
        line(b.x + 6.5f, b.b() - 4, b.r() - 3.5f, b.y + 4, Color::hex(0xFFFFFF), 1.8f);
    } else {
        fill(b, theme.field, 3);
        stroke(b, h ? theme.textFaint : theme.border, 3);
    }
    if (clicked(r)) { v = !v; return true; }
    return false;
}

bool UI::dragFloat(const char* id, const Rect& r, float& v, float speed, const wchar_t* fmt, const wchar_t* axis,
                   const Color* axisColor) {
    uint32_t h = hashId(id);
    bool hov = hover(r);
    bool act = m_activeId == h;
    fill(r, theme.field, 4);
    stroke(r, act ? theme.accent : (hov ? theme.textFaint : theme.border), 4);
    float tx = r.x + 6;
    if (axis) {
        Rect tag(r.x + 1, r.y + 1, 16, r.h - 2);
        text(axis, tag, axisColor ? *axisColor : theme.textDim, 11, true, Align::Center);
        line(tag.r(), r.y + 5, tag.r(), r.b() - 5, theme.border);
        tx = tag.r() + 5;
    }
    wchar_t buf[64];
    swprintf(buf, 64, fmt, v);
    text(buf, Rect(tx, r.y, r.r() - tx - 4, r.h), theme.text, 12);
    if (hov) tooltip(L"Glisser horizontalement pour modifier (Maj : précis)");
    bool changed = false;
    if (clicked(r)) m_activeId = h;
    if (act && mouseDown[0]) {
        static float lastX = 0;
        static uint32_t lastId = 0;
        if (lastId != h || mousePressed[0]) { lastId = h; lastX = mx; }
        float d = mx - lastX;
        lastX = mx;
        if (d != 0) {
            v += d * speed * (keyDown(VK_SHIFT) ? 0.1f : 1.0f);
            changed = true;
        }
    }
    return changed;
}

Rect UI::popupRect() const {
    float w = std::max(m_popupAnchor.w, 170.0f);
    float h = m_popupItems.size() * 28.0f + 8.0f;
    Rect r(m_popupAnchor.x, m_popupAnchor.b() + 3, w, h);
    if (r.r() > width - 4) r.x = width - 4 - r.w;
    if (r.b() > height - 4) r.y = m_popupAnchor.y - 3 - h;
    return r;
}

bool UI::dropdown(const char* id, const Rect& r, const std::vector<std::wstring>& items, int& sel, const Color* swatch) {
    uint32_t h = hashId(id);
    bool changed = false;
    if (m_popupPickId == h && m_popupPick >= 0) {
        if (sel != m_popupPick) changed = true;
        sel = m_popupPick;
        m_popupPick = -1;
        m_popupPickId = 0;
    }
    bool hov = hover(r);
    bool open = m_popupId == h;
    fill(r, theme.card, 4);
    stroke(r, open ? theme.accent : (hov ? theme.textFaint : theme.border), 4);
    float tx = r.x + 10;
    if (swatch) {
        Rect s(r.x + 6, r.y + 5, r.h - 10, r.h - 10);
        fill(s, *swatch, 3);
        tx = s.r() + 8;
    }
    if (sel >= 0 && sel < (int)items.size()) text(items[sel], Rect(tx, r.y, r.r() - tx - 24, r.h), theme.text, 12.5f);
    icon(0xE70D, Rect(r.r() - 24, r.y, 20, r.h), theme.textDim, 10);
    if (clicked(r)) {
        if (open) m_popupId = 0;
        else {
            m_popupId = h;
            m_popupAnchor = r;
            m_popupItems = items;
            m_popupSel = sel;
            m_popupJustOpened = true;
        }
        consumeClick();
    }
    (void)items;
    return changed;
}

int UI::menu(const char* id, const Rect& r, const std::wstring& label, const std::vector<std::wstring>& items, bool highlighted) {
    uint32_t h = hashId(id);
    int picked = -1;
    if (m_popupPickId == h && m_popupPick >= 0) {
        picked = m_popupPick;
        m_popupPick = -1;
        m_popupPickId = 0;
    }
    bool open = m_popupId == h;
    bool hov = hover(r);
    if (open || hov) fill(r.inset(0, 7), theme.cardHover, 4);
    (void)highlighted;
    text(label, r, open || hov ? theme.text : theme.text.scaled(0.88f), 12.5f, false, Align::Center);
    if (clicked(r)) {
        if (open) m_popupId = 0;
        else {
            m_popupId = h;
            m_popupAnchor = r;
            m_popupItems = items;
            m_popupSel = -1;
            m_popupJustOpened = true;
        }
        consumeClick();
    }
    // Hover-switching between open menus.
    if (m_popupId != 0 && m_popupId != h && hov && !m_popupItems.empty() && m_popupAnchor.y == r.y) {
        m_popupId = h;
        m_popupAnchor = r;
        m_popupItems = items;
        m_popupSel = -1;
    }
    return picked;
}

bool UI::textField(const char* id, const Rect& r, std::wstring& s, const std::wstring& placeholder, wchar_t glyph) {
    uint32_t h = hashId(id);
    bool focused = m_focusId == h;
    bool hov = hover(r);
    fill(r, theme.field, 5);
    stroke(r, focused ? theme.accent : (hov ? theme.textFaint : theme.border), 5);
    float tx = r.x + 10;
    if (glyph) {
        icon(glyph, Rect(r.x + 6, r.y, 22, r.h), theme.textDim, 13);
        tx = r.x + 32;
    }
    bool changed = false;
    if (focused) {
        // Control keys come from the key state, typed characters from the input queue.
        if (keyPressed(VK_BACK)) { if (!s.empty()) { s.pop_back(); changed = true; } }
        if (keyPressed(VK_RETURN) || keyPressed(VK_ESCAPE)) m_focusId = 0;
        for (wchar_t c : m_chars) {
            if (c >= 32 && c != 127) { s.push_back(c); changed = true; }
        }
        m_chars.clear();
    }
    if (s.empty() && !focused) text(placeholder, Rect(tx, r.y, r.r() - tx - 6, r.h), theme.textFaint, 12.5f);
    else text(s, Rect(tx, r.y, r.r() - tx - 6, r.h), theme.text, 12.5f);
    if (focused && (GetTickCount() / 500) % 2 == 0) {
        float cx = tx + textWidth(s, 12.5f) + 1;
        line(cx, r.y + 7, cx, r.b() - 7, theme.accent, 1.2f);
    }
    if (mousePressed[0]) {
        if (hov) m_focusId = h;
        else if (focused) m_focusId = 0;
    }
    return changed;
}

void UI::tooltip(const std::wstring& s) { m_tooltip = s; }

void UI::drawPopup() {
    if (!m_popupId) return;
    bool savedBlocked = m_blocked;
    m_blocked = false;
    Rect pr = popupRect();
    fill(Rect(pr.x + 2, pr.y + 4, pr.w, pr.h), Color::hex(0x000000, 0.4f), 6);
    fill(pr, Color::hex(0x1A1D22), 6);
    stroke(pr, theme.border, 6);
    for (size_t i = 0; i < m_popupItems.size(); ++i) {
        Rect ir(pr.x + 4, pr.y + 4 + i * 28.0f, pr.w - 8, 28);
        bool h = ir.contains(mx, my);
        if (h) fill(ir, theme.accentDim, 4);
        bool sel = (int)i == m_popupSel;
        if (sel) icon(0xE73E, Rect(ir.r() - 24, ir.y, 20, ir.h), theme.accent, 11);
        text(m_popupItems[i], Rect(ir.x + 10, ir.y, ir.w - 36, ir.h), sel ? theme.accent : theme.text, 12.5f);
        if (h && mousePressed[0] && !m_popupJustOpened) {
            m_popupPickId = m_popupId;
            m_popupPick = (int)i;
            m_popupId = 0;
            consumeClick();
            break;
        }
    }
    m_popupJustOpened = false;
    m_blocked = savedBlocked;
}

} // namespace vp3d
