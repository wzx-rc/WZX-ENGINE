// WZX Engine editor: hosts the vp3d renderer in the Atelier workspaces (Scene, Graph Editor, Material Studio)
// drawn with Dear ImGui.
#pragma once
#include "Content.h"
#include "ImGuiLayer.h"
#include "Json.h"
#include "MeshFile.h"
#include "Prefab.h"
#include "PrefabInstance.h"
#include "Rig.h"
#include "UvCheck.h"
#include "Theme.h"
#include "atelier/material_host.h"
#include "atelier/material_model.h"
#include <functional>
#include <map>
#include <memory>
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
// Built-in cameras of the viewports (Cameras.cpp): persp, then the orthographic ones; VCAM_OBJECT: a camera object.
enum ViewCam { VCAM_PERSP = 0, VCAM_TOP, VCAM_FRONT, VCAM_SIDE, VCAM_BOTTOM, VCAM_BACK, VCAM_LEFT, VCAM_COUNT, VCAM_OBJECT = VCAM_COUNT };
// Split layouts of the viewport (Panes.cpp): single, two side by side, two stacked, 2 x 2, 3 x 3, 4 x 4.
constexpr int kLayoutCount = 6;
constexpr int kPreviewSlot = 16;   // extra view of the camera preview (the panes use 0..15)

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
    struct Item { int obj; float3 pos; float4 rot; float3 scale; float3 ax[3]; float3 pivot; float3 base; };   // base: contact point (object space)
    std::vector<Item> items;     // objects driven (each on its own pivot and axes)
    bool pivotEdit = false;      // Edit Pivot: the move moves the pivots, the geometry stays
    bool placeAnchor = false;    // Place on: the scale holds the contact point (the bottom of the object) still, its pivot unchanged
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
    int denoiser = -1;          // --denoiser: 0 off, 1 a-trous, 2 Open Image Denoise (-1: the default of the mode)
    int dlss = -1;              // --dlss: 0 a-trous while the view moves, 1 DLSS Ray Reconstruction, 2 DLAA input (-1: default)
    int adaptive = -1;          // --adaptive 0/1: adaptive sampling of the progressive mode (-1: default)
    float adaptThreshold = -1;  // --adapt-threshold: its noise threshold
    bool sppMap = false;        // --spp-map 1: heat map of the samples per pixel
    float shotGpuMs = 0;        // --shot-gpu-ms: capture once the accumulation used this much GPU time (equal-time tests)
    int view = -1;
    int select = -1;
    int tool = -1;              // test: start with this tool (1 move, 2 rotate)
    int width = 1600, height = 900;
    int quality = 1;
    bool raster = true;         // light raster viewport by default, P toggles path tracing
    std::wstring openPath;      // scene file to open at startup
    std::wstring gizmoTest;     // manipulator self-test: report written there, then exit (--gizmo-test)
    std::wstring selfTest;      // editor self-test: report written there, then exit (--self-test, tools\selftest.bat)
    std::string cam;            // camera of the view at startup: persp, top, front, side, bottom, back, left (--cam)
    int layout = -1;            // layout of the view at startup: 0 single ... 3 four (2 x 2), 5 sixteen (--layout)
    bool demoCameras = false;   // test: two camera objects around the view at startup, one of them the render camera (--demo-cameras)
    int snap = -1;              // test: absolute snapping on, always, on these grid points (--snap 0 intersections, 1 centres, 2 edges)
    bool pivotEdit = false;     // test: Edit Pivot on at startup (--pivot-edit)
    int selStyle = -1;          // selection style at startup: 0 yellow outline, 1 blue glow (--sel-style)
    std::wstring gizmoDemo;     // "tool handle dx dy menuDir": frozen drag or marking menu for a capture (--gizmo-demo)
    std::string openMenu;       // test: view menu opened at startup for a capture (vmCamera, vmDisplay, vmRender, vmAxes)
    int shotSeq = 0;            // test: --shot-seq N saves N images (name_00.png...) one every 8 frames
    int aa = -1;                // viewport anti-aliasing at startup: MSAA samples (--aa 1, 4, 8 or 16)
    std::wstring hdri;          // HDRI to load at startup (file, or "sky" for the procedural sky)
    // DDGI probe GI (real-time mode)
    bool ddgi = false;          // start in the real-time probe GI mode
    bool ddgiRaster = false;    // probe rays from raster cube captures (no ray tracing for the probes)
    bool ddgiRasterView = false;   // diagnostic: raster viewport reading ray traced probes (--ddgi-raster-view 1)
    bool split = false;            // start in the Split Radiance Cascades mode (--split 1)
    int splitPx = 0, splitCascades = 0;   // its probe size in pixels, cascade count (--split-px, --split-cascades)
    int splitHistory = -1, splitDebug = 0;   // accumulation frames, inspection view (--split-history, --split-debug)
    int splitFilter = -1;                    // probe filter 0 / 1 (--split-filter)
    int aaQuality = -1, shadowQuality = -1;  // GI raster modes: AA 0..2, shadows 0..4 (--aa, --shadow-quality)
    float camDist = 0, camPitch = -1000;      // test: camera distance factor, pitch in degrees (--cam-dist, --cam-pitch)
    float splitInterval = 0;              // its first interval in probe cells (--split-interval)
    // Ghosting test: object index and / or light 0 swinging by animMove (--anim-object N, --anim-light 1,
    // --anim-move x,y,z, --anim-period frames, --anim-at F = frozen at the pose of frame F)
    int animObject = -1, animPeriod = 120, animAt = -1, animStop = -1;   // --anim-stop F: moving until frame F, then still
    bool animLight = false;
    vp3d::float3 animMove = vp3d::float3(10, 0, 0);
    bool showProbes = false;       // draw the probes as spheres shaded with their irradiance (--show-probes 1)
    bool baked = false;            // Lit mode: baked probes (--baked 1)
    // Stability tests: camera yaw oscillation (radians, 2 s period), first object bobbing, and the flicker statistics
    // (per-pixel standard deviation of the luminance over statFrames frames from statFrom, written to statPath).
    float sway = 0.0f;
    int swayStop = 0;           // --sway-stop N: the sway stops at frame N (the view still after a move)
    bool anim = false;
    std::wstring statPath;
    int statFrom = 60, statFrames = 120;
    int ddgiCapture = 32;       // probes captured per frame (raster)
    int probes = 2048;          // target probe count over the scene bounds
    int ddgiView = 0;           // 0 full, 1 indirect only, 2 direct only
    int temporalCap = 1024;      // history length in frames (converges; the history is dropped on lighting changes)
    float hysteresis = -1.0f;   // < 0: 0.995 with the progressive probes, 0.97 without
    int rasterMoving = 1;       // GI mode: the raster view of the probes while something moves, then a fade to the traced
                                // image (--raster-moving 0: always traced)
    int progressive = 1;        // GI mode: progressive probes (running average while still, restart around what changed,
                                // a quarter of the probes per frame once still); --progressive 0 = the former blend
    int light = -1;             // test: 1 light mode, 0 dark mode
    bool openSettings = false;  // test: open the settings window at startup
    std::string theme;          // test: start with this theme ("Atelier" or "Ambre"), not saved
    int workspace = 0;          // Atelier workspace at startup: 0 scene, 1 graph, 2 material, 3 prefab
    // test: Prefab studio state at startup (--prefab-view, --prefab-select, --prefab-time, --prefab-tab, --prefab-clip)
    int prefabView = -1, prefabSelect = -1, prefabTab = -1, prefabClip = -1, prefabBottom = -1, prefabLight = -1;
    float prefabTime = -1;
    std::wstring prefabOpen;
    int prefabRig = -1, prefabJoint = -1;   // test: Rig module mode (0..3; loads the rig sample when the prefab has no joint), joint
    std::wstring prefabClipName;   // test: clip shown, by name (--prefab-clip-name)
    bool prefabDq = false;      // test: dual quaternion skinning on every skin (--prefab-dq)
    std::wstring prefabExportGltf;   // test: exports the first skinned part as glTF at startup (--prefab-export-gltf)
    std::wstring prefabRigFbx;  // test: imports this FBX in a new prefab and opens the Rig module
    std::wstring prefabUv;      // test: opens the UV Editor: "setA,setB,compare,display,background" (--prefab-uv)
    bool prefabPlace = false;   // test: places the prefab in the scene at startup and shows the Scene workspace (--prefab-place)
    std::wstring prefabSave;    // test: writes the prefab of the studio to this file at startup (--prefab-save)
    int inspectorTab = -1;      // test: inspector tab at startup (0 actor, 1 light, 2 render, 3 world)
    ImVec4 mainColor = ImVec4(-1, -1, -1, 1);   // test: main colour for this run (--main-color r,g,b)
    ImVec4 sizes = ImVec4(-1, -1, -1, 0);       // test: text, icon and layout size for this run (--sizes t,i,l)
    std::wstring curveTest;     // test: curve tools sequence, report written to this file (--curve-test)
};

// Procedural thumbnails of the content browser (Editor.cpp).
void drawPrimitiveIcon(ImDrawList* dl, PartKind kind, ImVec2 a, ImVec2 b, ImU32 col);
void drawAssetThumb(ImDrawList* dl, int kind, int index, ImVec2 p0, ImVec2 p1, float dpi);   // 0 primitive, 1 material, 2 scene

// Writes a file through a temporary file then replaces it (SceneWzx.cpp).
bool writeFileSafe(const std::wstring& path, const std::string& text);

// Open / save file dialog (PrefabStudio.cpp). Empty = cancelled.
std::wstring pickFileDialog(HWND hwnd, bool save, const wchar_t* filter, const wchar_t* defExt, const std::wstring& initialDir,
                           const std::wstring& initialName);

// Screen-space rectangle (pixels).
struct ScreenRect {
    float x = 0, y = 0, w = 1, h = 1;
    float r() const { return x + w; }
    float b() const { return y + h; }
    bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

class GameHost;
class CurveEditor;

class App {
    friend class GameHost;   // game layer (GameHost.h, AppGame.cpp)
    friend class CurveEditor;   // curve tools (CurveEditor.h)
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
    void aabbOf(const Object& o, const float3& pos, const float4& rot, float3& mn, float3& mx) const;
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

    // scene files: .wzx (SceneWzx.cpp, reader shared with the player in WzxRead.cpp), the old .v3d.json (SceneIO.cpp, import)
    bool saveSceneFile(const std::wstring& path);   // by extension (.wzx, else .v3d.json); the document is unchanged
    bool loadSceneFile(const std::wstring& path);   // .wzx, or an old .v3d.json (import); becomes the document
    bool saveLegacySceneFile(const std::wstring& path);
    bool loadLegacySceneFile(const Json& j, const std::wstring& path);
    // mapPath: optional rewrite of every file path written (the Build points them at its copies)
    std::string wzxSceneJson(bool withCamera, const std::function<std::string(const std::string&)>& mapPath = {}) const;
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
    std::wstring m_lastShot;   // file of the last saveScreenshot (empty: it failed)

    // editor UI (Editor.cpp)
    void drawEditor();
    void drawShortcutsWindow();
    void drawAboutWindow();
    void drawSettingsWindow();
    bool m_showSettings = false;
    float m_swayBase = 0.0f, m_animBase = 0.0f;   // camera yaw and object height before the stability test moves them
    float3 m_animObjBase, m_animLightBase, m_animLightTarget;   // ghosting test: poses before it moves them
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

    // Prefab studio (workspace 3): assembles parts (mesh + material + transform) into an asset with animation clips,
    // in its own scene and lighting. Logic in PrefabStudio.cpp, panels in PrefabUI.inl (included by AtelierUI.cpp).
    struct PrefabStudio {
        PrefabAsset asset;
        std::wstring path;                 // file of the asset (empty = not saved yet)
        std::string savedJson;             // the asset as saved: modified test
        bool initialised = false;
        Scene scene;                       // the studio world: floor, lights, the parts
        bool active = false;               // the renderer holds the studio scene
        bool rebuild = true;               // parts structure, meshes or textures changed: rebuild the scene
        uint32_t buildSerial = 0;          // counts the rebuilds (UV layout cache)
        std::vector<int> partObject;       // part index -> object of the studio scene (-1 = pivot)
        int floorObject = -1, footprintObject = -1;
        std::map<std::string, MeshData> meshCache;   // mesh files read once (path -> mesh, pivot recentred)
        std::map<std::string, std::string> meshErrors;
        std::map<std::string, float3> meshShift;     // offset removed from the vertices of a recentred mesh file
        std::map<std::string, Texture> texCache;
        std::vector<float3> meshMin, meshMax;        // bounds of the studio scene meshes (by mesh index)
        // move gizmo on the selected part (world axes)
        int gizmoAxis = -1, gizmoHover = -1;
        float gizmoT0 = 0;
        float3 gizmoStart, gizmoDir, gizmoOrigin;
        std::vector<PartPose> pose;
        std::vector<PartWorld> world;
        // selection and editing
        int sel = -1;                      // selected part index
        std::vector<PrefabAsset> undo, redo;
        bool editing = false;              // a field drag is in progress (one undo step per drag)
        uint32_t renameId = 0;
        char renameBuf[96] = {};
        bool renameFocus = false;
        uint32_t dragPart = 0;             // part dragged in the list (re-parenting)
        // animation
        int clip = 0;
        float time = 0;
        bool playing = false;
        float speed = 1.0f;
        bool autoKey = false;
        int keyTrack = -1, keyIndex = -1;  // selected key (track index in the clip, key index)
        bool keyDrag = false, scrubbing = false;
        float keyDragStart = 0;
        float timelineZoom = 1.0f;         // pixels per second factor
        // view
        Camera camera;
        bool orbiting = false, panning = false;
        float lastX = 0, lastY = 0;
        ScreenRect rcView;
        ViewRect vpPx;
        int debugView = 0;                 // 0 lit, 1 albedo, 2 normals, 3 UV checker, 4 clay, 5 part colours
        int wireframe = 0;                 // 0 off, 1 wire, 2 overlay
        bool showGrid = true, showBounds = true, showFootprint = true, showPivots = true, showSelection = true;
        bool turntable = false;
        // lighting of the studio world
        int lightPreset = 0;               // 0 studio, 1 daylight, 2 sunset, 3 night, 4 HDRI
        float keyIntensity = 1.0f, fillIntensity = 1.0f, rimIntensity = 1.0f, envIntensity = 1.0f;
        float sunAzimuth = 35.0f, sunElevation = 48.0f;
        float exposure = 0.0f;
        int floorStyle = 0;                // 0 grid, 1 plain, 2 none
        std::wstring hdri;
        // layout
        float leftW = 250, rightW = 300, bottomH = 236;
        int inspectorTab = 0;              // 0 part, 1 lighting, 2 debug, 3 asset
        // UV Editor (centre tab 1): UV sets of the meshes, view, checks
        std::map<std::string, std::vector<UvSet>> uvCache;   // mesh file key -> its UV sets
        std::vector<std::vector<UvSet>> partUv;              // part index -> UV sets of its mesh (scene build)
        int centerTab = 0;                 // 0 3D view, 1 UV Editor
        int uvSetA = 0, uvSetB = 1;
        int uvCompare = 0;                 // 0 one set, 1 overlay, 2 side by side
        int uvFill = 0;                    // 0 wireframe, 1 islands, 2 overlap, 3 flipped, 4 texel density, 5 stretch
        int uvBackground = 1;              // 0 none, 1 checker, 2 texture
        float uvZoom = 0;                  // pixels per UV unit (0 = frame at the next draw)
        float2 uvPan{0.5f, 0.5f};          // UV at the centre of the canvas
        int uvFrame = 2;                   // frame at the next draw: 1 the 0-1 square, 2 all the UVs
        bool uvPanning = false;
        std::map<std::string, ImTextureID> uvTextures;      // texture path -> ImGui texture (background)
        std::map<uint64_t, UvAnalysis> uvAnalysis;          // (part id, set, build) -> checks
        // Rig module (centre tab 2): skeleton, skinning, weight painting, pose
        int rigMode = 0;                   // 0 skeleton, 1 skin, 2 paint weights, 3 pose
        int rigTab = 0;                    // inspector: 0 joint, 1 skin, 2 paint
        int rigTool = 2;                   // 0 select, 1 move, 2 rotate
        int selJoint = -1;                 // joint index
        int skinPart = -1;                 // part index of the skin shown / bound / painted
        bool showBones = true, showJointNames = true, showWeights = true, xray = true;
        int weightView = 0;                // 0 selected influence (heat), 1 every influence (joint colours)
        SkinBindOptions bindOpt;
        int brushMode = 0;                 // PaintMode
        float brushRadius = 0.3f, brushStrength = 0.4f, brushValue = 1.0f;
        int brushFalloff = 0;              // 0 smooth, 1 linear, 2 constant
        bool painting = false, brushHitValid = false;
        float3 brushHit, brushNormal;
        float brushLastStamp = 0;
        std::map<uint32_t, PartPose> poseOverride;   // joint id -> local pose set in Pose mode (not keyed)
        std::vector<PartPose> jointPose;
        std::vector<PartWorld> jointNow, jointRest;
        std::vector<MeshData> partRest;    // part index -> mesh as built (local), kept for the skinned parts
        std::map<uint32_t, WeldGraph> weld;           // part id -> welded points (smoothing, painting)
        std::map<uint32_t, uint64_t> skinSig;         // part id -> pose + weights signature of the last deformation
        uint32_t weightsVersion = 1;       // bumped by every weight edit (the skins deform again)
        int jgAxis = -1, jgHover = -1;     // joint gizmo: axis dragged / hovered (0..2 move, 3..5 rotate)
        float jgT0 = 0, jgAngle0 = 0;
        float3 jgStartPos, jgStartRot, jgDir, jgOrigin;
        uint32_t jointRename = 0;
        char jointRenameBuf[96] = {};
        bool jointRenameFocus = false;
        uint32_t jointDragTarget = 0;
        char jointSearch[96] = {};
        std::string rigStatus;             // last import / bind report
        int bottomTab = 0;                 // 0 timeline, 1 UV layout
        char search[96] = {};
    } m_pf;
    void prefabInit();
    void prefabNew(bool demo);
    bool prefabSave(bool saveAs);
    bool prefabOpen(const std::wstring& path);
    void prefabOpenDialog();
    bool prefabDirty() const;
    void prefabPushUndo();
    void prefabUndo();
    void prefabRedo();
    void prefabBuildScene();             // full rebuild (structure, meshes, textures)
    void prefabSyncScene();              // every frame: pose, materials, lights
    void prefabLighting();
    void renderPrefab();
    void prefabFrameAll();
    void prefabViewInput(bool hovered);
    void prefabShortcuts();
    void prefabAddPart(int kind);        // 0 pivot, 1.. PRIM_*, 100 mesh file (dialog)
    void prefabDuplicate();
    void prefabDelete();
    void prefabKeySelected(int channel = -1);   // key the selected part at the current time (-1 = all channels)
    void prefabSetTime(float t);
    void prefabAddToScene();             // place the prefab in the main scene: a live instance (PrefabInstance.h)
    void prefabUnpackSelected();         // the selected instance becomes a plain group of objects
    void prefabAddToSceneFlat();         // the former placement: the current pose as a group of plain objects
    // live prefab instances of the main scene: their prefab files, parts kept in sync and posed every frame (clips
    // play in Play, and in the editor when m_prefabAnimate)
    PrefabLibrary m_prefabs;
    bool m_prefabAnimate = false;
    uint64_t m_prefabCheckTick = 0;
    void updatePrefabs();
    bool prefabMeshFor(const PrefabPart& p, MeshData& out, std::string* err);
    PrefabClip* prefabClip();
    void prefabStep(float dt);
    // panels (PrefabUI.inl)
    void drawPrefabStudio(float X, float Y, float W, float H);
    void pfParts(float x, float y, float w, float h);
    void pfViewport(float x, float y, float w, float h);
    void pfInspector(float x, float y, float w, float h);
    void pfTimeline(float x, float y, float w, float h);
    // Rig module (PrefabStudio.cpp logic, RigUI.inl panels)
    void rigAddJoint(bool child);        // at the selected joint (child) or at the origin
    void rigAddChain(int count, float length);
    void rigDeleteJoint();
    void rigOrientJoint();             // the selected joint aims +Y at its first child (children keep their place)
    void rigMirrorJoints();            // creates the right side of the left joints (and the reverse), mirrored on X
    void rigBind(int partIndex);         // bind the part to every joint (bind options of the studio)
    void rigUnbind(int partIndex);
    void rigJointPose(int joint, PartPose& local) const;   // local pose shown (override, clip, rest)
    void rigSetJointLocal(int joint, int channel, const float3& v);   // edit in the current mode (rest / pose / key)
    void prefabKeyJoint(int channel = -1);
    void rigLoadSample();
    void rigImportFbx();
    bool rigExportGltf(int partIndex, const std::wstring& path);   // skinned part + skeleton + clips as a .gltf (embedded buffer)
    bool rigImportFile(const std::wstring& path);
    void rigViewInput(bool hovered);
    void rigPaintStamp(bool first);
    void pfSkeleton(float x, float y, float w, float h);
    void pfRigViewport(float x, float y, float w, float h);
    void pfRigInspector(float x, float y, float w, float h);
    void pfRigOverlay(float x, float y, float w, float h);
    void pfUvEditor(float x, float y, float w, float h);
    void pfUvCanvas(float x, float y, float w, float h, int part, int set, int other, const char* id);
    void pfUvSidebar(float x, float y, float w, float h, int part);
    const UvAnalysis& prefabUvAnalysis(int part, int set);   // checks of a UV set of a part (cached until the next scene build)
    ImTextureID prefabUvTexture(const std::string& path);     // background image of the UV Editor

    // Atelier layout (AtelierUI.cpp): fixed concept layout with the Atelier components
    void drawAtelier();
    void atlTopBar(float x0, float y0, float width);
    void atlHierarchy(float x, float y, float w, float h);
    void atlViewport(float x, float y, float w, float h);
    void atlViewMenus(float x, float y, float w);   // camera, display, renderer and axes menus inside the view (w: the room)
    void atlContent(float x, float y, float w, float h);
    void atlInspector(float x, float y, float w, float h);
    void atlMenuItems(int menu);                                        // items of the top bar menus (classic and Spartan)
    void atlInspectorPage(int page, float x, float y, float w, float h);  // an inspector page in a scrolling child
    // Spartan theme (SpartanUI.cpp, design/halo/mockup-halo-espaces.html): its own layout and drawing for every workspace
    void spDraw(float X, float Y, float W, float H);
    void spTopBar(float x, float y, float w, float h);
    void spRail(float x, float y, float w, float h);
    void spScenePanel(float x, float y, float w, float h);
    void spViewport(float x, float y, float w, float h);
    void spDrawer(float x, float y, float w, float h);
    void spInspector(float x, float y, float w, float h);
    void spStatus(float x, float y, float w, float h);
    void spMaterials(float x, float y, float w, float h);
    void spPrefab(float x, float y, float w, float h);
    struct SpartanState {
        ImTextureID icons = 0;      // assets/ui/spartan_icons.png
        float iconsW = 1024, iconsH = 896;
        bool loaded = false;
        int railPage = 0;           // inspector page shown (Actor, Light, Render, World)
        int drawerTab = 0;          // Assets, Console, Graph
        bool sect[8] = {true, true, true, false, true, true, true, true};   // open inspector sections (3: Avancé, closed)
        int matSel = 0, matShape = 0, matTab = 0;
        int prefabSel = 0, prefabComp = 4, prefabTab = 0;
        bool prefabOn[7] = {true, true, true, true, true, false, true};
    } m_sp;
    void atlInspectorActor(float x, float y, float w, float h);
    void atlInspectorLighting(float x, float y, float w, float h);
    void atlInspectorRender(float x, float y, float w, float h);
    void atlInspectorWorld(float x, float y, float w, float h);
    struct AtelierState {
        float leftW = 222, rightW = 284, bottomH = 190;   // reference pixels of the Graph Editor (scaled by s)
        bool searchFocus = false;                          // Halo rail: focus the Scene Manager search next frame
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
    float gizmoScale(const float3& pivot) const;   // size of the curve point manipulator (CurveEditor.cpp): 7 % of the distance
    void gizmoFrame(int obj, int tool, float3 ax[3]) const;
    std::vector<int> gizmoTargets() const;
    bool markingMenuInput(bool inView);
    void drawMarkingMenu(ImDrawList* dl);
    void markingSelect(int op);   // 0 all, 1 none, 2 invert, 3 hierarchy, 4 Advanced Scale on
    void markingReset(int what);  // 0 position, 1 rotation, 2 scale
    // Advanced Scale (AdvancedScale.cpp): bounding box scaling of the selection from its faces, edges and corners.
    // A separate tool (Alt+T), the standard manipulators are unchanged. Handles: index (s0+1)*9 + (s1+1)*3 + (s2+1) of
    // the side vector s in {-1, 0, 1}^3 of the box (one non-zero: face, two: edge, three: corner).
    struct AdvBox { bool valid = false; float3 c, ax[3], h; int flatAxis = -1; };   // centre, axes, half sizes
    struct AdvDrag {
        bool active = false, moved = false;
        int handle = -1;
        int s[3] = {0, 0, 0};
        AdvBox box0;
        float3 p0, hit0, n;      // handle position at the press, press point on the drag plane, its normal
        float t0 = 0;            // face handle: press parameter along the axis
        bool snapped = false;    // vertex snapping (V) found a vertex this frame
        float3 snapVertex;
        struct Item { int obj; float3 pos; float4 rot; float3 scale; };
        std::vector<Item> items;
    };
    bool m_advScale = false;             // the tool is on
    bool m_advCorners = false;           // edge handles without holding Ctrl (menu "Edge Handles"; the corners are always shown)
    bool m_advShowInPlay = true;         // the box stays drawn while playing (menu "Show in Game View")
    bool m_advPicking = false;           // eyedropper: the next click picks the orientation object
    uint32_t m_advRefId = 0;             // object giving the orientation of the box of several objects (0 = world)
    int m_advHover = -1;
    bool m_showAdvSettings = false;      // Advanced Scale Settings window
    AdvDrag m_adv;
    bool advScaleBox(AdvBox& b);         // the bounding box of the selection
    bool advScaleInput(bool inView);     // hover and drag of the handles; true while it uses the mouse
    void advScaleBegin(int handle, const AdvBox& b, float mx, float my);
    void advScaleDrag(float mx, float my);
    int m_advMods = -1;                  // self-test and captures: forced modifiers (1 Shift, 2 Alt, 4 V), -1 = the keyboard
    void advScaleDemo(const std::string& mode);   // --open-menu advScale...: states of the tool for the captures
    void drawAdvScale(ImDrawList* dl);
    void drawAdvScaleMenu(float x, float y, float w, float h);   // menu and modifier hints of the viewport (AtelierUI.cpp)
    void drawAdvScaleSettings();          // Advanced Scale Settings window (Editor.cpp)
    void setAdvScale(bool on);
    // cameras of the viewports (Cameras.cpp): built-in persp / top / front / side... and the camera objects
    struct ViewPane {
        int cam = VCAM_PERSP;          // the built-in camera it looks through (ViewCam), or VCAM_OBJECT
        uint32_t object = 0;           // VCAM_OBJECT: id of the camera object
        ScreenRect rc;                 // on screen, this frame
    };
    Camera m_viewCams[VCAM_COUNT];     // the built-in cameras (the active pane's is m_scene.camera meanwhile)
    std::vector<ViewPane> m_panes = std::vector<ViewPane>(1);   // the panes of the layout (Panes.cpp)
    int m_activePane = 0;              // looks through m_scene.camera, drawn by the full renderer
    Camera m_objCamLoaded;             // the camera object's view as loaded this frame (written back when it moved)
    static const char* viewCamName(int k);    // persp, top, front, side, bottom, back, left
    static const char* viewCamLabel(int k);   // menu labels
    void resetViewCams();
    int cameraObject(uint32_t id) const;
    Camera cameraOfObject(const Object& o) const;
    void cameraToObject(Object& o, const Camera& c);
    Camera paneCamera(int pane) const;
    std::string paneCameraName(int pane) const;
    const Camera& perspCamera() const;
    void storeActiveCamera();
    void loadActiveCamera();
    void setPaneCamera(int pane, int cam, uint32_t object = 0);
    void activatePane(int pane);
    bool orbitLocked() const;
    void applyViewBookmark(const Camera& c);
    int renderCamera() const;          // the camera object that is the render camera, -1 for none
    void setRenderCamera(int idx);
    int createCameraFromView(bool render);
    void lookThroughRenderCam();
    void lookThroughSelected();
    void demoCameras();                // --demo-cameras: a render camera and a camera around the view (the latter selected)
    bool cameraIcon(int idx, std::vector<std::pair<float3, float3>>& lines) const;
    void drawCameraIcons(ImDrawList* dl);
    int pickCameraIcon(float mx, float my);
    int m_iconPane = -1;               // drawCameraIcons for this pane (-1: the active one)
    void cameraMenu(int pane);         // the cameras a pane can look through (AtelierUI.cpp)
    // split layouts (Panes.cpp) and the camera preview
    int m_layout = 0;                  // kLayoutCount layouts, see layoutName
    bool m_paneMax = false;            // Space: the active pane alone in the view
    bool m_paneClicked = false;        // a click made this pane the active one: the view takes that click too (Maya)
    uint32_t m_previewPin = 0;         // camera object kept in the preview when it is not selected
    ScreenRect m_previewRc;            // image of the preview on screen, this frame (0: none)
    bool m_previewShown = false;
    static const char* layoutName(int layout);
    static int layoutCols(int layout);
    static int layoutRows(int layout);
    void setLayout(int layout);
    ScreenRect layoutPanes(const ScreenRect& area);
    void togglePaneMax(bool underMouse = true);   // (false: the active pane)
    void paneInput(bool windowHovered);
    // the lines between the panes, dragged to resize them: boundaries of the columns and rows of each layout (fractions
    // of the area, 0: the equal split); the line under the mouse or dragged (0: none, else 1 + its index)
    float m_splitX[kLayoutCount][3] = {}, m_splitY[kLayoutCount][3] = {};
    int m_splitCol = 0, m_splitRow = 0;
    bool m_splitDrag = false;
    bool m_splitHot = false;           // a line under the mouse or dragged: the panes and the view leave the mouse to it
    ScreenRect m_paneArea;             // the area of the panes this frame (layoutPanes)
    float splitAt(bool rows, int k) const;   // boundary k (0..n) of the columns or rows of the layout, 0..1
    void splitInput(const ScreenRect& area, bool windowHovered);
    // Space, as Maya: a tap goes from one view to the four views, and shows the pane under the mouse alone (and back);
    // held, the view menu at the mouse: top, side, front and persp around it, the cameras of the scene listed under
    // it; a quick flick chooses without the menu (the choice shown for a moment). hover: index of the item
    struct ViewMenu { bool down = false, shown = false; int pane = 0, hover = -1; float x = 0, y = 0; double t0 = 0; } m_vm;
    struct ViewMenuItem { int dir; int cam; uint32_t object; std::string label; ImVec2 b0, b1; };   // dir -1: in the list of cameras
    std::vector<ViewMenuItem> viewMenuItems() const;   // with their pill around the centre of the menu (m_vm.x, y)
    int viewMenuHover(const std::vector<ViewMenuItem>& items, float mx, float my) const;
    void spaceInput(bool allowed);   // after the editor of the frame (the view sees the menu open on the frame it closes)
    void drawViewMenu(ImDrawList* dl);
    bool selectionInView(const Camera& c, float w, float h, float2& centre, float& radius) const;
    void renderPanes();
    int previewCamera() const;
    void drawPanes();                  // the other panes: images, frames, camera names (AtelierUI.cpp)
    void drawCameraPreview(float x, float y, float w, float h);   // AtelierUI.cpp
    // selection overlay (SelectionFx.cpp): white glow on the outline vertices, outline in the floor reflection
    struct SilEdge { float3 a, b, n0, n1; bool open = true; };   // mesh edge (object space) and its two face normals
    struct SilCache { bool built = false; uint32_t version = 0; size_t vertexCount = 0, indexCount = 0; float3 first; std::vector<SilEdge> edges; };
    std::map<int, SilCache> m_silCache;   // per mesh
    const std::vector<SilEdge>& silhouetteEdges(int mesh);
    void drawSelectionFx(ImDrawList* dl);
    struct FxStats { int glints = 0, creases = 0, mirrorLines = 0, mirrorGlints = 0; } m_fxStats;   // last frame (self-test)
    void toggleGroundGrid();
    void marqueeInput();

    HWND m_hwnd = nullptr;
    Options m_opt;
    vp3d::Renderer m_renderer;
    ImGuiLayer m_gui;
    GameHost* m_game = nullptr;   // node graph, Play, builds, command API
    CurveEditor* m_curves = nullptr;   // Create > Curve, point editing (CurveEditor.cpp)
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
    float m_shotGpuAcc = 0.0f;   // GPU time of the accumulation so far (--shot-gpu-ms)
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
    bool m_showGridOptions = false; // Grid Options window (Shift+click on the Grid button)
    void drawGridOptionsWindow();     // Editor.cpp
    void toggleGrid();                // Grid button: shows / hides the editor grid (Shift: options)
    int m_msaa = 4;               // viewport anti-aliasing: MSAA samples (1 off, 4, 8, 16; Display menu)
    size_t m_selSig = 0;          // selection signature: the glow animation restarts when it changes
    double m_selSince = 0;
    int m_axisMode = 0;          // move / rotate axes: 0 world, 1 object, 2 parent (scale: always object)
    float m_stepMove = 1.0f, m_stepRotate = 15.0f, m_stepScale = 0.1f;   // increments (Shift, or Snapping on)
    bool m_snapAbsolute = false; // move increments land on the grid of the step instead of relative to the start
    // Snapping (Snapping.cpp): the grid points of the absolute snapping (intersections, centres of the cells, middles of
    // the edges, SnapOptions), drawn around the moved object; V: the vertex nearest to the cursor; the options window
    // (Shift + the Snapping button); placement on the surfaces (Place button); the pivot (Edit Pivot: C or Insert,
    // Center Pivot: Alt+Y)
    bool m_showSnapOptions = false;
    bool m_surfacePlace = false;       // Place: a selected object dragged with the left button slides over the surfaces
    struct SurfaceDrag { bool active = false, moved = false; std::vector<int> objs; std::vector<float3> offs; std::vector<float4> rots; int lead = -1; float3 hit, normal; } m_sd;
    bool m_pivotEdit = false;          // Insert: Edit Pivot until pressed again
    bool m_pivotHold = false;          // D held: Edit Pivot meanwhile
    int m_pivotTool = -1;              // the tool before Edit Pivot (the move manipulator moves the pivot)
    bool m_snapShown = false;          // snap points drawn this frame (tests)
    float3 m_snapTarget;               // the grid point the dragged pivot lands on (drawn lit)
    bool m_snapTargetOn = false;
    bool pivotEditing() const { return m_pivotEdit || m_pivotHold; }
    float snapCell() const;            // size of the cells of the absolute snapping
    float3 snapGridPoint(const float3& p, const bool on[3]) const;
    bool nearestVertex(float mx, float my, const std::vector<int>& skip, float maxPx, float3& out) const;
    void drawSnapPoints(ImDrawList* dl);
    void drawSnapOptionsWindow();
    void toggleSnap();                 // the Snapping button: on / off; Shift: the options
    bool surfacePlaceInput(bool inView, bool click);
    void surfacePlaceDrag(float mx, float my);
    void pivotInput(bool allowed);     // D / Insert / Alt+Y
    void centerPivot();
    void hierRebase(uint32_t id);      // Outliner.cpp: the children do not follow this move of their parent
    float m_camSpeed = 1.0f;     // camera navigation speed factor
    struct Marquee { bool active = false, moved = false; float x0 = 0, y0 = 0; } m_marq;
    GizmoDrag m_gz;
    int m_gzHover = MH_NONE;
    int m_gzLast[4] = {MH_NONE, MH_NONE, MH_NONE, MH_NONE};   // last used handle per tool (middle button, drawn yellow)
    // (shown: drawn, after a short delay: a quick flick only does the item in its direction, as in Maya)
    struct MarkingMenu { bool open = false, shown = false; int tool = 0, hover = -1; float x = 0, y = 0, mx = 0, my = 0; double t0 = 0; } m_mm;
    // a quick gesture: the chosen item alone, lit for a moment (the marking menus, the view menu of Space)
    struct RadialFlash { double t = -10; ImVec2 b0, b1, centre; std::string label; };   // (centre: of the menu, the line of the choice)
    RadialFlash m_mmFlash, m_vmFlash;
    static void pillRect(ImVec2 centre, int dir, const std::string& label, float dpi, ImVec2& b0, ImVec2& b1);
    static void drawPill(ImDrawList* dl, ImVec2 b0, ImVec2 b1, const std::string& label, bool hot, float alpha, float dpi, ImU32 edge);
    void drawRadialFlash(ImDrawList* dl, const RadialFlash& f);
    bool m_gzDemo = false;       // --gizmo-demo: a drag / marking menu frozen on screen for a capture
    int m_shotSeqDone = 0;       // --shot-seq: images saved
    void gizmoScript();          // --gizmo-demo / --gizmo-test (command line), called once per frame
    float2 gizmoHandlePoint(int handle);   // screen point on a handle of the current manipulator (tests)
    float2 gizmoHandleDir(int handle, float2 at);   // screen direction to drag that handle along (tests)
    int gizmoTestDrag(int tool, int handle, float px, bool snap, bool keep);   // scripted drag; returns the handle hit
    std::string gizmoLogicTests(int& fails);   // logic checks of the manipulators and Advanced Scale (report lines)
    // editor self-test (--self-test, SelfTest.cpp): logic, interface driven by scripted input, renders measured
    struct SelfTest;
    std::shared_ptr<SelfTest> m_selfTest;
    void selfTestFrame();        // called once per frame
    int m_exitCode = 0;          // of run(): 1 when the self-test failed
    std::vector<Snapshot> m_undo, m_redo;


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
