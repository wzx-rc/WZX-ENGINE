// Game layer, RTS placement: houses on the grid (game::Placement), dragged in the view or out of the
// HUD, animated, and turned into scene objects. Two places to place them: the village mode of the editor
// (Scene view, not Play) and during Play, whatever the "Caméra RTS" node does. The village is saved with the scene.
#include "App.h"
#include "GameHost.h"
#include "vp3d/AssetImport.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace app {

using namespace game;

namespace {

const float kDropHeight = 5.0f;   // a house added from the HUD falls from this height

// Ray against an axis-aligned box; `tHit` is the distance along the ray (in the units of `d`).
bool slabHit(const float3& o, const float3& d, const float3& mn, const float3& mx, float& tHit) {
    const float ov[3] = {o.x, o.y, o.z}, dv[3] = {d.x, d.y, d.z}, lo[3] = {mn.x, mn.y, mn.z}, hi[3] = {mx.x, mx.y, mx.z};
    float t0 = 0.0f, t1 = 1e30f;
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(dv[i]) < 1e-9f) {
            if (ov[i] < lo[i] || ov[i] > hi[i]) return false;
            continue;
        }
        float a = (lo[i] - ov[i]) / dv[i], b = (hi[i] - ov[i]) / dv[i];
        if (a > b) std::swap(a, b);
        t0 = std::max(t0, a);
        t1 = std::min(t1, b);
        if (t0 > t1) return false;
    }
    tHit = t0;
    return true;
}

bool differs(const float3& a, const float3& b) { return a.x != b.x || a.y != b.y || a.z != b.z; }
bool differs(const float4& a, const float4& b) { return a.x != b.x || a.y != b.y || a.z != b.z || a.w != b.w; }

}

// The scene view is on screen: the player, or the Scene workspace (not the Graph or the Material Studio).
bool GameHost::viewVisible() const { return m_player || m_app.m_workspace == 0; }

// The houses take the mouse and the keys: during Play (the houses of the graph), or the village mode of the editor.
// The "Caméra RTS" node is not needed for them: it only adds the camera, the sun dial and the sun.
bool GameHost::placeActive() const { return viewVisible() && (m_playing || m_placeMode); }

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
    m_build.remove(idx);
    m_buildHover = m_buildPress = m_buildSelected = -1;
}

// Selection, hover, drag and HUD state (the village itself is kept by the callers).
void GameHost::resetTransient() {
    m_invalidMat = -1;
    m_buildHover = m_buildPress = m_buildSelected = -1;
    m_buildHud = m_hudDrag = false;
}

// A new scene: the village is emptied (its houses went with the old scene).
void GameHost::resetVillage() {
    m_build.clear();
    m_saveBuild = m_build;
    resetTransient();
}

// The village mode of the editor: houses are placed on the Scene view, not during Play.
void GameHost::togglePlacement() {
    if (m_playing) {
        toast(L"Stop Play to place houses");
        return;
    }
    if (m_placeMode) {
        finishDrag(true);
        m_buildSelected = -1;
        m_placeMode = false;
        return;
    }
    if (!ensureHouseModel()) return;
    m_placeMode = true;
}

// The village as saved in the scene file: the placed houses by type name, cell and turn. During Play the
// village of the editor is saved, not the houses placed in the Play session.
std::string GameHost::villageJson() const {
    const Placement& v = m_playing ? m_saveBuild : m_build;
    std::string out = "[";
    bool first = true;
    for (const game::Building& b : v.buildings()) {
        if (!b.placed) continue;
        out += first ? "\n    " : ",\n    ";
        out += "{\"type\": \"" + jsonEscape(v.types()[b.type].name) + "\", \"cell\": [" + std::to_string(b.cell.x) + ", " +
               std::to_string(b.cell.z) + "], \"rot\": " + std::to_string(b.rot) + "}";
        first = false;
    }
    return out + (first ? "]" : "\n  ]");
}

// The village of a scene file: each house on its cell and turn. Its object follows in the next sync.
void GameHost::loadVillage(const Json& list) {
    m_build.clear();
    if (list.type == Json::Array) {
        for (const Json& e : list.a) {
            const std::string type = e["type"].str();
            int t = -1;
            for (int k = 0; k < (int)m_build.types().size(); ++k)
                if (m_build.types()[k].name == type) t = k;
            if (t < 0) continue;
            m_build.place(t, Cell{(int)e["cell"][0].num(0), (int)e["cell"][1].num(0)}, (int)e["rot"].num(0));
        }
    }
    resetTransient();
}

bool GameHost::hasBuilding(int id) const {
    for (const game::Building& b : m_build.buildings())
        if (b.id == id) return true;
    return false;
}

// The house under the ray, from its box in the object's frame (the scene's own picking skips the houses).
int GameHost::houseUnder(const float3& o, const float3& d, float& tHit) const {
    int best = -1;
    tHit = 1e30f;
    const Scene& s = m_app.m_scene;
    for (int i = 0; i < (int)m_build.buildings().size(); ++i) {
        const game::Building& b = m_build.buildings()[i];
        if (!objectAlive(b.object)) continue;
        Affine inv = s.objects[b.object].transform().inverse();
        float t = 0;
        if (slabHit(inv.point(o), inv.vector(d), m_houseMin - float3(1e-3f), m_houseMax + float3(1e-3f), t) && t < tHit) {
            tHit = t;
            best = i;
        }
    }
    return best;
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

// The house model of the RTS starter (assets/HOUSE): the OBJ, its base colour image and one material for
// every house. The mesh and the image stay in the scene; the material is made again after each Stop,
// which restores the materials of the scene.
bool GameHost::ensureHouseModel() {
    Scene& s = m_app.m_scene;
    const bool loaded = m_houseMesh >= 0 && m_houseMesh < (int)s.meshNames.size() && s.meshNames[m_houseMesh] == "House01_low";
    if (!loaded) {
        std::wstring dir = m_app.m_exeDir + L"\\assets\\HOUSE\\";
        vp3d::MeshData mesh;
        vp3d::ObjInfo info;
        std::string err;
        if (!vp3d::loadObj(dir + L"House01_low.obj", mesh, &info, &err)) {
            if (!m_houseFailed) toast(L"House model not loaded: " + fromUtf8(err));
            m_houseFailed = true;
            return false;
        }
        // centimetres; the base on y = 0 and centred on x / z (the pivot of a house is its footprint centre)
        float3 mn, mx;
        mesh.bounds(mn, mx);
        float3 shift((mn.x + mx.x) * 0.5f, mn.y, (mn.z + mx.z) * 0.5f);
        for (vp3d::Vertex& v : mesh.vertices) v.pos -= shift;
        mesh.bounds(m_houseMin, m_houseMax);
        // base colour: the image named by the .mtl, else the *basecolor* image of the folder (the export names a .bmp that is not there)
        std::wstring texPath;
        if (!info.diffuseMap.empty() && GetFileAttributesW((dir + fromUtf8(info.diffuseMap)).c_str()) != INVALID_FILE_ATTRIBUTES) {
            texPath = dir + fromUtf8(info.diffuseMap);
        } else {
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW((dir + L"*basecolor*.*").c_str(), &fd);
            if (h != INVALID_HANDLE_VALUE) {
                texPath = dir + fd.cFileName;
                FindClose(h);
            }
        }
        m_houseTex = -1;
        if (!texPath.empty()) {
            vp3d::Texture tex;
            std::string terr;
            if (vp3d::loadImage(texPath, tex, &terr, 2048)) m_houseTex = s.addTexture(std::move(tex));
            else toast(L"Texture not loaded: " + fromUtf8(terr));
        }
        m_houseMesh = s.addMesh("House01_low", std::move(mesh));
    }
    if (m_houseMat < 0 || m_houseMat >= (int)s.materials.size() || s.materials[m_houseMat].name != "House") {
        Material hm;
        hm.name = "House";
        hm.baseColor = float3(1, 1, 1);
        hm.roughness = 0.75f;
        hm.specular = 0.3f;
        hm.baseColorTex = m_houseTex;
        if (m_houseTex < 0) hm.baseColor = float3(0.6f, 0.45f, 0.35f);
        m_houseMat = s.addMaterial(hm);
    }
    m_houseFailed = false;
    return true;
}

// Each house is a scene object of part "House" (grab = its building id): the model scaled to its type,
// turned, tilted and squashed by the animation; a house in hand over an invalid cell turns red. The objects
// are not selectable (the village is edited from its mode only). An object whose building is gone is hidden,
// not erased, so the indices of the other objects stay valid.
void GameHost::syncBuildings() {
    Scene& s = m_app.m_scene;
    const std::vector<BuildingType>& types = m_build.types();
    std::vector<game::Building>& bs = m_build.buildings();
    bool changed = false;
    for (int i = 0; i < (int)bs.size(); ++i) {
        game::Building& b = bs[i];
        const BuildingType& t = types[b.type];
        const bool owned = objectAlive(b.object) && s.objects[b.object].part == "House" && s.objects[b.object].grab == b.id;
        if (!owned) {
            if (!ensureHouseModel()) return;
            Object fresh;
            fresh.name = uniqueName(t.name);
            fresh.part = "House";
            fresh.grab = b.id;
            fresh.mesh = m_houseMesh;
            fresh.material = m_houseMat;
            fresh.position = b.pos;
            fresh.useRot = true;
            fresh.selectable = false;
            b.object = s.addObject(fresh);
            changed = true;
        }
        Object& o = s.objects[b.object];
        // the model is scaled to its type; the squash widens it and flattens it, the hover grows it
        float k = b.hover * t.modelScale;
        float3 scale((1 + b.squash * 0.5f) * k, (1 - b.squash) * k, (1 + b.squash * 0.5f) * k);
        float4 rot = qMul(qAxisAngle(float3(0, 1, 0), b.yaw), qMul(qAxisAngle(float3(1, 0, 0), b.tiltX), qAxisAngle(float3(0, 0, 1), b.tiltZ)));
        int material = (i == m_build.dragging() && !m_build.dragValid()) ? invalidMaterial() : m_houseMat;
        if (differs(o.position, b.pos) || differs(o.scale, scale) || differs(o.rot, rot) || o.material != material) changed = true;
        o.position = b.pos;
        o.useRot = true;
        o.rot = rot;
        o.scale = scale;
        o.material = material;
    }
    for (vp3d::Object& o : s.objects)
        if (o.part == "House" && o.visible && !hasBuilding(o.grab)) {
            o.visible = false;
            changed = true;
        }
    if (changed) m_app.markDirty();
}

// Every frame: the house under the cursor, the press and the drag on the grid, the keys R (turn) and Del
// (remove), the animation and the sync with the scene. The input works while placeActive(); the houses
// follow their village in every mode.
void GameHost::updatePlacement(float dt) {
    ImGuiIO& io = ImGui::GetIO();
    const ScreenRect& v = m_app.m_rcView;
    const bool active = placeActive();
    const bool inView = active && !m_buildHud && v.contains(io.MousePos.x, io.MousePos.y) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
    if (!active && m_build.dragging() >= 0) finishDrag(true);

    int hovered = -1;
    if (inView && m_build.dragging() < 0 && !m_build.buildings().empty()) {
        float3 o, d;
        m_app.cameraRay(io.MousePos.x, io.MousePos.y, o, d);
        float tHouse = 0;
        int h = houseUnder(o, d, tHouse);
        // a house behind a nearer object of the scene is not the one under the cursor
        if (h >= 0 && tHouse <= pick(m_app.m_scene, o, d).t) hovered = h;
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
// the view; a drag takes it from the button onto the grid. The sun dial comes with the RTS camera (Play).
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
        if (m_playing) {   // Play: the buttons declared by the graph
            for (size_t k = 0; k < m_hudButtons.size(); ++k) {
                ImGui::PushID((int)k);
                const std::string label = m_hudButtons[k].second;
                const std::string id = m_hudButtons[k].first;
                if (ImGui::Button(label.c_str())) m_rt.fireHudButton(id);
                ImGui::PopID();
                ImGui::SameLine();
            }
            ImGui::NewLine();
        }
        const std::vector<BuildingType>& types = m_build.types();
        for (int t = 0; t < (int)types.size() && !m_playing; ++t) {   // village mode of the editor: built-in buttons
            ImGui::PushID(t);
            std::string label = "+ Add " + types[t].name;
            const bool clicked = ImGui::Button(label.c_str());   // true only for a click: pressed and released on the button
            if (ImGui::IsItemActivated()) m_hudPress = io.MousePos;
            if (ImGui::IsItemActive() && ImGui::IsMouseDown(0) && !m_hudDrag && m_build.dragging() < 0) {
                float dx = io.MousePos.x - m_hudPress.x, dy = io.MousePos.y - m_hudPress.y;
                float3 g;
                if (dx * dx + dy * dy > 36.0f && groundUnderMouse(g)) {
                    m_build.beginNewDrag(t, g);
                    m_hudDrag = true;
                }
            }
            if (clicked && !m_hudDrag) {
                if (placeNew(t, viewCentreGround()) < 0) toast(L"No free space on the grid");
                else toast(L"House added");
            }
            if (ImGui::IsItemDeactivated()) m_hudDrag = false;
            ImGui::PopID();
            ImGui::SameLine();
        }
        ImGui::NewLine();
        ImGui::TextDisabled("Drag a house onto the grid  ·  R rotate  ·  Del delete");
    }
    ImGui::End();
    if (m_rtsCam) drawSunDial();
}

// Graph nodes of the houses (World): the ids are stable, so a node keeps its house when another is removed.
int GameHost::houseIndex(int id) const {
    for (int i = 0; i < (int)m_build.buildings().size(); ++i)
        if (m_build.buildings()[i].id == id) return i;
    return -1;
}

int GameHost::placeHouse(int type, int cx, int cz, int rot) {
    if (type < 0 || type >= (int)m_build.types().size()) return -1;
    int idx = m_build.place(type, Cell{cx, cz}, rot, kDropHeight);   // falls onto its cells, as the HUD houses do
    return idx < 0 ? -1 : m_build.buildings()[idx].id;
}

bool GameHost::removeHouse(int id) {
    int i = houseIndex(id);
    if (i < 0) return false;
    removeBuilding(i);
    return true;
}

bool GameHost::turnHouse(int id) {
    int i = houseIndex(id);
    return i >= 0 && m_build.rotate(i);
}

int GameHost::houseAt(int cx, int cz) const {
    for (const game::Building& b : m_build.buildings()) {
        if (!b.placed) continue;
        const BuildingType& t = m_build.types()[b.type];
        if (cx >= b.cell.x && cx < b.cell.x + b.footW(t) && cz >= b.cell.z && cz < b.cell.z + b.footD(t)) return b.id;
    }
    return -1;
}

int GameHost::houseCount() const {
    int n = 0;
    for (const game::Building& b : m_build.buildings()) n += b.placed;
    return n;
}

// HUD buttons and the sun, from the graph (Play). The buttons are declared by "HUD Button" nodes.
void GameHost::hudButton(const std::string& id, const std::string& label) {
    for (auto& b : m_hudButtons)
        if (b.first == id) {
            b.second = label;
            return;
        }
    m_hudButtons.push_back({id, label});
}

// The house under the cursor is outlined like a selection: its halo while the cursor is on it (AppRts.cpp, updatePlacement).
void GameHost::addHoverOutline(std::vector<int>& objects) const {
    if (m_buildHover < 0 || m_buildHover >= (int)m_build.buildings().size()) return;
    const int obj = m_build.buildings()[m_buildHover].object;
    if (objectAlive(obj)) objects.push_back(obj);
}

void GameHost::setSun(float azimuth, float height) {
    m_app.m_scene.sun.enabled = true;   // the Set Sun node lights the RTS (Stop gives back the scene's sun)
    m_sunAzimuth = radians(azimuth);
    m_sunElevation = radians(clampf(height, 10.0f, 85.0f));
    updateSun();
}

void GameHost::cellAt(float x, float z, int& cx, int& cz) const {
    Cell c = m_build.grid().snapOrigin(float3(x, 0, z), 1, 1);
    cx = c.x;
    cz = c.z;
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
