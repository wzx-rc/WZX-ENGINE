// game - graph interpreter. Runs a Graph against a World during Play.
#pragma once
#include "Graph.h"
#include <deque>
#include <functional>

namespace game {

// What the graph can see and change. Implemented by the host application.
// Object handles are scene object indices; they stay valid during Play
// (destroyed objects are hidden, never erased).
class World {
public:
    virtual ~World() = default;
    virtual int objectCount() const = 0;
    virtual bool objectAlive(int obj) const = 0;
    virtual std::string objectName(int obj) const = 0;
    virtual std::string objectPart(int obj) const = 0;
    virtual float3 getPosition(int obj) const = 0;
    virtual void setPosition(int obj, float3 p) = 0;
    virtual void rotateY(int obj, float degrees) = 0;
    virtual void setScale(int obj, float3 s) = 0;
    virtual void setColor(int obj, float3 rgb) = 0;
    virtual int spawn(const std::string& part, float3 pos, float3 rgb) = 0;
    virtual void destroy(int obj) = 0;
    virtual int selected() const = 0;
    virtual void select(int obj) = 0;
    virtual void message(const std::string& text) = 0;
    virtual void setRtsCamera(bool on) { (void)on; }   // "Caméra RTS" node: the host owns the camera
    // RTS houses of the village. Ids are stable; a cell is the lower corner of the footprint.
    virtual int placeHouse(int type, int cx, int cz, int rot) { (void)type; (void)cx; (void)cz; (void)rot; return -1; }   // type 0 small, 1 large
    virtual bool removeHouse(int id) { (void)id; return false; }
    virtual bool turnHouse(int id) { (void)id; return false; }
    virtual int houseAt(int cx, int cz) const { (void)cx; (void)cz; return -1; }
    virtual int houseCount() const { return 0; }
    virtual void hudButton(const std::string& id, const std::string& label) { (void)id; (void)label; }
    virtual void setSun(float azimuth, float height) { (void)azimuth; (void)height; }
    virtual void cellAt(float x, float z, int& cx, int& cz) const { (void)x; (void)z; cx = cz = 0; }
    // live prefab instances (PrefabInstance.h): name or file of the prefab; -1 when it cannot be read
    virtual int spawnPrefab(const std::string& prefab, float3 pos, float yawDeg) { (void)prefab; (void)pos; (void)yawDeg; return -1; }
    virtual bool playClip(int obj, const std::string& clip, bool loop, float speed) { (void)obj; (void)clip; (void)loop; (void)speed; return false; }
    virtual void stopClip(int obj) { (void)obj; }
    // RTS units (the hosts use app/RtsPlay.h): selection, what an instance is, movement and buildings
    virtual std::vector<int> selection() const { return {}; }
    virtual void setSelection(const std::vector<int>& objs) { (void)objs; }
    virtual std::string objectPrefab(int obj) const { (void)obj; return std::string(); }     // MechaWorker...
    virtual std::string objectCategory(int obj) const { (void)obj; return std::string(); }   // Unit, Building, Resource, Prop
    virtual void setYaw(int obj, float degrees) { (void)obj; (void)degrees; }
    // waypoints from a point to another around the buildings (the last one at or next to `to`)
    virtual std::vector<float3> findPath(float3 from, float3 to) { (void)from; return {to}; }
    virtual float3 doorOf(int building) const { return getPosition(building); }   // where its units come out
    // the player places a prefab (ghost on the grid); the host answers with Runtime::firePlacement
    virtual bool beginPlacement(const std::string& prefab) { (void)prefab; return false; }
    virtual void setPartColor(int obj, const std::string& part, float3 rgb) { (void)obj; (void)part; (void)rgb; }
    virtual void setPartGlow(int obj, const std::string& part, float k) { (void)obj; (void)part; (void)k; }
};

class Runtime {
public:
    // Starts a play session (fires "Au démarrage").
    void start(Graph* g, World* w);
    void stop();
    bool running() const { return m_graph != nullptr; }

    // Advances timers / movers and fires "À chaque frame".
    void tick(float dt);
    void fireClickObject(int obj, float3 point);
    void fireClickGround(float3 point);
    void fireHouseClick(int house);   // house = its id
    void fireHudButton(const std::string& id);
    void fireClipFinished(int obj, const std::string& clip);   // a non-looping clip of a prefab instance ended
    // RTS units (the hosts' input, app/RtsPlay.h)
    void fireRightClickGround(float3 point);
    void fireRightClickObject(int obj, float3 point);
    void fireSelectionChanged();
    void firePlacement(int building);   // the building placed (-1: cancelled) for the pending « Place Building »
    bool placementPending() const { return m_placeNode >= 0; }

    // Per-object state (read by the HUD: cargo and hit point bars)
    float objectVar(int obj, const std::string& name, float def = 0.0f) const;
    bool hasObjectVar(int obj, const std::string& name) const;
    bool unitMoving(int obj) const;
    // Production of a building: progress 0..1 of the current unit (-1: none), units queued, prefab being made
    float production(int building, int* queued = nullptr, std::string* prefab = nullptr) const;
    // HUD declared by the graph (drawn by the hosts)
    struct HudCounter { std::string id, label, var; float3 color; };
    struct Objective { std::string id, text; bool done = false; };
    std::vector<HudCounter> hudCounters;
    std::vector<Objective> objectives;
    std::string messageTitle, messageText;   // empty title: no message box
    bool messageShown = false;
    // key: upper-case name as accepted by the "Touche pressée" node (A..Z, 0..9, ESPACE, ENTREE, F1..F12, HAUT, BAS, GAUCHE, DROITE).
    void fireKey(const std::string& key);

    float time() const { return m_time; }
    const std::map<std::string, Value>& variables() const { return m_vars; }
    // Editor feedback: time (in play seconds) a node last ran, for highlighting.
    float lastRun(int nodeId) const;
    std::deque<std::string> log;          // most recent last, capped
    std::deque<int> trace;                // ids of the nodes run since Play, most recent last, capped (Execution Trace)
    std::string lastError;
    // Data flow on the links since start (hover info of a connection): last value received by an input pin
    // (key: node, input pin) and the times an exec output fired (key: node, output pin).
    std::map<std::pair<int, int>, Value> linkValues;
    std::map<std::pair<int, int>, int> execFired;

private:
    struct Timer { float left; int node; int pin; };
    struct Mover { int obj; float3 target; float speed; int node; int pin; };

    void fire(Op op, const std::function<bool(const Node&)>& filter = nullptr);
    void execFrom(int nodeId, int outPin);   // follows the exec link of an output pin
    void exec(Node& n);
    Value input(const Node& n, int pin);
    Value output(const Node& n, int pin);    // evaluates a pure node or reads the cache of an impure one
    void setOut(const Node& n, int pin, const Value& v);
    void error(const std::string& e);

    Graph* m_graph = nullptr;
    World* m_world = nullptr;
    float m_time = 0;
    std::map<std::string, Value> m_vars;
    std::map<int, std::vector<Value>> m_cache;   // outputs of impure nodes (event payloads, loop items...)
    std::map<int, float> m_lastRun;
    void recordRun(int nodeId);
    std::vector<Timer> m_timers;
    std::vector<Mover> m_movers;
    // RTS units
    struct ObjTimer { int obj; std::string name; float left; };
    struct UnitMove { int obj; std::vector<float3> path; size_t next; float speed; int node; float stuck; float3 order; };   // order: the point asked (formation)
    struct Production { int building; std::string prefab; float left, total; int node; };
    std::map<int, std::map<std::string, float>> m_objVars;
    std::map<int, std::map<std::string, int>> m_objLinks;
    std::vector<ObjTimer> m_objTimers;
    std::vector<UnitMove> m_unitMoves;
    std::vector<Production> m_production;
    int m_placeNode = -1;
    std::string m_placeCostVar;
    float m_placeCost = 0;
    void tickUnits(float dt);
    std::map<int, bool> m_doneOnce;
    int m_budget = 0;                            // node executions left for the current event (loop guard)
    int m_depth = 0;
};

} // namespace game
