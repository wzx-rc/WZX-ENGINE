// RTS play layer (see RtsPlay.h).
#include "RtsPlay.h"
#include "imgui.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <queue>

namespace app {

namespace {

const float kPi = 3.14159265f;

bool selectableCategory(const std::string& c) { return c == "Unit" || c == "Building" || c == "Resource"; }

ImU32 col(float r, float g, float b, float a) { return IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), (int)(a * 255)); }

ImU32 ringColor(const std::string& cat, float a) {
    if (cat == "Unit") return col(0.35f, 1.0f, 0.55f, a);       // own units: green
    if (cat == "Resource") return col(1.0f, 0.85f, 0.3f, a);    // resources: amber
    return col(0.45f, 0.85f, 1.0f, a);                           // buildings: cyan
}

std::string fileStem(const std::string& path) {
    size_t s = path.find_last_of("/\\");
    std::string n = s == std::string::npos ? path : path.substr(s + 1);
    size_t d = n.rfind('.');
    return d == std::string::npos ? n : n.substr(0, d);
}

} // namespace

void RtsPlay::begin(Scene* scene, PrefabLibrary* lib, game::Runtime* rt) {
    m_scene = scene;
    m_lib = lib;
    m_rt = rt;
    m_sel.clear();
    m_hover = m_ghost = -1;
    m_leftDown = m_dragging = m_rightDown = false;
    m_baseEmission.clear();
    m_gesture = kNone;
    m_startT = -1;
    m_lastTapObj = -1;
    m_orderT = -10;
    m_objectivesOpen = false;   // (a phone: folded to one line at the start)
}

void RtsPlay::end() {
    if (m_scene && m_ghost >= 0 && m_ghost < (int)m_scene->objects.size()) m_scene->objects[m_ghost].visible = false;
    m_scene = nullptr;
    m_lib = nullptr;
    m_rt = nullptr;
    m_sel.clear();
    m_ghost = m_hover = -1;
}

const PrefabAsset* RtsPlay::assetOf(int obj) const {
    if (!m_scene || !m_lib || obj < 0 || obj >= (int)m_scene->objects.size()) return nullptr;
    const Object& o = m_scene->objects[obj];
    return o.isInstanceRoot() ? m_lib->get(o.prefab) : nullptr;
}

std::string RtsPlay::objectPrefab(int obj) const {
    if (!assetOf(obj)) return std::string();
    return fileStem(m_scene->objects[obj].prefab);
}

std::string RtsPlay::objectCategory(int obj) const {
    const PrefabAsset* a = assetOf(obj);
    return a ? a->game.category : std::string();
}

void RtsPlay::changed() {
    if (m_rt) m_rt->fireSelectionChanged();
}

void RtsPlay::setSelection(const std::vector<int>& objs) {
    std::vector<int> s;
    for (int o : objs) {
        const int r = instanceRootOf(*m_scene, o);
        if (r >= 0 && r < (int)m_scene->objects.size() && m_scene->objects[r].visible && std::find(s.begin(), s.end(), r) == s.end()) s.push_back(r);
    }
    if (s == m_sel) return;
    m_sel = s;
    changed();
}

void RtsPlay::setYaw(int obj, float degrees) {
    if (!m_scene || obj < 0 || obj >= (int)m_scene->objects.size()) return;
    Object& o = m_scene->objects[obj];
    // turned smoothly towards the wanted heading (units walking)
    const float want = degrees * kPi / 180.0f;
    float d = want - o.yaw;
    while (d > kPi) d -= 2 * kPi;
    while (d < -kPi) d += 2 * kPi;
    o.yaw += d * 0.25f;
    o.useRot = true;
    o.rot = qAxisAngle(float3(0, 1, 0), o.yaw);
}

void RtsPlay::setPartColor(int obj, const std::string& part, float3 rgb) {
    const int r = instanceRootOf(*m_scene, obj);
    if (r < 0) return;
    const uint32_t id = m_scene->objects[r].id;
    for (Object& o : m_scene->objects) {
        if (o.instanceRoot != id || (!part.empty() && o.name.find(part) == std::string::npos)) continue;
        if (o.material < 0 || o.material >= (int)m_scene->materials.size()) continue;
        m_scene->materials[o.material].baseColor = float3(srgbToLinear(rgb.x), srgbToLinear(rgb.y), srgbToLinear(rgb.z));
    }
}

void RtsPlay::setPartGlow(int obj, const std::string& part, float k) {
    const int r = instanceRootOf(*m_scene, obj);
    if (r < 0) return;
    const uint32_t id = m_scene->objects[r].id;
    for (Object& o : m_scene->objects) {
        if (o.instanceRoot != id || (!part.empty() && o.name.find(part) == std::string::npos)) continue;
        if (o.material < 0 || o.material >= (int)m_scene->materials.size()) continue;
        Material& m = m_scene->materials[o.material];
        auto it = m_baseEmission.find(o.material);
        if (it == m_baseEmission.end()) it = m_baseEmission.emplace(o.material, m.emission).first;
        m.emission = it->second * std::max(k, 0.0f);
    }
}

// --------------------------------------------------------------------------- navigation

void RtsPlay::footprintOf(int root, float3& mn, float3& mx) const {
    const Object& o = m_scene->objects[root];
    const PrefabAsset* a = assetOf(root);
    float fx = a ? (float)a->game.footprintX : 1.0f, fz = a ? (float)a->game.footprintZ : 1.0f;
    // a quarter turn swaps the sides (other angles: the larger side both ways)
    const float yaw = std::fabs(std::fmod(std::fabs(o.yaw * 180.0f / kPi), 180.0f));
    if (std::fabs(yaw - 90.0f) < 20.0f) std::swap(fx, fz);
    else if (yaw > 20.0f && yaw < 160.0f) fx = fz = std::max(fx, fz);
    fx *= o.scale.x;
    fz *= o.scale.z;
    mn = float3(o.position.x - fx * 0.5f, 0, o.position.z - fz * 0.5f);
    mx = float3(o.position.x + fx * 0.5f, 0, o.position.z + fz * 0.5f);
}

void RtsPlay::buildNav(int ignore) {
    m_nav.assign((size_t)kN * kN, 0);
    const float half = kN * kCell * 0.5f;
    for (int i = 0; i < (int)m_scene->objects.size(); ++i) {
        const Object& o = m_scene->objects[i];
        if (i == ignore || !o.isInstanceRoot() || !o.visible || o.ghost) continue;
        const PrefabAsset* a = assetOf(i);
        if (!a || !a->game.blocksPath || a->game.category == "Unit") continue;
        float3 mn, mx;
        footprintOf(i, mn, mx);
        const float pad = 0.15f;   // a unit's half width, its body does not enter the walls
        const int x0 = std::max(0, (int)std::floor((mn.x - pad + half) / kCell)), x1 = std::min(kN - 1, (int)std::floor((mx.x + pad + half) / kCell - 1e-4f));
        const int z0 = std::max(0, (int)std::floor((mn.z - pad + half) / kCell)), z1 = std::min(kN - 1, (int)std::floor((mx.z + pad + half) / kCell - 1e-4f));
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) m_nav[(size_t)z * kN + x] = 1;
    }
}

bool RtsPlay::cellFree(int cx, int cz) const {
    return cx >= 0 && cz >= 0 && cx < kN && cz < kN && !m_nav[(size_t)cz * kN + cx];
}

bool RtsPlay::blocked(float x, float z) {
    buildNav();
    const float half = kN * kCell * 0.5f;
    return !cellFree((int)std::floor((x + half) / kCell), (int)std::floor((z + half) / kCell));
}

std::vector<float3> RtsPlay::findPath(float3 from, float3 to) {
    if (!m_scene) return {to};
    buildNav();
    const float half = kN * kCell * 0.5f;
    auto cellOf = [&](const float3& p, int& cx, int& cz) {
        cx = std::clamp((int)std::floor((p.x + half) / kCell), 0, kN - 1);
        cz = std::clamp((int)std::floor((p.z + half) / kCell), 0, kN - 1);
    };
    auto centre = [&](int cx, int cz) { return float3((cx + 0.5f) * kCell - half, 0, (cz + 0.5f) * kCell - half); };
    // the free cell nearest to a cell (a click on a building: the unit stops beside it)
    auto nearestFree = [&](int& cx, int& cz, const float3& towards) {
        if (cellFree(cx, cz)) return true;
        for (int r = 1; r < 40; ++r) {
            float best = 1e30f;
            int bx = -1, bz = -1;
            for (int dz = -r; dz <= r; ++dz)
                for (int dx = -r; dx <= r; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dz)) != r || !cellFree(cx + dx, cz + dz)) continue;
                    const float3 c = centre(cx + dx, cz + dz);
                    const float d = length(float3(c.x - towards.x, 0, c.z - towards.z));
                    if (d < best) { best = d; bx = cx + dx; bz = cz + dz; }
                }
            if (bx >= 0) { cx = bx; cz = bz; return true; }
        }
        return false;
    };
    int sx, sz, gx, gz;
    cellOf(from, sx, sz);
    cellOf(to, gx, gz);
    const bool goalFree = cellFree(gx, gz);
    if (!nearestFree(sx, sz, from) || !nearestFree(gx, gz, from)) return {to};
    // A* (8 neighbours, no corner cutting)
    std::vector<float> g((size_t)kN * kN, 1e30f);
    std::vector<int> parent((size_t)kN * kN, -1);
    using QE = std::pair<float, int>;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> open;
    auto h = [&](int x, int z) {
        const int dx = std::abs(x - gx), dz = std::abs(z - gz);
        return (float)(std::max(dx, dz) - std::min(dx, dz)) + 1.41421356f * std::min(dx, dz);
    };
    const int start = sz * kN + sx, goal = gz * kN + gx;
    g[start] = 0;
    open.push({h(sx, sz), start});
    int expanded = 0;
    while (!open.empty() && expanded < kN * kN) {
        const auto [f, cur] = open.top();
        open.pop();
        if (cur == goal) break;
        ++expanded;
        const int cx = cur % kN, cz = cur / kN;
        if (f - h(cx, cz) > g[cur] + 1e-3f) continue;
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                if (!dx && !dz) continue;
                const int nx = cx + dx, nz = cz + dz;
                if (!cellFree(nx, nz)) continue;
                if (dx && dz && (!cellFree(cx + dx, cz) || !cellFree(cx, cz + dz))) continue;
                const int ni = nz * kN + nx;
                const float ng = g[cur] + (dx && dz ? 1.41421356f : 1.0f);
                if (ng < g[ni]) {
                    g[ni] = ng;
                    parent[ni] = cur;
                    open.push({ng + h(nx, nz), ni});
                }
            }
    }
    if (parent[goal] < 0 && goal != start) return {to};   // no way: straight there
    std::vector<int> cells;
    for (int c = goal; c != -1; c = parent[c]) cells.push_back(c);
    std::reverse(cells.begin(), cells.end());
    // smoothing: from each kept cell, the farthest cell in a straight free line
    auto lineFree = [&](int a, int b) {
        const float3 pa = centre(a % kN, a / kN), pb = centre(b % kN, b / kN);
        const float len = length(pb - pa);
        const int steps = std::max(1, (int)std::ceil(len / (kCell * 0.35f)));
        for (int k = 0; k <= steps; ++k) {
            const float3 p = pa + (pb - pa) * ((float)k / steps);
            int cx, cz;
            cellOf(p, cx, cz);
            if (!cellFree(cx, cz)) return false;
        }
        return true;
    };
    std::vector<float3> path;
    size_t i = 0;
    while (i + 1 < cells.size()) {
        size_t j = cells.size() - 1;
        while (j > i + 1 && !lineFree(cells[i], cells[j])) --j;
        path.push_back(centre(cells[j] % kN, cells[j] / kN));
        i = j;
    }
    if (path.empty()) path.push_back(centre(gx, gz));
    if (goalFree) path.back() = float3(to.x, 0, to.z);
    for (float3& p : path) p.y = from.y;
    return path;
}

float3 RtsPlay::doorOf(int building) const {
    if (!m_scene || building < 0 || building >= (int)m_scene->objects.size()) return float3(0, 0, 0);
    const Object& o = m_scene->objects[building];
    const PrefabAsset* a = assetOf(building);
    const float dz = (a ? a->game.footprintZ * 0.5f : 1.0f) * o.scale.z + 0.6f;
    return o.position + qRot(o.useRot ? o.rot : qAxisAngle(float3(0, 1, 0), o.yaw), float3(0, 0, dz));
}

// --------------------------------------------------------------------------- placement

bool RtsPlay::beginPlacement(const std::string& prefab) {
    if (!m_scene || !m_lib) return false;
    const std::string path = m_lib->resolve(prefab);
    if (path.empty() || !m_lib->get(path)) return false;
    if (m_ghost >= 0 && m_ghost < (int)m_scene->objects.size()) m_scene->objects[m_ghost].visible = false;
    m_ghost = addPrefabInstance(*m_scene, *m_lib, path, float3(0, 0, 0), 0.0f, m_lib->get(path)->name);
    if (m_ghost < 0) return false;
    Object& g = m_scene->objects[m_ghost];
    g.ghost = true;
    g.ghostAlpha = 0.55f;
    g.selectable = false;
    syncPrefabInstances(*m_scene, *m_lib, false);
    float3 c;   // touch: the ghost starts at the centre of the view (the mouse puts it under the cursor)
    if (m_touch && groundAt(m_view, m_view.x + m_view.w * 0.5f, m_view.y + m_view.h * 0.5f, c)) snapGhost(c);
    return true;
}

bool RtsPlay::footprintFree(int root) const {
    float3 mn, mx;
    footprintOf(root, mn, mx);
    const float half = kN * kCell * 0.5f;
    const int x0 = (int)std::floor((mn.x + half) / kCell + 1e-3f), x1 = (int)std::ceil((mx.x + half) / kCell - 1e-3f) - 1;
    const int z0 = (int)std::floor((mn.z + half) / kCell + 1e-3f), z1 = (int)std::ceil((mx.z + half) / kCell - 1e-3f) - 1;
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x)
            if (!cellFree(x, z)) return false;
    // no unit standing there
    for (int i = 0; i < (int)m_scene->objects.size(); ++i) {
        const Object& o = m_scene->objects[i];
        if (i == root || !o.isInstanceRoot() || !o.visible || o.ghost || objectCategory(i) != "Unit") continue;
        if (o.position.x > mn.x - 0.3f && o.position.x < mx.x + 0.3f && o.position.z > mn.z - 0.3f && o.position.z < mx.z + 0.3f) return false;
    }
    return true;
}

// --------------------------------------------------------------------------- input

bool RtsPlay::project(const RtsView& v, const float3& p, float& sx, float& sy) const {
    float2 n;
    if (!m_scene->camera.project(p, v.w / v.h, n)) return false;
    sx = v.x + (0.5f + 0.5f * n.x) * v.w;
    sy = v.y + (0.5f - 0.5f * n.y) * v.h;
    return true;
}

bool RtsPlay::groundAt(const RtsView& v, float mx, float my, float3& p) const {
    float3 o, d;
    m_scene->camera.ray(2 * (mx - v.x) / v.w - 1, 1 - 2 * (my - v.y) / v.h, v.w / v.h, o, d, 1e4f);
    if (std::fabs(d.y) < 1e-5f) return false;
    const float t = -o.y / d.y;
    if (t <= 0) return false;
    p = o + d * t;
    return true;
}

int RtsPlay::pickRoot(const RtsView& v, float mx, float my, float3* point) const {
    float3 o, d;
    m_scene->camera.ray(2 * (mx - v.x) / v.w - 1, 1 - 2 * (my - v.y) / v.h, v.w / v.h, o, d, 1e4f);
    PickResult pr = pick(*m_scene, o, d);
    if (point) *point = pr.position;
    if (pr.object < 0 || !m_scene->objects[pr.object].selectable || !m_scene->objects[pr.object].visible) return -1;
    const int r = instanceRootOf(*m_scene, pr.object);
    if (r < 0 || m_scene->objects[r].ghost || !selectableCategory(objectCategory(r))) return -1;
    return r;
}

const RtsDeviceSpec* rtsDeviceSpec(int device) {
    // iPad Air 11" and iPhone 15 in landscape: the home indicator at the bottom, the notch on a side
    static const RtsDeviceSpec ipad{"iPad", 1180, 820, 0, 0, 0, 20};
    static const RtsDeviceSpec iphone{"iPhone", 852, 393, 59, 0, 59, 21};
    return device == kTablet ? &ipad : device == kPhone ? &iphone : nullptr;
}

void RtsPlay::previewRect(float x0, float y0, float x1, float y1) {
    m_gesture = kNone;
    m_dragging = true;
    m_downX = x0;
    m_downY = y0;
    m_fingerX = x1;
    m_fingerY = y1;
}

const char* rtsDeviceName(int device) { return device == kPhone ? "Phone" : device == kTablet ? "Tablet" : "Desktop"; }

const char* RtsPlay::layoutName() const {
    if (m_device == kPhone) return "Phone";
    return m_device == kTablet || m_touch ? "Tablet" : "Desktop";   // a Windows touch screen gets the large targets
}

void RtsPlay::setTouch(bool on) {
    if (on == m_touch) return;
    m_touch = on;
    m_gesture = kNone;
    m_leftDown = m_dragging = m_rightDown = false;
    m_startT = -1;   // the controls hint of the new mode shows again
    if (m_rt) m_rt->fireInputModeChanged(inputModeName());
}

bool RtsPlay::touchPan(float& x, float& y) const {
    if (!m_scene || !m_touch || m_gesture != kPan) return false;
    x = m_fingerX;
    y = m_fingerY;
    return true;
}

void RtsPlay::snapGhost(float3 p) {
    if (m_ghost < 0) return;
    Object& g = m_scene->objects[m_ghost];
    const PrefabAsset* a = assetOf(m_ghost);
    const float ox = a && (a->game.footprintX % 2) ? 0.5f : 0.0f, oz = a && (a->game.footprintZ % 2) ? 0.5f : 0.0f;
    g.position = float3(std::floor(p.x - ox + 0.5f) + ox, 0, std::floor(p.z - oz + 0.5f) + oz);
    buildNav(m_ghost);
    m_ghostOk = footprintFree(m_ghost);
}

void RtsPlay::finishPlacement(int placed) {
    if (m_ghost < 0) return;
    Object& g = m_scene->objects[m_ghost];
    if (placed >= 0) {
        g.ghost = false;
        g.selectable = true;
    } else {
        g.visible = false;
    }
    m_ghost = -1;
    m_gesture = kNone;
    m_rt->firePlacement(placed);
}

bool RtsPlay::confirmPlacement() {
    if (!m_scene || !m_rt || m_ghost < 0) return false;
    buildNav(m_ghost);
    m_ghostOk = footprintFree(m_ghost);
    if (!m_ghostOk) return false;
    finishPlacement(m_ghost);
    return true;
}

void RtsPlay::cancelPlacement() {
    if (!m_scene || !m_rt || m_ghost < 0) return;
    finishPlacement(-1);
}

bool RtsPlay::selectedUnits() const {
    for (int s : m_sel)
        if (objectCategory(s) == "Unit") return true;
    return false;
}

void RtsPlay::markOrder(float3 p) {
    m_orderAt = p;
    m_orderT = ImGui::GetTime();
}

void RtsPlay::rectSelect(const RtsView& v, float ax, float ay, float bx, float by, bool add) {
    std::vector<int> s = add ? m_sel : std::vector<int>();
    const float x0 = std::min(ax, bx), x1 = std::max(ax, bx), y0 = std::min(ay, by), y1 = std::max(ay, by);
    for (int i = 0; i < (int)m_scene->objects.size(); ++i) {
        const Object& o = m_scene->objects[i];
        if (!o.isInstanceRoot() || !o.visible || o.ghost || objectCategory(i) != "Unit") continue;
        float sx, sy;
        if (project(v, o.position + float3(0, 0.5f, 0), sx, sy) && sx >= x0 && sx <= x1 && sy >= y0 && sy <= y1 &&
            std::find(s.begin(), s.end(), i) == s.end())
            s.push_back(i);
    }
    setSelection(s);
}

void RtsPlay::touchRect(const RtsView& v, float x0, float y0, float x1, float y1) {
    if (!m_scene || !m_rt) return;
    m_dragging = false;
    rectSelect(v, x0, y0, x1, y1, false);
}

void RtsPlay::selectSameType(const RtsView& v, int obj) {
    if (!m_scene || obj < 0) return;
    const std::string prefab = objectPrefab(obj);
    std::vector<int> s{obj};
    for (int i = 0; i < (int)m_scene->objects.size(); ++i) {
        const Object& o = m_scene->objects[i];
        if (i == obj || !o.isInstanceRoot() || !o.visible || o.ghost || objectPrefab(i) != prefab) continue;
        float sx, sy;   // the ones in view
        if (project(v, o.position + float3(0, 0.5f, 0), sx, sy) && sx >= v.x && sx <= v.x + v.w && sy >= v.y && sy <= v.y + v.h) s.push_back(i);
    }
    setSelection(s);
}

void RtsPlay::selectAllUnits() {
    if (!m_scene) return;
    std::vector<int> s;
    for (int i = 0; i < (int)m_scene->objects.size(); ++i) {
        const Object& o = m_scene->objects[i];
        if (o.isInstanceRoot() && o.visible && !o.ghost && objectCategory(i) == "Unit") s.push_back(i);
    }
    setSelection(s);
}

// A tap: a unit or a building is selected; with units selected, a tap on a station (a resource) or on the ground is
// their order (the right click of the mouse); a second tap on the same unit selects the units of its type in view;
// during a placement the ghost goes where the finger tapped.
void RtsPlay::tap(const RtsView& v, float x, float y) {
    if (!m_scene || !m_rt) return;
    if (m_ghost >= 0) {
        float3 g;
        if (groundAt(v, x, y, g)) snapGhost(g);
        return;
    }
    const double now = ImGui::GetTime();
    float3 point;
    const int r = pickRoot(v, x, y, &point);
    const bool twice = r >= 0 && r == m_lastTapObj && now - m_lastTapT < 0.4 && std::hypot(x - m_lastTapX, y - m_lastTapY) < 40 * v.scale;
    m_lastTapT = twice ? -10.0 : now;
    m_lastTapX = x;
    m_lastTapY = y;
    m_lastTapObj = r;
    const bool units = selectedUnits();
    if (r >= 0) {
        const std::string cat = objectCategory(r);
        if (twice && cat == "Unit") selectSameType(v, r);
        else if (units && cat == "Resource") {
            markOrder(m_scene->objects[r].position);
            m_rt->fireRightClickObject(r, point);
            return;
        } else setSelection({r});
        m_rt->fireClickObject(r, point);
        return;
    }
    float3 g;
    if (!groundAt(v, x, y, g)) return;
    if (units) {
        markOrder(g);
        m_rt->fireRightClickGround(g);
    } else {
        setSelection({});
    }
    m_rt->fireClickGround(g);
}

bool RtsPlay::input(const RtsView& v, bool overUi) {
    if (!m_scene || !m_rt) return false;
    ImGuiIO& io = ImGui::GetIO();
    m_view = v;
    m_device = v.device;
    // the input mode: forced (tests), the platform (iOS, the simulator), else the pointer last used
    if (m_forceMode >= 0) setTouch(m_forceMode == 1);
    else if (v.touch) setTouch(true);
    else {
        const bool used = ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1) || io.MouseWheel != 0.0f ||
                          std::fabs(io.MouseDelta.x) + std::fabs(io.MouseDelta.y) > 2.0f;
        if (used) m_autoTouch = io.MouseSource == ImGuiMouseSource_TouchScreen || io.MouseSource == ImGuiMouseSource_Pen;
        setTouch(m_autoTouch);
    }
    if (m_startT < 0) m_startT = ImGui::GetTime();
    const float mx = io.MousePos.x, my = io.MousePos.y;
    const bool inView = mx >= v.x && my >= v.y && mx < v.x + v.w && my < v.y + v.h && !overUi && !m_hudHovered;
    // the selection keeps only what is alive
    m_sel.erase(std::remove_if(m_sel.begin(), m_sel.end(), [&](int o) { return o < 0 || o >= (int)m_scene->objects.size() || !m_scene->objects[o].visible; }), m_sel.end());
    if (m_touch) return touchInput(v, inView);
    m_hover = inView && !m_dragging ? pickRoot(v, mx, my, nullptr) : -1;

    // placement of a building: the ghost follows the ground under the mouse, snapped to the 1 unit grid
    if (m_ghost >= 0) {
        float3 p;
        if (std::fabs(io.MouseDelta.x) + std::fabs(io.MouseDelta.y) > 0.5f) m_ghostHeld = false;
        if (!m_ghostHeld && inView && groundAt(v, mx, my, p)) snapGhost(p);
        else {
            buildNav(m_ghost);
            m_ghostOk = footprintFree(m_ghost);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || (inView && ImGui::IsMouseClicked(1))) {
            cancelPlacement();
            return true;
        }
        if (inView && ImGui::IsMouseClicked(0)) {
            confirmPlacement();   // red: nothing happens
            return true;
        }
        return inView && (ImGui::IsMouseDown(0) || ImGui::IsMouseReleased(0));
    }

    bool handled = false;
    // left: click selects, drag draws a rectangle, a double click selects the units of that type in view
    if (inView && ImGui::IsMouseClicked(0)) {
        m_leftDown = true;
        m_dragging = false;
        m_downX = mx;
        m_downY = my;
        m_doubleClick = ImGui::IsMouseDoubleClicked(0);
    }
    if (m_leftDown && ImGui::IsMouseDown(0)) {
        if (!m_dragging && (std::fabs(mx - m_downX) > 6 * v.scale || std::fabs(my - m_downY) > 6 * v.scale)) m_dragging = true;
        handled = true;
    }
    if (m_leftDown && ImGui::IsMouseReleased(0)) {
        m_leftDown = false;
        handled = true;
        const bool add = io.KeyShift;
        if (m_dragging) {
            m_dragging = false;
            rectSelect(v, m_downX, m_downY, mx, my, add);
        } else {
            std::vector<int> s = add ? m_sel : std::vector<int>();
            float3 point;
            const int r = pickRoot(v, mx, my, &point);
            if (r >= 0) {
                if (m_doubleClick && !add && objectCategory(r) == "Unit") selectSameType(v, r);
                else {
                    auto it = std::find(s.begin(), s.end(), r);
                    if (add && it != s.end()) s.erase(it);
                    else if (it == s.end()) s.push_back(r);
                    setSelection(s);
                }
                m_rt->fireClickObject(r, point);
            } else {
                if (!add) setSelection({});
                float3 g;
                if (groundAt(v, mx, my, g)) m_rt->fireClickGround(g);
            }
        }
    }
    // right: a click (not a drag of the camera) is an order
    if (inView && ImGui::IsMouseClicked(1)) {
        m_rightDown = true;
        m_rdownX = mx;
        m_rdownY = my;
    }
    if (m_rightDown && ImGui::IsMouseReleased(1)) {
        m_rightDown = false;
        if (std::fabs(mx - m_rdownX) < 6 * v.scale && std::fabs(my - m_rdownY) < 6 * v.scale) {
            float3 point;
            const int r = pickRoot(v, mx, my, &point);
            if (r >= 0) {
                if (selectedUnits()) markOrder(m_scene->objects[r].position);
                m_rt->fireRightClickObject(r, point);
            } else {
                float3 g;
                if (groundAt(v, mx, my, g)) {
                    if (selectedUnits()) markOrder(g);
                    m_rt->fireRightClickGround(g);
                }
            }
        }
    }
    return handled;
}

// Touch: one finger is the mouse button 0 of Dear ImGui (iOS, a Windows touch screen, the simulator of the editor).
// A tap, a drag (camera), a long press (deselects), a long press then a drag (rectangle); during a placement a
// finger put on the ghost drags it, elsewhere it moves the camera, a tap puts the ghost there.
bool RtsPlay::touchInput(const RtsView& v, bool inView) {
    ImGuiIO& io = ImGui::GetIO();
    const float mx = io.MousePos.x, my = io.MousePos.y;
    const double now = ImGui::GetTime();
    const float slop = 10 * v.scale;
    const double hold = 0.45;
    m_hover = -1;   // a finger does not hover
    if (m_ghost >= 0) {
        buildNav(m_ghost);
        m_ghostOk = footprintFree(m_ghost);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            cancelPlacement();
            return true;
        }
    }
    if (inView && ImGui::IsMouseClicked(0)) {
        m_gesture = kPending;
        m_gestureT = now;
        m_downX = m_fingerX = mx;
        m_downY = m_fingerY = my;
        float3 g;
        if (m_ghost >= 0 && groundAt(v, mx, my, g)) {   // on the ghost (its footprint and a margin): it is dragged
            float3 mn, mxp;
            footprintOf(m_ghost, mn, mxp);
            if (g.x > mn.x - 1.0f && g.x < mxp.x + 1.0f && g.z > mn.z - 1.0f && g.z < mxp.z + 1.0f) {
                m_gesture = kGhost;
                m_ghostGrab = g;
                m_ghostFrom = m_scene->objects[m_ghost].position;
            }
        }
    }
    if (m_gesture == kNone) return false;
    if (mx > -1e6f && my > -1e6f) {   // (a lifted finger has no position)
        m_fingerX = mx;
        m_fingerY = my;
    }
    const bool down = ImGui::IsMouseDown(0);
    const float dx = m_fingerX - m_downX, dy = m_fingerY - m_downY;
    const bool moved = dx * dx + dy * dy > slop * slop;
    switch (m_gesture) {
    case kPending:
        if (!down) {
            m_gesture = kNone;
            tap(v, m_downX, m_downY);
        } else if (moved) m_gesture = kPan;
        else if (now - m_gestureT >= hold) m_gesture = kHold;
        break;
    case kHold:
        if (!down) {   // a long press alone: nothing selected
            m_gesture = kNone;
            if (m_ghost < 0) setSelection({});
        } else if (moved) m_gesture = m_ghost >= 0 ? kPan : kRect;
        break;
    case kRect:
        m_dragging = true;
        if (!down) {
            m_gesture = kNone;
            m_dragging = false;
            rectSelect(v, m_downX, m_downY, m_fingerX, m_fingerY, false);
        }
        break;
    case kGhost: {
        float3 g;
        if (m_ghost >= 0 && groundAt(v, m_fingerX, m_fingerY, g)) snapGhost(m_ghostFrom + (g - m_ghostGrab));
        if (!down) m_gesture = kNone;
        break;
    }
    default:   // kPan: the host moves the camera (touchPan)
        if (!down) m_gesture = kNone;
        break;
    }
    return true;
}

// --------------------------------------------------------------------------- overlay

void RtsPlay::drawOverlay(ImDrawList* dl, const RtsView& v) {
    if (!m_scene) return;
    dl->PushClipRect(ImVec2(v.x, v.y), ImVec2(v.x + v.w, v.y + v.h), true);
    auto ring = [&](int obj, ImU32 c, float thick, bool fill) {
        const Object& o = m_scene->objects[obj];
        const PrefabAsset* a = assetOf(obj);
        const float r = (a ? a->game.selectionRadius : 0.7f) * std::max(o.scale.x, o.scale.z);
        ImVec2 pts[40];
        int n = 0;
        for (int k = 0; k < 40; ++k) {
            const float t = k * 2 * kPi / 40;
            float sx, sy;
            if (!project(v, float3(o.position.x + std::cos(t) * r, 0.03f, o.position.z + std::sin(t) * r), sx, sy)) return;
            pts[n++] = ImVec2(sx, sy);
        }
        if (fill) dl->AddConvexPolyFilled(pts, n, (c & 0x00FFFFFF) | (40u << 24));
        dl->AddPolyline(pts, n, c, ImDrawFlags_Closed, thick * v.scale);
    };
    auto bar = [&](float cx, float y, float w, float k, ImU32 c) {
        const float h = 5 * v.scale;
        dl->AddRectFilled(ImVec2(cx - w * 0.5f - 1, y - 1), ImVec2(cx + w * 0.5f + 1, y + h + 1), IM_COL32(10, 12, 16, 200), 2 * v.scale);
        dl->AddRectFilled(ImVec2(cx - w * 0.5f, y), ImVec2(cx - w * 0.5f + w * std::clamp(k, 0.0f, 1.0f), y + h), c, 2 * v.scale);
    };
    if (m_hover >= 0 && std::find(m_sel.begin(), m_sel.end(), m_hover) == m_sel.end())
        ring(m_hover, IM_COL32(255, 255, 255, 140), 1.5f, false);
    for (int s : m_sel) {
        const std::string cat = objectCategory(s);
        ring(s, ringColor(cat, 0.95f), 2.2f, true);
        // bars above it: hit points, cargo, production
        const Object& o = m_scene->objects[s];
        const PrefabAsset* a = assetOf(s);
        float sx, sy;
        if (!project(v, o.position + float3(0, (a ? a->game.height : 1.5f) * o.scale.y + 0.25f, 0), sx, sy)) continue;
        const float w = (cat == "Unit" ? 46.0f : 70.0f) * v.scale;
        float y = sy - 12 * v.scale;
        if (m_rt->hasObjectVar(s, "hp")) {
            const float k = m_rt->objectVar(s, "hp") / std::max(m_rt->objectVar(s, "hpMax", 100.0f), 1.0f);
            bar(sx, y, w, k, k > 0.5f ? IM_COL32(90, 230, 120, 255) : k > 0.25f ? IM_COL32(240, 200, 60, 255) : IM_COL32(240, 80, 60, 255));
            y += 8 * v.scale;
        }
        if (m_rt->hasObjectVar(s, "cargo") && cat == "Unit")
            bar(sx, y, w, m_rt->objectVar(s, "cargo") / std::max(m_rt->objectVar(s, "cargoMax", 10.0f), 1.0f), IM_COL32(80, 255, 140, 255)), y += 8 * v.scale;
        if (m_rt->hasObjectVar(s, "stock") && cat == "Resource")
            bar(sx, y, w, m_rt->objectVar(s, "stock") / std::max(m_rt->objectVar(s, "stockMax", 100.0f), 1.0f), IM_COL32(80, 255, 140, 255)), y += 8 * v.scale;
        const float prod = m_rt->production(s);
        if (prod >= 0) bar(sx, y, w, prod, IM_COL32(120, 190, 255, 255));
    }
    // the last order: a ring shrinking on the ground where the units go
    const double ot = ImGui::GetTime() - m_orderT;
    if (ot >= 0 && ot < 0.6) {
        const float k = (float)(ot / 0.6);
        ImVec2 pts[32];
        int n = 0;
        for (int i = 0; i < 32; ++i) {
            const float t = i * 2 * kPi / 32, r = 0.9f * (1.0f - 0.6f * k);
            float sx, sy;
            if (!project(v, float3(m_orderAt.x + std::cos(t) * r, 0.04f, m_orderAt.z + std::sin(t) * r), sx, sy)) break;
            pts[n++] = ImVec2(sx, sy);
        }
        if (n == 32) dl->AddPolyline(pts, n, col(0.35f, 1.0f, 0.55f, 1.0f - k), ImDrawFlags_Closed, 2.5f * v.scale);
    }
    // touch: a press held still fills a ring around the finger (the long press: the rectangle, or deselect)
    if (m_touch && (m_gesture == kPending || m_gesture == kHold) && m_ghost < 0) {
        const float k = std::clamp((float)((ImGui::GetTime() - m_gestureT - 0.12) / 0.33), 0.0f, 1.0f);
        if (k > 0) {
            const ImVec2 c(m_fingerX, m_fingerY);
            const float r = 34 * v.scale;
            dl->AddCircle(c, r, IM_COL32(255, 255, 255, 60), 40, 3 * v.scale);
            dl->PathArcTo(c, r, -kPi * 0.5f, -kPi * 0.5f + 2 * kPi * k, 40);
            dl->PathStroke(IM_COL32(90, 255, 140, 230), 0, 3.5f * v.scale);
        }
    }
    // selection rectangle
    if (m_dragging) {
        ImGuiIO& io = ImGui::GetIO();
        const float ex = m_touch ? m_fingerX : io.MousePos.x, ey = m_touch ? m_fingerY : io.MousePos.y;
        const ImVec2 a(std::min(m_downX, ex), std::min(m_downY, ey)), b(std::max(m_downX, ex), std::max(m_downY, ey));
        dl->AddRectFilled(a, b, IM_COL32(90, 255, 140, 40));
        dl->AddRect(a, b, IM_COL32(90, 255, 140, 220), 0, 0, 1.5f * v.scale);
    }
    // the footprint of the building being placed
    if (m_ghost >= 0) {
        float3 mn, mx;
        footprintOf(m_ghost, mn, mx);
        const float3 c[4] = {float3(mn.x, 0.03f, mn.z), float3(mx.x, 0.03f, mn.z), float3(mx.x, 0.03f, mx.z), float3(mn.x, 0.03f, mx.z)};
        ImVec2 q[4];
        bool ok = true;
        for (int k = 0; k < 4 && ok; ++k) ok = project(v, c[k], q[k].x, q[k].y);
        if (ok) {
            const ImU32 fc = m_ghostOk ? IM_COL32(80, 255, 120, 70) : IM_COL32(255, 70, 60, 90);
            dl->AddConvexPolyFilled(q, 4, fc);
            dl->AddPolyline(q, 4, (fc & 0x00FFFFFF) | 0xE0000000u, ImDrawFlags_Closed, 2 * v.scale);
        }
    }
    dl->PopClipRect();
}

// --------------------------------------------------------------------------- HUD

// The HUD in three layouts (layoutName): Desktop as before; Tablet (an iPad, or a touch screen on a PC): 44 point
// buttons, the command bar along the bottom; Phone (an iPhone in landscape): small counters, folded objectives,
// the commands in a grid at the bottom right under the thumb, a reduced selection card. All inside the safe area.
void RtsPlay::drawHud(const RtsView& v, const RtsButtons* buttons) {
    if (!m_scene || !m_rt) return;
    m_device = v.device;
    const float d = v.scale;
    const std::string layout = layoutName();
    const bool phone = layout == "Phone", tablet = layout == "Tablet", big = phone || tablet;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize;
    // the safe area, then the margin
    const float m = (phone ? 8.0f : 12.0f) * d;
    const float L = v.x + v.safeL + m, T = v.y + v.safeT + m, R = v.x + v.w - v.safeR - m, B = v.y + v.h - v.safeB - m;
    const float base = ImGui::GetFontSize();
    const float targetH = big ? 44.0f * d : 0.0f;   // touch targets: 44 points (Apple HIG)
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, big ? ImVec2(12 * d, 8 * d) : ImGui::GetStyle().FramePadding);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, big ? ImVec2(8 * d, 8 * d) : ImGui::GetStyle().ItemSpacing);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, phone ? ImVec2(8 * d, 6 * d) : ImGui::GetStyle().WindowPadding);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, big ? 6 * d : ImGui::GetStyle().FrameRounding);
    bool hovered = false;
    float objectivesY = T;
    // resource counters, top left
    if (!m_rt->hudCounters.empty()) {
        ImGui::PushFont(nullptr, base * (phone ? 0.92f : tablet ? 1.15f : 1.0f));
        ImGui::SetNextWindowPos(ImVec2(L, T), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.78f);
        if (ImGui::Begin("##rtscounters", nullptr, flags)) {
            for (size_t k = 0; k < m_rt->hudCounters.size(); ++k) {
                const auto& c = m_rt->hudCounters[k];
                if (k) ImGui::SameLine(0, (phone ? 14.0f : 22.0f) * d);
                const ImVec2 p = ImGui::GetCursorScreenPos();
                const float r = (phone ? 5.5f : 7.0f) * d;
                ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + r, p.y + ImGui::GetTextLineHeight() * 0.5f), r, col(c.color.x, c.color.y, c.color.z, 1.0f));
                ImGui::GetWindowDrawList()->AddCircle(ImVec2(p.x + r, p.y + ImGui::GetTextLineHeight() * 0.5f), r, IM_COL32(255, 255, 255, 120), 0, 1.2f * d);
                ImGui::Dummy(ImVec2(2 * r + 4 * d, ImGui::GetTextLineHeight()));
                ImGui::SameLine();
                auto vars = m_rt->variables();
                auto it = vars.find(c.var);
                const int value = it == vars.end() ? 0 : (int)std::lround(it->second.asFloat());
                ImGui::Text("%s  %d", c.label.c_str(), value);
            }
            hovered |= ImGui::IsWindowHovered();
            objectivesY = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y + (phone ? 6.0f : 8.0f) * d;
        }
        ImGui::End();
        ImGui::PopFont();
    }
    // objectives, top left (Phone: folded to a line, a tap opens them)
    if (!m_rt->objectives.empty()) {
        ImGui::PushFont(nullptr, base * (phone ? 0.92f : tablet ? 1.1f : 1.0f));
        ImGui::SetNextWindowPos(ImVec2(L, objectivesY), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.72f);
        if (ImGui::Begin("##rtsobjectives", nullptr, flags)) {
            int done = 0;
            for (const auto& o : m_rt->objectives) done += o.done ? 1 : 0;
            bool open = !phone || m_objectivesOpen;
            if (phone) {
                char head[64];
                snprintf(head, sizeof head, "OBJECTIFS  %d/%d  %s##objfold", done, (int)m_rt->objectives.size(), m_objectivesOpen ? "-" : "+");
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.4f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                if (ImGui::Button(head, ImVec2(0, 36 * d))) m_objectivesOpen = !m_objectivesOpen;
                ImGui::PopStyleColor(2);
                open = m_objectivesOpen;
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "OBJECTIFS");
            }
            for (size_t k = 0; open && k < m_rt->objectives.size(); ++k) {
                const auto& o = m_rt->objectives[k];
                const ImVec2 p = ImGui::GetCursorScreenPos();
                const float r = 5 * d, cy = p.y + ImGui::GetTextLineHeight() * 0.5f;
                ImDrawList* w = ImGui::GetWindowDrawList();
                if (o.done) w->AddCircleFilled(ImVec2(p.x + r, cy), r, IM_COL32(90, 230, 120, 255));
                else w->AddCircle(ImVec2(p.x + r, cy), r, IM_COL32(220, 220, 220, 200), 0, 1.4f * d);
                ImGui::Dummy(ImVec2(2 * r + 4 * d, ImGui::GetTextLineHeight()));
                ImGui::SameLine();
                if (o.done) ImGui::TextDisabled("%s", o.text.c_str());
                else ImGui::TextUnformatted(o.text.c_str());
            }
            hovered |= ImGui::IsWindowHovered();
        }
        ImGui::End();
        ImGui::PopFont();
    }

    // the commands: the HUD buttons of the graph, then (touch) the selection and the placement
    struct Cmd { std::string label; int kind; std::string id; bool enabled; };   // kind 0 graph, 1 all, 2 none, 3 ok, 4 cancel
    std::vector<Cmd> cmds;
    if (buttons)
        for (const auto& b : *buttons) cmds.push_back({b.second, 0, b.first, m_ghost < 0});
    if (m_touch) {
        if (m_ghost >= 0) {
            cmds.push_back({"Valider", 3, "", m_ghostOk});
            cmds.push_back({"Annuler", 4, "", true});
        } else {
            cmds.push_back({phone ? "Tout" : "Tout sélectionner", 1, "", true});
            if (!m_sel.empty()) cmds.push_back({phone ? "Aucun" : "Désélectionner", 2, "", true});
        }
    }
    float barTop = B;   // the top of the command bar (the selection card stays above it on a phone... or beside)
    if (!cmds.empty()) {
        ImGui::PushFont(nullptr, base * (tablet ? 1.12f : 1.0f));
        auto press = [&](const Cmd& c) {
            switch (c.kind) {
            case 0: m_rt->fireHudButton(c.id); break;
            case 1: selectAllUnits(); break;
            case 2: setSelection({}); break;
            case 3: confirmPlacement(); break;
            default: cancelPlacement(); break;
            }
        };
        auto button = [&](const Cmd& c, float w) {
            const bool accent = c.kind == 3;
            if (accent) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.55f, 0.28f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.65f, 0.33f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.26f, 0.75f, 0.38f, 1.0f));
            }
            ImGui::BeginDisabled(!c.enabled);
            if (ImGui::Button(c.label.c_str(), ImVec2(w, targetH))) press(c);
            ImGui::EndDisabled();
            if (accent) ImGui::PopStyleColor(3);
        };
        if (phone) {   // a grid of three columns at the bottom right, under the thumb
            ImGui::SetNextWindowPos(ImVec2(R, B), ImGuiCond_Always, ImVec2(1.0f, 1.0f));
            ImGui::SetNextWindowBgAlpha(0.0f);
            ImGui::PushFont(nullptr, base * 0.86f);
            float bw = 84 * d;   // one width for the grid: the longest label
            for (const Cmd& c : cmds) bw = std::max(bw, ImGui::CalcTextSize(c.label.c_str()).x + 16 * d);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4 * d, 4 * d));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6 * d, 6 * d));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.10f, 0.12f, 0.82f));
            if (ImGui::Begin("##rtscommands", nullptr, flags)) {
                for (size_t k = 0; k < cmds.size(); ++k) {
                    ImGui::PushID((int)k);
                    if (k % 3) ImGui::SameLine();
                    button(cmds[k], bw);
                    ImGui::PopID();
                }
                hovered |= ImGui::IsWindowHovered();
                barTop = ImGui::GetWindowPos().y;
            }
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(3);
            ImGui::PopFont();
        } else {
            // the buttons of the graph at the bottom left (as before), the touch commands at the bottom right
            const bool any0 = std::any_of(cmds.begin(), cmds.end(), [](const Cmd& c) { return c.kind == 0; });
            if (any0) {
                ImGui::SetNextWindowPos(ImVec2(L, B), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
                ImGui::SetNextWindowBgAlpha(0.85f);
                if (ImGui::Begin("##rtsbuttons", nullptr, flags)) {
                    bool first = true;
                    for (size_t k = 0; k < cmds.size(); ++k) {
                        if (cmds[k].kind != 0) continue;
                        ImGui::PushID((int)k);
                        if (!first) ImGui::SameLine();
                        first = false;
                        button(cmds[k], 0);
                        ImGui::PopID();
                    }
                    if (!m_touch && m_ghost >= 0) ImGui::TextDisabled("Clic : placer  ·  Clic droit ou Échap : annuler");
                    hovered |= ImGui::IsWindowHovered();
                }
                ImGui::End();
            }
            const bool any1 = std::any_of(cmds.begin(), cmds.end(), [](const Cmd& c) { return c.kind != 0; });
            if (any1) {
                ImGui::SetNextWindowPos(ImVec2(R, B), ImGuiCond_Always, ImVec2(1.0f, 1.0f));
                ImGui::SetNextWindowBgAlpha(0.85f);
                if (ImGui::Begin("##rtstouch", nullptr, flags)) {
                    bool first = true;
                    for (size_t k = 0; k < cmds.size(); ++k) {
                        if (cmds[k].kind == 0) continue;
                        ImGui::PushID((int)k);
                        if (!first) ImGui::SameLine();
                        first = false;
                        button(cmds[k], 0);
                        ImGui::PopID();
                    }
                    hovered |= ImGui::IsWindowHovered();
                }
                ImGui::End();
            }
        }
        ImGui::PopFont();
    }

    // the selection card: bottom centre (Phone: reduced, bottom left)
    if (!m_sel.empty()) {
        ImGui::PushFont(nullptr, base * (phone ? 0.92f : tablet ? 1.12f : 1.0f));
        if (phone) ImGui::SetNextWindowPos(ImVec2(L, B), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
        else ImGui::SetNextWindowPos(ImVec2((L + R) * 0.5f, B), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
        ImGui::SetNextWindowBgAlpha(0.8f);
        if (ImGui::Begin("##rtsselection", nullptr, flags)) {
            const PrefabAsset* a = assetOf(m_sel[0]);
            const std::string name = a ? a->name : m_scene->objects[m_sel[0]].name;
            if (m_sel.size() > 1) ImGui::Text("%s  x%d", name.c_str(), (int)m_sel.size());
            else ImGui::TextUnformatted(name.c_str());
            int queued = 0;
            std::string prefab;
            const float p = m_rt->production(m_sel[0], &queued, &prefab);
            if (p >= 0) {
                if (!phone) ImGui::TextDisabled("Production: %s%s", prefab.c_str(), queued > 1 ? ("  (+" + std::to_string(queued - 1) + ")").c_str() : "");
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.45f, 0.75f, 1.0f, 1.0f));
                ImGui::ProgressBar(p, ImVec2((phone ? 140.0f : 220.0f) * d, (phone ? 6.0f : 8.0f) * d), "");
                ImGui::PopStyleColor();
            }
            if (m_rt->hasObjectVar(m_sel[0], "cargo"))
                ImGui::TextDisabled("Cargo %d / %d", (int)m_rt->objectVar(m_sel[0], "cargo"), (int)m_rt->objectVar(m_sel[0], "cargoMax", 10));
            if (m_rt->hasObjectVar(m_sel[0], "stock"))
                ImGui::TextDisabled("Stock %d", (int)m_rt->objectVar(m_sel[0], "stock"));
            hovered |= ImGui::IsWindowHovered();
        }
        ImGui::End();
        ImGui::PopFont();
    }

    // the controls of the input mode, top centre, for a while after Play (or after the mode changed)
    const double since = m_startT < 0 ? 0.0 : ImGui::GetTime() - m_startT;
    if (since < 14.0 && !m_rt->messageShown) {
        const float alpha = (float)std::clamp((14.0 - since) / 1.5, 0.0, 1.0);
        const char* hint = !m_touch ? "Clic : sélection  ·  Glisser : rectangle  ·  Double-clic : même type  ·  Clic droit : ordre  ·  ZQSD, bords, clic droit glissé : caméra"
                         : phone ? "Toucher : sélection  ·  Toucher le sol ou une station : ordre  ·  Glisser : caméra  ·  Appui long + glisser : rectangle"
                                 : "Toucher : sélection  ·  Double toucher : même type  ·  Toucher le sol ou une station : ordre  ·  Glisser : caméra  ·  Appui long + glisser : rectangle  ·  Pincer : zoom";
        const float w = std::min(R - L - 2 * 230 * d, 620 * d);
        if (w > 160 * d) {
            ImGui::PushFont(nullptr, base * (phone ? 0.86f : 0.95f));
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
            ImGui::SetNextWindowPos(ImVec2((L + R) * 0.5f, T), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
            ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(w, FLT_MAX));
            ImGui::SetNextWindowBgAlpha(0.55f);
            if (ImGui::Begin("##rtshint", nullptr, flags | ImGuiWindowFlags_NoInputs)) {
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w - 2 * ImGui::GetStyle().WindowPadding.x);
                ImGui::TextUnformatted(hint);
                ImGui::PopTextWrapPos();
            }
            ImGui::End();
            ImGui::PopStyleVar();
            ImGui::PopFont();
        }
    }

    // message box, centre
    if (m_rt->messageShown) {
        const float w = std::min((R - L) * 0.8f, (phone ? 440.0f : 480.0f) * d);
        ImGui::SetNextWindowPos(ImVec2((L + R) * 0.5f, v.y + v.h * 0.42f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(w, 0), ImVec2(w, FLT_MAX));
        ImGui::SetNextWindowBgAlpha(0.92f);
        ImGui::PushFont(nullptr, base * (tablet ? 1.12f : 1.0f));
        if (ImGui::Begin("##rtsmessage", nullptr, flags)) {
            ImGui::PushFont(nullptr, base * (phone ? 1.35f : 1.6f));
            ImGui::TextColored(ImVec4(0.55f, 1.0f, 0.65f, 1.0f), "%s", m_rt->messageTitle.c_str());
            ImGui::PopFont();
            if (!m_rt->messageText.empty()) ImGui::TextWrapped("%s", m_rt->messageText.c_str());
            ImGui::Spacing();
            if (ImGui::Button("OK", ImVec2(120 * d, targetH))) m_rt->messageShown = false;
            hovered |= ImGui::IsWindowHovered();
        }
        ImGui::End();
        ImGui::PopFont();
    }
    ImGui::PopStyleVar(4);
    (void)barTop;
    m_hudHovered = hovered;
}

} // namespace app

namespace app {

bool RtsPlay::placeAt(float3 p) {
    if (!m_scene || !m_rt || m_ghost < 0) return false;
    Object& g = m_scene->objects[m_ghost];
    const PrefabAsset* a = assetOf(m_ghost);
    const float ox = a && (a->game.footprintX % 2) ? 0.5f : 0.0f, oz = a && (a->game.footprintZ % 2) ? 0.5f : 0.0f;
    g.position = float3(std::floor(p.x - ox + 0.5f) + ox, 0, std::floor(p.z - oz + 0.5f) + oz);
    buildNav(m_ghost);
    m_ghostOk = footprintFree(m_ghost);
    if (!m_ghostOk) return false;
    g.ghost = false;
    g.selectable = true;
    const int placed = m_ghost;
    m_ghost = -1;
    m_rt->firePlacement(placed);
    return true;
}

} // namespace app

namespace app {

bool RtsPlay::moveGhost(float3 p) {
    if (!m_scene || m_ghost < 0) return false;
    Object& g = m_scene->objects[m_ghost];
    const PrefabAsset* a = assetOf(m_ghost);
    const float ox = a && (a->game.footprintX % 2) ? 0.5f : 0.0f, oz = a && (a->game.footprintZ % 2) ? 0.5f : 0.0f;
    g.position = float3(std::floor(p.x - ox + 0.5f) + ox, 0, std::floor(p.z - oz + 0.5f) + oz);
    buildNav(m_ghost);
    m_ghostOk = footprintFree(m_ghost);
    m_ghostHeld = true;
    return m_ghostOk;
}

} // namespace app
