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
    m_timers.clear();
    m_movers.clear();
    m_doneOnce.clear();
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
    log.push_back("Erreur : " + e);
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
        if (--m_budget < 0) { if (m_budget == -1) error("boucle infinie stoppée (trop de noeuds exécutés)"); return; }
        if (m_depth >= kMaxDepth) { error("chaîne d'exécution trop profonde"); return; }
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
    return convert(output(*src, l->fromPin), t);
}

Value Runtime::output(const Node& n, int pin) {
    PinType t = n.def->outs[pin].type;
    if (!n.def->pure) {
        auto it = m_cache.find(n.id);
        if (it != m_cache.end() && pin < (int)it->second.size() && it->second[pin].type == t) return it->second[pin];
        return Value::None(t);
    }
    if (m_depth >= kMaxDepth) { error("cycle dans les données"); return Value::None(t); }
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

void Runtime::exec(Node& n) {
    m_lastRun[n.id] = m_time;
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
    case Op::SetColor: { int o = Ob(1); if (o >= 0) m_world->setColor(o, V(2)); break; }
    case Op::SetScale: { int o = Ob(1); if (o >= 0) m_world->setScale(o, V(2)); break; }
    case Op::Select: m_world->select(Ob(1)); break;
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
