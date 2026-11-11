// RTS play layer (see RtsPlay.h).
#include "RtsPlay.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
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

bool RtsPlay::input(const RtsView& v, bool overUi) {
    if (!m_scene || !m_rt) return false;
    ImGuiIO& io = ImGui::GetIO();
    const float mx = io.MousePos.x, my = io.MousePos.y;
    const bool inView = mx >= v.x && my >= v.y && mx < v.x + v.w && my < v.y + v.h && !overUi && !m_hudHovered;
    // the selection keeps only what is alive
    m_sel.erase(std::remove_if(m_sel.begin(), m_sel.end(), [&](int o) { return o < 0 || o >= (int)m_scene->objects.size() || !m_scene->objects[o].visible; }), m_sel.end());
    m_hover = inView && !m_dragging ? pickRoot(v, mx, my, nullptr) : -1;

    // placement of a building: the ghost follows the ground under the mouse, snapped to the 1 unit grid
    if (m_ghost >= 0) {
        Object& g = m_scene->objects[m_ghost];
        float3 p;
        if (std::fabs(io.MouseDelta.x) + std::fabs(io.MouseDelta.y) > 0.5f) m_ghostHeld = false;
        if (!m_ghostHeld && inView && groundAt(v, mx, my, p)) {
            const PrefabAsset* a = assetOf(m_ghost);
            const float ox = a && (a->game.footprintX % 2) ? 0.5f : 0.0f, oz = a && (a->game.footprintZ % 2) ? 0.5f : 0.0f;
            g.position = float3(std::floor(p.x - ox + 0.5f) + ox, 0, std::floor(p.z - oz + 0.5f) + oz);
        }
        buildNav(m_ghost);
        m_ghostOk = footprintFree(m_ghost);
        const bool cancel = ImGui::IsKeyPressed(ImGuiKey_Escape, false) || (inView && ImGui::IsMouseClicked(1));
        if (cancel) {
            g.visible = false;
            m_ghost = -1;
            m_rt->firePlacement(-1);
            return true;
        }
        if (inView && ImGui::IsMouseClicked(0)) {
            if (!m_ghostOk) return true;   // red: nothing happens
            g.ghost = false;
            g.selectable = true;
            const int placed = m_ghost;
            m_ghost = -1;
            m_rt->firePlacement(placed);
            return true;
        }
        return inView && (ImGui::IsMouseDown(0) || ImGui::IsMouseReleased(0));
    }

    bool handled = false;
    // left: click selects, drag draws a rectangle
    if (inView && ImGui::IsMouseClicked(0)) {
        m_leftDown = true;
        m_dragging = false;
        m_downX = mx;
        m_downY = my;
    }
    if (m_leftDown && ImGui::IsMouseDown(0)) {
        if (!m_dragging && (std::fabs(mx - m_downX) > 6 * v.scale || std::fabs(my - m_downY) > 6 * v.scale)) m_dragging = true;
        handled = true;
    }
    if (m_leftDown && ImGui::IsMouseReleased(0)) {
        m_leftDown = false;
        handled = true;
        const bool add = io.KeyShift;
        std::vector<int> s = add ? m_sel : std::vector<int>();
        if (m_dragging) {
            m_dragging = false;
            const float x0 = std::min(m_downX, mx), x1 = std::max(m_downX, mx), y0 = std::min(m_downY, my), y1 = std::max(m_downY, my);
            for (int i = 0; i < (int)m_scene->objects.size(); ++i) {
                const Object& o = m_scene->objects[i];
                if (!o.isInstanceRoot() || !o.visible || o.ghost || objectCategory(i) != "Unit") continue;
                float sx, sy;
                if (project(v, o.position + float3(0, 0.5f, 0), sx, sy) && sx >= x0 && sx <= x1 && sy >= y0 && sy <= y1 &&
                    std::find(s.begin(), s.end(), i) == s.end())
                    s.push_back(i);
            }
            setSelection(s);
        } else {
            float3 point;
            const int r = pickRoot(v, mx, my, &point);
            if (r >= 0) {
                auto it = std::find(s.begin(), s.end(), r);
                if (add && it != s.end()) s.erase(it);
                else if (it == s.end()) s.push_back(r);
                setSelection(s);
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
            if (r >= 0) m_rt->fireRightClickObject(r, point);
            else {
                float3 g;
                if (groundAt(v, mx, my, g)) m_rt->fireRightClickGround(g);
            }
        }
    }
    return handled;
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
    // selection rectangle
    if (m_dragging) {
        ImGuiIO& io = ImGui::GetIO();
        const ImVec2 a(std::min(m_downX, io.MousePos.x), std::min(m_downY, io.MousePos.y)), b(std::max(m_downX, io.MousePos.x), std::max(m_downY, io.MousePos.y));
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

void RtsPlay::drawHud(const RtsView& v) {
    if (!m_scene || !m_rt) return;
    const float d = v.scale;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize;
    bool hovered = false;
    float objectivesY = v.y + 12 * d;
    // resource counters, top left
    if (!m_rt->hudCounters.empty()) {
        ImGui::SetNextWindowPos(ImVec2(v.x + 12 * d, v.y + 12 * d), ImGuiCond_Always);   // top left: resources, then objectives
        ImGui::SetNextWindowBgAlpha(0.78f);
        if (ImGui::Begin("##rtscounters", nullptr, flags)) {
            for (size_t k = 0; k < m_rt->hudCounters.size(); ++k) {
                const auto& c = m_rt->hudCounters[k];
                if (k) ImGui::SameLine(0, 22 * d);
                const ImVec2 p = ImGui::GetCursorScreenPos();
                const float r = 7 * d;
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
            objectivesY = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y + 8 * d;
        }
        ImGui::End();
    }
    // objectives, top left
    if (!m_rt->objectives.empty()) {
        ImGui::SetNextWindowPos(ImVec2(v.x + 12 * d, objectivesY), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.72f);
        if (ImGui::Begin("##rtsobjectives", nullptr, flags)) {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "OBJECTIFS");
            for (const auto& o : m_rt->objectives) {
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
    }
    // the selection card, bottom centre: what it is, how many, its production
    if (!m_sel.empty()) {
        ImGui::SetNextWindowPos(ImVec2(v.x + v.w * 0.5f, v.y + v.h - 12 * d), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
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
                ImGui::TextDisabled("Production: %s%s", prefab.c_str(), queued > 1 ? ("  (+" + std::to_string(queued - 1) + ")").c_str() : "");
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.45f, 0.75f, 1.0f, 1.0f));
                ImGui::ProgressBar(p, ImVec2(220 * d, 8 * d), "");
                ImGui::PopStyleColor();
            }
            if (m_rt->hasObjectVar(m_sel[0], "cargo"))
                ImGui::TextDisabled("Cargo %d / %d", (int)m_rt->objectVar(m_sel[0], "cargo"), (int)m_rt->objectVar(m_sel[0], "cargoMax", 10));
            if (m_rt->hasObjectVar(m_sel[0], "stock"))
                ImGui::TextDisabled("Stock %d", (int)m_rt->objectVar(m_sel[0], "stock"));
            hovered |= ImGui::IsWindowHovered();
        }
        ImGui::End();
    }
    // message box, centre
    if (m_rt->messageShown) {
        ImGui::SetNextWindowPos(ImVec2(v.x + v.w * 0.5f, v.y + v.h * 0.42f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowBgAlpha(0.92f);
        if (ImGui::Begin("##rtsmessage", nullptr, flags)) {
            ImGui::SetWindowFontScale(1.6f);
            ImGui::TextColored(ImVec4(0.55f, 1.0f, 0.65f, 1.0f), "%s", m_rt->messageTitle.c_str());
            ImGui::SetWindowFontScale(1.0f);
            if (!m_rt->messageText.empty()) ImGui::TextWrapped("%s", m_rt->messageText.c_str());
            ImGui::Spacing();
            if (ImGui::Button("OK", ImVec2(120 * d, 0))) m_rt->messageShown = false;
            hovered |= ImGui::IsWindowHovered();
        }
        ImGui::End();
    }
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
