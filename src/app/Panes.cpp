// Split layouts of the viewport, as the panes of Maya: single, two side by side or stacked, four (2 x 2), nine (3 x 3)
// or sixteen (4 x 4) panes, each looking through its own camera (Cameras.cpp). The active pane (the last one clicked)
// is drawn by the full renderer (Lit, path tracing, GI) with the manipulators and the menus of the view; the others
// are raster views of their camera (Renderer::renderView) with the grid, the wireframe and the selection glow.
// Space maximizes the pane under the mouse, and gives the layout back. The selected camera object is also shown in a
// small preview at the bottom right of the active pane (pinnable), as in Unreal.
#include "App.h"
#include "GameHost.h"

namespace app {

namespace {

const int kCols[kLayoutCount] = {1, 2, 1, 2, 3, 4};
const int kRows[kLayoutCount] = {1, 1, 2, 2, 3, 4};
const double kViewMenuDelay = 0.2;   // seconds of Space held before the view menu is drawn (as the marking menus)

} // namespace

const char* App::layoutName(int l) {
    static const char* names[kLayoutCount] = {"Single", "Two Side by Side", "Two Stacked", "Four (2 x 2)", "Nine (3 x 3)", "Sixteen (4 x 4)"};
    return names[std::clamp(l, 0, kLayoutCount - 1)];
}

int App::layoutCols(int l) { return kCols[std::clamp(l, 0, kLayoutCount - 1)]; }
int App::layoutRows(int l) { return kRows[std::clamp(l, 0, kLayoutCount - 1)]; }

// A new layout: the cameras of Maya's four view (top, persp / front, side) and then the other built-in cameras and the
// camera objects; one pane: the camera of the active pane; two: it and the persp (or top) camera. The active pane
// stays on what it looked through.
void App::setLayout(int l) {
    l = std::clamp(l, 0, kLayoutCount - 1);
    storeActiveCamera();
    const ViewPane keep = m_panes[m_activePane];
    const int n = kCols[l] * kRows[l];
    std::vector<ViewPane> panes(n);
    std::vector<ViewPane> order;
    auto builtin = [](int cam) { ViewPane p; p.cam = cam; return p; };
    if (n == 1) order = {keep};
    else if (n == 2) order = {keep, builtin(keep.cam == VCAM_PERSP ? VCAM_TOP : VCAM_PERSP)};
    else {
        for (int c : {VCAM_TOP, VCAM_PERSP, VCAM_FRONT, VCAM_SIDE, VCAM_BOTTOM, VCAM_BACK, VCAM_LEFT}) order.push_back(builtin(c));
        for (const Object& o : m_scene.objects)
            if (o.camera) {
                ViewPane p;
                p.cam = VCAM_OBJECT;
                p.object = o.id;
                order.push_back(p);
            }
    }
    for (int i = 0; i < n; ++i) panes[i] = order[i % order.size()];
    int active = 0;
    for (int i = 0; i < n; ++i)
        if (panes[i].cam == keep.cam && panes[i].object == keep.object) { active = i; break; }
    m_panes = panes;
    m_activePane = active;
    m_layout = l;
    m_paneMax = false;
    loadActiveCamera();
    cameraChanged();
}

// The rectangles of the panes in the area of the view (a maximized pane: the whole area), the active one returned.
ScreenRect App::layoutPanes(const ScreenRect& area) {
    m_paneArea = area;
    const int n = (int)m_panes.size();
    if (n <= 1 || m_paneMax) {
        for (int i = 0; i < n; ++i) m_panes[i].rc = i == m_activePane ? area : ScreenRect{0, 0, 0, 0};
        return area;
    }
    const int cols = kCols[m_layout], rows = kRows[m_layout];
    const float gap = 3.0f * m_dpi;
    for (int i = 0; i < n; ++i) {
        const int c = i % cols, r = i / cols;
        // the lines between the panes are the boundaries of the columns and rows (dragged: splitInput), the gap around them
        const float x0 = area.x + splitAt(false, c) * area.w + (c > 0 ? gap * 0.5f : 0.0f);
        const float x1 = area.x + splitAt(false, c + 1) * area.w - (c < cols - 1 ? gap * 0.5f : 0.0f);
        const float y0 = area.y + splitAt(true, r) * area.h + (r > 0 ? gap * 0.5f : 0.0f);
        const float y1 = area.y + splitAt(true, r + 1) * area.h - (r < rows - 1 ? gap * 0.5f : 0.0f);
        m_panes[i].rc = {std::floor(x0), std::floor(y0), std::floor(x1) - std::floor(x0), std::floor(y1) - std::floor(y0)};
    }
    return m_panes[m_activePane].rc;
}

float App::splitAt(bool rows, int k) const {
    const int n = rows ? kRows[m_layout] : kCols[m_layout];
    if (k <= 0) return 0.0f;
    if (k >= n) return 1.0f;
    const float f = (rows ? m_splitY : m_splitX)[m_layout][k - 1];
    return f > 0 ? f : (float)k / n;
}

// The lines between the panes: dragged with the left button, they resize the columns and rows (a pane keeps 60 px at
// least); where two lines cross, both move; a double-click gives the equal split back. Before paneInput and the view:
// the mouse on a line belongs to it.
void App::splitInput(const ScreenRect& area, bool windowHovered) {
    m_splitHot = false;
    ImGuiIO& io = ImGui::GetIO();
    const int cols = kCols[m_layout], rows = kRows[m_layout];
    if (m_panes.size() <= 1 || m_paneMax) { m_splitDrag = false; m_splitCol = m_splitRow = 0; return; }
    auto drag = [&](bool r, int k, float m) {   // boundary k of the rows or columns to the mouse, between its neighbours
        const float len = r ? area.h : area.w, minF = std::min(60.0f * m_dpi / std::max(len, 1.0f), 0.2f);
        const float f = clampf((m - (r ? area.y : area.x)) / std::max(len, 1.0f), splitAt(r, k - 1) + minF, splitAt(r, k + 1) - minF);
        (r ? m_splitY : m_splitX)[m_layout][k - 1] = f;
    };
    if (m_splitDrag) {
        if (!io.MouseDown[0]) { m_splitDrag = false; return; }
        m_splitHot = true;
        if (m_splitCol) drag(false, m_splitCol, io.MousePos.x);
        if (m_splitRow) drag(true, m_splitRow, io.MousePos.y);
        ImGui::SetMouseCursor(m_splitCol && m_splitRow ? ImGuiMouseCursor_ResizeAll : m_splitCol ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
        return;
    }
    m_splitCol = m_splitRow = 0;
    if (!windowHovered || ImGui::IsAnyItemActive() || m_gz.active || m_adv.active || m_mm.open || m_marq.active || m_vm.down) return;
    const float mx = io.MousePos.x, my = io.MousePos.y, grab = 4.0f * m_dpi;
    if (!area.contains(mx, my)) return;
    for (int k = 1; k < cols; ++k)
        if (std::fabs(mx - (area.x + splitAt(false, k) * area.w)) < grab) m_splitCol = k;
    for (int k = 1; k < rows; ++k)
        if (std::fabs(my - (area.y + splitAt(true, k) * area.h)) < grab) m_splitRow = k;
    if (!m_splitCol && !m_splitRow) return;
    m_splitHot = true;
    ImGui::SetMouseCursor(m_splitCol && m_splitRow ? ImGuiMouseCursor_ResizeAll : m_splitCol ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
    if (ImGui::IsMouseDoubleClicked(0)) {   // the equal split back
        if (m_splitCol) m_splitX[m_layout][m_splitCol - 1] = 0;
        if (m_splitRow) m_splitY[m_layout][m_splitRow - 1] = 0;
        cameraChanged();
    } else if (ImGui::IsMouseClicked(0)) m_splitDrag = true;
}

// Space: the pane under the mouse alone in the view, and back to the layout.
void App::togglePaneMax(bool underMouse) {
    if (m_panes.size() <= 1) return;
    if (!m_paneMax && underMouse) {
        const ImVec2 m = ImGui::GetIO().MousePos;
        for (int i = 0; i < (int)m_panes.size(); ++i)
            if (m_panes[i].rc.w > 1 && m_panes[i].rc.contains(m.x, m.y)) activatePane(i);
    }
    m_paneMax = !m_paneMax;
    cameraChanged();
}

// A click (any button) or the wheel over another pane makes it the active one, before the view handles the input.
void App::paneInput(bool windowHovered) {
    if (m_panes.size() <= 1 || m_paneMax || !windowHovered) return;
    ImGuiIO& io = ImGui::GetIO();
    if (!(ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1) || ImGui::IsMouseClicked(2) || io.MouseWheel != 0)) return;
    if (m_gz.active || m_adv.active || m_mm.open || m_marq.active) return;
    for (int i = 0; i < (int)m_panes.size(); ++i)
        if (i != m_activePane && m_panes[i].rc.w > 1 && m_panes[i].rc.contains(io.MousePos.x, io.MousePos.y)) {
            activatePane(i);
            m_paneClicked = true;   // (Dear ImGui hovers the moved view a frame later: viewportCanvas takes the click now)
            return;
        }
}

// Screen centre and radius (pixels of the pane) of the selection seen by camera c in a pane of size w x h: the light
// of the selection glow runs around it.
bool App::selectionInView(const Camera& c, float w, float h, float2& centre, float& radius) const {
    std::vector<int> sel = selectionAll();
    float3 mn(1e30f), mx(-1e30f);
    for (int i : sel) {
        if (i < 0 || i >= (int)m_scene.objects.size()) continue;
        const Object& o = m_scene.objects[i];
        float3 a, b;
        aabbOf(o, o.position, objRot(o), a, b);
        mn = minv(mn, a);
        mx = maxv(mx, b);
    }
    if (mn.x > mx.x || w < 1 || h < 1) return false;
    float3 cr, cu, cf;
    c.basis(cr, cu, cf);
    const float3 mid = (mn + mx) * 0.5f;
    float2 a, b;
    if (!c.project(mid, w / h, a) || !c.project(mid + cr * (length(mx - mn) * 0.5f), w / h, b)) return false;
    centre = float2((0.5f + 0.5f * a.x) * w, (0.5f - 0.5f * a.y) * h);
    radius = std::max(8.0f, std::fabs(b.x - a.x) * 0.5f * w);
    return true;
}

// The other panes and the camera preview, after the main view of the frame (raster views).
void App::renderPanes() {
    const bool game = m_game && (m_game->playing() || m_game->playerMode());
    std::vector<int> drawn;
    m_renderer.beginViews();
    if (m_panes.size() > 1 && !m_paneMax)
        for (int i = 0; i < (int)m_panes.size(); ++i) {
            const ViewPane& p = m_panes[i];
            if (i == m_activePane || p.rc.w < 18 || p.rc.h < 18) continue;
            const Camera c = paneCamera(i);
            RenderSettings rs = m_rs;
            rs.editorGridAxis = std::max(std::max(m_rs.editorGrid, m_scene.gridScale) * 2.5f, (c.ortho ? c.orthoHeight : c.distance) * 0.45f);
            const float w = p.rc.w - 2, h = p.rc.h - 2;   // its image inside the frame, the size of the active pane's (drawPanes)
            float2 centre(0, 0);
            float radius = 100;
            selectionInView(c, w, h, centre, radius);
            m_renderer.renderView(i, m_scene, c, rs, (uint32_t)w, (uint32_t)h, centre, radius);
            drawn.push_back(i);
        }
    // the preview of the selected (or pinned) camera
    m_previewShown = false;
    const int pc = previewCamera();
    if (pc >= 0 && !game && m_previewRc.w >= 16 && m_previewRc.h >= 16) {
        const Camera c = cameraOfObject(m_scene.objects[pc]);
        RenderSettings rs = m_rs;
        rs.editorGrid = 0;   // the picture of the camera: no editor grid
        float2 centre(0, 0);
        float radius = 100;
        selectionInView(c, m_previewRc.w, m_previewRc.h, centre, radius);
        m_renderer.renderView(kPreviewSlot, m_scene, c, rs, (uint32_t)m_previewRc.w, (uint32_t)m_previewRc.h, centre, radius);
        drawn.push_back(kPreviewSlot);
        m_previewShown = true;
    }
    m_renderer.endViews();
    for (int i : drawn) m_gui.viewTexture(i);   // (an image recreated at a new size gets its view again)
}

// The camera object shown in the preview: the pinned one, else the selected camera (not the one the pane looks through).
int App::previewCamera() const {
    int idx = m_previewPin ? cameraObject(m_previewPin) : -1;
    if (idx < 0 && m_selected >= 0 && m_selected < (int)m_scene.objects.size() && m_scene.objects[m_selected].camera) idx = m_selected;
    if (idx < 0) return -1;
    const ViewPane& p = m_panes[m_activePane];
    if (p.cam == VCAM_OBJECT && p.object == m_scene.objects[idx].id) return -1;
    return idx;
}

// --------------------------------------------------------------------------- Space: the view menu

// Top, side, front and persp around the centre (north, east, south, west), the camera objects of the scene in a list
// under it (the render camera first), with their pills.
std::vector<App::ViewMenuItem> App::viewMenuItems() const {
    std::vector<ViewMenuItem> v = {{0, VCAM_TOP, 0, "Top"}, {2, VCAM_SIDE, 0, "Side"}, {4, VCAM_FRONT, 0, "Front"}, {6, VCAM_PERSP, 0, "Persp"}};
    const ImVec2 c(m_vm.x, m_vm.y);
    for (ViewMenuItem& it : v) pillRect(c, it.dir, it.label, m_dpi, it.b0, it.b1);
    std::vector<int> cams;
    const int rc = renderCamera();
    if (rc >= 0) cams.push_back(rc);
    for (size_t i = 0; i < m_scene.objects.size(); ++i)
        if (m_scene.objects[i].camera && (int)i != rc) cams.push_back((int)i);
    float y = v[2].b1.y + 30.0f * m_dpi;   // under Front, after the "Cameras" title
    for (int i : cams) {
        const Object& o = m_scene.objects[i];
        ViewMenuItem it{-1, VCAM_OBJECT, o.id, o.name};
        const ImVec2 sz = ImGui::CalcTextSize(o.name.c_str());
        const float pw = std::max(sz.x + 18 * m_dpi, 110.0f * m_dpi), ph = sz.y + 8 * m_dpi;
        it.b0 = ImVec2(c.x - pw * 0.5f, y);
        it.b1 = ImVec2(c.x + pw * 0.5f, y + ph);
        y += ph + 4 * m_dpi;
        v.push_back(it);
    }
    return v;
}

// The item under the mouse: a camera of the list under its pill, else the view in the direction of the mouse (four
// sectors) away from the centre.
int App::viewMenuHover(const std::vector<ViewMenuItem>& items, float mx, float my) const {
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].dir < 0 && mx >= items[i].b0.x - 2 && mx <= items[i].b1.x + 2 && my >= items[i].b0.y - 2 && my <= items[i].b1.y + 2) return (int)i;
    const float dx = mx - m_vm.x, dy = my - m_vm.y;
    if (std::hypot(dx, dy) <= 22.0f * m_dpi) return -1;
    if (my > items[2].b1.y + 10.0f * m_dpi && std::fabs(dx) < std::fabs(dy) * 1.5f && items.size() > 4) return -1;   // (beside the list)
    const int dir = std::fabs(dx) > std::fabs(dy) ? (dx > 0 ? 2 : 6) : (dy > 0 ? 4 : 0);
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].dir == dir) return (int)i;
    return -1;
}

// Space pressed: the pane under the mouse is the one the menu acts on. Released: the item under the mouse (also after
// a quick flick, the menu not drawn yet: the item is then shown for a moment) makes that pane look through its camera;
// a tap (no item, menu not shown) switches the view as Maya: one view -> the four views, a pane of the layout -> that
// pane alone, and back. A left click on an item chooses it too.
void App::spaceInput(bool allowed) {
    ImGuiIO& io = ImGui::GetIO();
    ViewMenu& vm = m_vm;
    if (!vm.down) {
        if (!allowed || io.WantTextInput || io.KeyCtrl || io.KeyAlt || ImGui::IsAnyItemActive() || !ImGui::IsKeyPressed(ImGuiKey_Space, false)) return;
        vm = ViewMenu();
        vm.down = true;
        vm.x = io.MousePos.x;
        vm.y = io.MousePos.y;
        vm.t0 = ImGui::GetTime();
        vm.pane = m_activePane;
        if (!m_paneMax)
            for (int i = 0; i < (int)m_panes.size(); ++i)
                if (m_panes[i].rc.w > 1 && m_panes[i].rc.contains(vm.x, vm.y)) vm.pane = i;
        return;
    }
    if (m_opt.openMenu == "viewMenu") return;   // (captures: kept open)
    const std::vector<ViewMenuItem> items = viewMenuItems();
    vm.hover = viewMenuHover(items, io.MousePos.x, io.MousePos.y);
    if (ImGui::GetTime() - vm.t0 >= kViewMenuDelay) vm.shown = true;
    const bool click = ImGui::IsMouseClicked(0) && vm.hover >= 0;
    if (ImGui::IsKeyDown(ImGuiKey_Space) && !click) return;
    vm.down = false;
    if (vm.pane >= (int)m_panes.size()) return;
    if (vm.hover >= 0) {
        const ViewMenuItem& it = items[vm.hover];
        if (!vm.shown) {   // a quick flick: the chosen item shown for a moment
            m_vmFlash.t = ImGui::GetTime();
            m_vmFlash.label = it.label;
            m_vmFlash.centre = ImVec2(vm.x, vm.y);
            m_vmFlash.b0 = it.b0;
            m_vmFlash.b1 = it.b1;
        }
        activatePane(vm.pane);
        setPaneCamera(vm.pane, it.cam, it.object);
        status(L"View: " + fromUtf8(it.label));
    } else if (!vm.shown) {   // a tap
        if (m_panes.size() <= 1) setLayout(3);   // one view: the four views
        else {
            if (!m_paneMax) activatePane(vm.pane);
            togglePaneMax(false);
        }
    }
}

void App::drawViewMenu(ImDrawList* dl) {
    drawRadialFlash(dl, m_vmFlash);
    if (!m_vm.down || !m_vm.shown) return;
    const float d = m_dpi;
    const ImVec2 c(m_vm.x, m_vm.y), m = ImGui::GetIO().MousePos;
    const ViewPane& p = m_panes[std::clamp(m_vm.pane, 0, (int)m_panes.size() - 1)];
    const std::vector<ViewMenuItem> items = viewMenuItems();
    dl->AddCircleFilled(c, 5 * d, IM_COL32(236, 236, 226, 200), 20);
    dl->AddCircle(c, 22 * d, IM_COL32(236, 236, 226, 70), 40, 1.0f * d);
    if (m_vm.hover >= 0 && items[m_vm.hover].dir >= 0) dl->AddLine(c, m, IM_COL32(255, 226, 102, 200), 1.2f * d);
    const char* title = "Views";
    const ImVec2 ts = ImGui::CalcTextSize(title);
    dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y + 26 * d), IM_COL32(236, 236, 226, 160), title);
    if (items.size() > 4) {   // the cameras of the scene
        const char* ct = "Cameras";
        const ImVec2 cs = ImGui::CalcTextSize(ct);
        dl->AddText(ImVec2(c.x - cs.x * 0.5f, items[4].b0.y - cs.y - 4 * d), IM_COL32(236, 236, 226, 140), ct);
    }
    for (size_t i = 0; i < items.size(); ++i) {
        const ViewMenuItem& it = items[i];
        const bool current = p.cam == it.cam && (it.cam != VCAM_OBJECT || p.object == it.object);
        const std::string label = (current ? "\xE2\x80\xA2 " : "") + it.label;   // bullet: what the pane looks through
        const int ci = it.cam == VCAM_OBJECT ? cameraObject(it.object) : -1;
        const ImU32 edge = ci >= 0 && m_scene.objects[ci].camRender ? IM_COL32(255, 196, 92, 200) : 0;   // the render camera: orange
        ImVec2 b0 = it.b0, b1 = it.b1;
        if (current && it.dir >= 0) pillRect(c, it.dir, label, d, b0, b1);
        else if (current) b1.x = std::max(b1.x, b0.x + ImGui::CalcTextSize(label.c_str()).x + 18 * d);
        drawPill(dl, b0, b1, label, m_vm.hover == (int)i, 1.0f, d, edge);
    }
}

} // namespace app
