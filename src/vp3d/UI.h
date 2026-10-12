// vp3d - immediate-mode UI drawn with Direct2D/DirectWrite over the D3D12
// swapchain (through D3D11On12). No third-party dependency.
#pragma once
#include "Math.h"
#include <d2d1_3.h>
#include <d3d11on12.h>
#include <d3d12.h>
#include <dwrite_3.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <map>
#include <string>
#include <vector>

namespace vp3d {

using Microsoft::WRL::ComPtr;

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
    Color bg = Color::hex(0x0E1013);          // window chrome (top bar, gaps)
    Color panel = Color::hex(0x15181C);       // panel body
    Color panel2 = Color::hex(0x1B1F24);      // panel headers, section bars
    Color card = Color::hex(0x20242A);
    Color cardHover = Color::hex(0x2A2F36);
    Color field = Color::hex(0x101216);
    Color border = Color::hex(0x262A31);
    Color text = Color::hex(0xE4E6EA);
    Color textDim = Color::hex(0x979DA6);
    Color textFaint = Color::hex(0x5F6670);
    Color accent = Color::hex(0x4C8DFF);
    Color accentDim = Color::hex(0x1E3A66);   // selected rows
};

class UI {
public:
    Theme theme;

    bool init(ID3D12Device* device, ID3D12CommandQueue* queue);
    void createTargets(IDXGISwapChain3* sc, uint32_t count);
    void releaseTargets();
    void shutdown();

    void newFrame(float dpiScale, float width, float height);  // latch input for this frame
    void beginDraw(uint32_t backBuffer);                       // after the 3D frame was submitted
    void end();   // finishes drawing, transitions the back buffer to PRESENT

    // Window input (pixel coordinates).
    void onMouseMove(float x, float y);
    void onMouseButton(int button, bool down);
    void onWheel(float delta);
    void onChar(wchar_t c);
    void onKey(int vk, bool down);

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
    ComPtr<ID2D1Bitmap1> createBitmap(const uint8_t* rgba, uint32_t w, uint32_t h);
    void image(ID2D1Bitmap1* bmp, const Rect& r, float opacity = 1.0f);

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
    // Checkbox at the right end of the row (inspector style).
    bool checkbox(const Rect& r, const std::wstring& label, bool& v);
    // Numeric box edited by dragging horizontally; axisColor tints the small axis tag on its left (X / Y / Z).
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

    ID2D1DeviceContext2* context() const { return m_ctx.Get(); }

private:
    IDWriteTextFormat* format(float size, bool bold, bool icon = false);
    ID2D1SolidColorBrush* brush(const Color& c);
    static uint32_t hashId(const char* s);
    void drawPopup();

    ComPtr<ID3D11Device> m_d11;
    ComPtr<ID3D11DeviceContext> m_d11ctx;
    ComPtr<ID3D11On12Device> m_on12;
    ComPtr<ID2D1Factory3> m_factory;
    ComPtr<ID2D1Device2> m_device;
    ComPtr<ID2D1DeviceContext2> m_ctx;
    ComPtr<IDWriteFactory> m_dw;
    ComPtr<ID2D1SolidColorBrush> m_brush;
    std::vector<ComPtr<ID3D11Resource>> m_wrapped;
    std::vector<ComPtr<ID2D1Bitmap1>> m_targets;
    std::map<int, ComPtr<IDWriteTextFormat>> m_formats;
    std::wstring m_iconFont = L"Segoe Fluent Icons";
    uint32_t m_current = 0;

    // input accumulation between frames
    float m_rawX = 0, m_rawY = 0;
    bool m_rawDown[3] = {};
    bool m_rawPressed[3] = {};
    bool m_rawReleased[3] = {};
    float m_rawWheel = 0;
    std::wstring m_rawChars;
    bool m_keys[256] = {};
    bool m_keysPressedRaw[256] = {};
    bool m_keysPressed[256] = {};
    uint32_t m_lastClickTime = 0;
    bool m_rawDouble = false;

    bool m_blocked = false;
    uint32_t m_activeId = 0;
    uint32_t m_focusId = 0;
    std::wstring m_chars;

    // popup (dropdown / menu)
    uint32_t m_popupId = 0;
    Rect m_popupAnchor;
    std::vector<std::wstring> m_popupItems;
    std::vector<Color> m_popupSwatches;
    int m_popupSel = -1;
    uint32_t m_popupPickId = 0;
    int m_popupPick = -1;
    bool m_popupJustOpened = false;
    Rect popupRect() const;

    std::wstring m_tooltip;
    std::vector<D2D1_RECT_F> m_clips;
};

} // namespace vp3d
