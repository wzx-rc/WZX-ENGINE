// Shared colours and text size of the Atelier modules (scene, graph, material studio, inspector).
// The defaults are the exact Atelier "Blue 01" values; the engine updates them every frame from its
// theme settings (theme, light / dark mode, main colour, text size).
#pragma once
#include "imgui.h"

namespace atelier {

struct Palette {
    // surfaces
    ImU32 bg = IM_COL32(23, 28, 32, 255);          // window background, fields
    ImU32 panel = IM_COL32(28, 33, 38, 255);       // panels
    ImU32 raised = IM_COL32(37, 43, 49, 255);      // buttons, headers
    ImU32 hover = IM_COL32(48, 58, 66, 255);       // hovered button
    ImU32 bar = IM_COL32(21, 26, 30, 255);         // top bar
    ImU32 border = IM_COL32(49, 57, 64, 255);
    ImU32 canvas = IM_COL32(22, 27, 31, 255);      // graph canvas
    ImU32 node = IM_COL32(34, 40, 46, 255);        // graph node body
    ImU32 nodeOff = IM_COL32(29, 34, 39, 255);     // disabled node body
    ImU32 gridDot = IM_COL32(62, 71, 78, 150);
    ImU32 gridLine = IM_COL32(43, 51, 58, 95);
    ImU32 overlay = IM_COL32(35, 43, 50, 220);     // boxes drawn over a picture / canvas
    ImU32 minimap = IM_COL32(28, 34, 40, 240);     // graph overview, preview statistics
    ImU32 toast = IM_COL32(30, 47, 54, 245);
    ImU32 headerBar = IM_COL32(26, 31, 36, 255);   // inspector tab strip
    ImU32 popupHeader = IM_COL32(32, 39, 46, 255);
    ImU32 track = IM_COL32(53, 64, 74, 255);       // slider track
    ImU32 frameHover = IM_COL32(70, 84, 95, 255);  // field outline on hover
    ImU32 checkerA = IM_COL32(67, 76, 85, 255), checkerB = IM_COL32(120, 129, 138, 255);
    // text
    ImU32 text = IM_COL32(201, 208, 216, 255);
    ImU32 muted = IM_COL32(132, 143, 155, 255);
    ImU32 mutedLight = IM_COL32(153, 165, 177, 255);
    ImU32 knob = IM_COL32(214, 222, 229, 255);      // slider handle
    ImU32 pinExec = IM_COL32(219, 226, 233, 255);
    // accents
    ImU32 accent = IM_COL32(102, 186, 208, 255);
    ImU32 select = IM_COL32(43, 72, 88, 255);
    ImU32 strong = IM_COL32(58, 126, 188, 255);
    // user sizes (Settings): geometry follows layoutScale; text and icons are relative to it
    float textScale = 1.0f;   // text size / layout size
    float iconScale = 1.0f;   // icon size / layout size
    float layoutScale = 1.0f; // layout size (spacing): multiplies the module scale
    bool light = false;
    // Drawn themes: 0 Atelier (flat boxes), 1 Ambre (graphite + amber: gradients, glow, light edges),
    // 2 Corail (anthracite + coral: rounded cards, floating bars, sliders with the value inside).
    // When non zero the modules draw their buttons, panels and tabs with the effects of ambre_fx.h.
    int skin = 0;
    ImFont* fontBold = nullptr;   // semibold text of the Ambre tabs and primary buttons (null: regular font)
    // top bar: start and width of the menus, offset of the workspace buttons (the Ambre brand is wider)
    float menuX = 53, menuW = 53, headerShift = 0;
};

Palette& Style();

} // namespace atelier
