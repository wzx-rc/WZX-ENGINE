// WZX Engine editor: hosts the vp3d renderer in the Atelier workspaces (Scene, Graph Editor, Material Studio)
// drawn with Dear ImGui.
#pragma once
#include "Content.h"
#include "ImGuiLayer.h"
#include "Json.h"
#include "MeshFile.h"
#include "Prefab.h"
#include "Rig.h"
#include "UvCheck.h"
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
enum SceneId { SCENE_CORNELL = 0, SCENE_STUDIO = 1, SCENE_LOOKDEV = 2 };

// Move / rotate manipulator drag on the selected object.
struct GizmoDrag {
    bool active = false;
    int axis = 0;                // 0 X, 1 Y, 2 Z
    bool rotate = false;
    int mode = 0;                // 0 move, 1 rotate, 2 scale
    float3 dir;                  // axis direction at the press (world or local)
    float3 startScale;
    float s0 = 1;                // gizmo size at the press
    int obj = -1;
    float3 startPos, pivot;
    float4 startRot = qIdentity();
    float t0 = 0;                // move: parameter along the axis at the press
    float3 v0;                   // rotate: vector pivot -> press point in the ring plane
    int steps = 0;
    bool moved = false;
    struct Other { int obj; float3 pos; float4 rot; float3 scale; };
    std::vector<Other> others;   // rest of a multi-selection moved / turned with the primary object
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

class App {
    friend class GameHost;   // game layer (GameHost.h, AppGame.cpp)
public:
    bool init(HINSTANCE inst, const Options& opt);
    int run();
    LRESULT wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

private:
    // core (App.cpp)
    void loadScene(int id);
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
    void prefabAddToScene();             // place the prefab (current pose) in the main scene as a group
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
    bool gizmoInput(bool inView);
    void drawGizmo(ImDrawList* dl);
    bool gizmoHit(float mx, float my, int& axis, bool& rotate);
    float gizmoScale(const float3& pivot) const;
    float3 gizmoAxis(int a) const;
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
    bool m_magnet = true;
    int m_selected = -1;
    std::vector<int> m_multi;
    bool m_orbiting = false, m_panning = false, m_dragging = false;
    float m_lastX = 0, m_lastY = 0;
    float m_dragPlaneY = 0;
    float3 m_dragOffset;
    bool m_dragMoved = false;
    bool m_turntable = false;
    bool m_autoFocus = true;
    bool m_showGrid = true;
    bool m_showStats = true;
    bool m_showGizmo = true;
    bool m_localSpace = false;   // gizmo axes: world (false) or the object's own axes
    float m_camSpeed = 1.0f;     // camera navigation speed factor
    struct Marquee { bool active = false, moved = false; float x0 = 0, y0 = 0; } m_marq;
    GizmoDrag m_gz;
    int m_gzHover = -1;
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
