// Curve tools of the Scene editor (Create > Curve): creation by clicks on the ground (CV, EP, Bezier, Linear),
// curve primitives, display over the viewport, selection, point editing (F8) with its own move manipulator,
// inspector section and .wzx read / write. The curves are scene objects (part "CURVE", mesh -1): undo,
// duplicate, delete, groups and the Scene Manager work as for the meshes.
#pragma once
#include "App.h"

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
    void enterPoints();
    void exitMode();
    bool editingPoints() const { return m_mode == MODE_POINTS; }
    bool creating() const { return m_mode == MODE_CREATE; }

    std::wstring testReport;                 // --curve-test: report file

private:
    enum Mode { MODE_NONE, MODE_CREATE, MODE_POINTS };
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
    bool m_marquee = false;
    float2 m_marq0{0, 0};
    int m_testFrame = 0;

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
    void insertPointAt(float mx, float my);
    void finishCreate();
    void cancelCreate();
    int addCurveObject(const CurveData& local, const float3& pos, const std::string& name);
    void drawCurve(ImDrawList* dl, const Object& o, ImU32 col, float width) const;
    void drawPoints(ImDrawList* dl, const Object& o) const;
    void drawManipulator(ImDrawList* dl) const;
    void drawDraft(ImDrawList* dl);
    void runTest();
};

} // namespace app
