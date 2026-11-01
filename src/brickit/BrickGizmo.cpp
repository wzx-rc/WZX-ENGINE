// BrickIt - move / rotate manipulators of the web builder: thin LEGO-coloured arrows (red X, yellow Y, blue Z) move the
// selection by studs / plates, rings turn it by 90 degrees around its pivot, the white centre sphere takes it in hand
// (free move). Drawn over everything (depthTest false), sized 0.07 x camera distance, white under the mouse.
#include "BrickApp.h"
#include <algorithm>
#include <cmath>

namespace brickit {

static const ImU32 kGz[3] = {IM_COL32(0xc9, 0x1a, 0x09, 242), IM_COL32(0xf2, 0xcd, 0x37, 242), IM_COL32(0x00, 0x55, 0xbf, 242)};
static float3 axisOf(int a) { return float3(a == 0 ? 1.0f : 0.0f, a == 1 ? 1.0f : 0.0f, a == 2 ? 1.0f : 0.0f); }
// two unit vectors spanning the plane of a ring around axis a
static void ringBasis(int a, float3& u, float3& v) {
    if (a == 0) { u = float3(0, 1, 0); v = float3(0, 0, -1); }
    else if (a == 1) { u = float3(1, 0, 0); v = float3(0, 0, -1); }
    else { u = float3(1, 0, 0); v = float3(0, 1, 0); }
}
static float segDist(ImVec2 p, ImVec2 a, ImVec2 b) {
    float dx = b.x - a.x, dy = b.y - a.y, l = dx * dx + dy * dy;
    float t = l > 0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / l, 0.0f, 1.0f) : 0;
    float ex = a.x + dx * t - p.x, ey = a.y + dy * t - p.y;
    return std::sqrt(ex * ex + ey * ey);
}

bool BrickApp::gizmoVisible() const {
    return m_selected >= 0 && m_selected < (int)m_bricks.size() && !m_holding && !m_mechOn && (m_mode == 2 || m_mode == 3);
}
float3 BrickApp::gizmoPivot(int b) const { return m_build.worldVoxel(m_bricks[b], m_build.grabOf(m_bricks[b])); }
float BrickApp::gizmoScale(const float3& piv) const {
    float3 r, u, f;
    m_scene.camera.basis(r, u, f);
    float3 eye = m_scene.camera.target - f * m_scene.camera.distance;
    return length(eye - piv) * 0.07f;
}

// Screen-space version of the web's invisible hit meshes (cylinders 0.11, tori 0.07, sphere 0.3); the sphere wins.
BrickApp::GizmoHandle BrickApp::pickGizmo(float sx, float sy) const {
    GizmoHandle none;
    if (!gizmoVisible()) return none;
    View v = view();
    const float3 piv = gizmoPivot(m_selected);
    const float k = gizmoScale(piv);
    float cx, cy;
    if (!v.project(piv, cx, cy)) return none;
    float ex, ey;
    float3 r, u, f;
    m_scene.camera.basis(r, u, f);
    v.project(piv + r * k, ex, ey);
    const float px = std::max(1.0f, std::fabs(ex - cx));   // pixels per gizmo unit
    const ImVec2 m(sx, sy);
    if (m_mode == 2 && std::hypot(sx - cx, sy - cy) < 0.3f * px) return GizmoHandle{2, 0};
    float best = 1e30f;
    GizmoHandle bh;
    for (int a = 0; a < 3; ++a) {
        if (m_mode == 2) {
            ImVec2 p0, p1;
            if (!v.project(piv, p0.x, p0.y) || !v.project(piv + axisOf(a) * (1.25f * k), p1.x, p1.y)) continue;
            float d = segDist(m, p0, p1);
            if (d < 0.11f * px + 4 && d < best) { best = d; bh = GizmoHandle{0, a}; }
        } else {
            float3 ru, rv;
            ringBasis(a, ru, rv);
            ImVec2 prev;
            bool hasPrev = false;
            for (int i = 0; i <= 48; ++i) {
                float t = i * 6.2831853f / 48;
                ImVec2 q;
                bool ok = v.project(piv + (ru * std::cos(t) + rv * std::sin(t)) * (0.78f * k), q.x, q.y);
                if (ok && hasPrev) {
                    float d = segDist(m, prev, q);
                    if (d < 0.07f * px + 4 && d < best) { best = d; bh = GizmoHandle{1, a}; }
                }
                prev = q;
                hasPrev = ok;
            }
        }
    }
    return bh;
}

// closest parameter along the axis line (through o, direction a) to the mouse ray
static float axisParam(const float3& ro, const float3& rd, const float3& o, const float3& a) {
    float3 w = o - ro;
    float b = dot(a, rd), d = dot(a, w), e = dot(rd, w);
    float den = 1 - b * b;
    return std::fabs(den) < 1e-6f ? 0 : (b * e - d) / den;
}
static bool rayPlane(const float3& ro, const float3& rd, const float3& n, const float3& p, float3& out) {
    float den = dot(n, rd);
    if (std::fabs(den) < 1e-6f) return false;
    float t = dot(n, p - ro) / den;
    if (t < 0) return false;
    out = ro + rd * t;
    return true;
}

void BrickApp::gizmoStart(const GizmoHandle& h, float sx, float sy) {
    pushUndo();
    GizmoDrag& g = m_gz;
    g = GizmoDrag();
    g.h = h;
    g.brick = m_selected;
    g.startPos = m_bricks[g.brick].pos;
    g.startRot = m_bricks[g.brick].rot;
    g.pivot = gizmoPivot(g.brick);
    g.others = m_multi;
    for (int o : m_multi) g.othersStart.push_back(m_bricks[o]);
    float3 ro, rd;
    view().ray(sx, sy, ro, rd);
    const float3 a = axisOf(h.axis);
    if (h.kind == 0) g.t0 = axisParam(ro, rd, g.pivot, a);
    else {
        float3 p;
        g.hasV0 = rayPlane(ro, rd, a, g.pivot, p);
        g.v0 = p - g.pivot;
    }
    m_gizmoDragging = true;
}

void BrickApp::gizmoDragTo(float sx, float sy) {
    GizmoDrag& g = m_gz;
    if (g.brick < 0 || g.brick >= (int)m_bricks.size()) { m_gizmoDragging = false; return; }
    Brick& b = m_bricks[g.brick];
    const float3 a = axisOf(g.h.axis);
    float3 ro, rd;
    view().ray(sx, sy, ro, rd);
    if (g.h.kind == 0) {
        float d = axisParam(ro, rd, g.pivot, a) - g.t0;
        if (m_build.gridSnap) { float step = g.h.axis == 1 ? kPlate : kPitch; d = std::round(d / step) * step; }
        b.pos = g.startPos + a * d;
        const float floorMin = m_build.aabbOf(b.part, b.rot, float3(0)).mn.y;
        if (g.h.axis == 1 && b.pos.y + floorMin < 0) b.pos.y = -floorMin;
        const float3 delta = b.pos - g.startPos;
        for (size_t i = 0; i < g.others.size(); ++i) m_bricks[g.others[i]].pos = g.othersStart[i].pos + delta;
    } else {
        float3 p;
        if (!g.hasV0 || !rayPlane(ro, rd, a, g.pivot, p)) return;
        const float3 v = p - g.pivot;
        const float ang = std::atan2(dot(a, cross(g.v0, v)), dot(g.v0, v));
        const int steps = (int)std::lround(ang / (kPi * 0.5f));
        if (steps == g.steps) return;
        g.steps = steps;
        const float4 dq = qAxisAngle(a, steps * kPi * 0.5f);
        b.rot = snapQuat(qMul(dq, g.startRot));
        b.pos = g.pivot + qRot(dq, g.startPos - g.pivot);
        for (size_t i = 0; i < g.others.size(); ++i) {
            Brick& o = m_bricks[g.others[i]];
            o.rot = snapQuat(qMul(dq, g.othersStart[i].rot));
            o.pos = g.pivot + qRot(dq, g.othersStart[i].pos - g.pivot);
        }
        if (m_animOn) m_settles.push_back({g.brick, 0.92f, 0, 0});
    }
    syncObjects();
}

void BrickApp::drawGizmo() {
    if (!gizmoVisible()) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    View v = view();
    const float3 piv = gizmoPivot(m_selected);
    const float k = gizmoScale(piv);
    float cx, cy;
    if (!v.project(piv, cx, cy)) return;
    float3 r, u, f;
    m_scene.camera.basis(r, u, f);
    float ex, ey;
    v.project(piv + r * k, ex, ey);
    const float px = std::max(1.0f, std::fabs(ex - cx));
    const GizmoHandle hov = m_gizmoDragging ? m_gz.h : m_gizmoHover;
    const ImU32 white = IM_COL32(255, 255, 255, 242);
    if (m_mode == 2) {
        // arrows drawn back to front (farthest axis tip first)
        int order[3] = {0, 1, 2};
        std::sort(order, order + 3, [&](int a, int b) { return dot(axisOf(a), f) > dot(axisOf(b), f); });
        for (int a : order) {
            const ImU32 col = (hov.kind == 0 && hov.axis == a) ? white : kGz[a];
            ImVec2 p0, p1, tip;
            const float3 d = axisOf(a);
            if (!v.project(piv + d * (0.05f * k), p0.x, p0.y) || !v.project(piv + d * (1.03f * k), p1.x, p1.y) || !v.project(piv + d * (1.21f * k), tip.x, tip.y)) continue;
            dl->AddLine(p0, p1, col, std::max(1.5f, 0.036f * px));
            // cone (radius 0.06, length 0.18) seen from the camera: triangle from its base to the tip
            float3 side = cross(d, f);
            if (length(side) < 1e-4f) side = r;
            side = normalize(side) * (0.06f * k);
            ImVec2 b0, b1;
            v.project(piv + d * (1.03f * k) + side, b0.x, b0.y);
            v.project(piv + d * (1.03f * k) - side, b1.x, b1.y);
            dl->AddTriangleFilled(b0, b1, tip, col);
        }
        // white centre sphere (0.17), larger under the mouse
        const float rad = 0.17f * px * (hov.kind == 2 ? 1.35f : 1.0f);
        dl->AddCircleFilled(ImVec2(cx, cy), rad, IM_COL32(255, 255, 255, 245), 32);
    } else {
        for (int a = 0; a < 3; ++a) {
            const ImU32 col = (hov.kind == 1 && hov.axis == a) ? IM_COL32(255, 255, 255, 179) : ((kGz[a] & 0x00ffffffu) | (179u << 24));   // opacity .7
            float3 ru, rv;
            ringBasis(a, ru, rv);
            std::vector<ImVec2> pts;
            for (int i = 0; i <= 64; ++i) {
                float t = i * 6.2831853f / 64;
                ImVec2 q;
                if (v.project(piv + (ru * std::cos(t) + rv * std::sin(t)) * (0.78f * k), q.x, q.y)) pts.push_back(q);
            }
            if (pts.size() > 1) dl->AddPolyline(pts.data(), (int)pts.size(), col, 0, std::max(1.2f, 0.024f * px));
        }
    }
}

// --------------------------------------------------------------------------- pivot edit (web: aimPivot, refreshOverlays)

// Nearest voxel of the focus brick to the cursor, on screen.
void BrickApp::aimPivot(float sx, float sy) {
    const int b = focusBrick();
    if (b < 0 || b >= (int)m_bricks.size()) return;
    const Part& p = m_lib.parts[m_bricks[b].part];
    Affine xf = m_scene.objects[m_firstBrickObject + b].transform();
    View v = view();
    float best = 1e30f;
    int bi = -1;
    for (size_t i = 0; i < p.voxels.size(); ++i) {
        float x, y;
        if (!v.project(xf.point(p.voxels[i]), x, y)) continue;
        float d = (x - sx) * (x - sx) + (y - sy) * (y - sy);
        if (d < best) { best = d; bi = (int)i; }
    }
    if (bi < 0 || bi == m_bricks[b].grab) return;
    m_bricks[b].grab = bi;
    if (m_holding && m_held.brick == b) {
        m_held.grab = p.voxels[bi];
        m_held.cacheValid = false;
        updateHeld(sx, sy);
    }
}

// Spheres of radius 0.2 on every voxel (stud yellow, pin cyan, axle magenta, 90 %), the current pivot white and 1.5x,
// over everything (depthTest false).
void BrickApp::drawPivots() {
    if (!m_pivotEdit) return;
    const int b = focusBrick();
    if (b < 0 || b >= (int)m_bricks.size()) return;
    static const ImU32 col[3] = {IM_COL32(0xff, 0xc2, 0x1a, 230), IM_COL32(0x38, 0xd0, 0xe8, 230), IM_COL32(0xe0, 0x5a, 0xd7, 230)};
    const Part& p = m_lib.parts[m_bricks[b].part];
    Affine xf = m_scene.objects[m_firstBrickObject + b].transform();
    View v = view();
    float3 r, u, f;
    m_scene.camera.basis(r, u, f);
    const float3 cur = m_holding && m_held.brick == b ? m_held.grab : m_build.grabOf(m_bricks[b]);
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    int curIdx = -1;
    for (size_t i = 0; i < p.voxels.size(); ++i) {
        const float3& vx = p.voxels[i];
        if (std::fabs(vx.x - cur.x) < 1e-3f && std::fabs(vx.y - cur.y) < 1e-3f && std::fabs(vx.z - cur.z) < 1e-3f) { curIdx = (int)i; continue; }
        float3 w = xf.point(vx);
        float x, y, ex, ey;
        if (!v.project(w, x, y) || !v.project(w + r * 0.2f, ex, ey)) continue;
        uint8_t c = p.conn[i];
        int t = (c == CONN_PIN || c == CONN_PINHOLE) ? 1 : (c == CONN_AXLE || c == CONN_AXLEHOLE) ? 2 : 0;
        dl->AddCircleFilled(ImVec2(x, y), std::max(2.0f, std::fabs(ex - x)), col[t], 18);
    }
    if (curIdx >= 0) {   // drawn last, on top
        float3 w = xf.point(p.voxels[curIdx]);
        float x, y, ex, ey;
        if (v.project(w, x, y) && v.project(w + r * 0.3f, ex, ey)) dl->AddCircleFilled(ImVec2(x, y), std::max(3.0f, std::fabs(ex - x)), IM_COL32(255, 255, 255, 255), 24);
    }
}

} // namespace brickit
