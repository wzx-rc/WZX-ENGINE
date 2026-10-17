// Advanced Scale: bounding box scaling tool (after "Advanced Scale" by DDDtools for Unreal). A separate tool (Alt+T or
// the three-colour button): the standard manipulators are unchanged.
//   - one object: the box is oriented on the object (its local box); several objects: on the world, or on the object
//     picked with the eyedropper;
//   - face handles (centre of each face): one axis, the opposite face stays fixed;
//   - edge and corner handles (Ctrl held, or "Corner Handles" in the menu; they appear near the cursor): an edge scales
//     two axes in their plane, a corner the three along the diagonal of the screen; the opposite edge or corner stays
//     fixed;
//   - Shift: uniform scale; Alt: both sides (around the centre); V: the handle snaps to the vertex nearest to the cursor
//     (any object, even far away); Snapping on: the handle lands on the grid of the move step;
//   - flat objects (a side thinner than the flat threshold): faces on four sides only, corners of the rectangle;
//   - several objects, rotated and mirrored ones included: each one is scaled along its own axes and moved with the box
//     (its pivot is not changed).
#include "App.h"
#include "GameHost.h"
#include <map>

namespace app {

namespace {

float3 unitAxis(int a) { return float3(a == 0 ? 1.0f : 0.0f, a == 1 ? 1.0f : 0.0f, a == 2 ? 1.0f : 0.0f); }

void handleSides(int h, int s[3]) {
    s[0] = h / 9 - 1;
    s[1] = (h / 3) % 3 - 1;
    s[2] = h % 3 - 1;
}
int sideCount(const int s[3]) { return (s[0] != 0) + (s[1] != 0) + (s[2] != 0); }

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

ImU32 col32(const ImVec4& c, float alpha = 1.0f, float lighten = 0.0f) {
    ImVec4 v(c.x + (1 - c.x) * lighten, c.y + (1 - c.y) * lighten, c.z + (1 - c.z) * lighten, c.w * alpha);
    return ImGui::ColorConvertFloat4ToU32(v);
}

} // namespace

void App::setAdvScale(bool on) {
    if (on == m_advScale) return;
    m_advScale = on;
    m_adv = AdvDrag();
    m_advHover = -1;
    m_advPicking = false;
    if (!on) m_advRefId = 0;
    status(on ? L"Advanced Scale: on (Alt+T)" : L"Advanced Scale: off");
    markDirty();
}

// The box: one object, its own axes and local bounds; several objects (or a group), the world axes or those of the
// orientation object, around the corners of every mesh (the descendants of the groups included).
bool App::advScaleBox(AdvBox& b) {
    b = AdvBox();
    const std::vector<int> targets = gizmoTargets();
    if (targets.empty()) return false;
    std::vector<int> geo;
    for (int i : targets) {
        geo.push_back(i);
        std::vector<NodeRef> d;
        nodeDescendants(m_scene.objects[i].id, d);
        for (const NodeRef& n : d)
            if (n.kind == NODE_OBJECT) geo.push_back(n.index);
    }
    // local bounds of a mesh, cached (meshes change rarely)
    static std::map<int, std::pair<uint64_t, std::pair<float3, float3>>> cache;
    auto bounds = [&](int mesh, float3& mn, float3& mx) {
        if (mesh < 0 || mesh >= (int)m_scene.meshes.size() || m_scene.meshes[mesh].vertices.empty()) return false;
        const uint64_t key = ((uint64_t)m_scene.geometryVersion << 32) ^ (uint64_t)m_scene.meshes[mesh].vertices.size();
        auto it = cache.find(mesh);
        if (it == cache.end() || it->second.first != key) {
            float3 a, c;
            m_scene.meshes[mesh].bounds(a, c);
            cache[mesh] = {key, {a, c}};
            it = cache.find(mesh);
        }
        mn = it->second.second.first;
        mx = it->second.second.second;
        return true;
    };
    const Object* single = targets.size() == 1 && geo.size() == 1 ? &m_scene.objects[targets[0]] : nullptr;
    float3 lmn, lmx;
    if (single && bounds(single->mesh, lmn, lmx)) {
        const float4 q = objRot(*single);
        for (int k = 0; k < 3; ++k) b.ax[k] = normalize(qRot(q, unitAxis(k)));
        const float3 lh = (lmx - lmn) * 0.5f, sc = single->scale;
        b.h = float3(std::fabs(lh.x * sc.x), std::fabs(lh.y * sc.y), std::fabs(lh.z * sc.z));
        b.c = single->transform().point((lmn + lmx) * 0.5f);
    } else {
        float4 q = qIdentity();
        if (m_advRefId) {
            NodeRef r = findNode(m_advRefId);
            if (r.kind == NODE_OBJECT) q = objRot(m_scene.objects[r.index]);
        }
        for (int k = 0; k < 3; ++k) b.ax[k] = normalize(qRot(q, unitAxis(k)));
        float3 mn(1e30f), mx(-1e30f);
        bool any = false;
        for (int i : geo) {
            const Object& o = m_scene.objects[i];
            float3 a, c;
            const Affine T = o.transform();
            if (!bounds(o.mesh, a, c)) {
                if (o.mesh >= 0) continue;
                a = c = float3(0, 0, 0);   // group without geometry: its pivot
            }
            for (int k = 0; k < 8; ++k) {
                const float3 p = T.point(float3((k & 1) ? c.x : a.x, (k & 2) ? c.y : a.y, (k & 4) ? c.z : a.z));
                for (int ax = 0; ax < 3; ++ax) {
                    const float v = dot(p, b.ax[ax]);
                    mn[ax] = std::min(mn[ax], v);
                    mx[ax] = std::max(mx[ax], v);
                }
                any = true;
            }
        }
        if (!any) return false;
        b.c = b.ax[0] * ((mn.x + mx.x) * 0.5f) + b.ax[1] * ((mn.y + mx.y) * 0.5f) + b.ax[2] * ((mn.z + mx.z) * 0.5f);
        b.h = (mx - mn) * 0.5f;
    }
    // flat mode: the thinnest side under the threshold (relative to the largest one)
    const float big = std::max(b.h.x, std::max(b.h.y, b.h.z));
    const float th = th::settings().advScale.flatThreshold;
    int thin = 0;
    for (int k = 1; k < 3; ++k) if (b.h[k] < b.h[thin]) thin = k;
    if (big > 0 && b.h[thin] <= th * big) b.flatAxis = thin;
    for (int k = 0; k < 3; ++k) b.h[k] = std::max(b.h[k], 1e-4f);
    b.valid = true;
    return true;
}

// --------------------------------------------------------------------------- input

bool App::advScaleInput(bool inView) {
    ImGuiIO& io = ImGui::GetIO();
    const float mx = io.MousePos.x, my = io.MousePos.y;
    const bool playing = m_game && (m_game->playing() || m_game->playerMode());
    if (playing) { m_adv.active = false; return false; }
    if (m_gzDemo && m_adv.active) return true;   // captures (--open-menu advScale...): a drag frozen on screen
    // eyedropper: the next click picks the object giving the orientation of the box (the selection is kept)
    if (m_advPicking && inView && ImGui::IsMouseClicked(0) && !io.KeyAlt) {
        float3 o, d;
        cameraRay(mx, my, o, d);
        PickResult pr = pick(m_scene, o, d);
        if (pr.object >= 0 && m_scene.objects[pr.object].part != "GROUND") {
            outlinerIds();
            m_advRefId = m_scene.objects[pr.object].id;
            status(L"Advanced Scale: orientation of " + fromUtf8(m_scene.objects[pr.object].name));
        }
        m_advPicking = false;
        markDirty();
        return true;
    }
    if (m_selected < 0) { m_advRefId = 0; m_adv.active = false; m_advHover = -1; return false; }
    AdvDrag& g = m_adv;
    if (g.active) {
        if (!io.MouseDown[0]) {
            g.active = false;
            if (!g.moved && !m_undo.empty()) m_undo.pop_back();
            return true;
        }
        for (const auto& it : g.items)
            if (it.obj >= (int)m_scene.objects.size()) { g.active = false; return true; }
        advScaleDrag(mx, my);
        return true;
    }
    AdvBox b;
    if (!inView) { m_advHover = -1; return false; }
    if (!advScaleBox(b)) { m_advHover = -1; return false; }
    const AdvScaleOptions& opt = th::settings().advScale;
    const bool corners = m_advCorners || io.KeyCtrl;
    const float3 eye = m_scene.camera.eye();
    int best = -1;
    float bestD = opt.interactDist * m_dpi;
    for (int h = 0; h < 27; ++h) {
        int s[3];
        handleSides(h, s);
        const int n = sideCount(s);
        if (n == 0) continue;
        if (b.flatAxis >= 0) {
            if (s[b.flatAxis] != 0) continue;   // flat: the four sides and the corners of the rectangle
            if (n == 2 && !corners) continue;
        } else if (n > 1 && !corners) continue;
        float3 dir(0, 0, 0), p = b.c;
        for (int k = 0; k < 3; ++k) { p = p + b.ax[k] * (s[k] * b.h[k]); dir = dir + b.ax[k] * (float)s[k]; }
        float2 sp;
        if (!project(p, sp)) continue;
        float dd = std::hypot(mx - sp.x, my - sp.y);
        if (dot(dir, eye - p) < 0) dd += 4.0f * m_dpi;   // a handle behind the box loses against one in front
        if (dd < bestD) { bestD = dd; best = h; }
    }
    m_advHover = best;
    if (best < 0 || io.KeyAlt || !ImGui::IsMouseClicked(0)) return false;   // (Alt + click: the camera)
    advScaleBegin(best, b, mx, my);   // press on a handle: the drag starts
    return true;
}

// Start of a drag of the handle h of the box b, pressed at (mx, my): the objects as they are, the drag plane or axis.
void App::advScaleBegin(int best, const AdvBox& b, float mx, float my) {
    AdvDrag& g = m_adv;
    pushUndo();
    g = AdvDrag();
    g.active = true;
    g.handle = best;
    handleSides(best, g.s);
    g.box0 = b;
    g.p0 = b.c;
    for (int k = 0; k < 3; ++k) g.p0 = g.p0 + b.ax[k] * (g.s[k] * b.h[k]);
    for (int i : gizmoTargets()) {
        Object& o = m_scene.objects[i];
        g.items.push_back({i, o.position, objRot(o), o.scale});
    }
    float3 ro, rd;
    cameraRay(mx, my, ro, rd);
    float3 r, u, f;
    m_scene.camera.basis(r, u, f);
    const int n = sideCount(g.s);
    if (n == 1) {
        int k = g.s[0] ? 0 : (g.s[1] ? 1 : 2);
        g.t0 = axisParam(g.p0, b.ax[k], ro, rd);
    } else {
        if (n == 2) {
            // plane of the two axes of the edge (the one facing the camera when edge-on)
            int a = -1, c = -1;
            for (int k = 0; k < 3; ++k) if (g.s[k]) { if (a < 0) a = k; else c = k; }
            g.n = normalize(cross(b.ax[a], b.ax[c]));
            if (std::fabs(dot(g.n, f)) < 0.15f) g.n = f;
        } else g.n = f;   // corner: the plane facing the camera
        if (!rayPlane(ro, rd, g.p0, g.n, g.hit0)) g.hit0 = g.p0;
    }
}

void App::advScaleDrag(float mx, float my) {
    AdvDrag& g = m_adv;
    const AdvBox& b = g.box0;
    ImGuiIO& io = ImGui::GetIO();
    float3 ro, rd;
    cameraRay(mx, my, ro, rd);
    const int n = sideCount(g.s);
    float3 D(0, 0, 0);
    if (n == 1) {
        int k = g.s[0] ? 0 : (g.s[1] ? 1 : 2);
        D = b.ax[k] * (axisParam(g.p0, b.ax[k], ro, rd) - g.t0);
    } else {
        float3 hit;
        if (!rayPlane(ro, rd, g.p0, g.n, hit)) return;
        D = hit - g.hit0;
    }
    float delta[3] = {0, 0, 0};   // outward displacement of the handle along each of its axes
    for (int k = 0; k < 3; ++k) if (g.s[k]) delta[k] = dot(D, b.ax[k]) * g.s[k];

    // V: the vertex nearest to the cursor on screen (objects not scaled), the handle goes to its level on each axis
    g.snapped = false;
    if (m_advMods >= 0 ? (m_advMods & 4) != 0 : ImGui::IsKeyDown(ImGuiKey_V)) {
        const float radius = th::settings().advScale.snapRadius * m_dpi;
        float bestD = radius;
        size_t budget = 400000;
        for (size_t i = 0; i < m_scene.objects.size() && budget > 0; ++i) {
            const Object& o = m_scene.objects[i];
            if (!o.visible || o.ghost || o.mesh < 0 || o.mesh >= (int)m_scene.meshes.size()) continue;
            bool mine = false;
            for (const auto& it : g.items) if (it.obj == (int)i) mine = true;
            if (mine) continue;
            const Affine T = o.transform();
            const MeshData& m = m_scene.meshes[o.mesh];
            for (const Vertex& v : m.vertices) {
                if (budget-- == 0) break;
                const float3 w = T.point(v.pos);
                float2 sp;
                if (!project(w, sp)) continue;
                const float dd = std::hypot(sp.x - mx, sp.y - my);
                if (dd < bestD) { bestD = dd; g.snapVertex = w; g.snapped = true; }
            }
        }
        if (g.snapped)
            for (int k = 0; k < 3; ++k)
                if (g.s[k]) delta[k] = g.s[k] * (dot(g.snapVertex, b.ax[k]) - dot(g.p0, b.ax[k]));
    } else if (m_magnet && m_advMods < 0) {
        // grid snapping: the handle lands on the grid of the move step (world aligned axes), else whole steps
        const float step = std::max(m_stepMove, 1e-4f);
        for (int k = 0; k < 3; ++k) {
            if (!g.s[k]) continue;
            const float3& a = b.ax[k];
            const bool aligned = std::fabs(a.x) > 0.9999f || std::fabs(a.y) > 0.9999f || std::fabs(a.z) > 0.9999f;
            if (aligned) {
                const float c0 = dot(g.p0, a), target = std::round((c0 + g.s[k] * delta[k]) / step) * step;
                delta[k] = g.s[k] * (target - c0);
            } else delta[k] = std::round(delta[k] / step) * step;
        }
    }

    // scale factors along the box axes: Alt, both sides (around the centre); Shift, the same on the three axes
    const bool both = m_advMods >= 0 ? (m_advMods & 2) != 0 : io.KeyAlt;   // (m_advMods: forced by the self-test)
    // corner (three axes): the factor along the screen diagonal from the fixed point (the opposite corner, the centre
    // with Alt), the same on the three axes, so that pulling the corner away from its opposite always enlarges the
    // box (a 2D drag cannot tell three axes apart); with V the corner goes to the level of the vertex on each axis
    bool diagonal = false;
    float ratio = 1.0f;
    if (n == 3 && !g.snapped) {
        float3 fixedPt = b.c;
        if (!both)
            for (int k = 0; k < 3; ++k) fixedPt = fixedPt - b.ax[k] * (g.s[k] * b.h[k]);
        float2 aS, hS;
        if (project(fixedPt, aS) && project(g.p0, hS)) {
            const float vx = hS.x - aS.x, vy = hS.y - aS.y, l2 = vx * vx + vy * vy;
            if (l2 > 4.0f) {
                ratio = ((mx - aS.x) * vx + (my - aS.y) * vy) / l2;
                diagonal = true;
            }
        }
        if (diagonal && m_magnet && m_advMods < 0) {   // grid: the largest side lands on a whole number of steps
            const float big = 2.0f * std::max(b.h.x, std::max(b.h.y, b.h.z)), step = std::max(m_stepMove, 1e-4f);
            ratio = std::round(big * ratio / step) * step / big;
        }
    }
    float f[3] = {1, 1, 1};
    for (int k = 0; k < 3; ++k)
        if (g.s[k]) f[k] = diagonal ? std::max(0.01f, ratio) : std::max(0.01f, (2.0f * b.h[k] + (both ? 2.0f : 1.0f) * delta[k]) / (2.0f * b.h[k]));
    if (m_advMods >= 0 ? (m_advMods & 1) != 0 : io.KeyShift) {
        int lead = -1;
        for (int k = 0; k < 3; ++k)
            if (g.s[k] && (lead < 0 || std::fabs(f[k] - 1) > std::fabs(f[lead] - 1))) lead = k;
        if (lead >= 0) f[0] = f[1] = f[2] = f[lead];
    }
    // fixed point: the opposite face / edge / corner (the centre for Alt and for the axes not dragged)
    float3 A = b.c;
    if (!both)
        for (int k = 0; k < 3; ++k) if (g.s[k]) A = A - b.ax[k] * (g.s[k] * b.h[k]);
    bool changed = false;
    for (const auto& it : g.items) {
        Object& o = m_scene.objects[it.obj];
        const float3 rel = it.pos - A;
        float3 np = A;
        for (int k = 0; k < 3; ++k) np = np + b.ax[k] * (f[k] * dot(rel, b.ax[k]));
        // each object along its own axes: the length its axis takes in the scaled box
        float3 ns = it.scale;
        for (int i = 0; i < 3; ++i) {
            const float3 l = qRot(it.rot, unitAxis(i));
            float3 v(0, 0, 0);
            for (int k = 0; k < 3; ++k) v = v + b.ax[k] * (f[k] * dot(l, b.ax[k]));
            ns[i] = it.scale[i] * length(v);
        }
        if (length(np - o.position) > 1e-7f || length(ns - o.scale) > 1e-7f) {
            o.position = np;
            o.scale = ns;
            changed = true;
        }
    }
    if (changed) {
        g.moved = true;
        markDirty();
    }
}

// --------------------------------------------------------------------------- drawing

void App::drawAdvScale(ImDrawList* dl) {
    const bool playing = m_game && (m_game->playing() || m_game->playerMode());
    if (!m_advScale || m_selected < 0 || (playing && !m_advShowInPlay)) return;
    AdvBox b;
    if (!advScaleBox(b)) return;
    const AdvScaleOptions& opt = th::settings().advScale;
    const float d = m_dpi;
    const float3 eye = m_scene.camera.eye();
    ImGuiIO& io = ImGui::GetIO();
    auto corner = [&](int i) {
        float3 p = b.c;
        for (int k = 0; k < 3; ++k) p = p + b.ax[k] * (((i >> k) & 1) ? b.h[k] : -b.h[k]);
        return p;
    };
    // a face of the box is turned to the camera
    auto faceFront = [&](int k, int sg) { const float3 n = b.ax[k] * (float)sg; return dot(n, eye - (b.c + n * b.h[k])) > 0; };
    // while dragging: the box as it was at the press, faint (the fixed side shows)
    if (m_adv.active) {
        const AdvBox& o = m_adv.box0;
        auto oc = [&](int i) {
            float3 p = o.c;
            for (int k = 0; k < 3; ++k) p = p + o.ax[k] * (((i >> k) & 1) ? o.h[k] : -o.h[k]);
            return p;
        };
        for (int i = 0; i < 8; ++i)
            for (int k = 0; k < 3; ++k) {
                if ((i >> k) & 1) continue;
                float2 pa, pb;
                if (project(oc(i), pa) && project(oc(i | (1 << k)), pb))
                    dl->AddLine(ImVec2(pa.x, pa.y), ImVec2(pb.x, pb.y), IM_COL32(220, 230, 240, 70), 1.0f * d);
            }
    }
    // frame: the 12 edges, the hidden ones fainter
    for (int i = 0; i < 8; ++i)
        for (int k = 0; k < 3; ++k) {
            if ((i >> k) & 1) continue;
            const int j = i | (1 << k);
            float2 pa, pb;
            if (!project(corner(i), pa) || !project(corner(j), pb)) continue;
            // the two faces along this edge
            const int a1 = (k + 1) % 3, a2 = (k + 2) % 3;
            const bool front = faceFront(a1, ((i >> a1) & 1) ? 1 : -1) || faceFront(a2, ((i >> a2) & 1) ? 1 : -1);
            dl->AddLine(ImVec2(pa.x, pa.y), ImVec2(pb.x, pb.y), col32(opt.colorEdge, front ? 1.0f : 0.35f), 1.3f * d);
        }
    if (playing) return;   // "Show in Game View": the frame only

    const bool corners = m_advCorners || io.KeyCtrl;
    const float hs = opt.handleSize * 0.5f * d;
    for (int h = 0; h < 27; ++h) {
        int s[3];
        handleSides(h, s);
        const int n = sideCount(s);
        if (n == 0) continue;
        bool isCorner = n == 3;
        if (b.flatAxis >= 0) {
            if (s[b.flatAxis] != 0) continue;
            if (n == 2) { if (!corners) continue; isCorner = true; }
        } else if (n > 1 && !corners) continue;
        float3 p = b.c, dir(0, 0, 0);
        for (int k = 0; k < 3; ++k) { p = p + b.ax[k] * (s[k] * b.h[k]); dir = dir + b.ax[k] * (float)s[k]; }
        float2 sp;
        if (!project(p, sp)) continue;
        const bool dragged = m_adv.active && m_adv.handle == h, hover = !m_adv.active && m_advHover == h;
        // edge and corner handles appear near the cursor
        float alpha = 1.0f;
        if (n > 1 && !dragged && !hover && !m_gzDemo) {   // (captures: all shown)
            const float dist = std::hypot(io.MousePos.x - sp.x, io.MousePos.y - sp.y);
            alpha = clampf(1.0f - (dist - 30.0f * d) / (50.0f * d), 0.0f, 1.0f);
            if (alpha <= 0.01f) continue;
        }
        const bool front = dot(dir, eye - p) >= 0;
        const ImVec4& c = isCorner ? opt.colorCorner : (n == 2 ? opt.colorEdgeHandle : opt.colorFace);
        const float r = (dragged || hover) ? hs * 1.45f : hs;
        const ImVec2 a(sp.x - r, sp.y - r), e(sp.x + r, sp.y + r);
        if (front || dragged || hover) {
            dl->AddRectFilled(a, e, col32(c, alpha, (dragged || hover) ? 0.45f : 0.0f));
            dl->AddRect(a, e, col32(ImVec4(0.05f, 0.08f, 0.12f, 1), 0.6f * alpha), 0, 0, 1.0f);
        } else {   // behind the box: dark, dotted look
            dl->AddRectFilled(a, e, col32(ImVec4(c.x * 0.25f, c.y * 0.25f, c.z * 0.25f, 1), 0.8f * alpha));
            dl->AddRect(a, e, col32(c, 0.5f * alpha), 0, 0, 1.0f);
        }
    }
    // while dragging: the sizes of the box, the vertex the handle snaps to
    if (m_adv.active) {
        AdvBox now;
        if (advScaleBox(now)) {
            char t[96];
            snprintf(t, sizeof t, "%.3f x %.3f x %.3f", now.h.x * 2, now.h.y * 2, now.h.z * 2);
            // next to the dragged handle (where it is now)
            float3 hp = now.c;
            for (int k = 0; k < 3; ++k) hp = hp + now.ax[k] * (m_adv.s[k] * now.h[k]);
            float2 hs2;
            ImVec2 m = project(hp, hs2) ? ImVec2(hs2.x, hs2.y) : io.MousePos, ts = ImGui::CalcTextSize(t);
            ImVec2 p(m.x + 14 * d, m.y + 10 * d);
            dl->AddRectFilled(ImVec2(p.x - 4 * d, p.y - 2 * d), ImVec2(p.x + ts.x + 4 * d, p.y + ts.y + 2 * d), IM_COL32(20, 22, 26, 200), 3 * d);
            dl->AddText(p, IM_COL32(235, 240, 245, 255), t);
        }
        float2 vp;
        if (m_adv.snapped && project(m_adv.snapVertex, vp)) {
            dl->AddCircle(ImVec2(vp.x, vp.y), 6.0f * d, IM_COL32(255, 214, 90, 255), 16, 1.5f * d);
            dl->AddCircleFilled(ImVec2(vp.x, vp.y), 2.0f * d, IM_COL32(255, 214, 90, 255), 8);
        }
    }
}

// --------------------------------------------------------------------------- captures

// --open-menu advScale<Mode> (with --gizmo-demo, scene Concept): states of the tool frozen for the captures of the
// documentation. advScale: face handles; Corners: + edge / corner handles; Multi: three objects (world box); Orient:
// objects turned, box oriented on the cube (eyedropper); Flat: a plane (flat mode); Settings: the settings window;
// Drag / Shift / Alt / Corner / Snap: a drag in progress (+X face, Shift from the top face, Alt, corner, V towards a
// vertex of the cylinder).
void App::advScaleDemo(const std::string& mode) {
    auto find = [&](const char* name) {
        for (size_t i = 0; i < m_scene.objects.size(); ++i) if (m_scene.objects[i].name == name) return (int)i;
        return -1;
    };
    const int cube = find("Cube"), sphere = find("Sphere"), cyl = find("Cylinder");
    if (cube < 0) return;
    outlinerIds();
    m_advScale = true;
    m_advCorners = mode == "advScaleCorners" || mode == "advScaleCorner" || mode == "advScaleFlat";
    auto selSet = [&](std::vector<int> v) { select(-1); select(v[0]); m_multi.assign(v.begin() + 1, v.end()); };
    if (mode == "advScaleMulti" && sphere >= 0 && cyl >= 0) selSet({cube, sphere, cyl});
    if (mode == "advScaleOrient" && cyl >= 0) {
        Object& c = m_scene.objects[cube];
        c.rot = qAxisAngle(float3(0, 1, 0), radians(30.0f));
        c.useRot = true;
        Object& y = m_scene.objects[cyl];
        y.rot = qAxisAngle(float3(0, 1, 0), radians(-25.0f));
        y.useRot = true;
        selSet({cube, cyl});
        m_advRefId = c.id;   // the eyedropper picked the cube
    }
    if (mode == "advScaleFlat") {
        // a thin panel standing in front of the cube
        const int p = app::addPrimitive(m_scene, PRIM_CUBE, float3(2.2f, 0.0f, 2.6f));
        if (p >= 0) {
            Object& o = m_scene.objects[p];
            o.name = "Panel";
            o.scale = float3(1.6f, 1.4f, 0.02f);
            o.rot = qAxisAngle(float3(0, 1, 0), radians(20.0f));
            o.useRot = true;
            m_renderer.setScene(m_scene);
            outlinerIds();
            selSet({p});
        }
    }
    if (mode == "advScaleSettings") m_showAdvSettings = true;
    // drags in progress
    int h = -1, mods = 0;
    float px = 70.0f;
    if (mode == "advScaleDrag") h = 2 * 9 + 1 * 3 + 1;                 // +X face
    if (mode == "advScaleShift") { h = 1 * 9 + 2 * 3 + 1; mods = 1; px = 45.0f; }   // top face, Shift
    if (mode == "advScaleAlt") { h = 2 * 9 + 1 * 3 + 1; mods = 2; px = 45.0f; }      // +X face, Alt
    if (mode == "advScaleCorner") { h = 2 * 9 + 2 * 3 + 2; px = 55.0f; }            // corner (+X, +Y, +Z)
    if (mode == "advScaleSnap") { h = 2 * 9 + 1 * 3 + 1; mods = 4; }                // +X face, V
    if (h < 0) return;
    AdvBox b;
    if (!advScaleBox(b)) return;
    int s[3];
    handleSides(h, s);
    float3 p = b.c;
    for (int k = 0; k < 3; ++k) p = p + b.ax[k] * (s[k] * b.h[k]);
    float2 sp, sc;
    if (!project(p, sp) || !project(b.c, sc)) return;
    float2 end(sp.x + (sp.x - sc.x) / std::max(std::hypot(sp.x - sc.x, sp.y - sc.y), 1e-3f) * px,
               sp.y + (sp.y - sc.y) / std::max(std::hypot(sp.x - sc.x, sp.y - sc.y), 1e-3f) * px);
    if (mode == "advScaleSnap" && cyl >= 0) {
        // the cursor on the far side of the cylinder (top rim, largest X): the face goes all the way to its level
        const Object& o = m_scene.objects[cyl];
        const Affine T = o.transform();
        float best = -1e30f;
        for (const Vertex& v : m_scene.meshes[o.mesh].vertices) {
            const float3 w = T.point(v.pos);
            float2 q;
            if (w.x + 0.2f * w.y <= best || !project(w, q)) continue;
            best = w.x + 0.2f * w.y;
            end = float2(q.x + 3.0f, q.y + 2.0f);
        }
    }
    m_advMods = mods;
    advScaleBegin(h, b, sp.x, sp.y);
    for (int i = 1; i <= 8; ++i) advScaleDrag(sp.x + (end.x - sp.x) * i / 8, sp.y + (end.y - sp.y) * i / 8);
    m_advMods = -1;
}

} // namespace app
