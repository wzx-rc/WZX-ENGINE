// Editor tools of the PC viewport: move / rotate manipulators (thin arrows and rings on the origin of the
// selected object), selection rectangle, top bar tool icons and the remembered preferences.
#include "App.h"
#include <fstream>
#include <sstream>

namespace app {

static const Color kAxisColor[3] = {Color::hex(0xE5484D), Color::hex(0x46A758), Color::hex(0x3E8EF7)};   // X red / Y green / Z blue

static float3 axisVec(int a) { return float3(a == 0 ? 1.0f : 0.0f, a == 1 ? 1.0f : 0.0f, a == 2 ? 1.0f : 0.0f); }

static float distSeg(float px, float py, float2 a, float2 b) {
    float dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
    float t = l2 > 1e-6f ? clampf(((px - a.x) * dx + (py - a.y) * dy) / l2, 0, 1) : 0;
    float qx = a.x + dx * t - px, qy = a.y + dy * t - py;
    return std::sqrt(qx * qx + qy * qy);
}

// ring of an axis: two unit vectors spanning its plane
static void ringBasis(int axis, float3& u, float3& v) {
    if (axis == 0) { u = float3(0, 1, 0); v = float3(0, 0, 1); }
    else if (axis == 1) { u = float3(0, 0, 1); v = float3(1, 0, 0); }
    else { u = float3(1, 0, 0); v = float3(0, 1, 0); }
}

static const int kRingSeg = 64;
static const float kRingR = 0.78f, kArrowTip = 1.3f, kArrowBase = 1.0f;

float App::gizmoScale(const float3& pivot) const {
    float3 d = pivot - m_scene.camera.eye();
    return length(d) * 0.07f;
}

static bool gizmoShown(int tool, int selected) {
    return (tool == TOOL_MOVE || tool == TOOL_ROTATE) && selected >= 0;
}

bool App::gizmoHit(float mx, float my, int& axis, bool& rotate) {
    if (!gizmoShown(m_tool, m_selected)) return false;
    const Object& o = m_scene.objects[m_selected];
    float3 pv = o.position;
    float s = gizmoScale(pv);
    rotate = m_tool == TOOL_ROTATE;
    float best = rotate ? 9.0f : 11.0f;
    axis = -1;
    for (int a = 0; a < 3; ++a) {
        float3 av = axisVec(a);
        float d = 1e30f;
        if (!rotate) {
            float2 p0, p1;
            if (project(pv, p0) && project(pv + av * (kArrowTip * s), p1)) d = distSeg(mx, my, p0, p1);
        } else {
            float3 u, v;
            ringBasis(a, u, v);
            float2 prev;
            bool hasPrev = false;
            for (int i = 0; i <= kRingSeg; ++i) {
                float ang = i * 2 * kPi / kRingSeg;
                float2 p;
                bool ok = project(pv + (u * std::cos(ang) + v * std::sin(ang)) * (kRingR * s), p);
                if (ok && hasPrev) d = std::min(d, distSeg(mx, my, prev, p));
                prev = p;
                hasPrev = ok;
            }
        }
        if (d < best) { best = d; axis = a; }
    }
    return axis >= 0;
}

// closest parameter along the axis line (through o, direction a) to the current mouse ray
static float axisParam(const float3& o, const float3& a, const float3& ro, const float3& rd) {
    float3 w = o - ro;
    float b = dot(a, rd), d = dot(a, w), e = dot(rd, w);
    float den = 1 - b * b;
    return std::fabs(den) < 1e-6f ? 0.0f : (b * e - d) / den;
}

bool App::gizmoInput(bool inView) {
    UI& ui = m_ui;
    if (!gizmoShown(m_tool, m_selected)) {
        m_gz.active = false;
        m_gzHover = -1;
        return false;
    }
    float3 ro, rd;
    cameraRay(ui.mx, ui.my, ro, rd);
    if (m_gz.active) {
        GizmoDrag& g = m_gz;
        if (!ui.mouseDown[0] || g.obj != m_selected) {
            g.active = false;
            if (!g.moved && !m_undo.empty()) m_undo.pop_back();
            return true;
        }
        Object& o = m_scene.objects[g.obj];
        float3 a = axisVec(g.axis);
        if (!g.rotate) {
            float d = axisParam(g.pivot, a, ro, rd) - g.t0;
            if (m_magnet) d = std::round(d);
            float3 np = g.startPos + a * d;
            float3 mn, mx;
            aabbOf(o, float3(0, 0, 0), objRot(o), mn, mx);
            if (np.y + mn.y < 0) np.y = -mn.y;
            if (np.x != o.position.x || np.y != o.position.y || np.z != o.position.z) {
                float3 delta = np - g.startPos;
                o.position = np;
                for (auto& ot : g.others) m_scene.objects[ot.obj].position = ot.pos + delta;
                g.moved = true;
                markDirty();
            }
        } else {
            float den = dot(rd, a);
            if (std::fabs(den) > 1e-5f) {
                float t = dot(g.pivot - ro, a) / den;
                float3 v = ro + rd * t - g.pivot;
                float ang = std::atan2(dot(a, cross(g.v0, v)), dot(g.v0, v));
                int steps = (int)std::lround(ang / (kPi * 0.5f));
                if (steps != g.steps) {
                    g.steps = steps;
                    float4 dq = qAxisAngle(a, steps * kPi * 0.5f);
                    o.rot = qSnap90(qMul(dq, g.startRot));
                    o.position = g.pivot + qRot(dq, g.startPos - g.pivot);
                    for (auto& ot : g.others) {
                        Object& oo = m_scene.objects[ot.obj];
                        oo.rot = qSnap90(qMul(dq, ot.rot));
                        oo.position = g.pivot + qRot(dq, ot.pos - g.pivot);
                    }
                    g.moved = true;
                    markDirty();
                }
            }
        }
        return true;
    }
    int axis;
    bool rotate;
    bool hit = inView && gizmoHit(ui.mx, ui.my, axis, rotate);
    m_gzHover = hit ? axis : -1;
    if (hit && ui.mousePressed[0]) {
        pushUndo();
        Object& o = m_scene.objects[m_selected];
        ensureRot(o);
        GizmoDrag& g = m_gz;
        g = GizmoDrag();
        g.active = true;
        g.axis = axis;
        g.rotate = rotate;
        g.obj = m_selected;
        g.startPos = o.position;
        g.startRot = o.rot;
        g.pivot = o.position;
        for (int oi : m_multi)
            if (oi >= 0 && oi < (int)m_scene.objects.size()) {
                Object& oo = m_scene.objects[oi];
                ensureRot(oo);
                g.others.push_back({oi, oo.position, oo.rot});
            }
        float3 a = axisVec(axis);
        if (!rotate) g.t0 = axisParam(g.pivot, a, ro, rd);
        else {
            float den = dot(rd, a);
            float t = std::fabs(den) > 1e-5f ? dot(g.pivot - ro, a) / den : 0.0f;
            g.v0 = ro + rd * t - g.pivot;
        }
        return true;
    }
    return false;
}

void App::drawGizmo() {
    if (!gizmoShown(m_tool, m_selected)) return;
    UI& ui = m_ui;
    const Object& o = m_scene.objects[m_selected];
    float3 pv = o.position;
    float s = gizmoScale(pv);
    bool rotate = m_tool == TOOL_ROTATE;
    float2 pc;
    if (!project(pv, pc)) return;
    for (int a = 0; a < 3; ++a) {
        bool lit = (m_gz.active && m_gz.axis == a) || (!m_gz.active && m_gzHover == a);
        Color c = lit ? Color::rgbf(1, 1, 1, 1) : kAxisColor[a].withAlpha(rotate ? 0.85f : 0.95f);
        float3 av = axisVec(a);
        if (!rotate) {
            float2 pb, pt;
            if (!project(pv + av * (kArrowBase * s), pb) || !project(pv + av * (kArrowTip * s), pt)) continue;
            ui.line(pc.x, pc.y, pb.x, pb.y, c, lit ? 3.0f : 2.2f);
            float dx = pt.x - pb.x, dy = pt.y - pb.y, l = std::sqrt(dx * dx + dy * dy);
            if (l < 1e-3f) continue;
            dx /= l; dy /= l;
            float hw = lit ? 6.5f : 5.5f;
            float2 tri[3] = {float2(pt.x, pt.y), float2(pb.x - dy * hw, pb.y + dx * hw), float2(pb.x + dy * hw, pb.y - dx * hw)};
            ui.polygon(tri, 3, c);
        } else {
            float3 u, v;
            ringBasis(a, u, v);
            float2 prev;
            bool hasPrev = false;
            for (int i = 0; i <= kRingSeg; ++i) {
                float ang = i * 2 * kPi / kRingSeg;
                float2 p;
                bool ok = project(pv + (u * std::cos(ang) + v * std::sin(ang)) * (kRingR * s), p);
                if (ok && hasPrev) ui.line(prev.x, prev.y, p.x, p.y, c, lit ? 3.0f : 2.0f);
                prev = p;
                hasPrev = ok;
            }
        }
    }
    ui.circle(pc.x, pc.y, 3.5f, Color::hex(0x0F1013, 0.7f));
    ui.circle(pc.x, pc.y, 2.2f, Color::rgbf(1, 1, 1, 0.95f));
}

// Gizmo + selection rectangle, drawn over the viewport.
void App::drawSelectionOverlay() {
    UI& ui = m_ui;
    ui.pushClip(m_rcView);
    drawGizmo();
    if (m_marq.active && m_marq.moved) {
        Color acc = ui.theme.accent;
        Rect r(std::min(m_marq.x0, ui.mx), std::min(m_marq.y0, ui.my), std::fabs(ui.mx - m_marq.x0), std::fabs(ui.my - m_marq.y0));
        ui.fill(r, acc.withAlpha(0.10f));
        ui.stroke(r, acc.withAlpha(0.9f), 0, 1.5f);
    }
    ui.popClip();
}

// --------------------------------------------------------------------------- selection rectangle (select tool)

void App::marqueeInput(bool inView) {
    (void)inView;
    UI& ui = m_ui;
    if (!m_marq.active) return;
    if (ui.mouseDown[0]) {
        if (std::hypot(ui.mx - m_marq.x0, ui.my - m_marq.y0) > 5.0f) m_marq.moved = true;
        return;
    }
    m_marq.active = false;
    bool shift = ui.keyDown(VK_SHIFT);
    if (!m_marq.moved) { if (!shift) select(-1); return; }
    Rect r(std::min(m_marq.x0, ui.mx), std::min(m_marq.y0, ui.my), std::fabs(ui.mx - m_marq.x0), std::fabs(ui.my - m_marq.y0));
    std::vector<int> hit;
    for (size_t i = 0; i < m_scene.objects.size(); ++i) {
        const Object& o = m_scene.objects[i];
        if (!o.visible || !o.selectable || o.ghost || o.part == "GROUND" || o.part == "WALL") continue;
        float3 mn, mx;
        aabbOf(o, o.position, objRot(o), mn, mx);
        float2 s;
        if (project((mn + mx) * 0.5f, s) && r.contains(s.x, s.y)) hit.push_back((int)i);
    }
    if (shift)
        for (int i : selectionAll())
            if (std::find(hit.begin(), hit.end(), i) == hit.end()) hit.push_back(i);
    if (hit.empty()) { if (!shift) select(-1); return; }
    m_selected = hit[0];
    m_multi.assign(hit.begin() + 1, hit.end());
    markDirty();
    wchar_t b[64];
    swprintf(b, 64, L"%zu objets sélectionnés", hit.size());
    m_status = b;
    m_statusTime = GetTickCount();
}

// --------------------------------------------------------------------------- preferences

static std::string prefsString(bool magnet, int tool, const std::string& panels) {
    std::ostringstream o;
    o << "magnet=" << (int)magnet << "\ntool=" << tool << "\n" << panels;
    return o.str();
}

std::string App::panelsString() const {
    std::ostringstream o;
    for (int i = 0; i < 3; ++i)
        o << "pan" << i << "=" << (int)m_pan[i].visible << "," << (int)m_pan[i].collapsed << "," << (int)m_pan[i].floating << "," << m_pan[i].dock << ","
          << (int)m_pan[i].fr.x << "," << (int)m_pan[i].fr.y << "\n";
    return o.str();
}

void App::loadPrefs() {
    std::ifstream f(m_exeDir + L"\\vp3d_prefs.txt");
    std::string line;
    while (std::getline(f, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        int v = atoi(line.c_str() + eq + 1);
        if (k == "magnet") m_magnet = v != 0;
        else if (k == "tool" && v >= TOOL_MOVE && v <= TOOL_ROTATE) m_lastTool = (Tool)v;
        else if (k.rfind("pan", 0) == 0 && k.size() == 4 && k[3] >= '0' && k[3] <= '2') {
            int vis = 1, col = 0, fl = 0, dock = 0;
            float x = 0, y = 0;
            if (sscanf_s(line.c_str() + eq + 1, "%d,%d,%d,%d,%f,%f", &vis, &col, &fl, &dock, &x, &y) == 6) {
                PanelState& P = m_pan[k[3] - '0'];
                P.visible = vis != 0; P.collapsed = col != 0; P.floating = fl != 0; P.dock = dock == 1 ? 1 : 0;
                P.fr = Rect(x, y, 0, 0);
            }
        }
    }
    m_lastPrefs = prefsString(m_magnet, (int)m_lastTool, panelsString());
}

// Every setting change is remembered on this device.
void App::savePrefs() {
    if (m_gripActive >= 0) return;   // not while a panel is being dragged
    if (m_tool != TOOL_SELECT) m_lastTool = m_tool;
    std::string s = prefsString(m_magnet, (int)m_lastTool, panelsString());
    if (s == m_lastPrefs) return;
    m_lastPrefs = s;
    std::ofstream f(m_exeDir + L"\\vp3d_prefs.txt", std::ios::binary);
    if (f) f << s;
}

// --------------------------------------------------------------------------- top bar tool icons

// kind: 1 move, 2 rotate, 3 grid
bool App::toolIcon(const Rect& r, int kind, bool active, const wchar_t* tip) {
    UI& ui = m_ui;
    const Theme& th = ui.theme;
    bool h = ui.hover(r);
    if (active) { ui.fill(r, th.accent.withAlpha(0.16f), 7); ui.stroke(r, th.accent.withAlpha(0.55f), 7); }
    else if (h) ui.fill(r, th.cardHover, 7);
    Color c = active ? th.accent : (h ? th.text : th.textDim);
    float cx = r.cx(), cy = r.cy(), w = 2.0f;
    switch (kind) {
    case 1: {   // move: cross with arrow heads
        float a = 9;
        ui.line(cx - a, cy, cx + a, cy, c, w);
        ui.line(cx, cy - a, cx, cy + a, c, w);
        const float dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
        for (int i = 0; i < 4; ++i) {
            float tx = cx + dx[i] * a, ty = cy + dy[i] * a;
            float2 tri[3] = {float2(tx, ty), float2(tx - dx[i] * 4 - dy[i] * 3.2f, ty - dy[i] * 4 + dx[i] * 3.2f),
                             float2(tx - dx[i] * 4 + dy[i] * 3.2f, ty - dy[i] * 4 - dx[i] * 3.2f)};
            ui.polygon(tri, 3, c);
        }
        break;
    }
    case 2: {   // rotate: arc + arrow head
        float R = 8;
        float2 prev;
        for (int i = 0; i <= 18; ++i) {
            float ang = radians(40.0f + i * 15.0f);
            float2 p(cx + R * std::cos(ang), cy - R * std::sin(ang));
            if (i) ui.line(prev.x, prev.y, p.x, p.y, c, w);
            prev = p;
        }
        float ang = radians(40.0f);
        float2 tip(cx + R * std::cos(ang), cy - R * std::sin(ang));
        float2 tri[3] = {float2(tip.x + 5.5f, tip.y - 1.0f), float2(tip.x - 3.5f, tip.y - 5.0f), float2(tip.x - 2.0f, tip.y + 4.5f)};
        ui.polygon(tri, 3, c);
        break;
    }
    case 3: {   // grid
        float a = 8;
        ui.stroke(Rect(cx - a, cy - a, 2 * a, 2 * a), c, 2, 1.6f);
        ui.line(cx - a, cy - a / 3, cx + a, cy - a / 3, c, 1.4f);
        ui.line(cx - a, cy + a / 3, cx + a, cy + a / 3, c, 1.4f);
        ui.line(cx - a / 3, cy - a, cx - a / 3, cy + a, c, 1.4f);
        ui.line(cx + a / 3, cy - a, cx + a / 3, cy + a, c, 1.4f);
        break;
    }
    default: break;
    }
    if (h && tip) ui.tooltip(tip);
    return ui.clicked(r);
}

} // namespace app
