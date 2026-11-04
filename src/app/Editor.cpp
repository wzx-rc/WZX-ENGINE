// Editor shell: the Atelier workspaces (AtelierUI.cpp, atelier/*) plus the Settings, Controls and About windows,
// and the procedural thumbnails shared by the workspaces.
#include "App.h"
#include "Icons.h"

namespace app {

// --------------------------------------------------------------------------- helpers

// Label column + value column row used by the settings grid.
static void propLabel(const char* label, float labelW) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(th::dimV(), "%s", label);
    ImGui::SameLine(labelW);
}
// Procedural thumbnail of a primitive (shaded sphere / cube / shader ball).
void drawPrimitiveIcon(ImDrawList* dl, PartKind kind, ImVec2 a, ImVec2 b, ImU32 col) {
    ImVec4 c = ImGui::ColorConvertU32ToFloat4(col);
    auto shade = [&](float k, float alpha = 1) { return ImGui::ColorConvertFloat4ToU32(ImVec4(std::min(c.x * k, 1.0f), std::min(c.y * k, 1.0f), std::min(c.z * k, 1.0f), alpha)); };
    float cx = (a.x + b.x) * 0.5f, cy = (a.y + b.y) * 0.5f, s = std::min(b.x - a.x, b.y - a.y);
    if (kind == K_CUBE) {
        float r = s * 0.30f;
        ImVec2 top[4] = {{cx, cy - r * 1.05f}, {cx + r * 0.95f, cy - r * 0.5f}, {cx, cy + r * 0.05f}, {cx - r * 0.95f, cy - r * 0.5f}};
        ImVec2 left[4] = {{cx - r * 0.95f, cy - r * 0.5f}, {cx, cy + r * 0.05f}, {cx, cy + r * 1.15f}, {cx - r * 0.95f, cy + r * 0.6f}};
        ImVec2 right[4] = {{cx, cy + r * 0.05f}, {cx + r * 0.95f, cy - r * 0.5f}, {cx + r * 0.95f, cy + r * 0.6f}, {cx, cy + r * 1.15f}};
        dl->AddConvexPolyFilled(top, 4, shade(1.08f));
        dl->AddConvexPolyFilled(left, 4, shade(0.78f));
        dl->AddConvexPolyFilled(right, 4, shade(0.58f));
        return;
    }
    float R = s * 0.30f;
    float y = cy - (kind == K_SHADERBALL ? R * 0.15f : 0);
    if (kind == K_SHADERBALL) dl->AddEllipseFilled(ImVec2(cx, y + R * 1.08f), ImVec2(R * 1.12f, R * 0.30f), shade(0.45f));
    dl->AddCircleFilled(ImVec2(cx, y), R, shade(0.55f), 48);
    dl->AddCircleFilled(ImVec2(cx - R * 0.10f, y - R * 0.10f), R * 0.86f, shade(0.80f), 48);
    dl->AddCircleFilled(ImVec2(cx - R * 0.24f, y - R * 0.24f), R * 0.52f, shade(1.0f), 48);
    dl->AddCircleFilled(ImVec2(cx - R * 0.38f, y - R * 0.40f), R * 0.13f, IM_COL32(255, 255, 255, 170), 24);
}

static ImU32 paletteU32(int colorId) {
    const PaletteColor& p = palette()[findColor(colorId)];
    return IM_COL32(p.r, p.g, p.b, 255);
}

// Thumbnail of a content-browser asset: kind 0 primitive, 1 material preset, 2 scene.
void drawAssetThumb(ImDrawList* dl, int kind, int index, ImVec2 p0, ImVec2 p1, float d) {
    dl->AddRectFilled(p0, p1, th::bg0(), 4 * d);
    if (kind == 0) drawPrimitiveIcon(dl, parts()[index].kind, p0, p1, paletteU32(parts()[index].defaultColor));
    else if (kind == 1) {
        Material m;
        m.baseColor = float3(0.8f, 0.8f, 0.8f);
        ImU32 c = IM_COL32(200, 200, 205, 255);
        switch (index) {
        case PRESET_CHROME: c = IM_COL32(215, 220, 228, 255); break;
        case PRESET_BRUSHED: c = IM_COL32(160, 165, 172, 255); break;
        case PRESET_RUBBER: c = IM_COL32(60, 62, 66, 255); break;
        case PRESET_WAX: c = IM_COL32(236, 224, 208, 255); break;
        case PRESET_LACQUER: c = IM_COL32(200, 40, 40, 255); break;
        case PRESET_MATTE: c = IM_COL32(150, 150, 150, 255); break;
        case PRESET_EMISSIVE: c = IM_COL32(255, 200, 90, 255); break;
        case PRESET_PEARL: c = IM_COL32(225, 190, 140, 255); break;
        case PRESET_TRANSPARENT: c = IM_COL32(150, 200, 230, 170); break;
        default: c = IM_COL32(70, 120, 220, 255); break;
        }
        drawPrimitiveIcon(dl, K_SPHERE, p0, p1, c);
    } else {
        ImVec2 c((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
        float s = (p1.x - p0.x) * 0.22f;
        dl->AddRectFilled(ImVec2(c.x - s * 1.3f, c.y - s), ImVec2(c.x + s * 1.3f, c.y + s), index == SCENE_CORNELL ? IM_COL32(190, 185, 175, 255) : IM_COL32(40, 44, 52, 255), 3 * d);
        if (index == SCENE_CORNELL) {
            dl->AddRectFilled(ImVec2(c.x - s * 1.3f, c.y - s), ImVec2(c.x - s * 0.9f, c.y + s), IM_COL32(180, 30, 25, 255));
            dl->AddRectFilled(ImVec2(c.x + s * 0.9f, c.y - s), ImVec2(c.x + s * 1.3f, c.y + s), IM_COL32(40, 140, 30, 255));
        } else if (index == SCENE_LOOKDEV) {   // five reference spheres on a grey floor
            dl->AddRectFilled(ImVec2(c.x - s * 1.3f, c.y + s * 0.15f), ImVec2(c.x + s * 1.3f, c.y + s), IM_COL32(70, 72, 76, 255), 3 * d);
            const ImU32 cols[5] = {IM_COL32(150, 150, 150, 255), IM_COL32(215, 220, 228, 255), IM_COL32(150, 200, 230, 170),
                                   IM_COL32(255, 200, 110, 255), IM_COL32(236, 214, 170, 255)};
            for (int k = 0; k < 5; ++k) dl->AddCircleFilled(ImVec2(c.x + (k - 2) * s * 0.5f, c.y), s * 0.22f, cols[k]);
        } else if (index == SCENE_CONCEPT) {   // sphere, glowing cube, cylinder on a dark floor
            dl->AddRectFilled(ImVec2(c.x - s * 1.3f, c.y + s * 0.2f), ImVec2(c.x + s * 1.3f, c.y + s), IM_COL32(24, 28, 36, 255), 3 * d);
            dl->AddCircleFilled(ImVec2(c.x - s * 0.8f, c.y + s * 0.05f), s * 0.3f, IM_COL32(120, 124, 132, 255));
            dl->AddRectFilled(ImVec2(c.x - s * 0.25f, c.y - s * 0.2f), ImVec2(c.x + s * 0.25f, c.y + s * 0.3f), IM_COL32(110, 116, 128, 255));
            dl->AddRect(ImVec2(c.x - s * 0.27f, c.y - s * 0.22f), ImVec2(c.x + s * 0.27f, c.y + s * 0.32f), IM_COL32(90, 160, 255, 255), 0, 0, 1.2f * d);
            dl->AddRectFilled(ImVec2(c.x + s * 0.6f, c.y - s * 0.3f), ImVec2(c.x + s * 1.0f, c.y + s * 0.2f), IM_COL32(110, 114, 122, 255));
        } else {
            for (int k = -2; k <= 2; ++k) dl->AddLine(ImVec2(c.x + k * s * 0.5f, c.y - s), ImVec2(c.x + k * s * 0.5f, c.y + s), IM_COL32(90, 96, 108, 255));
            for (int k = -1; k <= 1; ++k) dl->AddLine(ImVec2(c.x - s * 1.3f, c.y + k * s * 0.5f), ImVec2(c.x + s * 1.3f, c.y + k * s * 0.5f), IM_COL32(90, 96, 108, 255));
        }
    }
}


// --------------------------------------------------------------------------- shell

void App::drawEditor() {
    m_viewportVisible = false;
    // The Atelier sources are authored for an 11 px Segoe UI base font scaled per window (SetWindowFontScale).
    // Their geometry follows the layout size; the text size is applied relative to it.
    ImGuiStyle& st = ImGui::GetStyle();
    float savedScale = st.FontScaleMain;
    st.FontScaleMain = th::textSize() / th::layoutSize();
    ImGui::PushFont(nullptr, 11.0f);
    drawAtelier();
    ImGui::PopFont();
    st.FontScaleMain = savedScale;
    if (m_showSettings) drawSettingsWindow();
    if (m_showGridOptions) drawGridOptionsWindow();
    if (m_showSnapOptions) drawSnapOptionsWindow();
    if (m_showAdvSettings) drawAdvScaleSettings();
    if (m_showShortcuts) drawShortcutsWindow();
    if (m_showAbout) drawAboutWindow();
}
// --------------------------------------------------------------------------- dialogs

// Grid button (viewport and its header): shows / hides the editor grid; with Shift, opens the Grid Options window.
void App::toggleGrid() {
    if (ImGui::GetIO().KeyShift) {
        m_showGridOptions = true;
        return;
    }
    GridOptions& g = th::settings().grid;
    g.visible = !g.visible;
    if (!m_themeTestOnly) th::saveSettings(m_exeDir + L"\\wzx_settings.ini");
    markDirty();
}

// Advanced Scale Settings: colours of the frame and of the handles, sizes in pixels, vertex snapping, flat mode, menu.
void App::drawAdvScaleSettings() {
    const float d = m_dpi;
    ImGui::SetNextWindowSize(ImVec2(440 * d, 0), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Advanced Scale Settings###advScaleSettings", &m_showAdvSettings, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    AdvScaleOptions& a = th::settings().advScale;
    bool changed = false;
    const float labelW = 200 * d, fieldW = 190 * d;
    auto section = [&](const char* title) {
        ImGui::Spacing();
        ImGui::TextColored(th::accentTextV(), "%s", title);
        ImGui::Separator();
    };
    auto color = [&](const char* label, const char* id, ImVec4* v) {
        propLabel(label, labelW);
        changed |= ImGui::ColorEdit3(id, &v->x);
    };
    auto slider = [&](const char* label, const char* id, float* v, float lo, float hi, const char* fmt) {
        propLabel(label, labelW);
        ImGui::SetNextItemWidth(fieldW);
        changed |= ImGui::SliderFloat(id, v, lo, hi, fmt);
    };
    section("Appearance");
    color("Edge color", "##aEdge", &a.colorEdge);
    color("Face handle color", "##aFace", &a.colorFace);
    color("Edge handle color", "##aEdgeH", &a.colorEdgeHandle);
    color("Corner handle color", "##aCorner", &a.colorCorner);
    section("Viewport menu");
    slider("Bottom offset", "##aOffset", &a.menuOffset, 0.0f, 300.0f, "%.0f px");
    propLabel("Show modifier hints", labelW);
    changed |= ImGui::Checkbox("##aHints", &a.showHints);
    section("Point handles");
    slider("Handle size", "##aSize", &a.handleSize, 3.0f, 20.0f, "%.1f px");
    slider("Interaction distance", "##aDist", &a.interactDist, 3.0f, 40.0f, "%.0f px");
    section("Vertex snapping");
    slider("Screen snap radius", "##aSnap", &a.snapRadius, 5.0f, 200.0f, "%.0f px");
    section("Flat mode");
    slider("Flat mode threshold", "##aFlat", &a.flatThreshold, 0.0f, 0.5f, "%.2f");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    if (ImGui::Button("Reset", ImVec2(120 * d, 0))) { a = AdvScaleOptions(); changed = true; }
    ImGui::SameLine();
    if (ImGui::Button("Close", ImVec2(120 * d, 0))) m_showAdvSettings = false;
    ImGui::End();
    static bool unsaved = false;   // saved once the edit is over
    if (changed) unsaved = true;
    if (unsaved && !ImGui::IsAnyItemActive()) {
        if (!m_themeTestOnly) th::saveSettings(m_exeDir + L"\\wzx_settings.ini");
        unsaved = false;
    }
}

// Snapping Options (Shift + the Snapping button): when it applies, the grid points of the absolute snapping, the
// steps, the vertex snapping (V), the placement on the surfaces, the pivot. Saved with the UI settings.
void App::drawSnapOptionsWindow() {
    const float d = m_dpi;
    ImGui::SetNextWindowSize(ImVec2(470 * d, 0), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Snapping Options###snapOptions", &m_showSnapOptions, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    SnapOptions& o = th::settings().snap;
    bool changed = false;
    const float labelW = 210 * d, fieldW = 200 * d;
    auto section = [&](const char* title) {
        ImGui::Spacing();
        ImGui::TextColored(th::accentTextV(), "%s", title);
        ImGui::Separator();
    };
    auto check = [&](const char* label, const char* id, bool* v) {
        propLabel(label, labelW);
        changed |= ImGui::Checkbox(id, v);
    };
    section("Snapping");
    check("Always on (else hold Shift)", "##snAlways", &m_magnet);
    check("Snap to the grid (absolute)", "##snAbs", &m_snapAbsolute);
    section("Grid points");
    static const char* points[3] = {"Intersections", "Centres of the cells", "Middles of the edges"};
    for (int k = 0; k < 3; ++k) {
        propLabel(k == 0 ? "Snap to the" : "", labelW);
        char id[24];
        snprintf(id, sizeof id, "%s##snPt%d", points[k], k);
        if (ImGui::RadioButton(id, o.gridPoint == k)) { o.gridPoint = k; changed = true; }
    }
    check("On the cells of the editor grid", "##snGridStep", &o.gridStep);
    propLabel("Else every (move step)", labelW);
    ImGui::SetNextItemWidth(fieldW);
    changed |= ImGui::SliderFloat("##snStep", &m_stepMove, 0.05f, 10.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    check("Show the snap points", "##snPoints", &o.showPoints);
    ImGui::TextDisabled("Cells of the snapping now: %.3g units", snapCell());
    section("Steps");
    propLabel("Rotate step", labelW);
    ImGui::SetNextItemWidth(fieldW);
    ImGui::SliderFloat("##snRot", &m_stepRotate, 1.0f, 90.0f, "%.0f deg");
    propLabel("Scale step", labelW);
    ImGui::SetNextItemWidth(fieldW);
    ImGui::SliderFloat("##snScale", &m_stepScale, 0.01f, 1.0f, "%.2f");
    section("Vertex snapping (hold V while moving)");
    propLabel("Screen radius", labelW);
    ImGui::SetNextItemWidth(fieldW);
    changed |= ImGui::SliderFloat("##snVtx", &o.vertexRadius, 5.0f, 200.0f, "%.0f px");
    section("Placement on the surfaces");
    check("Place (drag over the surfaces)", "##snPlace", &m_surfacePlace);
    check("Turn to the surface", "##snAlign", &o.alignToSurface);
    section("Pivot");
    ImGui::TextDisabled("Edit Pivot: C (or Insert) in and out, V snaps it to a vertex  ·  Center Pivot: Alt+Y");
    if (ImGui::Button("Center Pivot", ImVec2(150 * d, 0))) centerPivot();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    if (ImGui::Button("Reset", ImVec2(120 * d, 0))) { o = SnapOptions(); changed = true; }
    ImGui::SameLine();
    if (ImGui::Button("Close", ImVec2(120 * d, 0))) m_showSnapOptions = false;
    ImGui::End();
    static bool unsaved = false;   // saved once the edit is over
    if (changed) unsaved = true;
    if (unsaved && !ImGui::IsAnyItemActive()) {
        if (!m_themeTestOnly) th::saveSettings(m_exeDir + L"\\wzx_settings.ini");
        unsaved = false;
    }
}

// Grid Options (Maya's option box of the grid): size, display, line thickness in pixels, colours. Changes apply live
// and are saved with the UI settings.
void App::drawGridOptionsWindow() {
    const float d = m_dpi;
    ImGui::SetNextWindowSize(ImVec2(470 * d, 0), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Grid Options###gridOptions", &m_showGridOptions, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    GridOptions& g = th::settings().grid;
    bool changed = false;
    const float labelW = 190 * d, fieldW = 220 * d;
    auto section = [&](const char* title) {
        ImGui::Spacing();
        ImGui::TextColored(th::accentTextV(), "%s", title);
        ImGui::Separator();
    };
    auto check = [&](const char* label, const char* id, bool* v) {
        propLabel(label, labelW);
        changed |= ImGui::Checkbox(id, v);
    };
    auto width = [&](const char* label, const char* id, float* v) {
        propLabel(label, labelW);
        ImGui::SetNextItemWidth(fieldW);
        changed |= ImGui::SliderFloat(id, v, 0.25f, 6.0f, "%.2f px");
    };
    auto color = [&](const char* label, const char* id, ImVec4* v) {
        propLabel(label, labelW);
        changed |= ImGui::ColorEdit4(id, &v->x, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
    };

    section("Size");
    check("Use the scene spacing", "##sceneSpacing", &g.sceneSpacing);
    propLabel("Grid lines every", labelW);
    ImGui::SetNextItemWidth(fieldW);
    ImGui::BeginDisabled(g.sceneSpacing);
    changed |= ImGui::DragFloat("##every", g.sceneSpacing ? &m_scene.gridScale : &g.every, 0.05f, 0.01f, 10000.0f, "%.2f units",
                                ImGuiSliderFlags_Logarithmic);
    ImGui::EndDisabled();
    propLabel("Subdivisions", labelW);
    ImGui::SetNextItemWidth(fieldW);
    changed |= ImGui::SliderInt("##subdiv", &g.subdivisions, 1, 20);
    propLabel("Length and width", labelW);
    ImGui::SetNextItemWidth(fieldW);
    changed |= ImGui::DragFloat("##extent", &g.extent, 0.5f, 0.0f, 100000.0f, g.extent > 0 ? "%.1f units" : "infinite");

    section("Display");
    check("Grid lines", "##lines", &g.lines);
    check("Subdivision lines", "##subdivLines", &g.subdivLines);
    check("Axes", "##axes", &g.axes);
    check("Thicker line for axes", "##thickAxes", &g.thickAxes);
    check("Axis arrows and letters", "##arrows", &g.arrows);
    check("Grid numbers on axes", "##numbers", &g.numbers);
    check("Fade with distance", "##fade", &g.fade);

    section("Line thickness (pixels)");
    width("Grid lines", "##wGrid", &g.widthGrid);
    width("Subdivision lines", "##wSub", &g.widthSub);
    ImGui::BeginDisabled(!g.thickAxes);
    width("Axes", "##wAxis", &g.widthAxis);
    ImGui::EndDisabled();

    section("Color");
    color("Grid lines", "##cGrid", &g.colorGrid);
    color("Subdivision lines", "##cSub", &g.colorSub);
    color("X axis", "##cX", &g.colorX);
    color("Y axis", "##cY", &g.colorY);
    color("Z axis", "##cZ", &g.colorZ);
    propLabel("Opacity", labelW);
    ImGui::SetNextItemWidth(fieldW);
    changed |= ImGui::SliderFloat("##opacity", &g.opacity, 0.0f, 1.0f, "%.2f");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    const float bw = 120 * d;
    if (ImGui::Button(g.visible ? "Hide Grid" : "Show Grid", ImVec2(bw, 0))) { g.visible = !g.visible; changed = true; }
    ImGui::SameLine();
    if (ImGui::Button("Reset", ImVec2(bw, 0))) {
        const bool visible = g.visible;
        g = GridOptions();
        g.visible = visible;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Close", ImVec2(bw, 0))) m_showGridOptions = false;
    ImGui::End();
    // saved once the edit is over (not every frame of a slider drag)
    static bool unsaved = false;
    if (changed) {
        markDirty();
        unsaved = true;
    }
    if (unsaved && !ImGui::IsAnyItemActive()) {
        if (!m_themeTestOnly) th::saveSettings(m_exeDir + L"\\wzx_settings.ini");
        unsaved = false;
    }
}

void App::drawShortcutsWindow() {
    ImGui::SetNextWindowSize(ImVec2(520 * m_dpi, 0), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::Begin("Controls & Shortcuts", &m_showShortcuts, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse)) {
        static const char* rows[][2] = {
            {"Right click (or Alt+left click)", "Orbit"}, {"Alt+middle / Shift+right click", "Pan the view"}, {"Wheel", "Zoom"},
            {"Double-click", "Pivot + focus"}, {"Click / rectangle", "Select"}, {"Shift / Ctrl / Ctrl+Shift+click", "Toggle / remove / add"},
            {"Q / W / E / R", "Select / move / rotate / scale"}, {"Q W E R + left click (hold)", "Marking menu"},
            {"Shift while dragging", "Increments"}, {"Middle drag", "Manipulator: handle under the mouse, else the last one"},
            {"Arrows, PgUp / PgDn", "Move by 1 cm"}, {"F", "Frame Selection"},
            {"T", "Auto rotate"}, {"P", "Real time / path tracing"}, {"Ctrl+Z / Ctrl+Y", "Undo / redo"},
            {"Ctrl+D / Del", "Duplicate / delete"}, {"Ctrl+S / Ctrl+O", "Save / open a scene"}, {"F1 / F2", "Cornell Box / Village scenes"},
            {"F12", "Viewport Capture"}, {"Drag an asset", "Add to the view / apply a material"}};
        if (ImGui::BeginTable("keys", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
            for (auto& r : rows) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(th::accentTextV(), "%s", r[0]);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(r[1]);
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

void App::drawSettingsWindow() {
    const float d = m_dpi;
    ImGui::SetNextWindowSize(ImVec2(560 * d, 0), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin(ICON_SETTINGS "  Settings###settings", &m_showSettings, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
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
            const char* themeNames[] = {"Atelier (Blue 01)", "Ambre (graphite + amber)", "Anthracite-Corail (charcoal + coral)", "Spartan (new editor layout)"};
            const char* themeIds[] = {"Atelier", "Ambre", "Corail", "Spartan"};
            const int skin = th::skin();
            if (ImGui::BeginCombo("##uitheme", themeNames[skin])) {
                for (int i = 0; i < 4; ++i)
                    if (ImGui::Selectable(themeNames[i], skin == i)) { s.theme = themeIds[i]; changed = true; }
                ImGui::EndCombo();
            }
            const bool amb = skin != 0;
            if (amb) {
                indent();
                ImGui::TextColored(th::dimV(), skin == 1 ? "Fixed colours: dark mode, amber accent." : skin == 2 ? "Dark mode; main and selection colours of its own."
                                                                                                        : "Dark mode; main, background and second colours below.");
            }
            if (skin == 3) {
                // Halo: three colours; the panels and borders are lighter steps of the background
                auto colorRow = [&](const char* label, const char* id, ImVec4& value, const Preset* presets, int count, const char* tip) {
                    ImGui::Spacing();
                    propLabel(label, labelW);
                    int k = presetRow(id, presets, count, value);
                    if (k >= 0) { value = presets[k].c; changed = true; }
                    indent();
                    ImGui::SetNextItemWidth(-1);
                    float c3[3] = {value.x, value.y, value.z};
                    if (ImGui::ColorEdit3((std::string("##") + id).c_str(), c3, ImGuiColorEditFlags_DisplayHex)) { value = ImVec4(c3[0], c3[1], c3[2], 1); changed = true; }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
                };
                static const Preset haloMains[] = {{"Ice", ImVec4(0.424f, 0.769f, 0.863f, 1)},   {"Cobalt", ImVec4(0.290f, 0.560f, 0.960f, 1)},
                                                   {"Signal", ImVec4(0.420f, 0.820f, 0.560f, 1)}, {"Visor", ImVec4(0.910f, 0.663f, 0.235f, 1)},
                                                   {"Red", ImVec4(0.880f, 0.360f, 0.340f, 1)},    {"White", ImVec4(0.850f, 0.880f, 0.900f, 1)}};
                static const Preset haloBgs[] = {{"Graphite", ImVec4(0.051f, 0.063f, 0.075f, 1)}, {"Black", ImVec4(0.035f, 0.035f, 0.040f, 1)},
                                                 {"Navy", ImVec4(0.039f, 0.059f, 0.098f, 1)},     {"Olive", ImVec4(0.067f, 0.075f, 0.059f, 1)},
                                                 {"Space", ImVec4(0.043f, 0.071f, 0.078f, 1)},    {"Steel", ImVec4(0.090f, 0.100f, 0.110f, 1)}};
                static const Preset haloSeconds[] = {{"Amber", ImVec4(0.839f, 0.651f, 0.290f, 1)}, {"Orange", ImVec4(0.960f, 0.550f, 0.200f, 1)},
                                                     {"Ice", ImVec4(0.424f, 0.769f, 0.863f, 1)},   {"Green", ImVec4(0.500f, 0.780f, 0.600f, 1)},
                                                     {"Red", ImVec4(0.880f, 0.360f, 0.340f, 1)},   {"Steel", ImVec4(0.700f, 0.740f, 0.780f, 1)}};
                colorRow("Main Color", "haloMain", s.haloMain, haloMains, 6, "Selection, active tools, tabs, sliders and checkboxes");
                colorRow("Background", "haloBg", s.haloBg, haloBgs, 6, "Window background; panels, fields and borders are lighter steps of it");
                colorRow("Second Color", "haloSecond", s.haloSecond, haloSeconds, 6, "Primary action (Build) and warnings");
            } else {
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
            }   // skin != 3
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
        if (ImGui::BeginTabItem("Viewport")) {
            ImGui::Spacing();
            propLabel("Statistics", labelW);
            ImGui::Checkbox("##stats", &m_showStats);
            propLabel("Gizmo", labelW);
            ImGui::Checkbox("##gizmo", &m_showGizmo);
            propLabel("Snapping always", labelW);
            ImGui::Checkbox("##magnet", &m_magnet);
            propLabel("Auto focus", labelW);
            ImGui::Checkbox("##af", &m_autoFocus);
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
    if (changed) {
        th::apply(m_dpi);
        if (!m_themeTestOnly) th::saveSettings(m_exeDir + L"\\wzx_settings.ini");
    }
    ImGui::End();
}

void App::drawAboutWindow() {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::Begin("About", &m_showAbout, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        ImGui::PushFont(m_gui.fontBold, 0);
        ImGui::TextUnformatted("WZX ENGINE — editor");
        ImGui::PopFont();
        ImGui::TextColored(th::dimV(), "vp3d engine: DirectX 12 + DXR 1.1, progressive path tracing and raster viewport.");
        ImGui::TextColored(th::dimV(), "Interface: Dear ImGui %s, Atelier workspaces, Segoe UI font.", IMGUI_VERSION);
        ImGui::Text("GPU : %s", m_renderer.adapterName().c_str());
    }
    ImGui::End();
}

} // namespace app
