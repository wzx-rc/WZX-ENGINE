// Viewport: the path traced / raster image of the renderer, camera navigation, picking, selection rectangle,
// move / rotate / scale gizmo and drag & drop from the content browser (overlays: AtelierUI.cpp).
#include "App.h"
#include "GameHost.h"
#include "Icons.h"
#include "imgui_internal.h"

namespace app {

static const ImU32 kAxisCol[3] = {IM_COL32(229, 72, 77, 255), IM_COL32(70, 167, 88, 255), IM_COL32(62, 142, 247, 255)};
static const int kRingSeg = 64;
static const float kRingR = 0.78f, kArrowTip = 1.3f, kArrowBase = 1.0f;

static float3 axisVec(int a) { return float3(a == 0 ? 1.0f : 0.0f, a == 1 ? 1.0f : 0.0f, a == 2 ? 1.0f : 0.0f); }

static float distSeg(float px, float py, float2 a, float2 b) {
    float dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
    float t = l2 > 1e-6f ? clampf(((px - a.x) * dx + (py - a.y) * dy) / l2, 0, 1) : 0;
    float qx = a.x + dx * t - px, qy = a.y + dy * t - py;
    return std::sqrt(qx * qx + qy * qy);
}

// closest parameter along the axis line (through o, direction a) to the mouse ray
static float axisParam(const float3& o, const float3& a, const float3& ro, const float3& rd) {
    float3 w = o - ro;
    float b = dot(a, rd), d = dot(a, w), e = dot(rd, w);
    float den = 1 - b * b;
    return std::fabs(den) < 1e-6f ? 0.0f : (b * e - d) / den;
}

static ImVec2 iv(float2 p) { return ImVec2(p.x, p.y); }

// 3D image + input + gizmo in a screen rectangle of the current window (Atelier Scene workspace).
void App::viewportCanvas(ImVec2 p0, ImVec2 size) {
    m_viewportVisible = true;
    m_rcView = {p0.x, p0.y, size.x, size.y};
    m_vpPx.x = 0;
    m_vpPx.y = 0;
    m_vpPx.w = std::max(16, (int)size.x);
    m_vpPx.h = std::max(16, (int)size.y);

    ImGui::SetCursorScreenPos(p0);
    ImGui::SetNextItemAllowOverlap();   // overlay buttons drawn on top of the image stay clickable
    ImGui::InvisibleButton("##viewport", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    bool hovered = ImGui::IsItemHovered() && !(m_game && m_game->graphOpen());   // the graph editor covers the view
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // the texture of the previous size is stretched for one frame when the panel is resized
    dl->AddImage(m_gui.viewportTexture(), p0, ImVec2(p0.x + size.x, p0.y + size.y));

    // drop an asset from the content browser
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("WZX_ASSET")) {
            int idx = *(const int*)pl->Data;
            if (idx >= 0 && idx < (int)parts().size()) {
                float3 o, dir;
                ImVec2 m = ImGui::GetMousePos();
                cameraRay(m.x, m.y, o, dir);
                PickResult pr = pick(m_scene, o, dir);
                float3 at = m_scene.camera.target;
                at.y = 0;
                if (pr.object >= 0) at = pr.position;
                else if (std::fabs(dir.y) > 1e-4f) {
                    float t = -o.y / dir.y;
                    if (t > 0) at = o + dir * t;
                }
                addPartFromLibrary(parts()[idx], &at);
            }
        }
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("WZX_MATERIAL")) {
            int preset = *(const int*)pl->Data;
            float3 o, dir;
            ImVec2 m = ImGui::GetMousePos();
            cameraRay(m.x, m.y, o, dir);
            PickResult pr = pick(m_scene, o, dir);
            if (pr.object >= 0 && m_scene.objects[pr.object].selectable) {
                select(pr.object);
                setPreset(preset);
                status(L"Material applied: " + presetNames()[preset]);
            } else status(L"Drop the material on a selectable object", 2);
        }
        ImGui::EndDragDropTarget();
    }

    viewportInput(hovered);
    dl->PushClipRect(p0, ImVec2(p0.x + size.x, p0.y + size.y), true);
    drawGizmo(dl);
    dl->PopClipRect();
}

// --------------------------------------------------------------------------- input

void App::viewportInput(bool hovered) {
    ImGuiIO& io = ImGui::GetIO();
    Camera& cam = m_scene.camera;
    float mx = io.MousePos.x, my = io.MousePos.y;
    float dx = mx - m_lastX, dy = my - m_lastY;
    m_lastX = mx;
    m_lastY = my;
    bool inView = hovered;
    bool alt = io.KeyAlt, shift = io.KeyShift;

    if (inView && io.MouseWheel != 0) {
        cam.distance *= std::pow(0.88f, io.MouseWheel * m_camSpeed);
        cam.distance = clampf(cam.distance, 0.5f, 2000.0f);
        cameraChanged();
    }
    if (inView && (ImGui::IsMouseClicked(1) || (alt && ImGui::IsMouseClicked(0)))) {
        if (shift) m_panning = true;
        else m_orbiting = true;
    }
    if (inView && ImGui::IsMouseClicked(2)) m_panning = true;
    if (m_orbiting) {
        if (!(io.MouseDown[1] || io.MouseDown[0])) m_orbiting = false;
        else if (dx != 0 || dy != 0) {
            cam.yaw -= dx * 0.0065f * m_camSpeed / m_dpi;
            cam.pitch = clampf(cam.pitch + dy * 0.0065f * m_camSpeed / m_dpi, -1.55f, 1.55f);
            cameraChanged();
        }
    }
    if (m_panning) {
        if (!(io.MouseDown[2] || io.MouseDown[1])) m_panning = false;
        else if (dx != 0 || dy != 0) {
            float3 right, up, fwd;
            cam.basis(right, up, fwd);
            float k = cam.distance * std::tan(cam.fovY * 0.5f) * 2.0f / m_rcView.h * m_camSpeed;
            cam.target = cam.target - right * (dx * k) + up * (dy * k);
            cameraChanged();
        }
    }

    // axis widget (bottom-left): click an axis to look along it
    if (inView && ImGui::IsMouseClicked(0) && !alt) {
        float3 r, u, f;
        cam.basis(r, u, f);
        float gx = m_rcView.x + 52 * m_dpi, gy = m_rcView.b() - 52 * m_dpi, L = 30 * m_dpi;
        const int views[3] = {4, 5, 1};
        for (int a = 0; a < 3; ++a) {
            float3 av = axisVec(a);
            float ex = gx + dot(av, r) * L, ey = gy - dot(av, u) * L;
            if (std::hypot(mx - ex, my - ey) < 10 * m_dpi) { setViewAxis(views[a]); return; }
        }
    }

    bool gzBusy = gizmoInput(inView);
    marqueeInput();
    if (m_marq.active) gzBusy = true;

    // Left button: selection / tools / focus.
    if (!gzBusy && inView && ImGui::IsMouseClicked(0) && !alt) {
        float3 o, d;
        cameraRay(mx, my, o, d);
        PickResult pr = pick(m_scene, o, d);
        if (ImGui::IsMouseDoubleClicked(0)) {
            if (pr.object >= 0 || pr.t < 1e29f) {
                float3 p = pr.object >= 0 ? pr.position : o + d * cam.distance;
                setPivotKeepEye(p);
                cam.focusDistance = cam.distance;
                status(L"Pivot and focus on the clicked point");
                cameraChanged();
            }
        } else if (pr.object >= 0) {
            if (shift) {
                // Shift+click: add / remove from the selection
                if (m_selected < 0) select(pr.object);
                else if (pr.object == m_selected) { if (!m_multi.empty()) { m_selected = m_multi.front(); m_multi.erase(m_multi.begin()); } else m_selected = -1; markDirty(); }
                else {
                    auto it = std::find(m_multi.begin(), m_multi.end(), pr.object);
                    if (it != m_multi.end()) m_multi.erase(it); else m_multi.push_back(pr.object);
                    markDirty();
                }
                return;
            }
            select(pr.object);
            if (m_tool == TOOL_MOVE) {
                pushUndo();
                m_dragging = true;
                m_dragMoved = false;
                m_dragPlaneY = pr.position.y;
                float3 op = m_scene.objects[pr.object].position;
                m_dragOffset = float3(op.x - pr.position.x, 0, op.z - pr.position.z);
            }
        } else if (m_tool == TOOL_SELECT) {
            // empty space in select mode: start a selection rectangle (a simple click clears the selection)
            m_marq.active = true;
            m_marq.moved = false;
            m_marq.x0 = mx;
            m_marq.y0 = my;
        } else {
            select(-1);
        }
    }
    if (m_dragging) {
        if (!io.MouseDown[0]) {
            m_dragging = false;
            if (!m_dragMoved && !m_undo.empty()) m_undo.pop_back();
        } else if (m_selected >= 0 && (dx != 0 || dy != 0)) {
            float3 o, d;
            cameraRay(mx, my, o, d);
            if (std::fabs(d.y) > 1e-4f) {
                float t = (m_dragPlaneY - o.y) / d.y;
                if (t > 0) {
                    float3 p = o + d * t + m_dragOffset;
                    Object& ob = m_scene.objects[m_selected];
                    float3 np(p.x, ob.position.y, p.z);
                    if (m_magnet) { np.x = std::round(np.x); np.z = std::round(np.z); }
                    if (np.x != ob.position.x || np.z != ob.position.z) {
                        float3 delta = np - ob.position;
                        for (int i : selectionAll()) m_scene.objects[i].position = m_scene.objects[i].position + delta;
                        m_dragMoved = true;
                        markDirty();
                    }
                }
            }
        }
    }
}

void App::marqueeInput() {
    if (!m_marq.active) return;
    ImGuiIO& io = ImGui::GetIO();
    float mx = io.MousePos.x, my = io.MousePos.y;
    if (io.MouseDown[0]) {
        if (std::hypot(mx - m_marq.x0, my - m_marq.y0) > 5.0f) m_marq.moved = true;
        return;
    }
    m_marq.active = false;
    bool shift = io.KeyShift;
    if (!m_marq.moved) { if (!shift) select(-1); return; }
    float x0 = std::min(m_marq.x0, mx), y0 = std::min(m_marq.y0, my), x1 = std::max(m_marq.x0, mx), y1 = std::max(m_marq.y0, my);
    std::vector<int> hit;
    for (size_t i = 0; i < m_scene.objects.size(); ++i) {
        const Object& o = m_scene.objects[i];
        if (!o.visible || !o.selectable || o.ghost || o.part == "GROUND" || o.part == "WALL") continue;
        float3 mn, mxv;
        aabbOf(o, o.position, objRot(o), mn, mxv);
        float2 s;
        if (project((mn + mxv) * 0.5f, s) && s.x >= x0 && s.x <= x1 && s.y >= y0 && s.y <= y1) hit.push_back((int)i);
    }
    if (shift)
        for (int i : selectionAll())
            if (std::find(hit.begin(), hit.end(), i) == hit.end()) hit.push_back(i);
    if (hit.empty()) { if (!shift) select(-1); return; }
    m_selected = hit[0];
    m_multi.assign(hit.begin() + 1, hit.end());
    markDirty();
    status(std::to_wstring(hit.size()) + L" objects selected");
}

// --------------------------------------------------------------------------- gizmo

float App::gizmoScale(const float3& pivot) const {
    float3 d = pivot - m_scene.camera.eye();
    return length(d) * 0.07f;
}

static bool gizmoShown(bool show, int tool, int selected) {
    return show && (tool == TOOL_MOVE || tool == TOOL_ROTATE || tool == TOOL_SCALE) && selected >= 0;
}

// Direction of a gizmo axis: world axes, or the object's own axes in local space (always local for scaling).
float3 App::gizmoAxis(int a) const {
    float3 v = axisVec(a);
    if (m_selected < 0 || (!m_localSpace && m_tool != TOOL_SCALE)) return v;
    return normalize(qRot(objRot(m_scene.objects[m_selected]), v));
}

bool App::gizmoHit(float mx, float my, int& axis, bool& rotate) {
    if (!gizmoShown(m_showGizmo, m_tool, m_selected)) return false;
    const Object& o = m_scene.objects[m_selected];
    float3 pv = o.position;
    float s = gizmoScale(pv);
    rotate = m_tool == TOOL_ROTATE;
    float best = (rotate ? 9.0f : 11.0f) * m_dpi;
    axis = -1;
    for (int a = 0; a < 3; ++a) {
        float3 av = gizmoAxis(a);
        float dd = 1e30f;
        if (!rotate) {
            float2 p0, p1;
            if (project(pv, p0) && project(pv + av * (kArrowTip * s), p1)) dd = distSeg(mx, my, p0, p1);
        } else {
            float3 u = gizmoAxis((a + 1) % 3), v = gizmoAxis((a + 2) % 3);
            float2 prev;
            bool hasPrev = false;
            for (int i = 0; i <= kRingSeg; ++i) {
                float ang = i * 2 * kPi / kRingSeg;
                float2 p;
                bool ok = project(pv + (u * std::cos(ang) + v * std::sin(ang)) * (kRingR * s), p);
                if (ok && hasPrev) dd = std::min(dd, distSeg(mx, my, prev, p));
                prev = p;
                hasPrev = ok;
            }
        }
        if (dd < best) { best = dd; axis = a; }
    }
    return axis >= 0;
}

bool App::gizmoInput(bool inView) {
    ImGuiIO& io = ImGui::GetIO();
    if (!gizmoShown(m_showGizmo, m_tool, m_selected)) {
        m_gz.active = false;
        m_gzHover = -1;
        return false;
    }
    float3 ro, rd;
    cameraRay(io.MousePos.x, io.MousePos.y, ro, rd);
    if (m_gz.active) {
        GizmoDrag& g = m_gz;
        if (!io.MouseDown[0] || g.obj != m_selected) {
            g.active = false;
            if (!g.moved && !m_undo.empty()) m_undo.pop_back();
            return true;
        }
        Object& o = m_scene.objects[g.obj];
        float3 a = g.dir;
        if (g.mode == 0) {
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
        } else if (g.mode == 2) {
            // scale along the object's own axis: the handle length maps to a factor of 1
            float d = axisParam(g.pivot, a, ro, rd) - g.t0;
            float k = std::max(0.05f, 1.0f + d / std::max(g.s0 * kArrowTip, 1e-3f));
            if (m_magnet) k = std::max(0.1f, std::round(k * 10.0f) / 10.0f);
            float3 ns = g.startScale;
            (&ns.x)[g.axis] = std::max(0.01f, (&g.startScale.x)[g.axis] * k);
            if (ns.x != o.scale.x || ns.y != o.scale.y || ns.z != o.scale.z) {
                o.scale = ns;
                for (auto& ot : g.others) {
                    float3 os = ot.scale;
                    (&os.x)[g.axis] = std::max(0.01f, (&ot.scale.x)[g.axis] * k);
                    m_scene.objects[ot.obj].scale = os;
                }
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
                    o.rot = qNormalize(qMul(dq, g.startRot));
                    o.position = g.pivot + qRot(dq, g.startPos - g.pivot);
                    for (auto& ot : g.others) {
                        Object& oo = m_scene.objects[ot.obj];
                        oo.rot = qNormalize(qMul(dq, ot.rot));
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
    bool hit = inView && gizmoHit(io.MousePos.x, io.MousePos.y, axis, rotate);
    m_gzHover = hit ? axis : -1;
    if (hit && ImGui::IsMouseClicked(0)) {
        pushUndo();
        Object& o = m_scene.objects[m_selected];
        ensureRot(o);
        GizmoDrag& g = m_gz;
        g = GizmoDrag();
        g.active = true;
        g.axis = axis;
        g.mode = m_tool == TOOL_ROTATE ? 1 : (m_tool == TOOL_SCALE ? 2 : 0);
        g.rotate = rotate;
        g.obj = m_selected;
        g.startPos = o.position;
        g.startRot = o.rot;
        g.startScale = o.scale;
        g.pivot = o.position;
        g.s0 = gizmoScale(o.position);
        g.dir = gizmoAxis(axis);
        for (int oi : m_multi)
            if (oi >= 0 && oi < (int)m_scene.objects.size()) {
                Object& oo = m_scene.objects[oi];
                ensureRot(oo);
                g.others.push_back({oi, oo.position, oo.rot, oo.scale});
            }
        float3 a = g.dir;
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

void App::drawGizmo(ImDrawList* dl) {
    if (!gizmoShown(m_showGizmo, m_tool, m_selected)) return;
    const Object& o = m_scene.objects[m_selected];
    float3 pv = o.position;
    float s = gizmoScale(pv);
    bool rotate = m_tool == TOOL_ROTATE, scale = m_tool == TOOL_SCALE;
    float2 pc;
    if (!project(pv, pc)) return;
    const float d = m_dpi;
    for (int a = 0; a < 3; ++a) {
        bool lit = (m_gz.active && m_gz.axis == a) || (!m_gz.active && m_gzHover == a);
        ImU32 c = lit ? IM_COL32(255, 255, 255, 255) : kAxisCol[a];
        float3 av = gizmoAxis(a);
        if (!rotate) {
            float2 pb, pt;
            if (!project(pv + av * (kArrowBase * s), pb) || !project(pv + av * (kArrowTip * s), pt)) continue;
            if (scale) {
                // line to a square handle
                dl->AddLine(iv(pc), iv(pt), c, (lit ? 3.0f : 2.2f) * d);
                float hs = (lit ? 6.0f : 5.0f) * d;
                dl->AddRectFilled(ImVec2(pt.x - hs, pt.y - hs), ImVec2(pt.x + hs, pt.y + hs), c, 1.5f * d);
                continue;
            }
            dl->AddLine(iv(pc), iv(pb), c, (lit ? 3.0f : 2.2f) * d);
            float dx = pt.x - pb.x, dy = pt.y - pb.y, l = std::sqrt(dx * dx + dy * dy);
            if (l < 1e-3f) continue;
            dx /= l; dy /= l;
            float hw = (lit ? 6.5f : 5.5f) * d;
            dl->AddTriangleFilled(ImVec2(pt.x, pt.y), ImVec2(pb.x - dy * hw, pb.y + dx * hw), ImVec2(pb.x + dy * hw, pb.y - dx * hw), c);
        } else {
            float3 u = gizmoAxis((a + 1) % 3), v = gizmoAxis((a + 2) % 3);
            float2 prev;
            bool hasPrev = false;
            for (int i = 0; i <= kRingSeg; ++i) {
                float ang = i * 2 * kPi / kRingSeg;
                float2 p;
                bool ok = project(pv + (u * std::cos(ang) + v * std::sin(ang)) * (kRingR * s), p);
                if (ok && hasPrev) dl->AddLine(iv(prev), iv(p), c, (lit ? 3.0f : 2.0f) * d);
                prev = p;
                hasPrev = ok;
            }
        }
    }
    // small square on the pivot, like the concept's transform handle
    float hs = 5 * d;
    dl->AddRectFilled(ImVec2(pc.x - hs, pc.y - hs), ImVec2(pc.x + hs, pc.y + hs), IM_COL32(255, 255, 255, 60));
    dl->AddRect(ImVec2(pc.x - hs, pc.y - hs), ImVec2(pc.x + hs, pc.y + hs), IM_COL32(255, 255, 255, 220), 0, 0, 1.5f * d);
}

} // namespace app
