// BrickIt - compact panels for a 4K monitor (WZX Engine theme): toolbar on top, library on two rows at the bottom
// (fixed height), colours on the right, stats on the left, toasts above the library.
#include "BrickApp.h"
#include "app/Icons.h"
#include "app/Theme.h"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace brickit {

namespace th = app::th;

#define ICON_CLOCK u8""
#define ICON_DUP   u8""
#define ICON_TURN  u8""
#define ICON_TRASH u8""
#define ICON_UNDO_ u8""
#define ICON_REDO_ u8""
#define ICON_SAVE_ u8""
#define ICON_OPEN_ u8""
#define ICON_FIND  u8""
#define ICON_STAR_ u8""
#define ICON_STARF u8""
#define ICON_SIZE  u8""
#define ICON_FOCUS_ u8""
#define ICON_GRID_ u8""
#define ICON_RT    u8""
#define ICON_CUBE_ u8""

static const ImGuiWindowFlags kPanel = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus;

// French labels of the LDraw categories and the LDraw families (same tree as the web builder).
static const char* catFr(const std::string& c) {
    static const std::pair<const char*, const char*> t[] = {
        {"Brick", "Briques"}, {"Plate", "Plaques"}, {"Tile", "Tuiles"}, {"Slope", "Pentes"}, {"Wedge", "Biseaux"}, {"Arch", "Arches"},
        {"Cylinder", "Cylindres"}, {"Cone", "Cônes"}, {"Dish", "Paraboles"}, {"Sphere", "Sphères"}, {"Technic", "Technic"},
        {"Panel", "Panneaux"}, {"Bracket", "Équerres"}, {"Hinge", "Charnières"}, {"Wheel", "Roues"}};
    for (auto& e : t)
        if (c == e.first) return e.second;
    return c.c_str();
}

// Row 1: LDraw families (as the web builder); row 2: the categories of the family (Technic: its sub-families).
void BrickApp::buildNav() {
    m_nav.clear();
    m_families = {"Basique", "Formes", "Technic"};
    m_nav.push_back({"Récents", 0, ""});
    m_nav.push_back({"Favoris", 1, ""});
    static const char* basic[] = {"Brick", "Plate", "Tile", "Baseplate", "Bracket", "Panel"};
    static const char* shapes[] = {"Slope", "Wedge", "Arch", "Cylinder", "Cone", "Dish", "Sphere"};
    auto addCats = [&](const char* const* cats, int n, int fam) {
        for (int k = 0; k < n; ++k) {
            bool any = false;
            for (const Part& p : m_lib.parts) any |= p.base && p.category == cats[k];
            if (any) m_nav.push_back({catFr(cats[k]), 2, cats[k], fam});
        }
    };
    addCats(basic, 6, 0);
    addCats(shapes, 7, 1);
    static const char* techBase[] = {"Technic"};
    addCats(techBase, 1, 2);
    m_nav.back().label = "Base";
    static const char* subs[] = {"Poutres", "Axes", "Pins", "Connecteurs", "Engrenages", "Briques", "Panneaux", "Direction",
                                 "Pneumatique", "Chenilles", "Moteurs", "Rotation", "Flex", "Bionicle", "Divers"};
    for (const char* s : subs) {
        bool any = false;
        for (const Part& p : m_lib.parts) any |= !p.base && p.sub == s;
        if (any) m_nav.push_back({s, 3, s, 2});
    }
    m_navSel = std::min<int>(2, (int)m_nav.size() - 1);
}

static std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::vector<int> BrickApp::visibleParts() const {
    std::vector<int> out;
    if (m_searchOpen && !m_search.empty()) {   // search: every category
        const std::string q = lower(m_search);
        for (int i = 0; i < (int)m_lib.parts.size(); ++i) {
            const Part& p = m_lib.parts[i];
            if (p.loaded && (lower(p.label).find(q) != std::string::npos || p.name.find(q) != std::string::npos)) out.push_back(i);
        }
        return out;
    }
    if (m_navSel < 0 || m_navSel >= (int)m_nav.size()) return out;
    const NavItem& n = m_nav[m_navSel];
    if (n.kind == 0) return m_recent;
    if (n.kind == 1) return m_fav;
    for (int i = 0; i < (int)m_lib.parts.size(); ++i) {
        const Part& p = m_lib.parts[i];
        if (!p.loaded) continue;
        if ((n.kind == 2 && p.base && p.category == n.key) || (n.kind == 3 && !p.base && p.sub == n.key)) out.push_back(i);
    }
    return out;
}

// Thumbnail card: the image alone (no text), framed; label in the tooltip; right click = favourite.
bool BrickApp::partCard(int part, float size) {
    ImGui::PushID(part);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton("card", ImVec2(size, size));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p1(p0.x + size, p0.y + size);
    const float rr = 4.0f * m_ui;
    ImTextureID tex;
    ImVec2 uv0, uv1;
    if (thumbUv(part, tex, uv0, uv1)) dl->AddImageRounded(tex, p0, p1, uv0, uv1, IM_COL32_WHITE, rr);
    else dl->AddRectFilled(p0, p1, th::field(), rr);
    dl->AddRect(p0, p1, hovered ? th::strong() : th::border(), rr, 0, hovered ? 2.0f * m_ui : 1.0f * m_ui);
    const bool fav = std::find(m_fav.begin(), m_fav.end(), part) != m_fav.end();
    if (fav) dl->AddText(ImVec2(p1.x - 16 * m_ui, p0.y + 2 * m_ui), IM_COL32(255, 196, 40, 255), ICON_STARF);
    if (hovered) {
        ImGui::SetTooltip("%s\n%s", m_lib.parts[part].label.c_str(), m_lib.parts[part].name.c_str());
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            if (fav) m_fav.erase(std::find(m_fav.begin(), m_fav.end(), part));
            else m_fav.push_back(part);
        }
    }
    ImGui::PopID();
    return clicked;
}

// --------------------------------------------------------------------------- panels

void BrickApp::drawToolbar(float x, float y, float w) {
    const float s = m_ui;
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4 * s, 4 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2 * s, 2 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5 * s, 3 * s));
    ImGui::Begin("##toolbar", nullptr, kPanel | ImGuiWindowFlags_AlwaysAutoResize);
    auto tool = [&](const char* icon, const char* tip, bool on = false) {
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        bool c = ImGui::Button(icon);
        if (on) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        ImGui::SameLine();
        return c;
    };
    auto sep = [&]() { ImGui::TextDisabled("|"); ImGui::SameLine(); };
    if (tool(ICON_UNDO_, "Annuler (Ctrl+Z)")) undo();
    if (tool(ICON_REDO_, "Rétablir (Ctrl+Y)")) redo();
    sep();
    if (tool(ICON_DUP, "Dupliquer au-dessus (Ctrl+D)")) duplicateSelected();
    if (tool(ICON_TURN, "Tourner de 90° (R, Maj+R)")) rotateSelected(1);
    if (tool(ICON_TRASH, "Supprimer (Suppr)")) deleteSelected();
    sep();
    if (tool(ICON_FOCUS_, "Mode focus : le reste assombri à 50 % (H)", m_focusDim)) { m_focusDim = !m_focusDim; syncObjects(); }
    if (tool(ICON_GRID_, "Grille des tenons au sol (G)", m_build.gridSnap)) m_build.gridSnap = !m_build.gridSnap;
    if (tool(ICON_RT, m_rs.raster ? "Path tracing (P)" : "Temps réel (P)", !m_rs.raster)) { m_rs.raster = !m_rs.raster; m_renderer.resetAccumulation(); }
    if (tool(u8"\uE90F", "Mode Mécanique (M)", m_mechOn)) { if (m_mechOn) mechExit(); else mechEnter(); }
    sep();
    if (tool(ICON_SAVE_, "Enregistrer .v3d.json (Ctrl+S)")) saveDialog();
    if (tool(ICON_OPEN_, "Ouvrir .v3d.json (Ctrl+O)")) openDialog();
    ImGui::NewLine();
    ImGui::End();
    ImGui::PopStyleVar(3);
    (void)w;
}

void BrickApp::drawStats(float x, float y) {
    const float s = m_ui;
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowBgAlpha(0.8f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6 * s, 4 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4 * s, 1 * s));
    ImGui::Begin("##stats", nullptr, kPanel | ImGuiWindowFlags_AlwaysAutoResize);
    if (m_gui.fontSmall) ImGui::PushFont(m_gui.fontSmall);
    ImGui::Text("%s · %d briques", m_rs.raster ? "Temps réel" : "Path tracing", (int)m_bricks.size());
    ImGui::Text("GPU %.2f ms · CPU %.2f ms · %.0f i/s", m_renderer.gpuFrameMs(), m_cpuMs, m_frameMs > 0 ? 1000.0 / m_frameMs : 0.0);
    if (m_rs.raster) ImGui::Text("Triangles dessinés %.2f M%s", m_renderer.rasterTriangles() / 1e6, m_scene.lods.empty() ? " (sans LOD)" : "");
    else ImGui::Text("Échantillons %u", m_renderer.sampleCount());
    if (m_holding) ImGui::TextColored(m_held.blocked ? ImVec4(1, 0.4f, 0.35f, 1) : ImVec4(1, 0.85f, 0.3f, 1), "%s",
                                      m_held.blocked ? "Bloquée : chevauchement > 50 %" : (m_held.suggested ? "Pivot voisin proposé" : "Brique en main"));
    if (m_gui.fontSmall) ImGui::PopFont();
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void BrickApp::drawColors(float x, float y, float w, float h) {
    const float s = m_ui;
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));   // h = 0: the height of the content
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6 * s, 6 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3 * s, 3 * s));
    ImGui::Begin("##colors", nullptr, kPanel | ImGuiWindowFlags_AlwaysAutoResize);
    if (m_gui.fontSmall) ImGui::PushFont(m_gui.fontSmall);
    ImGui::TextDisabled("COULEURS");
    const float sw = 18 * s;
    const int perRow = std::max(1, (int)((w - 12 * s + 3 * s) / (sw + 3 * s)));
    auto group = [&](const char* title, const char* key) {
        ImGui::TextDisabled("%s", title);
        int k = 0;
        for (int i = 0; i < (int)m_lib.palette.size(); ++i) {
            const Color& c = m_lib.palette[i];
            if (c.group != key) continue;
            if (k++ % perRow) ImGui::SameLine();
            ImGui::PushID(i);
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("sw", ImVec2(sw, sw))) applyColor(i);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImU32 col = IM_COL32((int)(c.srgb.x * 255), (int)(c.srgb.y * 255), (int)(c.srgb.z * 255), c.group == "transparent" ? 150 : 255);
            dl->AddRectFilled(p0, ImVec2(p0.x + sw, p0.y + sw), col, 3 * s);
            const bool cur = i == m_color;
            dl->AddRect(p0, ImVec2(p0.x + sw, p0.y + sw), cur ? IM_COL32_WHITE : th::border(), 3 * s, 0, cur ? 2.0f * s : 1.0f);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s (%d)", c.name.c_str(), c.id);
            ImGui::PopID();
        }
    };
    group("Standard", "standard");
    group("Transparent", "transparent");
    group("Métal", "metallic");
    if (m_gui.fontSmall) ImGui::PopFont();
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void BrickApp::drawLibrary(float x, float y, float w, float h) {
    const float s = m_ui;
    static const float kSizes[3] = {56, 72, 96};
    const float card = kSizes[std::clamp(m_thumbSize, 0, 2)] * s;
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::SetNextWindowBgAlpha(0.94f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6 * s, 5 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3 * s, 4 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6 * s, 2 * s));
    ImGui::Begin("##library", nullptr, kPanel);
    // row 1: recents (clock), favourites (star), search (magnifier opening a field), thumbnail size, categories
    auto iconToggle = [&](const char* icon, const char* tip, bool on) {
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        bool c = ImGui::Button(icon);
        if (on) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        ImGui::SameLine();
        return c;
    };
    if (iconToggle(ICON_CLOCK, "Récents", m_navSel == 0 && !m_searchOpen)) { m_navSel = 0; m_searchOpen = false; }
    if (iconToggle(ICON_STAR_, "Favoris (clic droit sur une pièce)", m_navSel == 1 && !m_searchOpen)) { m_navSel = 1; m_searchOpen = false; }
    if (iconToggle(ICON_FIND, "Rechercher", m_searchOpen)) m_searchOpen = !m_searchOpen;
    if (m_searchOpen) {
        char buf[128];
        snprintf(buf, sizeof(buf), "%s", m_search.c_str());
        ImGui::SetNextItemWidth(180 * s);
        if (ImGui::IsWindowAppearing() || ImGui::IsItemDeactivated()) {}
        if (ImGui::InputTextWithHint("##q", "nom ou référence…", buf, sizeof(buf))) m_search = buf;
        ImGui::SameLine();
    }
    if (iconToggle(ICON_SIZE, "Taille des miniatures", false)) m_thumbSize = (m_thumbSize + 1) % 3;
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    // row 1: families
    const int curFam = (m_navSel >= 0 && m_navSel < (int)m_nav.size()) ? m_nav[m_navSel].fam : -1;
    for (int f = 0; f < (int)m_families.size(); ++f) {
        const bool on = f == curFam && !m_searchOpen;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(m_families[f].c_str())) {
            for (int i = 0; i < (int)m_nav.size(); ++i)
                if (m_nav[i].fam == f) { m_navSel = i; break; }
            m_searchOpen = false;
        }
        if (on) ImGui::PopStyleColor();
        ImGui::SameLine();
    }
    ImGui::NewLine();
    // row 2: categories (sub-families) of the family, a strip that scrolls with the wheel
    ImGui::BeginChild("##cats", ImVec2(0, ImGui::GetFrameHeight()), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_HorizontalScrollbar);
    for (int i = 2; i < (int)m_nav.size(); ++i) {
        if (m_nav[i].fam != curFam) continue;
        const bool on = i == m_navSel && !m_searchOpen;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::SmallButton(m_nav[i].label.c_str())) { m_navSel = i; m_searchOpen = false; }
        if (on) ImGui::PopStyleColor();
        ImGui::SameLine();
    }
    if (curFam < 0) ImGui::TextDisabled(m_navSel == 0 ? "Pièces utilisées récemment" : "Clic droit sur une miniature pour l'ajouter aux favoris");
    if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0) ImGui::SetScrollX(ImGui::GetScrollX() - ImGui::GetIO().MouseWheel * 60 * s);
    ImGui::EndChild();

    // rows 2-3: the cards of the category on two rows, scrolling horizontally (column by column)
    std::vector<int> parts = visibleParts();
    ImGui::BeginChild("##cards", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const float gap = 4 * s;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));
    const int cols = (int)(parts.size() + 1) / 2;
    for (int c = 0; c < cols; ++c) {
        ImGui::BeginGroup();
        for (int r = 0; r < 2; ++r) {
            int k = c * 2 + r;
            if (k >= (int)parts.size()) break;
            if (partCard(parts[k], card)) {
                ImVec2 d = ImGui::GetIO().DisplaySize;
                spawn(parts[k], d.x * 0.5f, d.y * 0.45f);
            }
        }
        ImGui::EndGroup();
        ImGui::SameLine();
    }
    if (parts.empty()) ImGui::TextDisabled(m_navSel == 1 ? "Pas encore de favoris : clic droit sur une miniature." : "Rien ici pour l'instant.");
    ImGui::PopStyleVar();
    if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0) ImGui::SetScrollX(ImGui::GetScrollX() - ImGui::GetIO().MouseWheel * card);
    ImGui::EndChild();
    ImGui::End();
    ImGui::PopStyleVar(3);
}

void BrickApp::drawToast() {
    if (m_toast.empty() || ImGui::GetTime() - m_toastTime > 3.5) return;
    ImVec2 d = ImGui::GetIO().DisplaySize;
    ImVec2 ts = ImGui::CalcTextSize(m_toast.c_str());
    const float s = m_ui;
    ImVec2 p0(d.x * 0.5f - ts.x * 0.5f - 10 * s, d.y * 0.62f);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x + ts.x + 20 * s, p0.y + ts.y + 10 * s), IM_COL32(20, 22, 26, 220), 6 * s);
    dl->AddText(ImVec2(p0.x + 10 * s, p0.y + 5 * s), IM_COL32(235, 237, 240, 255), m_toast.c_str());
}

void BrickApp::drawUi() {
    const float s = m_ui;
    ImVec2 d = ImGui::GetIO().DisplaySize;
    static const float kSizes[3] = {56, 72, 96};
    const float card = kSizes[std::clamp(m_thumbSize, 0, 2)] * s;
    const float m = 8 * s;
    // library: fixed height, exactly two nav rows and two rows of cards (+ the horizontal scrollbar), whatever the category holds
    const float spacing = 4 * s;   // ItemSpacing.y of the panel
    const float libH = 5 * s * 2 + ImGui::GetFrameHeight() * 2 + spacing * 2 + 2 * card + 4 * s + ImGui::GetStyle().ScrollbarSize + 2 * s;
    const float colW = 8 * (18 * s + 3 * s) + 12 * s;
    drawLibrary(m, d.y - libH - m, d.x - colW - 3 * m, libH);
    drawColors(d.x - colW - m, 48 * s, colW, 0);
    drawToolbar(d.x * 0.5f - 200 * s, m, 400 * s);
    drawStats(m, m);
    if (m_mechOn) drawMech(m, 64 * s);
    drawToast();
    m_overUi = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) || ImGui::IsAnyItemActive();
}

} // namespace brickit
