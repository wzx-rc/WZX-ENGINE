// Game layer of the editor: node graph, Play mode, builds, player mode and the local
// command API. Lives beside App (friend) so the UI files stay untouched.
#pragma once
#include "Json.h"
#include "game/CommandServer.h"
#include "game/NodeEditor.h"
#include "game/Runtime.h"
#include "vp3d/Scene.h"
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

    // Called by App::frame after UI::newFrame. Returns true when the game layer owns the
    // keyboard this frame (App then skips its shortcuts); viewportInput is skipped when
    // skipViewport is set (graph editor open).
    bool beginFrame(bool& skipViewport);
    void update(float dt);
    void draw();                 // after the editor UI (or alone in player mode)
    void afterDraw();            // frame complete, before present (API screenshots)
    bool playerMode() const { return m_player; }

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

private:
    void play();
    void stop();
    bool saveGraph(std::string* err = nullptr);
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
    void drawToolbar();
    void drawToasts();
    void toast(const std::wstring& s);

    App& m_app;
    game::Graph m_graph;
    game::Runtime m_rt;
    game::NodeEditor m_editor;
    game::CommandServer m_server;
    bool m_showGraph = false;
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
    // floating toolbar
    struct Btn { vp3d::Rect r; int action; };
    std::vector<Btn> m_toolbar;
    int m_pendingToolbar = 0;
    struct Toast { std::wstring text; uint64_t t; };
    std::vector<Toast> m_toasts;
    std::string m_lastBuild;
    std::shared_ptr<game::CommandRequest> m_shotReq;
    bool m_shotWindow = false;
};

} // namespace app
