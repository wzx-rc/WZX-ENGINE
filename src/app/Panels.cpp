// Dockable panels of the PC app. Every panel (library, properties, camera views) has a grip bar:
//   click = fold / unfold, click-drag = undock (floating window) and drop on a screen edge to dock it again.
// The "Fenêtres" menu shows or hides each panel. The layout is remembered with the other preferences.
#include "App.h"

namespace app {

static const wchar_t* kPanelTitle[3] = {L"Hiérarchie", L"Inspecteur", L"Contenu"};
static const wchar_t kPanelGlyph[3] = {0xE8FD, 0xE946, 0xE8B7};
static const float kGripH = 26, kStripW = 28, kStripH = 26;
static const float kPanelW[3] = {290, 340, 760};
static const float kPanelFloatH[3] = {640, 700, 230};
static const float kBottomH = 230;
static const float kDockZone = 70;
static const float kStatusH = 24;

void App::resetPanels() {
    for (int i = 0; i < 3; ++i) m_pan[i] = PanelState();
    m_pan[0].dock = 0;
    m_pan[1].dock = 1;
    m_pan[2].dock = 2;
}

void App::togglePanel(int i) {
    m_pan[i].visible = !m_pan[i].visible;
    if (m_pan[i].visible) m_pan[i].collapsed = false;
}

// Computes the frame / grip / content rectangles of the three panels and the viewport rectangle.
void App::layoutPanels(float y0) {
    m_rcStatus = Rect(0, m_H - kStatusH, m_W, kStatusH);
    const float H = m_H - kStatusH;   // panels stop above the status bar
    // who sits in the left column, right column and bottom strip
    int colOcc[2] = {-1, -1};
    for (int i = 0; i < 2; ++i)
        if (m_pan[i].visible && !m_pan[i].floating && colOcc[m_pan[i].dock] < 0) colOcc[m_pan[i].dock] = i;
    bool bottomDocked = m_pan[2].visible && !m_pan[2].floating;
    float leftW = 0, rightW = 0, bottomH = 0;
    if (colOcc[0] >= 0) leftW = m_pan[colOcc[0]].collapsed ? kStripW : kPanelW[colOcc[0]];
    if (colOcc[1] >= 0) rightW = m_pan[colOcc[1]].collapsed ? kStripW : kPanelW[colOcc[1]];
    if (bottomDocked) bottomH = m_pan[2].collapsed ? kStripH : kBottomH;

    for (int i = 0; i < 3; ++i) {
        const PanelState& P = m_pan[i];
        m_panShown[i] = P.visible;
        if (!P.visible) { m_panFrame[i] = m_panGrip[i] = Rect(); continue; }
        Rect f;
        bool strip = false;
        if (P.floating) {
            f = P.fr;
            f.w = kPanelW[i];
            f.h = P.collapsed ? kGripH : kPanelFloatH[i];
            f.x = clampf(f.x, 0, std::max(0.0f, m_W - 80));
            f.y = clampf(f.y, 0, std::max(0.0f, H - 24));
        } else if (i < 2) {
            f = P.dock == 0 ? Rect(0, y0, leftW, H - y0 - bottomH) : Rect(m_W - rightW, y0, rightW, H - y0);
            strip = P.collapsed;
        } else {
            f = Rect(0, H - bottomH, m_W - rightW, bottomH);
            strip = P.collapsed;
        }
        m_panFrame[i] = f;
        if (strip && i < 2) m_panGrip[i] = f;                       // vertical strip: the whole thing is the grip
        else m_panGrip[i] = Rect(f.x, f.y, f.w, kGripH);
    }
    for (int i = 0; i < 3; ++i) {
        Rect c;
        if (m_pan[i].visible && !m_pan[i].collapsed) c = Rect(m_panFrame[i].x, m_panFrame[i].y + kGripH, m_panFrame[i].w, m_panFrame[i].h - kGripH);
        (i == 0 ? m_rcLeft : i == 1 ? m_rcRight : m_rcBottom) = c;
    }
    m_rcView = Rect(leftW + 4, y0 + 4, m_W - leftW - rightW - 8, H - y0 - bottomH - 8);
}

bool App::overPanels(float x, float y) const {
    for (int i = 0; i < 3; ++i)
        if (m_panShown[i] && m_panFrame[i].contains(x, y)) return true;
    return false;
}

bool App::slotFree(int col, int self) const {
    for (int j = 0; j < 2; ++j)
        if (j != self && m_pan[j].visible && !m_pan[j].floating && m_pan[j].dock == col) return false;
    return true;
}

void App::panelInput() {
    UI& ui = m_ui;
    if (m_gripActive < 0) {
        if (!ui.mousePressed[0] || ui.popupOpen()) return;
        for (int i = 0; i < 3; ++i)
            if (m_panShown[i] && m_panGrip[i].contains(ui.mx, ui.my)) {
                m_gripActive = i;
                m_gripMoved = false;
                m_gripSX = ui.mx;
                m_gripSY = ui.my;
                ui.consumeClick();
                return;
            }
        return;
    }
    PanelState& P = m_pan[m_gripActive];
    if (ui.mouseDown[0]) {
        if (!m_gripMoved && std::hypot(ui.mx - m_gripSX, ui.my - m_gripSY) > 6) {
            m_gripMoved = true;
            if (!P.floating) {
                // undock: the window opens under the cursor, unfolded
                P.floating = true;
                P.collapsed = false;
                P.fr = Rect(ui.mx - 60, ui.my - 10, kPanelW[m_gripActive], kPanelFloatH[m_gripActive]);
                m_gripOX = 60;
                m_gripOY = 10;
            } else {
                m_gripOX = m_gripSX - P.fr.x;
                m_gripOY = m_gripSY - P.fr.y;
            }
        }
        if (m_gripMoved) {
            P.fr.x = ui.mx - m_gripOX;
            P.fr.y = ui.my - m_gripOY;
        }
        return;
    }
    // released
    int i = m_gripActive;
    m_gripActive = -1;
    if (!m_gripMoved) { P.collapsed = !P.collapsed; return; }
    if (i < 2) {
        if (ui.mx < kDockZone && slotFree(0, i)) { P.dock = 0; P.floating = false; }
        else if (ui.mx > m_W - kDockZone && slotFree(1, i)) { P.dock = 1; P.floating = false; }
    } else if (ui.my > m_H - kDockZone) {
        P.floating = false;
    }
}

// Highlights where the panel being dragged would dock.
void App::drawPanelDropZones() {
    if (m_gripActive < 0 || !m_gripMoved) return;
    UI& ui = m_ui;
    float y0 = m_rcTool.b();
    Color acc = ui.theme.accent;
    auto zone = [&](Rect r, bool hot) {
        ui.fill(r, acc.withAlpha(hot ? 0.28f : 0.10f), 6);
        ui.stroke(r, acc.withAlpha(hot ? 0.9f : 0.4f), 6, hot ? 2.0f : 1.0f);
    };
    int i = m_gripActive;
    if (i < 2) {
        Rect l(0, y0, kDockZone, m_H - y0), r(m_W - kDockZone, y0, kDockZone, m_H - y0);
        if (slotFree(0, i)) zone(l, l.contains(ui.mx, ui.my));
        if (slotFree(1, i)) zone(r, r.contains(ui.mx, ui.my));
    } else {
        Rect b(0, m_H - kDockZone, m_W, kDockZone);
        zone(b, b.contains(ui.mx, ui.my));
    }
}

void App::drawPanels() {
    UI& ui = m_ui;
    const Theme& th = ui.theme;
    // docked first, floating windows on top (the one being dragged last)
    int order[3] = {0, 1, 2};
    std::stable_sort(order, order + 3, [&](int a, int b) {
        int ka = (m_pan[a].floating ? 1 : 0) + (m_gripActive == a ? 1 : 0);
        int kb = (m_pan[b].floating ? 1 : 0) + (m_gripActive == b ? 1 : 0);
        return ka < kb;
    });
    for (int k = 0; k < 3; ++k) {
        int i = order[k];
        if (!m_panShown[i]) continue;
        const PanelState& P = m_pan[i];
        Rect f = m_panFrame[i], g = m_panGrip[i];
        bool strip = P.collapsed && !P.floating && g.w == f.w && g.h == f.h;
        if (P.floating) {
            ui.fill(Rect(f.x + 4, f.y + 6, f.w, f.h), Color::rgbf(0, 0, 0, 0.4f), 8);
            ui.fill(f, th.panel, 6);
            ui.stroke(f, th.border, 6);
        } else {
            ui.fill(f, th.panel);
            if (i == 0) ui.line(f.r() - 0.5f, f.y, f.r() - 0.5f, f.b(), th.border);
            else if (i == 1) ui.line(f.x + 0.5f, f.y, f.x + 0.5f, f.b(), th.border);
            else ui.line(f.x, f.y + 0.5f, f.r(), f.y + 0.5f, th.border);
        }
        // header bar (grip)
        bool hot = ui.hover(g) || m_gripActive == i;
        ui.fill(g, hot ? th.cardHover : th.panel2, P.floating ? 6 : 0);
        if (strip) {
            ui.icon(kPanelGlyph[i], Rect(g.x, g.y + 8, g.w, 24), hot ? th.text : th.textDim, 13);
            for (int d = 0; d < 4; ++d) ui.circle(g.cx(), g.y + 44 + d * 6, 1.3f, hot ? th.accent : th.textFaint);
        } else {
            ui.line(g.x, g.b() - 0.5f, g.r(), g.b() - 0.5f, th.border);
            ui.icon(kPanelGlyph[i], Rect(g.x + 6, g.y, 22, g.h), hot ? th.accent : th.textDim, 12);
            ui.text(kPanelTitle[i], Rect(g.x + 30, g.y, g.w - 60, g.h), th.text, 12, true);
            // fold indicator
            float cx = g.r() - 14, cy = g.cy();
            if (P.collapsed) { ui.line(cx - 4, cy - 2, cx, cy + 2, th.textDim, 1.4f); ui.line(cx, cy + 2, cx + 4, cy - 2, th.textDim, 1.4f); }
            else { ui.line(cx - 4, cy + 2, cx, cy - 2, th.textDim, 1.4f); ui.line(cx, cy - 2, cx + 4, cy + 2, th.textDim, 1.4f); }
        }
        if (hot && !m_gripMoved) ui.tooltip(L"Clic : réduire / ouvrir  ·  Glisser : détacher et déplacer (bord de l'écran : ancrer)");
        if (P.collapsed) continue;
        Rect c(f.x, f.y + kGripH, f.w, f.h - kGripH);
        ui.pushClip(c);
        if (i == 0) drawLeftPanel();
        else if (i == 1) drawRightPanel();
        else drawBottomStrip();
        ui.popClip();
    }
    drawPanelDropZones();
}

} // namespace app

