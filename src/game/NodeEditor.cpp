#include "NodeEditor.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <windows.h>

namespace game {

using vp3d::float2;

static const float kHeader = 40, kRow = 24, kFooter = 22, kPad = 8;   // graph units: title band, rows, description line
static const float kBarH = 36;                           // toolbar height (DIPs)

// Colours of the editor (same palette as the former immediate UI).
static const uint32_t kField = 0x101216, kBorder = 0x262A31, kText = 0xE4E6EA, kTextDim = 0x979DA6,
                      kTextFaint = 0x5F6670, kAccent = 0x4C8DFF, kAccentDim = 0x1E3A66, kCard = 0x20242A,
                      kCardHover = 0x2A2F36, kPanel2 = 0x1B1F24, kPopup = 0x1A1D22, kRed = 0xE5534B, kGreen = 0x57C35B;

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

static ImU32 rgb(uint32_t c, float alpha = 1.0f) {
    return IM_COL32((c >> 16) & 255, (c >> 8) & 255, c & 255, (int)(alpha * 255.0f + 0.5f));
}

// Segoe Fluent Icons glyph as UTF-8 (the icon range is merged into the editor fonts).
static std::string glyph(uint32_t cp) {
    std::string s;
    s += (char)(0xE0 | (cp >> 12));
    s += (char)(0x80 | ((cp >> 6) & 0x3F));
    s += (char)(0x80 | (cp & 0x3F));
    return s;
}

static ImU32 pinColor(PinType t) {
    switch (t) {
    case PinType::Exec: return rgb(0xF2F2F2);
    case PinType::Float: return rgb(0x7ED957);
    case PinType::Bool: return rgb(0xE05A5A);
    case PinType::Vec3: return rgb(0xF2C744);
    case PinType::Object: return rgb(0x4FA3F7);
    case PinType::String: return rgb(0xE36FD0);
    }
    return rgb(0xFFFFFF);
}

static uint32_t categoryHeader(const std::string& c) {
    if (c == "Événements") return 0x6E3441;
    if (c == "Flux") return 0x6B5628;
    if (c == "Actions") return 0x2F4F78;
    if (c == "Données") return 0x4B3A6B;
    if (c == "Maths") return 0x2E5846;
    if (c == "RTS") return 0x7A5224;
    return 0x444A54;
}

// Bright tone of a category (labels, menu dots)
static uint32_t categoryAccent(const std::string& c) {
    if (c == "Événements") return 0xE06C75;
    if (c == "Flux") return 0xE5B567;
    if (c == "Actions") return 0x6CA8F0;
    if (c == "Données") return 0xB48EE0;
    if (c == "Maths") return 0x7BD39B;
    if (c == "RTS") return 0xE8A25C;
    return 0xAAB0BA;
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

// Native hit area on a rectangle (screen pixels). `pressed` is true on the frame of the click.
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

// Toolbar button: icon and label centred, accent when active.
static bool barButton(ImDrawList* dl, ImFont* font, const char* id, ImVec2 a, ImVec2 b, uint32_t icon, const char* label,
                      bool active, float d) {
    Hit h = hitArea(id, a, b);
    dl->AddRectFilled(a, b, active ? rgb(kAccentDim) : rgb(h.hovered ? kCardHover : kCard), 4 * d);
    dl->AddRect(a, b, active ? rgb(kAccent, 0.7f) : rgb(kBorder), 4 * d);
    ImFont* f = font ? font : ImGui::GetFont();
    std::string ic = glyph(icon);
    ImVec2 is = f->CalcTextSizeA(13 * d, FLT_MAX, 0, ic.c_str());
    ImVec2 ls = f->CalcTextSizeA(12.5f * d, FLT_MAX, 0, label);
    float total = is.x + 7 * d + ls.x;
    float x = (a.x + b.x - total) * 0.5f, cy = (a.y + b.y) * 0.5f;
    dl->AddText(f, 13 * d, ImVec2(x, cy - is.y * 0.5f), active ? rgb(kAccent) : rgb(kText), ic.c_str());
    dl->AddText(f, 12.5f * d, ImVec2(x + is.x + 7 * d, cy - ls.y * 0.5f), rgb(kText), label);
    return h.pressed;
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

ImVec2 NodeEditor::nodeMin(const Node& n) const { return toScreen(ImVec2(n.pos.x, n.pos.y)); }

ImVec2 NodeEditor::nodeMax(const Node& n) const {
    ImVec2 size(nodeWidth(n), kHeader + rowCount(n) * kRow + kFooter + kPad);   // graph units
    return nodeMin(n) + size * scale();
}

ImVec2 NodeEditor::pinPos(const Node& n, int pin, bool output) const {
    ImVec2 p0 = nodeMin(n);
    float s = scale();
    int row = output ? pin : (n.def->ins[pin].type == PinType::Exec ? 0 : dataInputRow(n, pin));
    float y = p0.y + (kHeader + row * kRow + kRow * 0.5f) * s;
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
        y1 = std::max(y1, n.pos.y + kHeader + rowCount(n) * kRow + kFooter + kPad);
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
    const ImGuiWindowFlags winFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                      ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("##nodegraph", nullptr, winFlags);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float barH = kBarH * m_dpi;
    drawToolbar(pos, size.x, playing);

    // canvas below the toolbar
    m_origin = ImVec2(pos.x, pos.y + barH);
    m_canvas = ImVec2(size.x, std::max(1.0f, size.y - barH));
    dl->AddRectFilled(m_origin, m_origin + m_canvas, rgb(0x121417));

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
    drawMenu(g);
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void NodeEditor::drawCanvas(Graph& g, const Runtime* rt) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c0 = m_origin, c1 = m_origin + m_canvas;
    float s = scale();

    // grid
    float step = 16 * s;
    if (step > 5) {
        float ox = std::fmod(-m_pan.x * s, step), oy = std::fmod(-m_pan.y * s, step);
        int ix = (int)std::floor(m_pan.x / 16), iy = (int)std::floor(m_pan.y / 16);
        int k = 0;
        for (float x = c0.x + ox; x < c1.x; x += step, ++k)
            dl->AddLine(ImVec2(x, c0.y), ImVec2(x, c1.y), ((ix + k + 1) % 8 == 0) ? rgb(0x23272D) : rgb(0x181B1F));
        k = 0;
        for (float y = c0.y + oy; y < c1.y; y += step, ++k)
            dl->AddLine(ImVec2(c0.x, y), ImVec2(c1.x, y), ((iy + k + 1) % 8 == 0) ? rgb(0x23272D) : rgb(0x181B1F));
    }
    dl->PushClipRect(c0, c1, true);
    drawLinks(g, rt);

    // link being dragged
    ImGuiIO& io = ImGui::GetIO();
    if (m_drag == DragLink) {
        const Node* n = g.node(m_linkFrom.node);
        if (n) {
            ImVec2 a = pinPos(*n, m_linkFrom.pin, m_linkFrom.output), b = io.MousePos;
            if (!m_linkFrom.output) std::swap(a, b);
            PinType t = m_linkFrom.output ? n->def->outs[m_linkFrom.pin].type : n->def->ins[m_linkFrom.pin].type;
            float dx = std::max(std::fabs(b.x - a.x) * 0.5f, 40 * s);
            ImVec2 prev = a;
            for (int i = 1; i <= 24; ++i) {
                float t2 = i / 24.0f, u = 1 - t2;
                ImVec2 p(u * u * u * a.x + 3 * u * u * t2 * (a.x + dx) + 3 * u * t2 * t2 * (b.x - dx) + t2 * t2 * t2 * b.x,
                         u * u * u * a.y + 3 * u * u * t2 * a.y + 3 * u * t2 * t2 * b.y + t2 * t2 * t2 * b.y);
                dl->AddLine(prev, p, pinColor(t), 2.0f * s);
                prev = p;
            }
        }
    }
    if (m_drag == DragMarquee) {
        ImVec2 a(std::min(m_pressPos.x, io.MousePos.x), std::min(m_pressPos.y, io.MousePos.y));
        ImVec2 b(std::max(m_pressPos.x, io.MousePos.x), std::max(m_pressPos.y, io.MousePos.y));
        dl->AddRectFilled(a, b, rgb(kAccent, 0.12f));
        dl->AddRect(a, b, rgb(kAccent, 0.7f));
    }

    // nodes: the body is drawn first, then its inline widgets on top
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
    dl->AddText(f, 14 * m_dpi, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), rgb(kTextDim), msg);
}

void NodeEditor::drawLinks(const Graph& g, const Runtime* rt) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float s = scale();
    for (const Link& l : g.links) {
        const Node* a = g.node(l.fromNode);
        const Node* b = g.node(l.toNode);
        if (!a || !b) continue;
        ImVec2 p0 = pinPos(*a, l.fromPin, true), p1 = pinPos(*b, l.toPin, false);
        PinType t = a->def->outs[l.fromPin].type;
        ImU32 c = pinColor(t);
        float w = (t == PinType::Exec ? 2.6f : 2.0f) * s;
        if (rt && t == PinType::Exec && rt->running() && rt->time() - rt->lastRun(b->id) < 0.25f) { c = rgb(0xFFD24A); w *= 1.6f; }
        float dx = std::max(std::fabs(p1.x - p0.x) * 0.5f, 40 * s);
        ImVec2 prev = p0;
        for (int i = 1; i <= 24; ++i) {
            float t2 = i / 24.0f, u = 1 - t2;
            ImVec2 p(u * u * u * p0.x + 3 * u * u * t2 * (p0.x + dx) + 3 * u * t2 * t2 * (p1.x - dx) + t2 * t2 * t2 * p1.x,
                     u * u * u * p0.y + 3 * u * u * t2 * p0.y + 3 * u * t2 * t2 * p1.y + t2 * t2 * t2 * p1.y);
            dl->AddLine(prev, p, c, w);
            prev = p;
        }
    }
}

static void drawPinShape(ImDrawList* dl, ImVec2 p, PinType t, bool connected, float s) {
    ImU32 c = pinColor(t);
    if (t == PinType::Exec) {
        float k = 5.5f * s;
        ImVec2 pts[5] = {{p.x - k, p.y - k}, {p.x + k * 0.2f, p.y - k}, {p.x + k, p.y}, {p.x + k * 0.2f, p.y + k}, {p.x - k, p.y + k}};
        if (connected) dl->AddConvexPolyFilled(pts, 5, c);
        else dl->AddPolyline(pts, 5, c, ImDrawFlags_Closed, 1.4f * s);
    } else {
        dl->AddCircle(p, 4.8f * s, c, 0, 1.6f * s);
        if (connected) dl->AddCircleFilled(p, 2.8f * s, c);
    }
}

// Boxed numeric value dragged horizontally (drag = change, Shift = finer), as in the former editor.
// Returns true when the value changed this frame.
static bool dragBox(ImDrawList* dl, ImFont* font, const Hit& h, ImVec2 a, ImVec2 b, float& v, float speed, const char* fmt,
                    float s, float dpi, const char* axis = nullptr, uint32_t axisColor = 0) {
    dl->AddRectFilled(a, b, rgb(kField), 4 * s);
    dl->AddRect(a, b, h.active ? rgb(kAccent) : rgb(h.hovered ? kTextFaint : kBorder), 4 * s);
    float tx = a.x + 6 * s;
    if (axis) {
        ImVec2 tag(a.x + 8 * s, (a.y + b.y) * 0.5f);
        canvasText(dl, font, 11 * s, tag, rgb(axisColor), axis);
        dl->AddLine(ImVec2(a.x + 16 * s, a.y + 5 * s), ImVec2(a.x + 16 * s, b.y - 5 * s), rgb(kBorder));
        tx = a.x + 21 * s;
    }
    char buf[48];
    snprintf(buf, sizeof buf, fmt, v);
    canvasText(dl, font, 12 * s, ImVec2(tx, (a.y + b.y) * 0.5f), rgb(kText), buf);
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
    ImU32 headCol = n.def->pure ? rgb(0x3A3F47) : rgb(categoryHeader(n.def->category));

    // card: soft shadow, body, rounded header in the category colour
    dl->AddRectFilled(p0 + ImVec2(2 * s, 5 * s), p1 + ImVec2(2 * s, 5 * s), rgb(0x000000, 0.45f), 8 * s);
    dl->AddRectFilled(p0, p1, rgb(0x25282E, 0.98f), 8 * s);
    dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + kHeader * s), headCol, 8 * s, ImDrawFlags_RoundCornersTop);

    // header: optional event icon, title, category line, node number
    float tx = p0.x + (n.def->event ? 27 : 10) * s;
    if (n.def->event) canvasText(dl, m_font, 12 * s, ImVec2(p0.x + 9 * s, p0.y + 14 * s), rgb(0xFFFFFF, 0.9f), glyph(0xE945));
    canvasText(dl, m_fontBold, 12.5f * s, ImVec2(tx, p0.y + 14 * s), rgb(0xFFFFFF), n.def->label);
    canvasText(dl, m_font, 9.5f * s, ImVec2(tx, p0.y + 30 * s), rgb(0xFFFFFF, 0.65f), n.def->category);
    char idx[16];
    snprintf(idx, sizeof idx, "#%03d", n.id);
    canvasText(dl, m_font, 9.5f * s, ImVec2(p1.x - 9 * s, p0.y + 14 * s), rgb(0xFFFFFF, 0.5f), idx, true);

    // border: selection / running node in colour, otherwise a faint edge
    if (hot || sel) dl->AddRect(p0, p1, hot ? rgb(0xFFD24A) : rgb(kAccent), 8 * s, 0, 2.0f * s);
    else dl->AddRect(p0, p1, rgb(0x3A3F47), 8 * s, 0, 1.0f * s);

    // footer: what the node does
    if (!n.def->help.empty()) {
        float fy = p1.y - kFooter * s;
        dl->AddLine(ImVec2(p0.x + 8 * s, fy), ImVec2(p1.x - 8 * s, fy), rgb(0x3A3F47));
        canvasText(dl, m_font, 10 * s, ImVec2(p0.x + 10 * s, fy + kFooter * s * 0.5f), rgb(0x8A919C), n.def->help);
    }

    // outputs
    for (int i = 0; i < (int)n.def->outs.size(); ++i) {
        const PinDef& p = n.def->outs[i];
        ImVec2 pp = pinPos(n, i, true);
        bool conn = false;
        for (const Link& l : g.links) if (l.fromNode == n.id && l.fromPin == i) { conn = true; break; }
        drawPinShape(dl, pp, p.type, conn, s);
        if (!p.name.empty()) canvasText(dl, m_font, 11.5f * s, ImVec2(p1.x - 10 * s, pp.y), rgb(kText), p.name, true);
    }

    // inputs
    for (int i = 0; i < (int)n.def->ins.size(); ++i) {
        const PinDef& p = n.def->ins[i];
        ImVec2 pp = pinPos(n, i, false);
        bool conn = g.inputLink(n.id, i) != nullptr;
        drawPinShape(dl, pp, p.type, conn, s);
        if (p.type == PinType::Exec) continue;
        float labelW = p.type == PinType::Vec3 ? 70.0f : 88.0f;
        canvasText(dl, m_font, 11.5f * s, ImVec2(p0.x + 14 * s, pp.y), rgb(kTextDim), p.name);
        if (p.type == PinType::Object || conn) continue;
        Value& v = n.inputs[i];

        ImVec2 a, b;
        if (!inlineArea(g, n, i, a, b)) {
            // small zoom: the value as text, no widget
            canvasText(dl, m_font, 11 * s, ImVec2(p0.x + (14 + labelW) * s, pp.y), rgb(kTextFaint), v.asString());
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
            static const uint32_t axc[3] = {kRed, kGreen, kAccent};
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
            dl->AddRectFilled(ba, bb, rgb(kField), 3 * s);
            dl->AddRect(ba, bb, rgb(h.hovered ? kAccent : kBorder), 3 * s);
            if (v.b) {
                std::string ck = glyph(0xE73E);
                ImFont* f = m_font ? m_font : ImGui::GetFont();
                ImVec2 cs = f->CalcTextSizeA(11 * s, FLT_MAX, 0, ck.c_str());
                dl->AddText(f, 11 * s, ImVec2((ba.x + bb.x - cs.x) * 0.5f, (ba.y + bb.y - cs.y) * 0.5f), rgb(kAccent), ck.c_str());
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
            ImGui::PushStyleColor(ImGuiCol_FrameBg, rgb(kField));
            ImGui::PushStyleColor(ImGuiCol_Border, rgb(kBorder));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4 * s);
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

void NodeEditor::drawToolbar(ImVec2 pos, float width, bool playing) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float d = m_dpi;
    ImVec2 a = pos, b = pos + ImVec2(width, kBarH * d);
    dl->AddRectFilled(a, b, rgb(kPanel2, 0.96f));
    dl->AddLine(ImVec2(a.x, b.y - 0.5f), ImVec2(b.x, b.y - 0.5f), rgb(kBorder));

    ImFont* f = m_font ? m_font : ImGui::GetFont();
    float cy = (a.y + b.y) * 0.5f;
    std::string gi = glyph(0xE8A1);
    dl->AddText(f, 15 * d, ImVec2(a.x + 10 * d, cy - 9 * d), rgb(kAccent), gi.c_str());
    canvasText(dl, m_fontBold, 13 * d, ImVec2(a.x + 34 * d, cy), rgb(kText), "Graphe du jeu");

    float x = a.x + 160 * d, y0 = a.y + 5 * d, h = 26 * d;
    auto btn = [&](const char* id, float w, uint32_t icon, const char* label, bool active) {
        bool pressed = barButton(dl, m_font, id, ImVec2(x, y0), ImVec2(x + w * d, y0 + h), icon, label, active, d);
        x += (w + 6) * d;
        return pressed;
    };
    if (btn("##tb_play", 96, playing ? 0xE71A : 0xE768, playing ? "Stop" : "Jouer", playing)) m_action = TogglePlay;
    if (btn("##tb_save", 112, 0xE74E, "Enregistrer", false)) m_action = Save;
    if (btn("##tb_build", 96, 0xE7B8, "Build", false)) m_action = Build;
    if (btn("##tb_frame", 100, 0xE9A6, "Recadrer", false)) m_wantFrame = true;

    // hint, or the last message of the editor
    float closeX = b.x - 112 * d;
    std::string hint = status.empty() ? "Clic droit : ajouter  ·  Glisser une broche : relier  ·  Alt+clic : délier  ·  Suppr  ·  Ctrl+D  ·  Ctrl+Z" : status;
    dl->PushClipRect(ImVec2(x + 8 * d, a.y), ImVec2(closeX - 8 * d, b.y), true);
    canvasText(dl, m_font, 11.5f * d, ImVec2(x + 8 * d, cy), rgb(status.empty() ? kTextDim : kText), hint);
    dl->PopClipRect();

    if (btn("##tb_close", 104, 0xE711, "Fermer (F3)", false)) m_action = Close;
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
    dl->AddRectFilled(r0, r1, rgb(0x0B0C0E, 0.85f), 6 * d);
    dl->AddRect(r0, r1, rgb(kBorder), 6 * d);
    float y = r0.y + 7 * d;
    for (int i = (int)rt->log.size() - lines; i < (int)rt->log.size(); ++i, y += lineH) {
        const std::string& s = rt->log[i];
        bool err = s.rfind("Erreur", 0) == 0;
        canvasText(dl, m_font, 11.5f * d, ImVec2(r0.x + 10 * d, y + lineH * 0.5f), err ? rgb(kRed) : rgb(kTextDim), s);
    }
    if (!vars.empty()) canvasText(dl, m_font, 11.5f * d, ImVec2(r0.x + 10 * d, y + lineH * 0.5f), rgb(kAccent), vars);
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
    // the former menu look: dark card, rounded, search field on top, rows 22 high
    ImGui::PushStyleColor(ImGuiCol_PopupBg, rgb(kPopup));
    ImGui::PushStyleColor(ImGuiCol_Border, rgb(kBorder));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, rgb(kAccentDim));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, rgb(kAccentDim));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8 * d);
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
        ImGui::PushStyleColor(ImGuiCol_FrameBg, rgb(kField));
        ImGui::PushStyleColor(ImGuiCol_Border, rgb(kAccent, 0.7f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5 * d);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(26 * d, 4 * d));
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##search", fromNode ? "Noeuds compatibles… tapez pour filtrer" : "Tapez pour chercher un noeud…",
                                 m_search, sizeof m_search);
        ImVec2 smin = ImGui::GetItemRectMin();
        std::string si = glyph(0xE721);
        ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), 12 * d, ImVec2(smin.x + 6 * d, smin.y + 5 * d), rgb(kTextDim), si.c_str());
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
                ImGui::TextColored(ImColor(rgb(categoryAccent(nd.category))), "%s", nd.category.c_str());
                lastCat = nd.category;
            }
            ImGui::PushID(nd.id.c_str());
            bool clicked = ImGui::Selectable(nd.label.c_str(), false, 0, ImVec2(0, 22 * d));
            ImVec2 rmin = ImGui::GetItemRectMin();
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(rmin.x + 14 * d, rmin.y + 11 * d), 3.5f * d,
                                                        rgb(categoryAccent(nd.category)));
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

// Adds a node of the library at the centre of the view (no pin to wire to).
void NodeEditor::addNodeAtView(Graph& g, const NodeDef& d) {
    m_menuFrom = PinRef();
    m_menuGraph = toGraph(m_origin + m_canvas * 0.5f);
    addNodeFromMenu(g, d);
}

ImU32 categoryAccentColor(const std::string& category) { return rgb(categoryAccent(category)); }

} // namespace game
