// RTS play layer shared by the editor's Play (GameHost) and the players (player/src/Game.cpp): the units and
// buildings are live prefab instances (PrefabInstance.h) whose prefab says what they are (PrefabGameData: category,
// footprint, selection radius, height, blocksPath).
//  - input: left click selects (Shift adds), a drag draws a selection rectangle (units), a right click is an order
//    (graph events « Ground / Object Right Clicked »); the former click events of the graph still fire;
//  - navigation: a grid of 0.5 unit cells over the scene, the buildings' footprints blocked; A* paths, smoothed;
//  - placement of a building from a prefab: a ghost on the 1 unit grid, green when its cells are free, red when not;
//  - overlay (ImGui draw list over the 3D view): selection rings on the ground, hover ring, cargo / hit point /
//    production bars above the selected, the selection rectangle, the footprint of the ghost;
//  - HUD (ImGui windows): resource counters, objectives, message box, the card of the selection (production), the
//    command bar (the HUD buttons of the graph);
//  - the platform: the device (Desktop, Tablet = iPad, Phone = iPhone) sets the layout of the HUD; the input mode
//    (Mouse or Touch) switches live with the last input used (a Windows touch screen touched, iOS always) and sets
//    the controls. Touch: a tap selects, a tap on the ground or a station with units selected is the order, a double
//    tap selects the units of that type in view, a finger dragged moves the camera, a long press then a drag draws the
//    rectangle (a long press alone deselects), pinch / two fingers: camera (the platform); placement: the ghost at the
//    centre of the view, dragged with the finger, then Valider / Annuler.
// The graph state it shows (variables, counters, production) lives in game::Runtime.
#pragma once
#include "PrefabInstance.h"
#include "game/Runtime.h"
#include <map>
#include <string>
#include <utility>
#include <vector>

struct ImDrawList;

namespace app {

enum RtsDevice { kDesktop = 0, kTablet = 1, kPhone = 2 };
const char* rtsDeviceName(int device);   // "Desktop", "Tablet", "Phone"
// A device of the simulator of the editor, in landscape: its screen and safe area in points (null: Desktop)
struct RtsDeviceSpec { const char* label; float w, h, safeL, safeT, safeR, safeB; };
const RtsDeviceSpec* rtsDeviceSpec(int device);

// The 3D view on screen and the UI scale (pixels per point); touch: the platform is a touch screen only (iOS, the
// device simulator of the editor) — a Windows touch screen is detected from the pointer itself (Dear ImGui mouse
// source); device: RtsDevice; safe*: margins the HUD keeps clear (notch, home indicator), pixels.
struct RtsView {
    float x = 0, y = 0, w = 1, h = 1, scale = 1;
    bool touch = false;
    int device = kDesktop;
    float safeL = 0, safeT = 0, safeR = 0, safeB = 0;
};
using RtsButtons = std::vector<std::pair<std::string, std::string>>;   // HUD buttons of the graph: id, label

class RtsPlay {
public:
    void begin(Scene* scene, PrefabLibrary* lib, game::Runtime* rt);
    void end();                   // Stop: the ghost removed, nothing selected
    bool active() const { return m_scene != nullptr; }

    // Inside an ImGui frame, before the HUD is drawn: mouse and keys over the view (overUi: the pointer is on the HUD).
    // Returns true when it handled the left click (the host does not fire its own click events then).
    bool input(const RtsView& v, bool overUi);
    void drawOverlay(ImDrawList* dl, const RtsView& v);
    // buttons: the HUD buttons of the graph, drawn in the command bar (null: the host draws them itself)
    void drawHud(const RtsView& v, const RtsButtons* buttons = nullptr);
    bool hudHovered() const { return m_hudHovered; }

    // the platform (see above)
    int device() const { return m_device; }
    bool touchMode() const { return m_touch; }
    const char* inputModeName() const { return m_touch ? "Touch" : "Mouse"; }
    const char* layoutName() const;   // the HUD layout in use: "Desktop", "Tablet" or "Phone"
    void forceInputMode(int mode) { m_forceMode = mode; }   // -1: detected, 0: mouse, 1: touch (tests)
    // a finger dragging the camera (touch): where it is now; the host keeps the grabbed ground point under it
    bool touchPan(float& x, float& y) const;
    // touch gestures at screen points (tests, API input.tap / input.touchrect): the same code as the fingers
    void tap(const RtsView& v, float x, float y);
    void touchRect(const RtsView& v, float x0, float y0, float x1, float y1);
    void previewRect(float x0, float y0, float x1, float y1);   // the rectangle shown, nothing selected (captures)
    void cancelGesture() { m_gesture = kNone; m_dragging = false; }   // a second finger: the camera takes it (iOS)
    void forgetTap() { m_lastTapObj = -1; }   // the next tap is not the second of a double tap
    void selectAllUnits();
    void selectSameType(const RtsView& v, int obj);   // the units of its prefab in view (double tap / double click)
    bool confirmPlacement();   // the ghost placed where it is, if green (touch: Valider)
    void cancelPlacement();    // touch: Annuler (mouse: Esc, right click)
    bool ghostOk() const { return m_ghostOk; }

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
    bool moveGhost(float3 p);   // the ghost put there (snapped) without placing it: true when it is green
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
    bool m_ghostHeld = false;   // moved by moveGhost (API): the mouse does not move it until it moves
    bool m_hudHovered = false;
    // platform and touch gestures
    int m_device = kDesktop;
    bool m_touch = false, m_autoTouch = false;
    int m_forceMode = -1;
    enum Gesture { kNone, kPending, kPan, kHold, kRect, kGhost };
    int m_gesture = kNone;
    double m_gestureT = 0;
    float m_fingerX = 0, m_fingerY = 0;
    double m_lastTapT = -10;
    float m_lastTapX = 0, m_lastTapY = 0;
    int m_lastTapObj = -1;
    double m_startT = 0;           // Play started (the controls hint fades after a while)
    bool m_objectivesOpen = true;  // Phone: the objectives fold
    RtsView m_view;                // the last view (the ghost of a touch placement starts at its centre)
    bool m_doubleClick = false;
    float3 m_ghostGrab, m_ghostFrom;   // touch: the ground point grabbed on the ghost, the ghost then
    float3 m_orderAt;                  // the last order (a ring shrinks there)
    double m_orderT = -10;
    void setTouch(bool on);
    bool touchInput(const RtsView& v, bool inView);
    void finishPlacement(int placed);
    void snapGhost(float3 p);   // the ghost there, on the 1 unit grid; green or red
    bool selectedUnits() const;
    void markOrder(float3 p);
    void rectSelect(const RtsView& v, float x0, float y0, float x1, float y1, bool add);
    std::map<int, float3> m_baseEmission;   // material -> its emission before Set Part Glow
    // navigation grid
    static constexpr float kCell = 0.5f;
    static constexpr int kN = 192;           // cells per side (96 units), centred on the origin
    std::vector<uint8_t> m_nav;
};

} // namespace app
