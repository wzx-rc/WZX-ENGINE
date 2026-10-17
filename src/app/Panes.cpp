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
    const int n = (int)m_panes.size();
    if (n <= 1 || m_paneMax) {
        for (int i = 0; i < n; ++i) m_panes[i].rc = i == m_activePane ? area : ScreenRect{0, 0, 0, 0};
        return area;
    }
    const int cols = kCols[m_layout], rows = kRows[m_layout];
    const float gap = 3.0f * m_dpi;
    const float cw = (area.w - gap * (cols - 1)) / cols, ch = (area.h - gap * (rows - 1)) / rows;
    for (int i = 0; i < n; ++i) {
        const int c = i % cols, r = i / cols;
        m_panes[i].rc = {std::floor(area.x + c * (cw + gap)), std::floor(area.y + r * (ch + gap)), std::floor(cw), std::floor(ch)};
    }
    return m_panes[m_activePane].rc;
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

} // namespace app
