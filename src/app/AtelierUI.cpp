// "Atelier" layout, Scene workspace: the engine scene editor drawn in the visual language of the Atelier
// Graph Editor (atelier/archive/atelier-graph/former Atelier graph.cpp): same header, panel titles, buttons, metrics (1600 x 900 reference)
// and the shared Atelier palette (atelier/atelier_style.h), fed by the theme settings.
#include "App.h"
#include "GameHost.h"
#include "CurveEditor.h"
#include "AtelierIcons.h"
#include "Icons.h"
#include "atelier/atelier_style.h"
#include "atelier/ambre_fx.h"
#include "imgui_internal.h"   // TempInputIsActive (Corail sliders)
#include "atelier/material_ui.h"
#include <cctype>

namespace app {

using atl::Icon;
namespace afx = atelier::fx;

namespace {

struct R {
    float x, y, w, h;
    ImVec2 min() const { return ImVec2(x, y); }
    ImVec2 max() const { return ImVec2(x + w, y + h); }
};

float s = 1.0f;   // Graph Editor scale: window / 1600 x 900
ImDrawList* dl = nullptr;
ImFont* font = nullptr;

const atelier::Palette& P() { return atelier::Style(); }
ImU32 Fade(ImU32 c, int a) { return (c & 0x00FFFFFFu) | ((ImU32)a << 24); }
// family colours of the graph nodes, darker on light backgrounds
ImU32 Family(ImU32 c) {
    if (!P().light) return c;
    auto d = [&](int sh) { return (ImU32)(((c >> sh) & 255) * 0.72f) << sh; };
    return d(0) | d(8) | d(16) | (c & 0xFF000000u);
}
const ImU32 kGreen = IM_COL32(120, 185, 132, 255), kAmber = IM_COL32(219, 166, 100, 255);

std::string lower(std::string t) { for (auto& c : t) c = (char)std::tolower((unsigned char)c); return t; }
bool matches(const std::string& t, const char* q) { return !*q || lower(t).find(lower(q)) != std::string::npos; }

// --- helpers of archive/atelier-graph/former Atelier graph.cpp (sizes in reference pixels, text scaled by the user text size)
float ts() { return P().textScale; }
void Text(float x, float y, const std::string& text, ImU32 color, float size = 12) { dl->AddText(font, size * s * ts(), ImVec2(x, y), color, text.c_str()); }
ImVec2 TextSize(const std::string& text, float size = 12) { return font->CalcTextSizeA(size * s * ts(), 10000, 0, text.c_str()); }
std::string Fit(const std::string& text, float width, float size) {
    if (TextSize(text, size).x <= width) return text;
    std::string t = text;
    while (!t.empty() && TextSize(t + "…", size).x > width) {
        t.pop_back();
        while (!t.empty() && (t.back() & 0xC0) == 0x80) t.pop_back();
        if (!t.empty() && (unsigned char)t.back() >= 0xC0) t.pop_back();
    }
    return t + "…";
}
void Center(R r, const std::string& text, ImU32 color, float size = 12) {
    ImVec2 z = TextSize(text, size);
    Text(r.x + (r.w - z.x) / 2, r.y + (r.h - z.y) / 2, text, color, size);
}
void Box(R r, ImU32 color, float rounding = 3, bool border = false) {
    dl->AddRectFilled(r.min(), r.max(), color, rounding * s);
    if (border) dl->AddRect(r.min(), r.max(), P().border, rounding * s);
}
// glyph of a view layout: its cols x rows panes in a small rectangle
void LayoutGlyph(ImDrawList* d, int cols, int rows, ImVec2 p, ImVec2 size, ImU32 col) {
    d->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), col, 1.5f * s, 0, 1.2f * s);
    for (int c = 1; c < cols; ++c) {
        const float x = std::floor(p.x + size.x * c / cols) + 0.5f;
        d->AddLine(ImVec2(x, p.y), ImVec2(x, p.y + size.y), col, 1.0f * s);
    }
    for (int r = 1; r < rows; ++r) {
        const float y = std::floor(p.y + size.y * r / rows) + 0.5f;
        d->AddLine(ImVec2(p.x, y), ImVec2(p.x + size.x, y), col, 1.0f * s);
    }
}
// icon size setting: scaled around the same centre
void Glyph(Icon icon, float x, float y, float size, ImU32 color) {
    float k = P().iconScale, o = size * s * (k - 1) / 2;
    atl::DrawIcon(dl, icon, ImVec2(x - o, y - o), size * s * k, color);
}
// Ambre: icon with the soft halo of the pack (amber-glow filter): faint copies around it, then the icon
void GlowGlyph(Icon icon, float x, float y, float size, ImU32 color, ImU32 glow, float alpha) {
    for (const ImVec2& o : afx::kGlowOffsets) Glyph(icon, x + o.x * 1.3f * s, y + o.y * 1.3f * s, size, afx::Alpha(glow, alpha));
    Glyph(icon, x, y, size, color);
}
// text in the semibold font when the theme provides one
void TextBold(float x, float y, const std::string& text, ImU32 color, float size = 12) {
    dl->AddText(P().fontBold ? P().fontBold : font, size * s * ts(), ImVec2(x, y), color, text.c_str());
}
float g_labelRight = 0;   // Ambre: right end of the line drawn after a section label
void Tip(const char* t) { if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", t); }
bool Hit(const char* id, R r, bool enabled = true) {
    ImGui::SetCursorScreenPos(r.min());
    ImGui::BeginDisabled(!enabled);
    bool q = ImGui::InvisibleButton(id, ImVec2(std::max(1.0f, r.w), std::max(1.0f, r.h)));
    ImGui::EndDisabled();
    return q && enabled;
}
// former Atelier graph Button: raised box with border, icon at 8, label at 28; iconOnly centres the icon
bool Button(const char* id, R r, const char* label, Icon icon, bool active = false, bool iconOnly = false, bool enabled = true) {
    bool q = Hit(id, r, enabled);
    bool hover = enabled && ImGui::IsItemHovered();
    const float ix = r.x + (iconOnly ? (r.w / s - 13) / 2 : 8) * s, iy = r.y + (r.h - 13 * s) / 2;
    if (P().skin) {
        // gradient face, light top edge; active: amber border, inner ring and halo, glowing icon
        afx::Face f = !enabled ? afx::Face::Disabled : active ? afx::Face::Active : hover ? afx::Face::Hover : afx::Face::Normal;
        ImU32 c = afx::Button(dl, r.min(), r.max(), f, s, 4);
        if (P().skin >= 2) Glyph(icon, ix, iy, 13, c);   // Corail, Halo: no glow
        else if (active) GlowGlyph(icon, ix, iy, 13, c, afx::col::accent, 0.08f);
        else GlowGlyph(icon, ix, iy, 13, c, c, enabled ? 0.025f : 0.0f);
        if (!iconOnly && label && *label) {
            dl->PushClipRect(r.min(), ImVec2(r.x + r.w - 4 * s, r.y + r.h), true);
            Text(r.x + 28 * s, r.y + (r.h - 12 * s * ts()) / 2, label, active && P().skin == 1 ? afx::col::accentHi : c);
            dl->PopClipRect();
        }
        return q;
    }
    Box(r, active ? P().select : (hover ? P().hover : P().raised), 3, true);
    ImU32 c = !enabled ? Fade(P().muted, 120) : (active ? P().accent : P().text);
    Glyph(icon, ix, iy, 13, c);
    if (!iconOnly && label && *label) {
        dl->PushClipRect(r.min(), ImVec2(r.x + r.w - 4 * s, r.y + r.h), true);
        Text(r.x + 28 * s, r.y + (r.h - 12 * s * ts()) / 2, label, enabled ? P().text : Fade(P().muted, 160));
        dl->PopClipRect();
    }
    return q;
}
// main action of a view (Build): solid amber button with dark text in the Ambre theme, a normal button otherwise
bool PrimaryButton(const char* id, R r, const char* label, Icon icon, bool enabled = true) {
    if (!P().skin || !enabled) return Button(id, r, label, icon, false, false, enabled);
    bool q = Hit(id, r, enabled);
    bool hover = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    ImU32 c = afx::Button(dl, r.min(), r.max(), afx::Face::Primary, s, 4);
    if (hover) dl->AddRectFilled(r.min(), r.max(), held ? IM_COL32(60, 30, 0, 40) : IM_COL32(255, 255, 255, 28), 4 * s);
    Glyph(icon, r.x + 9 * s, r.y + (r.h - 13 * s) / 2, 13, c);
    TextBold(r.x + 28 * s, r.y + (r.h - 12 * s * ts()) / 2, label, c);
    return q;
}
// tab of a tab row: underlined amber tab in the Ambre theme (tab-assembly.svg), a button otherwise
bool TabButton(const char* id, R r, const char* label, Icon icon, bool active) {
    if (!P().skin) return Button(id, r, label, icon, active);
    bool q = Hit(id, r);
    bool hover = ImGui::IsItemHovered();
    ImU32 c = active ? P().accent : (hover ? P().text : P().muted);
    float tw = TextSize(label, 12).x, total = 13 * s * P().iconScale + 6 * s + tw;
    float x = r.x + std::max(4 * s, (r.w - total) / 2);
    dl->PushClipRect(r.min(), r.max(), true);
    if (active && P().skin == 1) GlowGlyph(icon, x, r.y + (r.h - 13 * s) / 2 - 1 * s, 13, c, afx::col::accent, 0.07f);
    else Glyph(icon, x, r.y + (r.h - 13 * s) / 2 - 1 * s, 13, c);
    float tx = x + 13 * s * P().iconScale + 6 * s, ty = r.y + (r.h - 12 * s * ts()) / 2 - 1 * s;
    if (active) TextBold(tx, ty, label, c);
    else Text(tx, ty, label, c);
    dl->PopClipRect();
    afx::TabLine(dl, r.min(), r.max(), active, hover, s);
    return q;
}
// checkbox; the Ambre theme redraws its box (amber border and tick when on)
bool Check(const char* id, bool* v) {
    bool c = ImGui::Checkbox(id, v);
    if (P().skin) {
        ImVec2 a = ImGui::GetItemRectMin();
        float sz = ImGui::GetFrameHeight();
        afx::Checkbox(ImGui::GetWindowDrawList(), a, ImVec2(a.x + sz, a.y + sz), *v, ImGui::IsItemHovered(), s);
    }
    return c;
}
// selected row of a list / tree
void SelectedRow(R r) {
    if (P().skin) afx::SelectedRow(dl, r.min(), r.max(), s);
    else Box(r, P().select, 3);
}
// selected card / thumbnail
void SelectedFrame(R r) {
    if (P().skin) afx::SelectedFrame(dl, r.min(), r.max(), s, 4);
    else dl->AddRect(r.min(), r.max(), P().accent, 4 * s, 0, 1.4f * s);
}
// button with a value and a drop arrow (combos)
bool DropButton(const char* id, R r, const char* label, Icon icon, bool enabled = true) {
    bool q = Button(id, r, label, icon, false, false, enabled);
    Glyph(Icon::ChevronDown, r.x + r.w - 15 * s, r.y + (r.h - 9 * s) / 2, 9, P().muted);
    return q;
}
bool Combo(const char* id, R r, int& value, const char* const* options, int count, Icon icon) {
    bool q = false;
    if (DropButton(id, r, options[std::clamp(value, 0, count - 1)], icon)) ImGui::OpenPopup(id);
    // the list is as wide as the box and its text as large as the box text (the default popup text was too small)
    ImGui::SetNextWindowSize(ImVec2(r.w, 0));
    if (ImGui::BeginPopup(id)) {
        ImGui::SetWindowFontScale(1.4f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8 * s, 6 * s));
        for (int i = 0; i < count; ++i)
            if (ImGui::Selectable(options[i], value == i)) { value = i; q = true; }
        ImGui::PopStyleVar();
        ImGui::EndPopup();
    }
    return q;
}
bool Search(const char* id, R r, char* value, size_t length, const char* hint) {
    ImGui::SetCursorScreenPos(r.min());
    ImGui::SetNextItemWidth(r.w);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(25 * s, std::max(1.0f, (r.h - ImGui::GetFontSize()) / 2)));
    bool q = ImGui::InputTextWithHint(id, hint, value, length);
    ImGui::PopStyleVar();
    Glyph(Icon::Search, r.x + 8 * s, r.y + (r.h - 11 * s) / 2, 11, P().muted);
    return q;
}
// panel frame with the former Atelier graph title row: icon, title, small tag on the right
void Panel(R r, Icon icon, const char* title, const char* tag, ImU32 iconColor) {
    if (P().skin == 2) {   // Corail card: bold title, the tag in a small pill ("Ruins Kit" of the reference)
        afx::Panel(dl, r.min(), r.max(), s, 0);
        TextBold(r.x + 16 * s, r.y + 11 * s, title, P().text, 13.5f);
        if (tag && *tag) {
            ImVec2 z = TextSize(tag, 8);
            R pill = {r.x + r.w - 14 * s - z.x - 14 * s, r.y + 11 * s, z.x + 14 * s, 18 * s};
            Box(pill, P().raised, 5);
            Text(pill.x + 7 * s, pill.y + (pill.h - z.y) / 2, tag, P().muted, 8);
        }
        return;
    }
    if (P().skin) {   // surface gradient with a title band (structure-header.svg)
        afx::Panel(dl, r.min(), r.max(), s, 34 * s);
        if (P().skin == 3) {   // Halo: muted icon, condensed spaced capitals
            Glyph(icon, r.x + 12 * s, r.y + 11 * s, 13, P().muted);
            std::string up = title;
            for (char& ch : up) ch = (char)std::toupper((unsigned char)ch);
            afx::SpacedText(dl, P().fontHud ? P().fontHud : font, 10.5f * s * ts(), ImVec2(r.x + 33 * s, r.y + 11 * s), P().mutedLight, up.c_str(), 1.5f * s);
        } else {
            GlowGlyph(icon, r.x + 12 * s, r.y + 11 * s, 13, iconColor, iconColor, iconColor == P().accent ? 0.06f : 0.0f);
            Text(r.x + 33 * s, r.y + 10 * s, title, P().text, 13);
        }
        if (tag && *tag) Text(r.x + r.w - 12 * s - TextSize(tag, 8).x, r.y + 14 * s, tag, P().muted, 8);
        return;
    }
    Box(r, P().panel, 4, true);
    Glyph(icon, r.x + 12 * s, r.y + 12 * s, 13, iconColor);
    Text(r.x + 33 * s, r.y + 12 * s, title, P().text, 12);
    if (tag && *tag) Text(r.x + r.w - 12 * s - TextSize(tag, 8).x, r.y + 14 * s, tag, P().muted, 8);
}
// section label ("NODE PROPERTIES"); Ambre: lighter label followed by a separator line up to g_labelRight
void Label(float x, float y, const char* t) {
    if (!P().skin) { Text(x, y, t, P().muted, 9); return; }
    TextBold(x, y, t, P().text, 9);
    float e = x + TextSize(t, 9).x + 10 * s;
    if (g_labelRight > e) dl->AddLine(ImVec2(e, y + 6 * s), ImVec2(g_labelRight, y + 6 * s), P().border);
}
void Splitter(const char* id, R r, float& value, float low, float high, bool vertical, bool reverse = false) {
    Hit(id, r);
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(vertical ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
        Box(r, Fade(P().accent, 60), 0);
    }
    if (ImGui::IsItemActive()) {
        float d = (vertical ? ImGui::GetIO().MouseDelta.x : ImGui::GetIO().MouseDelta.y) / s;
        value = std::clamp(value + (reverse ? -d : d), low, high);
    }
}
// label above a full width slider (material / graph inspector pattern); returns true while edited
bool SliderRow(const char* id, const char* label, float x, float& y, float w, float* v, float lo, float hi, const char* fmt, bool& activated) {
    if (P().skin == 2) {
        // Corail slider (slider-rugosite.svg): one field with the fill, the matte handle, the label on the left and
        // the value on the right. ImGui's slider gives the interaction (drag, Ctrl+click to type); its own drawing
        // is hidden except while a value is typed. The right 24 px stay free (info icon slot).
        const ImGuiID gid = ImGui::GetID(id);
        const bool typing = ImGui::TempInputIsActive(gid);
        const float fw = w - 24 * s, fh = 32 * s;
        if (typing) afx::Field(dl, ImVec2(x, y), ImVec2(x + fw, y + fh), s, true);
        const ImVec4 clear(0, 0, 0, 0);
        for (ImGuiCol k : {ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive, ImGuiCol_SliderGrab, ImGuiCol_SliderGrabActive})
            ImGui::PushStyleColor(k, clear);
        ImGui::PushStyleColor(ImGuiCol_Text, typing ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : clear);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12 * s, std::max(0.0f, (fh - ImGui::GetFontSize()) / 2)));
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        ImGui::SetNextItemWidth(fw);
        bool c = ImGui::SliderFloat(id, v, lo, hi, fmt);
        activated |= ImGui::IsItemActivated();
        const bool focus = ImGui::IsItemActive() || ImGui::IsItemHovered();
        ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(6);
        Tip("Drag to edit · Ctrl+click to type a value");
        if (!typing) {
            char value[64];
            snprintf(value, sizeof(value), fmt, *v);
            afx::SliderField(dl, font, 11 * s * ts(), a, b, hi > lo ? (*v - lo) / (hi - lo) : 0.0f, label, value, focus, s);
        }
        y += 40 * s;
        return c;
    }
    if (P().skin) {
        // slider of the pack (slider-roughness.svg): label and numeric field on one row, thin amber track and flat
        // silver handle below; the right 24 px stay free (reset / info icon slot)
        Text(x, y, label, P().text, 11);
        const float vw = 70 * s, vh = 22 * s, vx = x + w - vw - 24 * s;
        ImGui::SetCursorScreenPos(ImVec2(vx, y - 3 * s));
        ImGui::SetNextItemWidth(vw);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6 * s, std::max(0.0f, (vh - ImGui::GetFontSize()) / 2)));
        std::string vid = std::string(id) + "_value";
        bool c = ImGui::DragFloat(vid.c_str(), v, (hi - lo) / 400.0f, lo, hi, fmt, ImGuiSliderFlags_AlwaysClamp);
        activated |= ImGui::IsItemActivated();
        bool focus = ImGui::IsItemActive();
        Tip("Drag to edit · double-click to type a value");
        ImGui::PopStyleVar();
        y += 25 * s;
        // invisible ImGui slider for the interaction, track and handle drawn over it
        const ImVec4 clear(0, 0, 0, 0);
        for (ImGuiCol k : {ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive, ImGuiCol_SliderGrab, ImGuiCol_SliderGrabActive})
            ImGui::PushStyleColor(k, clear);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, std::max(0.0f, (16 * s - ImGui::GetFontSize()) / 2)));
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        ImGui::SetNextItemWidth(w);
        c |= ImGui::SliderFloat(id, v, lo, hi, "", ImGuiSliderFlags_NoInput);
        activated |= ImGui::IsItemActivated();
        focus |= ImGui::IsItemActive();
        ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(5);
        afx::SliderTrack(dl, a.x, b.x, (a.y + b.y) / 2, hi > lo ? (*v - lo) / (hi - lo) : 0.0f, true, focus, s);
        y += 25 * s;
        return c;
    }
    Text(x, y, label, P().muted, 11);
    y += 19 * s;
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    ImGui::SetNextItemWidth(w);
    bool c = ImGui::SliderFloat(id, v, lo, hi, fmt);
    activated |= ImGui::IsItemActivated();
    y += 31 * s;
    return c;
}
// X / Y / Z drag fields with a coloured stripe
bool AxisFields(const char* id, float x, float y, float width, float* values, float speed, const char* fmt, bool& activated) {
    ImGui::PushID(id);
    float gap = 5 * s, w = (width - gap * 2) / 3;
    const ImU32 colors[] = {IM_COL32(222, 110, 121, 255), IM_COL32(136, 199, 150, 255), IM_COL32(113, 168, 233, 255)};
    const char* axes[] = {"X", "Y", "Z"};
    bool changed = false;
    for (int i = 0; i < 3; ++i) {
        float fx = x + i * (w + gap);
        ImGui::PushID(i);
        ImGui::SetCursorScreenPos(ImVec2(fx, y));
        ImGui::SetNextItemWidth(w);
        const bool amb = P().skin, cor = P().skin == 2;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2((amb ? (cor ? 18 : 24) : 16) * s, (cor ? 7 : 5) * s));
        if (amb) ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(cor ? afx::coral::border : afx::col::fieldBorder));
        changed |= ImGui::DragFloat("##axis", &values[i], speed, 0, 0, fmt, ImGuiSliderFlags_NoRoundToFormat);
        activated |= ImGui::IsItemActivated();
        const bool focus = ImGui::IsItemActive();
        if (amb) ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        float h = ImGui::GetItemRectSize().y;
        if (amb) {   // tinted X / Y / Z tag on the left of the field (transform-position-x.svg), amber border while edited
            ImVec2 a(fx, y), b(fx + w, y + h);
            afx::AxisTag(dl, a, b, i, s);
            if (focus) dl->AddRect(a, b, cor ? afx::coral::selectionBorder : afx::col::accent, (cor ? 5 : 3) * s);
            ImU32 ac = afx::AxisColor(i);
            if (cor) TextBold(fx + 9 * s, y + (h - 10 * s * ts()) / 2, axes[i], ac, 10);   // vector-position.svg: letter only
            else Text(fx + 12 * s - TextSize(axes[i], 10).x / 2, y + (h - 10 * s * ts()) / 2, axes[i], ac, 10);
        } else {
            dl->AddLine(ImVec2(fx + 1.5f * s, y + 4 * s), ImVec2(fx + 1.5f * s, y + h - 4 * s), colors[i], 2 * s);
            Text(fx + 6 * s, y + (h - 9 * s * ts()) / 2, axes[i], colors[i], 9);
        }
        Tip("Drag to edit · Ctrl+click to type a value");
        ImGui::PopID();
    }
    ImGui::PopID();
    return changed;
}

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
float linToSrgb(float c) { c = clampf(c, 0, 1); return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f; }
ImU32 srgbU32(const float3& lin) { return IM_COL32((int)(linToSrgb(lin.x) * 255), (int)(linToSrgb(lin.y) * 255), (int)(linToSrgb(lin.z) * 255), 255); }

// Content browser assets (same catalogue as the docking layout)
struct Asset { int kind; int index; std::string name; const char* type; const char* folder; };
std::vector<Asset> assetList() {
    std::vector<Asset> a;
    for (size_t i = 0; i < parts().size(); ++i) a.push_back({0, (int)i, "SM_" + toUtf8(parts()[i].name), "Static Mesh", "Primitives"});
    for (size_t i = 0; i < presetNames().size(); ++i) a.push_back({1, (int)i, "M_" + toUtf8(presetNames()[i]), "Material", "Materials"});
    a.push_back({2, SCENE_CORNELL, "Cornell Box", "Scene", "Scenes"});
    a.push_back({2, SCENE_STUDIO, "Village", "Scene", "Scenes"});
    a.push_back({2, SCENE_LOOKDEV, "LookDev", "Scene", "Scenes"});
    a.push_back({2, SCENE_CONCEPT, "Concept", "Scene", "Scenes"});
    return a;
}

} // namespace

// --------------------------------------------------------------------------- shared palette

// Feeds the Atelier modules (graph, material studio, inspector, scene) with the current theme settings:
// dark mode: the Atelier reference colours with the main colour tint (Blue Slate gives the exact values),
// light mode: derived from the light palette.
static void syncAtelierStyle(ImFont* bold, ImFont* hud) {
    atelier::Palette& a = atelier::Style();
    const atelier::Palette ref;
    const ThemePalette& p = th::P();
    auto u = [](const ImVec4& c, float al = 1) { return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, al)); };
    auto mixc = [](const ImVec4& x, const ImVec4& y, float t) { return ImVec4(x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t, x.z + (y.z - x.z) * t, 1); };
    auto v = [](ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); };
    a.layoutScale = th::layoutSize();
    a.textScale = th::textSize() / a.layoutScale;
    a.iconScale = th::iconSize() / a.layoutScale;
    a.fontBold = bold;
    a.fontHud = hud;
    a.skin = th::skin();
    if (a.skin == 1) {
        // Ambre: colours of the pack; the graph keeps its Atelier node style on graphite surfaces
        using namespace atelier::fx::col;
        a.light = false;
        a.accent = accent; a.select = selection; a.strong = accent;
        a.bg = canvas; a.panel = panel; a.raised = raised; a.hover = rgb(0x2D3036); a.bar = topBar; a.border = border;
        a.text = text; a.muted = secondary; a.mutedLight = rgb(0xB0B4BB);
        a.canvas = rgb(0x121417); a.node = rgb(0x202328); a.nodeOff = rgb(0x1A1C20);
        a.gridDot = rgb(0x3A3F46, 150); a.gridLine = rgb(0x2A2E33, 95);
        a.overlay = rgb(0x1D2024, 225); a.minimap = rgb(0x181A1E, 240); a.headerBar = rgb(0x1D2024); a.popupHeader = rgb(0x25282B);
        a.track = border; a.frameHover = rgb(0x4A5058); a.checkerA = rgb(0x3A3E44); a.checkerB = rgb(0x6E737A);
        a.toast = rgb(0x2A2117, 245); a.knob = thumb; a.pinExec = rgb(0xE0E1E4);
        a.menuX = 168; a.menuW = 46; a.headerShift = 111;   // room for the WZX ENGINE brand
        return;
    }
    if (a.skin == 2) {
        // Anthracite-Corail: colours of the pack; the graph keeps its Atelier node style on charcoal surfaces
        using namespace atelier::fx::coral;
        // the main colour of the settings (Corail value) tints the neutral colours, the coral accents keep their hue
        auto tint32 = [](ImU32 c) { return ImGui::ColorConvertFloat4ToU32(th::tint(ImGui::ColorConvertU32ToFloat4(c))); };
        atelier::fx::TintCoral(tint32);
        atelier::fx::SetCoralAccent(ImGui::ColorConvertFloat4ToU32(th::settings().corailAccent));   // selection colour of the settings
        auto rgb = [&](unsigned h, int al = 255) { return tint32(atelier::fx::col::rgb(h, al)); };
        a.light = false;
        a.accent = accent; a.select = selection; a.strong = rgb(0xB94F2F);
        a.bg = shell; a.panel = panel; a.raised = raised; a.hover = rgb(0x23262A); a.bar = shell; a.border = border;
        a.text = text; a.muted = muted; a.mutedLight = rgb(0xB4B8BE);
        a.canvas = viewport; a.node = rgb(0x2A2D31); a.nodeOff = rgb(0x1C1E21);
        a.gridDot = rgb(0x3A3E44, 150); a.gridLine = rgb(0x2A2D31, 95);
        a.overlay = rgb(0x181A1C, 230); a.minimap = rgb(0x181A1C, 240); a.headerBar = card; a.popupHeader = rgb(0x24272B);
        a.track = fill; a.frameHover = borderStrong; a.checkerA = rgb(0x3F4248); a.checkerB = rgb(0x2A2E33);
        a.toast = rgb(0x24201E, 245); a.knob = thumb; a.pinExec = rgb(0xE2E4E7);
        a.menuX = 168; a.menuW = 46; a.headerShift = 111;   // room for the WZX Engine brand
        return;
    }
    if (a.skin == 3) {
        // Halo: the three colours of the settings; the graph keeps its Atelier node style on these surfaces
        using namespace atelier::fx::halo;
        const UiSettings& us = th::settings();
        atelier::fx::SetHalo(u(us.haloMain), u(us.haloBg), u(us.haloSecond));
        auto over = [&](ImU32 base, ImU32 c, float k, int al = 255) { ImVec4 m = mixc(v(base), v(c), k); return u(m, al / 255.0f); };
        a.light = false;
        a.accent = accent; a.select = accentBg; a.strong = second;
        a.bg = shell; a.panel = panel; a.raised = raised; a.hover = hover; a.bar = panel; a.border = border;
        a.text = text; a.muted = muted; a.mutedLight = atelier::fx::col::rgb(0xAEB7C0);
        a.canvas = over(shell, raised, 0.6f); a.node = raised; a.nodeOff = panel;
        a.gridDot = over(border, muted, 0.2f, 150); a.gridLine = over(panel, border, 0.8f, 95);
        a.overlay = over(panel, panel, 0, 230); a.minimap = over(panel, panel, 0, 240); a.headerBar = panel; a.popupHeader = raised;
        a.track = border; a.frameHover = borderHi; a.checkerA = atelier::fx::col::rgb(0x3A424A); a.checkerB = atelier::fx::col::rgb(0x6B757E);
        a.toast = over(panel, accent, 0.10f, 245); a.knob = text; a.pinExec = atelier::fx::col::rgb(0xE2E7EB);
        a.menuX = 158; a.menuW = 46; a.headerShift = 101;   // room for the WZX brand
        return;
    }
    a.menuX = 53; a.menuW = 53; a.headerShift = 53;
    a.light = th::settings().light;
    a.accent = u(a.light ? th::accentTextV() : p.accent);
    a.select = u(p.select);
    a.strong = u(p.strong);
    a.bg = u(p.bg0); a.panel = u(p.bg1); a.raised = u(p.bg2); a.hover = u(p.hover); a.bar = u(p.bar); a.border = u(p.border);
    a.text = u(p.text); a.muted = u(p.dim);
    if (!a.light) {
        // reference colour of the Atelier sources, tinted by the main colour
        auto shift = [&](ImU32 lit) { return ImGui::ColorConvertFloat4ToU32(th::tint(v(lit))); };
        a.canvas = shift(ref.canvas); a.node = shift(ref.node); a.nodeOff = shift(ref.nodeOff); a.gridDot = shift(ref.gridDot); a.gridLine = shift(ref.gridLine);
        a.overlay = shift(ref.overlay); a.minimap = shift(ref.minimap); a.headerBar = shift(ref.headerBar); a.popupHeader = shift(ref.popupHeader);
        a.track = shift(ref.track); a.frameHover = shift(ref.frameHover); a.checkerA = ref.checkerA; a.checkerB = ref.checkerB;
        a.toast = u(mixc(p.select, p.bg1, 0.35f), 0.96f);
        a.mutedLight = u(mixc(p.dim, p.text, 0.3f)); a.knob = u(mixc(p.text, ImVec4(1, 1, 1, 1), 0.15f)); a.pinExec = u(mixc(p.text, ImVec4(1, 1, 1, 1), 0.2f));
    } else {
        a.canvas = u(mixc(p.bg0, p.bg1, 0.35f)); a.node = u(p.field); a.nodeOff = u(p.bg2);
        a.gridDot = u(p.border, 0.9f); a.gridLine = u(p.border, 0.45f);
        a.overlay = u(p.field, 0.88f); a.minimap = u(p.field, 0.94f); a.headerBar = u(mixc(p.bg1, p.bg2, 0.5f)); a.popupHeader = u(p.bg2);
        a.track = u(p.border); a.frameHover = u(mixc(p.border, p.dim, 0.4f)); a.checkerA = IM_COL32(186, 190, 196, 255); a.checkerB = IM_COL32(220, 223, 227, 255);
        a.toast = u(mixc(p.select, p.field, 0.2f), 0.97f);
        a.mutedLight = u(mixc(p.dim, p.text, 0.25f)); a.knob = u(mixc(p.dim, p.text, 0.35f)); a.pinExec = u(p.text);
    }
}

// --------------------------------------------------------------------------- frame

void App::drawAtelier() {
    syncAtelierStyle(m_gui.fontBold, m_gui.fontHud);
    if (m_workspace == 3 && P().skin != 3) m_workspace = 0;   // Prefab: Spartan workspace only
    if (m_workspace == 2 && P().skin != 3) {
        int target = atelier::material::Draw();
        if (target) m_workspace = target == 1 ? 0 : 1;
        return;
    }
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float X = vp->WorkPos.x, Y = vp->WorkPos.y, W = vp->WorkSize.x, H = vp->WorkSize.y;
    s = std::clamp(std::min(W / 1600.0f, H / 900.0f), 0.8f, 1.6f) * P().layoutScale;   // scale rule of archive/atelier-graph/former Atelier graph.cpp, layout size setting
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##atelier", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar(2);
    ImGui::SetWindowFontScale(s * 1.08f);
    font = ImGui::GetFont();
    dl = ImGui::GetWindowDrawList();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10 * s, 8 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7 * s, 5 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6 * s, 6 * s));
    Box({X, Y, W, H}, P().bg, 0);

    AtelierState& A = m_atl;
    if (P().skin == 3) spDraw(X, Y, W, H);   // Spartan: its own layout (SpartanUI.cpp)
    else {
    atlTopBar(X, Y, W);
    m_atelierTop = 42 * s;   // the game layer draws the Graph tab below this bar
    float maxL = std::max(185.0f, std::min(360.0f, W / s - 680)), maxR = std::max(248.0f, std::min(400.0f, W / s - 650)), maxB = std::max(141.0f, H / s - 430);
    A.leftW = std::clamp(A.leftW, 185.0f, maxL);
    A.rightW = std::clamp(A.rightW, 248.0f, maxR);
    A.bottomH = std::clamp(A.bottomH, 140.0f, maxB);
    float lw = A.leftW * s, rw = A.rightW * s, bh = A.bottomH * s, top = Y + 42 * s;
    float cx = X + lw + 12 * s, cw = W - lw - 12 * s - rw - 12 * s;
    if (m_workspace == 0) {   // the Scene panels; the Graph tab is drawn by the game layer
        atlHierarchy(X + 5 * s, top, lw, H - 52 * s);
        atlViewport(cx, top, cw, H - 42 * s - bh - 18 * s);
        atlContent(cx, Y + H - bh - 8 * s, cw, bh);
        atlInspector(X + W - rw - 6 * s, top, rw, H - 52 * s);
        dl = ImGui::GetWindowDrawList();
        Splitter("splitLeft", {X + lw + 5 * s, top, 7 * s, H - 52 * s}, A.leftW, 185, maxL, true);
        Splitter("splitRight", {X + W - rw - 12 * s, top, 6 * s, H - 52 * s}, A.rightW, 248, maxR, true, true);
        Splitter("splitBottom", {cx, Y + H - bh - 13 * s, cw, 5 * s}, A.bottomH, 140, maxB, false, true);
    }
    }   // not Halo

    // toast (status messages), as in archive/atelier-graph/former Atelier graph.cpp
    if (!m_status.empty() && GetTickCount64() - m_statusTime < 3500) {
        std::string t = toUtf8(m_status);
        ImVec2 z = TextSize(t, 11);
        R r = {X + W / 2 - z.x / 2 - 15 * s, Y + H - 46 * s, z.x + 30 * s, 28 * s};
        Box(r, P().toast, 4, true);
        Center(r, t, m_statusLevel == 2 ? kAmber : P().text, 11);
    }
    ImGui::PopStyleVar(3);
    ImGui::End();
}

// One inspector page (0 Actor, 1 Light, 2 Render, 3 World) in a scrolling child at (x, y): used by the Spartan
// inspector for the pages it does not redraw itself.
void App::atlInspectorPage(int page, float x, float y, float w, float h) {
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("spInspectorPage", ImVec2(w, h), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImGui::SetWindowFontScale(atelier::Style().layoutScale);
    ImGui::PopStyleVar();
    dl = ImGui::GetWindowDrawList();
    g_labelRight = x + w - 8 * s;
    ImVec2 c = ImGui::GetCursorScreenPos();
    const float bw = w - 8 * s;
    if (page == 0) atlInspectorActor(c.x, c.y, bw, 0);
    else if (page == 1) atlInspectorLighting(c.x, c.y, bw, 0);
    else if (page == 2) atlInspectorRender(c.x, c.y, bw, 0);
    else atlInspectorWorld(c.x, c.y, bw, 0);
    ImGui::EndChild();
    dl = ImGui::GetWindowDrawList();
}

// --------------------------------------------------------------------------- header (former Atelier graph HeaderBar)

// Items of the top bar menus (File, Edit, Create, View, Tools, Help), shared by the classic and Spartan top bars.
void App::atlMenuItems(int mi) {
    auto save = [&] { if (!m_themeTestOnly) th::saveSettings(m_exeDir + L"\\wzx_settings.ini"); };
    // menu text as large as the top bar labels (the default popup text was too small), same for the sub-menus
    const float menuScale = 1.4f * ts();
    ImGui::SetWindowFontScale(menuScale);
    if (mi == 0) {
        if (ImGui::MenuItem("New Scene", "Ctrl+N")) m_pendingAction = PA_NEW;
        if (ImGui::MenuItem("Open Scene…", "Ctrl+O")) m_pendingAction = PA_OPEN;
        if (ImGui::MenuItem("Save Scene", "Ctrl+S")) m_pendingAction = PA_SAVE;
        if (ImGui::MenuItem("Save Scene As…", "Ctrl+Shift+S")) m_pendingAction = PA_SAVE_AS;
        if (ImGui::MenuItem("Import OBJ Mesh…")) m_pendingAction = PA_IMPORT_MESH;
        ImGui::Separator();
        if (ImGui::MenuItem("Cornell Box Scene", "F1", m_sceneId == SCENE_CORNELL)) m_pendingScene = SCENE_CORNELL;
        if (ImGui::MenuItem("Village Scene", "F2", m_sceneId == SCENE_STUDIO)) m_pendingScene = SCENE_STUDIO;
        if (ImGui::MenuItem("LookDev Scene", "F3", m_sceneId == SCENE_LOOKDEV)) m_pendingScene = SCENE_LOOKDEV;
        if (ImGui::MenuItem("Concept Scene", "F4", m_sceneId == SCENE_CONCEPT)) m_pendingScene = SCENE_CONCEPT;
        ImGui::Separator();
        if (ImGui::MenuItem("Build…", "F7") && m_game) m_game->openBuildWindow();
        ImGui::Separator();
        if (ImGui::MenuItem("Viewport Capture", "F12")) saveScreenshot(false);
        if (ImGui::MenuItem("Settings…")) m_showSettings = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Exit", "Alt+F4")) PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
    }
    if (mi == 1) {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !m_undo.empty())) undo();
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !m_redo.empty())) redo();
        ImGui::Separator();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, m_selected >= 0)) duplicateSelected();
        if (ImGui::MenuItem("Delete", "Del", false, m_selected >= 0)) deleteSelected();
        ImGui::Separator();
        if (ImGui::BeginMenu("Add Object")) {
            ImGui::SetWindowFontScale(menuScale);
            for (const PartDef& p : parts())
                if (ImGui::MenuItem(toUtf8(p.name).c_str())) addPartFromLibrary(p);
            ImGui::EndMenu();
        }
    }
    if (mi == 2 && ImGui::BeginMenu("Mesh")) {
        ImGui::SetWindowFontScale(menuScale);
        for (int t : {PRIM_CUBE, PRIM_SPHERE, PRIM_PLANE, PRIM_CYLINDER, PRIM_TORUS, PRIM_DISK})
            if (ImGui::MenuItem(primitiveName(t))) addPrimitive(t);
        ImGui::EndMenu();
    }
    if (mi == 2) {   // a camera object where the view is (the first one is the render camera)
        if (ImGui::MenuItem("Camera from View")) createCameraFromView(false);
        if (ImGui::MenuItem("Render Camera from View")) createCameraFromView(true);
    }
    if (mi == 2 && m_curves) m_curves->menu(menuScale);   // Create > Curve (CurveEditor.cpp)
    if (mi == 3) {
        if (ImGui::MenuItem("Frame Selection", "F")) frameSelection();
        ImGui::MenuItem("Viewport Statistics", nullptr, &m_showStats);
        ImGui::MenuItem("Transform Gizmo", nullptr, &m_showGizmo);
        ImGui::Separator();
        if (ImGui::MenuItem("Wireframe", nullptr, m_rs.wireframe == 1)) { m_rs.wireframe = m_rs.wireframe == 1 ? 0 : 1; markDirty(); }
        if (ImGui::MenuItem("Wireframe Override", nullptr, m_rs.wireframe == 2)) { m_rs.wireframe = m_rs.wireframe == 2 ? 0 : 2; markDirty(); }
        ImGui::Separator();
        if (ImGui::MenuItem("Restore Panel Layout")) { m_atl.leftW = 222; m_atl.rightW = 284; m_atl.bottomH = 190; }
        ImGui::Separator();
        if (ImGui::MenuItem("Light Mode", nullptr, th::settings().light && !th::styled(), !th::styled())) { th::settings().light = !th::settings().light; th::apply(m_dpi); save(); }
        if (ImGui::BeginMenu("Theme")) {
            ImGui::SetWindowFontScale(menuScale);
            const char* names[] = {"Atelier", "Ambre", "Anthracite-Corail", "Spartan"}, *ids[] = {"Atelier", "Ambre", "Corail", "Spartan"};
            for (int i = 0; i < 4; ++i)
                if (ImGui::MenuItem(names[i], nullptr, th::skin() == i)) { th::settings().theme = ids[i]; th::apply(m_dpi); save(); }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Appearance Settings…")) m_showSettings = true;
    }
    if (mi == 4) {
        if (ImGui::MenuItem("Select", "Q", m_tool == TOOL_SELECT)) m_tool = TOOL_SELECT;
        if (ImGui::MenuItem("Move", "W", m_tool == TOOL_MOVE)) m_tool = TOOL_MOVE;
        if (ImGui::MenuItem("Rotate", "E", m_tool == TOOL_ROTATE)) m_tool = TOOL_ROTATE;
        if (ImGui::MenuItem("Scale", "R", m_tool == TOOL_SCALE)) m_tool = TOOL_SCALE;
        ImGui::Separator();
        ImGui::MenuItem("Snapping always on (else Shift)", nullptr, &m_magnet);
        if (ImGui::BeginMenu("Axes (Move / Rotate)")) {
            ImGui::SetWindowFontScale(menuScale);
            const char* modes[] = {"World", "Object", "Parent"};
            for (int i = 0; i < 3; ++i)
                if (ImGui::MenuItem(modes[i], nullptr, m_axisMode == i)) m_axisMode = i;
            ImGui::EndMenu();
        }
        ImGui::SetNextItemWidth(150 * s);
        ImGui::SliderFloat("Camera speed", &m_camSpeed, 0.1f, 5.0f, "%.1f");
    }
    if (mi == 5) {
        if (ImGui::MenuItem("Controls & Shortcuts")) m_showShortcuts = true;
        if (ImGui::MenuItem("About")) m_showAbout = true;
    }
}

void App::atlTopBar(float x0, float y0, float width) {
    const bool halo = P().skin == 3;
    const float barH = (halo ? 40 : 37) * s;
    Box({x0, y0, width, barH}, P().bar, 0);
    if (P().skin) {   // topbar.svg: gold cube and WZX ENGINE wordmark, bottom border
        dl->AddLine(ImVec2(x0, y0 + barH - 0.5f), ImVec2(x0 + width, y0 + barH - 0.5f), P().border);
        afx::Brand(dl, font, P().fontBold, ImVec2(x0 + 13 * s, y0 + 6.5f * s), s);
    } else {
        Glyph(Icon::Cube, x0 + 15 * s, y0 + 10 * s, 17, P().accent);
    }
    const float sep = P().menuX - 10;
    if (!halo) dl->AddLine(ImVec2(x0 + sep * s, y0 + 7 * s), ImVec2(x0 + sep * s, y0 + 30 * s), P().border);
    const float my = halo ? y0 + 8 * s : y0 + 6 * s;   // menu row
    const char* menus[] = {"File", "Edit", "Create", "View", "Tools", "Help"};
    float x = x0 + P().menuX * s;
    auto save = [&] { if (!m_themeTestOnly) th::saveSettings(m_exeDir + L"\\wzx_settings.ini"); };
    for (int mi = 0; mi < 6; ++mi) {
        std::string id = "atlmenu" + std::to_string(mi);
        R r = {x, my, P().menuW * s, 24 * s};
        if (Hit(id.c_str(), r)) ImGui::OpenPopup(id.c_str());
        const bool menuHover = ImGui::IsItemHovered() || ImGui::IsPopupOpen(id.c_str());
        if (menuHover) Box(r, halo ? P().hover : P().raised, halo ? 2 : 3);
        Center(r, menus[mi], halo && !menuHover ? P().muted : P().text, 11);
        if (ImGui::BeginPopup(id.c_str())) {
            atlMenuItems(mi);
            ImGui::EndPopup();
        }
        x += P().menuW * s;
    }
    if (halo) {
        // workspaces: one joined strip of tabs (active: raised, main-colour underline)
        const char* wsNames[] = {"SCENE", "GRAPH", "MATERIALS"};
        const Icon wsIcons[] = {Icon::Scene, Icon::Blueprint, Icon::Material};
        const float wsW[] = {92, 92, 120};
        float wx = x + 14 * s, wy = y0 + 7 * s, wh = 26 * s;
        float total = 0;
        for (float v : wsW) total += v * s;
        dl->AddRect(ImVec2(wx, wy), ImVec2(wx + total, wy + wh), P().border, 0);
        for (int i = 0; i < 3; ++i) {
            R t = {wx, wy, wsW[i] * s, wh};
            char id[24];
            snprintf(id, sizeof(id), "haloWs%d", i);
            const bool on = m_workspace == i;
            if (Hit(id, t)) m_workspace = i;
            const bool hv = ImGui::IsItemHovered();
            if (on) { Box(t, P().raised, 0); dl->AddRectFilled(ImVec2(t.x, t.y + t.h - 2 * s), ImVec2(t.x + t.w, t.y + t.h), P().accent); }
            else if (hv) Box(t, P().hover, 0);
            if (i > 0) dl->AddLine(ImVec2(t.x, t.y), ImVec2(t.x, t.y + t.h), P().border);
            ImFont* hud = P().fontHud ? P().fontHud : font;
            const float fs = 10.5f * s * ts();
            float lw = afx::SpacedText(dl, hud, fs, ImVec2(-1e5f, 0), 0, wsNames[i], 1.6f * s) + 1e5f;   // measure
            float gx = t.x + (t.w - (13 * s + 7 * s + lw)) / 2;
            ImU32 c = on ? P().text : (hv ? P().text : P().muted);
            Glyph(wsIcons[i], gx, t.y + (t.h - 13 * s) / 2, 13, on ? P().accent : c);
            afx::SpacedText(dl, hud, fs, ImVec2(gx + 20 * s, t.y + (t.h - fs) / 2), c, wsNames[i], 1.6f * s);
            wx += t.w;
        }
        // right: save, undo, redo | Play (main colour), Build (second colour) | settings
        float bx = x0 + width - 12 * s, by = y0 + 6 * s, bh = 28 * s;
        auto left = [&](float bw) { bx -= bw; float r0 = bx; bx -= 6 * s; return r0; };
        if (Button("sceneSettings", {left(28 * s), by, 28 * s, bh}, "", Icon::Settings, m_showSettings, true)) m_showSettings = !m_showSettings;
        Tip("Settings: theme, colours, sizes");
        bx -= 6 * s;
        if (PrimaryButton("buildScene", {left(98 * s), by, 98 * s, bh}, "Build  F7", Icon::Check, m_game != nullptr)) m_game->openBuildWindow();
        Tip("Build  (F7)\nOpens the Build window: pick a platform profile, then Build or Build and Run.");
        const bool playing = m_game && m_game->playing();
        if (Button("playScene", {left(92 * s), by, 92 * s, bh}, playing ? "Stop  F5" : "Play  F5", Icon::Play, true, false, m_game != nullptr)) m_game->requestPlay();
        Tip(playing ? "Stop  (F5)\nEnds the test: the scene comes back as it was before Play."
                    : "Play  (F5)\nTests the game in the editor viewport with its graph.\nStop restores the scene as it was.");
        bx -= 6 * s;
        if (Button("haloRedo", {left(28 * s), by, 28 * s, bh}, "", Icon::Redo, false, true, !m_redo.empty())) redo();
        Tip("Redo (Ctrl+Y)");
        if (Button("haloUndo", {left(28 * s), by, 28 * s, bh}, "", Icon::Undo, false, true, !m_undo.empty())) undo();
        Tip("Undo (Ctrl+Z)");
        if (Button("saveScene", {left(28 * s), by, 28 * s, bh}, "", Icon::Save, false, true)) m_pendingAction = 1;
        Tip("Save the scene (Ctrl+S)");
        return;
    }
    // workspaces, same positions as the Graph Editor and Material Studio headers
    float ty = y0 + 7 * s, wx = x0 + P().headerShift * s;
    // the active tab follows the workspace shown (the Graph tab was never highlighted before)
    if (Button("workspaceScene", {wx + 339 * s, ty, 79 * s, 24 * s}, "Scene", Icon::Scene, m_workspace == 0)) m_workspace = 0;
    if (Button("workspaceGraph", {wx + 422 * s, ty, 96 * s, 24 * s}, "Graph", Icon::Blueprint, m_workspace == 1)) m_workspace = 1;
    if (Button("workspaceMaterials", {wx + 522 * s, ty, 117 * s, 24 * s}, "Materials", Icon::Material, m_workspace == 2)) m_workspace = 2;
    // centre: build window and test player
    float cx = x0 + std::max((655 + P().headerShift) * s, width / 2 - 89 * s);
    if (PrimaryButton("buildScene", {cx, y0 + 6 * s, 103 * s, 25 * s}, "Build", Icon::Check, m_game != nullptr)) m_game->openBuildWindow();
    Tip("Build  (F7)\nOpens the Build window: pick a platform profile, then Build or Build and Run.\nWindows now, iOS coming soon.");
    const bool playing = m_game && m_game->playing();
    if (Button("playScene", {cx + 109 * s, y0 + 6 * s, 71 * s, 25 * s}, playing ? "Stop" : "Play", Icon::Play, playing, false, m_game != nullptr))
        m_game->requestPlay();
    Tip(playing ? "Stop  (F5)\nEnds the test: the scene comes back as it was before Play."
                : "Play  (F5)\nTests the game in the editor viewport with its graph.\nStop restores the scene as it was.");
    // right
    if (Button("saveScene", {x0 + width - 182 * s, y0 + 6 * s, 72 * s, 25 * s}, "Save", Icon::Save)) m_pendingAction = 1;
    Tip("Save the scene (Ctrl+S)");
    if (Button("sceneSettings", {x0 + width - 102 * s, y0 + 6 * s, 28 * s, 25 * s}, "", Icon::Settings, m_showSettings, true)) m_showSettings = !m_showSettings;
    Tip("Settings: theme, light mode, main colour, text size");
    if (Hit("profile", {x0 + width - 54 * s, y0 + 6 * s, 46 * s, 25 * s})) m_showAbout = true;
    Tip("About WZX Engine");
    Glyph(Icon::Person, x0 + width - 48 * s, y0 + 12 * s, 13, P().text);
    Glyph(Icon::ChevronDown, x0 + width - 24 * s, y0 + 15 * s, 9, P().muted);
}

// --------------------------------------------------------------------------- left panel (former Atelier graph Palette)

// preset colours of the Scene Manager collections (Collection::color)
static const ImU32 kCollectionColors[] = {
    IM_COL32(128, 140, 160, 255), IM_COL32(229, 96, 96, 255), IM_COL32(232, 152, 72, 255), IM_COL32(222, 196, 82, 255),
    IM_COL32(112, 190, 112, 255), IM_COL32(82, 190, 180, 255), IM_COL32(92, 152, 235, 255), IM_COL32(162, 122, 230, 255),
    IM_COL32(225, 112, 182, 255), IM_COL32(160, 160, 160, 255)};
static const char* kCollectionColorNames[] = {"Slate", "Red", "Orange", "Yellow", "Green", "Teal", "Blue", "Violet", "Pink", "Grey"};
static const int kCollectionColorCount = 10;

// Scene Manager: scene camera first, then the collections and the root items in their order, the children below
// their parent. Click / Ctrl+click / Shift+click select, double-click or F2 renames, drag & drop reorders (between
// two rows) or parents (on a row), right-click gives the actions of the row (or New Collection on the empty area).
void App::atlHierarchy(float x, float y, float w, float h) {
    AtelierState& A = m_atl;
    outlinerIds();
    R r = {x, y, w, h};
    const bool halo = P().skin == 3;
    Panel(r, Icon::Scene, "Scene Manager", halo ? "" : "LEVEL", P().accent);
    float px = r.x + 10 * s, py = r.y + 44 * s, pw = r.w - 20 * s;
    g_labelRight = px + pw;
    R addR = {px, 0, (pw - 6 * s) / 2, 23 * s}, colR = addR;   // Add Object / Collection buttons
    if (halo) {
        // Halo: the scenes are in the rail; the header holds the item count and the two add buttons, then the search
        size_t count = m_scene.lights.size() + 1;
        for (const Object& o : m_scene.objects) count += !o.ghost && !(o.part == "House" && o.grab == -2);
        addR = {r.x + r.w - 62 * s, r.y + 6 * s, 24 * s, 22 * s};
        colR = {r.x + r.w - 34 * s, r.y + 6 * s, 24 * s, 22 * s};
        std::string cnt = std::to_string(count);
        Text(addR.x - 8 * s - TextSize(cnt, 9).x, r.y + 12 * s, cnt, P().muted, 9);
        if (A.searchFocus) { ImGui::SetKeyboardFocusHere(); A.searchFocus = false; }
        Search("##atlSceneSearch", {px, r.y + 44 * s, pw, 28 * s}, A.sceneSearch, sizeof(A.sceneSearch), "Search scene...");
        py = r.y + 80 * s;
    } else {
    Label(px, py, "SCENES");
    py += 21 * s;
    const char* scenes[] = {"Cornell Box", "Village", "LookDev", "Concept"};
    for (int i = 0; i < 4; ++i) {
        char id[16];
        snprintf(id, sizeof(id), "scene%d", i);
        if (Button(id, {px, py, pw, 26 * s}, scenes[i], i == 0 ? Icon::Cube : Icon::Sun, m_sceneId == i) && m_sceneId != i) m_pendingScene = i;
        py += 30 * s;
    }
    py += 14 * s;
    dl->AddLine(ImVec2(px, py), ImVec2(px + pw, py), P().border);
    py += 16 * s;
    size_t count = m_scene.lights.size() + 1;
    for (const Object& o : m_scene.objects) count += !o.ghost && !(o.part == "House" && o.grab == -2);
    Text(px, py, "OUTLINER", P().text, 11);
    std::string cnt = std::to_string(count) + " items";
    Text(px + pw - TextSize(cnt, 9).x, py + 2 * s, cnt, P().muted, 9);
    py += 24 * s;
    Search("##atlSceneSearch", {px, py, pw, 28 * s}, A.sceneSearch, sizeof(A.sceneSearch), "Search scene...");
    py += 36 * s;
    addR.y = colR.y = py;
    colR.x = px + addR.w + 6 * s;
    py += 33 * s;
    }
    if (Button("addObject", addR, halo ? "" : "Add Object", Icon::Plus, false, halo)) ImGui::OpenPopup("atlNewActor");
    Tip("Add a primitive to the scene (or drag one from the Content Browser)");
    if (ImGui::BeginPopup("atlNewActor")) {
        for (const PartDef& p : parts())
            if (ImGui::MenuItem(toUtf8(p.name).c_str())) addPartFromLibrary(p);
        ImGui::EndPopup();
    }
    if (Button("addCollection", colR, halo ? "" : "Collection", Icon::Collection, false, halo)) {
        pushUndo();
        uint32_t c = addCollection("Collection", (int)(m_scene.collections.size() % kCollectionColorCount));
        // with a selection: the new collection takes it (one undo step)
        std::vector<uint32_t> sel = selectedNodeIds();
        if (!sel.empty()) {
            size_t depth = m_undo.size();
            moveNodes(sel, 2, c);
            if (m_undo.size() > depth) m_undo.pop_back();
        }
        startRename(c);
    }
    Tip("New collection (holds the selection if there is one): a folder of the Scene Manager, no effect on the scene");

    ImGui::SetCursorScreenPos(ImVec2(px, py));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("atlTree", ImVec2(pw, std::max(20.0f, r.y + r.h - py - 9 * s)), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImGui::SetWindowFontScale(atelier::Style().layoutScale);   // child text independent of the layout size
    ImGui::PopStyleVar();
    dl = ImGui::GetWindowDrawList();
    float yy = ImGui::GetCursorScreenPos().y;
    const float rowH = 27 * s, colH = 34 * s, indentW = 14 * s;
    const bool ctrl = ImGui::GetIO().KeyCtrl, shift = ImGui::GetIO().KeyShift;

    std::vector<OutlinerRow> rows;
    outlinerRows(rows, A.sceneSearch);
    struct PendingMove { int mode = -1; uint32_t target = 0; } move;
    static uint32_t ctxId = 0;   // node of the context menu
    static std::vector<uint32_t> clickPrev;   // selection before the last plain click (double-click multi-renaming)
    bool openCtx = false, openEmpty = false;

    // selection rectangle swept over the rows (Maya Outliner): from the empty area or an unselected row, Ctrl / Shift
    // add to the selection; the rows crossed vertically are selected. Start kept in content space (the list scrolls).
    static struct Sweep { bool active = false; float y0 = 0, x0 = 0; std::vector<uint32_t> base, last; } sweep;
    const float winY = ImGui::GetWindowPos().y, winH = ImGui::GetWindowHeight();
    bool sweepEnded = false;
    if (sweep.active && !ImGui::IsMouseDown(0)) { sweep.active = false; sweepEnded = true; }
    std::vector<std::pair<R, NodeRef>> rowRects;
    auto startSweep = [&]() {
        sweep.active = true;
        sweep.x0 = ImGui::GetIO().MouseClickedPos[0].x;
        sweep.y0 = ImGui::GetIO().MouseClickedPos[0].y - winY + ImGui::GetScrollY();
        sweep.base = (ctrl || shift) ? selectedNodeIds() : std::vector<uint32_t>();
        sweep.last.clear();
    };

    // drop indicator of a row: line before / after, frame when dropped into the row
    auto dropZone = [&](R rr, float indent, NodeRef n, bool open, uint32_t id) {
        if (!ImGui::BeginDragDropTarget()) return;
        const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("WZX_NODE", ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
        if (pl) {
            float t = (ImGui::GetMousePos().y - rr.y) / rr.h;
            int mode;
            if (n.kind == NODE_LIGHT || n.kind == NODE_SUN) mode = t < 0.5f ? 0 : 1;
            else if (n.kind == NODE_COLLECTION) mode = t < 0.25f ? 0 : (t > 0.75f && !open ? 1 : 2);
            else mode = t < 0.25f ? 0 : (t > 0.75f ? 1 : 2);
            const ImU32 c = P().accent;
            if (mode == 2) dl->AddRect(rr.min(), rr.max(), c, 3 * s, 0, 1.6f * s);
            else {
                float ly = mode == 0 ? rr.y : rr.y + rr.h;
                dl->AddLine(ImVec2(rr.x + indent, ly), ImVec2(rr.x + rr.w, ly), c, 2 * s);
                dl->AddCircleFilled(ImVec2(rr.x + indent, ly), 3 * s, c);
            }
            if (pl->IsDelivery()) move = {mode, id};
        }
        ImGui::EndDragDropTarget();
    };

    // fixed first row: the scene camera (not ordered, not a drop target except "before the first item")
    {
        R rr = {px, yy, pw, rowH - 2 * s};
        ImGui::PushID("sceneCamera");
        bool q = Hit("node", rr);
        bool hv = ImGui::IsItemHovered();
        if (hv) Box(rr, P().raised, 3);
        if (!rows.empty()) {
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("WZX_NODE", ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                    dl->AddLine(ImVec2(rr.x, rr.y + rr.h), ImVec2(rr.x + rr.w, rr.y + rr.h), P().accent, 2 * s);
                    if (pl->IsDelivery()) move = {0, rows.front().id};
                }
                ImGui::EndDragDropTarget();
            }
        }
        // same left padding as the root rows (fold arrow slot, icon, name), a thin line below
        Glyph(Icon::Camera, px + 15 * s, yy + (rowH - 2 * s - 13 * s) / 2, 13, halo ? P().muted : P().accent);
        Text(px + 37 * s, yy + (rowH - 2 * s - 11 * s * ts()) / 2, "Scene Camera", P().text, 11);
        dl->AddLine(ImVec2(px, yy + rowH - 1.5f * s), ImVec2(px + pw, yy + rowH - 1.5f * s), Fade(P().border, 150), 1.0f);
        Tip("The scene camera: always first (its settings: Render tab)");
        if (q) { select(-1); A.inspectorTab = 2; }
        ImGui::PopID();
        yy += rowH;
    }

    for (size_t ri = 0; ri < rows.size(); ++ri) {
        const OutlinerRow& row = rows[ri];
        const NodeRef n = row.node;
        const bool isCol = n.kind == NODE_COLLECTION;
        const float hh = isCol ? colH : rowH;
        const float ind = row.depth * indentW;
        R rr = {px, yy, pw, hh - 2 * s};
        const bool sel = nodeSelected(n);
        const bool vis = nodeVisible(n), locked = nodeLocked(n);
        char id[32];
        snprintf(id, sizeof(id), "node%u", row.id);
        ImGui::PushID(id);
        // left and middle buttons: left click selects, left drag from a selected row or middle drag moves the
        // nodes (Maya), left drag from an unselected row sweeps a selection rectangle over the rows
        ImGui::SetCursorScreenPos(rr.min());
        bool q = ImGui::InvisibleButton("node", ImVec2(std::max(1.0f, rr.w - 48 * s), rr.h), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
        q = q && !ImGui::IsMouseReleased(ImGuiMouseButton_Middle) && !sweepEnded;
        const bool hv = ImGui::IsItemHovered();
        const bool dbl = hv && ImGui::IsMouseDoubleClicked(0);
        if (hv && ImGui::IsMouseReleased(1)) { ctxId = row.id; openCtx = true; }
        rowRects.push_back({rr, n});
        const bool middle = ImGui::IsItemActive() && ImGui::GetCurrentContext()->ActiveIdMouseButton == ImGuiMouseButton_Middle;
        // the gesture is chosen at the press: a row selected at that moment (or the middle button) moves, an unselected
        // row sweeps a selection rectangle; the rows the rectangle selects never turn it into a drag & drop
        static uint32_t pressId = 0;
        static bool pressSel = false;
        if (ImGui::IsItemActivated()) { pressId = row.id; pressSel = sel; }
        const bool canMove = ImGui::IsItemActive() && !sweep.active && (middle || (pressId == row.id && pressSel && ImGui::GetCurrentContext()->ActiveIdMouseButton == ImGuiMouseButton_Left));
        if (ImGui::IsItemActive() && !canMove && !middle && !sweep.active && ImGui::IsMouseDragging(0, 4 * s)) startSweep();
        // drag the selection (or this row alone when it is not selected)
        if (canMove && ImGui::BeginDragDropSource()) {
            if (ImGui::GetDragDropPayload() == nullptr || !ImGui::GetDragDropPayload()->IsDataType("WZX_NODE")) {
                m_outDrag.clear();
                if (sel) m_outDrag = selectedNodeIds();
                else m_outDrag.push_back(row.id);
            }
            int one = 1;
            ImGui::SetDragDropPayload("WZX_NODE", &one, sizeof(one));
            if (m_outDrag.size() == 1) ImGui::TextUnformatted(nodeName(findNode(m_outDrag[0])).c_str());
            else ImGui::Text("%d items", (int)m_outDrag.size());
            ImGui::EndDragDropSource();
        }
        dropZone(rr, ind + 6 * s, n, row.open, row.id);

        // background
        if (isCol) {
            const Collection& c = m_scene.collections[n.index];
            ImU32 cc = kCollectionColors[std::clamp(c.color, 0, kCollectionColorCount - 1)];
            Box(rr, Fade(cc, sel ? 70 : (hv ? 46 : 30)), 4);
            dl->AddRectFilled(rr.min(), ImVec2(rr.x + 3.5f * s, rr.y + rr.h), cc, 4 * s, ImDrawFlags_RoundCornersLeft);
            if (sel) dl->AddRect(rr.min(), rr.max(), Fade(cc, 200), 4 * s, 0, 1.2f * s);
        } else if (sel) SelectedRow(rr);
        else if (hv) Box(rr, P().raised, 3);

        // fold arrow
        const float ax = px + ind + 2 * s, ty = yy + (hh - 2 * s - 13 * s) / 2;
        if (row.hasChildren || isCol)
            Glyph(row.open ? Icon::ChevronDown : Icon::ChevronRight, ax, yy + (hh - 2 * s - 9 * s) / 2, 9, isCol ? kCollectionColors[std::clamp(m_scene.collections[n.index].color, 0, kCollectionColorCount - 1)] : P().muted);
        const float ix = ax + 13 * s;
        Icon icon = Icon::Cube;
        ImU32 icol = P().text;
        std::string sub;
        if (isCol) {
            const Collection& c = m_scene.collections[n.index];
            icon = Icon::Collection;
            icol = kCollectionColors[std::clamp(c.color, 0, kCollectionColorCount - 1)];
            sub = std::to_string(nodeChildren(0, c.id, false).size());
        } else if (n.kind == NODE_OBJECT) {
            const Object& o = m_scene.objects[n.index];
            if (o.group) { icon = Icon::Folder; icol = IM_COL32(150, 170, 200, 255); }
            else if (o.camera) { icon = Icon::Camera; icol = o.camRender ? IM_COL32(255, 196, 92, 255) : IM_COL32(214, 220, 230, 255); }   // the render camera: orange, as its icon in the view
            else if (o.curve.valid()) { icon = Icon::Curve; icol = IM_COL32(98, 200, 150, 255); }   // curve (Create > Curve)
            else icol = IM_COL32(104, 160, 226, 255);   // every mesh: the same bluish icon
        } else if (n.kind == NODE_LIGHT) { icon = Icon::Light; icol = IM_COL32(242, 201, 76, 255); }
        else if (n.kind == NODE_SUN) { icon = Icon::Sun; icol = IM_COL32(242, 201, 76, 255); }
        const bool dim = !vis;
        if (halo && !isCol) icol = sel ? P().accent : P().muted;   // Halo: neutral icons, the selection in the main colour
        Glyph(icon, ix, ty, 13, dim ? P().muted : icol);
        const float nx = ix + 22 * s;
        if (m_renameId == row.id) {
            // inline rename: Enter validates, Esc cancels, a click elsewhere validates
            ImGui::SetCursorScreenPos(ImVec2(nx - 4 * s, yy + (hh - 2 * s - ImGui::GetFrameHeight()) / 2));
            ImGui::SetNextItemWidth(std::max(40 * s, px + pw - 52 * s - nx));
            if (m_renameFocus) { ImGui::SetKeyboardFocusHere(); m_renameFocus = false; }
            bool enter = ImGui::InputText("##rename", m_renameBuf, sizeof(m_renameBuf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            bool done = enter || ImGui::IsItemDeactivated();
            if (done) {
                const bool cancel = !enter && ImGui::IsKeyPressed(ImGuiKey_Escape, false);
                if (!cancel && m_renameBuf[0] && !m_renameMulti.empty()) {
                    // multi-renaming: text + _001, _002... in the order of the list (rows hidden by a fold last)
                    std::vector<uint32_t> order;
                    for (const OutlinerRow& r2 : rows)
                        if (std::find(m_renameMulti.begin(), m_renameMulti.end(), r2.id) != m_renameMulti.end()) order.push_back(r2.id);
                    for (uint32_t m : m_renameMulti)
                        if (std::find(order.begin(), order.end(), m) == order.end()) order.push_back(m);
                    pushUndo();
                    for (size_t k = 0; k < order.size(); ++k) {
                        char nm[160];
                        snprintf(nm, sizeof(nm), "%s_%03d", m_renameBuf, (int)k + 1);
                        setNodeName(findNode(order[k]), nm);
                    }
                    status(std::to_wstring(order.size()) + L" items renamed: " + fromUtf8(m_renameBuf) + L"_001...");
                } else if (!cancel && m_renameBuf[0] && nodeName(n) != m_renameBuf) { pushUndo(); setNodeName(n, m_renameBuf); status(L"Renamed: " + fromUtf8(m_renameBuf)); }
                m_renameMulti.clear();
                m_renameId = 0;
            }
        } else {
            dl->PushClipRect(ImVec2(px, yy), ImVec2(px + pw - 52 * s - (sub.empty() ? 0 : 18 * s), yy + hh), true);
            // name colour: normal, locked (pale green tint, darker on light themes), hidden (muted), locked and hidden (faded tint)
            const ImU32 lockCol = Family(IM_COL32(150, 204, 156, 255));
            const ImU32 nameCol = locked ? (dim ? Fade(lockCol, 130) : lockCol) : (dim ? P().muted : P().text);
            if (isCol) TextBold(nx, yy + (hh - 2 * s - 12 * s * ts()) / 2, nodeName(n), nameCol, 12);
            else Text(nx, yy + (hh - 2 * s - 11 * s * ts()) / 2, nodeName(n), nameCol, 11);
            dl->PopClipRect();
            if (!sub.empty()) Text(px + pw - 52 * s - TextSize(sub, 9).x - 4 * s, yy + (hh - 2 * s - 9 * s * ts()) / 2, sub, P().muted, 9);
        }
        // eye / lock (a collection or a group: applied to its contents)
        const float ey = yy + (hh - 2 * s - 11 * s) / 2;
        if (Hit("eye", {px + pw - 44 * s, yy + 2 * s, 20 * s, hh - 6 * s})) { pushUndo(); setNodeVisible(n, !vis); }
        Tip(vis ? "Hide" : "Show");
        Glyph(Icon::Eye, px + pw - 40 * s, ey, 11, vis ? P().muted : Fade(P().muted, 90));
        if (Hit("lock", {px + pw - 22 * s, yy + 2 * s, 20 * s, hh - 6 * s})) { pushUndo(); setNodeLocked(n, !locked); }
        Tip(locked ? "Unlock" : "Lock (not selectable)");
        Glyph(Icon::Lock, px + pw - 18 * s, ey, 11, locked ? P().accent : Fade(P().muted, 90));
        ImGui::PopID();

        if (dbl && n.kind != NODE_SUN) {
            if (clickPrev.size() > 1 && std::find(clickPrev.begin(), clickPrev.end(), row.id) != clickPrev.end()) {
                std::vector<NodeRef> back;
                for (uint32_t c : clickPrev) back.push_back(findNode(c));
                selectNodes(back, n);   // the first click of the double-click had kept this row only
            }
            startRename(row.id);
        }
        else if (q && m_renameId != row.id) {   // (the release of a double-click keeps the selection being renamed)
            const bool onArrow = ImGui::GetMousePos().x < ix && (row.hasChildren || isCol);
            if (onArrow || (isCol && ctrl)) {   // fold / unfold (Ctrl+click on a collection)
                if (isCol) m_scene.collections[n.index].collapsed = row.open;
                else if (n.kind == NODE_OBJECT) m_scene.objects[n.index].collapsed = row.open;
            } else if (!isCol && locked) {
                status(L"Locked: unlock it to select it", 2);
            } else if (ctrl) {
                toggleNodeSelected(n);
                m_outAnchor = row.id;
            } else if (shift && m_outAnchor) {
                // range from the anchor to this row (the locked rows and the collections are skipped)
                int a = -1, b = (int)ri;
                for (size_t k = 0; k < rows.size(); ++k) if (rows[k].id == m_outAnchor) a = (int)k;
                if (a < 0) a = b;
                std::vector<NodeRef> range;
                for (int k = std::min(a, b); k <= std::max(a, b); ++k)
                    if (rows[k].node.kind != NODE_COLLECTION && !nodeLocked(rows[k].node)) range.push_back(rows[k].node);
                if (isCol) range.push_back(n);
                selectNodes(range, n);
            } else {
                clickPrev = selectedNodeIds();   // a double-click on a row of a multi-selection renames them all
                selectNodes({n}, n);
                m_outAnchor = row.id;
            }
        }
        yy += hh;
    }

    // empty area below the rows: drop = back to the root (end of the list), right-click = new collection
    {
        float rest = std::max(ImGui::GetWindowPos().y + ImGui::GetWindowHeight() - yy, 40 * s);
        R er = {px, yy, pw, rest};
        Hit("outlinerEmpty", er);
        bool hv = ImGui::IsItemHovered();
        if (hv && ImGui::IsMouseReleased(1)) openEmpty = true;
        if (hv && ImGui::IsMouseClicked(0) && !ctrl && !shift) select(-1);
        if (ImGui::IsItemActive() && !sweep.active && ImGui::IsMouseDragging(0, 4 * s)) startSweep();
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("WZX_NODE", ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                dl->AddLine(ImVec2(px, yy + 1 * s), ImVec2(px + pw, yy + 1 * s), P().accent, 2 * s);
                if (pl->IsDelivery()) move = {3, 0};
            }
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("WZX_ASSET")) addPartFromLibrary(parts()[*(const int*)pl->Data]);
            ImGui::EndDragDropTarget();
        }
        yy += rest;
    }
    ImGui::SetCursorScreenPos(ImVec2(px, yy));
    ImGui::Dummy(ImVec2(1, 1));

    if (sweep.active) {
        const ImVec2 m = ImGui::GetMousePos();
        // scroll while the rectangle goes past the top or the bottom of the list
        if (m.y > winY + winH - 8 * s) ImGui::SetScrollY(ImGui::GetScrollY() + 8 * s);
        else if (m.y < winY + 8 * s) ImGui::SetScrollY(std::max(0.0f, ImGui::GetScrollY() - 8 * s));
        const float ys = sweep.y0 - ImGui::GetScrollY() + winY;
        const float y0 = std::min(ys, m.y), y1 = std::max(ys, m.y);
        std::vector<uint32_t> hits;
        NodeRef primary;
        for (auto& rn : rowRects) {
            const R& rr = rn.first;
            if (rr.y + rr.h < y0 || rr.y > y1) continue;
            if (rn.second.kind == NODE_COLLECTION || nodeLocked(rn.second)) continue;
            hits.push_back(nodeId(rn.second));
            if (!primary.valid()) primary = rn.second;
        }
        if (hits != sweep.last) {
            sweep.last = hits;
            std::vector<NodeRef> nodes;
            for (uint32_t id : sweep.base) nodes.push_back(findNode(id));
            for (uint32_t id : hits) nodes.push_back(findNode(id));
            if (!primary.valid() && !nodes.empty()) primary = nodes.front();
            selectNodes(nodes, primary);
            if (primary.valid()) m_outAnchor = nodeId(primary);
        }
        const ImVec2 a(std::min(sweep.x0, m.x), y0), b(std::max(sweep.x0, m.x), y1);
        dl->AddRectFilled(a, b, Fade(P().accent, 40));
        dl->AddRect(a, b, Fade(P().accent, 200), 0, 0, 1.0f);
    }

    if (move.mode >= 0 && !m_outDrag.empty()) {
        moveNodes(m_outDrag, move.mode, move.target);
        m_outDrag.clear();
    }

    // context menus
    if (openCtx) {
        NodeRef n = findNode(ctxId);
        if (n.valid() && !nodeSelected(n) && (n.kind == NODE_COLLECTION || !nodeLocked(n))) { selectNodes({n}, n); m_outAnchor = ctxId; }
        ImGui::OpenPopup("outlinerCtx");
    }
    if (openEmpty) ImGui::OpenPopup("outlinerEmptyCtx");
    if (ImGui::BeginPopup("outlinerCtx")) {
        NodeRef n = findNode(ctxId);
        if (!n.valid()) ImGui::CloseCurrentPopup();
        else if (n.kind == NODE_COLLECTION) {
            Collection& c = m_scene.collections[n.index];
            ImGui::TextDisabled("%s", c.name.c_str());
            if (ImGui::MenuItem("Rename", "F2")) startRename(c.id);
            if (ImGui::BeginMenu("Colour")) {
                for (int k = 0; k < kCollectionColorCount; ++k) {
                    ImVec2 p = ImGui::GetCursorScreenPos();
                    float sz = ImGui::GetTextLineHeight();
                    ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + sz, p.y + sz), kCollectionColors[k], 3);
                    ImGui::SetCursorScreenPos(ImVec2(p.x + sz + 6, p.y));
                    char lbl[32];
                    snprintf(lbl, sizeof(lbl), "%s##col%d", kCollectionColorNames[k], k);
                    if (ImGui::MenuItem(lbl, nullptr, c.color == k)) { pushUndo(); m_scene.collections[n.index].color = k; }
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem(c.collapsed ? "Expand" : "Collapse", "Ctrl+click")) c.collapsed = !c.collapsed;
            if (ImGui::MenuItem("Select Contents")) {
                std::vector<NodeRef> d, pick;
                nodeDescendants(c.id, d);
                for (NodeRef k : d) if (!nodeLocked(k) && !(k.kind == NODE_OBJECT && m_scene.objects[k.index].group)) pick.push_back(k);
                selectNodes(pick, pick.empty() ? NodeRef() : pick.front());
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete Collection (keep contents)", "Del")) { m_selected = -1; m_multi.clear(); m_outSel = {c.id}; deleteSelected(); }
        } else {
            const bool obj = n.kind == NODE_OBJECT;
            const bool grp = obj && m_scene.objects[n.index].group;
            ImGui::TextDisabled("%s", nodeName(n).c_str());
            if (ImGui::MenuItem("Frame", "F")) frameSelection();
            if (n.kind != NODE_SUN && ImGui::MenuItem("Rename", "F2")) startRename(ctxId);
            if (obj && ImGui::MenuItem("Duplicate", "Ctrl+D")) duplicateSelected();
            if ((obj || n.kind == NODE_LIGHT) && ImGui::MenuItem("Group", "Ctrl+G")) groupSelection();
            if (grp && ImGui::MenuItem("Ungroup")) ungroup(ctxId);
            if (nodeParent(n) && ImGui::MenuItem("Unparent")) moveNodes(selectedNodeIds(), 3, 0);
            if (ImGui::BeginMenu("Move to Collection")) {
                if (ImGui::MenuItem("None (root)")) moveNodes(selectedNodeIds(), 3, 0);
                for (const Collection& c : m_scene.collections)
                    if (ImGui::MenuItem(c.name.c_str())) moveNodes(selectedNodeIds(), 2, c.id);
                ImGui::Separator();
                if (ImGui::MenuItem("New Collection")) {
                    pushUndo();
                    uint32_t cid = addCollection("Collection", (int)(m_scene.collections.size() % kCollectionColorCount));
                    size_t depth = m_undo.size();
                    moveNodes(selectedNodeIds(), 2, cid);
                    if (m_undo.size() > depth) m_undo.pop_back();
                    startRename(cid);
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (n.kind != NODE_SUN && ImGui::MenuItem("Delete", "Del")) deleteSelected();
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("outlinerEmptyCtx")) {
        if (ImGui::MenuItem("New Collection")) {
            pushUndo();
            uint32_t c = addCollection("Collection", (int)(m_scene.collections.size() % kCollectionColorCount));
            std::vector<uint32_t> sel = selectedNodeIds();   // the selection goes in it (one undo step)
            if (!sel.empty()) { size_t depth = m_undo.size(); moveNodes(sel, 2, c); if (m_undo.size() > depth) m_undo.pop_back(); }
            startRename(c);
        }
        if (ImGui::MenuItem("Group Selection", "Ctrl+G", false, !selectedNodeIds().empty())) groupSelection();
        if (ImGui::BeginMenu("Add Object")) {
            for (const PartDef& p : parts())
                if (ImGui::MenuItem(toUtf8(p.name).c_str())) addPartFromLibrary(p);
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }
    ImGui::EndChild();
    dl = ImGui::GetWindowDrawList();
}

// --------------------------------------------------------------------------- viewport (former Atelier graph CanvasHeader + Canvas + Footer)

void App::atlViewport(float x, float y, float w, float h) {
    const bool halo = P().skin == 3;
    // header (Halo: none, the tools float over the picture)
    R hr = {x, y, w, 35 * s};
    if (!halo) {
    if (P().skin == 2) afx::FloatBar(dl, hr.min(), hr.max(), s);   // Corail: floating tool bar with its shadow
    else if (P().skin == 3) {   // Halo: flat strip with its corner mark
        Box(hr, P().panel, 2, true);
        afx::CornerTick(dl, hr.min(), 8 * s, afx::Alpha(P().accent, 0.8f));
    } else if (P().skin) {   // header strip of the pack (header gradient, border, light edge)
        afx::Gradient(dl, hr.min(), hr.max(), afx::col::headerTop, afx::col::headerBottom, 4 * s);
        dl->AddRect(hr.min(), hr.max(), P().border, 4 * s);
        afx::TopEdge(dl, hr.min(), hr.max(), s, IM_COL32(181, 186, 192, 255), 0.10f);
    } else Box(hr, P().panel, 3, true);
    Glyph(Icon::Scene, hr.x + 12 * s, hr.y + 11 * s, 13, P().accent);
    std::string name = m_scene.name.empty() ? "Scene" : m_scene.name;
    Text(hr.x + 34 * s, hr.y + 11 * s, Fit(name, 140 * s, 12), P().text, 12);
    float nx = hr.x + 34 * s + std::min(TextSize(name, 12).x, 140 * s) + 12 * s;
    Glyph(Icon::ChevronRight, nx, hr.y + 13 * s, 9, P().muted);
    Text(nx + 16 * s, hr.y + 12 * s, std::string("Viewport · ") + paneCameraName(m_activePane), P().muted, 11);
    // tools (right to left): stats, grid, frame | snapping, scale, rotate, move. Camera, display, renderer and
    // axes are menus inside the view (atlViewMenus).
    float bx = hr.x + hr.w - 8 * s, by = hr.y + 6 * s;
    auto right = [&](float bw) { bx -= bw; float r0 = bx; bx -= 6 * s; return r0; };
    if (Button("atlStats", {right(28 * s), by, 28 * s, 24 * s}, "", Icon::Monitor, m_showStats, true)) m_showStats = !m_showStats;
    Tip("Viewport statistics");
    if (Button("atlGrid", {right(28 * s), by, 28 * s, 24 * s}, "", Icon::Grid, th::settings().grid.visible, true)) toggleGrid();
    Tip("Grid: show / hide (Shift+click: Grid Options)");
    {
        const bool placing = m_game && m_game->placementMode();
        if (Button("atlVillage", {right(28 * s), by, 28 * s, 24 * s}, "", Icon::Cube, placing, true) && m_game) m_game->togglePlacement();
        Tip("RTS village: place houses on the grid (stop Play first)");
    }
    if (Button("atlFrame", {right(28 * s), by, 28 * s, 24 * s}, "", Icon::Home, false, true)) frameSelection();
    Tip("Frame selection (F)");
    {   // layout of the view (Panes.cpp): single, two, 2 x 2, 3 x 3, 4 x 4; Space: the pane under the mouse alone
        const R lb = {right(28 * s), by, 28 * s, 24 * s};
        const bool clicked = Hit("atlLayout", lb) || (m_frameCounter == 10 && m_opt.openMenu == "atlLayout");   // --open-menu: captures
        const bool hov = ImGui::IsItemHovered();
        const bool split = m_panes.size() > 1;
        Box(lb, split ? P().select : (hov ? P().hover : P().raised), 3, true);
        LayoutGlyph(dl, layoutCols(m_layout), layoutRows(m_layout), ImVec2(lb.x + 7 * s, lb.y + 5 * s), ImVec2(14 * s, 14 * s), split ? P().accent : P().text);
        Tip("View layout: single, two, 2 x 2, 3 x 3, 4 x 4 (Space: the pane under the mouse alone, and back)");
        if (clicked) ImGui::OpenPopup("atlLayoutMenu");
        ImGui::SetNextWindowPos(ImVec2(lb.x + lb.w, lb.y + lb.h + 4 * s), ImGuiCond_Always, ImVec2(1, 0));
        if (ImGui::BeginPopup("atlLayoutMenu")) {
            ImGui::SetWindowFontScale(1.4f * ts());
            ImDrawList* pd = ImGui::GetWindowDrawList();
            for (int l = 0; l < kLayoutCount; ++l) {
                ImGui::PushID(l);
                if (ImGui::MenuItem((std::string("       ") + layoutName(l)).c_str(), nullptr, m_layout == l)) setLayout(l);
                ImGui::PopID();
                const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
                const float gh = std::floor((b.y - a.y) * 0.62f);
                LayoutGlyph(pd, layoutCols(l), layoutRows(l), ImVec2(a.x + 2 * s, std::floor(a.y + (b.y - a.y - gh) * 0.5f)), ImVec2(std::floor(gh * 1.45f), gh),
                            m_layout == l ? P().accent : P().text);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Maximize the Active Pane", "Space", m_paneMax, split)) togglePaneMax(false);
            ImGui::EndPopup();
        }
    }
    bx -= 6 * s;
    if (Button("atlSnap", {right(28 * s), by, 28 * s, 24 * s}, "", Icon::Magnet, m_magnet, true)) toggleSnap();
    Tip("Snapping always on (else hold Shift while dragging) · Shift+click: Snapping Options");
    {   // Place: the selection dragged over the surfaces of the scene
        const R pb = {right(28 * s), by, 28 * s, 24 * s};
        const bool clicked = Hit("atlPlace", pb);
        const bool hov = ImGui::IsItemHovered();
        Box(pb, m_surfacePlace ? P().select : (hov ? P().hover : P().raised), 3, true);
        const ImU32 c = m_surfacePlace ? P().accent : P().text;
        const ImVec2 o(pb.x + pb.w * 0.5f, pb.y + pb.h * 0.5f);   // a box resting on a surface, an arrow down to it
        dl->AddLine(ImVec2(o.x - 8 * s, o.y + 6 * s), ImVec2(o.x + 8 * s, o.y + 6 * s), c, 1.4f * s);
        dl->AddRect(ImVec2(o.x - 1 * s, o.y - 1 * s), ImVec2(o.x + 6 * s, o.y + 6 * s), c, 0, 0, 1.2f * s);
        dl->AddLine(ImVec2(o.x - 5 * s, o.y - 7 * s), ImVec2(o.x - 5 * s, o.y + 3 * s), c, 1.2f * s);
        dl->AddTriangleFilled(ImVec2(o.x - 7.5f * s, o.y + 1 * s), ImVec2(o.x - 2.5f * s, o.y + 1 * s), ImVec2(o.x - 5 * s, o.y + 5 * s), c);
        if (clicked) m_surfacePlace = !m_surfacePlace;
        Tip("Place: drag a selected object over the surfaces of the scene (Snapping Options: turned to the surface)");
    }
    {   // the points of the grid the absolute snapping takes: intersections, centres of the cells, middles of the edges
        static const char* ids[3] = {"atlSnapInter", "atlSnapCenter", "atlSnapEdge"};
        static const char* tips[3] = {"Snap to the grid: intersections of the lines (click again: relative steps)",
                                      "Snap to the grid: centres of the cells (click again: relative steps)",
                                      "Snap to the grid: middles of the edges (click again: relative steps)"};
        SnapOptions& so = th::settings().snap;
        for (int k = 2; k >= 0; --k) {
            const R sb = {right(26 * s), by, 26 * s, 24 * s};
            const bool clicked = Hit(ids[k], sb);
            const bool hov = ImGui::IsItemHovered();
            const bool on = m_snapAbsolute && so.gridPoint == k;
            Box(sb, on ? P().select : (hov ? P().hover : P().raised), 3, true);
            const ImU32 c = on ? P().accent : Fade(P().text, 170);
            // a cell of the grid and its snap point(s)
            const float x0 = sb.x + 6 * s, y0 = sb.y + 5 * s, e = 14 * s;
            dl->AddRect(ImVec2(x0, y0), ImVec2(x0 + e, y0 + e), Fade(c, 150), 0, 0, 1.0f * s);
            auto pt = [&](float fx, float fy) { dl->AddCircleFilled(ImVec2(x0 + fx * e, y0 + fy * e), 2.2f * s, c, 8); };
            if (k == 0) { pt(0, 0); pt(1, 0); pt(0, 1); pt(1, 1); }
            else if (k == 1) pt(0.5f, 0.5f);
            else { pt(0.5f, 0); pt(0, 0.5f); pt(1, 0.5f); pt(0.5f, 1); }
            if (clicked) {
                if (on) m_snapAbsolute = false;
                else { so.gridPoint = k; m_snapAbsolute = true; }
                if (!m_themeTestOnly) th::saveSettings(m_exeDir + L"\\wzx_settings.ini");
            }
            Tip(tips[k]);
            if (k > 0) bx += 4 * s;   // (the three side by side)
        }
    }
    bx -= 6 * s;
    {   // Advanced Scale: the three-colour button (face, edge and corner handles)
        const R ab = {right(28 * s), by, 28 * s, 24 * s};
        const bool clicked = Hit("atlAdvScale", ab);
        const bool hov = ImGui::IsItemHovered();
        Box(ab, m_advScale ? P().select : (hov ? P().hover : P().raised), 3, true);
        const AdvScaleOptions& ao = th::settings().advScale;
        const ImVec4 cols[3] = {ao.colorFace, ao.colorEdgeHandle, ao.colorCorner};
        for (int i = 0; i < 3; ++i) {
            const ImVec2 c(ab.x + ab.w * 0.5f + (i - 1) * 6.0f * s, ab.y + ab.h * 0.5f + (1 - i) * 3.0f * s);
            dl->AddRectFilled(ImVec2(c.x - 2.5f * s, c.y - 2.5f * s), ImVec2(c.x + 2.5f * s, c.y + 2.5f * s), ImGui::ColorConvertFloat4ToU32(cols[i]));
        }
        if (clicked) setAdvScale(!m_advScale);
        Tip("Advanced Scale: bounding box scaling (Alt+T)");
    }
    if (Button("atlScale", {right(28 * s), by, 28 * s, 24 * s}, "", Icon::Scale, m_tool == TOOL_SCALE && !m_advScale, true)) { m_tool = TOOL_SCALE; setAdvScale(false); }
    Tip("Scale (R)");
    if (Button("atlRotate", {right(28 * s), by, 28 * s, 24 * s}, "", Icon::Rotate, m_tool == TOOL_ROTATE && !m_advScale, true)) { m_tool = TOOL_ROTATE; setAdvScale(false); }
    Tip("Rotate (E)");
    if (Button("atlMove", {right(28 * s), by, 28 * s, 24 * s}, "", Icon::Move, m_tool == TOOL_MOVE && !m_advScale, true)) { m_tool = TOOL_MOVE; setAdvScale(false); }
    Tip("Move (W)");
    }   // !halo

    // canvas: the panes of the layout (Panes.cpp). The active one is the renderer's image with the input, the tools and
    // the overlays below; drawPanes shows the others (raster views of their camera).
    const R area = {x, y + 40 * s, w, h - 40 * s - 27 * s};
    const bool canvasHovered = ImGui::IsWindowHovered() && !(m_game && (m_game->graphOpen() || m_game->rtsCameraOn()));
    splitInput({area.x, area.y, area.w, area.h}, canvasHovered);   // the lines between the panes first
    if (!m_splitHot) paneInput(canvasHovered);
    const ScreenRect ar = layoutPanes({area.x, area.y, area.w, area.h});
    R r = {ar.x, ar.y, ar.w, ar.h};
    drawPanes();
    Box(r, P().canvas, P().skin == 2 ? 8 : 3, P().skin != 2);   // Corail: borderless rounded viewport card
    viewportCanvas(ImVec2(r.x + 1, r.y + 1), ImVec2(r.w - 2, r.h - 2));   // image, input and gizmo; overlays below
    dl = ImGui::GetWindowDrawList();
    ImGui::PushClipRect(r.min(), r.max(), true);   // (ImGui's own: the overlay buttons of a small pane are not clickable outside it)
    ImGuiIO& io = ImGui::GetIO();
    if (m_marq.active && m_marq.moved) {
        ImVec2 a(std::min(m_marq.x0, io.MousePos.x), std::min(m_marq.y0, io.MousePos.y));
        ImVec2 b(std::max(m_marq.x0, io.MousePos.x), std::max(m_marq.y0, io.MousePos.y));
        dl->AddRectFilled(a, b, Fade(P().accent, 24));
        dl->AddRect(a, b, Fade(P().accent, 150));
    }
    // statistics box (graph overview style)
    size_t objs = 0;
    for (auto& o : m_scene.objects) if (o.visible && o.part != "WALL" && o.part != "GROUND") ++objs;
    size_t tris = m_scene.triangleCount();
    char triText[32];
    if (tris >= 1000000) snprintf(triText, sizeof(triText), "%.1f M", tris / 1e6);
    else if (tris >= 10000) snprintf(triText, sizeof(triText), "%.1f k", tris / 1e3);
    else snprintf(triText, sizeof(triText), "%zu", tris);
    const bool roomy = r.w > 330 * s && r.h > 210 * s;   // (a small pane of a layout: no view cube, no statistics)
    if (m_showStats && roomy && r.h > 300 * s) {
        // (text at 11, as the panels: 9 was too small to read)
        R st = {r.x + r.w - 214 * s, r.y + 116 * s, 200 * s, 158 * s};   // under the view cube
        Box(st, P().minimap, 4, true);
        Text(st.x + 12 * s, st.y + 11 * s, "VIEWPORT", P().accent, 10);
        char v[6][32];
        snprintf(v[0], 32, "%.0f FPS", 1000.0f / std::max(m_frameMs, 0.1f));
        snprintf(v[1], 32, "%.1f ms", m_frameMs);
        const bool gpuRow = m_rs.raster || renderMode() == 4;   // real-time modes: GPU time, else the samples
        if (renderMode() == 4) snprintf(v[2], 32, "%.2f (RC %.2f)", m_renderer.gpuFrameMs(), m_renderer.splitMs());
        else if (m_rs.raster) snprintf(v[2], 32, "%.2f ms", m_renderer.gpuFrameMs());
        else snprintf(v[2], 32, "%u / %u", std::min<uint32_t>(m_renderer.sampleCount(), m_rs.maxSamples), m_rs.maxSamples);
        snprintf(v[3], 32, "%s", triText);
        snprintf(v[4], 32, "%zu · %zu", objs, m_scene.lights.size());
        const uint32_t aa = m_renderer.msaaSamples();
        if (aa > 1) snprintf(v[5], 32, m_renderer.msaaTemporal() ? "MSAA x%u (%ux2)" : "MSAA x%u", aa, aa / 2); else snprintf(v[5], 32, "Off");
        const char* names[] = {"Frame rate", "Frame time", (m_rs.raster || renderMode() == 4) ? "GPU" : "Samples", "Triangles", "Objects · lights", "Anti-aliasing"};
        for (int i = 0; i < 6; ++i) {
            Text(st.x + 12 * s, st.y + (34 + i * 19) * s, names[i], P().muted, 11);
            Text(st.x + 112 * s, st.y + (34 + i * 19) * s, v[i], P().text, 11);
        }
    }
    // view cube (top right): turns with the camera, world axes X / Y / Z; a click on a face looks along it
    if (roomy) {
        float3 cr, cu, cf;
        m_scene.camera.basis(cr, cu, cf);
        const float hs = 11 * s;   // half size of the cube; the axes reach 3 times further, their letter just after
        ImVec2 c(r.x + r.w - 56 * s, r.y + 58 * s);
        auto scr = [&](const float3& v) { return ImVec2(c.x + dot(v, cr) * hs, c.y - dot(v, cu) * hs); };
        R cubeR = {c.x - 48 * s, c.y - 48 * s, 96 * s, 96 * s};
        bool clicked = Hit("##viewCube", cubeR);
        bool hov = ImGui::IsItemHovered();
        ImVec2 m = ImGui::GetIO().MousePos;
        const ImU32 axCol[3] = {IM_COL32(236, 110, 110, 255), IM_COL32(120, 214, 140, 255), IM_COL32(100, 156, 245, 255)};
        const char* axName[3] = {"X", "Y", "Z"};
        auto unit = [](int a) { return float3(a == 0 ? 1.0f : 0.0f, a == 1 ? 1.0f : 0.0f, a == 2 ? 1.0f : 0.0f); };
        const float tip = 3.0f, label = 3.6f;   // end of an axis and its letter, in half sizes of the cube
        int hotAxis = -1;
        for (int a = 0; a < 3 && hov; ++a) {
            ImVec2 e = scr(unit(a) * label);
            if (std::hypot(m.x - e.x, m.y - e.y) < 8.0f * s) hotAxis = a;
        }
        auto axisCol = [&](int a, bool front) { return hotAxis == a ? IM_COL32(255, 255, 255, 255) : (front ? axCol[a] : Fade(axCol[a], 120)); };
        auto drawLetter = [&](int a, bool front) {
            ImVec2 e = scr(unit(a) * label), ts = TextSize(axName[a], 10);
            TextBold(e.x - ts.x * 0.5f, e.y - ts.y * 0.5f, axName[a], axisCol(a, front), 10);
        };
        // seen through the translucent cube: the axes from its centre (whole when they go away), the edges behind
        for (int a = 0; a < 3; ++a) {
            const bool front = dot(unit(a), cf) <= 0;
            dl->AddLine(scr(float3(0, 0, 0)), scr(unit(a) * (front ? 1.0f : tip)), axisCol(a, front), 1.3f * s);
            if (!front) drawLetter(a, false);
        }
        auto corner = [](int i) { return float3((i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, (i & 4) ? 1.0f : -1.0f); };
        for (int i = 0; i < 8; ++i)
            for (int b = 1; b < 8; b <<= 1)
                if (!(i & b)) dl->AddLine(scr(corner(i)), scr(corner(i | b)), IM_COL32(235, 240, 250, 55), 1.0f * s);
        // faces turned to the camera (a convex cube: they do not overlap on screen)
        int hotFace = -1;
        for (int f = 0; f < 6; ++f) {
            int a = f / 2;
            float sg = (f & 1) ? -1.0f : 1.0f;
            float3 n = unit(a) * sg;
            if (dot(n, cf) > -0.02f) continue;
            float3 u = unit((a + 1) % 3), v = unit((a + 2) % 3);
            ImVec2 q[4] = {scr(n - u - v), scr(n + u - v), scr(n + u + v), scr(n - u + v)};
            bool in = false;
            if (hov) {
                int pos = 0, neg = 0;
                for (int i = 0; i < 4; ++i) {
                    ImVec2 e0 = q[i], e1 = q[(i + 1) % 4];
                    float cz = (e1.x - e0.x) * (m.y - e0.y) - (e1.y - e0.y) * (m.x - e0.x);
                    if (cz > 0) ++pos; else if (cz < 0) ++neg;
                }
                in = pos == 0 || neg == 0;
            }
            if (in) hotFace = f;
            float shade = 0.5f + 0.5f * -dot(n, cf);
            dl->AddConvexPolyFilled(q, 4, in ? IM_COL32(90, 160, 255, 90) : IM_COL32(170, 190, 225, (int)(10 + 18 * shade)));   // translucent
            dl->AddPolyline(q, 4, IM_COL32(245, 248, 255, in ? 245 : 200), ImDrawFlags_Closed, 1.0f * s);   // thin white edges
        }
        for (int a = 0; a < 3; ++a)   // axes coming towards the camera: out of the cube, over its faces
            if (dot(unit(a), cf) <= 0) {
                dl->AddLine(scr(unit(a)), scr(unit(a) * tip), axisCol(a, true), 1.3f * s);
                drawLetter(a, true);
            }
        if (hotAxis >= 0) hotFace = hotAxis * 2;   // a letter: the view from that axis (as its face)
        if (hov) Tip(hotFace >= 0 ? "Look along this axis" : "View cube");
        if (clicked && hotFace >= 0 && !orbitLocked()) {   // (the orthographic cameras keep their direction)
            // +X right, -X left, +Y top, -Y bottom, +Z front, -Z back (the camera sits on the face's side)
            Camera& cam = m_scene.camera;
            const float yaws[6] = {kPi * 0.5f, -kPi * 0.5f, cam.yaw, cam.yaw, 0.0f, kPi};
            const float pitches[6] = {0, 0, radians(89.0f), radians(-89.0f), 0, 0};
            cam.yaw = yaws[hotFace];
            cam.pitch = pitches[hotFace];
            cameraChanged();
        }
    }
    // grid numbers along the X and Z axes (Grid Options), one per grid line
    if (m_rs.editorGrid > 0 && th::settings().grid.numbers && th::settings().grid.axes) {
        const float every = m_rs.editorGrid, L = m_rs.editorGridAxis;
        const int n = std::min(40, (int)std::floor(L / every * 0.92f));
        for (int i = -n; i <= n; ++i) {
            if (i == 0) continue;
            char t[32];
            snprintf(t, sizeof t, "%g", i * every);
            for (int a = 0; a < 2; ++a) {
                float2 sp;
                const float3 w = a == 0 ? float3(i * every, 0, 0) : float3(0, 0, i * every);
                if (!project(w, sp) || sp.x < r.x || sp.y < r.y || sp.x > r.x + r.w || sp.y > r.y + r.h) continue;
                Text(sp.x + 3 * s, sp.y + 1 * s, t, IM_COL32(205, 210, 222, 150), 9);
            }
        }
    }
    // labels at the ends of the world axes of the editor grid
    if (m_rs.editorGrid > 0 && th::settings().grid.axes && th::settings().grid.arrows) {
        const float L = m_rs.editorGridAxis;
        const float3 ends[3] = {float3(L * 1.04f, 0, 0), float3(0, L * 1.05f, 0), float3(0, 0, L * 1.04f)};   // past the arrows (EditorGrid.hlsl)
        const ImU32 cols[3] = {IM_COL32(238, 96, 96, 235), IM_COL32(110, 220, 130, 235), IM_COL32(96, 146, 252, 235)};
        const char* names[3] = {"X", "Y", "Z"};
        for (int a = 0; a < 3; ++a) {
            float2 sp;
            if (!project(ends[a], sp) || sp.x < r.x || sp.y < r.y || sp.x > r.x + r.w || sp.y > r.y + r.h) continue;
            ImVec2 ts = TextSize(names[a], 14);
            Text(sp.x - ts.x * 0.5f, sp.y - ts.y * 0.5f, names[a], cols[a], 14);
        }
    }
    atlViewMenus(r.x + 10 * s, r.y + 10 * s, r.w - (roomy ? 120 : 20) * s);   // (the view cube on their right)
    drawAdvScaleMenu(r.x, r.y, r.w, r.h);   // Advanced Scale: menu (bottom left) and modifier hints
    // path tracing progress
    if (!m_rs.raster) {
        float prog = clampf((float)m_renderer.sampleCount() / m_rs.maxSamples, 0, 1);
        ImVec2 a(r.x + r.w * 0.5f - 120 * s, r.y + r.h - 10 * s);
        dl->AddRectFilled(a, ImVec2(a.x + 240 * s, a.y + 3 * s), Fade(P().bg, 160), 2 * s);
        dl->AddRectFilled(a, ImVec2(a.x + 240 * s * prog, a.y + 3 * s), prog >= 1 ? kGreen : P().accent, 2 * s);
    }
    drawCameraPreview(r.x, r.y, r.w, r.h);   // the selected (or pinned) camera, bottom right
    ImGui::PopClipRect();
    drawViewMenu(ImGui::GetForegroundDrawList());   // Space held
    // footer (former Atelier graph Footer): state of the tools and of the renderer
    R fr = {x, area.y + area.h + 3 * s, w, 24 * s};
    if (P().skin == 2) afx::FloatBar(dl, fr.min(), fr.max(), s);   // Corail: floating status bar
    else if (P().skin) { Box(fr, P().skin == 3 ? P().bg : afx::col::footer, 0); dl->AddLine(ImVec2(fr.x, fr.y + 0.5f), ImVec2(fr.x + fr.w, fr.y + 0.5f), P().border); }
    else Box(fr, P().bg, 0);
    float fx = fr.x + 9 * s;
    const char* toolNames[] = {"Select", "Move", "Rotate", "Scale"};
    const ImU32 toolCols[] = {P().text, IM_COL32(92, 192, 213, 255), IM_COL32(219, 166, 100, 255), IM_COL32(178, 146, 223, 255)};
    for (int i = 0; i < 4; ++i) {
        bool on = (int)m_tool == i;
        dl->AddCircleFilled(ImVec2(fx + 4 * s, fr.y + 12 * s), 3 * s, on ? toolCols[i] : Fade(P().muted, 110));
        Text(fx + 13 * s, fr.y + 6 * s, toolNames[i], on ? P().text : P().muted, 9);
        fx += 61 * s;
    }
    dl->AddCircleFilled(ImVec2(fx + 4 * s, fr.y + 12 * s), 3 * s, m_magnet ? kGreen : Fade(P().muted, 110));
    Text(fx + 13 * s, fr.y + 6 * s, m_magnet ? "Snapping" : "Free", P().muted, 9);
    std::string info = std::to_string(objs) + " objects  /  " + triText + " triangles";
    Text(fr.x + fr.w - 290 * s, fr.y + 6 * s, info, P().muted, 9);
    static const char* modeNames[] = {"●  Real time", "●  Metal iOS", "●  Progressive PT", "●  GI probes", "●  Split RC"};
    Text(fr.x + fr.w - 116 * s, fr.y + 6 * s, modeNames[renderMode()], m_rs.raster ? kGreen : P().accent, 9);
}

// --------------------------------------------------------------------------- Advanced Scale menu

// Menu of the Advanced Scale tool at the bottom left of the view (when an object is selected) and the modifier hints at
// the bottom: Corner Handles, Show in Game View, the eyedropper of the box orientation (several objects) and its reset,
// the settings.
void App::drawAdvScaleMenu(float x, float y, float w, float h) {
    if (!m_advScale) return;
    const AdvScaleOptions& opt = th::settings().advScale;
    if (opt.showHints) {
        const char* keys[4] = {"Ctrl", "Shift", "Alt", "V"};
        const char* what[4] = {"Edge Handles", "Uniform Scale", "2-Side Scale", "Vertex Snap"};
        float total = 0;
        for (int i = 0; i < 4; ++i) total += TextSize(keys[i], 10).x + TextSize(what[i], 10).x + 34 * s;
        float hx = x + (w - total) * 0.5f, hy = y + h - 26 * s;
        for (int i = 0; i < 4; ++i) {
            const ImVec2 ks = TextSize(keys[i], 10);
            Box({hx, hy, ks.x + 10 * s, 17 * s}, Fade(P().bg, 200), 3, true);
            Text(hx + 5 * s, hy + 2 * s, keys[i], P().text, 10);
            hx += ks.x + 16 * s;
            Text(hx, hy + 2 * s, what[i], P().muted, 10);
            hx += TextSize(what[i], 10).x + 18 * s;
        }
    }
    if (m_selected < 0) return;
    const float pw = 214 * s, ph = 96 * s;
    const R pr = {x + 10 * s, y + h - opt.menuOffset * s - ph - (opt.showHints ? 26 * s : 0), pw, ph};
    Box(pr, Fade(P().panel, 235), 4, true);
    ImGui::SetNextItemAllowOverlap();   // (the widgets drawn over it stay clickable)
    Hit("##advMenuBg", pr);             // the panel takes the clicks from the view
    Text(pr.x + 10 * s, pr.y + 8 * s, "Advanced Scale", P().text, 11);
    if (Button("advSettings", {pr.x + pw - 30 * s, pr.y + 5 * s, 22 * s, 20 * s}, "", Icon::Settings, m_showAdvSettings, true)) m_showAdvSettings = true;
    Tip("Advanced Scale Settings");
    auto check = [&](const char* id, const char* label, bool* v, float cy) {
        ImGui::SetCursorScreenPos(ImVec2(pr.x + 9 * s, cy));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2 * s, 2 * s));
        Check(id, v);
        ImGui::PopStyleVar();
        Text(pr.x + 32 * s, cy + 2 * s, label, P().text, 10.5f);
    };
    check("##advCorners", "Edge Handles", &m_advCorners, pr.y + 28 * s);
    check("##advPlay", "Show in Game View", &m_advShowInPlay, pr.y + 48 * s);
    // eyedropper: the orientation of the box of several objects; X: back to the world axes
    std::string label = "Get Orientation";
    if (m_advRefId) {
        NodeRef n = findNode(m_advRefId);
        if (n.kind == NODE_OBJECT) label = m_scene.objects[n.index].name;
        else m_advRefId = 0;
    }
    const bool several = gizmoTargets().size() > 1 || (m_selected >= 0 && m_scene.objects[m_selected].group);
    if (Button("advPick", {pr.x + 8 * s, pr.y + 70 * s, pw - 50 * s, 20 * s}, Fit(label, pw - 90 * s, 10).c_str(), Icon::Pin, m_advPicking, false, several)) m_advPicking = !m_advPicking;
    Tip("Eyedropper: click an object, the box takes its orientation (several objects)");
    if (Button("advWorld", {pr.x + pw - 38 * s, pr.y + 70 * s, 30 * s, 20 * s}, "x", Icon::World, false, true)) { m_advRefId = 0; m_advPicking = false; markDirty(); }
    Tip("Box orientation: back to the world axes");
}

// --------------------------------------------------------------------------- menus inside the view

// Floating drop-down menus at the top left of the view: camera, display, renderer and manipulator axes, the grid and
// Persp / RenderCam. When the row does not fit in the room of the pane (a small pane of a layout), the display,
// renderer and axes menus show their icon only (their value in the tooltip).
void App::atlViewMenus(float x, float y, float room) {
    const float menuScale = 1.4f * ts();
    static const char* displays[] = {"Shaded", "Wireframe", "Wireframe Override"};
    static const char* modes[] = {"Lit", "Metal iOS", "Progressive", "GI", "Split RC"};
    static const char* axes[] = {"World", "Object", "Parent"};
    const std::string camName = paneCameraName(m_activePane);
    const char* display = displays[std::clamp(m_rs.wireframe, 0, 2)];
    const char* mode = modes[std::clamp(renderMode(), 0, 4)];
    const char* axis = axes[std::clamp(m_axisMode, 0, 2)];
    auto pillW = [&](const std::string& l) { return TextSize(l, 12).x + 52 * s; };
    const float rowW = pillW(camName) + pillW(display) + pillW(mode) + pillW(axis) + TextSize("Grid", 12).x + 38 * s +
                       TextSize("Persp", 11).x + TextSize("RenderCam", 11).x + 40 * s + 36 * s;
    const bool compact = rowW > room;
    float px = x;
    auto pill = [&](const char* id, const std::string& label, Icon icon, const char* tip, bool iconOnly = false) {
        float w = iconOnly ? 46 * s : pillW(label);
        R b = {px, y, w, 24 * s};
        if (DropButton(id, b, iconOnly ? "" : label.c_str(), icon) || (m_frameCounter == 10 && m_opt.openMenu == id)) ImGui::OpenPopup(id);   // --open-menu: captures
        if (iconOnly) Tip((label + "  ·  " + tip).c_str());
        else Tip(tip);
        ImGui::SetNextWindowPos(ImVec2(px, y + 28 * s));
        px += w + 6 * s;
        bool open = ImGui::BeginPopup(id);
        if (open) ImGui::SetWindowFontScale(menuScale);
        return open;
    };
    if (pill("vmCamera", camName, Icon::Camera, "Camera: what this view looks through")) {
        cameraMenu(m_activePane);
        if (ImGui::MenuItem("Look Through Selected", nullptr, false, m_selected >= 0 && m_scene.objects[m_selected].camera)) lookThroughSelected();
        if (ImGui::MenuItem("New Camera from View")) createCameraFromView(false);
        ImGui::Separator();
        if (ImGui::MenuItem("Frame Selection", "F")) frameSelection();
        ImGui::MenuItem("Turntable", "T", &m_turntable);
        Camera& vc = m_scene.camera;
        if (!orbitLocked() && ImGui::MenuItem("Orthographic", nullptr, vc.ortho)) {
            // the same size of view at the pivot, in the other projection
            const float tanH = std::tan(vc.fovY * 0.5f);
            if (vc.ortho) vc.distance = clampf(vc.orthoHeight * 0.5f / std::max(tanH, 1e-3f), 0.5f, 2000.0f);
            else vc.orthoHeight = 2.0f * vc.distance * tanH;
            vc.ortho = !vc.ortho;
            cameraChanged();
        }
        if (ImGui::MenuItem("Depth of Field", nullptr, vc.dof, !vc.ortho)) { vc.dof = !vc.dof; cameraChanged(); }
        ImGui::SetNextItemWidth(170 * s);
        if (vc.ortho) {
            if (ImGui::SliderFloat("View size", &vc.orthoHeight, 0.1f, 1000.0f, "%.2f", ImGuiSliderFlags_Logarithmic)) cameraChanged();
        } else {
            float fov = degrees(vc.fovY);
            if (ImGui::SliderFloat("Field of view", &fov, 10.0f, 100.0f, "%.0f°")) { vc.fovY = radians(fov); cameraChanged(); }
        }
        ImGui::SetNextItemWidth(170 * s);
        ImGui::SliderFloat("Navigation speed", &m_camSpeed, 0.1f, 5.0f, "%.1f");
        ImGui::EndPopup();
    }
    if (pill("vmDisplay", display, Icon::Cube, "Display", compact)) {
        for (int i = 0; i < 3; ++i)
            if (ImGui::MenuItem(displays[i], nullptr, m_rs.wireframe == i)) { m_rs.wireframe = i; markDirty(); }
        ImGui::Separator();
        if (ImGui::MenuItem("Grid", nullptr, th::settings().grid.visible)) {
            toggleGrid();   // (Shift: the options)
        }
        if (ImGui::MenuItem("Grid Options...")) m_showGridOptions = true;
        bool gridOn = false;
        for (const Object& o : m_scene.objects)
            if (o.part == "GROUND" && o.material >= 0 && o.material < (int)m_scene.materials.size()) gridOn = (m_scene.materials[o.material].flags & 1u) != 0;
        if (ImGui::MenuItem("Ground Grid (scene)", nullptr, gridOn)) toggleGroundGrid();
        ImGui::MenuItem("Manipulators", nullptr, &m_showGizmo);
        ImGui::MenuItem("Statistics", nullptr, &m_showStats);
        ImGui::Separator();
        if (ImGui::MenuItem("Selection: Blue Glow", nullptr, m_rs.selectionStyle == 1)) { m_rs.selectionStyle = 1; markDirty(); }
        if (ImGui::MenuItem("Selection: Outline", nullptr, m_rs.selectionStyle == 0)) { m_rs.selectionStyle = 0; markDirty(); }
        ImGui::Separator();
        if (ImGui::BeginMenu("Anti-aliasing")) {
            ImGui::SetWindowFontScale(menuScale);
            // hardware multisampling of the raster passes; x16 without 16 hardware samples: 8 samples over two frames
            // (temporal, while the view does not move); a count out of reach is greyed out
            const int counts[4] = {1, 4, 8, 16};
            const char* names[4] = {"Off", "MSAA x4", "MSAA x8", "MSAA x16"};
            for (int i = 0; i < 4; ++i) {
                const uint32_t n = (uint32_t)counts[i];
                const bool hw = m_renderer.msaaSupported(n), twoFrames = !hw && n > 2 && m_renderer.msaaSupported(n / 2);
                char note[48];
                snprintf(note, sizeof note, twoFrames ? "%u samples x 2 frames" : "not supported by this GPU", n / 2);
                if (ImGui::MenuItem(names[i], hw ? nullptr : note, m_msaa == counts[i], hw || twoFrames)) { m_msaa = counts[i]; markDirty(); }
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }
    if (pill("vmRender", mode, Icon::Sun,
             "Renderer: Lit (real-time raster) · Metal iOS (Lit + GI probes, as the iOS build) · Progressive (path tracing) · GI (ray traced probes) · Split RC (Split Radiance Cascades, ray traced, no history)", compact)) {
        for (int i = 0; i < 5; ++i)
            if (ImGui::MenuItem(modes[i], nullptr, renderMode() == i)) setRenderMode(i);
        ImGui::EndPopup();
    }
    if (pill("vmAxes", axis, Icon::World, "Manipulator axes and steps", compact)) {
        for (int i = 0; i < 3; ++i)
            if (ImGui::MenuItem(axes[i], nullptr, m_axisMode == i)) m_axisMode = i;
        ImGui::Separator();
        ImGui::MenuItem("Snapping always (else Shift)", nullptr, &m_magnet);
        ImGui::MenuItem("Snap to grid (absolute)", nullptr, &m_snapAbsolute);
        ImGui::MenuItem("Place on Surfaces", nullptr, &m_surfacePlace);
        if (ImGui::MenuItem("Snapping Options...")) m_showSnapOptions = true;
        ImGui::SetNextItemWidth(150 * s);
        ImGui::SliderFloat("Move step", &m_stepMove, 0.05f, 10.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
        ImGui::SetNextItemWidth(150 * s);
        ImGui::SliderFloat("Rotate step", &m_stepRotate, 1.0f, 90.0f, "%.0f°");
        ImGui::SetNextItemWidth(150 * s);
        ImGui::SliderFloat("Scale step", &m_stepScale, 0.01f, 1.0f, "%.2f");
        ImGui::EndPopup();
    }
    // Grid: shows / hides the editor grid; Shift+click opens its options (the option box of Maya)
    {
        const char* label = "Grid";
        const float w = compact ? 28 * s : TextSize(label, 12).x + 38 * s;
        if (Button("vmGrid", {px, y, w, 24 * s}, label, Icon::Grid, th::settings().grid.visible, compact)) toggleGrid();
        Tip("Grid: show / hide (Shift+click: Grid Options)");
        px += w + 6 * s;
    }
    // Persp / RenderCam: the persp camera, or the render camera (made from the view when the scene has none)
    {
        const ViewPane& ap = m_panes[m_activePane];
        const int rc = renderCamera();
        const bool onPersp = ap.cam == VCAM_PERSP, onRender = rc >= 0 && ap.cam == VCAM_OBJECT && ap.object == m_scene.objects[rc].id;
        auto seg = [&](const char* id, const char* label, R b, bool on, const char* tip) {
            const bool q = Hit(id, b);
            const bool hov = ImGui::IsItemHovered();
            Box(b, on ? P().select : (hov ? P().hover : P().raised), 3, true);
            const ImVec2 ts = TextSize(label, 11);
            Text(b.x + (b.w - ts.x) * 0.5f, b.y + (b.h - ts.y) * 0.5f, label, on ? P().accent : P().text, 11);
            Tip(tip);
            return q;
        };
        const float w1 = TextSize("Persp", 11).x + 20 * s, w2 = TextSize("RenderCam", 11).x + 20 * s;
        if (seg("vmPersp", "Persp", {px, y, w1, 24 * s}, onPersp, "Look through the persp camera")) setViewAxis(VCAM_PERSP);
        if (seg("vmRenderCam", "RenderCam", {px + w1, y, w2, 24 * s}, onRender,
                rc >= 0 ? "Look through the render camera" : "Look through the render camera (made from this view)")) lookThroughRenderCam();
        px += w1 + w2 + 6 * s;
    }
    if (m_frameCounter == 10 && m_opt.openMenu == "gridOptions") m_showGridOptions = true;   // --open-menu gridOptions: captures
    if (m_frameCounter == 10 && m_opt.openMenu == "snapOptions") m_showSnapOptions = true;   // --open-menu snapOptions: captures
    if (m_frameCounter == 10 && m_opt.openMenu.rfind("advScale", 0) == 0) advScaleDemo(m_opt.openMenu);   // --open-menu advScale...: captures
    if (m_frameCounter == 10 && m_opt.openMenu == "viewMenu") {   // --open-menu viewMenu: the menu of Space held, for captures
        m_vm = ViewMenu();
        m_vm.down = m_vm.shown = true;
        m_vm.x = m_rcView.x + m_rcView.w * 0.5f;
        m_vm.y = m_rcView.y + m_rcView.h * 0.5f;
        m_vm.pane = m_activePane;
    }
}

// The cameras a pane can look through: persp, the orthographic ones, the camera objects of the scene (the render camera
// marked), and a render camera made from the view when the scene has none.
void App::cameraMenu(int pane) {
    const ViewPane p = m_panes[pane];
    if (ImGui::MenuItem("Perspective", nullptr, p.cam == VCAM_PERSP)) setPaneCamera(pane, VCAM_PERSP);
    ImGui::TextDisabled("Orthographic Views");
    for (int k = VCAM_TOP; k < VCAM_COUNT; ++k)
        if (ImGui::MenuItem(viewCamLabel(k), nullptr, p.cam == k)) setPaneCamera(pane, k);
    ImGui::Separator();
    for (size_t i = 0; i < m_scene.objects.size(); ++i) {
        const Object& o = m_scene.objects[i];
        if (!o.camera) continue;
        const std::string label = o.name + (o.camRender ? "  (render camera)" : "");
        if (ImGui::MenuItem(label.c_str(), nullptr, p.cam == VCAM_OBJECT && p.object == o.id)) setPaneCamera(pane, VCAM_OBJECT, o.id);
    }
    if (renderCamera() < 0 && ImGui::MenuItem("Render Camera (new, from the view)")) {
        activatePane(pane);
        lookThroughRenderCam();
    }
}

// The panes of the layout other than the active one: their image (raster view of their camera), frame, camera name
// and the camera icons they see; the active pane gets a thin accent frame.
void App::drawPanes() {
    if (m_panes.size() <= 1 || m_paneMax) return;
    for (int i = 0; i < (int)m_panes.size(); ++i) {
        if (i == m_activePane) continue;
        const ScreenRect& rc = m_panes[i].rc;
        if (rc.w < 2 || rc.h < 2) continue;
        const R pr = {rc.x, rc.y, rc.w, rc.h};
        const ScreenRect im = {rc.x + 1, rc.y + 1, rc.w - 2, rc.h - 2};   // its image: inside the frame, as the active pane's
        Box(pr, P().canvas, 3, true);
        if (ImTextureID t = m_gui.viewTexture(i)) dl->AddImage(t, ImVec2(im.x, im.y), ImVec2(im.x + im.w, im.y + im.h));
        dl->PushClipRect(pr.min(), pr.max(), true);
        // the camera icons seen in that pane (its camera and image rectangle for the projection)
        const Camera saveCam = m_scene.camera;
        const ScreenRect saveRc = m_rcView;
        m_scene.camera = paneCamera(i);
        m_rcView = im;
        m_iconPane = i;
        drawCameraIcons(dl);
        m_iconPane = -1;
        m_scene.camera = saveCam;
        m_rcView = saveRc;
        const std::string label = paneCameraName(i);
        const ImVec2 ts = TextSize(label, 11);
        Box({pr.x + 6 * s, pr.y + 6 * s, ts.x + 14 * s, ts.y + 6 * s}, Fade(P().bg, 190), 3, false);
        Text(pr.x + 13 * s, pr.y + 9 * s, label, P().muted, 11);
        dl->PopClipRect();
        dl->AddRect(pr.min(), pr.max(), P().border, 3 * s, 0, 1.0f * s);
    }
    const ScreenRect& a = m_panes[m_activePane].rc;
    dl->AddRect(ImVec2(a.x, a.y), ImVec2(a.x + a.w, a.y + a.h), P().accent, 3 * s, 0, 1.5f * s);
    // the lines between the panes: a grip of three dots in the middle of each piece (dragged to resize the panes), the
    // line under the mouse or dragged lit
    const ScreenRect& ar = m_paneArea;
    const int cols = layoutCols(m_layout), rows = layoutRows(m_layout);
    const ImU32 grip = Fade(P().text, 120), lit = P().accent;
    for (int k = 1; k < cols; ++k) {
        const float lx = std::floor(ar.x + splitAt(false, k) * ar.w) + 0.5f;
        if (m_splitCol == k) dl->AddLine(ImVec2(lx, ar.y), ImVec2(lx, ar.y + ar.h), lit, 2.0f * s);
        for (int r = 0; r < rows; ++r) {
            const float cy = ar.y + (splitAt(true, r) + splitAt(true, r + 1)) * 0.5f * ar.h;
            for (int dd = -1; dd <= 1; ++dd) dl->AddCircleFilled(ImVec2(lx, cy + dd * 5.0f * s), 1.3f * s, m_splitCol == k ? lit : grip, 8);
        }
    }
    for (int k = 1; k < rows; ++k) {
        const float ly = std::floor(ar.y + splitAt(true, k) * ar.h) + 0.5f;
        if (m_splitRow == k) dl->AddLine(ImVec2(ar.x, ly), ImVec2(ar.x + ar.w, ly), lit, 2.0f * s);
        for (int c = 0; c < cols; ++c) {
            const float cx = ar.x + (splitAt(false, c) + splitAt(false, c + 1)) * 0.5f * ar.w;
            for (int dd = -1; dd <= 1; ++dd) dl->AddCircleFilled(ImVec2(cx + dd * 5.0f * s, ly), 1.3f * s, m_splitRow == k ? lit : grip, 8);
        }
    }
}

// Preview of the selected (or pinned) camera at the bottom right of the active pane, as in Unreal: its picture with the
// shape of the camera's image, its name, a pin (kept when it is not selected) and Look Through.
void App::drawCameraPreview(float x, float y, float w, float h) {
    m_previewRc = {0, 0, 0, 0};
    const int pc = previewCamera();
    if (pc < 0 || (m_game && (m_game->playing() || m_game->playerMode()))) return;
    const Object& o = m_scene.objects[pc];
    const float pw = std::clamp(w * 0.28f, 150 * s, 440 * s), ph = pw / std::clamp(o.camAspect, 0.5f, 3.0f), th = 24 * s;
    if (pw > w * 0.45f || ph + th > h * 0.5f) return;   // (a small pane of a layout: Space shows it alone)
    const R box = {x + w - pw - 12 * s, y + h - ph - th - 12 * s, pw, ph + th};
    Box(box, Fade(P().panel, 240), 4, true);
    ImGui::SetNextItemAllowOverlap();   // (the buttons drawn over it stay clickable)
    Hit("##camPreviewBg", box);         // the preview takes the clicks from the view
    Text(box.x + 9 * s, box.y + 6 * s, Fit(o.name + (o.camRender ? "  ·  render camera" : ""), pw - 70 * s, 10), P().text, 10);
    const bool pinned = m_previewPin == o.id;
    if (Button("camPreviewPin", {box.x + pw - 54 * s, box.y + 3 * s, 24 * s, 18 * s}, "", Icon::Pin, pinned, true)) m_previewPin = pinned ? 0 : o.id;
    Tip(pinned ? "Unpin: the preview follows the selection" : "Pin: keep this camera in the preview");
    if (Button("camPreviewLook", {box.x + pw - 28 * s, box.y + 3 * s, 24 * s, 18 * s}, "", Icon::Camera, false, true))
        setPaneCamera(m_activePane, VCAM_OBJECT, o.id);
    Tip("Look through this camera");
    m_previewRc = {std::floor(box.x + 1 * s), std::floor(box.y + th), std::floor(pw - 2 * s), std::floor(ph - 1 * s)};   // whole pixels: the size of its image
    const R img = {m_previewRc.x, m_previewRc.y, m_previewRc.w, m_previewRc.h};
    if (ImTextureID t = m_gui.viewTexture(kPreviewSlot)) dl->AddImage(t, img.min(), img.max());
    dl->AddRect(img.min(), img.max(), IM_COL32(255, 255, 255, 40), 0, 0, 1.0f);
}

// --------------------------------------------------------------------------- bottom panel (former Atelier graph Output)

void App::atlContent(float x, float y, float w, float h) {
    AtelierState& A = m_atl;
    R r = {x, y, w, h};
    if (P().skin) afx::Panel(dl, r.min(), r.max(), s, 33 * s);   // the tab row sits in the title band
    else Box(r, P().panel, 4, true);
    const bool halo = P().skin == 3;
    const char* names[] = {"Content Browser", "Materials", "Camera Views", "Output Log"};
    const char* haloNames[] = {"Assets", "Materials", "Views", "Console"};
    Icon icons[] = {halo ? Icon::FolderOpen : Icon::Cube, Icon::Material, Icon::Camera, Icon::List};
    const float widths[] = {140, 100, 122, 108}, haloWidths[] = {96, 104, 82, 96};
    float tx = r.x + 8 * s;
    A.contentTab = std::clamp(A.contentTab, 0, 3);
    for (int i = 0; i < 4; ++i) {
        char id[24];
        snprintf(id, sizeof(id), "contentTab%d", i);
        const float tw = (halo ? haloWidths[i] : widths[i]) * s;
        if (TabButton(id, {tx, r.y + 4 * s, tw, (P().skin ? 29 : 25) * s}, halo ? haloNames[i] : names[i], icons[i], A.contentTab == i)) A.contentTab = i;
        tx += tw + 5 * s;
    }
    if (halo) {   // the graph of the scene opens in its workspace
        if (TabButton("contentTabGraph", {tx, r.y + 4 * s, 86 * s, 29 * s}, "Graph", Icon::Blueprint, false)) m_workspace = 1;
        Tip("Open the graph of this scene (F3)");
        tx += 91 * s;
    }
    std::vector<Asset> assets = assetList();
    static const char* folders[] = {"All", "Primitives", "Materials", "Scenes"};
    auto activate = [&](const Asset& a) {
        if (a.kind == 0) addPartFromLibrary(parts()[a.index]);
        else if (a.kind == 1) { if (m_selected >= 0) setPreset(a.index); else status(L"Select an object first to apply this material", 2); }
        else m_pendingScene = a.index;
    };
    // tools on the right of the tab row
    if (A.contentTab == 0) {
        float rx = r.x + r.w - 37 * s;
        if (Button("listToggle", {rx, r.y + 5 * s, 26 * s, 23 * s}, "", Icon::List, A.listMode, true)) A.listMode = !A.listMode;
        Tip("List view");
        if (Button("refreshAssets", {rx - 35 * s, r.y + 5 * s, 26 * s, 23 * s}, "", Icon::Refresh, false, true)) { A.assetSearch[0] = 0; status(std::to_wstring(assets.size()) + L" assets available"); }
        Tip("Clear the search");
        float sw = std::min(210 * s, std::max(80 * s, rx - 45 * s - tx - (halo ? 128 * s : 0)));
        Search("##atlAssetSearch", {rx - 43 * s - sw, r.y + 5 * s, sw, 23 * s}, A.assetSearch, sizeof(A.assetSearch), "Search assets...");
        if (halo) {   // Halo: the folder tree becomes a filter next to the search
            int f = A.folder;
            if (Combo("haloFolder", {rx - 43 * s - sw - 124 * s, r.y + 5 * s, 118 * s, 23 * s}, f, folders, 4, Icon::FolderOpen)) A.folder = f;
        }
    } else if (A.contentTab == 3) {
        if (Button("copyLog", {r.x + r.w - 72 * s, r.y + 5 * s, 26 * s, 23 * s}, "", Icon::List, false, true)) {
            std::string all;
            for (const LogLine& l : m_log) all += l.time + "  " + l.text + "\n";
            ImGui::SetClipboardText(all.c_str());
            status(L"Output copied to clipboard");
        }
        Tip("Copy the output log");
        if (Button("clearLog", {r.x + r.w - 37 * s, r.y + 5 * s, 26 * s, 23 * s}, "", Icon::Trash, false, true)) m_log.clear();
        Tip("Clear output messages");
    }
    R body = {r.x + 2 * s, r.y + 35 * s, r.w - 4 * s, r.h - 38 * s};

    if (A.contentTab == 0) {
        float tree = halo ? 0 : 128 * s, details = halo ? 0 : 176 * s;   // Halo: one wide grid of tiles
        // folders (graph library rows)
        R ft = {body.x + 8 * s, body.y + 2 * s, tree - 14 * s, body.h - 6 * s};
        float fy = ft.y;
        Icon folderIcons[] = {Icon::FolderOpen, Icon::Cube, Icon::Material, Icon::Scene};
        for (int f = 0; f < (halo ? 0 : 4); ++f) {
            char id[20];
            snprintf(id, sizeof(id), "folder%d", f);
            R fr = {ft.x, fy, ft.w, 26 * s};
            bool q = Hit(id, fr);
            if (A.folder == f) SelectedRow(fr);
            else if (ImGui::IsItemHovered()) Box(fr, P().raised, 3);
            if (q) A.folder = f;
            Glyph(folderIcons[f], fr.x + 8 * s, fy + 7 * s, 12, A.folder == f ? P().accent : P().muted);
            Text(fr.x + 28 * s, fy + 7 * s, folders[f], P().text, 11);
            fy += 28 * s;
        }
        if (!halo) dl->AddLine(ImVec2(body.x + tree, body.y + 2 * s), ImVec2(body.x + tree, body.y + body.h - 4 * s), P().border);
        // grid
        R grid = {body.x + tree + 2 * s, body.y, body.w - tree - details - 4 * s, body.h};
        ImGui::SetCursorScreenPos(grid.min());
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("atlAssetGrid", ImVec2(grid.w, grid.h), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
        ImGui::SetWindowFontScale(atelier::Style().layoutScale);   // child text independent of the layout size
        ImGui::PopStyleVar();
        dl = ImGui::GetWindowDrawList();
        float gy = ImGui::GetCursorScreenPos().y + 4 * s, thumb = (halo ? 80.0f : std::max(56.0f, A.thumb)) * s, gap = 9 * s;
        int cols = std::max(1, (int)((grid.w - 20 * s + gap) / (thumb + gap))), shown = 0;
        if (halo) cols = std::max(1, (int)((grid.w - 20 * s + 10 * s) / (thumb + 26 * s)));
        float gx = grid.x + 10 * s;
        for (size_t i = 0; i < assets.size(); ++i) {
            const Asset& a = assets[i];
            if (A.folder != 0 && a.kind != A.folder - 1) continue;
            if (!matches(a.name, A.assetSearch)) continue;
            ImGui::PushID((int)i);
            R card = A.listMode ? R{gx, gy, grid.w - 22 * s, 38 * s} : R{gx, gy, thumb, thumb + 24 * s};
            if (halo && !A.listMode) card = R{gx, gy, thumb + 16 * s, thumb + 38 * s};   // mockup tile: 96 x 116
            bool q = Hit("asset", card);
            bool hovered = ImGui::IsItemHovered();
            if (q) m_contentAsset = (int)i;
            if (hovered && ImGui::IsMouseDoubleClicked(0)) activate(a);
            if (a.kind != 2 && ImGui::BeginDragDropSource()) {
                int payload = a.index;
                ImGui::SetDragDropPayload(a.kind == 0 ? "WZX_ASSET" : "WZX_MATERIAL", &payload, sizeof(int));
                ImGui::TextUnformatted(a.name.c_str());
                ImGui::EndDragDropSource();
            }
            if (halo && !A.listMode) {
                // chamfered tile: picture on top, name strip below a separator; selected: main-colour outline
                const bool selA = m_contentAsset == (int)i;
                afx::Chamfer(dl, card.min(), card.max(), 8 * s, P().raised, selA ? P().accent : (hovered ? afx::halo::borderHi : P().border));
                const float nameY = card.y + card.h - 22 * s;
                dl->AddLine(ImVec2(card.x + 1, nameY), ImVec2(card.x + card.w - 1, nameY), P().border);
                drawAssetThumb(dl, a.kind, a.index, ImVec2(card.x + 14 * s, card.y + 10 * s), ImVec2(card.x + card.w - 14 * s, nameY - 8 * s), m_dpi);
                dl->PushClipRect(ImVec2(card.x, nameY), card.max(), true);
                Text(card.x + 8 * s, nameY + 5 * s, Fit(a.name, card.w - 14 * s, 9), selA ? P().text : P().muted, 9);
                dl->PopClipRect();
                Tip(a.kind == 0 ? "Click: select · double-click: add · drag to the viewport" : a.kind == 1 ? "Double-click: apply to the selection · drag onto an object" : "Double-click: open the scene");
                ImGui::PopID();
                ++shown;
                if (shown % cols == 0) { gx = grid.x + 10 * s; gy += card.h + 10 * s; }
                else gx += card.w + 10 * s;
                continue;
            }
            R tile = A.listMode ? R{gx, gy, 34 * s, 34 * s} : R{gx, gy, thumb, thumb};
            Box(tile, P().bg, 4, true);
            if (m_contentAsset == (int)i) SelectedFrame(tile);
            else if (hovered) dl->AddRect(tile.min(), tile.max(), P().muted, 4 * s);
            drawAssetThumb(dl, a.kind, a.index, ImVec2(tile.x + 5 * s, tile.y + 5 * s), ImVec2(tile.x + tile.w - 5 * s, tile.y + tile.h - 5 * s), m_dpi);
            if (A.listMode) {
                Text(gx + 44 * s, gy + 5 * s, a.name, P().text, 11);
                Text(gx + 44 * s, gy + 21 * s, std::string(a.type) + "  ·  " + a.folder, P().muted, 9);
            } else {
                dl->PushClipRect(ImVec2(gx - 4 * s, gy), ImVec2(gx + thumb + 4 * s, gy + thumb + 26 * s), true);
                Center({gx - 2 * s, gy + thumb + 5 * s, thumb + 4 * s, 16 * s}, Fit(a.name, thumb + 2 * s, 9), P().text, 9);
                dl->PopClipRect();
            }
            Tip(a.kind == 0 ? "Click: select · double-click: add · drag to the viewport" : a.kind == 1 ? "Double-click: apply to the selection · drag onto an object" : "Double-click: open the scene");
            ImGui::PopID();
            ++shown;
            if (A.listMode) gy += 43 * s;
            else if (shown % cols == 0) { gx = grid.x + 10 * s; gy += thumb + 31 * s; }
            else gx += thumb + gap;
        }
        if (!shown) Center({grid.x, grid.y + 24 * s, grid.w, 22 * s}, "No matching assets", P().muted, 11);
        ImGui::SetCursorScreenPos(ImVec2(grid.x + 2 * s, gy + thumb + 28 * s));
        ImGui::Dummy(ImVec2(1, 1));
        ImGui::EndChild();
        dl = ImGui::GetWindowDrawList();
        if (halo) return;   // no details column
        // details
        R dr = {body.x + body.w - details, body.y, details, body.h};
        dl->AddLine(ImVec2(dr.x, dr.y + 2 * s), ImVec2(dr.x, dr.y + dr.h - 4 * s), P().border);
        if (m_contentAsset < 0 || m_contentAsset >= (int)assets.size()) m_contentAsset = 0;
        const Asset& a = assets[m_contentAsset];
        dl->PushClipRect(dr.min(), dr.max(), true);
        float dx = dr.x + 12 * s, dy = dr.y + 4 * s;
        Text(dx, dy, Fit(a.name, dr.w - 24 * s, 12), P().text, 12);
        Text(dx, dy + 18 * s, a.type, P().accent, 9);
        R thumbR = {dx, dy + 36 * s, dr.w - 24 * s, std::min(76 * s, dr.h - 96 * s)};
        if (thumbR.h > 24 * s) {
            Box(thumbR, P().bg, 4, true);
            float sq = std::min(thumbR.w, thumbR.h) - 10 * s;
            drawAssetThumb(dl, a.kind, a.index, ImVec2(thumbR.x + (thumbR.w - sq) / 2, thumbR.y + 5 * s), ImVec2(thumbR.x + (thumbR.w + sq) / 2, thumbR.y + 5 * s + sq), m_dpi);
        }
        float iy = thumbR.y + std::max(thumbR.h, 0.0f) + 9 * s;
        if (a.kind == 0) {
            static std::vector<std::pair<size_t, size_t>> stats;
            if (stats.size() != parts().size()) {
                stats.clear();
                for (const PartDef& p : parts()) { MeshData md = makePartMesh(p); stats.push_back({md.triangleCount(), md.vertices.size()}); }
            }
            const char* labels[] = {"Triangles", "Vertices"};
            std::string values[] = {std::to_string(stats[a.index].first), std::to_string(stats[a.index].second)};
            for (int i = 0; i < 2; ++i) { Text(dx, iy, labels[i], P().muted, 10); Text(dx + 70 * s, iy, values[i], P().text, 10); iy += 17 * s; }
        } else {
            Text(dx, iy, a.kind == 1 ? "Material preset" : "Test scene", P().muted, 10);
        }
        dl->PopClipRect();
        ImGui::SetCursorScreenPos(thumbR.min());
        ImGui::InvisibleButton("assetLargePreview", ImVec2(std::max(1.0f, thumbR.w), std::max(1.0f, thumbR.h)));
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) activate(a);
        Tip(a.kind == 0 ? "Double-click: add to the scene" : a.kind == 1 ? "Double-click: apply to the selection" : "Double-click: open the scene");
    } else if (A.contentTab == 1) {
        ImGui::SetCursorScreenPos(body.min());
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("atlMaterials", ImVec2(body.w, body.h), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
        ImGui::SetWindowFontScale(atelier::Style().layoutScale);   // child text independent of the layout size
        ImGui::PopStyleVar();
        dl = ImGui::GetWindowDrawList();
        float mx = body.x + 12 * s, my = ImGui::GetCursorScreenPos().y + 4 * s, cwid = 112 * s, chei = std::max(96 * s, std::min(126 * s, body.h - 12 * s));
        int cur = m_selected >= 0 ? m_scene.materials[m_scene.objects[m_selected].material].preset : -1;
        for (size_t i = 0; i < presetNames().size(); ++i) {
            if (mx + cwid > body.x + body.w) { mx = body.x + 12 * s; my += chei + 9 * s; }
            R card = {mx, my, cwid, chei};
            char id[24];
            snprintf(id, sizeof(id), "matCard%zu", i);
            bool q = Hit(id, card);
            bool hv = ImGui::IsItemHovered();
            Box(card, P().bg, 4, true);
            if ((int)i == cur) SelectedFrame(card);
            else if (hv) dl->AddRect(card.min(), card.max(), P().muted, 4 * s);
            float th = chei - 46 * s;
            drawAssetThumb(dl, 1, (int)i, ImVec2(mx + (cwid - th) / 2, my + 8 * s), ImVec2(mx + (cwid + th) / 2, my + 8 * s + th), m_dpi);
            dl->PushClipRect(card.min(), card.max(), true);
            Center({mx, my + chei - 34 * s, cwid, 16 * s}, Fit(toUtf8(presetNames()[i]), cwid - 10 * s, 10), P().text, 10);
            Center({mx, my + chei - 19 * s, cwid, 14 * s}, m_selected >= 0 ? "Click to apply" : "Select an object", P().muted, 8);
            dl->PopClipRect();
            if (q && m_selected >= 0) setPreset((int)i);
            else if (q) status(L"Select an object first to apply this material", 2);
            if (ImGui::BeginDragDropSource()) {
                int payload = (int)i;
                ImGui::SetDragDropPayload("WZX_MATERIAL", &payload, sizeof(int));
                ImGui::TextUnformatted(toUtf8(presetNames()[i]).c_str());
                ImGui::EndDragDropSource();
            }
            mx += cwid + 9 * s;
        }
        ImGui::SetCursorScreenPos(ImVec2(body.x, my + chei + 8 * s));
        ImGui::Dummy(ImVec2(1, 1));
        ImGui::EndChild();
        dl = ImGui::GetWindowDrawList();
    } else if (A.contentTab == 2) {
        float ch = std::max(50 * s, body.h - 34 * s), cw = ch * 16 / 9, vx = body.x + 12 * s, vy = body.y + 4 * s;
        for (size_t i = 0; i <= m_views.size(); ++i) {
            if (vx + cw > body.x + body.w) break;
            R c = {vx, vy, cw, ch};
            char id[24];
            snprintf(id, sizeof(id), "view%zu", i);
            bool q = Hit(id, c);
            bool hv = ImGui::IsItemHovered();
            if (i == m_views.size()) {
                Box(c, hv ? P().hover : P().raised, 4, true);
                Glyph(Icon::Plus, c.x + c.w / 2 - 8 * s, c.y + c.h / 2 - 14 * s, 16, hv ? P().accent : P().muted);
                Center({c.x, c.y + c.h / 2 + 4 * s, c.w, 16 * s}, "Add View", P().muted, 10);
                if (q) {
                    ViewBookmark vb;
                    vb.name = L"View " + std::to_wstring(m_views.size() + 1);
                    vb.cam = m_scene.camera;
                    std::vector<uint8_t> img;
                    uint32_t iw, ih;
                    if (m_renderer.readViewport(img, iw, ih)) vb.thumb = makeThumb(img, iw, ih);
                    m_views.push_back(vb);
                    m_activeView = (int)m_views.size() - 1;
                    status(L"Camera view added");
                }
                break;
            }
            ViewBookmark& v = m_views[i];
            Box(c, P().bg, 4, true);
            if (v.thumb) dl->AddImageRounded(v.thumb, ImVec2(c.x + 1, c.y + 1), ImVec2(c.x + c.w - 1, c.y + c.h - 1), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 4 * s);
            Text(c.x + 2 * s, c.y + c.h + 6 * s, std::to_string(i + 1) + "  " + toUtf8(v.name), (int)i == m_activeView ? P().text : P().muted, 10);
            dl->AddRect(c.min(), c.max(), (int)i == m_activeView ? P().accent : (hv ? P().muted : P().border), 4 * s, 0, (int)i == m_activeView ? 1.4f * s : 1.0f);
            if (q) { applyViewBookmark(v.cam); m_activeView = (int)i; }
            vx += cw + 10 * s;
        }
    } else {
        ImGui::SetCursorScreenPos(ImVec2(body.x + 8 * s, body.y));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("atlLog", ImVec2(body.w - 16 * s, body.h - 4 * s), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
        ImGui::SetWindowFontScale(atelier::Style().layoutScale);   // child text independent of the layout size
        ImGui::PopStyleVar();
        dl = ImGui::GetWindowDrawList();
        float lx = ImGui::GetCursorScreenPos().x, ly = ImGui::GetCursorScreenPos().y;
        for (const LogLine& l : m_log) {
            ImU32 c = l.level == 2 ? kAmber : (l.level == 1 ? kGreen : P().accent);
            Glyph(l.level == 2 ? Icon::Info : Icon::Check, lx + 4 * s, ly + 5 * s, 10, c);
            Text(lx + 23 * s, ly + 4 * s, l.time, P().muted, 10);
            Text(lx + 80 * s, ly + 4 * s, l.text, l.level == 2 ? kAmber : P().muted, 10);
            ly += 23 * s;
        }
        ImGui::SetCursorScreenPos(ImVec2(lx, ly + 4 * s));
        ImGui::Dummy(ImVec2(1, 1));
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
        dl = ImGui::GetWindowDrawList();
    }
}

// --------------------------------------------------------------------------- inspector (former Atelier graph Inspector)

void App::atlInspector(float x, float y, float w, float h) {
    AtelierState& A = m_atl;
    R r = {x, y, w, h};
    A.inspectorTab = std::clamp(A.inspectorTab, 0, 3);
    const char* tags[] = {"ACTOR", "LIGHTING", "RENDER", "WORLD"};
    const bool halo = P().skin == 3;
    Panel(r, Icon::Info, "Inspector", tags[A.inspectorTab], P().text);
    float by = r.y + 76 * s;
    if (halo) by = r.y + 46 * s;   // Halo: the pages are chosen in the icon rail
    else {
    const char* tabs[] = {"Actor", "Light", "Render", "World"};
    Icon is[] = {Icon::Cube, Icon::Sun, Icon::Camera, Icon::World};
    float tw = (r.w - 26 * s - 15 * s) / 4, tx = r.x + 13 * s;
    for (int i = 0; i < 4; ++i) {
        char id[24];
        snprintf(id, sizeof(id), "inspectorTab%d", i);
        if (TabButton(id, {tx, r.y + 40 * s, tw, (P().skin ? 29 : 25) * s}, tabs[i], is[i], A.inspectorTab == i)) A.inspectorTab = i;
        tx += tw + 5 * s;
    }
    if (P().skin) dl->AddLine(ImVec2(r.x + 13 * s, r.y + 69 * s), ImVec2(r.x + r.w - 13 * s, r.y + 69 * s), P().border);
    }
    g_labelRight = r.x + r.w - 21 * s;   // inner width of the inspector body (scrollbar room excluded)
    ImGui::SetCursorScreenPos(ImVec2(r.x + 13 * s, by));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("atlInspectorBody", ImVec2(r.w - 26 * s, r.y + r.h - by - 9 * s), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImGui::SetWindowFontScale(atelier::Style().layoutScale);   // child text independent of the layout size
    ImGui::PopStyleVar();
    dl = ImGui::GetWindowDrawList();
    ImVec2 c = ImGui::GetCursorScreenPos();
    float bw = r.w - 26 * s - 8 * s;   // room for the scrollbar
    if (A.inspectorTab == 0) atlInspectorActor(c.x, c.y, bw, 0);
    else if (A.inspectorTab == 1) atlInspectorLighting(c.x, c.y, bw, 0);
    else if (A.inspectorTab == 2) atlInspectorRender(c.x, c.y, bw, 0);
    else atlInspectorWorld(c.x, c.y, bw, 0);
    ImGui::EndChild();
    dl = ImGui::GetWindowDrawList();
}

void App::atlInspectorActor(float x, float y, float w, float) {
    AtelierState& A = m_atl;
    Object* o = m_selected >= 0 ? &m_scene.objects[m_selected] : nullptr;
    if (!o) {
        Text(x, y, m_scene.name, P().text, 15);
        y += 25 * s;
        Text(x, y, "No object selected", P().muted, 11);
        y += 41 * s;
        Box({x, y, w, 98 * s}, P().bg, 4, true);
        Text(x + 12 * s, y + 12 * s, "SELECTION", P().accent, 9);
        Text(x + 12 * s, y + 35 * s, "Click an object in the viewport", P().muted, 10);
        Text(x + 12 * s, y + 54 * s, "or in the Scene Manager", P().muted, 10);
        Text(x + 12 * s, y + 73 * s, "Shift+click adds to the selection", P().muted, 10);
        ImGui::SetCursorScreenPos(ImVec2(x, y + 110 * s));
        ImGui::Dummy(ImVec2(1, 1));
        return;
    }
    if (o->curve.valid() && m_curves) { m_curves->inspector(x, y, w); return; }   // curve: its own section (CurveInspector.inl)
    if (o->group) {   // group node: its transform moves its children
        bool act = false;
        Box({x, y, w, 56 * s}, Fade(P().accent, 30), 4, true);
        Glyph(Icon::Folder, x + 12 * s, y + 15 * s, 24, IM_COL32(150, 170, 200, 255));
        dl->PushClipRect(ImVec2(x, y), ImVec2(x + w - 8 * s, y + 56 * s), true);
        Text(x + 48 * s, y + 11 * s, o->name, P().text, 14);
        std::string type = "Group · " + std::to_string(nodeChildren(o->id, 0, false).size()) + " children";
        if (!m_multi.empty()) type += "  (+" + std::to_string(m_multi.size()) + ")";
        Text(x + 48 * s, y + 33 * s, type, P().accent, 10);
        dl->PopClipRect();
        y += 69 * s;
        Label(x, y, "TRANSFORM");
        y += 24 * s;
        Text(x, y, "Position", P().muted, 11);
        y += 19 * s;
        if (AxisFields("gpos", x, y, w, &o->position.x, 0.1f, "%.3f", act)) markDirty();
        y += 35 * s;
        float3 e = eulerYXZ(objRot(*o));
        Text(x, y, "Rotation", P().muted, 11);
        y += 19 * s;
        if (AxisFields("grot", x, y, w, &e.x, 0.5f, "%.1f", act)) { o->rot = quatYXZ(e); o->useRot = true; o->yaw = radians(e.y); markDirty(); }
        y += 35 * s;
        Text(x, y, "Scale", P().muted, 11);
        y += 19 * s;
        if (AxisFields("gscl", x, y, w, &o->scale.x, 0.01f, "%.2f", act)) { o->scale = maxv(o->scale, float3(0.01f)); markDirty(); }
        y += 41 * s;
        if (act) pushUndo();
        dl->AddLine(ImVec2(x, y), ImVec2(x + w, y), P().border);
        y += 14 * s;
        float bw = (w - 6 * s) / 2;
        const uint32_t gid = o->id;
        if (Button("ungroup", {x, y, bw, 27 * s}, "Ungroup", Icon::Folder)) { ungroup(gid); return; }
        if (Button("del", {x + bw + 6 * s, y, bw, 27 * s}, "Delete", Icon::Trash)) { deleteSelected(); return; }
        ImGui::SetCursorScreenPos(ImVec2(x, y + 40 * s));
        ImGui::Dummy(ImVec2(1, 1));
        return;
    }
    if (o->camera) {   // camera object: its transform (no scale), its lens, render camera, Look Through
        bool act = false;
        Box({x, y, w, 56 * s}, Fade(P().accent, 30), 4, true);
        Glyph(Icon::Camera, x + 12 * s, y + 15 * s, 24, o->camRender ? IM_COL32(255, 196, 92, 255) : IM_COL32(214, 220, 230, 255));
        dl->PushClipRect(ImVec2(x, y), ImVec2(x + w - 8 * s, y + 56 * s), true);
        Text(x + 48 * s, y + 11 * s, o->name, P().text, 14);
        std::string type = std::string(o->camOrtho ? "Orthographic camera" : "Perspective camera") + (o->camRender ? " · render camera" : "");
        if (!m_multi.empty()) type += "  (+" + std::to_string(m_multi.size()) + ")";
        Text(x + 48 * s, y + 33 * s, type, P().accent, 10);
        dl->PopClipRect();
        y += 69 * s;
        Label(x, y, "TRANSFORM");
        y += 24 * s;
        Text(x, y, "Position", P().muted, 11);
        y += 19 * s;
        if (AxisFields("cpos", x, y, w, &o->position.x, 0.1f, "%.3f", act)) markDirty();
        y += 35 * s;
        float3 e = eulerYXZ(objRot(*o));
        Text(x, y, "Rotation", P().muted, 11);
        y += 19 * s;
        if (AxisFields("crot", x, y, w, &e.x, 0.5f, "%.1f", act)) { o->rot = quatYXZ(e); o->useRot = true; o->yaw = radians(e.y); markDirty(); }
        y += 41 * s;
        Label(x, y, "LENS");
        y += 24 * s;
        bool ortho = o->camOrtho;
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        if (Check("Orthographic##cam", &ortho)) {
            pushUndo();
            // the same size of view at the centre of interest, in the other projection
            if (ortho) o->camOrthoHeight = std::max(2.0f * o->camDistance * std::tan(o->camFovY * 0.5f), 0.01f);
            o->camOrtho = ortho;
            markDirty();
        }
        y += 32 * s;
        if (o->camOrtho) {
            if (SliderRow("##camOrthoH", "View Size (height)", x, y, w, &o->camOrthoHeight, 0.1f, 200.0f, "%.2f", act)) {
                o->camOrthoHeight = std::max(o->camOrthoHeight, 0.01f);
                markDirty();
            }
        } else {
            // focal length on a 36 x 24 mm film (Maya's default aperture), the vertical angle below
            float focal = 12.0f / std::tan(o->camFovY * 0.5f);
            if (SliderRow("##camFocal", "Focal Length (mm)", x, y, w, &focal, 8.0f, 200.0f, "%.1f", act)) {
                o->camFovY = 2.0f * std::atan(12.0f / std::max(focal, 1.0f));
                markDirty();
            }
            char fov[64];
            snprintf(fov, sizeof fov, "Vertical angle of view: %.1f°", degrees(o->camFovY));
            Text(x, y - 2 * s, fov, P().muted, 9);
            y += 16 * s;
        }
        if (SliderRow("##camAspect", "Aspect Ratio (preview)", x, y, w, &o->camAspect, 0.5f, 3.0f, "%.3f", act)) markDirty();
        if (SliderRow("##camCoi", "Centre of Interest", x, y, w, &o->camDistance, 0.1f, 200.0f, "%.2f", act)) {
            o->camDistance = std::max(o->camDistance, 0.01f);
            markDirty();
        }
        Label(x, y, "CAMERA");
        y += 24 * s;
        bool render = o->camRender;
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        if (Check("Render Camera", &render)) { pushUndo(); setRenderCamera(render ? m_selected : -1); }
        Tip("The camera of RenderCam and of the renders (one per scene)");
        y += 32 * s;
        bool vis = o->visible;
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        if (Check("Visible##cam", &vis)) { pushUndo(); o->visible = vis; markDirty(); }
        y += 38 * s;
        if (act) pushUndo();
        dl->AddLine(ImVec2(x, y), ImVec2(x + w, y), P().border);
        y += 14 * s;
        const uint32_t cid = o->id;
        if (Button("camLook", {x, y, w, 27 * s}, "Look Through", Icon::Camera)) setPaneCamera(m_activePane, VCAM_OBJECT, cid);
        Tip("The active view looks through this camera");
        y += 33 * s;
        const float bw = (w - 6 * s) / 2;
        if (Button("dup", {x, y, bw, 27 * s}, "Duplicate", Icon::Plus)) { duplicateSelected(); return; }
        if (Button("del", {x + bw + 6 * s, y, bw, 27 * s}, "Delete", Icon::Trash)) { deleteSelected(); return; }
        ImGui::SetCursorScreenPos(ImVec2(x, y + 40 * s));
        ImGui::Dummy(ImVec2(1, 1));
        return;
    }
    Material& mat = m_scene.materials[o->material];
    const PartDef* pd = findPart(o->part);
    const PartKind kind = o->prim.type == PRIM_SPHERE || o->prim.type == PRIM_TORUS ? K_SPHERE : pd ? pd->kind : K_CUBE;
    ImU32 col = srgbU32(mat.baseColor);
    // header card (graph inspector node card)
    Box({x, y, w, 56 * s}, Fade(P().accent, 30), 4, true);
    drawPrimitiveIcon(dl, kind, ImVec2(x + 9 * s, y + 12 * s), ImVec2(x + 39 * s, y + 42 * s), col);
    dl->PushClipRect(ImVec2(x, y), ImVec2(x + w - 34 * s, y + 56 * s), true);
    Text(x + 48 * s, y + 11 * s, o->name, P().text, 14);
    std::string type = o->prim.type != PRIM_NONE ? std::string("Primitive · ") + primitiveName(o->prim.type)
                     : pd ? "Primitive · " + o->part : (o->part == "WALL" ? "Wall · static mesh" : "Mesh · " + o->part);
    if (!m_multi.empty()) type += "  (+" + std::to_string(m_multi.size()) + ")";
    Text(x + 48 * s, y + 33 * s, type, P().accent, 10);
    dl->PopClipRect();
    {
        bool locked = !o->selectable;
        if (Hit("lockActor", {x + w - 30 * s, y + 16 * s, 24 * s, 24 * s})) { o->selectable = locked; if (!o->selectable) { select(-1); return; } }
        Tip(locked ? "Unlock" : "Lock (not selectable)");
        Glyph(Icon::Lock, x + w - 25 * s, y + 21 * s, 13, locked ? P().accent : P().muted);
    }
    y += 69 * s;
    Search("##atlPropSearch", {x, y, w, 28 * s}, A.propSearch, sizeof(A.propSearch), "Search properties...");
    y += 40 * s;
    auto show = [&](const char* keys) { return matches(keys, A.propSearch); };
    bool act = false;

    if (show("Transform Position Rotation Scale")) {
        Label(x, y, "TRANSFORM");
        y += 24 * s;
        Text(x, y, "Position", P().muted, 11);
        y += 19 * s;
        if (AxisFields("pos", x, y, w, &o->position.x, 0.1f, "%.3f", act)) markDirty();
        y += 35 * s;
        float3 e = eulerYXZ(objRot(*o));
        Text(x, y, "Rotation", P().muted, 11);
        y += 19 * s;
        if (AxisFields("rot", x, y, w, &e.x, 0.5f, "%.1f", act)) { o->rot = quatYXZ(e); o->useRot = true; o->yaw = radians(e.y); markDirty(); }
        y += 35 * s;
        Text(x, y, "Scale", P().muted, 11);
        y += 19 * s;
        if (AxisFields("scl", x, y, w, &o->scale.x, 0.01f, "%.2f", act)) { o->scale = maxv(o->scale, float3(0.01f)); markDirty(); }
        y += 41 * s;
    }
    if (o->prim.type != PRIM_NONE && show("Primitive Mesh Dimensions Size Width Height Depth Attributes Radius Tube Segmentation Segments Rings Sides Subdivisions")) {
        // parameters of a Create > Mesh primitive: the mesh is rebuilt from them
        PrimitiveParams& pp = o->prim;
        bool ch = false;
        auto length = [&](const char* id, const char* label, float& v) {
            float f = v;
            if (SliderRow(id, label, x, y, w, &f, 0.05f, 20.0f, "%.2f", act) && f != v) { v = std::max(f, 0.01f); ch = true; }
        };
        auto count = [&](const char* id, const char* label, int& v, int lo, int hi) {
            float f = (float)v;
            if (SliderRow(id, label, x, y, w, &f, (float)lo, (float)hi, "%.0f", act)) {
                int n = std::clamp((int)std::lround(f), lo, hi);
                if (n != v) { v = n; ch = true; }
            }
        };
        if (pp.type == PRIM_CUBE || pp.type == PRIM_PLANE || pp.type == PRIM_CYLINDER) {
            Label(x, y, "DIMENSIONS");
            y += 24 * s;
        }
        if (pp.type == PRIM_CUBE) {
            float v[3] = {pp.size.x, pp.size.y, pp.size.z};
            Text(x, y, "Size", P().muted, 11);
            y += 19 * s;
            if (AxisFields("primSize", x, y, w, v, 0.01f, "%.2f", act)) { pp.size = maxv(float3(v[0], v[1], v[2]), float3(0.01f)); ch = true; }
            y += 41 * s;
        } else if (pp.type == PRIM_PLANE) {
            length("##primW", "Width", pp.size.x);
            length("##primD", "Depth", pp.size.z);
        } else if (pp.type == PRIM_CYLINDER) {
            length("##primH", "Height", pp.size.y);
        }
        if (pp.type == PRIM_SPHERE || pp.type == PRIM_CYLINDER || pp.type == PRIM_DISK || pp.type == PRIM_TORUS) {
            Label(x, y, "ATTRIBUTES");
            y += 24 * s;
            length("##primR", "Radius", pp.radius);
            if (pp.type == PRIM_TORUS) length("##primR2", "Tube Radius", pp.radius2);
        }
        Label(x, y, "SEGMENTATION");
        y += 24 * s;
        if (pp.type == PRIM_SPHERE) {
            count("##primSeg", "Segments", pp.segments, 3, 128);
            count("##primRings", "Rings", pp.rings, 2, 64);
        } else if (pp.type == PRIM_CYLINDER) {
            count("##primSeg", "Segments", pp.segments, 3, 128);
            count("##primCuts", "Height Cuts", pp.subdiv, 1, 32);
        } else if (pp.type == PRIM_TORUS) {
            count("##primSeg", "Segments", pp.segments, 3, 128);
            count("##primRings", "Tube Sides", pp.rings, 3, 64);
        } else if (pp.type == PRIM_DISK) {
            count("##primSeg", "Segments", pp.segments, 3, 128);
            count("##primCuts", "Rings", pp.subdiv, 1, 32);
        } else {
            count("##primCuts", "Subdivisions", pp.subdiv, 1, 32);
        }
        if (ch && syncPrimitiveMesh(m_scene, m_selected)) markDirty();
        y += 4 * s;
    }
    if (show("Static Mesh")) {
        Label(x, y, "STATIC MESH");
        y += 24 * s;
        R slot = {x, y, w, 44 * s};
        Box(slot, P().bg, 4, true);
        drawPrimitiveIcon(dl, kind, ImVec2(slot.x + 7 * s, slot.y + 6 * s), ImVec2(slot.x + 39 * s, slot.y + 38 * s), IM_COL32(190, 194, 200, 255));
        std::string meshName = o->mesh >= 0 && o->mesh < (int)m_scene.meshNames.size() ? m_scene.meshNames[o->mesh] : o->part;
        dl->PushClipRect(slot.min(), slot.max(), true);
        Text(slot.x + 48 * s, slot.y + 8 * s, "SM_" + meshName, P().text, 11);
        Text(slot.x + 48 * s, slot.y + 25 * s, std::to_string(m_scene.meshes[o->mesh].triangleCount()) + " triangles", P().muted, 9);
        dl->PopClipRect();
        y += 56 * s;
    }
    if (show("Material Colour Preset Roughness Metallic Transmission Subsurface Clearcoat Emission")) {
        Label(x, y, "MATERIAL");
        y += 24 * s;
        R slot = {x, y, w, 38 * s};
        bool q = Hit("matSlot", slot);
        Box(slot, ImGui::IsItemHovered() ? P().hover : P().bg, 4, true);
        dl->AddCircleFilled(ImVec2(slot.x + 19 * s, slot.y + 19 * s), 13 * s, col, 32);
        dl->AddCircleFilled(ImVec2(slot.x + 15 * s, slot.y + 14 * s), 4 * s, IM_COL32(255, 255, 255, 90), 16);
        std::string pname = mat.preset >= 0 && mat.preset < (int)presetNames().size() ? toUtf8(presetNames()[mat.preset]) : "Material";
        dl->PushClipRect(slot.min(), slot.max(), true);
        Text(slot.x + 42 * s, slot.y + 12 * s, "M_" + pname, P().text, 11);
        dl->PopClipRect();
        Glyph(Icon::ChevronDown, slot.x + slot.w - 16 * s, slot.y + 15 * s, 9, P().muted);
        Tip("Choose a material preset (or drop one from the Content Browser)");
        if (q) ImGui::OpenPopup("atlMatPicker");
        if (ImGui::BeginPopup("atlMatPicker")) {
            for (size_t k = 0; k < presetNames().size(); ++k)
                if (ImGui::Selectable(toUtf8(presetNames()[k]).c_str(), mat.preset == (int)k)) setPreset((int)k);
            ImGui::EndPopup();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("WZX_MATERIAL")) setPreset(*(const int*)pl->Data);
            ImGui::EndDragDropTarget();
        }
        y += 48 * s;
        Text(x, y, "Colour", P().muted, 11);
        y += 19 * s;
        const auto& pal = palette();
        int cols = 12;
        float cell = std::floor((w + 3 * s) / cols);
        for (size_t i = 0; i < pal.size(); ++i) {
            R sw = {x + (i % cols) * cell, y + (i / cols) * cell, cell - 3 * s, cell - 3 * s};
            char id[16];
            snprintf(id, sizeof(id), "sw%zu", i);
            if (Hit(id, sw)) setColor((int)i);
            bool hv = ImGui::IsItemHovered();
            dl->AddRectFilled(sw.min(), sw.max(), IM_COL32(pal[i].r, pal[i].g, pal[i].b, pal[i].group == ColorGroup::Transparent ? 170 : 255), 3 * s);
            if (mat.colorId == pal[i].id) dl->AddRect(ImVec2(sw.x - 1, sw.y - 1), ImVec2(sw.x + sw.w + 1, sw.y + sw.h + 1), P().accent, 3 * s, 0, 1.5f * s);
            else if (hv) dl->AddRect(sw.min(), sw.max(), P().text, 3 * s);
            if (hv) ImGui::SetTooltip("%s", toUtf8(pal[i].name).c_str());
        }
        y += ((pal.size() + cols - 1) / cols) * cell + 12 * s;
        bool ch = false, mact = false;
        ch |= SliderRow("##rough", "Roughness", x, y, w, &mat.roughness, 0, 1, "%.3f", mact);
        ch |= SliderRow("##metal", "Metallic", x, y, w, &mat.metallic, 0, 1, "%.3f", mact);
        ch |= SliderRow("##trans", "Transmission", x, y, w, &mat.transmission, 0, 1, "%.3f", mact);
        ch |= SliderRow("##sss", "Subsurface", x, y, w, &mat.subsurface, 0, 1, "%.3f", mact);
        ch |= SliderRow("##coat", "Clearcoat", x, y, w, &mat.coat, 0, 1, "%.3f", mact);
        ch |= SliderRow("##emit", "Emission Strength", x, y, w, &mat.emissionStrength, 0, 30, "%.2f", mact);
        if (mat.emissionStrength > 0 && mat.emission.x + mat.emission.y + mat.emission.z == 0) mat.emission = mat.baseColor;
        if (mact) pushUndo();
        if (ch) markDirty();
        y += 4 * s;
    }
    if (show("Rendering Visible Selectable Rounded Bevel")) {
        Label(x, y, "RENDERING");
        y += 24 * s;
        bool vis = o->visible;
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        if (Check("Visible", &vis)) { pushUndo(); o->visible = vis; markDirty(); }
        y += 32 * s;
        bool selb = o->selectable;
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        if (Check("Selectable", &selb)) o->selectable = selb;
        y += 32 * s;
        bool bevel = mat.bevelRadius > 0;
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        if (Check("Rounded edges", &bevel)) { pushUndo(); mat.bevelRadius = bevel ? 0.05f : 0.0f; markDirty(); }
        y += 38 * s;
    }
    if (act) pushUndo();
    dl->AddLine(ImVec2(x, y), ImVec2(x + w, y), P().border);
    y += 14 * s;
    float bw = (w - 6 * s) / 2;
    if (Button("dup", {x, y, bw, 27 * s}, "Duplicate", Icon::Plus)) duplicateSelected();
    if (Button("del", {x + bw + 6 * s, y, bw, 27 * s}, "Delete", Icon::Trash)) deleteSelected();
    ImGui::SetCursorScreenPos(ImVec2(x, y + 40 * s));
    ImGui::Dummy(ImVec2(1, 1));
}

void App::atlInspectorLighting(float x, float y, float w, float) {
    bool ch = false, act = false;
    // the light selected in the Scene Manager: framed, scrolled into view when it gets selected
    static uint32_t shown = 0;
    uint32_t want = 0;
    for (uint32_t id : m_outSel) { int k = findNode(id).kind; if (k == NODE_LIGHT || k == NODE_SUN) { want = id; break; } }
    auto lightCard = [&](uint32_t id, float cy) {
        if (id != want) return;
        SelectedFrame({x, cy, w, 56 * s});
        if (shown != want) { ImGui::SetCursorScreenPos(ImVec2(x, cy)); ImGui::SetScrollHereY(0.0f); shown = want; }
    };
    if (!want) shown = 0;
    {   // the directional light of the scene (the sun): sharp shadows, one light
        SunLight& sun = m_scene.sun;
        ImGui::PushID("sun");
        Box({x, y, w, 56 * s}, Fade(IM_COL32(242, 201, 76, 255), 30), 4, true);
        lightCard(sun.id, y);
        Glyph(Icon::Sun, x + 10 * s, y + 15 * s, 22, sun.enabled ? IM_COL32(242, 201, 76, 255) : P().muted);
        Text(x + 43 * s, y + 11 * s, "Directional Light", P().text, 14);
        Text(x + 43 * s, y + 33 * s, sun.enabled ? "Directional · on" : "Directional · off", sun.enabled ? IM_COL32(212, 188, 109, 255) : P().muted, 10);
        bool en = sun.enabled;
        ImGui::SetCursorScreenPos(ImVec2(x + w - 32 * s, y + 16 * s));
        if (Check("##sunen", &en)) { sun.enabled = en; ch = true; }
        y += 69 * s;
        Label(x, y, "SUN PROPERTIES");
        y += 24 * s;
        float el = degrees(std::asin(std::clamp(sun.direction.y, -1.0f, 1.0f)));
        float az = degrees(std::atan2(sun.direction.x, sun.direction.z));
        bool moved = false;
        moved |= SliderRow("##sunaz", "Azimuth", x, y, w, &az, -180, 180, "%.0f deg", act);
        moved |= SliderRow("##sunel", "Elevation", x, y, w, &el, 5, 85, "%.0f deg", act);
        if (moved) {
            const float ea = radians(el), aa = radians(az), ce = std::cos(ea);
            sun.direction = float3(std::sin(aa) * ce, std::sin(ea), std::cos(aa) * ce);
            ch = true;
        }
        ch |= SliderRow("##sunint", "Intensity", x, y, w, &sun.intensity, 0, 12, "%.2f", act);
        Text(x, y, "Colour", P().muted, 11);
        y += 19 * s;
        float sc3[3] = {sun.color.x, sun.color.y, sun.color.z};
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        ImGui::SetNextItemWidth(w);
        if (ImGui::ColorEdit3("##suncol", sc3)) { sun.color = float3(sc3[0], sc3[1], sc3[2]); ch = true; }
        y += 41 * s;
        dl->AddLine(ImVec2(x, y), ImVec2(x + w, y), P().border);
        y += 18 * s;
        ImGui::PopID();
    }
    for (size_t i = 0; i < m_scene.lights.size(); ++i) {
        AreaLight& l = m_scene.lights[i];
        ImGui::PushID((int)i);
        Box({x, y, w, 56 * s}, Fade(IM_COL32(242, 201, 76, 255), 30), 4, true);
        lightCard(l.id, y);
        Glyph(Icon::Light, x + 10 * s, y + 15 * s, 22, l.enabled ? IM_COL32(242, 201, 76, 255) : P().muted);
        Text(x + 43 * s, y + 11 * s, l.name, P().text, 14);
        Text(x + 43 * s, y + 33 * s, l.enabled ? "Area light · on" : "Area light · off", l.enabled ? IM_COL32(212, 188, 109, 255) : P().muted, 10);
        bool en = l.enabled;
        ImGui::SetCursorScreenPos(ImVec2(x + w - 32 * s, y + 16 * s));
        if (Check("##en", &en)) { l.enabled = en; ch = true; }
        y += 69 * s;
        Label(x, y, "LIGHT PROPERTIES");
        y += 24 * s;
        ch |= SliderRow("##int", "Intensity", x, y, w, &l.intensity, 0, 60, "%.1f", act);
        ch |= SliderRow("##w", "Width", x, y, w, &l.width, 1, 80, "%.1f cm", act);
        Text(x, y, "Colour", P().muted, 11);
        y += 19 * s;
        float c3[3] = {l.color.x, l.color.y, l.color.z};
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        ImGui::SetNextItemWidth(w);
        if (ImGui::ColorEdit3("##col", c3)) { l.color = float3(c3[0], c3[1], c3[2]); ch = true; }
        y += 41 * s;
        dl->AddLine(ImVec2(x, y), ImVec2(x + w, y), P().border);
        y += 18 * s;
        ImGui::PopID();
    }
    Label(x, y, "ENVIRONMENT");
    y += 24 * s;
    ch |= SliderRow("##envint", "Sky intensity", x, y, w, &m_scene.env.intensity, 0, 4, "%.2f", act);
    float ev = std::log2(std::max(m_rs.exposure, 1e-4f));
    Text(x, y, "Exposure", P().muted, 11);
    y += 19 * s;
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    ImGui::SetNextItemWidth(w);
    if (ImGui::SliderFloat("##ev", &ev, -4, 4, "%+.2f EV")) m_rs.exposure = std::pow(2.0f, ev);
    y += 36 * s;
    if (act) pushUndo();   // light edits are undoable (the undo restores the lights of the Scene Manager)
    if (ch) markDirty();
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    ImGui::Dummy(ImVec2(1, 1));
}

// Options of the render engine in use. Each mode has its own: the viewport mode (raster) has none of the path
// tracing ones, and the progressive and GI engines have theirs. Each option has an info icon.
void App::atlInspectorRender(float x, float y, float w, float) {
    bool ch = false, act = false;
    static const char* names[] = {"Real time (raster)", "Metal iOS (raster + GI probes)", "Progressive path tracing", "GI probes (DDGI, ray traced)", "Split Radiance Cascades"};
    const int mode = renderMode();
    Text(x, y, names[mode], P().text, 14);
    y += 25 * s;
    Text(x, y, "Options of this engine only", P().muted, 11);
    y += 35 * s;
    // info icon: a small "i" at (ix, iy); the explanation shows on hover
    auto info = [&](const char* id, float ix, float iy, const char* text) {
        ImGui::SetCursorScreenPos(ImVec2(ix, iy));
        ImGui::InvisibleButton(id, ImVec2(18 * s, 18 * s));
        const bool hov = ImGui::IsItemHovered();
        dl->AddCircle(ImVec2(ix + 9 * s, iy + 9 * s), 8 * s, hov ? P().accent : P().muted, 0, 1.5f);
        Text(ix + 7 * s, iy + 2 * s, "i", hov ? P().accent : P().muted, 13);
        if (hov) ImGui::SetTooltip("%s", text);
    };
    auto check = [&](const char* id, const char* label, bool& v, const char* help) {
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        bool b = v;
        if (Check(id, &b)) { v = b; ch = true; }
        Text(x + 26 * s, y + 3 * s, label, P().text, 11);
        info((std::string(id) + "_i").c_str(), x + w - 18 * s, y + 0 * s, help);
        y += 26 * s;
    };
    auto sliderF = [&](const char* id, const char* label, float& v, float lo, float hi, const char* fmt, const char* help) {
        info((std::string(id) + "_i").c_str(), x + w - 18 * s, y + (P().skin == 2 ? 7 : 0) * s, help);
        ch |= SliderRow(id, label, x, y, w, &v, lo, hi, fmt, act);
    };
    auto sliderI = [&](const char* id, const char* label, int& v, float lo, float hi, const char* fmt, const char* help) {
        info((std::string(id) + "_i").c_str(), x + w - 18 * s, y + (P().skin == 2 ? 7 : 0) * s, help);
        float f = (float)v;
        if (SliderRow(id, label, x, y, w, &f, lo, hi, fmt, act)) { v = (int)std::round(f); ch = true; }
    };
    auto denoiser = [&]() {
        Label(x, y, "DENOISER");
        y += 24 * s;
        check("##dn", "Denoiser", m_rs.denoise, "Smooths the noise with an edge-aware filter. Off keeps the image as rendered.");
        sliderF("##dns", "Strength", m_rs.denoiseStrength, 0, 2, "%.2f", "Overall strength of the denoiser. 0 does nothing; above 1 starts to blur fine detail.");
        sliderI("##dpass", "Passes", m_rs.denoisePasses, 1, 5, "%.0f", "Number of filter passes (steps 1, 2, 4, ...). More = smoother but blurrier, and more GPU time.");
        sliderF("##dedge", "Edge sensitivity", m_rs.denoiseEdgeLuma, 0.05f, 4, "%.2f", "Lower = the filter stops at light and shadow edges, so shadows stay sharp. Higher = smoother, softer shadows.");
        y += 8 * s;
    };
    // HDRI: shared by every engine (each one uses it its own way, see the text under the options)
    {
        Label(x, y, "HDRI ENVIRONMENT · ALL ENGINES");
        y += 24 * s;
        std::vector<std::wstring> files = hdriFiles();
        std::vector<std::string> names = {"None (gradient sky)", "Procedural sky"};
        int cur = m_hdriPath.empty() ? 0 : (m_hdriPath == L"sky" ? 1 : -1);
        for (size_t i = 0; i < files.size(); ++i) {
            std::wstring f = files[i];
            std::wstring n = f.substr(f.find_last_of(L"\\/") + 1);
            names.push_back(toUtf8(n) + (f.find(L"01 - INBOX") != std::wstring::npos ? "  (inbox)" : ""));
            if (_wcsicmp(f.c_str(), m_hdriPath.c_str()) == 0) cur = (int)i + 2;
        }
        if (cur < 0) {   // loaded from another folder (Load...)
            std::wstring n = m_hdriPath.substr(m_hdriPath.find_last_of(L"\\/") + 1);
            names.push_back(toUtf8(n));
            files.push_back(m_hdriPath);
            cur = (int)names.size() - 1;
        }
        std::vector<const char*> opts;
        for (auto& n : names) opts.push_back(n.c_str());
        int sel = cur;
        const float bw = 64 * s;
        if (Combo("##hdri", R{x, y, w - bw - 6 * s - 22 * s, 28 * s}, sel, opts.data(), (int)opts.size(), Icon::Sun) && sel != cur)
            setHdri(sel == 0 ? std::wstring() : (sel == 1 ? std::wstring(L"sky") : files[sel - 2]));
        if (Button("##hdriLoad", R{x + w - bw - 22 * s, y, bw, 28 * s}, "Load…", Icon::FolderOpen)) m_pendingAction = 3;
        info("##hdri_i", x + w - 18 * s, y + 5 * s,
             "Image that surrounds the scene: it lights it, shows in reflections and in the background.\n"
             "Files: .hdr and .exr (half/float, uncompressed, RLE, ZIP or PIZ) are read as they are; jpg/png are\n"
             "converted to linear but have no real dynamic range (no sun). Images wider than 4096 are reduced.\n"
             "The list shows the .hdr/.exr of 01 - INBOX and the images of assets/hdri.");
        y += 36 * s;
        if (m_scene.env.hdri) {
            const EnvImage& e = *m_scene.env.hdri;
            char b[160];
            snprintf(b, sizeof(b), "%u x %u%s", e.sourceWidth, e.sourceHeight, e.sourceWidth != e.width ? " (reduced to 4096)" : "");
            Text(x, y, b, P().muted, 10);
            y += 20 * s;
            sliderF("##hdrInt", "HDRI intensity", m_scene.env.hdriIntensity, 0, 4, "%.2f", "Multiplies the light of the image (light, reflections and background).");
            float deg = std::fmod(degrees(m_scene.env.hdriRotation) + 720.0f, 360.0f);
            info("##hdrRot_i", x + w - 18 * s, y, "Turns the image around the vertical axis: moves the sun and the reflections.");
            if (SliderRow("##hdrRot", "HDRI rotation", x, y, w, &deg, 0, 360, "%.0f deg", act)) {
                m_scene.env.hdriRotation = radians(deg);
                ch = true;
            }
            check("##hdrVis", "Visible to the camera", m_scene.env.visibleToCamera, "Off: the background shows the gradient sky, the image still lights the scene and shows in reflections.");
        }
        y += 8 * s;
    }
    if (mode == 0) {
        Label(x, y, "GLOBAL ILLUMINATION");
        y += 24 * s;
        check("##bp", "Baked probes", m_rs.bakedProbes, "Indirect light from probes baked once with the ray traced GI (converged), saved with the scene (game/probes) and read as they are by the Windows and iOS builds. The bake restarts by itself half a second after the scene changes. Off: the hemisphere / HDRI ambient of Lit.");
        if (m_rs.bakedProbes) {
            if (Button("##bakeNow", R{x, y, 110 * s, 26 * s}, "Bake now", Icon::Sun, false, false, !m_renderer.baking())) m_bakeNow = true;
            std::string st = m_renderer.baking() ? "Baking, " + std::to_string(m_renderer.bakeFramesLeft()) + " frames left"
                             : (m_renderer.hasBakedProbes() ? std::to_string(m_renderer.giProbeCount()) + " probes baked" : "No bake yet");
            Text(x + 120 * s, y + 6 * s, st, P().muted, 11);
            y += 34 * s;
        }
        y += 8 * s;
        Label(x, y, "HDRI (APPROXIMATE)");
        y += 24 * s;
        check("##envsun", "Sun of the HDRI casts shadows", m_rs.rasterEnvSun, "The brightest spot of the image (the sun) becomes a directional light with the shadow map; the rest of the image lights the scene as a soft ambient (SH). Off: all the image is ambient, no shadows. Ignored when the scene has its own sun.");
        Text(x, y, "Ambient from the HDRI, glossy reflections from its blurred mips.", P().muted, 11);
        y += 19 * s;
        Text(x, y, "Emissive objects light as sphere lights (no shadow).", P().muted, 11);
        y += 25 * s;
    } else if (mode == 2) {
        Label(x, y, "SAMPLES");
        y += 24 * s;
        sliderI("##msp", "Max samples", m_rs.maxSamples, 16, 16384, "%.0f", "Samples per pixel of the progressive image before it stops. More = cleaner, slower.");
        y += 8 * s;
        denoiser();
        Label(x, y, "LIGHT");
        y += 24 * s;
        sliderI("##bnc", "Bounces", m_rs.maxBounces, 1, 16, "%.0f", "Light bounces per path. More = more indirect light, slower.");
        sliderF("##fl", "Firefly clamp", m_rs.fireflyClamp, 1, 200, "%.0f", "Caps very bright single samples to prevent sparkles.");
        sliderF("##fg", "Filter glossy (caustics)", m_rs.filterGlossy, 0, 1, "%.2f", "After a rough bounce, mirrors and glass are seen at least this rough. 0 = exact, with the caustics of the chrome and glass balls (sparkles until the image converges). 0.3 to 0.5 = smooth and fast, the caustics are spread (approximate).");
        sliderF("##ic", "Indirect clamp", m_rs.indirectClamp, 0, 20, m_rs.indirectClamp > 0 ? "%.1f" : "off", "Caps each light contribution after a rough bounce or through subsurface scattering. Removes the sparkles of rare bright paths; lower = cleaner but the indirect light gets a little darker. 0 = off (exact).");
        Text(x, y, "HDRI and emissive objects are sampled as lights (MIS).", P().muted, 11);
        y += 22 * s;
    } else if (mode == 4) {
        Label(x, y, "SPLIT RADIANCE CASCADES");
        y += 24 * s;
        sliderI("##srpx", "Probe size (pixels)", m_rs.rcSplitProbePx, 4, 16, "%.0f px", "Size on screen of a cascade-0 probe cell (levels of detail keep it so with distance). Smaller = finer contact shadows, fewer rays per probe (noisier) and more GPU cost.");
        sliderF("##srint", "First interval", m_rs.rcSplitInterval, 0.5f, 6, "%.1f cells", "Length of the cascade-0 rays in probe cells (x4 per cascade). Shorter = sharper near occlusion, more angular aliasing further.");
        sliderI("##srcas", "Cascades", m_rs.rcSplitCascades, 2, 6, "%.0f", "Cascades of probes (the top one sees to infinity and the sky). Each has 2x the spacing and 4x the directions of the one below. More cascades = longer rays before the sky (big scenes); the top ones have many directions, so few rays each.");
        sliderI("##srhist", "Accumulate while still", m_rs.rcSplitHistory, 0, 64, m_rs.rcSplitHistory > 1 ? "%.0f frames" : "off", "Averages the probes over up to this many frames, in world space: the camera can move without losing it. Dropped around an object that moves, and everywhere when a light or a material changes, so nothing ghosts. Off (0 or 1): every frame on its own (flickers a little more).");
        check("##srflt", "Probe filter", m_rs.rcSplitFilter, "Each probe is averaged with its 6 neighbour cells: removes the isolated dark or bright cells (a probe that caught too few rays towards a small bright source), at the cost of a probe cell of blur.");
        check("##sranim", "Moving ray jitter", m_rs.rcSplitAnimate, "Turns the ray set every frame (always on with the accumulation). Off and without accumulation: a still image stays perfectly still.");
        check("##srbn", "Further bounces", m_rs.rcBounces, "The ray hits read the world cascades of the previous frame (the bounce cache, no history either). Off: one bounce.");
        static const char* views[] = {"Full", "Indirect only", "Direct only"};
        int view = m_rs.ddgiView;
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        if (Combo("##srview", R{x, y, w - 22 * s, 28 * s}, view, views, 3, Icon::Sun)) { m_rs.ddgiView = view; ch = true; }
        info("##srview_i", x + w - 18 * s, y + 5 * s, "Full image, only the indirect light, or only the direct light (debug).");
        y += 36 * s;
        check("##srsh", "Ray traced sun shadow", m_rs.rtSunShadows, "Sun shadow traced per pixel (soft, filtered, no history) instead of the shadow map.");
        check("##srrf", "Ray traced reflections", m_rs.rtReflections, "Metals and glossy surfaces reflect the scene (traced per pixel, filtered, no history).");
        y += 8 * s;
        Label(x, y, "QUALITY");
        y += 24 * s;
        {
            static const char* aa[] = {"Anti-aliasing: MSAA 4x", "Anti-aliasing: MSAA 8x", "Anti-aliasing: AAA (8x, HDR resolve)"};
            int a = std::clamp(m_rs.aaQuality, 0, 2);
            ImGui::SetCursorScreenPos(ImVec2(x, y));
            if (Combo("##sraa", R{x, y, w - 22 * s, 28 * s}, a, aa, 3, Icon::Cube)) { m_rs.aaQuality = a; ch = true; }
            info("##sraa_i", x + w - 18 * s, y + 5 * s, "Edges of the objects. MSAA 4x: as Lit. 8x: twice the samples per pixel. AAA: 8x resolved in tonemapped space, so the edges between a bright and a dark surface stay smooth instead of stair-stepping. Lit keeps 4x.");
            y += 36 * s;
            static const char* sq[] = {"Shadows: low", "Shadows: medium", "Shadows: high (default)", "Shadows: ultra", "Shadows: AAA"};
            int q = std::clamp(m_rs.shadowQuality, 0, 4);
            ImGui::SetCursorScreenPos(ImVec2(x, y));
            if (Combo("##srsq", R{x, y, w - 22 * s, 28 * s}, q, sq, 5, Icon::Sun)) { m_rs.shadowQuality = q; ch = true; }
            info("##srsq_i", x + w - 18 * s, y + 5 * s, "Ray traced shadows of the sun and of the main lamp: rays per pixel (sun 2 / 4 / 8 / 16 / 32, lamp twice as many) and the width of their smoothing filter. AAA: 32 / 64 rays, the softest and cleanest penumbra, the most GPU time.");
            y += 36 * s;
        }
        y += 8 * s;
        Label(x, y, "INSPECTION");
        y += 24 * s;
        {
            static const char* insp[] = {"Off (shaded)", "Probe cells", "Probe irradiance (no blend)", "Cascade hit by the ray", "Rays per probe", "Level of detail", "Accumulated frames", "Shadow mask (ray traced)"};
            int d = std::clamp(m_rs.rcSplitDebug, 0, 7);
            ImGui::SetCursorScreenPos(ImVec2(x, y));
            if (Combo("##srdbg", R{x, y, w - 22 * s, 28 * s}, d, insp, 8, Icon::Eye)) { m_rs.rcSplitDebug = d; ch = true; }
            info("##srdbg_i", x + w - 18 * s, y + 5 * s, "Probe cells: a colour per probe cell of the chosen cascade (magenta = no probe, dark lines = cell borders). Probe irradiance: the light of the nearest cascade-0 probe, unblended. Cascade hit: where each pixel's ray found its light (blue = cascade 0 ... red = top, black = sky). Rays per probe: rays of the pixel's cascade-0 probe (white = 256). Level of detail: a colour per LOD. Accumulated frames: black = none (just moved), white = full history.");
            y += 36 * s;
            if (m_rs.rcSplitDebug == 1) sliderI("##srdbgc", "Cascade shown", m_rs.rcSplitDebugCascade, 0, (float)std::max(0, m_rs.rcSplitCascades - 1), "%.0f", "Cascade whose probe cells are drawn.");
        }
        y += 8 * s;
        const vp3d::RCStats& st = m_renderer.rcStats();
        char b[160];
        snprintf(b, sizeof(b), "GPU %.2f ms, of which Split RC %.2f ms", m_renderer.gpuFrameMs(), m_renderer.splitMs());
        Text(x, y, b, P().muted, 11);
        y += 19 * s;
        snprintf(b, sizeof(b), "%llu rays (1 per pixel), %d cascades, %u to %u directions", (unsigned long long)st.splitRays,
                 st.splitCascades, st.splitDirs[0], st.splitDirs[std::max(0, st.splitCascades - 1)]);
        Text(x, y, b, P().muted, 11);
        y += 19 * s;
        snprintf(b, sizeof(b), "Probes C0 %u / %u, C1 %u / %u%s", st.splitProbes[0], st.splitCapacity[0], st.splitProbes[1],
                 st.splitCapacity[1], st.splitProbes[0] > st.splitCapacity[0] ? " (full: bigger probes)" : "");
        Text(x, y, b, st.splitProbes[0] > st.splitCapacity[0] ? P().accent : P().muted, 11);
        y += 19 * s;
        Text(x, y, "History only while nothing moves: what moves is right the next frame.", P().muted, 11);
        y += 22 * s;
    } else {
        Label(x, y, "PROBES");
        y += 24 * s;
        sliderI("##pb", "Probe budget", m_rs.ddgiProbeBudget, 256, 8192, "%.0f", "Number of irradiance probes over the scene. More = finer indirect light, more GPU cost.");
        sliderF("##hy", "Hysteresis", m_rs.ddgiHysteresis, 0.80f, 0.999f, "%.3f", "How much each probe keeps its previous value per frame. Higher = steadier, slower to react. Progressive probes: the value reached once still.");
        if (mode == 3) check("##rmov", "Raster pendant le mouvement", m_rs.giRasterWhileMoving, "While the camera or an object moves: the raster view of the probes (no noise), then a short fade to the ray traced image once still.");
        check("##prog", "Progressive probes", m_rs.ddgiProgressive, "Running average while nothing changes (converged and still), only the probes around a moved object restart, a quarter of the probes per frame once still. Off: the former per-frame blend.");
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        static const char* views[] = {"Full", "Indirect only", "Direct only"};
        int view = m_rs.ddgiView;
        if (Combo("##gview", R{x, y, w - 22 * s, 28 * s}, view, views, 3, Icon::Sun)) { m_rs.ddgiView = view; ch = true; }
        info("##gview_i", x + w - 18 * s, y + 5 * s, "Full image, only the indirect light, or only the direct light (debug).");
        y += 36 * s;
        check("##sp", "Show probes", m_rs.ddgiShowProbes, "Draws the probes as small spheres to check their placement.");
        if (mode == 1) {   // Metal iOS: probes captured in raster, no image history
            sliderI("##cap", "Probes captured per frame", m_rs.ddgiCaptureProbes, 1, 512, "%.0f", "Probes refreshed each frame (six faces each). The whole grid is refreshed in total / this frames: more = reacts faster to moving lights, more GPU cost.");
            Text(x, y, "No ray tracing: what the iOS build renders.", P().muted, 11);
            y += 22 * s;
        } else {
            check("##rom", "Reset history when objects move", m_rs.resetOnMove, "When an object moves, its old light stays in the history. This clears it.");
            sliderF("##rd", "Fade after a move", m_rs.resetDelay, 0.2f, 2.0f, "%.1f s", "How long the old history fades out after a move.");
            y += 8 * s;
            Label(x, y, "HISTORY");
            y += 24 * s;
            sliderI("##tcap", "History length", m_rs.temporalCap, 16, 1024, "%.0f frames", "How many frames the image is averaged over. Longer = steadier.");
            y += 8 * s;
            denoiser();
        }
    }
    if (ch) markDirty();
}

void App::atlInspectorWorld(float x, float y, float w, float) {
    Text(x, y, m_scene.name.empty() ? "Scene" : m_scene.name, P().text, 15);
    y += 25 * s;
    Text(x, y, !m_docPath.empty() ? "File · " + toUtf8(m_docName) + (m_docDirty ? " (modified)" : "")
               : m_sceneId == SCENE_CORNELL ? "Test scene · Cornell Box" : m_sceneId == SCENE_LOOKDEV ? "Test scene · LookDev" : m_sceneId == SCENE_CONCEPT ? "Test scene · Concept" : (m_sceneId == SCENE_STUDIO ? "Test scene · Village (RTS)" : "New scene"), P().muted, 11);
    y += 41 * s;
    Label(x, y, "SCENE PROPERTIES");
    y += 24 * s;
    Text(x, y, "Scene name", P().muted, 11);
    y += 19 * s;
    char name[128];
    snprintf(name, sizeof(name), "%s", m_scene.name.c_str());
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    ImGui::SetNextItemWidth(w);
    if (ImGui::InputText("##worldName", name, sizeof(name))) m_scene.name = name;
    y += 35 * s;
    Text(x, y, "Rendering", P().muted, 11);
    y += 19 * s;
    static const char* modes[] = {"Real time", "Metal iOS", "Progressive", "GI probes (DDGI)", "Split RC"};
    int mode = renderMode();
    if (Combo("worldRender", {x, y, w, 27 * s}, mode, modes, 5, Icon::Sun)) setRenderMode(mode);
    y += 43 * s;
    Label(x, y, "SCENE SUMMARY");
    y += 25 * s;
    std::pair<const char*, size_t> rows[] = {{"Objects", m_scene.objects.size()}, {"Triangles", m_scene.triangleCount()}, {"Lights", m_scene.lights.size()}, {"Undo steps", m_undo.size()}};
    for (auto& p : rows) {
        Text(x, y, p.first, P().muted, 11);
        std::string v = std::to_string(p.second);
        Text(x + w - TextSize(v, 11).x, y, v, P().text, 11);
        y += 27 * s;
    }
    y += 14 * s;
    if (Button("worldSave", {x, y, w, 27 * s}, "Save Scene", Icon::Save)) m_pendingAction = 1;
    y += 33 * s;
    if (Button("worldLoad", {x, y, w, 27 * s}, "Open Scene", Icon::FolderOpen)) m_pendingAction = 2;
    y += 33 * s;
    if (Button("worldCapture", {x, y, w, 27 * s}, "Viewport Capture", Icon::Camera)) saveScreenshot(false);
    y += 45 * s;
    Box({x, y, w, 98 * s}, P().bg, 4, true);
    Text(x + 12 * s, y + 12 * s, "VIEWPORT CONTROLS", P().accent, 9);
    Text(x + 12 * s, y + 35 * s, "Right drag: orbit · middle drag: pan", P().muted, 10);
    Text(x + 12 * s, y + 54 * s, "Wheel: zoom · F: frame selection", P().muted, 10);
    Text(x + 12 * s, y + 73 * s, "W / E / R: move / rotate / scale", P().muted, 10);
    ImGui::SetCursorScreenPos(ImVec2(x, y + 110 * s));
    ImGui::Dummy(ImVec2(1, 1));
}


// Actor tab of a curve object (uses the widgets above)
#include "CurveInspector.inl"

} // namespace app
