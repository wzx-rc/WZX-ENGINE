#include "RtsCamera.h"
#include <cmath>

namespace game {

using vp3d::clampf;

void RtsCamera::reset(const float3& target, float yaw, float distance) {
    m_target = m_targetGoal = target;
    m_yaw = m_yawGoal = yaw;
    m_dist = m_distGoal = clampf(distance, minDistance, maxDistance);
    clampGoal();
}

// vp3d::Camera puts the eye at target + (sin yaw, ., cos yaw) * distance: the view looks towards -that.
float3 RtsCamera::forward() const { return float3(-std::sin(m_yaw), 0, -std::cos(m_yaw)); }
float3 RtsCamera::right() const { return float3(std::cos(m_yaw), 0, -std::sin(m_yaw)); }

void RtsCamera::clampGoal() {
    m_targetGoal.x = clampf(m_targetGoal.x, -bound, bound);
    m_targetGoal.z = clampf(m_targetGoal.z, -bound, bound);
    m_targetGoal.y = 0;
    m_distGoal = clampf(m_distGoal, minDistance, maxDistance);
}

void RtsCamera::update(const CameraInput& in, float dt) {
    // Keyboard / edges: speed grows with the zoom distance so the screen speed stays similar.
    float speed = moveSpeed * (in.fast ? fastFactor : 1.0f) * std::max(0.35f, m_distGoal / 14.0f);
    float gy = m_yawGoal;
    float3 f(-std::sin(gy), 0, -std::cos(gy)), r(std::cos(gy), 0, -std::sin(gy));
    m_targetGoal += (r * in.moveX + f * in.moveZ) * (speed * dt);
    m_yawGoal += in.turn * turnSpeed * dt;

    if (in.wheel != 0) {
        float old = m_distGoal;
        m_distGoal = clampf(m_distGoal * std::pow(0.85f, in.wheel), minDistance, maxDistance);
        // Zoom towards the cursor: move the target by the same ratio towards the point under the mouse.
        if (in.hasCursorGround && old > 0) {
            float k = 1.0f - m_distGoal / old;
            float3 c = in.cursorGround;
            c.y = 0;
            m_targetGoal += (c - m_targetGoal) * k;
        }
    }
    clampGoal();

    float a = 1.0f - std::exp(-smoothing * dt);
    m_target = m_target + (m_targetGoal - m_target) * a;
    m_yaw += (m_yawGoal - m_yaw) * a;
    m_dist += (m_distGoal - m_dist) * a;
}

void RtsCamera::dragTo(const float3& grabbed, const float3& underCursor) {
    float3 d = grabbed - underCursor;
    d.y = 0;
    m_target += d;
    m_targetGoal = m_target;
    clampGoal();
    m_target = m_targetGoal;
}

void RtsCamera::apply(vp3d::Camera& cam) const {
    cam.target = m_target;
    cam.yaw = m_yaw;
    cam.pitch = vp3d::radians(pitchDeg);
    cam.distance = m_dist;
    cam.fovY = vp3d::radians(fovDeg);
    cam.dof = false;
}

} // namespace game
