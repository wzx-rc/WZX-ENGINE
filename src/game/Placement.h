// game - RTS placement system: buildings on the grid (footprint, snapping, validity), dragging,
// rotation, and the animation of the pieces (lift and bob in the hand, tilt when they slide, landing
// squash). Independent of the scene: the host turns each building into a scene object.
#pragma once
#include "Grid.h"
#include <string>
#include <vector>

namespace game {

struct BuildingType {
    std::string name = "House";
    int w = 1, d = 1;                       // footprint in cells
    float height = 1.2f;                    // metres
    float3 color{0.80f, 0.60f, 0.42f};      // base colour (0..1, sRGB)
};

struct Building {
    int id = 0;
    int type = 0;
    Cell cell;                              // origin cell (lower-left) of the footprint when placed
    int rot = 0;                            // quarter turns
    bool placed = false;                    // registered in the grid
    int object = -1;                        // scene object (set by the host)
    // simulated state (metres, radians)
    float3 pos;                             // centre of the bottom (y = height above the ground)
    float3 goal;                            // x / z the building slides to
    float vy = 0;
    bool lifted = false;
    float yaw = 0, yawGoal = 0;
    float tiltX = 0, tiltZ = 0, tiltVX = 0, tiltVZ = 0;
    float squash = 0, squashV = 0;
    float hover = 1.0f;
    float bobPhase = 0;
    int footW(const BuildingType& t) const { return (rot & 1) ? t.d : t.w; }
    int footD(const BuildingType& t) const { return (rot & 1) ? t.w : t.d; }
};

enum class DropResult { Placed, Returned, Cancelled };

class Placement {
public:
    void init(int cellsX, int cellsZ, float cellSize);   // also sets the two default house types
    void clear();                                        // forgets every building (Stop)

    std::vector<BuildingType>& types() { return m_types; }
    const std::vector<BuildingType>& types() const { return m_types; }
    std::vector<Building>& buildings() { return m_b; }
    const std::vector<Building>& buildings() const { return m_b; }
    const Grid& grid() const { return m_grid; }

    // A building on the free cell nearest to `cell`, dropped from `dropHeight`. Returns its index or -1.
    int add(int type, Cell cell, float dropHeight);
    // Removes a building; its scene object must already be gone. Indices after it move down by one.
    void remove(int idx);
    // Turns a building a quarter turn (in hand, or on the grid when the new footprint is free).
    bool rotate(int idx);

    // Dragging: `ground` is the point under the cursor, `allowed` is false over the UI.
    void beginDrag(int idx, const float3& ground, bool isNew);
    int beginNewDrag(int type, const float3& ground);     // a new building in hand
    void updateDrag(const float3& ground, bool allowed);
    // Drops the building in hand. A cancelled new building stays in the list (the host removes it).
    DropResult endDrag(bool cancelled);
    int dragging() const { return m_drag; }
    bool draggingNew() const { return m_dragNew; }
    bool dragValid() const { return m_dragValid; }

    // Animation of every building (fixed step). `hovered` is the building under the cursor, or -1.
    void simulate(float dt, int hovered);

private:
    Grid m_grid;
    std::vector<BuildingType> m_types;
    std::vector<Building> m_b;
    int m_next = 1;
    int m_drag = -1;
    bool m_dragNew = false;
    bool m_dragValid = false;
    Cell m_dragCell, m_dragFromCell;
    int m_dragFromRot = 0;
    float3 m_grabOffset;
};

} // namespace game
