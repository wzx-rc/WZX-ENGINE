// BrickIt - groups and sub-groups: Ctrl+G groups the selection, Ctrl+Shift+G ungroups, a click selects the whole
// group at the level entered, a double click enters it (Échap leaves), the group moves / turns / duplicates as one
// block, hidden or isolated from the outliner. A brick keeps the path of its groups (outermost first, ids in
// Builder::groups): the undo snapshots of the bricks keep the groups with them.
#include "BrickApp.h"
#include <algorithm>
#include <cstdio>

namespace brickit {

static bool startsWith(const std::vector<int>& path, const std::vector<int>& prefix) {
    if (path.size() < prefix.size()) return false;
    for (size_t k = 0; k < prefix.size(); ++k)
        if (path[k] != prefix[k]) return false;
    return true;
}

int BrickApp::unitGroup(int brick) const {
    if (brick < 0 || brick >= (int)m_bricks.size()) return -1;
    const std::vector<int>& p = m_bricks[brick].path;
    if (!startsWith(p, m_groupCtx) || p.size() <= m_groupCtx.size()) return -1;
    return p[m_groupCtx.size()];
}

std::vector<int> BrickApp::unitOf(int brick) const {
    std::vector<int> u;
    const int g = unitGroup(brick);
    if (g < 0) { if (brick >= 0) u.push_back(brick); return u; }
    const size_t k = m_groupCtx.size();
    for (int i = 0; i < (int)m_bricks.size(); ++i) {
        const std::vector<int>& p = m_bricks[i].path;
        if (p.size() > k && p[k] == g && startsWith(p, m_groupCtx)) u.push_back(i);
    }
    return u;
}

std::vector<int> BrickApp::groupMembers(int group) const {
    std::vector<int> u;
    for (int i = 0; i < (int)m_bricks.size(); ++i)
        if (std::find(m_bricks[i].path.begin(), m_bricks[i].path.end(), group) != m_bricks[i].path.end()) u.push_back(i);
    return u;
}

bool BrickApp::brickHidden(int i) const {
    if (i < 0 || i >= (int)m_bricks.size()) return false;
    const std::vector<int>& p = m_bricks[i].path;
    for (int g : p)
        if (g >= 0 && g < (int)m_build.groups.size() && m_build.groups[g].hidden) return true;
    if (m_isolateGroup >= 0 && std::find(p.begin(), p.end(), m_isolateGroup) == p.end()) return true;
    return false;
}

// The context follows the clicked brick: a brick outside the group entered leaves it (down to their common groups).
void BrickApp::fitGroupContext(int brick) {
    if (brick < 0 || brick >= (int)m_bricks.size()) return;
    const std::vector<int>& p = m_bricks[brick].path;
    size_t k = 0;
    while (k < m_groupCtx.size() && k < p.size() && m_groupCtx[k] == p[k]) ++k;
    m_groupCtx.resize(k);
}

void BrickApp::enterGroup(int brick) {
    if (brick < 0 || brick >= (int)m_bricks.size()) return;
    fitGroupContext(brick);
    const int g = unitGroup(brick);
    if (g < 0) return;
    m_groupCtx.push_back(g);
    select(brick);
    char t[160];
    snprintf(t, sizeof(t), "Dans le groupe « %s » : clic = une pièce ou un sous-groupe, Échap pour sortir", m_build.groups[g].name.c_str());
    toast(t);
}

bool BrickApp::leaveGroup() {
    if (m_groupCtx.empty()) return false;
    m_groupCtx.pop_back();
    if (m_selected >= 0) select(m_selected);
    toast(m_groupCtx.empty() ? "Sorti du groupe" : "Groupe parent");
    return true;
}

// Ctrl+G: the units selected (bricks or groups at the level entered) go into a new group, at the deepest level they
// all share.
void BrickApp::groupSelected() {
    std::vector<int> s = selection();
    if (s.empty()) return;
    std::vector<int> common = m_bricks[s[0]].path;
    for (int i : s) {
        const std::vector<int>& p = m_bricks[i].path;
        size_t k = 0;
        while (k < common.size() && k < p.size() && common[k] == p[k]) ++k;
        common.resize(k);
    }
    // not deeper than the units themselves: a brick alone at that level stays a member, not a group of one
    if (common.size() > m_groupCtx.size() && startsWith(common, m_groupCtx)) common.resize(m_groupCtx.size());
    pushUndo();
    const int id = (int)m_build.groups.size();
    Group g;
    char nm[32];
    snprintf(nm, sizeof(nm), "Groupe %d", id + 1);
    g.name = nm;
    m_build.groups.push_back(g);
    for (int i : s) {
        std::vector<int>& p = m_bricks[i].path;
        p.insert(p.begin() + std::min(common.size(), p.size()), id);
    }
    m_groupCtx = common;
    m_pGroups.visible = true;   // the outliner shows the new group
    select(s[0]);
    char t[96];
    snprintf(t, sizeof(t), "%s : %d pièces (Ctrl+Maj+G pour dégrouper)", nm, (int)s.size());
    toast(t);
}

// Ctrl+Shift+G: the group of the selection (at the level entered) dissolves; its sub-groups stay.
void BrickApp::ungroupSelected() {
    const int g = unitGroup(m_selected);
    if (g < 0) { toast("La sélection n'est pas un groupe"); return; }
    pushUndo();
    for (int i : groupMembers(g)) {
        std::vector<int>& p = m_bricks[i].path;
        p.erase(std::remove(p.begin(), p.end(), g), p.end());
    }
    if (m_isolateGroup == g) m_isolateGroup = -1;
    select(m_selected);
    toast("Groupe dissous");
}

// a group turns as one block, 90 degrees around Y through the stud lattice point nearest to its centre
void BrickApp::rotateGroup(const std::vector<int>& s, int dir) {
    float3 mn(1e30f), mx(-1e30f);
    for (int i : s) {
        const Box b = m_build.aabbOf(m_bricks[i].part, m_bricks[i].rot, m_bricks[i].pos);
        mn = minv(mn, b.mn);
        mx = maxv(mx, b.mx);
    }
    float3 c = (mn + mx) * 0.5f;
    c.x = std::round(c.x / 0.4f) * 0.4f;   // half a stud: the rotated studs stay on the lattice
    c.z = std::round(c.z / 0.4f) * 0.4f;
    const float4 q = qAxisAngle(float3(0, 1, 0), dir * kPi * 0.5f);
    for (int i : s) {
        Brick& b = m_bricks[i];
        const float3 g = m_build.grabOf(b);
        const float3 w = m_build.worldVoxel(b, g);
        const float3 w2 = c + qRot(q, w - c);
        b.rot = snapQuat(qMul(q, b.rot));
        b.pos = w2 - qRot(b.rot, g);
    }
}

// Ctrl+D on a group: a copy of the whole group stacked right above it, a group of its own (its sub-groups copied too)
bool BrickApp::duplicateGroup(const std::vector<int>& s) {
    float3 mn(1e30f), mx(-1e30f);
    for (int i : s) {
        const Box b = m_build.aabbOf(m_bricks[i].part, m_bricks[i].rot, m_bricks[i].pos);
        mn = minv(mn, b.mn);
        mx = maxv(mx, b.mx);
    }
    const float h = std::max(0.32f, std::round((mx.y - mn.y) / 0.32f) * 0.32f);
    const size_t k = m_groupCtx.size();
    std::vector<std::pair<int, int>> remap;   // old group id -> its copy (the unit and every group inside it)
    auto copyOf = [&](int g) {
        for (auto& r : remap) if (r.first == g) return r.second;
        Group c = m_build.groups[g];
        c.name += " (copie)";
        c.hidden = false;
        m_build.groups.push_back(c);
        remap.push_back({g, (int)m_build.groups.size() - 1});
        return remap.back().second;
    };
    const size_t n0 = m_bricks.size();
    for (int i : s) {
        Brick b = m_bricks[i];
        b.pos.y += h;
        for (size_t d = k; d < b.path.size(); ++d) b.path[d] = copyOf(b.path[d]);
        m_bricks.push_back(b);
    }
    if (m_bricks.size() == n0) return false;
    m_selected = (int)n0;
    rebuildScene();
    select(m_selected);
    return true;
}

// Selection mode, a drag on a group: the whole group slides over the plane of the point grabbed, on the stud lattice
void BrickApp::beginGroupDrag(const std::vector<int>& s, float sx, float sy) {
    pushUndo();
    m_groupDrag = true;
    m_groupDragBricks = s;
    m_groupDragPos.clear();
    for (int i : s) m_groupDragPos.push_back(m_bricks[i].pos);
    m_groupDragY = m_pressPoint.y;
    float3 o, d;
    view().ray(sx, sy, o, d);
    const float t = std::fabs(d.y) > 1e-6f ? (m_groupDragY - o.y) / d.y : 0.0f;
    m_groupDragStart = o + d * std::max(t, 0.0f);
}

void BrickApp::updateGroupDrag(float sx, float sy) {
    float3 o, d;
    view().ray(sx, sy, o, d);
    if (std::fabs(d.y) < 1e-6f) return;
    const float t = (m_groupDragY - o.y) / d.y;
    if (t <= 0) return;
    float3 delta = o + d * t - m_groupDragStart;
    delta.y = 0;
    if (m_build.gridSnap) { delta.x = std::round(delta.x / 0.8f) * 0.8f; delta.z = std::round(delta.z / 0.8f) * 0.8f; }
    for (size_t k = 0; k < m_groupDragBricks.size(); ++k) m_bricks[m_groupDragBricks[k]].pos = m_groupDragPos[k] + delta;
    syncObjects();
}

void BrickApp::endGroupDrag() {
    m_groupDrag = false;
    rebuildScene();
    autosave();
}

// --group-test file: groups 0..3, then that group with brick 4 (nested), checks the selection by click, the context
// of a double click, a save / reload round trip, a group turned and duplicated as one block, a group hidden; writes
// the results to the file and leaves the outliner open for the capture.
void BrickApp::groupTest() {
    std::string log;
    int pass = 0, total = 0;
    auto check = [&](bool ok, const char* what) { ++total; pass += ok; log += std::string(ok ? "[OK] " : "[ECHEC] ") + what + "\n"; };
    if (m_bricks.size() < 6) { log = "scène trop petite\n"; }
    else {
        m_groupCtx.clear();
        m_selected = 0; m_multi = {1, 2, 3};
        groupSelected();
        check(unitOf(0).size() == 4 && unitGroup(2) == 0, "Ctrl+G : 4 pièces dans « Groupe 1 »");
        select(2);
        check(selection().size() == 4, "clic sur une pièce du groupe : tout le groupe sélectionné");
        m_selected = 0; m_multi = {1, 2, 3, 4};
        groupSelected();
        check(unitOf(4).size() == 5 && m_bricks[0].path.size() == 2 && m_bricks[4].path.size() == 1, "groupe imbriqué : « Groupe 2 » = Groupe 1 + pièce 4");
        m_groupCtx.clear();
        select(1);
        check(selection().size() == 5, "niveau du haut : clic = Groupe 2 entier (5 pièces)");
        enterGroup(1);
        check(selection().size() == 4 && m_groupCtx.size() == 1, "double clic : dans Groupe 2, clic = Groupe 1 (4 pièces)");
        select(4);
        check(selection().size() == 1, "dans Groupe 2 : la pièce 4 seule");
        leaveGroup();
        check(m_groupCtx.empty() && selection().size() == 5, "Échap : sorti, Groupe 2 sélectionné");
        // save / reload
        const std::string a = m_build.toJson(m_scene.camera);
        std::string msg;
        m_build.fromJson(a, nullptr, &msg);
        const std::string b = m_build.toJson(m_scene.camera);
        check(a == b && m_build.groups.size() == 2 && m_bricks[0].path.size() == 2, "enregistrer / rouvrir : groupes et chemins identiques");
        // turn as one block: the distances between the members are kept
        rebuildScene();
        select(0);
        const std::vector<int> s = selection();
        auto spread = [&]() { float d = 0; for (int i : s) d += length(m_build.worldVoxel(m_bricks[i], m_build.grabOf(m_bricks[i])) - m_build.worldVoxel(m_bricks[s[0]], m_build.grabOf(m_bricks[s[0]]))); return d; };
        const float d0 = spread();
        rotateGroup(s, 1);
        check(std::fabs(spread() - d0) < 1e-3f, "tourner le groupe d'un bloc : écarts entre pièces gardés");
        rotateGroup(s, -1);
        const size_t n0 = m_bricks.size();
        duplicateGroup(s);
        check(m_bricks.size() == n0 + s.size() && unitOf((int)n0).size() == s.size() && unitGroup((int)n0) != unitGroup(s[0]),
              "Ctrl+D : copie du groupe entier au-dessus, groupe à part");
        m_build.groups[0].hidden = true;
        syncObjects();
        int hidden = 0;
        for (int i = 0; i < (int)n0; ++i) hidden += !m_scene.objects[m_firstBrickObject + i].visible;
        check(hidden == 4, "masquer Groupe 1 : ses 4 pièces cachées");
        m_build.groups[0].hidden = false;
        m_groupsOpen = {1};
        m_pGroups.visible = true;
        select(0);
    }
    char t[64];
    snprintf(t, sizeof(t), "\n%d / %d réussis\n", pass, total);
    log += t;
    FILE* f = nullptr;
    if (_wfopen_s(&f, m_opt.groupTest.c_str(), L"wb") == 0 && f) { fwrite(log.data(), 1, log.size(), f); fclose(f); }
}

} // namespace brickit
