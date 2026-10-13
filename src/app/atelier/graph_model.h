#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// Windows multimedia headers expose a PlaySound macro, which would otherwise
// rewrite the public node type name in consumers of this header.
#ifdef PlaySound
#undef PlaySound
#endif

namespace atelier::graph {

using Id = std::uint32_t;

enum class Category { Event, Input, Flow, Math, Scene, Rendering, Audio, Variables };
enum class PinType { Exec, Float, Bool, Vector, Entity, Material, String };
enum class NodeType {
    BeginPlay, Tick, KeyInput, Branch, Delay, Sequence,
    FloatValue, BoolValue, VectorValue, Add, Multiply, Greater, Lerp,
    GetActor, SetPosition, RotateActor, SetVisibility, SetMaterial,
    PrintString, PlaySound, SpawnEffect, GetVariable, SetVariable,
    StringValue, MaterialValue
};

struct PinDef { const char* name; PinType type; };
struct NodeSpec {
    NodeType type;
    Category category;
    const char* title;
    const char* description;
    std::vector<PinDef> inputs;
    std::vector<PinDef> outputs;
};

const NodeSpec& GetSpec(NodeType type);
const std::vector<NodeType>& AllNodeTypes();
const char* CategoryName(Category category);
const char* PinTypeName(PinType type);

struct Node {
    Id id = 0;
    NodeType type = NodeType::BeginPlay;
    float x = 0.0f;
    float y = 0.0f;
    std::string title;
    std::string text;
    std::array<float, 4> values{0.0f, 0.0f, 0.0f, 0.0f};
    bool boolValue = true;
    bool enabled = true;
};

struct Connection {
    Id id = 0;
    Id fromNode = 0;
    int fromPin = 0;
    Id toNode = 0;
    int toPin = 0;
};

enum class Severity { Info, Warning, Error };
struct Diagnostic {
    Severity severity = Severity::Info;
    Id node = 0;
    std::string message;
};

struct RunResult {
    bool success = false;
    int steps = 0;
    std::vector<std::string> logs;
    std::vector<Id> executedNodes;
    std::array<float, 3> actorPosition{0.0f, 0.0f, 0.0f};
    float actorYaw = 0.0f;
    bool actorVisible = true;
    std::string actorMaterial = "M_Stone_Ruins";
    std::vector<std::string> effects;
};

class Graph {
public:
    const std::vector<Node>& Nodes() const { return nodes_; }
    const std::vector<Connection>& Connections() const { return connections_; }
    Node* FindNode(Id id);
    const Node* FindNode(Id id) const;
    const Connection* FindConnection(Id id) const;

    Id AddNode(NodeType type, float x, float y);
    bool RemoveNode(Id id);
    Id DuplicateNode(Id id, float dx = 40.0f, float dy = 40.0f);
    bool CanConnect(Id fromNode, int fromPin, Id toNode, int toPin,
                    std::string* error = nullptr) const;
    Id Connect(Id fromNode, int fromPin, Id toNode, int toPin,
               std::string* error = nullptr);
    bool Disconnect(Id connectionId);

    // Begin/End coalesce direct edits to nodes and regular graph mutations into
    // one undo operation. Never retain a Node* across add/remove/undo/load.
    void BeginEdit();
    bool EndEdit();
    void CancelEdit();
    bool Undo();
    bool Redo();
    bool CanUndo() const { return !undo_.empty(); }
    bool CanRedo() const { return !redo_.empty(); }
    void ClearHistory();

    void Clear();
    void SeedDemo();
    bool Save(const std::string& path, std::string* error = nullptr) const;
    bool Load(const std::string& path, std::string* error = nullptr);
    std::vector<Diagnostic> Validate() const;
    RunResult RunDemo(int maxSteps = 256) const;

private:
    struct Snapshot {
        std::vector<Node> nodes;
        std::vector<Connection> connections;
        Id nextId = 1;
    };
    Snapshot Capture() const;
    void Restore(const Snapshot& snapshot);
    void Record(const Snapshot& before);
    std::vector<Node> nodes_;
    std::vector<Connection> connections_;
    Id nextId_ = 1;
    std::vector<Snapshot> undo_;
    std::vector<Snapshot> redo_;
    bool editing_ = false;
    Snapshot editBefore_;
};

// Writes a versioned graph fixture in directory and a machine-readable report.
bool RunModelTests(const std::string& directory, const std::string& reportPath);

} // namespace atelier::graph
