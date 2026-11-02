// BrickIt - Mechanical mode (port of the web builder): mechanical connectors of the parts (from their typed voxels),
// rigid bodies (studs, axles in cross holes), pivots (axles in round holes, pins), gear meshes, kinematic tree with
// closed loops, Levenberg-Marquardt solver, motor commands driving the gear trains.
#pragma once
#include "Build.h"
#include <string>
#include <vector>

namespace brickit {

struct MechConn { float3 p, axis; float half = 0.4f; bool cross = false; };   // hole or male (axle / pin), local
// kind: 0 spur, 1 bevel / crown (also meshes at 90 degrees), 2 worm (teeth = starts = 1, radius from the mesh)
struct MechGear { int teeth = 0; float3 c, axis, ex; int kind = 0; float radius = 0; };   // ex: direction of a tooth tip (phase)
struct MechMeta {
    std::vector<MechConn> holes, males;   // males: cross = axle, else pin
    std::vector<float3> top, bottom;      // studs, anti-studs
    std::vector<MechGear> gears;
    // turntable half (web: metas[i].pair, never filled by the web v26): role 1 base / bottom, 2 top, family from the
    // LDraw title, vertical axis through the centre of the part, extent along the axis
    int pairRole = 0;
    std::string pairKind;
    float3 pairC, pairAxis{0, 1, 0};
    float pairLo = 0, pairHi = 0;
};

struct MechGearW { int brick, body, teeth; float3 c, u, ex; float spin = 0; int local = 0; int kind = 0; float radius = 0; };   // spin: tooth phase at rest (radians around u)
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
    // Gear mesh constraints, whatever carries the axles (same part, two parts, moving arms, planetary trains): the
    // tooth phase of each gear, measured from the direction of the other gear, times its teeth, sums to a constant
    // (sigma -1: the two phases turn the same way at the contact, as a crown or an inner ring)
    struct GearCon { int ga, gb; float sigma; double c0; };
    std::vector<GearCon> gearCons;
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
    int closureRes(const MechModel& m, const std::vector<vp3d::Affine>& mats, double* out) const;   // loops then gear meshes
public:
    static double gearPhase(const MechModel& m, const MechModel::GearCon& g, const std::vector<vp3d::Affine>& mats);
    static int residualRows(const MechModel& m) { return (int)m.closures.size() * 6 + (int)m.gearCons.size(); }
private:
};

} // namespace brickit
