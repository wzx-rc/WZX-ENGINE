// Game layer of the editor: node graph, Play mode, builds, player mode and the local
// command API. Lives beside App (friend) so the UI files stay untouched.
#pragma once
#include "Json.h"
#include "game/CommandServer.h"
#include "game/NodeEditor.h"
#include "game/Placement.h"
#include "game/RtsCamera.h"
#include "game/Runtime.h"
#include "vp3d/Scene.h"
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
    void togglePlacement();                  // village mode of the editor (not during Play)
    void resetVillage();                     // a new scene: no houses
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

private:
    void play();
    void stop();
    bool saveGraph(std::string* err = nullptr);
    void compileGraph();
    bool loadGraph(std::string* err = nullptr);
    void defaultGraph();
    std::vector<BuildProfile> loadProfiles();
    bool build(const std::string& profile, bool run, std::wstring& outDir, std::string& err);
    void handleRequests();
    std::string command(const Json& j, int& status);
    std::string sceneJson() const;
    int resolveObject(const Json& t) const;
    std::string uniqueName(const std::string& base) const;
    void playInput();
    void updateRtsCamera(float dt);
    bool groundUnderMouse(vp3d::float3& p) const;
    bool m_rtsCam = false;            // "Caméra RTS" node is active
    bool m_rmbDrag = false;
    vp3d::float3 m_rmbGrab;
    game::RtsCamera m_cam;
    void drawToolbar();
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
    std::shared_ptr<game::CommandRequest> m_shotReq;
    bool m_shotWindow = false;
};

} // namespace app
