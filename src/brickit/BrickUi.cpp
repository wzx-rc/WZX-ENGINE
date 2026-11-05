// BrickIt - the interface of the web builder v26 (docs/brickit/web/BrickIt_Builder.html), element by element: the CSS
// colours (--bg, --panel, --card, --line, --accent ...), the compact sizes (top bar 24 px buttons, tray, 84 px cards),
// the SVG icons (Svg.cpp), the French texts. Everything is drawn with ImDrawList in one window behind the ImGui popups;
// clicks are hit-tested here. m_ui scales the whole interface (4K: 2).
#include "BrickApp.h"
#include <commdlg.h>
#include "Svg.h"
#include "app/Theme.h"
#include <algorithm>
#include <functional>
#include <map>
#include <cctype>
#include <cmath>
#include <cstring>

namespace brickit {

// ------------------------------------------------------------------ web CSS colours
static const ImU32 C_BG = IM_COL32(0x13, 0x14, 0x17, 255);
static const ImU32 C_PANEL = IM_COL32(24, 25, 29, 235);           // rgba(24, 25, 29, 0.92)
static const ImU32 C_PANEL_SOLID = IM_COL32(0x1b, 0x1c, 0x20, 255);
static const ImU32 C_CARD = IM_COL32(0x25, 0x26, 0x2b, 255);
static const ImU32 C_CARD_HI = IM_COL32(0x30, 0x32, 0x3a, 255);
static const ImU32 C_LINE = IM_COL32(0x2f, 0x31, 0x37, 255);
static const ImU32 C_TEXT = IM_COL32(0xec, 0xec, 0xee, 255);
static const ImU32 C_DIM = IM_COL32(0x9a, 0x9c, 0xa4, 255);
static const ImU32 C_FAINT = IM_COL32(0x67, 0x6a, 0x72, 255);
static const ImU32 C_ACCENT = IM_COL32(0xf7, 0xc5, 0x1e, 255);
static const ImU32 C_INK = IM_COL32(0x17, 0x17, 0x1a, 255);
static const ImU32 C_LYEL = IM_COL32(0xf2, 0xcd, 0x37, 255);
static const ImU32 C_LBLU = IM_COL32(0x00, 0x55, 0xbf, 255);
static const ImU32 C_OVERLAY = IM_COL32(15, 16, 19, 219);        // rgba(15, 16, 19, 0.86)
static const ImU32 C_HUD = IM_COL32(15, 16, 19, 184);            // rgba(15, 16, 19, 0.72)
static const ImU32 C_DEV = IM_COL32(0x7f, 0xe3, 0xff, 255);

static ImU32 alphaOf(ImU32 c, float a) { return (c & 0x00FFFFFF) | ((ImU32)(((c >> 24) & 0xFF) * a) << 24); }

// LDraw families of the web (line 1 of the library) and their categories (line 2, empty ones dashed)
struct Fam { const char* name; std::vector<const char*> cats; };
static const std::vector<Fam>& families() {
    static const std::vector<Fam> f = {
        {"Basique", {"Brick", "Plate", "Tile", "Baseplate", "Bracket", "Panel"}},
        {"Formes", {"Slope", "Wedge", "Arch", "Cylinder", "Cone", "Dish", "Sphere"}},
        {"Technic", {"Technic", "Electric", "Znap", "Constraction", "Constraction Accessory"}},
        {"Mécanique", {"Hinge", "Turntable", "Bar", "Lever", "Winch", "Crane", "Grab", "Magnet", "Conveyor", "Screw", "Rack", "Hose", "Tap"}},
        {"Véhicules", {"Wheel", "Tyre", "Car", "Vehicle", "Train", "Monorail", "Boat", "Plane", "Wing", "Tail", "Propeller", "Cockpit", "Windscreen", "Exhaust", "Tractor", "Trailer", "Tipper", "Forklift"}},
        {"Bâtiment", {"Door", "Window", "Glass", "Fence", "Ladder", "Staircase", "Support", "Platform", "Garage", "Container", "Roadsign", "Flag", "Antenna"}},
        {"Personnages", {"Minifig", "Minifig Accessory", "Minifig Footwear", "Minifig Headwear", "Minifig Hipwear", "Minifig Neckwear", "Figure", "Figure Accessory", "Arm"}},
        {"Nature", {"Animal", "Plant", "Rock"}},
        {"Autres gammes", {"Duplo", "Scala", "Belville", "Clikits", "Homemaker", "Freestyle", "Sticker", "Sheet Cardboard", "Sheet Fabric", "Sheet Plastic", "Obsolete", "Pov-RAY"}},
    };
    return f;
}
static const char* catFr(const std::string& c) {
    static const std::pair<const char*, const char*> t[] = {
        {"Brick", "Briques"}, {"Plate", "Plaques"}, {"Tile", "Tuiles"}, {"Baseplate", "Plaques de base"}, {"Bracket", "Équerres"},
        {"Panel", "Panneaux"}, {"Slope", "Pentes"}, {"Wedge", "Biseaux"}, {"Arch", "Arches"}, {"Cylinder", "Cylindres"}, {"Cone", "Cônes"},
        {"Dish", "Paraboles"}, {"Sphere", "Sphères"}, {"Electric", "Électrique"}, {"Hinge", "Charnières"}, {"Turntable", "Plateaux tournants"},
        {"Bar", "Barres"}, {"Lever", "Leviers"}, {"Winch", "Treuils"}, {"Crane", "Grues"}, {"Grab", "Pinces"}, {"Magnet", "Aimants"},
        {"Conveyor", "Convoyeurs"}, {"Screw", "Vis"}, {"Rack", "Crémaillères"}, {"Hose", "Tuyaux"}, {"Tap", "Robinets"}, {"Wheel", "Roues"},
        {"Tyre", "Pneus"}, {"Car", "Voiture"}, {"Vehicle", "Véhicule"}, {"Train", "Train"}, {"Boat", "Bateau"}, {"Plane", "Avion"},
        {"Wing", "Ailes"}, {"Tail", "Dérives"}, {"Propeller", "Hélices"}, {"Cockpit", "Cockpits"}, {"Windscreen", "Pare-brise"},
        {"Door", "Portes"}, {"Window", "Fenêtres"}, {"Glass", "Vitres"}, {"Fence", "Clôtures"}, {"Ladder", "Échelles"},
        {"Staircase", "Escaliers"}, {"Support", "Supports"}, {"Platform", "Plateformes"}, {"Container", "Conteneurs"},
        {"Roadsign", "Panneaux routiers"}, {"Flag", "Drapeaux"}, {"Antenna", "Antennes"}, {"Minifig", "Minifigs"}, {"Animal", "Animaux"},
        {"Plant", "Plantes"}, {"Rock", "Rochers"}};
    for (auto& e : t)
        if (c == e.first) return e.second;
    return c.c_str();
}
static const char* kTechSubs[] = {"Poutres", "Axes", "Pins", "Connecteurs", "Engrenages", "Briques", "Panneaux", "Direction", "Pneumatique",
                                  "Chenilles", "Moteurs", "Rotation", "Flex", "Bionicle", "Divers"};

const std::string& BrickApp::partCat(int part) const {
    static const std::string other = "Autre";
    const Part& p = m_lib.parts[part];
    return p.category.empty() ? other : p.category;
}

void BrickApp::buildNav() {
    // Technic parts of the base set get their sub-family from the second word of their title, as the web (techSub)
    static const std::pair<const char*, std::vector<const char*>> subs[] = {
        {"Poutres", {"Beam", "Liftarm"}}, {"Axes", {"Axle"}}, {"Pins", {"Pin"}}, {"Connecteurs", {"Connector", "Cross", "Angle", "Bush", "Link"}},
        {"Engrenages", {"Gear", "Worm", "Differential", "Gearbox", "Sprocket", "Clutch", "Changeover", "Transmission", "Cam"}},
        {"Briques", {"Brick", "Plate", "Tile"}}, {"Panneaux", {"Panel", "Fairing", "Dome", "Wedge", "Bumper", "Grille", "Windscreen", "Wing"}},
        {"Direction", {"Steering", "Suspension", "Shock", "Universal", "Ball"}}, {"Pneumatique", {"Pneumatic"}},
        {"Chenilles", {"Chain", "Tread", "Belt", "Rubber", "Track", "Wheel", "Pulley"}}, {"Moteurs", {"Engine", "Linear", "Piston", "Cylinder"}},
        {"Rotation", {"Turntable", "Rotor", "Rotation"}}, {"Flex", {"Flex-System", "Cable"}}, {"Bionicle", {"Bionicle", "Disc", "Sword"}}};
    for (Part& p : m_lib.parts) {
        if (p.category != "Technic" || !p.sub.empty()) continue;
        std::string w;
        size_t a = p.label.find(' ');
        if (a != std::string::npos) { size_t b = p.label.find(' ', a + 1); w = p.label.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1); }
        p.sub = "Divers";
        for (auto& sbs : subs)
            for (const char* k : sbs.second)
                if (w == k) p.sub = sbs.first;
    }
    // --cat N (scripted tests): 0 recents, 1 favourites, then the families
    m_nav.clear();
    m_nav.push_back({"Récentes", 0, "recent"});
    m_nav.push_back({"Favoris", 1, "fav"});
    for (const Fam& f : families()) m_nav.push_back({f.name, 2, f.name});
    for (const char* s : kTechSubs) m_nav.push_back({s, 3, s, 2});
}

std::vector<int> BrickApp::visibleParts() const {
    std::vector<int> out;
    auto lower = [](std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; };
    if (m_searchOpen && !m_search.empty()) {
        const std::string q = lower(m_search);
        for (int i = 0; i < (int)m_lib.parts.size(); ++i) {
            const Part& p = m_lib.parts[i];
            if (!p.available()) continue;
            const std::string& c = partCat(i);
            if (lower(p.name).find(q) != std::string::npos || lower(p.label).find(q) != std::string::npos || lower(catFr(c)).find(q) != std::string::npos ||
                lower(c).find(q) != std::string::npos)
                out.push_back(i);
        }
        return out;
    }
    if (m_fam == "fav") return m_fav;
    if (m_fam == "recent") return m_recent;
    const Fam* fam = nullptr;
    for (const Fam& f : families()) if (m_fam == f.name) fam = &f;
    for (int i = 0; i < (int)m_lib.parts.size(); ++i) {
        const Part& p = m_lib.parts[i];
        if (!p.available()) continue;
        if (m_fam == "all") { out.push_back(i); continue; }
        if (!fam) continue;
        const std::string& c = partCat(i);
        bool in = m_cat.empty() ? std::find_if(fam->cats.begin(), fam->cats.end(), [&](const char* x) { return c == x; }) != fam->cats.end() : c == m_cat;
        if (in && (m_sub.empty() || p.sub == m_sub)) out.push_back(i);
    }
    return out;
}

// ------------------------------------------------------------------ drawing helpers

struct Ui {
    ImDrawList* dl;
    float s;
    ImVec2 mouse;
    bool pressed, released;
    ImFont* font;
    ImFont* bold;
    static constexpr float kFont = 1.22f;   // Segoe UI in ImGui reads smaller than in the browser at the same px size
    float fs(float px) const { return px * s * kFont; }
    bool hover(ImVec2 a, ImVec2 b) const { return mouse.x >= a.x && mouse.y >= a.y && mouse.x < b.x && mouse.y < b.y; }
    ImVec2 text(ImVec2 p, float px, ImU32 col, const char* t, bool b = false) {
        ImFont* f = b ? bold : font;
        dl->AddText(f, fs(px), p, col, t);
        return f->CalcTextSizeA(fs(px), 1e9f, 0, t);
    }
    float textW(float px, const char* t, bool b = false) const { return (b ? bold : font)->CalcTextSizeA(fs(px), 1e9f, 0, t).x; }
    void grip(ImVec2 p, float w, const char* label, bool collapsed, bool chev = true) {   // dots + LABEL + chevron
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 6; ++c) dl->AddCircleFilled(ImVec2(p.x + (3 + c * 6) * s, p.y + (3 + r * 4) * s), 1.1f * s, C_FAINT, 6);
        if (label) text(ImVec2(p.x + 42 * s, p.y - 1 * s), 10, C_FAINT, label);
        if (chev) {
            ImVec2 c(p.x + w - 6 * s, p.y + 5 * s);
            float k = 3.5f * s;
            if (!collapsed) dl->AddPolyline(std::vector<ImVec2>{ImVec2(c.x - k, c.y - k * 0.5f), c, ImVec2(c.x + k, c.y - k * 0.5f)}.data(), 3, C_FAINT, 0, 2 * s);
            else dl->AddPolyline(std::vector<ImVec2>{ImVec2(c.x - k, c.y + k * 0.5f), ImVec2(c.x, c.y - k * 0.5f), ImVec2(c.x + k, c.y + k * 0.5f)}.data(), 3, C_FAINT, 0, 2 * s);
        }
    }
};

static Ui ui(float s) {
    Ui u;
    u.dl = ImGui::GetWindowDrawList();
    u.s = s;
    u.mouse = ImGui::GetIO().MousePos;
    u.pressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    u.released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    u.font = ImGui::GetFont();
    u.bold = ImGui::GetFont();
    return u;
}

// ------------------------------------------------------------------ top bar

void BrickApp::uiHeader() {
    Ui u = ui(m_ui);
    if (m_gui.fontBold) u.bold = m_gui.fontBold;
    const float s = m_ui;
    ImVec2 d = ImGui::GetIO().DisplaySize;
    const float H = 30 * s;
    u.dl->AddRectFilled(ImVec2(0, 0), ImVec2(d.x, H), C_PANEL);
    u.dl->AddLine(ImVec2(0, H - 0.5f), ImVec2(d.x, H - 0.5f), C_LINE, 1.0f);
    m_uiRects.push_back(ImVec4(0, 0, d.x, H));
    // logo: yellow 20 px square "3D" + BrickIt Builder
    ImVec2 l(10 * s, 5 * s);
    u.dl->AddRectFilled(l, ImVec2(l.x + 20 * s, l.y + 20 * s), C_ACCENT, 5 * s);
    float tw = u.textW(9, "3D", true);
    u.text(ImVec2(l.x + 10 * s - tw * 0.5f, l.y + 4.5f * s), 9, C_INK, "3D", true);
    u.text(ImVec2(l.x + 26 * s, l.y + 3 * s), 12, C_TEXT, "BrickIt Builder", true);

    // buttons, right aligned, in the order of the web
    struct B { int kind; svg::Icon icon; const char* label; const char* tip; bool on, enabled; int id; };   // kind 0 icon, 1 select, 2 icon+text, 3 text, 4 sep
    const bool sel = m_selected >= 0 || !m_multi.empty();
    const char* tmNames[4] = {"ACES", "AgX", "AgX", "Std"};
    std::vector<B> bs = {
        {0, svg::I_UNDO, nullptr, "Annuler (Ctrl+Z)", false, !m_undo.empty() || m_holding, 1},
        {0, svg::I_REDO, nullptr, "Rétablir", false, !m_redo.empty(), 2},
        {4, svg::I_UNDO, nullptr, nullptr, false, true, 0},
        {0, svg::I_SELECT, nullptr, "Mode Sélection : touche une brique, ou trace un rectangle pour en sélectionner plusieurs", m_mode == 0, true, 3},
        {0, svg::I_VISOR, nullptr, "Mode Viseur : toucher une brique la prend en main", m_mode == 1, true, 4},
        {0, svg::I_MOVE, nullptr, "Mode Déplacer : flèches sur la brique sélectionnée", m_mode == 2, true, 5},
        {0, svg::I_ROTATE, nullptr, "Mode Tourner : anneaux sur la brique sélectionnée", m_mode == 3, true, 6},
        {4, svg::I_UNDO, nullptr, nullptr, false, true, 0},
        {0, svg::I_GRID, nullptr, "Grille ON : snap sur la grille du sol · OFF : pas de sol, snap sur le voxel le plus proche", m_build.gridSnap, true, 7},
        {0, svg::I_PIVOT, nullptr, "Auto Pivot : le point touché devient le pivot", m_autoPivot, true, 8},
        {0, svg::I_SAFETY, nullptr, "Sécurité : refuse de poser si plus de 50 % de la brique est dans une autre", m_build.safety, true, 9},
        {0, svg::I_FOCUS, nullptr, "Mode focus : la brique sélectionnée reste claire, les autres passent à 50 % (F)", m_focusDim, true, 10},
        {0, svg::I_ANIM, nullptr, "Animations des briques (A)", m_animOn, true, 11},
        {4, svg::I_UNDO, nullptr, nullptr, false, true, 0},
        {0, svg::I_VOXELS, nullptr, "Voir les voxels : tenons jaunes, pins cyan, axles magenta (V)", m_showVox, true, 12},
        {0, svg::I_DELETE, nullptr, "Supprimer la sélection", false, sel && !m_holding, 13},
        {1, svg::I_UNDO, m_rcMode ? "Radiance C." : !m_rs.raster ? "Path tracing" : m_quality ? "HQ" : "Rapide", "Qualité", false, true, 14},
        {1, svg::I_UNDO, tmNames[std::clamp(m_rs.tonemapper, 0, 3)], "Transformation de vue", false, true, 15},
        {4, svg::I_UNDO, nullptr, nullptr, false, true, 0},
        {2, svg::I_MECH, "Mécanique", "Mode Mécanique : animer pivots, engrenages et axes (cinématique)", m_mechOn, true, 16},
        {3, svg::I_UNDO, "DEV", "Mode développeur : connecteurs LDraw, export / réimport Maya des pièces", m_dev, true, 17},
        {0, svg::I_WINDOWS, nullptr, "Fenêtres : afficher / masquer les panneaux", m_popup == 3, true, 18},
        {0, svg::I_FILE, nullptr, "Scène : ouvrir / enregistrer (.v3d.json)", m_popup == 4, true, 19},
        {0, svg::I_SETTINGS, nullptr, "Réglages : échelle de l'interface, miniatures, qualité, transformation de vue…", m_popup == 5, true, 20},
    };
    auto width = [&](const B& b) {
        switch (b.kind) {
        case 0: return 26 * s;
        case 1: return u.textW(11, b.label) + 24 * s;
        case 2: return u.textW(11, b.label) + 32 * s;
        case 3: return u.textW(12, b.label, true) + 12 * s;
        default: return 5 * s;
        }
    };
    float total = 0;
    for (const B& b : bs) total += width(b) + 4 * s;
    float x = d.x - 10 * s - total + 4 * s;
    const float y0 = 3 * s, y1 = 27 * s;
    for (const B& b : bs) {
        const float w = width(b);
        ImVec2 a(x, y0), c(x + w, y1);
        x += w + 4 * s;
        if (b.kind == 4) { u.dl->AddLine(ImVec2(a.x + 2.5f * s, 8 * s), ImVec2(a.x + 2.5f * s, 22 * s), C_LINE, 1.0f); continue; }
        const bool h = b.enabled && u.hover(a, c) && m_popup == 0;
        ImU32 bg = b.on ? (b.id == 17 ? C_DEV : C_ACCENT) : (h ? C_CARD_HI : C_CARD);
        ImU32 fg = b.on ? (b.id == 17 ? IM_COL32(0x0d, 0x1a, 0x1f, 255) : C_INK) : C_TEXT;
        if (!b.enabled) { bg = alphaOf(bg, 0.4f); fg = alphaOf(fg, 0.4f); }
        u.dl->AddRectFilled(a, c, bg, 6 * s);
        const float cy = (y0 + y1) * 0.5f;
        if (b.kind == 0) svg::icon(u.dl, b.icon, ImVec2((a.x + c.x) * 0.5f, cy), 14 * s, fg);
        else if (b.kind == 1) {
            u.text(ImVec2(a.x + 6 * s, cy - 7 * s), 11, fg, b.label);
            ImVec2 k(c.x - 10 * s, cy);
            u.dl->AddTriangleFilled(ImVec2(k.x - 3 * s, k.y - 1.5f * s), ImVec2(k.x + 3 * s, k.y - 1.5f * s), ImVec2(k.x, k.y + 2 * s), C_DIM);
        } else if (b.kind == 2) {
            svg::icon(u.dl, b.icon, ImVec2(a.x + 13 * s, cy), 14 * s, fg);
            u.text(ImVec2(a.x + 24 * s, cy - 7 * s), 11, fg, b.label, b.on);
        } else u.text(ImVec2(a.x + 6 * s, cy - 8 * s), 12, fg, b.label, true);
        if (h && b.tip && ImGui::GetIO().MouseDownDuration[0] < 0) ImGui::SetTooltip("%s", b.tip);
        if (!(h && u.pressed)) continue;
        switch (b.id) {
        case 1: undo(); break;
        case 2: redo(); break;
        case 3: case 4: case 5: case 6:
            m_mode = b.id - 3;
            if (m_mode >= 2) toast(m_mode == 2 ? "Mode Déplacer : les flèches arrivent bientôt dans la version native" : "Mode Tourner : les anneaux arrivent bientôt dans la version native");
            break;
        case 7: m_build.gridSnap = !m_build.gridSnap; toast(m_build.gridSnap ? "Grille ON : snap sur la grille du sol" : "Grille OFF : snap sur le voxel le plus proche"); break;
        case 8: m_autoPivot = !m_autoPivot; toast(m_autoPivot ? "Auto Pivot : le point touché devient le pivot" : "Auto Pivot désactivé"); break;
        case 9: m_build.safety = !m_build.safety; toast(m_build.safety ? "Sécurité ON : refus au-delà de 50 % de recouvrement" : "Sécurité OFF"); break;
        case 10: m_focusDim = !m_focusDim; syncObjects(); break;
        case 11: m_animOn = !m_animOn; break;
        case 12: m_showVox = !m_showVox; syncObjects(); toast(m_showVox ? "Voxels : affichés (tenons jaunes, pins cyan, axles magenta)" : "Voxels : masqués"); break;
        case 13: deleteSelected(); break;
        case 14: m_popup = m_popup == 1 ? 0 : 1; m_popupAt = ImVec2(a.x, y1 + 4 * s); break;
        case 15: m_popup = m_popup == 2 ? 0 : 2; m_popupAt = ImVec2(a.x, y1 + 4 * s); break;
        case 16: if (m_mechOn) mechExit(); else mechEnter(); break;
        case 17: devSetOn(!m_dev); break;
        case 18: m_popup = m_popup == 3 ? 0 : 3; m_popupAt = ImVec2(d.x - 220 * s, y1 + 4 * s); break;
        case 19: m_popup = 0; m_sheetOpen = true; m_sheetText = m_build.toJson(m_scene.camera); break;
        case 20: m_popup = m_popup == 5 ? 0 : 5; m_popupAt = ImVec2(d.x - 330 * s, y1 + 4 * s); break;
        }
    }
}

// ------------------------------------------------------------------ left-hand radial menu

void BrickApp::uiRadial() {
    if (!m_touchHud || !m_pOpts.visible || m_mechOn) return;
    Ui u = ui(m_ui);
    const float s = m_ui;
    ImVec2 d = ImGui::GetIO().DisplaySize;
    const float trayTop = m_uiRects.size() > 1 ? m_uiRects[1].y : d.y;   // the tray is drawn before (index 1)
    const float size = 200 * s, k = size / 220.0f;   // the web svg is 220 x 220 (viewBox -110..110)
    const float titleH = 30 * s;
    ImVec2 p0(m_optsRight ? d.x - 8 * s - size : 10 * s, trayTop - 14 * s - size - titleH - (m_holding ? 40 * s : 0));
    if (m_pOpts.floating) p0 = ImVec2(std::max(0.0f, std::min(m_pOpts.x, d.x - 80 * s)), std::max(0.0f, std::min(m_pOpts.y, d.y - 34 * s)));
    ImVec2 c(p0.x + size * 0.5f, p0.y + titleH + size * 0.5f);
    m_uiRects.push_back(ImVec4(p0.x, p0.y, p0.x + size, p0.y + titleH + size + (m_holding ? 40 * s : 0)));
    // grip (56 px) + title
    u.grip(ImVec2(c.x - 18 * s, p0.y + 2 * s), 36 * s, nullptr, m_pOpts.collapsed, false);
    if (gripPanel(1, m_pOpts, ImVec2(c.x - 28 * s, p0.y), ImVec2(c.x + 28 * s, p0.y + 14 * s), p0)) return;
    std::string title = "AUCUNE BRIQUE";
    int focus = m_holding ? m_held.brick : m_selected;
    if (focus >= 0) title = m_lib.parts[m_bricks[focus].part].label;
    std::string up = title;
    for (char& ch : up) ch = (char)std::toupper((unsigned char)ch);
    if (up.size() > 28) up = up.substr(0, 27) + "…";
    float tw = u.textW(10.5f, up.c_str());
    u.text(ImVec2(c.x - tw * 0.5f, p0.y + 14 * s), 10.5f, C_DIM, up.c_str());
    if (m_pOpts.collapsed) return;
    const bool active = focus >= 0;
    const float alpha = active ? 1.0f : 0.45f;   // .opts.radial.idle #radial { opacity: 0.45 }
    const float RIN = 58 * k, ROUT = 100 * k, RDUP = 50 * k;
    struct Wedge { const char* act; float a; const char* icon; const char* tip; };
    static const Wedge W[4] = {{"rotU", -90, "M-9 4 L0 -5 L9 4", "Basculer vers le haut"},
                               {"rotR", 0, "M-6 -7 A9 9 0 1 1 -8 3 M-6 -7 L-6 -1 M-6 -7 L0 -7", "Tourner à droite 90°"},
                               {"rotD", 90, "M-9 -4 L0 5 L9 -4", "Basculer vers le bas"},
                               {"rotL", 180, "M6 -7 A9 9 0 1 0 8 3 M6 -7 L6 -1 M6 -7 L0 -7", "Tourner à gauche 90°"}};
    const float dx = u.mouse.x - c.x, dy = u.mouse.y - c.y, dist = std::sqrt(dx * dx + dy * dy);
    int hot = -1;   // 0..3 wedges, 4 duplicate
    if (dist <= RDUP + 4 * k) hot = 4;
    else if (dist >= RIN - 3 * k && dist <= ROUT + 6 * k) {
        float a = std::atan2(dy, dx) * 180.0f / 3.14159265f, bd = 360;
        for (int i = 0; i < 4; ++i) { float diff = std::fabs(std::fmod(a - W[i].a + 540.0f, 360.0f) - 180.0f); if (diff < bd) { bd = diff; hot = i; } }
    }
    const float half = 41.0f * 3.14159265f / 180.0f;
    for (int i = 0; i < 4; ++i) {
        float a = W[i].a * 3.14159265f / 180.0f;
        u.dl->PathArcTo(c, ROUT, a - half, a + half, 16);
        u.dl->PathArcTo(c, RIN, a + half, a - half, 12);
        const bool on = hot == i && active;
        u.dl->PathFillConvex(alphaOf(on ? C_CARD_HI : IM_COL32(27, 28, 32, 240), alpha));
        u.dl->PathArcTo(c, ROUT, a - half, a + half, 16);
        u.dl->PathArcTo(c, RIN, a + half, a - half, 12);
        u.dl->PathStroke(alphaOf(on ? C_LYEL : C_LINE, alpha), ImDrawFlags_Closed, 1.5f * k);
        ImVec2 ic(c.x + std::cos(a) * (RIN + ROUT) * 0.5f, c.y + std::sin(a) * (RIN + ROUT) * 0.5f);
        svg::path(u.dl, W[i].icon, ic, k, alphaOf(C_TEXT, alpha), 2.4f * k);
        if (hot == i) ImGui::SetTooltip("%s", W[i].tip);
    }
    const bool onDup = hot == 4 && active;
    u.dl->AddCircleFilled(c, RDUP, alphaOf(onDup ? C_LYEL : IM_COL32(27, 28, 32, 245), alpha), 48);
    u.dl->AddCircle(c, RDUP, alphaOf(C_LYEL, alpha), 48, 2 * k);
    svg::path(u.dl, "M-14 -10 h16 a3 3 0 0 1 3 3 v16 a3 3 0 0 1 -3 3 h-16 a3 3 0 0 1 -3 -3 v-16 a3 3 0 0 1 3 -3 z M5 -16 h11 a3 3 0 0 1 3 3 v13",
              c, k, alphaOf(onDup ? IM_COL32(0x1b, 0x15, 0x00, 255) : C_TEXT, alpha), 2.4f * k);
    float lw = u.textW(8 * k / s, "Dupliquer");
    u.text(ImVec2(c.x - lw * 0.5f, c.y + 22 * k), 8 * k / s, alphaOf(C_DIM, alpha), "Dupliquer");
    if (hot == 4) ImGui::SetTooltip("Dupliquer la brique");
    if (u.pressed && active && hot >= 0) {
        if (hot == 4) duplicateSelected();
        else if (hot == 0) tiltSelected(-1);
        else if (hot == 2) tiltSelected(1);
        else rotateSelected(hot == 1 ? -1 : 1);
    }
    // pills while a brick is in hand: Poser / Annuler
    if (m_holding) {
        float py = p0.y + titleH + size + 4 * s;
        const char* labels[2] = {"Poser", "Annuler"};
        float x = c.x - 70 * s;
        for (int i = 0; i < 2; ++i) {
            ImVec2 a(x, py), b(x + 66 * s, py + 34 * s);
            bool h = u.hover(a, b);
            u.dl->AddRectFilled(a, b, i == 0 ? C_LBLU : (h ? C_CARD_HI : IM_COL32(27, 28, 32, 240)), 17 * s);
            float w = u.textW(12.5f, labels[i], i == 0);
            u.text(ImVec2((a.x + b.x) * 0.5f - w * 0.5f, py + 9 * s), 12.5f, C_TEXT, labels[i], i == 0);
            if (h && u.pressed) { if (i == 0) dropHeld(); else cancelHeld(); }
            x += 74 * s;
        }
    }
}

// ------------------------------------------------------------------ touch HUD around the selection (Tiny Glade)
// Anchored to the projected bounds of the selection, at a fixed screen size: a thin dashed white frame, white line
// icons with a soft shadow and no panel: turn 15° left / right (sides), duplicate (below), the colour swatch (above).
// The swatch opens an arc of round swatches around the selection, the real colours of the part (Rebrickable) unless
// « Toutes les couleurs », 12 per page; a click applies and closes, or press on the swatch, slide, release (marking menu).

void BrickApp::uiSelHud() {
    const std::vector<int> sel = selection();
    if (!m_touchHud || sel.empty() || m_holding || m_mechOn || m_reading || (m_pressed && m_dragStarted)) { m_arcOpen = m_arcDrag = false; return; }
    if (m_opt.colorArc) m_arcOpen = true;   // (captures)
    Ui u = ui(m_ui);
    const float s = m_ui;
    const ImVec2 d = ImGui::GetIO().DisplaySize;
    // screen bounds of the selection
    float3 mn(1e30f), mx(-1e30f);
    for (int i : sel) {
        const Box b = m_build.aabbOf(m_bricks[i].part, m_bricks[i].rot, m_bricks[i].pos);
        mn = minv(mn, b.mn);
        mx = maxv(mx, b.mx);
    }
    const View v = view();
    ImVec2 a(1e30f, 1e30f), b(-1e30f, -1e30f);
    for (int k = 0; k < 8; ++k) {
        float x, y;
        if (!v.project(float3((k & 1) ? mx.x : mn.x, (k & 2) ? mx.y : mn.y, (k & 4) ? mx.z : mn.z), x, y)) { m_arcOpen = false; return; }
        a = ImVec2(std::min(a.x, x), std::min(a.y, y));
        b = ImVec2(std::max(b.x, x), std::max(b.y, y));
    }
    const float top = 30 * s;   // under the header
    if (b.x < 0 || a.x > d.x || b.y < top || a.y > d.y) { m_arcOpen = false; return; }
    a = ImVec2(a.x - 8 * s, a.y - 8 * s);
    b = ImVec2(b.x + 8 * s, b.y + 8 * s);
    const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    const ImU32 white = IM_COL32(255, 255, 255, 240), shade = IM_COL32(0, 0, 0, 80);
    const ImVec2 sh(1.0f * s, 1.5f * s);   // offset of the soft shadow
    // dashed frame (on screen)
    {
        const ImVec2 fa(std::max(a.x, 2.0f), std::max(a.y, top + 2)), fb(std::min(b.x, d.x - 2), std::min(b.y, d.y - 2));
        auto dashes = [&](ImVec2 p, ImVec2 q, ImU32 col, float w) {
            const float len = std::sqrt((q.x - p.x) * (q.x - p.x) + (q.y - p.y) * (q.y - p.y)), dash = 7 * s, gap = 5 * s;
            for (float t = 0; t < len; t += dash + gap) {
                const float t1 = std::min(len, t + dash);
                u.dl->AddLine(ImVec2(p.x + (q.x - p.x) * t / len, p.y + (q.y - p.y) * t / len), ImVec2(p.x + (q.x - p.x) * t1 / len, p.y + (q.y - p.y) * t1 / len), col, w);
            }
        };
        for (int pass = 0; pass < 2; ++pass) {
            const ImVec2 o = pass ? ImVec2(0, 0) : sh;
            const ImU32 col = pass ? IM_COL32(255, 255, 255, 210) : IM_COL32(0, 0, 0, 60);
            const float w = (pass ? 1.4f : 2.2f) * s;
            dashes(ImVec2(fa.x + o.x, fa.y + o.y), ImVec2(fb.x + o.x, fa.y + o.y), col, w);
            dashes(ImVec2(fb.x + o.x, fa.y + o.y), ImVec2(fb.x + o.x, fb.y + o.y), col, w);
            dashes(ImVec2(fb.x + o.x, fb.y + o.y), ImVec2(fa.x + o.x, fb.y + o.y), col, w);
            dashes(ImVec2(fa.x + o.x, fb.y + o.y), ImVec2(fa.x + o.x, fa.y + o.y), col, w);
        }
    }
    const float R = 20 * s, M = R + 6 * s;   // button radius, margin to the screen edges
    auto place = [&](ImVec2 p) { return ImVec2(std::clamp(p.x, M, d.x - M), std::clamp(p.y, top + M, d.y - M)); };
    auto hot = [&](ImVec2 p, float r) { return (u.mouse.x - p.x) * (u.mouse.x - p.x) + (u.mouse.y - p.y) * (u.mouse.y - p.y) <= r * r; };
    auto lineIcon = [&](const char* path, ImVec2 p, bool h) {
        const float k = (h ? 1.45f : 1.25f) * s;
        svg::path(u.dl, path, ImVec2(p.x + sh.x, p.y + sh.y), k, shade, 3.6f * k);
        svg::path(u.dl, path, p, k, white, 2.0f * k);
        m_uiRects.push_back(ImVec4(p.x - R, p.y - R, p.x + R, p.y + R));
    };
    const ImVec2 pSw = place(ImVec2(c.x, a.y - 30 * s));
    const int part = m_bricks[sel[0]].part;
    const int curColor = m_bricks[sel[0]].color;
    // the colour swatch: a disc of the colour of the selection, thin white ring
    {
        const bool h = hot(pSw, R);
        const float r = (h ? 13.0f : 11.5f) * s;
        const Color& cc = m_lib.palette[std::clamp(curColor, 0, (int)m_lib.palette.size() - 1)];
        u.dl->AddCircleFilled(ImVec2(pSw.x + sh.x, pSw.y + sh.y), r + 2.5f * s, shade, 32);
        u.dl->AddCircleFilled(pSw, r, IM_COL32((int)(cc.srgb.x * 255), (int)(cc.srgb.y * 255), (int)(cc.srgb.z * 255), 255), 32);
        u.dl->AddCircle(pSw, r + 1.0f * s, white, 32, 2.0f * s);
        m_uiRects.push_back(ImVec4(pSw.x - R, pSw.y - R, pSw.x + R, pSw.y + R));
        if (h && !m_arcOpen) ImGui::SetTooltip("Couleur");
        if (h && u.pressed) {
            m_arcOpen = !m_arcOpen;
            m_arcDrag = m_arcOpen;
            m_arcPress = u.mouse;
            u.pressed = false;
        }
    }
    if (m_arcOpen) {
        std::vector<int> cols;
        const bool filter = !m_allColors && m_lib.hasColorTable(part);
        for (int i = 0; i < (int)m_lib.palette.size(); ++i)
            if (!filter || m_lib.colorExists(part, i)) cols.push_back(i);
        const int per = 12, pages = std::max(1, ((int)cols.size() + per - 1) / per);
        m_arcPage = std::clamp(m_arcPage, 0, pages - 1);
        const int first = m_arcPage * per, n = std::min(per, (int)cols.size() - first);
        const int slots = n + (pages > 1 ? 1 : 0);   // (+ the next page)
        const float rs = 22 * s;                    // swatches of 44 px
        const float halfDiag = 0.5f * std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
        const float rad = std::clamp(halfDiag + 34 * s, slots * 54 * s / 6.2832f, 0.45f * std::min(d.x, d.y - top));
        float step = 54 * s / rad;
        if (step * slots > 6.2832f) step = 6.2832f / slots;
        const ImVec2 ac(std::clamp(c.x, rad + rs + 4 * s, std::max(rad + rs + 4 * s, d.x - rad - rs - 4 * s)),
                        std::clamp(c.y, top + rad + rs + 4 * s, std::max(top + rad + rs + 4 * s, d.y - rad - rs - 4 * s)));
        m_uiRects.push_back(ImVec4(0, 0, d.x, d.y));   // open: a click outside closes it, nothing else
        int hit = -1;
        std::vector<ImVec2> at(slots);
        for (int k = 0; k < slots; ++k) {
            const float ang = -1.5708f + (k - (slots - 1) * 0.5f) * step;
            at[k] = ImVec2(ac.x + std::cos(ang) * rad, ac.y + std::sin(ang) * rad);
            if (hot(at[k], rs + 3 * s)) hit = k;
        }
        for (int k = 0; k < slots; ++k) {
            const bool h = k == hit;
            const float r = rs * (h ? 1.18f : 1.0f);
            const ImVec2 p = at[k];
            u.dl->AddCircleFilled(ImVec2(p.x + 1.5f * s, p.y + 2.5f * s), r + 1.5f * s, IM_COL32(0, 0, 0, 70), 40);
            if (k == n) {   // next page
                u.dl->AddCircleFilled(p, r, IM_COL32(30, 31, 36, 200), 40);
                u.dl->AddCircle(p, r, IM_COL32(255, 255, 255, 150), 40, 1.5f * s);
                char t[16];
                snprintf(t, sizeof(t), "%d/%d", m_arcPage + 1, pages);
                const float tw = u.textW(10, t, true);
                u.text(ImVec2(p.x - tw * 0.5f, p.y - 7 * s), 10, white, t, true);
                if (h) ImGui::SetTooltip("Autres couleurs");
                continue;
            }
            const Color& col = m_lib.palette[cols[first + k]];
            const bool trans = col.group == "transparent";
            u.dl->AddCircleFilled(p, r, IM_COL32((int)(col.srgb.x * 255), (int)(col.srgb.y * 255), (int)(col.srgb.z * 255), trans ? 170 : 255), 40);
            if (trans || col.group == "metallic")   // a sheen, as the swatches of the panel
                u.dl->AddCircleFilled(ImVec2(p.x - r * 0.3f, p.y - r * 0.3f), r * 0.35f, IM_COL32(255, 255, 255, trans ? 90 : 70), 24);
            if (cols[first + k] == curColor) u.dl->AddCircle(p, r + 2.0f * s, IM_COL32(255, 255, 255, 255), 40, 4.0f * s);
            else u.dl->AddCircle(p, r, IM_COL32(255, 255, 255, 140), 40, 1.5f * s);
        }
        if (hit >= 0 && hit < n) {   // the name of the colour under the cursor, at the centre of the arc
            const Color& col = m_lib.palette[cols[first + hit]];
            const float tw = u.textW(12, col.name.c_str(), true);
            const ImVec2 tp(ac.x - tw * 0.5f, ac.y - rad + rs + 14 * s);
            u.text(ImVec2(tp.x + sh.x, tp.y + sh.y), 12, IM_COL32(0, 0, 0, 150), col.name.c_str(), true);
            u.text(tp, 12, white, col.name.c_str(), true);
        }
        const bool slid = (u.mouse.x - m_arcPress.x) * (u.mouse.x - m_arcPress.x) + (u.mouse.y - m_arcPress.y) * (u.mouse.y - m_arcPress.y) > 64 * s * s;
        auto choose = [&](int k) {
            if (k == n) { m_arcPage = (m_arcPage + 1) % pages; return; }
            applyColor(cols[first + k]);
            m_arcOpen = false;
        };
        if (u.pressed) {
            if (hit >= 0) choose(hit);
            else if (!hot(pSw, R)) m_arcOpen = false;
        }
        if (u.released && m_arcDrag) {   // marking menu: pressed on the swatch, released on a colour
            m_arcDrag = false;
            if (slid && hit >= 0) choose(hit);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) m_arcOpen = false;
        return;   // (the other buttons hidden while the arc is open)
    }
    // turn 15° left / right, on the sides
    static const char* kTurnL = "M6 -7 A9 9 0 1 0 8 3 M6 -7 L6 -1 M6 -7 L0 -7";
    static const char* kTurnR = "M-6 -7 A9 9 0 1 1 -8 3 M-6 -7 L-6 -1 M-6 -7 L0 -7";
    static const char* kDup = "M-3 -9 h10 a2 2 0 0 1 2 2 v10 a2 2 0 0 1 -2 2 h-10 a2 2 0 0 1 -2 -2 v-10 a2 2 0 0 1 2 -2 z M-9 -3 v10 a2 2 0 0 0 2 2 h10 M2 -6 v8 M-2 -2 h8";
    const ImVec2 pL = place(ImVec2(a.x - 28 * s, c.y)), pR = place(ImVec2(b.x + 28 * s, c.y)), pD = place(ImVec2(c.x, b.y + 30 * s));
    struct Btn { ImVec2 p; const char* path; const char* tip; int id; };
    const Btn btns[3] = {{pL, kTurnL, "Tourner de 15° à gauche", 0}, {pR, kTurnR, "Tourner de 15° à droite", 1}, {pD, kDup, "Dupliquer", 2}};
    for (const Btn& bt : btns) {
        const bool h = hot(bt.p, R);
        lineIcon(bt.path, bt.p, h);
        if (h) ImGui::SetTooltip("%s", bt.tip);
        if (h && u.pressed) {
            if (bt.id == 2) duplicateSelected();
            else turnSelectedFree(bt.id == 0 ? 1 : -1);
        }
    }
    if (m_bricks[sel[0]].free) {   // the free angle, next to the swatch
        const float3 f = qRot(m_bricks[sel[0]].rot, float3(1, 0, 0));
        float deg = std::fmod(std::atan2(-f.z, f.x) * 57.29578f + 360.0f, 90.0f);
        if (deg > 45) deg -= 90;
        char t[16];
        snprintf(t, sizeof(t), "%+d°", (int)std::lround(deg));
        const ImVec2 tp(pSw.x + R + 2 * s, pSw.y - 10 * s);
        u.text(ImVec2(tp.x + sh.x, tp.y + sh.y), 13, IM_COL32(0, 0, 0, 170), t, true);
        u.text(tp, 13, white, t, true);
    }
}

// ------------------------------------------------------------------ colours panel (right)

void BrickApp::uiColors() {
    if (!m_pColors.visible || m_mechOn) return;
    Ui u = ui(m_ui);
    const float s = m_ui;
    ImVec2 d = ImGui::GetIO().DisplaySize;
    const float W = 104 * s, pad = 6 * s, sw = 28 * s, gap = 4 * s;
    // height of the content
    int nStd = 0, nTr = 0, nMe = 0;
    for (const Color& c : m_lib.palette) (c.group == "transparent" ? nTr : c.group == "metallic" ? nMe : nStd)++;
    auto rows = [](int n) { return (n + 2) / 3; };
    const float h6 = 16 * s;
    float H = pad + 14 * s + 6 * s;
    if (!m_pColors.collapsed) H += 14 * s + 6 * s + 18 * s + 3 * h6 + (rows(nStd) + rows(nTr) + rows(nMe)) * (sw + gap) + pad;
    // the colours that exist for real for the part selected or in hand (Rebrickable data); the others greyed out
    const int fbp = focusBrick();
    const int fpart = fbp >= 0 && fbp < (int)m_bricks.size() ? m_bricks[fbp].part : -1;
    const bool filter = !m_allColors && fpart >= 0 && m_lib.hasColorTable(fpart);
    const float headerH = 30 * s, trayTop = m_uiRects.size() > 1 ? m_uiRects[1].y : d.y;
    float y = std::max(headerH + 12 * s, headerH + (trayTop - headerH - H) * 0.5f);
    ImVec2 p0(d.x - 10 * s - W, y), p1(d.x - 10 * s, y + H);
    u.dl->AddRectFilled(p0, p1, C_OVERLAY, 14 * s);
    m_uiRects.push_back(ImVec4(p0.x, p0.y, p1.x, p1.y));
    u.grip(ImVec2(p0.x + pad, p0.y + pad), W - 2 * pad, "COULEURS", m_pColors.collapsed);
    if (u.hover(p0, ImVec2(p1.x, p0.y + pad + 14 * s)) && u.pressed) m_pColors.collapsed = !m_pColors.collapsed;
    if (m_pColors.collapsed) return;
    float cy = p0.y + pad + 14 * s + 6 * s;
    const Color& cur = m_lib.palette[std::clamp(m_color, 0, (int)m_lib.palette.size() - 1)];
    ImU32 curCol = IM_COL32((int)(cur.srgb.x * 255), (int)(cur.srgb.y * 255), (int)(cur.srgb.z * 255), 255);
    u.dl->AddRectFilled(ImVec2(p0.x + pad, cy + 1 * s), ImVec2(p0.x + pad + 12 * s, cy + 13 * s), curCol, 3 * s);
    u.dl->AddRect(ImVec2(p0.x + pad, cy + 1 * s), ImVec2(p0.x + pad + 12 * s, cy + 13 * s), IM_COL32(255, 255, 255, 51), 3 * s);
    char buf[96];
    snprintf(buf, sizeof(buf), "%s · %d", cur.name.c_str(), cur.id);
    u.dl->PushClipRect(ImVec2(p0.x, cy), ImVec2(p1.x - pad, cy + 16 * s), true);
    u.text(ImVec2(p0.x + pad + 18 * s, cy), 10.5f, C_DIM, buf);
    u.dl->PopClipRect();
    cy += 14 * s + 6 * s;
    {   // toggle: every colour (bypass the real colours of the part)
        ImVec2 ca(p0.x + pad + 2 * s, cy + 1 * s), cb(ca.x + 12 * s, ca.y + 12 * s);
        const bool h = u.hover(ImVec2(p0.x, cy), ImVec2(p1.x, cy + 16 * s));
        if (m_allColors) {
            u.dl->AddRectFilled(ca, cb, C_ACCENT, 3 * s);
            u.dl->AddPolyline(std::vector<ImVec2>{ImVec2(ca.x + 3 * s, ca.y + 6 * s), ImVec2(ca.x + 5 * s, ca.y + 9 * s), ImVec2(ca.x + 9.5f * s, ca.y + 3 * s)}.data(), 3, C_INK, 0, 1.6f * s);
        } else u.dl->AddRect(ca, cb, h ? C_TEXT : C_FAINT, 3 * s, 0, 1.3f * s);
        u.text(ImVec2(cb.x + 5 * s, cy), 9.5f, h ? C_TEXT : C_DIM, "Toutes les couleurs");
        if (h) ImGui::SetTooltip("Non coché : seules les couleurs qui existent pour cette pièce (données Rebrickable)");
        if (h && u.pressed) m_allColors = !m_allColors;
        cy += 18 * s;
    }
    auto group = [&](const char* title, const char* key) {
        u.text(ImVec2(p0.x + pad + 2 * s, cy + 3 * s), 9.5f, C_FAINT, title, true);
        cy += h6;
        int k = 0;
        for (int i = 0; i < (int)m_lib.palette.size(); ++i) {
            const Color& c = m_lib.palette[i];
            if (c.group != key) continue;
            ImVec2 a(p0.x + pad + 2 * s + (k % 3) * (sw + gap), cy + (k / 3) * (sw + gap)), b(a.x + sw, a.y + sw);
            ImU32 col = IM_COL32((int)(c.srgb.x * 255), (int)(c.srgb.y * 255), (int)(c.srgb.z * 255), 255);
            u.dl->AddRectFilled(a, b, col, 7 * s);
            if (c.group == "transparent")   // .sw.trans::after: light diagonal
                u.dl->AddRectFilledMultiColor(ImVec2(a.x + 4 * s, a.y + 4 * s), ImVec2(b.x - 4 * s, b.y - 4 * s), IM_COL32(255, 255, 255, 115), IM_COL32(255, 255, 255, 20), IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 20));
            if (c.group == "metallic")   // .sw.metal::after: vertical sheen
                u.dl->AddRectFilledMultiColor(a, ImVec2(b.x, (a.y + b.y) * 0.5f), IM_COL32(255, 255, 255, 115), IM_COL32(255, 255, 255, 115), IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
            u.dl->AddRect(a, b, IM_COL32(255, 255, 255, 26), 7 * s);
            const bool exists = !filter || m_lib.colorExists(fpart, i);
            if (!exists) {   // does not exist for this part: veiled and struck through
                u.dl->AddRectFilled(a, b, IM_COL32(0x13, 0x14, 0x17, 190), 7 * s);
                u.dl->AddLine(ImVec2(a.x + 5 * s, b.y - 5 * s), ImVec2(b.x - 5 * s, a.y + 5 * s), IM_COL32(255, 255, 255, 70), 1.5f * s);
            }
            if (i == m_color) u.dl->AddRect(ImVec2(a.x - 3 * s, a.y - 3 * s), ImVec2(b.x + 3 * s, b.y + 3 * s), C_ACCENT, 9 * s, 0, 2 * s);
            if (u.hover(a, b)) {
                if (exists) ImGui::SetTooltip("%s · %d", c.name.c_str(), c.id);
                else ImGui::SetTooltip("%s · %d : n'existe pas pour cette pièce (coche « Toutes les couleurs »)", c.name.c_str(), c.id);
                if (u.pressed && exists) applyColor(i);
            }
            ++k;
        }
        cy += rows(k) * (sw + gap);
    };
    group("UNIES", "standard");
    group("TRANSPARENTES", "transparent");
    group("MÉTAL", "metallic");
}

// ------------------------------------------------------------------ library (tray, bottom)

void BrickApp::uiTray() {
    if (!m_pTray.visible) { m_uiRects.push_back(ImVec4(0, ImGui::GetIO().DisplaySize.y, 0, ImGui::GetIO().DisplaySize.y)); return; }
    Ui u = ui(m_ui);
    if (m_gui.fontBold) u.bold = m_gui.fontBold;
    const float s = m_ui;
    ImVec2 d = ImGui::GetIO().DisplaySize;
    static const float kTh[3] = {64, 84, 112};
    const float th = kTh[std::clamp(m_thumbSize, 0, 2)] * s;
    const float padX = 12 * s, padT = 4 * s, padB = 6 * s, gap = 4 * s;
    const float gripH = 14 * s, famH = 30 * s, catH = 26 * s, cardsH = th * 2 + 6 * s + 6 * s;
    const float H = padT + gripH + (m_pTray.collapsed ? 0 : gap + famH + gap + catH + gap + cardsH) + padB;
    const bool fl = m_pTray.floating;
    const float TW = fl ? std::min(780 * s, d.x * 0.94f) : d.x;
    const float TX = fl ? std::max(0.0f, std::min(m_pTray.x, d.x - 80 * s)) : 0.0f;
    const float TY = fl ? std::max(0.0f, std::min(m_pTray.y, d.y - 34 * s)) : d.y - H;
    ImVec2 p0(TX, TY), p1(TX + TW, TY + H);
    const float alpha = m_mechOn ? 0.45f : 1.0f;   // #app.mech-on .tray { opacity: .45 }
    if (fl) {   // .tray.floating: accent border .55, radius 12, shadow
        m_uiRects.push_back(ImVec4(0, d.y, 0, d.y));   // (index 1: the stage reaches the bottom)
        u.dl->AddRectFilled(ImVec2(p0.x, p0.y + 14 * s), ImVec2(p1.x, p1.y + 14 * s), IM_COL32(0, 0, 0, 70), 12 * s);
        u.dl->AddRectFilled(p0, p1, C_PANEL, 12 * s);
        u.dl->AddRect(p0, p1, IM_COL32(247, 197, 30, 140), 12 * s);
    } else {
        u.dl->AddRectFilled(p0, p1, C_PANEL);
        u.dl->AddLine(ImVec2(0, p0.y + 0.5f), ImVec2(d.x, p0.y + 0.5f), C_LINE, 1.0f);
    }
    m_uiRects.push_back(ImVec4(p0.x, p0.y, p1.x, p1.y));
    float y = p0.y + padT;
    u.grip(ImVec2(TX + padX, y), TW - 2 * padX, "BIBLIOTHÈQUE", m_pTray.collapsed);
    if (gripPanel(0, m_pTray, ImVec2(TX, y - 2 * s), ImVec2(TX + TW, y + gripH), p0)) return;
    if (m_pTray.collapsed) return;
    const bool live = !m_mechOn;
    y += gripH + gap;
    // ---- row 1: recent / favourites icons, families with counts, then search and size tools on the right
    // .fscroll: the tabs scroll sideways (scrollbar hidden) left of the tools (.ftools: search field, magnifier, size)
    const float toolsW = (m_searchOpen ? 194 * s : 0) + 36 * s + 32 * s;
    const ImVec2 fa(TX + padX, y), fb(TX + TW - padX - toolsW - 6 * s, y + famH);
    const float famStart = TX + padX - m_famScroll;
    float x = famStart;
    u.dl->PushClipRect(fa, fb, true);
    auto famTab = [&](const char* label, int n, bool selected, svg::Icon icon, bool isIcon, float& xx) {
        char cnt[16];
        snprintf(cnt, sizeof(cnt), "%d", n);
        float w = isIcon ? 32 * s : 12 * s + u.textW(13, label, selected) + (n >= 0 ? 4 * s + u.textW(11, cnt) : 0) + 12 * s;
        ImVec2 a(xx, y), b(xx + w, y + famH);
        bool h = live && u.hover(a, b) && u.hover(fa, fb);
        if (selected) u.dl->AddRectFilled(a, b, alphaOf(C_CARD_HI, alpha), 15 * s);
        ImU32 col = alphaOf(selected ? C_ACCENT : (h ? C_TEXT : C_DIM), alpha);
        if (isIcon) svg::icon(u.dl, icon, ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), 16 * s, col);
        else {
            ImVec2 ts = u.text(ImVec2(a.x + 12 * s, a.y + 7 * s), 13, col, label, selected);
            if (n >= 0) u.text(ImVec2(a.x + 12 * s + ts.x + 4 * s, a.y + 9 * s), 11, alphaOf(col, 0.55f), cnt);
        }
        xx += w + 4 * s;
        return h && u.released && !swipeTaken(1) && m_swipeDragged != 1;
    };
    if (famTab("", -1, m_fam == "recent" && !m_searchOpen, svg::I_RECENT, true, x)) { m_fam = "recent"; m_cat.clear(); m_sub.clear(); m_searchOpen = false; m_cardsScroll = 0; }
    if (u.hover(ImVec2(x - 36 * s, y), ImVec2(x - 4 * s, y + famH))) ImGui::SetTooltip("Récentes");
    if (famTab("", -1, m_fam == "fav" && !m_searchOpen, svg::I_FAV, true, x)) { m_fam = "fav"; m_cat.clear(); m_sub.clear(); m_searchOpen = false; m_cardsScroll = 0; }
    if (u.hover(ImVec2(x - 36 * s, y), ImVec2(x - 4 * s, y + famH))) ImGui::SetTooltip("Favoris (appui long sur une pièce pour l'ajouter)");
    // parts per family, per category, per Technic sub-family: counted once (the full library has 22 000 parts)
    if (m_countsFor != m_lib.parts.size()) {
        m_countsFor = m_lib.parts.size();
        m_catCount.clear();
        m_techSubCount.clear();
        m_availCount = 0;
        for (int i = 0; i < (int)m_lib.parts.size(); ++i) {
            const Part& p = m_lib.parts[i];
            if (!p.available()) continue;
            ++m_availCount;
            ++m_catCount[partCat(i)];
            if (p.category == "Technic") ++m_techSubCount[p.sub];
        }
    }
    const int loaded = m_availCount;
    if (famTab("Toutes", loaded, m_fam == "all" && !m_searchOpen, svg::I_RECENT, false, x)) { m_fam = "all"; m_cat.clear(); m_sub.clear(); m_searchOpen = false; m_cardsScroll = 0; }
    for (const Fam& f : families()) {
        int n = 0;
        for (const char* k : f.cats) { auto it = m_catCount.find(k); if (it != m_catCount.end()) n += it->second; }
        if (famTab(f.name, n, m_fam == f.name && !m_searchOpen, svg::I_RECENT, false, x)) { m_fam = f.name; m_cat.clear(); m_sub.clear(); m_searchOpen = false; m_cardsScroll = 0; }
    }
    u.dl->PopClipRect();
    {
        const float content = x - famStart, view = fb.x - fa.x;
        if (live && u.hover(fa, fb)) m_famScroll -= (ImGui::GetIO().MouseWheel + ImGui::GetIO().MouseWheelH) * 60 * s;
        swipeRow(1, fa, fb, m_famScroll, m_famVel, std::max(0.0f, content - view), live);
    }
    // tools on the right: [search field] search, size
    float tx = TX + TW - padX - 32 * s;
    {
        ImVec2 a(tx, y), b(tx + 32 * s, y + famH);
        bool h = live && u.hover(a, b);
        svg::icon(u.dl, svg::I_SIZE, ImVec2(a.x + 16 * s, a.y + 15 * s), 16 * s, alphaOf(h ? C_TEXT : C_DIM, alpha));
        static const char* sizes[3] = {"petites", "moyennes", "grandes"};
        if (h) ImGui::SetTooltip("Taille des miniatures (%s)", sizes[std::clamp(m_thumbSize, 0, 2)]);
        if (h && u.pressed) m_thumbSize = (m_thumbSize + 1) % 3;
        tx -= 36 * s;
        a = ImVec2(tx, y); b = ImVec2(tx + 32 * s, y + famH);
        h = live && u.hover(a, b);
        if (m_searchOpen) u.dl->AddRectFilled(a, b, C_CARD_HI, 15 * s);
        svg::icon(u.dl, svg::I_SEARCH, ImVec2(a.x + 16 * s, a.y + 15 * s), 16 * s, alphaOf(m_searchOpen ? C_ACCENT : (h ? C_TEXT : C_DIM), alpha));
        if (h) ImGui::SetTooltip("Chercher une pièce");
        if (h && u.pressed) { m_searchOpen = !m_searchOpen; if (!m_searchOpen) m_search.clear(); }
        if (m_searchOpen) {   // input.search: 190 px, 28 px high, round, card background
            ImVec2 sa(tx - 194 * s, y + 1 * s);
            ImGui::SetNextWindowPos(sa);
            ImGui::SetNextWindowSize(ImVec2(190 * s, 28 * s));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 14 * s);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12 * s, 6 * s));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(C_CARD));
            ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(C_LINE));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
            ImGui::Begin("##search", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);
            char buf[128];
            snprintf(buf, sizeof(buf), "%s", m_search.c_str());
            ImGui::SetNextItemWidth(190 * s);
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            if (ImGui::InputTextWithHint("##q", "Nom ou réf. LDraw…", buf, sizeof(buf))) { m_search = buf; m_cardsScroll = 0; }
            if (ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape)) { m_search.clear(); m_searchOpen = false; }
            ImGui::End();
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(4);
        }
    }
    y += famH + gap;
    // ---- row 2: categories (pills) or a note; .cats scrolls sideways too
    const ImVec2 qa(TX + padX, y), qb(TX + TW - padX, y + catH);
    const float catStart = TX + padX - m_catScroll;
    x = catStart;
    u.dl->PushClipRect(qa, qb, true);
    auto catPill = [&](const char* label, int n, bool selected, bool empty) {
        char cnt[16];
        snprintf(cnt, sizeof(cnt), "%d", n);
        float w = 10 * s + u.textW(12, label) + (n > 0 ? 4 * s + u.textW(10.5f, cnt) : 0) + 10 * s;
        ImVec2 a(x, y + 1 * s), b(x + w, y + 25 * s);
        bool h = live && !empty && u.hover(a, b) && u.hover(qa, qb);
        float al = (empty ? 0.38f : 1.0f) * alpha;
        ImU32 border = selected ? C_ACCENT : C_LINE;
        if (empty) {   // dashed border
            for (float dx = a.x + 8 * s; dx < b.x - 8 * s; dx += 6 * s) {
                u.dl->AddLine(ImVec2(dx, a.y), ImVec2(dx + 3 * s, a.y), alphaOf(border, al), 1.0f);
                u.dl->AddLine(ImVec2(dx, b.y), ImVec2(dx + 3 * s, b.y), alphaOf(border, al), 1.0f);
            }
        } else u.dl->AddRect(a, b, alphaOf(border, al), 12 * s, 0, 1.0f);
        ImU32 col = alphaOf(selected ? C_ACCENT : (h ? C_TEXT : C_DIM), al);
        ImVec2 ts = u.text(ImVec2(a.x + 10 * s, a.y + 4.5f * s), 12, col, label);
        if (n > 0) u.text(ImVec2(a.x + 10 * s + ts.x + 4 * s, a.y + 6 * s), 10.5f, alphaOf(col, 0.55f), cnt);
        x += w + 4 * s;
        return h && u.released && !swipeTaken(2) && m_swipeDragged != 2;
    };
    const Fam* fam = nullptr;
    for (const Fam& f : families()) if (m_fam == f.name) fam = &f;
    if (m_searchOpen && !m_search.empty()) { std::string t = "Recherche « " + m_search + " »"; u.text(ImVec2(x + 4 * s, y + 6 * s), 11.5f, C_FAINT, t.c_str()); }
    else if (!fam) {
        const char* t = m_fam == "recent" ? "Récentes : les 24 dernières pièces posées" : m_fam == "fav" ? "Favoris : appui long sur une pièce > Ajouter aux favoris" : "Toutes les pièces importées";
        u.text(ImVec2(x + 4 * s, y + 6 * s), 11.5f, C_FAINT, t);
    } else {
        if (catPill("Tout", 0, m_cat.empty(), false)) { m_cat.clear(); m_sub.clear(); m_cardsScroll = 0; }
        std::vector<std::pair<const char*, int>> cs;
        for (const char* c : fam->cats) {
            auto it = m_catCount.find(c);
            cs.push_back({c, it == m_catCount.end() ? 0 : it->second});
        }
        std::stable_sort(cs.begin(), cs.end(), [](const std::pair<const char*, int>& a, const std::pair<const char*, int>& b) { return (a.second > 0) > (b.second > 0); });
        for (auto& c : cs) {
            if (std::string(c.first) == "Technic" && c.second) {   // 700+ Technic parts: one chip per sub-family
                for (const char* sn : kTechSubs) {
                    auto it = m_techSubCount.find(sn);
                    const int k = it == m_techSubCount.end() ? 0 : it->second;
                    if (!k) continue;
                    if (catPill(sn, k, m_cat == "Technic" && m_sub == sn, false)) { m_cat = "Technic"; m_sub = sn; m_cardsScroll = 0; }
                }
                continue;
            }
            if (catPill(catFr(c.first), c.second, m_cat == c.first, c.second == 0)) { m_cat = c.first; m_sub.clear(); m_cardsScroll = 0; }
        }
    }
    u.dl->PopClipRect();
    {
        const float content = x - catStart, view = qb.x - qa.x;
        if (live && u.hover(qa, qb)) m_catScroll -= (ImGui::GetIO().MouseWheel + ImGui::GetIO().MouseWheelH) * 60 * s;
        swipeRow(2, qa, qb, m_catScroll, m_catVel, std::max(0.0f, content - view), live);
    }
    y += catH + gap;
    // ---- cards on two rows (column by column), horizontal scroll with the wheel
    std::vector<int> parts = visibleParts();
    const float cgap = 6 * s;
    const int cols = (int)(parts.size() + 1) / 2;
    const float contentW = cols * (th + cgap), viewW = TW - 2 * padX;
    ImVec2 ca(TX + padX, y), cb(TX + TW - padX, y + cardsH);
    if (live && u.hover(ca, cb) && ImGui::GetIO().MouseWheel != 0) m_cardsScroll -= ImGui::GetIO().MouseWheel * th;
    // click + drag sideways = slide the cards (inertia); upwards = the brick goes in hand (below)
    swipeRow(0, ca, cb, m_cardsScroll, m_cardsVel, std::max(0.0f, contentW - viewW), live);
    u.dl->PushClipRect(ca, cb, true);
    if (parts.empty()) {
        const char* msg = (m_searchOpen && !m_search.empty()) ? "Aucune pièce pour cette recherche" : m_fam == "recent" ? "Les pièces que tu poses apparaîtront ici"
                        : m_fam == "fav" ? "Aucun favori : appui long sur une pièce, puis Ajouter aux favoris" : "Aucune pièce importée dans cette catégorie";
        u.text(ImVec2(TX + padX + 8 * s, y + th - 8 * s), 12, C_FAINT, msg);
    }
    const double now = ImGui::GetTime();
    for (int k = 0; k < (int)parts.size(); ++k) {
        int c = k / 2, r = k % 2;
        ImVec2 a(TX + padX + c * (th + cgap) - m_cardsScroll, y + r * (th + cgap)), b(a.x + th, a.y + th);
        if (b.x < ca.x || a.x > cb.x) continue;
        const int part = parts[k];
        bool h = live && u.hover(a, b) && u.hover(ca, cb);
        u.dl->AddRectFilled(a, b, alphaOf(h ? C_CARD_HI : C_CARD, alpha), 10 * s);
        ImTextureID tex;
        ImVec2 uv0, uv1;
        if (thumbUv(part, tex, uv0, uv1)) {   // img: object-fit contain, scale(1.4): the middle of the thumbnail, zoomed
            ImVec2 du((uv1.x - uv0.x) * (1 - 1 / 1.4f) * 0.5f, (uv1.y - uv0.y) * (1 - 1 / 1.4f) * 0.5f);
            u.dl->AddImageRounded(tex, ImVec2(a.x + 1.5f * s, a.y + 1.5f * s), ImVec2(b.x - 1.5f * s, b.y - 1.5f * s), ImVec2(uv0.x + du.x, uv0.y + du.y),
                                  ImVec2(uv1.x - du.x, uv1.y - du.y), alphaOf(IM_COL32_WHITE, alpha), 9 * s);
        }
        u.dl->AddRect(a, b, alphaOf(h || m_libPress == part ? C_ACCENT : C_LINE, alpha), 10 * s, 0, 1.5f * s);
        if (std::find(m_fav.begin(), m_fav.end(), part) != m_fav.end()) u.text(ImVec2(b.x - 14 * s, a.y + 2 * s), 11, C_ACCENT, "★");
        if (m_dev && m_devUseOverrides && m_devOverrides.count(devId(part))) {   // .card.devmod::after: "M" badge
            ImVec2 ma(b.x - 6 * s - 18 * s, a.y + 6 * s), mb(b.x - 6 * s, a.y + 24 * s);
            u.dl->AddRectFilled(ma, mb, C_DEV, 5 * s);
            float mw = u.textW(11, "M", true);
            u.text(ImVec2((ma.x + mb.x) * 0.5f - mw * 0.5f, ma.y + 2 * s), 11, IM_COL32(0x0d, 0x1a, 0x1f, 255), "M", true);
        }
        if (h) {
            char tip[200];
            snprintf(tip, sizeof(tip), "%s · %s", m_lib.parts[part].label.c_str(), m_lib.parts[part].name.c_str());
            if (m_libPress < 0) ImGui::SetTooltip("%s", tip);
            if (u.pressed) { m_libPress = part; m_libPressPos = u.mouse; m_libPressTime = now; m_libLong = false; }
        }
    }
    u.dl->PopClipRect();
    // scrollbar (thin)
    if (contentW > viewW) {
        float bw = viewW * viewW / contentW, bx = TX + padX + (viewW - bw) * (m_cardsScroll / (contentW - viewW));
        u.dl->AddRectFilled(ImVec2(bx, cb.y - 3 * s), ImVec2(bx + bw, cb.y - 1 * s), C_LINE, 2 * s);
    }
    // press on a card: long press (480 ms, still) = part sheet; drag out of the tray = brick in hand following the
    // cursor (drop on release); click = brick in hand in the middle of the view
    if (m_libPress >= 0 && (swipeTaken(0) || m_swipeDragged == 0)) m_libPress = -1;   // a side drag: no brick
    if (m_libPress >= 0) {
        ImVec2 dm(u.mouse.x - m_libPressPos.x, u.mouse.y - m_libPressPos.y);
        const bool moved = dm.x * dm.x + dm.y * dm.y > 64 * s * s;
        if (!m_libLong && !moved && now - m_libPressTime > 0.48) { m_libLong = true; m_infoPart = m_libPress; }
        if (moved && u.mouse.y < p0.y) {   // out of the tray: drag and drop
            int part = m_libPress;
            m_libPress = -1;
            spawn(part, u.mouse.x, u.mouse.y);
            m_dragStarted = true;   // released over the view = dropped (editInput)
            m_pressed = false;
        } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (!m_libLong && !moved) spawn(m_libPress, d.x * 0.5f, (30 * s + p0.y) * 0.55f);
            m_libPress = -1;
        }
    }
}

// ------------------------------------------------------------------ Mechanical panel (right)

void BrickApp::uiMechPanel() {
    if (!m_mechOn) return;
    Ui u = ui(m_ui);
    if (m_gui.fontBold) u.bold = m_gui.fontBold;
    const float s = m_ui;
    ImVec2 d = ImGui::GetIO().DisplaySize;
    const float W = 280 * s, pad = 12 * s;
    ImVec2 p0(d.x - 16 * s - W, 30 * s + 12 * s);
    // content height first (two passes would be cleaner; the layout is linear)
    float H = pad + 20 * s + 38 * s + 38 * s + 44 * s + m_mm.warnings.size() * 18 * s + m_mm.cmds.size() * 78 * s + 30 * s + pad;
    if (m_mm.cmds.empty()) H += 34 * s;
    ImVec2 p1(p0.x + W, p0.y + H);
    u.dl->AddRectFilled(p0, p1, C_PANEL, 12 * s);
    u.dl->AddRect(p0, p1, C_LINE, 12 * s);
    m_uiRects.push_back(ImVec4(p0.x, p0.y, p1.x, p1.y));
    float y = p0.y + pad, x = p0.x + pad;
    u.text(ImVec2(x, y), 14, C_TEXT, "Mécanique", true);
    {   // Quitter: back to building
        float qw = u.textW(12, "Quitter") + 20 * s;
        ImVec2 a(p1.x - pad - qw, y - 6 * s), b(p1.x - pad, y + 24 * s);
        bool h = u.hover(a, b);
        u.dl->AddRectFilled(a, b, h ? C_CARD_HI : C_CARD, 7 * s);
        u.text(ImVec2(a.x + 10 * s, a.y + 8 * s), 12, C_TEXT, "Quitter");
        if (h) ImGui::SetTooltip("Revenir au montage (Échap)");
        if (h && u.pressed) { mechExit(); return; }
    }
    y += 26 * s;
    auto smallBtn = [&](const char* label, bool on, float& bx, float by) {
        float w = u.textW(12, label, on) + 20 * s;
        ImVec2 a(bx, by), b(bx + w, by + 30 * s);
        bool h = u.hover(a, b);
        u.dl->AddRectFilled(a, b, on ? C_ACCENT : (h ? C_CARD_HI : C_CARD), 7 * s);
        u.text(ImVec2(a.x + 10 * s, a.y + 8 * s), 12, on ? C_INK : C_TEXT, label, on);
        bx += w + 6 * s;
        return h && u.pressed;
    };
    float bx = x;
    if (smallBtn("Pose de montage", false, bx, y)) {
        for (MechCmd& c : m_mm.cmds) c.value = 0;
        std::fill(m_mm.q.begin(), m_mm.q.end(), 0.0);
        m_mech.solve(m_mm);
        mechApplyPose();
    }
    y += 38 * s;
    bx = x;
    if (smallBtn("Liaisons", m_mechJoints, bx, y)) m_mechJoints = !m_mechJoints;
    if (smallBtn("Couleurs par corps", m_mechTint, bx, y)) { m_mechTint = !m_mechTint; syncObjects(); mechApplyPose(); }
    y += 38 * s;
    // stats
    int nRev = 0, nSlide = 0;
    for (const MechTreeJ& t : m_mm.tree) (t.j.type == 1 ? nSlide : nRev)++;
    for (const MechJoint& j : m_mm.closures) (j.type == 1 ? nSlide : nRev)++;
    char line[200];
    snprintf(line, sizeof(line), "%d corps (%d mobiles) · %d pivots · %d vérins", m_mm.bodyCount, m_mm.bodyCount - (int)m_mm.roots.size(), nRev, nSlide);
    u.text(ImVec2(x, y), 11.5f, C_DIM, line);
    snprintf(line, sizeof(line), "%d engrenages · %d boucles · %d DDL pour %d commandes", (int)m_mm.meshes.size(), (int)m_mm.closures.size(), m_mm.dof, (int)m_mm.cmds.size());
    u.text(ImVec2(x, y + 20 * s), 11.5f, C_DIM, line);
    y += 44 * s;
    for (const std::string& w : m_mm.warnings) { u.text(ImVec2(x, y), 12, IM_COL32(0xff, 0x8a, 0x80, 255), w.c_str()); y += 18 * s; }
    if (m_mm.cmds.empty()) {
        u.text(ImVec2(x, y), 12, C_DIM, "Aucune commande : ajoute un vérin ou un train");
        u.text(ImVec2(x, y + 16 * s), 12, C_DIM, "d'engrenages, ou attrape une pièce mobile.");
        y += 34 * s;
    }
    ImGuiIO& io = ImGui::GetIO();
    for (size_t k = 0; k < m_mm.cmds.size(); ++k) {
        MechCmd& c = m_mm.cmds[k];
        u.dl->AddLine(ImVec2(x, y), ImVec2(p1.x - pad, y), C_LINE, 1.0f);
        y += 9 * s;
        ImVec2 ns = u.text(ImVec2(x, y), 12.5f, C_TEXT, c.name.c_str(), true);
        std::string ratio;
        for (auto& r : c.ratio) {
            char b[24];
            float v = std::fabs(1.0f / r.second);
            snprintf(b, sizeof(b), "%s1:%.1f", ratio.empty() ? "" : ", ", v);
            std::string t(b);
            size_t z = t.find(".0");
            if (z != std::string::npos && z + 2 == t.size()) t.erase(z);
            ratio += t;
        }
        if (!ratio.empty()) u.text(ImVec2(x + ns.x + 6 * s, y + 1 * s), 11.5f, C_FAINT, ratio.c_str());
        if (c.type == 1) snprintf(line, sizeof(line), "%.1f cm", c.value);
        else snprintf(line, sizeof(line), "%d°", (int)std::lround(c.value * 180.0f / 3.14159265f));
        float vw = u.textW(12, line);
        u.text(ImVec2(p1.x - pad - vw, y), 12, C_ACCENT, line);
        y += 20 * s;
        // range -720..720 degrees, accent colour
        ImVec2 ra(x, y + 6 * s), rb(p1.x - pad, y + 10 * s);
        u.dl->AddRectFilled(ra, rb, C_CARD_HI, 2 * s);
        float deg = std::clamp(c.value * 180.0f / 3.14159265f, -720.0f, 720.0f), t = (deg + 720.0f) / 1440.0f;
        if (c.type == 1) t = c.hi > c.lo ? std::clamp((c.value - c.lo) / (c.hi - c.lo), 0.0f, 1.0f) : 0.5f;
        float kx = ra.x + (rb.x - ra.x) * t;
        u.dl->AddRectFilled(ra, ImVec2(kx, rb.y), C_ACCENT, 2 * s);
        u.dl->AddCircleFilled(ImVec2(kx, (ra.y + rb.y) * 0.5f), 7 * s, C_ACCENT, 20);
        static int dragCmd = -1;
        if (u.hover(ImVec2(ra.x, y), ImVec2(rb.x, y + 16 * s)) && u.pressed) dragCmd = (int)k;
        if (dragCmd == (int)k) {
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) dragCmd = -1;
            else if (c.type == 1) c.value = c.lo + (c.hi - c.lo) * std::clamp((u.mouse.x - ra.x) / (rb.x - ra.x), 0.0f, 1.0f);
            else c.value = (-720.0f + 1440.0f * std::clamp((u.mouse.x - ra.x) / (rb.x - ra.x), 0.0f, 1.0f)) * 3.14159265f / 180.0f;
        }
        y += 20 * s;
        // keys: ◀ Q / A ▶ (hold buttons)
        auto hold = [&](const char* glyph, float hx, int dir) {
            ImVec2 a(hx, y), b(hx + 34 * s, y + 28 * s);
            bool h = u.hover(a, b), down = h && ImGui::IsMouseDown(ImGuiMouseButton_Left);
            u.dl->AddRectFilled(a, b, down ? C_ACCENT : C_CARD, 7 * s);
            (void)glyph;   // ◀ / ▶ drawn as triangles (the UI font has no such glyphs)
            ImVec2 m((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
            const float t = 5 * s;
            if (dir < 0) u.dl->AddTriangleFilled(ImVec2(m.x - t, m.y), ImVec2(m.x + t * 0.8f, m.y - t), ImVec2(m.x + t * 0.8f, m.y + t), down ? C_INK : C_TEXT);
            else u.dl->AddTriangleFilled(ImVec2(m.x + t, m.y), ImVec2(m.x - t * 0.8f, m.y - t), ImVec2(m.x - t * 0.8f, m.y + t), down ? C_INK : C_TEXT);
            if (down) c.value += dir * c.speed * io.DeltaTime;
        };
        hold("◀", x, -1);
        float kx2 = x + 40 * s;
        auto kbd = [&](const char* t) {
            float w = u.textW(11.5f, t) + 12 * s;
            u.dl->AddRectFilled(ImVec2(kx2, y + 5 * s), ImVec2(kx2 + w, y + 23 * s), C_CARD, 4 * s);
            u.text(ImVec2(kx2 + 6 * s, y + 7 * s), 11.5f, C_TEXT, t);
            kx2 += w + 4 * s;
        };
        kbd(c.keyMinus ? ImGui::GetKeyName((ImGuiKey)c.keyMinus) : "");
        u.text(ImVec2(kx2, y + 6 * s), 12, C_DIM, "/");
        kx2 += 10 * s;
        kbd(c.keyPlus ? ImGui::GetKeyName((ImGuiKey)c.keyPlus) : "");
        hold("▶", kx2 + 2 * s, 1);
        y += 36 * s;
    }
    u.text(ImVec2(x, y + 4 * s), 10.5f, C_DIM, "Glisse une pièce mobile : tout ce qui y est relié suit.");
    if (!m_mechFlash.empty() && ImGui::GetTime() - m_mechFlashTime < 0.9) {
        ImVec2 a(x, y + 24 * s), b(p1.x - pad, y + 50 * s);
        u.dl->AddRectFilled(a, b, IM_COL32(229, 72, 77, 46), 8 * s);
        u.text(ImVec2(a.x + 8 * s, a.y + 6 * s), 12, IM_COL32(0xff, 0xb3, 0xb5, 255), m_mechFlash.c_str());
    }
}

// ------------------------------------------------------------------ HUD (Fenêtres > Infos), part sheet, popups, toast

void BrickApp::uiHud() {
    if (!m_pHud.visible) return;
    Ui u = ui(m_ui);
    const float s = m_ui;
    char l1[160], l2[200];
    snprintf(l1, sizeof(l1), "%d pièces · %d fps · GPU %.2f ms", (int)m_bricks.size(), (int)std::lround(m_frameMs > 0 ? 1000.0 / m_frameMs : 0.0), m_renderer.gpuFrameMs());
    const char* st = m_mechOn ? "Mode Mécanique : touches du panneau ou glisse une pièce mobile"
                   : m_holding ? (m_held.suggested ? "Pivot voisin valide proposé" : m_held.blocked ? "Placement refusé (> 50 %)" : "Brique en main")
                   : m_selected >= 0 ? m_lib.parts[m_bricks[m_selected].part].label.c_str() : "Touche une brique pour la prendre";
    snprintf(l2, sizeof(l2), "%s", st);
    float w = std::max(u.textW(11, l1), u.textW(11, l2)) + 16 * s;
    ImVec2 a(10 * s, 30 * s + 8 * s), b(a.x + w, a.y + 40 * s);
    u.dl->AddRectFilled(a, b, C_HUD, 10 * s);
    u.text(ImVec2(a.x + 8 * s, a.y + 4 * s), 11, C_DIM, l1);
    u.text(ImVec2(a.x + 8 * s, a.y + 20 * s), 11, C_DIM, l2);
}

// Part sheet of the normal mode (web: showPartSimple): turntable 3D view ("Glisse pour tourner la pièce"), name,
// family › category, ☆ Favori, Fermer, Ajouter à la scène. Box min(440 px, 92 vw), padding 18, gap 8.
void BrickApp::uiInfoSheet() {
    if (m_infoPart < 0) return;
    if (m_dev) { uiDevSheet(); return; }   // full sheet (metadata, connectors, Maya export / import)
    Ui u = ui(m_ui);
    if (m_gui.fontBold) u.bold = m_gui.fontBold;
    const float s = m_ui;
    ImVec2 d = ImGui::GetIO().DisplaySize;
    u.dl->AddRectFilled(ImVec2(0, 0), d, IM_COL32(0, 0, 0, 128));   // .sheet rgba(0, 0, 0, .5)
    m_uiRects.push_back(ImVec4(0, 0, d.x, d.y));
    const Part& p = m_lib.parts[m_infoPart];
    if (m_pvPart != m_infoPart) { m_pvPart = m_infoPart; m_pvYaw = m_pvPitch = 0; }
    const float W = std::min(440 * s, d.x * 0.92f), pad = 18 * s, gap = 8 * s;
    const float vw = W - 2 * pad, vh = vw * 4 / 5;   // canvas aspect 5 / 4
    const float H = pad + vh + gap + 4 * s + 24 * s + gap + 17 * s + gap + 28 * s + pad;
    ImVec2 p0((d.x - W) * 0.5f, (d.y - H) * 0.5f), p1(p0.x + W, p0.y + H);
    u.dl->AddRectFilled(p0, p1, C_PANEL_SOLID, 14 * s);
    u.dl->AddRect(p0, p1, C_LINE, 14 * s);
    // viewer
    ImVec2 va(p0.x + pad, p0.y + pad), vb(va.x + vw, va.y + vh);
    u.dl->AddRectFilled(va, vb, C_CARD, 12 * s);
    m_pvRect = ImVec4(va.x, va.y, vb.x, vb.y);
    if (m_frozenTex) u.dl->AddImageRounded(m_gui.viewportTexture(), va, vb, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 12 * s);
    u.text(ImVec2(va.x + 10 * s, vb.y - 8 * s - 13 * s), 10.5f, C_FAINT, "Glisse pour tourner la pièce");
    ImGuiIO& io = ImGui::GetIO();
    const bool overView = u.hover(va, vb);
    if (overView) ImGui::SetMouseCursor(m_pvDrag ? ImGuiMouseCursor_ResizeAll : ImGuiMouseCursor_Hand);   // cursor: grab
    if (overView && u.pressed) m_pvDrag = true;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) m_pvDrag = false;
    if (m_pvDrag) {
        m_pvYaw += io.MouseDelta.x * 0.012f;
        m_pvPitch = std::clamp(m_pvPitch + io.MouseDelta.y * 0.01f, -1.4f, 1.4f);
    }
    // name, family › category
    float y = vb.y + gap + 4 * s;
    u.text(ImVec2(p0.x + pad, y), 18, C_TEXT, p.label.c_str(), true);
    y += 24 * s + gap;
    const std::string cat = partCat(m_infoPart);
    const char* fam = "Autres gammes";
    for (const Fam& f : families())
        for (const char* c : f.cats)
            if (cat == c) fam = f.name;
    std::string path = std::string(fam) + " › " + catFr(cat);
    u.text(ImVec2(p0.x + pad, y), 12.5f, C_DIM, path.c_str());
    y += 17 * s + gap;
    // row: ☆ Favori · grow · Fermer · Ajouter à la scène (.btn compact: 28 px, 12 px, padding 10, radius 7)
    auto button = [&](float x, const char* label, bool accent, ImU32 textCol, bool& clicked) {
        float w = u.textW(12, label) + 20 * s;
        ImVec2 a(x, y), b(x + w, y + 28 * s);
        bool h = u.hover(a, b);
        u.dl->AddRectFilled(a, b, accent ? C_ACCENT : (h ? C_CARD_HI : C_CARD), 7 * s);
        u.text(ImVec2(a.x + 10 * s, a.y + 7 * s), 12, accent ? C_INK : textCol, label, accent);
        clicked = h && u.pressed;
        return w;
    };
    const bool fav = std::find(m_fav.begin(), m_fav.end(), m_infoPart) != m_fav.end();
    bool cFav = false, cClose = false, cAdd = false;
    button(p0.x + pad, fav ? "★ Favori" : "☆ Favori", false, fav ? C_ACCENT : C_TEXT, cFav);
    const float addW = u.textW(12, "Ajouter à la scène") + 20 * s, closeW = u.textW(12, "Fermer") + 20 * s;
    button(p1.x - pad - addW, "Ajouter à la scène", true, C_INK, cAdd);
    button(p1.x - pad - addW - gap - closeW, "Fermer", false, C_TEXT, cClose);
    if (cFav) {
        if (fav) m_fav.erase(std::find(m_fav.begin(), m_fav.end(), m_infoPart));
        else m_fav.push_back(m_infoPart);
        savePrefs();
    }
    const int part = m_infoPart;
    if (cClose || ImGui::IsKeyPressed(ImGuiKey_Escape) || (u.pressed && !u.hover(p0, p1))) m_infoPart = -1;
    if (cAdd) {
        m_infoPart = -1;
        ImVec2 c((m_stage.x + m_stage.z) * 0.5f, (m_stage.y + m_stage.w) * 0.5f);
        spawn(part, c.x, c.y);
    }
}

void BrickApp::uiPopups() {
    if (!m_popup) return;
    if (m_popup == 5) { uiSettings(); return; }
    Ui u = ui(m_ui);
    const float s = m_ui;
    struct Item { const char* label; bool on; int id; };
    std::vector<Item> items;
    if (m_popup == 1) items = {{"Rapide", m_rs.raster && m_quality == 0, 0}, {"HQ", m_rs.raster && m_quality == 1, 1}, {"Path tracing", pathTraced(), 2}, {"Radiance Cascades", m_rcMode, 3}};
    else if (m_popup == 2) items = {{"ACES", m_rs.tonemapper == 0, 0}, {"AgX", m_rs.tonemapper == 1, 1}, {"Std", m_rs.tonemapper == 3, 3}};
    else if (m_popup == 3) items = {{"Bibliothèque", m_pTray.visible, 0}, {"Manuel d'instructions", m_pManual.visible, 4}, {"Infos (pièces, fps)", m_pHud.visible, 3},
                                    {"Couleurs", m_pColors.visible, 2}, {"Actions de la brique", m_pOpts.visible, 1}, {"Groupes", m_pGroups.visible, 6},
                                    {"Réinitialiser la disposition", false, 5}};
    else {
        items = {{"Nouvelle scène", false, 0}, {"Ouvrir… (.v3d.json, .ldr, .mpd)", false, 1}, {"Enregistrer… (.v3d.json)", false, 2}, {"Ouvrir la maison test (GI)", false, 3},
                 {"Ouvrir la maison officielle (31153)", false, 5}};
        if (hasSteps()) items.push_back({m_reading ? "Fermer la notice" : "Lire la notice (étapes du modèle)", false, 4});
    }
    const bool checks = m_popup == 3;
    float w = 0;
    for (const Item& it : items) w = std::max(w, u.textW(12.5f, it.label));
    w += (checks ? 44 : 28) * s;
    w = std::max(w, (m_popup >= 3 ? 210.0f : 90.0f) * s);
    ImVec2 d = ImGui::GetIO().DisplaySize;
    ImVec2 p0(std::min(m_popupAt.x, d.x - w - 10 * s), m_popupAt.y), p1(p0.x + w, p0.y + 12 * s + items.size() * 32 * s);
    u.dl->AddRectFilled(p0, p1, C_PANEL_SOLID, 10 * s);
    u.dl->AddRect(p0, p1, C_LINE, 10 * s);
    m_uiRects.push_back(ImVec4(p0.x, p0.y, p1.x, p1.y));
    float y = p0.y + 6 * s;
    bool clickedItem = false;
    for (const Item& it : items) {
        ImVec2 a(p0.x + 6 * s, y), b(p1.x - 6 * s, y + 30 * s);
        bool h = u.hover(a, b);
        if (h) u.dl->AddRectFilled(a, b, C_CARD_HI, 7 * s);
        float tx = a.x + 10 * s;
        if (checks && it.id != 5) {
            ImVec2 ca(tx, a.y + 8 * s), cb(tx + 14 * s, a.y + 22 * s);
            if (it.on) {
                u.dl->AddRectFilled(ca, cb, C_ACCENT, 4 * s);
                u.dl->AddPolyline(std::vector<ImVec2>{ImVec2(ca.x + 3.5f * s, ca.y + 7 * s), ImVec2(ca.x + 6 * s, ca.y + 10 * s), ImVec2(ca.x + 11 * s, ca.y + 4 * s)}.data(), 3, C_INK, 0, 2 * s);
            } else u.dl->AddRect(ca, cb, C_FAINT, 4 * s, 0, 1.5f * s);
            tx += 24 * s;
        }
        u.text(ImVec2(tx, a.y + 7 * s), 12.5f, (it.on && !checks) ? C_ACCENT : C_TEXT, it.label);
        if (h && u.pressed) {
            clickedItem = true;
            if (m_popup == 1) {
                if (it.id == 3) { setRC(true); applyLook(); }
                else if (it.id == 2) { setRC(false); m_rs.raster = false; applyLook(); }
                else { setRC(false); m_quality = it.id; m_rs.msaaSamples = m_quality ? 4 : 1; if (!m_rs.raster) { m_rs.raster = true; applyLook(); } }
            }
            else if (m_popup == 2) { m_rs.tonemapper = it.id; m_renderer.resetAccumulation(); }
            else if (m_popup == 3) {
                PanelState* ps[5] = {&m_pTray, &m_pOpts, &m_pColors, &m_pHud, &m_pManual};
                if (it.id < 4) ps[it.id]->visible = !ps[it.id]->visible;
                else if (it.id == 4) { m_pManual.visible = !m_pManual.visible; if (m_pManual.visible) m_pManual.collapsed = false; }
                else if (it.id == 6) m_pGroups.visible = !m_pGroups.visible;
                else { m_pTray = m_pOpts = m_pColors = PanelState(); m_pHud = m_pManual = PanelState{false, false}; m_optsRight = false; toast("Disposition réinitialisée"); }
            } else {
                if (it.id == 0) { pushUndo(); m_bricks.clear(); m_selected = -1; m_multi.clear(); rebuildScene(); toast("Nouvelle scène"); }
                else if (it.id == 1) openDialog();
                else if (it.id == 3) openTestHouse();
                else if (it.id == 4) { if (m_reading) stopReading(); else startReading(1); }
                else if (it.id == 5) openOfficialHouse();
                else saveDialog();
            }
        }
        y += 32 * s;
    }
    if (u.pressed && !clickedItem && !u.hover(p0, p1) && u.mouse.y > 30 * s) m_popup = 0;
    if (clickedItem && m_popup != 3) m_popup = 0;
}

// « Scène partagée PC · Mac · iPad » (#sheet): the .v3d.json text, copy, load, open, exports, new scene.
void BrickApp::uiSceneSheet() {
    if (!m_sheetOpen) return;
    const float s = m_ui;
    ImVec2 d = ImGui::GetIO().DisplaySize;
    ImGui::GetForegroundDrawList();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(d);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.5f));
    ImGui::Begin("##sheetbg", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
    ImGui::End();
    ImGui::PopStyleColor();
    const float W = std::min(640 * s, d.x - 32 * s);
    ImGui::SetNextWindowPos(ImVec2((d.x - W) * 0.5f, d.y * 0.5f - 190 * s));
    ImGui::SetNextWindowSize(ImVec2(W, 0));
    ImGui::SetNextWindowFocus();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18 * s, 18 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14 * s);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8 * s, 12 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7 * s);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10 * s, 6 * s));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(C_PANEL_SOLID));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(C_LINE));
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(C_CARD));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(C_CARD_HI));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0x0f / 255.0f, 0x10 / 255.0f, 0x12 / 255.0f, 1));
    ImGui::Begin("##sheet", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
    if (m_gui.fontBold) ImGui::PushFont(m_gui.fontBold);
    ImGui::TextUnformatted("Scène partagée PC · Mac · iPad");
    if (m_gui.fontBold) ImGui::PopFont();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(C_DIM));
    ImGui::TextWrapped("Même format que l'application bureau (.v3d.json). Copie ce texte dans un fichier pour l'ouvrir sur PC (Ctrl+O) et lancer le rendu path tracing, ou colle ici une scène exportée du PC.");
    ImGui::PopStyleColor();
    static std::vector<char> buf;
    if (buf.size() < m_sheetText.size() + 65536) buf.assign(m_sheetText.size() + 65536, 0);
    if (ImGui::IsWindowAppearing() || std::strncmp(buf.data(), m_sheetText.c_str(), buf.size()) != 0) {
        std::memcpy(buf.data(), m_sheetText.c_str(), m_sheetText.size() + 1);
    }
    if (ImGui::InputTextMultiline("##scenetext", buf.data(), buf.size(), ImVec2(-1, 180 * s))) m_sheetText = buf.data();
    if (ImGui::Button("Copier")) { ImGui::SetClipboardText(m_sheetText.c_str()); toast("Scène copiée dans le presse-papiers"); }
    ImGui::SameLine();
    if (ImGui::Button("Charger ce texte")) {
        std::string msg;
        std::vector<Brick> before = m_bricks;
        if (m_build.fromJson(m_sheetText, &m_scene.camera, &msg)) { m_undo.push_back(before); m_selected = -1; m_multi.clear(); rebuildScene(); m_sheetOpen = false; }
        toast(msg);
    }
    ImGui::SameLine();
    if (ImGui::Button("Ouvrir un fichier…")) { openDialog(); m_sheetText = m_build.toJson(m_scene.camera); }
    ImGui::SameLine();
    float right = 0;
    const char* rb[4] = {"Exporter .ldr", "Exporter vers Maya", "Nouvelle scène", "Fermer"};
    for (const char* t : rb) right += ImGui::CalcTextSize(t).x + 20 * s + 8 * s;
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - right + 8 * s));
    if (ImGui::Button("Exporter .ldr")) {
        m_sheetText = sceneLdr();
        wchar_t file[MAX_PATH] = L"brickit_scene.ldr";
        OPENFILENAMEW ofn = {sizeof(ofn)};
        ofn.hwndOwner = m_hwnd;
        ofn.lpstrFilter = L"LDraw (*.ldr)\0*.ldr\0";
        ofn.lpstrFile = file;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrDefExt = L"ldr";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        if (GetSaveFileNameW(&ofn)) {
            FILE* f = nullptr;
            if (_wfopen_s(&f, file, L"wb") == 0 && f) { fwrite(m_sheetText.data(), 1, m_sheetText.size(), f); fclose(f); }
        }
        char t[96];
        snprintf(t, sizeof(t), "Scène exportée en .ldr (%d pièces)", (int)m_bricks.size());
        toast(t);
    }
    ImGui::SameLine();
    if (ImGui::Button("Exporter vers Maya")) mayaExportScene();
    ImGui::SameLine();
    if (ImGui::Button("Nouvelle scène")) { pushUndo(); m_bricks.clear(); m_selected = -1; m_multi.clear(); rebuildScene(); m_sheetText = m_build.toJson(m_scene.camera); toast("Nouvelle scène"); }
    ImGui::SameLine();
    if (ImGui::Button("Fermer") || ImGui::IsKeyPressed(ImGuiKey_Escape)) m_sheetOpen = false;
    ImGui::End();
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(5);
    m_overUi = true;
}

void BrickApp::drawToast() {
    // .toast: top 8 px of the view, round, dark, thin yellow border
    if (m_toast.empty() || ImGui::GetTime() - m_toastTime > 3.2) return;
    const float s = m_ui;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* f = ImGui::GetFont();
    ImVec2 ts = f->CalcTextSizeA(12 * s, 1e9f, 0, m_toast.c_str());
    ImVec2 d = ImGui::GetIO().DisplaySize;
    float a = (float)std::clamp(3.2 - (ImGui::GetTime() - m_toastTime), 0.0, 0.4) / 0.4f;
    ImVec2 p0(d.x * 0.5f - ts.x * 0.5f - 12 * s, 30 * s + 8 * s), p1(d.x * 0.5f + ts.x * 0.5f + 12 * s, p0.y + ts.y + 10 * s);
    dl->AddRectFilled(p0, p1, alphaOf(IM_COL32(15, 16, 19, 224), a), 18 * s);
    dl->AddRect(p0, p1, alphaOf(IM_COL32(247, 197, 30, 115), a), 18 * s);
    dl->AddText(f, 12 * s, ImVec2(p0.x + 12 * s, p0.y + 5 * s), alphaOf(C_TEXT, a), m_toast.c_str());
}

// ------------------------------------------------------------------ frame of the interface

// ------------------------------------------------------------------ groups (outliner)
// The tree of the groups: a row per group (unfold, name, number of pieces, shown / hidden, isolated), a click selects
// the group, a double click on its name renames it. Affichage › Groupes, or the panel opens on the first Ctrl+G.
void BrickApp::uiGroups() {
    if (!m_pGroups.visible) return;
    Ui u = ui(m_ui);
    if (m_gui.fontBold) u.bold = m_gui.fontBold;
    const float s = m_ui;
    const int G = (int)m_build.groups.size();
    // parent of each group and its number of pieces, from the paths of the bricks
    std::vector<int> parent(G, -2), count(G, 0);
    int loose = 0;
    for (const Brick& b : m_bricks) {
        if (b.path.empty()) ++loose;
        for (size_t k = 0; k < b.path.size(); ++k) {
            const int g = b.path[k];
            if (g < 0 || g >= G) continue;
            ++count[g];
            if (parent[g] == -2) parent[g] = k ? b.path[k - 1] : -1;
        }
    }
    struct Row { int g, depth; };
    std::vector<Row> rows;
    std::function<void(int, int)> add = [&](int par, int depth) {
        for (int g = 0; g < G; ++g) {
            if (parent[g] != par || count[g] == 0) continue;
            rows.push_back({g, depth});
            if (std::find(m_groupsOpen.begin(), m_groupsOpen.end(), g) != m_groupsOpen.end()) add(g, depth + 1);
        }
    };
    add(-1, 0);
    const float W = 270 * s, pad = 10 * s, rowH = 24 * s;
    const int shown = std::min((int)rows.size(), 30);
    const float H = pad + 20 * s + 6 * s + (shown ? shown * rowH : 18 * s) + 6 * s + 16 * s + pad;
    ImVec2 a(10 * s, (m_pHud.visible ? 86 : 44) * s), b(a.x + W, a.y + H);
    u.dl->AddRectFilled(a, b, C_PANEL_SOLID, 10 * s);
    u.dl->AddRect(a, b, C_LINE, 10 * s);
    m_uiRects.push_back(ImVec4(a.x, a.y, b.x, b.y));
    u.text(ImVec2(a.x + pad, a.y + pad - 2 * s), 13, C_TEXT, "Groupes", true);
    if (m_isolateGroup >= 0) {   // "Tout afficher" while a group is isolated
        const char* lab = "Tout afficher";
        const float tw = u.textW(11, lab);
        ImVec2 ba(b.x - pad - tw - 12 * s, a.y + pad - 3 * s), bb(b.x - pad, a.y + pad + 15 * s);
        const bool h = u.hover(ba, bb);
        u.dl->AddRectFilled(ba, bb, h ? C_CARD_HI : C_CARD, 6 * s);
        u.text(ImVec2(ba.x + 6 * s, ba.y + 2 * s), 11, C_TEXT, lab);
        if (h && u.pressed) { m_isolateGroup = -1; syncObjects(); }
    }
    float y = a.y + pad + 20 * s + 6 * s;
    if (rows.empty()) {
        u.text(ImVec2(a.x + pad, y), 11, C_DIM, "Aucun groupe : sélectionne des pièces puis Ctrl+G");
        y += 18 * s;
    }
    const int sel = unitGroup(m_selected);
    for (int r = 0; r < shown; ++r) {
        const Row& row = rows[r];
        const int g = row.g;
        Group& gr = m_build.groups[g];
        ImVec2 ra(a.x + 4 * s, y), rb(b.x - 4 * s, y + rowH - 2 * s);
        const bool h = u.hover(ra, rb);
        if (g == sel) u.dl->AddRectFilled(ra, rb, C_ACCENT, 6 * s);
        else if (h) u.dl->AddRectFilled(ra, rb, C_CARD_HI, 6 * s);
        float x = ra.x + 6 * s + row.depth * 14 * s;
        // unfold chevron (when it has sub-groups)
        bool hasKids = false;
        for (int k = 0; k < G; ++k) if (parent[k] == g && count[k] > 0) hasKids = true;
        const bool open = std::find(m_groupsOpen.begin(), m_groupsOpen.end(), g) != m_groupsOpen.end();
        ImVec2 ca(x, y + 4 * s), cb(x + 14 * s, y + rowH - 6 * s);
        if (hasKids) {
            const ImVec2 c((ca.x + cb.x) * 0.5f, (ca.y + cb.y) * 0.5f);
            const float k = 3.5f * s;
            if (open) u.dl->AddPolyline(std::vector<ImVec2>{ImVec2(c.x - k, c.y - k * 0.5f), ImVec2(c.x, c.y + k * 0.5f), ImVec2(c.x + k, c.y - k * 0.5f)}.data(), 3, C_DIM, 0, 1.8f * s);
            else u.dl->AddPolyline(std::vector<ImVec2>{ImVec2(c.x - k * 0.5f, c.y - k), ImVec2(c.x + k * 0.5f, c.y), ImVec2(c.x - k * 0.5f, c.y + k)}.data(), 3, C_DIM, 0, 1.8f * s);
        }
        const bool overChev = hasKids && u.hover(ca, cb);
        x += 18 * s;
        char nm[96];
        snprintf(nm, sizeof(nm), "%s", gr.name.c_str());
        u.text(ImVec2(x, y + 3 * s), 12, gr.hidden ? C_FAINT : (g == sel ? C_INK : C_TEXT), nm, g == sel);
        char cn[24];
        snprintf(cn, sizeof(cn), "%d", count[g]);
        // right: number of pieces, shown / hidden (eye), isolate
        const float bx2 = rb.x - 4 * s;
        ImVec2 ia(bx2 - 20 * s, y + 2 * s), ib(bx2, y + rowH - 4 * s);            // isolate
        ImVec2 ea(ia.x - 24 * s, y + 2 * s), eb(ia.x - 4 * s, y + rowH - 4 * s);   // eye
        u.text(ImVec2(ea.x - 6 * s - u.textW(11, cn), y + 4 * s), 11, g == sel ? C_INK : C_DIM, cn);
        const bool he = u.hover(ea, eb), hi = u.hover(ia, ib);
        {   // eye: an almond with its pupil, struck through when hidden
            const ImVec2 c((ea.x + eb.x) * 0.5f, (ea.y + eb.y) * 0.5f);
            const ImU32 col = he ? C_TEXT : (g == sel ? C_INK : C_DIM);
            u.dl->AddEllipse(c, ImVec2(7 * s, 4 * s), col, 0, 0, 1.4f * s);
            if (!gr.hidden) u.dl->AddCircleFilled(c, 2.2f * s, col);
            else u.dl->AddLine(ImVec2(c.x - 7 * s, c.y + 5 * s), ImVec2(c.x + 7 * s, c.y - 5 * s), col, 1.4f * s);
        }
        {   // isolate: a target, filled when this group is the one isolated
            const ImVec2 c((ia.x + ib.x) * 0.5f, (ia.y + ib.y) * 0.5f);
            const ImU32 col = m_isolateGroup == g ? C_ACCENT : (hi ? C_TEXT : (g == sel ? C_INK : C_DIM));
            u.dl->AddCircle(c, 5.5f * s, col, 0, 1.4f * s);
            if (m_isolateGroup == g) u.dl->AddCircleFilled(c, 2.5f * s, col);
        }
        if (h && u.pressed) {
            if (he) { gr.hidden = !gr.hidden; syncObjects(); autosave(); }
            else if (hi) { m_isolateGroup = m_isolateGroup == g ? -1 : g; syncObjects(); }
            else if (overChev) {
                auto it = std::find(m_groupsOpen.begin(), m_groupsOpen.end(), g);
                if (it != m_groupsOpen.end()) m_groupsOpen.erase(it); else m_groupsOpen.push_back(g);
            } else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                m_renameGroup = g;
                snprintf(m_renameBuf, sizeof(m_renameBuf), "%s", gr.name.c_str());
            } else {   // select the group: the level of its parent is entered
                std::vector<int> m = groupMembers(g);
                if (!m.empty()) {
                    const std::vector<int>& p = m_bricks[m[0]].path;
                    const auto at = std::find(p.begin(), p.end(), g);
                    m_groupCtx.assign(p.begin(), at);
                    m_selected = m[0];
                    m_multi.assign(m.begin() + 1, m.end());
                    syncObjects();
                }
            }
        }
        if (m_renameGroup == g) {   // rename: a text field over the row, Entrée to keep, Échap to cancel
            ImGui::SetNextWindowPos(ImVec2(x - 4 * s, y));
            ImGui::SetNextWindowSize(ImVec2(rb.x - x - 70 * s, rowH));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2 * s, 1 * s));
            ImGui::Begin("##renamegroup", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            if (ImGui::InputText("##name", m_renameBuf, sizeof(m_renameBuf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
                if (m_renameBuf[0]) gr.name = m_renameBuf;
                m_renameGroup = -1;
                autosave();
            } else if (ImGui::IsKeyPressed(ImGuiKey_Escape) || (!ImGui::IsWindowFocused() && !ImGui::IsWindowAppearing())) m_renameGroup = -1;
            ImGui::End();
            ImGui::PopStyleVar();
        }
        y += rowH;
    }
    char lb[96];
    snprintf(lb, sizeof(lb), "%d pièces hors groupe · Ctrl+G grouper · Ctrl+Maj+G dégrouper", loose);
    u.text(ImVec2(a.x + pad, b.y - pad - 13 * s), 9.5f, C_FAINT, lb);
}

// ------------------------------------------------------------------ reading the instructions of a model
// The page of the booklet over the 3D view: model, step n / N, the submodel built, previous / next, close; under it
// the parts of the step (thumbnail, colour, x count), as the boxes of the official booklets.
void BrickApp::uiReader() {
    if (!m_reading) return;
    Ui u = ui(m_ui);
    if (m_gui.fontBold) u.bold = m_gui.fontBold;
    const float s = m_ui;
    const ImVec2 d = ImGui::GetIO().DisplaySize;
    // the parts of the step, grouped by part and colour
    std::map<std::pair<int, int>, int> cnt;
    for (int i : readStepBricks()) ++cnt[{m_bricks[i].part, m_bricks[i].color}];
    std::vector<std::pair<std::pair<int, int>, int>> items(cnt.begin(), cnt.end());
    std::sort(items.begin(), items.end(), [](auto& a, auto& b) { return a.second > b.second; });
    const float cell = 58 * s, gap = 6 * s, pad = 12 * s, headH = 30 * s;
    const int maxCells = std::max(1, (int)((std::min(980 * s, d.x - 40 * s) - 2 * pad + gap) / (cell + gap)));
    const int shown = std::min((int)items.size(), maxCells);
    const float W = std::min(980 * s, d.x - 40 * s), H = pad + headH + gap + cell + 16 * s + pad;
    ImVec2 a((d.x - W) * 0.5f, 44 * s), b(a.x + W, a.y + H);
    u.dl->AddRectFilled(a, b, C_PANEL_SOLID, 12 * s);
    u.dl->AddRect(a, b, C_LINE, 12 * s);
    m_uiRects.push_back(ImVec4(a.x, a.y, b.x, b.y));
    float x = a.x + pad;
    const float y = a.y + pad;
    x += u.text(ImVec2(x, y + 4 * s), 14, C_TEXT, m_modelName.empty() ? "Notice" : m_modelName.c_str(), true).x + 14 * s;
    char st[64];
    snprintf(st, sizeof(st), "Étape %d / %d", m_readStep, m_readSteps);
    x += u.text(ImVec2(x, y + 4 * s), 14, C_ACCENT, st, true).x + 14 * s;
    const std::string& sub = m_readStep - 1 < (int)m_readTitles.size() ? m_readTitles[m_readStep - 1] : std::string();
    if (!sub.empty()) {
        const std::string t = "Sous-modèle : " + sub;
        u.text(ImVec2(x, y + 6 * s), 12, C_DIM, t.c_str());
    }
    // buttons: previous, next, close
    auto button = [&](float bx, const char* lab, bool enabled) {
        const float tw = u.textW(12, lab, true);
        ImVec2 p0(bx - tw - 22 * s, y), p1(bx, y + headH);
        const bool h = enabled && u.hover(p0, p1);
        u.dl->AddRectFilled(p0, p1, h ? C_ACCENT : C_CARD, 8 * s);
        u.text(ImVec2(p0.x + 11 * s, y + (headH - u.fs(12)) * 0.5f), 12, h ? C_INK : (enabled ? C_TEXT : C_FAINT), lab, true);
        return std::pair<bool, float>(h && u.pressed, p0.x - 6 * s);
    };
    auto [close, x1] = button(b.x - pad, "Fermer", true);
    auto [next, x2] = button(x1, "Suivante  ›", m_readStep < m_readSteps);
    auto [prev, x3] = button(x2, "‹  Précédente", m_readStep > 1);
    (void)x3;
    if (close) stopReading();
    if (next) readStepTo(m_readStep + 1);
    if (prev) readStepTo(m_readStep - 1);
    // the parts of the step
    float cx = a.x + pad;
    const float cy = y + headH + gap;
    for (int k = 0; k < shown; ++k) {
        const int part = items[k].first.first, col = items[k].first.second;
        ImVec2 c0(cx, cy), c1(cx + cell, cy + cell);
        u.dl->AddRectFilled(c0, c1, C_CARD, 8 * s);
        ImTextureID tex;
        ImVec2 uv0, uv1;
        if (thumbUv(part, tex, uv0, uv1)) u.dl->AddImage(tex, ImVec2(c0.x + 3 * s, c0.y + 3 * s), ImVec2(c1.x - 3 * s, c1.y - 3 * s), uv0, uv1);
        const Color& cc = m_lib.palette[std::clamp(col, 0, (int)m_lib.palette.size() - 1)];
        u.dl->AddCircleFilled(ImVec2(c0.x + 9 * s, c1.y - 9 * s), 5 * s, IM_COL32((int)(cc.srgb.x * 255), (int)(cc.srgb.y * 255), (int)(cc.srgb.z * 255), 255));
        u.dl->AddCircle(ImVec2(c0.x + 9 * s, c1.y - 9 * s), 5 * s, IM_COL32(255, 255, 255, 60), 0, 1 * s);
        char nb[16];
        snprintf(nb, sizeof(nb), "%d×", items[k].second);
        u.text(ImVec2(c0.x + 4 * s, c1.y + 1 * s), 10.5f, C_TEXT, nb, true);
        if (u.hover(c0, c1)) ImGui::SetTooltip("%s · %s", m_lib.parts[part].label.c_str(), cc.name.c_str());
        cx += cell + gap;
    }
    if ((int)items.size() > shown) {
        char more[32];
        snprintf(more, sizeof(more), "+ %d", (int)items.size() - shown);
        u.text(ImVec2(cx, cy + cell * 0.4f), 12, C_DIM, more);
    }
}

void BrickApp::drawUi() {
    m_lazyThumbBudget = 12;   // thumbnails of the full library decoded this frame at most
    ImVec2 d = ImGui::GetIO().DisplaySize;
    m_uiRects.clear();
    // one window covering the screen, behind every ImGui window, holding the draw list of the panels
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(d);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##webui", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    uiHeader();      // m_uiRects[0]
    uiTray();        // m_uiRects[1]
    uiRadial();
    uiSelHud();
    uiColors();
    uiMechPanel();
    uiHud();
    uiGroups();
    uiReader();
    uiPopups();
    uiManual();
    uiDevPanel();
    uiInfoSheet();
    uiDevReview();
    uiManualBig();
    drawDropzone();
    ImGui::End();
    ImGui::PopStyleVar(2);
    uiSceneSheet();
    drawToast();
    const ImVec2 mp = ImGui::GetIO().MousePos;
    m_overUi = m_libPress >= 0 || ImGui::GetIO().WantCaptureMouse;
    for (const ImVec4& r : m_uiRects)
        if (mp.x >= r.x && mp.y >= r.y && mp.x < r.z && mp.y < r.w) m_overUi = true;
}

// --------------------------------------------------------------------------- DEV mode (web: devRenderPanel, showInfo
// in DEV, devInfoActions, devReview)

static const ImU32 C_DEVLINE = IM_COL32(0x2b, 0x4b, 0x55, 255);
static const ImU32 C_DEVINK = IM_COL32(0x0d, 0x1a, 0x1f, 255);
static const char* kConnFr[] = {"Tenons", "Anti-tenons", "Pins", "Trous de pin", "Axe", "Trous d'axe"};   // CONN_FR
static const char* kDevTypeFr[] = {"Tenon", "Anti-tenon", "Pin", "Trou de pin", "Axe", "Trou d'axe"};
static const ImU32 kDevCol[] = {IM_COL32(0xf7, 0xc5, 0x1e, 255), IM_COL32(0xff, 0x8a, 0x3d, 255), IM_COL32(0x7f, 0xe3, 0xff, 255),
                                IM_COL32(0x4a, 0xa3, 0xff, 255), IM_COL32(0x5f, 0xd0, 0x68, 255), IM_COL32(0xc7, 0x7d, 0xff, 255)};
// legend order of the web (stud, antistud, pinhole, pin, axle, axlehole) -> Conn
static const int kDevLegend[] = {CONN_STUD, CONN_ANTISTUD, CONN_PINHOLE, CONN_PIN, CONN_AXLE, CONN_AXLEHOLE};

// toLocaleString("fr-FR") of an integer: narrow no-break space between thousands
static std::string frInt(long long v) {
    std::string d = std::to_string(v < 0 ? -v : v), o;
    for (size_t i = 0; i < d.size(); ++i) {
        if (i && (d.size() - i) % 3 == 0) o += "\xE2\x80\xAF";
        o.push_back(d[i]);
    }
    return (v < 0 ? "-" : "") + o;
}
// fmt: (Math.round(x * 100) / 100).toLocaleString("fr-FR")
static std::string frNum(double x) {
    double r = std::round(x * 100) / 100;
    char b[48];
    snprintf(b, sizeof(b), "%.2f", std::fabs(r));
    std::string s = b;
    while (s.back() == '0') s.pop_back();
    if (s.back() == '.') s.pop_back();
    for (char& c : s) if (c == '.') c = ',';
    return (r < 0 ? "-" : "") + s;
}
static float wrapText(Ui& u, ImVec2 p, float px, ImU32 col, const char* t, float wrap, bool draw = true) {
    ImFont* f = u.font;
    if (draw) u.dl->AddText(f, u.fs(px), p, col, t, nullptr, wrap);
    return f->CalcTextSizeA(u.fs(px), 1e9f, wrap, t).y;
}
// .btn of the web (compact: 28 px, 12 px text, padding 10, radius 7); sm: 28 px, 11 px, padding 8
static bool webButton(Ui& u, ImVec2 a, const char* label, float& w, bool pressed = false, ImU32 onBg = C_ACCENT, ImU32 onInk = C_INK,
                      bool disabled = false, float h = 28, float px = 12, float pad = 10) {
    const float s = u.s;
    w = u.textW(px, label, pressed) + 2 * pad * s;
    ImVec2 b(a.x + w, a.y + h * s);
    const bool hv = !disabled && u.hover(a, b);
    const float al = disabled ? 0.4f : 1.0f;
    u.dl->AddRectFilled(a, b, alphaOf(pressed ? onBg : (hv ? C_CARD_HI : C_CARD), al), 7 * s);
    u.text(ImVec2(a.x + pad * s, a.y + (h * s - u.fs(px)) * 0.5f), px, alphaOf(pressed ? onInk : C_TEXT, al), label, pressed);
    return hv && u.pressed;
}

void BrickApp::uiDevPanel() {
    if (!m_dev || m_mechOn) return;
    Ui u = ui(m_ui);
    if (m_gui.fontBold) u.bold = m_gui.fontBold;
    const float s = m_ui;
    const float W = std::min(340 * s, (m_stage.z - m_stage.x) - 32 * s), pad = 12 * s, gap = 10 * s;
    const float x0 = m_stage.x + 16 * s, inner = W - 2 * pad;
    // layout first (heights), then drawing from the bottom (bottom: 12 px)
    const char* store = "stocké sur cet appareil";
    std::vector<std::string> ids;
    for (auto& [id, o] : m_devOverrides) ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    const float headH = 18 * s, toolsH = 30 * s, legendH = 2 * 16 * s, itemH = 44 * s;
    const char* empty = "Aucun remplacement. Appui long sur une pièce de la bibliothèque, puis « Exporter pour Maya ».";
    const float listH = ids.empty() ? wrapText(u, ImVec2(0, 0), 12, C_DIM, empty, inner, false) * 1.15f : ids.size() * itemH + (ids.size() - 1) * 6 * s;
    int ok = 0, stale = 0, missing = 0;
    for (auto& id : ids) {
        int p = devPartById(id);
        if (p < 0) ++missing;
        else if (!m_devOverrides[id].baseRelease.empty() && !m_lib.parts[p].release.empty() && m_devOverrides[id].baseRelease != m_lib.parts[p].release) ++stale;
        else ++ok;
    }
    std::string sum;
    if (!ids.empty()) {
        sum = std::to_string(ids.size()) + " remplacement" + (ids.size() > 1 ? "s" : "") + " · " + std::to_string(ok) + " à jour";
        if (stale) sum += " · " + std::to_string(stale) + " à refaire (LDraw modifié)";
        if (missing) sum += " · " + std::to_string(missing) + " pièce" + (missing > 1 ? "s" : "") + " absente" + (missing > 1 ? "s" : "");
    }
    const float sumH = sum.empty() ? 0 : wrapText(u, ImVec2(0, 0), 12, C_DIM, sum.c_str(), inner, false);
    const float H = pad + headH + gap + toolsH + gap + legendH + gap + listH + gap + (sum.empty() ? 0 : sumH + gap) + 28 * s + pad;
    ImVec2 p0(x0, m_stage.w - 12 * s - H), p1(x0 + W, m_stage.w - 12 * s);
    u.dl->AddRectFilled(ImVec2(p0.x, p0.y + 8 * s), ImVec2(p1.x, p1.y + 8 * s), IM_COL32(0, 0, 0, 40), 12 * s);   // shadow
    u.dl->AddRectFilled(p0, p1, C_PANEL, 12 * s);
    u.dl->AddRect(p0, p1, C_DEVLINE, 12 * s);
    m_uiRects.push_back(ImVec4(p0.x, p0.y, p1.x, p1.y));
    float y = p0.y + pad, x = p0.x + pad;
    u.text(ImVec2(x, y), 13, C_DEV, "Mode développeur", true);
    u.text(ImVec2(p1.x - pad - u.textW(12, store), y + 1 * s), 12, C_FAINT, store);
    y += headH + gap;
    // toggles
    struct Tog { const char* label; bool* v; };
    Tog togs[] = {{"Connecteurs", &m_devShowVox}, {"Contour LDraw", &m_devShowEdges}, {"Remplacements", &m_devUseOverrides}};
    float tx = x;
    for (int i = 0; i < 3; ++i) {
        float w;
        if (webButton(u, ImVec2(tx, y), togs[i].label, w, *togs[i].v, C_DEV, C_DEVINK, false, 30)) {
            *togs[i].v = !*togs[i].v;
            savePrefs();
            if (i == 2) devApplyAll();
        }
        tx += w + 6 * s;
    }
    y += toolsH + gap;
    // legend (wraps on two lines in 340 px)
    float lx = x, ly = y;
    for (int k : kDevLegend) {
        const float w = 12 * s + u.textW(12, kDevTypeFr[k]);
        if (lx + w > x + inner) { lx = x; ly += 16 * s; }
        u.dl->AddCircleFilled(ImVec2(lx + 4 * s, ly + 8 * s), 4 * s, kDevCol[k], 12);
        u.text(ImVec2(lx + 12 * s, ly), 12, C_DIM, kDevTypeFr[k]);
        lx += w + 10 * s;
    }
    y += legendH + gap;
    // list
    if (ids.empty()) wrapText(u, ImVec2(x, y), 12, C_DIM, empty, inner);
    std::string removeId;
    for (const std::string& id : ids) {
        const DevOverride& o = m_devOverrides[id];
        const int part = devPartById(id);
        ImVec2 a(x, y), b(x + inner, y + itemH);
        u.dl->AddRectFilled(a, b, C_CARD, 8 * s);
        float bw1 = u.textW(11, "Retirer") + 16 * s, bw2 = u.textW(11, "Exporter") + 16 * s;
        float bx = b.x - 8 * s - bw1;
        float w;
        if (webButton(u, ImVec2(bx, y + (itemH - 28 * s) * 0.5f), "Retirer", w, false, C_ACCENT, C_INK, false, 28, 11, 8)) removeId = id;
        bx -= 6 * s + bw2;
        if (webButton(u, ImVec2(bx, y + (itemH - 28 * s) * 0.5f), "Exporter", w, false, C_ACCENT, C_INK, part < 0, 28, 11, 8) && part >= 0) devExport(part);
        const bool isStale = part >= 0 && !o.baseRelease.empty() && !m_lib.parts[part].release.empty() && o.baseRelease != m_lib.parts[part].release;
        const char* chip = part < 0 ? "absente" : isStale ? "LDraw modifié" : "à jour";
        const ImU32 cb = part < 0 ? IM_COL32(0x4a, 0x1c, 0x1e, 255) : isStale ? IM_COL32(0x4a, 0x38, 0x12, 255) : IM_COL32(0x1f, 0x3b, 0x2a, 255);
        const ImU32 cf = part < 0 ? IM_COL32(0xff, 0x9a, 0x9d, 255) : isStale ? IM_COL32(0xff, 0xd2, 0x7a, 255) : IM_COL32(0x7e, 0xe2, 0xa0, 255);
        const float cw = u.textW(11, chip) + 14 * s;
        ImVec2 ca(bx - 6 * s - cw, y + itemH * 0.5f - 9 * s), cbb(bx - 6 * s, y + itemH * 0.5f + 9 * s);
        u.dl->AddRectFilled(ca, cbb, cb, 9 * s);
        u.text(ImVec2(ca.x + 7 * s, ca.y + 2 * s), 11, cf, chip);
        if (u.hover(ca, cbb)) ImGui::SetTooltip("%s · base %s", part < 0 ? "Pièce absente de la bibliothèque" : isStale ? ("LDraw mis à jour : " + m_lib.parts[part].release).c_str() : "À jour", o.baseRelease.empty() ? "?" : o.baseRelease.c_str());
        u.dl->PushClipRect(a, ImVec2(ca.x - 6 * s, b.y), true);
        u.text(ImVec2(x + 8 * s, y + 6 * s), 12, C_TEXT, id.c_str(), true);
        std::string sub = o.label + " · " + frInt(o.triangles) + " tri";
        u.text(ImVec2(x + 8 * s, y + 23 * s), 12, C_DIM, sub.c_str());
        u.dl->PopClipRect();
        y += itemH + 6 * s;
    }
    if (!ids.empty()) y -= 6 * s;
    y = (ids.empty() ? y + listH : y) + gap;
    if (!sum.empty()) { wrapText(u, ImVec2(x, y), 12, C_DIM, sum.c_str(), inner); y += sumH + gap; }
    float w;
    if (webButton(u, ImVec2(x, y), "Importer un maillage… (.obj / .fbx)", w)) devImport(-1);
    if (!removeId.empty()) {
        std::wstring q = L"Retirer le remplacement de " + std::wstring(removeId.begin(), removeId.end()) + L" et revenir à LDraw ?";
        if (MessageBoxW(m_hwnd, q.c_str(), L"BrickIt Builder", MB_OKCANCEL | MB_ICONQUESTION) == IDOK) {
            const int part = devPartById(removeId);
            if (part >= 0) devRevert(part);
            else { devDelete(removeId); m_devOverrides.erase(removeId); }
        }
    }
}

// Full part sheet of the DEV mode (web: showInfo + devInfoActions). Box min(640 px), padding 18, gap 12.
void BrickApp::uiDevSheet() {
    Ui u = ui(m_ui);
    if (m_gui.fontBold) u.bold = m_gui.fontBold;
    const float s = m_ui;
    ImVec2 d = ImGui::GetIO().DisplaySize;
    u.dl->AddRectFilled(ImVec2(0, 0), d, IM_COL32(0, 0, 0, 128));
    m_uiRects.push_back(ImVec4(0, 0, d.x, d.y));
    const int part = m_infoPart;
    const Part& p = m_lib.parts[part];
    const std::string id = devId(part);
    const float W = std::min(640 * s, d.x - 32 * s), pad = 18 * s, gap = 12 * s, inner = W - 2 * pad;
    // rows of the dl
    std::vector<std::pair<std::string, std::string>> rows;
    auto row = [&](const char* k, const std::string& v) { if (!v.empty()) rows.push_back({k, v}); };
    row("Famille", p.family);
    row("Référence LDraw", p.ldraw.empty() ? "" : p.ldraw + ".dat");
    row("Catégorie LDraw", p.category);
    if (p.hasSizeStuds) row("Encombrement", frNum(p.sizeStuds[0]) + " × " + frNum(p.sizeStuds[1]) + " tenons · " + std::to_string(p.heightPlates) + " plate" + (p.heightPlates > 1 ? "s" : "") + " de haut");
    if (p.hasSizeCm) row("Dimensions", frNum(p.sizeCm[0]) + " × " + frNum(p.sizeCm[2]) + " × " + frNum(p.sizeCm[1]) + " cm (L × P × H, tenons compris)");
    row("Points de connexion", std::to_string(p.voxels.size()) + " voxels");
    row("Maillage", frInt(p.triangles) + " triangles · " + frInt(p.vertices) + " sommets");
    row("Auteur LDraw", p.author);
    row("Licence", p.license);
    row("Version LDraw", p.release);
    if (p.history) row("Historique", std::to_string(p.history) + " révision" + (p.history > 1 ? "s" : ""));
    row("Mots-clés", p.keywords);
    if (p.hasOrigin) row("Origine LDraw", frNum(p.ldrawOrigin.x) + " · " + frNum(p.ldrawOrigin.y) + " · " + frNum(p.ldrawOrigin.z) + " cm (coin min.)");
    float dtW = 0;
    for (auto& r : rows) dtW = std::max(dtW, u.textW(13, r.first.c_str()));
    const float ddX = dtW + 14 * s, ddW = inner - ddX;
    float rowsH = 0;
    std::vector<float> rh;
    for (auto& r : rows) { float h = wrapText(u, ImVec2(0, 0), 13, C_TEXT, r.second.c_str(), ddW, false); rh.push_back(h); rowsH += h + 6 * s; }
    const char* credit = "Géométrie : bibliothèque LDraw (ldraw.org, CC BY). Connexions : LDraw + LDCad shadow library, converties automatiquement. Couleurs réelles : données Rebrickable (rebrickable.com).";
    const float creditH = wrapText(u, ImVec2(0, 0), 11, C_FAINT, credit, inner, false) * 1.1f;
    auto ov = m_devOverrides.find(id);
    std::string note;
    if (ov != m_devOverrides.end()) {
        const DevOverride& o = ov->second;
        const bool stale = !o.baseRelease.empty() && !p.release.empty() && o.baseRelease != p.release;
        note = "Mode DEV · remplacement actif (" + frInt(o.triangles) + " triangles, " + o.fileName + ") · base " + (o.baseRelease.empty() ? "?" : o.baseRelease) + " · " + (stale ? "LDraw mis à jour : " + p.release : std::string("À jour"));
    }
    const float noteH = note.empty() ? 0 : wrapText(u, ImVec2(0, 0), 11, C_FAINT, note.c_str(), inner, false) * 1.1f;
    const float headH = 192 * s;   // image 240 x 192 (aspect 5 / 4)
    const float H = pad + headH + gap + rowsH - 6 * s + gap + creditH + gap + (note.empty() ? 0 : noteH + gap) + 8 * s + 28 * s + gap + 28 * s + pad;
    ImVec2 p0((d.x - W) * 0.5f, std::max(8 * s, (d.y - H) * 0.5f)), p1(p0.x + W, p0.y + H);
    u.dl->AddRectFilled(p0, p1, C_PANEL_SOLID, 14 * s);
    u.dl->AddRect(p0, p1, C_LINE, 14 * s);
    float x = p0.x + pad, y = p0.y + pad;
    // head: image (radial gradient #34363d -> #1d1e22) and title + chips
    {
        ImVec2 ia(x, y), ib(x + 240 * s, y + headH);
        u.dl->AddRectFilled(ia, ib, IM_COL32(0x1d, 0x1e, 0x22, 255), 10 * s);
        const ImVec2 c((ia.x + ib.x) * 0.5f, ia.y + headH * 0.45f);
        for (int k = 12; k >= 1; --k) {
            const float t = k / 12.0f;
            const int r = (int)(0x34 + (0x1d - 0x34) * t), g = (int)(0x36 + (0x1e - 0x36) * t), b = (int)(0x3d + (0x22 - 0x3d) * t);
            u.dl->PushClipRect(ia, ib, true);
            u.dl->AddCircleFilled(c, 150 * s * t, IM_COL32(r, g, b, 255), 48);
            u.dl->PopClipRect();
        }
        ImTextureID tex;
        ImVec2 uv0, uv1;
        if (thumbUv(part, tex, uv0, uv1)) u.dl->AddImage(tex, ImVec2(c.x - headH * 0.5f, ia.y), ImVec2(c.x + headH * 0.5f, ib.y), uv0, uv1);
        const float rx = ib.x + 12 * s, rw = p1.x - pad - rx;
        float ry = y;
        ry += u.text(ImVec2(rx, ry), 17, C_TEXT, p.label.c_str(), true).y + 2 * s;   // h2
        std::string t2;
        for (char ch : p.title) { if (std::isspace((unsigned char)ch)) { if (t2.empty() || t2.back() != ' ') t2.push_back(' '); } else t2.push_back(ch); }
        ry += wrapText(u, ImVec2(rx, ry), 12, C_DIM, t2.c_str(), rw) + 10 * s;
        float cx = rx;
        for (auto& [k, n] : p.connectors) {
            int ci = -1;
            for (int q = 0; q < CONN_COUNT; ++q) if (k == std::string(q == 0 ? "stud" : q == 1 ? "antistud" : q == 2 ? "pin" : q == 3 ? "pinhole" : q == 4 ? "axle" : "axlehole")) ci = q;
            const std::string num = std::to_string(n), lab = ci >= 0 ? kConnFr[ci] : k;
            const float w = 16 * s + u.textW(12, num.c_str(), true) + 4 * s + u.textW(12, lab.c_str());
            if (cx + w > p1.x - pad) { cx = rx; ry += 26 * s; }
            u.dl->AddRectFilled(ImVec2(cx, ry), ImVec2(cx + w, ry + 22 * s), C_CARD, 11 * s);
            float tw = u.text(ImVec2(cx + 8 * s, ry + 3 * s), 12, C_ACCENT, num.c_str(), true).x;
            u.text(ImVec2(cx + 8 * s + tw + 4 * s, ry + 3 * s), 12, C_TEXT, lab.c_str());
            cx += w + 6 * s;
        }
    }
    y += headH + gap;
    for (size_t i = 0; i < rows.size(); ++i) {
        u.text(ImVec2(x, y), 13, C_DIM, rows[i].first.c_str());
        wrapText(u, ImVec2(x + ddX, y), 13, C_TEXT, rows[i].second.c_str(), ddW);
        y += rh[i] + 6 * s;
    }
    y += gap - 6 * s;
    wrapText(u, ImVec2(x, y), 11, C_FAINT, credit, inner);
    y += creditH + gap;
    if (!note.empty()) { wrapText(u, ImVec2(x, y), 11, C_FAINT, note.c_str(), inner); y += noteH + gap; }
    // DEV row (dashed top border)
    for (float dx = x; dx < p1.x - pad; dx += 6 * s) u.dl->AddLine(ImVec2(dx, y), ImVec2(std::min(dx + 3 * s, p1.x - pad), y), C_DEVLINE, 1);
    y += 8 * s;
    {
        const float tw = u.textW(11, "DEV", true) + 12 * s;
        u.dl->AddRectFilled(ImVec2(x, y + 5 * s), ImVec2(x + tw, y + 23 * s), C_DEV, 6 * s);
        u.text(ImVec2(x + 6 * s, y + 7 * s), 11, C_DEVINK, "DEV", true);
        float bx = x + tw + 8 * s, w;
        if (webButton(u, ImVec2(bx, y), "Exporter pour Maya", w)) devExport(part);
        bx += w + 8 * s;
        if (webButton(u, ImVec2(bx, y), "Importer le maillage modifié", w)) { m_infoPart = -1; devImport(part); return; }
        bx += w + 8 * s;
        if (ov != m_devOverrides.end() && webButton(u, ImVec2(bx, y), "Revenir à LDraw", w)) {
            devRevert(part);
            m_infoPart = -1;
            toast(id + " : géométrie LDraw rétablie");
            return;
        }
    }
    y += 28 * s + gap;
    // actions (right): Fermer, favourites, Ajouter à la scène
    const bool fav = std::find(m_fav.begin(), m_fav.end(), part) != m_fav.end();
    const char* fl = fav ? "★ Retirer des favoris" : "☆ Ajouter aux favoris";
    float wClose = u.textW(12, "Fermer") + 20 * s, wFav = u.textW(12, fl) + 20 * s, wAdd = u.textW(12, "Ajouter à la scène", true) + 20 * s, w;
    float bx = p1.x - pad - wAdd - 8 * s - wFav - 8 * s - wClose;
    const bool cClose = webButton(u, ImVec2(bx, y), "Fermer", w);
    bx += wClose + 8 * s;
    if (webButton(u, ImVec2(bx, y), fl, w)) {
        if (fav) m_fav.erase(std::find(m_fav.begin(), m_fav.end(), part));
        else m_fav.push_back(part);
        savePrefs();
    }
    bx += wFav + 8 * s;
    const bool cAdd = webButton(u, ImVec2(bx, y), "Ajouter à la scène", w, true);
    if (cClose || ImGui::IsKeyPressed(ImGuiKey_Escape) || (u.pressed && !u.hover(p0, p1))) m_infoPart = -1;
    if (cAdd) {
        m_infoPart = -1;
        spawn(part, (m_stage.x + m_stage.z) * 0.5f, (m_stage.y + m_stage.w) * 0.5f);
    }
}

// Review of a re-imported mesh (web: devReview): live preview in the scene, rows, Annuler / Recaler / Garder.
void BrickApp::uiDevReview() {
    if (!m_devReview.open) return;
    Ui u = ui(m_ui);
    if (m_gui.fontBold) u.bold = m_gui.fontBold;
    const float s = m_ui;
    ImVec2 d = ImGui::GetIO().DisplaySize;
    u.dl->AddRectFilled(ImVec2(0, 0), d, IM_COL32(0, 0, 0, 128));
    m_uiRects.push_back(ImVec4(0, 0, d.x, d.y));
    DevReview& R = m_devReview;
    const Part& p = m_lib.parts[R.part];
    const std::string id = devId(R.part);
    const float W = std::min(640 * s, d.x - 32 * s), pad = 18 * s, gap = 12 * s, inner = W - 2 * pad;
    const int tri = (int)(R.g.indices.size() / 3), ltri = (int)((p.hasLdrawMesh ? p.ldrawMesh : R.prev).indices.size() / 3);
    struct Row { std::string k, v; bool warn; };
    std::vector<Row> rows;
    rows.push_back({"Pièce", id + " · " + p.label, false});
    rows.push_back({"Fichier", R.fileName, false});
    rows.push_back({"Triangles", frInt(tri) + " (LDraw : " + frInt(ltri) + ")", false});
    rows.push_back({"Dimensions", frNum(R.size.x) + " × " + frNum(R.size.z) + " × " + frNum(R.size.y) + " cm (LDraw : " + frNum(R.ref.x) + " × " + frNum(R.ref.z) + " × " + frNum(R.ref.y) + ")", false});
    const float offLen = length(R.offset);
    rows.push_back({"Décalage du centre", offLen < 0.02f ? std::string("aligné (< 0,2 mm)") : frNum(R.offset.x) + " · " + frNum(R.offset.y) + " · " + frNum(R.offset.z) + " cm", offLen >= 0.08f});
    if (!R.note.empty()) rows.push_back({"Échelle", R.note, true});
    if (R.swapped) rows.push_back({"Axes", "Proportions différentes de LDraw : Z vers le haut dans Maya, ou pièce tournée ?", true});
    rows.push_back({"Connecteurs", std::to_string(p.voxels.size()) + " points, toujours calculés depuis LDraw", false});
    rows.push_back({"Base LDraw", p.release.empty() ? std::string("inconnue") : p.release, false});
    float dtW = 0;
    for (auto& r : rows) dtW = std::max(dtW, u.textW(13, r.k.c_str()));
    const float ddX = dtW + 14 * s, ddW = inner - ddX;
    std::vector<float> rh;
    float rowsH = 0;
    for (auto& r : rows) { float h = wrapText(u, ImVec2(0, 0), 13, C_TEXT, r.v.c_str(), ddW, false); rh.push_back(h); rowsH += h + 6 * s; }
    const char* intro = "Aperçu en direct dans la scène, avec les connecteurs LDraw et le contour LDraw par-dessus.";
    const float introH = wrapText(u, ImVec2(0, 0), 13, C_DIM, intro, inner, false) * 1.15f;
    const float H = pad + 22 * s + gap + introH + gap + rowsH - 6 * s + gap + 28 * s + pad;
    ImVec2 p0((d.x - W) * 0.5f, (d.y - H) * 0.5f), p1(p0.x + W, p0.y + H);
    u.dl->AddRectFilled(p0, p1, C_PANEL_SOLID, 14 * s);
    u.dl->AddRect(p0, p1, C_LINE, 14 * s);
    float x = p0.x + pad, y = p0.y + pad;
    u.text(ImVec2(x, y), 17, C_TEXT, ("Réimport de " + id).c_str(), true);
    y += 22 * s + gap;
    wrapText(u, ImVec2(x, y), 13, C_DIM, intro, inner);
    y += introH + gap;
    for (size_t i = 0; i < rows.size(); ++i) {
        u.text(ImVec2(x, y), 13, C_DIM, rows[i].k.c_str());
        wrapText(u, ImVec2(x + ddX, y), 13, rows[i].warn ? IM_COL32(0xff, 0xd2, 0x7a, 255) : C_TEXT, rows[i].v.c_str(), ddW);
        y += rh[i] + 6 * s;
    }
    y += gap - 6 * s;
    float w;
    if (webButton(u, ImVec2(x, y), "Annuler", w)) { devReviewCancel(); return; }
    const float wKeep = u.textW(12, "Garder ce remplacement", true) + 20 * s, wRe = u.textW(12, "Recaler sur le centre LDraw") + 20 * s;
    const bool keep = webButton(u, ImVec2(p1.x - pad - wKeep, y), "Garder ce remplacement", w, true);
    if (offLen >= 0.02f && webButton(u, ImVec2(p1.x - pad - wKeep - 8 * s - wRe, y), "Recaler sur le centre LDraw", w)) devReviewRecenter();
    if (keep) devReviewKeep();
}

// --------------------------------------------------------------------------- instruction manual (web: #manual, #mbig,
// #pdfbig): steps of the scene read vertically, imported PDF read vertically, resizable from the bottom left corner

static const ImU32 C_PAPER = IM_COL32(0xfb, 0xfa, 0xf6, 255), C_PAPER_INK = IM_COL32(0x22, 0x25, 0x2d, 255), C_PAPER_CANVAS = IM_COL32(0xf4, 0xf2, 0xea, 255);

// One page of the scene manual (.mp): step number, part to add (thumbnail + "1×"), step image (8 / 5), "p. n".
// big: the #mbig layout (padding 20 24 24, gap 12, number 44 px, thumbnail 48 x 38, 13 px, no page number).
float BrickApp::manualPage(Ui& u, ImVec2 a, float w, int n, int corner, bool big, bool draw) {
    const float s = u.s;
    const float padT = (big ? 20 : 8) * s, padX = (big ? 24 : 10) * s, padB = (big ? 24 : 10) * s, gap = (big ? 12 : 5) * s;
    const float numPx = big ? 44 : 26;
    const float inner = w - 2 * padX, ch = inner * 5 / 8;
    const float H = padT + u.fs(numPx) + gap + ch + padB;
    if (!draw) return H;
    const float r = 8 * s;
    ImDrawFlags fl = corner == 0 ? (ImDrawFlags_RoundCornersTopLeft | ImDrawFlags_RoundCornersBottomLeft) : (ImDrawFlags_RoundCornersTopRight | ImDrawFlags_RoundCornersBottomRight);
    u.dl->AddRectFilled(a, ImVec2(a.x + w, a.y + H), C_PAPER, big ? 0 : r, big ? 0 : fl);
    const bool valid = n >= 1 && n <= (int)m_bricks.size();
    float y = a.y + padT;
    if (valid) {
        const std::string sn = std::to_string(n);
        u.text(ImVec2(a.x + padX, y), numPx, C_PAPER_INK, sn.c_str(), true);
        // .mneed: thumbnail (34 x 26, #e9e6dc, radius 4) + "1×", at the right
        const float iw = (big ? 48 : 34) * s, ih = (big ? 38 : 26) * s, tp = big ? 13 : 11;
        const float tw = u.textW(tp, "1×", true);
        ImVec2 ib(a.x + w - padX - tw - 5 * s - iw, y + (u.fs(numPx) - ih) * 0.5f);
        u.dl->AddRectFilled(ib, ImVec2(ib.x + iw, ib.y + ih), IM_COL32(0xe9, 0xe6, 0xdc, 255), 4 * s);
        ImTextureID tex;
        ImVec2 uv0, uv1;
        const int part = m_bricks[n - 1].part;
        if (thumbUv(part, tex, uv0, uv1)) u.dl->AddImage(tex, ImVec2(ib.x + (iw - ih) * 0.5f, ib.y), ImVec2(ib.x + (iw + ih) * 0.5f, ib.y + ih), uv0, uv1);
        u.text(ImVec2(ib.x + iw + 5 * s, y + (u.fs(numPx) - u.fs(tp)) * 0.5f), tp, C_PAPER_INK, "1×", true);
        if (u.hover(ib, ImVec2(ib.x + iw + 5 * s + tw, ib.y + ih))) ImGui::SetTooltip("%s", m_lib.parts[part].label.c_str());
    }
    y += u.fs(numPx) + gap;
    ImVec2 ca(a.x + padX, y), cb(a.x + padX + inner, y + ch);
    u.dl->AddRectFilled(ca, cb, C_PAPER_CANVAS, 6 * s);
    if (valid) {
        ImTextureID t = stepImage(n);
        if (t) u.dl->AddImageRounded(t, ca, cb, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 6 * s);
    }
    u.dl->AddRect(ca, cb, C_PAPER_INK, 6 * s, 0, 1.5f * s);
    if (!big && n >= 1) {
        const std::string pn = "p. " + std::to_string(n);
        u.text(ImVec2(a.x + w - 8 * s - u.textW(9, pn.c_str()), a.y + H - 2 * s - u.fs(9)), 9, IM_COL32(0x7a, 0x7e, 0x88, 255), pn.c_str());
    }
    return H;
}

// Vertical PDF view (.pdfview): white pages one under the other, gap 10, rendered when they come near the view.
void BrickApp::pdfView(Ui& u, ImVec2 a, ImVec2 b, float& scroll, bool main) {
    const float s = u.s;
    const float w = b.x - a.x - 4 * s;
    if (m_pdfSizes.empty()) {
        const char* msg = m_pdfMsg.empty() ? "Ouverture du PDF…" : m_pdfMsg.c_str();
        u.text(ImVec2((a.x + b.x) * 0.5f - u.textW(12, msg) * 0.5f, a.y + 16 * s), 12, C_DIM, msg);
        return;
    }
    std::vector<float> top(m_pdfSizes.size());
    float y = 0;
    for (size_t i = 0; i < m_pdfSizes.size(); ++i) {
        top[i] = y;
        y += w * m_pdfSizes[i].second / std::max(1.0f, m_pdfSizes[i].first) + 10 * s;
    }
    const float content = y - 10 * s + 4 * s, viewH = b.y - a.y;
    // scroll requests (‹ ›, start)
    if (main && m_pdfScrollTo > 0) {
        const int n = std::clamp(m_pdfScrollTo, 1, (int)m_pdfSizes.size());
        m_pdfScrollGoal = top[n - 1];
        if (!m_pdfScrollSmooth) scroll = m_pdfScrollGoal;
        m_pdfScrollTo = 0;
        m_pdfLock = ImGui::GetTime() + 0.7;
    }
    if (!main && m_bigScrollTo > 0) {
        scroll = top[std::clamp(m_bigScrollTo, 1, (int)m_pdfSizes.size()) - 1];
        m_bigScrollTo = 0;
    }
    const bool over = u.hover(a, b);
    if (over && ImGui::GetIO().MouseWheel != 0) { scroll -= ImGui::GetIO().MouseWheel * 90 * s; if (main) m_pdfScrollGoal = -1; }
    if (main && m_pdfScrollGoal >= 0) {
        scroll += (m_pdfScrollGoal - scroll) * std::min(1.0f, ImGui::GetIO().DeltaTime * 12);   // behavior: "smooth"
        if (std::fabs(m_pdfScrollGoal - scroll) < 0.5f) { scroll = m_pdfScrollGoal; m_pdfScrollGoal = -1; }
    }
    scroll = std::clamp(scroll, 0.0f, std::max(0.0f, content - viewH));
    u.dl->PushClipRect(a, b, true);
    const int want = (int)std::round(w);
    for (size_t i = 0; i < m_pdfSizes.size(); ++i) {
        const float h = w * m_pdfSizes[i].second / std::max(1.0f, m_pdfSizes[i].first);
        ImVec2 pa(a.x + 2 * s, a.y + 2 * s + top[i] - scroll), pb(pa.x + w, pa.y + h);
        const bool inRange = pb.y > a.y - 600 * s && pa.y < b.y + 600 * s;   // rootMargin: 600px
        PdfTex& t = m_pdfTex[i];
        if (inRange && (!t.tex || std::abs(t.w - want) > 2) && !t.asked && m_pdf) { m_pdf->request((int)i, want); t.asked = true; }
        if (!inRange && t.tex && std::abs((int)i + 1 - m_pdfPage) > 6) { m_gui.releaseTexture(t.tex); t = PdfTex(); }
        if (pb.y < a.y || pa.y > b.y) continue;
        u.dl->AddRectFilled(ImVec2(pa.x, pa.y + 4 * s), ImVec2(pb.x, pb.y + 4 * s), IM_COL32(0, 0, 0, 60), 4 * s);
        u.dl->AddRectFilled(pa, pb, IM_COL32_WHITE, 4 * s);
        if (t.tex) u.dl->AddImageRounded(t.tex, pa, pb, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 4 * s);
        const std::string lab = std::to_string(i + 1) + " / " + std::to_string(m_pdfSizes.size());
        const float lw = u.textW(10, lab.c_str()) + 10 * s;
        ImVec2 la(pb.x - 6 * s - lw, pb.y - 4 * s - u.fs(10) - 2 * s);
        u.dl->AddRectFilled(la, ImVec2(la.x + lw, pb.y - 4 * s), IM_COL32(255, 255, 255, 204), 6 * s);
        u.text(ImVec2(la.x + 5 * s, la.y + 1 * s), 10, IM_COL32(0x55, 0x55, 0x55, 255), lab.c_str());
    }
    u.dl->PopClipRect();
    if (main && ImGui::GetTime() >= m_pdfLock) {   // updatePdfPage: the last page whose top is above the view top + 30
        int n = 1;
        for (size_t i = 0; i < top.size(); ++i)
            if (a.y + 2 * s + top[i] - scroll <= a.y + 30 * s) n = (int)i + 1;
            else break;
        if (n != m_pdfPage) { m_pdfPage = n; m_prefsDirty = true; }
    }
}

void BrickApp::uiManual() {
    if (!m_pManual.visible || m_mechOn) return;
    Ui u = ui(m_ui);
    if (m_gui.fontBold) u.bold = m_gui.fontBold;
    const float s = m_ui;
    const float stageW = m_stage.z - m_stage.x;
    float W = m_manW >= 260 ? m_manW : std::min(stageW * 0.34f, 500 * s);
    W = std::min(W, std::max(300 * s, stageW - 40 * s));
    const float padT = 8 * s, padX = 10 * s, padB = 10 * s, gap = 8 * s, headH = 28 * s;
    const float x1 = m_stage.z - 14 * s, x0 = x1 - W, y0 = m_stage.y + 12 * s;
    const bool folded = m_pManual.collapsed;
    const float innerW = W - 2 * padX;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    // body height: sized (m_manH) or the pages up to min(70vh, 900) / the PDF up to min(72vh, 920)
    const int total = (int)m_bricks.size();
    m_manRight = std::clamp(m_manRight, 2, std::max(2, total));
    const int l = m_manRight - 1, r = m_manRight;
    float pagesH = manualPage(u, ImVec2(0, 0), innerW, l, 0, false, false) + 8 * s + manualPage(u, ImVec2(0, 0), innerW, r, 1, false, false);
    float bodyH;
    if (m_manH >= 160) bodyH = m_manH - padT - headH - gap - padB;
    else if (m_pdfOn) bodyH = std::min(ds.y * 0.72f, 920 * s);
    else bodyH = std::min(pagesH, std::min(ds.y * 0.70f, 900 * s));
    bodyH = std::max(40 * s, std::min(bodyH, m_stage.w - y0 - padT - headH - gap - padB - 8 * s));
    const float H = padT + headH + (folded ? 0 : gap + bodyH) + padB;
    ImVec2 p0(x0, y0), p1(x1, y0 + H);
    u.dl->AddRectFilled(ImVec2(p0.x, p0.y + 16 * s), ImVec2(p1.x, p1.y + 16 * s), IM_COL32(0, 0, 0, 40), 16 * s);   // shadow
    u.dl->AddRectFilled(p0, p1, C_PANEL, 16 * s);
    u.dl->AddRect(p0, p1, C_LINE, 16 * s);
    m_uiRects.push_back(ImVec4(p0.x, p0.y, p1.x, p1.y));
    // header: dots, Manuel, step / page, buttons
    float x = p0.x + padX, y = p0.y + padT;
    for (int rr = 0; rr < 2; ++rr)
        for (int cc = 0; cc < 5; ++cc) u.dl->AddCircleFilled(ImVec2(x + (3 + cc * 6) * s, y + headH * 0.5f - 2 * s + rr * 4 * s), 1.1f * s, C_FAINT, 6);
    x += 26 * s + 6 * s;
    float tw = u.text(ImVec2(x, y + (headH - u.fs(13)) * 0.5f), 13, C_TEXT, "Manuel", true).x;
    x += tw + 6 * s;
    char step[96];
    if (m_pdfOn && !m_pdfSizes.empty()) snprintf(step, sizeof(step), "PDF p. %d / %d", m_pdfPage, (int)m_pdfSizes.size());
    else if (total) snprintf(step, sizeof(step), "Étapes %d–%d / %d", l, std::min(r, total), total);
    else snprintf(step, sizeof(step), "Aucune brique");
    u.text(ImVec2(x, y + (headH - u.fs(11)) * 0.5f), 11, C_DIM, step);
    struct Btn { const char* label; bool wide; int id; const char* tip; };
    std::vector<Btn> btns;
    if (m_pdf && (!m_pdfSizes.empty() || m_pdfOpening)) btns.push_back({m_pdfOn ? "Scène" : "PDF", true, 0, m_pdfOn ? "Revenir au manuel de ta scène" : "Revenir au PDF importé"});
    btns.push_back({"PDF", true, 1, "Importer un PDF d'instructions"});
    btns.push_back({"‹", false, 2, "Pages précédentes"});
    btns.push_back({"›", false, 3, "Pages suivantes"});
    btns.push_back({"–", false, 4, "Réduire"});
    btns.push_back({"⤢", false, 5, "Agrandir"});
    float bx = p1.x - padX;
    int clicked = -1;
    for (int i = (int)btns.size() - 1; i >= 0; --i) {
        const Btn& bt = btns[i];
        const float bw = bt.wide ? u.textW(11, bt.label, true) + 16 * s : 28 * s;
        bx -= bw;
        ImVec2 a(bx, y), b(bx + bw, y + 28 * s);
        const bool h = u.hover(a, b);
        u.dl->AddRectFilled(a, b, h ? C_LYEL : C_CARD, 8 * s);
        const float px = bt.wide ? 11 : 15;
        const float lw = u.textW(px, bt.label, bt.wide);
        u.text(ImVec2((a.x + b.x) * 0.5f - lw * 0.5f, a.y + (28 * s - u.fs(px)) * 0.5f), px, h ? IM_COL32(0x1b, 0x15, 0x00, 255) : C_TEXT, bt.label, bt.wide);
        if (h) ImGui::SetTooltip("%s", bt.tip);
        if (h && u.pressed) clicked = bt.id;
        bx -= 4 * s;
    }
    if (clicked == 0) { m_pdfOn = !m_pdfOn; if (m_pdfOn) m_pdfScrollTo = m_pdfPage, m_pdfScrollSmooth = false; }
    if (clicked == 1) pdfDialog();
    if (clicked == 2 || clicked == 3) {   // manualStep
        const int d = clicked == 2 ? -1 : 1;
        if (m_pdfOn && !m_pdfSizes.empty()) { m_pdfScrollTo = std::clamp(m_pdfPage + d, 1, (int)m_pdfSizes.size()); m_pdfScrollSmooth = true; m_pdfPage = m_pdfScrollTo; }
        else { m_manRight += d; m_manScroll = 0; }
    }
    if (clicked == 4) { m_pManual.collapsed = !m_pManual.collapsed; savePrefs(); }
    if (clicked == 5) { if (m_pdfOn) { m_pdfBig = true; m_bigScrollTo = m_pdfPage; } else { m_manBig = true; m_bigScroll = 0; } }
    if (folded) return;
    // body
    ImVec2 ba(p0.x + padX, p0.y + padT + headH + gap), bb(p1.x - padX, ba.y + bodyH);
    if (m_pdfOn) pdfView(u, ba, bb, m_pdfScroll, true);
    else {
        const bool over = u.hover(ba, bb);
        if (over && ImGui::GetIO().MouseWheel != 0) m_manScroll -= ImGui::GetIO().MouseWheel * 60 * s;
        m_manScroll = std::clamp(m_manScroll, 0.0f, std::max(0.0f, pagesH - bodyH));
        u.dl->PushClipRect(ba, bb, true);
        float py = ba.y - m_manScroll;
        py += manualPage(u, ImVec2(ba.x, py), innerW, l, 0, false, true) + 8 * s;
        manualPage(u, ImVec2(ba.x, py), innerW, r, 1, false, true);
        u.dl->PopClipRect();
        if (over) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);   // cursor: zoom-in
        if (over && u.released && ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).x == 0 && ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).y == 0 && !m_manResizing) { m_manBig = true; m_bigScroll = 0; }
    }
    // resize handle (bottom left, 24 x 24): width follows the drag (anchored at the right), height too (anchored at the top)
    ImVec2 ra(p0.x + 3 * s, p1.y - 3 * s - 24 * s), rb(ra.x + 24 * s, ra.y + 24 * s);
    const bool rh = u.hover(ra, rb);
    if (rh && u.pressed) { m_manResizing = true; m_manResize0 = u.mouse; m_manW0 = W; m_manH0 = H; }
    if (m_manResizing) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const float maxW = std::max(300 * s, stageW - 40 * s);
            m_manW = std::clamp(m_manW0 + (m_manResize0.x - u.mouse.x), 260 * s, std::min(900 * s, maxW));
            m_manH = std::round(std::clamp(m_manH0 + (u.mouse.y - m_manResize0.y), 160 * s, m_stage.w - y0 - 8 * s));
        } else { m_manResizing = false; savePrefs(); }
    }
    u.dl->AddRectFilled(ra, rb, (rh || m_manResizing) ? C_LYEL : IM_COL32(0, 0, 0, 89), 8 * s);
    if (rh || m_manResizing) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNESW);
    svg::path(u.dl, "M2 14 L8 8 M2 14 L2 9 M2 14 L7 14 M14 2 L8 8", ImVec2(ra.x + 4 * s, ra.y + 4 * s), s, (rh || m_manResizing) ? IM_COL32(0x1b, 0x15, 0x00, 255) : C_DIM, 1.8f * s);
}

// #mbig (scene pages, vertical book) and #pdfbig (the PDF, large)
void BrickApp::uiManualBig() {
    if (!m_manBig && !m_pdfBig) return;
    Ui u = ui(m_ui);
    if (m_gui.fontBold) u.bold = m_gui.fontBold;
    const float s = m_ui;
    const ImVec2 d = ImGui::GetIO().DisplaySize;
    m_uiRects.push_back(ImVec4(0, 0, d.x, d.y));
    auto roundBtn = [&](ImVec2 c, float r, const char* label, float px) {
        ImVec2 a(c.x - r, c.y - r), b(c.x + r, c.y + r);
        const bool h = u.hover(a, b);
        u.dl->AddCircleFilled(c, r, h ? C_CARD_HI : C_PANEL_SOLID, 32);
        u.dl->AddCircle(c, r, C_LINE, 32, 1.0f);
        const float lw = u.textW(px, label);
        u.text(ImVec2(c.x - lw * 0.5f, c.y - u.fs(px) * 0.5f), px, C_TEXT, label);
        return h && u.pressed;
    };
    if (m_pdfBig) {
        u.dl->AddRectFilled(ImVec2(0, 0), d, IM_COL32(8, 10, 14, 209));
        const float w = std::min(1000 * s, d.x * 0.94f);
        ImVec2 a((d.x - w) * 0.5f, 16 * s), b(a.x + w, d.y - 40 * s);
        pdfView(u, a, b, m_bigScroll, false);
        if (roundBtn(ImVec2(d.x - 14 * s - 19 * s, 12 * s + 19 * s), 19 * s, "✕", 14) || ImGui::IsKeyPressed(ImGuiKey_Escape)) m_pdfBig = false;
        return;
    }
    u.dl->AddRectFilled(ImVec2(0, 0), d, IM_COL32(8, 10, 14, 140));
    const float w = std::min(760 * s, d.x * 0.92f);
    const int total = (int)m_bricks.size();
    m_manRight = std::clamp(m_manRight, 2, std::max(2, total));
    const int l = m_manRight - 1, r = m_manRight;
    const float h1 = manualPage(u, ImVec2(0, 0), w, l, 0, true, false), h2 = manualPage(u, ImVec2(0, 0), w, r, 1, true, false);
    const float content = h1 + 12 * s + h2, viewH = std::min(content, d.y * 0.92f);
    ImVec2 a((d.x - w) * 0.5f, (d.y - viewH) * 0.5f), b(a.x + w, a.y + viewH);
    if (u.hover(a, b) && ImGui::GetIO().MouseWheel != 0) m_bigScroll -= ImGui::GetIO().MouseWheel * 80 * s;
    m_bigScroll = std::clamp(m_bigScroll, 0.0f, std::max(0.0f, content - viewH));
    u.dl->AddRectFilled(ImVec2(a.x, a.y + 24 * s), ImVec2(b.x, b.y + 24 * s), IM_COL32(0, 0, 0, 60), 8 * s);   // drop-shadow
    u.dl->PushClipRect(a, b, true);
    manualPage(u, ImVec2(a.x, a.y - m_bigScroll), w, l, 0, true, true);
    manualPage(u, ImVec2(a.x, a.y - m_bigScroll + h1 + 12 * s), w, r, 1, true, true);
    u.dl->PopClipRect();
    bool close = false;
    if (roundBtn(ImVec2(b.x + 10 * s - 17 * s, a.y - 14 * s + 17 * s), 17 * s, "✕", 13)) close = true;
    if (roundBtn(ImVec2(a.x - 22 * s + 22 * s, (a.y + b.y) * 0.5f), 22 * s, "‹", 22)) { m_manRight -= 1; return; }
    if (roundBtn(ImVec2(b.x + 22 * s - 22 * s, (a.y + b.y) * 0.5f), 22 * s, "›", 22)) { m_manRight += 1; return; }
    if (close || ImGui::IsKeyPressed(ImGuiKey_Escape) || (u.pressed && !u.hover(a, b) && !close)) m_manBig = false;
}


// --------------------------------------------------------------------------- dockable panels (web: grip pointer events)
// Returns true while the panel is being dragged out (the caller stops drawing its content this frame).
bool BrickApp::gripPanel(int id, PanelState& P, ImVec2 ga, ImVec2 gb, ImVec2 panelTopLeft) {
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 m = io.MousePos;
    const float s = m_ui;
    if (m_grip.id < 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && m.x >= ga.x && m.y >= ga.y && m.x < gb.x && m.y < gb.y) {
        m_grip = GripDrag();
        m_grip.id = id;
        m_grip.start = m;
        m_grip.ox = m.x - panelTopLeft.x;
        m_grip.oy = m.y - panelTopLeft.y;
    }
    if (m_grip.id != id) return false;
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (!m_grip.moved && std::hypot(m.x - m_grip.start.x, m.y - m_grip.start.y) > 6) {
            m_grip.moved = true;
            if (!P.floating) {
                m_grip.ox = std::min(m.x - panelTopLeft.x, 120 * s);
                m_grip.oy = 8 * s;
                P.floating = true;
                P.collapsed = false;
            }
        }
        if (m_grip.moved) { P.x = m.x - m_grip.ox; P.y = m.y - m_grip.oy; }
        return false;
    }
    // released: a click folds, a drop on the dock zone docks
    const ImVec2 d = io.DisplaySize;
    if (!m_grip.moved) P.collapsed = !P.collapsed;
    else {
        const char* t = id == 0 ? (m.y > d.y - 80 * s ? "bottom" : nullptr) : (m.x < 90 * s ? "left" : m.x > d.x - 90 * s ? "right" : nullptr);
        if (t) { P.floating = false; if (id == 1) m_optsRight = std::string(t) == "right"; }
    }
    m_grip = GripDrag();
    savePrefs();
    return false;
}

void BrickApp::drawDropzone() {
    if (m_grip.id < 0 || !m_grip.moved) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 d = ImGui::GetIO().DisplaySize, m = ImGui::GetIO().MousePos;
    const float s = m_ui;
    const char* t = m_grip.id == 0 ? (m.y > d.y - 80 * s ? "bottom" : nullptr) : (m.x < 90 * s ? "left" : m.x > d.x - 90 * s ? "right" : nullptr);
    ImVec2 a, b;
    if (m_grip.id == 0) { a = ImVec2(8 * s, d.y - 74 * s); b = ImVec2(d.x - 8 * s, d.y - 8 * s); }
    else {
        const bool right = t && std::string(t) == "right";
        a = ImVec2(right ? d.x - 96 * s : 6 * s, 60 * s);
        b = ImVec2(a.x + 90 * s, d.y - 80 * s);
    }
    const float op = t ? 1.0f : 0.45f;
    dl->AddRectFilled(a, b, IM_COL32(247, 197, 30, (int)(36 * op)), 10 * s);
    // 2 px dashed accent border
    auto dash = [&](ImVec2 p, ImVec2 q) {
        const float len = std::hypot(q.x - p.x, q.y - p.y);
        for (float u0 = 0; u0 < len; u0 += 10 * s) {
            const float u1 = std::min(len, u0 + 6 * s);
            dl->AddLine(ImVec2(p.x + (q.x - p.x) * u0 / len, p.y + (q.y - p.y) * u0 / len), ImVec2(p.x + (q.x - p.x) * u1 / len, p.y + (q.y - p.y) * u1 / len), IM_COL32(247, 197, 30, (int)(255 * op)), 2 * s);
        }
    };
    dash(a, ImVec2(b.x, a.y));
    dash(ImVec2(b.x, a.y), b);
    dash(b, ImVec2(a.x, b.y));
    dash(ImVec2(a.x, b.y), a);
}


// --------------------------------------------------------------------------- drag scrolling of the library rows
// A press in the row starts a gesture; after 6 px the dominant direction decides: sideways = the row slides with the
// cursor and keeps its speed on release (decelerating, like the touch scrolling of the web), up / down = left to the
// card (brick in hand when it leaves the tray). Returns true while the row is being dragged.
bool BrickApp::swipeRow(int id, ImVec2 a, ImVec2 b, float& scroll, float& vel, float maxScroll, bool live) {
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 m = io.MousePos;
    const float s = m_ui, dt = std::max(1e-3f, io.DeltaTime);
    if (m_swipeDragged == id && !ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseReleased(ImGuiMouseButton_Left)) m_swipeDragged = -1;
    if (live && m_swipe.id < 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && m.x >= a.x && m.y >= a.y && m.x < b.x && m.y < b.y) {
        m_swipe = Swipe();
        m_swipe.id = id;
        m_swipe.start = m_swipe.last = m;
        vel = 0;
    }
    bool dragging = false;
    if (m_swipe.id == id) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const float dx = m.x - m_swipe.start.x, dy = m.y - m_swipe.start.y;
            if (m_swipe.dir == 0 && dx * dx + dy * dy > 36 * s * s) m_swipe.dir = std::fabs(dx) > std::fabs(dy) ? 1 : 2;
            if (m_swipe.dir == 1) {
                const float step = m.x - m_swipe.last.x;
                scroll -= step;
                vel = vel * 0.6f + (-step / dt) * 0.4f;   // smoothed speed (px / s)
                dragging = true;
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            }
            m_swipe.last = m;
        } else {
            if (m_swipe.dir != 1) vel = 0;
            else m_swipeDragged = id;   // (the release of this gesture is not a click)
            m_swipe = Swipe();
        }
    } else if (vel != 0) {   // inertia
        scroll += vel * dt;
        vel *= std::exp(-dt * 4.0f);
        if (std::fabs(vel) < 20 * s) vel = 0;
    }
    if (scroll < 0) { scroll = 0; vel = 0; }
    if (scroll > maxScroll) { scroll = maxScroll; vel = 0; }
    return dragging;
}

// --------------------------------------------------------------------------- settings (native addition, gear button)
// UI scale (text and panels, 4K), thumbnail size, real time quality / path tracing, ambient occlusion of the HQ mode,
// tone mapping, voxels, animations, automatic save. Saved in brickit_prefs.json.
void BrickApp::uiSettings() {
    Ui u = ui(m_ui);
    if (m_gui.fontBold) u.bold = m_gui.fontBold;
    const float s = m_ui;
    const ImVec2 d = ImGui::GetIO().DisplaySize;
    const float W = 320 * s, pad = 14 * s, rowH = 30 * s, gap = 10 * s;
    struct Opt { const char* label; bool on; };
    struct Row { const char* title; std::vector<Opt> opts; int id; };
    char scaleLab[5][8];
    static const float kScales[5] = {0.75f, 1.0f, 1.25f, 1.5f, 2.0f};
    std::vector<Opt> scales;
    for (int i = 0; i < 5; ++i) { snprintf(scaleLab[i], sizeof(scaleLab[i]), "%d %%", (int)std::lround(kScales[i] * 100)); scales.push_back({scaleLab[i], std::fabs(m_uiScaleUser - kScales[i]) < 0.01f}); }
    std::vector<Row> rows = {
        {"Échelle de l'interface", scales, 0},
        {"Réglages par défaut", {{"Enregistrer ceux-ci", false}, {"Rétablir", false}}, 13},
        {"HUD tactile (iPad)", {{"Oui", m_touchHud}, {"Non", !m_touchHud}}, 14},
        {"Taille des miniatures", {{"Petites", m_thumbSize == 0}, {"Moyennes", m_thumbSize == 1}, {"Grandes", m_thumbSize == 2}}, 1},
        {"Rendu", {{"Rapide", m_rs.raster && m_quality == 0}, {"HQ", m_rs.raster && m_quality == 1}, {"PT", pathTraced()}, {"RC", m_rcMode}}, 2},
        {"Anti-aliasing (Radiance Cascades)", {{"MSAA 4x", m_rs.aaQuality == 0}, {"MSAA 8x", m_rs.aaQuality == 1}, {"AAA", m_rs.aaQuality == 2}}, 9},
        {"Ombres (Radiance Cascades)", {{"Basse", m_rs.shadowQuality <= 1}, {"Moyenne", m_rs.shadowQuality == 2}, {"Haute", m_rs.shadowQuality == 3}, {"AAA", m_rs.shadowQuality == 4}}, 10},
        {"Anti-aliasing au repos (HQ)", {{"Oui", m_stillAA}, {"Non", !m_stillAA}}, 12},
        {"Échelle de rendu", {{"100 %", m_renderScale > 0.95f}, {"85 %", std::fabs(m_renderScale - 0.85f) < 0.01f}, {"75 %", std::fabs(m_renderScale - 0.75f) < 0.01f}, {"67 %", m_renderScale < 0.7f}}, 11},
        {"Occlusion HDRI (HQ)", {{"Oui", m_ssaoOn}, {"Non", !m_ssaoOn}}, 3},   // the traced occlusion of the environment (about 1.2 ms)
        {"Transformation de vue", {{"ACES", m_rs.tonemapper == 0}, {"AgX", m_rs.tonemapper == 1}, {"Std", m_rs.tonemapper == 3}}, 4},
        {"Voxels", {{"Affichés", m_showVox}, {"Masqués", !m_showVox}}, 6},
        {"Animations", {{"Oui", m_animOn}, {"Non", !m_animOn}}, 7},
        {"Sauvegarde automatique", {{"Oui", m_autosaveOn}, {"Non", !m_autosaveOn}}, 8},
    };
    const float H = pad + 20 * s + gap + rows.size() * (16 * s + rowH + gap) - gap + pad;
    // second column: Lumière (HDRI, rotation, intensity, custom HDRI, colour of the grid)
    const float hdriRowsN = (float)((m_hdriPaths.size() + 1) / 2);
    const float H2 = (16 * s + rowH + gap) + pad + 20 * s + gap + 16 * s + hdriRowsN * (rowH + 4 * s) + gap + rowH + gap + 2 * (16 * s + rowH + gap) + 16 * s + rowH + gap + 16 * s + rowH + gap + 18 * s + pad;
    const float W2 = 2 * W;
    // (the Lumière column as tall as it was drawn last frame: its rows depend on the settings)
    ImVec2 p0(std::min(m_popupAt.x, d.x - W2 - 10 * s), m_popupAt.y), p1(p0.x + W2, std::min(p0.y + std::max(H, std::max(H2, m_lumColumnH)), d.y - 8 * s));
    u.dl->AddRectFilled(ImVec2(p0.x, p0.y + 10 * s), ImVec2(p1.x, p1.y + 10 * s), IM_COL32(0, 0, 0, 60), 12 * s);
    u.dl->AddRectFilled(p0, p1, C_PANEL_SOLID, 12 * s);
    u.dl->AddRect(p0, p1, C_LINE, 12 * s);
    m_uiRects.push_back(ImVec4(p0.x, p0.y, p1.x, p1.y));
    float y = p0.y + pad;
    u.text(ImVec2(p0.x + pad, y), 14, C_TEXT, "Réglages", true);
    y += 20 * s + gap;
    int clickRow = -1, clickOpt = -1;
    for (const Row& r : rows) {
        u.text(ImVec2(p0.x + pad, y), 11, C_DIM, r.title);
        y += 16 * s;
        // segmented buttons (.btn, the active one in accent)
        float x = p0.x + pad;
        const float bw = (W - 2 * pad - (r.opts.size() - 1) * 4 * s) / r.opts.size();
        for (size_t i = 0; i < r.opts.size(); ++i) {
            ImVec2 a(x, y), b(x + bw, y + rowH);
            const bool h = u.hover(a, b);
            u.dl->AddRectFilled(a, b, r.opts[i].on ? C_ACCENT : (h ? C_CARD_HI : C_CARD), 7 * s);
            const float tw = u.textW(12, r.opts[i].label, r.opts[i].on);
            u.text(ImVec2((a.x + b.x) * 0.5f - tw * 0.5f, a.y + (rowH - u.fs(12)) * 0.5f), 12, r.opts[i].on ? C_INK : C_TEXT, r.opts[i].label, r.opts[i].on);
            if (h && u.pressed) { clickRow = r.id; clickOpt = (int)i; }
            x += bw + 4 * s;
        }
        y += rowH + gap;
    }
    // ---- Lumière (native addition): the same scene for every renderer
    {
        const float x0 = p0.x + W, x1 = p1.x;
        u.dl->AddLine(ImVec2(x0, p0.y + pad), ImVec2(x0, p1.y - pad), C_LINE, 1.0f);
        float ly = p0.y + pad;
        u.text(ImVec2(x0 + pad, ly), 14, C_TEXT, "Lumière", true);
        ly += 20 * s + gap;
        bool lookChanged = false;
        {   // GI douce (HQ): on / off (off = the HQ as before, no probe computed)
            u.text(ImVec2(x0 + pad, ly), 11, C_DIM, "GI douce (HQ)");
            ly += 16 * s;
            const float bw2 = (W - 2 * pad - 4 * s) / 2;
            for (int k = 0; k < 2; ++k) {
                const bool on = (k == 0) == m_softGI;
                ImVec2 a(x0 + pad + k * (bw2 + 4 * s), ly), b(a.x + bw2, ly + rowH);
                const bool h = u.hover(a, b);
                u.dl->AddRectFilled(a, b, on ? C_ACCENT : (h ? C_CARD_HI : C_CARD), 7 * s);
                const char* lab = k == 0 ? "Oui" : "Non";
                const float tw = u.textW(12, lab, on);
                u.text(ImVec2((a.x + b.x) * 0.5f - tw * 0.5f, a.y + (rowH - u.fs(12)) * 0.5f), 12, on ? C_INK : C_TEXT, lab, on);
                if (h && u.pressed && !on) { m_softGI = k == 0; m_renderer.resetAccumulation(); savePrefs(); }
            }
            ly += rowH + gap;
        }
        if (m_softGI) {   // Figer la GI pendant le déplacement: the probes held while a brick is carried (Non: they follow it live)
            u.text(ImVec2(x0 + pad, ly), 11, C_DIM, "Figer la GI pendant le déplacement");
            ly += 16 * s;
            const float bw2 = (W - 2 * pad - 4 * s) / 2;
            for (int k = 0; k < 2; ++k) {
                const bool on = (k == 0) == m_softGIHold;
                ImVec2 a(x0 + pad + k * (bw2 + 4 * s), ly), b(a.x + bw2, ly + rowH);
                const bool h = u.hover(a, b);
                u.dl->AddRectFilled(a, b, on ? C_ACCENT : (h ? C_CARD_HI : C_CARD), 7 * s);
                const char* lab = k == 0 ? "Oui" : "Non";
                const float tw = u.textW(12, lab, on);
                u.text(ImVec2((a.x + b.x) * 0.5f - tw * 0.5f, a.y + (rowH - u.fs(12)) * 0.5f), 12, on ? C_INK : C_TEXT, lab, on);
                if (h && u.pressed && !on) { m_softGIHold = k == 0; savePrefs(); }
            }
            ly += rowH + gap;
        }
        if (m_softGI) {   // Sondes GI: their number (quality and cost of the soft GI); a change restarts the GI
            u.text(ImVec2(x0 + pad, ly), 11, C_DIM, "Qualité des sondes");
            ly += 16 * s;
            // (16384 at most: the probe atlases are 16384 texels wide, lit::placeProbeGrid)
            static const int kBudgets[3] = {4096, 8192, 16384};
            static const char* kLabels[3] = {"Basse", "Moyenne", "Haute"};
            const float bw4 = (W - 2 * pad - 2 * 4 * s) / 3;
            for (int k = 0; k < 3; ++k) {
                const bool on = m_softGIProbes == kBudgets[k];
                ImVec2 a(x0 + pad + k * (bw4 + 4 * s), ly), b(a.x + bw4, ly + rowH);
                const bool h = u.hover(a, b);
                u.dl->AddRectFilled(a, b, on ? C_ACCENT : (h ? C_CARD_HI : C_CARD), 7 * s);
                const float tw = u.textW(12, kLabels[k], on);
                u.text(ImVec2((a.x + b.x) * 0.5f - tw * 0.5f, a.y + (rowH - u.fs(12)) * 0.5f), 12, on ? C_INK : C_TEXT, kLabels[k], on);
                if (h && u.pressed && !on) { m_softGIProbes = kBudgets[k]; m_renderer.resetAccumulation(); savePrefs(); }
            }
            ly += rowH + gap;
            u.text(ImVec2(x0 + pad, ly), 11, C_DIM, "Afficher les sondes");
            ly += 16 * s;
            const float bw2 = (W - 2 * pad - 4 * s) / 2;
            for (int k = 0; k < 2; ++k) {
                const bool on = (k == 0) == m_showProbes;
                ImVec2 a(x0 + pad + k * (bw2 + 4 * s), ly), b(a.x + bw2, ly + rowH);
                const bool h = u.hover(a, b);
                u.dl->AddRectFilled(a, b, on ? C_ACCENT : (h ? C_CARD_HI : C_CARD), 7 * s);
                const char* lab = k == 0 ? "Oui" : "Non";
                const float tw = u.textW(12, lab, on);
                u.text(ImVec2((a.x + b.x) * 0.5f - tw * 0.5f, a.y + (rowH - u.fs(12)) * 0.5f), 12, on ? C_INK : C_TEXT, lab, on);
                if (h && u.pressed && !on) { m_showProbes = k == 0; savePrefs(); }
            }
            ly += rowH + gap;
        }
        {   // Lookdev GI: one light grey for every brick, no reflection (the lighting alone); not saved
            u.text(ImVec2(x0 + pad, ly), 11, C_DIM, "Lookdev GI (gris uni)");
            ly += 16 * s;
            const float bw2 = (W - 2 * pad - 4 * s) / 2;
            for (int k = 0; k < 2; ++k) {
                const bool on = (k == 0) == m_giLookdev;
                ImVec2 a(x0 + pad + k * (bw2 + 4 * s), ly), b(a.x + bw2, ly + rowH);
                const bool h = u.hover(a, b);
                u.dl->AddRectFilled(a, b, on ? C_ACCENT : (h ? C_CARD_HI : C_CARD), 7 * s);
                const char* lab = k == 0 ? "Oui" : "Non";
                const float tw = u.textW(12, lab, on);
                u.text(ImVec2((a.x + b.x) * 0.5f - tw * 0.5f, a.y + (rowH - u.fs(12)) * 0.5f), 12, on ? C_INK : C_TEXT, lab, on);
                if (h && u.pressed && !on) setLookdev(k == 0);
            }
            ly += rowH + gap;
        }
        // HDRI: the list, two per line
        u.text(ImVec2(x0 + pad, ly), 11, C_DIM, "HDRI");
        ly += 16 * s;
        const float bw = (W - 2 * pad - 4 * s) / 2;
        for (size_t i = 0; i < m_hdriPaths.size(); ++i) {
            const float bx = x0 + pad + (i % 2) * (bw + 4 * s);
            ImVec2 a(bx, ly), b(bx + bw, ly + rowH);
            const bool on = m_hdriPaths[i] == m_hdriPath, h = u.hover(a, b);
            u.dl->AddRectFilled(a, b, on ? C_ACCENT : (h ? C_CARD_HI : C_CARD), 7 * s);
            std::string name = "Ciel procédural";
            if (!m_hdriPaths[i].empty()) {
                std::wstring f = m_hdriPaths[i].substr(m_hdriPaths[i].find_last_of(L"\\/") + 1);
                const size_t dot = f.find_last_of(L'.');
                std::wstring ext = dot != std::wstring::npos ? f.substr(dot + 1) : L"";
                if (dot != std::wstring::npos) f = f.substr(0, dot);
                for (const wchar_t* cut : {L"_2k", L"_4k", L"_1k", L"_8k"}) { const size_t k = f.find(cut); if (k != std::wstring::npos) f = f.substr(0, k); }
                for (auto& c : f) if (c == L'_') c = L' ';
                if (f.size() > 16) f = f.substr(0, 16);
                if (ext == L"exr") f += L" (exr)";
                name.assign(f.begin(), f.end());   // (file names of the HDRI: plain ASCII)
            }
            const float tw = u.textW(11, name.c_str(), on);
            u.text(ImVec2((a.x + b.x) * 0.5f - tw * 0.5f, a.y + (rowH - u.fs(11)) * 0.5f), 11, on ? C_INK : C_TEXT, name.c_str(), on);
            if (h && u.pressed && !on) { setHdri(m_hdriPaths[i]); savePrefs(); }
            if (i % 2 == 1 || i + 1 == m_hdriPaths.size()) ly += rowH + 4 * s;
        }
        ly += gap - 4 * s;
        {   // load a custom HDRI
            ImVec2 a(x0 + pad, ly), b(x1 - pad, ly + rowH);
            const bool h = u.hover(a, b);
            u.dl->AddRectFilled(a, b, h ? C_CARD_HI : C_CARD, 7 * s);
            const char* lab = "Charger une HDRI…  (.hdr, .exr)";
            const float tw = u.textW(12, lab, false);
            u.text(ImVec2((a.x + b.x) * 0.5f - tw * 0.5f, a.y + (rowH - u.fs(12)) * 0.5f), 12, C_TEXT, lab);
            if (h && u.pressed) loadHdriDialog();
            ly += rowH + gap;
        }
        // sliders (live)
        auto slider = [&](int id, const char* label, float& v, float lo, float hi, const char* fmt, float shown) {
            char vb[32];
            snprintf(vb, sizeof(vb), fmt, shown);
            u.text(ImVec2(x0 + pad, ly), 11, C_DIM, label);
            const float vw = u.textW(11, vb, false);
            u.text(ImVec2(x1 - pad - vw, ly), 11, C_TEXT, vb);
            ly += 16 * s;
            ImVec2 a(x0 + pad, ly), b(x1 - pad, ly + rowH);
            const bool h = u.hover(a, b);
            if (h && u.pressed) m_setDrag = id;
            bool changed = false;
            if (m_setDrag == id) {
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    const float t = std::clamp((u.mouse.x - a.x) / std::max(1.0f, b.x - a.x), 0.0f, 1.0f);
                    const float nv = lo + t * (hi - lo);
                    if (std::fabs(nv - v) > 1e-5f) { v = nv; changed = true; }
                } else { m_setDrag = -1; savePrefs(); }
            }
            const float t = std::clamp((v - lo) / (hi - lo), 0.0f, 1.0f), cy = ly + rowH * 0.5f;
            u.dl->AddRectFilled(ImVec2(a.x, cy - 3 * s), ImVec2(b.x, cy + 3 * s), C_CARD, 3 * s);
            u.dl->AddRectFilled(ImVec2(a.x, cy - 3 * s), ImVec2(a.x + (b.x - a.x) * t, cy + 3 * s), C_ACCENT, 3 * s);
            u.dl->AddCircleFilled(ImVec2(a.x + (b.x - a.x) * t, cy), (h || m_setDrag == id ? 9 : 8) * s, C_ACCENT);
            ly += rowH + gap;
            return changed;
        };
        if (slider(101, "Rotation de la HDRI", m_hdriRotDeg, 0.0f, 360.0f, "%.0f°", m_hdriRotDeg)) lookChanged = true;
        if (slider(102, "Intensité de la HDRI", m_hdriInt, 0.0f, 3.0f, "%.2f", m_hdriInt)) lookChanged = true;
        if (m_softGI) slider(104, "Intensité GI (HQ)", m_softGIStrength, 0.0f, 2.0f, "%.2f", m_softGIStrength);   // (live, probes kept)
        // SSS (HQ): the light under the surface of the plastic (HQ and RC), 0 = off
        slider(105, "SSS (HQ) · 0 = coupé", m_hqSSSStrength, 0.0f, 2.0f, "%.2f", m_hqSSSStrength);
        // the highlight of the selection: its thin outline and its white veil (Wzx: tunable)
        slider(106, "Contour de sélection", m_selOutline, 0.0f, 1.0f, "%.2f", m_selOutline);
        slider(107, "Voile de sélection", m_selFill, 0.0f, 0.5f, "%.2f", m_selFill);
        // the lens of the HQ: depth of field on the camera target, and the glow of the bright parts
        slider(108, "Profondeur de champ (HQ)", m_dof, 0.0f, 1.0f, "%.2f", m_dof);
        slider(109, "Bloom", m_bloom, 0.0f, 1.0f, "%.2f", m_bloom);
        // colour of the ground grid: swatches (Défaut = the lines of the web), then its opacity
        u.text(ImVec2(x0 + pad, ly), 11, C_DIM, "Couleur de la grille");
        ly += 16 * s;
        {
            static const uint32_t sw[7] = {0x9aa3b8, 0xffffff, 0xf2c230, 0x4fc3f7, 0xe53935, 0x43a047, 0x7e57c2};
            const float cw = (W - 2 * pad - 7 * 4 * s) / 8;
            for (int k = 0; k < 8; ++k) {
                const float bx = x0 + pad + k * (cw + 4 * s);
                ImVec2 a(bx, ly), b(bx + cw, ly + rowH);
                const bool h = u.hover(a, b);
                bool on;
                if (k == 0) {
                    on = m_gridAlpha <= 0;
                    u.dl->AddRectFilled(a, b, on ? C_ACCENT : (h ? C_CARD_HI : C_CARD), 7 * s);
                    const float tw = u.textW(10, "Défaut", on);
                    u.text(ImVec2((a.x + b.x) * 0.5f - tw * 0.5f, a.y + (rowH - u.fs(10)) * 0.5f), 10, on ? C_INK : C_TEXT, "Défaut", on);
                    if (h && u.pressed) { m_gridAlpha = 0; lookChanged = true; savePrefs(); }
                } else {
                    const uint32_t c = sw[k - 1];
                    const float3 rgb(((c >> 16) & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, (c & 255) / 255.0f);
                    on = m_gridAlpha > 0 && length(rgb - m_gridRgb) < 0.01f;
                    u.dl->AddRectFilled(a, b, IM_COL32((c >> 16) & 255, (c >> 8) & 255, c & 255, 255), 7 * s);
                    if (on || h) u.dl->AddRect(a, b, on ? C_ACCENT : C_TEXT, 7 * s, 0, (on ? 3.0f : 1.5f) * s);
                    if (h && u.pressed) { m_gridRgb = rgb; if (m_gridAlpha <= 0) m_gridAlpha = 0.6f; lookChanged = true; savePrefs(); }
                }
            }
            ly += rowH + gap;
        }
        float alphaShown = m_gridAlpha * 100.0f;
        if (slider(103, "Opacité de la grille", m_gridAlpha, 0.0f, 1.0f, "%.0f %%", alphaShown)) lookChanged = true;
        {
            char ib[96];
            snprintf(ib, sizeof(ib), "HDRI chargée en %.0f ms · le soleil reste fixe", m_hdriLoadMs);
            u.text(ImVec2(x0 + pad, ly), 10, C_DIM, ib);
            m_lumColumnH = ly + 14 * s + pad - p0.y;
        }
        if (lookChanged) {   // live: the environment and the grid of every renderer (accumulations reset)
            m_scene.env.hdriRotation = radians(m_hdriRotDeg);
            m_scene.env.hdriIntensity = m_hdriInt;
            m_scene.gridLine = gridLineValue();
            m_renderer.resetAccumulation();
        }
    }
    if (clickRow >= 0) {
        switch (clickRow) {
        case 0: m_uiScaleUser = kScales[clickOpt]; m_ui = (m_opt.uiScale > 0 ? m_opt.uiScale : m_dpi) * m_uiScaleUser; app::th::apply(m_ui); break;
        case 1: m_thumbSize = clickOpt; break;
        case 2:
            if (clickOpt == 3) { setRC(true); applyLook(); }
            else if (clickOpt == 2) { setRC(false); m_rs.raster = false; applyLook(); }
            else { setRC(false); m_quality = clickOpt; m_rs.msaaSamples = m_quality ? 4 : 1; if (!m_rs.raster) { m_rs.raster = true; applyLook(); } }
            break;
        case 3: m_ssaoOn = clickOpt == 0; break;
        case 4: m_rs.tonemapper = clickOpt == 2 ? 3 : clickOpt; break;
        case 6: m_showVox = clickOpt == 0; syncObjects(); break;
        case 7: m_animOn = clickOpt == 0; break;
        case 8: m_autosaveOn = clickOpt == 0; break;
        case 9: m_rs.aaQuality = clickOpt; break;
        case 10: m_rs.shadowQuality = clickOpt == 0 ? 0 : clickOpt + 1; break;
        case 12: m_stillAA = clickOpt == 0; break;
        case 14: m_touchHud = clickOpt == 0; m_arcOpen = false; break;
        case 13: if (clickOpt == 0) saveDefaultPrefs(); else resetToDefaultPrefs(); break;
        case 11: { static const float k[4] = {1.0f, 0.85f, 0.75f, 0.67f}; m_renderScale = k[std::clamp(clickOpt, 0, 3)]; break; }
        }
        m_renderer.resetAccumulation();
        savePrefs();
    }
    if (u.pressed && clickRow < 0 && !u.hover(p0, p1) && u.mouse.y > 30 * s) m_popup = 0;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) m_popup = 0;
}

} // namespace brickit
