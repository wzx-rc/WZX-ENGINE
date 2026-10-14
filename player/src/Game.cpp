// The game of the player (see Game.h). Same rules as the Play mode of the editor: GameHost (src/app/AppGame.cpp)
// for the graph, the clicks, the keys and the RTS camera; AppRts.cpp for the houses, the HUD and the sun dial.
#include "Game.h"
#include "app/Content.h"
#include "vp3d/AssetImport.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using namespace vp3d;
using game::BuildingType;
using game::Cell;
using game::DropResult;

namespace player {

namespace {

const float kDropHeight = 5.0f;   // a house added from the HUD falls from this height

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

fs::path u8(const std::string& s) { return fs::u8path(s); }

} // namespace

// --------------------------------------------------------------------------- setup

bool Game::init(Scene* scene, const std::string& dataDir, float uiScale, bool touch, std::string* error) {
    m_scene = scene;
    m_dataDir = dataDir;
    m_s = uiScale;
    m_touch = touch;
    m_build.init(20, 20, 1.0f);   // the RTS grid of the editor: 20 x 20 cells of 1 unit
    const Camera& c = m_scene->camera;
    m_cam.reset(c.target, c.yaw, std::min(c.distance, m_cam.maxDistance));   // framed as the first view of the editor

    std::ifstream f(u8(dataDir + "/game/main.graph.json"), std::ios::binary);
    if (!f) {
        if (error) *error = "Game graph not found: " + dataDir + "/game/main.graph.json";
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string err;
    if (!m_graph.fromJson(ss.str(), &err)) {
        if (error) *error = "Game graph not loaded: " + err;
        return false;
    }
    ensureHouseModel();
    m_rt.start(&m_graph, this);   // Play
    return true;
}

// --------------------------------------------------------------------------- frame

void Game::frame(float dt, float viewW, float viewH, const CameraControls& cc) {
    m_viewW = std::max(1.0f, viewW);
    m_viewH = std::max(1.0f, viewH);
    m_time += dt;
    // the HUD of the previous frame tells whether the pointer is on it
    playInput();
    m_rt.tick(dt);
    updateRtsCamera(dt, cc);
    if (m_rtsCam) updateSun();
    updatePlacement(dt);

    m_buildHud = false;
    m_touchKeys.clear();
    drawRtsHud();
    drawToasts();
}

void Game::cameraRay(float px, float py, float3& o, float3& d) const {
    const Camera& c = m_scene->camera;
    float3 right, up, fwd;
    c.basis(right, up, fwd);
    float u = px / m_viewW, v = py / m_viewH;
    float t = std::tan(c.fovY * 0.5f);
    float aspect = m_viewW / m_viewH;
    o = c.eye();
    d = normalize(fwd + right * ((2 * u - 1) * aspect * t) + up * ((1 - 2 * v) * t));
}

bool Game::groundAt(float px, float py, float3& p) const {
    float3 o, d;
    cameraRay(px, py, o, d);
    if (std::fabs(d.y) < 1e-5f) return false;
    float t = -o.y / d.y;
    if (t <= 0) return false;
    p = o + d * t;
    return true;
}

bool Game::groundUnderMouse(float3& p) const {
    ImVec2 m = ImGui::GetIO().MousePos;
    return groundAt(m.x, m.y, p);
}

bool Game::mouseInView() const {
    ImGuiIO& io = ImGui::GetIO();
    // ImGui knows at once whether the pointer is on the HUD (a tap moves and presses in the same frame)
    return !io.WantCaptureMouse && io.MousePos.x >= 0 && io.MousePos.y >= 0 && io.MousePos.x < m_viewW &&
           io.MousePos.y < m_viewH;
}

bool Game::keyPressed(int key, const char* name) const {
    if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed((ImGuiKey)key, false)) return true;
    return name && std::find(m_touchKeys.begin(), m_touchKeys.end(), name) != m_touchKeys.end();
}

// Game clicks and keys (GameHost::playInput).
void Game::playInput() {
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsMouseClicked(0) && mouseInView()) {
        float3 o, d;
        cameraRay(io.MousePos.x, io.MousePos.y, o, d);
        PickResult pr = pick(*m_scene, o, d);
        float tHouse = 0;
        const int house = houseUnder(o, d, tHouse);
        if (house >= 0 && tHouse <= pr.t) {
            m_rt.fireHouseClick(m_build.buildings()[house].id);
        } else if (pr.object >= 0 && objectAlive(pr.object) && !m_scene->objects[pr.object].part.empty()) {
            m_rt.fireClickObject(pr.object, pr.position);
        } else if (std::fabs(d.y) > 1e-5f) {
            float t = -o.y / d.y;
            if (t > 0) m_rt.fireClickGround(o + d * t);
        }
    }
    for (const std::string& k : m_touchKeys) m_rt.fireKey(k);
    if (io.WantTextInput) return;
    for (int k = ImGuiKey_A; k <= ImGuiKey_Z; ++k)
        if (ImGui::IsKeyPressed((ImGuiKey)k, true)) m_rt.fireKey(std::string(1, (char)('A' + k - ImGuiKey_A)));
    for (int k = ImGuiKey_0; k <= ImGuiKey_9; ++k)
        if (ImGui::IsKeyPressed((ImGuiKey)k, true)) m_rt.fireKey(std::string(1, (char)('0' + k - ImGuiKey_0)));
    for (int k = ImGuiKey_F1; k <= ImGuiKey_F12; ++k)
        if (ImGui::IsKeyPressed((ImGuiKey)k, true)) m_rt.fireKey("F" + std::to_string(k - ImGuiKey_F1 + 1));
    static const struct { ImGuiKey key; const char* name; } kKeys[] = {
        {ImGuiKey_Space, "ESPACE"}, {ImGuiKey_Enter, "ENTREE"}, {ImGuiKey_Escape, "ECHAP"}, {ImGuiKey_Delete, "SUPPR"},
        {ImGuiKey_Tab, "TAB"}, {ImGuiKey_UpArrow, "HAUT"}, {ImGuiKey_DownArrow, "BAS"}, {ImGuiKey_LeftArrow, "GAUCHE"},
        {ImGuiKey_RightArrow, "DROITE"},
    };
    for (auto& k : kKeys) if (ImGui::IsKeyPressed(k.key, true)) m_rt.fireKey(k.name);
}

// RTS camera (GameHost::updateRtsCamera): keys, screen edges, wheel zoom to the cursor, right drag, middle turn;
// on touch screens two fingers drag the ground, pinch to zoom and twist to turn. The player always moves its
// camera this way; the "RTS Camera" node adds the sun dial, as in the editor.
void Game::updateRtsCamera(float dt, const CameraControls& cc) {
    ImGuiIO& io = ImGui::GetIO();
    const float e = 6 * m_s;
    const bool inView = mouseInView();
    game::CameraInput in;
    in.moveX = cc.moveX;
    in.moveZ = cc.moveZ;
    in.turn = cc.turn;
    if (!io.WantTextInput) {
        in.moveZ += (ImGui::IsKeyDown(ImGuiKey_UpArrow) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_DownArrow) ? 1.0f : 0.0f);
        in.moveX += (ImGui::IsKeyDown(ImGuiKey_RightArrow) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_LeftArrow) ? 1.0f : 0.0f);
    }
    in.fast = io.KeyShift;
    if (inView) {
        if (!m_touch) {
            if (io.MousePos.x < e) in.moveX -= 1;
            if (io.MousePos.x > m_viewW - e) in.moveX += 1;
            if (io.MousePos.y < e) in.moveZ += 1;
            if (io.MousePos.y > m_viewH - e) in.moveZ -= 1;
        }
        in.wheel = io.MouseWheel;
        float3 g;
        if (groundUnderMouse(g)) { in.hasCursorGround = true; in.cursorGround = g; }
    }
    if (cc.twoFingers && cc.pinch > 0.0f && std::fabs(cc.pinch - 1.0f) > 1e-4f) {
        in.wheel += std::log(cc.pinch) / -std::log(0.85f);   // RtsCamera: distance * 0.85^wheel
        float3 g;
        if (groundAt(cc.fingersX, cc.fingersY, g)) { in.hasCursorGround = true; in.cursorGround = g; }
    }
    m_cam.update(in, dt);
    if (inView && ImGui::IsMouseDown(1)) {
        float3 g;
        if (groundUnderMouse(g)) {
            if (!m_rmbDrag) { m_rmbDrag = true; m_rmbGrab = g; }
            else m_cam.dragTo(m_rmbGrab, g);
        }
    } else {
        m_rmbDrag = false;
    }
    if (cc.twoFingers) {
        float3 g;
        if (groundAt(cc.fingersX, cc.fingersY, g)) {
            if (!m_twoDrag) { m_twoDrag = true; m_twoGrab = g; }
            else m_cam.dragTo(m_twoGrab, g);
        }
        m_cam.addYaw(-cc.twist);
    } else {
        m_twoDrag = false;
    }
    if (inView && ImGui::IsMouseDown(2)) m_cam.addYaw(io.MouseDelta.x * 0.01f);
    m_cam.apply(m_scene->camera);
}

void Game::setRtsCamera(bool on) {
    m_rtsCam = on;
    m_rmbDrag = false;
    m_scene->sun.enabled = on || m_scene->sun.enabled;   // the RTS game is lit by its sun (the studio already is)
    if (on) {
        m_houseFailed = false;
        ensureHouseModel();
        updateSun();
    }
}

void Game::updateSun() {
    float ce = std::cos(m_sunElevation);
    m_scene->sun.direction = normalize(float3(std::sin(m_sunAzimuth) * ce, std::sin(m_sunElevation), std::cos(m_sunAzimuth) * ce));
}

void Game::setSun(float azimuth, float height) {
    m_scene->sun.enabled = true;
    m_sunAzimuth = radians(azimuth);
    m_sunElevation = radians(clampf(height, 10.0f, 85.0f));
    updateSun();
}

// --------------------------------------------------------------------------- World

int Game::objectCount() const { return (int)m_scene->objects.size(); }

int Game::objectsShown() const {
    int n = 0;
    for (const Object& o : m_scene->objects) n += o.visible;
    return n;
}

bool Game::objectAlive(int i) const {
    return i >= 0 && i < objectCount() && m_scene->objects[i].visible && !m_scene->objects[i].ghost;
}

std::string Game::objectName(int i) const { return objectAlive(i) ? m_scene->objects[i].name : ""; }
std::string Game::objectPart(int i) const { return objectAlive(i) ? m_scene->objects[i].part : ""; }
float3 Game::getPosition(int i) const { return objectAlive(i) ? m_scene->objects[i].position : float3(0, 0, 0); }

void Game::setPosition(int i, float3 p) {
    if (objectAlive(i)) m_scene->objects[i].position = p;
}

void Game::rotateY(int i, float deg) {
    if (!objectAlive(i)) return;
    Object& o = m_scene->objects[i];
    o.yaw += radians(deg);
    if (o.useRot) o.rot = qNormalize(qMul(qAxisAngle(float3(0, 1, 0), radians(deg)), o.rot));
}

void Game::setScale(int i, float3 s) {
    if (objectAlive(i)) m_scene->objects[i].scale = s;
}

void Game::setColor(int i, float3 rgb) {
    if (!objectAlive(i)) return;
    Material& m = m_scene->materials[m_scene->objects[i].material];
    m.baseColor = float3(srgbToLinear(clampf(rgb.x, 0, 1)), srgbToLinear(clampf(rgb.y, 0, 1)), srgbToLinear(clampf(rgb.z, 0, 1)));
    m.colorId = -1;
}

std::string Game::uniqueName(const std::string& base) const {
    auto used = [&](const std::string& n) {
        for (const Object& o : m_scene->objects) if (o.name == n) return true;
        return false;
    };
    if (!used(base)) return base;
    for (int k = 2;; ++k) {
        std::string n = base + " " + std::to_string(k);
        if (!used(n)) return n;
    }
}

int Game::spawn(const std::string& part, float3 pos, float3 rgb) {
    const app::PartDef* p = app::findPart(part);
    if (!p) return -1;
    int idx = app::addPart(*m_scene, p->id, p->defaultColor, -1, pos, 0);
    if (idx < 0) return -1;
    m_scene->objects[idx].name.clear();
    m_scene->objects[idx].name = uniqueName(app::toUtf8(p->name));
    setColor(idx, rgb);
    return idx;
}

void Game::destroy(int i) {
    if (!objectAlive(i)) return;
    Object& o = m_scene->objects[i];
    o.visible = false;
    o.selectable = false;
    if (m_selected == i) m_selected = -1;
}

void Game::select(int i) { m_selected = objectAlive(i) ? i : -1; }

void Game::toast(const std::string& s) {
    m_toasts.push_back({s, m_time});
    if (m_toasts.size() > 5) m_toasts.erase(m_toasts.begin());
}

// --------------------------------------------------------------------------- houses (AppRts.cpp)

int Game::invalidMaterial() {
    if (m_invalidMat >= 0 && m_invalidMat < (int)m_scene->materials.size()) return m_invalidMat;
    Material m;
    m.name = "Invalid placement";
    m.baseColor = float3(srgbToLinear(0.95f), srgbToLinear(0.15f), srgbToLinear(0.12f));
    m.colorId = -1;
    m_scene->materials.push_back(m);
    m_invalidMat = (int)m_scene->materials.size() - 1;
    return m_invalidMat;
}

bool Game::ensureHouseModel() {
    Scene& s = *m_scene;
    const bool loaded = m_houseMesh >= 0 && m_houseMesh < (int)s.meshNames.size() && s.meshNames[m_houseMesh] == "House01_low";
    if (!loaded) {
        const std::string dir = m_dataDir + "/assets/HOUSE/";
        MeshData mesh;
        ObjInfo info;
        std::string err;
        if (!loadObj(app::fromUtf8(dir + "House01_low.obj"), mesh, &info, &err)) {
            if (!m_houseFailed) toast("House model not loaded: " + err);
            m_houseFailed = true;
            return false;
        }
        float3 mn, mx;
        mesh.bounds(mn, mx);
        float3 shift((mn.x + mx.x) * 0.5f, mn.y, (mn.z + mx.z) * 0.5f);
        for (Vertex& v : mesh.vertices) v.pos -= shift;
        mesh.bounds(m_houseMin, m_houseMax);
        // base colour: the image named by the .mtl, else the *basecolor* image of the folder
        std::string texPath;
        std::error_code ec;
        if (!info.diffuseMap.empty() && fs::exists(u8(dir + info.diffuseMap), ec)) {
            texPath = dir + info.diffuseMap;
        } else {
            for (const fs::directory_entry& e : fs::directory_iterator(u8(dir), ec)) {
                std::string n = e.path().filename().u8string();
                std::string low = n;
                for (char& ch : low) ch = (char)tolower((unsigned char)ch);
                if (low.find("basecolor") != std::string::npos) { texPath = dir + n; break; }
            }
        }
        m_houseTex = -1;
        if (!texPath.empty()) {
            Texture tex;
            std::string terr;
            if (loadImage(app::fromUtf8(texPath), tex, &terr, 2048)) m_houseTex = s.addTexture(std::move(tex));
            else toast("Texture not loaded: " + terr);
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

bool Game::hasBuilding(int id) const {
    for (const game::Building& b : m_build.buildings())
        if (b.id == id) return true;
    return false;
}

int Game::houseUnder(const float3& o, const float3& d, float& tHit) const {
    int best = -1;
    tHit = 1e30f;
    for (int i = 0; i < (int)m_build.buildings().size(); ++i) {
        const game::Building& b = m_build.buildings()[i];
        if (!objectAlive(b.object)) continue;
        Affine inv = m_scene->objects[b.object].transform().inverse();
        float t = 0;
        if (slabHit(inv.point(o), inv.vector(d), m_houseMin - float3(1e-3f), m_houseMax + float3(1e-3f), t) && t < tHit) {
            tHit = t;
            best = i;
        }
    }
    return best;
}

void Game::finishDrag(bool cancelled) {
    int idx = m_build.dragging();
    bool isNew = m_build.draggingNew();
    DropResult r = m_build.endDrag(cancelled);
    if (r == DropResult::Placed) {
        m_buildSelected = idx;
        toast(isNew ? "House placed" : "House moved");
    } else if (r == DropResult::Returned) {
        m_buildSelected = idx;
        if (!cancelled) toast("No room there: the house goes back");
    } else if (isNew) {
        removeBuilding(idx);
        if (!cancelled) toast("Not placed: drop it on the grid");
    }
}

void Game::removeBuilding(int idx) {
    std::vector<game::Building>& bs = m_build.buildings();
    if (idx < 0 || idx >= (int)bs.size()) return;
    if (objectAlive(bs[idx].object)) destroy(bs[idx].object);
    m_build.remove(idx);
    m_buildHover = m_buildPress = m_buildSelected = -1;
}

void Game::syncBuildings() {
    Scene& s = *m_scene;
    const std::vector<BuildingType>& types = m_build.types();
    std::vector<game::Building>& bs = m_build.buildings();
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
        }
        Object& o = s.objects[b.object];
        float k = b.hover * t.modelScale;
        o.scale = float3((1 + b.squash * 0.5f) * k, (1 - b.squash) * k, (1 + b.squash * 0.5f) * k);
        o.rot = qMul(qAxisAngle(float3(0, 1, 0), b.yaw), qMul(qAxisAngle(float3(1, 0, 0), b.tiltX), qAxisAngle(float3(0, 0, 1), b.tiltZ)));
        o.position = b.pos;
        o.useRot = true;
        o.material = (i == m_build.dragging() && !m_build.dragValid()) ? invalidMaterial() : m_houseMat;
    }
    for (Object& o : s.objects)
        if (o.part == "House" && o.visible && !hasBuilding(o.grab)) o.visible = false;
}

// Hover, press and drag of the houses, R (turn) and Del (remove), animation (GameHost::updatePlacement).
void Game::updatePlacement(float dt) {
    ImGuiIO& io = ImGui::GetIO();
    const bool inView = mouseInView();

    int hovered = -1;
    if (inView && m_build.dragging() < 0 && !m_build.buildings().empty()) {
        float3 o, d;
        cameraRay(io.MousePos.x, io.MousePos.y, o, d);
        float tHouse = 0;
        int h = houseUnder(o, d, tHouse);
        if (h >= 0 && tHouse <= pick(*m_scene, o, d).t) hovered = h;
    }
    // a finger has no hover: the house under it is found when it touches the screen
    m_buildHover = m_touch && !ImGui::IsMouseDown(0) ? -1 : hovered;

    float3 ground;
    bool hasGround = groundUnderMouse(ground);
    if (inView && ImGui::IsMouseClicked(0)) {
        m_buildSelected = hovered;
        m_buildPress = hovered;
        m_pressX = io.MousePos.x;
        m_pressY = io.MousePos.y;
    }
    if (m_buildPress >= 0 && m_build.dragging() < 0) {
        float dx = io.MousePos.x - m_pressX, dy = io.MousePos.y - m_pressY;
        const float slop = 6.0f * m_s;
        if (!ImGui::IsMouseDown(0)) {
            m_buildPress = -1;
        } else if (hasGround && dx * dx + dy * dy > slop * slop) {
            m_build.beginDrag(m_buildPress, ground, false);
            m_buildPress = -1;
        }
    }
    if (m_build.dragging() >= 0) {
        if (!ImGui::IsMouseDown(0)) finishDrag(false);
        else if (hasGround) m_build.updateDrag(ground, inView);
    }
    if (keyPressed(ImGuiKey_R, "R")) {
        int i = m_build.dragging() >= 0 ? m_build.dragging() : m_buildSelected;
        if (i >= 0) m_build.rotate(i);
    }
    if (keyPressed(ImGuiKey_Delete, "SUPPR") && m_build.dragging() < 0 && m_buildSelected >= 0) {
        removeBuilding(m_buildSelected);
        toast("House removed");
    }
    m_build.simulate(dt, m_buildHover);
    syncBuildings();
}

int Game::houseIndex(int id) const {
    for (int i = 0; i < (int)m_build.buildings().size(); ++i)
        if (m_build.buildings()[i].id == id) return i;
    return -1;
}

int Game::placeHouse(int type, int cx, int cz, int rot) {
    if (type < 0 || type >= (int)m_build.types().size()) return -1;
    int idx = m_build.place(type, Cell{cx, cz}, rot, kDropHeight);
    return idx < 0 ? -1 : m_build.buildings()[idx].id;
}

bool Game::removeHouse(int id) {
    int i = houseIndex(id);
    if (i < 0) return false;
    removeBuilding(i);
    return true;
}

bool Game::turnHouse(int id) {
    int i = houseIndex(id);
    return i >= 0 && m_build.rotate(i);
}

int Game::houseAt(int cx, int cz) const {
    for (const game::Building& b : m_build.buildings()) {
        if (!b.placed) continue;
        const BuildingType& t = m_build.types()[b.type];
        if (cx >= b.cell.x && cx < b.cell.x + b.footW(t) && cz >= b.cell.z && cz < b.cell.z + b.footD(t)) return b.id;
    }
    return -1;
}

int Game::houseCount() const {
    int n = 0;
    for (const game::Building& b : m_build.buildings()) n += b.placed;
    return n;
}

void Game::hudButton(const std::string& id, const std::string& label) {
    for (auto& b : m_hudButtons)
        if (b.first == id) {
            b.second = label;
            return;
        }
    m_hudButtons.push_back({id, label});
}

void Game::cellAt(float x, float z, int& cx, int& cz) const {
    Cell c = m_build.grid().snapOrigin(float3(x, 0, z), 1, 1);
    cx = c.x;
    cz = c.z;
}

// --------------------------------------------------------------------------- HUD

// Bottom left: the buttons declared by the graph; on touch screens also the keys of the game (R, C, Del).
void Game::drawRtsHud() {
    const float d = m_s;
    ImGui::SetNextWindowPos(ImVec2(m_safeL + 12 * d, m_viewH - m_safeB - 12 * d), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.85f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize;
    if (ImGui::Begin("##rtshud", nullptr, flags)) {
        if (ImGui::IsWindowHovered()) m_buildHud = true;
        for (size_t k = 0; k < m_hudButtons.size(); ++k) {
            ImGui::PushID((int)k);
            if (ImGui::Button(m_hudButtons[k].second.c_str())) m_rt.fireHudButton(m_hudButtons[k].first);
            ImGui::PopID();
            ImGui::SameLine();
        }
        ImGui::NewLine();
        if (m_touch) {
            static const struct { const char* label; const char* key; } kTouch[] = {
                {"Turn  (R)", "R"}, {"Cube  (C)", "C"}, {"Delete", "SUPPR"},
            };
            for (auto& t : kTouch) {
                if (ImGui::Button(t.label)) m_touchKeys.push_back(t.key);
                ImGui::SameLine();
            }
            ImGui::NewLine();
            ImGui::TextDisabled("Drag a house  ·  two fingers: move, pinch, turn");
        } else {
            ImGui::TextDisabled("Drag a house onto the grid  ·  R rotate  ·  Del delete");
        }
    }
    ImGui::End();
    if (m_rtsCam) drawSunDial();
}

// Sun dial, top right (AppRts.cpp, GameHost::drawSunDial).
void Game::drawSunDial() {
    ImGuiIO& io = ImGui::GetIO();
    const float d = m_s;
    const float R = 62 * d;
    const ImVec2 c(m_viewW - m_safeR - R - 28 * d, m_safeT + 40 * d + R);
    ImGui::SetNextWindowPos(ImVec2(c.x - R - 20 * d, c.y - R - 20 * d), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(2 * R + 40 * d, 2 * R + 80 * d), ImGuiCond_Always);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBackground;
    if (ImGui::Begin("##sundial", nullptr, flags)) {
        if (ImGui::IsWindowHovered()) m_buildHud = true;
        ImGui::SetCursorScreenPos(ImVec2(c.x - R - 14 * d, c.y - R - 14 * d));
        ImGui::InvisibleButton("##dial", ImVec2(2 * R + 28 * d, 2 * R + 28 * d));
        float3 right, up, fwd;
        m_scene->camera.basis(right, up, fwd);
        float3 f(fwd.x, 0, fwd.z), r(right.x, 0, right.z);
        f = length(f) > 1e-4f ? normalize(f) : float3(0, 0, 1);
        r = length(r) > 1e-4f ? normalize(r) : float3(1, 0, 0);
        if (ImGui::IsItemActivated()) m_sunGrabAnim = 1.0f;
        if (ImGui::IsItemActive()) {
            m_buildHud = true;
            float dx = io.MousePos.x - c.x, dy = io.MousePos.y - c.y;
            if (dx * dx + dy * dy > 4.0f) {
                float a = std::atan2(dx, -dy);
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
        ImVec2 ad(-std::sin(th), std::cos(th));
        ImVec2 tip(c.x + ad.x * R * 0.42f, c.y + ad.y * R * 0.42f), tail(c.x - ad.x * R * 0.18f, c.y - ad.y * R * 0.18f);
        dl->AddLine(tail, tip, IM_COL32(200, 210, 225, 220), 3 * d);
        ImVec2 pr(-ad.y, ad.x);
        float ah = 9 * d;
        dl->AddTriangleFilled(ImVec2(tip.x + ad.x * ah, tip.y + ad.y * ah), ImVec2(tip.x + pr.x * ah * 0.7f, tip.y + pr.y * ah * 0.7f),
                              ImVec2(tip.x - pr.x * ah * 0.7f, tip.y - pr.y * ah * 0.7f), IM_COL32(200, 210, 225, 220));
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
        ImGui::SetCursorScreenPos(ImVec2(c.x - R, c.y + R + 22 * d));
        ImGui::SetNextItemWidth(2 * R);
        float elev = degrees(m_sunElevation);
        if (ImGui::SliderFloat("##elev", &elev, 10.0f, 85.0f, "Height %.0f°")) m_sunElevation = radians(elev);
        if (ImGui::IsItemActive()) m_buildHud = true;
    }
    ImGui::End();
    updateSun();
}

void Game::drawToasts() {
    m_toasts.erase(std::remove_if(m_toasts.begin(), m_toasts.end(), [&](const Toast& t) { return m_time - t.t > 5.0f; }), m_toasts.end());
    const float d = m_s;
    float y = m_safeT + 48 * d;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove;
    int i = 0;
    for (const Toast& t : m_toasts) {
        float a = clampf((5.0f - (m_time - t.t)) / 0.6f, 0, 1);
        ImGui::SetNextWindowPos(ImVec2(m_viewW * 0.5f, y), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, a);
        ImGui::SetNextWindowBgAlpha(0.88f);
        std::string id = "##toast" + std::to_string(i++);
        if (ImGui::Begin(id.c_str(), nullptr, flags)) ImGui::TextUnformatted(t.text.c_str());
        ImGui::End();
        ImGui::PopStyleVar();
        y += 36 * d;
    }
}

} // namespace player
