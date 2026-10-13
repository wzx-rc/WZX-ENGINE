#include "Placement.h"
#include <algorithm>
#include <cmath>

namespace game {

namespace {

// Behaviour of the pieces (same values as the Unity "Clickable" of the RTS starter).
const float kLiftHeight = 0.35f, kBobAmplitude = 0.04f, kBobSpeed = 3.0f;
const float kFollowSpeed = 18.0f, kGravity = 35.0f, kRestitution = 0.45f;
const float kTiltAmount = 2.5f, kMaxTilt = 14.0f, kHoverScale = 1.04f;
const float kStep = 1.0f / 120.0f;          // fixed simulation step (springs stay stable on slow frames)
const float kPi = 3.14159265f;
const float kSpawnHeight = 5.0f;            // a building added from the HUD falls from this height

// Damped spring towards `target` (x and v are updated).
float springStep(float& x, float& v, float target, float k, float damping, float h) {
    float c = 2.0f * std::sqrt(k) * damping;
    v += (k * (target - x) - c * v) * h;
    x += v * h;
    return x;
}

}

void Placement::init(int cellsX, int cellsZ, float cellSize) {
    m_grid.init(cellsX, cellsZ, cellSize);
    m_types.clear();
    BuildingType small;
    small.name = "Small House";
    small.w = 1;
    small.d = 1;
    small.height = 1.2f;
    small.color = float3(0.80f, 0.60f, 0.42f);
    small.modelScale = 0.01645f;   // 0.81 m wide: one cell (as the RTS starter)
    BuildingType large;
    large.name = "Large House";
    large.w = 3;
    large.d = 3;
    large.height = 2.4f;
    large.color = float3(0.62f, 0.52f, 0.44f);
    large.modelScale = 0.051f;     // 2.52 m wide: three cells (as the RTS starter)
    m_types = {small, large};
    clear();
}

void Placement::clear() {
    m_b.clear();
    m_grid.init(m_grid.cellsX(), m_grid.cellsZ(), m_grid.cellSize());
    m_next = 1;
    m_drag = -1;
    m_dragNew = false;
    m_dragValid = false;
}

int Placement::add(int type, Cell cell, float dropHeight) {
    if (type < 0 || type >= (int)m_types.size()) return -1;
    const BuildingType& t = m_types[type];
    Cell c;
    if (!m_grid.findFreeNear(cell, t.w, t.d, c)) return -1;
    Building b;
    b.id = m_next++;
    b.type = type;
    b.cell = c;
    b.rot = 0;
    b.placed = true;
    b.goal = m_grid.center(c, t.w, t.d);
    b.pos = b.goal;
    b.pos.y = dropHeight;
    b.bobPhase = (float)(b.id * 1.7);
    m_grid.place(c, t.w, t.d, b.id);
    m_b.push_back(b);
    return (int)m_b.size() - 1;
}

void Placement::remove(int idx) {
    if (idx < 0 || idx >= (int)m_b.size()) return;
    if (m_b[idx].placed) m_grid.remove(m_b[idx].id);
    m_b.erase(m_b.begin() + idx);
    if (m_drag == idx) {
        m_drag = -1;
        m_dragNew = false;
        m_dragValid = false;
    } else if (m_drag > idx) {
        --m_drag;
    }
}

bool Placement::rotate(int idx) {
    if (idx < 0 || idx >= (int)m_b.size()) return false;
    Building& b = m_b[idx];
    const BuildingType& t = m_types[b.type];
    int newRot = (b.rot + 1) & 3;
    if (!b.placed) {                        // in hand: the drag update re-snaps the footprint
        b.rot = newRot;
        b.yawGoal += kPi * 0.5f;
        b.squashV += 1.2f;
        return true;
    }
    int w = (newRot & 1) ? t.d : t.w, d = (newRot & 1) ? t.w : t.d;
    Cell c = m_grid.snapOrigin(m_grid.center(b.cell, b.footW(t), b.footD(t)), w, d);
    if (!m_grid.canPlace(c, w, d, b.id)) {
        b.tiltVZ += 3.0f;                   // no room: the building shakes
        return false;
    }
    m_grid.remove(b.id);
    b.rot = newRot;
    b.cell = c;
    m_grid.place(c, w, d, b.id);
    b.goal = m_grid.center(c, w, d);
    b.yawGoal += kPi * 0.5f;
    b.squashV += 1.2f;
    return true;
}

void Placement::beginDrag(int idx, const float3& grab, bool isNew) {
    if (idx < 0 || idx >= (int)m_b.size()) return;
    Building& b = m_b[idx];
    m_dragFromCell = b.cell;
    m_dragFromRot = b.rot;
    if (b.placed) m_grid.remove(b.id);
    b.placed = false;
    b.lifted = true;
    b.squashV -= 2.0f;                      // stretched "pop" when taken
    m_grabOffset = isNew ? float3(0, 0, 0) : float3(b.pos.x - grab.x, 0, b.pos.z - grab.z);
    m_drag = idx;
    m_dragNew = isNew;
    m_dragCell = b.cell;
    m_dragValid = false;
}

int Placement::beginNewDrag(int type, const float3& ground) {
    if (type < 0 || type >= (int)m_types.size()) return -1;
    const BuildingType& t = m_types[type];
    Building b;
    b.id = m_next++;
    b.type = type;
    b.rot = 0;
    b.placed = false;
    b.cell = m_grid.snapOrigin(ground, t.w, t.d);
    b.goal = m_grid.center(b.cell, t.w, t.d);
    b.pos = b.goal;
    b.pos.y = 0.0f;
    b.bobPhase = (float)(b.id * 1.7);
    m_b.push_back(b);
    int idx = (int)m_b.size() - 1;
    beginDrag(idx, ground, true);
    return idx;
}

void Placement::updateDrag(const float3& ground, bool allowed) {
    if (m_drag < 0) return;
    Building& b = m_b[m_drag];
    const BuildingType& t = m_types[b.type];
    float3 p = ground + m_grabOffset;
    int w = b.footW(t), d = b.footD(t);
    m_dragCell = m_grid.snapOrigin(p, w, d);
    m_dragValid = allowed && m_grid.canPlace(m_dragCell, w, d);
    b.goal = m_grid.center(m_dragCell, w, d);
}

DropResult Placement::endDrag(bool cancelled) {
    if (m_drag < 0) return DropResult::Cancelled;
    Building& b = m_b[m_drag];
    const BuildingType& t = m_types[b.type];
    bool wasNew = m_dragNew, valid = m_dragValid;
    m_drag = -1;
    m_dragNew = false;
    m_dragValid = false;
    b.lifted = false;
    if (valid && !cancelled) {
        b.cell = m_dragCell;
        b.goal = m_grid.center(b.cell, b.footW(t), b.footD(t));
        m_grid.place(b.cell, b.footW(t), b.footD(t), b.id);
        b.placed = true;
        return DropResult::Placed;
    }
    if (wasNew) return DropResult::Cancelled;   // a new building that is not placed is removed by the host
    // back to where it was
    b.rot = m_dragFromRot;
    b.yawGoal = b.rot * kPi * 0.5f;
    b.cell = m_dragFromCell;
    b.goal = m_grid.center(b.cell, b.footW(t), b.footD(t));
    m_grid.place(b.cell, b.footW(t), b.footD(t), b.id);
    b.placed = true;
    return DropResult::Returned;
}

void Placement::simulate(float dt, int hovered) {
    if (dt <= 0) return;
    int steps = std::min(24, std::max(1, (int)std::ceil(dt / kStep)));
    float h = dt / steps;
    for (int s = 0; s < steps; ++s)
        for (int i = 0; i < (int)m_b.size(); ++i) {
            Building& o = m_b[i];
            // horizontal: exponential follow of the goal
            float a = 1.0f - std::exp(-kFollowSpeed * h);
            float vx = (o.goal.x - o.pos.x) * a / h, vz = (o.goal.z - o.pos.z) * a / h;
            o.pos.x += vx * h;
            o.pos.z += vz * h;
            // vertical: damped spring when lifted (with a small bob), else gravity and bounces
            if (o.lifted) {
                o.bobPhase += kBobSpeed * h;
                springStep(o.pos.y, o.vy, kLiftHeight + kBobAmplitude * std::sin(o.bobPhase), 220.0f, 0.55f, h);
            } else {
                o.vy -= kGravity * h;
                o.pos.y += o.vy * h;
                if (o.pos.y <= 0) {
                    o.pos.y = 0;
                    if (o.vy < -1.2f) {
                        float impact = -o.vy;
                        o.vy = impact * kRestitution;
                        o.squashV += impact * 0.9f;   // squash on landing
                    } else {
                        o.vy = 0;
                    }
                }
            }
            // lean into the movement
            float maxT = kMaxTilt * kPi / 180.0f;
            float tx = std::clamp(kTiltAmount * kPi / 180.0f * vz, -maxT, maxT);
            float tz = std::clamp(-kTiltAmount * kPi / 180.0f * vx, -maxT, maxT);
            if (o.lifted) tz += 0.03f * std::sin(o.bobPhase * 1.3f);   // light roll in the hand
            springStep(o.tiltX, o.tiltVX, tx, 140.0f, 0.5f, h);
            springStep(o.tiltZ, o.tiltVZ, tz, 140.0f, 0.5f, h);
            springStep(o.squash, o.squashV, 0.0f, 260.0f, 0.3f, h);
            o.squash = std::clamp(o.squash, -0.35f, 0.4f);
            float hs = (i == hovered || i == m_drag) ? kHoverScale : 1.0f;
            o.hover += (hs - o.hover) * (1.0f - std::exp(-12.0f * h));
            o.yaw += (o.yawGoal - o.yaw) * (1.0f - std::exp(-14.0f * h));
        }
}

} // namespace game
