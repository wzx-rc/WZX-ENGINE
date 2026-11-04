// WZX Player - the game itself on every platform: the node graph (game::Runtime), the RTS camera, the houses of
// the village (game::Placement), the HUD and the sun dial, as in the Play mode of the editor (src/app/AppGame.cpp,
// AppRts.cpp) but without the editor. Input is read from Dear ImGui (mouse / one finger, keys), plus the
// physical move keys and the touch gestures given by the platform file.
#pragma once
#include "app/Json.h"
#include "game/Placement.h"
#include "game/RtsCamera.h"
#include "game/Runtime.h"
#include "vp3d/Scene.h"

#include <deque>
#include <string>
#include <utility>
#include <vector>

namespace player {

// Camera input the platform reads itself (ImGui has no physical key positions, nor touch gestures).
struct CameraControls {
    float moveX = 0, moveZ = 0, turn = 0;   // physical W/S, D/A, E/Q (any keyboard layout), -1..1
    bool twoFingers = false;                // touch: two fingers on the screen this frame
    float fingersX = 0, fingersY = 0;       // their centre, in pixels
    float pinch = 1.0f;                     // distance between them, relative to the previous frame
    float twist = 0.0f;                     // their turn since the previous frame, radians
};

class Game : public game::World {
public:
    // dataDir: folder holding assets/HOUSE (and game/main.graph.json when graphText is empty). graphText: the graph of the scene
    // (a build's graph.json). touch: no screen edge scrolling, touch buttons.
    bool init(vp3d::Scene* scene, const std::string& dataDir, float uiScale, bool touch, std::string* error,
              const std::string& graphText = std::string());
    // The village of a scene file: each house on its cell and turn (GameHost::loadVillage), call after init.
    void loadVillage(const app::Json& list);
    // Inside an ImGui frame: input, graph, camera, houses, then the HUD.
    void frame(float dt, float viewW, float viewH, const CameraControls& cam);
    const std::deque<std::string>& log() const { return m_rt.log; }   // the graph's messages and errors
    // Margins the HUD keeps clear, in pixels (iPhone notch and home indicator).
    void setSafeArea(float left, float top, float right, float bottom) { m_safeL = left; m_safeT = top; m_safeR = right; m_safeB = bottom; }
    int objectsShown() const;                                          // visible objects of the scene (tests)

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
    int selected() const override { return m_selected; }
    void select(int obj) override;
    void message(const std::string& text) override { toast(text); }
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
    void cameraRay(float px, float py, vp3d::float3& o, vp3d::float3& d) const;
    bool groundAt(float px, float py, vp3d::float3& p) const;
    bool groundUnderMouse(vp3d::float3& p) const;
    bool mouseInView() const;
    void playInput();
    void updateRtsCamera(float dt, const CameraControls& cc);
    void updateSun();
    void updatePlacement(float dt);
    void syncBuildings();
    void finishDrag(bool cancelled);
    void removeBuilding(int idx);
    int houseUnder(const vp3d::float3& o, const vp3d::float3& d, float& tHit) const;
    int houseIndex(int id) const;
    bool hasBuilding(int id) const;
    int invalidMaterial();
    bool ensureHouseModel();
    std::string uniqueName(const std::string& base) const;
    void toast(const std::string& s);
    void drawRtsHud();
    void drawSunDial();
    void drawToasts();
    bool keyPressed(int imguiKey, const char* name) const;   // a key, or its touch button this frame

    vp3d::Scene* m_scene = nullptr;
    std::string m_dataDir;
    float m_s = 1.0f;                 // UI scale (DPI)
    bool m_touch = false;
    float m_viewW = 1, m_viewH = 1;
    float m_safeL = 0, m_safeT = 0, m_safeR = 0, m_safeB = 0;
    float m_time = 0;
    int m_selected = -1;

    game::Graph m_graph;
    game::Runtime m_rt;
    bool m_rtsCam = false;
    bool m_rmbDrag = false, m_twoDrag = false;
    vp3d::float3 m_rmbGrab, m_twoGrab;
    game::RtsCamera m_cam;

    game::Placement m_build;
    std::vector<std::pair<std::string, std::string>> m_hudButtons;   // declared by the graph: id, label
    vp3d::float3 m_houseMin, m_houseMax;
    int m_houseMesh = -1, m_houseTex = -1, m_houseMat = -1;
    bool m_houseFailed = false;
    int m_invalidMat = -1;
    int m_buildHover = -1, m_buildSelected = -1, m_buildPress = -1;
    float m_pressX = 0, m_pressY = 0;
    bool m_buildHud = false;          // the pointer is over the HUD (no game click, no camera)
    std::vector<std::string> m_touchKeys;   // keys pressed with the touch buttons this frame

    float m_sunAzimuth = vp3d::radians(220.0f);
    float m_sunElevation = vp3d::radians(48.0f);
    float m_sunGrabAnim = 0.0f;

    struct Toast { std::string text; float t; };
    std::vector<Toast> m_toasts;
};

} // namespace player
