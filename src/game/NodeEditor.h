// game - Blueprint-style node graph editor drawn with the vp3d immediate UI.
// Self-contained: the host gives it a rectangle, the graph and (optionally) the
// running Runtime for execution highlights, and reads back the requested action.
#pragma once
#include "Runtime.h"
#include "vp3d/UI.h"

namespace game {

class NodeEditor {
public:
    enum Action { None, TogglePlay, Save, Close, Build };

    // Draws the editor over `area` and handles its mouse / keyboard input.
    void draw(vp3d::UI& ui, const vp3d::Rect& area, Graph& g, const Runtime* rt, bool playing);
    Action takeAction() { Action a = m_action; m_action = None; return a; }
    // Centres the view on all nodes.
    void frameAll(const Graph&) { m_wantFrame = true; }   // applied at the next draw (area known)
    void resetView() { m_pan = float2(-40, -60); m_zoom = 1.0f; }
    bool modified() const { return m_modified; }
    void clearModified() { m_modified = false; }
    std::string status;                   // small message shown in the editor toolbar

private:
    struct PinRef { int node = 0, pin = -1; bool output = false; };
    float2 toScreen(float2 g) const { return float2(m_area.x + (g.x - m_pan.x) * m_zoom, m_area.y + (g.y - m_pan.y) * m_zoom); }
    float2 toGraph(float2 s) const { return float2((s.x - m_area.x) / m_zoom + m_pan.x, (s.y - m_area.y) / m_zoom + m_pan.y); }
    float nodeWidth(const Node& n) const;
    int rowCount(const Node& n) const;
    vp3d::Rect nodeRect(const Node& n) const;        // screen space
    float2 pinPos(const Node& n, int pin, bool output) const;
    bool pinVisible(const Node& n, int pin, bool output) const;
    void drawNode(vp3d::UI& ui, Graph& g, Node& n, const Runtime* rt);
    void drawLinks(vp3d::UI& ui, Graph& g, const Runtime* rt);
    void drawMenu(vp3d::UI& ui, Graph& g);
    void drawToolbar(vp3d::UI& ui, bool playing);
    void drawLog(vp3d::UI& ui, const Runtime* rt);
    void handleInput(vp3d::UI& ui, Graph& g);
    PinRef hitPin(const Graph& g, float mx, float my) const;
    int hitNode(const Graph& g, float mx, float my) const;
    void openMenu(float2 screen, PinRef from);
    void doFrame(const Graph& g);
    void pushUndo(const Graph& g);
    void undo(Graph& g);
    void deleteSelection(Graph& g);
    void duplicateSelection(Graph& g);
    bool selected(int id) const;

    vp3d::Rect m_area;
    float2 m_pan{-40, -60};
    float m_zoom = 1.0f;
    std::vector<int> m_sel;
    std::vector<std::string> m_undo;
    Action m_action = None;
    bool m_modified = false;
    bool m_wantFrame = false;

    // interaction
    enum Drag { DragNone, DragNodes, DragLink, DragPan, DragMarquee };
    Drag m_drag = DragNone;
    PinRef m_linkFrom;
    float2 m_pressPos, m_lastMouse;
    bool m_dragMoved = false;
    bool m_overWidget = false;
    int m_rightPressed = 0;      // 1 = right button pressed in the area, waiting for release
    float2 m_rightPos;

    // add-node menu
    bool m_menu = false;
    float2 m_menuScreen, m_menuGraph;
    PinRef m_menuFrom;
    std::wstring m_search;
    float m_menuScroll = 0;
};

} // namespace game
