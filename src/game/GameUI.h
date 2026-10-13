// Immediate-mode widgets for the game layer (node graph editor, toolbar, toasts), drawn with
// Dear ImGui's foreground draw list. Same API as the former vp3d UI, so the graph code is
// unchanged: rectangles are in DIPs (the host's dpi scale is applied when drawing) and the
// widgets are plain functions that return true on the frame they are used.
#pragma once
#include "vp3d/Math.h"
#include <algorithm>
#include "imgui.h"
#include <cstdint>
#include <string>
#include <vector>

struct ImFont;

namespace vp3d {

struct Color {
    float r = 0, g = 0, b = 0, a = 1;
    static Color hex(uint32_t rgb, float a = 1.0f) {
        return {((rgb >> 16) & 255) / 255.0f, ((rgb >> 8) & 255) / 255.0f, (rgb & 255) / 255.0f, a};
    }
    static Color rgbf(float r, float g, float b, float a = 1.0f) { return {r, g, b, a}; }
    Color withAlpha(float na) const { return {r, g, b, na}; }
    Color scaled(float k) const { return {std::min(r * k, 1.0f), std::min(g * k, 1.0f), std::min(b * k, 1.0f), a}; }
};

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    Rect() = default;
    Rect(float x_, float y_, float w_, float h_) : x(x_), y(y_), w(w_), h(h_) {}
    float r() const { return x + w; }
    float b() const { return y + h; }
    float cx() const { return x + w * 0.5f; }
    float cy() const { return y + h * 0.5f; }
    bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    Rect inset(float d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
    Rect inset(float dx, float dy) const { return {x + dx, y + dy, w - 2 * dx, h - 2 * dy}; }
};

enum class Align { Left, Center, Right };

// Theme colours used by the widgets (neutral dark UI with a blue accent).
struct Theme {
    Color bg = Color::hex(0x0E1013);
    Color panel = Color::hex(0x15181C);
    Color panel2 = Color::hex(0x1B1F24);
    Color card = Color::hex(0x20242A);
    Color cardHover = Color::hex(0x2A2F36);
    Color field = Color::hex(0x101216);
    Color border = Color::hex(0x262A31);
    Color text = Color::hex(0xE4E6EA);
    Color textDim = Color::hex(0x979DA6);
    Color textFaint = Color::hex(0x5F6670);
    Color accent = Color::hex(0x4C8DFF);
    Color accentDim = Color::hex(0x1E3A66);
};

class UI {
public:
    Theme theme;

    // Fonts used for text (any size: ImGui scales the glyphs). Both carry the icon glyphs.
    void setFonts(ImFont* regular, ImFont* bold) { m_regular = regular; m_bold = bold; }

    // Reads the input for this frame. Call after ImGui::NewFrame, before drawing.
    void newFrame(float dpiScale, float width, float height);
    // Draws the pending popup and tooltip. Call once the widgets of the frame were issued.
    void end();

    // Per-frame input state (DIPs).
    float mx = 0, my = 0;
    bool mouseDown[3] = {};
    bool mousePressed[3] = {};
    bool mouseReleased[3] = {};
    bool mouseDoubleClick = false;
    float wheel = 0;
    float dpi = 1.0f;
    float width = 0, height = 0;
    bool keyPressed(int vk) const;
    bool keyDown(int vk) const;
    bool blocked() const { return m_blocked; }
    bool popupOpen() const { return m_popupId != 0; }
    bool textFocused() const { return m_focusId != 0; }

    bool hover(const Rect& r) const { return !m_blocked && r.contains(mx, my); }
    bool clicked(const Rect& r, int button = 0) const { return hover(r) && mousePressed[button]; }
    void consumeClick() { mousePressed[0] = mousePressed[1] = mousePressed[2] = false; }

    // Drawing primitives
    void fill(const Rect& r, const Color& c, float radius = 0);
    void stroke(const Rect& r, const Color& c, float radius = 0, float width = 1);
    void line(float x0, float y0, float x1, float y1, const Color& c, float width = 1);
    void circle(float cx, float cy, float radius, const Color& c, bool filled = true, float width = 1);
    void ellipse(float cx, float cy, float rx, float ry, const Color& c, bool filled = true, float width = 1);
    void polygon(const float2* pts, int n, const Color& c, bool filled = true, float width = 1);
    void text(const std::wstring& s, const Rect& r, const Color& c, float size = 13, bool bold = false,
              Align align = Align::Left);
    float textWidth(const std::wstring& s, float size = 13, bool bold = false);
    void icon(wchar_t glyph, const Rect& r, const Color& c, float size = 16);
    void pushClip(const Rect& r);
    void popClip();

    // Widgets
    bool button(const Rect& r, const std::wstring& label, bool active = false, wchar_t glyph = 0);
    bool iconButton(const Rect& r, wchar_t glyph, bool active = false, float size = 16, const wchar_t* tooltip = nullptr);
    bool toolButton(const Rect& r, wchar_t glyph, const std::wstring& label, bool active, bool enabled = true,
                    bool dropArrow = false, bool boxed = true);
    bool tab(const Rect& r, const std::wstring& label, bool active);
    bool slider(const char* id, const Rect& r, const std::wstring& label, float& v, float mn, float mx,
                const wchar_t* fmt = L"%.2f", float labelW = 92, bool logScale = false);
    bool sliderInt(const char* id, const Rect& r, const std::wstring& label, int& v, int mn, int mx, float labelW = 92);
    bool toggle(const Rect& r, const std::wstring& label, bool& v);
    bool checkbox(const Rect& r, const std::wstring& label, bool& v);
    bool dragFloat(const char* id, const Rect& r, float& v, float speed, const wchar_t* fmt, const wchar_t* axis = nullptr,
                   const Color* axisColor = nullptr);
    bool isActive(const char* id) const { return m_activeId == hashId(id); }
    bool dropdown(const char* id, const Rect& r, const std::vector<std::wstring>& items, int& sel,
                  const Color* swatch = nullptr);
    // Menu: returns picked item index or -1.
    int menu(const char* id, const Rect& r, const std::wstring& label, const std::vector<std::wstring>& items,
             bool highlighted = false);
    bool textField(const char* id, const Rect& r, std::wstring& s, const std::wstring& placeholder, wchar_t glyph = 0);
    void tooltip(const std::wstring& s);

private:
    static uint32_t hashId(const char* s);
    void drawPopup();
    Rect popupRect() const;
    ImDrawList* dl() const { return ImGui::GetForegroundDrawList(); }
    float px(float v) const { return v * dpi; }

    ImFont* m_regular = nullptr;
    ImFont* m_bold = nullptr;
    int m_clipDepth = 0;

    bool m_blocked = false;
    uint32_t m_activeId = 0;
    uint32_t m_focusId = 0;
    std::wstring m_chars;

    // popup (dropdown / menu)
    uint32_t m_popupId = 0;
    Rect m_popupAnchor;
    std::vector<std::wstring> m_popupItems;
    int m_popupSel = -1;
    uint32_t m_popupPickId = 0;
    int m_popupPick = -1;
    bool m_popupJustOpened = false;

    std::wstring m_tooltip;
};

} // namespace vp3d
