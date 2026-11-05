// BrickIt - native brick builder (PC, DirectX 12) on the vp3d renderer, with the WZX Engine Dear ImGui theme.
//   BrickApp.cpp  window, loop, stage, scene sync, test scenes
//   BrickEdit.cpp mouse and keys: brick in hand, selection, undo, files
//   BrickUi.cpp   panels: toolbar, library (two rows at the bottom), colours (right), stats
//   Thumbs.cpp    part thumbnails rendered by the engine (atlas, cached on disk)
#pragma once
#include "Build.h"
#include "Pdf.h"
#include "Mech.h"
#include "Library.h"
#include "app/ImGuiLayer.h"
#include "vp3d/Renderer.h"
#include <windows.h>
#include <string>
#include <map>
#include <memory>
#include <vector>

namespace brickit {

struct Ui;   // drawing helper of BrickUi.cpp
// view of a thumbnail (Thumbs.cpp): angles, zoom (1 = the box at 85 % of the image), pan (fractions of the half view)
struct ThumbView { float yaw = 0, pitch = 0, zoom = 1, panX = 0, panY = 0; };

using namespace vp3d;

struct Options {
    int width = 1600, height = 900;
    std::wstring shotPath;    // --shot file.png: window capture then exit
    int shotSpp = 64;         // --spp N: path tracing samples before the capture
    int shotFrames = 20;      // raster: frames before the capture
    bool grid = false;        // --grid: every part of the library side by side
    int bench = 0;            // --bench N: N random bricks
    bool pathTrace = false;   // --pt: progressive path tracing
    int bounces = -1;         // --bounces N (path tracing, 0 = primary rays only)
    int bevelSpp = -1;
    bool rc = false;          // --rc: Radiance Cascades mode
    bool noGiBounds = false;  // --no-gi-bounds
    std::wstring hdri;        // --hdri name (part of a file name of assets\hdri, "ciel" = procedural sky) or path
    float hdriRot = -1, hdriInt = -1;   // --hdri-rot degrees, --hdri-int
    int gridColor = -1;       // --grid-color RRGGBB (hex)
    float gridAlpha = -1;     // --grid-alpha 0..1
    int softGI = -1;          // --soft-gi 0 / 1
    float giStrength = -1;    // --gi-strength 0..2
    int showProbes = -1;     // --show-probes 0 / 1
    int softGIProbes = -1;   // --soft-gi-probes n: probe budget of the soft GI (-1: the setting)
    float softGIMargin = 1.0f;  // --soft-gi-margin k (measures: the margin of the probe volume around the bricks, x k)
    int giAgeCap = 400;   // --gi-age-cap n: updates after which a probe is converged and left alone (0: always updated)
    float hqSSS = -1;   // --hq-sss s: subsurface of the HQ view (0 = off, -1: the setting)
    std::wstring thumbSampleDir; std::string thumbSampleParts;   // --thumb-sample <dir> <part,part,...>
    std::string thumbEdit;      // --thumb-edit <part>: the thumbnail editor open (captures); --thumb-edit-save: saved at frame 90
    bool thumbEditSave = false;
    std::wstring thumbDir;      // --thumb-dir <dir>: the overrides there instead of %LOCALAPPDATA% (tests)
    std::wstring fullThumbs;   // --build-full-thumbs pack: renders the thumbnails of the full library and exits
    std::wstring importPath;   // --import model.mpd: an LDraw model (OMR), the reading mode opened
    int readStep = -1;         // --read-step n: the page read for the capture
    std::wstring importReport; // --import-report file: pieces, steps, parts not found
    std::wstring groupTest;   // --group-test file: the groups checked (BrickGroups.cpp)
    bool useDefaults = false;   // --use-defaults: a capture with the default settings (assets / %LOCALAPPDATA%)
    int stillAA = -1;   // --still-aa 0 / 1
    int touchHud = -1;  // --touch-hud 0 / 1
    int freeRot = 0;    // --free-rot n: the selection turned n x 15 degrees at start (captures)
    bool colorArc = false;   // --color-arc: the colour arc of the touch HUD open (captures)
    float arcPhase = -1;     // --arc-phase t: the arrival of the swatches frozen at t (0..1, captures of the animation)
    float dof = -1, bloom = -1;   // --dof s, --bloom s (-1: the settings)
    float renderScale = -1;  // --render-scale s: the image rendered at s of the window (-1: the setting)
    int giSoft = 1;          // --gi-soft 0: the sharp visibility weights of the probes (comparison)
    float giSmooth = 0.0f;   // --gi-smooth r: probe GI read over the surface (radius in spacings, 0 = off; 0.5: +6 ms at 2000 x 1125)
    int giHold = 1;   // --gi-hold 0: the probes go on updating while a brick is carried (comparison)
    int giCarryBrick = -1, giCarryFrom = 0, giCarryTo = 0;   // --gi-carry i f1 f2: brick i carried in the air from frame f1, put down 3.5 cm in front at f2 (GI hold)
    int giEditBrick = -1, giEditOff = 0, giEditOn = 0;   // --gi-edit i f1 f2: brick i removed at frame f1, back at f2 (GI reactivity)  // --soft-gi-probes n (measures: probe budget of the soft GI)
    std::wstring writeHouse;  // --write-house file: builds the GI reference house (light grey bricks) and saves it
    bool giLookdev = false;   // --gi-lookdev: one light grey material, rough, no specular / coat, a light SSS
    float hqAO = -1;          // --hq-ao cm
    int hqRefl = -1;          // --hq-refl rays
    int aa = -1, shadows = -1;   // --aa 0..2 (MSAA 4x / 8x / AAA), --shadows 0..4 (ray traced shadows of the RC mode)        // --bevel-spp N (0 = bevel off)
    bool opaque = false;      // --opaque: benchmark bricks in standard colours only
    int denoise = -1;         // --denoise 0|1
    int ppf = 1;              // --ppf N: path tracing samples per frame
    bool noHdri = false, noGround = false;   // --no-hdri, --no-ground (render cost tests)
    float groundSize = 2000.0f;   // --ground size (cm)
    float ptMoveScale = 0.5f;     // path tracing resolution while the view moves
    bool sss = true;              // --no-sss
    bool demo = false;            // --demo: showcase of a few bricks (beauty captures)
    bool cleanGround = false;     // --clean: floor without the grid lines
    float exposure = 1.3f;        // --exposure
    int tonemapper = 0;           // --tonemap 0 ACES (default, as the web builder), 1 AgX, 2 AgX Punchy, 3 Standard
    float yaw = -1000, pitch = -1000;   // --yaw / --pitch degrees (camera)
    int stride = 0;           // --stride 1|4: path tracing pixels per frame (0 = 4 interactive, 1 for captures)
    float zoom = 1.0f;        // --zoom f: camera distance x f after framing everything
    bool noUi = false;        // --no-ui: viewport and stats only (benchmarks, beauty captures)
    float uiScale = 0.0f;     // --ui-scale f: forced UI scale (1 = 100 %, 1.5 = 150 %), 0 = the monitor's
    std::wstring openPath;    // --open scene.v3d.json
    // scripted UI test (captures): spawn a part in hand at a screen point, select a brick, a library category
    std::string spawnPart;    // --spawn PART
    float spawnX = 0.5f, spawnY = 0.5f;   // --at x y (fractions of the viewport)
    int selectBrick = -1;     // --select N
    int mode = -1;
    std::string infoPart;   // --info <part>: open the part sheet (captures)
    bool pivotEdit = false;
    bool dev = false;
    std::wstring pdfPath;       // --pdf <file>: import a PDF into the manual (captures)
    bool manual = false;        // --manual: show the manual window
    bool floatTray = false, optsRight = false;   // --float-tray x y, --opts-right (captures)
    float floatX = 0, floatY = 0;
    std::string ttBase, ttTop;
    std::string mouseTest;
    bool mechTests = false;     // --mechtests: the battery of mechanics tests, then quit
    int quality = -1;
    bool settings = false;
    bool cornell = false;       // --cornell
    bool noEnv = false;         // --no-env
    std::wstring ptTrace;       // --pt-trace <file>      // --settings: the settings panel open (captures)           // --quality 0 | 1      // --mouse-test marquee | gizmo | ring | mech  // --turntable-test <base> <top>: mechanics self-test
    std::string devRoundtrip;
    std::wstring mayaOut;       // --maya <file.zip>: export the scene to Maya then quit (tests)   // --dev-roundtrip <part>: OBJ export / import self-test
    int libCategory = -1;     // --cat N
    bool focusDim = false;    // --focus
    std::wstring savePath;    // --save file: the scene saved at the capture (file format test)
    bool duplicate = false;   // --dup: Ctrl+D on the selection at start
    bool mechDemo = false;    // --mechdemo: Technic gear train
    bool mech = false;        // --mech: start in Mechanical mode
    float motor = 0;          // --motor deg: value of the first command (Mechanical mode test)
};

class BrickApp {
public:
    bool init(HINSTANCE inst, const Options& opt);
    int run();
    LRESULT wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

private:
    // BrickApp.cpp
    void frame();
    void cameraInput();
    void rebuildScene();      // bricks -> vp3d scene (meshes once per part, one object per brick)
    void syncObjects();       // brick transforms and materials only
    int meshOf(int part);     // scene mesh of a part (uploaded once)
    void buildPartGrid();
    void buildBench(int count);
    void buildDemo();
    int colorIndex(int ldrawId) const;   // palette index of an LDraw colour code
    void setupStage();        // ground, HDRI, materials of the palette
    void buildHouse();        // the GI reference house (--write-house)
    void setLookdev(bool on); // GI look development: every colour the same light grey (live, Réglages › Lumière)
    void openTestHouse();     // Dossier › Ouvrir la maison test (docs/brickit/tests/maison_gi.v3d.json)
    // Réglages > Lumière (native addition): environment image, its rotation and intensity, colour of the ground grid;
    // the same scene for every renderer (Rapide, HQ, RC, path tracing), saved in brickit_prefs.json ("light")
    void buildHdriList();     // L"" (procedural sky), assets\hdri\*.hdr | *.exr, then the user's HDRI
    void setHdri(const std::wstring& path);
    void loadHdriDialog();
    uint32_t gridLineValue() const;
    std::vector<std::wstring> m_hdriPaths, m_hdriCustom;
    std::wstring m_hdriPath = L"*";   // "*": the studio of the web (first start)
    std::map<std::wstring, vp3d::EnvImagePtr> m_hdriCache;
    float m_hdriRotDeg = 0.0f, m_hdriInt = 0.55f, m_hdriLoadMs = 0.0f;
    float3 m_gridRgb{0.6f, 0.64f, 0.72f};
    float m_gridAlpha = 0.0f;   // 0 = the default lines
    int m_setDrag = -1;         // slider of the settings being dragged
    void applyLook();         // real time = the dark stage of the web; path tracing = the bright studio
    void frameAll();
    bool savePng(const std::wstring& path, std::vector<uint8_t> rgba, uint32_t w, uint32_t h);
    void toast(const std::string& text);

    // BrickEdit.cpp
    View view() const;
    void editInput();                       // viewport mouse and keys
    int pickBrick(float sx, float sy, float3* point = nullptr, int exclude = -1);
    void spawn(int part, float sx, float sy);
    void beginMove(int brick, float sx, float sy);
    void updateHeld(float sx, float sy);
    void dropHeld(bool keepCopy = false);
    void cancelHeld();
    void select(int brick, bool add = false);
    // groups (BrickGroups.cpp)
    std::vector<int> m_groupCtx;   // the group entered by a double click (and its parents), outermost first
    int m_isolateGroup = -1;       // outliner: only this group shown (-1 = all)
    int unitGroup(int brick) const;              // the group selected with the brick at the level entered, -1 = alone
    std::vector<int> unitOf(int brick) const;    // the bricks selected with it
    std::vector<int> groupMembers(int group) const;
    bool brickHidden(int i) const;
    void fitGroupContext(int brick);
    void enterGroup(int brick);
    bool leaveGroup();
    void groupSelected();
    void ungroupSelected();
    void rotateGroup(const std::vector<int>& s, int dir);
    bool duplicateGroup(const std::vector<int>& s);
    bool m_groupDrag = false;
    std::vector<int> m_groupDragBricks;
    std::vector<float3> m_groupDragPos;
    float3 m_groupDragStart;
    float m_groupDragY = 0;
    void beginGroupDrag(const std::vector<int>& s, float sx, float sy);
    void updateGroupDrag(float sx, float sy);
    void endGroupDrag();
    void uiGroups();   // the outliner of the groups (BrickUi.cpp)
    void groupTest();  // --group-test file
    std::string prefsJson();
    void applyPrefs(const std::string& text);
    bool readDefaultPrefs(std::string& text);
    void saveDefaultPrefs();     // Réglages › Enregistrer comme réglages par défaut
    void resetToDefaultPrefs();  // Réglages › Rétablir
    void openOfficialHouse();
    // LDraw models and the reading of their instructions (BrickLDraw.cpp, BrickRead.cpp)
    bool importLDraw(const std::wstring& path);
    bool hasSteps() const;
    void startReading(int step);
    void stopReading();
    void readStepTo(int step);
    void frameReadStep();
    bool readHidden(int i) const;
    std::vector<int> readStepBricks() const;
    void readInput();
    void uiReader();
    bool m_reading = false;
    int m_readStep = 1, m_readSteps = 0;
    std::vector<std::string> m_readTitles;   // per step: the submodel built ("" = the model)
    std::string m_modelName, m_importReport;
    std::vector<int> m_groupsOpen;   // outliner: the groups unfolded
    int m_renameGroup = -1;
    char m_renameBuf[64] = {};
    std::vector<int> selection() const;
    void deleteSelected();
    void duplicateSelected();
    void rotateSelected(int dir);
    void applyColor(int color);
    void pushUndo();
    void undo();
    void redo();
    void restore(const std::vector<Brick>& snap);
    bool saveScene(const std::wstring& path);
    bool openScene(const std::wstring& path);
    void saveDialog();
    void openDialog();
    void noteRecent(int part);

    // BrickUi.cpp: the interface of the web builder v26, element by element (CSS colours, sizes, icons, texts)
    void drawUi();
    void uiHeader();
    void uiRadial();
    void uiColors();
    void uiTray();
    void uiMechPanel();
    void uiHud();
    void uiInfoSheet();
    void uiPopups();
    void drawToast();
    void buildNav();                        // library families / categories counts
    std::vector<int> visibleParts() const;  // parts of the current family / category / search
    const std::string& partCat(int part) const;    // LDraw category of a part
    void tiltSelected(int dir);             // radial: tilt towards the viewer (up / down)
    void turnSelectedFree(int dir);         // touch HUD: 15 degrees about the vertical axis through the centre
    void uiSelHud();                        // touch HUD around the selection: turn 15 degrees, duplicate, colour arc
    void drawVoxels();                      // voxel display (studs yellow, pins cyan, axles magenta)

    // Mechanical mode (BrickMech.cpp)
    void mechEnter();
    void mechExit();
    void mechUpdate(float dt);
    void mechApplyPose();     // bricks drawn at the pose of their body
    void drawMechOverlay();
    void buildMechDemo();

    // BrickPrefs.cpp: what the web keeps in localStorage, in files next to the exe; scene of the first launch; .ldr
    void loadPrefs();
    void savePrefs();
    void autosave();                 // vp3d-builder-scene: the scene after every change
    bool restoreAutosave();
    void sampleScene();
    std::string sceneLdr() const;
    void uiSceneSheet();
    void pickUpSelected();           // Space: the selected brick comes into the hand
    void cycleGrab();                // C: next pivot voxel of the selection / brick in hand

    // BrickAnim.cpp: animations of the web (brick in hand, drop, settle), placement preview (ghost, visor, drop
    // line, ring, glow of the aimed voxel), selection rectangle
    void updateAnim(float dt);
    void updateFocus(float dt);             // focus distance of the depth of field (Réglages › Mise au point)
    void updateGridFade();                  // radial fade of the ground grid around the model
    void finishDrop();
    void drawHoldOverlay();
    void placeAnimated(int brick, const float3& pos, const float4& rot, float squash, float tiltX, float tiltZ);
    struct Settle { int brick; float s, v, t; };
    std::vector<Settle> m_settles;
    int m_ghostObject = -1;            // translucent copy of the brick in hand at its landing spot
    bool m_marquee = false;
    ImVec2 m_marquee0, m_marquee1;

    // BrickGizmo.cpp: move / rotate manipulators of the web (arrows by studs / plates, rings by 90 degrees, centre
    // sphere = free move in hand)
    struct GizmoHandle { int kind = -1; int axis = 0; };   // kind 0 move, 1 rotate, 2 free
    struct GizmoDrag {
        GizmoHandle h;
        int brick = -1, steps = 0;
        float3 startPos, pivot, v0;
        float4 startRot{0, 0, 0, 1};
        float t0 = 0;
        bool hasV0 = false;
        std::vector<int> others;
        std::vector<Brick> othersStart;
    };
    bool gizmoVisible() const;
    float3 gizmoPivot(int b) const;
    float gizmoScale(const float3& piv) const;
    GizmoHandle pickGizmo(float sx, float sy) const;
    void gizmoStart(const GizmoHandle& h, float sx, float sy);
    void gizmoDragTo(float sx, float sy);
    void drawGizmo();
    // pivot edit (P): the voxels of the focus brick as markers, the cursor aims the nearest one
    int focusBrick() const { return m_holding ? m_held.brick : m_selected; }
    void aimPivot(float sx, float sy);
    void drawPivots();
    GizmoHandle m_gizmoHover;
    bool m_gizmoDragging = false, m_leftOrbit = false;
    GizmoDrag m_gz;

    // BrickDev.cpp / BrickUi.cpp: DEV mode of the web (connectors by type on every brick, LDraw outline over a
    // replacement, export for Maya, re-import with review, replacements kept in dev_overrides next to the exe)
    struct DevOverride { std::string id, label, baseRelease, importedAt, fileName; int triangles = 0, ldrawTriangles = 0; MeshData geometry; };
    struct DevReview {
        bool open = false, swapped = false, wasUse = true;
        int part = -1;
        MeshData g, prev;
        std::string fileName, note;
        float scale = 1;
        float3 offset, size, ref;
    };
    std::map<std::string, DevOverride> m_devOverrides;
    bool m_devShowVox = true, m_devShowEdges = true, m_devUseOverrides = true;
    DevReview m_devReview;
    std::map<int, std::vector<std::pair<float3, float3>>> m_devEdgeCache;
    std::string devId(int part) const;
    int devPartById(const std::string& id) const;
    std::wstring devDir() const;
    std::string devObjText(int part, const MeshData& g, const float3& origin, bool markers) const;
    std::string devReadme(int part) const;
    void devExport(int part);
    bool devParseObj(const std::string& text, MeshData& g, std::map<std::string, std::string>& header) const;
    void devFit(int part, MeshData& g, DevReview& r);
    void devSetGeometry(int part, const MeshData& g);
    void devApplyAll();
    void devLoadAll();
    bool devSave(const DevOverride& o, const std::string& obj);
    void devDelete(const std::string& id);
    void devImport(int forcedPart);
    void devReviewCancel();
    void devReviewRecenter();
    void devReviewKeep();
    void devRevert(int part);
    void devSetOn(bool on);
    const std::vector<std::pair<float3, float3>>& devEdges(int part);
    void drawDevOverlay();
    void uiDevPanel();
    void uiDevSheet();
    void uiDevReview();

    // BrickMaya.cpp: scene export to Maya (web: maya_export.js + maya_template.py)
    struct MayaStats { int bricks = 0, parts = 0, bodies = 0, joints = 0, cmds = 0; std::vector<std::string> fails; double coupling = 0; };
    bool mayaExportScript(std::string& py, MayaStats& st, std::string& err);
    void mayaExportScene(const std::wstring& forcedPath = std::wstring());

    // BrickManual.cpp / BrickUi.cpp: instruction manual of the web (steps of the scene read vertically, large book,
    // imported PDF read vertically, resizable window)
    void manualDirty();
    void renderSteps();
    ImTextureID stepImage(int n);
    float manualPage(Ui& u, ImVec2 a, float w, int n, int corner, bool big, bool draw);
    void pdfView(Ui& u, ImVec2 a, ImVec2 b, float& scroll, bool main);
    void uiManual();
    void uiManualBig();
    void pdfOpen(const std::wstring& path, const std::string& name, bool save);
    void pdfPump();
    void pdfDialog();
    std::map<int, ImTextureID> m_stepTex;
    std::vector<int> m_stepWanted;
    std::map<int, int> m_hiMaterial;     // material -> its copy highlighted (current step)
    int m_manRight = 2;
    float m_manW = 0, m_manH = 0, m_manScroll = 0, m_bigScroll = 0;
    bool m_manBig = false, m_pdfBig = false, m_manResizing = false;
    ImVec2 m_manResize0;
    float m_manW0 = 0, m_manH0 = 0;
    std::unique_ptr<PdfReader> m_pdf;
    bool m_pdfOn = false, m_pdfOpening = false, m_pdfScrollSmooth = false, m_prefsDirty = false;
    std::string m_pdfName, m_pdfMsg;
    int m_pdfPage = 1, m_pdfScrollTo = 0, m_bigScrollTo = 0;
    float m_pdfScroll = 0, m_pdfScrollGoal = -1;
    double m_pdfLock = 0;
    std::vector<std::pair<float, float>> m_pdfSizes;
    struct PdfTex { ImTextureID tex = 0; int w = 0, h = 0; bool asked = false; };
    std::vector<PdfTex> m_pdfTex;

    // BrickMouseTest.cpp: scripted mouse tests
public:
    struct MouseStep { int frame; float x, y; int btn; };   // btn -1 move, 0 press, 1 release
private:
    void mouseTestInput();
    void mouseTestFrame();
    void runMechTests();   // BrickMechTests.cpp (--mechtests)
    std::vector<MouseStep> m_mouseScript;
    Brick m_mtBefore;
    std::vector<double> m_mtQ;
    int m_mtBrick = -1;
    std::string m_mtDiag;
    float m_mtScroll0 = 0, m_mtScrollRel = 0;

    // Thumbs.cpp
    void buildThumbs();
    int buildFullThumbs();       // --build-full-thumbs pack: the thumbnails of the full library
    void loadFullThumbIndex();
    bool lazyThumb(int part, ImTextureID& tex);
    std::vector<uint8_t> m_fullThumbData;
    std::vector<std::pair<uint32_t, uint32_t>> m_fullThumbAt;   // per part: offset, size in m_fullThumbData
    std::unordered_map<int, std::pair<ImTextureID, int>> m_lazyThumbs;   // part -> texture, frame last used
    int m_lazyThumbBudget = 0;
    static bool decodeImage(const uint8_t* data, size_t size, std::vector<uint8_t>& rgba, int& w, int& h);
    int setupStudio(Scene& ts);
    int setupStudio2(Scene& ts, int& glassMat);   // thumbnails v2: + a soft fill light, a glass material
    bool renderThumb2(Scene& ts, int obj, const Part& p, const ThumbView& v, int px, std::vector<uint8_t>& rgba);
    static ThumbView defaultThumbView();
    static RenderSettings thumbSettings();
    static void fitThumbCamera(Camera& c, const Part& p, const ThumbView& v);
    // Edit thumbnail (DEV, BrickThumbEdit.cpp): right click menu, viewer, overrides in %LOCALAPPDATA%\\BrickIt\\thumb_overrides
    std::wstring thumbOverrideDir() const;
    ImTextureID overrideTexture(const std::vector<uint8_t>& rgba, int w, int h);
    void loadThumbOverrides();
    bool readThumbView(int part, ThumbView& v);
    void openThumbEditor(int part);
    void renderThumbEditor();
    void saveThumbOverride();
    void uiCtxMenu();
    void uiThumbEditor();
    std::map<int, ImTextureID> m_thumbOverride;   // part -> its saved thumbnail
    int m_ctxPart = -1;                          // right click menu (DEV) on this part
    ImVec2 m_ctxPos;
    int m_teditPart = -1, m_teMat = -1, m_teGlass = -1, m_teObj = -1;   // thumbnail editor
    ThumbView m_teView;
    bool m_teSave = false, m_teBuilt = false, m_teDrag = false;
    Scene m_te;
    ImVec4 m_teRect{0, 0, 0, 0};
    int thumbSample();           // --thumb-sample dir parts
    // 3D viewer of the part sheet (web: partViewer): the part alone in the thumbnail studio, turned by dragging. While the
    // sheet is open the main view is frozen (its last image) and the renderer draws the viewer instead.
    void renderPreview();
    Scene m_pv;
    bool m_pvBuilt = false;
    int m_pvObj = -1, m_pvPart = -1, m_pvMat = -1;
    float m_pvYaw = 0, m_pvPitch = 0;
    bool m_pvDrag = false;
    ImVec4 m_pvRect{0, 0, 0, 0};
    ImTextureID m_frozenTex = 0;
    bool thumbUv(int part, ImTextureID& tex, ImVec2& uv0, ImVec2& uv1);

    HWND m_hwnd = nullptr;
    Options m_opt;
    std::wstring m_exeDir;
    float m_dpi = 1.0f;
    bool m_running = true, m_minimized = false;
    Renderer m_renderer;
    app::ImGuiLayer m_gui;
    RenderSettings m_rs;
    Scene m_scene;
    Library m_lib;
    Builder m_build{m_lib};
    std::vector<Brick>& m_bricks = m_build.bricks;
    std::vector<int> m_partMesh;       // part -> scene mesh (-1 = not uploaded)
    std::vector<int> m_colorMaterial;  // palette index -> scene material
    std::vector<int> m_dimMaterial;    // palette index -> same material at 50 % (focus mode)
    std::vector<int> m_tintMaterial;   // Mechanical mode: one material per body (Couleurs par corps)
    int m_blockedMaterial = -1;        // brick in hand that cannot be dropped (red ghost)
    int m_ghostMaterial = -1, m_ghostBlockedMaterial = -1;
    int m_rimLight = -1;
    bool m_startMaximized = false;
    bool m_cornell = false;
    int m_adaptiveSpp = 1;             // path tracing samples per frame (adaptive, see frame)            // --cornell: the WZX Engine Cornell box instead of bricks
    // native settings (gear button): UI scale over the screen scale, ambient occlusion of the HQ mode, automatic save
    float m_uiScaleUser = 1.0f;
    bool m_ssaoOn = true, m_autosaveOn = true;
    void uiSettings();
    // drag scrolling of the library rows (cards, families, categories): direction of the first pixels, inertia
    struct Swipe { int id = -1; ImVec2 start, last; int dir = 0; double t = 0; };
    Swipe m_swipe;
    int m_swipeDragged = -1;           // row whose drag just ended (its release is not a click)
    float m_cardsVel = 0, m_famVel = 0, m_catVel = 0;
    bool swipeRow(int id, ImVec2 a, ImVec2 b, float& scroll, float& vel, float maxScroll, bool live);
    bool swipeTaken(int id) const { return m_swipe.id == id && m_swipe.dir == 1; }   // the default size does not fit the screen               // cool rim light of the web (raster)   // placement preview (ghostMat of the web)
    int m_firstBrickObject = 0;        // scene objects before it: the stage
    ViewRect m_vp;
    int m_frame = 0;
    double m_cpuMs = 0, m_frameMs = 0;
    LARGE_INTEGER m_qpf{}, m_last{};
    std::string m_status;
    double m_lastMove = -10.0;   // time of the last camera or scene change (dynamic resolution)
    std::vector<Scene::MeshLod> m_lodsOff;   // the levels of detail while they are turned off (L)

    // editing
    Held m_held;
    bool m_holding = false;
    int m_selected = -1;
    std::vector<int> m_multi;
    int m_color = 0;                   // current colour (palette index)
    std::vector<std::vector<Brick>> m_undo, m_redo;
    bool m_focusDim = false;
    bool m_pressed = false, m_dragStarted = false;
    int m_pressBrick = -1;
    float3 m_pressPoint;
    ImVec2 m_pressPos, m_rmbPos;
    bool m_rmbMoved = false;
    std::wstring m_scenePath;
    std::vector<Box> m_pickBoxes;      // world boxes of the bricks (picking), for the scene revision m_pickVersion
    uint32_t m_pickVersion = 0;

    // UI (web builder state)
    struct NavItem { std::string label; int kind; std::string key; int fam = -1; };   // kept for --cat
    std::vector<std::string> m_families;
    std::vector<NavItem> m_nav;
    int m_navSel = 2;
    std::string m_fam = "Basique";     // "recent", "fav", "all" or an LDraw family
    std::string m_cat, m_sub;          // LDraw category / Technic sub-family ("" = Tout)
    std::string m_search;
    bool m_searchOpen = false;
    std::vector<int> m_recent, m_fav;
    int m_thumbSize = 0;               // 0 small, 1 medium, 2 large (64 / 84 / 112 px as the web; the web starts at 64)
    float m_ui = 1.0f;                 // UI scale (dpi or --ui-scale)
    std::string m_toast;
    double m_toastTime = -10.0;
    bool m_overUi = false;             // the mouse is over a panel this frame
    int m_mode = 1;                    // 0 select, 1 visor, 2 move, 3 rotate
    bool m_autoPivot = false, m_animOn = true, m_showVox = false, m_dev = false;
    int m_quality = 1;                 // 0 Rapide, 1 HQ
    // Radiance Cascades (native addition, Réglages): the Split Radiance Cascades of WZX Engine (real time GI, ray
    // traced, no history: sparse world probes around the visible surfaces), with the path tracing look
    bool m_rcMode = false;
    // HQ + GI douce (Tiny Glade like): the HQ view reads a world probe volume (DDGI, ray traced, long hysteresis,
    // bounded to the bricks): soft bounces and occlusion, stable, no ray per pixel for the GI
    bool m_softGI = true;
    float m_softGIStrength = 1.0f;
    int m_softGIProbes = 16384;       // Réglages › Lumière › Sondes GI: probe budget of the soft GI
    bool m_showProbes = false;        // Réglages › Lumière › Afficher les sondes
    size_t m_countsFor = 0;   // the library tray: parts counted for this many parts
    std::unordered_map<std::string, int> m_catCount, m_techSubCount;
    int m_availCount = 0;
    bool m_allColors = false;         // Couleurs: every colour offered, even those that do not exist for the part
    bool m_touchHud = true;           // Réglages › HUD tactile (iPad): the radial ring and the HUD around the selection
    bool m_arcOpen = false, m_arcDrag = false;   // colour arc of the touch HUD open; opened by a press still held (marking menu)
    static constexpr int HUD_ICONS = 7;          // turn left / right, duplicate, move, more, colour drop, hexagon (assets/brickit/hud)
    ImTextureID m_hudTex[HUD_ICONS] = {};
    void loadHudIcons();
    int m_hudKey = -1;                           // the selection the HUD eased in for
    double m_hudT0 = 0, m_arcT0 = 0, m_arcPickT = 0, m_moreT0 = 0;
    ImVec2 m_hudA, m_hudB;                       // the frame, following the selection smoothly
    float m_hudHover[8] = {}, m_arcHover[64] = {};
    int m_arcPick = -1;                          // the swatch picked (popping, the arc folding back)
    bool m_arcApplied = false, m_moreOpen = false;
    int m_arcPage = 0;
    ImVec2 m_arcPress;
    bool m_stillAA = false;           // Réglages › Anti-aliasing au repos (HQ: jittered frames accumulated while still)
    float m_dof = 0.0f, m_bloom = 0.0f;   // Réglages › Lumière › Profondeur de champ / Bloom (0 = off)
    int m_focusMode = 0;              // Réglages › Lumière › Mise au point: 0 auto (the selection / held brick), 1 centre of the screen, 2 manual
    float m_focusManual = 40.0f;      // manual focus distance (cm)
    float m_focusCur = -1.0f, m_focusAim = -1.0f;   // focus distance shown (eased towards the aim)
    float m_renderScale = 1.0f;       // Réglages › Échelle de rendu (1, 0.85, 0.75, 0.67)
    float m_selOutline = 0.22f, m_selFill = 0.15f;   // Réglages › Lumière: Contour / Voile de sélection
    float m_lumColumnH = 0.0f;        // Réglages: height of the Lumière column drawn last frame
    float m_hqSSSStrength = 1.0f;     // Réglages › Lumière › SSS (HQ): strength 0..2 (0 = off)
    bool m_softGIHold = true;   // Réglages › Lumière › Figer la GI pendant le déplacement: the probes held while a brick is carried
    bool m_giLookdev = false;                 // (not saved: a test view, off at the next launch)
    std::vector<Material> m_colorMaterialOrig;   // the palette materials as made, restored when the lookdev ends
    uint64_t m_giBoundsVersionSoft = ~0ull;
    float3 m_softLo{0, 0, 0}, m_softHi{0, 0, 0};
    float m_softMargin = 4.0f;
    uint64_t m_giBoundsVersion = ~0ull;   // scene version of the GI bounds (Radiance Cascades)
    void setRC(bool on);
    bool userSession() const;   // not a test / demo / capture run: the preferences and the autosave may be written
    bool pathTraced() const { return !m_rs.raster && !m_rcMode; }
    struct PanelState { bool visible = true, collapsed = false, floating = false; float x = 0, y = 0; };
    // grip of the library / brick actions: click folds, click-drag undocks into a floating window, dropped on the
    // bottom edge (library) or a side edge (actions) it docks again (web: dockable panels)
    struct GripDrag { int id = -1; ImVec2 start; bool moved = false; float ox = 0, oy = 0; };
    GripDrag m_grip;
    bool m_optsRight = false;
    bool gripPanel(int id, PanelState& P, ImVec2 ga, ImVec2 gb, ImVec2 panelTopLeft);
    void drawDropzone();
    PanelState m_pTray, m_pOpts, m_pColors, m_pHud{false, false}, m_pManual{false, false}, m_pGroups{false, false};
    int m_popup = 0;                   // 0 none, 1 quality, 2 tonemap, 3 windows, 4 scene
    ImVec2 m_popupAt;
    int m_libPress = -1;               // library card pressed (drag / long press)
    ImVec2 m_libPressPos;
    double m_libPressTime = 0;
    bool m_libLong = false;
    int m_infoPart = -1;               // part sheet (long press on a card)
    float m_cardsScroll = 0, m_famScroll = 0, m_catScroll = 0;
    ImVec4 m_stage{0, 0, 1, 1};        // the 3D view: between the top bar and the tray (as the web's main.stage)
    std::vector<ImVec4> m_uiRects;     // panels of this frame (x0, y0, x1, y1): the mouse over one = UI
    bool m_mechJoints = true, m_mechTint = false;
    bool m_sheetOpen = false;          // « Scène partagée » sheet
    std::string m_sheetText;
    bool m_pivotEdit = false;          // P
    int m_voxStep = 0;                 // V: voxels hidden / shown

    // Mechanical mode
    Mech m_mech{m_lib};
    MechModel m_mm;
    bool m_mechOn = false;
    std::string m_mechFlash;
    Mech::Drag m_mechDrag;
    float3 m_mechDragPlaneN;
    bool m_mechDragging = false;
    bool m_testCarry = false;   // --gi-carry: a brick carried by the test
    double m_mechFlashTime = -10;

    // thumbnails
    std::vector<ImTextureID> m_thumbAtlas;
    std::vector<int> m_thumbSlot;      // part -> slot (-1 none)
    int m_thumbPx = 128, m_thumbCols = 21;   // the web images are 240 x 192: 128 px cells stay sharp at 4K
};

} // namespace brickit
