// BrickIt - Mechanical mode (port of the web builder): mechanical connectors of the parts (from their typed voxels),
// rigid bodies (studs, axles in cross holes), pivots (axles in round holes, pins), gear meshes, kinematic tree with
// closed loops, Levenberg-Marquardt solver, motor commands driving the gear trains.
#pragma once
#include "Build.h"
#include <string>
#include <vector>

namespace brickit {

struct MechConn { float3 p, axis; float half = 0.4f; bool cross = false; };   // hole or male (axle / pin), local
struct MechGear { int teeth = 0; float3 c, axis, ex; };   // ex: direction of a tooth tip (phase)
struct MechMeta {
    std::vector<MechConn> holes, males;   // males: cross = axle, else pin
    std::vector<float3> top, bottom;      // studs, anti-studs
    std::vector<MechGear> gears;
};

struct MechGearW { int brick, body, teeth; float3 c, u, ex; float spin = 0; int local = 0; };   // spin: tooth phase at rest (radians around u)
struct MechJoint { int a, b; float3 p, u; float3 p2; };   // revolute between bodies a and b, axis u through p
struct MechTreeJ { MechJoint j; int parent, child, qi; };
struct MechCmd { std::string name; int qi; float value = 0, speed = 3.2f; std::vector<std::pair<int, float>> ratio; int keyPlus = 0, keyMinus = 0; };

struct MechModel {
    int bodyCount = 0;
    std::vector<int> brickBody;
    std::vector<int> bodyBricks;          // count per body
    std::vector<int> roots;
    std::vector<MechTreeJ> tree;
    std::vector<MechJoint> closures;
    std::vector<MechGearW> gears;
    std::vector<std::pair<int, int>> meshes;   // gear indices
    struct QMode { int kind = 0; int master = -1; float k = 0; int cmd = -1; };   // kind 0 free, 1 command, 2 gear (k * master), 3 locked
    std::vector<QMode> qmode;
    std::vector<MechCmd> cmds;
    std::vector<std::string> warnings;
    std::vector<double> q;
    std::vector<vp3d::Affine> mats;       // per body
    std::vector<std::pair<int, vp3d::Affine>> spins;   // brick -> local rotation of its gear (teeth interleaved at rest)
    int dof = 0;
    double buildMs = 0;
};

class Mech {
public:
    explicit Mech(Library& lib) : m_lib(lib) {}
    const MechMeta& meta(int part);
    bool build(const std::vector<Brick>& bricks, MechModel& out);   // false: empty scene
    // Mouse drag of a moving part: a point of body `body` (local = in its rest frame) pulled towards `target`.
    struct Drag { int body = -1; float3 local, target; };
    bool solve(MechModel& m, const Drag* drag = nullptr);              // commands -> joint values; false at a stop
    void fk(const MechModel& m, const std::vector<double>& q, std::vector<vp3d::Affine>& mats) const;
    // world matrix of a brick in the current pose
    vp3d::Affine brickMatrix(const MechModel& m, const Brick& b, int brick) const;

private:
    Library& m_lib;
    std::vector<MechMeta> m_meta;
    std::vector<bool> m_metaDone;
    void applyGears(const MechModel& m, std::vector<double>& q) const;
    int closureRes(const MechModel& m, const std::vector<vp3d::Affine>& mats, double* out) const;
};

} // namespace brickit
