#include "Runtime.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace game {

static const int kBudget = 20000;    // node executions per event (infinite loop guard)
static const int kMaxDepth = 256;

static std::mt19937& rng() {
    static std::mt19937 r(12345);
    return r;
}

static Value convert(const Value& v, PinType t) {
    switch (t) {
    case PinType::Float: return Value::Float(v.asFloat());
    case PinType::Bool: return Value::Bool(v.asBool());
    case PinType::Vec3: return Value::Vec3(v.asVec3());
    case PinType::String: return Value::Str(v.asString());
    case PinType::Object: return v.type == PinType::Object ? v : Value::Obj(-1);
    default: return v;
    }
}

void Runtime::start(Graph* g, World* w) {
    m_graph = g;
    m_world = w;
    m_time = 0;
    m_vars.clear();
    m_cache.clear();
    m_lastRun.clear();
    trace.clear();
    linkValues.clear();
    execFired.clear();
    m_timers.clear();
    m_movers.clear();
    m_doneOnce.clear();
    m_objVars.clear();
    m_objLinks.clear();
    m_objTimers.clear();
    m_unitMoves.clear();
    m_production.clear();
    m_placeNode = -1;
    hudCounters.clear();
    objectives.clear();
    messageTitle.clear();
    messageText.clear();
    messageShown = false;
    lastError.clear();
    for (Node& n : g->nodes) if (!n.def) n.def = findNodeDef(n.type);
    log.push_back("--- Play ---");
    fire(Op::OnStart);
}

void Runtime::stop() {
    if (m_graph) log.push_back("--- Stop ---");
    m_graph = nullptr;
    m_world = nullptr;
    m_timers.clear();
    m_movers.clear();
}

float Runtime::lastRun(int nodeId) const {
    auto it = m_lastRun.find(nodeId);
    return it == m_lastRun.end() ? -1e9f : it->second;
}

void Runtime::error(const std::string& e) {
    lastError = e;
    log.push_back("Error: " + e);
    while (log.size() > 200) log.pop_front();
}

void Runtime::tick(float dt) {
    if (!m_graph) return;
    m_time += dt;
    // movers
    std::vector<Mover> arrived;
    for (size_t i = 0; i < m_movers.size();) {
        Mover& m = m_movers[i];
        if (!m_world->objectAlive(m.obj)) { m_movers.erase(m_movers.begin() + i); continue; }
        float3 p = m_world->getPosition(m.obj);
        float3 d = m.target - p;
        float len = length(d);
        float step = m.speed * dt;
        if (len <= step || len < 1e-4f) {
            m_world->setPosition(m.obj, m.target);
            arrived.push_back(m);
            m_movers.erase(m_movers.begin() + i);
            continue;
        }
        m_world->setPosition(m.obj, p + d * (step / len));
        ++i;
    }
    for (const Mover& m : arrived) {
        if (!m_graph) return;
        m_budget = kBudget;
        execFrom(m.node, m.pin);
    }
    // timers
    std::vector<Timer> due;
    for (size_t i = 0; i < m_timers.size();) {
        m_timers[i].left -= dt;
        if (m_timers[i].left <= 0) { due.push_back(m_timers[i]); m_timers.erase(m_timers.begin() + i); }
        else ++i;
    }
    for (const Timer& t : due) {
        if (!m_graph) return;
        m_budget = kBudget;
        execFrom(t.node, t.pin);
    }
    if (!m_graph) return;
    tickUnits(dt);   // RTS units: paths, object timers, production
    if (!m_graph) return;
    for (Node& n : m_graph->nodes)
        if (n.def->op == Op::OnTick) m_cache[n.id] = {Value(), Value::Float(dt)};
    fire(Op::OnTick);
}

void Runtime::fireClickObject(int obj, float3 point) {
    if (!m_graph) return;
    for (Node& n : m_graph->nodes)
        if (n.def->op == Op::OnClickObject) m_cache[n.id] = {Value(), Value::Obj(obj), Value::Vec3(point)};
    fire(Op::OnClickObject);
}

void Runtime::fireHouseClick(int house) {
    if (!m_graph) return;
    for (Node& n : m_graph->nodes)
        if (n.def->op == Op::OnHouseClicked) m_cache[n.id] = {Value(), Value::Float((float)house)};
    fire(Op::OnHouseClicked);
}

void Runtime::fireClipFinished(int obj, const std::string& clip) {
    if (!m_graph) return;
    for (Node& n : m_graph->nodes)
        if (n.def->op == Op::OnClipFinished) m_cache[n.id] = {Value(), Value::Obj(obj), Value::Str(clip)};
    fire(Op::OnClipFinished);
}

// --------------------------------------------------------------------------- RTS units

void Runtime::fireRightClickGround(float3 point) {
    if (!m_graph) return;
    for (Node& n : m_graph->nodes)
        if (n.def->op == Op::OnRightClickGround) m_cache[n.id] = {Value(), Value::Vec3(point)};
    fire(Op::OnRightClickGround);
}

void Runtime::fireRightClickObject(int obj, float3 point) {
    if (!m_graph) return;
    for (Node& n : m_graph->nodes)
        if (n.def->op == Op::OnRightClickObject) m_cache[n.id] = {Value(), Value::Obj(obj), Value::Vec3(point)};
    fire(Op::OnRightClickObject);
}

void Runtime::fireSelectionChanged() {
    if (!m_graph) return;
    fire(Op::OnSelectionChanged);
}

void Runtime::firePlacement(int building) {
    if (!m_graph || m_placeNode < 0) return;
    const int node = m_placeNode;
    m_placeNode = -1;
    Node* n = m_graph->node(node);
    if (!n) return;
    m_budget = kBudget;
    if (building >= 0) {
        if (!m_placeCostVar.empty()) m_vars[m_placeCostVar] = Value::Float(m_vars[m_placeCostVar].asFloat() - m_placeCost);
        setOut(*n, 2, Value::Obj(building));
        execFrom(node, 1);
    } else {
        execFrom(node, 3);
    }
}

float Runtime::objectVar(int obj, const std::string& name, float def) const {
    auto it = m_objVars.find(obj);
    if (it == m_objVars.end()) return def;
    auto k = it->second.find(name);
    return k == it->second.end() ? def : k->second;
}

bool Runtime::hasObjectVar(int obj, const std::string& name) const {
    auto it = m_objVars.find(obj);
    return it != m_objVars.end() && it->second.count(name) > 0;
}

bool Runtime::unitMoving(int obj) const {
    for (const UnitMove& m : m_unitMoves)
        if (m.obj == obj) return true;
    return false;
}

float Runtime::production(int building, int* queued, std::string* prefab) const {
    int q = 0;
    float progress = -1.0f;
    for (const Production& p : m_production) {
        if (p.building != building) continue;
        if (q == 0) {
            progress = 1.0f - p.left / std::max(p.total, 1e-3f);
            if (prefab) *prefab = p.prefab;
        }
        ++q;
    }
    if (queued) *queued = q;
    return progress;
}

// Units walking their paths (turned towards their motion, kept apart from the other units), object timers and
// production queues: their continuations run after the updates (like the movers of « Move To »).
void Runtime::tickUnits(float dt) {
    // the units (for the separation): every alive instance of category Unit
    std::vector<int> units;
    for (int i = 0; i < m_world->objectCount(); ++i)
        if (m_world->objectAlive(i) && m_world->objectCategory(i) == "Unit") units.push_back(i);
    std::vector<std::pair<int, int>> arrived;   // (node, unit)
    for (size_t i = 0; i < m_unitMoves.size();) {
        UnitMove& m = m_unitMoves[i];
        if (!m_world->objectAlive(m.obj)) { m_unitMoves.erase(m_unitMoves.begin() + i); continue; }
        float3 p = m_world->getPosition(m.obj);
        float3 t = m.path[m.next];
        float3 d(t.x - p.x, 0, t.z - p.z);
        float len = length(d);
        const float step = m.speed * dt;
        const bool last = m.next + 1 >= m.path.size();
        // a group sent to one point: near it, a unit stops when another one already stands on it
        bool taken = false;
        if (last && len < 1.6f)
            for (int u : units)
                if (u != m.obj && !unitMoving(u)) {
                    const float3 q = m_world->getPosition(u);
                    if (length(float3(q.x - t.x, 0, q.z - t.z)) < 0.95f + 0.9f * (len > 0.95f ? 1.0f : 0.0f) && length(float3(q.x - p.x, 0, q.z - p.z)) < 1.0f) taken = true;
                }
        // arrived at a waypoint (the last one: within a little of it, another unit may stand there)
        if (len <= step || len < (last ? 0.05f : 0.25f) || taken || (last && m.stuck > 0.6f && len < 1.6f)) {
            if (!last) { ++m.next; ++i; continue; }
            if (len <= step) m_world->setPosition(m.obj, float3(t.x, p.y, t.z));
            m_world->playClip(m.obj, "Idle", true, 1.0f);
            arrived.push_back({m.node, m.obj});
            m_unitMoves.erase(m_unitMoves.begin() + i);
            continue;
        }
        float3 dir = d * (1.0f / len);
        float3 np = p + dir * step;
        // separation: pushed out of the other units' circles (radius 0.45 each), a little sideways
        for (int u : units) {
            if (u == m.obj) continue;
            const float3 q = m_world->getPosition(u);
            float3 a(np.x - q.x, 0, np.z - q.z);
            const float dd = length(a);
            if (dd < 0.9f && dd > 1e-4f) np = np + a * ((0.9f - dd) / dd * 0.5f);
        }
        const float moved = length(float3(np.x - p.x, 0, np.z - p.z));
        m.stuck = moved < step * 0.3f ? m.stuck + dt : 0.0f;
        m_world->setPosition(m.obj, float3(np.x, p.y, np.z));
        m_world->setYaw(m.obj, std::atan2(dir.x, dir.z) * 57.2957795f);
        ++i;
    }
    for (auto& [node, unit] : arrived) {
        if (!m_graph) return;
        Node* n = m_graph->node(node);
        if (!n) continue;
        m_budget = kBudget;
        setOut(*n, 2, Value::Obj(unit));
        execFrom(node, 1);
    }
    // object timers
    std::vector<ObjTimer> due;
    for (size_t i = 0; i < m_objTimers.size();) {
        m_objTimers[i].left -= dt;
        if (m_objTimers[i].left <= 0) { due.push_back(m_objTimers[i]); m_objTimers.erase(m_objTimers.begin() + i); }
        else ++i;
    }
    for (const ObjTimer& t : due) {
        if (!m_graph) return;
        for (Node& n : m_graph->nodes)
            if (n.def->op == Op::OnTimer) m_cache[n.id] = {Value(), Value::Obj(t.obj), Value::Str(t.name)};
        fire(Op::OnTimer);
    }
    // production: the first unit queued at each building counts down
    std::vector<int> busy;
    std::vector<Production> done;
    for (size_t i = 0; i < m_production.size();) {
        Production& p = m_production[i];
        if (!m_world->objectAlive(p.building)) { m_production.erase(m_production.begin() + i); continue; }
        if (std::find(busy.begin(), busy.end(), p.building) != busy.end()) { ++i; continue; }
        busy.push_back(p.building);
        p.left -= dt;
        if (p.left <= 0) { done.push_back(p); m_production.erase(m_production.begin() + i); continue; }
        ++i;
    }
    for (const Production& p : done) {
        if (!m_graph) return;
        const float3 door = m_world->doorOf(p.building);
        const float3 c = m_world->getPosition(p.building);
        float3 out(door.x - c.x, 0, door.z - c.z);
        const float ol = length(out);
        const float yaw = ol > 1e-3f ? std::atan2(out.x, out.z) * 57.2957795f : 0.0f;
        const int u = m_world->spawnPrefab(p.prefab, door, yaw);
        if (u < 0) { error("prefab not found: " + p.prefab); continue; }
        // it walks out of the door
        const float3 to = ol > 1e-3f ? door + out * (1.6f / ol) : door + float3(0, 0, 1.6f);
        std::vector<float3> path = m_world->findPath(door, to);
        if (path.empty()) path.push_back(to);
        m_unitMoves.push_back({u, path, 0, 2.5f, -1, 0.0f, to});
        m_world->playClip(u, "Walk", true, 1.0f);
        Node* n = m_graph->node(p.node);
        if (!n) continue;
        m_budget = kBudget;
        setOut(*n, 2, Value::Obj(u));
        execFrom(p.node, 1);
    }
}

void Runtime::fireHudButton(const std::string& id) {
    if (!m_graph) return;
    for (Node& n : m_graph->nodes)
        if (n.def->op == Op::OnHudButton) m_cache[n.id] = {Value(), Value::Str(id)};
    fire(Op::OnHudButton);
}

void Runtime::fireClickGround(float3 point) {
    if (!m_graph) return;
    for (Node& n : m_graph->nodes)
        if (n.def->op == Op::OnClickGround) m_cache[n.id] = {Value(), Value::Vec3(point)};
    fire(Op::OnClickGround);
}

static std::string upper(std::string s) {
    for (char& c : s) if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    return s;
}

void Runtime::fireKey(const std::string& key) {
    if (!m_graph) return;
    fire(Op::OnKey, [&](const Node& n) { return upper(n.inputs[0].s) == key; });
}

void Runtime::fire(Op op, const std::function<bool(const Node&)>& filter) {
    if (!m_graph) return;
    bool top = m_depth == 0;
    // collect first: the graph may be edited by the API between frames, never during a call
    std::vector<int> ids;
    for (Node& n : m_graph->nodes)
        if (n.def->op == op && (!filter || filter(n))) ids.push_back(n.id);
    for (int id : ids) {
        if (top) m_budget = kBudget;
        m_lastRun[id] = m_time;
        recordRun(id);
        execFrom(id, 0);
        if (!m_graph) return;
    }
}

void Runtime::execFrom(int nodeId, int outPin) {
    if (!m_graph) return;
    for (const Link& l : m_graph->links) {
        if (l.fromNode != nodeId || l.fromPin != outPin) continue;
        Node* n = m_graph->node(l.toNode);
        if (!n) return;
        ++execFired[{nodeId, outPin}];
        if (--m_budget < 0) { if (m_budget == -1) error("infinite loop stopped (too many nodes executed)"); return; }
        if (m_depth >= kMaxDepth) { error("execution chain too deep"); return; }
        ++m_depth;
        exec(*n);
        --m_depth;
        return;
    }
}

void Runtime::setOut(const Node& n, int pin, const Value& v) {
    auto& c = m_cache[n.id];
    if ((int)c.size() < (int)n.def->outs.size()) c.resize(n.def->outs.size());
    c[pin] = v;
}

Value Runtime::input(const Node& n, int pin) {
    PinType t = n.def->ins[pin].type;
    const Link* l = m_graph->inputLink(n.id, pin);
    if (!l) return n.inputs[pin];
    const Node* src = m_graph->node(l->fromNode);
    if (!src) return n.inputs[pin];
    Value v = convert(output(*src, l->fromPin), t);
    linkValues[{n.id, pin}] = v;
    return v;
}

Value Runtime::output(const Node& n, int pin) {
    PinType t = n.def->outs[pin].type;
    if (!n.def->pure) {
        auto it = m_cache.find(n.id);
        if (it != m_cache.end() && pin < (int)it->second.size() && it->second[pin].type == t) return it->second[pin];
        return Value::None(t);
    }
    if (m_depth >= kMaxDepth) { error("cycle in the data"); return Value::None(t); }
    ++m_depth;
    Value r = Value::None(t);
    auto F = [&](int i) { return input(n, i).asFloat(); };
    auto Bv = [&](int i) { return input(n, i).asBool(); };
    auto V = [&](int i) { return input(n, i).asVec3(); };
    auto S = [&](int i) { return input(n, i).asString(); };
    auto Ob = [&](int i) { int o = input(n, i).obj; return m_world->objectAlive(o) ? o : -1; };
    switch (n.def->op) {
    case Op::ConstFloat: case Op::ConstBool: case Op::ConstVec3: case Op::ConstString: r = input(n, 0); break;
    case Op::GetVar: { auto it = m_vars.find(S(0)); r = it == m_vars.end() ? Value::Float(0) : it->second; break; }
    case Op::Selected: r = Value::Obj(m_world->selected()); break;
    case Op::GetSelected: { std::vector<int> s = m_world->selection(); r = Value::Obj(s.empty() ? -1 : s[0]); break; }
    case Op::CountSelected: r = Value::Float((float)m_world->selection().size()); break;
    case Op::IsSelected: {
        const int o = Ob(0);
        const std::vector<int> s = m_world->selection();
        r = Value::Bool(o >= 0 && std::find(s.begin(), s.end(), o) != s.end());
        break;
    }
    case Op::GetObjVar: r = Value::Float(objectVar(Ob(0), S(1))); break;
    case Op::GetObjLink: {
        const int o = Ob(0);
        int t = -1;
        auto it = m_objLinks.find(o);
        if (it != m_objLinks.end()) {
            auto k = it->second.find(S(1));
            if (k != it->second.end() && m_world->objectAlive(k->second)) t = k->second;
        }
        r = Value::Obj(t);
        break;
    }
    case Op::ObjectType: { int o = Ob(0); r = Value::Str(o >= 0 ? m_world->objectPrefab(o) : ""); break; }
    case Op::ObjectCategory: { int o = Ob(0); r = Value::Str(o >= 0 ? m_world->objectCategory(o) : ""); break; }
    case Op::FindNearest: {
        const float3 from = V(0);
        const std::string type = S(1), var = S(2);
        const float minV = F(3);
        int best = -1;
        float bestD = 1e30f;
        for (int i = 0; i < m_world->objectCount(); ++i) {
            if (!m_world->objectAlive(i)) continue;
            if (m_world->objectPrefab(i) != type && m_world->objectCategory(i) != type) continue;
            if (!var.empty() && objectVar(i, var) < minV) continue;
            const float3 p = m_world->getPosition(i);
            const float dd = (p.x - from.x) * (p.x - from.x) + (p.z - from.z) * (p.z - from.z);
            if (dd < bestD) { bestD = dd; best = i; }
        }
        r = Value::Obj(best);
        break;
    }
    case Op::FindObject: {
        std::string name = S(0);
        int found = -1;
        for (int i = 0; i < m_world->objectCount() && found < 0; ++i)
            if (m_world->objectAlive(i) && m_world->objectName(i) == name) found = i;
        r = Value::Obj(found);
        break;
    }
    case Op::GetPosition: { int o = Ob(0); r = Value::Vec3(o >= 0 ? m_world->getPosition(o) : float3(0, 0, 0)); break; }
    case Op::PartOf: { int o = Ob(0); r = Value::Str(o >= 0 ? m_world->objectPart(o) : ""); break; }
    case Op::IsValid: r = Value::Bool(Ob(0) >= 0); break;
    case Op::MakeVec3: r = Value::Vec3(float3(F(0), F(1), F(2))); break;
    case Op::BreakVec3: { float3 v = V(0); r = Value::Float(v[pin]); break; }
    case Op::Distance: r = Value::Float(length(V(0) - V(1))); break;
    case Op::Time: r = Value::Float(m_time); break;
    case Op::Add: r = Value::Float(F(0) + F(1)); break;
    case Op::Sub: r = Value::Float(F(0) - F(1)); break;
    case Op::Mul: r = Value::Float(F(0) * F(1)); break;
    case Op::Div: { float b = F(1); r = Value::Float(b != 0 ? F(0) / b : 0.0f); break; }
    case Op::Greater: r = Value::Bool(F(0) > F(1)); break;
    case Op::Less: r = Value::Bool(F(0) < F(1)); break;
    case Op::Equal: r = Value::Bool(std::fabs(F(0) - F(1)) < 1e-5f); break;
    case Op::StrEqual: r = Value::Bool(S(0) == S(1)); break;
    case Op::And: r = Value::Bool(Bv(0) && Bv(1)); break;
    case Op::Or: r = Value::Bool(Bv(0) || Bv(1)); break;
    case Op::Not: r = Value::Bool(!Bv(0)); break;
    case Op::Random: { float a = F(0), b = F(1); r = Value::Float(a + (b - a) * std::uniform_real_distribution<float>(0, 1)(rng())); break; }
    case Op::AddVec3: r = Value::Vec3(V(0) + V(1)); break;
    case Op::ScaleVec3: r = Value::Vec3(V(0) * F(1)); break;
    case Op::Concat: r = Value::Str(S(0) + S(1)); break;
    case Op::SnapGrid: {
        float3 p = V(0);
        float s = F(1);
        if (s > 0) { p.x = std::round(p.x / s) * s; p.z = std::round(p.z / s) * s; }
        r = Value::Vec3(p);
        break;
    }
    case Op::CellFree: {
        float3 p = V(0);
        float h = std::max(F(1), 0.01f) * 0.5f;
        bool free = true;
        for (int i = 0; i < m_world->objectCount() && free; ++i) {
            if (!m_world->objectAlive(i) || m_world->objectPart(i).empty()) continue;
            float3 q = m_world->getPosition(i);
            if (std::fabs(q.x - p.x) < h && std::fabs(q.z - p.z) < h) free = false;
        }
        r = Value::Bool(free);
        break;
    }
    case Op::HouseAt: r = Value::Float((float)m_world->houseAt((int)F(0), (int)F(1))); break;
    case Op::HouseCount: r = Value::Float((float)m_world->houseCount()); break;
    case Op::CellAt: {
        int cx = 0, cz = 0;
        float3 p = V(0);
        m_world->cellAt(p.x, p.z, cx, cz);
        r = Value::Float((float)(pin == 0 ? cx : cz));
        break;
    }
    case Op::CountPart: {
        std::string part = S(0);
        int c = 0;
        for (int i = 0; i < m_world->objectCount(); ++i)
            if (m_world->objectAlive(i) && !m_world->objectPart(i).empty() && (part.empty() || m_world->objectPart(i) == part)) ++c;
        r = Value::Float((float)c);
        break;
    }
    default: break;
    }
    --m_depth;
    return r.type == t ? r : convert(r, t);
}

void Runtime::recordRun(int nodeId) {
    trace.push_back(nodeId);
    while (trace.size() > 300) trace.pop_front();
}

void Runtime::exec(Node& n) {
    m_lastRun[n.id] = m_time;
    recordRun(n.id);
    auto F = [&](int i) { return input(n, i).asFloat(); };
    auto V = [&](int i) { return input(n, i).asVec3(); };
    auto S = [&](int i) { return input(n, i).asString(); };
    auto Ob = [&](int i) { int o = input(n, i).obj; return m_world->objectAlive(o) ? o : -1; };
    switch (n.def->op) {
    case Op::Branch: execFrom(n.id, input(n, 1).asBool() ? 0 : 1); return;
    case Op::Sequence: for (int i = 0; i < 3 && m_graph; ++i) execFrom(n.id, i); return;
    case Op::Delay: m_timers.push_back({std::max(F(1), 0.0f), n.id, 0}); return;
    case Op::ForEachPart: {
        std::string part = S(1);
        std::vector<int> objs;
        for (int i = 0; i < m_world->objectCount(); ++i)
            if (m_world->objectAlive(i) && !m_world->objectPart(i).empty() && (part.empty() || m_world->objectPart(i) == part)) objs.push_back(i);
        for (int o : objs) {
            if (!m_graph || m_budget < 0) return;
            setOut(n, 1, Value::Obj(o));
            execFrom(n.id, 0);
        }
        execFrom(n.id, 2);
        return;
    }
    case Op::DoOnce:
        if (m_doneOnce[n.id]) return;
        m_doneOnce[n.id] = true;
        execFrom(n.id, 0);
        return;
    case Op::FireCustom: {
        std::string name = S(1);
        fire(Op::OnCustom, [&](const Node& e) { return e.inputs[0].s == name; });
        execFrom(n.id, 0);
        return;
    }
    case Op::SetPosition: { int o = Ob(1); if (o >= 0) m_world->setPosition(o, V(2)); break; }
    case Op::MoveTo: {
        int o = Ob(1);
        if (o >= 0) {
            m_movers.erase(std::remove_if(m_movers.begin(), m_movers.end(), [&](const Mover& m) { return m.obj == o; }), m_movers.end());
            m_movers.push_back({o, V(2), std::max(F(3), 0.001f), n.id, 1});
        }
        break;
    }
    case Op::RotateY: { int o = Ob(1); if (o >= 0) m_world->rotateY(o, F(2)); break; }
    case Op::Spawn: {
        int o = m_world->spawn(S(1), V(2), V(3));
        if (o < 0) error("type d'objet inconnu : " + S(1));
        setOut(n, 1, Value::Obj(o));
        break;
    }
    case Op::Destroy: { int o = Ob(1); if (o >= 0) m_world->destroy(o); break; }
    case Op::SpawnPrefab: {
        int o = m_world->spawnPrefab(S(1), V(2), F(3));
        if (o < 0) error("prefab not found: " + S(1));
        setOut(n, 1, Value::Obj(o));
        break;
    }
    case Op::PlayClip: {
        int o = Ob(1);
        if (o >= 0 && !m_world->playClip(o, S(2), input(n, 3).asBool(), F(4))) error("no clip " + S(2) + " on " + m_world->objectName(o));
        break;
    }
    case Op::StopClip: { int o = Ob(1); if (o >= 0) m_world->stopClip(o); break; }
    // ---- RTS units
    case Op::ForEachSelected: {
        const std::vector<int> objs = m_world->selection();
        for (int o : objs) {
            if (!m_graph || m_budget < 0) return;
            if (!m_world->objectAlive(o)) continue;
            setOut(n, 1, Value::Obj(o));
            execFrom(n.id, 0);
        }
        if (m_graph) execFrom(n.id, 2);
        return;
    }
    case Op::SelectAdd: {
        const int o = Ob(1);
        std::vector<int> s = input(n, 2).asBool() ? m_world->selection() : std::vector<int>();
        if (o >= 0 && std::find(s.begin(), s.end(), o) == s.end()) s.push_back(o);
        m_world->setSelection(s);
        break;
    }
    case Op::ClearSelection: m_world->setSelection({}); break;
    case Op::SetObjVar: { int o = Ob(1); if (o >= 0) m_objVars[o][S(2)] = F(3); break; }
    case Op::AddObjVar: { int o = Ob(1); if (o >= 0) m_objVars[o][S(2)] += F(3); break; }
    case Op::SetObjLink: {
        const int o = Ob(1);
        if (o >= 0) m_objLinks[o][S(2)] = Ob(3);
        break;
    }
    case Op::StartTimer: {
        const int o = Ob(1);
        const std::string name = S(2);
        m_objTimers.erase(std::remove_if(m_objTimers.begin(), m_objTimers.end(), [&](const ObjTimer& t) { return t.obj == o && t.name == name; }),
                          m_objTimers.end());
        m_objTimers.push_back({o, name, std::max(F(3), 0.0f)});
        break;
    }
    case Op::MoveUnit: {
        const int o = Ob(1);
        if (o >= 0) {
            m_unitMoves.erase(std::remove_if(m_unitMoves.begin(), m_unitMoves.end(), [&](const UnitMove& m) { return m.obj == o; }), m_unitMoves.end());
            // formation: the units sent to the same point (one order to a group) each take a place around it
            const float3 order = V(2);
            int k = 0;
            for (const UnitMove& m : m_unitMoves)
                if (length(float3(m.order.x - order.x, 0, m.order.z - order.z)) < 0.05f) ++k;
            float3 target = order;
            if (k > 0) {   // rings of 6, 12... places, 0.95 apart
                int ring = 1, first = 1;
                while (k >= first + 6 * ring) { first += 6 * ring; ++ring; }
                const float a = (k - first) * 6.2831853f / (6 * ring) + ring * 0.5f;
                target = order + float3(std::cos(a), 0, std::sin(a)) * (0.95f * ring);
            }
            std::vector<float3> path = m_world->findPath(m_world->getPosition(o), target);
            if (path.empty()) path.push_back(target);
            m_unitMoves.push_back({o, path, 0, std::max(F(3), 0.1f), n.id, 0.0f, order});
            m_world->playClip(o, "Walk", true, 1.0f);
        }
        break;
    }
    case Op::StopUnit: {
        const int o = Ob(1);
        if (o >= 0 && unitMoving(o)) {
            m_unitMoves.erase(std::remove_if(m_unitMoves.begin(), m_unitMoves.end(), [&](const UnitMove& m) { return m.obj == o; }), m_unitMoves.end());
            m_world->playClip(o, "Idle", true, 1.0f);
        }
        break;
    }
    case Op::PlaceBuilding: {
        const std::string cv = S(2);
        const float cost = F(3);
        auto it = m_vars.find(cv);
        const float have = it == m_vars.end() ? 0.0f : it->second.asFloat();
        if (!cv.empty() && have < cost) {
            m_world->message("Not enough " + cv + " (" + std::to_string((int)cost) + ")");
            execFrom(n.id, 0);
            if (m_graph) execFrom(n.id, 3);
            return;
        }
        if (m_placeNode >= 0) m_world->message("A building is already being placed");
        else if (m_world->beginPlacement(S(1))) {
            m_placeNode = n.id;
            m_placeCostVar = cv;
            m_placeCost = cost;
        } else error("prefab not found: " + S(1));
        break;
    }
    case Op::ProduceUnit: {
        const int b = Ob(1);
        if (b >= 0) {
            const float s = std::max(F(3), 0.1f);
            m_production.push_back({b, S(2), s, s, n.id});
        }
        break;
    }
    case Op::HudCounter: {
        HudCounter c{S(1), S(2), S(3), V(4)};
        auto it = std::find_if(hudCounters.begin(), hudCounters.end(), [&](const HudCounter& h) { return h.id == c.id; });
        if (it != hudCounters.end()) *it = c;
        else hudCounters.push_back(c);
        break;
    }
    case Op::SetObjective: {
        Objective ob{S(1), S(2), input(n, 3).asBool()};
        auto it = std::find_if(objectives.begin(), objectives.end(), [&](const Objective& h) { return h.id == ob.id; });
        if (it != objectives.end()) *it = ob;
        else objectives.push_back(ob);
        break;
    }
    case Op::ShowMessage:
        messageTitle = S(1);
        messageText = S(2);
        messageShown = true;
        log.push_back(messageTitle + (messageText.empty() ? "" : ": " + messageText));
        break;
    case Op::SetPartColor: { int o = Ob(1); if (o >= 0) m_world->setPartColor(o, S(2), V(3)); break; }
    case Op::SetPartGlow: { int o = Ob(1); if (o >= 0) m_world->setPartGlow(o, S(2), F(3)); break; }
    case Op::SetColor: { int o = Ob(1); if (o >= 0) m_world->setColor(o, V(2)); break; }
    case Op::SetScale: { int o = Ob(1); if (o >= 0) m_world->setScale(o, V(2)); break; }
    case Op::Select: m_world->select(Ob(1)); break;
    case Op::SetRtsCamera: m_world->setRtsCamera(input(n, 1).asBool()); break;
    case Op::PlaceHouse: {
        int id = m_world->placeHouse((int)F(1), (int)F(2), (int)F(3), (int)F(4));
        if (id < 0) error("maison non placée (case prise ou hors de la grille)");
        setOut(n, 1, Value::Float((float)id));
        break;
    }
    case Op::RemoveHouse: m_world->removeHouse((int)F(1)); break;
    case Op::TurnHouse: m_world->turnHouse((int)F(1)); break;
    case Op::HudButton: m_world->hudButton(S(1), S(2)); break;
    case Op::SetSun: m_world->setSun(F(1), F(2)); break;
    case Op::Print: {
        std::string s = S(1);
        log.push_back(s);
        while (log.size() > 200) log.pop_front();
        m_world->message(s);
        break;
    }
    case Op::SetVar: m_vars[S(1)] = Value::Float(F(2)); break;
    case Op::AddVar: {
        std::string k = S(1);
        float cur = m_vars.count(k) ? m_vars[k].asFloat() : 0.0f;
        m_vars[k] = Value::Float(cur + F(2));
        break;
    }
    default: break;
    }
    if (m_graph) execFrom(n.id, 0);
}

} // namespace game
