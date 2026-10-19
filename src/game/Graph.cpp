#include <map>
#include "Graph.h"
#include "app/Json.h"
#include <algorithm>
#include <functional>
#include <set>
#include <fstream>
#include "vp3d/FilePath.h"
#include <sstream>

namespace game {

using app::Json;
using app::jsonEscape;

// --------------------------------------------------------------------------- values

float Value::asFloat() const {
    switch (type) {
    case PinType::Float: return f;
    case PinType::Bool: return b ? 1.0f : 0.0f;
    case PinType::Vec3: return v.x;
    case PinType::Object: return (float)obj;
    case PinType::String: return (float)atof(s.c_str());
    default: return 0;
    }
}

bool Value::asBool() const {
    switch (type) {
    case PinType::Float: return f != 0;
    case PinType::Bool: return b;
    case PinType::Object: return obj >= 0;
    case PinType::String: return !s.empty();
    default: return false;
    }
}

float3 Value::asVec3() const {
    if (type == PinType::Vec3) return v;
    if (type == PinType::Float) return float3(f);
    return float3(0, 0, 0);
}

std::string Value::asString() const {
    char b2[96];
    switch (type) {
    case PinType::Float: snprintf(b2, sizeof b2, "%g", f); return b2;
    case PinType::Bool: return b ? "vrai" : "faux";
    case PinType::Vec3: snprintf(b2, sizeof b2, "(%g, %g, %g)", v.x, v.y, v.z); return b2;
    case PinType::Object: snprintf(b2, sizeof b2, "objet #%d", obj); return b2;
    case PinType::String: return s;
    default: return "";
    }
}

const char* pinTypeName(PinType t) {
    switch (t) {
    case PinType::Exec: return "exec";
    case PinType::Float: return "float";
    case PinType::Bool: return "bool";
    case PinType::Vec3: return "vec3";
    case PinType::Object: return "object";
    case PinType::String: return "string";
    }
    return "?";
}

std::string valueToJson(const Value& v) {
    char b[128];
    switch (v.type) {
    case PinType::Float: snprintf(b, sizeof b, "%g", v.f); return b;
    case PinType::Bool: return v.b ? "true" : "false";
    case PinType::Vec3: snprintf(b, sizeof b, "[%g, %g, %g]", v.v.x, v.v.y, v.v.z); return b;
    case PinType::Object: snprintf(b, sizeof b, "%d", v.obj); return b;
    case PinType::String: return "\"" + jsonEscape(v.s) + "\"";
    default: return "null";
    }
}

static bool valueFromJson(const Json& j, PinType t, Value& out) {
    out.type = t;
    switch (t) {
    case PinType::Float: if (j.type != Json::Number) return false; out.f = (float)j.n; return true;
    case PinType::Bool:
        if (j.type == Json::Bool) { out.b = j.b; return true; }
        if (j.type == Json::Number) { out.b = j.n != 0; return true; }
        return false;
    case PinType::Vec3:
        if (j.type == Json::Number) { out.v = float3((float)j.n); return true; }
        if (j.type != Json::Array || j.a.size() < 3) return false;
        out.v = float3((float)j[0].num(), (float)j[1].num(), (float)j[2].num());
        return true;
    case PinType::String:
        if (j.type == Json::String) { out.s = j.s; return true; }
        if (j.type == Json::Number) { char b[64]; snprintf(b, sizeof b, "%g", j.n); out.s = b; return true; }
        return false;
    default: return false;
    }
}

// --------------------------------------------------------------------------- node defs

// Pin names of the graph files written before the English names: still accepted when loading.
static std::string legacyPinName(const std::string& n) {
    static const std::map<std::string, std::string> kLegacy = {
        {"Objet", "Object"}, {"Nom", "Name"}, {"Vrai", "True"}, {"Faux", "False"}, {"Puis 1", "Then 1"}, {"Puis 2", "Then 2"},
        {"Puis 3", "Then 3"}, {"Secondes", "Seconds"}, {"Boucle", "Loop"}, {"Terminé", "Completed"}, {"Cible", "Target"},
        {"Vitesse", "Speed"}, {"Arrivé", "Arrived"}, {"Degrés", "Degrees"}, {"Couleur", "Color"}, {"Échelle", "Scale"},
        {"Texte", "Text"}, {"Valeur", "Value"}, {"Quantité", "Amount"}, {"Vecteur", "Vector"}, {"Résultat", "Result"},
        {"Pas", "Step"}, {"Libre", "Free"}, {"Actif", "Active"}, {"Nombre", "Count"}, {"Facteur", "Factor"}, {"Valide", "Valid"},
        {"Touche", "Key"}};
    auto it = kLegacy.find(n);
    return it == kLegacy.end() ? n : it->second;
}

int NodeDef::findIn(const std::string& n) const {
    for (size_t i = 0; i < ins.size(); ++i) if (ins[i].name == n) return (int)i;
    const std::string e = legacyPinName(n);
    for (size_t i = 0; i < ins.size(); ++i) if (ins[i].name == e) return (int)i;
    return -1;
}

int NodeDef::findOut(const std::string& n) const {
    for (size_t i = 0; i < outs.size(); ++i) if (outs[i].name == n) return (int)i;
    const std::string e = legacyPinName(n);
    for (size_t i = 0; i < outs.size(); ++i) if (outs[i].name == e) return (int)i;
    return -1;
}

const NodeDef* findNodeDef(const std::string& id) {
    for (const NodeDef& d : nodeDefs()) if (d.id == id) return &d;
    return nullptr;
}

// --------------------------------------------------------------------------- graph

Node* Graph::node(int id) {
    for (Node& n : nodes) if (n.id == id) return &n;
    return nullptr;
}

const Node* Graph::node(int id) const {
    for (const Node& n : nodes) if (n.id == id) return &n;
    return nullptr;
}

int Graph::addNode(const std::string& type, float2 pos) {
    const NodeDef* d = findNodeDef(type);
    if (!d) return 0;
    Node n;
    n.id = nextId++;
    n.type = type;
    n.pos = pos;
    n.def = d;
    n.inputs = d->defaults;
    n.inputs.resize(d->ins.size());
    for (size_t i = 0; i < d->ins.size(); ++i) n.inputs[i].type = d->ins[i].type;
    nodes.push_back(n);
    ++version;
    return n.id;
}

void Graph::removeNode(int id) {
    links.erase(std::remove_if(links.begin(), links.end(), [&](const Link& l) { return l.fromNode == id || l.toNode == id; }),
                links.end());
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&](const Node& n) { return n.id == id; }), nodes.end());
    ++version;
}

bool Graph::compatible(PinType out, PinType in) {
    if (out == in) return true;
    if (out == PinType::Exec || in == PinType::Exec) return false;
    if (in == PinType::Object || out == PinType::Object) return in == PinType::Bool && out == PinType::Object;
    if (in == PinType::String) return true;                               // anything prints
    if (in == PinType::Float && out == PinType::Vec3) return true;           // colour to a value (Maya shader graph)
    if (in == PinType::Float || in == PinType::Bool) return out == PinType::Float || out == PinType::Bool;
    if (in == PinType::Vec3) return out == PinType::Float;
    return false;
}

bool Graph::connect(int fromNode, int fromPin, int toNode, int toPin, std::string* err) {
    auto fail = [&](const char* m) { if (err) *err = m; return false; };
    const Node* a = node(fromNode);
    const Node* b = node(toNode);
    if (!a || !b) return fail("noeud introuvable");
    if (fromNode == toNode) return fail("un noeud ne peut pas se connecter a lui-meme");
    if (fromPin < 0 || fromPin >= (int)a->def->outs.size()) return fail("sortie introuvable");
    if (toPin < 0 || toPin >= (int)b->def->ins.size()) return fail("entree introuvable");
    // "+" pin of a compound Input / Output node: a new promoted pin takes the name and type of the other side
    const bool wildOut = a->def->outs[fromPin].wildcard, wildIn = b->def->ins[toPin].wildcard;
    if (wildOut || wildIn) {
        if ((wildOut && wildIn) || !io) return fail("deux broches + ne se connectent pas");
        if (wildOut) {
            const PinDef t = b->def->ins[toPin];
            if (t.type == PinType::Exec) return fail("broche de flux");
            io->ins.push_back({t.parent >= 0 ? b->def->ins[t.parent].name + " " + t.name.substr(t.name.rfind('.') + 1) : t.name, t.type});
            io->defaults.push_back(b->inputs[toPin]);
            fromPin = (int)io->ins.size() - 1;
        } else {
            const PinDef t = a->def->outs[fromPin];
            if (t.type == PinType::Exec) return fail("broche de flux");
            io->outs.push_back({t.name, t.type});
            toPin = (int)io->outs.size() - 1;
        }
        ++io->version;
        refreshDefs();
        a = node(fromNode);
        b = node(toNode);
    }
    PinType o = a->def->outs[fromPin].type, i = b->def->ins[toPin].type;
    if (!compatible(o, i)) return fail("types de broches incompatibles");
    if (o == PinType::Exec) disconnectPin(fromNode, fromPin, true);   // one exec output -> one target
    else disconnectPin(toNode, toPin, false);                         // one source per data input
    // a colour and its channels (R, G, B) are not driven together: the last connection wins
    if (b->def->ins[toPin].parent >= 0) disconnectPin(toNode, b->def->ins[toPin].parent, false);
    for (size_t k = 0; k < b->def->ins.size(); ++k)
        if (b->def->ins[k].parent == toPin) disconnectPin(toNode, (int)k, false);
    links.push_back({fromNode, fromPin, toNode, toPin});
    ++version;
    return true;
}

void Graph::disconnectPin(int nodeId, int pin, bool output) {
    links.erase(std::remove_if(links.begin(), links.end(), [&](const Link& l) {
        return output ? (l.fromNode == nodeId && l.fromPin == pin) : (l.toNode == nodeId && l.toPin == pin);
    }), links.end());
    ++version;
}

void Graph::removeLink(const Link& l) {
    links.erase(std::remove_if(links.begin(), links.end(), [&](const Link& k) {
        return k.fromNode == l.fromNode && k.fromPin == l.fromPin && k.toNode == l.toNode && k.toPin == l.toPin;
    }), links.end());
    ++version;
}

const Link* Graph::inputLink(int nodeId, int pin) const {
    for (const Link& l : links) if (l.toNode == nodeId && l.toPin == pin) return &l;
    return nullptr;
}

void Graph::clear() {
    nodes.clear();
    links.clear();
    groups.clear();
    nextId = 1;
    ++version;
}

// --------------------------------------------------------------------------- JSON

std::string Graph::toJson() const {
    std::ostringstream js;
    js << "{\n  \"format\": \"wzx-graph\",\n  \"version\": 1,\n  \"name\": \"" << jsonEscape(name) << "\",\n  \"nodes\": [";
    bool first = true;
    for (const Node& n : nodes) {
        js << (first ? "\n" : ",\n") << "    {\"id\": " << n.id << ", \"type\": \"" << jsonEscape(n.type) << "\", \"x\": "
           << (int)n.pos.x << ", \"y\": " << (int)n.pos.y << ", \"in\": {";
        bool f2 = true;
        for (size_t i = 0; i < n.def->ins.size(); ++i) {
            const PinDef& p = n.def->ins[i];
            if (p.type == PinType::Exec || p.type == PinType::Object || p.wildcard) continue;
            js << (f2 ? "" : ", ") << "\"" << jsonEscape(p.name) << "\": " << valueToJson(n.inputs[i]);
            f2 = false;
        }
        js << "}";
        if (n.sub && n.sub->io) {   // compound: promoted pins and the inner graph
            const CompoundIO& io2 = *n.sub->io;
            auto pins = [&](const std::vector<PinDef>& v) {
                std::string s = "[";
                for (size_t i = 0; i < v.size(); ++i)
                    s += std::string(i ? ", " : "") + "[\"" + jsonEscape(v[i].name) + "\", \"" + pinTypeName(v[i].type) + "\"]";
                return s + "]";
            };
            js << ", \"compound\": {\"name\": \"" << jsonEscape(io2.name) << "\", \"ins\": " << pins(io2.ins) << ", \"outs\": "
               << pins(io2.outs) << ", \"sub\": " << n.sub->toJson() << "}";
        }
        js << "}";
        first = false;
    }
    js << "\n  ],\n  \"links\": [";
    first = true;
    for (const Link& l : links) {
        const Node* a = node(l.fromNode);
        const Node* b = node(l.toNode);
        js << (first ? "\n" : ",\n") << "    {\"from\": [" << l.fromNode << ", \"" << jsonEscape(a->def->outs[l.fromPin].name)
           << "\"], \"to\": [" << l.toNode << ", \"" << jsonEscape(b->def->ins[l.toPin].name) << "\"]}";
        first = false;
    }
    js << "\n  ],\n  \"groups\": [";
    first = true;
    for (const Group& gr : groups) {
        js << (first ? "\n" : ",\n") << "    {\"title\": \"" << jsonEscape(gr.title) << "\", \"x\": " << gr.x
           << ", \"y\": " << gr.y << ", \"w\": " << gr.w << ", \"h\": " << gr.h
           << ", \"category\": \"" << jsonEscape(gr.category) << "\"}";
        first = false;
    }
    js << "\n  ]\n}\n";
    return js.str();
}

bool Graph::fromJson(const std::string& text, std::string* err) {
    Json j;
    if (!Json::parse(text, j) || j.type != Json::Object) { if (err) *err = "JSON invalide"; return false; }
    if (j["format"].str("wzx-graph") != "wzx-graph") { if (err) *err = "format inconnu"; return false; }
    return fromJsonValue(j, io, err);
}

static PinType pinTypeFromName(const std::string& s) {
    for (PinType t : {PinType::Exec, PinType::Float, PinType::Bool, PinType::Vec3, PinType::Object, PinType::String})
        if (s == pinTypeName(t)) return t;
    return PinType::Float;
}

bool Graph::fromJsonValue(const Json& j, std::shared_ptr<CompoundIO> ioIn, std::string* err) {
    Graph g;
    g.io = ioIn;
    g.name = j["name"].str("main");
    for (const Json& jn : j["nodes"].a) {
        const NodeDef* d = findNodeDef(jn["type"].str());
        if (!d) { if (err) *err = "type de noeud inconnu : " + jn["type"].str(); return false; }
        int id = (int)jn["id"].num(0);
        g.nextId = std::max(g.nextId, id);
        int nid = g.addNode(d->id, float2((float)jn["x"].num(), (float)jn["y"].num()));
        Node* n = g.node(nid);
        n->id = id > 0 ? id : nid;
        if (jn["compound"].type == Json::Object) {   // compound: promoted pins, then its inner graph
            const Json& jc = jn["compound"];
            auto cio = std::make_shared<CompoundIO>();
            cio->name = jc["name"].str("Compound");
            for (const Json& p : jc["ins"].a) { cio->ins.push_back({p[0].str(), pinTypeFromName(p[1].str())}); cio->defaults.push_back(Value::None(cio->ins.back().type)); }
            for (const Json& p : jc["outs"].a) cio->outs.push_back({p[0].str(), pinTypeFromName(p[1].str())});
            auto sub = std::make_shared<Graph>();
            if (!sub->fromJsonValue(jc["sub"], cio, err)) return false;
            n->sub = sub;
        }
        g.refreshDefs();
        n = g.node(id > 0 ? id : nid);
        for (auto& kv : jn["in"].o) {
            int pi = n->def->findIn(kv.first);
            if (pi >= 0 && !n->def->ins[pi].wildcard) valueFromJson(kv.second, n->def->ins[pi].type, n->inputs[pi]);
        }
        if (n->sub)   // the compound inputs keep their values as defaults
            for (size_t k = 0; k < n->sub->io->defaults.size() && k < n->inputs.size(); ++k) n->sub->io->defaults[k] = n->inputs[k];
    }
    for (Node& n : g.nodes) g.nextId = std::max(g.nextId, n.id + 1);
    for (const Json& jl : j["links"].a) {
        int a = (int)jl["from"][0].num(), b = (int)jl["to"][0].num();
        const Node* na = g.node(a);
        const Node* nb = g.node(b);
        if (!na || !nb) continue;
        int pa = jl["from"][1].type == Json::String ? na->def->findOut(jl["from"][1].s) : (int)jl["from"][1].num(-1);
        int pb = jl["to"][1].type == Json::String ? nb->def->findIn(jl["to"][1].s) : (int)jl["to"][1].num(-1);
        std::string e;
        if (!g.connect(a, pa, b, pb, &e)) { if (err) *err = "lien invalide : " + e; return false; }
    }
    for (const Json& jg : j["groups"].a) {
        Group gr;
        gr.title = jg["title"].str("GROUP");
        gr.x = (float)jg["x"].num();
        gr.y = (float)jg["y"].num();
        gr.w = (float)jg["w"].num(400);
        gr.h = (float)jg["h"].num(240);
        gr.category = jg["category"].str("Flow Control");
        g.groups.push_back(gr);
    }
    g.version = version + 1;
    *this = std::move(g);
    return true;
}

bool Graph::save(const std::wstring& path) const {
    std::ofstream f(vp3d::nativePath(path), std::ios::binary);
    if (!f) return false;
    f << toJson();
    return (bool)f;
}

bool Graph::load(const std::wstring& path, std::string* err) {
    std::ifstream f(vp3d::nativePath(path), std::ios::binary);
    if (!f) { if (err) *err = "fichier introuvable"; return false; }
    std::stringstream ss;
    ss << f.rdbuf();
    return fromJson(ss.str(), err);
}

// --------------------------------------------------------------------------- compounds

void Graph::refreshDefs() {
    for (Node& n : nodes) {
        if (!n.def) n.def = findNodeDef(n.type);
        std::shared_ptr<CompoundIO> cio;
        if (n.type == "compound") {
            if (!n.sub) { n.sub = std::make_shared<Graph>(); n.sub->io = std::make_shared<CompoundIO>(); }
            cio = n.sub->io;
            n.sub->refreshDefs();
        } else if (n.type == "compound.input" || n.type == "compound.output") {
            cio = io;
        }
        if (!cio) continue;
        if (n.ownDef && n.defVersion == cio->version) { n.def = n.ownDef.get(); continue; }
        auto d = std::make_shared<NodeDef>(*findNodeDef(n.type));
        PinDef plus{"+", PinType::Float};
        plus.wildcard = true;
        if (n.type == "compound") {
            d->label = cio->name;
            d->ins = cio->ins;
            d->outs = cio->outs;
            d->defaults = cio->defaults;
        } else if (n.type == "compound.input") {
            d->outs = cio->ins;
            d->outs.push_back(plus);
        } else {
            d->ins = cio->outs;
            d->ins.push_back(plus);
            d->defaults.clear();
            for (const PinDef& p : d->ins) d->defaults.push_back(Value::None(p.type));
        }
        d->defaults.resize(d->ins.size());
        // inline values: the existing ones are kept, the new pins take their default
        size_t old = n.inputs.size();
        n.inputs.resize(d->ins.size());
        for (size_t i = 0; i < d->ins.size(); ++i) {
            if (i >= old || n.inputs[i].type != d->ins[i].type) n.inputs[i] = d->defaults[i];
            n.inputs[i].type = d->ins[i].type;
        }
        n.ownDef = d;
        n.def = d.get();
        n.defVersion = cio->version;
    }
}

int Graph::addCompound(float2 pos, const std::string& name) {
    int id = addNode("compound", pos);
    if (!id) return 0;
    Node* n = node(id);
    n->sub = std::make_shared<Graph>();
    n->sub->io = std::make_shared<CompoundIO>();
    n->sub->io->name = name;
    n->sub->name = name;
    n->sub->addNode("compound.input", float2(-320, 0));
    n->sub->addNode("compound.output", float2(320, 0));
    refreshDefs();
    return id;
}

std::shared_ptr<Graph> Graph::cloneSub(const Node& c) const {
    if (!c.sub) return nullptr;
    auto io2 = std::make_shared<CompoundIO>(*c.sub->io);
    auto g = std::make_shared<Graph>();
    Json j;
    if (!Json::parse(c.sub->toJson(), j) || !g->fromJsonValue(j, io2, nullptr)) return nullptr;
    return g;
}

int Graph::collapse(const std::vector<int>& ids, const std::string& name) {
    std::set<int> inSet(ids.begin(), ids.end());
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, cy = 0;
    int count = 0;
    for (int id : ids)
        if (const Node* n = node(id)) {
            x0 = std::min(x0, n->pos.x); x1 = std::max(x1, n->pos.x); y0 = std::min(y0, n->pos.y); cy += n->pos.y;
            ++count;
        }
    if (!count) return 0;
    cy /= count;
    int cid = addCompound(float2((x0 + x1) * 0.5f, cy), name);
    std::shared_ptr<Graph> sub = node(cid)->sub;
    sub->nodes.clear();
    sub->nextId = 1;
    int inId = sub->addNode("compound.input", float2(x0 - 320, cy));
    int outId = sub->addNode("compound.output", float2(x1 + 300, cy));
    // the nodes move inside (same positions), with new ids
    std::map<int, int> remap;
    for (int id : ids)
        if (const Node* n = node(id)) {
            if (n->type == "compound.input" || n->type == "compound.output") continue;
            Node c = *n;
            c.id = sub->nextId++;
            remap[id] = c.id;
            sub->nodes.push_back(c);
        }
    sub->refreshDefs();
    std::vector<Link> inner, incoming, outgoing;
    for (const Link& l : links) {
        bool a = inSet.count(l.fromNode) > 0, b = inSet.count(l.toNode) > 0;
        if (a && b) inner.push_back(l);
        else if (b) incoming.push_back(l);
        else if (a) outgoing.push_back(l);
    }
    for (const Link& l : inner) sub->links.push_back({remap[l.fromNode], l.fromPin, remap[l.toNode], l.toPin});
    // a link entering the selection promotes the inner input; one promoted output per inner output used outside
    std::vector<std::pair<Link, int>> parentLinks;   // parent link (compound side filled in) and the promoted pin
    for (const Link& l : incoming) {
        int plus = (int)sub->io->ins.size();
        if (sub->connect(inId, plus, remap[l.toNode], l.toPin)) parentLinks.push_back({{l.fromNode, l.fromPin, cid, (int)sub->io->ins.size() - 1}, 0});
    }
    std::map<std::pair<int, int>, int> promotedOut;
    for (const Link& l : outgoing) {
        auto key = std::make_pair(l.fromNode, l.fromPin);
        if (!promotedOut.count(key)) {
            int plus = (int)sub->io->outs.size();
            if (!sub->connect(remap[l.fromNode], l.fromPin, outId, plus)) continue;
            promotedOut[key] = (int)sub->io->outs.size() - 1;
        }
        parentLinks.push_back({{cid, promotedOut[key], l.toNode, l.toPin}, 0});
    }
    for (int id : ids) removeNode(id);
    refreshDefs();
    for (auto& pl : parentLinks) connect(pl.first.fromNode, pl.first.fromPin, pl.first.toNode, pl.first.toPin);
    ++version;
    return cid;
}

// --------------------------------------------------------------------------- validation

std::vector<Diagnostic> Graph::validate() const {
    std::vector<Diagnostic> out;
    // exec links: an exec output leads to the node wired to it
    std::map<int, std::vector<int>> next;
    std::set<int> hasExecLink;
    for (const Link& l : links) {
        const Node* a = node(l.fromNode);
        const Node* b = node(l.toNode);
        if (!a || !b) continue;
        if (a->def->outs[l.fromPin].type != PinType::Exec) continue;
        next[a->id].push_back(b->id);
        hasExecLink.insert(a->id);
    }
    // walk the flow from every event; reaching a node again on the same walk is a loop
    std::set<int> reached, onWalk, looped;
    std::function<void(int)> walk = [&](int id) {
        if (onWalk.count(id)) {
            if (looped.insert(id).second) out.push_back({2, "Execution loop: this node leads back to itself", id});
            return;
        }
        if (!reached.insert(id).second) return;
        onWalk.insert(id);
        for (int nx : next[id]) walk(nx);
        onWalk.erase(id);
    };
    bool anyEvent = false;
    for (const Node& n : nodes) {
        if (!n.def->event) continue;
        anyEvent = true;
        walk(n.id);
    }
    if (!anyEvent) out.push_back({1, "No event node: nothing runs on Play", 0});
    for (const Node& n : nodes) {
        if (n.def->event) {
            bool hasExecOut = !n.def->outs.empty() && n.def->outs[0].type == PinType::Exec;
            if (hasExecOut && !hasExecLink.count(n.id)) out.push_back({1, n.def->label + ": nothing is connected after this event", n.id});
            continue;
        }
        bool hasExecIn = !n.def->ins.empty() && n.def->ins[0].type == PinType::Exec;
        if (hasExecIn && !reached.count(n.id)) out.push_back({1, n.def->label + ": not reached from an event", n.id});
        for (size_t i = 0; i < n.def->ins.size(); ++i)
            if (n.def->ins[i].type == PinType::Object && !inputLink(n.id, (int)i))
                out.push_back({1, n.def->label + ": the object input is not connected", n.id});
    }
    return out;
}

} // namespace game
