// BrickIt - the SVG icons of the web builder drawn with Dear ImGui: a small path interpreter (M L H V C A Z, absolute
// and relative) stroking or filling into an ImDrawList, plus circle and rounded rect helpers. Coordinates are in the
// icon's own space (viewBox origin `o`, `scale` pixels per unit).
#pragma once
#include "imgui.h"

namespace svg {

void path(ImDrawList* dl, const char* d, ImVec2 o, float scale, ImU32 col, float thick, bool fill = false);
void circle(ImDrawList* dl, float cx, float cy, float r, ImVec2 o, float scale, ImU32 col, float thick, bool fill = false,
            float dash = 0.0f);
void rect(ImDrawList* dl, float x, float y, float w, float h, float rx, ImVec2 o, float scale, ImU32 col, float thick);

// The icons of the web top bar and library (24 x 24 viewBox, stroke 2), drawn centred on c, `size` pixels wide.
enum Icon {
    I_UNDO, I_REDO, I_SELECT, I_VISOR, I_MOVE, I_ROTATE, I_GRID, I_PIVOT, I_SAFETY, I_FOCUS, I_ANIM, I_VOXELS, I_DELETE,
    I_MECH, I_WINDOWS, I_FILE, I_RECENT, I_FAV, I_SEARCH, I_SIZE, I_CLOSE, I_SETTINGS
};
void icon(ImDrawList* dl, Icon i, ImVec2 c, float size, ImU32 col);

} // namespace svg
