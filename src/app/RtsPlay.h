// RTS play layer shared by the editor's Play (GameHost) and the players (player/src/Game.cpp): the units and
// buildings are live prefab instances (PrefabInstance.h) whose prefab says what they are (PrefabGameData: category,
// footprint, selection radius, height, blocksPath).
//  - input: left click selects (Shift adds), a drag draws a selection rectangle (units), a right click is an order
//    (graph events « Ground / Object Right Clicked »); the former click events of the graph still fire;
//  - navigation: a grid of 0.5 unit cells over the scene, the buildings' footprints blocked; A* paths, smoothed;
//  - placement of a building from a prefab: a ghost on the 1 unit grid, green when its cells are free, red when not;
//  - overlay (ImGui draw list over the 3D view): selection rings on the ground, hover ring, cargo / hit point /
//    production bars above the selected, the selection rectangle, the footprint of the ghost;
//  - HUD (ImGui windows): resource counters, objectives, message box, the card of the selection (production).
// The graph state it shows (variables, counters, production) lives in game::Runtime.
#pragma once
#include "PrefabInstance.h"
#include "game/Runtime.h"
#include <string>
#include <vector>

struct ImDrawList;

namespace app {

struct RtsView { float x = 0, y = 0, w = 1, h = 1, scale = 1; };   // the 3D view on screen, the UI scale

class RtsPlay {
public:
    void begin(Scene* scene, PrefabLibrary* lib, game::Runtime* rt);
    void end();                   // Stop: the ghost removed, nothing selected
    bool active() const { return m_scene != nullptr; }

    // Inside an ImGui frame, before the HUD is drawn: mouse and keys over the view (overUi: the pointer is on the HUD).
    // Returns true when it handled the left click (the host does not fire its own click events then).
    bool input(const RtsView& v, bool overUi);
    void drawOverlay(ImDrawList* dl, const RtsView& v);
    void drawHud(const RtsView& v);
    bool hudHovered() const { return m_hudHovered; }

    // game::World services of the hosts
    const std::vector<int>& selection() const { return m_sel; }
    void setSelection(const std::vector<int>& objs);
    std::string objectPrefab(int obj) const;       // file name of the prefab (MechaWorker)
    std::string objectCategory(int obj) const;     // Unit, Building, Resource, Prop
    std::vector<float3> findPath(float3 from, float3 to);
    float3 doorOf(int building) const;
    bool beginPlacement(const std::string& prefab);
    // tests / API: the ghost put at a ground point (snapped) and placed there if its cells are free; false otherwise
    bool placeAt(float3 p);
    bool placing() const { return m_ghost >= 0; }
    void setYaw(int obj, float degrees);
    void setPartColor(int obj, const std::string& part, float3 rgb);
    void setPartGlow(int obj, const std::string& part, float k);

    // tests: the cells of the navigation grid (true: blocked) and its geometry
    bool blocked(float x, float z);

private:
    const PrefabAsset* assetOf(int obj) const;
    int pickRoot(const RtsView& v, float mx, float my, float3* point) const;
    bool groundAt(const RtsView& v, float mx, float my, float3& p) const;
    bool project(const RtsView& v, const float3& p, float& sx, float& sy) const;
    void buildNav(int ignore = -1);
    bool cellFree(int cx, int cz) const;
    bool footprintFree(int root) const;
    void footprintOf(int root, float3& mn, float3& mx) const;
    void changed();

    Scene* m_scene = nullptr;
    PrefabLibrary* m_lib = nullptr;
    game::Runtime* m_rt = nullptr;
    std::vector<int> m_sel;
    int m_hover = -1;
    bool m_leftDown = false, m_dragging = false;
    float m_downX = 0, m_downY = 0;
    bool m_rightDown = false;
    float m_rdownX = 0, m_rdownY = 0;
    int m_ghost = -1;             // the building being placed
    bool m_ghostOk = false;
    bool m_hudHovered = false;
    std::map<int, float3> m_baseEmission;   // material -> its emission before Set Part Glow
    // navigation grid
    static constexpr float kCell = 0.5f;
    static constexpr int kN = 192;           // cells per side (96 units), centred on the origin
    std::vector<uint8_t> m_nav;
};

} // namespace app
