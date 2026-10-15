#pragma once
#include "imgui.h"

namespace mayashader {
// Draws the WZX settings window (Appearance, Graph); returns true when a setting changed (apply and save it).
bool drawSettingsWindow(bool* open, float dpi);
}
