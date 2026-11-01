// BrickIt - native brick builder (PC, DirectX 12) on the vp3d renderer, with the WZX Engine Dear ImGui theme.
//   BrickApp.cpp  window, loop, stage, scene sync, test scenes
//   BrickEdit.cpp mouse and keys: brick in hand, selection, undo, files
//   BrickUi.cpp   panels: toolbar, library (two rows at the bottom), colours (right), stats
//   Thumbs.cpp    part thumbnails rendered by the engine (atlas, cached on disk)
#pragma once
#include "Build.h"
#include "Mech.h"
#include "Library.h"
#include "app/ImGuiLayer.h"
#include "vp3d/Renderer.h"
#include <windows.h>
#include <string>
#include <vector>

namespace brickit {

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
    int bevelSpp = -1;        // --bevel-spp N (0 = bevel off)
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
    float fingerprints = 0.6f;    // --fingerprints: strength of the roughness mask (0 = off)
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
    std::string partCat(int part) const;    // LDraw category of a part
    void tiltSelected(int dir);             // radial: tilt towards the viewer (up / down)
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
    void finishDrop();
    void drawHoldOverlay();
    void placeAnimated(int brick, const float3& pos, const float4& rot, float squash, float tiltX, float tiltZ);
    struct Settle { int brick; float s, v, t; };
    std::vector<Settle> m_settles;
    int m_ghostObject = -1;            // translucent copy of the brick in hand at its landing spot
    bool m_marquee = false;
    ImVec2 m_marquee0, m_marquee1;

    // Thumbs.cpp
    void buildThumbs();
    bool thumbUv(int part, ImTextureID& tex, ImVec2& uv0, ImVec2& uv1) const;

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
    int m_ghostMaterial = -1, m_ghostBlockedMaterial = -1;   // placement preview (ghostMat of the web)
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
    struct PanelState { bool visible = true, collapsed = false; };
    PanelState m_pTray, m_pOpts, m_pColors, m_pHud{false, false}, m_pManual{false, false};
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
    double m_mechFlashTime = -10;

    // thumbnails
    std::vector<ImTextureID> m_thumbAtlas;
    std::vector<int> m_thumbSlot;      // part -> slot (-1 none)
    int m_thumbPx = 96, m_thumbCols = 21;
};

} // namespace brickit
