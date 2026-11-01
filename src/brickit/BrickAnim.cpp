// BrickIt - animations of the web builder (updateHold, settles): the brick in hand floats 0.9 above its target on a
// spring (w = 14), bobs, tilts with its speed, squashes on a spring (w = 22, z = 0.35); the drop lasts 0.15 s; a dropped
// brick settles (squash 0.86, spring w = 30, z = 0.28, 0.6 s). Placement preview: ghost at the landing spot (yellow, red
// when refused), visor squares on the footprint, dashed drop line, ring at the pivot, glow of the aimed voxel.
#include "BrickApp.h"
#include <algorithm>
#include <cmath>

namespace brickit {

// Brick object at an animated pose: squash along its local Y around the top of the brick (the pivot of the web is at
// heightPlates x plate), small tilt around the same point.
void BrickApp::placeAnimated(int i, const float3& pos, const float4& rot, float squash, float tiltX, float tiltZ) {
    Object& o = m_scene.objects[m_firstBrickObject + i];
    const Part& p = m_lib.parts[m_bricks[i].part];
    const float3 pivot(0, p.heightPlates * kPlate, 0);
    o.rot = rot;
    o.useRot = true;
    o.scale = float3(1, squash, 1);
    o.position = pos + qRot(rot, pivot * (1.0f - squash));
    o.tiltX = tiltX;
    o.tiltZ = tiltZ;
    o.tiltPivot = pivot;
}

void BrickApp::finishDrop() {
    if (!m_holding) return;
    const int b = m_held.brick;
    const bool keep = m_held.keepCopy;
    m_holding = false;
    m_held.cacheValid = false;
    m_held.dropping = false;
    placeAnimated(b, m_bricks[b].pos, m_bricks[b].rot, 1, 0, 0);
    if (m_animOn) m_settles.push_back({b, 0.86f, 0, 0});
    m_selected = b;
    if (keep) {   // Ctrl+click: a copy stays in the hand
        Brick c = m_bricks[b];
        m_bricks.push_back(c);
        rebuildScene();
        m_build.beginHold(m_held, (int)m_bricks.size() - 1, true);
        m_held.vpos = m_bricks[b].pos + float3(0, 1.2f, 0);
        m_holding = true;
        return;
    }
    syncObjects();
}

void BrickApp::updateAnim(float dt) {
    bool changed = false;
    for (size_t i = 0; i < m_settles.size();) {
        Settle& s = m_settles[i];
        s.t += dt;
        const float w = 30, z = 0.28f;
        s.v += ((1 - s.s) * w * w - s.v * 2 * z * w) * dt;
        s.s += s.v * dt;
        const bool done = s.t > 0.6f || s.brick >= (int)m_bricks.size();
        if (s.brick < (int)m_bricks.size()) placeAnimated(s.brick, m_bricks[s.brick].pos, m_bricks[s.brick].rot, done ? 1 : s.s, 0, 0);
        changed = true;
        if (done) m_settles.erase(m_settles.begin() + i);
        else ++i;
    }
    Object* ghost = (m_ghostObject >= 0 && m_ghostObject < (int)m_scene.objects.size()) ? &m_scene.objects[m_ghostObject] : nullptr;
    const bool showGhost = m_holding && m_animOn && m_held.hasTarget && !m_held.dropping;
    if (ghost && ghost->visible != showGhost) { ghost->visible = showGhost; changed = true; }
    if (m_holding && m_held.hasTarget) {
        Held& H = m_held;
        const int b = H.brick;
        H.time += dt;
        H.snapT += dt;
        if (H.dropping) {
            H.dropT += dt / 0.15f;
            const float t = std::min(H.dropT, 1.0f), e = t * t;
            H.vpos = H.dropFrom + (H.target - H.dropFrom) * e;
            H.vrot = qSlerp(H.dropRotFrom, H.rot, std::min(1.0f, t * 1.5f));
            H.tiltX *= 0.7f;
            H.tiltZ *= 0.7f;
            placeAnimated(b, H.vpos, H.vrot, 1, H.tiltX, H.tiltZ);
            changed = true;
            if (H.dropT >= 1) finishDrop();
        } else if (!m_animOn) {
            H.vpos = H.target;
            H.vvel = float3(0);
            H.vrot = H.rot;
            H.tiltX = H.tiltZ = 0;
            H.squash = 1;
            placeAnimated(b, H.vpos, H.vrot, 1, 0, 0);
            changed = true;
        } else {
            const float bob = 0.07f * std::sin(H.time * 3) + 0.025f * std::sin(H.time * 7.3f);
            const float3 desired = H.target + float3(0, m_pivotEdit ? 0.0f : 0.9f + bob, 0);
            const float w = 14;
            H.vvel = H.vvel + ((desired - H.vpos) * (w * w) - H.vvel * (2 * w)) * dt;
            H.vpos = H.vpos + H.vvel * dt;
            H.vrot = qSlerp(H.vrot, H.rot, 1 - std::exp(-dt * 12));
            const float k = 1 - std::exp(-dt * 10);
            const float tx = std::clamp(H.vvel.z * 0.035f, -0.3f, 0.3f), tz = std::clamp(-H.vvel.x * 0.035f, -0.3f, 0.3f);
            H.tiltX += (tx + 0.03f * std::sin(H.time * 2.1f) - H.tiltX) * k;
            H.tiltZ += (tz + 0.03f * std::sin(H.time * 1.7f) - H.tiltZ) * k;
            const float ws = 22, zs = 0.35f;
            H.squashVel += ((1 - H.squash) * ws * ws - H.squashVel * 2 * zs * ws) * dt;
            H.squash += H.squashVel * dt;
            placeAnimated(b, H.vpos, H.vrot, H.squash, H.tiltX, H.tiltZ);
            changed = true;
        }
        if (ghost && showGhost) {
            ghost->mesh = m_scene.objects[m_firstBrickObject + b].mesh;
            ghost->position = H.target;
            ghost->rot = H.rot;
            ghost->material = H.blocked ? m_ghostBlockedMaterial : m_ghostMaterial;
        }
    }
    if (changed) {
        ++m_scene.objectsVersion;
        m_renderer.resetAccumulation();
    }
}

// Visor (yellow squares on the footprint), dashed drop line, ring at the pivot target, glow of the aimed voxel and
// the selection rectangle: drawn over the 3D view (depthTest false in the web).
void BrickApp::drawHoldOverlay() {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float s = m_ui;
    if (m_marquee) {
        ImVec2 a(std::min(m_marquee0.x, m_marquee1.x), std::min(m_marquee0.y, m_marquee1.y)), b(std::max(m_marquee0.x, m_marquee1.x), std::max(m_marquee0.y, m_marquee1.y));
        dl->AddRectFilled(a, b, IM_COL32(247, 197, 30, 31));
        dl->AddRect(a, b, IM_COL32(247, 197, 30, 255), 0, 0, 1.5f);
    }
    if (!m_holding || !m_held.hasTarget || !m_animOn || m_held.dropping) return;
    const Held& H = m_held;
    View v = view();
    const Part& p = m_lib.parts[m_bricks[H.brick].part];
    const float pulse = 0.5f + 0.5f * std::sin(H.time * 5);
    // footprint: voxels of the lowest layer once oriented
    std::vector<float3> vw;
    float minY = 1e30f;
    for (const float3& vx : p.voxels) { float3 w = qRot(H.rot, vx) + H.target; vw.push_back(w); minY = std::min(minY, w.y); }
    const ImU32 fill = IM_COL32(0xf7, 0xc5, 0x1e, (int)(255 * (0.14f + 0.08f * pulse))), edge = IM_COL32(0xf7, 0xc5, 0x1e, 217);
    std::vector<std::pair<float, float>> seen;
    for (const float3& w : vw) {
        if (w.y > minY + 0.02f) continue;
        bool dup = false;
        for (auto& q : seen) if (std::fabs(q.first - w.x) < 0.01f && std::fabs(q.second - w.z) < 0.01f) dup = true;
        if (dup) continue;
        seen.push_back({w.x, w.z});
        ImVec2 c[4];
        bool ok = true;
        const float hsz = 0.38f;
        const float3 corners[4] = {float3(w.x - hsz, minY + 0.006f, w.z - hsz), float3(w.x + hsz, minY + 0.006f, w.z - hsz), float3(w.x + hsz, minY + 0.006f, w.z + hsz), float3(w.x - hsz, minY + 0.006f, w.z + hsz)};
        for (int k = 0; k < 4; ++k) ok &= v.project(corners[k], c[k].x, c[k].y);
        if (!ok) continue;
        dl->AddConvexPolyFilled(c, 4, fill);
        dl->AddPolyline(c, 4, edge, ImDrawFlags_Closed, 1.0f);
    }
    // dashed line from the pivot in hand to its target
    const float3 gT = qRot(H.rot, H.grab) + H.target, gH = qRot(H.vrot, H.grab) + H.vpos;
    float ax, ay, bx, by;
    if (v.project(gH, ax, ay) && v.project(gT, bx, by)) {
        const float len = std::sqrt((bx - ax) * (bx - ax) + (by - ay) * (by - ay));
        const float dash = 6 * s, gap = 4.5f * s;
        for (float t = 0; t < len; t += dash + gap) {
            float t1 = std::min(len, t + dash);
            dl->AddLine(ImVec2(ax + (bx - ax) * t / len, ay + (by - ay) * t / len), ImVec2(ax + (bx - ax) * t1 / len, ay + (by - ay) * t1 / len), IM_COL32(255, 255, 255, 191), 1.5f * s);
        }
    }
    // ring (0.17 .. 0.25) and its dot at the pivot target, pulsing
    {
        const float sc = 1 + 0.35f * pulse;
        std::vector<ImVec2> outer, inner;
        for (int k = 0; k <= 40; ++k) {
            float a = k * 2 * 3.14159265f / 40;
            float x, y;
            if (v.project(gT + float3(std::cos(a) * 0.25f * sc, 0.012f, std::sin(a) * 0.25f * sc), x, y)) outer.push_back(ImVec2(x, y));
            if (v.project(gT + float3(std::cos(a) * 0.17f * sc, 0.012f, std::sin(a) * 0.17f * sc), x, y)) inner.push_back(ImVec2(x, y));
        }
        for (size_t k = 0; k + 1 < outer.size() && k + 1 < inner.size(); ++k) {
            ImVec2 q[4] = {outer[k], outer[k + 1], inner[k + 1], inner[k]};
            dl->AddConvexPolyFilled(q, 4, IM_COL32(0xf7, 0xc5, 0x1e, 230));
        }
        float cx, cy;
        if (v.project(gT, cx, cy)) dl->AddCircleFilled(ImVec2(cx, cy), 3 * s, IM_COL32(0xf7, 0xc5, 0x1e, 230), 12);
    }
    // glow of the voxel the pivot snaps onto: pops in (0.18 s, small overshoot) and pulses
    {
        float sx, sy;
        if (v.project(H.snapVoxel, sx, sy)) {
            const float t = std::min(H.snapT / 0.18f, 1.0f);
            const float pop = t < 1 ? 1 - std::pow(1 - t, 3.0f) * std::cos(t * 6) : 1;
            const float puls = 0.5f + 0.5f * std::sin(H.time * 6);
            float3 r, u, f;
            m_scene.camera.basis(r, u, f);
            float ex, ey;
            v.project(H.snapVoxel + r * 0.15f, ex, ey);
            const float R = std::max(2.0f, std::fabs(ex - sx)) * std::max(0.01f, pop) * (1 + 0.15f * puls);
            const ImU32 core = H.blocked ? IM_COL32(0xff, 0x4d, 0x4d, (int)(255 * (0.45f + 0.35f * t))) : IM_COL32(0xff, 0xe0, 0x66, (int)(255 * (0.45f + 0.35f * t)));
            const ImU32 halo = H.blocked ? IM_COL32(0xe5, 0x48, 0x4d, (int)(255 * (0.08f + 0.12f * puls) * t)) : IM_COL32(0xf7, 0xc5, 0x1e, (int)(255 * (0.08f + 0.12f * puls) * t));
            dl->AddCircleFilled(ImVec2(sx, sy), R, halo, 24);
            dl->AddCircleFilled(ImVec2(sx, sy), R * 0.5f, core, 16);
        }
    }
}

} // namespace brickit
