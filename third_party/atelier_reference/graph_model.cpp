#include "graph_model.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef PlaySound
#undef PlaySound
#endif
#endif

namespace atelier::graph {
namespace {

constexpr std::size_t HistoryLimit = 128;
constexpr std::size_t NodeLimit = 20000;
constexpr std::size_t ConnectionLimit = 100000;
constexpr std::size_t StringLimit = 1024 * 1024;

const std::vector<NodeSpec>& Specs() {
    static const std::vector<NodeSpec> specs = {
        {NodeType::BeginPlay, Category::Event, "Begin Play", "Runs once when the scene starts.",
            {}, {{"Out", PinType::Exec}}},
        {NodeType::Tick, Category::Event, "Event Tick", "Runs every frame in the host engine.",
            {}, {{"Out", PinType::Exec}, {"Delta Time", PinType::Float}}},
        {NodeType::KeyInput, Category::Input, "Keyboard Input", "Triggers when the configured key is pressed.",
            {}, {{"Pressed", PinType::Exec}, {"Is Down", PinType::Bool}}},
        {NodeType::Branch, Category::Flow, "Branch", "Selects one execution path from a boolean condition.",
            {{"In", PinType::Exec}, {"Condition", PinType::Bool}}, {{"True", PinType::Exec}, {"False", PinType::Exec}}},
        {NodeType::Delay, Category::Flow, "Delay", "Schedules the next action after a duration.",
            {{"In", PinType::Exec}, {"Duration", PinType::Float}}, {{"Completed", PinType::Exec}}},
        {NodeType::Sequence, Category::Flow, "Sequence", "Executes Then 0 followed by Then 1.",
            {{"In", PinType::Exec}}, {{"Then 0", PinType::Exec}, {"Then 1", PinType::Exec}}},
        {NodeType::FloatValue, Category::Math, "Float", "A floating-point constant.",
            {}, {{"Value", PinType::Float}}},
        {NodeType::BoolValue, Category::Math, "Boolean", "A boolean constant.",
            {}, {{"Value", PinType::Bool}}},
        {NodeType::VectorValue, Category::Math, "Vector 3", "A three-dimensional vector.",
            {}, {{"Vector", PinType::Vector}}},
        {NodeType::Add, Category::Math, "Add", "Adds two floating-point values.",
            {{"A", PinType::Float}, {"B", PinType::Float}}, {{"Result", PinType::Float}}},
        {NodeType::Multiply, Category::Math, "Multiply", "Multiplies two floating-point values.",
            {{"A", PinType::Float}, {"B", PinType::Float}}, {{"Result", PinType::Float}}},
        {NodeType::Greater, Category::Math, "Greater Than", "Tests whether A is greater than B.",
            {{"A", PinType::Float}, {"B", PinType::Float}}, {{"Result", PinType::Bool}}},
        {NodeType::Lerp, Category::Math, "Lerp", "Interpolates A to B using Alpha.",
            {{"A", PinType::Float}, {"B", PinType::Float}, {"Alpha", PinType::Float}}, {{"Value", PinType::Float}}},
        {NodeType::GetActor, Category::Scene, "Get Actor", "Finds a named actor in the scene.",
            {}, {{"Actor", PinType::Entity}, {"Position", PinType::Vector}}},
        {NodeType::SetPosition, Category::Scene, "Set Position", "Moves the target actor to a world position.",
            {{"In", PinType::Exec}, {"Target", PinType::Entity}, {"Position", PinType::Vector}}, {{"Then", PinType::Exec}}},
        {NodeType::RotateActor, Category::Scene, "Rotate Actor", "Adds a rotation around the actor's Y axis.",
            {{"In", PinType::Exec}, {"Target", PinType::Entity}, {"Yaw", PinType::Float}}, {{"Then", PinType::Exec}}},
        {NodeType::SetVisibility, Category::Rendering, "Set Visibility", "Changes the actor's visibility.",
            {{"In", PinType::Exec}, {"Target", PinType::Entity}, {"Visible", PinType::Bool}}, {{"Then", PinType::Exec}}},
        {NodeType::SetMaterial, Category::Rendering, "Set Material", "Assigns a material to the target actor.",
            {{"In", PinType::Exec}, {"Target", PinType::Entity}, {"Material", PinType::Material}}, {{"Then", PinType::Exec}}},
        {NodeType::PrintString, Category::Flow, "Print String", "Writes a message to the output log.",
            {{"In", PinType::Exec}, {"Message", PinType::String}}, {{"Then", PinType::Exec}}},
        {NodeType::PlaySound, Category::Audio, "Play Sound", "Emits a named audio cue at the target actor.",
            {{"In", PinType::Exec}, {"Target", PinType::Entity}, {"Sound", PinType::String}}, {{"Then", PinType::Exec}}},
        {NodeType::SpawnEffect, Category::Rendering, "Spawn Effect", "Emits a named visual effect at the target actor.",
            {{"In", PinType::Exec}, {"Target", PinType::Entity}, {"Effect", PinType::String}}, {{"Then", PinType::Exec}}},
        {NodeType::GetVariable, Category::Variables, "Get Variable", "Reads a named floating-point variable.",
            {}, {{"Value", PinType::Float}}},
        {NodeType::SetVariable, Category::Variables, "Set Variable", "Stores a named floating-point variable.",
            {{"In", PinType::Exec}, {"Value", PinType::Float}}, {{"Then", PinType::Exec}, {"Value", PinType::Float}}},
        {NodeType::StringValue, Category::Variables, "String", "A text constant.",
            {}, {{"Value", PinType::String}}},
        {NodeType::MaterialValue, Category::Rendering, "Material", "A named material asset.",
            {}, {{"Material", PinType::Material}}}
    };
    return specs;
}

bool ValidType(NodeType type) {
    const auto numeric = static_cast<int>(type);
    return numeric >= 0 && numeric < static_cast<int>(Specs().size());
}

void SetError(std::string* error, const std::string& value) {
    if (error) *error = value;
}

bool SameNode(const Node& a, const Node& b) {
    return a.id == b.id && a.type == b.type && a.x == b.x && a.y == b.y &&
           a.title == b.title && a.text == b.text && a.values == b.values &&
           a.boolValue == b.boolValue && a.enabled == b.enabled;
}

bool SameConnection(const Connection& a, const Connection& b) {
    return a.id == b.id && a.fromNode == b.fromNode && a.fromPin == b.fromPin &&
           a.toNode == b.toNode && a.toPin == b.toPin;
}

Node MakeNode(Id id, NodeType type, float x, float y) {
    Node node;
    node.id = id;
    node.type = type;
    node.x = x;
    node.y = y;
    node.title = GetSpec(type).title;
    switch (type) {
    case NodeType::FloatValue: node.values[0] = 0.75f; break;
    case NodeType::Tick: node.values[0] = 1.0f / 60.0f; break;
    case NodeType::VectorValue:
    case NodeType::SetPosition: node.values = {12.8f, 5.12f, 2.3f, 0.0f}; break;
    case NodeType::Add: node.values = {0.25f, 0.5f, 0.0f, 0.0f}; break;
    case NodeType::Multiply: node.values = {45.0f, 0.75f, 0.0f, 0.0f}; break;
    case NodeType::Greater: node.values = {0.75f, 0.5f, 0.0f, 0.0f}; break;
    case NodeType::Lerp: node.values = {0.0f, 1.0f, 0.5f, 0.0f}; break;
    case NodeType::RotateActor: node.values[0] = 45.0f; break;
    case NodeType::Delay: node.values[0] = 1.0f; break;
    case NodeType::KeyInput: node.text = "Space"; break;
    case NodeType::GetActor: node.text = "Archway"; break;
    case NodeType::PlaySound: node.text = "Ancient_Wind"; break;
    case NodeType::SpawnEffect: node.text = "Dust_Motes"; break;
    case NodeType::PrintString:
    case NodeType::StringValue: node.text = "Archway ready"; break;
    case NodeType::SetMaterial:
    case NodeType::MaterialValue: node.text = "M_Stone_Ruins"; break;
    case NodeType::GetVariable:
    case NodeType::SetVariable: node.text = "RotationSpeed"; node.values[0] = 0.75f; break;
    default: break;
    }
    return node;
}

std::string Number(float value) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(2) << value;
    return out.str();
}

std::string JsonEscape(const std::string& value) {
    std::ostringstream out;
    for (unsigned char c : value) {
        switch (c) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c) << std::dec;
            else out << c;
        }
    }
    return out.str();
}

} // namespace

const NodeSpec& GetSpec(NodeType type) {
    if (!ValidType(type)) throw std::out_of_range("Unknown graph node type");
    return Specs()[static_cast<std::size_t>(type)];
}

const std::vector<NodeType>& AllNodeTypes() {
    static const std::vector<NodeType> types = [] {
        std::vector<NodeType> result;
        for (const auto& spec : Specs()) result.push_back(spec.type);
        return result;
    }();
    return types;
}

const char* CategoryName(Category category) {
    switch (category) {
    case Category::Event: return "Events";
    case Category::Input: return "Input";
    case Category::Flow: return "Flow Control";
    case Category::Math: return "Math";
    case Category::Scene: return "Scene";
    case Category::Rendering: return "Rendering";
    case Category::Audio: return "Audio";
    case Category::Variables: return "Variables";
    }
    return "Unknown";
}

const char* PinTypeName(PinType type) {
    switch (type) {
    case PinType::Exec: return "Execution";
    case PinType::Float: return "Float";
    case PinType::Bool: return "Boolean";
    case PinType::Vector: return "Vector 3";
    case PinType::Entity: return "Entity";
    case PinType::Material: return "Material";
    case PinType::String: return "String";
    }
    return "Unknown";
}

Node* Graph::FindNode(Id id) {
    auto found = std::find_if(nodes_.begin(), nodes_.end(), [id](const Node& n) { return n.id == id; });
    return found == nodes_.end() ? nullptr : &*found;
}

const Node* Graph::FindNode(Id id) const {
    auto found = std::find_if(nodes_.begin(), nodes_.end(), [id](const Node& n) { return n.id == id; });
    return found == nodes_.end() ? nullptr : &*found;
}

const Connection* Graph::FindConnection(Id id) const {
    auto found = std::find_if(connections_.begin(), connections_.end(), [id](const Connection& c) { return c.id == id; });
    return found == connections_.end() ? nullptr : &*found;
}

Graph::Snapshot Graph::Capture() const { return {nodes_, connections_, nextId_}; }

void Graph::Restore(const Snapshot& snapshot) {
    nodes_ = snapshot.nodes;
    connections_ = snapshot.connections;
    // Identity allocation never rewinds, including undo/cancel. New objects
    // cannot silently reuse an ID still selected or referenced by a host.
    nextId_ = std::max(nextId_, snapshot.nextId);
}

void Graph::Record(const Snapshot& before) {
    if (editing_) return;
    undo_.push_back(before);
    if (undo_.size() > HistoryLimit) undo_.erase(undo_.begin());
    redo_.clear();
}

Id Graph::AddNode(NodeType type, float x, float y) {
    if (!ValidType(type) || !std::isfinite(x) || !std::isfinite(y) ||
        nodes_.size() >= NodeLimit || nextId_ == std::numeric_limits<Id>::max()) return 0;
    auto before = Capture();
    const Id id = nextId_++;
    nodes_.push_back(MakeNode(id, type, x, y));
    Record(before);
    return id;
}

bool Graph::RemoveNode(Id id) {
    if (!FindNode(id)) return false;
    auto before = Capture();
    nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(), [id](const Node& n) { return n.id == id; }), nodes_.end());
    connections_.erase(std::remove_if(connections_.begin(), connections_.end(), [id](const Connection& c) {
        return c.fromNode == id || c.toNode == id;
    }), connections_.end());
    Record(before);
    return true;
}

Id Graph::DuplicateNode(Id id, float dx, float dy) {
    const auto* node = FindNode(id);
    if (!node || !std::isfinite(dx) || !std::isfinite(dy) || nodes_.size() >= NodeLimit ||
        nextId_ == std::numeric_limits<Id>::max()) return 0;
    Node copy = *node;
    copy.x += dx;
    copy.y += dy;
    if (!std::isfinite(copy.x) || !std::isfinite(copy.y)) return 0;
    auto before = Capture();
    copy.id = nextId_++;
    nodes_.push_back(copy);
    Record(before);
    return copy.id;
}

bool Graph::CanConnect(Id fromNode, int fromPin, Id toNode, int toPin, std::string* error) const {
    SetError(error, "");
    const Node* source = FindNode(fromNode);
    const Node* target = FindNode(toNode);
    if (!source || !target) { SetError(error, "The source or destination node does not exist."); return false; }
    if (source->id == target->id) { SetError(error, "A node cannot connect to itself."); return false; }
    if (!ValidType(source->type) || !ValidType(target->type)) {
        SetError(error, "The source or destination node has an invalid type."); return false;
    }
    const auto& outputs = GetSpec(source->type).outputs;
    const auto& inputs = GetSpec(target->type).inputs;
    if (fromPin < 0 || static_cast<std::size_t>(fromPin) >= outputs.size() ||
        toPin < 0 || static_cast<std::size_t>(toPin) >= inputs.size()) {
        SetError(error, "Connect an output pin to an existing input pin."); return false;
    }
    if (outputs[fromPin].type != inputs[toPin].type) {
        SetError(error, std::string("Cannot connect ") + PinTypeName(outputs[fromPin].type) +
                        " to " + PinTypeName(inputs[toPin].type) + ".");
        return false;
    }
    for (const auto& connection : connections_) {
        if (connection.toNode == toNode && connection.toPin == toPin) {
            SetError(error, "This input already has a connection. Disconnect it first."); return false;
        }
        if (outputs[fromPin].type == PinType::Exec && connection.fromNode == fromNode && connection.fromPin == fromPin) {
            SetError(error, "Execution outputs have one destination. Use Sequence to fork execution."); return false;
        }
    }
    return true;
}

Id Graph::Connect(Id fromNode, int fromPin, Id toNode, int toPin, std::string* error) {
    if (!CanConnect(fromNode, fromPin, toNode, toPin, error)) return 0;
    if (connections_.size() >= ConnectionLimit || nextId_ == std::numeric_limits<Id>::max()) {
        SetError(error, "The graph object limit has been reached."); return 0;
    }
    auto before = Capture();
    const Id id = nextId_++;
    connections_.push_back({id, fromNode, fromPin, toNode, toPin});
    Record(before);
    return id;
}

bool Graph::Disconnect(Id connectionId) {
    if (!FindConnection(connectionId)) return false;
    auto before = Capture();
    connections_.erase(std::remove_if(connections_.begin(), connections_.end(), [connectionId](const Connection& c) {
        return c.id == connectionId;
    }), connections_.end());
    Record(before);
    return true;
}

void Graph::BeginEdit() {
    if (editing_) return;
    editBefore_ = Capture();
    editing_ = true;
}

bool Graph::EndEdit() {
    if (!editing_) return false;
    editing_ = false;
    const bool sameNodes = nodes_.size() == editBefore_.nodes.size() &&
        std::equal(nodes_.begin(), nodes_.end(), editBefore_.nodes.begin(), SameNode);
    const bool sameConnections = connections_.size() == editBefore_.connections.size() &&
        std::equal(connections_.begin(), connections_.end(), editBefore_.connections.begin(), SameConnection);
    if (sameNodes && sameConnections) { editBefore_ = {}; return false; }
    Record(editBefore_);
    editBefore_ = {};
    return true;
}

void Graph::CancelEdit() {
    if (!editing_) return;
    Restore(editBefore_);
    editBefore_ = {};
    editing_ = false;
}

bool Graph::Undo() {
    if (editing_) EndEdit();
    if (undo_.empty()) return false;
    redo_.push_back(Capture());
    Restore(undo_.back());
    undo_.pop_back();
    return true;
}

bool Graph::Redo() {
    if (editing_) EndEdit();
    if (redo_.empty()) return false;
    undo_.push_back(Capture());
    Restore(redo_.back());
    redo_.pop_back();
    return true;
}

void Graph::ClearHistory() {
    editing_ = false;
    editBefore_ = {};
    undo_.clear();
    redo_.clear();
}

void Graph::Clear() {
    if (nodes_.empty() && connections_.empty()) return;
    auto before = Capture();
    nodes_.clear();
    connections_.clear();
    Record(before);
}

void Graph::SeedDemo() {
    ClearHistory();
    nodes_.clear();
    connections_.clear();
    BeginEdit();
    const Id begin = AddNode(NodeType::BeginPlay, 40.0f, 50.0f);
    const Id position = AddNode(NodeType::SetPosition, 310.0f, 50.0f);
    const Id branch = AddNode(NodeType::Branch, 580.0f, 50.0f);
    const Id rotate = AddNode(NodeType::RotateActor, 850.0f, 50.0f);
    const Id sound = AddNode(NodeType::PlaySound, 850.0f, 340.0f);
    const Id print = AddNode(NodeType::PrintString, 580.0f, 340.0f);
    const Id actor = AddNode(NodeType::GetActor, 40.0f, 270.0f);
    const Id vector = AddNode(NodeType::VectorValue, 310.0f, 320.0f);
    const Id variable = AddNode(NodeType::GetVariable, 40.0f, 490.0f);
    const Id greater = AddNode(NodeType::Greater, 580.0f, 540.0f);
    const Id multiply = AddNode(NodeType::Multiply, 850.0f, 540.0f);
    const Id effect = AddNode(NodeType::SpawnEffect, 310.0f, 550.0f);
    FindNode(greater)->values[1] = 0.5f;
    FindNode(multiply)->values[0] = 45.0f;
    Connect(begin, 0, position, 0);
    Connect(position, 0, branch, 0);
    Connect(branch, 0, rotate, 0);
    Connect(branch, 1, effect, 0);
    Connect(rotate, 0, sound, 0);
    Connect(sound, 0, print, 0);
    Connect(actor, 0, position, 1);
    Connect(actor, 0, rotate, 1);
    Connect(actor, 0, sound, 1);
    Connect(actor, 0, effect, 1);
    Connect(vector, 0, position, 2);
    Connect(variable, 0, greater, 0);
    Connect(variable, 0, multiply, 1);
    Connect(greater, 0, branch, 1);
    Connect(multiply, 0, rotate, 2);
    EndEdit();
    ClearHistory();
}

bool Graph::Save(const std::string& path, std::string* error) const {
    SetError(error, "");
    for (const auto& diagnostic : Validate()) {
        // Semantic cycles are still editable and saveable; invalid records are not.
        if (diagnostic.severity == Severity::Error && diagnostic.message.find("cycle") == std::string::npos) {
            SetError(error, diagnostic.message); return false;
        }
    }
    std::filesystem::path temporaryPath;
    auto cleanup = [&] {
        if (!temporaryPath.empty()) {
            std::error_code ignored;
            std::filesystem::remove(temporaryPath, ignored);
        }
    };
    try {
        const auto destination = std::filesystem::u8path(path);
        temporaryPath = destination;
        temporaryPath += ".writing-" + std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());
        std::ofstream out(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!out) { SetError(error, "Cannot open the graph file for writing."); return false; }
        out.imbue(std::locale::classic());
        out << "ATELIER_GRAPH 1\n" << std::setprecision(std::numeric_limits<float>::max_digits10);
        out << "NODES " << nodes_.size() << '\n';
        for (const auto& n : nodes_) {
            out << "NODE " << n.id << ' ' << static_cast<int>(n.type) << ' ' << n.x << ' ' << n.y << ' '
                << static_cast<int>(n.enabled) << ' ' << static_cast<int>(n.boolValue);
            for (float value : n.values) out << ' ' << value;
            out << ' ' << std::quoted(n.title) << ' ' << std::quoted(n.text) << '\n';
        }
        out << "CONNECTIONS " << connections_.size() << '\n';
        for (const auto& c : connections_) out << "LINK " << c.id << ' ' << c.fromNode << ' ' << c.fromPin << ' ' << c.toNode << ' ' << c.toPin << '\n';
        out << "END\n";
        out.flush();
        if (!out) { out.close(); cleanup(); SetError(error, "The graph file could not be completely written."); return false; }
        out.close();
        if (!out) { cleanup(); SetError(error, "The graph file could not be closed after writing."); return false; }
#ifdef _WIN32
        if (!MoveFileExW(temporaryPath.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            const auto code = GetLastError();
            cleanup();
            SetError(error, "Cannot replace the graph file (Windows error " + std::to_string(code) + "). The previous file is preserved.");
            return false;
        }
#else
        std::filesystem::rename(temporaryPath, destination);
#endif
        return true;
    } catch (const std::exception& exception) {
        cleanup(); SetError(error, exception.what()); return false;
    }
}

bool Graph::Load(const std::string& path, std::string* error) {
    SetError(error, "");
    try {
        const auto filePath = std::filesystem::u8path(path);
        if (std::filesystem::file_size(filePath) > 64 * 1024 * 1024) {
            SetError(error, "The graph file exceeds the 64 MB limit."); return false;
        }
        std::ifstream in(filePath, std::ios::binary);
        if (!in) { SetError(error, "Cannot open the graph file for reading."); return false; }
        in.imbue(std::locale::classic());
        std::string tag;
        int version = 0;
        if (!(in >> tag >> version) || tag != "ATELIER_GRAPH" || version != 1) {
            SetError(error, "Unsupported graph format. Expected ATELIER_GRAPH version 1."); return false;
        }
        Graph parsed;
        std::size_t count = 0;
        if (!(in >> tag >> count) || tag != "NODES" || count > NodeLimit) {
            SetError(error, "Invalid node count."); return false;
        }
        std::unordered_set<Id> ids;
        Id maximum = 0;
        for (std::size_t i = 0; i < count; ++i) {
            Node n;
            int type = 0, enabled = 0, boolean = 0;
            long long rawId = 0;
            if (!(in >> tag >> rawId >> type >> n.x >> n.y >> enabled >> boolean) || tag != "NODE" ||
                rawId <= 0 || rawId >= std::numeric_limits<Id>::max() ||
                !ValidType(static_cast<NodeType>(type)) || !std::isfinite(n.x) || !std::isfinite(n.y) ||
                (enabled != 0 && enabled != 1) || (boolean != 0 && boolean != 1)) {
                SetError(error, "Invalid node record at index " + std::to_string(i) + "."); return false;
            }
            n.id = static_cast<Id>(rawId);
            if (!ids.insert(n.id).second) { SetError(error, "Duplicate node ID."); return false; }
            n.type = static_cast<NodeType>(type);
            n.enabled = enabled != 0;
            n.boolValue = boolean != 0;
            for (float& value : n.values) {
                if (!(in >> value) || !std::isfinite(value)) { SetError(error, "Invalid node numeric property."); return false; }
            }
            if (!(in >> std::quoted(n.title) >> std::quoted(n.text)) || n.title.size() > StringLimit || n.text.size() > StringLimit) {
                SetError(error, "Invalid node text property."); return false;
            }
            maximum = std::max(maximum, n.id);
            parsed.nodes_.push_back(std::move(n));
        }
        if (!(in >> tag >> count) || tag != "CONNECTIONS" || count > ConnectionLimit) {
            SetError(error, "Invalid connection count."); return false;
        }
        for (std::size_t i = 0; i < count; ++i) {
            Connection c;
            long long rawId = 0, rawSource = 0, rawTarget = 0;
            if (!(in >> tag >> rawId >> rawSource >> c.fromPin >> rawTarget >> c.toPin) || tag != "LINK" ||
                rawId <= 0 || rawId >= std::numeric_limits<Id>::max() ||
                rawSource <= 0 || rawSource >= std::numeric_limits<Id>::max() ||
                rawTarget <= 0 || rawTarget >= std::numeric_limits<Id>::max()) {
                SetError(error, "Invalid connection record at index " + std::to_string(i) + "."); return false;
            }
            c.id = static_cast<Id>(rawId);
            c.fromNode = static_cast<Id>(rawSource);
            c.toNode = static_cast<Id>(rawTarget);
            if (!ids.insert(c.id).second) { SetError(error, "Duplicate graph object ID."); return false; }
            std::string reason;
            if (!parsed.CanConnect(c.fromNode, c.fromPin, c.toNode, c.toPin, &reason)) {
                SetError(error, "Invalid connection: " + reason); return false;
            }
            maximum = std::max(maximum, c.id);
            parsed.connections_.push_back(c);
        }
        if (!(in >> tag) || tag != "END") { SetError(error, "Missing graph END marker."); return false; }
        if (in >> tag) { SetError(error, "Unexpected trailing graph data."); return false; }
        if (editing_) EndEdit();
        auto before = Capture();
        nodes_ = std::move(parsed.nodes_);
        connections_ = std::move(parsed.connections_);
        nextId_ = std::max(nextId_, maximum + 1);
        Record(before);
        return true;
    } catch (const std::exception& exception) {
        SetError(error, exception.what()); return false;
    }
}

std::vector<Diagnostic> Graph::Validate() const {
    std::vector<Diagnostic> result;
    std::unordered_set<Id> ids;
    std::set<std::pair<Id, int>> inputs;
    std::set<std::pair<Id, int>> execOutputs;
    std::unordered_map<Id, std::vector<Id>> execAdjacency, dataAdjacency;
    std::unordered_set<Id> referenced;
    bool hasStart = false;
    for (const auto& n : nodes_) {
        if (!n.id || !ids.insert(n.id).second) result.push_back({Severity::Error, n.id, "Duplicate or zero node ID."});
        if (!ValidType(n.type)) { result.push_back({Severity::Error, n.id, "Invalid node type."}); continue; }
        if (!std::isfinite(n.x) || !std::isfinite(n.y) ||
            !std::all_of(n.values.begin(), n.values.end(), [](float v) { return std::isfinite(v); })) {
            result.push_back({Severity::Error, n.id, "Node contains a non-finite number."});
        }
        if (n.title.size() > StringLimit || n.text.size() > StringLimit) result.push_back({Severity::Error, n.id, "Node text exceeds the size limit."});
        if ((n.type == NodeType::BeginPlay || n.type == NodeType::Tick || n.type == NodeType::KeyInput) && n.enabled) hasStart = true;
        if ((n.type == NodeType::GetVariable || n.type == NodeType::SetVariable) && n.text.empty()) {
            result.push_back({Severity::Warning, n.id, "Variable name is empty."});
        }
    }
    for (const auto& c : connections_) {
        if (!c.id || !ids.insert(c.id).second) result.push_back({Severity::Error, c.toNode, "Duplicate or zero connection ID."});
        const auto* a = FindNode(c.fromNode);
        const auto* b = FindNode(c.toNode);
        if (!a || !b || !ValidType(a->type) || !ValidType(b->type)) {
            result.push_back({Severity::Error, c.toNode, "Connection references a missing or invalid node."}); continue;
        }
        const auto& out = GetSpec(a->type).outputs;
        const auto& in = GetSpec(b->type).inputs;
        if (c.fromPin < 0 || c.toPin < 0 || static_cast<std::size_t>(c.fromPin) >= out.size() || static_cast<std::size_t>(c.toPin) >= in.size()) {
            result.push_back({Severity::Error, c.toNode, "Connection references an invalid pin."}); continue;
        }
        if (out[c.fromPin].type != in[c.toPin].type) result.push_back({Severity::Error, c.toNode, "Connection has incompatible pin types."});
        if (!inputs.insert({c.toNode, c.toPin}).second) result.push_back({Severity::Error, c.toNode, "An input has multiple connections."});
        if (out[c.fromPin].type == PinType::Exec) {
            if (!execOutputs.insert({c.fromNode, c.fromPin}).second) result.push_back({Severity::Error, c.fromNode, "An execution output has multiple destinations."});
            execAdjacency[c.fromNode].push_back(c.toNode);
        } else dataAdjacency[c.fromNode].push_back(c.toNode);
        referenced.insert(c.fromNode);
        referenced.insert(c.toNode);
    }
    auto detectCycles = [&result](const std::unordered_map<Id, std::vector<Id>>& adjacency, const char* kind) {
        // Iterative traversal remains bounded even on deliberately deep files.
        std::unordered_map<Id, int> state;
        std::set<Id> reported;
        for (const auto& entry : adjacency) {
            if (state[entry.first] != 0) continue;
            std::vector<std::pair<Id, std::size_t>> stack{{entry.first, 0}};
            state[entry.first] = 1;
            while (!stack.empty()) {
                auto& top = stack.back();
                auto found = adjacency.find(top.first);
                if (found == adjacency.end() || top.second >= found->second.size()) {
                    state[top.first] = 2;
                    stack.pop_back();
                    continue;
                }
                Id next = found->second[top.second++];
                if (state[next] == 1 && reported.insert(next).second) {
                    result.push_back({Severity::Error, next, std::string(kind) + " cycle detected. Break the loop before running."});
                } else if (state[next] == 0) {
                    state[next] = 1;
                    stack.push_back({next, 0});
                }
            }
        }
    };
    detectCycles(execAdjacency, "Execution");
    detectCycles(dataAdjacency, "Data dependency");
    if (nodes_.empty()) result.push_back({Severity::Info, 0, "The graph is empty. Add an event to begin."});
    else if (!hasStart) result.push_back({Severity::Warning, 0, "No enabled event. Add Begin Play, Event Tick, or Keyboard Input."});
    for (const auto& n : nodes_) {
        if (n.enabled && !referenced.count(n.id)) result.push_back({Severity::Info, n.id, "Node is disconnected."});
    }
    if (std::none_of(result.begin(), result.end(), [](const Diagnostic& d) { return d.severity == Severity::Error; })) {
        result.push_back({Severity::Info, 0, "Graph validation passed."});
    }
    return result;
}

RunResult Graph::RunDemo(int maxSteps) const {
    RunResult result;
    if (maxSteps <= 0) { result.logs.push_back("[Error] Execution step budget must be positive."); return result; }
    maxSteps = std::min(maxSteps, 100000);
    const auto diagnostics = Validate();
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.severity == Severity::Error) result.logs.push_back("[Error] " + diagnostic.message);
    }
    if (!result.logs.empty()) return result;

    struct Value {
        PinType type = PinType::Float;
        float number = 0.0f;
        bool boolean = false;
        std::array<float, 3> vector{0.0f, 0.0f, 0.0f};
        std::string text;
    };
    std::unordered_map<std::string, float> variables;
    for (const auto& node : nodes_) {
        if (node.type == NodeType::GetVariable) variables.emplace(node.text, node.values[0]);
    }
    std::set<std::pair<Id, int>> evaluating;
    int evaluationBudget = std::max(1024, maxSteps * 64);
    bool evaluationFailed = false;
    std::function<Value(const Node&, int)> output;
    std::function<Value(const Node&, int)> input;
    auto defaultInput = [](const Node& node, int pin) {
        Value value;
        value.type = GetSpec(node.type).inputs[pin].type;
        switch (value.type) {
        case PinType::Float:
            if (node.type == NodeType::Add || node.type == NodeType::Multiply || node.type == NodeType::Greater || node.type == NodeType::Lerp) value.number = node.values[pin];
            else value.number = node.values[0];
            break;
        case PinType::Bool: value.boolean = node.boolValue; break;
        case PinType::Vector: value.vector = {node.values[0], node.values[1], node.values[2]}; break;
        case PinType::Entity: value.text = "Archway"; break;
        case PinType::Material:
        case PinType::String: value.text = node.text; break;
        default: break;
        }
        return value;
    };
    input = [&](const Node& node, int pin) {
        for (const auto& connection : connections_) {
            if (connection.toNode == node.id && connection.toPin == pin) {
                const auto* source = FindNode(connection.fromNode);
                if (source && source->enabled) return output(*source, connection.fromPin);
            }
        }
        return defaultInput(node, pin);
    };
    output = [&](const Node& node, int pin) {
        Value value;
        value.type = GetSpec(node.type).outputs[pin].type;
        const std::pair<Id, int> key{node.id, pin};
        if (--evaluationBudget < 0 || evaluating.size() >= 512 || !evaluating.insert(key).second) {
            evaluationFailed = true;
            return value;
        }
        switch (node.type) {
        case NodeType::FloatValue: value.number = node.values[0]; break;
        case NodeType::BoolValue: value.boolean = node.boolValue; break;
        case NodeType::VectorValue: value.vector = {node.values[0], node.values[1], node.values[2]}; break;
        case NodeType::Add: value.number = input(node, 0).number + input(node, 1).number; break;
        case NodeType::Multiply: value.number = input(node, 0).number * input(node, 1).number; break;
        case NodeType::Greater: value.boolean = input(node, 0).number > input(node, 1).number; break;
        case NodeType::Lerp: {
            float a = input(node, 0).number;
            float b = input(node, 1).number;
            float alpha = input(node, 2).number;
            value.number = a + (b - a) * alpha;
            break;
        }
        case NodeType::GetActor:
            if (pin == 0) value.text = node.text.empty() ? "Archway" : node.text;
            else value.vector = result.actorPosition;
            break;
        case NodeType::GetVariable: {
            auto found = variables.find(node.text);
            value.number = found == variables.end() ? node.values[0] : found->second;
            break;
        }
        case NodeType::SetVariable: {
            auto found = variables.find(node.text);
            value.number = found == variables.end() ? node.values[0] : found->second;
            break;
        }
        case NodeType::Tick: value.number = node.values[0]; break;
        case NodeType::KeyInput: value.boolean = node.boolValue; break;
        case NodeType::StringValue:
        case NodeType::MaterialValue: value.text = node.text; break;
        default: break;
        }
        if (!std::isfinite(value.number) || !std::all_of(value.vector.begin(), value.vector.end(), [](float v) { return std::isfinite(v); })) evaluationFailed = true;
        evaluating.erase(key);
        return value;
    };
    std::vector<Id> pending;
    for (auto node = nodes_.rbegin(); node != nodes_.rend(); ++node) {
        if (node->type == NodeType::BeginPlay && node->enabled) pending.push_back(node->id);
    }
    const bool beginPlay = !pending.empty();
    if (!beginPlay) {
        for (auto node = nodes_.rbegin(); node != nodes_.rend(); ++node) {
            if ((node->type == NodeType::Tick || node->type == NodeType::KeyInput) && node->enabled) pending.push_back(node->id);
        }
    }
    if (pending.empty()) { result.logs.push_back("[Warning] Add an enabled event to run the demo."); return result; }
    result.logs.push_back(beginPlay ? "[Run] Begin Play - demo actor: Archway" : "[Run] Simulated event update - demo actor: Archway");
    auto pushNext = [&](Id nodeId, int pin) {
        for (const auto& connection : connections_) {
            if (connection.fromNode == nodeId && connection.fromPin == pin) pending.push_back(connection.toNode);
        }
    };
    while (!pending.empty()) {
        if (result.steps >= maxSteps) {
            result.logs.push_back("[Error] Execution step budget exhausted (" + std::to_string(maxSteps) + ").");
            return result;
        }
        const Id id = pending.back();
        pending.pop_back();
        const Node* node = FindNode(id);
        if (!node) { result.logs.push_back("[Error] Missing execution node."); return result; }
        ++result.steps;
        result.executedNodes.push_back(id);
        if (!node->enabled) {
            result.logs.push_back("[Skip] " + node->title);
            pushNext(id, 0);
            continue;
        }
        int nextPin = 0;
        switch (node->type) {
        case NodeType::BeginPlay: break;
        case NodeType::Tick:
            result.logs.push_back("[Demo] Event Tick: one frame, Delta Time = " + Number(node->values[0]) + " s");
            break;
        case NodeType::KeyInput:
            result.logs.push_back("[Demo] Keyboard Input: simulated " + node->text + " press");
            break;
        case NodeType::Branch: {
            const bool condition = input(*node, 1).boolean;
            nextPin = condition ? 0 : 1;
            result.logs.push_back(std::string("[Flow] Branch -> ") + (condition ? "True" : "False"));
            break;
        }
        case NodeType::Sequence:
            pushNext(id, 1);
            break;
        case NodeType::Delay: {
            const float seconds = std::max(0.0f, input(*node, 1).number);
            result.logs.push_back("[Demo] Delay " + Number(seconds) + " s (simulated)");
            break;
        }
        case NodeType::SetPosition:
            result.actorPosition = input(*node, 2).vector;
            result.logs.push_back("[Scene] " + input(*node, 1).text + " position = (" + Number(result.actorPosition[0]) + ", " + Number(result.actorPosition[1]) + ", " + Number(result.actorPosition[2]) + ")");
            break;
        case NodeType::RotateActor:
            result.actorYaw += input(*node, 2).number;
            result.logs.push_back("[Scene] " + input(*node, 1).text + " yaw = " + Number(result.actorYaw) + " deg");
            break;
        case NodeType::SetVisibility:
            result.actorVisible = input(*node, 2).boolean;
            result.logs.push_back("[Render] " + input(*node, 1).text + (result.actorVisible ? " visible" : " hidden"));
            break;
        case NodeType::SetMaterial:
            result.actorMaterial = input(*node, 2).text;
            result.logs.push_back("[Render] " + input(*node, 1).text + " material = " + result.actorMaterial);
            break;
        case NodeType::PrintString:
            result.logs.push_back("[Print] " + input(*node, 1).text);
            break;
        case NodeType::PlaySound: {
            const std::string cue = input(*node, 2).text;
            result.effects.push_back("Sound: " + cue);
            result.logs.push_back("[Audio] " + cue + " at " + input(*node, 1).text);
            break;
        }
        case NodeType::SpawnEffect: {
            const std::string effect = input(*node, 2).text;
            result.effects.push_back("Effect: " + effect);
            result.logs.push_back("[VFX] " + effect + " at " + input(*node, 1).text);
            break;
        }
        case NodeType::SetVariable:
            variables[node->text] = input(*node, 1).number;
            result.logs.push_back("[Variable] " + node->text + " = " + Number(variables[node->text]));
            break;
        default:
            result.logs.push_back("[Error] A data node was encountered on the execution path.");
            return result;
        }
        if (evaluationFailed || !std::isfinite(result.actorYaw)) {
            result.logs.push_back("[Error] Data evaluation exceeded its budget, recursed too deeply, or produced a non-finite value.");
            return result;
        }
        pushNext(id, nextPin);
    }
    result.success = true;
    result.logs.push_back("[Complete] " + std::to_string(result.steps) + " execution nodes.");
    return result;
}

bool RunModelTests(const std::string& directory, const std::string& reportPath) {
    struct Test { std::string name; bool passed; std::string detail; };
    std::vector<Test> tests;
    auto check = [&](const std::string& name, bool passed, const std::string& detail = "") {
        tests.push_back({name, passed, detail});
    };
    try {
        std::filesystem::create_directories(std::filesystem::u8path(directory));
        const auto fixture = (std::filesystem::u8path(directory) / "graph-model-roundtrip.agraph").u8string();
        const auto invalidFixture = (std::filesystem::u8path(directory) / "graph-model-invalid.agraph").u8string();
        Graph graph;
        graph.SeedDemo();
        const auto validation = graph.Validate();
        check("seed_validation", std::none_of(validation.begin(), validation.end(), [](const Diagnostic& d) { return d.severity == Severity::Error; }));
        auto run = graph.RunDemo();
        check("connected_execution_and_data", run.success && run.steps == 6 && std::abs(run.actorYaw - 33.75f) < 0.0001f &&
            std::abs(run.actorPosition[0] - 12.8f) < 0.0001f && run.effects == std::vector<std::string>{"Sound: Ancient_Wind"});
        const auto again = graph.RunDemo();
        check("deterministic_run", again.success && run.logs == again.logs && run.executedNodes == again.executedNodes && run.actorPosition == again.actorPosition);
        const Id constant = graph.AddNode(NodeType::FloatValue, 25.0f, 25.0f);
        const Id boolNode = graph.AddNode(NodeType::BoolValue, 100.0f, 100.0f);
        const Id branch = graph.AddNode(NodeType::Branch, 200.0f, 100.0f);
        std::string error;
        const std::size_t connectionCount = graph.Connections().size();
        check("type_mismatch_rejected", !graph.Connect(constant, 0, branch, 1, &error) && !error.empty() && graph.Connections().size() == connectionCount);
        const Id boolLink = graph.Connect(boolNode, 0, branch, 1);
        check("input_cardinality", boolLink != 0 && !graph.Connect(boolNode, 0, branch, 1));
        check("direction_and_pin_index", !graph.Connect(constant, 1, branch, 1) && !graph.Connect(branch, 0, constant, 0));
        graph.ClearHistory();
        graph.BeginEdit();
        graph.FindNode(constant)->values[0] = 9.875f;
        graph.FindNode(constant)->x = 355.0f;
        graph.FindNode(constant)->title = "Valeur \"spéciale\"";
        graph.FindNode(constant)->text = "Première ligne\nDeuxième ligne \\ dossier";
        graph.EndEdit();
        check("coalesced_property_undo_redo", graph.Undo() && graph.FindNode(constant)->values[0] == 0.75f && !graph.CanUndo() &&
            graph.Redo() && graph.FindNode(constant)->values[0] == 9.875f && graph.FindNode(constant)->x == 355.0f);
        graph.BeginEdit();
        graph.FindNode(constant)->values[0] = 999.0f;
        graph.CancelEdit();
        check("edit_cancel", graph.FindNode(constant)->values[0] == 9.875f);
        const auto expectedNodes = graph.Nodes();
        const auto expectedConnections = graph.Connections();
        Graph loaded;
        const bool roundtrip = graph.Save(fixture, &error) && loaded.Load(fixture, &error) &&
            loaded.Nodes().size() == expectedNodes.size() && loaded.Connections().size() == expectedConnections.size() &&
            std::equal(loaded.Nodes().begin(), loaded.Nodes().end(), expectedNodes.begin(), SameNode) &&
            std::equal(loaded.Connections().begin(), loaded.Connections().end(), expectedConnections.begin(), SameConnection);
        check("utf8_quoted_text_roundtrip", roundtrip, error);
        const Id duplicated = loaded.DuplicateNode(constant);
        check("duplicate_preserves_properties", duplicated != 0 && duplicated != constant &&
            loaded.FindNode(duplicated)->values == loaded.FindNode(constant)->values &&
            loaded.FindNode(duplicated)->x == loaded.FindNode(constant)->x + 40.0f);
        const std::size_t beforeRemove = loaded.Connections().size();
        loaded.RemoveNode(boolNode);
        check("remove_node_and_links_undo", !loaded.FindNode(boolNode) && loaded.Connections().size() == beforeRemove - 1 &&
            loaded.Undo() && loaded.FindNode(boolNode) && loaded.FindConnection(boolLink));
        const Id lastAllocated = loaded.AddNode(NodeType::Add, 0.0f, 0.0f);
        loaded.Undo();
        const Id newer = loaded.AddNode(NodeType::Add, 0.0f, 0.0f);
        check("ids_are_not_reused_after_undo", newer > lastAllocated);
        {
            std::ofstream invalid(std::filesystem::u8path(invalidFixture), std::ios::binary);
            invalid << "ATELIER_GRAPH 1\nNODES 1\nNODE 1 0 0 0 1 1 0 0 0 0 \"Begin\" \"\"\nCONNECTIONS 1\nLINK 2 1 0 999 0\nEND\n";
        }
        const std::size_t preservedCount = loaded.Nodes().size();
        const Id preservedId = loaded.Nodes().front().id;
        check("failed_load_is_atomic", !loaded.Load(invalidFixture, &error) && !error.empty() &&
            loaded.Nodes().size() == preservedCount && loaded.Nodes().front().id == preservedId);
        Graph cycle;
        const Id cycleA = cycle.AddNode(NodeType::Delay, 0.0f, 0.0f);
        const Id cycleB = cycle.AddNode(NodeType::Delay, 0.0f, 0.0f);
        cycle.Connect(cycleA, 0, cycleB, 0);
        cycle.Connect(cycleB, 0, cycleA, 0);
        const auto cycleDiagnostics = cycle.Validate();
        check("execution_cycle_detected", std::any_of(cycleDiagnostics.begin(), cycleDiagnostics.end(), [](const Diagnostic& d) {
            return d.severity == Severity::Error && d.message.find("Execution cycle") != std::string::npos;
        }) && !cycle.RunDemo().success);
        Graph dataCycle;
        const Id addA = dataCycle.AddNode(NodeType::Add, 0.0f, 0.0f);
        const Id addB = dataCycle.AddNode(NodeType::Add, 0.0f, 0.0f);
        dataCycle.Connect(addA, 0, addB, 0);
        dataCycle.Connect(addB, 0, addA, 0);
        const auto dataDiagnostics = dataCycle.Validate();
        check("data_cycle_detected", std::any_of(dataDiagnostics.begin(), dataDiagnostics.end(), [](const Diagnostic& d) {
            return d.severity == Severity::Error && d.message.find("Data dependency cycle") != std::string::npos;
        }));
        Graph branched;
        branched.SeedDemo();
        for (const auto& node : branched.Nodes()) {
            if (node.type == NodeType::GetVariable) branched.FindNode(node.id)->values[0] = 0.25f;
        }
        const auto falseRun = branched.RunDemo();
        check("branch_runs_only_selected_path", falseRun.success && falseRun.steps == 4 && falseRun.actorYaw == 0.0f &&
            falseRun.effects == std::vector<std::string>{"Effect: Dust_Motes"});
        const auto capped = graph.RunDemo(2);
        check("execution_budget", !capped.success && capped.steps == 2 && !capped.logs.empty() && capped.logs.back().find("budget exhausted") != std::string::npos);
        Graph sequence;
        const Id start = sequence.AddNode(NodeType::BeginPlay, 0.0f, 0.0f);
        const Id seq = sequence.AddNode(NodeType::Sequence, 0.0f, 0.0f);
        const Id first = sequence.AddNode(NodeType::PrintString, 0.0f, 0.0f);
        const Id second = sequence.AddNode(NodeType::PrintString, 0.0f, 0.0f);
        const Id disconnected = sequence.AddNode(NodeType::PrintString, 0.0f, 0.0f);
        sequence.FindNode(first)->text = "FIRST";
        sequence.FindNode(second)->text = "SECOND";
        sequence.FindNode(disconnected)->text = "UNCONNECTED";
        sequence.Connect(start, 0, seq, 0);
        sequence.Connect(seq, 0, first, 0);
        sequence.Connect(seq, 1, second, 0);
        const auto seqRun = sequence.RunDemo();
        const auto firstLog = std::find(seqRun.logs.begin(), seqRun.logs.end(), "[Print] FIRST");
        const auto secondLog = std::find(seqRun.logs.begin(), seqRun.logs.end(), "[Print] SECOND");
        check("sequence_order_and_disconnected_actions", seqRun.success && seqRun.steps == 4 && firstLog < secondLog &&
            std::find(seqRun.logs.begin(), seqRun.logs.end(), "[Print] UNCONNECTED") == seqRun.logs.end());
        check("execution_output_cardinality", !sequence.Connect(start, 0, disconnected, 0));
        Graph update;
        const Id tick = update.AddNode(NodeType::Tick, 0.0f, 0.0f);
        const Id rotation = update.AddNode(NodeType::RotateActor, 0.0f, 0.0f);
        update.Connect(tick, 0, rotation, 0);
        update.Connect(tick, 1, rotation, 2);
        const auto updateRun = update.RunDemo();
        check("single_simulated_tick", updateRun.success && updateRun.steps == 2 &&
            std::abs(updateRun.actorYaw - 1.0f / 60.0f) < 0.00001f &&
            updateRun.logs[1].find("one frame") != std::string::npos);
        Graph inputGraph;
        const Id key = inputGraph.AddNode(NodeType::KeyInput, 0.0f, 0.0f);
        const Id keyBranch = inputGraph.AddNode(NodeType::Branch, 0.0f, 0.0f);
        const Id keyPrint = inputGraph.AddNode(NodeType::PrintString, 0.0f, 0.0f);
        inputGraph.Connect(key, 0, keyBranch, 0);
        inputGraph.Connect(key, 1, keyBranch, 1);
        inputGraph.Connect(keyBranch, 0, keyPrint, 0);
        const auto keyRun = inputGraph.RunDemo();
        check("single_simulated_key_press", keyRun.success && keyRun.steps == 3 &&
            std::find(keyRun.logs.begin(), keyRun.logs.end(), "[Flow] Branch -> True") != keyRun.logs.end());
        Graph variableGraph;
        const Id variableStart = variableGraph.AddNode(NodeType::BeginPlay, 0.0f, 0.0f);
        const Id setter = variableGraph.AddNode(NodeType::SetVariable, 0.0f, 0.0f);
        const Id getter = variableGraph.AddNode(NodeType::GetVariable, 0.0f, 0.0f);
        const Id variableRotation = variableGraph.AddNode(NodeType::RotateActor, 0.0f, 0.0f);
        variableGraph.FindNode(setter)->values[0] = 12.5f;
        variableGraph.FindNode(getter)->values[0] = 2.0f;
        variableGraph.Connect(variableStart, 0, setter, 0);
        variableGraph.Connect(setter, 0, variableRotation, 0);
        variableGraph.Connect(getter, 0, variableRotation, 2);
        const auto variableRun = variableGraph.RunDemo();
        check("named_variable_reads_execution_state", variableRun.success && variableRun.actorYaw == 12.5f);
        Graph enabledGraph;
        enabledGraph.SeedDemo();
        for (const auto& node : enabledGraph.Nodes()) {
            if (node.type == NodeType::RotateActor) enabledGraph.FindNode(node.id)->enabled = false;
        }
        const auto enabledRun = enabledGraph.RunDemo();
        check("disabled_action_bypasses_and_continues", enabledRun.success && enabledRun.actorYaw == 0.0f &&
            enabledRun.effects == std::vector<std::string>{"Sound: Ancient_Wind"});
        Graph fileGuard;
        fileGuard.SeedDemo();
        const bool originalSaved = fileGuard.Save(fixture);
        fileGuard.FindNode(fileGuard.Nodes().front().id)->x = std::numeric_limits<float>::infinity();
        const bool rejectedSave = !fileGuard.Save(fixture, &error);
        Graph guardedLoad;
        check("invalid_save_preserves_existing_file", originalSaved && rejectedSave && guardedLoad.Load(fixture) && guardedLoad.RunDemo().success);
    } catch (const std::exception& exception) {
        check("unexpected_exception", false, exception.what());
    }
    const bool passed = !tests.empty() && std::all_of(tests.begin(), tests.end(), [](const Test& test) { return test.passed; });
    try {
        const auto parent = std::filesystem::u8path(reportPath).parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent);
        std::ofstream out(std::filesystem::u8path(reportPath), std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << "{\n  \"suite\": \"Atelier graph model\",\n  \"passed\": " << (passed ? "true" : "false")
            << ",\n  \"tests\": [\n";
        for (std::size_t i = 0; i < tests.size(); ++i) {
            out << "    {\"name\": \"" << JsonEscape(tests[i].name) << "\", \"passed\": "
                << (tests[i].passed ? "true" : "false") << ", \"detail\": \"" << JsonEscape(tests[i].detail) << "\"}"
                << (i + 1 < tests.size() ? "," : "") << '\n';
        }
        out << "  ]\n}\n";
        out.flush();
        return passed && static_cast<bool>(out);
    } catch (...) { return false; }
}

} // namespace atelier::graph

#ifdef ATELIER_GRAPH_MODEL_TEST_MAIN
int main(int argc, char** argv) {
    const std::string directory = argc > 1 ? argv[1] : "outputs";
    const std::string report = argc > 2 ? argv[2] : "outputs/graph-model-test.json";
    return atelier::graph::RunModelTests(directory, report) ? 0 : 1;
}
#endif
