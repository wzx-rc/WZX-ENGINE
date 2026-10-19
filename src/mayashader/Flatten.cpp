#include "Flatten.h"

namespace mayashader {

using game::Graph;
using game::Link;
using game::Node;
using game::PinType;
using game::Value;

namespace {

// A graph being walked: the root, or the inner graph of compound `compound` of the parent context.
struct Ctx {
    const Graph* g = nullptr;
    int prefix = 0;            // flat id of the compound (0 at the root)
    const Ctx* parent = nullptr;
    int compound = 0;          // id of the compound in the parent graph
    int flat(int local) const { return prefix ? prefix * 1000 + local : local; }
};

bool isIo(const Node& n) { return n.type == "compound.input" || n.type == "compound.output"; }

FlatRef resolve(const Ctx& c, int nodeId, int pin, int depth = 0) {
    FlatRef r;
    const Node* n = c.g->node(nodeId);
    if (!n || depth > 64) return r;
    if (n->sub) {   // compound: what feeds its Output node
        Ctx inner{n->sub.get(), c.flat(n->id), &c, n->id};
        for (const Node& o : n->sub->nodes)
            if (o.type == "compound.output")
                if (const Link* l = n->sub->inputLink(o.id, pin)) return resolve(inner, l->fromNode, l->fromPin, depth + 1);
        return r;
    }
    if (n->type == "compound.input") {   // the matching input of the compound, in the parent graph
        if (!c.parent) return r;
        if (const Link* l = c.parent->g->inputLink(c.compound, pin)) return resolve(*c.parent, l->fromNode, l->fromPin, depth + 1);
        const Node* cn = c.parent->g->node(c.compound);
        if (cn && pin < (int)cn->inputs.size()) { r.constant = true; r.value = cn->inputs[pin]; }
        return r;
    }
    r.node = c.flat(n->id);
    r.pin = pin;
    return r;
}

Value convert(const Value& v, PinType t) {
    Value o = v;
    o.type = t;
    if (t == PinType::Float) o.f = v.type == PinType::Vec3 ? v.v.x : v.asFloat();
    else if (t == PinType::Vec3) o.v = v.asVec3();
    else if (t == PinType::Bool) o.b = v.asBool();
    return o;
}

void flattenInto(const Ctx& c, Graph& out) {
    for (const Node& n : c.g->nodes) {
        if (n.sub || isIo(n) || !n.def) continue;
        Node f = n;
        f.id = c.flat(n.id);
        f.def = game::findNodeDef(n.type);
        out.nodes.push_back(f);
        out.nextId = std::max(out.nextId, f.id + 1);
    }
    for (const Link& l : c.g->links) {
        const Node* to = c.g->node(l.toNode);
        if (!to || to->sub || isIo(*to)) continue;
        FlatRef s = resolve(c, l.fromNode, l.fromPin);
        if (!s.valid()) continue;
        if (s.constant) {   // an unconnected compound input: its value goes to the inner input
            if (Node* f = out.node(c.flat(to->id)))
                if (l.toPin < (int)f->inputs.size()) {
                    const game::PinDef& p = f->def->ins[l.toPin];
                    if (p.parent >= 0) f->inputs[p.parent].v[p.channel] = convert(s.value, PinType::Float).f;
                    else f->inputs[l.toPin] = convert(s.value, p.type);
                }
            continue;
        }
        out.links.push_back({s.node, s.pin, c.flat(to->id), l.toPin});
    }
    for (const Node& n : c.g->nodes)
        if (n.sub) flattenInto(Ctx{n.sub.get(), c.flat(n.id), &c, n.id}, out);
}

// Contexts from the root down to the graph opened at `path` (kept alive by the caller's vector).
const Ctx* walk(const Graph& root, const std::vector<int>& path, std::vector<Ctx>& chain) {
    chain.clear();
    chain.reserve(path.size() + 1);
    chain.push_back(Ctx{&root, 0, nullptr, 0});
    for (int id : path) {
        const Ctx& c = chain.back();
        const Node* n = c.g->node(id);
        if (!n || !n->sub) return nullptr;
        chain.push_back(Ctx{n->sub.get(), c.flat(id), &chain[chain.size() - 1], id});
    }
    return &chain.back();
}

} // namespace

Graph flatten(const Graph& root) {
    Graph out;
    out.name = root.name;
    out.nextId = 1;
    flattenInto(Ctx{&root, 0, nullptr, 0}, out);
    return out;
}

FlatRef resolveOut(const Graph& root, const std::vector<int>& path, int node, int pin) {
    std::vector<Ctx> chain;
    const Ctx* c = walk(root, path, chain);
    return c ? resolve(*c, node, pin) : FlatRef();
}

FlatRef resolveIn(const Graph& root, const std::vector<int>& path, int node, int pin) {
    std::vector<Ctx> chain;
    const Ctx* c = walk(root, path, chain);
    if (!c) return FlatRef();
    const Link* l = c->g->inputLink(node, pin);
    return l ? resolve(*c, l->fromNode, l->fromPin) : FlatRef();
}

} // namespace mayashader
