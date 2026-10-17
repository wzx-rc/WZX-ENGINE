// Viewport: the path traced / raster image of the renderer, camera navigation, picking, selection rectangle,
// move / rotate / scale gizmo and drag & drop from the content browser (overlays: AtelierUI.cpp).
#include "App.h"
#include "GameHost.h"
#include "Icons.h"
#include "imgui_internal.h"

namespace app {


// 3D image + input + gizmo in a screen rectangle of the current window (Atelier Scene workspace).
void App::viewportCanvas(ImVec2 p0, ImVec2 size) {
    m_viewportVisible = true;
    m_rcView = {p0.x, p0.y, size.x, size.y};
    m_vpPx.x = 0;
    m_vpPx.y = 0;
    m_rs.pixelScale = 1.0f;
    m_rs.msaaSamples = m_msaa;   // anti-aliasing: hardware MSAA (Display menu)
    m_vpPx.w = std::max(16, (int)size.x);
    m_vpPx.h = std::max(16, (int)size.y);

    ImGui::SetCursorScreenPos(p0);
    ImGui::SetNextItemAllowOverlap();   // overlay buttons drawn on top of the image stay clickable
    ImGui::InvisibleButton("##viewport", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    // (a click that made this pane the active one: the view takes it, as the button, moved here, is hovered next frame)
    const bool clickedIn = m_paneClicked && m_rcView.contains(ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y);
    m_paneClicked = false;
    bool hovered = (ImGui::IsItemHovered() || clickedIn) && !(m_game && (m_game->graphOpen() || m_game->rtsCameraOn()));   // the graph editor covers the view
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
    drawSelectionFx(dl);   // white glow on the outline vertices, reflection on the floor
    drawCameraIcons(dl);   // the camera objects
    drawAdvScale(dl);      // Advanced Scale: bounding box and handles
    if (!(m_game && m_game->placementMode())) drawGizmo(dl);
    dl->PopClipRect();
    drawMarkingMenu(ImGui::GetForegroundDrawList());
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
    const bool placing = m_game && (m_game->placementMode() || m_game->playing());   // the left button places houses or plays: no selection, no gizmo, no marquee

    // hold Q / W / E / R + left click: marking menu of the tool (nothing else while it is open)
    if (!placing && markingMenuInput(inView)) return;
    // with a manipulator shown, the middle button drives it (Maya); Alt + middle still pans
    const bool manipMiddle = !placing && gizmoShown() && !alt;

    if (inView && io.MouseWheel != 0) {
        const float k = std::pow(0.88f, io.MouseWheel * m_camSpeed);
        if (cam.ortho) cam.orthoHeight = clampf(cam.orthoHeight * k, 1e-3f, 1e6f);   // orthographic: the size of the view
        else cam.distance = clampf(cam.distance * k, 0.5f, 2000.0f);
        cameraChanged();
    }
    if (inView && (ImGui::IsMouseClicked(1) || (alt && ImGui::IsMouseClicked(0)))) {
        if (shift || orbitLocked()) m_panning = true;   // (the orthographic views do not tumble, as in Maya: they pan)
        else m_orbiting = true;
    }
    if (inView && ImGui::IsMouseClicked(2) && !manipMiddle) m_panning = true;
    if (m_orbiting) {
        if (!(io.MouseDown[1] || io.MouseDown[0])) m_orbiting = false;
        else if (dx != 0 || dy != 0) {
            cam.yaw -= dx * 0.0065f * m_camSpeed / m_dpi;
            cam.pitch = clampf(cam.pitch + dy * 0.0065f * m_camSpeed / m_dpi, -1.55f, 1.55f);
            cameraChanged();
        }
    }
    if (m_panning) {
        if (!(io.MouseDown[2] || io.MouseDown[1] || io.MouseDown[0])) m_panning = false;
        else if (dx != 0 || dy != 0) {
            float3 right, up, fwd;
            cam.basis(right, up, fwd);
            float k = (cam.ortho ? cam.orthoHeight : cam.distance * std::tan(cam.fovY * 0.5f) * 2.0f) / m_rcView.h * m_camSpeed;
            cam.target = cam.target - right * (dx * k) + up * (dy * k);
            cameraChanged();
        }
    }

    // Advanced Scale (a separate tool) replaces the manipulator while it is on
    bool gzBusy = placing ? false : (m_advScale ? advScaleInput(inView) : gizmoInput(inView));
    if (!placing) marqueeInput();
    if (m_marq.active) gzBusy = true;

    // Left button: selection / tools / focus.
    if (!gzBusy && inView && !placing && ImGui::IsMouseClicked(0) && !alt) {
        float3 o, d;
        cameraRay(mx, my, o, d);
        PickResult pr = pick(m_scene, o, d);
        const int camIcon = pickCameraIcon(mx, my);   // the camera icons are drawn over the view
        if (camIcon >= 0) {
            pr.object = camIcon;
            pr.position = m_scene.objects[camIcon].position;
        }
        if (ImGui::IsMouseDoubleClicked(0)) {
            if (pr.object >= 0 || pr.t < 1e29f) {
                float3 p = pr.object >= 0 ? pr.position : o + d * dot(cam.target - o, d);
                setPivotKeepEye(p);
                cam.focusDistance = cam.distance;
                status(L"Pivot and focus on the clicked point");
                cameraChanged();
            }
        } else if (pr.object >= 0) {
            // Maya: Shift+click toggles, Ctrl+click removes, Ctrl+Shift+click adds
            const bool ctrl = io.KeyCtrl;
            if (shift || ctrl) {
                bool inSel = pr.object == m_selected || std::find(m_multi.begin(), m_multi.end(), pr.object) != m_multi.end();
                bool add = ctrl && shift ? true : ctrl ? false : !inSel;
                if (add && !inSel) {
                    if (m_selected < 0) select(pr.object);
                    else { m_multi.push_back(pr.object); markDirty(); }
                } else if (!add && inSel) {
                    if (pr.object == m_selected) {
                        if (!m_multi.empty()) { m_selected = m_multi.front(); m_multi.erase(m_multi.begin()); } else m_selected = -1;
                    } else m_multi.erase(std::find(m_multi.begin(), m_multi.end(), pr.object));
                    markDirty();
                }
                return;
            }
            select(pr.object);
        } else {
            // empty space, any tool (Maya): selection rectangle; a simple click clears the selection
            m_marq.active = true;
            m_marq.moved = false;
            m_marq.x0 = mx;
            m_marq.y0 = my;
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
        if (!o.visible || !o.selectable || o.ghost || o.group || o.part == "GROUND" || o.part == "WALL") continue;
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

} // namespace app
