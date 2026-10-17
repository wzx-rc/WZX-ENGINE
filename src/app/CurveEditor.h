// Curve tools of the Scene editor (Create > Curve): creation by clicks on the ground (CV, EP, Bezier, Linear),
// curve primitives, display over the viewport, selection, point editing (F8) with its own move manipulator,
// inspector section and .wzx read / write. The curves are scene objects (part "CURVE", mesh -1): undo,
// duplicate, delete, groups and the Scene Manager work as for the meshes.
#pragma once
#include "App.h"
#include <functional>

namespace app {

// .wzx: the "curve" block of an object
Json curveToJson(const CurveData& c);
bool curveFromJson(const Json& j, CurveData& c);   // false when j is not a curve block

class CurveEditor {
public:
    explicit CurveEditor(App& app) : a(app) {}

    // hooks of the editor
    void beginFrame();                       // App::frame, before the ImGui frame (test sequence)
    bool keys();                             // before App::handleShortcuts: true = a key was used here
    bool input(bool inView, bool placing);   // viewport left button: true = used here (no selection / gizmo after)
    void draw(ImDrawList* dl);               // over the viewport image, under the gizmo
    bool hidesGizmo() const { return m_mode != MODE_NONE; }
    void menu(float menuScale);              // Create > Curve
    bool worldBounds(const Object& o, float3& mn, float3& mx) const;
    void inspector(float x, float& y, float w);   // Actor tab of a curve (AtelierUI.cpp, CurveInspector.inl)

    // tools
    void startCreate(int type);
    void addPrimitive(int shape);
    void startDraw(int shape);   // Create > Curve primitives: drawn in the viewport
    void enterPoints();
    void exitMode();
    bool editingPoints() const { return m_mode == MODE_POINTS; }
    bool creating() const { return m_mode == MODE_CREATE; }

    std::wstring testReport;                 // --curve-test: report file

private:
    enum Mode { MODE_NONE, MODE_CREATE, MODE_POINTS, MODE_DRAW };
    enum Part { PART_POINT = 0, PART_IN = 1, PART_OUT = 2 };
    struct Item { int index; int part; };
    struct Start { Item item; float3 local; };

    App& a;
    int m_mode = MODE_NONE;
    // creation
    int m_createType = CURVE_CV;
    CurveData m_draft;                 // points in world space while created
    bool m_draftDrag = false;          // Bezier: the button is held after placing an anchor (pulls its handles)
    float2 m_pressAt{0, 0};
    // point editing
    int m_obj = -1;                    // object edited in point mode
    std::vector<Item> m_sel;
    int m_hoverAxis = -1;              // manipulator: 0..2 axis, 3 free (centre square)
    bool m_dragging = false;
    int m_dragAxis = -1;
    float3 m_dragPivot, m_dragDir, m_dragHit0;
    float m_dragT0 = 0;
    std::vector<Start> m_dragStart;
    bool m_dragMoved = false;
    bool m_pendingFree = false;        // pressed on a point: a drag moves it freely
    bool m_pulling = false;            // Ctrl+drag from a Bezier point: pulls symmetric tangents
    int m_pullIndex = -1;
    bool m_marquee = false;
    float2 m_marq0{0, 0};
    int m_testFrame = 0;
    // primitives drawn with the mouse (3ds Max style): press, drag the first size, then move for the second one
    int m_drawShape = 0, m_drawStep = 0;   // step 0 waits for the press, 1 drags, 2 sets the second size (click ends)
    float3 m_drawStart{0, 0, 0}, m_drawCur{0, 0, 0};
    float m_draw2 = 0, m_drawPx0 = 0;
    // fillet (3ds Max): drag a point upwards, or a radius applied to the selected points
    bool m_filletTool = false, m_filleting = false;
    CurveData m_filletBase;
    std::vector<int> m_filletIdx;
    float m_filletPx0 = 0, m_filletR = 0, m_filletRadius = 0.5f;
    float3 m_filletAt{0, 0, 0};
    // manipulator of the points: shown after a click on a point; hidden while a point is dragged directly (press and
    // move: the point follows the mouse), after such a drag, and with the fillet tool
    bool m_manipHidden = false;
    bool manipulatorShown() const;

    // mouse seen by the tools: ImGui's, or the scripted mouse of --curve-test (interactions tested frame by frame)
    struct MouseState { float x = 0, y = 0; bool down = false, clicked = false, dbl = false, shift = false, ctrl = false, alt = false; };
    MouseState mouse() const;
    MouseState m_fake;
    bool m_useFake = false;
    bool m_testMouse = false;          // --curve-test capture: mouse held on m_testMouseAt
    float3 m_testMouseAt{0, 0, 0};
    // scripted frames: the mouse of each frame (a world point + a pixel offset) and checks run before it
    struct Step { float3 at{0, 0, 0}; float dx = 0, dy = 0; bool down = false, clicked = false; std::function<void()> before; };
    std::vector<Step> m_script;
    size_t m_scriptPos = 0;
    std::string m_reportText;
    int m_fails = 0;
    void testCheck(bool ok, const std::string& what);
    void writeReport();
    void capture(const std::wstring& png);   // the window image of this frame, then the editor closes
    // Rhino-style precision: object snaps (osnap), ortho (Shift) and typed coordinates while drawing
    enum Snap { SNAP_END = 0, SNAP_POINT, SNAP_MID, SNAP_CENTER, SNAP_NEAR, SNAP_COUNT };
    bool m_osnap = true;
    int m_snapMask = (1 << SNAP_COUNT) - 1;
    struct SnapHit { bool on = false; float3 p{0, 0, 0}; int kind = 0; } m_snap;
    char m_coordBuf[96] = {};
    bool m_coordFocus = false, m_coordCaretEnd = false;

    // helpers
    float grid() const;
    float3 snap(const float3& p) const;
    bool groundPoint(float mx, float my, float planeY, float3& out) const;
    float3 toWorld(const Object& o, const float3& p) const;
    float3 toLocalDir(const Object& o, const float3& d) const;
    float3 itemLocal(const CurveData& c, Item it) const;
    float3& itemRef(CurveData& c, Item it);
    bool isSelected(Item it) const;
    void validate();
    bool pickPoint(float mx, float my, Item& out) const;
    int pickCurve(float mx, float my, float* distOut = nullptr) const;
    bool selectionPivot(float3& p) const;
    int manipulatorHit(float mx, float my) const;
    void beginDrag(int axis, float mx, float my);
    void updateDrag(float mx, float my);
    void applyDelta(const float3& worldDelta);
    void deleteSelectedPoints();
    // Bezier tangent of a point: 0 smooth (aligned), 1 broken (independent), 2 corner (no tangent)
    int tangentMode(const CurveData& c, int i) const;
    void setTangentMode(int i, int mode);
    void toggleCorner(int i);
    void insertPointAt(float mx, float my);
    void finishCreate();
    void cancelCreate();
    int addCurveObject(const CurveData& local, const float3& pos, const std::string& name);
    void drawCurve(ImDrawList* dl, const Object& o, ImU32 col, float width) const;
    bool handlesShown(int i) const;   // Bezier: tangents of the selected anchors only
    void drawPoints(ImDrawList* dl, const Object& o) const;
    void drawManipulator(ImDrawList* dl) const;
    void drawDraft(ImDrawList* dl);
    void runTest();
    bool findSnap(float mx, float my, SnapHit& out) const;
    float3 createTarget(float mx, float my, bool& ok);   // creation: ground point, osnap, grid (X), ortho (Shift)
    void draftAdd(const float3& p);
    bool parseCoord(const char* text, float3& out) const;
    void drawSnap(ImDrawList* dl) const;
    void drawCoordField();
    CurveData drawShape(float3& pos, float4& rot) const;
    void finishDraw();
    float worldPerPixel(const float3& p) const;
    std::vector<int> selectedAnchors() const;
    void applyFillet(float radius);
};

} // namespace app
