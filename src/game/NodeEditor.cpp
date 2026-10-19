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
static const float kHeaderPx = 37, kFooterPx = 24, kInspectorPx = 284, kOutputPx = 151;   // DIPs
static const float kGroupTitle = 25;                                                       // graph units

// Atelier palette (Blue 01 defaults, atelier_style.h); setGraphPalette replaces the surfaces with the UI theme.
static ImU32 kBg = IM_COL32(23, 28, 32, 255), kPanel = IM_COL32(28, 33, 38, 255), kRaised = IM_COL32(37, 43, 49, 255),
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

// Node category colours of the Atelier graph (Event, Input, Flow, Scene, Variables, Math, Rendering).
static ImU32 catColor(const std::string& c) {
    if (c == "Events") return IM_COL32(218, 106, 128, 255);
    if (c == "Input") return IM_COL32(92, 192, 213, 255);
    if (c == "Flow Control") return IM_COL32(219, 166, 100, 255);
    if (c == "Scene") return IM_COL32(117, 159, 229, 255);
    if (c == "Data") return IM_COL32(186, 132, 203, 255);
    if (c == "Math") return IM_COL32(134, 189, 144, 255);
    if (c == "RTS") return IM_COL32(178, 146, 223, 255);
    if (c == "Native C++") return IM_COL32(255, 190, 40, 255);   // gold: the C++ node stands out
    // Maya shader graph (WzxShaderGraph)
    if (c == "Output") return IM_COL32(218, 106, 128, 255);
    if (c == "Shaders") return IM_COL32(117, 159, 229, 255);
    if (c == "Textures") return IM_COL32(219, 166, 100, 255);
    if (c == "Utilities") return IM_COL32(134, 189, 144, 255);
    if (c == "Constants") return IM_COL32(186, 132, 203, 255);
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

// Output levels: 0 info, 1 warning, 2 error, 3 success.
static ImU32 levelColor(int level) {
    if (level == 1) return kAmber;
    if (level == 2) return kRed;
    if (level == 3) return kGreen;
    return kMuted;
}
static uint32_t levelIcon(int level) {
    if (level == 1) return 0xE7BA;   // warning
    if (level == 2) return 0xE783;   // error badge
    if (level == 3) return 0xE73E;   // check
    return 0xE946;                   // info
}

// Vec3 pins named "...Color...", "Input" or "A" / "B" are edited as colours (Maya shader graph).
static bool isColorPin(const PinDef& p) {
    return p.type == PinType::Vec3 && (p.name.find("Color") != std::string::npos || p.name == "Input" || p.name == "A" || p.name == "B");
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

static long long expandKey(int node, int pin, bool output) { return ((long long)node << 20) | (pin << 1) | (output ? 1 : 0); }
static const int kThumbRows = 3;   // preview square: 3 rows high

bool NodeEditor::hasChannels(const Node& n, int pin, bool output) const {
    const auto& pins = output ? n.def->outs : n.def->ins;
    for (const PinDef& p : pins) if (p.parent == pin) return true;
    return false;
}

// Rows of the cards: each pin on its side, the channel pins (R, G, B) under their colour when it is expanded (or one of them
// is connected), then the preview square.
void NodeEditor::computeLayout(const Graph& g) {
    m_layout.clear();
    for (const Node& n : g.nodes) {
        if (!n.def) continue;
        NodeLayout L;
        int rows[2] = {0, 0};
        for (int side = 0; side < 2; ++side) {
            const auto& pins = side ? n.def->outs : n.def->ins;
            std::vector<int>& row = side ? L.outRow : L.inRow;
            row.assign(pins.size(), -1);
            for (int i = 0; i < (int)pins.size(); ++i) {
                if (pins[i].parent >= 0) continue;
                row[i] = rows[side]++;
                bool open = m_expanded.count(expandKey(n.id, i, side == 1)) > 0;
                for (int c = 0; c < (int)pins.size() && !open; ++c) {
                    if (pins[c].parent != i) continue;
                    for (const Link& l : g.links)
                        if (side ? (l.fromNode == n.id && l.fromPin == c) : (l.toNode == n.id && l.toPin == c)) { open = true; break; }
                }
                if (!open) continue;
                for (int c = 0; c < (int)pins.size(); ++c)
                    if (pins[c].parent == i) row[c] = rows[side]++;
            }
        }
        L.rows = std::max(1, std::max(rows[0], rows[1]));
        if (nodeThumb && nodeThumb(n)) { L.thumbRow = L.rows; L.rows += kThumbRows; }
        m_layout[n.id] = L;
    }
}

int NodeEditor::rowOf(const Node& n, int pin, bool output) const {
    auto it = m_layout.find(n.id);
    if (it == m_layout.end()) return pin;
    const std::vector<int>& r = output ? it->second.outRow : it->second.inRow;
    return pin >= 0 && pin < (int)r.size() ? r[pin] : pin;
}

// The +/- box of a colour pin that has channel pins: left of the input label, right of the output label.
bool NodeEditor::expanderRect(const Node& n, int pin, bool output, ImVec2& a, ImVec2& b) const {
    if (m_zoom < 0.6f || rowOf(n, pin, output) < 0 || !hasChannels(n, pin, output)) return false;
    ImVec2 pp = pinPos(n, pin, output);
    float s = scale(), h = 5 * s;
    float cx = output ? pp.x - 19 * s : pp.x + 17 * s;
    a = ImVec2(cx - h, pp.y - h);
    b = ImVec2(cx + h, pp.y + h);
    return true;
}

int NodeEditor::rowCount(const Node& n) const {
    auto it = m_layout.find(n.id);
    if (it != m_layout.end()) return it->second.rows;
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
    int row = rowOf(n, pin, output);
    if (row < 0) {   // hidden channel pin: at its colour pin
        const auto& pins = output ? n.def->outs : n.def->ins;
        row = rowOf(n, pins[pin].parent, output);
    }
    float y = p0.y + (kPinTop + row * kRow) * s;
    float x = output ? p0.x + nodeWidth(n) * s : p0.x;
    return ImVec2(x, y);
}

// Screen rectangle of the inline value of an input pin (only when it is drawn as a widget).
bool NodeEditor::inlineArea(const Graph& g, const Node& n, int pin, ImVec2& a, ImVec2& b) const {
    const PinDef& p = n.def->ins[pin];
    if (m_zoom < 0.75f || p.type == PinType::Exec || p.type == PinType::Object || p.wildcard) return false;
    if (g.inputLink(n.id, pin) || rowOf(n, pin, false) < 0) return false;
    if (p.parent >= 0 && g.inputLink(n.id, p.parent)) return false;   // channel of a connected colour
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
        for (int side = 0; side < 2; ++side)
            for (int i = 0; i < (int)(side ? n.def->outs : n.def->ins).size(); ++i) {
                ImVec2 a, b;
                if (expanderRect(n, i, side == 1, a, b) && p.x >= a.x - 2 && p.x <= b.x + 2 && p.y >= a.y - 2 && p.y <= b.y + 2) return true;
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
                if (rowOf(n, i, o == 1) < 0) continue;
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

// Title bar of a group (the top 25 graph units of its frame).
int NodeEditor::hitGroupTitle(const Graph& g, ImVec2 p) const {
    for (int i = (int)g.groups.size() - 1; i >= 0; --i) {
        const Group& gr = g.groups[i];
        ImVec2 a = toScreen(ImVec2(gr.x, gr.y));
        ImVec2 b = toScreen(ImVec2(gr.x + gr.w, gr.y + kGroupTitle));
        if (p.x >= a.x && p.x < b.x && p.y >= a.y && p.y < b.y) return i;
    }
    return -1;
}

// Resize corner of a backdrop: the bottom right square (14 px on screen).
int NodeEditor::hitGroupGrip(const Graph& g, ImVec2 p) const {
    const float r = 14 * m_dpi;
    for (int i = (int)g.groups.size() - 1; i >= 0; --i) {
        const Group& gr = g.groups[i];
        ImVec2 b = toScreen(ImVec2(gr.x + gr.w, gr.y + gr.h));
        if (p.x >= b.x - r && p.x <= b.x + 2 && p.y >= b.y - r && p.y <= b.y + 2) return i;
    }
    return -1;
}

int NodeEditor::collapseSelection(Graph& g) {
    std::vector<int> ids;
    for (int id : m_sel)
        if (const Node* n = g.node(id))
            if (n->type != "compound.input" && n->type != "compound.output") ids.push_back(id);
    if (ids.empty()) { status = "Select nodes to make a compound"; return 0; }
    pushUndo(g);
    int c = g.collapse(ids, "Compound");
    m_sel = {c};
    m_selGroup = -1;
    m_modified = true;
    status = "Compound created: double click to enter it";
    return c;
}

void NodeEditor::addBackdrop(Graph& g) {
    pushUndo(g);
    Group gr;
    gr.title = "BACKDROP";
    gr.category = "Neutral";
    if (!m_sel.empty()) {
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (int id : m_sel)
            if (const Node* n = g.node(id)) {
                ImVec2 a = toGraph(nodeMin(*n)), b = toGraph(nodeMax(*n));
                x0 = std::min(x0, a.x); y0 = std::min(y0, a.y); x1 = std::max(x1, b.x); y1 = std::max(y1, b.y);
            }
        const float pad = 24;
        gr.x = std::floor((x0 - pad) / 8) * 8;
        gr.y = std::floor((y0 - pad - kGroupTitle) / 8) * 8;
        gr.w = std::ceil((x1 + pad - gr.x) / 8) * 8;
        gr.h = std::ceil((y1 + pad - gr.y) / 8) * 8;
    } else {
        ImVec2 c = toGraph(m_origin + m_canvas * 0.5f);
        gr.x = std::round((c.x - gr.w * 0.5f) / 8) * 8;
        gr.y = std::round((c.y - gr.h * 0.5f) / 8) * 8;
    }
    // behind the other backdrops it encloses: drawn first
    g.groups.insert(g.groups.begin(), gr);
    m_selGroup = 0;
    m_modified = true;
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
    m_selGroup = -1;
    m_modified = true;
}

// Arrange: two cards that overlap are separated by moving the lower one under the upper one (with a gap).
// Only the overlapping nodes move, and only downwards, so the layout of the others is kept.
int NodeEditor::arrangeOverlaps(Graph& g) {
    const float gap = 16.0f;   // graph units between the moved card and the one above it
    std::string before = g.toJson();
    std::vector<float> y0;
    for (const Node& n : g.nodes) y0.push_back(n.pos.y);
    auto height = [&](const Node& n) { return kPinTop + rowCount(n) * kRow + kFooter; };
    std::vector<Node*> order;
    for (Node& n : g.nodes) order.push_back(&n);
    for (int pass = 0; pass < 64; ++pass) {
        std::stable_sort(order.begin(), order.end(), [](const Node* a, const Node* b) {
            return a->pos.y != b->pos.y ? a->pos.y < b->pos.y : a->pos.x < b->pos.x;
        });
        bool any = false;
        for (size_t i = 0; i < order.size(); ++i)
            for (size_t j = i + 1; j < order.size(); ++j) {
                const Node& a = *order[i];
                Node& b = *order[j];
                bool side = b.pos.x < a.pos.x + nodeWidth(a) && a.pos.x < b.pos.x + nodeWidth(b);
                bool stacked = b.pos.y < a.pos.y + height(a) && a.pos.y < b.pos.y + height(b);
                if (side && stacked) {
                    b.pos.y = a.pos.y + height(a) + gap;
                    any = true;
                }
            }
        if (!any) break;
    }
    int moved = 0;
    for (size_t k = 0; k < g.nodes.size(); ++k) if (g.nodes[k].pos.y != y0[k]) ++moved;
    if (moved) {
        m_undo.push_back(before);
        if (m_undo.size() > 100) m_undo.erase(m_undo.begin());
        m_modified = true;
    }
    return moved;
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
        if (copy.type == "compound.input" || copy.type == "compound.output") continue;
        int nid = g.addNode(copy.type, float2(copy.pos.x + 30, copy.pos.y + 30));
        if (copy.sub) g.node(nid)->sub = g.cloneSub(copy);   // a copied compound gets its own inner graph
        g.refreshDefs();
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
    if ((g.nodes.empty() && g.groups.empty()) || m_canvas.x <= 0) { resetView(); return; }
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (const Node& n : g.nodes) {
        x0 = std::min(x0, n.pos.x);
        y0 = std::min(y0, n.pos.y);
        x1 = std::max(x1, n.pos.x + nodeWidth(n));
        y1 = std::max(y1, n.pos.y + kPinTop + rowCount(n) * kRow + kFooter);
    }
    for (const Group& gr : g.groups) {
        x0 = std::min(x0, gr.x);
        y0 = std::min(y0, gr.y);
        x1 = std::max(x1, gr.x + gr.w);
        y1 = std::max(y1, gr.y + gr.h);
    }
    // fit in the canvas (margins in ImGui pixels), never below 0.6 : the graph is then cropped, not shrunk further
    float aw = m_canvas.x - 80 * m_dpi, ah = m_canvas.y - 90 * m_dpi;
    float fit = std::min(aw / std::max((x1 - x0) * m_dpi, 1.0f), ah / std::max((y1 - y0) * m_dpi, 1.0f));
    m_zoom = std::min(std::max(fit, 0.45f), 1.2f);
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

// Selects a node and centres the view on it (Execution Trace and Compiler Results rows).
void NodeEditor::focusNode(const Graph& g, int id) {
    const Node* n = g.node(id);
    if (!n) return;
    m_sel = {id};
    m_selGroup = -1;
    ImVec2 c(n->pos.x + nodeWidth(*n) * 0.5f, n->pos.y + 40);
    m_pan = c - (m_canvas * 0.5f) / scale();
}

// --------------------------------------------------------------------------- output

void NodeEditor::addMessage(int level, const std::string& text, int node) {
    m_messages.push_back({level, text, node});
    if (m_messages.size() > 300) m_messages.erase(m_messages.begin());
    m_scrollOutput = true;
}

void NodeEditor::setDiagnostics(const std::vector<Diagnostic>& d) {
    m_diagnostics = d;
    m_checked = true;
    m_outTab = 1;
}

// --------------------------------------------------------------------------- drawing

void NodeEditor::draw(ImVec2 pos, ImVec2 size, float dpi, Graph& g, const Runtime* rt, bool playing) {
    m_dpi = dpi > 0 ? dpi : 1.0f;
    g.refreshDefs();   // new nodes, compound pins promoted since the last frame
    computeLayout(g);

    // one window over the whole graph workspace: canvas column (header, canvas, footer, output) and the Inspector
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kCanvas);
    const ImGuiWindowFlags winFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                      ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("##nodegraph", nullptr, winFlags);

    const float d = m_dpi;
    const float inspW = inspectorPx * d, outH = outputPx * d, headH = kHeaderPx * d, footH = kFooterPx * d;
    const float cw = std::max(1.0f, size.x - inspW);     // canvas column: canvas, footer and output
    drawToolbar(pos, cw, playing);                        // canvas header: breadcrumb and Compile / Run / Build

    const float outY = pos.y + size.y - outH;             // output panel, bottom of the column
    const float footY = outY - footH - 2 * d;             // footer right above it
    m_origin = ImVec2(pos.x, pos.y + headH);
    m_canvas = ImVec2(cw, std::max(1.0f, footY - 2 * d - m_origin.y));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(m_origin, m_origin + m_canvas, kCanvas);

    if (m_wantFrame) { doFrame(g); m_wantFrame = false; }

    // input: the canvas owns the mouse unless a widget or the popup is under it
    ImGuiIO& io = ImGui::GetIO();
    bool inArea = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(m_origin, m_origin + m_canvas) &&
                  !ImGui::IsPopupOpen("##addnode") && !ImGui::IsAnyItemActive();
    bool overWidget = overInlineWidget(g, io.MousePos);
    handleInput(g, inArea, overWidget);
    computeLayout(g);
    m_hoverLink = (inArea && !overWidget && m_drag == DragNone && !hitNode(g, io.MousePos) && hitPin(g, io.MousePos).pin < 0)
                      ? hitLink(g, io.MousePos) : -1;
    if (forceHoverLink >= 0) m_hoverLink = forceHoverLink;

    drawCanvas(g, rt);
    // link under the mouse: what flows through it (Houdini style)
    if (m_hoverLink >= 0 && m_hoverLink < (int)g.links.size()) {
        const Link& l = g.links[m_hoverLink];
        const Node* a = g.node(l.fromNode);
        const Node* b = g.node(l.toNode);
        if (a && b) {
            const PinDef& po = a->def->outs[l.fromPin];
            if (forceHoverLink >= 0) {   // test capture: the tooltip at the middle of the link
                ImVec2 p0 = pinPos(*a, l.fromPin, true), p1 = pinPos(*b, l.toPin, false);
                ImGui::SetNextWindowPos((p0 + p1) * 0.5f + ImVec2(12, 12));
            }
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10 * d, 8 * d));
            ImGui::BeginTooltip();
            const char* tn = po.type == PinType::Vec3 ? labels.vectorPin : po.type == PinType::Object ? labels.objectPin : pinTypeName(po.type);
            ImGui::TextColored(ImColor(pinColor(po.type)), "%s", tn);
            ImGui::SameLine();
            ImGui::TextDisabled("link");
            ImGui::Text("From  %s . %s", a->def->label.c_str(), po.name.c_str());
            ImGui::Text("To      %s . %s", b->def->label.c_str(), b->def->ins[l.toPin].name.c_str());
            if (linkInfo) {
                ImGui::Separator();
                linkInfo(g, l);
            }
            ImGui::EndTooltip();
            ImGui::PopStyleVar();
        }
    }
    drawHint(g);
    drawMinimap(g);
    drawFooter(ImVec2(pos.x, footY), cw, g);
    drawOutput(ImVec2(pos.x, outY), ImVec2(cw, outH), g, rt);
    drawInspector(ImVec2(pos.x + cw, pos.y + inspectorTop), ImVec2(inspW, std::max(1.0f, size.y - inspectorTop)), g, rt);
    drawMenu(g);
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void NodeEditor::drawCanvas(Graph& g, const Runtime* rt) {
    g.refreshDefs();
    computeLayout(g);   // nodes and links may have changed with the input of this frame
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

    drawGroups(g);
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

// Group frames (Atelier): a tinted frame, a title bar and the title; behind the links and the nodes.
void NodeEditor::drawGroups(const Graph& g) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float z = scale();
    for (size_t i = 0; i < g.groups.size(); ++i) {
        const Group& gr = g.groups[i];
        ImVec2 a = toScreen(ImVec2(gr.x, gr.y)), b = toScreen(ImVec2(gr.x + gr.w, gr.y + gr.h));
        ImU32 c = gr.category == "Neutral" ? kMuted : catColor(gr.category);
        const bool sel = (int)i == m_selGroup;
        dl->AddRectFilled(a, b, withA(c, 22), 5 * z);
        dl->AddRect(a, b, sel ? kCyan : withA(c, 70), 5 * z, 0, sel ? 1.5f : 1.0f);
        dl->AddRectFilled(a, ImVec2(b.x, a.y + kGroupTitle * z), withA(c, 48), 5 * z, ImDrawFlags_RoundCornersTop);
        if ((int)i != m_renameGroup)
            canvasText(dl, m_fontBold ? m_fontBold : m_font, 13 * z, a + ImVec2(12 * z, kGroupTitle * z * 0.5f), withA(c, 235), gr.title);
        // resize grip (bottom right corner)
        const float r = 10 * m_dpi;
        bool gripHot = sel || ImGui::IsMouseHoveringRect(ImVec2(b.x - r - 4, b.y - r - 4), b);
        if (gripHot)
            for (int k = 1; k <= 3; ++k)
                dl->AddLine(ImVec2(b.x - 3 - k * r / 3, b.y - 3), ImVec2(b.x - 3, b.y - 3 - k * r / 3), withA(c, 200), 1.2f);
    }
    // title edited in place (double click on the title bar)
    if (m_renameGroup >= 0 && m_renameGroup < (int)g.groups.size()) {
        const Group& gr = g.groups[m_renameGroup];
        ImVec2 a = toScreen(ImVec2(gr.x, gr.y)), b = toScreen(ImVec2(gr.x + gr.w, gr.y + kGroupTitle));
        ImGui::SetCursorScreenPos(ImVec2(a.x + 6 * z, a.y + 2 * z));
        ImGui::SetNextItemWidth(std::max(60.0f, b.x - a.x - 12 * z));
        ImGui::SetKeyboardFocusHere();
        bool done = ImGui::InputText("##grename", m_renameBuf, sizeof m_renameBuf, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        if (done || (!ImGui::IsItemActive() && ImGui::IsMouseClicked(0) && !ImGui::IsItemHovered())) {
            Graph& gm = const_cast<Graph&>(g);
            if (gm.groups[m_renameGroup].title != m_renameBuf) { pushUndo(gm); gm.groups[m_renameGroup].title = m_renameBuf; m_modified = true; }
            m_renameGroup = -1;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) m_renameGroup = -1;
    }
}

void NodeEditor::drawHint(const Graph& g) {
    if (!g.nodes.empty()) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const char* msg = "Empty graph: right-click to add a node (e.g. « Ground Clicked »).";
    ImVec2 c = m_origin + m_canvas * 0.5f;
    ImFont* f = m_font ? m_font : ImGui::GetFont();
    ImVec2 ts = f->CalcTextSizeA(14 * m_dpi, FLT_MAX, 0, msg);
    dl->AddText(f, 14 * m_dpi, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), kMuted, msg);
}

// Link whose curve passes within a few pixels of p, or -1.
int NodeEditor::hitLink(const Graph& g, ImVec2 m) const {
    float z = scale(), best = 6.0f * m_dpi;
    int found = -1;
    for (int k = 0; k < (int)g.links.size(); ++k) {
        const Link& l = g.links[k];
        const Node* a = g.node(l.fromNode);
        const Node* b = g.node(l.toNode);
        if (!a || !b) continue;
        ImVec2 p0 = pinPos(*a, l.fromPin, true), p3 = pinPos(*b, l.toPin, false);
        float d = std::max(65 * z, std::fabs(p3.x - p0.x) * 0.46f);
        ImVec2 p1(p0.x + d, p0.y), p2(p3.x - d, p3.y);
        ImVec2 prev = p0;
        for (int i = 1; i <= 24; ++i) {
            float t = i / 24.0f, u = 1 - t;
            ImVec2 q = p0 * (u * u * u) + p1 * (3 * u * u * t) + p2 * (3 * u * t * t) + p3 * (t * t * t);
            ImVec2 e = q - prev;
            float len2 = e.x * e.x + e.y * e.y;
            float s = len2 > 0 ? std::min(1.0f, std::max(0.0f, ((m.x - prev.x) * e.x + (m.y - prev.y) * e.y) / len2)) : 0;
            ImVec2 c = prev + e * s;
            float dist = std::sqrt((m.x - c.x) * (m.x - c.x) + (m.y - c.y) * (m.y - c.y));
            if (dist < best) { best = dist; found = k; }
            prev = q;
        }
    }
    return found;
}

// Wires as in the Atelier graph: cubic curves with a soft halo, colour of the output pin.
void NodeEditor::drawLinks(const Graph& g, const Runtime* rt) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float z = scale();
    for (int k = 0; k < (int)g.links.size(); ++k) {
        const Link& l = g.links[k];
        const Node* a = g.node(l.fromNode);
        const Node* b = g.node(l.toNode);
        if (!a || !b) continue;
        ImVec2 p0 = pinPos(*a, l.fromPin, true), p1 = pinPos(*b, l.toPin, false);
        PinType t = a->def->outs[l.fromPin].type;
        ImU32 c = pinColor(t);
        float w = 2.0f * z;
        if (rt && t == PinType::Exec && rt->running() && rt->time() - rt->lastRun(b->id) < 0.25f) { c = IM_COL32(255, 210, 74, 255); w *= 1.6f; }
        if (k == m_hoverLink) w *= 2.0f;
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
    if (m_nodeIcon) m_nodeIcon(dl, n.def->id, ImVec2(p0.x + 12 * s, p0.y + 10 * s), 16 * s, color);
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

    // outline from the Graph settings; the selected node in cyan, a node that just ran in yellow
    // the stroke lies outside the node: its inner edge sits on the node border, in the node's category colour
    auto outside = [&](ImU32 c, float w) {
        float h = w * 0.5f;
        dl->AddRect(ImVec2(p0.x - h, p0.y - h), ImVec2(p1.x + h, p1.y + h), c, 5 * s + h, 0, w);
    };
    if (sel) outside(m_selectColor, 2 * m_outlineWeight * s);
    else if (m_outline) outside(withA(color, 128), m_outlineWeight * s);   // the node colour, half transparent
    if (hot) outside(IM_COL32(255, 210, 74, 255), 2 * m_outlineWeight * s);

    // preview square (rendered by the host): what the node outputs
    auto lay = m_layout.find(n.id);
    if (nodeThumb && lay != m_layout.end() && lay->second.thumbRow >= 0) {
        if (ImTextureID tex = nodeThumb(n)) {
            float top = p0.y + (kPinTop + lay->second.thumbRow * kRow - 8) * s, side = (kThumbRows * kRow - 6) * s;
            ImVec2 ta(p0.x + 12 * s, top), tb(ta.x + side, top + side);
            dl->AddImageRounded(tex, ta, tb, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 4 * s);
            dl->AddRect(ta, tb, withA(kBorderC, 220), 4 * s);
        }
    }

    // +/- of the colour pins that have channel pins (R, G, B)
    auto expander = [&](int i, bool output) {
        ImVec2 a, b;
        if (!expanderRect(n, i, output, a, b)) return;
        bool open = rowOf(n, i + 0, output) >= 0 && [&] {
            const auto& pins = output ? n.def->outs : n.def->ins;
            for (int c = 0; c < (int)pins.size(); ++c) if (pins[c].parent == i) return rowOf(n, c, output) >= 0;
            return false;
        }();
        ImGui::PushID(n.id * 128 + i * 2 + (output ? 1 : 0));
        Hit h = hitArea("##exp", ImVec2(a.x - 2, a.y - 2), ImVec2(b.x + 2, b.y + 2));
        ImGui::PopID();
        dl->AddRectFilled(a, b, h.hovered ? kHover : kBg, 2 * s);
        dl->AddRect(a, b, h.hovered ? kCyan : kMuted, 2 * s);
        float cx = (a.x + b.x) * 0.5f, cy = (a.y + b.y) * 0.5f, r = 2.6f * s;
        dl->AddLine(ImVec2(cx - r, cy), ImVec2(cx + r, cy), kText, 1.2f * m_dpi);
        if (!open) dl->AddLine(ImVec2(cx, cy - r), ImVec2(cx, cy + r), kText, 1.2f * m_dpi);
        if (h.pressed) {
            long long key = expandKey(n.id, i, output);
            if (m_expanded.count(key)) m_expanded.erase(key);
            else m_expanded.insert(key);
        }
    };

    // outputs: labels on the inner side of the pin
    for (int i = 0; i < (int)n.def->outs.size(); ++i) {
        const PinDef& p = n.def->outs[i];
        if (rowOf(n, i, true) < 0) continue;
        ImVec2 pp = pinPos(n, i, true);
        bool conn = false;
        for (const Link& l : g.links) if (l.fromNode == n.id && l.fromPin == i) { conn = true; break; }
        drawPinShape(dl, pp, p.type, conn, s, m_dpi);
        std::string label = p.parent >= 0 ? p.name.substr(p.name.rfind('.') + 1) : p.name;
        float lx = pp.x - 13 * s - (hasChannels(n, i, true) ? 14 * s : 0);
        if (!label.empty()) canvasText(dl, m_font, 12.5f * s, ImVec2(lx, pp.y), p.wildcard || p.parent >= 0 ? kMuted : kText, label, true);
        expander(i, true);
    }

    // inputs: labels right of the pin, inline values below the label
    for (int i = 0; i < (int)n.def->ins.size(); ++i) {
        const PinDef& p = n.def->ins[i];
        if (rowOf(n, i, false) < 0) continue;
        ImVec2 pp = pinPos(n, i, false);
        bool conn = g.inputLink(n.id, i) != nullptr;
        drawPinShape(dl, pp, p.type, conn, s, m_dpi);
        if (p.type == PinType::Exec) continue;
        float labelW = p.type == PinType::Vec3 ? 70.0f : 88.0f;
        std::string label = p.parent >= 0 ? p.name.substr(p.name.rfind('.') + 1) : p.name;
        float lx = pp.x + 13 * s + (hasChannels(n, i, false) ? 13 * s : 0) + (p.parent >= 0 ? 12 * s : 0);
        canvasText(dl, m_font, 12.5f * s, ImVec2(lx, pp.y), p.wildcard || p.parent >= 0 ? kMuted : kText, label);
        expander(i, false);
        if (p.type == PinType::Object || p.wildcard || conn) continue;
        if (p.parent >= 0 && g.inputLink(n.id, p.parent)) continue;
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
            // a channel pin edits its colour's channel
            float& fv = p.parent >= 0 ? n.inputs[p.parent].v[p.channel] : v.f;
            float speed = std::max(0.02f, std::fabs(fv) * 0.01f);
            bool changed = dragBox(dl, m_font, h, a, b, fv, speed, "%.2f", s, m_dpi);
            trackEdit(g, h.activated, changed, h.deactivated);
            break;
        }
        case PinType::Vec3: {
            if (isColorPin(p)) {   // colour swatch, click: colour picker (Maya shader graph)
                Hit h = hitArea("##c", a, b);
                dl->AddRectFilled(a, b, ImGui::ColorConvertFloat4ToU32(ImVec4(v.v.x, v.v.y, v.v.z, 1)), 3 * s);
                dl->AddRect(a, b, h.hovered ? kCyan : kBorderC, 3 * s);
                if (h.pressed) { ImGui::OpenPopup("##cpick"); m_snap = g.toJson(); m_editChanged = false; }
                if (ImGui::BeginPopup("##cpick")) {
                    if (ImGui::ColorPicker3("##picker", &v.v.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoSidePreview)) {
                        m_editChanged = true;
                        m_modified = true;
                    }
                    ImGui::EndPopup();
                    if (!ImGui::IsPopupOpen("##cpick") && m_editChanged) trackEdit(g, false, false, true);
                }
                break;
            }
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

// Canvas header (Atelier): breadcrumb, status, and the Compile / Run / Build / frame buttons on the right.
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
    canvasText(dl, m_fontBold, 12 * d, ImVec2(a.x + 34 * d, cy), kText, labels.title);
    canvasText(dl, m_font, 11 * d, ImVec2(a.x + 172 * d, cy), kMuted, labels.subtitle);
    if (!status.empty()) canvasText(dl, m_font, 11 * d, ImVec2(a.x + 290 * d, cy), kMuted, status);

    float x = b.x - (84 + 103 + 71 + 72 + 28 + 5 * 6 + 12) * d, y0 = a.y + 6 * d, h = 25 * d;
    auto btn = [&](const char* id, float w, uint32_t icon, const char* label, bool active) {
        bool pressed = barButton(dl, m_font, id, ImVec2(x, y0), ImVec2(x + w * d, y0 + h), icon, label, active, d);
        x += (w + 6) * d;
        return pressed;
    };
    if (btn("##tb_arrange", 84, 0xE8CB, "Arrange", false)) m_action = Arrange;
    if (btn("##tb_compile", 103, labels.compileIcon, labels.compile.c_str(), false)) m_action = Compile;
    if (!labels.run.empty() ? btn("##tb_play", 71, labels.runIcon, labels.run.c_str(), false) : btn("##tb_play", 71, playing ? 0xE71A : 0xE768, playing ? "Stop" : "Run", playing)) m_action = TogglePlay;
    if (btn("##tb_build", 72, labels.buildIcon, labels.build.c_str(), false)) m_action = Build;
    if (btn("##tb_frame", 28, 0xE9A6, "", false)) m_wantFrame = true;
}

// Bottom strip of the canvas: pin legend, counts and the modified / compiled state (Atelier footer).
void NodeEditor::drawFooter(ImVec2 pos, float width, const Graph& g) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float d = m_dpi;
    ImVec2 a = pos, b(pos.x + width, pos.y + kFooterPx * d);
    dl->AddRectFilled(a, b, kBg);
    const char* names[] = {"Exec", "Float", "Bool", labels.vectorPin, labels.objectPin, "String"};
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
               m_modified ? "\xE2\x97\x8F  Modified" : labels.cleanState);
}

// Overview of the graph in the corner (Atelier): tiles coloured by category.
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

// --------------------------------------------------------------------------- inspector

void NodeEditor::drawInspector(ImVec2 pos, ImVec2 size, Graph& g, const Runtime* rt) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float d = m_dpi;
    dl->AddRectFilled(pos, pos + size, kPanel);
    dl->AddLine(pos, ImVec2(pos.x, pos.y + size.y), kBorderC);
    canvasText(dl, m_font, 13 * d, pos + ImVec2(12 * d, 17 * d), kText, glyph(0xE946));
    canvasText(dl, m_fontBold, 12 * d, pos + ImVec2(33 * d, 17 * d), kText, "Inspector");
    bool group = m_selGroup >= 0 && m_selGroup < (int)g.groups.size();
    bool node = !group && !m_sel.empty() && g.node(m_sel[0]) != nullptr;
    canvasText(dl, m_font, 8 * d, pos + ImVec2(size.x - 14 * d, 17 * d), kMuted, group ? "GROUP" : (node ? "NODE" : "GRAPH"), true);

    float bodyY = pos.y + 38 * d;
    float w = size.x - 26 * d;
    ImGui::SetCursorScreenPos(ImVec2(pos.x + 13 * d, bodyY));
    ImGui::BeginChild("##inspector", ImVec2(w, std::max(1.0f, size.y - (bodyY - pos.y) - 8 * d)), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground);
    if (group) drawInspectorGroup(g, w);
    else if (node) drawInspectorNode(g, *g.node(m_sel[0]), w);
    else drawInspectorGraph(g, rt, w);
    ImGui::EndChild();
}

void NodeEditor::drawInspectorGroup(Graph& g, float w) {
    Group& gr = g.groups[m_selGroup];
    ImGui::TextColored(ImColor(gr.category == "Neutral" ? kText : catColor(gr.category)), "Backdrop");
    ImGui::TextDisabled("Title");
    char buf[160] = {};
    std::strncpy(buf, gr.title.c_str(), sizeof buf - 1);
    ImGui::SetNextItemWidth(-FLT_MIN);
    bool changed = ImGui::InputText("##gtitle", buf, sizeof buf);
    if (changed) gr.title = buf;
    trackEdit(g, ImGui::IsItemActivated(), changed, ImGui::IsItemDeactivatedAfterEdit());

    ImGui::TextDisabled("Size");
    float size[2] = {gr.w, gr.h};
    ImGui::SetNextItemWidth(-FLT_MIN);
    bool sized = ImGui::DragFloat2("##gsize", size, 1.0f, 80.0f, 4000.0f, "%.0f");
    if (sized) { gr.w = size[0]; gr.h = size[1]; }
    trackEdit(g, ImGui::IsItemActivated(), sized, ImGui::IsItemDeactivatedAfterEdit());

    ImGui::Spacing();
    ImGui::TextDisabled("Colour");
    std::vector<std::string> cats = {"Neutral"};   // the node families of the library, plus a neutral grey
    for (const NodeDef& nd : nodeDefs())
        if (std::find(cats.begin(), cats.end(), nd.category) == cats.end()) cats.push_back(nd.category);
    for (const std::string& cs : cats) {
        const char* c = cs.c_str();
        ImGui::PushID(c);
        ImGui::PushStyleColor(ImGuiCol_Text, cs == "Neutral" ? kText : catColor(c));
        bool picked = ImGui::Selectable(c, gr.category == c);
        ImGui::PopStyleColor();
        if (picked && gr.category != c) { pushUndo(g); gr.category = c; }
        ImGui::PopID();
    }
    ImGui::Spacing();
    if (ImGui::Button("Remove backdrop", ImVec2(w, 0))) {
        pushUndo(g);
        g.groups.erase(g.groups.begin() + m_selGroup);
        m_selGroup = -1;
    }
}

void NodeEditor::drawInspectorNode(Graph& g, Node& n, float w) {
    const NodeDef& def = *n.def;
    ImU32 color = catColor(def.category);
    const float d = m_dpi;

    // header card: icon, name, category, what the node does
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(p, p + ImVec2(w, 56 * d), withA(color, 30), 4 * d);
    dl->AddRect(p, p + ImVec2(w, 56 * d), withA(color, 120), 4 * d);
    if (m_nodeIcon) m_nodeIcon(dl, def.id, p + ImVec2(10 * d, 13 * d), 22 * d, color);
    canvasText(dl, m_fontBold, 14 * d, p + ImVec2(42 * d, 15 * d), kText, def.label);
    canvasText(dl, m_font, 10 * d, p + ImVec2(42 * d, 37 * d), color, def.category);
    ImGui::Dummy(ImVec2(w, 62 * d));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(ImColor(kMuted), "%s", def.help.c_str());
    ImGui::PopTextWrapPos();

    ImGui::Spacing();
    ImGui::TextDisabled("NODE PROPERTIES");
    ImGui::TextDisabled("Canvas position");
    float at[2] = {n.pos.x, n.pos.y};
    ImGui::SetNextItemWidth(-FLT_MIN);
    bool moved = ImGui::DragFloat2("##npos", at, 1.0f, -10000.0f, 10000.0f, "%.0f");
    if (moved) n.pos = float2(at[0], at[1]);
    trackEdit(g, ImGui::IsItemActivated(), moved, ImGui::IsItemDeactivatedAfterEdit());

    ImGui::Spacing();
    if (n.sub && n.sub->io) {   // compound: its name, how to edit it
        ImGui::Spacing();
        ImGui::TextDisabled("COMPOUND");
        char nb[128] = {};
        std::strncpy(nb, n.sub->io->name.c_str(), sizeof nb - 1);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputText("##cname", nb, sizeof nb, ImGuiInputTextFlags_EnterReturnsTrue) && nb[0]) {
            pushUndo(g);
            n.sub->io->name = nb;
            n.sub->name = nb;
            ++n.sub->io->version;
            m_modified = true;
        }
        ImGui::TextDisabled("%d inputs, %d outputs promoted", (int)n.sub->io->ins.size(), (int)n.sub->io->outs.size());
        if (ImGui::Button("Open compound", ImVec2(-FLT_MIN, 0))) { m_action = EnterCompound; m_enterNode = n.id; }
    }
    ImGui::Spacing();
    ImGui::TextDisabled("DEFAULT VALUES");
    bool any = false;
    for (int i = 0; i < (int)def.ins.size(); ++i) {
        const PinDef& p2 = def.ins[i];
        if (p2.type == PinType::Exec || p2.type == PinType::Object || p2.parent >= 0 || p2.wildcard || g.inputLink(n.id, i)) continue;
        any = true;
        ImGui::PushID(i);
        ImGui::TextColored(ImColor(pinColor(p2.type)), "%s", p2.name.c_str());
        ImGui::SetNextItemWidth(-FLT_MIN);
        Value& v = n.inputs[i];
        bool changed = false, done = false;
        switch (p2.type) {
        case PinType::Float:
            changed = ImGui::DragFloat("##v", &v.f, std::max(0.02f, std::fabs(v.f) * 0.01f), 0, 0, "%.3f");
            done = ImGui::IsItemDeactivatedAfterEdit();
            break;
        case PinType::Vec3:
            if (isColorPin(p2)) changed = ImGui::ColorEdit3("##v", &v.v.x, ImGuiColorEditFlags_Float);
            else changed = ImGui::DragFloat3("##v", &v.v.x, 0.05f, 0, 0, "%.2f");
            done = ImGui::IsItemDeactivatedAfterEdit();
            break;
        case PinType::String: {
            char buf[256] = {};
            std::strncpy(buf, v.s.c_str(), sizeof buf - 1);
            changed = ImGui::InputText("##v", buf, sizeof buf);
            if (changed) v.s = buf;
            done = ImGui::IsItemDeactivatedAfterEdit();
            break;
        }
        case PinType::Bool: {
            // a checkbox changes once per click: the graph before the click is the undo step
            std::string before = g.toJson();
            changed = ImGui::Checkbox("##v", &v.b);
            if (changed) { m_undo.push_back(before); m_modified = true; }
            break;
        }
        default: break;
        }
        if (p2.type != PinType::Bool) {
            trackEdit(g, ImGui::IsItemActivated(), changed, done);
        }
        ImGui::PopID();
    }
    if (!any) ImGui::TextDisabled("No value to set on this node.");

    ImGui::Spacing();
    ImGui::TextDisabled("PINS & CONNECTIONS");
    for (int side = 0; side < 2; ++side) {
        const auto& pins = side ? def.outs : def.ins;
        for (int i = 0; i < (int)pins.size(); ++i) {
            ImVec2 c = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddCircleFilled(c + ImVec2(5 * d, 8 * d), 3 * d, pinColor(pins[i].type));
            ImGui::Indent(14 * d);
            ImGui::TextUnformatted(pins[i].name.empty() ? (side ? "Out" : "In") : pins[i].name.c_str());
            ImGui::SameLine(w - 62 * d);
            ImGui::TextDisabled("%s", pinTypeName(pins[i].type));
            ImGui::Unindent(14 * d);
        }
    }
    ImGui::Spacing();
    if (ImGui::Button("Disconnect all pins", ImVec2(w, 0))) {
        pushUndo(g);
        int id = n.id;
        g.links.erase(std::remove_if(g.links.begin(), g.links.end(),
                                     [&](const Link& l) { return l.fromNode == id || l.toNode == id; }),
                      g.links.end());
    }
    if (m_sel.size() > 1) ImGui::TextColored(ImColor(kCyan), "%d nodes selected", (int)m_sel.size());
}

void NodeEditor::drawInspectorGraph(Graph& g, const Runtime* rt, float w) {
    ImGui::TextUnformatted(labels.title.c_str());
    ImGui::TextDisabled("%s", labels.description.c_str());
    ImGui::Spacing();
    ImGui::TextDisabled("GRAPH SUMMARY");
    auto row = [&](const char* label, const std::string& value, ImU32 col) {
        ImGui::TextDisabled("%s", label);
        ImGui::SameLine(w - 90 * m_dpi);
        ImGui::TextColored(ImColor(col), "%s", value.c_str());
    };
    row("Nodes", std::to_string(g.nodes.size()), kText);
    row("Links", std::to_string(g.links.size()), kText);
    row("Groups", std::to_string(g.groups.size()), kText);
    row("Status", m_modified ? "Modified" : "Compiled", m_modified ? kAmber : kGreen);

    ImGui::Spacing();
    ImGui::TextDisabled("CANVAS CONTROLS");
    ImGui::TextDisabled("Wheel to zoom, middle drag to pan");
    ImGui::TextDisabled("Right click to add a node");
    ImGui::TextDisabled("Drag pins to connect, drag a linked input to detach it, Alt+click to detach");
    ImGui::TextDisabled("Backdrops: Ctrl+G around the selection, drag the title to move, double click to rename, drag the corner to resize");

    ImGui::Spacing();
    ImGui::TextDisabled("NODE FAMILIES");
    std::vector<std::string> fams;   // categories of the node library, in order
    for (const NodeDef& d : nodeDefs())
        if (std::find(fams.begin(), fams.end(), d.category) == fams.end()) fams.push_back(d.category);
    for (const std::string& fs : fams) {
        const char* f = fs.c_str();
        ImVec2 c = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddCircleFilled(c + ImVec2(5 * m_dpi, 8 * m_dpi), 4 * m_dpi, catColor(f));
        ImGui::Indent(16 * m_dpi);
        ImGui::TextUnformatted(f);
        ImGui::Unindent(16 * m_dpi);
    }

    if (rt && !rt->trace.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("RUN RESULT");
        ImGui::TextUnformatted((std::to_string(rt->trace.size()) + " executed steps").c_str());
        for (const auto& kv : rt->variables()) {
            std::string line = kv.first + " = " + kv.second.asString();
            ImGui::TextDisabled("%s", line.c_str());
        }
    }
}

// --------------------------------------------------------------------------- output panel

void NodeEditor::drawOutput(ImVec2 pos, ImVec2 size, Graph& g, const Runtime* rt) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float d = m_dpi;
    dl->AddRectFilled(pos, pos + size, kPanel, 4 * d);
    dl->AddRect(pos, pos + size, kBorderC, 4 * d);

    // tabs and the clear button, as in the Atelier output panel
    struct Tab { const char* id; const char* label; uint32_t icon; float width; };
    static const Tab tabs[] = {{"##out0", "Output", 0xE946, 87}, {"##out1", "Compiler Results", 0xE73E, 142},
                               {"##out2", "Execution Trace", 0xE768, 140}};
    float x = pos.x + 8 * d;
    for (int i = 0; i < 3; ++i) {
        if (barButton(dl, m_font, tabs[i].id, ImVec2(x, pos.y + 4 * d), ImVec2(x + tabs[i].width * d, pos.y + 29 * d),
                      tabs[i].icon, tabs[i].label, m_outTab == i, d))
            m_outTab = i;
        x += (tabs[i].width + 5) * d;
    }
    if (barButton(dl, m_font, "##outclear", ImVec2(pos.x + size.x - 37 * d, pos.y + 5 * d),
                  ImVec2(pos.x + size.x - 11 * d, pos.y + 28 * d), 0xE74D, "", false, d)) {
        m_messages.clear();
        m_diagnostics.clear();
    }

    ImGui::SetCursorScreenPos(ImVec2(pos.x + 10 * d, pos.y + 36 * d));
    ImGui::BeginChild("##outrows", ImVec2(size.x - 20 * d, std::max(1.0f, size.y - 42 * d)), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground);
    if (m_outTab == 2) {
        // execution trace: the nodes run since Play, click to select one
        if (!rt || rt->trace.empty()) {
            ImGui::TextDisabled("Run the graph to see its execution trace.");
        } else {
            int index = 0;
            for (int id : rt->trace) {
                ++index;
                const Node* n = g.node(id);
                std::string label = std::to_string(index) + "   " + (n ? n->def->label : std::string("(removed node)")) +
                                    "   #" + std::to_string(id);
                ImGui::PushID(index);
                ImGui::PushStyleColor(ImGuiCol_Text, kGreen);
                if (ImGui::Selectable(label.c_str(), selected(id)) && n) focusNode(g, id);
                ImGui::PopStyleColor();
                ImGui::PopID();
            }
        }
    } else if (m_outTab == 1) {
        // compiler results of the last Compile, click a row to select its node
        if (m_diagnostics.empty()) {
            ImGui::TextDisabled(m_checked ? "No problem found." : "Press Compile to check the graph.");
        }
        for (size_t i = 0; i < m_diagnostics.size(); ++i) {
            const Diagnostic& dg = m_diagnostics[i];
            std::string label = glyph(levelIcon(dg.level)) + "  " + dg.text;
            ImGui::PushID((int)i);
            ImGui::PushStyleColor(ImGuiCol_Text, levelColor(dg.level));
            if (ImGui::Selectable(label.c_str()) && dg.node) focusNode(g, dg.node);
            ImGui::PopStyleColor();
            ImGui::PopID();
        }
    } else {
        // output log: messages of the editor, then the runtime log of the last Play
        for (size_t i = 0; i < m_messages.size(); ++i) {
            const OutputMessage& m = m_messages[i];
            std::string label = glyph(levelIcon(m.level)) + "  " + m.text;
            ImGui::PushID((int)i);
            ImGui::PushStyleColor(ImGuiCol_Text, levelColor(m.level));
            if (ImGui::Selectable(label.c_str()) && m.node) focusNode(g, m.node);
            ImGui::PopStyleColor();
            ImGui::PopID();
        }
        if (rt) {
            for (size_t i = 0; i < rt->log.size(); ++i) {
                const std::string& line = rt->log[i];
                bool err = line.rfind("Error", 0) == 0;
                ImGui::PushStyleColor(ImGuiCol_Text, err ? kRed : kText);
                ImGui::TextUnformatted(line.c_str());
                ImGui::PopStyleColor();
            }
        }
        if (m_messages.empty() && (!rt || rt->log.empty())) ImGui::TextDisabled("Nothing to show yet.");
    }
    if (m_scrollOutput) {
        ImGui::SetScrollHereY(1.0f);
        m_scrollOutput = false;
    }
    ImGui::EndChild();
}

// Creates the node picked in the popup, wired to the pin the popup was opened from.
void NodeEditor::addNodeFromMenu(Graph& g, const NodeDef& d) {
    pushUndo(g);
    float2 at(std::round(m_menuGraph.x / 8) * 8, std::round(m_menuGraph.y / 8) * 8);
    int id = d.id == "compound" ? g.addCompound(at) : g.addNode(d.id, at);   // a compound comes with its Input and Output
    g.refreshDefs();
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
    m_selGroup = -1;
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
        ImGui::InputTextWithHint("##search", fromNode ? "Compatible nodes… type to filter" : "Type to search for a node…",
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
        if (q.empty() && !fromNode) {
            // adds a group frame at the popup position (Atelier "Add comment group")
            if (!m_sel.empty() && ImGui::Selectable("+ Backdrop around selection  (Ctrl+G)", false, 0, ImVec2(0, 22 * d))) {
                addBackdrop(g);
                ImGui::CloseCurrentPopup();
            }
            if (!m_sel.empty() && ImGui::Selectable("+ Compound from selection  (Ctrl+Shift+G)", false, 0, ImVec2(0, 22 * d))) {
                collapseSelection(g);
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::Selectable("+ Add backdrop", false, 0, ImVec2(0, 22 * d))) {
                pushUndo(g);
                Group gr;
                gr.title = "BACKDROP";
                gr.category = "Neutral";
                gr.x = std::round(m_menuGraph.x / 8) * 8;
                gr.y = std::round(m_menuGraph.y / 8) * 8;
                g.groups.insert(g.groups.begin(), gr);
                m_selGroup = 0;
                m_sel.clear();
                ImGui::CloseCurrentPopup();
            }
        }
        std::string lastCat;
        for (const NodeDef& nd : nodeDefs()) {
            if (!compatibleWithPin(nd)) continue;
            if (nd.id == "compound.input" || nd.id == "compound.output") continue;   // created with the compound
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
        if (!first) ImGui::TextDisabled("No node");
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
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            if (!m_sel.empty()) deleteSelection(g);
            else if (m_selGroup >= 0 && m_selGroup < (int)g.groups.size()) {
                pushUndo(g);
                g.groups.erase(g.groups.begin() + m_selGroup);
                m_selGroup = -1;
            }
        }
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D)) duplicateSelection(g);
        if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_G)) addBackdrop(g);
        if (ctrl && shift && ImGui::IsKeyPressed(ImGuiKey_G)) collapseSelection(g);
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z)) undo(g);
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_A)) { m_sel.clear(); m_selGroup = -1; for (const Node& n : g.nodes) m_sel.push_back(n.id); }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { m_sel.clear(); m_selGroup = -1; }
        if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_F)) frameAll(g);
    }

    // presses
    if (inArea && ImGui::IsMouseClicked(0) && m_drag == DragNone && !overWidget) {
        m_pressPos = mouse;
        m_dragMoved = false;
        PinRef p = hitPin(g, mouse);
        if (p.pin >= 0) {
            if (alt) {
                pushUndo(g);
                g.disconnectPin(p.node, p.pin, p.output);
            } else if (!p.output && g.inputLink(p.node, p.pin)) {
                // pick up an existing link (data or flow) from its input end: dragged out, it detaches
                m_picked = *g.inputLink(p.node, p.pin);
                PinRef src{m_picked.fromNode, m_picked.fromPin, true};
                pushUndo(g);
                g.removeLink(m_picked);
                m_linkFrom = src;
                m_linkPicked = true;
                m_drag = DragLink;
            } else {
                m_linkFrom = p;
                m_linkPicked = false;
                m_drag = DragLink;
            }
        } else if (int id = hitNode(g, mouse)) {
            if (ImGui::IsMouseDoubleClicked(0)) {   // double click on a compound: open it
                const Node* nd = g.node(id);
                if (nd && nd->sub) { m_action = EnterCompound; m_enterNode = id; }
            }
            if (ctrl || shift) {
                auto it = std::find(m_sel.begin(), m_sel.end(), id);
                if (it != m_sel.end()) m_sel.erase(it); else m_sel.push_back(id);
            } else if (!selected(id)) {
                m_sel = {id};
            }
            m_selGroup = -1;
            // bring to front
            auto it = std::find_if(g.nodes.begin(), g.nodes.end(), [&](const Node& n) { return n.id == id; });
            if (it != g.nodes.end() && it + 1 != g.nodes.end()) { Node n = *it; g.nodes.erase(it); g.nodes.push_back(n); }
            if (selected(id)) m_drag = DragNodes;
        } else {
            int gi = hitGroupTitle(g, mouse);
            int grip = hitGroupGrip(g, mouse);
            if (grip >= 0) {
                m_sel.clear();
                m_selGroup = grip;
                m_drag = DragGroupResize;
            } else if (gi >= 0 && ImGui::IsMouseDoubleClicked(0)) {
                m_selGroup = gi;
                m_renameGroup = gi;
                strncpy_s(m_renameBuf, g.groups[gi].title.c_str(), _TRUNCATE);
            } else if (gi >= 0) {
                // a group moves with the nodes whose corner is inside its frame
                m_sel.clear();
                m_selGroup = gi;
                m_groupNodes.clear();
                const Group& gr = g.groups[gi];
                for (const Node& n : g.nodes)
                    if (n.pos.x >= gr.x && n.pos.x < gr.x + gr.w && n.pos.y >= gr.y && n.pos.y < gr.y + gr.h) m_groupNodes.push_back(n.id);
                m_drag = DragGroup;
            } else {
                if (!ctrl && !shift) { m_sel.clear(); m_selGroup = -1; }
                m_drag = DragMarquee;
            }
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
    case DragGroup:
        if (!io.MouseDown[0]) {
            m_drag = DragNone;
        } else if (delta.x != 0 || delta.y != 0) {
            if (!m_dragMoved) { pushUndo(g); m_dragMoved = true; }
            float2 dg(delta.x / scale(), delta.y / scale());
            if (m_selGroup >= 0 && m_selGroup < (int)g.groups.size()) {
                g.groups[m_selGroup].x += dg.x;
                g.groups[m_selGroup].y += dg.y;
            }
            for (int id : m_groupNodes)
                if (Node* n = g.node(id)) n->pos = n->pos + dg;
            m_modified = true;
        }
        break;
    case DragGroupResize:
        if (!io.MouseDown[0]) {
            if (m_selGroup >= 0 && m_selGroup < (int)g.groups.size()) {
                Group& gr = g.groups[m_selGroup];
                gr.w = std::round(gr.w / 8) * 8;
                gr.h = std::round(gr.h / 8) * 8;
            }
            m_drag = DragNone;
        } else if ((delta.x != 0 || delta.y != 0) && m_selGroup >= 0 && m_selGroup < (int)g.groups.size()) {
            if (!m_dragMoved) { pushUndo(g); m_dragMoved = true; }
            Group& gr = g.groups[m_selGroup];
            ImVec2 gp = toGraph(mouse);
            gr.w = std::max(120.0f, gp.x - gr.x);
            gr.h = std::max(kGroupTitle + 40.0f, gp.y - gr.y);
            m_modified = true;
        }
        break;
    case DragLink:
        if (!io.MouseDown[0]) {
            m_drag = DragNone;
            PinRef t = hitPin(g, mouse);
            bool connected = false;
            if (t.pin >= 0 && t.output != m_linkFrom.output) {
                std::string err;
                pushUndo(g);
                bool ok = m_linkFrom.output ? g.connect(m_linkFrom.node, m_linkFrom.pin, t.node, t.pin, &err)
                                            : g.connect(t.node, t.pin, m_linkFrom.node, m_linkFrom.pin, &err);
                if (!ok) { m_undo.pop_back(); status = "Connection impossible: " + err; }
                else { status.clear(); connected = true; }
            }
            if (m_linkPicked) {
                // taken from an input: on a pin that refuses it, it goes back; anywhere else it stays detached
                if (!connected && t.pin >= 0) g.connect(m_picked.fromNode, m_picked.fromPin, m_picked.toNode, m_picked.toPin);
                else if (!connected) status = "Link detached";
            } else if (!connected && t.pin < 0 && !hitNode(g, mouse) && inArea) {
                openMenu(mouse, m_linkFrom);   // dropped in the void: pick a node to connect
            }
            m_linkPicked = false;
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

namespace game {

void setGraphPalette(const GraphPalette& p) {
    kCanvas = p.canvas;
    kBg = p.canvas;
    kBar = p.bar;
    kPanel = p.panel;
    kNode = p.node;
    kNodeOff = p.panel;
    kRaised = p.raised;
    kHover = p.hover;
    kBorderC = p.border;
    kText = p.text;
    kMuted = p.muted;
    kCyan = p.accent;
    kSelect = p.select;
    kGridDot = withA(p.grid, 150);
    kGridLine = withA(p.grid, 95);
    kOverlay = withA(p.node, 220);
    kMinimap = withA(p.panel, 240);
    kPinExec = p.text;
}

} // namespace game
