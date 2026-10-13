// Game layer, RTS placement: houses on the grid (game::Placement), dragged in the view or out of the
// HUD, animated, and turned into scene objects. The houses are simulated during Play; the input and the
// HUD need the "Caméra RTS" node (the RTS camera) and the Scene view on screen.
#include "App.h"
#include "GameHost.h"
#include <cmath>
#include <string>

namespace app {

using namespace game;

namespace {
const float kDropHeight = 5.0f;   // a house added from the HUD falls from this height
}

// The scene view is on screen: the player, or the Scene workspace (not the Graph or the Material Studio).
bool GameHost::viewVisible() const { return m_player || m_app.m_workspace == 0; }

// Ground point under the centre of the view (the camera target when the view looks at the sky).
float3 GameHost::viewCentreGround() const {
    const ScreenRect& v = m_app.m_rcView;
    float3 o, d;
    m_app.cameraRay(v.x + v.w * 0.5f, v.y + v.h * 0.5f, o, d);
    if (std::fabs(d.y) > 1e-5f) {
        float t = -o.y / d.y;
        if (t > 0) return o + d * t;
    }
    return m_app.m_scene.camera.target;
}

// A new house on the free cell nearest to `at`, dropped from above. Returns its index or -1.
int GameHost::placeNew(int type, const float3& at) {
    if (type < 0 || type >= (int)m_build.types().size()) return -1;
    const BuildingType& t = m_build.types()[type];
    int i = m_build.add(type, m_build.grid().snapOrigin(at, t.w, t.d), kDropHeight);
    if (i >= 0) m_buildSelected = i;
    return i;
}

// Drops the house in hand: placed, back to its place, or (a new one that is not placed) removed.
void GameHost::finishDrag(bool cancelled) {
    int idx = m_build.dragging();
    bool isNew = m_build.draggingNew();
    DropResult r = m_build.endDrag(cancelled);
    if (r == DropResult::Placed) {
        m_buildSelected = idx;
        toast(isNew ? L"House placed" : L"House moved");
    } else if (r == DropResult::Returned) {
        m_buildSelected = idx;
        if (!cancelled) toast(L"No room there: the house goes back");
    } else if (isNew) {
        removeBuilding(idx);
        if (!cancelled) toast(L"Not placed: drop it on the grid");
    }
}

void GameHost::removeBuilding(int idx) {
    std::vector<game::Building>& bs = m_build.buildings();
    if (idx < 0 || idx >= (int)bs.size()) return;
    if (objectAlive(bs[idx].object)) destroy(bs[idx].object);
    m_buildMat.erase(bs[idx].id);
    m_build.remove(idx);
    m_buildHover = m_buildPress = m_buildSelected = -1;
}

// Stop: the scene is restored, so the houses and their materials go with it.
void GameHost::clearBuildings() {
    m_build.clear();
    m_buildMat.clear();
    m_invalidMat = -1;
    m_buildHover = m_buildPress = m_buildSelected = -1;
    m_buildHud = m_hudDrag = false;
}

bool GameHost::isBuilding(int obj) const {
    if (obj < 0) return false;
    for (const game::Building& b : m_build.buildings())
        if (b.object == obj) return true;
    return false;
}

// Red material of an invalid drop (created on first use in a Play session).
int GameHost::invalidMaterial() {
    Scene& s = m_app.m_scene;
    if (m_invalidMat >= 0 && m_invalidMat < (int)s.materials.size()) return m_invalidMat;
    Material m;
    m.name = "Invalid placement";
    m.baseColor = float3(srgbToLinear(0.95f), srgbToLinear(0.15f), srgbToLinear(0.12f));
    m.colorId = -1;
    s.materials.push_back(m);
    m_invalidMat = (int)s.materials.size() - 1;
    return m_invalidMat;
}

// Each house is a scene object: a CUBE scaled to the footprint and the height, turned, tilted and
// squashed by the animation. A house in hand over an invalid cell turns red.
void GameHost::syncBuildings() {
    Scene& s = m_app.m_scene;
    const std::vector<BuildingType>& types = m_build.types();
    std::vector<game::Building>& bs = m_build.buildings();
    for (int i = 0; i < (int)bs.size(); ++i) {
        game::Building& b = bs[i];
        const BuildingType& t = types[b.type];
        if (!objectAlive(b.object)) {
            b.object = spawn("CUBE", b.pos, t.color);
            if (b.object < 0) continue;
            s.objects[b.object].name = uniqueName(t.name);
            m_buildMat[b.id] = s.objects[b.object].material;
        }
        Object& o = s.objects[b.object];
        // the CUBE part is 6 units wide and stands on y = 0; the house fills 90 % of its cells
        float k = b.hover;
        float sx = (1 + b.squash * 0.5f) * k * b.footW(t) * 0.9f / 6.0f;
        float sz = (1 + b.squash * 0.5f) * k * b.footD(t) * 0.9f / 6.0f;
        float sy = (1 - b.squash) * k * t.height / 6.0f;
        o.position = b.pos;
        o.useRot = true;
        o.rot = qMul(qAxisAngle(float3(0, 1, 0), b.yaw), qMul(qAxisAngle(float3(1, 0, 0), b.tiltX), qAxisAngle(float3(0, 0, 1), b.tiltZ)));
        o.scale = float3(sx, sy, sz);
        bool invalid = i == m_build.dragging() && !m_build.dragValid();
        auto own = m_buildMat.find(b.id);
        o.material = invalid ? invalidMaterial() : (own != m_buildMat.end() ? own->second : o.material);
    }
    if (!bs.empty()) m_app.markDirty();
}

// Every frame of Play: the house under the cursor, the press and the drag on the grid, the keys
// R (turn) and Del (remove), the animation and the sync with the scene.
void GameHost::updatePlacement(float dt) {
    ImGuiIO& io = ImGui::GetIO();
    const ScreenRect& v = m_app.m_rcView;
    const bool active = m_rtsCam && viewVisible();
    const bool inView = active && !m_buildHud && v.contains(io.MousePos.x, io.MousePos.y) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
    if (!active && m_build.dragging() >= 0) finishDrag(true);

    int hovered = -1;
    if (inView && m_build.dragging() < 0 && !m_build.buildings().empty()) {
        float3 o, d;
        m_app.cameraRay(io.MousePos.x, io.MousePos.y, o, d);
        int obj = pick(m_app.m_scene, o, d).object;
        if (obj >= 0)
            for (int i = 0; i < (int)m_build.buildings().size(); ++i)
                if (m_build.buildings()[i].object == obj) hovered = i;
    }
    m_buildHover = hovered;

    // a press on a house selects it; a move of more than 6 px takes it in hand
    float3 ground;
    bool hasGround = groundUnderMouse(ground);
    if (inView && ImGui::IsMouseClicked(0)) {
        m_buildSelected = hovered;
        m_buildPress = hovered;
        m_buildPressPos = io.MousePos;
    }
    if (m_buildPress >= 0 && m_build.dragging() < 0) {
        float dx = io.MousePos.x - m_buildPressPos.x, dy = io.MousePos.y - m_buildPressPos.y;
        if (!ImGui::IsMouseDown(0)) {
            m_buildPress = -1;
        } else if (hasGround && dx * dx + dy * dy > 36.0f) {
            m_build.beginDrag(m_buildPress, ground, false);
            m_buildPress = -1;
        }
    }
    // the house in hand follows the ground under the cursor; released, it drops
    if (m_build.dragging() >= 0) {
        if (!ImGui::IsMouseDown(0)) finishDrag(false);
        else if (hasGround) m_build.updateDrag(ground, inView);
    }
    if (active && !io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
            int i = m_build.dragging() >= 0 ? m_build.dragging() : m_buildSelected;
            if (i >= 0) m_build.rotate(i);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && m_build.dragging() < 0 && m_buildSelected >= 0) {
            removeBuilding(m_buildSelected);
            toast(L"House removed");
        }
    }
    m_build.simulate(dt, m_buildHover);
    syncBuildings();
}

// RTS HUD, bottom left of the view: one button per house type. A click adds a house at the centre of
// the view; a drag takes it from the button onto the grid.
void GameHost::drawRtsHud() {
    const ScreenRect& v = m_app.m_rcView;
    float d = m_app.m_dpi;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(v.x + 12 * d, v.y + v.h - 12 * d), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.85f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize;
    if (ImGui::Begin("##rtshud", nullptr, flags)) {
        m_buildHud = ImGui::IsWindowHovered();
        const std::vector<BuildingType>& types = m_build.types();
        for (int t = 0; t < (int)types.size(); ++t) {
            ImGui::PushID(t);
            std::string label = "+ Add " + types[t].name;
            ImGui::Button(label.c_str());
            if (ImGui::IsItemActivated()) m_hudPress = io.MousePos;
            if (ImGui::IsItemActive() && !m_hudDrag && m_build.dragging() < 0) {
                float dx = io.MousePos.x - m_hudPress.x, dy = io.MousePos.y - m_hudPress.y;
                float3 g;
                if (dx * dx + dy * dy > 36.0f && groundUnderMouse(g)) {
                    m_build.beginNewDrag(t, g);
                    m_hudDrag = true;
                }
            }
            if (ImGui::IsItemDeactivated()) {
                if (!m_hudDrag) {
                    if (placeNew(t, viewCentreGround()) < 0) toast(L"No free space on the grid");
                    else toast(L"House added");
                }
                m_hudDrag = false;
            }
            ImGui::PopID();
            ImGui::SameLine();
        }
        ImGui::NewLine();
        ImGui::TextDisabled("Drag a house onto the grid  ·  R rotate  ·  Del delete");
    }
    ImGui::End();
    drawSunDial();
}

// The sun: its direction from the azimuth (around the vertical) and the height.
void GameHost::updateSun() {
    float ce = std::cos(m_sunElevation);
    m_app.m_scene.sun.direction = normalize(float3(std::sin(m_sunAzimuth) * ce, std::sin(m_sunElevation), std::cos(m_sunAzimuth) * ce));
}

// Sun dial, top right of the view. The top of the dial is where the camera looks: the sun sits on the
// ring (drag it to turn the light), the arrow in the centre gives the direction of the shadows, and the
// slider sets the height of the sun.
void GameHost::drawSunDial() {
    const ScreenRect& v = m_app.m_rcView;
    ImGuiIO& io = ImGui::GetIO();
    const float d = m_app.m_dpi;
    const float R = 62 * d;
    const ImVec2 c(v.r() - R - 28 * d, v.y + 180 * d + R);   // below the viewport statistics box
    ImGui::SetNextWindowPos(ImVec2(c.x - R - 20 * d, c.y - R - 20 * d), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(2 * R + 40 * d, 2 * R + 80 * d), ImGuiCond_Always);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBackground;
    if (ImGui::Begin("##sundial", nullptr, flags)) {
        if (ImGui::IsWindowHovered()) m_buildHud = true;
        ImGui::SetCursorScreenPos(ImVec2(c.x - R - 14 * d, c.y - R - 14 * d));
        ImGui::InvisibleButton("##dial", ImVec2(2 * R + 28 * d, 2 * R + 28 * d));
        // the view on the ground: f = forward, r = right (the dial turns with the camera)
        float3 right, up, fwd;
        m_app.m_scene.camera.basis(right, up, fwd);
        float3 f(fwd.x, 0, fwd.z), r(right.x, 0, right.z);
        f = length(f) > 1e-4f ? normalize(f) : float3(0, 0, 1);
        r = length(r) > 1e-4f ? normalize(r) : float3(1, 0, 0);
        if (ImGui::IsItemActivated()) m_sunGrabAnim = 1.0f;
        if (ImGui::IsItemActive()) {
            m_buildHud = true;   // the camera does not move while the dial is held
            float dx = io.MousePos.x - c.x, dy = io.MousePos.y - c.y;
            if (dx * dx + dy * dy > 4.0f) {
                float a = std::atan2(dx, -dy);   // 0 at the top, clockwise
                float3 h = f * std::cos(a) + r * std::sin(a);
                m_sunAzimuth = std::atan2(h.x, h.z);
            }
        }
        m_sunGrabAnim = std::max(0.0f, m_sunGrabAnim - io.DeltaTime * 3.0f);
        float3 sunH(std::sin(m_sunAzimuth), 0, std::cos(m_sunAzimuth));
        float th = std::atan2(dot(sunH, r), dot(sunH, f));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddCircleFilled(c, R, IM_COL32(20, 26, 36, 190), 64);
        dl->AddCircle(c, R, IM_COL32(230, 236, 245, 230), 64, 3 * d);
        dl->AddCircle(c, R * 0.55f, IM_COL32(230, 236, 245, 60), 48, 1 * d);
        // arrow in the centre: the direction of the shadows (opposite of the sun)
        ImVec2 ad(-std::sin(th), std::cos(th));
        ImVec2 tip(c.x + ad.x * R * 0.42f, c.y + ad.y * R * 0.42f), tail(c.x - ad.x * R * 0.18f, c.y - ad.y * R * 0.18f);
        dl->AddLine(tail, tip, IM_COL32(200, 210, 225, 220), 3 * d);
        ImVec2 pr(-ad.y, ad.x);
        float ah = 9 * d;
        dl->AddTriangleFilled(ImVec2(tip.x + ad.x * ah, tip.y + ad.y * ah), ImVec2(tip.x + pr.x * ah * 0.7f, tip.y + pr.y * ah * 0.7f),
                              ImVec2(tip.x - pr.x * ah * 0.7f, tip.y - pr.y * ah * 0.7f), IM_COL32(200, 210, 225, 220));
        // the sun on the ring (it bounces when grabbed)
        ImVec2 sp(c.x + std::sin(th) * R, c.y - std::cos(th) * R);
        float pulse = 1.0f + 0.35f * std::sin(m_sunGrabAnim * 3.14159265f) + (ImGui::IsItemActive() ? 0.12f : 0.0f);
        float sr = 11 * d * pulse;
        float t = (float)ImGui::GetTime();
        for (int k = 0; k < 8; ++k) {
            float a = k * 3.14159265f / 4 + t * 0.5f;
            dl->AddLine(ImVec2(sp.x + std::cos(a) * sr * 1.3f, sp.y + std::sin(a) * sr * 1.3f),
                        ImVec2(sp.x + std::cos(a) * sr * 1.8f, sp.y + std::sin(a) * sr * 1.8f), IM_COL32(255, 210, 80, 255), 2.5f * d);
        }
        dl->AddCircleFilled(sp, sr, IM_COL32(255, 214, 90, 255), 24);
        dl->AddCircle(sp, sr, IM_COL32(200, 120, 20, 255), 24, 1.5f * d);
        // height of the sun
        ImGui::SetCursorScreenPos(ImVec2(c.x - R, c.y + R + 22 * d));
        ImGui::SetNextItemWidth(2 * R);
        float elev = degrees(m_sunElevation);
        if (ImGui::SliderFloat("##elev", &elev, 10.0f, 85.0f, "Height %.0f°")) m_sunElevation = radians(elev);
        if (ImGui::IsItemActive()) m_buildHud = true;
    }
    ImGui::End();
    updateSun();
}

} // namespace app
