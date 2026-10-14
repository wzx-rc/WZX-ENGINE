// Drawing effects of the Ambre theme (graphite + amber), taken from the "Ambre UI" design pack
// (01 - INBOX/UI/Ambre_UI_Composants: components/*.svg, theme.json): vertical gradients of the surfaces and
// buttons, the light top edge, the amber halo of the active buttons, the selected rows, the underlined tabs,
// the gold brand mark and the flat silver slider handle. Sizes are in reference pixels times the scale s.
#pragma once
#include "imgui.h"

namespace atelier::fx {

// colours of the pack (theme.json and the gradients of the component SVGs)
namespace col {
constexpr ImU32 rgb(unsigned hex, int a = 255) { return IM_COL32((hex >> 16) & 255, (hex >> 8) & 255, hex & 255, a); }
constexpr ImU32 canvas = rgb(0x101114), panel = rgb(0x1A1C20), input = rgb(0x14161A), raised = rgb(0x24272C);
constexpr ImU32 border = rgb(0x363A40), borderBtn = rgb(0x373D43), fieldBorder = rgb(0x333940);
constexpr ImU32 text = rgb(0xD9DADF), secondary = rgb(0x959AA3), disabled = rgb(0x787D85);
constexpr ImU32 accent = rgb(0xECA43D), accentHi = rgb(0xFFD18B), accentPressed = rgb(0xB5772C), accentBorder = rgb(0x8B6028);
constexpr ImU32 selection = rgb(0x33271B), hover = rgb(0x29231C), modified = rgb(0xB28A5B), primaryText = rgb(0x17120B);
constexpr ImU32 thumb = rgb(0xC7C9CB), topBar = rgb(0x171B1E), footer = rgb(0x121519), checkOff = rgb(0x59626D), checkBg = rgb(0x16191C);
constexpr ImU32 axisX = rgb(0xD46A62), axisY = rgb(0x85B476), axisZ = rgb(0x64A8D4);
// gradients (top, bottom) of the pack
constexpr ImU32 surfaceTop = rgb(0x22262A), surfaceBottom = rgb(0x171A1D);
constexpr ImU32 headerTop = rgb(0x25282B), headerBottom = rgb(0x1A1D20);
constexpr ImU32 buttonTop = rgb(0x252A2F), buttonBottom = rgb(0x1B1F23);
constexpr ImU32 hoverTop = rgb(0x2D3036), hoverBottom = rgb(0x212328);
constexpr ImU32 activeTop = rgb(0x3A2B17), activeBottom = rgb(0x241D14);
constexpr ImU32 primaryTop = rgb(0xFFC460), primaryBottom = rgb(0xECA43D), primaryBorder = rgb(0xDCAD59);
constexpr ImU32 selectedLeft = rgb(0x553916), selectedRight = rgb(0x33271B);
} // namespace col

ImU32 Alpha(ImU32 c, float a);   // c with its alpha multiplied by a

// Filled rounded rectangle with a linear gradient: vertical (top -> bottom) or horizontal (left -> right).
void Gradient(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 top, ImU32 bottom, float rounding, ImDrawFlags flags = 0);
void GradientH(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 left, ImU32 right, float rounding = 0);
// Soft halo around a rounded rectangle (the amber-glow filter of the pack: blurred copy at 48 % alpha).
// spread: halo width in pixels; strength: alpha of the first ring.
void Glow(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 color, float rounding, float spread, float strength = 0.45f);
// The 1 px light line along the top inside edge of buttons and panels ("M3 1.5H41", 14 % alpha).
void TopEdge(ImDrawList* dl, ImVec2 a, ImVec2 b, float s, ImU32 color = IM_COL32(195, 199, 205, 255), float alpha = 0.14f);

enum class Face { Normal, Hover, Active, Primary, Disabled };
// Background, border, light edge and glow of a button; returns the colour of its icon and label.
ImU32 Button(ImDrawList* dl, ImVec2 a, ImVec2 b, Face face, float s, float rounding = 4);
// Panel: surface gradient, border, light edge. headerH > 0 adds the title band (header gradient + separator).
void Panel(ImDrawList* dl, ImVec2 a, ImVec2 b, float s, float headerH = 0, float rounding = 4);
// Selected row of a tree or list: brown gradient + 2 px amber bar on the left.
void SelectedRow(ImDrawList* dl, ImVec2 a, ImVec2 b, float s);
// Underlined tab: 2 px amber line under an active tab, a faint one on hover.
void TabLine(ImDrawList* dl, ImVec2 a, ImVec2 b, bool active, bool hovered, float s);
// Highlight of a selected card / thumbnail: amber border and halo.
void SelectedFrame(ImDrawList* dl, ImVec2 a, ImVec2 b, float s, float rounding = 4);
// Text field frame (focus: amber border).
void Field(ImDrawList* dl, ImVec2 a, ImVec2 b, float s, bool focus, float rounding = 3);
// Axis tag of an X / Y / Z field: tinted strip on the left of the field (axis 0, 1, 2).
ImU32 AxisColor(int axis);
void AxisTag(ImDrawList* dl, ImVec2 a, ImVec2 b, int axis, float s);
// Slider of the pack: 3 px track filled in amber up to t, flat 5 x 16 silver handle (grey when disabled).
void SliderTrack(ImDrawList* dl, float x0, float x1, float y, float t, bool enabled, bool focus, float s);
// Overlay of an ImGui checkbox drawn at box (a, b): amber border and tick when on, grey border when off.
void Checkbox(ImDrawList* dl, ImVec2 a, ImVec2 b, bool on, bool hovered, float s);
// Gold isometric cube of the WZX brand, in a size x size square at p.
void BrandMark(ImDrawList* dl, ImVec2 p, float size);
// Brand "WZX ENGINE": gold cube, WZX bold, ENGINE light and spaced. Returns the right end.
float Brand(ImDrawList* dl, ImFont* font, ImFont* bold, ImVec2 p, float s);
// Text with extra spacing between the letters.
float SpacedText(ImDrawList* dl, ImFont* font, float size, ImVec2 p, ImU32 color, const char* text, float spacing);
// Small offsets used to draw the soft halo of a vector icon (draw the icon at each offset with a low alpha).
extern const ImVec2 kGlowOffsets[8];

} // namespace atelier::fx
