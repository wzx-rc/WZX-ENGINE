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
    // key: upper-case name as accepted by the "Touche pressée" node (A..Z, 0..9, ESPACE, ENTREE, F1..F12, HAUT, BAS, GAUCHE, DROITE).
    void fireKey(const std::string& key);

    float time() const { return m_time; }
    const std::map<std::string, Value>& variables() const { return m_vars; }
    // Editor feedback: time (in play seconds) a node last ran, for highlighting.
    float lastRun(int nodeId) const;
    std::deque<std::string> log;          // most recent last, capped
    std::deque<int> trace;                // ids of the nodes run since Play, most recent last, capped (Execution Trace)
    std::string lastError;

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
    std::map<int, bool> m_doneOnce;
    int m_budget = 0;                            // node executions left for the current event (loop guard)
    int m_depth = 0;
};

} // namespace game
