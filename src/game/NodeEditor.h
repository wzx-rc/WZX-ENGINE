// game - Blueprint-style node graph editor drawn with Dear ImGui, in the Atelier graph style: canvas
// (grid, groups, links, nodes, pins), the Inspector on the right and the Output panel at the bottom.
// The canvas is drawn with the draw list; the inline values are native ImGui widgets and the add-node
// menu is an ImGui popup. The host gives it a rectangle, the graph and (optionally) the running
// Runtime for execution highlights and the trace, and reads back the requested action.
#pragma once
#include "Runtime.h"
#include "imgui.h"
#include <string>

struct ImFont;

namespace game {

class NodeEditor {
public:
    enum Action { None, TogglePlay, Save, Compile, Close, Build, Arrange, Execute };

    // Drag & drop payload of a node type (its id, no terminator) dropped from the node library on the canvas.
    static constexpr const char* kNodePayload = "WZX_NODEDEF";

    // Colours of the graph (the Interface Theme); the Atelier Blue 01 values until the host sets them.
    struct Colors {
        ImU32 bg, panel, raised, hover, bar, border, canvas, node, nodeOff, gridDot, gridLine, overlay, minimap;
        ImU32 text, muted, accent, select, pinExec, frameHover;
        bool light = false;
    };
    static void setColors(const Colors& c);

    // Fonts for the canvas text (regular and bold); the ImGui default font is used when null.
    void setFonts(ImFont* regular, ImFont* bold) { m_font = regular; m_fontBold = bold; }

    // Outlines around the nodes (Graph settings): off hides them; the selected node keeps its highlight.
    void setOutline(bool on, float weight, ImU32 selectColor) { m_outline = on; m_outlineWeight = weight; m_selectColor = selectColor; }

    // Draws the icon of a node type (the app owns the icon set); called with the node type id.
    using NodeIconFn = void (*)(ImDrawList* dl, const std::string& nodeId, ImVec2 p, float size, ImU32 color);
    void setNodeIconDrawer(NodeIconFn f) { m_nodeIcon = f; }

    // Draws the whole graph workspace in the rectangle `pos` / `size` (ImGui pixels): canvas header,
    // canvas, Inspector (right) and Output panel (bottom), and handles their input.
    void draw(ImVec2 pos, ImVec2 size, float dpi, Graph& g, const Runtime* rt, bool playing);
    Action takeAction() { Action a = m_action; m_action = None; return a; }
    // Centres the view on all nodes at the next draw.
    void frameAll(const Graph&) { m_wantFrame = true; m_frameSelection = false; }
    // Output panel height in DIPs (dragged from its top edge); the host keeps it in the settings.
    float outputHeight() const { return m_outH; }
    void setOutputHeight(float h) { m_outH = h; }
    // Execute: plays the execution path of a dry run (node ids in order) along the wires, and lists it in the trace.
    void showExecution(const std::vector<int>& trace);
    bool showingExecution() const { return !m_execTrace.empty(); }
    // Arrange: moves the nodes that overlap down, under the node above them (one undo step). Returns the count moved.
    int arrangeOverlaps(Graph& g);
    // Adds a node of the library at the centre of the view (from the node library panel).
    void addNodeAtView(Graph& g, const NodeDef& d);
    void resetView() { m_pan = ImVec2(-40, -60); m_zoom = 1.0f; }
    // Another graph is shown (scene change): selection, undo, results and execution path are cleared.
    void reset();
    bool modified() const { return m_modified; }
    void clearModified() { m_modified = false; }
    std::string status;                   // small message shown in the canvas header

    // Output panel: a message (0 info, 1 warning, 2 error, 3 success) and the node it concerns (0 = none).
    void addMessage(int level, const std::string& text, int node = 0);
    // Results of the last Compile (shown in the "Compiler Results" tab).
    void setDiagnostics(const std::vector<Diagnostic>& d);
    void setOutputTab(int tab) { m_outTab = tab < 0 ? 0 : (tab > 2 ? 2 : tab); }   // 0 Output, 1 Compiler Results, 2 Execution Trace

private:
    struct PinRef { int node = 0, pin = -1; bool output = false; };
    struct OutputMessage { int level = 0; std::string text; int node = 0; };
    enum Drag { DragNone, DragNodes, DragLink, DragPan, DragMarquee, DragGroup };

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
    int hitGroupTitle(const Graph& g, ImVec2 p) const;   // index of the group whose title bar is under p, or -1
    bool selected(int id) const;

    void handleInput(Graph& g, bool inArea, bool overWidget);
    void drawCanvas(Graph& g, const Runtime* rt);
    void drawGroups(const Graph& g);
    void drawLinks(const Graph& g, const Runtime* rt);
    void drawNode(Graph& g, Node& n, const Runtime* rt);
    void drawHint(const Graph& g);
    void drawToolbar(ImVec2 pos, float width, bool playing);
    void drawFooter(ImVec2 pos, float width, const Graph& g);
    void drawMinimap(const Graph& g);
    void drawInspector(ImVec2 pos, ImVec2 size, Graph& g, const Runtime* rt);
    void drawInspectorGroup(Graph& g, float w);
    void drawInspectorNode(Graph& g, Node& n, float w);
    void drawInspectorGraph(Graph& g, const Runtime* rt, float w);
    void drawOutput(ImVec2 pos, ImVec2 size, Graph& g, const Runtime* rt);
    void focusNode(const Graph& g, int id);
    void trackEdit(const Graph& g, bool activated, bool changed, bool deactivated);
    void drawMenu(Graph& g);
    void addNodeFromMenu(Graph& g, const NodeDef& d);

    void openMenu(ImVec2 screen, PinRef from);
    void doFrame(const Graph& g, bool selectionOnly = false);
    void pushUndo(const Graph& g);
    void undo(Graph& g);
    void deleteSelection(Graph& g);
    void duplicateSelection(Graph& g);

    ImFont* m_font = nullptr;
    ImFont* m_fontBold = nullptr;
    NodeIconFn m_nodeIcon = nullptr;
    bool m_outline = true;
    float m_outlineWeight = 0.5f;
    ImU32 m_selectColor = IM_COL32(102, 186, 208, 255);   // outline of the selected node (Graph settings)
    float m_dpi = 1.0f;
    ImVec2 m_origin;                       // canvas top-left (screen)
    ImVec2 m_canvas;                       // canvas size (screen)
    ImVec2 m_pan{-40, -60};                // graph units at the top-left corner
    float m_zoom = 1.0f;
    std::vector<int> m_sel;
    int m_selGroup = -1;                   // selected group (index), -1 = none
    std::vector<std::string> m_undo;
    Action m_action = None;
    bool m_modified = true;                // not compiled or saved yet
    bool m_wantFrame = false;
    bool m_frameSelection = false;         // F with a selection: frame the selected nodes only
    float m_outH = 151;                    // output panel height (DIPs)
    ImVec2 m_mapMin, m_mapMax;             // overview map (screen)
    bool m_mapDrag = false;                // the view is dragged from the overview map
    // Execute: path of the last dry run, played step by step from m_execStart (seconds, ImGui time)
    std::vector<int> m_execTrace;
    double m_execStart = 0;
    float execProgress() const;            // steps reached (fractional), or -1 when no execution is shown
    std::vector<int> m_execList;           // path of the last Execute (Execution Trace tab)
    std::vector<int> m_execLinks;          // per step: the exec link that led to it (-1 none), this frame
    void updateExecution(const Graph& g);
    int execState(int nodeId, int& step) const;

    // interaction
    Drag m_drag = DragNone;
    PinRef m_linkFrom;
    bool m_linkPicked = false;             // the link being dragged was taken from an input (dropped in the void, it stays detached)
    Link m_picked;                         // that link, put back when it cannot connect
    ImVec2 m_pressPos, m_rightPos;
    bool m_dragMoved = false;
    int m_rightButton = 0;
    std::vector<int> m_groupNodes;         // nodes inside the group being moved

    // add-node popup
    bool m_menuRequest = false;            // open the popup at the next draw
    std::string m_snap;                    // graph before the inline edit in progress (undo)
    bool m_editChanged = false;            // the inline edit in progress has changed the value
    bool m_focusSearch = false;
    ImVec2 m_menuScreen, m_menuGraph;
    PinRef m_menuFrom;
    char m_search[128] = {};
    int m_menuIndex = 0;                   // highlighted row of the add-node popup (arrow keys)
    bool m_menuScroll = false;             // bring the highlighted row into view

    // output panel
    std::vector<OutputMessage> m_messages;
    std::vector<Diagnostic> m_diagnostics;
    int m_outTab = 0;                      // 0 Output, 1 Compiler Results, 2 Execution Trace
    bool m_scrollOutput = false;
    bool m_checked = false;                // a Compile has run (Compiler Results)
};

// Bright colour of a node category (library headers).
ImU32 categoryAccentColor(const std::string& category);

} // namespace game
