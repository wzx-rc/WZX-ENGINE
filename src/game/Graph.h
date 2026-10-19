// game - node graph (Blueprint style) describing the game interactions.
// The graph is pure data: node types come from a registry (NodeLib.cpp), the
// interpreter lives in Runtime, the editor view in NodeEditor.
#pragma once
#include "vp3d/Math.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace app { struct Json; }

namespace game {

using vp3d::float2;
using vp3d::float3;

enum class PinType { Exec, Float, Bool, Vec3, Object, String };

struct Value {
    PinType type = PinType::Float;
    float f = 0;
    bool b = false;
    float3 v;
    int obj = -1;          // scene object index (-1 = none)
    std::string s;

    static Value Float(float x) { Value r; r.type = PinType::Float; r.f = x; return r; }
    static Value Bool(bool x) { Value r; r.type = PinType::Bool; r.b = x; return r; }
    static Value Vec3(float3 x) { Value r; r.type = PinType::Vec3; r.v = x; return r; }
    static Value Obj(int i) { Value r; r.type = PinType::Object; r.obj = i; return r; }
    static Value Str(const std::string& x) { Value r; r.type = PinType::String; r.s = x; return r; }
    static Value None(PinType t) { Value r; r.type = t; return r; }

    // Conversions used when a pin of another type is connected (Float <-> Bool, etc.).
    float asFloat() const;
    bool asBool() const;
    float3 asVec3() const;
    std::string asString() const;
};

struct PinDef {
    std::string name;
    PinType type;
    int parent = -1;         // channel pin (R, G, B / X, Y, Z) of the Vec3 pin at this index (Maya style expansion)
    int channel = 0;         // 0, 1, 2 for a channel pin
    bool wildcard = false;   // "+" pin of a compound Input / Output node: connecting it promotes a new pin
};

// Operation implemented by the runtime for each node type.
enum class Op {
    // events
    OnStart, OnTick, OnClickObject, OnClickGround, OnKey, OnCustom,
    // flow
    Branch, Sequence, Delay, ForEachPart, DoOnce, FireCustom,
    // actions
    SetPosition, MoveTo, RotateY, Spawn, Destroy, SetColor, SetScale, Select, Print, SetVar, AddVar,
    // data
    ConstFloat, ConstBool, ConstVec3, ConstString, GetVar, Selected, FindObject, GetPosition, PartOf, IsValid,
    MakeVec3, BreakVec3, Distance, Time,
    // maths / logic
    Add, Sub, Mul, Div, Greater, Less, Equal, StrEqual, And, Or, Not, Random, AddVec3, ScaleVec3, Concat,
    // RTS
    SnapGrid, CellFree, CountPart, SetRtsCamera, PlaceHouse, RemoveHouse, TurnHouse, HouseAt, HouseCount, OnHouseClicked, CellAt, SetSun, HudButton, OnHudButton,
};

struct NodeDef {
    Op op = Op::OnStart;
    std::string id;          // stable identifier, e.g. "event.click"
    std::string label;       // shown in the editor (UTF-8)
    std::string category;    // Événements, Flux, Actions, Données, Maths, RTS
    std::string help;        // one-line description (editor tooltip, API listing)
    std::vector<PinDef> ins, outs;
    std::vector<Value> defaults;   // one per input (ignored for Exec pins)
    bool pure = false;       // no exec pins: evaluated on demand when an output is read
    bool event = false;      // entry point fired by the runtime
    int findIn(const std::string& n) const;
    int findOut(const std::string& n) const;
};

const std::vector<NodeDef>& nodeDefs();
const NodeDef* findNodeDef(const std::string& id);
const char* pinTypeName(PinType t);

class Graph;

// Pins promoted on a compound: the inputs / outputs of the compound node, i.e. the outputs of its Input node and the
// inputs of its Output node. Shared by the compound node and its inner graph.
struct CompoundIO {
    std::string name = "Compound";
    std::vector<PinDef> ins, outs;
    std::vector<Value> defaults;   // value of each input when the compound input is not connected
    uint32_t version = 1;
};

struct Node {
    int id = 0;
    std::string type;
    float2 pos;                      // editor position (graph units)
    std::vector<Value> inputs;       // inline values of unconnected inputs
    const NodeDef* def = nullptr;    // resolved from type
    // compound (LookDevX): its inner graph; compound, Input and Output nodes have pins of their own (ownDef)
    std::shared_ptr<Graph> sub;
    std::shared_ptr<NodeDef> ownDef;
    uint32_t defVersion = 0;
};

// A labelled frame around nodes (Atelier "group"): moving it moves the nodes inside.
struct Group {
    std::string title = "GROUP";
    float x = 0, y = 0, w = 400, h = 240;     // graph units (the title bar is the top 25)
    std::string category = "Flow Control";    // colour of the frame
};

// Result of Graph::validate(): level 0 info, 1 warning, 2 error.
struct Diagnostic {
    int level = 0;
    std::string text;
    int node = 0;                             // node concerned (0 = whole graph)
};

struct Link {
    int fromNode = 0, fromPin = 0;   // output pin index
    int toNode = 0, toPin = 0;       // input pin index
};

class Graph {
public:
    std::string name = "main";
    std::vector<Node> nodes;
    std::vector<Link> links;
    std::vector<Group> groups;
    int nextId = 1;
    uint32_t version = 1;            // bumped on every structural edit
    std::shared_ptr<CompoundIO> io;  // inner graph of a compound: its promoted pins (null at the root)

    Node* node(int id);
    const Node* node(int id) const;
    // Returns the new node id, or 0 if the type is unknown.
    int addNode(const std::string& type, float2 pos);
    void removeNode(int id);
    // Connects an output to an input. Replaces the previous link of a data input / exec output.
    // Returns false (and sets err) if the pins are incompatible.
    bool connect(int fromNode, int fromPin, int toNode, int toPin, std::string* err = nullptr);
    void disconnectPin(int nodeId, int pin, bool output);
    void removeLink(const Link& l);   // this link only: a flow input can take several links
    const Link* inputLink(int nodeId, int pin) const;
    static bool compatible(PinType out, PinType in);
    void clear();
    // Checks the execution flow: cycles, events without action, actions no event reaches, unconnected objects.
    std::vector<Diagnostic> validate() const;

    // Compounds (LookDevX). refreshDefs rebuilds the pins of the compound / Input / Output nodes (and the defs of new
    // nodes), recursively; call it after loading or editing. addCompound creates an empty compound (Input and Output
    // inside); collapse moves nodes into a new compound, promoting the links that cross its border.
    void refreshDefs();
    int addCompound(float2 pos, const std::string& name = "Compound");
    int collapse(const std::vector<int>& ids, const std::string& name = "Compound");
    std::shared_ptr<Graph> cloneSub(const Node& compound) const;

    std::string toJson() const;
    bool fromJson(const std::string& text, std::string* err = nullptr);
    bool fromJsonValue(const app::Json& j, std::shared_ptr<CompoundIO> io, std::string* err);
    bool save(const std::wstring& path) const;
    bool load(const std::wstring& path, std::string* err = nullptr);
};

// Small helpers shared by the game module.
std::string valueToJson(const Value& v);

} // namespace game
