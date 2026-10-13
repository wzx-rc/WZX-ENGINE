// game - Blueprint-style node graph editor drawn with Dear ImGui. The canvas (grid, links,
// nodes, pins) is drawn with the draw list; the inline values are native ImGui widgets and the
// add-node menu is an ImGui popup. The host gives it a rectangle, the graph and (optionally)
// the running Runtime for execution highlights, and reads back the requested action.
#pragma once
#include "Runtime.h"
#include "imgui.h"
#include <string>

struct ImFont;

namespace game {

class NodeEditor {
public:
    enum Action { None, TogglePlay, Save, Close, Build };

    // Fonts for the canvas text (regular and bold); the ImGui default font is used when null.
    void setFonts(ImFont* regular, ImFont* bold) { m_font = regular; m_fontBold = bold; }

    // Draws the editor in the rectangle `pos` / `size` (ImGui pixels) and handles its input.
    void draw(ImVec2 pos, ImVec2 size, float dpi, Graph& g, const Runtime* rt, bool playing);
    Action takeAction() { Action a = m_action; m_action = None; return a; }
    // Centres the view on all nodes at the next draw.
    void frameAll(const Graph&) { m_wantFrame = true; }
    // Adds a node of the library at the centre of the view (from the node library panel).
    void addNodeAtView(Graph& g, const NodeDef& d);
    void resetView() { m_pan = ImVec2(-40, -60); m_zoom = 1.0f; }
    bool modified() const { return m_modified; }
    void clearModified() { m_modified = false; }
    std::string status;                   // small message shown in the toolbar

private:
    struct PinRef { int node = 0, pin = -1; bool output = false; };
    enum Drag { DragNone, DragNodes, DragLink, DragPan, DragMarquee };

    float scale() const { return m_zoom * m_dpi; }     // graph units -> ImGui pixels
    ImVec2 toScreen(ImVec2 g) const { return m_origin + (g - m_pan) * scale(); }
    ImVec2 toGraph(ImVec2 s) const { return (s - m_origin) / scale() + m_pan; }
    float nodeWidth(const Node& n) const;
    int rowCount(const Node& n) const;
    ImVec2 nodeMin(const Node& n) const;                 // screen
    ImVec2 nodeMax(const Node& n) const;                 // screen
    ImVec2 pinPos(const Node& n, int pin, bool output) const;
    bool inlineArea(const Graph& g, const Node& n, int pin, ImVec2& a, ImVec2& b) const;
    bool overInlineWidget(const Graph& g, ImVec2 p) const;
    PinRef hitPin(const Graph& g, ImVec2 p) const;
    int hitNode(const Graph& g, ImVec2 p) const;
    bool selected(int id) const;

    void handleInput(Graph& g, bool inArea, bool overWidget);
    void drawCanvas(Graph& g, const Runtime* rt);
    void drawLinks(const Graph& g, const Runtime* rt);
    void drawNode(Graph& g, Node& n, const Runtime* rt);
    void drawLog(const Runtime* rt);
    void drawHint(const Graph& g);
    void drawToolbar(ImVec2 pos, float width, bool playing);
    void drawFooter(ImVec2 pos, ImVec2 size, const Graph& g);
    void drawMinimap(const Graph& g);
    void trackEdit(const Graph& g, bool activated, bool changed, bool deactivated);
    void drawMenu(Graph& g);
    void addNodeFromMenu(Graph& g, const NodeDef& d);

    void openMenu(ImVec2 screen, PinRef from);
    void doFrame(const Graph& g);
    void pushUndo(const Graph& g);
    void undo(Graph& g);
    void deleteSelection(Graph& g);
    void duplicateSelection(Graph& g);

    ImFont* m_font = nullptr;
    ImFont* m_fontBold = nullptr;
    float m_dpi = 1.0f;
    ImVec2 m_origin;                       // canvas top-left (screen)
    ImVec2 m_canvas;                       // canvas size (screen)
    ImVec2 m_pan{-40, -60};                // graph units at the top-left corner
    float m_zoom = 1.0f;
    std::vector<int> m_sel;
    std::vector<std::string> m_undo;
    Action m_action = None;
    bool m_modified = false;
    bool m_wantFrame = false;

    // interaction
    Drag m_drag = DragNone;
    PinRef m_linkFrom;
    ImVec2 m_pressPos, m_rightPos;
    bool m_dragMoved = false;
    int m_rightButton = 0;

    // add-node popup
    bool m_menuRequest = false;            // open the popup at the next draw
    std::string m_snap;                    // graph before the inline edit in progress (undo)
    bool m_editChanged = false;            // the inline edit in progress has changed the value
    bool m_focusSearch = false;
    ImVec2 m_menuScreen, m_menuGraph;
    PinRef m_menuFrom;
    char m_search[128] = {};
};

// Bright colour of a node category (library headers).
ImU32 categoryAccentColor(const std::string& category);

} // namespace game
