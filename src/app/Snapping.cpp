// Snapping, placement and pivot:
//   - absolute snapping ("Snap to the grid"): a move lands on the points of the grid of the chosen kind, the
//     intersections of the lines, the centres of the cells or the middles of their edges (SnapOptions::gridPoint), on
//     the cells of the editor grid (or the move step); these points are drawn around the moved object;
//   - V held during a move: the pivot goes to the vertex nearest to the cursor (any visible object);
//   - Place (button of the header): a selected object dragged with the left button slides over the surfaces of the
//     scene (the ground at y = 0 where there is none), resting on them, turned to them if asked; the grid snapping
//     applies on flat ground;
//   - Edit Pivot (C or Insert, a switch): the move manipulator moves the pivot of the objects, their
//     geometry stays (Object::pivot); Center Pivot (Alt+Y): the pivot at the centre of the geometry.
#include "App.h"
#include "GameHost.h"
#include <set>

namespace app {

namespace {

float4 qConj(const float4& q) { return float4(-q.x, -q.y, -q.z, q.w); }

// the shortest rotation from the unit vector a to the unit vector b
float4 qFromTo(const float3& a, const float3& b) {
    const float c = dot(a, b);
    if (c < -0.9999f) {   // opposite: half a turn around any perpendicular axis
        const float3 p = std::fabs(a.x) < 0.9f ? normalize(cross(a, float3(1, 0, 0))) : normalize(cross(a, float3(0, 1, 0)));
        return float4(p.x, p.y, p.z, 0.0f);
    }
    const float3 x = cross(a, b);
    return qNormalize(float4(x.x, x.y, x.z, 1.0f + c));
}

} // namespace

// --------------------------------------------------------------------------- grid points

// The cells of the absolute snapping: the lines of the editor grid (subdivisions included when they are drawn), else
// the move step.
float App::snapCell() const {
    const SnapOptions& o = th::settings().snap;
    if (o.gridStep && m_rs.editorGrid > 0) {
        const GridOptions& g = th::settings().grid;
        return m_rs.editorGrid / (float)(g.subdivLines ? std::max(1, g.subdivisions) : 1);
    }
    return std::max(m_stepMove, 1e-4f);
}

// The grid point the absolute snapping takes for p, on the axes `on` only: on the ground plane (X, Z) the
// intersections, the centres of the cells or the middles of their edges; the height on the lines.
float3 App::snapGridPoint(const float3& p, const bool on[3]) const {
    const float s = snapCell();
    auto line = [&](float v) { return std::round(v / s) * s; };
    auto mid = [&](float v) { return (std::floor(v / s) + 0.5f) * s; };
    float3 q = p;
    if (on[1]) q.y = line(p.y);
    const int mode = th::settings().snap.gridPoint;
    if (mode == 0) {
        if (on[0]) q.x = line(p.x);
        if (on[2]) q.z = line(p.z);
    } else if (mode == 1) {
        if (on[0]) q.x = mid(p.x);
        if (on[2]) q.z = mid(p.z);
    } else if (on[0] && on[2]) {   // middles of the edges: one coordinate on a line, the other halfway
        const float ax = mid(p.x), az = line(p.z), bx = line(p.x), bz = mid(p.z);
        const bool first = (ax - p.x) * (ax - p.x) + (az - p.z) * (az - p.z) <= (bx - p.x) * (bx - p.x) + (bz - p.z) * (bz - p.z);
        q.x = first ? ax : bx;
        q.z = first ? az : bz;
    } else {   // along one axis: halfway when the other coordinate lies on a line, else on the lines
        if (on[0]) q.x = std::fabs(p.z - line(p.z)) < s * 0.01f ? mid(p.x) : line(p.x);
        if (on[2]) q.z = std::fabs(p.x - line(p.x)) < s * 0.01f ? mid(p.z) : line(p.z);
    }
    return q;
}

// The vertex of a visible object nearest to the cursor on screen, within maxPx (the objects of `skip` left out).
bool App::nearestVertex(float mx, float my, const std::vector<int>& skip, float maxPx, float3& out) const {
    float best = maxPx * m_dpi;
    bool found = false;
    size_t budget = 600000;
    for (size_t i = 0; i < m_scene.objects.size() && budget > 0; ++i) {
        const Object& o = m_scene.objects[i];
        if (!o.visible || o.ghost || o.mesh < 0 || o.mesh >= (int)m_scene.meshes.size()) continue;
        if (std::find(skip.begin(), skip.end(), (int)i) != skip.end()) continue;
        const Affine T = o.transform();
        for (const Vertex& v : m_scene.meshes[o.mesh].vertices) {
            if (budget-- == 0) break;
            const float3 w = T.point(v.pos);
            float2 s;
            if (!project(w, s)) continue;
            const float d = std::hypot(s.x - mx, s.y - my);
            if (d < best) { best = d; out = w; found = true; }
        }
    }
    return found;
}

// The snap points of the grid around the moved object (absolute snapping on, a move manipulator shown or a drag):
// small dots at the height of the pivot (the points it lands on), fading with the distance.
void App::drawSnapPoints(ImDrawList* dl) {
    m_snapShown = false;
    const SnapOptions& so = th::settings().snap;
    const ImGuiIO& io = ImGui::GetIO();
    const bool moving = (gizmoShown() && m_tool == TOOL_MOVE) || m_sd.active;
    if (!so.showPoints || !m_snapAbsolute || !moving || m_selected < 0 || !(m_magnet || io.KeyShift)) {
        if (m_snapTargetOn && !m_gz.active && !m_sd.active) m_snapTargetOn = false;
        return;
    }
    const float s = snapCell();
    const float3 c = m_scene.objects[m_selected].position;
    const int n = 4, mode = so.gridPoint;
    const float cx = std::round(c.x / s), cz = std::round(c.z / s);
    // the boxes of the visible objects hide the points behind them (under the selection too)
    std::vector<std::pair<float3, float3>> boxes;
    for (const Object& o : m_scene.objects) {
        if (!o.visible || o.ghost || o.group || o.mesh < 0 || o.part == "GROUND") continue;
        float3 a, b;
        aabbOf(o, o.position, objRot(o), a, b);
        boxes.push_back({a, b});
    }
    const Camera& cam = m_scene.camera;
    auto hidden = [&](const float3& p) {
        const float3 dir = cam.toViewer(p);
        const float tp = cam.ortho ? 1e6f : length(cam.eye() - p);
        for (const auto& bx : boxes) {
            float t0 = 1e-4f, t1 = tp;
            bool miss = false;
            for (int k = 0; k < 3 && !miss; ++k) {
                if (std::fabs(dir[k]) < 1e-8f) { miss = p[k] < bx.first[k] || p[k] > bx.second[k]; continue; }
                float ta = (bx.first[k] - p[k]) / dir[k], tb = (bx.second[k] - p[k]) / dir[k];
                if (ta > tb) std::swap(ta, tb);
                t0 = std::max(t0, ta);
                t1 = std::min(t1, tb);
                miss = t0 > t1;
            }
            if (!miss) return true;
        }
        return false;
    };
    auto dot2 = [&](float x, float z) {
        float2 p;
        const float dd = std::hypot(x - c.x, z - c.z) / (s * (n + 0.5f));
        const float3 w(x, c.y, z);   // (at the height of the pivot: the points it lands on)
        if (dd > 1.0f || !project(w, p) || hidden(w)) return;
        const int a = (int)(220 * (1.0f - dd * dd));
        dl->AddCircleFilled(ImVec2(p.x, p.y), 1.1f * m_dpi, IM_COL32(120, 220, 255, a), 8);
        m_snapShown = true;
    };
    for (int i = -n; i <= n; ++i)
        for (int j = -n; j <= n; ++j) {
            const float x = (cx + i) * s, z = (cz + j) * s;
            if (mode == 0) dot2(x, z);
            else if (mode == 1) dot2(x + 0.5f * s, z + 0.5f * s);
            else {
                dot2(x + 0.5f * s, z);
                dot2(x, z + 0.5f * s);
            }
        }
}

// The Snapping button: snapping always on / off; with Shift, its options.
void App::toggleSnap() {
    if (ImGui::GetIO().KeyShift) {
        m_showSnapOptions = true;
        return;
    }
    m_magnet = !m_magnet;
}

// --------------------------------------------------------------------------- placement on the surfaces

// Place on: a press on an object selects it (unless it is in the selection) and drags the selection over the
// surfaces until the button is released.
bool App::surfacePlaceInput(bool inView, bool click) {
    ImGuiIO& io = ImGui::GetIO();
    if (m_sd.active) {
        if (!io.MouseDown[0]) {
            m_sd.active = false;
            m_snapTargetOn = false;
            if (!m_sd.moved && !m_undo.empty()) m_undo.pop_back();
            return true;
        }
        surfacePlaceDrag(io.MousePos.x, io.MousePos.y);
        return true;
    }
    if (!m_surfacePlace || !inView || !click || io.KeyAlt || io.KeyShift || io.KeyCtrl) return false;
    float3 o, d;
    cameraRay(io.MousePos.x, io.MousePos.y, o, d);
    const PickResult pr = pick(m_scene, o, d);
    if (pr.object < 0) return false;
    std::vector<int> sel = selectionAll();
    if (std::find(sel.begin(), sel.end(), pr.object) == sel.end()) select(pr.object);
    pushUndo();
    m_sd = SurfaceDrag();
    m_sd.active = true;
    m_sd.objs = gizmoTargets();
    m_sd.lead = std::find(m_sd.objs.begin(), m_sd.objs.end(), pr.object) != m_sd.objs.end() ? pr.object : m_selected;
    const float3 lp = m_scene.objects[m_sd.lead].position;
    for (int i : m_sd.objs) {
        m_sd.offs.push_back(m_scene.objects[i].position - lp);
        m_sd.rots.push_back(objRot(m_scene.objects[i]));
    }
    return true;
}

void App::surfacePlaceDrag(float mx, float my) {
    if (m_sd.lead < 0 || m_sd.lead >= (int)m_scene.objects.size()) return;
    float3 ro, rd;
    cameraRay(mx, my, ro, rd);
    // the surface under the cursor: every visible object but the dragged ones and their children (locked ones too)
    std::set<int> dragged(m_sd.objs.begin(), m_sd.objs.end());
    for (int i : m_sd.objs) {
        std::vector<NodeRef> kids;
        nodeDescendants(m_scene.objects[i].id, kids);
        for (const NodeRef& k : kids)
            if (k.kind == NODE_OBJECT) dragged.insert(k.index);
    }
    std::vector<std::pair<bool, bool>> saved(m_scene.objects.size());
    for (size_t i = 0; i < m_scene.objects.size(); ++i) {
        Object& o = m_scene.objects[i];
        saved[i] = {o.visible, o.selectable};
        if (dragged.count((int)i)) o.visible = false;
        else o.selectable = true;
    }
    const PickResult pr = pick(m_scene, ro, rd);
    for (size_t i = 0; i < m_scene.objects.size(); ++i) {
        m_scene.objects[i].visible = saved[i].first;
        m_scene.objects[i].selectable = saved[i].second;
    }
    float3 hit, n(0, 1, 0);
    if (pr.object >= 0) {
        hit = pr.position;
        n = normalize(pr.normal);
        if (dot(n, rd) > 0) n = n * -1.0f;
    } else if (rd.y < -1e-5f) hit = ro + rd * (-ro.y / rd.y);   // no surface: the ground
    else return;
    // the grid snapping on a flat floor
    const ImGuiIO& io = ImGui::GetIO();
    m_snapTargetOn = false;
    if ((m_magnet || io.KeyShift) && m_snapAbsolute && n.y > 0.99f) {
        const bool on[3] = {true, false, true};
        hit = snapGridPoint(hit, on);
        m_snapTarget = hit;
        m_snapTargetOn = true;
    }
    const size_t li = std::find(m_sd.objs.begin(), m_sd.objs.end(), m_sd.lead) - m_sd.objs.begin();
    Object& L = m_scene.objects[m_sd.lead];
    float4 rot = m_sd.rots[li];
    if (th::settings().snap.alignToSurface) rot = qNormalize(qMul(qFromTo(normalize(qRot(rot, float3(0, 1, 0))), n), rot));   // its up along the normal
    // resting on the surface: the lowest corner of its box, along the normal, on the hit point
    float lo = 0;
    if (L.mesh >= 0 && L.mesh < (int)m_scene.meshes.size()) {
        float3 a, b;
        m_scene.meshes[L.mesh].bounds(a, b);
        lo = 1e30f;
        for (int k = 0; k < 8; ++k) {
            const float3 corner((k & 1) ? b.x : a.x, (k & 2) ? b.y : a.y, (k & 4) ? b.z : a.z);
            lo = std::min(lo, dot(qRot(rot, (corner - L.pivot) * L.scale), n));
        }
    }
    const float3 np = hit - n * lo;
    for (size_t k = 0; k < m_sd.objs.size(); ++k) {
        Object& o = m_scene.objects[m_sd.objs[k]];
        o.position = np + m_sd.offs[k];
    }
    if (th::settings().snap.alignToSurface) {
        L.rot = rot;
        L.useRot = true;
    }
    m_sd.hit = hit;
    m_sd.normal = n;
    m_sd.moved = true;
    markDirty();
}

// --------------------------------------------------------------------------- pivot

// C or Insert: in and out of Edit Pivot (the move manipulator moves the pivot); Alt+Y: Center Pivot.
void App::pivotInput(bool allowed) {
    ImGuiIO& io = ImGui::GetIO();
    const bool keys = allowed && !io.WantTextInput;
    m_pivotHold = false;
    // C or Insert: a switch, in and out of Edit Pivot
    if (keys && !io.KeyCtrl && !io.KeyAlt && (ImGui::IsKeyPressed(ImGuiKey_C, false) || ImGui::IsKeyPressed(ImGuiKey_Insert, false))) m_pivotEdit = !m_pivotEdit;
    if (keys && io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_Y, false)) centerPivot();
    // the move manipulator while the pivot is edited, the tool of before after
    if (pivotEditing() && m_pivotTool < 0 && !m_gz.active) {
        m_pivotTool = m_tool;
        m_tool = TOOL_MOVE;
        setAdvScale(false);
    } else if (!pivotEditing() && m_pivotTool >= 0 && !m_gz.active) {
        m_tool = (Tool)m_pivotTool;
        m_pivotTool = -1;
    }
}

// Center Pivot: the pivot of each selected object at the centre of its geometry (the geometry does not move).
void App::centerPivot() {
    std::vector<int> sel = gizmoTargets();
    if (sel.empty()) { status(L"Select an object to center its pivot", 2); return; }
    pushUndo();
    int n = 0;
    for (int i : sel) {
        Object& o = m_scene.objects[i];
        if (o.mesh < 0 || o.mesh >= (int)m_scene.meshes.size()) continue;
        float3 a, b;
        m_scene.meshes[o.mesh].bounds(a, b);
        const float3 c = (a + b) * 0.5f;
        o.position = o.transform().point(c);
        o.pivot = c;
        hierRebase(o.id);
        ++n;
    }
    status(std::to_wstring(n) + L" pivot(s) centred", 1);
    markDirty();
}

} // namespace app
