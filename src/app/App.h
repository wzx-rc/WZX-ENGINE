// WZX Engine editor: hosts the vp3d renderer in the Atelier workspaces (Scene, Graph Editor, Material Studio)
// drawn with Dear ImGui.
#pragma once
#include "Content.h"
#include "ImGuiLayer.h"
#include "Json.h"
#include "Theme.h"
#include "atelier/material_host.h"
#include "atelier/material_model.h"
#include <map>
#include "vp3d/Renderer.h"
#include <string>
#include <vector>
#include <windows.h>

namespace app {

enum Tool { TOOL_SELECT, TOOL_MOVE, TOOL_ROTATE, TOOL_SCALE };
enum SceneId { SCENE_CORNELL = 0, SCENE_STUDIO = 1, SCENE_LOOKDEV = 2, SCENE_CONCEPT = 3 };

// Maya-style manipulator handles (Manipulators.cpp): axes, plane squares (2 axes, named by their normal), centre
// anchor, rotate view ring and trackball.
enum ManipHandle { MH_NONE = -1, MH_X = 0, MH_Y, MH_Z, MH_YZ, MH_XZ, MH_XY, MH_CENTER, MH_VIEW, MH_FREE };

// Manipulator drag on the selection.
struct GizmoDrag {
    bool active = false;
    int handle = MH_NONE;
    int tool = 0;
    int button = 0;              // 0 left, 2 middle
    int obj = -1;                // object carrying the manipulator
    float3 pivot;                // manipulator position at the press
    float3 ax[3];                // manipulator axes at the press
    float size = 1;              // manipulator size (world) at the press
    float3 n, hit0, v0;          // plane normal, press point on the plane, rotate: start direction in the ring plane
    float t0 = 0;                // press parameter along the axis
    float2 mouse0, prevMouse, tan2;
    bool tangent = false;        // rotate: ring seen edge-on, dragged along its screen tangent
    float angle = 0, lastAng = 0;
    float4 free = qIdentity();   // trackball rotation
    float3 value;                // shown next to the mouse: displacement, degrees (x) or scale factors
    bool moved = false;
    struct Item { int obj; float3 pos; float4 rot; float3 scale; float3 ax[3]; };
    std::vector<Item> items;     // objects driven (each on its own pivot and axes)
};

struct ViewBookmark {
    std::wstring name;
    Camera cam;
    ImTextureID thumb = 0;
};

struct Snapshot {
    std::vector<Object> objects;
    std::vector<Material> materials;
    int selected = -1;
    // Scene Manager state (lights can be parented, renamed, locked; collections)
    std::vector<AreaLight> lights;
    SunLight sun;
    std::vector<Collection> collections;
};

// Scene Manager node: an object or group, an area light, the sun or a collection.
enum NodeKind { NODE_NONE = 0, NODE_OBJECT, NODE_LIGHT, NODE_SUN, NODE_COLLECTION };
struct NodeRef {
    int kind = NODE_NONE;
    int index = -1;
    bool valid() const { return kind != NODE_NONE; }
};
// world transform of a node at the last frame (Scene Manager: the children follow their parent)
struct HierXf { float3 p, t; float4 r; float3 sc; float yaw = 0; bool useRot = false; };
// visible row of the Scene Manager list
struct OutlinerRow {
    NodeRef node;
    uint32_t id = 0;
    int depth = 0;
    bool hasChildren = false;
    bool open = false;
};

struct LogLine {
    std::string time;
    std::string text;
    int level = 0;               // 0 info, 1 success, 2 warning
};

struct Options {
    int scene = SCENE_CORNELL;
    std::wstring shotPath;      // auto screenshot then exit
    bool shotViewport = false;  // the auto screenshot is the 3D viewport only (--shot-viewport)
    int shotSpp = 256;
    int view = -1;
    int select = -1;
    int tool = -1;              // test: start with this tool (1 move, 2 rotate)
    int width = 1600, height = 900;
    int quality = 1;
    bool raster = true;         // light raster viewport by default, P toggles path tracing
    std::wstring openPath;      // scene file to open at startup
    std::wstring gizmoTest;     // manipulator self-test: report written there, then exit (--gizmo-test)
    std::wstring gizmoDemo;     // "tool handle dx dy menuDir": frozen drag or marking menu for a capture (--gizmo-demo)
    std::string openMenu;       // test: view menu opened at startup for a capture (vmCamera, vmDisplay, vmRender, vmAxes)
    int shotSeq = 0;            // test: --shot-seq N saves N images (name_00.png...) one every 8 frames
    std::wstring hdri;          // HDRI to load at startup (file, or "sky" for the procedural sky)
    // DDGI probe GI (real-time mode)
    bool ddgi = false;          // start in the real-time probe GI mode
    bool ddgiRaster = false;    // probe rays from raster cube captures (no ray tracing for the probes)
    bool ddgiRasterView = false;   // diagnostic: raster viewport reading ray traced probes (--ddgi-raster-view 1)
    bool showProbes = false;       // draw the probes as spheres shaded with their irradiance (--show-probes 1)
    bool baked = false;            // Lit mode: baked probes (--baked 1)
    // Stability tests: camera yaw oscillation (radians, 2 s period), first object bobbing, and the flicker statistics
    // (per-pixel standard deviation of the luminance over statFrames frames from statFrom, written to statPath).
    float sway = 0.0f;
    bool anim = false;
    std::wstring statPath;
    int statFrom = 60, statFrames = 120;
    int ddgiCapture = 32;       // probes captured per frame (raster)
    int probes = 2048;          // target probe count over the scene bounds
    int ddgiView = 0;           // 0 full, 1 indirect only, 2 direct only
    int temporalCap = 1024;      // history length in frames (converges; the history is dropped on lighting changes)
    float hysteresis = 0.97f;
    int light = -1;             // test: 1 light mode, 0 dark mode
    bool openSettings = false;  // test: open the settings window at startup
    std::string theme;          // test: start with this theme ("Atelier" or "Ambre"), not saved
    int workspace = 0;          // Atelier workspace at startup: 0 scene, 1 graph, 2 material
    int inspectorTab = -1;      // test: inspector tab at startup (0 actor, 1 light, 2 render, 3 world)
    ImVec4 mainColor = ImVec4(-1, -1, -1, 1);   // test: main colour for this run (--main-color r,g,b)
    ImVec4 sizes = ImVec4(-1, -1, -1, 0);       // test: text, icon and layout size for this run (--sizes t,i,l)
};

// Procedural thumbnails of the content browser (Editor.cpp).
void drawPrimitiveIcon(ImDrawList* dl, PartKind kind, ImVec2 a, ImVec2 b, ImU32 col);
void drawAssetThumb(ImDrawList* dl, int kind, int index, ImVec2 p0, ImVec2 p1, float dpi);   // 0 primitive, 1 material, 2 scene

// Writes a file through a temporary file then replaces it (SceneWzx.cpp).
bool writeFileSafe(const std::wstring& path, const std::string& text);

// Screen-space rectangle (pixels).
struct ScreenRect {
    float x = 0, y = 0, w = 1, h = 1;
    float r() const { return x + w; }
    float b() const { return y + h; }
    bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

class GameHost;

class App {
    friend class GameHost;   // game layer (GameHost.h, AppGame.cpp)
public:
    bool init(HINSTANCE inst, const Options& opt);
    int run();
    LRESULT wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

private:
    // core (App.cpp)
    void loadScene(int id, bool newScene = false);   // newScene: File > New Scene (the Village stage with an empty graph)
    // Baked probes (Lit mode): file next to the game data, automatic bake when the scene changes (see updateBake).
    std::wstring probeFile() const;
    void loadBakedProbesFile();
    bool saveBakedProbesFile(const std::wstring& path);
    void updateBake();
    void frame();
    void markDirty();
    void cameraChanged();
    void applyQuality();
    // Render modes of the viewport: 0 real time (raster), 1 progressive path tracing (denoiser optional),
    // 2 real-time GI probes (DDGI)
    void setRenderMode(int mode);
    int renderMode() const;
    void handleShortcuts();
    void cameraRay(float px, float py, float3& o, float3& d) const;
    bool project(const float3& p, float2& s) const;
    void setPivotKeepEye(const float3& p);
    void frameSelection();
    void setViewAxis(int axis);
    void log(const std::string& text, int level = 0);
    void status(const std::wstring& text, int level = 0);

    // editing (App.cpp)
    void pushUndo();
    void undo();
    void redo();
    void select(int idx);
    void addPartFromLibrary(const PartDef& p, const float3* at = nullptr);
    void addPrimitive(int type);   // Create > Mesh
    void syncPrimitives();         // rebuilds primitive meshes whose parameters changed (undo / redo)
    void duplicateSelected();
    void deleteSelected();
    void rotateSelected(float deg);
    void nudgeSelected(int dx, int dy, int dz);
    void setColor(int paletteIndex);
    void setPreset(int preset);
    float4 objRot(const Object& o) const { return o.useRot ? o.rot : qAxisAngle(float3(0, 1, 0), o.yaw); }
    void ensureRot(Object& o) { if (!o.useRot) { o.rot = qSnap90(qAxisAngle(float3(0, 1, 0), o.yaw)); o.useRot = true; } }
    void aabbOf(const Object& o, const float3& pos, const float4& rot, float3& mn, float3& mx);
    std::vector<int> selectionAll() const { std::vector<int> v; if (m_selected >= 0) v.push_back(m_selected); for (int i : m_multi) v.push_back(i); return v; }

    // Scene Manager (Outliner.cpp): ids, parent / child links, order, groups, collections, selection of the list.
    // Transforms stay world values; outlinerSync moves the children of a moved parent (before the render).
    void outlinerIds();
    void outlinerSync();
    uint32_t newNodeId();
    NodeRef findNode(uint32_t id) const;
    uint32_t nodeId(NodeRef n) const;
    uint32_t nodeParent(NodeRef n) const;
    uint32_t nodeCollection(NodeRef n) const;
    std::string nodeName(NodeRef n) const;
    void setNodeName(NodeRef n, const std::string& name);
    bool nodeVisible(NodeRef n) const;       // containers: one member visible
    bool nodeLocked(NodeRef n) const;        // containers: every member locked
    void setNodeVisible(NodeRef n, bool on); // containers: applied to the members (and the descendants)
    void setNodeLocked(NodeRef n, bool on);
    std::vector<NodeRef> nodeChildren(uint32_t parent, uint32_t collection, bool withCollections) const;   // sorted siblings
    void nodeDescendants(uint32_t id, std::vector<NodeRef>& out) const;   // children of an object / group, members of a collection
    bool isAncestor(uint32_t ancestor, uint32_t id) const;
    void outlinerRows(std::vector<OutlinerRow>& rows, const char* filter);
    // drop of the dragged nodes: 0 before target, 1 after target, 2 into target (parent / collection), 3 root end
    void moveNodes(std::vector<uint32_t> ids, int mode, uint32_t target);
    void groupSelection();
    void ungroup(uint32_t groupId);
    uint32_t addCollection(const std::string& name, int color);
    uint32_t villageCollection();             // the RTS houses go in the "Village" collection
    std::string uniqueNodeName(const std::string& base) const;
    bool nodeSelected(NodeRef n) const;
    void selectNodes(const std::vector<NodeRef>& nodes, NodeRef primary);   // replaces the selection
    void toggleNodeSelected(NodeRef n);
    std::vector<uint32_t> selectedNodeIds() const;
    void startRename(uint32_t id);
    void selectionOutline(std::vector<int>& outlined) const;   // groups and parents: their descendants are outlined too
    std::vector<uint32_t> m_outSel;          // selected lights, sun and collections (objects: m_selected / m_multi)
    uint32_t m_outAnchor = 0;                // Shift+click range start
    uint32_t m_renameId = 0;                 // node renamed in the list (0 = none)
    bool m_renameFocus = false;
    char m_renameBuf[128] = {};
    std::vector<uint32_t> m_renameMulti;     // several nodes renamed at once: text + _001, _002... (list order)
    std::vector<uint32_t> m_outDrag;         // nodes dragged in the list

    std::map<uint32_t, HierXf> m_hierCache;  // world transforms of the last frame (children follow their parent)

    // scene files: .wzx (SceneWzx.cpp), the old .v3d.json (SceneIO.cpp: written for the Build and the player, read as an import)
    bool saveSceneFile(const std::wstring& path);   // by extension (.wzx, else .v3d.json); the document is unchanged
    bool loadSceneFile(const std::wstring& path);   // .wzx, or an old .v3d.json (import); becomes the document
    bool saveLegacySceneFile(const std::wstring& path);
    bool loadLegacySceneFile(const Json& j, const std::wstring& path);
    std::string wzxSceneJson(bool withCamera) const;
    void loadWzxScene(const Json& j, const std::wstring& path);
    void importMeshFile(const std::wstring& path);   // OBJ as a new object, its path kept in the scene
    std::wstring projectRoot() const;
    std::string relPath(const std::wstring& path) const;   // path written in a scene file (relative to the project)
    std::wstring absPath(const std::string& path) const;
    void runPendingAction();
    enum PendingAction { PA_SAVE = 1, PA_OPEN, PA_HDRI, PA_SAVE_AS, PA_NEW, PA_IMPORT_MESH, PA_DOC_NEXT };
    int m_pendingAction = 0;   // PendingAction, run before the next ImGui frame (file dialogs)
    std::map<int, int> m_placeholderMat;   // red material of a missing mesh placeholder -> material saved in the file
    Json m_pendingVillage;                 // village of a scene opened before the game layer exists
    Json m_pendingGraph;                   // graph of a scene opened before the game layer exists

    // document (SceneWzx.cpp): current file, modified state (* in the title), "Save changes?" before New / Open / closing
    enum DocAction { DOC_OPEN = 1, DOC_NEW, DOC_CLOSE, DOC_SCENE = 100 /* + scene id */ };
    bool saveDocument(bool saveAs);
    void requestDocAction(int action);
    void runDocAction(int action);
    void markSaved();
    void updateDocState(bool force = false);
    void updateTitle();
    void drawSaveChangesPopup();
    std::wstring m_docPath;                // empty = not saved yet
    std::wstring m_docName = L"Untitled";
    std::string m_savedSig;                // the scene as saved (without the camera), compared to know if it is modified
    bool m_docDirty = false;
    uint64_t m_docCheckTick = 0;
    std::wstring m_title;
    int m_confirmAction = 0;               // DocAction waiting for the "Save changes?" answer
    int m_docNext = 0;
    bool m_docSaveFirst = false;
    bool m_forceClose = false;

    // HDRI environment (App.cpp): files of 01 - INBOX and assets/hdri, loaded once and kept in a cache
    bool setHdri(const std::wstring& path);   // empty = gradient sky, L"sky" = procedural sky
    std::vector<std::wstring> hdriFiles() const;
    std::wstring defaultHdri() const;          // first HDRI of 01 - INBOX, else of assets/hdri, else the procedural sky
    std::map<std::wstring, EnvImagePtr> m_hdriCache;
    std::wstring m_hdriPath;                  // HDRI of the scene (empty = none)
    std::wstring m_lastHdri;                  // last HDRI chosen, reused when the LookDev scene is loaded again

    // output (App.cpp)
    void generateThumbnails();
    ImTextureID makeThumb(const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h);
    void clearViews();
    void saveScreenshot(bool window);

    // editor UI (Editor.cpp)
    void drawEditor();
    void drawShortcutsWindow();
    void drawAboutWindow();
    void drawSettingsWindow();
    bool m_showSettings = false;
    float m_swayBase = 0.0f, m_animBase = 0.0f;   // camera yaw and object height before the stability test moves them
    std::vector<double> m_statMean, m_statM2;     // flicker statistics (Welford), one per pixel
    int m_statCount = 0;
    bool m_themeTestOnly = false;   // theme forced from the command line: do not save the settings

    // Atelier workspaces: 0 Scene Editor (AtelierUI.cpp), 1 Graph Editor, 2 Material Studio (src/app/atelier)
    int m_workspace = 0;
    // Material Studio preview rendered by the engine (MaterialPreview.cpp)
    struct MaterialPreview : atelier::material::PreviewHost {
        App* app = nullptr;
        bool want = false;
        atelier::material::Material material;
        atelier::material::PreviewSettings settings;
        int width = 640, height = 360;
        void requestPreview(const atelier::material::Material& m, const atelier::material::PreviewSettings& s, int w, int h) override;
        void* previewTexture() override;
        bool exportPreview(const std::wstring& path) override;
        atelier::material::PreviewTexture loadTexture(const std::string& utf8Path) override;
        std::string pickTextureFile() override;
    } m_matHost;
    Scene m_previewScene;
    bool m_previewActive = false;      // the renderer holds the preview scene
    int m_previewShape = -1;
    std::string m_previewKey;          // last rendered parameters (restart the accumulation when they change)
    std::map<std::string, atelier::material::PreviewTexture> m_textureCache;
    void renderMaterialPreview();
    void buildPreviewScene(int shape);
    void leavePreview();

    // Atelier layout (AtelierUI.cpp): fixed concept layout with the Atelier components
    void drawAtelier();
    void atlTopBar(float x0, float y0, float width);
    void atlHierarchy(float x, float y, float w, float h);
    void atlViewport(float x, float y, float w, float h);
    void atlViewMenus(float x, float y);   // camera, display, renderer and axes menus inside the view
    void atlContent(float x, float y, float w, float h);
    void atlInspector(float x, float y, float w, float h);
    void atlInspectorActor(float x, float y, float w, float h);
    void atlInspectorLighting(float x, float y, float w, float h);
    void atlInspectorRender(float x, float y, float w, float h);
    void atlInspectorWorld(float x, float y, float w, float h);
    struct AtelierState {
        float leftW = 222, rightW = 284, bottomH = 190;   // reference pixels of the Graph Editor (scaled by s)
        int sceneTab = 0, inspectorTab = 0, contentTab = 0, folder = 0;
        bool expanded[10] = {true, true, true, true, false, false, false, true, true, true};
        bool folderOpen[4] = {true, false, true, true};    // hierarchy: root, environment, objects, lights
        float thumb = 64;
        bool listMode = false;
        char sceneSearch[128] = {}, assetSearch[128] = {}, propSearch[128] = {};
    } m_atl;

    // viewport (Viewport.cpp)
    void viewportCanvas(ImVec2 p0, ImVec2 size);
    void viewportInput(bool hovered);
    // manipulators (Manipulators.cpp)
    bool gizmoShown() const;
    bool gizmoInput(bool inView);
    void gizmoBegin(int handle, int button, float mx, float my);
    void gizmoDrag(float mx, float my, bool snap);
    void drawGizmo(ImDrawList* dl);
    int gizmoHit(float mx, float my);
    float gizmoSize(const float3& p) const;
    void gizmoFrame(int obj, int tool, float3 ax[3]) const;
    std::vector<int> gizmoTargets() const;
    bool markingMenuInput(bool inView);
    void drawMarkingMenu(ImDrawList* dl);
    void markingSelect(int op);   // 0 all, 1 none, 2 invert, 3 hierarchy
    void markingReset(int what);  // 0 position, 1 rotation, 2 scale
    // selection overlay (SelectionFx.cpp): white glow on the outline vertices, outline in the floor reflection
    struct SilEdge { float3 a, b, n0, n1; bool open = true; };   // mesh edge (object space) and its two face normals
    struct SilCache { bool built = false; uint32_t version = 0; size_t vertexCount = 0, indexCount = 0; float3 first; std::vector<SilEdge> edges; };
    std::map<int, SilCache> m_silCache;   // per mesh
    const std::vector<SilEdge>& silhouetteEdges(int mesh);
    void drawSelectionFx(ImDrawList* dl);
    void toggleGroundGrid();
    void marqueeInput();

    HWND m_hwnd = nullptr;
    Options m_opt;
    vp3d::Renderer m_renderer;
    ImGuiLayer m_gui;
    GameHost* m_game = nullptr;   // node graph, Play, builds, command API
    Scene m_scene;
    int m_sceneId = SCENE_CORNELL;
    std::string m_probeKey = "cornell";      // name of the baked probes file of the current scene
    uint32_t m_bakedSig = 0, m_bakeWantSig = 0;   // scene signature of the probes in use / of the scene now
    uint64_t m_bakeChangeTick = 0;
    bool m_bakeSaveWhenDone = false, m_bakeNow = false;
    int m_pendingScene = -1;
    bool m_viewportVisible = false;
    bool m_viewportRendered = false;
    float m_dpi = 1.0f;
    bool m_running = true;
    bool m_minimized = false;
    std::wstring m_exeDir;

    RenderSettings m_rs;
    int m_quality = 1;          // 0 interactive, 1 final
    int m_adaptiveSpp = 1;
    bool m_interacting = false;
    ViewRect m_vpPx;            // viewport image size in pixels
    ScreenRect m_rcView;        // viewport image rectangle on screen

    // interaction state
    Tool m_tool = TOOL_SELECT;
    bool m_magnet = false;       // Snapping always on (else hold Shift while dragging a manipulator)
    int m_selected = -1;
    std::vector<int> m_multi;
    bool m_orbiting = false, m_panning = false;
    float m_lastX = 0, m_lastY = 0;
    bool m_turntable = false;
    bool m_autoFocus = true;
    bool m_showGrid = true;
    bool m_showStats = true;
    bool m_showGizmo = true;
    bool m_showEditorGrid = true; // ground grid of the editor (EditorGrid.hlsl)
    size_t m_selSig = 0;          // selection signature: the glow animation restarts when it changes
    double m_selSince = 0;
    int m_axisMode = 0;          // move / rotate axes: 0 world, 1 object, 2 parent (scale: always object)
    float m_stepMove = 1.0f, m_stepRotate = 15.0f, m_stepScale = 0.1f;   // increments (Shift, or Snapping on)
    bool m_snapAbsolute = false; // move increments land on the grid of the step instead of relative to the start
    float m_camSpeed = 1.0f;     // camera navigation speed factor
    struct Marquee { bool active = false, moved = false; float x0 = 0, y0 = 0; } m_marq;
    GizmoDrag m_gz;
    int m_gzHover = MH_NONE;
    int m_gzLast[4] = {MH_NONE, MH_NONE, MH_NONE, MH_NONE};   // last used handle per tool (middle button, drawn yellow)
    struct MarkingMenu { bool open = false; int tool = 0, hover = -1; float x = 0, y = 0, mx = 0, my = 0; } m_mm;
    bool m_gzDemo = false;       // --gizmo-demo: a drag / marking menu frozen on screen for a capture
    int m_shotSeqDone = 0;       // --shot-seq: images saved
    void gizmoScript();          // --gizmo-demo / --gizmo-test (command line), called once per frame
    float2 gizmoHandlePoint(int handle);   // screen point on a handle of the current manipulator (tests)
    std::vector<Snapshot> m_undo, m_redo;
    int m_viewMode = 0;

    // panels
    int m_contentAsset = 0;     // selected asset in the content browser
    std::vector<ViewBookmark> m_views;
    int m_activeView = 0;
    bool m_thumbFresh = false;
    std::vector<LogLine> m_log;
    std::wstring m_status;
    int m_statusLevel = 0;
    uint64_t m_statusTime = 0;
    bool m_showShortcuts = false;
    bool m_showAbout = false;

    // fps
    uint64_t m_lastTick = 0;
    float m_frameMs = 16.0f;
    float m_atelierTop = 42.0f;   // height of the editor top bar (AtelierUI.cpp); the Graph tab starts below it
    float m_dt = 1.0f / 60.0f;
    int m_frameCounter = 0;
};

} // namespace app
