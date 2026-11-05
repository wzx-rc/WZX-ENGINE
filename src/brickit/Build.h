// BrickIt - building logic shared by the UI: bricks, typed voxel snap of the brick in hand, overlap safety through a
// spatial hash of the body boxes, stacked copies, undo snapshots and the .v3d.json scene format (web compatible).
#pragma once
#include "Library.h"
#include "vp3d/Scene.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace brickit {

using vp3d::float4;

constexpr float kPitch = 0.8f, kPlate = 0.32f;   // cm: stud pitch, plate height

struct Brick {
    int part = -1;            // index in Library::parts
    int color = 0;            // index in Library::palette
    float3 pos;               // origin of the part (its minimum corner when not turned)
    float4 rot{0, 0, 0, 1};   // quaternion, multiples of 90 degrees (any angle when free)
    bool free = false;        // turned by a free angle (15 degree steps of the touch HUD): no stud snapping on it
    int grab = -1;            // pivot voxel (-1 = default)
    std::vector<int> path;    // the groups holding the brick, outermost first (ids in Builder::groups)
    int step = -1;            // step of the instructions of an imported model (reading mode), -1 = none
};

// A group of bricks (BrickGroups.cpp): its members are the bricks whose path holds its id; ids are never reused
struct Group {
    std::string name;
    bool hidden = false;
};

struct Box {
    float3 mn, mx;
    bool overlaps(const Box& o) const {
        return mn.x < o.mx.x && o.mn.x < mx.x && mn.y < o.mx.y && o.mn.y < mx.y && mn.z < o.mx.z && o.mn.z < mx.z;
    }
};

// Uniform grid over boxes: the bricks near a box in O(1) (cell = 2 studs).
class BoxHash {
public:
    void clear() { m_cells.clear(); m_boxes.clear(); m_ids.clear(); }
    void insert(int id, const Box& b);
    // largest fraction of the volume of b inside one of the boxes (0 = free)
    float worstOverlap(const Box& b) const;
    bool any(const Box& b) const { return worstOverlap(b) > 0; }
private:
    static constexpr float kCell = 1.6f;
    std::unordered_map<int64_t, std::vector<int>> m_cells;   // cell -> index in m_boxes
    std::vector<Box> m_boxes;
    std::vector<int> m_ids;
};

// Camera projection helpers (perspective vp3d camera, pixels of a viewport w x h).
struct View {
    vp3d::Camera cam;
    float w = 1, h = 1;
    float ox = 0, oy = 0;   // top-left corner of the view in window pixels (the stage between the top bar and the tray)
    bool project(const float3& p, float& sx, float& sy) const;     // false behind the camera
    void ray(float sx, float sy, float3& o, float3& d) const;
};

// The brick in hand.
struct Held {
    int brick = -1;           // index in the brick list (the brick moves with the hand)
    bool isNew = false;       // spawned from the library (cancel = removed)
    Brick original;           // pose before the hold (cancel)
    float4 rot{0, 0, 0, 1};   // orientation in hand
    float3 grab;              // local pivot voxel
    float3 target;            // snapped position
    float3 snapVoxel;         // scene voxel (or floor point) the pivot goes to
    bool hasTarget = false, blocked = false, suggested = false;
    // cache of the scene without the held brick, built once per hold
    std::vector<float3> vox;
    std::vector<uint8_t> conn;
    BoxHash boxes;
    bool cacheValid = false;
    // animation of the web (updateHold): the brick floats above its target on a spring, tilts with its speed, squashes
    float3 vpos, vvel;
    float4 vrot{0, 0, 0, 1};
    float squash = 1, squashVel = 0, tiltX = 0, tiltZ = 0, time = 0, snapT = 0;
    bool dropping = false, keepCopy = false;
    float dropT = 0;
    float3 dropFrom, lastSnap;
    float4 dropRotFrom{0, 0, 0, 1};
};

class Builder {
public:
    explicit Builder(Library& lib) : m_lib(lib) {}
    std::vector<Brick> bricks;
    std::vector<Group> groups;
    bool gridSnap = true;     // free floor positions fall on the stud lattice
    bool safety = true;       // refuse a position more than 50 % inside another brick

    // geometry helpers
    float3 grabOf(const Brick& b) const;                 // local pivot voxel
    Box bodyBox(int part, const float4& rot, const float3& pos) const;   // studs left out, slightly shrunk
    Box aabbOf(int part, const float4& rot, const float3& pos) const;
    float3 worldVoxel(const Brick& b, const float3& v) const;

    // hold
    void beginHold(Held& H, int brick, bool isNew) const;
    void buildCache(Held& H) const;
    // target of the brick in hand under the cursor (pixels). Returns true when the target changed.
    // hit: point of the brick under the cursor (nullptr over the floor): the free voxels nearest to it win
    void computeTarget(Held& H, const View& view, float sx, float sy, const float3* hit) const;
    void rotateHeld(Held& H, const float3& axis, int dir) const;

    // edits
    int stackedCopy(int src);                 // copy right above (first free place), -1 if none
    void rotatePlaced(int brick, int dir);    // 90 degrees around Y through its pivot
    void rotatePlacedAxis(int brick, const float3& axis, int dir);   // 90 degrees around a world axis through its pivot

    // scene file (vp3d-scene v1, compatible with the web builder)
    std::string toJson(const vp3d::Camera& cam) const;
    bool fromJson(const std::string& text, vp3d::Camera* cam, std::string* message);

private:
    Library& m_lib;
};

// Exact multiples of 90 degrees.
float4 snapQuat(const float4& q);

} // namespace brickit
