// "Atelier" layout, Scene workspace: the engine scene editor drawn in the visual language of the Atelier
// Graph Editor (atelier/archive/atelier-graph/former Atelier graph.cpp): same header, panel titles, buttons, metrics (1600 x 900 reference)
// and the shared Atelier palette (atelier/atelier_style.h), fed by the theme settings.
#include "App.h"
#include "GameHost.h"
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
        if (P().skin == 2) Glyph(icon, ix, iy, 13, c);   // Corail: no glow
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
            Box(pill, IM_COL32(36, 39, 43, 255), 5);
            Text(pill.x + 7 * s, pill.y + (pill.h - z.y) / 2, tag, P().muted, 8);
        }
        return;
    }
    if (P().skin) {   // surface gradient with a title band (structure-header.svg)
        afx::Panel(dl, r.min(), r.max(), s, 34 * s);
        GlowGlyph(icon, r.x + 12 * s, r.y + 11 * s, 13, iconColor, iconColor, iconColor == P().accent ? 0.06f : 0.0f);
        Text(r.x + 33 * s, r.y + 10 * s, title, P().text, 13);
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
    a.push_back({2, SCENE_STUDIO, "Studio", "Scene", "Scenes"});
    a.push_back({2, SCENE_LOOKDEV, "LookDev", "Scene", "Scenes"});
    return a;
}

} // namespace

// --------------------------------------------------------------------------- shared palette

// Feeds the Atelier modules (graph, material studio, inspector, scene) with the current theme settings:
// dark mode: the Atelier reference colours with the main colour tint (Blue Slate gives the exact values),
// light mode: derived from the light palette.
static void syncAtelierStyle(ImFont* bold) {
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
        a.menuX = 168; a.menuW = 46; a.headerShift = 65;   // room for the WZX ENGINE brand
        return;
    }
    if (a.skin == 2) {
        // Anthracite-Corail: colours of the pack; the graph keeps its Atelier node style on charcoal surfaces
        using namespace atelier::fx::coral;
        using atelier::fx::col::rgb;
        a.light = false;
        a.accent = accent; a.select = selection; a.strong = rgb(0xB94F2F);
        a.bg = shell; a.panel = panel; a.raised = raised; a.hover = rgb(0x23262A); a.bar = shell; a.border = border;
        a.text = text; a.muted = muted; a.mutedLight = rgb(0xB4B8BE);
        a.canvas = viewport; a.node = rgb(0x2A2D31); a.nodeOff = rgb(0x1C1E21);
        a.gridDot = rgb(0x3A3E44, 150); a.gridLine = rgb(0x2A2D31, 95);
        a.overlay = rgb(0x181A1C, 230); a.minimap = rgb(0x181A1C, 240); a.headerBar = card; a.popupHeader = rgb(0x24272B);
        a.track = fill; a.frameHover = borderStrong; a.checkerA = rgb(0x3F4248); a.checkerB = rgb(0x2A2E33);
        a.toast = rgb(0x24201E, 245); a.knob = thumb; a.pinExec = rgb(0xE2E4E7);
        a.menuX = 168; a.menuW = 46; a.headerShift = 65;   // room for the WZX Engine brand
        return;
    }
    a.menuX = 53; a.menuW = 53; a.headerShift = 0;
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
    syncAtelierStyle(m_gui.fontBold);
    if (m_workspace == 2) {
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

// --------------------------------------------------------------------------- header (former Atelier graph HeaderBar)

void App::atlTopBar(float x0, float y0, float width) {
    Box({x0, y0, width, 37 * s}, P().bar, 0);
    if (P().skin) {   // topbar.svg: gold cube and WZX ENGINE wordmark, bottom border
        dl->AddLine(ImVec2(x0, y0 + 37 * s - 0.5f), ImVec2(x0 + width, y0 + 37 * s - 0.5f), P().border);
        afx::Brand(dl, font, P().fontBold, ImVec2(x0 + 13 * s, y0 + 6.5f * s), s);
    } else {
        Glyph(Icon::Cube, x0 + 15 * s, y0 + 10 * s, 17, P().accent);
    }
    const float sep = P().menuX - 10;
    dl->AddLine(ImVec2(x0 + sep * s, y0 + 7 * s), ImVec2(x0 + sep * s, y0 + 30 * s), P().border);
    const char* menus[] = {"Project", "Edit", "View", "Tools", "Help"};
    float x = x0 + P().menuX * s;
    auto save = [&] { if (!m_themeTestOnly) th::saveSettings(m_exeDir + L"\\wzx_settings.ini"); };
    for (int mi = 0; mi < 5; ++mi) {
        std::string id = "atlmenu" + std::to_string(mi);
        R r = {x, y0 + 6 * s, P().menuW * s, 24 * s};
        if (Hit(id.c_str(), r)) ImGui::OpenPopup(id.c_str());
        if (ImGui::IsItemHovered()) Box(r, P().raised);
        Center(r, menus[mi], P().text, 11);
        if (ImGui::BeginPopup(id.c_str())) {
            if (mi == 0) {
                if (ImGui::MenuItem("Open Scene…", "Ctrl+O")) m_pendingAction = 2;
                if (ImGui::MenuItem("Save Scene…", "Ctrl+S")) m_pendingAction = 1;
                ImGui::Separator();
                if (ImGui::MenuItem("Cornell Box Scene", "F1", m_sceneId == SCENE_CORNELL)) m_pendingScene = SCENE_CORNELL;
                if (ImGui::MenuItem("Studio Scene", "F2", m_sceneId == SCENE_STUDIO)) m_pendingScene = SCENE_STUDIO;
                if (ImGui::MenuItem("LookDev Scene", "F3", m_sceneId == SCENE_LOOKDEV)) m_pendingScene = SCENE_LOOKDEV;
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
                    for (const PartDef& p : parts())
                        if (ImGui::MenuItem(toUtf8(p.name).c_str())) addPartFromLibrary(p);
                    ImGui::EndMenu();
                }
            }
            if (mi == 2) {
                if (ImGui::MenuItem("Frame Selection", "F")) frameSelection();
                ImGui::MenuItem("Viewport Statistics", nullptr, &m_showStats);
                ImGui::MenuItem("Transform Gizmo", nullptr, &m_showGizmo);
                if (ImGui::MenuItem("Restore Panel Layout")) { m_atl.leftW = 222; m_atl.rightW = 284; m_atl.bottomH = 190; }
                ImGui::Separator();
                if (ImGui::MenuItem("Light Mode", nullptr, th::settings().light && !th::styled(), !th::styled())) { th::settings().light = !th::settings().light; th::apply(m_dpi); save(); }
                if (ImGui::BeginMenu("Theme")) {
                    const char* names[] = {"Atelier", "Ambre", "Anthracite-Corail"}, *ids[] = {"Atelier", "Ambre", "Corail"};
                    for (int i = 0; i < 3; ++i)
                        if (ImGui::MenuItem(names[i], nullptr, th::skin() == i)) { th::settings().theme = ids[i]; th::apply(m_dpi); save(); }
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem("Appearance Settings…")) m_showSettings = true;
            }
            if (mi == 3) {
                if (ImGui::MenuItem("Select", "Q", m_tool == TOOL_SELECT)) m_tool = TOOL_SELECT;
                if (ImGui::MenuItem("Move", "W", m_tool == TOOL_MOVE)) m_tool = TOOL_MOVE;
                if (ImGui::MenuItem("Rotate", "E", m_tool == TOOL_ROTATE)) m_tool = TOOL_ROTATE;
                if (ImGui::MenuItem("Scale", "R", m_tool == TOOL_SCALE)) m_tool = TOOL_SCALE;
                ImGui::Separator();
                ImGui::MenuItem("Snapping (1 cm / 90°)", nullptr, &m_magnet);
                if (ImGui::MenuItem("Local Axes", nullptr, m_localSpace)) m_localSpace = !m_localSpace;
                ImGui::SetNextItemWidth(150 * s);
                ImGui::SliderFloat("Camera speed", &m_camSpeed, 0.1f, 5.0f, "%.1f");
            }
            if (mi == 4) {
                if (ImGui::MenuItem("Controls & Shortcuts")) m_showShortcuts = true;
                if (ImGui::MenuItem("About")) m_showAbout = true;
            }
            ImGui::EndPopup();
        }
        x += P().menuW * s;
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
    Tip("Build the game for a platform: Windows now, iOS coming soon (F7)");
    const bool playing = m_game && m_game->playing();
    if (Button("playScene", {cx + 109 * s, y0 + 6 * s, 71 * s, 25 * s}, playing ? "Stop" : "Play", Icon::Play, playing, false, m_game != nullptr))
        m_game->requestPlay();
    Tip("Test the game in the editor (F5)");
    Button("stepScene", {cx + 186 * s, y0 + 6 * s, 27 * s, 25 * s}, "", Icon::Step, false, true, false);
    Tip("Test player: coming in a next step");
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

void App::atlHierarchy(float x, float y, float w, float h) {
    AtelierState& A = m_atl;
    R r = {x, y, w, h};
    Panel(r, Icon::Scene, "Scene", "LEVEL", P().accent);
    float px = r.x + 10 * s, py = r.y + 44 * s, pw = r.w - 20 * s;
    g_labelRight = px + pw;
    Label(px, py, "SCENES");
    py += 21 * s;
    const char* scenes[] = {"Cornell Box", "Studio", "LookDev"};
    for (int i = 0; i < 3; ++i) {
        char id[16];
        snprintf(id, sizeof(id), "scene%d", i);
        if (Button(id, {px, py, pw, 26 * s}, scenes[i], i == 0 ? Icon::Cube : Icon::Sun, m_sceneId == i) && m_sceneId != i) m_pendingScene = i;
        py += 30 * s;
    }
    py += 14 * s;
    dl->AddLine(ImVec2(px, py), ImVec2(px + pw, py), P().border);
    py += 16 * s;
    size_t count = m_scene.objects.size() + m_scene.lights.size();
    Text(px, py, "HIERARCHY", P().text, 11);
    std::string cnt = std::to_string(count) + " items";
    Text(px + pw - TextSize(cnt, 9).x, py + 2 * s, cnt, P().muted, 9);
    py += 24 * s;
    Search("##atlSceneSearch", {px, py, pw, 28 * s}, A.sceneSearch, sizeof(A.sceneSearch), "Search scene...");
    py += 36 * s;
    if (Button("addObject", {px, py, pw, 23 * s}, "Add Object", Icon::Plus)) ImGui::OpenPopup("atlNewActor");
    Tip("Add a primitive to the scene (or drag one from the Content Browser)");
    if (ImGui::BeginPopup("atlNewActor")) {
        for (const PartDef& p : parts())
            if (ImGui::MenuItem(toUtf8(p.name).c_str())) addPartFromLibrary(p);
        ImGui::EndPopup();
    }
    py += 33 * s;

    ImGui::SetCursorScreenPos(ImVec2(px, py));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("atlTree", ImVec2(pw, std::max(20.0f, r.y + r.h - py - 9 * s)), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImGui::SetWindowFontScale(atelier::Style().layoutScale);   // child text independent of the layout size
    ImGui::PopStyleVar();
    dl = ImGui::GetWindowDrawList();
    float yy = ImGui::GetCursorScreenPos().y;
    const float rowH = 27 * s;
    bool filtering = A.sceneSearch[0] != 0;

    // category heading of former Atelier graph (coloured, 10 px) with a fold arrow
    auto heading = [&](const char* id, const char* name, ImU32 color, bool& open, int n) {
        R hr = {px, yy, pw, 21 * s};
        if (Hit(id, hr)) open = !open;
        Glyph(open || filtering ? Icon::ChevronDown : Icon::ChevronRight, px + 3 * s, yy + 5 * s, 9, color);
        Text(px + 17 * s, yy + 2 * s, name, color, 10);
        std::string c = std::to_string(n);
        Text(px + pw - 8 * s - TextSize(c, 9).x, yy + 3 * s, c, P().muted, 9);
        // drop an asset on a heading: add it to the scene
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("WZX_ASSET")) addPartFromLibrary(parts()[*(const int*)pl->Data]);
            ImGui::EndDragDropTarget();
        }
        yy += 23 * s;
        return open || filtering;
    };
    // row of the graph node library: icon, name 11 px, eye / lock on the right
    auto row = [&](const char* id, Icon icon, ImU32 iconColor, const std::string& name, bool selected, bool dimmed, bool* eyeOn, bool* lockOn) -> bool {
        R rr = {px, yy, pw, rowH - 2 * s};
        ImGui::PushID(id);
        bool q = Hit("node", {rr.x, rr.y, rr.w - 48 * s, rr.h});
        if (selected) SelectedRow(rr);
        else if (ImGui::IsItemHovered()) Box(rr, P().raised, 3);
        Glyph(icon, px + 8 * s, yy + 7 * s, 13, dimmed ? P().muted : iconColor);
        dl->PushClipRect(ImVec2(px, yy), ImVec2(px + pw - 50 * s, yy + rowH), true);
        Text(px + 31 * s, yy + 7 * s, name, dimmed ? P().muted : P().text, 11);
        dl->PopClipRect();
        if (eyeOn) {
            if (Hit("eye", {px + pw - 44 * s, yy + 2 * s, 20 * s, rowH - 6 * s})) { pushUndo(); *eyeOn = !*eyeOn; markDirty(); }
            Tip(*eyeOn ? "Hide" : "Show");
            Glyph(Icon::Eye, px + pw - 40 * s, yy + 8 * s, 11, *eyeOn ? P().muted : Fade(P().muted, 90));
        }
        if (lockOn) {
            if (Hit("lock", {px + pw - 22 * s, yy + 2 * s, 20 * s, rowH - 6 * s})) *lockOn = !*lockOn;
            Tip(*lockOn ? "Unlock" : "Lock (not selectable)");
            Glyph(Icon::Lock, px + pw - 18 * s, yy + 8 * s, 11, *lockOn ? P().accent : Fade(P().muted, 90));
        }
        ImGui::PopID();
        yy += rowH;
        return q;
    };

    std::vector<int> env, objs;
    for (size_t i = 0; i < m_scene.objects.size(); ++i) {
        if (m_scene.objects[i].ghost) continue;
        (m_scene.objects[i].part == "WALL" || m_scene.objects[i].part == "GROUND" ? env : objs).push_back((int)i);
    }
    auto objectRow = [&](int i) {
        Object& o = m_scene.objects[i];
        if (!matches(o.name, A.sceneSearch) && !matches(o.part, A.sceneSearch)) return;
        bool sel = i == m_selected || std::find(m_multi.begin(), m_multi.end(), i) != m_multi.end();
        bool locked = !o.selectable;
        char id[32];
        snprintf(id, sizeof(id), "obj%d", i);
        float rowY = yy;
        const Material& mt = m_scene.materials[o.material];
        bool q = row(id, Icon::Cube, srgbU32(mt.baseColor), o.name, sel, !o.visible, &o.visible, &locked);
        if (locked == o.selectable) { o.selectable = !locked; if (!o.selectable && m_selected == i) select(-1); }
        ImGui::PushID(id);
        ImVec2 mp = ImGui::GetMousePos();
        if (ImGui::IsMouseClicked(1) && ImGui::IsWindowHovered() && mp.y >= rowY && mp.y < rowY + rowH && mp.x >= px && mp.x < px + pw - 50 * s)
            ImGui::OpenPopup("ctx");
        if (ImGui::BeginPopup("ctx")) {
            if (o.selectable) select(i);
            if (ImGui::MenuItem("Frame", "F")) frameSelection();
            if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, o.selectable)) duplicateSelected();
            if (ImGui::MenuItem("Delete", "Del", false, o.selectable)) deleteSelected();
            ImGui::EndPopup();
        }
        ImGui::PopID();
        if (q) {
            if (!o.selectable) status(L"Locked object: unlock it to select it", 2);
            else if (ImGui::GetIO().KeyShift && m_selected >= 0 && m_selected != i) {
                auto it = std::find(m_multi.begin(), m_multi.end(), i);
                if (it != m_multi.end()) m_multi.erase(it); else m_multi.push_back(i);
                markDirty();
            } else select(i);
        }
    };
    // category colours of the graph node families
    if (heading("hObjects", "Objects", Family(IM_COL32(92, 192, 213, 255)), A.folderOpen[2], (int)objs.size())) for (int i : objs) objectRow(i);
    yy += 6 * s;
    if (heading("hLights", "Lights", Family(IM_COL32(212, 188, 109, 255)), A.folderOpen[3], (int)m_scene.lights.size() + 1)) {
        // the directional light of the scene (the sun): edited in the Lighting tab
        bool sunOn = m_scene.sun.enabled;
        if (row("sunLight", Icon::Sun, IM_COL32(242, 201, 76, 255), "Directional Light", false, !m_scene.sun.enabled, &sunOn, nullptr)) A.inspectorTab = 1;
        if (sunOn != m_scene.sun.enabled) { m_scene.sun.enabled = sunOn; markDirty(); }
        for (size_t i = 0; i < m_scene.lights.size(); ++i) {
            AreaLight& l = m_scene.lights[i];
            if (!matches(l.name, A.sceneSearch)) continue;
            char id[32];
            snprintf(id, sizeof(id), "light%zu", i);
            bool on = l.enabled;
            if (row(id, Icon::Light, IM_COL32(242, 201, 76, 255), l.name, false, !l.enabled, &on, nullptr)) A.inspectorTab = 1;
            if (on != l.enabled) { l.enabled = on; markDirty(); }
        }
    }
    yy += 6 * s;
    if (heading("hEnv", "Environment", Family(IM_COL32(134, 189, 144, 255)), A.folderOpen[1], (int)env.size())) for (int i : env) objectRow(i);
    ImGui::SetCursorScreenPos(ImVec2(px, yy + 4 * s));
    ImGui::Dummy(ImVec2(1, 1));
    ImGui::EndChild();
    dl = ImGui::GetWindowDrawList();
}

// --------------------------------------------------------------------------- viewport (former Atelier graph CanvasHeader + Canvas + Footer)

void App::atlViewport(float x, float y, float w, float h) {
    // header
    R hr = {x, y, w, 35 * s};
    if (P().skin == 2) afx::FloatBar(dl, hr.min(), hr.max(), s);   // Corail: floating tool bar with its shadow
    else if (P().skin) {   // header strip of the pack (header gradient, border, light edge)
        afx::Gradient(dl, hr.min(), hr.max(), afx::col::headerTop, afx::col::headerBottom, 4 * s);
        dl->AddRect(hr.min(), hr.max(), P().border, 4 * s);
        afx::TopEdge(dl, hr.min(), hr.max(), s, IM_COL32(181, 186, 192, 255), 0.10f);
    } else Box(hr, P().panel, 3, true);
    Glyph(Icon::Scene, hr.x + 12 * s, hr.y + 11 * s, 13, P().accent);
    std::string name = m_scene.name.empty() ? "Scene" : m_scene.name;
    Text(hr.x + 34 * s, hr.y + 11 * s, Fit(name, 140 * s, 12), P().text, 12);
    float nx = hr.x + 34 * s + std::min(TextSize(name, 12).x, 140 * s) + 12 * s;
    Glyph(Icon::ChevronRight, nx, hr.y + 13 * s, 9, P().muted);
    static const char* views[] = {"Perspective", "Front", "Back", "Left", "Right", "Top"};
    Text(nx + 16 * s, hr.y + 12 * s, std::string("Viewport · ") + views[std::clamp(m_viewMode, 0, 5)], P().muted, 11);
    // tools (right to left): shading, view | stats, grid, frame | snapping, axes, scale, rotate, move
    float bx = hr.x + hr.w - 8 * s, by = hr.y + 6 * s;
    auto right = [&](float bw) { bx -= bw; float r0 = bx; bx -= 6 * s; return r0; };
    static const char* modes[] = {"Lit", "Metal iOS", "Progressive", "GI"};
    int mode = renderMode();
    if (Combo("atlShading", {right(118 * s), by, 118 * s, 24 * s}, mode, modes, 4, Icon::Sun)) setRenderMode(mode);
    Tip("Lit: real-time raster · Metal iOS: Lit + GI probes without ray tracing, as the iOS build · Progressive: path tracing, exact, denoiser optional · GI: real-time probes (DDGI, ray traced)");
    if (Combo("atlView", {right(118 * s), by, 118 * s, 24 * s}, m_viewMode, views, 6, Icon::Camera)) setViewAxis(m_viewMode);
    bx -= 6 * s;
    if (Button("atlStats", {right(28 * s), by, 28 * s, 24 * s}, "", Icon::Monitor, m_showStats, true)) m_showStats = !m_showStats;
    Tip("Viewport statistics");
    {
        bool gridOn = false;
        for (const Object& o : m_scene.objects)
            if (o.part == "GROUND" && o.material >= 0 && o.material < (int)m_scene.materials.size()) gridOn = (m_scene.materials[o.material].flags & 1u) != 0;
        if (Button("atlGrid", {right(28 * s), by, 28 * s, 24 * s}, "", Icon::Grid, gridOn, true)) toggleGroundGrid();
        Tip("Ground grid");
    }
    {
        const bool placing = m_game && m_game->placementMode();
        if (Button("atlVillage", {right(28 * s), by, 28 * s, 24 * s}, "", Icon::Cube, placing, true) && m_game) m_game->togglePlacement();
        Tip("RTS village: place houses on the grid (stop Play first)");
    }
    if (Button("atlFrame", {right(28 * s), by, 28 * s, 24 * s}, "", Icon::Home, false, true)) frameSelection();
    Tip("Frame selection (F)");
    bx -= 6 * s;
    if (Button("atlSnap", {right(28 * s), by, 28 * s, 24 * s}, "", Icon::Magnet, m_magnet, true)) m_magnet = !m_magnet;
    Tip("Snapping: 1 cm / 90° / x0.1");
    {
        float lx = right(76 * s);
        if (Button("atlSpace", {lx, by, 76 * s, 24 * s}, m_localSpace ? "Local" : "World", Icon::World)) m_localSpace = !m_localSpace;
        Tip("Gizmo axes: object (Local) or scene (World)");
    }
    if (Button("atlScale", {right(28 * s), by, 28 * s, 24 * s}, "", Icon::Scale, m_tool == TOOL_SCALE, true)) m_tool = TOOL_SCALE;
    Tip("Scale (R)");
    if (Button("atlRotate", {right(28 * s), by, 28 * s, 24 * s}, "", Icon::Rotate, m_tool == TOOL_ROTATE, true)) m_tool = TOOL_ROTATE;
    Tip("Rotate by 90° steps (E)");
    if (Button("atlMove", {right(28 * s), by, 28 * s, 24 * s}, "", Icon::Move, m_tool == TOOL_MOVE, true)) m_tool = TOOL_MOVE;
    Tip("Move (W)");

    // canvas
    R r = {x, y + 40 * s, w, h - 40 * s - 27 * s};
    Box(r, P().canvas, P().skin == 2 ? 8 : 3, P().skin != 2);   // Corail: borderless rounded viewport card
    viewportCanvas(ImVec2(r.x + 1, r.y + 1), ImVec2(r.w - 2, r.h - 2));   // image, input and gizmo; overlays below
    dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(r.min(), r.max(), true);
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
    if (m_showStats) {
        R st = {r.x + r.w - 165 * s, r.y + 12 * s, 151 * s, 113 * s};
        Box(st, P().minimap, 4, true);
        Text(st.x + 10 * s, st.y + 10 * s, "VIEWPORT", P().accent, 9);
        char v[5][32];
        snprintf(v[0], 32, "%.0f FPS", 1000.0f / std::max(m_frameMs, 0.1f));
        snprintf(v[1], 32, "%.1f ms", m_frameMs);
        if (m_rs.raster) snprintf(v[2], 32, "%.2f ms", m_renderer.gpuFrameMs());
        else snprintf(v[2], 32, "%u / %u", std::min<uint32_t>(m_renderer.sampleCount(), m_rs.maxSamples), m_rs.maxSamples);
        snprintf(v[3], 32, "%s", triText);
        snprintf(v[4], 32, "%zu · %zu", objs, m_scene.lights.size());
        const char* names[] = {"Frame rate", "Frame time", m_rs.raster ? "GPU" : "Samples", "Triangles", "Objects · lights"};
        for (int i = 0; i < 5; ++i) {
            Text(st.x + 10 * s, st.y + (30 + i * 16) * s, names[i], P().muted, 9);
            Text(st.x + 92 * s, st.y + (30 + i * 16) * s, v[i], P().text, 9);
        }
    }
    // orientation axes, turned with the camera (Y up)
    {
        float3 cr, cu, cf;
        m_scene.camera.basis(cr, cu, cf);
        ImVec2 o(r.x + 36 * s, r.y + r.h - 32 * s);
        const ImU32 cols[] = {IM_COL32(222, 110, 121, 255), IM_COL32(136, 199, 150, 255), IM_COL32(113, 168, 233, 255)};
        const char* names[] = {"X", "Y", "Z"};
        for (int a = 0; a < 3; ++a) {
            float3 av(a == 0 ? 1.0f : 0.0f, a == 1 ? 1.0f : 0.0f, a == 2 ? 1.0f : 0.0f);
            ImVec2 e(o.x + dot(av, cr) * 24 * s, o.y - dot(av, cu) * 24 * s);
            dl->AddLine(o, e, cols[a], 2 * s);
            dl->AddCircleFilled(e, 3 * s, cols[a]);
            Text(e.x + (e.x >= o.x ? 4 : -10) * s, e.y - 6 * s, names[a], cols[a], 9);
        }
        dl->AddCircleFilled(o, 2.5f * s, P().text);
    }
    // path tracing progress
    if (!m_rs.raster) {
        float prog = clampf((float)m_renderer.sampleCount() / m_rs.maxSamples, 0, 1);
        ImVec2 a(r.x + r.w * 0.5f - 120 * s, r.y + r.h - 10 * s);
        dl->AddRectFilled(a, ImVec2(a.x + 240 * s, a.y + 3 * s), Fade(P().bg, 160), 2 * s);
        dl->AddRectFilled(a, ImVec2(a.x + 240 * s * prog, a.y + 3 * s), prog >= 1 ? kGreen : P().accent, 2 * s);
    }
    dl->PopClipRect();
    // footer (former Atelier graph Footer): state of the tools and of the renderer
    R fr = {x, r.y + r.h + 3 * s, w, 24 * s};
    if (P().skin == 2) afx::FloatBar(dl, fr.min(), fr.max(), s);   // Corail: floating status bar
    else if (P().skin) { Box(fr, afx::col::footer, 0); dl->AddLine(ImVec2(fr.x, fr.y + 0.5f), ImVec2(fr.x + fr.w, fr.y + 0.5f), P().border); }
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
    static const char* modeNames[] = {"●  Real time", "●  Metal iOS", "●  Progressive PT", "●  GI probes"};
    Text(fr.x + fr.w - 116 * s, fr.y + 6 * s, modeNames[renderMode()], m_rs.raster ? kGreen : P().accent, 9);
}

// --------------------------------------------------------------------------- bottom panel (former Atelier graph Output)

void App::atlContent(float x, float y, float w, float h) {
    AtelierState& A = m_atl;
    R r = {x, y, w, h};
    if (P().skin) afx::Panel(dl, r.min(), r.max(), s, 33 * s);   // the tab row sits in the title band
    else Box(r, P().panel, 4, true);
    const char* names[] = {"Content Browser", "Materials", "Camera Views", "Output Log"};
    Icon icons[] = {Icon::Cube, Icon::Material, Icon::Camera, Icon::List};
    const float widths[] = {140, 100, 122, 108};
    float tx = r.x + 8 * s;
    A.contentTab = std::clamp(A.contentTab, 0, 3);
    for (int i = 0; i < 4; ++i) {
        char id[24];
        snprintf(id, sizeof(id), "contentTab%d", i);
        if (TabButton(id, {tx, r.y + 4 * s, widths[i] * s, (P().skin ? 29 : 25) * s}, names[i], icons[i], A.contentTab == i)) A.contentTab = i;
        tx += (widths[i] + 5) * s;
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
        float sw = std::min(210 * s, std::max(80 * s, rx - 45 * s - tx));
        Search("##atlAssetSearch", {rx - 43 * s - sw, r.y + 5 * s, sw, 23 * s}, A.assetSearch, sizeof(A.assetSearch), "Search assets...");
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
        float tree = 128 * s, details = 176 * s;
        // folders (graph library rows)
        R ft = {body.x + 8 * s, body.y + 2 * s, tree - 14 * s, body.h - 6 * s};
        float fy = ft.y;
        Icon folderIcons[] = {Icon::FolderOpen, Icon::Cube, Icon::Material, Icon::Scene};
        for (int f = 0; f < 4; ++f) {
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
        dl->AddLine(ImVec2(body.x + tree, body.y + 2 * s), ImVec2(body.x + tree, body.y + body.h - 4 * s), P().border);
        // grid
        R grid = {body.x + tree + 2 * s, body.y, body.w - tree - details - 4 * s, body.h};
        ImGui::SetCursorScreenPos(grid.min());
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("atlAssetGrid", ImVec2(grid.w, grid.h), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
        ImGui::SetWindowFontScale(atelier::Style().layoutScale);   // child text independent of the layout size
        ImGui::PopStyleVar();
        dl = ImGui::GetWindowDrawList();
        float gy = ImGui::GetCursorScreenPos().y + 4 * s, thumb = std::max(56.0f, A.thumb) * s, gap = 9 * s;
        int cols = std::max(1, (int)((grid.w - 20 * s + gap) / (thumb + gap))), shown = 0;
        float gx = grid.x + 10 * s;
        for (size_t i = 0; i < assets.size(); ++i) {
            const Asset& a = assets[i];
            if (A.folder != 0 && a.kind != A.folder - 1) continue;
            if (!matches(a.name, A.assetSearch)) continue;
            ImGui::PushID((int)i);
            R card = A.listMode ? R{gx, gy, grid.w - 22 * s, 38 * s} : R{gx, gy, thumb, thumb + 24 * s};
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
            if (q) { m_scene.camera = v.cam; m_activeView = (int)i; markDirty(); }
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
    Panel(r, Icon::Info, "Inspector", tags[A.inspectorTab], P().text);
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
    float by = r.y + 76 * s;
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
        Text(x + 12 * s, y + 54 * s, "or in the scene hierarchy", P().muted, 10);
        Text(x + 12 * s, y + 73 * s, "Shift+click adds to the selection", P().muted, 10);
        ImGui::SetCursorScreenPos(ImVec2(x, y + 110 * s));
        ImGui::Dummy(ImVec2(1, 1));
        return;
    }
    Material& mat = m_scene.materials[o->material];
    const PartDef* pd = findPart(o->part);
    ImU32 col = srgbU32(mat.baseColor);
    // header card (graph inspector node card)
    Box({x, y, w, 56 * s}, Fade(P().accent, 30), 4, true);
    drawPrimitiveIcon(dl, pd ? pd->kind : K_CUBE, ImVec2(x + 9 * s, y + 12 * s), ImVec2(x + 39 * s, y + 42 * s), col);
    dl->PushClipRect(ImVec2(x, y), ImVec2(x + w - 34 * s, y + 56 * s), true);
    Text(x + 48 * s, y + 11 * s, o->name, P().text, 14);
    std::string type = pd ? "Primitive · " + o->part : (o->part == "WALL" ? "Wall · static mesh" : "Mesh · " + o->part);
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
    if (show("Static Mesh")) {
        Label(x, y, "STATIC MESH");
        y += 24 * s;
        R slot = {x, y, w, 44 * s};
        Box(slot, P().bg, 4, true);
        drawPrimitiveIcon(dl, pd ? pd->kind : K_CUBE, ImVec2(slot.x + 7 * s, slot.y + 6 * s), ImVec2(slot.x + 39 * s, slot.y + 38 * s), IM_COL32(190, 194, 200, 255));
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
    {   // the directional light of the scene (the sun): sharp shadows, one light
        SunLight& sun = m_scene.sun;
        ImGui::PushID("sun");
        Box({x, y, w, 56 * s}, Fade(IM_COL32(242, 201, 76, 255), 30), 4, true);
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
    if (ch) markDirty();
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    ImGui::Dummy(ImVec2(1, 1));
}

// Options of the render engine in use. Each mode has its own: the viewport mode (raster) has none of the path
// tracing ones, and the progressive and GI engines have theirs. Each option has an info icon.
void App::atlInspectorRender(float x, float y, float w, float) {
    bool ch = false, act = false;
    static const char* names[] = {"Real time (raster)", "Metal iOS (raster + GI probes)", "Progressive path tracing", "GI probes (DDGI, ray traced)"};
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
    } else {
        Label(x, y, "PROBES");
        y += 24 * s;
        sliderI("##pb", "Probe budget", m_rs.ddgiProbeBudget, 256, 8192, "%.0f", "Number of irradiance probes over the scene. More = finer indirect light, more GPU cost.");
        sliderF("##hy", "Hysteresis", m_rs.ddgiHysteresis, 0.80f, 0.999f, "%.3f", "How much each probe keeps its previous value per frame. Higher = steadier, slower to react.");
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
    Text(x, y, m_sceneId == SCENE_CORNELL ? "Test scene · Cornell Box" : (m_sceneId == SCENE_LOOKDEV ? "Test scene · LookDev" : "Test scene · Studio"), P().muted, 11);
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
    static const char* modes[] = {"Real time", "Metal iOS", "Progressive", "GI probes (DDGI)"};
    int mode = renderMode();
    if (Combo("worldRender", {x, y, w, 27 * s}, mode, modes, 4, Icon::Sun)) setRenderMode(mode);
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

} // namespace app
