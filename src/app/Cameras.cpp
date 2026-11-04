// Cameras of the viewports, as in Maya: the built-in persp (the scene camera) and the orthographic top, front, side,
// bottom, back and left cameras, and the camera objects of the scene (Create > Camera, "New Camera from View"), one
// of them being the render camera. Each pane of the layout looks through one of them; the active pane's camera is
// m_scene.camera while it is active (the renderer and the tools use it), the others are kept in m_viewCams or in
// their camera object. The camera objects are drawn as camera icons and can be clicked.
#include "App.h"
#include "GameHost.h"

namespace app {

namespace {

// yaw / pitch of the built-in orthographic cameras (the persp camera is the scene's)
struct OrthoDir { float yaw, pitch; };
const OrthoDir kOrtho[VCAM_COUNT] = {{0, 0}, {0, kPi * 0.5f}, {0, 0}, {kPi * 0.5f, 0}, {0, -kPi * 0.5f}, {kPi, 0}, {-kPi * 0.5f, 0}};

} // namespace

const char* App::viewCamName(int k) {
    static const char* names[VCAM_COUNT] = {"persp", "top", "front", "side", "bottom", "back", "left"};
    return k >= 0 && k < VCAM_COUNT ? names[k] : "camera";
}

const char* App::viewCamLabel(int k) {
    static const char* labels[VCAM_COUNT] = {"Perspective", "Top", "Front", "Side (right)", "Bottom", "Back", "Left"};
    return k >= 0 && k < VCAM_COUNT ? labels[k] : "Camera";
}

// The built-in cameras of a scene just loaded: persp is its camera, the orthographic ones frame its objects.
void App::resetViewCams() {
    m_viewCams[VCAM_PERSP] = m_scene.camera;
    m_viewCams[VCAM_PERSP].ortho = false;
    for (ViewPane& p : m_panes)   // the camera objects of the previous scene are gone
        if (p.cam == VCAM_OBJECT && cameraObject(p.object) < 0) { p.cam = VCAM_PERSP; p.object = 0; }
    float3 mn(1e30f), mx(-1e30f);
    for (const Object& o : m_scene.objects) {
        if (!o.visible || o.mesh < 0 || o.part == "GROUND" || o.part == "WALL") continue;
        float3 a, b;
        aabbOf(o, o.position, objRot(o), a, b);
        mn = minv(mn, a);
        mx = maxv(mx, b);
    }
    if (mn.x > mx.x) { mn = float3(-5, 0, -5); mx = float3(5, 2, 5); }
    const float3 c = (mn + mx) * 0.5f;
    const float size = std::max(length(mx - mn), 4.0f);
    for (int k = VCAM_TOP; k < VCAM_COUNT; ++k) {
        Camera& v = m_viewCams[k];
        v = Camera();
        v.ortho = true;
        v.orthoHeight = size * 1.1f;
        v.target = c;
        v.yaw = kOrtho[k].yaw;
        v.pitch = kOrtho[k].pitch;
        v.distance = size * 2.0f;
        v.fovY = m_scene.camera.fovY;
        v.sensorScale = m_scene.camera.sensorScale;
    }
}

// the index of the camera object with this id, -1 when there is none
int App::cameraObject(uint32_t id) const {
    if (!id) return -1;
    for (size_t i = 0; i < m_scene.objects.size(); ++i)
        if (m_scene.objects[i].id == id && m_scene.objects[i].camera) return (int)i;
    return -1;
}

// The view of a camera object: at its position, along its -Z (its roll is not kept: the views stay upright).
Camera App::cameraOfObject(const Object& o) const {
    Camera c = m_viewCams[VCAM_PERSP];   // the lens settings the object has no field for (depth of field)
    const float3 f = normalize(qRot(objRot(o), float3(0, 0, -1)));
    c.yaw = std::atan2(-f.x, -f.z);
    c.pitch = std::asin(clampf(-f.y, -1.0f, 1.0f));
    c.distance = std::max(o.camDistance, 0.01f);
    c.target = o.position + f * c.distance;
    c.fovY = o.camFovY;
    c.ortho = o.camOrtho;
    c.orthoHeight = o.camOrthoHeight;
    return c;
}

void App::cameraToObject(Object& o, const Camera& c) {
    float3 r, u, f;
    c.basis(r, u, f);
    o.position = c.eye();
    o.rot = qNormalize(qFromBasis(r, u, -f));   // it looks along its -Z, +Y up
    o.useRot = true;
    o.camDistance = c.distance;
    o.camFovY = c.fovY;
    o.camOrtho = c.ortho;
    o.camOrthoHeight = c.orthoHeight;
}

// What a pane looks through, as it is now (the active pane: m_scene.camera).
Camera App::paneCamera(int i) const {
    if (i == m_activePane) return m_scene.camera;
    if (i < 0 || i >= (int)m_panes.size()) return m_viewCams[VCAM_PERSP];
    const ViewPane& p = m_panes[i];
    if (p.cam == VCAM_OBJECT) {
        const int o = cameraObject(p.object);
        return o >= 0 ? cameraOfObject(m_scene.objects[o]) : m_viewCams[VCAM_PERSP];
    }
    return m_viewCams[std::clamp(p.cam, 0, VCAM_COUNT - 1)];
}

std::string App::paneCameraName(int i) const {
    if (i < 0 || i >= (int)m_panes.size()) return "persp";
    const ViewPane& p = m_panes[i];
    if (p.cam == VCAM_OBJECT) {
        const int o = cameraObject(p.object);
        return o >= 0 ? m_scene.objects[o].name : std::string("persp");
    }
    return viewCamName(p.cam);
}

// The persp camera (the scene camera of the file) wherever it is.
const Camera& App::perspCamera() const {
    return m_panes[m_activePane].cam == VCAM_PERSP ? m_scene.camera : m_viewCams[VCAM_PERSP];
}

// m_scene.camera of the active pane back where it belongs (the built-in camera, or its object when it moved).
void App::storeActiveCamera() {
    ViewPane& p = m_panes[m_activePane];
    if (p.cam != VCAM_OBJECT) {
        m_viewCams[std::clamp(p.cam, 0, VCAM_COUNT - 1)] = m_scene.camera;
        return;
    }
    const int o = cameraObject(p.object);
    if (o < 0) return;
    const Camera& a = m_scene.camera;
    const Camera& b = m_objCamLoaded;
    const bool moved = length(a.target - b.target) > 1e-6f || a.yaw != b.yaw || a.pitch != b.pitch || a.distance != b.distance ||
                       a.fovY != b.fovY || a.ortho != b.ortho || a.orthoHeight != b.orthoHeight;
    if (!moved) return;
    cameraToObject(m_scene.objects[o], a);
    m_objCamLoaded = a;
}

// m_scene.camera from what the active pane looks through (a camera object deleted: back to persp).
void App::loadActiveCamera() {
    ViewPane& p = m_panes[m_activePane];
    if (p.cam == VCAM_OBJECT) {
        const int o = cameraObject(p.object);
        if (o >= 0) {
            m_scene.camera = cameraOfObject(m_scene.objects[o]);
            m_objCamLoaded = m_scene.camera;
            return;
        }
        p.cam = VCAM_PERSP;
        p.object = 0;
    }
    m_scene.camera = m_viewCams[std::clamp(p.cam, 0, VCAM_COUNT - 1)];
}

// A pane looks through a built-in camera or a camera object (VCAM_OBJECT and its id).
void App::setPaneCamera(int i, int cam, uint32_t object) {
    if (i < 0 || i >= (int)m_panes.size()) return;
    if (i == m_activePane) storeActiveCamera();
    m_panes[i].cam = cam;
    m_panes[i].object = cam == VCAM_OBJECT ? object : 0;
    if (i == m_activePane) {
        loadActiveCamera();
        cameraChanged();
    }
}

void App::activatePane(int i) {
    if (i == m_activePane || i < 0 || i >= (int)m_panes.size()) return;
    storeActiveCamera();
    m_activePane = i;
    loadActiveCamera();
    m_gz.active = false;
    m_adv.active = false;
    cameraChanged();
}

// Built-in orthographic camera in the active pane: no tumbling (Maya); orbiting pans instead.
bool App::orbitLocked() const {
    const ViewPane& p = m_panes[m_activePane];
    return p.cam != VCAM_OBJECT && p.cam != VCAM_PERSP;
}

// A camera bookmark (persp) shown in the active pane.
void App::applyViewBookmark(const Camera& c) {
    if (m_panes[m_activePane].cam != VCAM_PERSP) setPaneCamera(m_activePane, VCAM_PERSP);
    m_scene.camera = c;
    cameraChanged();
}

// --------------------------------------------------------------------------- camera objects

int App::renderCamera() const {
    for (size_t i = 0; i < m_scene.objects.size(); ++i)
        if (m_scene.objects[i].camera && m_scene.objects[i].camRender) return (int)i;
    return -1;
}

void App::setRenderCamera(int idx) {
    for (size_t i = 0; i < m_scene.objects.size(); ++i)
        if (m_scene.objects[i].camera) m_scene.objects[i].camRender = (int)i == idx;
    markDirty();
}

// A camera object where the active view is (its lens too); the first one becomes the render camera.
int App::createCameraFromView(bool render) {
    pushUndo();
    Object o;
    o.name = uniqueNodeName(render ? "RenderCam" : "Camera");
    o.part = "CAMERA";
    o.camera = true;
    o.mesh = -1;
    o.material = 0;
    cameraToObject(o, m_scene.camera);
    o.camRender = render || renderCamera() < 0;
    o.id = newNodeId();
    if (o.camRender)
        for (Object& k : m_scene.objects) k.camRender = false;
    const int idx = m_scene.addObject(o);
    select(idx);
    status(L"Camera created: " + fromUtf8(o.name), 1);
    markDirty();
    return idx;
}

// Persp / RenderCam: the active pane looks through the render camera (made from the view when there is none).
void App::lookThroughRenderCam() {
    int rc = renderCamera();
    if (rc < 0) rc = createCameraFromView(true);
    setPaneCamera(m_activePane, VCAM_OBJECT, m_scene.objects[rc].id);
}

// --demo-cameras (captures): a render camera on the right of the pivot and a camera on its left, higher, both looking
// at it and seen in the view; the latter is selected (the preview shows it), the view does not move.
void App::demoCameras() {
    const Camera keep = m_scene.camera;
    float3 r, u, f;
    keep.basis(r, u, f);
    const float d = keep.distance;
    auto aim = [&](const float3& pos) {
        Camera c = keep;
        c.ortho = false;
        const float3 v = keep.target - pos;
        const float3 dir = normalize(v);
        c.target = keep.target;
        c.distance = length(v);
        c.yaw = std::atan2(-dir.x, -dir.z);
        c.pitch = std::asin(clampf(-dir.y, -1.0f, 1.0f));
        m_scene.camera = c;
    };
    aim(keep.target + r * (0.3f * d) - f * (0.2f * d) + float3(0, 0.14f * d, 0));
    m_scene.objects[createCameraFromView(true)].camFovY = radians(58.0f);   // a wide lens: the whole scene from there
    aim(keep.target - r * (0.3f * d) - f * (0.05f * d) + float3(0, 0.2f * d, 0));
    createCameraFromView(false);
    m_scene.camera = keep;
}

void App::lookThroughSelected() {
    if (m_selected < 0 || !m_scene.objects[m_selected].camera) {
        status(L"Select a camera to look through it", 2);
        return;
    }
    setPaneCamera(m_activePane, VCAM_OBJECT, m_scene.objects[m_selected].id);
}

// Points of the camera icon (world): body box behind the lens, the image frame at its field of view, the up triangle.
// Its size stays about 34 pixels on screen (less in the small panes of a layout). false when it is not drawable (the
// pane looks through it).
bool App::cameraIcon(int idx, std::vector<std::pair<float3, float3>>& lines) const {
    lines.clear();
    const Object& o = m_scene.objects[idx];
    const ViewPane& p = m_panes[m_iconPane >= 0 ? m_iconPane : m_activePane];
    if (p.cam == VCAM_OBJECT && p.object == o.id) return false;   // (the pane looks through it)
    const float4 q = objRot(o);
    const float3 R = qRot(q, float3(1, 0, 0)), U = qRot(q, float3(0, 1, 0)), B = qRot(q, float3(0, 0, 1));
    const float k = clampf(m_rcView.h / (640.0f * m_dpi), 0.45f, 1.0f);
    const float s = 34.0f * k * m_dpi * m_scene.camera.pixelSize(o.position, m_rcView.h);
    const float3 P = o.position;
    auto box = [&](float3 c, float3 h) {
        float3 v[8];
        for (int k = 0; k < 8; ++k) v[k] = c + R * ((k & 1) ? h.x : -h.x) + U * ((k & 2) ? h.y : -h.y) + B * ((k & 4) ? h.z : -h.z);
        for (int k = 0; k < 8; ++k)
            for (int b = 1; b < 8; b <<= 1)
                if (!(k & b)) lines.push_back({v[k], v[k | b]});
    };
    box(P + B * (0.45f * s), float3(0.28f, 0.22f, 0.35f) * s);   // the body, behind the lens
    // the frame of the image: perspective, a pyramid from the lens; orthographic, a box
    const float d = 0.95f * s;
    float hh = o.camOrtho ? 0.30f * s : std::min(d * std::tan(o.camFovY * 0.5f), 1.2f * s);
    const float hw = hh * std::clamp(o.camAspect, 0.5f, 3.0f);
    const float3 lens = P - B * (0.1f * s), img = P - B * d;
    float3 c4[4];
    for (int k = 0; k < 4; ++k) c4[k] = img + R * ((k == 1 || k == 2) ? hw : -hw) + U * ((k >= 2) ? hh : -hh);
    for (int k = 0; k < 4; ++k) {
        lines.push_back({c4[k], c4[(k + 1) % 4]});
        const float3 from = o.camOrtho ? c4[k] + B * (d - 0.1f * s) : lens;
        lines.push_back({from, c4[k]});
    }
    // the up triangle over the frame
    const float3 t0 = img + U * (hh * 1.12f) - R * (hw * 0.35f), t1 = img + U * (hh * 1.12f) + R * (hw * 0.35f), t2 = img + U * (hh * 1.55f);
    lines.push_back({t0, t1});
    lines.push_back({t1, t2});
    lines.push_back({t2, t0});
    return true;
}

void App::drawCameraIcons(ImDrawList* dl) {
    if (m_game && (m_game->playing() || m_game->playerMode())) return;
    std::vector<std::pair<float3, float3>> lines;
    std::vector<int> sel = selectionAll();
    for (size_t i = 0; i < m_scene.objects.size(); ++i) {
        const Object& o = m_scene.objects[i];
        if (!o.camera || !o.visible || !cameraIcon((int)i, lines)) continue;
        const bool selected = std::find(sel.begin(), sel.end(), (int)i) != sel.end();
        const ImU32 col = selected ? IM_COL32(90, 200, 255, 255) : (o.camRender ? IM_COL32(255, 196, 92, 230) : IM_COL32(214, 220, 230, 210));
        for (const auto& l : lines) {
            float2 a, b;
            if (project(l.first, a) && project(l.second, b)) dl->AddLine(ImVec2(a.x, a.y), ImVec2(b.x, b.y), col, (selected ? 1.6f : 1.1f) * m_dpi);
        }
        float2 c;
        if (o.camRender && project(o.position, c)) dl->AddText(ImVec2(c.x + 12 * m_dpi, c.y - 22 * m_dpi), col, "R");
    }
}

// The camera icon under the mouse (its centre or its lines within 8 pixels), -1 for none.
int App::pickCameraIcon(float mx, float my) {
    std::vector<std::pair<float3, float3>> lines;
    int best = -1;
    float bestD = 8.0f * m_dpi;
    for (size_t i = 0; i < m_scene.objects.size(); ++i) {
        const Object& o = m_scene.objects[i];
        if (!o.camera || !o.visible || !o.selectable || !cameraIcon((int)i, lines)) continue;
        for (const auto& l : lines) {
            float2 a, b;
            if (!project(l.first, a) || !project(l.second, b)) continue;
            const float dx = b.x - a.x, dy = b.y - a.y, len2 = dx * dx + dy * dy;
            const float t = len2 > 1e-6f ? clampf(((mx - a.x) * dx + (my - a.y) * dy) / len2, 0, 1) : 0;
            const float d = std::hypot(a.x + dx * t - mx, a.y + dy * t - my);
            if (d < bestD) { bestD = d; best = (int)i; }
        }
    }
    return best;
}

} // namespace app
