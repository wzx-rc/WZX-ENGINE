#include "NodeEditor.h"
#include <algorithm>
#include <cmath>
#include <windows.h>

namespace game {

using namespace vp3d;

static const float kHeader = 26, kRow = 24, kPad = 8;

static std::wstring widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

static std::string narrow(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

// Lower case without accents, for the search box.
static std::wstring fold(const std::wstring& s) {
    std::wstring o;
    for (wchar_t c : s) {
        wchar_t l = (wchar_t)towlower(c);
        switch (l) {
        case L'é': case L'è': case L'ê': case L'ë': l = L'e'; break;
        case L'à': case L'â': case L'ä': l = L'a'; break;
        case L'î': case L'ï': l = L'i'; break;
        case L'ô': case L'ö': l = L'o'; break;
        case L'ù': case L'û': case L'ü': l = L'u'; break;
        case L'ç': l = L'c'; break;
        default: break;
        }
        o.push_back(l);
    }
    return o;
}

static Color pinColor(PinType t) {
    switch (t) {
    case PinType::Exec: return Color::hex(0xF2F2F2);
    case PinType::Float: return Color::hex(0x7ED957);
    case PinType::Bool: return Color::hex(0xE05A5A);
    case PinType::Vec3: return Color::hex(0xF2C744);
    case PinType::Object: return Color::hex(0x4FA3F7);
    case PinType::String: return Color::hex(0xE36FD0);
    }
    return Color::hex(0xFFFFFF);
}

static Color categoryColor(const std::string& c) {
    if (c == "Événements") return Color::hex(0x8E2F2F);
    if (c == "Flux") return Color::hex(0x4A505A);
    if (c == "Actions") return Color::hex(0x255294);
    if (c == "Données") return Color::hex(0x2F6E42);
    if (c == "Maths") return Color::hex(0x2B6668);
    if (c == "RTS") return Color::hex(0x91601F);
    return Color::hex(0x444444);
}

static bool hasVec3Input(const Node& n) {
    for (const PinDef& p : n.def->ins) if (p.type == PinType::Vec3) return true;
    return false;
}

// --------------------------------------------------------------------------- layout
// Rows: outputs on the right (row = output index); the exec input sits on row 0 on the
// left; data inputs follow below the outputs, one per row, with their inline value.

static int dataInputRow(const Node& n, int pin) {
    int base = std::max((int)n.def->outs.size(), (!n.def->ins.empty() && n.def->ins[0].type == PinType::Exec) ? 1 : 0);
    int k = 0;
    for (int i = 0; i < pin; ++i) if (n.def->ins[i].type != PinType::Exec) ++k;
    return base + k;
}

float NodeEditor::nodeWidth(const Node& n) const { return hasVec3Input(n) ? 236.0f : 196.0f; }

int NodeEditor::rowCount(const Node& n) const {
    int base = std::max((int)n.def->outs.size(), (!n.def->ins.empty() && n.def->ins[0].type == PinType::Exec) ? 1 : 0);
    int k = 0;
    for (const PinDef& p : n.def->ins) if (p.type != PinType::Exec) ++k;
    return std::max(1, base + k);
}

Rect NodeEditor::nodeRect(const Node& n) const {
    float2 p = toScreen(n.pos);
    return Rect(p.x, p.y, nodeWidth(n) * m_zoom, (kHeader + rowCount(n) * kRow + kPad) * m_zoom);
}

float2 NodeEditor::pinPos(const Node& n, int pin, bool output) const {
    Rect r = nodeRect(n);
    int row = output ? pin : (n.def->ins[pin].type == PinType::Exec ? 0 : dataInputRow(n, pin));
    float y = r.y + (kHeader + row * kRow + kRow * 0.5f) * m_zoom;
    return float2(output ? r.r() : r.x, y);
}

bool NodeEditor::pinVisible(const Node&, int, bool) const { return true; }

// --------------------------------------------------------------------------- hit tests

NodeEditor::PinRef NodeEditor::hitPin(const Graph& g, float mx, float my) const {
    float rad = std::max(8.0f * m_zoom, 6.0f);
    for (auto it = g.nodes.rbegin(); it != g.nodes.rend(); ++it) {
        const Node& n = *it;
        for (int o = 0; o < 2; ++o) {
            const auto& pins = o ? n.def->outs : n.def->ins;
            for (int i = 0; i < (int)pins.size(); ++i) {
                float2 p = pinPos(n, i, o == 1);
                // the label next to the pin is part of the target too
                float lx0 = o ? p.x - 70 * m_zoom : p.x - rad, lx1 = o ? p.x + rad : p.x + 70 * m_zoom;
                bool onLabel = mx >= lx0 && mx <= lx1 && std::fabs(my - p.y) < 9 * m_zoom;
                bool onDot = (mx - p.x) * (mx - p.x) + (my - p.y) * (my - p.y) < rad * rad;
                // inputs with an inline value: only the dot (the label area hosts the widget)
                if (!o && pins[i].type != PinType::Exec && pins[i].type != PinType::Object) onLabel = false;
                if (onDot || onLabel) return {n.id, i, o == 1};
            }
        }
    }
    return {};
}

int NodeEditor::hitNode(const Graph& g, float mx, float my) const {
    for (auto it = g.nodes.rbegin(); it != g.nodes.rend(); ++it)
        if (nodeRect(*it).contains(mx, my)) return it->id;
    return 0;
}

bool NodeEditor::selected(int id) const { return std::find(m_sel.begin(), m_sel.end(), id) != m_sel.end(); }

// --------------------------------------------------------------------------- undo / edit helpers

void NodeEditor::pushUndo(const Graph& g) {
    m_undo.push_back(g.toJson());
    if (m_undo.size() > 100) m_undo.erase(m_undo.begin());
    m_modified = true;
}

void NodeEditor::undo(Graph& g) {
    if (m_undo.empty()) return;
    g.fromJson(m_undo.back());
    m_undo.pop_back();
    m_sel.clear();
    m_modified = true;
}

void NodeEditor::deleteSelection(Graph& g) {
    if (m_sel.empty()) return;
    pushUndo(g);
    for (int id : m_sel) g.removeNode(id);
    m_sel.clear();
}

void NodeEditor::duplicateSelection(Graph& g) {
    if (m_sel.empty()) return;
    pushUndo(g);
    std::map<int, int> remap;
    for (int id : m_sel) {
        const Node* n = g.node(id);
        if (!n) continue;
        Node copy = *n;
        int nid = g.addNode(copy.type, float2(copy.pos.x + 30, copy.pos.y + 30));
        g.node(nid)->inputs = copy.inputs;
        remap[id] = nid;
    }
    std::vector<Link> links = g.links;
    for (const Link& l : links)
        if (remap.count(l.fromNode) && remap.count(l.toNode)) g.connect(remap[l.fromNode], l.fromPin, remap[l.toNode], l.toPin);
    m_sel.clear();
    for (auto& kv : remap) m_sel.push_back(kv.second);
}

void NodeEditor::doFrame(const Graph& g) {
    if (g.nodes.empty() || m_area.w <= 0) { resetView(); return; }
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (const Node& n : g.nodes) {
        x0 = std::min(x0, n.pos.x);
        y0 = std::min(y0, n.pos.y);
        x1 = std::max(x1, n.pos.x + nodeWidth(n));
        y1 = std::max(y1, n.pos.y + kHeader + rowCount(n) * kRow + kPad);
    }
    float aw = m_area.w - 80, ah = m_area.h - 90;
    // stay readable: below 0.6 the graph is shown from its top-left corner instead of shrunk further
    m_zoom = clampf(std::min(aw / std::max(x1 - x0, 1.0f), ah / std::max(y1 - y0, 1.0f)), 0.6f, 1.2f);
    float px = (x0 + x1) * 0.5f - m_area.w * 0.5f / m_zoom, py = (y0 + y1) * 0.5f - (m_area.h * 0.5f + 18) / m_zoom;
    if ((x1 - x0) * m_zoom > aw) px = x0 - 40 / m_zoom;
    if ((y1 - y0) * m_zoom > ah) py = y0 - 56 / m_zoom;
    m_pan = float2(px, py);
}

void NodeEditor::openMenu(float2 screen, PinRef from) {
    m_menu = true;
    m_menuScreen = screen;
    m_menuGraph = toGraph(screen);
    m_menuFrom = from;
    m_search.clear();
    m_menuScroll = 0;
}

// --------------------------------------------------------------------------- drawing

void NodeEditor::draw(UI& ui, const Rect& area, Graph& g, const Runtime* rt, bool playing) {
    m_area = area;
    m_overWidget = false;
    for (Node& n : g.nodes) if (!n.def) n.def = findNodeDef(n.type);
    const Theme& th = ui.theme;

    ui.pushClip(area);
    ui.fill(area, Color::hex(0x121417));
    // grid
    float step = 16 * m_zoom;
    if (step > 5) {
        float ox = std::fmod(-m_pan.x * m_zoom, step), oy = std::fmod(-m_pan.y * m_zoom, step);
        int ix = (int)std::floor(m_pan.x / 16), iy = (int)std::floor(m_pan.y / 16);
        int k = 0;
        for (float x = area.x + ox; x < area.r(); x += step, ++k)
            ui.line(x, area.y, x, area.b(), ((ix + k + 1) % 8 == 0) ? Color::hex(0x23272D) : Color::hex(0x181B1F));
        k = 0;
        for (float y = area.y + oy; y < area.b(); y += step, ++k)
            ui.line(area.x, y, area.r(), y, ((iy + k + 1) % 8 == 0) ? Color::hex(0x23272D) : Color::hex(0x181B1F));
    }
    drawLinks(ui, g, rt);
    for (Node& n : g.nodes) drawNode(ui, g, n, rt);

    // link being dragged
    if (m_drag == DragLink) {
        const Node* n = g.node(m_linkFrom.node);
        if (n) {
            float2 a = pinPos(*n, m_linkFrom.pin, m_linkFrom.output);
            float2 b(ui.mx, ui.my);
            if (!m_linkFrom.output) std::swap(a, b);
            PinType t = m_linkFrom.output ? n->def->outs[m_linkFrom.pin].type : n->def->ins[m_linkFrom.pin].type;
            float dx = std::max(std::fabs(b.x - a.x) * 0.5f, 40 * m_zoom);
            float2 prev = a;
            for (int i = 1; i <= 24; ++i) {
                float t2 = i / 24.0f, u = 1 - t2;
                float2 p(u * u * u * a.x + 3 * u * u * t2 * (a.x + dx) + 3 * u * t2 * t2 * (b.x - dx) + t2 * t2 * t2 * b.x,
                         u * u * u * a.y + 3 * u * u * t2 * a.y + 3 * u * t2 * t2 * b.y + t2 * t2 * t2 * b.y);
                ui.line(prev.x, prev.y, p.x, p.y, pinColor(t), 2.0f * m_zoom);
                prev = p;
            }
        }
    }
    if (m_drag == DragMarquee) {
        Rect mr(std::min(m_pressPos.x, ui.mx), std::min(m_pressPos.y, ui.my), std::fabs(ui.mx - m_pressPos.x), std::fabs(ui.my - m_pressPos.y));
        ui.fill(mr, th.accent.withAlpha(0.12f));
        ui.stroke(mr, th.accent.withAlpha(0.7f));
    }
    if (g.nodes.empty())
        ui.text(L"Graphe vide : clic droit pour ajouter un noeud (ex. « Clic sur un objet »).",
                Rect(area.x, area.cy() - 12, area.w, 24), th.textDim, 14, false, Align::Center);
    drawLog(ui, rt);
    drawToolbar(ui, playing);
    if (m_wantFrame) { doFrame(g); m_wantFrame = false; }
    bool menuWasOpen = m_menu;
    if (m_menu) drawMenu(ui, g);
    if (!menuWasOpen) handleInput(ui, g);
    ui.popClip();
}

void NodeEditor::drawLinks(UI& ui, Graph& g, const Runtime* rt) {
    for (const Link& l : g.links) {
        const Node* a = g.node(l.fromNode);
        const Node* b = g.node(l.toNode);
        if (!a || !b) continue;
        float2 p0 = pinPos(*a, l.fromPin, true), p1 = pinPos(*b, l.toPin, false);
        PinType t = a->def->outs[l.fromPin].type;
        Color c = pinColor(t);
        float w = (t == PinType::Exec ? 2.6f : 2.0f) * m_zoom;
        if (rt && t == PinType::Exec && rt->running() && rt->time() - rt->lastRun(b->id) < 0.25f) { c = Color::hex(0xFFD24A); w *= 1.6f; }
        float dx = std::max(std::fabs(p1.x - p0.x) * 0.5f, 40 * m_zoom);
        float2 prev = p0;
        for (int i = 1; i <= 24; ++i) {
            float t2 = i / 24.0f, u = 1 - t2;
            float2 p(u * u * u * p0.x + 3 * u * u * t2 * (p0.x + dx) + 3 * u * t2 * t2 * (p1.x - dx) + t2 * t2 * t2 * p1.x,
                     u * u * u * p0.y + 3 * u * u * t2 * p0.y + 3 * u * t2 * t2 * p1.y + t2 * t2 * t2 * p1.y);
            ui.line(prev.x, prev.y, p.x, p.y, c, w);
            prev = p;
        }
    }
}

static void drawPinShape(UI& ui, float2 p, PinType t, bool connected, float z) {
    Color c = pinColor(t);
    if (t == PinType::Exec) {
        float s = 5.5f * z;
        float2 pts[5] = {{p.x - s, p.y - s}, {p.x + s * 0.2f, p.y - s}, {p.x + s, p.y}, {p.x + s * 0.2f, p.y + s}, {p.x - s, p.y + s}};
        ui.polygon(pts, 5, c, connected, 1.4f * z);
    } else {
        ui.circle(p.x, p.y, 4.8f * z, c, false, 1.6f * z);
        if (connected) ui.circle(p.x, p.y, 2.8f * z, c, true);
    }
}

void NodeEditor::drawNode(UI& ui, Graph& g, Node& n, const Runtime* rt) {
    const Theme& th = ui.theme;
    const float z = m_zoom;
    Rect r = nodeRect(n);
    if (r.r() < m_area.x || r.x > m_area.r() || r.b() < m_area.y || r.y > m_area.b()) return;
    bool sel = selected(n.id);
    bool hot = rt && rt->running() && rt->time() - rt->lastRun(n.id) < 0.25f;
    ui.fill(Rect(r.x + 3 * z, r.y + 4 * z, r.w, r.h), Color::hex(0x000000, 0.35f), 7 * z);
    ui.fill(r, Color::hex(0x1C1F24, 0.97f), 7 * z);
    Color hc = categoryColor(n.def->category);
    Rect hr(r.x, r.y, r.w, kHeader * z);
    ui.fill(hr, n.def->pure ? hc.scaled(0.8f) : hc, 7 * z);
    ui.fill(Rect(hr.x, hr.b() - 7 * z, hr.w, 7 * z), n.def->pure ? hc.scaled(0.8f) : hc);
    if (n.def->event) ui.icon(0xE945, Rect(hr.x + 6 * z, hr.y, 16 * z, hr.h), Color::hex(0xFFFFFF, 0.9f), 11 * z);
    float tx = hr.x + (n.def->event ? 24 : 9) * z;
    ui.text(widen(n.def->label), Rect(tx, hr.y, hr.r() - tx - 6 * z, hr.h), Color::hex(0xFFFFFF), 12.5f * z, true);
    if (ui.hover(hr) && !m_menu) ui.tooltip(widen(n.def->help));
    ui.stroke(r, hot ? Color::hex(0xFFD24A) : (sel ? th.accent : th.border), 7 * z, hot || sel ? 2.0f : 1.0f);

    // outputs
    for (int i = 0; i < (int)n.def->outs.size(); ++i) {
        const PinDef& p = n.def->outs[i];
        float2 pp = pinPos(n, i, true);
        bool conn = false;
        for (const Link& l : g.links) if (l.fromNode == n.id && l.fromPin == i) { conn = true; break; }
        drawPinShape(ui, pp, p.type, conn, z);
        if (!p.name.empty())
            ui.text(widen(p.name), Rect(r.x + 8 * z, pp.y - 9 * z, r.w - 22 * z, 18 * z), th.text, 11.5f * z, false, Align::Right);
    }
    // inputs
    bool widgets = z >= 0.75f;
    for (int i = 0; i < (int)n.def->ins.size(); ++i) {
        const PinDef& p = n.def->ins[i];
        float2 pp = pinPos(n, i, false);
        bool conn = g.inputLink(n.id, i) != nullptr;
        drawPinShape(ui, pp, p.type, conn, z);
        if (p.type == PinType::Exec) continue;
        float labelW = p.type == PinType::Vec3 ? 70.0f : 88.0f;
        ui.text(widen(p.name), Rect(r.x + 14 * z, pp.y - 9 * z, labelW * z, 18 * z), th.textDim, 11.5f * z);
        if (conn || p.type == PinType::Object) continue;
        Value& v = n.inputs[i];
        float wx = r.x + (14 + labelW) * z, wr = r.r() - 10 * z;
        float wh = 19 * z;
        Rect wrc(wx, pp.y - wh * 0.5f, wr - wx, wh);
        if (!widgets) {
            ui.text(widen(v.asString()), wrc, th.textFaint, 11 * z);
            continue;
        }
        char id[64];
        switch (p.type) {
        case PinType::Float: {
            snprintf(id, sizeof id, "ng_%d_%d", n.id, i);
            Rect fr(wr - 64 * z, wrc.y, 64 * z, wh);
            if (ui.hover(fr)) m_overWidget = true;
            if (ui.dragFloat(id, fr, v.f, std::max(0.02f, std::fabs(v.f) * 0.01f), L"%.2f")) m_modified = true;
            break;
        }
        case PinType::Bool: {
            Rect br(wr - 16 * z, pp.y - 8 * z, 16 * z, 16 * z);
            ui.fill(br, th.field, 3 * z);
            ui.stroke(br, ui.hover(br) ? th.accent : th.border, 3 * z);
            if (v.b) ui.icon(0xE73E, br, th.accent, 11 * z);
            if (ui.hover(br)) m_overWidget = true;
            if (ui.clicked(br) && !m_menu) { pushUndo(g); v.b = !v.b; }
            break;
        }
        case PinType::Vec3: {
            static const wchar_t* ax[3] = {L"X", L"Y", L"Z"};
            static const Color axc[3] = {Color::hex(0xE5534B), Color::hex(0x57C35B), Color::hex(0x4C8DFF)};
            float cw = (wr - wx - 4 * z) / 3;
            for (int k = 0; k < 3; ++k) {
                snprintf(id, sizeof id, "ng_%d_%d_%d", n.id, i, k);
                Rect fr(wx + k * (cw + 2 * z), wrc.y, cw, wh);
                if (ui.hover(fr)) m_overWidget = true;
                float old = v.v[k];
                ui.dragFloat(id, fr, v.v[k], std::max(0.05f, std::fabs(v.v[k]) * 0.01f), L"%.1f", z >= 0.95f ? ax[k] : nullptr, &axc[k]);
                if (old != v.v[k]) m_modified = true;
            }
            break;
        }
        case PinType::String: {
            snprintf(id, sizeof id, "ng_%d_%d", n.id, i);
            if (ui.hover(wrc)) m_overWidget = true;
            std::wstring w = widen(v.s);
            if (ui.textField(id, wrc, w, L"")) { v.s = narrow(w); m_modified = true; }
            break;
        }
        default: break;
        }
    }
}

void NodeEditor::drawToolbar(UI& ui, bool playing) {
    const Theme& th = ui.theme;
    Rect bar(m_area.x, m_area.y, m_area.w, 36);
    ui.fill(bar, th.panel2.withAlpha(0.96f));
    ui.line(bar.x, bar.b() - 0.5f, bar.r(), bar.b() - 0.5f, th.border);
    ui.icon(0xE8A1, Rect(bar.x + 10, bar.y, 20, bar.h), th.accent, 15);
    ui.text(L"Graphe du jeu", Rect(bar.x + 34, bar.y, 140, bar.h), th.text, 13, true);
    float x = bar.x + 160;
    if (ui.button(Rect(x, bar.y + 5, 96, 26), playing ? L"Stop" : L"Jouer", playing, playing ? 0xE71A : 0xE768)) m_action = TogglePlay;
    x += 102;
    if (ui.button(Rect(x, bar.y + 5, 112, 26), L"Enregistrer", false, 0xE74E)) m_action = Save;
    x += 118;
    if (ui.button(Rect(x, bar.y + 5, 96, 26), L"Build", false, 0xE7B8)) m_action = Build;
    x += 102;
    if (ui.button(Rect(x, bar.y + 5, 100, 26), L"Recadrer", false, 0xE9A6)) m_wantFrame = true;
    x += 106;
    std::wstring hint = status.empty() ? L"Clic droit : ajouter  ·  Glisser une broche : relier  ·  Alt+clic : délier  ·  Suppr  ·  Ctrl+D  ·  Ctrl+Z"
                                       : widen(status);
    ui.text(hint, Rect(x + 8, bar.y, bar.r() - x - 130, bar.h), th.textDim, 11.5f);
    if (ui.button(Rect(bar.r() - 112, bar.y + 5, 104, 26), L"Fermer (F3)", false, 0xE711)) m_action = Close;
}

void NodeEditor::drawLog(UI& ui, const Runtime* rt) {
    if (!rt || rt->log.empty()) return;
    const Theme& th = ui.theme;
    int lines = std::min<int>(6, (int)rt->log.size());
    std::wstring vars;
    for (auto& kv : rt->variables()) vars += widen(kv.first) + L" = " + widen(kv.second.asString()) + L"   ";
    float h = lines * 17.0f + 14 + (vars.empty() ? 0 : 18);
    Rect r(m_area.x + 10, m_area.b() - h - 10, std::min(520.0f, m_area.w - 20), h);
    ui.fill(r, Color::hex(0x0B0C0E, 0.85f), 6);
    ui.stroke(r, th.border, 6);
    float y = r.y + 7;
    for (int i = (int)rt->log.size() - lines; i < (int)rt->log.size(); ++i, y += 17) {
        const std::string& s = rt->log[i];
        bool err = s.rfind("Erreur", 0) == 0;
        ui.text(widen(s), Rect(r.x + 10, y, r.w - 20, 17), err ? Color::hex(0xE5534B) : th.textDim, 11.5f);
    }
    if (!vars.empty()) ui.text(vars, Rect(r.x + 10, y + 1, r.w - 20, 17), th.accent, 11.5f);
}

void NodeEditor::drawMenu(UI& ui, Graph& g) {
    const Theme& th = ui.theme;
    // typing filters the list (the search box is not a text field: no click needed)
    for (int k = 'A'; k <= 'Z'; ++k) if (ui.keyPressed(k)) m_search.push_back((wchar_t)(k + 32));
    for (int k = '0'; k <= '9'; ++k) if (ui.keyPressed(k)) m_search.push_back((wchar_t)k);
    if (ui.keyPressed(VK_SPACE)) m_search.push_back(L' ');
    if (ui.keyPressed(VK_BACK) && !m_search.empty()) m_search.pop_back();
    if (ui.keyPressed(VK_ESCAPE)) { m_menu = false; return; }

    // candidate list
    const Node* fromNode = m_menuFrom.pin >= 0 ? g.node(m_menuFrom.node) : nullptr;
    PinType fromType = PinType::Exec;
    if (fromNode) fromType = m_menuFrom.output ? fromNode->def->outs[m_menuFrom.pin].type : fromNode->def->ins[m_menuFrom.pin].type;
    auto matchPin = [&](const NodeDef& d) -> int {
        if (!fromNode) return -1;
        const auto& pins = m_menuFrom.output ? d.ins : d.outs;
        for (int i = 0; i < (int)pins.size(); ++i)
            if (m_menuFrom.output ? Graph::compatible(fromType, pins[i].type) : Graph::compatible(pins[i].type, fromType)) return i;
        return -2;
    };
    std::wstring q = fold(m_search);
    struct Item { const NodeDef* d; std::wstring header; };
    std::vector<Item> items;
    std::string lastCat;
    for (const NodeDef& d : nodeDefs()) {
        if (fromNode && matchPin(d) == -2) continue;
        if (!q.empty() && fold(widen(d.label)).find(q) == std::wstring::npos && fold(widen(d.id)).find(q) == std::wstring::npos) continue;
        if (q.empty() && d.category != lastCat) { items.push_back({nullptr, widen(d.category)}); lastCat = d.category; }
        items.push_back({&d, L""});
    }

    const float rowH = 22, w = 270, h = 400;
    Rect r(m_menuScreen.x, m_menuScreen.y, w, h);
    if (r.r() > m_area.r() - 6) r.x = m_area.r() - 6 - w;
    if (r.b() > m_area.b() - 6) r.y = std::max(m_area.y + 40, m_area.b() - 6 - h);
    ui.fill(Rect(r.x + 3, r.y + 5, r.w, r.h), Color::hex(0x000000, 0.45f), 8);
    ui.fill(r, Color::hex(0x1A1D22), 8);
    ui.stroke(r, th.border, 8);
    Rect sr(r.x + 8, r.y + 8, r.w - 16, 26);
    ui.fill(sr, th.field, 5);
    ui.stroke(sr, th.accent.withAlpha(0.7f), 5);
    ui.icon(0xE721, Rect(sr.x + 4, sr.y, 20, sr.h), th.textDim, 12);
    std::wstring shown = m_search.empty() ? (fromNode ? L"Noeuds compatibles… tapez pour filtrer" : L"Tapez pour chercher un noeud…") : m_search;
    ui.text(shown, Rect(sr.x + 28, sr.y, sr.w - 32, sr.h), m_search.empty() ? th.textFaint : th.text, 12.5f);

    Rect lr(r.x + 4, sr.b() + 6, r.w - 8, r.b() - sr.b() - 10);
    float total = items.size() * rowH;
    if (ui.hover(r) && ui.wheel != 0) m_menuScroll -= ui.wheel * rowH * 3;
    m_menuScroll = clampf(m_menuScroll, 0, std::max(0.0f, total - lr.h));
    ui.pushClip(lr);
    int firstItem = -1;
    for (size_t i = 0; i < items.size(); ++i) {
        Rect ir(lr.x, lr.y + i * rowH - m_menuScroll, lr.w, rowH);
        if (ir.b() < lr.y || ir.y > lr.b()) continue;
        if (!items[i].d) {
            ui.text(items[i].header, ir.inset(8, 0), categoryColor(narrow(items[i].header)).scaled(1.8f), 11, true);
            continue;
        }
        if (firstItem < 0) firstItem = (int)i;
        bool hov = ui.hover(ir) && lr.contains(ui.mx, ui.my);
        if (hov) { ui.fill(ir, th.accentDim, 4); ui.tooltip(widen(items[i].d->help)); }
        ui.circle(ir.x + 14, ir.cy(), 3.5f, categoryColor(items[i].d->category).scaled(1.6f));
        ui.text(widen(items[i].d->label), Rect(ir.x + 26, ir.y, ir.w - 30, ir.h), th.text, 12.5f);
        if (hov && ui.mousePressed[0]) { firstItem = (int)i; m_search = L"\x01"; }   // marker: picked
    }
    ui.popClip();
    if (items.empty()) ui.text(L"Aucun noeud", lr.inset(10, 4), th.textFaint, 12);

    bool pick = (m_search == L"\x01") || (ui.keyPressed(VK_RETURN) && firstItem >= 0);
    if (pick && firstItem >= 0) {
        const NodeDef* d = items[firstItem].d;
        pushUndo(g);
        int id = g.addNode(d->id, float2(std::round(m_menuGraph.x / 8) * 8, std::round(m_menuGraph.y / 8) * 8));
        if (fromNode && id) {
            int p = matchPin(*d);
            if (p >= 0) {
                std::string err;
                if (m_menuFrom.output) g.connect(m_menuFrom.node, m_menuFrom.pin, id, p, &err);
                else g.connect(id, p, m_menuFrom.node, m_menuFrom.pin, &err);
            }
        }
        m_sel = {id};
        m_menu = false;
        ui.consumeClick();
        return;
    }
    if (m_search == L"\x01") m_search.clear();
    if ((ui.mousePressed[0] || ui.mousePressed[1] || ui.mousePressed[2]) && !r.contains(ui.mx, ui.my)) {
        m_menu = false;
        ui.consumeClick();
    }
}

void NodeEditor::handleInput(UI& ui, Graph& g) {
    float2 mouse(ui.mx, ui.my);
    Rect body(m_area.x, m_area.y + 36, m_area.w, m_area.h - 36);
    bool inArea = body.contains(ui.mx, ui.my) && !ui.popupOpen();
    bool ctrl = ui.keyDown(VK_CONTROL), shift = ui.keyDown(VK_SHIFT), alt = ui.keyDown(VK_MENU);
    float2 delta = mouse - m_lastMouse;
    m_lastMouse = mouse;

    // zoom around the mouse
    if (inArea && ui.wheel != 0) {
        float2 gp = toGraph(mouse);
        m_zoom = clampf(m_zoom * std::pow(1.12f, ui.wheel), 0.35f, 1.8f);
        m_pan = float2(gp.x - (mouse.x - m_area.x) / m_zoom, gp.y - (mouse.y - m_area.y) / m_zoom);
    }

    // keyboard
    if (!ui.textFocused()) {
        if (ui.keyPressed(VK_DELETE)) deleteSelection(g);
        if (ctrl && ui.keyPressed('D')) duplicateSelection(g);
        if (ctrl && ui.keyPressed('Z')) undo(g);
        if (ctrl && ui.keyPressed('A')) { m_sel.clear(); for (const Node& n : g.nodes) m_sel.push_back(n.id); }
        if (ui.keyPressed(VK_ESCAPE)) m_sel.clear();
        if (!ctrl && ui.keyPressed('F')) frameAll(g);
    }

    // presses
    if (inArea && ui.mousePressed[0] && m_drag == DragNone) {
        m_pressPos = mouse;
        m_dragMoved = false;
        if (m_overWidget) {
            pushUndo(g);   // value edit in an inline widget
        } else {
            PinRef p = hitPin(g, ui.mx, ui.my);
            if (p.pin >= 0) {
                const Node* n = g.node(p.node);
                if (alt) {
                    pushUndo(g);
                    g.disconnectPin(p.node, p.pin, p.output);
                } else if (!p.output && n->def->ins[p.pin].type != PinType::Exec && g.inputLink(p.node, p.pin)) {
                    // pick up an existing data link from its input end
                    const Link* l = g.inputLink(p.node, p.pin);
                    PinRef src{l->fromNode, l->fromPin, true};
                    pushUndo(g);
                    g.disconnectPin(p.node, p.pin, false);
                    m_linkFrom = src;
                    m_drag = DragLink;
                } else {
                    m_linkFrom = p;
                    m_drag = DragLink;
                }
            } else if (int id = hitNode(g, ui.mx, ui.my)) {
                if (ctrl || shift) {
                    auto it = std::find(m_sel.begin(), m_sel.end(), id);
                    if (it != m_sel.end()) m_sel.erase(it); else m_sel.push_back(id);
                } else if (!selected(id)) {
                    m_sel = {id};
                }
                // bring to front
                auto it = std::find_if(g.nodes.begin(), g.nodes.end(), [&](const Node& n) { return n.id == id; });
                if (it != g.nodes.end() && it + 1 != g.nodes.end()) { Node n = *it; g.nodes.erase(it); g.nodes.push_back(n); }
                if (selected(id)) m_drag = DragNodes;
            } else {
                if (!ctrl && !shift) m_sel.clear();
                m_drag = DragMarquee;
            }
        }
    }
    if (inArea && (ui.mousePressed[1] || ui.mousePressed[2]) && m_drag == DragNone) {
        m_drag = DragPan;
        m_rightPressed = ui.mousePressed[1] ? 1 : 2;
        m_rightPos = mouse;
        m_dragMoved = false;
    }

    // drags
    switch (m_drag) {
    case DragNodes:
        if (!ui.mouseDown[0]) {
            for (int id : m_sel)
                if (Node* n = g.node(id)) n->pos = float2(std::round(n->pos.x / 8) * 8, std::round(n->pos.y / 8) * 8);
            m_drag = DragNone;
        } else if (delta.x != 0 || delta.y != 0) {
            if (!m_dragMoved) { pushUndo(g); m_dragMoved = true; }
            for (int id : m_sel)
                if (Node* n = g.node(id)) n->pos = n->pos + delta * (1.0f / m_zoom);
            m_modified = true;
        }
        break;
    case DragLink:
        if (!ui.mouseDown[0]) {
            m_drag = DragNone;
            PinRef t = hitPin(g, ui.mx, ui.my);
            if (t.pin >= 0 && t.output != m_linkFrom.output) {
                std::string err;
                pushUndo(g);
                bool ok = m_linkFrom.output ? g.connect(m_linkFrom.node, m_linkFrom.pin, t.node, t.pin, &err)
                                            : g.connect(t.node, t.pin, m_linkFrom.node, m_linkFrom.pin, &err);
                if (!ok) { m_undo.pop_back(); status = "Liaison impossible : " + err; }
                else status.clear();
            } else if (t.pin < 0 && !hitNode(g, ui.mx, ui.my) && body.contains(ui.mx, ui.my)) {
                openMenu(mouse, m_linkFrom);   // drop in the void: pick a node to connect
            }
        }
        break;
    case DragPan: {
        int b = m_rightPressed;
        if (!ui.mouseDown[b]) {
            m_drag = DragNone;
            float mv = std::fabs(mouse.x - m_rightPos.x) + std::fabs(mouse.y - m_rightPos.y);
            if (b == 1 && mv < 4) openMenu(mouse, PinRef());
        } else if (delta.x != 0 || delta.y != 0) {
            m_pan = m_pan - delta * (1.0f / m_zoom);
        }
        break;
    }
    case DragMarquee:
        if (!ui.mouseDown[0]) {
            m_drag = DragNone;
            Rect mr(std::min(m_pressPos.x, ui.mx), std::min(m_pressPos.y, ui.my), std::fabs(ui.mx - m_pressPos.x), std::fabs(ui.my - m_pressPos.y));
            if (mr.w > 3 || mr.h > 3)
                for (const Node& n : g.nodes) {
                    Rect r = nodeRect(n);
                    if (r.x < mr.r() && r.r() > mr.x && r.y < mr.b() && r.b() > mr.y && !selected(n.id)) m_sel.push_back(n.id);
                }
        }
        break;
    default: break;
    }
}

} // namespace game
