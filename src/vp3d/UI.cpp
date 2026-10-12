#include "UI.h"
#include <cwchar>

namespace vp3d {

static D2D1_RECT_F toD2D(const Rect& r) { return D2D1::RectF(r.x, r.y, r.x + r.w, r.y + r.h); }
static D2D1_COLOR_F toD2D(const Color& c) { return D2D1::ColorF(c.r, c.g, c.b, c.a); }

bool UI::init(ID3D12Device* device, ID3D12CommandQueue* queue) {
    IUnknown* queues[] = {queue};
    if (FAILED(D3D11On12CreateDevice(device, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, queues, 1, 0, &m_d11,
                                     &m_d11ctx, nullptr)))
        return false;
    m_d11.As(&m_on12);
    D2D1_FACTORY_OPTIONS opts = {};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory3), &opts, &m_factory)))
        return false;
    ComPtr<IDXGIDevice> dxgi;
    m_on12.As(&dxgi);
    if (FAILED(m_factory->CreateDevice(dxgi.Get(), &m_device))) return false;
    m_device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &m_ctx);
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)m_dw.GetAddressOf());
    m_ctx->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &m_brush);
    m_ctx->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

    ComPtr<IDWriteFontCollection> fc;
    m_dw->GetSystemFontCollection(&fc);
    UINT32 idx = 0;
    BOOL exists = FALSE;
    fc->FindFamilyName(L"Segoe Fluent Icons", &idx, &exists);
    if (!exists) m_iconFont = L"Segoe MDL2 Assets";
    return true;
}

void UI::createTargets(IDXGISwapChain3* sc, uint32_t count) {
    m_wrapped.resize(count);
    m_targets.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        ComPtr<ID3D12Resource> bb;
        sc->GetBuffer(i, IID_PPV_ARGS(&bb));
        D3D11_RESOURCE_FLAGS rf = {D3D11_BIND_RENDER_TARGET};
        m_on12->CreateWrappedResource(bb.Get(), &rf, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT,
                                      IID_PPV_ARGS(&m_wrapped[i]));
        ComPtr<IDXGISurface> surf;
        m_wrapped[i].As(&surf);
        D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
        m_ctx->CreateBitmapFromDxgiSurface(surf.Get(), &bp, &m_targets[i]);
    }
}

void UI::releaseTargets() {
    if (m_ctx) m_ctx->SetTarget(nullptr);
    m_targets.clear();
    m_wrapped.clear();
    if (m_d11ctx) m_d11ctx->Flush();
}

void UI::shutdown() {
    releaseTargets();
    m_formats.clear();
}

// --------------------------------------------------------------------------- input

void UI::onMouseMove(float x, float y) { m_rawX = x; m_rawY = y; }

void UI::onMouseButton(int b, bool down) {
    if (b < 0 || b > 2) return;
    m_rawDown[b] = down;
    if (down) {
        m_rawPressed[b] = true;
        if (b == 0) {
            uint32_t now = GetTickCount();
            if (now - m_lastClickTime < GetDoubleClickTime()) m_rawDouble = true;
            m_lastClickTime = now;
        }
    } else {
        m_rawReleased[b] = true;
    }
}

void UI::onWheel(float d) { m_rawWheel += d; }
void UI::onChar(wchar_t c) { m_rawChars.push_back(c); }
void UI::onKey(int vk, bool down) {
    if (vk < 0 || vk > 255) return;
    if (down && !m_keys[vk]) m_keysPressedRaw[vk] = true;
    if (down) m_keysPressedRaw[vk] = true;  // allow key repeat (backspace, arrows)
    m_keys[vk] = down;
}
bool UI::keyPressed(int vk) const { return vk >= 0 && vk < 256 && m_keysPressed[vk]; }
bool UI::keyDown(int vk) const { return vk >= 0 && vk < 256 && m_keys[vk]; }

uint32_t UI::hashId(const char* s) {
    uint32_t h = 2166136261u;
    while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
    return h ? h : 1;
}

// --------------------------------------------------------------------------- frame

void UI::newFrame(float dpiScale, float w, float h) {
    dpi = dpiScale;
    width = w;
    height = h;
    mx = m_rawX / dpi;
    my = m_rawY / dpi;
    for (int i = 0; i < 3; ++i) {
        mouseDown[i] = m_rawDown[i];
        mousePressed[i] = m_rawPressed[i];
        mouseReleased[i] = m_rawReleased[i];
        m_rawPressed[i] = m_rawReleased[i] = false;
    }
    mouseDoubleClick = m_rawDouble;
    m_rawDouble = false;
    wheel = m_rawWheel;
    m_rawWheel = 0;
    m_chars.swap(m_rawChars);
    m_rawChars.clear();
    memcpy(m_keysPressed, m_keysPressedRaw, sizeof(m_keysPressed));
    memset(m_keysPressedRaw, 0, sizeof(m_keysPressedRaw));

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

void UI::beginDraw(uint32_t bb) {
    m_current = bb;
    if (bb < m_wrapped.size()) {
        ID3D11Resource* res[] = {m_wrapped[bb].Get()};
        m_on12->AcquireWrappedResources(res, 1);
        m_ctx->SetTarget(m_targets[bb].Get());
    }
    m_ctx->SetDpi(96.0f * dpi, 96.0f * dpi);
    m_ctx->BeginDraw();
    m_ctx->SetTransform(D2D1::Matrix3x2F::Identity());
}

void UI::end() {
    drawPopup();
    if (!m_tooltip.empty()) {
        float tw = textWidth(m_tooltip, 12) + 16;
        Rect r(mx + 14, my + 18, tw, 24);
        if (r.r() > width) r.x = width - r.w - 4;
        fill(r, Color::hex(0x0E0F11, 0.95f), 4);
        stroke(r, theme.border, 4);
        text(m_tooltip, r.inset(8, 0), theme.text, 12);
    }
    m_ctx->EndDraw();
    m_ctx->SetTarget(nullptr);
    if (m_current < m_wrapped.size()) {
        ID3D11Resource* res[] = {m_wrapped[m_current].Get()};
        m_on12->ReleaseWrappedResources(res, 1);
    }
    m_d11ctx->Flush();
    m_chars.clear();
}

// --------------------------------------------------------------------------- drawing

ID2D1SolidColorBrush* UI::brush(const Color& c) {
    m_brush->SetColor(toD2D(c));
    return m_brush.Get();
}

IDWriteTextFormat* UI::format(float size, bool bold, bool icon) {
    int key = (int)(size * 4.0f) * 4 + (bold ? 2 : 0) + (icon ? 1 : 0);
    auto it = m_formats.find(key);
    if (it != m_formats.end()) return it->second.Get();
    ComPtr<IDWriteTextFormat> f;
    const wchar_t* family = icon ? m_iconFont.c_str() : L"Segoe UI";
    m_dw->CreateTextFormat(family, nullptr, bold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                           DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"fr-fr", &f);
    f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    if (!icon) {
        ComPtr<IDWriteInlineObject> sign;
        m_dw->CreateEllipsisTrimmingSign(f.Get(), &sign);
        DWRITE_TRIMMING trim = {DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        f->SetTrimming(&trim, sign.Get());
    }
    m_formats[key] = f;
    return f.Get();
}

void UI::fill(const Rect& r, const Color& c, float radius) {
    if (radius > 0) m_ctx->FillRoundedRectangle(D2D1::RoundedRect(toD2D(r), radius, radius), brush(c));
    else m_ctx->FillRectangle(toD2D(r), brush(c));
}

void UI::stroke(const Rect& r, const Color& c, float radius, float w) {
    Rect rr = r.inset(w * 0.5f);
    if (radius > 0) m_ctx->DrawRoundedRectangle(D2D1::RoundedRect(toD2D(rr), radius, radius), brush(c), w);
    else m_ctx->DrawRectangle(toD2D(rr), brush(c), w);
}

void UI::line(float x0, float y0, float x1, float y1, const Color& c, float w) {
    m_ctx->DrawLine(D2D1::Point2F(x0, y0), D2D1::Point2F(x1, y1), brush(c), w);
}

void UI::circle(float cx, float cy, float r, const Color& c, bool filled, float w) { ellipse(cx, cy, r, r, c, filled, w); }

void UI::ellipse(float cx, float cy, float rx, float ry, const Color& c, bool filled, float w) {
    D2D1_ELLIPSE e = D2D1::Ellipse(D2D1::Point2F(cx, cy), rx, ry);
    if (filled) m_ctx->FillEllipse(e, brush(c));
    else m_ctx->DrawEllipse(e, brush(c), w);
}

void UI::polygon(const float2* pts, int n, const Color& c, bool filled, float w) {
    if (n < 2) return;
    ComPtr<ID2D1PathGeometry> geo;
    m_factory->CreatePathGeometry(&geo);
    ComPtr<ID2D1GeometrySink> sink;
    geo->Open(&sink);
    sink->BeginFigure(D2D1::Point2F(pts[0].x, pts[0].y), filled ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
    for (int i = 1; i < n; ++i) sink->AddLine(D2D1::Point2F(pts[i].x, pts[i].y));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    if (filled) m_ctx->FillGeometry(geo.Get(), brush(c));
    else m_ctx->DrawGeometry(geo.Get(), brush(c), w);
}

void UI::text(const std::wstring& s, const Rect& r, const Color& c, float size, bool bold, Align align) {
    if (s.empty()) return;
    IDWriteTextFormat* f = format(size, bold);
    f->SetTextAlignment(align == Align::Left ? DWRITE_TEXT_ALIGNMENT_LEADING
                        : align == Align::Center ? DWRITE_TEXT_ALIGNMENT_CENTER
                                                 : DWRITE_TEXT_ALIGNMENT_TRAILING);
    m_ctx->DrawText(s.c_str(), (UINT32)s.size(), f, toD2D(r), brush(c), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

float UI::textWidth(const std::wstring& s, float size, bool bold) {
    ComPtr<IDWriteTextLayout> l;
    m_dw->CreateTextLayout(s.c_str(), (UINT32)s.size(), format(size, bold), 10000, 100, &l);
    DWRITE_TEXT_METRICS m = {};
    if (l) l->GetMetrics(&m);
    return m.widthIncludingTrailingWhitespace;
}

void UI::icon(wchar_t glyph, const Rect& r, const Color& c, float size) {
    IDWriteTextFormat* f = format(size, false, true);
    f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_ctx->DrawText(&glyph, 1, f, toD2D(r), brush(c));
}

void UI::pushClip(const Rect& r) {
    m_ctx->PushAxisAlignedClip(toD2D(r), D2D1_ANTIALIAS_MODE_ALIASED);
    m_clips.push_back(toD2D(r));
}

void UI::popClip() {
    if (m_clips.empty()) return;
    m_ctx->PopAxisAlignedClip();
    m_clips.pop_back();
}

ComPtr<ID2D1Bitmap1> UI::createBitmap(const uint8_t* rgba, uint32_t w, uint32_t h) {
    ComPtr<ID2D1Bitmap1> b;
    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_R8G8B8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    m_ctx->CreateBitmap(D2D1::SizeU(w, h), rgba, w * 4, &bp, &b);
    return b;
}

void UI::image(ID2D1Bitmap1* bmp, const Rect& r, float opacity) {
    if (bmp) m_ctx->DrawBitmap(bmp, toD2D(r), opacity, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
}

// --------------------------------------------------------------------------- widgets

bool UI::button(const Rect& r, const std::wstring& label, bool active, wchar_t glyph) {
    bool h = hover(r);
    fill(r, active ? theme.accentDim : (h ? theme.cardHover : theme.card), 4);
    stroke(r, active ? theme.accent.withAlpha(0.7f) : theme.border, 4);
    Rect tr = r;
    if (glyph) {
        float tw = textWidth(label, 12.5f);
        float total = tw + 22;
        float x0 = r.cx() - total * 0.5f;
        icon(glyph, Rect(x0, r.y, 16, r.h), active ? theme.accent : theme.text, 13);
        text(label, Rect(x0 + 22, r.y, tw + 2, r.h), theme.text, 12.5f);
    } else {
        text(label, tr, active ? theme.accent : theme.text, 12.5f, false, Align::Center);
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
    float t = clampf(toT(v), 0, 1);
    if (t > 0.002f) fill(Rect(f.x, f.y, std::max(f.w * t, 6.0f), f.h), theme.accent.withAlpha(m_activeId == h ? 0.42f : 0.28f), 4);
    stroke(f, hov || m_activeId == h ? theme.accent.withAlpha(0.6f) : theme.border, 4);
    wchar_t buf[64];
    swprintf(buf, 64, fmt, v);
    text(buf, f.inset(8, 0), theme.text, 12, false, Align::Right);
    bool changed = false;
    if (clicked(f)) m_activeId = h;
    if (m_activeId == h && mouseDown[0]) {
        float nt = clampf((mx - f.x) / f.w, 0, 1);
        float nv = fromT(nt);
        if (nv != v) { v = nv; changed = true; }
    }
    if (hov && wheel != 0 && !mouseDown[0]) {
        float nt = clampf(t + (wheel > 0 ? 0.02f : -0.02f), 0, 1);
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
            m_popupSwatches.clear();
            m_popupSel = sel;
            m_popupJustOpened = true;
        }
        consumeClick();
    }
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
        for (wchar_t c : m_chars) {
            if (c == 8) { if (!s.empty()) { s.pop_back(); changed = true; } }
            else if (c == 13 || c == 27) { m_focusId = 0; }
            else if (c >= 32) { s.push_back(c); changed = true; }
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
