// Scene Manager (outliner) data: stable ids, parent / child links, order of the siblings, groups, collections and the
// selection of the list (drawn by App::atlHierarchy, AtelierUI.cpp).
// Transforms stay world values in vp3d::Object (the renderers and the players read them as they are): when a parent
// moves, outlinerSync moves its children by the same rigid change, so the children follow their parent.
#include "App.h"
#include <algorithm>
#include <cctype>
#include <functional>
#include <set>

namespace app {

// listed in the Scene Manager: not the placement ghosts, not the houses whose building is gone (AppRts.cpp)
static bool listed(const Object& o) { return !o.ghost && !(o.part == "House" && o.grab == -2); }

static Affine mul(const Affine& a, const Affine& b) {
    Affine r;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 4; ++j) {
            float v = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
            if (j == 3) v += a.m[i][3];
            r.m[i][j] = v;
        }
    return r;
}

static Affine objAffine(const Object& o) { return o.useRot ? Affine::trsQuat(o.position, o.rot, o.scale) : Affine::trs(o.position, o.yaw, o.scale); }

static bool translationOnly(const Affine& d) {
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            if (std::fabs(d.m[i][j] - (i == j ? 1.0f : 0.0f)) > 1e-6f) return false;
    return true;
}

// world transform of an object set from a matrix (scale along its own axes, the shear of a non uniform parent is dropped)
static void setFromAffine(Object& o, const Affine& m) {
    float3 X(m.m[0][0], m.m[1][0], m.m[2][0]), Y(m.m[0][1], m.m[1][1], m.m[2][1]), Z(m.m[0][2], m.m[1][2], m.m[2][2]);
    float sx = length(X), sy = length(Y), sz = length(Z);
    if (sx < 1e-8f || sy < 1e-8f || sz < 1e-8f) return;
    float3 x = X / sx;
    float3 y = Y - x * dot(x, Y);
    y = normalize(y);
    float3 z = cross(x, y);
    if (dot(z, Z) < 0) sz = -sz;
    o.position = float3(m.m[0][3], m.m[1][3], m.m[2][3]);
    o.rot = qNormalize(qFromBasis(x, y, z));
    o.useRot = true;
    o.scale = float3(sx, sy, std::fabs(sz));
}

static HierXf snap(const Object& o) {
    HierXf h;
    h.p = o.position;
    h.r = o.rot;
    h.sc = o.scale;
    h.yaw = o.yaw;
    h.useRot = o.useRot;
    return h;
}
static bool sameXf(const HierXf& h, const Object& o) {
    return h.p.x == o.position.x && h.p.y == o.position.y && h.p.z == o.position.z && h.sc.x == o.scale.x && h.sc.y == o.scale.y &&
           h.sc.z == o.scale.z && h.useRot == o.useRot && (o.useRot ? (h.r.x == o.rot.x && h.r.y == o.rot.y && h.r.z == o.rot.z && h.r.w == o.rot.w) : h.yaw == o.yaw);
}
static Affine cachedAffine(const HierXf& h) { return h.useRot ? Affine::trsQuat(h.p, h.r, h.sc) : Affine::trs(h.p, h.yaw, h.sc); }

// --------------------------------------------------------------------------- ids and links

uint32_t App::newNodeId() {
    uint32_t m = 0;
    for (const Object& o : m_scene.objects) m = std::max(m, o.id);
    for (const AreaLight& l : m_scene.lights) m = std::max(m, l.id);
    for (const Collection& c : m_scene.collections) m = std::max(m, c.id);
    m = std::max(m, m_scene.sun.id);
    return m + 1;
}

// Every node gets a unique id (copies made by Duplicate or by the game layer get a new one), the parents and the
// collections that do not exist any more (deleted) are dropped, and a parent loop is broken.
void App::outlinerIds() {
    std::set<uint32_t> seen;
    uint32_t next = newNodeId();
    auto fix = [&](uint32_t& id) {
        if (id == 0 || seen.count(id)) id = next++;
        seen.insert(id);
    };
    for (Collection& c : m_scene.collections) fix(c.id);
    fix(m_scene.sun.id);
    for (Object& o : m_scene.objects) fix(o.id);
    for (AreaLight& l : m_scene.lights) fix(l.id);

    std::map<uint32_t, int> objIdx;
    for (size_t i = 0; i < m_scene.objects.size(); ++i) objIdx[m_scene.objects[i].id] = (int)i;
    std::set<uint32_t> cols;
    for (const Collection& c : m_scene.collections) cols.insert(c.id);
    for (Object& o : m_scene.objects) {
        if (o.parent && (o.parent == o.id || !objIdx.count(o.parent))) o.parent = 0;
        if (o.collection && !cols.count(o.collection)) o.collection = 0;
        if (o.group) o.mesh = -1;
    }
    // parent loops (a node dropped under its own descendant by an old file): cut at the node that closes the loop
    for (Object& o : m_scene.objects) {
        uint32_t p = o.parent;
        for (size_t step = 0; p && step <= m_scene.objects.size(); ++step) {
            if (p == o.id) { o.parent = 0; break; }
            p = m_scene.objects[objIdx[p]].parent;
        }
    }
    for (AreaLight& l : m_scene.lights) {
        if (l.parent && !objIdx.count(l.parent)) l.parent = 0;
        if (l.collection && !cols.count(l.collection)) l.collection = 0;
    }
    if (m_scene.sun.collection && !cols.count(m_scene.sun.collection)) m_scene.sun.collection = 0;
}

// Ids, then the children follow their parent: a node whose world transform changed since the last frame (gizmo,
// inspector, game layer) moves its children by the same change unless they changed themselves (undo restores the
// whole family at once, a multi-selection moves every node itself).
void App::outlinerSync() {
    outlinerIds();
    std::map<uint32_t, std::vector<int>> kids, lightKids;
    for (size_t i = 0; i < m_scene.objects.size(); ++i) kids[m_scene.objects[i].parent].push_back((int)i);
    for (size_t i = 0; i < m_scene.lights.size(); ++i)
        if (m_scene.lights[i].parent) lightKids[m_scene.lights[i].parent].push_back((int)i);
    bool moved = false;
    std::set<uint32_t> alive;
    std::function<void(int, const Affine*)> visit = [&](int i, const Affine* pd) {
        Object& o = m_scene.objects[i];
        alive.insert(o.id);
        auto it = m_hierCache.find(o.id);
        Affine d;
        const Affine* out = nullptr;
        if (it != m_hierCache.end()) {
            if (!sameXf(it->second, o)) {
                d = mul(objAffine(o), cachedAffine(it->second).inverse());
                out = &d;
            } else if (pd) {
                if (translationOnly(*pd)) o.position = o.position + float3(pd->m[0][3], pd->m[1][3], pd->m[2][3]);
                else setFromAffine(o, mul(*pd, objAffine(o)));
                d = *pd;
                out = &d;
                moved = true;
            }
        }
        m_hierCache[o.id] = snap(o);
        auto lk = lightKids.find(o.id);
        if (lk != lightKids.end())
            for (int li : lk->second) {
                AreaLight& l = m_scene.lights[li];
                auto c = m_hierCache.find(l.id);
                bool self = c != m_hierCache.end() && (c->second.p.x != l.position.x || c->second.p.y != l.position.y || c->second.p.z != l.position.z ||
                                                       c->second.t.x != l.target.x || c->second.t.y != l.target.y || c->second.t.z != l.target.z);
                if (out && c != m_hierCache.end() && !self) {
                    l.position = out->point(l.position);
                    l.target = out->point(l.target);
                    moved = true;
                }
                HierXf h;
                h.p = l.position;
                h.t = l.target;
                m_hierCache[l.id] = h;
                alive.insert(l.id);
            }
        auto k = kids.find(o.id);
        if (k != kids.end())
            for (int ci : k->second) visit(ci, out);
    };
    auto roots = kids.find(0);
    if (roots != kids.end())
        for (int i : roots->second) visit(i, nullptr);
    for (const AreaLight& l : m_scene.lights)
        if (!l.parent) {
            HierXf h;
            h.p = l.position;
            h.t = l.target;
            m_hierCache[l.id] = h;
            alive.insert(l.id);
        }
    for (auto it = m_hierCache.begin(); it != m_hierCache.end();) it = alive.count(it->first) ? std::next(it) : m_hierCache.erase(it);
    if (moved) markDirty();
}

NodeRef App::findNode(uint32_t id) const {
    if (!id) return {};
    for (size_t i = 0; i < m_scene.objects.size(); ++i) if (m_scene.objects[i].id == id) return {NODE_OBJECT, (int)i};
    for (size_t i = 0; i < m_scene.lights.size(); ++i) if (m_scene.lights[i].id == id) return {NODE_LIGHT, (int)i};
    for (size_t i = 0; i < m_scene.collections.size(); ++i) if (m_scene.collections[i].id == id) return {NODE_COLLECTION, (int)i};
    if (m_scene.sun.id == id) return {NODE_SUN, 0};
    return {};
}

uint32_t App::nodeId(NodeRef n) const {
    switch (n.kind) {
    case NODE_OBJECT: return m_scene.objects[n.index].id;
    case NODE_LIGHT: return m_scene.lights[n.index].id;
    case NODE_SUN: return m_scene.sun.id;
    case NODE_COLLECTION: return m_scene.collections[n.index].id;
    }
    return 0;
}
uint32_t App::nodeParent(NodeRef n) const {
    if (n.kind == NODE_OBJECT) return m_scene.objects[n.index].parent;
    if (n.kind == NODE_LIGHT) return m_scene.lights[n.index].parent;
    return 0;
}
uint32_t App::nodeCollection(NodeRef n) const {
    if (nodeParent(n)) return 0;
    if (n.kind == NODE_OBJECT) return m_scene.objects[n.index].collection;
    if (n.kind == NODE_LIGHT) return m_scene.lights[n.index].collection;
    if (n.kind == NODE_SUN) return m_scene.sun.collection;
    return 0;
}
static int* nodeOrder(Scene& s, NodeRef n) {
    switch (n.kind) {
    case NODE_OBJECT: return &s.objects[n.index].order;
    case NODE_LIGHT: return &s.lights[n.index].order;
    case NODE_SUN: return &s.sun.order;
    case NODE_COLLECTION: return &s.collections[n.index].order;
    }
    return nullptr;
}
std::string App::nodeName(NodeRef n) const {
    switch (n.kind) {
    case NODE_OBJECT: return m_scene.objects[n.index].name;
    case NODE_LIGHT: return m_scene.lights[n.index].name;
    case NODE_SUN: return "Directional Light";
    case NODE_COLLECTION: return m_scene.collections[n.index].name;
    }
    return "";
}
void App::setNodeName(NodeRef n, const std::string& name) {
    if (name.empty()) return;
    if (n.kind == NODE_OBJECT) m_scene.objects[n.index].name = name;
    else if (n.kind == NODE_LIGHT) m_scene.lights[n.index].name = name;
    else if (n.kind == NODE_COLLECTION) m_scene.collections[n.index].name = name;
}

// Siblings in the order of the list: the children of an object / group, the members of a collection, or the root
// items outside any collection (with the collections when withCollections).
std::vector<NodeRef> App::nodeChildren(uint32_t parent, uint32_t collection, bool withCollections) const {
    std::vector<std::pair<int, NodeRef>> v;
    for (size_t i = 0; i < m_scene.objects.size(); ++i) {
        const Object& o = m_scene.objects[i];
        if (!listed(o) || o.parent != parent || (!parent && o.collection != collection)) continue;
        v.push_back({o.order, {NODE_OBJECT, (int)i}});
    }
    for (size_t i = 0; i < m_scene.lights.size(); ++i) {
        const AreaLight& l = m_scene.lights[i];
        if (l.parent != parent || (!parent && l.collection != collection)) continue;
        v.push_back({l.order, {NODE_LIGHT, (int)i}});
    }
    if (!parent && m_scene.sun.collection == collection) v.push_back({m_scene.sun.order, {NODE_SUN, 0}});
    if (!parent && !collection && withCollections)
        for (size_t i = 0; i < m_scene.collections.size(); ++i) v.push_back({m_scene.collections[i].order, {NODE_COLLECTION, (int)i}});
    std::stable_sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<NodeRef> r;
    for (auto& e : v) r.push_back(e.second);
    return r;
}

void App::nodeDescendants(uint32_t id, std::vector<NodeRef>& out) const {
    NodeRef n = findNode(id);
    if (!n.valid()) return;
    std::vector<NodeRef> c;
    if (n.kind == NODE_COLLECTION) c = nodeChildren(0, id, false);
    else if (n.kind == NODE_OBJECT) c = nodeChildren(id, 0, false);
    for (NodeRef k : c) {
        out.push_back(k);
        nodeDescendants(nodeId(k), out);
    }
}

bool App::isAncestor(uint32_t ancestor, uint32_t id) const {
    NodeRef n = findNode(id);
    for (size_t step = 0; n.valid() && step <= m_scene.objects.size(); ++step) {
        uint32_t p = nodeParent(n);
        if (!p) return nodeCollection(n) == ancestor && ancestor != 0;
        if (p == ancestor) return true;
        n = findNode(p);
    }
    return false;
}

bool App::nodeVisible(NodeRef n) const {
    if (n.kind == NODE_LIGHT) return m_scene.lights[n.index].enabled;
    if (n.kind == NODE_SUN) return m_scene.sun.enabled;
    if (n.kind == NODE_OBJECT && !m_scene.objects[n.index].group) return m_scene.objects[n.index].visible;
    std::vector<NodeRef> d;
    nodeDescendants(nodeId(n), d);
    if (d.empty()) return n.kind == NODE_OBJECT ? m_scene.objects[n.index].visible : true;
    for (NodeRef k : d)
        if (k.kind != NODE_OBJECT || !m_scene.objects[k.index].group)
            if (nodeVisible(k)) return true;
    return false;
}

bool App::nodeLocked(NodeRef n) const {
    if (n.kind == NODE_LIGHT) return !m_scene.lights[n.index].selectable;
    if (n.kind == NODE_SUN) return !m_scene.sun.selectable;
    if (n.kind == NODE_OBJECT && !m_scene.objects[n.index].group) return !m_scene.objects[n.index].selectable;
    std::vector<NodeRef> d;
    nodeDescendants(nodeId(n), d);
    if (n.kind == NODE_OBJECT && !m_scene.objects[n.index].selectable) return true;
    if (d.empty()) return false;
    for (NodeRef k : d)
        if (!nodeLocked(k)) return false;
    return true;
}

void App::setNodeVisible(NodeRef n, bool on) {
    std::vector<NodeRef> all{n};
    nodeDescendants(nodeId(n), all);
    for (NodeRef k : all) {
        if (k.kind == NODE_OBJECT) m_scene.objects[k.index].visible = on;
        else if (k.kind == NODE_LIGHT) m_scene.lights[k.index].enabled = on;
        else if (k.kind == NODE_SUN) m_scene.sun.enabled = on;
    }
    markDirty();
}

void App::setNodeLocked(NodeRef n, bool on) {
    std::vector<NodeRef> all{n};
    nodeDescendants(nodeId(n), all);
    for (NodeRef k : all) {
        if (k.kind == NODE_OBJECT) m_scene.objects[k.index].selectable = !on;
        else if (k.kind == NODE_LIGHT) m_scene.lights[k.index].selectable = !on;
        else if (k.kind == NODE_SUN) m_scene.sun.selectable = !on;
    }
    if (on) {   // a locked node leaves the selection
        std::vector<NodeRef> keep;
        NodeRef primary;
        for (uint32_t id : selectedNodeIds()) {
            NodeRef k = findNode(id);
            if (k.kind == NODE_COLLECTION || !nodeLocked(k)) { keep.push_back(k); if (!primary.valid()) primary = k; }
        }
        selectNodes(keep, primary);
    }
    markDirty();
}

// Rows of the list: collections and root items, then the children of the open nodes. While searching, a node is
// shown when it or one of its descendants matches, with its parents open.
void App::outlinerRows(std::vector<OutlinerRow>& rows, const char* filter) {
    auto lower = [](std::string t) { for (auto& c : t) c = (char)std::tolower((unsigned char)c); return t; };
    const std::string q = filter ? lower(filter) : std::string();
    std::function<bool(NodeRef, int)> add = [&](NodeRef n, int depth) -> bool {
        uint32_t id = nodeId(n);
        std::vector<NodeRef> kids;
        if (n.kind == NODE_COLLECTION) kids = nodeChildren(0, id, false);
        else if (n.kind == NODE_OBJECT) kids = nodeChildren(id, 0, false);
        bool open = n.kind == NODE_COLLECTION ? !m_scene.collections[n.index].collapsed : (n.kind == NODE_OBJECT && !m_scene.objects[n.index].collapsed);
        if (q.empty()) {
            rows.push_back({n, id, depth, !kids.empty(), open});
            if (open)
                for (NodeRef k : kids) add(k, depth + 1);
            return true;
        }
        size_t at = rows.size();
        rows.push_back({n, id, depth, !kids.empty(), true});
        bool any = false;
        for (NodeRef k : kids) any |= add(k, depth + 1);
        bool self = lower(nodeName(n)).find(q) != std::string::npos || (n.kind == NODE_OBJECT && lower(m_scene.objects[n.index].part).find(q) != std::string::npos);
        if (!self && !any) { rows.resize(at); return false; }
        return true;
    };
    for (NodeRef n : nodeChildren(0, 0, true)) add(n, 0);
}

// --------------------------------------------------------------------------- editing

// Moves the nodes (drag & drop of the list). World transforms are kept: only the links and the order change.
void App::moveNodes(std::vector<uint32_t> ids, int mode, uint32_t target) {
    NodeRef t = findNode(target);
    if (mode != 3 && !t.valid()) return;
    uint32_t parent = 0, col = 0;
    bool reorder = false;
    if (mode == 2) {
        if (t.kind == NODE_COLLECTION) col = target;
        else if (t.kind == NODE_OBJECT) parent = target;
        else { mode = 1; }   // a light or the sun has no children: drop after it
    }
    if (mode == 0 || mode == 1) {
        parent = nodeParent(t);
        col = nodeCollection(t);
        reorder = true;
    }
    // the moved set: no node dropped into itself or its descendants, no child whose parent moves too
    std::vector<uint32_t> moving;
    for (uint32_t id : ids) {
        NodeRef n = findNode(id);
        if (!n.valid() || id == target || std::find(moving.begin(), moving.end(), id) != moving.end()) continue;
        if (target && isAncestor(id, target) && n.kind != NODE_COLLECTION) continue;
        if (n.kind == NODE_COLLECTION && (parent || col)) continue;   // collections stay at the root
        if (n.kind == NODE_SUN && parent) continue;                    // the sun has no parent
        bool parentMoves = false;
        for (uint32_t o : ids) if (o != id && isAncestor(o, id) && findNode(o).kind != NODE_COLLECTION) parentMoves = true;
        if (parentMoves) continue;
        moving.push_back(id);
    }
    if (moving.empty()) return;
    pushUndo();
    std::vector<NodeRef> sib = nodeChildren(parent, col, !parent && !col);
    sib.erase(std::remove_if(sib.begin(), sib.end(), [&](NodeRef n) { return std::find(moving.begin(), moving.end(), nodeId(n)) != moving.end(); }), sib.end());
    size_t at = sib.size();
    if (reorder)
        for (size_t i = 0; i < sib.size(); ++i)
            if (nodeId(sib[i]) == target) { at = mode == 0 ? i : i + 1; break; }
    std::vector<NodeRef> ins;
    for (uint32_t id : moving) {
        NodeRef n = findNode(id);
        ins.push_back(n);
        if (n.kind == NODE_OBJECT) { m_scene.objects[n.index].parent = parent; m_scene.objects[n.index].collection = col; }
        else if (n.kind == NODE_LIGHT) { m_scene.lights[n.index].parent = parent; m_scene.lights[n.index].collection = col; }
        else if (n.kind == NODE_SUN) m_scene.sun.collection = col;
    }
    sib.insert(sib.begin() + at, ins.begin(), ins.end());
    for (size_t i = 0; i < sib.size(); ++i) *nodeOrder(m_scene, sib[i]) = (int)i;
    if (parent) {   // the new parent opens to show its child
        NodeRef p = findNode(parent);
        if (p.kind == NODE_OBJECT) m_scene.objects[p.index].collapsed = false;
    }
    if (col) {
        NodeRef c = findNode(col);
        if (c.valid()) m_scene.collections[c.index].collapsed = false;
    }
    markDirty();
}

std::string App::uniqueNodeName(const std::string& base) const {
    auto used = [&](const std::string& n) {
        for (const Object& o : m_scene.objects) if (o.name == n) return true;
        for (const Collection& c : m_scene.collections) if (c.name == n) return true;
        return false;
    };
    if (!used(base)) return base;
    for (int i = 1;; ++i) {
        std::string n = base + std::to_string(i);
        if (!used(n)) return n;
    }
}

// Ctrl+G: a group node at the base of the selection, put where the first selected node was, holding the selection.
void App::groupSelection() {
    std::vector<uint32_t> ids = selectedNodeIds();
    ids.erase(std::remove_if(ids.begin(), ids.end(), [&](uint32_t id) { int k = findNode(id).kind; return k != NODE_OBJECT && k != NODE_LIGHT; }), ids.end());
    if (ids.empty()) { status(L"Select objects to group (Ctrl+G)", 2); return; }
    float3 mn(1e30f), mx(-1e30f);
    for (uint32_t id : ids) {
        NodeRef n = findNode(id);
        std::vector<NodeRef> all{n};
        nodeDescendants(id, all);
        for (NodeRef k : all) {
            float3 a, b;
            if (k.kind == NODE_LIGHT) a = b = m_scene.lights[k.index].position;
            else if (k.kind == NODE_OBJECT) {
                const Object& o = m_scene.objects[k.index];
                aabbOf(o, o.position, objRot(o), a, b);
            } else continue;
            mn = minv(mn, a);
            mx = maxv(mx, b);
        }
    }
    NodeRef first = findNode(ids[0]);
    uint32_t parent = nodeParent(first), col = nodeCollection(first);
    int order = *nodeOrder(m_scene, first);
    pushUndo();
    Object g;
    g.name = uniqueNodeName("Group");
    g.part = "GROUP";
    g.group = true;
    g.mesh = -1;
    g.material = 0;
    g.position = mn.x <= mx.x ? float3((mn.x + mx.x) * 0.5f, std::max(0.0f, mn.y), (mn.z + mx.z) * 0.5f) : float3(0, 0, 0);
    if (m_magnet) { g.position.x = std::round(g.position.x); g.position.z = std::round(g.position.z); }
    g.useRot = true;
    g.rot = qIdentity();
    g.id = newNodeId();
    g.parent = parent;
    g.collection = col;
    g.order = order;
    // the group takes the place of the first node: the following siblings move one rank down
    for (NodeRef n : nodeChildren(parent, col, !parent && !col)) {
        int* o = nodeOrder(m_scene, n);
        if (*o >= order) ++*o;
    }
    int gi = m_scene.addObject(g);
    m_hierCache.erase(g.id);
    size_t undoDepth = m_undo.size();
    moveNodes(ids, 2, g.id);
    if (m_undo.size() > undoDepth) m_undo.pop_back();   // one undo step for the whole grouping
    m_selected = gi;
    m_multi.clear();
    m_outSel.clear();
    m_outAnchor = g.id;
    status(L"Grouped (" + std::to_wstring(ids.size()) + L")");
    markDirty();
}

// Ungroup: the children take the place of the group, the group node is removed.
void App::ungroup(uint32_t groupId) {
    NodeRef g = findNode(groupId);
    if (g.kind != NODE_OBJECT || !m_scene.objects[g.index].group) return;
    std::vector<uint32_t> kids;
    for (NodeRef k : nodeChildren(groupId, 0, false)) kids.push_back(nodeId(k));
    pushUndo();
    size_t undoDepth = m_undo.size();
    if (!kids.empty()) moveNodes(kids, 0, groupId);
    if (m_undo.size() > undoDepth) m_undo.pop_back();
    g = findNode(groupId);
    m_scene.objects.erase(m_scene.objects.begin() + g.index);
    m_selected = -1;
    m_multi.clear();
    m_outSel.clear();
    status(L"Ungrouped");
    markDirty();
}

uint32_t App::addCollection(const std::string& name, int color) {
    Collection c;
    c.id = newNodeId();
    c.name = uniqueNodeName(name);
    c.color = color;
    int last = -1;
    for (NodeRef n : nodeChildren(0, 0, true)) last = std::max(last, *nodeOrder(m_scene, n));
    c.order = last + 1;
    m_scene.collections.push_back(c);
    return c.id;
}

uint32_t App::villageCollection() {
    for (const Collection& c : m_scene.collections)
        if (c.name == "Village") return c.id;
    return addCollection("Village", 2);
}

// --------------------------------------------------------------------------- selection

bool App::nodeSelected(NodeRef n) const {
    if (n.kind == NODE_OBJECT) return n.index == m_selected || std::find(m_multi.begin(), m_multi.end(), n.index) != m_multi.end();
    return n.valid() && std::find(m_outSel.begin(), m_outSel.end(), nodeId(n)) != m_outSel.end();
}

std::vector<uint32_t> App::selectedNodeIds() const {
    std::vector<uint32_t> v;
    for (int i : selectionAll())
        if (i >= 0 && i < (int)m_scene.objects.size()) v.push_back(m_scene.objects[i].id);
    for (uint32_t id : m_outSel) v.push_back(id);
    return v;
}

void App::selectNodes(const std::vector<NodeRef>& nodes, NodeRef primary) {
    m_selected = -1;
    m_multi.clear();
    m_outSel.clear();
    if (primary.kind == NODE_OBJECT) m_selected = primary.index;
    for (NodeRef n : nodes) {
        if (n.kind == NODE_OBJECT) {
            if (m_selected < 0) m_selected = n.index;
            else if (n.index != m_selected && std::find(m_multi.begin(), m_multi.end(), n.index) == m_multi.end()) m_multi.push_back(n.index);
        } else if (n.valid()) {
            uint32_t id = nodeId(n);
            if (std::find(m_outSel.begin(), m_outSel.end(), id) == m_outSel.end()) m_outSel.push_back(id);
        }
    }
    // a light or the sun alone: the Inspector shows the Light tab
    if (m_selected < 0 && primary.valid() && (primary.kind == NODE_LIGHT || primary.kind == NODE_SUN)) m_atl.inspectorTab = 1;
    else if (m_selected >= 0) m_atl.inspectorTab = m_atl.inspectorTab == 1 ? 0 : m_atl.inspectorTab;
    markDirty();
}

void App::toggleNodeSelected(NodeRef n) {
    if (n.kind == NODE_OBJECT) {
        if (n.index == m_selected) {
            if (!m_multi.empty()) { m_selected = m_multi.front(); m_multi.erase(m_multi.begin()); }
            else m_selected = -1;
        } else {
            auto it = std::find(m_multi.begin(), m_multi.end(), n.index);
            if (it != m_multi.end()) m_multi.erase(it);
            else if (m_selected < 0) m_selected = n.index;
            else m_multi.push_back(n.index);
        }
    } else if (n.valid()) {
        uint32_t id = nodeId(n);
        auto it = std::find(m_outSel.begin(), m_outSel.end(), id);
        if (it != m_outSel.end()) m_outSel.erase(it);
        else m_outSel.push_back(id);
        if (m_selected < 0 && (n.kind == NODE_LIGHT || n.kind == NODE_SUN)) m_atl.inspectorTab = 1;
    }
    markDirty();
}

void App::startRename(uint32_t id) {
    NodeRef n = findNode(id);
    if (!n.valid() || n.kind == NODE_SUN) return;
    m_renameId = id;
    m_renameFocus = true;
    m_renameMulti.clear();
    std::string name = nodeName(n);
    // several nodes selected (this one among them): one field, every node gets the text + _001, _002...
    if (nodeSelected(n)) {
        for (uint32_t s : selectedNodeIds())
            if (findNode(s).kind != NODE_SUN) m_renameMulti.push_back(s);
        if (m_renameMulti.size() < 2) m_renameMulti.clear();
        else {   // the field starts with the base name, without a _NNN suffix
            size_t u = name.find_last_of('_');
            if (u != std::string::npos && u + 1 < name.size() && name.find_first_not_of("0123456789", u + 1) == std::string::npos) name = name.substr(0, u);
        }
    }
    snprintf(m_renameBuf, sizeof(m_renameBuf), "%s", name.c_str());
}

void App::selectionOutline(std::vector<int>& outlined) const {
    std::vector<uint32_t> ids = selectedNodeIds();
    for (uint32_t id : ids) {
        NodeRef n = findNode(id);
        if (n.kind != NODE_OBJECT && n.kind != NODE_COLLECTION) continue;
        std::vector<NodeRef> d;
        nodeDescendants(id, d);
        for (NodeRef k : d)
            if (k.kind == NODE_OBJECT && std::find(outlined.begin(), outlined.end(), k.index) == outlined.end() && k.index != m_selected)
                outlined.push_back(k.index);
    }
}

} // namespace app
