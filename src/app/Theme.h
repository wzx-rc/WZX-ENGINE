// Editor themes. A theme is a text file in the Dear ImGui style-export format (the "Export" button of
// ImGui::ShowStyleEditor), read from assets/themes/*.theme:
//   colors[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.13f, 0.15f, 1.00f);
//   style.FrameRounding = 3.0f;
//   style.FramePadding = ImVec2(5.0f, 3.0f);
// plus optional editor lines:
//   wzx.name = Blue 01
//   wzx.accent = ImVec4(0.40f, 0.73f, 0.82f, 1.00f);   (also wzx.bg0 / bg1 / bg2 / field / hover / border / text / dim / select)
//   wzx.fontSize = 14
// The editor uses the Blue 01 theme (style metrics and dark colours). The user settings (dark / light mode, main colour =
// tint of the surfaces, accent colour, text / icon / layout size) are kept in wzx_settings.ini.
#pragma once
#include "imgui.h"
#include <algorithm>
#include <string>
#include <vector>

namespace app {

// Colours used by the custom-drawn parts of the editor (toolbar, cards, overlays...).
struct ThemePalette {
    ImVec4 bg0, bg1, bg2, field, hover, border, text, dim, faint, accent, select;
    ImVec4 bar;      // top bar / toolbar background
    ImVec4 strong;   // filled state of checked boxes and strong buttons
};

struct ThemeDef {
    std::string name;
    std::string file;                      // empty for the built-in theme
    bool hasColors[ImGuiCol_COUNT] = {};
    ImVec4 colors[ImGuiCol_COUNT];
    ImGuiStyle style;                      // metrics read from the file (on top of the editor defaults)
    ThemePalette palette;                  // derived from the colours unless given by wzx.* lines
    float fontSize = 15.0f;
};

struct UiSettings {
    std::string theme = "Atelier";        // "Atelier" (Blue 01, main / accent colours), "Ambre", "Corail" or "Halo" (drawn themes)
    bool light = false;
    bool customAccent = false;
    ImVec4 accent = ImVec4(0.400f, 0.729f, 0.816f, 1.0f);   // used when customAccent
    ImVec4 mainColor = ImVec4(0.415f, 0.506f, 0.578f, 1.0f); // tint of the surfaces (Blue Slate = the Blue 01 theme)
    ImVec4 corailMain = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);      // Anthracite-Corail: tint of its surfaces (Graphite = the pack colours)
    ImVec4 corailAccent = ImVec4(0.847f, 0.592f, 0.486f, 1.0f); // Anthracite-Corail: selection colour (#D8977C coral of the pack)
    ImVec4 haloMain = ImVec4(0.424f, 0.769f, 0.863f, 1.0f);   // Halo: main colour = selection and active tools (#6CC4DC ice blue)
    ImVec4 haloBg = ImVec4(0.051f, 0.063f, 0.075f, 1.0f);     // Halo: background (#0D1013), the panels are derived from it
    ImVec4 haloSecond = ImVec4(0.839f, 0.651f, 0.290f, 1.0f); // Halo: second colour = primary action (Build) and warnings (#D6A64A)
    float textScale = 1.0f;
    float iconScale = 1.0f;
    float layoutScale = 1.0f;             // spacing of the Atelier layouts: < 1 compact, > 1 spacious
    float globalScale = 1.0f;             // Global UI: multiplies the text, icon and layout sizes
    bool lockBrightness = true;           // Main Color: only the hue counts (false: its brightness too)
    std::string language = "English";     // UI language (only English for now)
    bool graphOutline = true;             // Graph tab: outline around each node
    float graphOutlineWeight = 0.5f;      // Graph tab: outline thickness in px (the selected node gets twice this)
    ImVec4 graphSelectColor = ImVec4(0.400f, 0.729f, 0.816f, 1.0f);   // Graph tab: outline of the selected node (cyan)
    float graphOutputHeight = 151.0f;     // Graph tab: height of the Output panel (DIPs), dragged from its top edge
};

namespace th {
// Theme list and settings
void loadThemes(const std::wstring& folder);          // built-in "Default" + the .theme files of the folder
const std::vector<ThemeDef>& themes();
UiSettings& settings();
inline bool ambre() { return settings().theme == "Ambre"; }     // Ambre: graphite + amber
inline bool corail() { return settings().theme == "Corail"; }   // Anthracite-Corail: anthracite + coral
inline bool halo() { return settings().theme == "Halo"; }       // Halo: graphite, one main colour, chamfers and HUD marks
inline int skin() { return ambre() ? 1 : corail() ? 2 : halo() ? 3 : 0; }   // drawn theme (atelier::Palette::skin)
inline bool styled() { return skin() != 0; }                    // drawn themes: always dark, own colours and drawing effects
// effective sizes: each size setting times Global UI
inline float textSize();
inline float iconSize();
inline float layoutSize();
void loadSettings(const std::wstring& file);
void saveSettings(const std::wstring& file);
void apply(float dpi);                                 // applies settings() to the ImGui style and the palette
ImVec4 tint(const ImVec4& c);                          // applies the main colour to a near-grey colour of the Blue 01 theme
const std::wstring& folder();

// Current palette, for custom drawing
const ThemePalette& P();
inline ImU32 u32(const ImVec4& c, float a = -1) { return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, a < 0 ? c.w : a)); }
inline ImU32 bg0() { return u32(P().bg0); }
inline ImU32 bg1() { return u32(P().bg1); }
inline ImU32 bg2() { return u32(P().bg2); }
inline ImU32 bar() { return u32(P().bar); }
inline ImU32 strong() { return u32(P().strong); }
inline ImVec4 strongV() { return P().strong; }
inline ImU32 field() { return u32(P().field); }
inline ImU32 card() { return u32(P().bg2); }
inline ImU32 hover() { return u32(P().hover); }
inline ImU32 border() { return u32(P().border); }
inline ImU32 text(float a = 1) { return u32(P().text, a); }
inline ImU32 dim() { return u32(P().dim); }
inline ImU32 faint() { return u32(P().faint); }
inline ImU32 accent(float a = 1) { return u32(P().accent, a); }
inline ImU32 select() { return u32(P().select); }
inline ImU32 overlay(float a) { return u32(P().bg0, a); }
inline ImVec4 accentV() { return P().accent; }
inline ImVec4 dimV() { return P().dim; }
inline ImVec4 faintV() { return P().faint; }
inline ImVec4 selectV() { return P().select; }
// accent readable as text / icon colour (lighter on dark themes, darker in light mode)
inline ImVec4 accentTextV() {
    const ImVec4& a = P().accent;
    if (settings().light && !styled()) return ImVec4(a.x * 0.75f, a.y * 0.75f, a.z * 0.75f, 1);
    return ImVec4(a.x + (1 - a.x) * 0.35f, a.y + (1 - a.y) * 0.35f, a.z + (1 - a.z) * 0.35f, 1);
}
inline float textSize() { return std::clamp(settings().textScale * settings().globalScale, 0.6f, 1.9f); }
inline float iconSize() { return std::clamp(settings().iconScale * settings().globalScale, 0.6f, 1.9f); }
inline float layoutSize() { return std::clamp(settings().layoutScale * settings().globalScale, 0.65f, 1.6f); }
} // namespace th

} // namespace app
