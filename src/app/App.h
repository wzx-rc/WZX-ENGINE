// Viewport3D demo application: hosts the vp3d renderer and an editor-style UI.
#pragma once
#include "Content.h"
#include "vp3d/Renderer.h"
#include "vp3d/UI.h"
#include <map>
#include <windows.h>

namespace app {

// TOOL_MOVE / TOOL_ROTATE = arrows / rings on the selection.
enum Tool { TOOL_SELECT, TOOL_MOVE, TOOL_ROTATE };
enum SceneId { SCENE_CORNELL = 0, SCENE_STUDIO = 1 };

// Move / rotate manipulator drag on the selected object.
struct GizmoDrag {
    bool active = false;
    int axis = 0;                // 0 X, 1 Y, 2 Z
    bool rotate = false;
    int obj = -1;
    float3 startPos, pivot;
    float4 startRot = qIdentity();
    float t0 = 0;                // move: parameter along the axis at the press
    float3 v0;                   // rotate: vector pivot -> press point in the ring plane
    int steps = 0;
    bool moved = false;
    struct Other { int obj; float3 pos; float4 rot; };
    std::vector<Other> others;   // rest of a multi-selection moved / turned with the primary object
};

struct ViewBookmark {
    std::wstring name;
    Camera cam;
    ComPtr<ID2D1Bitmap1> thumb;
};

struct Snapshot {
    std::vector<Object> objects;
    std::vector<Material> materials;
    int selected = -1;
};

struct Options {
    int scene = SCENE_CORNELL;
    std::wstring shotPath;      // auto screenshot then exit
    bool shotWindow = true;     // capture the whole window (UI) instead of the viewport only
    int shotSpp = 256;
    int view = -1;
    int select = -1;
    int tool = -1;              // test: start with this tool (1 move, 2 rotate)
    int width = 1600, height = 900;
    int quality = 1;
    bool raster = true;         // light raster viewport by default, P toggles path tracing
    std::wstring openPath;      // scene file to open at startup
};

class App {
public:
    bool init(HINSTANCE inst, const Options& opt);
    int run();
    LRESULT wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

private:
    // core
    void loadScene(int id);
    void frame();
    void layout();
    void markDirty();
    void cameraChanged();
    void applyQuality();
    void handleShortcuts();
    void viewportInput();
    bool overViewportWidgets(float x, float y) const;
    void cameraRay(float px, float py, float3& o, float3& d) const;
    bool project(const float3& p, float2& s) const;
    void setPivotKeepEye(const float3& p);
    void frameSelection();
    void setViewAxis(int axis);

    // editing
    void pushUndo();
    void undo();
    void redo();
    void select(int idx);
    void addPartFromLibrary(const PartDef& p);
    void duplicateSelected();
    void deleteSelected();
    void rotateSelected(float deg);
    void nudgeSelected(int dx, int dy, int dz);
    void setColor(int paletteIndex);
    void setPreset(int preset);

    // scene files (SceneIO.cpp)
    bool saveSceneFile(const std::wstring& path);
    bool loadSceneFile(const std::wstring& path);
    void runPendingAction();
    int m_pendingAction = 0;   // 1 save dialog, 2 open dialog

    // output
    void generateThumbnails();
    ComPtr<ID2D1Bitmap1> makeThumb(const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h);
    void saveScreenshot(bool window);

    // UI (AppUI.cpp)
    void drawUI();
    void drawTopBar();
    void drawToolbar();
    void drawStatusBar();
    void drawLeftPanel();      // hierarchy (scene objects) / world (lights, environment)
    void drawRightPanel();     // inspector / render settings
    void drawObjectTab(Rect area);
    void drawRenderTab(Rect area);
    void drawBottomStrip();    // content browser / camera views
    void drawContentTab(Rect area);
    void drawViewsTab(Rect area);
    void drawViewportOverlay();
    void drawPartIcon(const PartDef& p, const Rect& r, const Color& c);
    void drawBox3D(float cx, float cy, float s, float W, float D, float H, const Color& c);
    // Collapsible inspector section: draws the header bar at y, advances y, returns true when open.
    bool section(float x, float& y, float w, const std::wstring& title, int idx);
    // 0-2 inspector, 3-7 render settings, 8-9 hierarchy folders
    bool m_secOpen[10] = {true, true, true, true, true, true, true, true, true, true};
    int m_bottomTab = 0;       // 0 content, 1 camera views
    Rect m_rcStatus;

    HWND m_hwnd = nullptr;
    Options m_opt;
    Renderer m_renderer;
    UI m_ui;
    Scene m_scene;
    int m_sceneId = SCENE_CORNELL;
    int m_pendingScene = -1;
    // orientation helpers
    float4 objRot(const Object& o) const { return o.useRot ? o.rot : qAxisAngle(float3(0, 1, 0), o.yaw); }
    void ensureRot(Object& o) { if (!o.useRot) { o.rot = qSnap90(qAxisAngle(float3(0, 1, 0), o.yaw)); o.useRot = true; } }
    void aabbOf(const Object& o, const float3& pos, const float4& rot, float3& mn, float3& mx);
    // manipulators (Gizmo.cpp)
    bool gizmoInput(bool inView);
    void drawGizmo();
    void drawSelectionOverlay();
    bool gizmoHit(float mx, float my, int& axis, bool& rotate);
    float gizmoScale(const float3& pivot) const;
    // preferences + top bar icons
    void loadPrefs();
    void savePrefs();
    bool toolIcon(const Rect& r, int kind, bool active, const wchar_t* tip);
    // dockable panels: library (0), properties (1), camera views (2). Grip: click = fold, drag = undock / dock.
    struct PanelState { bool visible = true, collapsed = false, floating = false; int dock = 0; Rect fr; };
    PanelState m_pan[3];
    Rect m_panFrame[3], m_panGrip[3];
    bool m_panShown[3] = {true, true, true};
    int m_gripActive = -1;
    bool m_gripMoved = false;
    float m_gripSX = 0, m_gripSY = 0, m_gripOX = 0, m_gripOY = 0;
    void layoutPanels(float y0);
    std::string panelsString() const;
    bool slotFree(int col, int self) const;
    void panelInput();
    void drawPanels();
    void drawPanelDropZones();
    bool overPanels(float x, float y) const;
    void resetPanels();
    void togglePanel(int i);
    // multi-selection (rectangle in select mode)
    std::vector<int> m_multi;                  // extra selected objects (m_selected is the primary one)
    struct Marquee { bool active = false, moved = false; float x0 = 0, y0 = 0; } m_marq;
    std::vector<int> selectionAll() const { std::vector<int> v; if (m_selected >= 0) v.push_back(m_selected); for (int i : m_multi) v.push_back(i); return v; }
    void marqueeInput(bool inView);
    GizmoDrag m_gz;
    int m_gzHover = -1;
    std::string m_lastPrefs;
    float m_dt = 1.0f / 60.0f;
    RenderSettings m_rs;
    int m_quality = 1;          // 0 interactive, 1 final
    int m_adaptiveSpp = 1;
    bool m_interacting = false;
    float m_dpi = 1.0f;
    bool m_running = true;
    bool m_minimized = false;
    bool m_resizePending = false;
    std::wstring m_exeDir;

    // layout (DIPs)
    float m_W = 1600, m_H = 900;
    Rect m_rcTop, m_rcTool, m_rcLeft, m_rcRight, m_rcBottom, m_rcView;
    ViewRect m_vpPx;

    // interaction state
    Tool m_tool = TOOL_SELECT;
    Tool m_lastTool = TOOL_MOVE;   // restored when the studio scene loads
    bool m_magnet = true;
    int m_snapMode = 0;
    int m_selected = -1;
    bool m_orbiting = false, m_panning = false, m_dragging = false;
    float m_lastX = 0, m_lastY = 0;
    float m_dragPlaneY = 0;
    float3 m_dragOffset;
    bool m_dragMoved = false;
    bool m_turntable = false;
    bool m_autoFocus = true;
    std::vector<Snapshot> m_undo, m_redo;

    // panels
    int m_leftTab = 0;
    std::wstring m_search;
    float m_leftScroll = 0, m_rightScroll = 0, m_listScroll = 0;
    int m_rightTab = 0;
    int m_viewMode = 0;
    std::vector<ViewBookmark> m_views;
    int m_activeView = 0;
    bool m_thumbFresh = false;
    int m_viewScroll = 0;
    int m_favoriteColor = -1;
    std::wstring m_status;
    uint32_t m_statusTime = 0;

    // fps
    uint64_t m_lastTick = 0;
    float m_frameMs = 16.0f;
    int m_frameCounter = 0;
};

} // namespace app
