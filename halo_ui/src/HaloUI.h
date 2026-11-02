// Halo editor UI: a from-scratch Dear ImGui reproduction of design/halo/mockup-halo.html (sizes in CSS pixels).
#pragma once
#include "imgui.h"

namespace halo {

struct Fonts {
    ImFont* ui = nullptr;     // Inter in the mockup  -> Segoe UI
    ImFont* hud = nullptr;    // Barlow Condensed     -> Bahnschrift
    ImFont* mono = nullptr;   // JetBrains Mono       -> Consolas
};

void Init(ImTextureID iconAtlas, int atlasW, int atlasH, const Fonts& fonts);
void Draw(float dpiScale);

} // namespace halo
