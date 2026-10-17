// Maya-style manipulators ("standard" version): Move (W), Rotate (E), Scale (R) on the selection, the Select tool (Q)
// has none. Pale colours and thin lines; the handle under the mouse and the last used handle are yellow.
//   Move  : 3 axes (cone), 3 plane squares (2 axes), centre square = camera plane
//   Rotate: 3 rings (front half), outer view ring, inner trackball (free rotation)
//   Scale : 3 axes (cube), 3 plane squares (2 axes), centre cube = uniform
// Shift (or the Snapping toggle) = increments. Middle button: drags the handle under the mouse, else the last used handle,
// from anywhere in the view. Hold Q / W / E / R + left click: marking menu of the tool.
// Several objects: each one moves / turns / scales on its own pivot (Maya); a selected child of a selected parent
// is left to the hierarchy (the Scene Manager moves it with its parent).
#include "App.h"
#include "GameHost.h"
#include <functional>

namespace app {

namespace {

const ImU32 kAxisCol[3] = {IM_COL32(236, 128, 128, 255), IM_COL32(150, 214, 140, 255), IM_COL32(128, 170, 240, 255)};
const ImU32 kHot = IM_COL32(255, 226, 102, 255);
const ImU32 kCenterCol = IM_COL32(236, 236, 226, 255);
const ImU32 kViewCol = IM_COL32(150, 210, 222, 255);
const ImU32 kBallCol = IM_COL32(200, 200, 200, 70);
const int kRingSeg = 72;
// geometry in units of the manipulator size (an axis is 1 long)
const float kConeBase = 0.86f, kPlane0 = 0.40f, kPlane1 = 0.58f, kCube = 0.045f, kRing = 0.85f, kViewRing = 1.08f;

float3 unitAxis(int a) { return float3(a == 0 ? 1.0f : 0.0f, a == 1 ? 1.0f : 0.0f, a == 2 ? 1.0f : 0.0f); }
ImVec2 iv(float2 p) { return ImVec2(p.x, p.y); }
ImU32 withAlpha(ImU32 c, int a) { return (c & 0x00FFFFFFu) | ((ImU32)a << 24); }

float distSeg(float px, float py, float2 a, float2 b) {
    float dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
    float t = l2 > 1e-6f ? clampf(((px - a.x) * dx + (py - a.y) * dy) / l2, 0, 1) : 0;
    float qx = a.x + dx * t - px, qy = a.y + dy * t - py;
    return std::sqrt(qx * qx + qy * qy);
}

bool inQuad(float px, float py, const float2 q[4]) {
    int pos = 0, neg = 0;
    for (int i = 0; i < 4; ++i) {
        float2 a = q[i], b = q[(i + 1) % 4];
        float c = (b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x);
        if (c > 0) ++pos; else if (c < 0) ++neg;
    }
    return pos == 0 || neg == 0;
}

// closest parameter along the line (o, a) to the mouse ray
float axisParam(const float3& o, const float3& a, const float3& ro, const float3& rd) {
    float3 w = o - ro;
    float b = dot(a, rd), d = dot(a, w), e = dot(rd, w);
    float den = 1 - b * b;
    return std::fabs(den) < 1e-6f ? 0.0f : (b * e - d) / den;
}

bool rayPlane(const float3& ro, const float3& rd, const float3& p, const float3& n, float3& hit) {
    float den = dot(rd, n);
    if (std::fabs(den) < 1e-6f) return false;
    float t = dot(p - ro, n) / den;
    if (t < 0) return false;
    hit = ro + rd * t;
    return true;
}

float snapTo(float v, float step) { return step > 0 ? std::round(v / step) * step : v; }
float wrapPi(float a) { while (a > kPi) a -= 2 * kPi; while (a < -kPi) a += 2 * kPi; return a; }

// the plane handle h (MH_YZ..MH_XY) spans the two axes other than its normal
void planeAxes(int h, int& u, int& v) { int n = h - MH_YZ; u = (n + 1) % 3; v = (n + 2) % 3; if (u > v) std::swap(u, v); }

} // namespace

// --------------------------------------------------------------------------- frame, size, targets

bool App::gizmoShown() const {
    if (!m_showGizmo || m_selected < 0 || m_selected >= (int)m_scene.objects.size()) return false;
    if (m_tool != TOOL_MOVE && m_tool != TOOL_ROTATE && m_tool != TOOL_SCALE) return false;
    return !(m_game && (m_game->placementMode() || m_game->playing()));
}

// world length of a manipulator axis at p: a constant size on screen
float App::gizmoSize(const float3& p) const {
    const Camera& c = m_scene.camera;
    float3 r, u, f;
    c.basis(r, u, f);
    float z = std::max(dot(p - c.eye(), f), 1e-3f);
    float wpp = 2.0f * z * std::tan(c.fovY * 0.5f) / std::max(m_rcView.h, 1.0f);
    return 92.0f * m_dpi * wpp;
}

// axes of the manipulator for an object: world, its own axes, or its parent's axes (the Scale tool is always local)
void App::gizmoFrame(int obj, int tool, float3 ax[3]) const {
    int mode = tool == TOOL_SCALE ? 1 : m_axisMode;
    float4 q = qIdentity();
    const Object& o = m_scene.objects[obj];
    if (mode == 1) q = objRot(o);
    else if (mode == 2 && o.parent) {
        NodeRef p = findNode(o.parent);
        if (p.kind == NODE_OBJECT) q = objRot(m_scene.objects[p.index]);
    }
    for (int a = 0; a < 3; ++a) ax[a] = mode == 0 ? unitAxis(a) : normalize(qRot(q, unitAxis(a)));
}

// selected objects driven by the manipulator: a child whose parent (or ancestor) is selected follows the hierarchy
std::vector<int> App::gizmoTargets() const {
    std::vector<int> all = selectionAll(), out;
    for (int i : all) {
        if (i < 0 || i >= (int)m_scene.objects.size()) continue;
        bool under = false;
        for (int j : all)
            if (j != i && j >= 0 && j < (int)m_scene.objects.size() && m_scene.objects[j].id && isAncestor(m_scene.objects[j].id, m_scene.objects[i].id)) under = true;
        if (!under) out.push_back(i);
    }
    return out;
}

// --------------------------------------------------------------------------- hit test

int App::gizmoHit(float mx, float my) {
    if (!gizmoShown()) return MH_NONE;
    const Object& o = m_scene.objects[m_selected];
    float3 pv = o.position, ax[3];
    gizmoFrame(m_selected, m_tool, ax);
    float s = gizmoSize(pv);
    float2 pc;
    if (!project(pv, pc)) return MH_NONE;
    const float d = m_dpi, lineTol = 7.0f * d;
    float3 eye = m_scene.camera.eye(), toCam = normalize(eye - pv);

    if (m_tool == TOOL_ROTATE) {
        int best = MH_NONE;
        float bd = lineTol;
        for (int a = 0; a < 3; ++a) {
            float3 u = ax[(a + 1) % 3], v = ax[(a + 2) % 3];
            float2 prev;
            bool hasPrev = false;
            for (int i = 0; i <= kRingSeg; ++i) {
                float ang = i * 2 * kPi / kRingSeg;
                float3 dir = u * std::cos(ang) + v * std::sin(ang);
                float2 p;
                bool ok = dot(dir, toCam) > -0.05f && project(pv + dir * (kRing * s), p);
                if (ok && hasPrev) { float dd = distSeg(mx, my, prev, p); if (dd < bd) { bd = dd; best = a; } }
                prev = p;
                hasPrev = ok;
            }
        }
        if (best != MH_NONE) return best;
        float r = std::hypot(mx - pc.x, my - pc.y), rPx = 92.0f * d;
        if (std::fabs(r - kViewRing * rPx) < lineTol) return MH_VIEW;
        if (r < kRing * rPx) return MH_FREE;
        return MH_NONE;
    }

    // move / scale: centre first, then the plane squares, then the axes
    if (std::hypot(mx - pc.x, my - pc.y) < 9.0f * d) return MH_CENTER;
    float3 viewDir = normalize(pv - eye);
    for (int h = MH_YZ; h <= MH_XY; ++h) {
        int u, v;
        planeAxes(h, u, v);
        if (std::fabs(dot(ax[h - MH_YZ], viewDir)) < 0.2f) continue;
        float2 q[4];
        bool ok = project(pv + ax[u] * (kPlane0 * s) + ax[v] * (kPlane0 * s), q[0]) && project(pv + ax[u] * (kPlane1 * s) + ax[v] * (kPlane0 * s), q[1]) &&
                  project(pv + ax[u] * (kPlane1 * s) + ax[v] * (kPlane1 * s), q[2]) && project(pv + ax[u] * (kPlane0 * s) + ax[v] * (kPlane1 * s), q[3]);
        if (ok && inQuad(mx, my, q)) return h;
    }
    int best = MH_NONE;
    float bd = lineTol;
    for (int a = 0; a < 3; ++a) {
        float2 pt;
        if (!project(pv + ax[a] * s, pt)) continue;
        if (std::hypot(pt.x - pc.x, pt.y - pc.y) < 14.0f * d) continue;   // axis pointing at the camera
        float dd = std::min(distSeg(mx, my, pc, pt), std::hypot(mx - pt.x, my - pt.y) - 3.0f * d);
        if (dd < bd) { bd = dd; best = a; }
    }
    return best;
}

// --------------------------------------------------------------------------- drag

void App::gizmoBegin(int handle, int button, float mx, float my) {
    float3 ro, rd;
    cameraRay(mx, my, ro, rd);
    pushUndo();
    GizmoDrag& g = m_gz;
    g = GizmoDrag();
    g.active = true;
    g.handle = handle;
    g.tool = m_tool;
    g.button = button;
    g.obj = m_selected;
    g.pivot = m_scene.objects[m_selected].position;
    g.size = gizmoSize(g.pivot);
    g.mouse0 = float2(mx, my);
    g.prevMouse = g.mouse0;
    gizmoFrame(m_selected, m_tool, g.ax);
    for (int i : gizmoTargets()) {
        const Object& o = m_scene.objects[i];
        GizmoDrag::Item it;
        it.obj = i;
        it.pos = o.position;
        it.rot = objRot(o);
        it.scale = o.scale;
        gizmoFrame(i, m_tool, it.ax);
        g.items.push_back(it);
    }
    m_gzLast[m_tool] = handle;
    float3 r, u, f;
    m_scene.camera.basis(r, u, f);
    const float3 viewDir = normalize(g.pivot - m_scene.camera.eye());

    if (m_tool == TOOL_ROTATE) {
        if (handle == MH_FREE) return;
        float3 n = handle == MH_VIEW ? viewDir : g.ax[handle];
        g.n = n;
        float3 hit;
        if (handle == MH_VIEW || std::fabs(dot(n, viewDir)) > 0.25f) {
            g.tangent = false;
            g.v0 = rayPlane(ro, rd, g.pivot, n, hit) ? normalize(hit - g.pivot) : normalize(cross(n, u));
        } else {
            // ring seen edge-on: drag along the ring's screen tangent at the nearest point
            g.tangent = true;
            float3 a = g.ax[(handle + 1) % 3], b = g.ax[(handle + 2) % 3];
            float best = 1e30f, bestAng = 0;
            for (int i = 0; i < kRingSeg; ++i) {
                float ang = i * 2 * kPi / kRingSeg;
                float2 p;
                if (project(g.pivot + (a * std::cos(ang) + b * std::sin(ang)) * (kRing * g.size), p)) {
                    float dd = std::hypot(p.x - mx, p.y - my);
                    if (dd < best) { best = dd; bestAng = ang; }
                }
            }
            float3 dir = a * std::cos(bestAng) + b * std::sin(bestAng), tan = a * -std::sin(bestAng) + b * std::cos(bestAng);
            g.v0 = dir;
            float2 p0, p1;
            if (project(g.pivot + dir * (kRing * g.size), p0) && project(g.pivot + (dir + tan * 0.1f) * (kRing * g.size), p1)) {
                float tx = p1.x - p0.x, ty = p1.y - p0.y, l = std::hypot(tx, ty);
                g.tan2 = l > 1e-4f ? float2(tx / l, ty / l) : float2(1, 0);
            } else g.tan2 = float2(1, 0);
        }
        return;
    }
    if (handle <= MH_Z) g.t0 = axisParam(g.pivot, g.ax[handle], ro, rd);
    else if (handle <= MH_XY) { g.n = g.ax[handle - MH_YZ]; rayPlane(ro, rd, g.pivot, g.n, g.hit0); }
    else { g.n = f; rayPlane(ro, rd, g.pivot, g.n, g.hit0); }
}

bool App::gizmoInput(bool inView) {
    ImGuiIO& io = ImGui::GetIO();
    if (!gizmoShown()) {
        if (m_gz.active && !m_gz.moved && !m_undo.empty()) m_undo.pop_back();
        m_gz.active = false;
        m_gzHover = MH_NONE;
        return false;
    }
    if (m_gzDemo) return m_gz.active;
    float mx = io.MousePos.x, my = io.MousePos.y;
    GizmoDrag& g = m_gz;
    if (g.active) {
        if (!io.MouseDown[g.button] || g.obj != m_selected || g.tool != m_tool) {
            g.active = false;
            if (!g.moved && !m_undo.empty()) m_undo.pop_back();
            return true;
        }
        for (const auto& it : g.items) if (it.obj >= (int)m_scene.objects.size()) { g.active = false; return true; }
        gizmoDrag(mx, my, io.KeyShift || m_magnet);
        g.prevMouse = float2(mx, my);
        return true;
    }
    if (io.KeyAlt) { m_gzHover = MH_NONE; return false; }
    int h = inView ? gizmoHit(mx, my) : MH_NONE;
    m_gzHover = h;
    if (!inView) return false;
    if (h != MH_NONE && ImGui::IsMouseClicked(0)) { gizmoBegin(h, 0, mx, my); return true; }
    // middle button (Maya): the handle under the mouse, else the last used handle, dragged from anywhere
    if (ImGui::IsMouseClicked(2)) {
        int mh = h != MH_NONE ? h : m_gzLast[m_tool];
        if (mh == MH_NONE) mh = m_tool == TOOL_ROTATE ? MH_FREE : MH_CENTER;
        gizmoBegin(mh, 2, mx, my);
        return true;
    }
    return false;
}

void App::gizmoDrag(float mx, float my, bool snap) {
    GizmoDrag& g = m_gz;
    float3 ro, rd;
    cameraRay(mx, my, ro, rd);
    float3 r, u, f;
    m_scene.camera.basis(r, u, f);

    if (g.tool == TOOL_MOVE) {
        float c[3] = {0, 0, 0};   // displacement along the manipulator axes
        float3 world(0, 0, 0);    // centre handle: displacement in world space
        if (g.handle <= MH_Z) c[g.handle] = axisParam(g.pivot, g.ax[g.handle], ro, rd) - g.t0;
        else {
            float3 hit;
            if (!rayPlane(ro, rd, g.pivot, g.n, hit)) return;
            float3 dv = hit - g.hit0;
            if (g.handle == MH_CENTER) world = dv;
            else { int a, b; planeAxes(g.handle, a, b); c[a] = dot(dv, g.ax[a]); c[b] = dot(dv, g.ax[b]); }
        }
        if (snap) {
            const bool worldAxes = m_axisMode == 0 || g.handle == MH_CENTER;
            for (int i = 0; i < 3; ++i) {
                float& v = g.handle == MH_CENTER ? world[i] : c[i];
                if (v == 0.0f) continue;
                // absolute: the pivot lands on the grid of the step (world axes only), else a multiple of the step
                if (m_snapAbsolute && worldAxes) v = snapTo(g.pivot[i] + v, m_stepMove) - g.pivot[i];
                else v = snapTo(v, m_stepMove);
            }
        }
        g.value = g.handle == MH_CENTER ? world : float3(c[0], c[1], c[2]);
        bool changed = false;
        for (const auto& it : g.items) {
            Object& o = m_scene.objects[it.obj];
            float3 np = it.pos + (g.handle == MH_CENTER ? world : it.ax[0] * c[0] + it.ax[1] * c[1] + it.ax[2] * c[2]);
            if (np.x != o.position.x || np.y != o.position.y || np.z != o.position.z) { o.position = np; changed = true; }
        }
        if (changed) { g.moved = true; markDirty(); }
        return;
    }

    if (g.tool == TOOL_ROTATE) {
        float4 dqFree = qIdentity();
        if (g.handle == MH_FREE) {
            float dx = mx - g.prevMouse.x, dy = my - g.prevMouse.y;
            float k = 1.0f / (92.0f * m_dpi);
            if (dx != 0 || dy != 0) g.free = qNormalize(qMul(qMul(qAxisAngle(u, dx * k), qAxisAngle(r, dy * k)), g.free));
            dqFree = g.free;
        } else if (g.tangent) {
            g.angle = ((mx - g.mouse0.x) * g.tan2.x + (my - g.mouse0.y) * g.tan2.y) / (kRing * 92.0f * m_dpi);
        } else {
            float3 hit;
            if (rayPlane(ro, rd, g.pivot, g.n, hit)) {
                float3 v = hit - g.pivot;
                float cur = std::atan2(dot(g.n, cross(g.v0, v)), dot(g.v0, v));
                g.angle += wrapPi(cur - g.lastAng);
                g.lastAng = cur;
            }
        }
        float ang = snap ? snapTo(g.angle, radians(m_stepRotate)) : g.angle;
        g.value = float3(degrees(ang), 0, 0);
        bool changed = false;
        for (const auto& it : g.items) {
            Object& o = m_scene.objects[it.obj];
            float4 dq = g.handle == MH_FREE ? dqFree : qAxisAngle(g.handle == MH_VIEW ? g.n : it.ax[g.handle], ang);
            float4 nr = qNormalize(qMul(dq, it.rot));
            if (!o.useRot || nr.x != o.rot.x || nr.y != o.rot.y || nr.z != o.rot.z || nr.w != o.rot.w) {
                o.rot = nr;
                o.useRot = true;
                changed = true;
            }
        }
        if (changed && (g.handle == MH_FREE ? (g.free.w < 0.999999f) : ang != 0.0f)) g.moved = true;
        if (changed) markDirty();
        return;
    }

    // scale: the handle length maps to a factor of 1
    float k = 1.0f;
    if (g.handle <= MH_Z) k = 1.0f + (axisParam(g.pivot, g.ax[g.handle], ro, rd) - g.t0) / g.size;
    else if (g.handle <= MH_XY) {
        float3 hit;
        if (!rayPlane(ro, rd, g.pivot, g.n, hit)) return;
        int a, b;
        planeAxes(g.handle, a, b);
        float3 diag = normalize(g.ax[a] + g.ax[b]);
        k = 1.0f + dot(hit - g.hit0, diag) / (g.size * kPlane1 * 1.41421f);
    } else k = 1.0f + (mx - g.mouse0.x) / (92.0f * m_dpi);
    if (snap) k = snapTo(k, m_stepScale);
    k = std::max(k, 0.01f);
    bool on[3] = {g.handle == MH_X || g.handle >= MH_CENTER, g.handle == MH_Y || g.handle >= MH_CENTER, g.handle == MH_Z || g.handle >= MH_CENTER};
    if (g.handle >= MH_YZ && g.handle <= MH_XY) { int a, b; planeAxes(g.handle, a, b); on[a] = on[b] = true; }
    g.value = float3(on[0] ? k : 1.0f, on[1] ? k : 1.0f, on[2] ? k : 1.0f);
    bool changed = false;
    for (const auto& it : g.items) {
        Object& o = m_scene.objects[it.obj];
        float3 ns = it.scale;
        for (int i = 0; i < 3; ++i) if (on[i]) ns[i] = std::max(0.001f, it.scale[i] * k);
        if (ns.x != o.scale.x || ns.y != o.scale.y || ns.z != o.scale.z) { o.scale = ns; changed = true; }
    }
    if (changed) { g.moved = true; markDirty(); }
}

// --------------------------------------------------------------------------- drawing

// small solid box (scale handles): the faces turned to the camera, shaded per axis
static void drawBox(App* app, ImDrawList* dl, const float3& c, const float3 ax[3], float half, ImU32 col, const float3& eye,
                    bool (App::*proj)(const float3&, float2&) const) {
    static const float shade[3] = {0.80f, 1.0f, 0.90f};
    for (int a = 0; a < 3; ++a)
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            float3 n = ax[a] * (float)sgn;
            float3 fc = c + n * half;
            if (dot(n, eye - fc) <= 0) continue;
            float3 u = ax[(a + 1) % 3] * half, v = ax[(a + 2) % 3] * half;
            float2 p[4];
            if (!(app->*proj)(fc - u - v, p[0]) || !(app->*proj)(fc + u - v, p[1]) || !(app->*proj)(fc + u + v, p[2]) || !(app->*proj)(fc - u + v, p[3])) return;
            int r = (int)(((col >> IM_COL32_R_SHIFT) & 255) * shade[a]), gg = (int)(((col >> IM_COL32_G_SHIFT) & 255) * shade[a]), b = (int)(((col >> IM_COL32_B_SHIFT) & 255) * shade[a]);
            ImVec2 q[4] = {iv(p[0]), iv(p[1]), iv(p[2]), iv(p[3])};
            dl->AddConvexPolyFilled(q, 4, IM_COL32(r, gg, b, 235));
            dl->AddPolyline(q, 4, IM_COL32(40, 40, 40, 120), ImDrawFlags_Closed, 1.0f);
        }
}

void App::drawGizmo(ImDrawList* dl) {
    if (!gizmoShown()) return;
    const Object& o = m_scene.objects[m_selected];
    float3 pv = o.position, ax[3];
    gizmoFrame(m_selected, m_tool, ax);
    float s = gizmoSize(pv);
    float2 pc;
    if (!project(pv, pc)) return;
    const float d = m_dpi, thin = 1.0f * d, thick = 1.6f * d;
    const GizmoDrag& g = m_gz;
    const bool dragging = g.active;
    const int last = m_gzLast[m_tool];
    // yellow: the dragged handle, else the handle under the mouse, else the last used handle (Maya)
    auto hot = [&](int h) { return dragging ? g.handle == h : (m_gzHover == h || (m_gzHover == MH_NONE && last == h)); };
    float3 eye = m_scene.camera.eye(), toCam = normalize(eye - pv), viewDir = -toCam;

    if (m_tool == TOOL_ROTATE) {
        float rPx = 92.0f * d;
        // trackball: the sphere outline (free rotation inside)
        bool ballHot = hot(MH_FREE);
        dl->AddCircleFilled(iv(pc), kRing * rPx, ballHot && dragging ? IM_COL32(255, 226, 102, 22) : IM_COL32(200, 200, 200, 14), 64);
        dl->AddCircle(iv(pc), kRing * rPx, ballHot ? withAlpha(kHot, 150) : kBallCol, 64, thin);
        // angle wedge while dragging a ring
        if (dragging && g.handle != MH_FREE) {
            float ang = radians(g.value.x);
            int n = std::max(2, (int)(std::fabs(ang) / (2 * kPi) * kRingSeg) + 2);
            ImVec2 c0 = iv(pc);
            float3 axn = g.handle == MH_VIEW ? g.n : g.ax[g.handle];
            float rr = (g.handle == MH_VIEW ? kViewRing : kRing) * s;
            float2 prev;
            bool hasPrev = false;
            for (int i = 0; i <= n; ++i) {
                float2 p;
                bool ok = project(pv + qRot(qAxisAngle(axn, ang * i / n), g.v0) * rr, p);
                if (ok && hasPrev) dl->AddTriangleFilled(c0, iv(prev), iv(p), IM_COL32(255, 226, 102, 60));
                prev = p;
                hasPrev = ok;
            }
            float2 p0;
            if (project(pv + g.v0 * rr, p0)) dl->AddLine(c0, iv(p0), withAlpha(kHot, 160), thin);
            if (hasPrev) dl->AddLine(c0, iv(prev), kHot, thin);
        }
        for (int a = 0; a < 3; ++a) {
            bool lit = hot(a);
            ImU32 c = lit ? kHot : kAxisCol[a];
            float3 u = ax[(a + 1) % 3], v = ax[(a + 2) % 3];
            float2 prev;
            bool hasPrev = false;
            for (int i = 0; i <= kRingSeg; ++i) {
                float ang = i * 2 * kPi / kRingSeg;
                float3 dir = u * std::cos(ang) + v * std::sin(ang);
                float2 p;
                bool ok = (dragging || dot(dir, toCam) > -0.05f) && project(pv + dir * (kRing * s), p);
                if (ok && hasPrev) dl->AddLine(iv(prev), iv(p), (dragging && !lit) ? withAlpha(c, 90) : c, lit ? thick : thin);
                prev = p;
                hasPrev = ok;
            }
        }
        dl->AddCircle(iv(pc), kViewRing * rPx, hot(MH_VIEW) ? kHot : kViewCol, 72, hot(MH_VIEW) ? thick : thin);
    } else {
        const bool scale = m_tool == TOOL_SCALE;
        float3 sv = dragging && scale ? g.value : float3(1, 1, 1);
        // plane squares (2 axes), coloured like the axis they are normal to
        for (int h = MH_YZ; h <= MH_XY; ++h) {
            int u, v;
            planeAxes(h, u, v);
            if (std::fabs(dot(ax[h - MH_YZ], viewDir)) < 0.2f) continue;
            float2 q[4];
            if (!(project(pv + ax[u] * (kPlane0 * s) + ax[v] * (kPlane0 * s), q[0]) && project(pv + ax[u] * (kPlane1 * s) + ax[v] * (kPlane0 * s), q[1]) &&
                  project(pv + ax[u] * (kPlane1 * s) + ax[v] * (kPlane1 * s), q[2]) && project(pv + ax[u] * (kPlane0 * s) + ax[v] * (kPlane1 * s), q[3]))) continue;
            bool lit = hot(h);
            ImU32 c = lit ? kHot : kAxisCol[h - MH_YZ];
            ImVec2 pts[4] = {iv(q[0]), iv(q[1]), iv(q[2]), iv(q[3])};
            dl->AddConvexPolyFilled(pts, 4, withAlpha(c, lit ? 110 : 55));
            dl->AddPolyline(pts, 4, withAlpha(c, lit ? 255 : 200), ImDrawFlags_Closed, thin);
        }
        for (int a = 0; a < 3; ++a) {
            float len = scale ? sv[a] : 1.0f;
            float2 pt, pb;
            if (!project(pv + ax[a] * (s * len), pt)) continue;
            if (std::hypot(pt.x - pc.x, pt.y - pc.y) < 14.0f * d * len) continue;   // pointing at the camera
            bool lit = hot(a);
            ImU32 c = lit ? kHot : kAxisCol[a];
            if (scale) {
                dl->AddLine(iv(pc), iv(pt), c, lit ? thick : thin);
                drawBox(this, dl, pv + ax[a] * (s * len), ax, kCube * s, c, eye, &App::project);
                continue;
            }
            if (!project(pv + ax[a] * (kConeBase * s), pb)) continue;
            dl->AddLine(iv(pc), iv(pb), c, lit ? thick : thin);
            // cone: a thin triangle from the base circle to the tip
            float dx = pt.x - pb.x, dy = pt.y - pb.y, l = std::sqrt(dx * dx + dy * dy);
            if (l < 1e-3f) continue;
            dx /= l; dy /= l;
            float hw = (lit ? 3.6f : 3.0f) * d;
            ImVec2 tri[3] = {iv(pt), ImVec2(pb.x - dy * hw, pb.y + dx * hw), ImVec2(pb.x + dy * hw, pb.y - dx * hw)};
            dl->AddTriangleFilled(tri[0], tri[1], tri[2], c);
        }
        // centre anchor: camera-plane square (move), uniform cube (scale)
        bool lit = hot(MH_CENTER);
        if (scale) {
            float k = dragging && g.handle == MH_CENTER ? g.value.x : 1.0f;
            drawBox(this, dl, pv, ax, kCube * 1.25f * s * std::min(k, 3.0f), lit ? kHot : kCenterCol, eye, &App::project);
        } else {
            float hs = 4.0f * d;
            dl->AddRectFilled(ImVec2(pc.x - hs, pc.y - hs), ImVec2(pc.x + hs, pc.y + hs), withAlpha(lit ? kHot : kCenterCol, lit ? 110 : 40));
            dl->AddRect(ImVec2(pc.x - hs, pc.y - hs), ImVec2(pc.x + hs, pc.y + hs), lit ? kHot : kCenterCol, 0, 0, thin);
        }
    }

    // value next to the mouse while dragging
    if (dragging && g.moved) {
        char buf[96];
        const float3& v = g.value;
        if (m_tool == TOOL_MOVE) {
            if (g.handle <= MH_Z) snprintf(buf, sizeof buf, "%+.2f", v[g.handle]);
            else snprintf(buf, sizeof buf, "%+.2f  %+.2f  %+.2f", v.x, v.y, v.z);
        } else if (m_tool == TOOL_ROTATE) {
            if (g.handle == MH_FREE) snprintf(buf, sizeof buf, "free");
            else snprintf(buf, sizeof buf, "%+.1f°", v.x);
        } else snprintf(buf, sizeof buf, "x%.2f", g.handle == MH_Y ? v.y : (g.handle == MH_Z ? v.z : v.x));
        ImVec2 m(g.prevMouse.x, g.prevMouse.y);
        ImVec2 ts = ImGui::CalcTextSize(buf);
        ImVec2 a(m.x + 16 * d, m.y + 14 * d);
        dl->AddRectFilled(ImVec2(a.x - 4 * d, a.y - 2 * d), ImVec2(a.x + ts.x + 4 * d, a.y + ts.y + 2 * d), IM_COL32(20, 22, 26, 190), 3 * d);
        dl->AddText(a, IM_COL32(240, 240, 235, 255), buf);
    }
}

// --------------------------------------------------------------------------- marking menu

namespace {
struct MMItem {
    int dir;   // 0 N, 1 NE, 2 E, 3 SE, 4 S, 5 SW, 6 W, 7 NW
    std::string label;
    bool checked;
    std::function<void()> run;
};
}

static std::vector<MMItem> markingItems(App& app, int tool, int& axisMode, bool& magnet, bool& absolute, float& stepMove, float& stepRot,
                                        float& stepScale, const std::function<void(int)>& selectOp, const std::function<void(int)>& resetOp) {
    std::vector<MMItem> v;
    char buf[64];
    if (tool == TOOL_SELECT) {
        v.push_back({0, "Select All", false, [=] { selectOp(0); }});
        v.push_back({2, "Invert Selection", false, [=] { selectOp(2); }});
        v.push_back({4, "Deselect", false, [=] { selectOp(1); }});
        v.push_back({6, "Select Hierarchy", false, [=] { selectOp(3); }});
        return v;
    }
    if (tool != TOOL_SCALE) {
        v.push_back({0, "World", axisMode == 0, [&axisMode] { axisMode = 0; }});
        v.push_back({1, "Object", axisMode == 1, [&axisMode] { axisMode = 1; }});
        v.push_back({7, "Parent", axisMode == 2, [&axisMode] { axisMode = 2; }});
    } else v.push_back({0, "Object axes", true, [] {}});
    v.push_back({4, "Snapping (always)", magnet, [&magnet] { magnet = !magnet; }});
    if (tool == TOOL_MOVE) {
        snprintf(buf, sizeof buf, "Step %g", stepMove);
        v.push_back({2, buf, false, [&stepMove] { stepMove = stepMove >= 1.0f ? 0.5f : stepMove >= 0.5f ? 0.25f : stepMove >= 0.25f ? 0.1f : 1.0f; }});
        v.push_back({5, "Snap to grid (absolute)", absolute, [&absolute] { absolute = !absolute; }});
        v.push_back({3, "Reset Position", false, [=] { resetOp(0); }});
    } else if (tool == TOOL_ROTATE) {
        snprintf(buf, sizeof buf, "Step %g°", stepRot);
        v.push_back({2, buf, false, [&stepRot] { stepRot = stepRot <= 5.0f ? 15.0f : stepRot <= 15.0f ? 45.0f : stepRot <= 45.0f ? 90.0f : 5.0f; }});
        v.push_back({3, "Reset Rotation", false, [=] { resetOp(1); }});
    } else {
        snprintf(buf, sizeof buf, "Step %g", stepScale);
        v.push_back({2, buf, false, [&stepScale] { stepScale = stepScale <= 0.1f ? 0.25f : stepScale <= 0.25f ? 0.5f : 0.1f; }});
        v.push_back({3, "Reset Scale", false, [=] { resetOp(2); }});
    }
    (void)app;
    return v;
}

static int mmDirOf(float dx, float dy) {
    float a = std::atan2(dx, -dy);   // 0 = up, clockwise
    int d = (int)std::lround(a / (kPi / 4));
    return (d + 8) % 8;
}

void App::markingSelect(int op) {
    std::vector<int> v;
    auto pickable = [&](const Object& o) { return o.visible && o.selectable && !o.ghost && !o.group && o.part != "GROUND" && o.part != "WALL"; };
    std::vector<int> cur = selectionAll();
    if (op == 0 || op == 2) {
        for (size_t i = 0; i < m_scene.objects.size(); ++i)
            if (pickable(m_scene.objects[i]) && (op == 0 || std::find(cur.begin(), cur.end(), (int)i) == cur.end())) v.push_back((int)i);
    } else if (op == 3) {
        v = cur;
        for (int i : cur) {
            std::vector<NodeRef> d;
            nodeDescendants(m_scene.objects[i].id, d);
            for (const NodeRef& n : d)
                if (n.kind == NODE_OBJECT && m_scene.objects[n.index].selectable && std::find(v.begin(), v.end(), n.index) == v.end()) v.push_back(n.index);
        }
    }
    select(-1);
    if (!v.empty()) {
        select(v[0]);
        m_multi.assign(v.begin() + 1, v.end());
        markDirty();
    }
    status(std::to_wstring(v.size()) + L" objects selected");
}

void App::markingReset(int what) {
    std::vector<int> sel = gizmoTargets();
    if (sel.empty()) { status(L"Nothing selected", 2); return; }
    pushUndo();
    for (int i : sel) {
        Object& o = m_scene.objects[i];
        if (what == 0) o.position = float3(0, 0, 0);
        else if (what == 1) { o.rot = qIdentity(); o.useRot = true; o.yaw = 0; }
        else o.scale = float3(1, 1, 1);
    }
    markDirty();
}

bool App::markingMenuInput(bool inView) {
    ImGuiIO& io = ImGui::GetIO();
    MarkingMenu& mm = m_mm;
    if (m_gzDemo) return mm.open;
    if (!mm.open) {
        if (!inView || io.KeyAlt || io.KeyCtrl || io.WantTextInput || !ImGui::IsMouseClicked(0)) return false;
        const ImGuiKey keys[4] = {ImGuiKey_Q, ImGuiKey_W, ImGuiKey_E, ImGuiKey_R};
        int tool = -1;
        for (int t = 0; t < 4; ++t) if (ImGui::IsKeyDown(keys[t])) tool = t;
        if (tool < 0) return false;
        if (tool == TOOL_SCALE && m_game && m_game->placementMode()) return false;
        m_tool = (Tool)tool;
        mm.open = true;
        mm.tool = tool;
        mm.x = io.MousePos.x;
        mm.y = io.MousePos.y;
        mm.hover = -1;
        return true;
    }
    mm.mx = io.MousePos.x;
    mm.my = io.MousePos.y;
    float dx = mm.mx - mm.x, dy = mm.my - mm.y;
    mm.hover = std::hypot(dx, dy) > 22.0f * m_dpi ? mmDirOf(dx, dy) : -1;
    if (!io.MouseDown[0]) {
        mm.open = false;
        if (mm.hover >= 0) {
            auto items = markingItems(*this, mm.tool, m_axisMode, m_magnet, m_snapAbsolute, m_stepMove, m_stepRotate, m_stepScale,
                                      [this](int op) { markingSelect(op); }, [this](int w) { markingReset(w); });
            for (auto& it : items)
                if (it.dir == mm.hover) { it.run(); status(L"Marking menu: " + std::wstring(it.label.begin(), it.label.end())); break; }
        }
    }
    return true;
}

void App::drawMarkingMenu(ImDrawList* dl) {
    if (!m_mm.open) return;
    const float d = m_dpi;
    ImVec2 c(m_mm.x, m_mm.y);
    auto items = markingItems(*this, m_mm.tool, m_axisMode, m_magnet, m_snapAbsolute, m_stepMove, m_stepRotate, m_stepScale, [](int) {}, [](int) {});
    static const char* names[4] = {"Select", "Move", "Rotate", "Scale"};
    ImVec2 m(m_mm.mx, m_mm.my);
    dl->AddCircleFilled(c, 5 * d, IM_COL32(236, 236, 226, 200), 20);
    dl->AddCircle(c, 22 * d, IM_COL32(236, 236, 226, 70), 40, 1.0f * d);
    if (m_mm.hover >= 0) dl->AddLine(c, m, IM_COL32(255, 226, 102, 200), 1.2f * d);
    ImVec2 ts = ImGui::CalcTextSize(names[m_mm.tool]);
    dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y + 26 * d), IM_COL32(236, 236, 226, 160), names[m_mm.tool]);
    for (const MMItem& it : items) {
        float a = it.dir * kPi / 4;
        float rad = 92.0f * d;
        ImVec2 p(c.x + std::sin(a) * rad, c.y - std::cos(a) * rad * 0.82f);
        std::string label = (it.checked ? "\xE2\x80\xA2 " : "") + it.label;   // bullet on the active option
        ImVec2 sz = ImGui::CalcTextSize(label.c_str());
        float pw = sz.x + 18 * d, ph = sz.y + 8 * d;
        // items on the left / right grow away from the centre
        float x0 = std::sin(a) > 0.3f ? p.x - 8 * d : std::sin(a) < -0.3f ? p.x - pw + 8 * d : p.x - pw * 0.5f;
        ImVec2 b0(x0, p.y - ph * 0.5f), b1(x0 + pw, p.y + ph * 0.5f);
        bool hot = m_mm.hover == it.dir;
        dl->AddRectFilled(b0, b1, hot ? IM_COL32(255, 226, 102, 240) : IM_COL32(34, 36, 42, 235), ph * 0.5f);
        dl->AddRect(b0, b1, hot ? IM_COL32(255, 226, 102, 255) : IM_COL32(110, 114, 124, 200), ph * 0.5f, 0, 1.0f * d);
        dl->AddText(ImVec2(b0.x + 9 * d, b0.y + 4 * d), hot ? IM_COL32(28, 28, 30, 255) : IM_COL32(228, 228, 232, 255), label.c_str());
    }
}

#include "ManipulatorsTest.inl"

} // namespace app
