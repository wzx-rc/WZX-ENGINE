// Editor UI of the Viewport3D demo: top bar + toolbar, hierarchy, inspector, content browser, status bar
// (visual direction from the concepts in "00 - REFERENCES/UI").
#include "App.h"
#include <cwctype>

namespace app {

static Color uiColor(const PaletteColor& c) { return Color::rgbf(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f); }

static float linToSrgb(float c) {
    c = clampf(c, 0, 1);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}
static Color uiColor(const float3& lin) { return Color::rgbf(linToSrgb(lin.x), linToSrgb(lin.y), linToSrgb(lin.z)); }

static Color mixc(const Color& a, const Color& b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

static std::wstring widen(const std::string& s) { return fromUtf8(s); }

static std::wstring thousands(size_t n) {
    std::wstring s = std::to_wstring(n);
    for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert(i, L" ");
    return s;
}

static bool containsNoCase(const std::wstring& hay, const std::wstring& needle) {
    if (needle.empty()) return true;
    std::wstring a = hay, b = needle;
    for (auto& c : a) c = (wchar_t)towlower(c);
    for (auto& c : b) c = (wchar_t)towlower(c);
    return a.find(b) != std::wstring::npos;
}

// Euler angles (degrees) of a rotation built as R = Ry * Rx * Rz.
static float3 eulerYXZ(const float4& q) {
    float m02 = 2 * (q.x * q.z + q.w * q.y), m12 = 2 * (q.y * q.z - q.w * q.x), m22 = 1 - 2 * (q.x * q.x + q.y * q.y);
    float m10 = 2 * (q.x * q.y + q.w * q.z), m11 = 1 - 2 * (q.x * q.x + q.z * q.z);
    float3 e(degrees(std::asin(clampf(-m12, -1, 1))), degrees(std::atan2(m02, m22)), degrees(std::atan2(m10, m11)));
    auto clean = [](float a) { return std::fabs(a) < 0.005f ? 0.0f : a; };   // no "-0.0°"
    return float3(clean(e.x), clean(e.y), clean(e.z));
}
static float4 quatYXZ(const float3& deg) {
    float4 qy = qAxisAngle(float3(0, 1, 0), radians(deg.y)), qx = qAxisAngle(float3(1, 0, 0), radians(deg.x));
    float4 qz = qAxisAngle(float3(0, 0, 1), radians(deg.z));
    return qNormalize(qMul(qMul(qy, qx), qz));
}

static const Color kAxisX = Color::hex(0xE5484D), kAxisY = Color::hex(0x46A758), kAxisZ = Color::hex(0x3E8EF7);

// --------------------------------------------------------------------------- isometric object icons

static const float kIsoA = 0.8660254f, kIsoB = 0.5f;

void App::drawBox3D(float ox, float oy, float s, float W, float D, float H, const Color& c) {
    UI& ui = m_ui;
    auto P = [&](float x, float y, float z) { return float2(ox + (x - z) * kIsoA * s, oy + (x + z) * kIsoB * s - y * s); };
    Color top = mixc(c, Color::rgbf(1, 1, 1), 0.10f);
    Color left = c.scaled(0.80f);
    Color right = c.scaled(0.60f);
    Color edge = Color::rgbf(0, 0, 0, 0.28f);
    float2 t[4] = {P(0, H, 0), P(W, H, 0), P(W, H, D), P(0, H, D)};
    float2 l[4] = {P(0, 0, D), P(W, 0, D), P(W, H, D), P(0, H, D)};
    float2 r[4] = {P(W, 0, 0), P(W, 0, D), P(W, H, D), P(W, H, 0)};
    ui.polygon(t, 4, top);
    ui.polygon(l, 4, left);
    ui.polygon(r, 4, right);
    ui.polygon(t, 4, edge, false, 0.7f);
    ui.polygon(l, 4, edge, false, 0.7f);
    ui.polygon(r, 4, edge, false, 0.7f);
}

void App::drawPartIcon(const PartDef& p, const Rect& r, const Color& c) {
    UI& ui = m_ui;
    switch (p.kind) {
    case K_CUBE: {
        float W = 1.6f, D = 1.6f, H = 1.6f;
        float s = std::min(r.w * 0.80f / ((W + D) * kIsoA), r.h * 0.78f / ((W + D) * kIsoB + H + 0.25f));
        float ox = r.cx() - (W - D) * kIsoA * s * 0.5f;
        float oy = r.cy() - ((W + D) * kIsoB * s - (H + 0.22f) * s) * 0.5f;
        drawBox3D(ox, oy, s, W, D, H, c);
        break;
    }
    case K_SHADERBALL: case K_SPHERE: {
        float R = std::min(r.w, r.h) * 0.32f;
        float cy = r.cy() - (p.kind == K_SHADERBALL ? R * 0.15f : 0);
        if (p.kind == K_SHADERBALL) ui.ellipse(r.cx(), cy + R * 1.05f, R * 1.1f, R * 0.32f, c.scaled(0.55f));
        ui.circle(r.cx(), cy, R, c.scaled(0.62f));
        ui.circle(r.cx() - R * 0.12f, cy - R * 0.12f, R * 0.84f, c.scaled(0.85f));
        ui.circle(r.cx() - R * 0.25f, cy - R * 0.25f, R * 0.5f, mixc(c, Color::rgbf(1, 1, 1), 0.12f));
        ui.circle(r.cx() - R * 0.38f, cy - R * 0.4f, R * 0.12f, Color::rgbf(1, 1, 1, 0.7f));
        break;
    }
    }
}

// --------------------------------------------------------------------------- frame

bool App::section(float x, float& y, float w, const std::wstring& title, int idx) {
    UI& ui = m_ui;
    const Theme& th = ui.theme;
    Rect bar(x - 16, y, w + 32, 28);
    bool h = ui.hover(bar);
    ui.fill(bar, h ? th.cardHover : th.panel2);
    ui.line(bar.x, bar.b() - 0.5f, bar.r(), bar.b() - 0.5f, th.border);
    ui.icon(m_secOpen[idx] ? 0xE70D : 0xE76C, Rect(x - 6, y, 16, 28), th.textDim, 9);
    ui.text(title, Rect(x + 14, y, w - 14, 28), th.text, 12.5f, true);
    if (ui.clicked(bar)) m_secOpen[idx] = !m_secOpen[idx];
    y += 36;
    return m_secOpen[idx];
}

void App::drawUI() {
    UI& ui = m_ui;
    const Theme& th = ui.theme;
    ui.fill(m_rcTop, th.bg);
    ui.fill(m_rcTool, th.panel2);
    ui.line(0, m_rcTool.b() - 0.5f, m_W, m_rcTool.b() - 0.5f, th.border);
    // gap around the viewport
    Rect va(m_rcView.x - 4, m_rcView.y - 4, m_rcView.w + 8, m_rcView.h + 8);
    ui.fill(Rect(va.x, va.y, va.w, m_rcView.y - va.y), th.bg);
    ui.fill(Rect(va.x, m_rcView.b(), va.w, va.b() - m_rcView.b()), th.bg);
    ui.fill(Rect(va.x, m_rcView.y, m_rcView.x - va.x, m_rcView.h), th.bg);
    ui.fill(Rect(m_rcView.r(), m_rcView.y, va.r() - m_rcView.r(), m_rcView.h), th.bg);
    ui.stroke(Rect(m_rcView.x - 1, m_rcView.y - 1, m_rcView.w + 2, m_rcView.h + 2), th.border, 0);

    drawViewportOverlay();
    drawTopBar();
    drawToolbar();
    drawPanels();
    drawStatusBar();
}

void App::drawTopBar() {
    UI& ui = m_ui;
    const Theme& th = ui.theme;
    Rect r = m_rcTop;
    Rect logo(12, 9, 22, 22);
    ui.fill(logo, th.accent, 5);
    ui.icon(0xE7B8, logo, Color::hex(0xFFFFFF), 12);
    ui.text(L"WZX ENGINE", Rect(42, 0, 120, r.h), th.text, 13, true);

    float x = 150;
    auto menuW = [&](const wchar_t* s) { return ui.textWidth(s, 12.5f) + 24; };
    int k;
    float w = menuW(L"Fichier");
    k = ui.menu("menu_file", Rect(x, 0, w, r.h), L"Fichier",
                {L"Scène Cornell Box (F1)", L"Scène Studio (F2)", L"Capture du viewport (F12)", L"Quitter", L"Ouvrir une scène… (Ctrl+O)", L"Enregistrer la scène… (Ctrl+S)"});
    if (k == 0) m_pendingScene = SCENE_CORNELL;
    if (k == 1) m_pendingScene = SCENE_STUDIO;
    if (k == 2) saveScreenshot(false);
    if (k == 3) PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
    if (k == 4) m_pendingAction = 2;
    if (k == 5) m_pendingAction = 1;
    x += w;
    w = menuW(L"Édition");
    k = ui.menu("menu_edit", Rect(x, 0, w, r.h), L"Édition",
                {L"Annuler (Ctrl+Z)", L"Rétablir (Ctrl+Y)", L"Dupliquer (Ctrl+D)", L"Supprimer (Suppr)", L"Tout désélectionner (Échap)"});
    if (k == 0) undo();
    if (k == 1) redo();
    if (k == 2) duplicateSelected();
    if (k == 3) deleteSelected();
    if (k == 4) select(-1);
    x += w;
    w = menuW(L"Affichage");
    k = ui.menu("menu_view", Rect(x, 0, w, r.h), L"Affichage",
                {L"Recadrer la sélection (F)", L"Vue principale", L"Rotation automatique (T)", L"Vue de face", L"Vue de dessus"});
    if (k == 0) frameSelection();
    if (k == 1 && !m_views.empty()) { m_scene.camera = m_views[0].cam; m_activeView = 0; markDirty(); }
    if (k == 2) m_turntable = !m_turntable;
    if (k == 3) setViewAxis(1);
    if (k == 4) setViewAxis(5);
    x += w;
    w = menuW(L"Fenêtres");
    k = ui.menu("menu_windows", Rect(x, 0, w, r.h), L"Fenêtres",
                {std::wstring(m_pan[0].visible ? L"✓  " : L"      ") + L"Hiérarchie", std::wstring(m_pan[1].visible ? L"✓  " : L"      ") + L"Inspecteur",
                 std::wstring(m_pan[2].visible ? L"✓  " : L"      ") + L"Contenu", L"Réinitialiser la disposition"});
    if (k >= 0 && k < 3) togglePanel(k);
    if (k == 3) resetPanels();
    x += w;
    w = menuW(L"Outils");
    k = ui.menu("menu_build", Rect(x, 0, w, r.h), L"Outils", {L"Sélectionner (Q)", L"Déplacer (G)", L"Tourner (R)", L"Grille ON/OFF"});
    if (k == 0) m_tool = TOOL_SELECT;
    if (k == 1) m_tool = TOOL_MOVE;
    if (k == 2) m_tool = TOOL_ROTATE;
    if (k == 3) m_magnet = !m_magnet;
    x += w;
    w = menuW(L"Rendu");
    k = ui.menu("menu_render", Rect(x, 0, w, r.h), L"Rendu",
                {L"Viewport temps réel (P)", L"Path tracing interactif", L"Path tracing qualité finale", L"Relancer l'accumulation", L"Paramètres de rendu"});
    if (k == 0) { m_rs.raster = true; markDirty(); }
    if (k == 1 || k == 2) { m_rs.raster = false; m_quality = k - 1; applyQuality(); markDirty(); }
    if (k == 3) markDirty();
    if (k == 4) m_rightTab = 1;
    x += w;
    w = menuW(L"Exporter");
    k = ui.menu("menu_export", Rect(x, 0, w, r.h), L"Exporter", {L"Image PNG du viewport (F12)", L"Image PNG de la fenêtre"});
    if (k == 0) saveScreenshot(false);
    if (k == 1) saveScreenshot(true);

    // right side: scene, capture, settings
    static const std::vector<std::wstring> scenes = {L"Cornell Box", L"Studio"};
    int sel = m_sceneId;
    ui.icon(0xE80F, Rect(r.r() - 290, 0, 20, r.h), th.textDim, 12);
    if (ui.dropdown("scene_select", Rect(r.r() - 266, 6, 180, 28), scenes, sel)) m_pendingScene = sel;
    if (ui.iconButton(Rect(r.r() - 78, 6, 30, 28), 0xE722, false, 14, L"Enregistrer une capture (F12)")) saveScreenshot(false);
    if (ui.iconButton(Rect(r.r() - 42, 6, 30, 28), 0xE713, m_rightTab == 1, 14, L"Paramètres de rendu")) m_rightTab = 1;
}

void App::drawToolbar() {
    UI& ui = m_ui;
    const Theme& th = ui.theme;
    Rect r = m_rcTool;
    float x = 10, y = r.y + 5, bw = 32, bh = 28;
    auto sep = [&]() { x += 6; ui.line(x, y + 5, x, y + bh - 5, th.border); x += 7; };
    auto btn = [&](wchar_t g, bool active, const wchar_t* tip, bool enabled = true) {
        Rect b(x, y, bw, bh);
        x += bw + 2;
        if (!enabled) { ui.icon(g, b, th.textFaint, 14); return false; }
        return ui.iconButton(b, g, active, 14, tip);
    };
    if (btn(0xE8B0, m_tool == TOOL_SELECT, L"Sélectionner (Q)")) m_tool = TOOL_SELECT;
    if (btn(0xE7C2, m_tool == TOOL_MOVE, L"Déplacer (G)")) m_tool = TOOL_MOVE;
    if (btn(0xE7AD, m_tool == TOOL_ROTATE, L"Tourner par pas de 90° (R)")) m_tool = TOOL_ROTATE;
    sep();
    if (btn(0xE80A, m_magnet, m_magnet ? L"Grille ON : snap de 1 cm" : L"Grille OFF : déplacement libre")) m_magnet = !m_magnet;
    sep();
    if (btn(0xE8C8, false, L"Dupliquer (Ctrl+D)", m_selected >= 0)) duplicateSelected();
    if (btn(0xE74D, false, L"Supprimer (Suppr)", m_selected >= 0)) deleteSelected();
    sep();
    if (btn(0xE7A7, false, L"Annuler (Ctrl+Z)", !m_undo.empty())) undo();
    if (btn(0xE7A6, false, L"Rétablir (Ctrl+Y)", !m_redo.empty())) redo();
    sep();
    if (btn(0xE74E, false, L"Enregistrer la scène (Ctrl+S)")) m_pendingAction = 1;
    if (btn(0xE8E5, false, L"Ouvrir une scène (Ctrl+O)")) m_pendingAction = 2;

    // right: camera helpers
    struct VB { wchar_t g; const wchar_t* tip; };
    VB vb[] = {{0xE80F, L"Vue principale"}, {0xE740, L"Recadrer la sélection (F)"}, {0xE7AD, L"Rotation automatique (T)"},
               {0xE890, L"Profondeur de champ"}, {0xE706, L"Débruitage"}};
    float rx = m_W - 10 - 5 * (bw + 2);
    for (int i = 0; i < 5; ++i) {
        bool active = (i == 2 && m_turntable) || (i == 3 && m_scene.camera.dof) || (i == 4 && m_rs.denoise);
        if (ui.iconButton(Rect(rx + i * (bw + 2), y, bw, bh), vb[i].g, active, 14, vb[i].tip)) {
            if (i == 0 && !m_views.empty()) { m_scene.camera = m_views[0].cam; m_activeView = 0; markDirty(); }
            if (i == 1) frameSelection();
            if (i == 2) m_turntable = !m_turntable;
            if (i == 3) { m_scene.camera.dof = !m_scene.camera.dof; markDirty(); }
            if (i == 4) { m_rs.denoise = !m_rs.denoise; markDirty(); }
        }
    }
}

void App::drawStatusBar() {
    UI& ui = m_ui;
    const Theme& th = ui.theme;
    Rect r = m_rcStatus;
    ui.fill(r, th.panel2);
    ui.line(0, r.y + 0.5f, m_W, r.y + 0.5f, th.border);
    bool fresh = !m_status.empty() && GetTickCount() - m_statusTime < 4000;
    ui.icon(fresh ? 0xE946 : 0xE73E, Rect(r.x + 8, r.y, 16, r.h), fresh ? th.accent : kAxisY, 11);
    ui.text(fresh ? m_status : std::wstring(L"Prêt"), Rect(r.x + 30, r.y, r.w * 0.5f, r.h), fresh ? th.text : th.textDim, 11.5f);

    size_t objs = 0;
    for (auto& o : m_scene.objects) if (o.visible && o.part != "WALL" && o.part != "GROUND") ++objs;
    wchar_t buf[256];
    std::wstring mode = m_rs.raster ? L"Viewport raster" : m_rs.ddgi ? L"GI sondes (DDGI)" : (m_quality ? L"Path tracing" : L"PT interactif");
    swprintf(buf, 256, L"%s   ·   %zu objets   ·   %s triangles   ·   %.1f ms GPU   ·   %s", widen(m_scene.name).c_str(), objs,
             thousands(m_scene.triangleCount()).c_str(), m_renderer.gpuFrameMs(), mode.c_str());
    ui.text(buf, Rect(r.cx(), r.y, r.w * 0.5f - 12, r.h), th.textDim, 11.5f, false, Align::Right);
}

// --------------------------------------------------------------------------- hierarchy

void App::drawLeftPanel() {
    UI& ui = m_ui;
    const Theme& th = ui.theme;
    Rect p = m_rcLeft;
    float tw = p.w / 2;
    if (ui.tab(Rect(p.x, p.y, tw, 30), L"Scène", m_leftTab != 1)) m_leftTab = 0;
    if (ui.tab(Rect(p.x + tw, p.y, tw, 30), L"Monde", m_leftTab == 1)) m_leftTab = 1;
    ui.line(p.x, p.y + 29.5f, p.r(), p.y + 29.5f, th.border);
    Rect content(p.x, p.y + 76, p.w, p.h - 76);
    bool hoverContent = ui.hover(content);

    if (m_leftTab != 1) {
        ui.textField("search_scene", Rect(p.x + 8, p.y + 38, p.w - 16, 28), m_search, L"Rechercher dans la scène…", 0xE721);
        Rect list(p.x, content.y, p.w, content.h - 4);
        struct Row { int kind; int idx; int depth; };   // kind: 0 scene root, 1 folder, 2 object, 3 light
        std::vector<Row> rows;
        rows.push_back({0, 0, 0});
        rows.push_back({1, 8, 1});
        if (m_secOpen[8])
            for (size_t i = 0; i < m_scene.objects.size(); ++i) {
                const Object& o = m_scene.objects[i];
                if (o.ghost) continue;
                if (!containsNoCase(widen(o.name), m_search) && !containsNoCase(widen(o.part), m_search)) continue;
                rows.push_back({2, (int)i, 2});
            }
        rows.push_back({1, 9, 1});
        if (m_secOpen[9])
            for (size_t i = 0; i < m_scene.lights.size(); ++i)
                if (containsNoCase(widen(m_scene.lights[i].name), m_search)) rows.push_back({3, (int)i, 2});
        float rowH = 24, contentH = rows.size() * rowH;
        if (hoverContent && ui.wheel != 0) m_listScroll -= ui.wheel * 60;
        m_listScroll = clampf(m_listScroll, 0, std::max(0.0f, contentH - list.h));
        ui.pushClip(list);
        for (size_t k = 0; k < rows.size(); ++k) {
            const Row& rw = rows[k];
            Rect rr(list.x, list.y + k * rowH - m_listScroll, list.w, rowH);
            if (rr.b() < list.y || rr.y > list.b()) continue;
            float ix = rr.x + 10 + rw.depth * 16;
            bool h = ui.hover(rr);
            if (rw.kind == 0) {
                ui.icon(0xE70D, Rect(ix - 14, rr.y, 12, rr.h), th.textDim, 8);
                ui.icon(0xE80F, Rect(ix, rr.y, 16, rr.h), th.textDim, 12);
                ui.text(widen(m_scene.name), Rect(ix + 22, rr.y, rr.w - ix - 30, rr.h), th.text, 12.5f, true);
            } else if (rw.kind == 1) {
                bool open = m_secOpen[rw.idx];
                if (h) ui.fill(rr, th.cardHover.withAlpha(0.6f));
                ui.icon(open ? 0xE70D : 0xE76C, Rect(ix - 14, rr.y, 12, rr.h), th.textDim, 8);
                ui.icon(0xE8B7, Rect(ix, rr.y, 16, rr.h), Color::hex(0xC9A227), 12);
                size_t n = rw.idx == 8 ? m_scene.objects.size() : m_scene.lights.size();
                ui.text(std::wstring(rw.idx == 8 ? L"Géométrie" : L"Lumières") + L"  (" + std::to_wstring(n) + L")",
                        Rect(ix + 22, rr.y, rr.w - ix - 30, rr.h), th.text, 12.5f);
                if (ui.clicked(rr)) m_secOpen[rw.idx] = !open;
            } else if (rw.kind == 2) {
                Object& o = m_scene.objects[rw.idx];
                bool sel = rw.idx == m_selected || std::find(m_multi.begin(), m_multi.end(), rw.idx) != m_multi.end();
                if (sel) ui.fill(rr, th.accentDim);
                else if (h) ui.fill(rr, th.cardHover.withAlpha(0.6f));
                ui.icon(0xE7B8, Rect(ix, rr.y, 16, rr.h), sel ? th.accent.scaled(1.3f) : th.textDim, 12);
                ui.text(widen(o.name), Rect(ix + 22, rr.y, rr.r() - ix - 80, rr.h), o.visible ? th.text : th.textFaint, 12.5f);
                Rect eye(rr.r() - 54, rr.y + 1, 22, rr.h - 2), lock(rr.r() - 28, rr.y + 1, 22, rr.h - 2);
                if (ui.iconButton(eye, o.visible ? 0xE890 : 0xED1A, false, 11, o.visible ? L"Masquer" : L"Afficher")) {
                    pushUndo();
                    o.visible = !o.visible;
                    markDirty();
                } else if (ui.iconButton(lock, o.selectable ? 0xE785 : 0xE72E, false, 11, o.selectable ? L"Verrouiller (non sélectionnable)" : L"Déverrouiller")) {
                    o.selectable = !o.selectable;
                    if (!o.selectable && m_selected == rw.idx) select(-1);
                } else if (ui.clicked(rr)) {
                    if (o.selectable) {
                        if (ui.keyDown(VK_SHIFT) && m_selected >= 0 && m_selected != rw.idx) {
                            auto it = std::find(m_multi.begin(), m_multi.end(), rw.idx);
                            if (it != m_multi.end()) m_multi.erase(it); else m_multi.push_back(rw.idx);
                            markDirty();
                        } else select(rw.idx);
                    }
                }
            } else {
                AreaLight& l = m_scene.lights[rw.idx];
                if (h) ui.fill(rr, th.cardHover.withAlpha(0.6f));
                ui.icon(0xE706, Rect(ix, rr.y, 16, rr.h), l.enabled ? Color::hex(0xF2C94C) : th.textFaint, 12);
                ui.text(widen(l.name), Rect(ix + 22, rr.y, rr.r() - ix - 54, rr.h), l.enabled ? th.text : th.textFaint, 12.5f);
                Rect eye(rr.r() - 28, rr.y + 1, 22, rr.h - 2);
                if (ui.iconButton(eye, l.enabled ? 0xE890 : 0xED1A, false, 11, l.enabled ? L"Éteindre" : L"Allumer")) { l.enabled = !l.enabled; markDirty(); }
                else if (ui.clicked(rr)) m_leftTab = 1;
            }
        }
        ui.popClip();
    } else {
        // World: lights and environment
        Rect area(p.x, p.y + 38, p.w, p.h - 42);
        float x = p.x + 10, w = p.w - 20;
        float y = area.y - m_leftScroll;
        ui.pushClip(area);
        for (size_t i = 0; i < m_scene.lights.size(); ++i) {
            AreaLight& l = m_scene.lights[i];
            Rect card(x, y, w, 158);
            ui.fill(card, th.card, 6);
            ui.stroke(card, th.border, 6);
            ui.icon(0xE706, Rect(card.x + 8, card.y + 4, 20, 28), l.enabled ? Color::hex(0xF2C94C) : th.textFaint, 13);
            ui.text(widen(l.name), Rect(card.x + 32, card.y + 4, card.w - 90, 28), th.text, 12.5f, true);
            bool en = l.enabled;
            if (ui.toggle(Rect(card.r() - 54, card.y + 4, 46, 28), L"", en)) { l.enabled = en; markDirty(); }
            float yy = card.y + 36;
            char id[64];
            sprintf_s(id, "light_int_%zu", i);
            if (ui.slider(id, Rect(card.x + 10, yy, card.w - 20, 28), L"Intensité", l.intensity, 0.0f, 60.0f, L"%.1f", 84)) markDirty();
            yy += 28;
            sprintf_s(id, "light_w_%zu", i);
            if (ui.slider(id, Rect(card.x + 10, yy, card.w - 20, 28), L"Largeur", l.width, 1.0f, 80.0f, L"%.1f", 84)) markDirty();
            yy += 28;
            sprintf_s(id, "light_h_%zu", i);
            if (ui.slider(id, Rect(card.x + 10, yy, card.w - 20, 28), L"Hauteur", l.height, 1.0f, 80.0f, L"%.1f", 84)) markDirty();
            yy += 30;
            bool vis = l.visibleToCamera;
            if (ui.checkbox(Rect(card.x + 10, yy, card.w - 20, 24), L"Visible à la caméra", vis)) { l.visibleToCamera = vis; markDirty(); }
            y += 166;
        }
        Rect card(x, y, w, 74);
        ui.fill(card, th.card, 6);
        ui.stroke(card, th.border, 6);
        ui.icon(0xE909, Rect(card.x + 8, card.y + 4, 20, 28), th.accent, 13);
        ui.text(L"Environnement", Rect(card.x + 32, card.y + 4, card.w - 50, 28), th.text, 12.5f, true);
        if (ui.slider("env_int", Rect(card.x + 10, card.y + 38, card.w - 20, 28), L"Intensité", m_scene.env.intensity, 0, 4, L"%.2f", 84)) markDirty();
        y += 82;
        ui.popClip();
        float contentH = y + m_leftScroll - area.y;
        if (ui.hover(area) && ui.wheel != 0) m_leftScroll -= ui.wheel * 60;
        m_leftScroll = clampf(m_leftScroll, 0, std::max(0.0f, contentH - area.h));
    }
}

// --------------------------------------------------------------------------- inspector

void App::drawRightPanel() {
    UI& ui = m_ui;
    const Theme& th = ui.theme;
    Rect p = m_rcRight;
    float tw = p.w / 2;
    if (ui.tab(Rect(p.x, p.y, tw, 30), L"Inspecteur", m_rightTab == 0)) { m_rightTab = 0; m_rightScroll = 0; }
    if (ui.tab(Rect(p.x + tw, p.y, tw, 30), L"Rendu", m_rightTab == 1)) { m_rightTab = 1; m_rightScroll = 0; }
    ui.line(p.x, p.y + 29.5f, p.r(), p.y + 29.5f, th.border);
    Rect area(p.x + 16, p.y + 40, p.w - 32, p.h - 44);
    ui.pushClip(Rect(p.x + 1, p.y + 30, p.w - 1, p.h - 30));
    if (m_rightTab == 0) drawObjectTab(area);
    else drawRenderTab(area);
    ui.popClip();
}

void App::drawObjectTab(Rect area) {
    UI& ui = m_ui;
    const Theme& th = ui.theme;
    float x = area.x, w = area.w;
    float y = area.y - m_rightScroll;
    Material* mat = m_selected >= 0 ? &m_scene.materials[m_scene.objects[m_selected].material] : nullptr;
    Object* obj = m_selected >= 0 ? &m_scene.objects[m_selected] : nullptr;
    const auto& pal = palette();

    // header: icon, name, type
    Rect head(x, y, w, 46);
    ui.fill(head, th.card, 6);
    ui.stroke(head, th.border, 6);
    if (obj && mat) {
        Rect ic(head.x + 6, head.y + 6, 34, 34);
        ui.fill(ic, th.field, 5);
        drawPartIcon(*findPart("SPHERE"), ic, uiColor(mat->baseColor));
        ui.text(widen(obj->name), Rect(ic.r() + 10, head.y + 4, w - 100, 22), th.text, 13, true);
        std::wstring type = findPart(obj->part) ? L"Primitive  ·  " + widen(obj->part) : (obj->part == "WALL" ? L"Mur" : L"Mesh  ·  " + widen(obj->part));
        if (!m_multi.empty()) type += L"  ·  +" + std::to_wstring(m_multi.size()) + L" sélectionnés";
        ui.text(type, Rect(ic.r() + 10, head.y + 23, w - 60, 18), th.textDim, 11.5f);
    } else {
        ui.icon(0xE8B0, Rect(head.x + 6, head.y, 34, head.h), th.textFaint, 16);
        ui.text(L"Aucun objet sélectionné", Rect(head.x + 46, head.y + 4, w - 56, 22), th.text, 13, true);
        ui.text(L"Cliquez un objet dans la vue ou la hiérarchie", Rect(head.x + 46, head.y + 23, w - 56, 18), th.textDim, 11.5f);
    }
    y += 58;

    if (obj && mat) {
        // ---- Transform
        if (section(x, y, w, L"Transform", 0)) {
            const float lw = 66, gap = 4;
            float fw = (w - lw - 2 * gap) / 3;
            auto vecRow = [&](const char* id, const wchar_t* label, float3& v, float speed, const wchar_t* fmt) {
                ui.text(label, Rect(x, y, lw, 26), th.textDim, 12);
                const wchar_t* ax[3] = {L"X", L"Y", L"Z"};
                const Color* col[3] = {&kAxisX, &kAxisY, &kAxisZ};
                float* comp[3] = {&v.x, &v.y, &v.z};
                bool changed = false;
                for (int a = 0; a < 3; ++a) {
                    char cid[64];
                    sprintf_s(cid, "%s_%d", id, a);
                    Rect f(x + lw + a * (fw + gap), y + 1, fw, 24);
                    if (ui.clicked(f)) pushUndo();
                    changed |= ui.dragFloat(cid, f, *comp[a], speed, fmt, ax[a], col[a]);
                }
                y += 30;
                return changed;
            };
            if (vecRow("tr_pos", L"Position", obj->position, 0.1f, L"%.2f")) markDirty();
            float3 e = eulerYXZ(objRot(*obj));
            if (vecRow("tr_rot", L"Rotation", e, 0.5f, L"%.1f°")) {
                obj->rot = quatYXZ(e);
                obj->useRot = true;
                obj->yaw = radians(e.y);
                markDirty();
            }
            if (vecRow("tr_scl", L"Échelle", obj->scale, 0.01f, L"%.2f")) {
                obj->scale = maxv(obj->scale, float3(0.01f));
                markDirty();
            }
            y += 6;
        }

        // ---- Material
        if (section(x, y, w, L"Matériau", 1)) {
            int preset = mat->preset;
            Color sw = uiColor(mat->baseColor);
            ui.text(L"Préréglage", Rect(x, y, 80, 30), th.textDim, 12);
            if (ui.dropdown("preset", Rect(x + 80, y + 1, w - 80, 28), presetNames(), preset, &sw)) setPreset(preset);
            y += 38;
            // palette
            const int cols = 14;
            float cell = w / cols;
            for (size_t i = 0; i < pal.size(); ++i) {
                Rect s(x + (i % cols) * cell, y + (i / cols) * cell, cell - 3, cell - 3);
                Color c = uiColor(pal[i]);
                if (pal[i].group == ColorGroup::Transparent) {
                    ui.fill(s, Color::hex(0x3A3C42), 3);
                    ui.fill(s, c.withAlpha(0.7f), 3);
                } else if (pal[i].group == ColorGroup::Metallic) {
                    ui.fill(s, c.scaled(0.7f), 3);
                    ui.fill(Rect(s.x, s.y, s.w, s.h * 0.5f), mixc(c, Color::rgbf(1, 1, 1), 0.25f), 3);
                } else {
                    ui.fill(s, c, 3);
                }
                bool selc = mat->colorId == pal[i].id;
                bool h = ui.hover(s);
                if (selc) ui.stroke(s.inset(-2), th.accent, 4, 2);
                else if (h) ui.stroke(s.inset(-1), Color::rgbf(1, 1, 1, 0.8f), 4, 1.2f);
                if (h) {
                    wchar_t tip[96];
                    swprintf(tip, 96, L"%s  (ID %d)", pal[i].name, pal[i].id % 1000);
                    ui.tooltip(tip);
                    if (ui.mousePressed[0]) setColor((int)i);
                }
            }
            y += ((pal.size() + cols - 1) / cols) * cell + 8;
            {
                int ci = mat->colorId >= 0 ? findColor(mat->colorId) : -1;
                bool known = ci >= 0 && pal[ci].id == mat->colorId;
                Color cc = uiColor(mat->baseColor);
                ui.fill(Rect(x, y + 5, 16, 16), cc, 3);
                wchar_t buf[128];
                if (known) swprintf(buf, 128, L"%s   ·   RGB %d, %d, %d", pal[ci].name, pal[ci].r, pal[ci].g, pal[ci].b);
                else swprintf(buf, 128, L"Couleur personnalisée   ·   RGB %d, %d, %d", (int)(cc.r * 255), (int)(cc.g * 255), (int)(cc.b * 255));
                ui.text(buf, Rect(x + 24, y, w - 24, 26), th.textDim, 11.5f);
                y += 32;
            }
            bool ch = false;
            Rect sr(x, y, w, 28);
            auto next = [&]() { y += 30; sr = Rect(x, y, w, 28); };
            ch |= ui.slider("p_rough", sr, L"Rugosité", mat->roughness, 0.0f, 1.0f, L"%.2f", 96); next();
            ch |= ui.slider("p_metal", sr, L"Métal", mat->metallic, 0.0f, 1.0f, L"%.2f", 96); next();
            ch |= ui.slider("p_trans", sr, L"Transmission", mat->transmission, 0.0f, 1.0f, L"%.2f", 96); next();
            ch |= ui.slider("p_sss", sr, L"Subsurface", mat->subsurface, 0.0f, 1.0f, L"%.2f", 96); next();
            float rad = mat->sssRadius.x;
            if (ui.slider("p_sssr", sr, L"Rayon SSS", rad, 0.01f, 5.0f, L"%.2f cm", 96, true)) {
                float k = rad / std::max(mat->sssRadius.x, 1e-4f);
                mat->sssRadius = mat->sssRadius * k;
                ch = true;
            }
            next();
            float bevMm = mat->bevelRadius * 10.0f;
            if (ui.slider("p_bevel", sr, L"Bevel", bevMm, 0.0f, 10.0f, L"%.2f mm", 96)) { mat->bevelRadius = bevMm / 10.0f; ch = true; }
            next();
            ch |= ui.slider("p_ior", sr, L"IOR", mat->ior, 1.0f, 2.5f, L"%.2f", 96); next();
            ch |= ui.slider("p_coat", sr, L"Vernis", mat->coat, 0.0f, 1.0f, L"%.2f", 96); next();
            ch |= ui.slider("p_emit", sr, L"Émission", mat->emissionStrength, 0.0f, 30.0f, L"%.1f", 96); next();
            if (mat->emissionStrength > 0 && mat->emission.x + mat->emission.y + mat->emission.z == 0) mat->emission = mat->baseColor;
            if (ch) markDirty();
            y += 4;
        }

        // ---- Object flags
        if (section(x, y, w, L"Objet", 2)) {
            bool vis = obj->visible;
            if (ui.checkbox(Rect(x, y, w, 26), L"Visible", vis)) { pushUndo(); obj->visible = vis; markDirty(); }
            y += 28;
            bool selb = obj->selectable;
            if (ui.checkbox(Rect(x, y, w, 26), L"Sélectionnable", selb)) obj->selectable = selb;
            y += 34;
            Rect b1(x, y, (w - 8) / 2, 30), b2(x + (w + 8) / 2, y, (w - 8) / 2, 30);
            if (ui.button(b1, L"Dupliquer", false, 0xE8C8)) duplicateSelected();
            if (ui.button(b2, L"Supprimer", false, 0xE74D)) deleteSelected();
            y += 42;
        }
    }
    float contentH = y + m_rightScroll - area.y;
    if (ui.hover(area) && ui.wheel != 0 && !ui.blocked()) m_rightScroll -= ui.wheel * 60;
    m_rightScroll = clampf(m_rightScroll, 0, std::max(0.0f, contentH - area.h));
}

void App::drawRenderTab(Rect area) {
    UI& ui = m_ui;
    const Theme& th = ui.theme;
    float x = area.x, w = area.w;
    float y = area.y - m_rightScroll;
    Camera& cam = m_scene.camera;
    bool ch = false;
    auto row = [&]() { Rect r(x, y, w, 28); y += 30; return r; };

    if (section(x, y, w, L"Moteur de rendu", 3)) {
        static const std::vector<std::wstring> q = {L"Viewport temps réel (raster)", L"Path tracing interactif", L"Path tracing qualité finale", L"GI sondes temps réel (DDGI)"};
        int mode = m_rs.raster ? 0 : (m_rs.ddgi ? 3 : 1 + m_quality);
        if (ui.dropdown("quality", Rect(x, y, w, 28), q, mode)) {
            m_rs.raster = mode == 0;
            m_rs.ddgi = mode == 3;
            if (mode == 1 || mode == 2) m_quality = mode - 1;
            applyQuality();
            ch = true;
        }
        y += 36;
        ch |= ui.sliderInt("r_bounces", row(), L"Rebonds", m_rs.maxBounces, 1, 24);
        float ms = (float)m_rs.maxSamples;
        if (ui.slider("r_maxspp", row(), L"Échant. max", ms, 16.0f, 16384.0f, L"%.0f", 92, true)) m_rs.maxSamples = (int)ms;
        bool dn = m_rs.denoise;
        if (ui.checkbox(row(), L"Débruitage (à-trous)", dn)) { m_rs.denoise = dn; ch = true; }
        ch |= ui.slider("r_dn", row(), L"Force", m_rs.denoiseStrength, 0.1f, 4.0f, L"%.2f");
        ch |= ui.slider("r_clamp", row(), L"Anti-lucioles", m_rs.fireflyClamp, 1.0f, 200.0f, L"%.0f", 92, true);
        y += 6;
    }

    if (section(x, y, w, L"Caméra", 4)) {
        float focal = 12.0f / std::tan(cam.fovY * 0.5f);
        if (ui.slider("c_focal", row(), L"Focale", focal, 12.0f, 200.0f, L"%.0f mm", 92, true)) {
            cam.fovY = 2.0f * std::atan(12.0f / focal);
            cam.focalLengthMm = focal;
            ch = true;
        }
        bool dof = cam.dof;
        if (ui.checkbox(row(), L"Profondeur de champ", dof)) { cam.dof = dof; ch = true; }
        ch |= ui.slider("c_fstop", row(), L"Ouverture f/", cam.fStop, 0.7f, 22.0f, L"%.1f", 92, true);
        bool af = m_autoFocus;
        if (ui.checkbox(row(), L"Mise au point sur le pivot", af)) { m_autoFocus = af; ch = true; }
        if (ui.slider("c_focus", row(), L"Distance MAP", cam.focusDistance, 1.0f, 400.0f, L"%.1f cm", 92, true)) {
            m_autoFocus = false;
            ch = true;
        }
        ui.text(L"Double-clic dans la vue : pivot + mise au point", Rect(x, y, w, 18), th.textFaint, 11);
        y += 26;
    }

    if (section(x, y, w, L"Matières", 5)) {
        bool bev = m_rs.bevel;
        if (ui.checkbox(row(), L"Bevel shader (arêtes arrondies)", bev)) { m_rs.bevel = bev; ch = true; }
        ch |= ui.slider("m_bevs", row(), L"Échelle bevel", m_rs.bevelScale, 0.0f, 6.0f, L"%.2f");
        ch |= ui.sliderInt("m_bevn", row(), L"Éch. bevel", m_rs.bevelSamples, 1, 16);
        ch |= ui.slider("m_sss", row(), L"Échelle SSS", m_rs.sssScale, 0.05f, 10.0f, L"%.2f", 92, true);
        y += 6;
    }

    if (section(x, y, w, L"Lumière & image", 6)) {
        static const std::vector<std::wstring> tm = {L"ACES (filmic, Hill fit)", L"AgX", L"AgX Punchy", L"Standard (sRGB, sans courbe)"};
        ui.text(L"Vue", Rect(x, y, 92, 28), th.textDim, 12);
        if (ui.dropdown("tonemap", Rect(x + 92, y, w - 92, 28), tm, m_rs.tonemapper)) ch = true;
        y += 34;
        float ev = std::log2(std::max(m_rs.exposure, 1e-4f));
        if (ui.slider("i_ev", row(), L"Exposition", ev, -4.0f, 4.0f, L"%+.2f EV")) m_rs.exposure = std::pow(2.0f, ev);
        ch |= ui.slider("i_light", row(), L"Lumières", m_rs.lightScale, 0.0f, 4.0f, L"%.2f");
        ch |= ui.slider("i_env", row(), L"Environnement", m_scene.env.intensity, 0.0f, 4.0f, L"%.2f");
        ui.slider("i_vig", row(), L"Vignettage", m_rs.vignette, 0.0f, 0.6f, L"%.2f");
        y += 6;
    }

    if (section(x, y, w, L"Infos", 7)) {
        wchar_t buf[160];
        std::wstring gpu(m_renderer.adapterName().begin(), m_renderer.adapterName().end());
        auto info = [&](const wchar_t* k, const std::wstring& v) {
            ui.text(k, Rect(x, y, 100, 22), th.textDim, 12);
            ui.text(v, Rect(x + 100, y, w - 100, 22), th.text, 12);
            y += 23;
        };
        info(L"GPU", gpu);
        info(L"API", L"DirectX 12 + DXR 1.1 (inline)");
        swprintf(buf, 160, L"%d × %d px", m_vpPx.w, m_vpPx.h);
        info(L"Résolution", buf);
        swprintf(buf, 160, L"%u / %d", m_renderer.sampleCount(), m_rs.maxSamples);
        info(L"Échantillons", buf);
        swprintf(buf, 160, L"%.2f ms GPU  ·  %d spp/image", m_renderer.gpuFrameMs(), m_rs.samplesPerFrame);
        info(L"Temps", buf);
        if (m_rs.ddgi) {
            swprintf(buf, 160, L"%u sondes  ·  %.2f + %.2f ms", m_renderer.giProbeCount(), m_renderer.giUpdateMs(), m_renderer.giBlendMs());
            info(L"Sondes", buf);
        }
        y += 6;
        if (ui.button(Rect(x, y, (w - 8) / 2, 30), L"Relancer", false, 0xE72C)) ch = true;
        if (ui.button(Rect(x + (w + 8) / 2, y, (w - 8) / 2, 30), L"Capture PNG", false, 0xE722)) saveScreenshot(false);
        y += 42;
    }

    if (ch) markDirty();
    float contentH = y + m_rightScroll - area.y;
    if (ui.hover(area) && ui.wheel != 0 && !ui.blocked()) m_rightScroll -= ui.wheel * 60;
    m_rightScroll = clampf(m_rightScroll, 0, std::max(0.0f, contentH - area.h));
}

// --------------------------------------------------------------------------- content browser / camera views

void App::drawBottomStrip() {
    UI& ui = m_ui;
    const Theme& th = ui.theme;
    Rect p = m_rcBottom;
    float tx = p.x;
    const wchar_t* tabs[2] = {L"Contenu", L"Vues caméra"};
    for (int i = 0; i < 2; ++i) {
        float tw = ui.textWidth(tabs[i], 12.5f, true) + 32;
        if (ui.tab(Rect(tx, p.y, tw, 30), tabs[i], m_bottomTab == i)) m_bottomTab = i;
        tx += tw;
    }
    ui.line(p.x, p.y + 29.5f, p.r(), p.y + 29.5f, th.border);
    Rect area(p.x, p.y + 30, p.w, p.h - 30);
    if (m_bottomTab == 0) drawContentTab(area);
    else drawViewsTab(area);
}

void App::drawContentTab(Rect area) {
    UI& ui = m_ui;
    const Theme& th = ui.theme;
    // folder column
    Rect tree(area.x, area.y, 170, area.h);
    ui.line(tree.r() - 0.5f, tree.y, tree.r() - 0.5f, tree.b(), th.border);
    float ty = tree.y + 8;
    ui.icon(0xE70D, Rect(tree.x + 6, ty, 12, 24), th.textDim, 8);
    ui.icon(0xE8B7, Rect(tree.x + 20, ty, 16, 24), Color::hex(0xC9A227), 12);
    ui.text(L"Contenu", Rect(tree.x + 42, ty, 120, 24), th.text, 12.5f);
    ty += 24;
    Rect cur(tree.x + 1, ty, tree.w - 2, 24);
    ui.fill(cur, th.accentDim);
    ui.icon(0xE8B7, Rect(tree.x + 36, ty, 16, 24), Color::hex(0xC9A227), 12);
    ui.text(L"Primitives", Rect(tree.x + 58, ty, 100, 24), th.text, 12.5f);

    // breadcrumb + search
    Rect top(tree.r() + 10, area.y + 6, area.r() - tree.r() - 20, 28);
    ui.text(L"Contenu", Rect(top.x, top.y, 60, top.h), th.textDim, 12);
    ui.icon(0xE76C, Rect(top.x + 56, top.y, 12, top.h), th.textFaint, 8);
    ui.text(L"Primitives", Rect(top.x + 72, top.y, 100, top.h), th.text, 12);
    static std::wstring contentSearch;
    ui.textField("search_content", Rect(top.r() - 220, top.y, 220, top.h), contentSearch, L"Rechercher un objet…", 0xE721);

    // asset grid
    Rect grid(tree.r() + 10, top.b() + 8, area.r() - tree.r() - 20, area.b() - top.b() - 14);
    float cw = 96, chh = 118, gap = 8;
    int cols = std::max(1, (int)((grid.w + gap) / (cw + gap)));
    std::vector<const PartDef*> list;
    for (const PartDef& d : parts())
        if (containsNoCase(d.name, contentSearch) || containsNoCase(widen(d.id), contentSearch)) list.push_back(&d);
    ui.pushClip(grid);
    for (size_t i = 0; i < list.size(); ++i) {
        const PartDef& d = *list[i];
        Rect cr(grid.x + (i % cols) * (cw + gap), grid.y + (i / cols) * (chh + gap), cw, chh);
        bool h = ui.hover(cr);
        ui.fill(cr, h ? th.cardHover : th.card, 5);
        ui.stroke(cr, h ? th.accent.withAlpha(0.8f) : th.border, 5, h ? 1.5f : 1.0f);
        Rect thumb(cr.x + 4, cr.y + 4, cr.w - 8, cr.w - 8);
        ui.fill(thumb, th.field, 4);
        drawPartIcon(d, thumb.inset(6), uiColor(palette()[findColor(d.defaultColor)]));
        ui.text(d.name, Rect(cr.x + 6, thumb.b() + 2, cr.w - 12, 16), th.text, 11.5f);
        ui.text(L"Primitive", Rect(cr.x + 6, thumb.b() + 17, cr.w - 12, 14), th.textFaint, 10.5f);
        if (h) ui.tooltip(std::wstring(d.name) + L" — clic pour l'ajouter à la scène");
        if (h && ui.mousePressed[0]) addPartFromLibrary(d);
    }
    ui.popClip();
}

void App::drawViewsTab(Rect area) {
    UI& ui = m_ui;
    const Theme& th = ui.theme;
    Rect p = area;
    ui.text(L"Clic droit : orbite  ·  Clic milieu / Maj+clic droit : pan  ·  Molette : zoom  ·  Double-clic : mise au point",
            Rect(p.x + 16, p.y + 4, p.w - 32, 22), th.textFaint, 11, false, Align::Right);
    float cw = 168, chh = 96, gap = 8, y = p.y + 30;
    float x0 = p.x + 40;
    int visible = std::max(1, (int)((p.w - 80 + gap) / (cw + gap)));
    int total = (int)m_views.size() + 1;
    m_viewScroll = std::max(0, std::min(m_viewScroll, total - visible));
    if (ui.iconButton(Rect(p.x + 8, y + 30, 26, 36), 0xE76B, false, 12)) m_viewScroll = std::max(0, m_viewScroll - 1);
    if (ui.iconButton(Rect(p.r() - 34, y + 30, 26, 36), 0xE76C, false, 12)) m_viewScroll = std::min(total - visible, m_viewScroll + 1);
    for (int k = 0; k < visible; ++k) {
        int i = m_viewScroll + k;
        if (i >= total) break;
        Rect c(x0 + k * (cw + gap), y, cw, chh);
        bool h = ui.hover(c);
        if (i == (int)m_views.size()) {
            ui.fill(c, h ? th.cardHover : th.card, 5);
            ui.stroke(c, th.border, 5);
            ui.icon(0xE710, Rect(c.x, c.y + 18, c.w, 32), h ? th.accent : th.textDim, 16);
            ui.text(L"Ajouter la vue", Rect(c.x, c.y + 52, c.w, 20), th.textDim, 11.5f, false, Align::Center);
            if (ui.clicked(c)) {
                ViewBookmark vb;
                vb.name = L"Vue " + std::to_wstring(m_views.size() + 1);
                vb.cam = m_scene.camera;
                std::vector<uint8_t> img;
                uint32_t iw, ih;
                if (m_renderer.readViewport(img, iw, ih)) vb.thumb = makeThumb(img, iw, ih);
                m_views.push_back(vb);
                m_activeView = (int)m_views.size() - 1;
                m_thumbFresh = m_renderer.sampleCount() >= 128;
            }
            continue;
        }
        ViewBookmark& v = m_views[i];
        ui.fill(c, th.card, 5);
        if (v.thumb) ui.image(v.thumb.Get(), c.inset(2));
        else ui.icon(0xE722, c, th.textFaint, 18);
        ui.fill(Rect(c.x + 2, c.b() - 22, c.w - 4, 20), Color::hex(0x000000, 0.5f), 0);
        wchar_t lab[96];
        swprintf(lab, 96, L"%d  %s", i + 1, v.name.c_str());
        ui.text(lab, Rect(c.x + 8, c.b() - 22, c.w - 12, 20), th.text, 11.5f);
        bool active = i == m_activeView;
        if (active) ui.stroke(c, th.accent, 5, 2);
        else if (h) ui.stroke(c, th.textFaint, 5, 1);
        if (ui.clicked(c)) {
            m_scene.camera = v.cam;
            m_activeView = i;
            markDirty();
        }
    }
}

// --------------------------------------------------------------------------- viewport overlay

void App::drawViewportOverlay() {
    UI& ui = m_ui;
    const Theme& th = ui.theme;
    const Rect& v = m_rcView;
    Color glass = Color::hex(0x0E1013, 0.78f);

    drawSelectionOverlay();

    // Top-left bar: projection, render mode
    {
        Rect bar(v.x + 8, v.y + 8, 444, 30);
        static const std::vector<std::wstring> modes = {L"Perspective", L"Face", L"Arrière", L"Gauche", L"Droite", L"Dessus"};
        ui.fill(Rect(bar.x, bar.y, 146, bar.h), glass, 5);
        if (ui.dropdown("proj_mode", Rect(bar.x + 2, bar.y + 2, 142, bar.h - 4), modes, m_viewMode)) setViewAxis(m_viewMode);
        Rect seg(bar.x + 152, bar.y, 230, bar.h);
        ui.fill(seg, glass, 5);
        Rect a(seg.x + 2, seg.y + 2, 112, seg.h - 4), b(seg.x + 116, seg.y + 2, 112, seg.h - 4);
        ui.fill(m_rs.raster ? a : b, th.accent, 4);
        ui.text(L"Viewport  (P)", a, m_rs.raster ? Color::hex(0xFFFFFF) : th.textDim, 12, m_rs.raster, Align::Center);
        ui.text(L"Path tracing", b, !m_rs.raster ? Color::hex(0xFFFFFF) : th.textDim, 12, !m_rs.raster, Align::Center);
        if (ui.clicked(a) && !m_rs.raster) { m_rs.raster = true; markDirty(); }
        if (ui.clicked(b) && m_rs.raster) { m_rs.raster = false; markDirty(); }
        Rect fr(seg.r() + 6, bar.y, 56, bar.h);
        ui.fill(fr, glass, 5);
        if (ui.iconButton(Rect(fr.x + 2, fr.y + 2, 25, fr.h - 4), 0xE890, m_scene.camera.dof, 12, L"Profondeur de champ")) { m_scene.camera.dof = !m_scene.camera.dof; markDirty(); }
        if (ui.iconButton(Rect(fr.x + 29, fr.y + 2, 25, fr.h - 4), 0xE706, m_rs.denoise, 12, L"Débruitage")) { m_rs.denoise = !m_rs.denoise; markDirty(); }
    }

    // Top-right stats
    {
        Rect st(v.r() - 178, v.y + 8, 170, 116);
        ui.fill(st, glass, 5);
        size_t objs = 0;
        for (auto& o : m_scene.objects) if (o.visible && o.part != "WALL" && o.part != "GROUND") ++objs;
        float ly = st.y + 6;
        auto row = [&](const wchar_t* k, const std::wstring& val) {
            ui.text(k, Rect(st.x + 10, ly, 80, 18), th.textDim, 11.5f);
            ui.text(val, Rect(st.x + 80, ly, st.w - 90, 18), th.text, 11.5f, false, Align::Right);
            ly += 17.5f;
        };
        wchar_t b[64];
        swprintf(b, 64, L"%.0f", 1000.0f / std::max(m_frameMs, 0.1f));
        row(L"FPS", b);
        swprintf(b, 64, L"%.1f ms", m_frameMs);
        row(L"Image", b);
        swprintf(b, 64, L"%.2f ms", m_renderer.gpuFrameMs());
        row(L"GPU", b);
        row(L"Triangles", thousands(m_scene.triangleCount()));
        row(L"Objets", std::to_wstring(objs));
        if (m_rs.raster) row(L"Lumières", std::to_wstring(m_scene.lights.size()));
        else {
            swprintf(b, 64, L"%u / %d", std::min<uint32_t>(m_renderer.sampleCount(), m_rs.maxSamples), m_rs.maxSamples);
            row(L"Échant.", b);
        }
    }

    // Bottom-left axis gizmo
    {
        float3 r, u, f;
        m_scene.camera.basis(r, u, f);
        float gx = v.x + 52, gy = v.b() - 52, L = 30;
        ui.circle(gx, gy, 42, Color::hex(0x0E1013, 0.5f));
        struct Ax { float3 d; Color c; const wchar_t* n; int view; };
        Ax axes[3] = {{float3(1, 0, 0), kAxisX, L"X", 4}, {float3(0, 1, 0), kAxisY, L"Y", 5}, {float3(0, 0, 1), kAxisZ, L"Z", 1}};
        int order[3] = {0, 1, 2};
        std::sort(order, order + 3, [&](int a, int c) { return dot(axes[a].d, f) > dot(axes[c].d, f); });
        for (int k = 0; k < 3; ++k) {
            const Ax& a = axes[order[k]];
            float ex = gx + dot(a.d, r) * L, ey = gy - dot(a.d, u) * L;
            float nx = gx - dot(a.d, r) * L * 0.7f, ny = gy + dot(a.d, u) * L * 0.7f;
            ui.circle(nx, ny, 3.5f, a.c.withAlpha(0.45f));
            ui.line(gx, gy, ex, ey, a.c, 2);
            Rect hit(ex - 9, ey - 9, 18, 18);
            bool h = ui.hover(hit);
            ui.circle(ex, ey, h ? 9.5f : 8.0f, a.c);
            ui.text(a.n, hit, Color::hex(0x101010), 10, true, Align::Center);
            if (ui.clicked(hit)) setViewAxis(a.view);
        }
        ui.circle(gx, gy, 2.5f, th.text.withAlpha(0.8f));
    }

    // Bottom-right grid / snapping
    static const std::vector<std::wstring> snaps = {L"Grille : 1 cm", L"Grille : libre"};
    Rect br(v.r() - 150, v.b() - 38, 142, 30);
    ui.fill(br, glass, 5);
    if (ui.dropdown("snap", br.inset(2), snaps, m_snapMode)) m_magnet = m_snapMode == 0;

    // Accumulation progress
    uint32_t spp = m_renderer.sampleCount();
    float prog = clampf((float)spp / m_rs.maxSamples, 0, 1);
    Rect pb(v.cx() - 120, v.b() - 12, m_rs.raster ? 0.0f : 240.0f, 3);
    ui.fill(pb, Color::hex(0x000000, 0.45f), 1.5f);
    ui.fill(Rect(pb.x, pb.y, pb.w * prog, pb.h), prog >= 1 ? kAxisY : th.accent, 1.5f);

    // Selected object label
    if (m_selected >= 0) {
        const Object& o = m_scene.objects[m_selected];
        std::wstring s = widen(o.name) + L"  ·  " + widen(o.part);
        float w = ui.textWidth(s, 12) + 28;
        Rect lr(v.cx() - w * 0.5f, v.b() - 46, w, 26);
        ui.fill(lr, glass, 13);
        ui.stroke(lr, th.accent.withAlpha(0.6f), 13);
        ui.text(s, lr, th.text, 12, true, Align::Center);
    }
}

} // namespace app
