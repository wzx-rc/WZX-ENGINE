// RTS camera: a target point on the ground seen from a fixed pitch, moved with the keyboard, the screen
// edges and a right-button drag, turned with Q/E (physical keys) or the middle button, zoomed to the cursor.
// Movements are smoothed; the result is written to a vp3d::Camera (orbit around the target).
#pragma once
#include "vp3d/Scene.h"

namespace game {

using vp3d::float2;
using vp3d::float3;

struct CameraInput {
    float moveX = 0, moveZ = 0;     // -1..1, screen right / screen up (keyboard + edges)
    bool fast = false;              // Shift
    float turn = 0;                 // -1..1 (Q / E)
    float wheel = 0;                // wheel notches (positive = zoom in)
    bool hasCursorGround = false;   // ground point under the cursor (zoom to cursor)
    float3 cursorGround;
};

class RtsCamera {
public:
    // Tuning (same values as the Unity starter)
    float moveSpeed = 8.0f;         // m/s at the default distance
    float fastFactor = 2.5f;
    float turnSpeed = 1.6f;         // rad/s
    float minDistance = 1.5f, maxDistance = 55.0f;
    float fovDeg = 35.0f;           // narrow: little perspective, units keep their size across the view
    float pitchDeg = 55.0f;
    float smoothing = 10.0f;
    float bound = 50.0f;            // target limited to +-bound on X and Z

    void reset(const float3& target, float yaw, float distance);
    void update(const CameraInput& in, float dt);
    // Right-button drag: keeps the grabbed ground point under the cursor (call while the button is held).
    void dragTo(const float3& grabbed, const float3& underCursor);
    // Middle-button rotation in radians.
    void addYaw(float r) { m_yawGoal += r; }

    void apply(vp3d::Camera& cam) const;
    float yaw() const { return m_yaw; }
    float3 target() const { return m_target; }
    float distance() const { return m_dist; }
    // Horizontal directions of the view (unit vectors on the ground).
    float3 forward() const;
    float3 right() const;

private:
    float3 m_target, m_targetGoal;
    float m_yaw = 0, m_yawGoal = 0;
    float m_dist = 14, m_distGoal = 14;
    void clampGoal();
};

} // namespace game
