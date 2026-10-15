// Settings window of the WZX Engine editor (App::drawSettingsWindow in src/app/Editor.cpp), Appearance and Graph tabs:
// theme, mode, main / accent colours, sizes and the graph outlines. Same settings file format (wzx_settings.ini).
#include "SettingsWindow.h"
#include "app/Icons.h"
#include "app/Theme.h"
#include <cmath>

namespace mayashader {

using namespace app;

static void propLabel(const char* label, float labelW) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(th::dimV(), "%s", label);
    ImGui::SameLine(labelW);
}

bool drawSettingsWindow(bool* open, float dpi) {
    const float d = dpi;
    ImGui::SetNextWindowSize(ImVec2(560 * d, 0), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin(ICON_SETTINGS "  Settings###settings", open, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return false;
    }
    UiSettings& s = th::settings();
    bool changed = false;
    const float labelW = 150 * d;
    struct Preset { const char* name; ImVec4 c; };
    // row of colour presets after the label column; returns the clicked preset or -1
    auto presetRow = [&](const char* id, const Preset* presets, int count, const ImVec4& current) {
        int clicked = -1;
        ImGui::PushID(id);
        for (int i = 0; i < count; ++i) {
            ImGui::PushID(i);
            if (i) ImGui::SameLine(0, 6 * d);
            ImVec2 p = ImGui::GetCursorScreenPos();
            if (ImGui::ColorButton(presets[i].name, presets[i].c, ImGuiColorEditFlags_NoTooltip, ImVec2(22 * d, 22 * d))) clicked = i;
            const ImVec4& c = presets[i].c;
            if (std::fabs(c.x - current.x) + std::fabs(c.y - current.y) + std::fabs(c.z - current.z) < 0.01f)
                ImGui::GetWindowDrawList()->AddRect(ImVec2(p.x - 2 * d, p.y - 2 * d), ImVec2(p.x + 24 * d, p.y + 24 * d), th::text(), 3 * d, 0, 1.5f * d);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", presets[i].name);
            ImGui::PopID();
        }
        ImGui::PopID();
        return clicked;
    };
    auto indent = [&] {
        ImGui::Dummy(ImVec2(labelW - ImGui::GetStyle().ItemSpacing.x, 1));
        ImGui::SameLine();
    };
    auto percent = [&](const char* label, const char* id, float& value, float lo, float hi, const char* fmt) {
        propLabel(label, labelW);
        ImGui::SetNextItemWidth(-1);
        float pct = value * 100.0f;
        if (ImGui::SliderFloat(id, &pct, lo, hi, fmt)) { value = pct / 100.0f; changed = true; }
    };

    if (ImGui::BeginTabBar("settings_tabs")) {
        if (ImGui::BeginTabItem("Appearance")) {
            ImGui::Spacing();
            // language (English only for now)
            propLabel("Language", labelW);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##language", s.language.c_str())) {
                if (ImGui::Selectable("English", s.language == "English")) { s.language = "English"; changed = true; }
                ImGui::BeginDisabled();
                ImGui::Selectable("Français (coming later)");
                ImGui::EndDisabled();
                ImGui::EndCombo();
            }
            // theme: Atelier (Blue 01, tinted by the main / accent colours below), Ambre or Anthracite-Corail (fixed colours)
            propLabel("Theme", labelW);
            ImGui::SetNextItemWidth(-1);
            const char* themeNames[] = {"Atelier (Blue 01)", "Ambre (graphite + amber)", "Anthracite-Corail (charcoal + coral)"};
            const char* themeIds[] = {"Atelier", "Ambre", "Corail"};
            const int skin = th::skin();
            if (ImGui::BeginCombo("##uitheme", themeNames[skin])) {
                for (int i = 0; i < 3; ++i)
                    if (ImGui::Selectable(themeNames[i], skin == i)) { s.theme = themeIds[i]; changed = true; }
                ImGui::EndCombo();
            }
            const bool amb = skin != 0;
            if (amb) {
                indent();
                ImGui::TextColored(th::dimV(), skin == 1 ? "Fixed colours: dark mode, amber accent." : "Dark mode; main and selection colours of its own.");
            }
            // mode and accent only apply to the Atelier theme; the main colour to Atelier and Anthracite-Corail (its own value)
            ImGui::BeginDisabled(amb);
            // mode
            propLabel("Mode", labelW);
            int mode = s.light ? 1 : 0;
            if (ImGui::RadioButton("Dark", &mode, 0)) { s.light = false; changed = true; }
            ImGui::SameLine(0, 18 * d);
            if (ImGui::RadioButton("Light", &mode, 1)) { s.light = true; changed = true; }
            ImGui::EndDisabled();
            // main colour: tint of the interface surfaces (neutral black to blue-grey...)
            ImGui::BeginDisabled(skin == 1);
            ImVec4& mainRef = skin == 2 ? s.corailMain : s.mainColor;
            ImGui::Spacing();
            propLabel("Main Color", labelW);
            static const Preset mains[] = {{"Graphite", ImVec4(0.5f, 0.5f, 0.5f, 1)},       {"Blue Slate", ImVec4(0.415f, 0.506f, 0.578f, 1)},
                                           {"Midnight", ImVec4(0.36f, 0.46f, 0.66f, 1)},    {"Forest", ImVec4(0.42f, 0.55f, 0.46f, 1)},
                                           {"Plum", ImVec4(0.55f, 0.45f, 0.60f, 1)},        {"Umber", ImVec4(0.60f, 0.50f, 0.41f, 1)}};
            int m = presetRow("main", mains, 6, mainRef);
            if (m >= 0) { mainRef = mains[m].c; changed = true; }
            indent();
            ImGui::SetNextItemWidth(-1);
            float m3[3] = {mainRef.x, mainRef.y, mainRef.z};
            if (ImGui::ColorEdit3("##maincolor", m3, ImGuiColorEditFlags_DisplayHex)) { mainRef = ImVec4(m3[0], m3[1], m3[2], 1); changed = true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tint of the panels and backgrounds");
            indent();
            if (ImGui::Checkbox("Lock brightness", &s.lockBrightness)) changed = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("On: only the hue of the main colour counts (constant contrast).\nOff: its brightness also lightens or darkens the panels (test).");
            ImGui::EndDisabled();
            ImGui::BeginDisabled(skin == 1);
            // accent colour: selection, active buttons, highlights (Anthracite-Corail: its own selection colour)
            ImGui::Spacing();
            propLabel(skin == 2 ? "Selection Color" : "Accent Color", labelW);
            static const Preset accents[] = {{"Cyan", ImVec4(0.400f, 0.729f, 0.816f, 1)},   {"Blue", ImVec4(0.235f, 0.502f, 0.980f, 1)},
                                             {"Violet", ImVec4(0.560f, 0.420f, 0.960f, 1)}, {"Green", ImVec4(0.270f, 0.700f, 0.420f, 1)},
                                             {"Yellow", ImVec4(0.960f, 0.770f, 0.200f, 1)}, {"Orange", ImVec4(0.960f, 0.550f, 0.200f, 1)},
                                             {"Pink", ImVec4(0.910f, 0.360f, 0.620f, 1)},   {"Red", ImVec4(0.900f, 0.290f, 0.290f, 1)}};
            static const Preset corailAccents[] = {{"Coral", ImVec4(0.847f, 0.592f, 0.486f, 1)}, {"Amber", ImVec4(0.925f, 0.643f, 0.239f, 1)},
                                                   {"Orange", ImVec4(0.960f, 0.550f, 0.200f, 1)}, {"Red", ImVec4(0.900f, 0.290f, 0.290f, 1)},
                                                   {"Pink", ImVec4(0.910f, 0.360f, 0.620f, 1)},   {"Violet", ImVec4(0.560f, 0.420f, 0.960f, 1)},
                                                   {"Cyan", ImVec4(0.400f, 0.729f, 0.816f, 1)},   {"Green", ImVec4(0.270f, 0.700f, 0.420f, 1)}};
            if (skin == 2) {
                int a = presetRow("corailAccent", corailAccents, 8, s.corailAccent);
                if (a >= 0) { s.corailAccent = corailAccents[a].c; changed = true; }
                indent();
                ImGui::SetNextItemWidth(-1);
                float c3[3] = {s.corailAccent.x, s.corailAccent.y, s.corailAccent.z};
                if (ImGui::ColorEdit3("##corailAccent", c3, ImGuiColorEditFlags_DisplayHex)) { s.corailAccent = ImVec4(c3[0], c3[1], c3[2], 1); changed = true; }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Selected rows, active tools, tabs and checkboxes");
            } else {
                ImVec4 curAccent = s.customAccent ? s.accent : accents[0].c;
                int a = presetRow("accent", accents, 8, curAccent);
                if (a >= 0) { s.accent = accents[a].c; s.customAccent = a != 0; changed = true; }
                indent();
                ImGui::SetNextItemWidth(-1);
                float c3[3] = {curAccent.x, curAccent.y, curAccent.z};
                if (ImGui::ColorEdit3("##accent", c3, ImGuiColorEditFlags_DisplayHex)) { s.accent = ImVec4(c3[0], c3[1], c3[2], 1); s.customAccent = true; changed = true; }
            }
            ImGui::EndDisabled();
            // sizes
            ImGui::Spacing();
            percent("Global UI", "##globalscale", s.globalScale, 80, 130, "%.0f %%");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Multiplies the text, icon and layout sizes below");
            ImGui::Dummy(ImVec2(1, 2 * d));
            percent("Text size", "##textscale", s.textScale, 80, 140, "%.0f %%");
            percent("Icon size", "##iconscale", s.iconScale, 80, 140, "%.0f %%");
            percent("Layout size", "##layoutscale", s.layoutScale, 85, 120, "%.0f %%");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Spacing between the elements: lower is more compact");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            if (ImGui::Button(ICON_REFRESH "  Default Settings")) {
                std::string theme = s.theme;   // the theme choice is kept
                s = UiSettings();
                s.theme = theme;
                changed = true;
            }
            ImGui::SameLine();
            ImGui::TextColored(th::dimV(), "Dark, Blue Slate, cyan accent, all sizes 100 %%");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Graph")) {
            ImGui::Spacing();
            propLabel("Node outline", labelW);
            changed |= ImGui::Checkbox("##nodeOutline", &th::settings().graphOutline);
            propLabel("Outline weight", labelW);
            changed |= ImGui::SliderFloat("##outlineWeight", &th::settings().graphOutlineWeight, 0.1f, 3.0f, "%.2f px");
            propLabel("Selection outline", labelW);
            ImVec4& sel = th::settings().graphSelectColor;
            float sel3[3] = {sel.x, sel.y, sel.z};
            if (ImGui::ColorEdit3("##selectOutline", sel3, ImGuiColorEditFlags_DisplayHex)) { sel = ImVec4(sel3[0], sel3[1], sel3[2], 1); changed = true; }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
    return changed;
}

} // namespace mayashader
