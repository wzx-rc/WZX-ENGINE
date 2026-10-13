#include "NodeEditor.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <windows.h>

namespace game {

using vp3d::float2;

// Geometry of the Atelier graph cards (graph units): 224 wide, title band 35, pins from 58 by 25,
// a footer line of 23 at the bottom. Height = 81 + rows x 25, as in the Atelier graph.
static const float kCardW = 224, kTitle = 35, kPinTop = 58, kRow = 25, kFooter = 23;
static const float kHeaderPx = 37, kFooterPx = 24;     // canvas header and footer (DIPs)

// Atelier palette (Blue 01 defaults, atelier_style.h).
static const ImU32 kBg = IM_COL32(23, 28, 32, 255), kPanel = IM_COL32(28, 33, 38, 255), kRaised = IM_COL32(37, 43, 49, 255),
                   kHover = IM_COL32(48, 58, 66, 255), kBar = IM_COL32(21, 26, 30, 255), kBorderC = IM_COL32(49, 57, 64, 255),
                   kCanvas = IM_COL32(22, 27, 31, 255), kNode = IM_COL32(34, 40, 46, 255), kNodeOff = IM_COL32(29, 34, 39, 255),
                   kGridDot = IM_COL32(62, 71, 78, 150), kGridLine = IM_COL32(43, 51, 58, 95), kOverlay = IM_COL32(35, 43, 50, 220),
                   kMinimap = IM_COL32(28, 34, 40, 240), kText = IM_COL32(201, 208, 216, 255), kMuted = IM_COL32(132, 143, 155, 255),
                   kCyan = IM_COL32(102, 186, 208, 255), kSelect = IM_COL32(43, 72, 88, 255), kGreen = IM_COL32(120, 185, 132, 255),
                   kAmber = IM_COL32(219, 166, 100, 255), kRed = IM_COL32(218, 106, 128, 255), kPinExec = IM_COL32(219, 226, 233, 255);

static std::wstring widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
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

static ImU32 withA(ImU32 c, int a) { return (c & 0x00ffffffu) | ((ImU32)a << 24); }

// Segoe Fluent Icons glyph as UTF-8 (the icon range is merged into the editor fonts).
static std::string glyph(uint32_t cp) {
    std::string s;
    s += (char)(0xE0 | (cp >> 12));
    s += (char)(0x80 | ((cp >> 6) & 0x3F));
    s += (char)(0x80 | (cp & 0x3F));
    return s;
}

// Node category colours of the Atelier graph (Event, Flow, Scene, Variables, Math, Rendering).
static ImU32 catColor(const std::string& c) {
    if (c == "Événements") return IM_COL32(218, 106, 128, 255);
    if (c == "Flux") return IM_COL32(219, 166, 100, 255);
    if (c == "Actions") return IM_COL32(117, 159, 229, 255);
    if (c == "Données") return IM_COL32(186, 132, 203, 255);
    if (c == "Maths") return IM_COL32(134, 189, 144, 255);
    if (c == "RTS") return IM_COL32(178, 146, 223, 255);
    return IM_COL32(150, 156, 166, 255);
}

// Pin colours of the Atelier graph.
static ImU32 pinColor(PinType t) {
    switch (t) {
    case PinType::Exec: return kPinExec;
    case PinType::Float: return IM_COL32(136, 199, 150, 255);
    case PinType::Bool: return IM_COL32(222, 110, 121, 255);
    case PinType::Vec3: return IM_COL32(221, 194, 106, 255);
    case PinType::Object: return IM_COL32(113, 168, 233, 255);
    case PinType::String: return IM_COL32(213, 148, 204, 255);
    }
    return kText;
}

static bool hasVec3Input(const Node& n) {
    for (const PinDef& p : n.def->ins) if (p.type == PinType::Vec3) return true;
    return false;
}

// Text on the draw list, sized in ImGui pixels, vertically centred on p.y.
// `right` aligns the text to p.x (its right edge).
static void canvasText(ImDrawList* dl, ImFont* font, float size, ImVec2 p, ImU32 col, const std::string& s, bool right = false) {
    if (s.empty()) return;
    ImFont* f = font ? font : ImGui::GetFont();
    ImVec2 ts = f->CalcTextSizeA(size, FLT_MAX, 0, s.c_str());
    if (right) p.x -= ts.x;
    p.y -= ts.y * 0.5f;
    dl->AddText(f, size, p, col, s.c_str());
}

// Native hit area on a rectangle (screen pixels).
struct Hit { bool pressed = false, hovered = false, active = false, activated = false, deactivated = false; };

static Hit hitArea(const char* id, ImVec2 a, ImVec2 b) {
    Hit h;
    ImGui::SetCursorScreenPos(a);
    h.pressed = ImGui::InvisibleButton(id, ImVec2(std::max(1.0f, b.x - a.x), std::max(1.0f, b.y - a.y)),
                                       ImGuiButtonFlags_MouseButtonLeft);
    h.hovered = ImGui::IsItemHovered();
    h.active = ImGui::IsItemActive();
    h.activated = ImGui::IsItemActivated();
    h.deactivated = ImGui::IsItemDeactivated();
    return h;
}

// Atelier button: raised box with border, icon at 8 px, label at 28 px (icon only when label is empty).
static bool barButton(ImDrawList* dl, ImFont* font, const char* id, ImVec2 a, ImVec2 b, uint32_t icon, const char* label,
                      bool active, float d) {
    Hit h = hitArea(id, a, b);
    dl->AddRectFilled(a, b, active ? kSelect : (h.hovered ? kHover : kRaised), 3 * d);
    dl->AddRect(a, b, kBorderC, 3 * d);
    ImFont* f = font ? font : ImGui::GetFont();
    std::string ic = glyph(icon);
    ImVec2 is = f->CalcTextSizeA(13 * d, FLT_MAX, 0, ic.c_str());
    float cy = (a.y + b.y) * 0.5f;
    bool iconOnly = !label || !*label;
    float x = iconOnly ? (a.x + b.x - is.x) * 0.5f : a.x + 8 * d;
    dl->AddText(f, 13 * d, ImVec2(x, cy - is.y * 0.5f), active ? kCyan : kText, ic.c_str());
    if (!iconOnly) {
        ImVec2 ls = f->CalcTextSizeA(12 * d, FLT_MAX, 0, label);
        dl->AddText(f, 12 * d, ImVec2(a.x + 28 * d, cy - ls.y * 0.5f), kText, label);
    }
    return h.pressed;
}

// --------------------------------------------------------------------------- layout

float NodeEditor::nodeWidth(const Node&) const { return kCardW; }

int NodeEditor::rowCount(const Node& n) const {
    return std::max(1, std::max((int)n.def->ins.size(), (int)n.def->outs.size()));
}

ImVec2 NodeEditor::nodeMin(const Node& n) const { return toScreen(ImVec2(n.pos.x, n.pos.y)); }

ImVec2 NodeEditor::nodeMax(const Node& n) const {
    ImVec2 size(nodeWidth(n), kPinTop + rowCount(n) * kRow + kFooter);   // graph units
    return nodeMin(n) + size * scale();
}

// Pins sit on their own side: row = index of the pin in its list (as in the Atelier graph).
ImVec2 NodeEditor::pinPos(const Node& n, int pin, bool output) const {
    ImVec2 p0 = nodeMin(n);
    float s = scale();
    float y = p0.y + (kPinTop + pin * kRow) * s;
    float x = output ? p0.x + nodeWidth(n) * s : p0.x;
    return ImVec2(x, y);
}

// Screen rectangle of the inline value of an input pin (only when it is drawn as a widget).
bool NodeEditor::inlineArea(const Graph& g, const Node& n, int pin, ImVec2& a, ImVec2& b) const {
    const PinDef& p = n.def->ins[pin];
    if (m_zoom < 0.75f || p.type == PinType::Exec || p.type == PinType::Object) return false;
    if (g.inputLink(n.id, pin)) return false;
    float s = scale();
    float labelW = p.type == PinType::Vec3 ? 70.0f : 88.0f;
    ImVec2 p0 = nodeMin(n), p1 = nodeMax(n);
    ImVec2 pp = pinPos(n, pin, false);
    float wx = p0.x + (14 + labelW) * s, wr = p1.x - 10 * s, wh = 19 * s;
    a = ImVec2(wx, pp.y - wh * 0.5f);
    b = ImVec2(wr, pp.y + wh * 0.5f);
    return true;
}

bool NodeEditor::overInlineWidget(const Graph& g, ImVec2 p) const {
    for (const Node& n : g.nodes) {
        for (int i = 0; i < (int)n.def->ins.size(); ++i) {
            ImVec2 a, b;
            if (inlineArea(g, n, i, a, b) && p.x >= a.x && p.x <= b.x && p.y >= a.y && p.y <= b.y) return true;
        }
    }
    return false;
}

// --------------------------------------------------------------------------- hit tests

NodeEditor::PinRef NodeEditor::hitPin(const Graph& g, ImVec2 m) const {
    float s = scale();
    float rad = std::max(8.0f * s, 6.0f);
    for (auto it = g.nodes.rbegin(); it != g.nodes.rend(); ++it) {
        const Node& n = *it;
        for (int o = 0; o < 2; ++o) {
            const auto& pins = o ? n.def->outs : n.def->ins;
            for (int i = 0; i < (int)pins.size(); ++i) {
                ImVec2 p = pinPos(n, i, o == 1);
                // the label next to the pin is part of the target too
                float lx0 = o ? p.x - 70 * s : p.x - rad, lx1 = o ? p.x + rad : p.x + 70 * s;
                bool onLabel = m.x >= lx0 && m.x <= lx1 && std::fabs(m.y - p.y) < 9 * s;
                float dx = m.x - p.x, dy = m.y - p.y;
                bool onDot = dx * dx + dy * dy < rad * rad;
                // inputs with an inline value: only the dot (the label area hosts the widget)
                if (!o && pins[i].type != PinType::Exec && pins[i].type != PinType::Object) onLabel = false;
                if (onDot || onLabel) return {n.id, i, o == 1};
            }
        }
    }
    return {};
}

int NodeEditor::hitNode(const Graph& g, ImVec2 m) const {
    for (auto it = g.nodes.rbegin(); it != g.nodes.rend(); ++it) {
        ImVec2 a = nodeMin(*it), b = nodeMax(*it);
        if (m.x >= a.x && m.x < b.x && m.y >= a.y && m.y < b.y) return it->id;
    }
    return 0;
}

bool NodeEditor::selected(int id) const { return std::find(m_sel.begin(), m_sel.end(), id) != m_sel.end(); }

// --------------------------------------------------------------------------- undo / edit helpers

void NodeEditor::pushUndo(const Graph& g) {
    m_undo.push_back(g.toJson());
    if (m_undo.size() > 100) m_undo.erase(m_undo.begin());
    m_modified = true;
}

// An inline edit is one undo step: the graph is kept at the press, committed at the release.
void NodeEditor::trackEdit(const Graph& g, bool activated, bool changed, bool deactivated) {
    if (activated) { m_snap = g.toJson(); m_editChanged = false; }
    if (changed) { m_editChanged = true; m_modified = true; }
    if (deactivated && m_editChanged) {
        m_undo.push_back(m_snap);
        if (m_undo.size() > 100) m_undo.erase(m_undo.begin());
        m_editChanged = false;
    }
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
    if (g.nodes.empty() || m_canvas.x <= 0) { resetView(); return; }
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (const Node& n : g.nodes) {
        x0 = std::min(x0, n.pos.x);
        y0 = std::min(y0, n.pos.y);
        x1 = std::max(x1, n.pos.x + nodeWidth(n));
        y1 = std::max(y1, n.pos.y + kPinTop + rowCount(n) * kRow + kFooter);
    }
    // fit in the canvas (margins in ImGui pixels), never below 0.6 : the graph is then cropped, not shrunk further
    float aw = m_canvas.x - 80 * m_dpi, ah = m_canvas.y - 90 * m_dpi;
    float fit = std::min(aw / std::max((x1 - x0) * m_dpi, 1.0f), ah / std::max((y1 - y0) * m_dpi, 1.0f));
    m_zoom = std::min(std::max(fit, 0.6f), 1.2f);
    ImVec2 centre((x0 + x1) * 0.5f, (y0 + y1) * 0.5f);
    m_pan = centre - (m_canvas * 0.5f) / scale();
}

void NodeEditor::openMenu(ImVec2 screen, PinRef from) {
    m_menuRequest = true;
    m_focusSearch = true;
    m_menuScreen = screen;
    m_menuGraph = toGraph(screen);
    m_menuFrom = from;
    m_search[0] = 0;
}

// --------------------------------------------------------------------------- drawing

void NodeEditor::draw(ImVec2 pos, ImVec2 size, float dpi, Graph& g, const Runtime* rt, bool playing) {
    m_dpi = dpi > 0 ? dpi : 1.0f;
    for (Node& n : g.nodes) if (!n.def) n.def = findNodeDef(n.type);

    // its own window over the viewport: the editor never shares a window with the panels
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kCanvas);
    const ImGuiWindowFlags winFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                      ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("##nodegraph", nullptr, winFlags);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float headH = kHeaderPx * m_dpi, footH = kFooterPx * m_dpi;
    drawToolbar(pos, size.x, playing);     // canvas header: breadcrumb and Compile / Run / Build

    // canvas between the header and the footer
    m_origin = ImVec2(pos.x, pos.y + headH);
    m_canvas = ImVec2(size.x, std::max(1.0f, size.y - headH - footH));
    dl->AddRectFilled(m_origin, m_origin + m_canvas, kCanvas);

    if (m_wantFrame) { doFrame(g); m_wantFrame = false; }

    // input: the canvas owns the mouse unless a widget or the popup is under it
    ImGuiIO& io = ImGui::GetIO();
    bool inArea = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(m_origin, m_origin + m_canvas) &&
                  !ImGui::IsPopupOpen("##addnode") && !ImGui::IsAnyItemActive();
    bool overWidget = overInlineWidget(g, io.MousePos);
    handleInput(g, inArea, overWidget);

    drawCanvas(g, rt);
    drawLog(rt);
    drawHint(g);
    drawMinimap(g);
    drawFooter(pos, size, g);
    drawMenu(g);
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void NodeEditor::drawCanvas(Graph& g, const Runtime* rt) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c0 = m_origin, c1 = m_origin + m_canvas;
    float z = scale();

    // grid: dots every 25, lines every 125 (Atelier)
    dl->PushClipRect(c0, c1, true);
    float spacing = 25 * z, major = 125 * z;
    float ox = std::fmod(m_pan.x * z, spacing), oy = std::fmod(m_pan.y * z, spacing);
    if (spacing > 4) {
        for (float x = c0.x - ox; x < c1.x; x += spacing)
            for (float y = c0.y - oy; y < c1.y; y += spacing) dl->AddCircleFilled(ImVec2(x, y), 0.65f * m_dpi, kGridDot);
    }
    float mx = std::fmod(m_pan.x * z, major), my = std::fmod(m_pan.y * z, major);
    for (float x = c0.x - mx; x < c1.x; x += major) dl->AddLine(ImVec2(x, c0.y), ImVec2(x, c1.y), kGridLine);
    for (float y = c0.y - my; y < c1.y; y += major) dl->AddLine(ImVec2(c0.x, y), ImVec2(c1.x, y), kGridLine);

    drawLinks(g, rt);

    // link being dragged
    ImGuiIO& io = ImGui::GetIO();
    if (m_drag == DragLink) {
        const Node* n = g.node(m_linkFrom.node);
        if (n) {
            ImVec2 a = pinPos(*n, m_linkFrom.pin, m_linkFrom.output), b = io.MousePos;
            if (!m_linkFrom.output) std::swap(a, b);
            PinType t = m_linkFrom.output ? n->def->outs[m_linkFrom.pin].type : n->def->ins[m_linkFrom.pin].type;
            float d = std::max(65 * z, std::fabs(b.x - a.x) * 0.46f);
            dl->AddBezierCubic(a, ImVec2(a.x + d, a.y), ImVec2(b.x - d, b.y), b, withA(pinColor(t), 190), 2.0f * z);
        }
    }
    if (m_drag == DragMarquee) {
        ImVec2 a(std::min(m_pressPos.x, io.MousePos.x), std::min(m_pressPos.y, io.MousePos.y));
        ImVec2 b(std::max(m_pressPos.x, io.MousePos.x), std::max(m_pressPos.y, io.MousePos.y));
        dl->AddRectFilled(a, b, withA(kCyan, 30));
        dl->AddRect(a, b, withA(kCyan, 180));
    }

    // nodes: the card first, then its inline values on top
    ImGui::SetWindowFontScale(std::max(0.5f, m_zoom));
    for (Node& n : g.nodes) drawNode(g, n, rt);
    ImGui::SetWindowFontScale(1.0f);
    dl->PopClipRect();
}

void NodeEditor::drawHint(const Graph& g) {
    if (!g.nodes.empty()) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const char* msg = "Graphe vide : clic droit pour ajouter un noeud (ex. « Clic sur un objet »).";
    ImVec2 c = m_origin + m_canvas * 0.5f;
    ImFont* f = m_font ? m_font : ImGui::GetFont();
    ImVec2 ts = f->CalcTextSizeA(14 * m_dpi, FLT_MAX, 0, msg);
    dl->AddText(f, 14 * m_dpi, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), kMuted, msg);
}

// Wires as in the Atelier graph: cubic curves with a soft halo, colour of the output pin.
void NodeEditor::drawLinks(const Graph& g, const Runtime* rt) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float z = scale();
    for (const Link& l : g.links) {
        const Node* a = g.node(l.fromNode);
        const Node* b = g.node(l.toNode);
        if (!a || !b) continue;
        ImVec2 p0 = pinPos(*a, l.fromPin, true), p1 = pinPos(*b, l.toPin, false);
        PinType t = a->def->outs[l.fromPin].type;
        ImU32 c = pinColor(t);
        float w = 2.0f * z;
        if (rt && t == PinType::Exec && rt->running() && rt->time() - rt->lastRun(b->id) < 0.25f) { c = IM_COL32(255, 210, 74, 255); w *= 1.6f; }
        float d = std::max(65 * z, std::fabs(p1.x - p0.x) * 0.46f);
        dl->AddBezierCubic(p0, ImVec2(p0.x + d, p0.y), ImVec2(p1.x - d, p1.y), p1, withA(c, 55), w + 3 * z);
        dl->AddBezierCubic(p0, ImVec2(p0.x + d, p0.y), ImVec2(p1.x - d, p1.y), p1, c, w);
    }
}

// Pin shape as in the Atelier graph: exec = triangle, data = ring with a dot when connected.
static void drawPinShape(ImDrawList* dl, ImVec2 p, PinType t, bool connected, float z, float dpi) {
    ImU32 c = pinColor(t);
    if (t == PinType::Exec) {
        ImVec2 a = {p.x - 4 * z, p.y - 5 * z}, b = {p.x + 5 * z, p.y}, d = {p.x - 4 * z, p.y + 5 * z};
        if (connected) dl->AddTriangleFilled(a, b, d, c);
        else dl->AddTriangle(a, b, d, c, 1.2f * dpi);
    } else {
        dl->AddCircleFilled(p, 5 * z, kBg, 16);
        if (connected) dl->AddCircleFilled(p, 3.5f * z, c, 16);
        dl->AddCircle(p, 5 * z, c, 16, 1.3f * dpi);
    }
}

// Boxed numeric value dragged horizontally (drag = change, Shift = finer).
// Returns true when the value changed this frame.
static bool dragBox(ImDrawList* dl, ImFont* font, const Hit& h, ImVec2 a, ImVec2 b, float& v, float speed, const char* fmt,
                    float s, float dpi, const char* axis = nullptr, ImU32 axisColor = 0) {
    dl->AddRectFilled(a, b, kBg, 3 * s);
    dl->AddRect(a, b, h.active ? kCyan : (h.hovered ? IM_COL32(70, 84, 95, 255) : kBorderC), 3 * s);
    float tx = a.x + 6 * s;
    if (axis) {
        canvasText(dl, font, 11 * s, ImVec2(a.x + 8 * s, (a.y + b.y) * 0.5f), axisColor, axis);
        dl->AddLine(ImVec2(a.x + 16 * s, a.y + 5 * s), ImVec2(a.x + 16 * s, b.y - 5 * s), kBorderC);
        tx = a.x + 21 * s;
    }
    char buf[48];
    snprintf(buf, sizeof buf, fmt, v);
    canvasText(dl, font, 12 * s, ImVec2(tx, (a.y + b.y) * 0.5f), kText, buf);
    if (h.active) {
        ImGuiIO& io = ImGui::GetIO();
        float d = io.MouseDelta.x / dpi;
        if (d != 0) {
            v += d * speed * (io.KeyShift ? 0.1f : 1.0f);
            return true;
        }
    }
    return false;
}

void NodeEditor::drawNode(Graph& g, Node& n, const Runtime* rt) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float s = scale();
    ImVec2 p0 = nodeMin(n), p1 = nodeMax(n);
    if (p1.x < m_origin.x || p0.x > m_origin.x + m_canvas.x || p1.y < m_origin.y || p0.y > m_origin.y + m_canvas.y) return;
    bool sel = selected(n.id);
    bool hot = rt && rt->running() && rt->time() - rt->lastRun(n.id) < 0.25f;
    ImU32 color = catColor(n.def->category);
    float fy = p1.y - 26 * s;

    // card: shadow, body, title band tinted by the category, accent bar
    dl->AddRectFilled(p0 + ImVec2(4 * s, 5 * s), p1 + ImVec2(4 * s, 5 * s), IM_COL32(0, 0, 0, 65), 5 * s);
    dl->AddRectFilled(p0, p1, kNode, 5 * s);
    dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + kTitle * s), withA(color, 48), 5 * s, ImDrawFlags_RoundCornersTop);
    dl->AddRectFilled(ImVec2(p0.x, p0.y + 5 * s), ImVec2(p0.x + 3 * s, p0.y + 30 * s), color, 2 * s);
    if (n.def->event) canvasText(dl, m_font, 14 * s, ImVec2(p0.x + 12 * s, p0.y + 17 * s), kText, glyph(0xE945));
    canvasText(dl, m_fontBold, 15 * s, ImVec2(p0.x + 36 * s, p0.y + 17 * s), kText, n.def->label);
    dl->AddCircleFilled(ImVec2(p1.x - 13 * s, p0.y + 17 * s), 2 * s, withA(color, 220));
    canvasText(dl, m_font, 8.5f * s, ImVec2(p0.x + 12 * s, p0.y + 39 * s), withA(color, 220), n.def->category);
    char idx[16];
    snprintf(idx, sizeof idx, "#%03d", n.id);
    canvasText(dl, m_font, 8 * s, ImVec2(p1.x - 36 * s, p0.y + 39 * s), kMuted, idx);

    // footer: what the node does
    dl->AddLine(ImVec2(p0.x + 9 * s, fy), ImVec2(p1.x - 9 * s, fy), withA(kBorderC, 190));
    if (!n.def->help.empty()) {
        dl->PushClipRect(ImVec2(p0.x + 11 * s, fy + 4 * s), ImVec2(p1.x - 10 * s, p1.y), true);
        canvasText(dl, m_font, 10 * s, ImVec2(p0.x + 12 * s, fy + 12 * s), kMuted, n.def->help);
        dl->PopClipRect();
    }

    // border: active node in cyan, a node that just ran in its category colour
    if (sel) dl->AddRect(p0, p1, kCyan, 5 * s, 0, 2 * s);
    else dl->AddRect(p0, p1, hot ? IM_COL32(255, 210, 74, 255) : (color), 5 * s, 0, 1 * s);
    if (hot) dl->AddRect(p0, p1, IM_COL32(255, 210, 74, 255), 5 * s, 0, 2 * s);

    // outputs: labels on the inner side of the pin
    for (int i = 0; i < (int)n.def->outs.size(); ++i) {
        const PinDef& p = n.def->outs[i];
        ImVec2 pp = pinPos(n, i, true);
        bool conn = false;
        for (const Link& l : g.links) if (l.fromNode == n.id && l.fromPin == i) { conn = true; break; }
        drawPinShape(dl, pp, p.type, conn, s, m_dpi);
        if (!p.name.empty()) canvasText(dl, m_font, 12.5f * s, ImVec2(pp.x - 13 * s, pp.y), kText, p.name, true);
    }

    // inputs: labels right of the pin, inline values below the label
    for (int i = 0; i < (int)n.def->ins.size(); ++i) {
        const PinDef& p = n.def->ins[i];
        ImVec2 pp = pinPos(n, i, false);
        bool conn = g.inputLink(n.id, i) != nullptr;
        drawPinShape(dl, pp, p.type, conn, s, m_dpi);
        if (p.type == PinType::Exec) continue;
        float labelW = p.type == PinType::Vec3 ? 70.0f : 88.0f;
        canvasText(dl, m_font, 12.5f * s, ImVec2(pp.x + 13 * s, pp.y), kText, p.name);
        if (p.type == PinType::Object || conn) continue;
        Value& v = n.inputs[i];

        ImVec2 a, b;
        if (!inlineArea(g, n, i, a, b)) {
            // small zoom: the value as text, no widget
            canvasText(dl, m_font, 11 * s, ImVec2(p0.x + (14 + labelW) * s, pp.y), kMuted, v.asString());
            continue;
        }

        // native items on the value area; the ids are scoped per node and pin
        ImGui::PushID(n.id * 64 + i);
        switch (p.type) {
        case PinType::Float: {
            Hit h = hitArea("##f", a, b);
            float speed = std::max(0.02f, std::fabs(v.f) * 0.01f);
            bool changed = dragBox(dl, m_font, h, a, b, v.f, speed, "%.2f", s, m_dpi);
            trackEdit(g, h.activated, changed, h.deactivated);
            break;
        }
        case PinType::Vec3: {
            static const char* axes[3] = {"X", "Y", "Z"};
            static const ImU32 axc[3] = {kRed, kGreen, kCyan};
            float cw = (b.x - a.x - 4 * s) / 3;
            for (int k = 0; k < 3; ++k) {
                ImVec2 fa(a.x + k * (cw + 2 * s), a.y), fb(fa.x + cw, b.y);
                ImGui::PushID(k);
                Hit h = hitArea("##v", fa, fb);
                float speed = std::max(0.05f, std::fabs(v.v[k]) * 0.01f);
                bool changed = dragBox(dl, m_font, h, fa, fb, v.v[k], speed, "%.1f", s, m_dpi, s >= 0.95f ? axes[k] : nullptr, axc[k]);
                trackEdit(g, h.activated, changed, h.deactivated);
                ImGui::PopID();
            }
            break;
        }
        case PinType::Bool: {
            ImVec2 ba(b.x - 16 * s, pp.y - 8 * s), bb(b.x, pp.y + 8 * s);
            Hit h = hitArea("##b", ba, bb);
            dl->AddRectFilled(ba, bb, kBg, 3 * s);
            dl->AddRect(ba, bb, h.hovered ? kCyan : kBorderC, 3 * s);
            if (v.b) {
                std::string ck = glyph(0xE73E);
                ImFont* f = m_font ? m_font : ImGui::GetFont();
                ImVec2 cs = f->CalcTextSizeA(11 * s, FLT_MAX, 0, ck.c_str());
                dl->AddText(f, 11 * s, ImVec2((ba.x + bb.x - cs.x) * 0.5f, (ba.y + bb.y - cs.y) * 0.5f), kCyan, ck.c_str());
            }
            if (h.pressed) {
                // one click, one undo step: the graph before the toggle is kept
                m_snap = g.toJson();
                v.b = !v.b;
                m_modified = true;
                m_undo.push_back(m_snap);
                if (m_undo.size() > 100) m_undo.erase(m_undo.begin());
            }
            break;
        }
        case PinType::String: {
            ImGui::SetCursorScreenPos(a);
            ImGui::SetNextItemWidth(std::max(10.0f, b.x - a.x));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, kBg);
            ImGui::PushStyleColor(ImGuiCol_Border, kBorderC);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3 * s);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6 * s, 2 * s));
            char buf[256] = {};
            std::strncpy(buf, v.s.c_str(), sizeof buf - 1);
            bool changed = ImGui::InputText("##s", buf, sizeof buf);
            if (changed) v.s = buf;
            trackEdit(g, ImGui::IsItemActivated(), changed, ImGui::IsItemDeactivatedAfterEdit());
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(2);
            break;
        }
        default: break;
        }
        ImGui::PopID();
    }
}

// Canvas header (Atelier): breadcrumb, status, and the Compile / Run / Build / Frame buttons on the right.
void NodeEditor::drawToolbar(ImVec2 pos, float width, bool playing) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float d = m_dpi;
    ImVec2 a = pos, b = pos + ImVec2(width, kHeaderPx * d);
    dl->AddRectFilled(a, b, kPanel);
    dl->AddLine(ImVec2(a.x, b.y - 0.5f), ImVec2(b.x, b.y - 0.5f), kBorderC);

    ImFont* f = m_font ? m_font : ImGui::GetFont();
    float cy = (a.y + b.y) * 0.5f;
    std::string gi = glyph(0xE8A1);
    dl->AddText(f, 13 * d, ImVec2(a.x + 12 * d, cy - 7 * d), kCyan, gi.c_str());
    canvasText(dl, m_font, 12 * d, ImVec2(a.x + 34 * d, cy), kText, "Graphe du jeu");
    canvasText(dl, m_font, 11 * d, ImVec2(a.x + 172 * d, cy), kMuted, "Event Graph");
    if (!status.empty()) canvasText(dl, m_font, 11 * d, ImVec2(a.x + 290 * d, cy), kMuted, status);

    float x = b.x - (103 + 71 + 72 + 28 + 4 * 6 + 12) * d, y0 = a.y + 6 * d, h = 25 * d;
    auto btn = [&](const char* id, float w, uint32_t icon, const char* label, bool active) {
        bool pressed = barButton(dl, m_font, id, ImVec2(x, y0), ImVec2(x + w * d, y0 + h), icon, label, active, d);
        x += (w + 6) * d;
        return pressed;
    };
    if (btn("##tb_save", 103, 0xE73E, "Compile", false)) m_action = Save;
    if (btn("##tb_play", 71, playing ? 0xE71A : 0xE768, playing ? "Stop" : "Run", playing)) m_action = TogglePlay;
    if (btn("##tb_build", 72, 0xE7B8, "Build", false)) m_action = Build;
    if (btn("##tb_frame", 28, 0xE9A6, "", false)) m_wantFrame = true;
}

// Bottom strip of the canvas: pin legend, counts and the compile state (Atelier footer).
void NodeEditor::drawFooter(ImVec2 pos, ImVec2 size, const Graph& g) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float d = m_dpi;
    ImVec2 a(pos.x, pos.y + size.y - kFooterPx * d), b(pos.x + size.x, pos.y + size.y);
    dl->AddRectFilled(a, b, kBg);
    static const char* names[] = {"Exec", "Float", "Bool", "Vector", "Entity", "String"};
    static const PinType types[] = {PinType::Exec, PinType::Float, PinType::Bool, PinType::Vec3, PinType::Object, PinType::String};
    float x = a.x + 9 * d;
    for (int i = 0; i < 6; ++i) {
        dl->AddCircleFilled(ImVec2(x + 4 * d, a.y + 12 * d), 3 * d, pinColor(types[i]));
        canvasText(dl, m_font, 9 * d, ImVec2(x + 13 * d, a.y + 12 * d), kMuted, names[i]);
        x += 61 * d;
    }
    std::string counts = std::to_string(g.nodes.size()) + " nodes  /  " + std::to_string(g.links.size()) + " links";
    canvasText(dl, m_font, 9 * d, ImVec2(b.x - 272 * d, a.y + 12 * d), kMuted, counts);
    canvasText(dl, m_font, 9 * d, ImVec2(b.x - 116 * d, a.y + 12 * d), m_modified ? kAmber : kGreen,
               m_modified ? "\xE2\x97\x8F  Modified" : "\xE2\x97\x8F  Compiled");
}

// Overview of the graph in the corner (Atelier): tiles coloured by category and the view rectangle.
void NodeEditor::drawMinimap(const Graph& g) {
    if (g.nodes.empty()) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float d = m_dpi;
    ImVec2 mn = m_origin + ImVec2(m_canvas.x - 196 * d, m_canvas.y - 127 * d);
    ImVec2 mx = mn + ImVec2(184 * d, 115 * d);
    dl->AddRectFilled(mn, mx, kMinimap, 4 * d);
    dl->AddRect(mn, mx, kBorderC, 4 * d);
    canvasText(dl, m_font, 8 * d, mn + ImVec2(9 * d, 8 * d), kMuted, "GRAPH OVERVIEW");
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (const Node& n : g.nodes) {
        x0 = std::min(x0, n.pos.x); y0 = std::min(y0, n.pos.y);
        x1 = std::max(x1, n.pos.x + nodeWidth(n)); y1 = std::max(y1, n.pos.y + kPinTop + rowCount(n) * kRow + kFooter);
    }
    x0 -= 15; y0 -= 15; x1 += 15; y1 += 15;
    float box = std::min((184 * d - 18 * d) / (x1 - x0), (115 * d - 31 * d) / (y1 - y0));
    ImVec2 o = mn + ImVec2(9 * d, 25 * d);
    for (const Node& n : g.nodes) {
        ImVec2 t0(o.x + (n.pos.x - x0) * box, o.y + (n.pos.y - y0) * box);
        ImVec2 t1(t0.x + nodeWidth(n) * box, t0.y + (kPinTop + rowCount(n) * kRow + kFooter) * box);
        dl->AddRectFilled(t0, t1, withA(catColor(n.def->category), selected(n.id) ? 210 : 95), 1 * d);
    }
}

void NodeEditor::drawLog(const Runtime* rt) {
    if (!rt || rt->log.empty()) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float d = m_dpi;
    int lines = std::min<int>(6, (int)rt->log.size());
    std::string vars;
    for (auto& kv : rt->variables()) vars += kv.first + " = " + kv.second.asString() + "   ";
    float lineH = 17 * d;
    float h = lines * lineH + 14 * d + (vars.empty() ? 0 : 18 * d);
    float w = std::min(520.0f * d, m_canvas.x - 20 * d);
    ImVec2 r0(m_origin.x + 10 * d, m_origin.y + m_canvas.y - h - 10 * d);
    ImVec2 r1(r0.x + w, r0.y + h);
    dl->AddRectFilled(r0, r1, kOverlay, 6 * d);
    dl->AddRect(r0, r1, kBorderC, 6 * d);
    float y = r0.y + 7 * d;
    for (int i = (int)rt->log.size() - lines; i < (int)rt->log.size(); ++i, y += lineH) {
        const std::string& s = rt->log[i];
        bool err = s.rfind("Erreur", 0) == 0;
        canvasText(dl, m_font, 11.5f * d, ImVec2(r0.x + 10 * d, y + lineH * 0.5f), err ? kRed : kMuted, s);
    }
    if (!vars.empty()) canvasText(dl, m_font, 11.5f * d, ImVec2(r0.x + 10 * d, y + lineH * 0.5f), kCyan, vars);
}

// Creates the node picked in the popup, wired to the pin the popup was opened from.
void NodeEditor::addNodeFromMenu(Graph& g, const NodeDef& d) {
    pushUndo(g);
    int id = g.addNode(d.id, float2(std::round(m_menuGraph.x / 8) * 8, std::round(m_menuGraph.y / 8) * 8));
    const Node* fromNode = m_menuFrom.pin >= 0 ? g.node(m_menuFrom.node) : nullptr;
    if (fromNode && id) {
        const auto& pins = m_menuFrom.output ? d.ins : d.outs;
        PinType fromType = m_menuFrom.output ? fromNode->def->outs[m_menuFrom.pin].type : fromNode->def->ins[m_menuFrom.pin].type;
        for (int i = 0; i < (int)pins.size(); ++i) {
            bool ok = m_menuFrom.output ? Graph::compatible(fromType, pins[i].type) : Graph::compatible(pins[i].type, fromType);
            if (!ok) continue;
            std::string err;
            if (m_menuFrom.output) g.connect(m_menuFrom.node, m_menuFrom.pin, id, i, &err);
            else g.connect(id, i, m_menuFrom.node, m_menuFrom.pin, &err);
            break;
        }
    }
    m_sel = {id};
}

void NodeEditor::drawMenu(Graph& g) {
    if (m_menuRequest) {
        ImGui::OpenPopup("##addnode");
        m_menuRequest = false;
    }
    const float d = m_dpi;
    // the Atelier popup: dark card, rounded, search field on top, rows 22 high
    ImGui::PushStyleColor(ImGuiCol_PopupBg, kPanel);
    ImGui::PushStyleColor(ImGuiCol_Border, kBorderC);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, kSelect);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, kSelect);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 5 * d);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4 * d, 4 * d));
    ImGui::SetNextWindowPos(m_menuScreen, ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(270 * d, 400 * d));
    bool open = ImGui::BeginPopup("##addnode");
    if (open) {
        const Node* fromNode = m_menuFrom.pin >= 0 ? g.node(m_menuFrom.node) : nullptr;
        PinType fromType = PinType::Exec;
        if (fromNode) fromType = m_menuFrom.output ? fromNode->def->outs[m_menuFrom.pin].type : fromNode->def->ins[m_menuFrom.pin].type;
        // a node can be added from a pin only if one of its pins accepts the pin's type
        auto compatibleWithPin = [&](const NodeDef& nd) -> bool {
            if (!fromNode) return true;
            const auto& pins = m_menuFrom.output ? nd.ins : nd.outs;
            for (const PinDef& p : pins)
                if (m_menuFrom.output ? Graph::compatible(fromType, p.type) : Graph::compatible(p.type, fromType)) return true;
            return false;
        };

        // search field with its icon
        if (m_focusSearch) { ImGui::SetKeyboardFocusHere(); m_focusSearch = false; }
        ImGui::PushStyleColor(ImGuiCol_FrameBg, kBg);
        ImGui::PushStyleColor(ImGuiCol_Border, kCyan);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4 * d);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(26 * d, 4 * d));
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##search", fromNode ? "Noeuds compatibles… tapez pour filtrer" : "Tapez pour chercher un noeud…",
                                 m_search, sizeof m_search);
        ImVec2 smin = ImGui::GetItemRectMin();
        std::string si = glyph(0xE721);
        ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), 12 * d, ImVec2(smin.x + 6 * d, smin.y + 5 * d), kMuted, si.c_str());
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);
        std::wstring q = fold(widen(m_search));

        const NodeDef* first = nullptr;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(26 * d, 3 * d));
        ImGui::BeginChild("##nodelist", ImVec2(0, 0));
        std::string lastCat;
        for (const NodeDef& nd : nodeDefs()) {
            if (!compatibleWithPin(nd)) continue;
            if (!q.empty() && fold(widen(nd.label)).find(q) == std::wstring::npos && fold(widen(nd.id)).find(q) == std::wstring::npos) continue;
            if (!first) first = &nd;
            if (q.empty() && nd.category != lastCat) {
                ImGui::Dummy(ImVec2(0, 2 * d));
                ImGui::TextColored(ImColor(catColor(nd.category)), "%s", nd.category.c_str());
                lastCat = nd.category;
            }
            ImGui::PushID(nd.id.c_str());
            bool clicked = ImGui::Selectable(nd.label.c_str(), false, 0, ImVec2(0, 22 * d));
            ImVec2 rmin = ImGui::GetItemRectMin();
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(rmin.x + 14 * d, rmin.y + 11 * d), 3.5f * d, catColor(nd.category));
            if (ImGui::IsItemHovered() && !nd.help.empty()) ImGui::SetTooltip("%s", nd.help.c_str());
            if (clicked) {
                addNodeFromMenu(g, nd);
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        // Enter picks the first match
        if (first && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            addNodeFromMenu(g, *first);
            ImGui::CloseCurrentPopup();
        }
        if (!first) ImGui::TextDisabled("Aucun noeud");
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(4);
}

// --------------------------------------------------------------------------- input

void NodeEditor::handleInput(Graph& g, bool inArea, bool overWidget) {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mouse = io.MousePos;
    bool ctrl = io.KeyCtrl, shift = io.KeyShift, alt = io.KeyAlt;

    // zoom around the mouse
    if (inArea && io.MouseWheel != 0) {
        ImVec2 gp = toGraph(mouse);
        m_zoom = std::min(std::max(m_zoom * std::pow(1.12f, io.MouseWheel), 0.35f), 1.8f);
        m_pan = gp - (mouse - m_origin) / scale();
    }

    // keyboard (not while a widget takes text)
    if (!io.WantTextInput && (inArea || ImGui::IsWindowFocused())) {
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) deleteSelection(g);
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D)) duplicateSelection(g);
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z)) undo(g);
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_A)) { m_sel.clear(); for (const Node& n : g.nodes) m_sel.push_back(n.id); }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) m_sel.clear();
        if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_F)) frameAll(g);
    }

    // presses
    if (inArea && ImGui::IsMouseClicked(0) && m_drag == DragNone && !overWidget) {
        m_pressPos = mouse;
        m_dragMoved = false;
        PinRef p = hitPin(g, mouse);
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
        } else if (int id = hitNode(g, mouse)) {
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
    if (inArea && ImGui::IsMouseClicked(1) && m_drag == DragNone) {
        m_drag = DragPan;
        m_rightButton = 1;
        m_rightPos = mouse;
        m_dragMoved = false;
    }

    // drags
    ImVec2 delta = io.MouseDelta;
    switch (m_drag) {
    case DragNodes:
        if (!io.MouseDown[0]) {
            for (int id : m_sel)
                if (Node* n = g.node(id)) n->pos = float2(std::round(n->pos.x / 8) * 8, std::round(n->pos.y / 8) * 8);
            m_drag = DragNone;
        } else if (delta.x != 0 || delta.y != 0) {
            if (!m_dragMoved) { pushUndo(g); m_dragMoved = true; }
            for (int id : m_sel)
                if (Node* n = g.node(id)) n->pos = n->pos + float2(delta.x / scale(), delta.y / scale());
            m_modified = true;
        }
        break;
    case DragLink:
        if (!io.MouseDown[0]) {
            m_drag = DragNone;
            PinRef t = hitPin(g, mouse);
            if (t.pin >= 0 && t.output != m_linkFrom.output) {
                std::string err;
                pushUndo(g);
                bool ok = m_linkFrom.output ? g.connect(m_linkFrom.node, m_linkFrom.pin, t.node, t.pin, &err)
                                            : g.connect(t.node, t.pin, m_linkFrom.node, m_linkFrom.pin, &err);
                if (!ok) { m_undo.pop_back(); status = "Liaison impossible : " + err; }
                else status.clear();
            } else if (t.pin < 0 && !hitNode(g, mouse) && inArea) {
                openMenu(mouse, m_linkFrom);   // dropped in the void: pick a node to connect
            }
        }
        break;
    case DragPan:
        if (!ImGui::IsMouseDown(m_rightButton)) {
            m_drag = DragNone;
            float moved = std::fabs(mouse.x - m_rightPos.x) + std::fabs(mouse.y - m_rightPos.y);
            if (m_rightButton == 1 && moved < 4 * m_dpi) openMenu(mouse, PinRef());
        } else if (delta.x != 0 || delta.y != 0) {
            m_pan = m_pan - delta / scale();
        }
        break;
    case DragMarquee:
        if (!io.MouseDown[0]) {
            m_drag = DragNone;
            ImVec2 a(std::min(m_pressPos.x, mouse.x), std::min(m_pressPos.y, mouse.y));
            ImVec2 b(std::max(m_pressPos.x, mouse.x), std::max(m_pressPos.y, mouse.y));
            if (b.x - a.x > 3 || b.y - a.y > 3)
                for (const Node& n : g.nodes) {
                    ImVec2 na = nodeMin(n), nb = nodeMax(n);
                    if (na.x < b.x && nb.x > a.x && na.y < b.y && nb.y > a.y && !selected(n.id)) m_sel.push_back(n.id);
                }
        }
        break;
    default: break;
    }
}

void NodeEditor::addNodeAtView(Graph& g, const NodeDef& d) {
    m_menuFrom = PinRef();
    m_menuGraph = toGraph(m_origin + m_canvas * 0.5f);
    addNodeFromMenu(g, d);
}

ImU32 categoryAccentColor(const std::string& category) { return catColor(category); }

} // namespace game
