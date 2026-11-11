// Game layer of the editor: node graph, Play mode, builds, player mode and the local
// command API. Lives beside App (friend) so the UI files stay untouched.
#pragma once
#include "Json.h"
#include "RtsPlay.h"
#include "game/CommandServer.h"
#include "game/NodeEditor.h"
#include "game/Placement.h"
#include "game/RtsCamera.h"
#include "game/Runtime.h"
#include "vp3d/Scene.h"
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace app {

class App;

struct BuildProfile {
    std::string name = "windows-release";
    std::string title = "WZX Game";
    int width = 1600, height = 900;
    bool fullscreen = false;
    bool pathTracing = false;    // allow the P key (path tracing) in the built game
    bool showFps = false;
    bool commandApi = false;     // keep the local command API in the built game
    bool zip = true;             // also pack the game folder into a .zip to send
};

class GameHost : public game::World {
public:
    explicit GameHost(App& app) : m_app(app) {}
    ~GameHost() override;
    void init();

    // Called by App::frame after ImGui::NewFrame. Returns true when the game layer owns the
    // keyboard this frame (App then skips its shortcuts).
    bool beginFrame();
    bool showGraph() const;                        // the Graph tab of the editor is open (node editor)
    void setGraphOpen(bool on);
    bool graphOpen() const { return showGraph(); }   // the graph editor covers the viewport
    void drawGraphWorkspace();
    void update(float dt);
    void draw();                 // after the editor UI (or alone in player mode)
    void afterDraw();            // frame complete, before present (API screenshots)
    bool playerMode() const { return m_player; }
    bool rtsCameraOn() const { return m_rtsCam; }   // the viewport must not orbit the camera meanwhile
    // The village (RTS houses): the editor's village mode, and the "village" section of the scene file
    bool placementMode() const { return m_placeMode; }
    bool playing() const { return m_playing; }
    void requestPlay() { m_pendingToolbar = 1; }   // the Play button of the top bar
    void openBuildWindow();                        // the Build button of the top bar, F7
    void openAiWindow() { m_showAi = true; }       // Tools > AI Control (MCP)
    void addHoverOutline(std::vector<int>& objects) const;   // the house under the cursor is outlined (its halo)
    void togglePlacement();                  // village mode of the editor (not during Play)
    void resetVillage();                     // a new scene: no houses
    // Graph of the scene: a built-in scene (SCENE_STUDIO = Village, the RTS graph of game/main.graph.json), -1 a new
    // scene, -2 a scene file without graph (both empty); loadSceneGraph: the "graph" of a .wzx scene.
    void sceneChanged(int sceneId);
    void loadSceneGraph(const Json& j);
    std::string graphJson() const;
    std::string villageJson() const;
    void loadVillage(const Json& list);

    // game::World
    int objectCount() const override;
    bool objectAlive(int obj) const override;
    std::string objectName(int obj) const override;
    std::string objectPart(int obj) const override;
    vp3d::float3 getPosition(int obj) const override;
    void setPosition(int obj, vp3d::float3 p) override;
    void rotateY(int obj, float degrees) override;
    void setScale(int obj, vp3d::float3 s) override;
    void setColor(int obj, vp3d::float3 rgb) override;
    int spawn(const std::string& part, vp3d::float3 pos, vp3d::float3 rgb) override;
    void destroy(int obj) override;
    int selected() const override;
    void select(int obj) override;
    void message(const std::string& text) override;
    void setRtsCamera(bool on) override;
    int placeHouse(int type, int cx, int cz, int rot) override;
    void hudButton(const std::string& id, const std::string& label) override;
    void setSun(float azimuth, float height) override;
    void cellAt(float x, float z, int& cx, int& cz) const override;
    bool removeHouse(int id) override;
    bool turnHouse(int id) override;
    int houseAt(int cx, int cz) const override;
    int houseCount() const override;
    int spawnPrefab(const std::string& prefab, vp3d::float3 pos, float yawDeg) override;
    bool playClip(int obj, const std::string& clip, bool loop, float speed) override;
    void stopClip(int obj) override;
    void clipFinished(int root, const std::string& clip);   // App::updatePrefabs: a non-looping clip ended (Play)
    // RTS units (app/RtsPlay.h): selection, orders, navigation, placement, overlay and HUD of the Play session
    std::vector<int> selection() const override { return m_rts.selection(); }
    void setSelection(const std::vector<int>& objs) override;
    std::string objectPrefab(int obj) const override { return m_rts.objectPrefab(obj); }
    std::string objectCategory(int obj) const override { return m_rts.objectCategory(obj); }
    void setYaw(int obj, float degrees) override { m_rts.setYaw(obj, degrees); m_app.markDirty(); }
    std::vector<vp3d::float3> findPath(vp3d::float3 from, vp3d::float3 to) override { return m_rts.findPath(from, to); }
    vp3d::float3 doorOf(int building) const override { return m_rts.doorOf(building); }
    bool beginPlacement(const std::string& prefab) override { return m_rts.beginPlacement(prefab); }
    void setPartColor(int obj, const std::string& part, vp3d::float3 rgb) override { m_rts.setPartColor(obj, part, rgb); m_app.markDirty(); }
    void setPartGlow(int obj, const std::string& part, float k) override { m_rts.setPartGlow(obj, part, k); m_app.markDirty(); }
    RtsPlay m_rts;
    game::Runtime& runtime() { return m_rt; }   // (self-test)

private:
    void play();
    void stop();
    bool saveGraph(std::string* err = nullptr);
    void compileGraph();
    void executeGraph();          // Execute of the graph toolbar: dry run in the editor
    bool villageGraphShown() const;   // the graph shown is the Village one (built-in scene, not a file)
    void persistVillageGraph();
    void resetGraphView(const char* what);
    bool m_villageGraph = false;      // the graph was loaded for the built-in Village scene
    bool loadGraph(std::string* err = nullptr);
    void defaultGraph();
    std::vector<BuildProfile> loadProfiles();
    bool saveProfiles(const std::vector<BuildProfile>& profiles);
    bool build(const std::string& profile, bool run, std::wstring& outDir, std::string& err);
    bool buildProfile(const BuildProfile& bp, bool run, std::wstring& outDir, std::string& err);
    bool exportScene(const std::filesystem::path& out, std::string& err, const std::string& skip = std::string());   // scene.wzx and the files it uses
    bool exportIos(const BuildProfile& bp, std::wstring& outDir, std::string& err);   // game\export for the iOS workflow
    void drawBuildWindow();
    // AI control (AiControl.h): the command API on the first free port from 7878, with the token of this installation
    bool startApi();
    void stopApi();
    void drawAiWindow();
    bool m_showAi = false;
    std::string m_aiCopied;   // the AI window: what was copied last ("config", "claude", "token")
    void handleRequests();
    std::string command(const Json& j, int& status);
    std::string extraCommand(const std::string& cmd, const Json& j, int& status, bool& handled);   // materials, curves, render, prefab... (AppAi.cpp)
    std::string sceneJson() const;
    int resolveObject(const Json& t) const;
    std::string uniqueName(const std::string& base) const;
    void playInput();
    void updateRtsCamera(float dt);
    bool groundUnderMouse(vp3d::float3& p) const;
    bool m_rtsCam = false;            // "Caméra RTS" node is active
    int m_rtsPane = -1;               // the pane the RTS camera took (it looks through persp meanwhile), -1: none
    int m_rtsPaneCam = 0;             // what that pane looked through before (ViewCam, VCAM_OBJECT), back on Stop
    uint32_t m_rtsPaneObject = 0;
    bool m_rmbDrag = false;
    vp3d::float3 m_rmbGrab;
    game::RtsCamera m_cam;
    void drawToasts();
    void toast(const std::wstring& s);

    // RTS placement (AppRts.cpp): houses on the grid, dragged in the view or out of the HUD, animated
    // and turned into scene objects. Simulated during Play; the input and the HUD work in Play and in the village mode.
    game::Placement m_build;             // the village being edited (or played)
    game::Placement m_saveBuild;         // the village at the start of Play, restored on Stop
    bool m_placeMode = false;            // village mode of the editor
    std::vector<std::pair<std::string, std::string>> m_hudButtons;   // HUD buttons declared by the graph (Play): id, label
    vp3d::float3 m_houseMin, m_houseMax; // the house model's box (centred, base on y = 0)
    int m_houseMesh = -1, m_houseTex = -1, m_houseMat = -1;   // the house model (assets/HOUSE), see AppRts.cpp
    bool m_houseFailed = false;          // the model could not be loaded (reported once)
    int m_invalidMat = -1;               // red material of an invalid drop (this Play session)
    int m_buildHover = -1, m_buildSelected = -1, m_buildPress = -1;
    ImVec2 m_buildPressPos;
    bool m_buildHud = false;             // the mouse is over the RTS HUD (set by draw)
    bool m_hudDrag = false;              // a house is dragged out of the HUD
    ImVec2 m_hudPress;
    bool viewVisible() const;
    vp3d::float3 viewCentreGround() const;
    int placeNew(int type, const vp3d::float3& at);
    void updatePlacement(float dt);
    void syncBuildings();
    int invalidMaterial();
    void finishDrag(bool cancelled);
    void removeBuilding(int idx);
    void resetTransient();
    bool placeActive() const;
    int houseUnder(const vp3d::float3& o, const vp3d::float3& d, float& tHit) const;
    bool hasBuilding(int id) const;
    int houseIndex(int id) const;
    void drawRtsHud();
    bool ensureHouseModel();
    // Sun (RTS play): azimuth and height from the dial (AppRts.cpp), restored on Stop
    vp3d::SunLight m_saveSun;
    float m_sunAzimuth = vp3d::radians(220.0f);
    float m_sunElevation = vp3d::radians(48.0f);
    float m_sunGrabAnim = 0.0f;         // the sun bounces when the dial is grabbed
    void updateSun();
    void drawSunDial();

    App& m_app;
    game::Graph m_graph;
    game::Runtime m_rt;
    game::NodeEditor m_editor;
    game::CommandServer m_server;
    bool m_graphFramed = false;      // the view was centred on the graph once
    bool m_playing = false;
    bool m_player = false;
    bool m_playerFps = false;
    bool m_playerPathTracing = false;
    std::wstring m_projectDir, m_gameDir, m_graphPath;
    // state restored on Stop
    std::vector<vp3d::Object> m_saveObjects;
    std::vector<vp3d::Material> m_saveMaterials;
    int m_saveSelected = -1;
    int m_saveTool = 0;
    std::vector<bool> m_destroyed;
    int m_pendingToolbar = 0;
    struct Toast { std::wstring text; uint64_t t; };
    std::vector<Toast> m_toasts;
    std::string m_lastBuild;
    // Build window (top bar Build, F7)
    bool m_showBuild = false;
    std::vector<BuildProfile> m_profiles;
    int m_buildPlatform = 0;   // Build window: 0 Windows, 1 iOS
    int m_profileIdx = 0;
    std::string m_buildResult;
    bool m_buildFailed = false;
    std::shared_ptr<game::CommandRequest> m_shotReq;
    bool m_shotWindow = false;
};

} // namespace app
