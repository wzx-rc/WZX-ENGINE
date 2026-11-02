// BrickIt - automatic tests of the mechanics (--mechtests): mounts built by aligning the connectors of real LDraw parts
// (holes, axles, gear centres), then for each: bodies, commands, and a full turn of the command (36 solver steps) that
// must move every gear by its expected ratio, the meshes held (solver residuals under tolerance). Results in
// mech_tests.txt next to the exe.
#include "BrickApp.h"
#include <cmath>
#include <cstdio>
#include <functional>
#include <array>
#include <map>

namespace brickit {

namespace {

struct Mount {
    BrickApp* app = nullptr;
    Library* lib = nullptr;
    Mech* mech = nullptr;
    std::vector<Brick> b;
    static std::vector<float4> rots() {   // the 24 rotations of the cube
        std::vector<float4> out;
        const float h = 3.14159265f * 0.5f;
        for (int a = 0; a < 4; ++a)
            for (int c = 0; c < 4; ++c)
                for (int d = 0; d < 4; ++d) {
                    float4 q = snapQuat(qMul(qAxisAngle(float3(0, 0, 1), d * h), qMul(qAxisAngle(float3(0, 1, 0), c * h), qAxisAngle(float3(1, 0, 0), a * h))));
                    bool dup = false;
                    for (const float4& o : out) dup |= std::fabs(std::fabs(o.x * q.x + o.y * q.y + o.z * q.z + o.w * q.w) - 1) < 1e-4f;
                    if (!dup) out.push_back(q);
                }
        return out;
    }
    int partOf(const char* name) {
        const int i = lib->find(name);
        if (i >= 0) lib->get(name);
        return i;
    }
    // a rotation taking the local axis `la` to the world axis `wa` (and, when given, the local `lb` to `wb`)
    static float4 rotFor(const float3& la, const float3& wa, const float3* lb = nullptr, const float3* wb = nullptr) {
        for (const float4& q : rots())
            if (dot(qRot(q, la), wa) > 0.99f && (!lb || dot(qRot(q, *lb), *wb) > 0.99f)) return q;
        return float4(0, 0, 0, 1);
    }
    int add(int part, const float4& rot, const float3& pos) {
        Brick k;
        k.part = part;
        k.color = 0;
        k.rot = rot;
        k.pos = pos;
        b.push_back(k);
        return (int)b.size() - 1;
    }
    // a part with holes: its hole `h` centred on P along the world axis `axis`; `along` (optional): the direction of its row of holes
    int hole(const char* name, int h, const float3& P, const float3& axis, const float3* along = nullptr) {
        const int part = partOf(name);
        const MechMeta& m = mech->meta(part);
        if (part < 0 || h >= (int)m.holes.size()) return -1;
        float4 q;
        if (along && m.holes.size() > 1) {
            float3 row = normalize(m.holes.back().p - m.holes.front().p);
            q = rotFor(m.holes[h].axis, axis, &row, along);
        } else q = rotFor(m.holes[h].axis, axis);
        return add(part, q, P - qRot(q, m.holes[h].p));
    }
    float3 holeW(int brick, int h) { const MechMeta& m = mech->meta(b[brick].part); return qRot(b[brick].rot, m.holes[h].p) + b[brick].pos; }
    // an axle (its first male connector) centred on P along `axis`
    int axle(const char* name, const float3& P, const float3& axis) {
        const int part = partOf(name);
        const MechMeta& m = mech->meta(part);
        if (part < 0 || m.males.empty()) return -1;
        const float4 q = rotFor(m.males[0].axis, axis);
        return add(part, q, P - qRot(q, m.males[0].p));
    }
    // a gear (its gear `g`) centred on P, turning around `axis`
    int gear(const char* name, const float3& P, const float3& axis, int g = 0) {
        const int part = partOf(name);
        const MechMeta& m = mech->meta(part);
        if (part < 0 || g >= (int)m.gears.size()) return -1;
        const float4 q = rotFor(m.gears[g].axis, axis);
        return add(part, q, P - qRot(q, m.gears[g].c));
    }
    float gearR(const char* name, int g = 0) { const MechMeta& m = mech->meta(partOf(name)); return g < (int)m.gears.size() ? (m.gears[g].kind == 2 ? m.gears[g].radius : m.gears[g].teeth * 0.05f) : 0; }
    // a beam lying in a layer (z), its hole `h` on P, its row of holes along `dir` (any direction of the XY plane)
    int beamAt(const char* name, int h, const float3& P, const float3& dir) {
        const int part = partOf(name);
        const MechMeta& m = mech->meta(part);
        if (part < 0 || m.holes.size() < 2) return -1;
        const float3 row = normalize(m.holes.back().p - m.holes.front().p), X(1, 0, 0), Z(0, 0, 1);
        const float4 base = rotFor(m.holes[0].axis, Z, &row, &X);
        const float4 q = qNormalize(qMul(qAxisAngle(Z, std::atan2(dir.y, dir.x)), base));
        return add(part, q, P - qRot(q, m.holes[h].p));
    }
    // a pin along Z centred on P (it joins the layers it crosses)
    int pinAt(const float3& P) { return axle("3673", P, float3(0, 0, 1)); }
    // end pin hole of an actuator half (across its axis), farthest along the axis on the side `sgn` (0: either side)
    static int endHoleOf(const MechMeta& m, float sgn) {
        int best = -1;
        float bv = 0;
        for (size_t i = 0; i < m.holes.size(); ++i) {
            if (std::fabs(dot(m.holes[i].axis, m.cylAxis)) > 0.1f) continue;
            float v = dot(m.holes[i].p - m.cylC, m.cylAxis);
            v = sgn == 0 ? std::fabs(v) : v * sgn;
            if (best < 0 || v > bv) { best = (int)i; bv = v; }
        }
        return best;
    }
    // the rod comes out on the side opposite the body end hole (sign along the body axis)
    static float rodSide(const MechMeta& bm) {
        const int hb = endHoleOf(bm, 0);
        return hb >= 0 && dot(bm.holes[hb].p - bm.cylC, bm.cylAxis) > 0 ? -1.0f : 1.0f;
    }
    // natural length (end hole to end hole) with the rod half inside the body
    float actLen(const char* body, const char* rodName) {
        const MechMeta& bm = mech->meta(partOf(body));
        const MechMeta& rm = mech->meta(partOf(rodName));
        const float sd = rodSide(bm);
        const int hb = endHoleOf(bm, -sd), hr = endHoleOf(rm, sd);
        if (hb < 0 || hr < 0) return 0;
        return std::fabs(sd * bm.cylLen * 0.5f + dot(rm.holes[hr].p - rm.cylC, rm.cylAxis) - dot(bm.holes[hb].p - bm.cylC, bm.cylAxis));
    }
    // an actuator from E1 (body end hole) to E2 (rod end hole), its pin holes along Z; returns {body, rod}
    std::pair<int, int> actuator(const char* body, const char* rodName, const float3& E1, const float3& E2) {
        const int bp = partOf(body), rp = partOf(rodName);
        const MechMeta& bm = mech->meta(bp);
        const MechMeta& rm = mech->meta(rp);
        const float sd = rodSide(bm);
        const int hb = endHoleOf(bm, -sd), hr = endHoleOf(rm, sd);
        if (hb < 0 || hr < 0) return {-1, -1};
        const float3 X(1, 0, 0), Z(0, 0, 1), d = normalize(E2 - E1);
        const float ang = std::atan2(d.y, d.x);
        const float3 ab = bm.cylAxis * sd, ar = rm.cylAxis * sd;
        const float4 qb = qNormalize(qMul(qAxisAngle(Z, ang), rotFor(ab, X, &bm.holes[hb].axis, &Z)));
        const float4 qr = qNormalize(qMul(qAxisAngle(Z, ang), rotFor(ar, X, &rm.holes[hr].axis, &Z)));
        return {add(bp, qb, E1 - qRot(qb, bm.holes[hb].p)), add(rp, qr, E2 - qRot(qr, rm.holes[hr].p))};
    }
    // direction d (XY) so that |O + d r - E| = L, the solution on the side of `side`; false if none
    static bool reachDir(const float3& O, float r, const float3& E, float L, const float3& side, float3& d) {
        const float dx = E.x - O.x, dy = E.y - O.y, D = std::sqrt(dx * dx + dy * dy);
        if (D < 1e-4f || D > r + L - 0.05f || D < std::fabs(r - L) + 0.05f) return false;
        const float base = std::atan2(dy, dx), c = (r * r + D * D - L * L) / (2 * r * D), w = std::acos(std::max(-1.0f, std::min(1.0f, c)));
        const float3 d1(std::cos(base + w), std::sin(base + w), 0), d2(std::cos(base - w), std::sin(base - w), 0);
        d = dot(d1, side) >= dot(d2, side) ? d1 : d2;
        return true;
    }
    // the best (hole a of the fixed part, hole b of the beam pivoting on O by its hole 0): the actuator at its natural
    // length L meets the beam at nearly 90 degrees (far from the dead points where the loop locks)
    static bool bestReach(const std::function<float3(int)>& fixedHole, int a0, int a1, const float3& O, int b0, int b1, float L, const float3& side, int& ia, int& ib, float3& d) {
        float best = 2;
        for (int a = a0; a <= a1; ++a)
            for (int bb = b0; bb <= b1; ++bb) {
                const float3 E = fixedHole(a);
                float3 dd;
                const float r = 0.8f * bb;
                if (!reachDir(O, r, E, L, side, dd)) continue;
                const float D2 = (E.x - O.x) * (E.x - O.x) + (E.y - O.y) * (E.y - O.y);
                const float c = std::fabs((L * L + r * r - D2) / (2 * L * r));
                if (c < best) { best = c; ia = a; ib = bb; d = dd; }
            }
        return best < 2;
    }
    void rotateAll(const float4& R) {
        for (Brick& k : b) { k.pos = qRot(R, k.pos); k.rot = snapQuat(qMul(R, k.rot)); }
    }
};

struct Expect { int brick; double ratio; bool absOnly = false; };   // a gear (or arm) brick and its turn per turn of the command

} // namespace

void BrickApp::runMechTests() {
    FILE* f = nullptr;
    if (_wfopen_s(&f, (m_exeDir + L"\\mech_tests.txt").c_str(), L"w") != 0 || !f) return;
    const float3 X(1, 0, 0), Y(0, 1, 0), Z(0, 0, 1);
    int passed = 0, total = 0;
    int drive = 0;   // the command turned by run
    auto run = [&](const char* name, Mount& mt, int expBodies, int expCmds, const std::vector<Expect>& ex, const char* note = nullptr, float target = 2.0f * 3.14159265f) {
        ++total;
        MechModel M;
        const bool built = m_mech.build(mt.b, M);
        std::string why;
        if (!built) why = "construction impossible";
        if (built && expBodies >= 0 && M.bodyCount != expBodies) why += "corps " + std::to_string(M.bodyCount) + " au lieu de " + std::to_string(expBodies) + "; ";
        if (built && expCmds >= 0 && (int)M.cmds.size() != expCmds) why += "commandes " + std::to_string(M.cmds.size()) + " au lieu de " + std::to_string(expCmds) + "; ";
        std::string got;
        if (built && (int)M.cmds.size() > drive && !ex.empty()) {
            // a full turn of the first command, in 36 steps (the solver follows the same branch of the loops)
            const int steps = 36;
            bool ok = true;
            int failStep = -1;
            for (int s = 1; s <= steps && ok; ++s) {
                MechCmd& dc = M.cmds[drive];
                dc.value = (dc.type == 1 ? std::min(target, dc.hi * 0.8f) : target) * s / steps;
                ok = m_mech.solve(M);
                if (!ok) failStep = s;
            }
            if (!ok) {
                char b[160];
                std::vector<double> r(Mech::residualRows(M));
                // the residuals with the command at the failing value and the free pivots at their last pose
                double mx = 0;
                for (const MechModel::GearCon& g : M.gearCons) {
                    double e = Mech::gearPhase(M, g, M.mats) - g.c0;
                    e -= 2 * 3.14159265358979 * std::round(e / (2 * 3.14159265358979));
                    mx = std::max(mx, std::fabs(e));
                }
                snprintf(b, sizeof(b), "le solveur bloque au pas %d / %d (phase %.3f rad, %zu boucles); ", failStep, steps, mx, M.closures.size());
                why += b;
            }
            const double turn = M.q[M.cmds[drive].qi];
            for (const Expect& e : ex) {
                const int body = M.brickBody[e.brick];
                int qi = -1;
                for (const MechTreeJ& t : M.tree) if (t.child == body) qi = t.qi;
                const double r = qi >= 0 ? M.q[qi] / turn : 0.0;
                char b[96];
                snprintf(b, sizeof(b), "%s%.4f", got.empty() ? "" : ", ", r);
                got += b;
                if (std::isnan(e.ratio)) { if (std::fabs(r) < 0.01) { snprintf(b, sizeof(b), "ne bouge pas (%.4f); ", r); why += b; } }
                else if (std::fabs((e.absOnly ? std::fabs(r) : r) - e.ratio) > 0.02 * std::max(1.0, std::fabs(e.ratio)) + 1e-3) {
                    snprintf(b, sizeof(b), "rapport %.4f au lieu de %.4f; ", r, e.ratio);
                    why += b;
                }
            }
            // meshes held at the end of the turn
            std::vector<double> res(Mech::residualRows(M));
            std::vector<Affine> mats;
            mats = M.mats;
            double mx = 0;
            for (const MechModel::GearCon& g : M.gearCons) {
                double e = Mech::gearPhase(M, g, mats) - g.c0;
                e -= 2 * 3.14159265358979 * std::round(e / (2 * 3.14159265358979));
                mx = std::max(mx, std::fabs(e));
            }
            if (mx > 0.05) { char b[64]; snprintf(b, sizeof(b), "engrènement décalé de %.3f rad; ", mx); why += b; }
        }
        const bool pass = why.empty();
        passed += pass;
        fprintf(f, "[%s] %s : %d corps, %zu commandes, %zu engrènements%s%s%s%s\n", pass ? "OK" : "ÉCHEC", name, M.bodyCount, M.cmds.size(), M.meshes.size(),
                got.empty() ? "" : ", rapports ", got.c_str(), pass ? "" : " -> ", why.c_str());
        for (const std::string& w : M.warnings) fprintf(f, "      avertissement : %s\n", w.c_str());
        if (!pass) {   // the kinematic tree, to see what is linked to what
            std::string tr;
            char b[96];
            for (const MechTreeJ& t : M.tree) { snprintf(b, sizeof(b), " %d->%d%s q=%.3f", t.parent, t.child, t.j.type == 1 ? "(gl)" : "", M.q[t.qi]); tr += b; }
            fprintf(f, "      arbre :%s ; %zu boucles ; corps des pièces :", tr.c_str(), M.closures.size());
            for (size_t i = 0; i < mt.b.size(); ++i) fprintf(f, " %s=%d", m_lib.parts[mt.b[i].part].name.c_str(), M.brickBody[i]);
            fprintf(f, "\n");
        }
        if (note) fprintf(f, "      %s\n", note);
        fflush(f);
    };
    auto mk = [&]() { Mount m; m.app = this; m.lib = &m_lib; m.mech = &m_mech; return m; };
    for (const char* pn : {"32524", "32525", "3673", "73230", "6541", "2909", "2910", "92695", "92696", "32526", "32140", "32056", "32348", "6629", "32271", "3700", "32270", "32269", "32198", "4019", "3705", "3706"}) {   // connectors of the parts used (diagnostic)
        const int pi = m_lib.find(pn);
        if (pi < 0) continue;
        m_lib.get(pn);
        const MechMeta& mm = m_mech.meta(pi);
        fprintf(f, "# %s : %zu trous, %zu mâles, %zu tenons, %zu anti-tenons\n", pn, mm.holes.size(), mm.males.size(), mm.top.size(), mm.bottom.size());
        if (mm.cylRole) fprintf(f, "#   vérin rôle %d type %s centre (%.2f %.2f %.2f) axe (%.2f %.2f %.2f) longueur %.2f\n", mm.cylRole, mm.cylKind.c_str(), mm.cylC.x, mm.cylC.y, mm.cylC.z, mm.cylAxis.x, mm.cylAxis.y, mm.cylAxis.z, mm.cylLen);
        for (const float3& v : mm.bottom) fprintf(f, "#   anti-tenon (%.2f %.2f %.2f)\n", v.x, v.y, v.z);
        fprintf(f, "#   boîte (%.2f %.2f %.2f) .. (%.2f %.2f %.2f) %s\n", m_lib.parts[pi].bmin.x, m_lib.parts[pi].bmin.y, m_lib.parts[pi].bmin.z, m_lib.parts[pi].bmax.x, m_lib.parts[pi].bmax.y, m_lib.parts[pi].bmax.z, m_lib.parts[pi].label.c_str());
        for (const MechGear& g : mm.gears) fprintf(f, "#   engrenage %d dents type %d centre (%.2f %.2f %.2f) axe (%.0f %.0f %.0f)\n", g.teeth, g.kind, g.c.x, g.c.y, g.c.z, g.axis.x, g.axis.y, g.axis.z);
        for (const MechConn& h : mm.holes) fprintf(f, "#   trou (%.2f %.2f %.2f) axe (%.0f %.0f %.0f) demi %.2f %s\n", h.p.x, h.p.y, h.p.z, h.axis.x, h.axis.y, h.axis.z, h.half, h.cross ? "croix" : "rond");
        for (const MechConn& h : mm.males) fprintf(f, "#   mâle (%.2f %.2f %.2f) axe (%.0f %.0f %.0f) demi %.2f %s\n", h.p.x, h.p.y, h.p.z, h.axis.x, h.axis.y, h.axis.z, h.half, h.cross ? "axe" : "pin");
    }

    // 1. two gears on one beam (Technic brick 1 x 16, holes 2 apart: 8 + 24 teeth = 1.6 cm)
    auto beamPair = [&](Mount& m, const char* g1, int h1, const char* g2, int h2) {
        const int beam = m.hole("3703", 0, float3(0, 0.6f, 0), Z, &X);
        int a = -1, c = -1;
        for (int k : {h1, h2}) {
            const float3 P = m.holeW(beam, k);
            m.axle("3706", P + Z * 0.8f, Z);
            const int gi = m.gear(k == h1 ? g1 : g2, P + Z * 1.2f, Z);
            (k == h1 ? a : c) = gi;
        }
        return std::make_pair(a, c);
    };
    {
        Mount m = mk();
        auto g = beamPair(m, "3647", 0, "3648b", 2);
        run("1. deux engrenages sur une poutre (8 / 24)", m, 3, 1, {{g.first, 1.0}, {g.second, -8.0 / 24}});
    }
    // 2. two separate supports (the 16 / 24 of Wzx, not studded together)
    {
        Mount m = mk();
        m.hole("6541", 0, float3(0, 0.6f, 0), Z, &X);
        m.hole("6541", 0, float3(2.0f, 0.6f, 0), Z, &X);
        m.axle("3706", float3(0, 0.6f, 0.8f), Z);
        m.axle("3706", float3(2.0f, 0.6f, 0.8f), Z);
        const int a = m.gear("94925", float3(0, 0.6f, 1.2f), Z), c = m.gear("3648b", float3(2.0f, 0.6f, 1.2f), Z);
        run("2. deux supports séparés (16 / 24)", m, 4, 1, {{a, 1.0}, {c, -16.0 / 24}});
    }
    // 3. stacked supports (two Technic bricks 1 x 8 studded, the upper one a stud along): 8 and 16 on the diagonal
    {
        Mount m = mk();
        const int lo = m.hole("3702", 0, float3(0, 0.6f, 0), Z, &X);
        const float3 p0 = m.holeW(lo, 0);
        const int hi = m.hole("3702", 0, p0 + float3(0.8f, 0.96f, 0), Z, &X);
        const float3 p1 = m.holeW(hi, 0);
        m.axle("3706", p0 + Z * 0.8f, Z);
        m.axle("3706", p1 + Z * 0.8f, Z);
        const int a = m.gear("3647", p0 + Z * 1.2f, Z), c = m.gear("94925", p1 + Z * 1.2f, Z);
        run("3. supports empilés (8 / 16 en diagonale)", m, 3, 1, {{a, 1.0}, {c, -8.0 / 16}});
    }
    // 4. side by side: two Technic bricks end to end, not linked (24 on the last hole of one, 8 on the first of the other)
    {
        Mount m = mk();
        const int A = m.hole("3702", 0, float3(0, 0.6f, 0), Z, &X);
        const float3 pa = m.holeW(A, 6);
        const int B = m.hole("3702", 0, pa + X * 1.6f, Z, &X);
        const float3 pb = m.holeW(B, 0);
        m.axle("3706", pa + Z * 0.8f, Z);
        m.axle("3706", pb + Z * 0.8f, Z);
        const int a = m.gear("3648b", pa + Z * 1.2f, Z), c = m.gear("3647", pb + Z * 1.2f, Z);
        run("4. côte à côte, deux briques non reliées (24 / 8)", m, 4, 1, {{c, 1.0}, {a, -8.0 / 24}});
    }
    // 5. planetary: sun 24 on an axle fixed in a cross hole, an arm turning on that axle, planet 8 on the arm
    {
        Mount m = mk();
        m.hole("73230", 0, float3(0, 0.6f, 0), Z, &X);
        m.axle("3706", float3(0, 0.6f, 1.6f), Z);
        m.gear("3648b", float3(0, 0.6f, 2.4f), Z);
        const int arm = m.hole("32524", 0, float3(0, 0.6f, 1.0f), Z, &X);
        const float3 pp = m.holeW(arm, 2);
        m.axle("3705", pp + Z * 0.8f, Z);
        const int planet = m.gear("3647", float3(pp.x, pp.y, 2.4f), Z);
        // the planet turns on the arm; the arm goes round the fixed sun: arm = planet x 8 / 24 (planet measured on the arm)
        run("5. train planétaire (soleil 24 fixe, satellite 8 sur un bras)", m, -1, 1, {{planet, 1.0}, {arm, 8.0 / 24}});
    }
    // 6. a train of 4 gears of 16 on one beam
    {
        Mount m = mk();
        const int beam = m.hole("3703", 0, float3(0, 0.6f, 0), Z, &X);
        std::vector<Expect> ex;
        for (int k = 0; k < 4; ++k) {
            const float3 P = m.holeW(beam, k * 2);
            m.axle("3706", P + Z * 0.8f, Z);
            ex.push_back({m.gear("94925", P + Z * 1.2f, Z), k % 2 ? -1.0 : 1.0});
        }
        run("6. train de 4 engrenages de 16", m, 5, 1, ex);
    }
    // 7. 8, 40 and 24 in line (8-40: 3 holes, 40-24: 4 holes)
    {
        Mount m = mk();
        const int beam = m.hole("3703", 0, float3(0, 0.6f, 0), Z, &X);
        const char* gs[3] = {"3647", "3649", "3648b"};
        const int hs[3] = {0, 3, 7};
        int ids[3];
        for (int k = 0; k < 3; ++k) {
            const float3 P = m.holeW(beam, hs[k]);
            m.axle("3706", P + Z * 0.8f, Z);
            ids[k] = m.gear(gs[k], P + Z * 1.2f, Z);
        }
        run("7. rapports 8 / 40 / 24", m, 4, 1, {{ids[0], 1.0}, {ids[1], -8.0 / 40}, {ids[2], 8.0 / 24}});
    }
    // 8. two 12 tooth double bevel gears at 90 degrees (axes crossing)
    {
        Mount m = mk();
        const float r = m.gearR("32270");
        const float3 P(0, 2.0f, 0);
        m.hole("6541", 0, P + Z * (r + 1.0f), Z, &X);
        m.axle("3706", P + Z * (r + 0.6f), Z);
        const int a = m.gear("32270", P + Z * r, Z);
        m.hole("6541", 0, P + X * (r + 1.0f), X, &Z);
        m.axle("3706", P + X * (r + 0.6f), X);
        const int c = m.gear("32270", P + X * r, X);
        run("8. engrenages coniques à 90° (12 / 12)", m, -1, 1, {{a, 1.0}, {c, 1.0, true}}, "(le signe dépend de l'orientation des axes : seul |rapport| = 1 est attendu)");
    }
    // 9. differential (6573): its 24 tooth ring driven by an 8
    {
        Mount m = mk();
        const MechMeta& dm = m_mech.meta(m.partOf("6573"));
        int g24 = 0;
        for (size_t k = 0; k < dm.gears.size(); ++k) if (dm.gears[k].teeth == 24) g24 = (int)k;
        m.hole("6541", 0, float3(0, 0.6f, 0), Z, &X);
        m.axle("3706", float3(0, 0.6f, 1.6f), Z);
        const int d = m.gear("6573", float3(0, 0.6f, 2.0f), Z, g24);
        const float3 c24 = qRot(m.b[d].rot, dm.gears[g24].c) + m.b[d].pos;
        m.hole("6541", 0, float3(1.6f, 0.6f, 0), Z, &X);
        m.axle("3706", float3(1.6f, 0.6f, 1.6f), Z);
        const int e = m.gear("3647", float3(1.6f, 0.6f, c24.z), Z);
        run("9. différentiel 6573 entraîné par un 8", m, -1, 1, {{e, 1.0}, {d, -8.0 / 24}}, "(boîtier seul : les satellites internes ne sont pas montés)");
    }
    // 10. turntable 28 driven by a 12 tooth double bevel at 90 degrees
    {
        Mount m = mk();
        const int bot = m.partOf("99009"), top = m.partOf("99010");
        const MechMeta& mb = m_mech.meta(bot);
        const MechMeta& mtp = m_mech.meta(top);
        const int B = m.add(bot, float4(0, 0, 0, 1), float3(0) - mb.pairC + float3(0, 0.5f, 0));
        const float3 cb = qRot(m.b[B].rot, mb.pairC) + m.b[B].pos;
        // the top on the same axis, its extent touching the base
        const float3 tpos = cb - mtp.pairC + float3(0, mb.pairHi - mtp.pairLo - 0.0f, 0);
        const int T = m.add(top, float4(0, 0, 0, 1), tpos);
        const float3 ct = mtp.pairC + tpos;
        const float rt = 28 * 0.05f, rd = m.gearR("32270");
        const float3 P = ct + Y * rd;
        m.hole("6541", 0, P + X * (rt + 0.4f), X, &Z);
        m.axle("3706", P + X * (rt + 0.2f), X);
        const int d = m.gear("32270", P + X * rt, X);
        run("10. plateau tournant 28 entraîné par un conique 12", m, -1, 1, {{d, 1.0}, {T, 12.0 / 28, true}}, "(seul |rapport| = 12/28 est attendu)");
    }
    // 11. worm gear and 24 (axes at 90 degrees, apart by the two radii)
    {
        Mount m = mk();
        const float rw = m.gearR("4716");
        m.hole("6541", 0, float3(0, 0.6f, 0), Z, &X);
        m.axle("3706", float3(0, 0.6f, 0.8f), Z);
        const int wheel = m.gear("3648b", float3(0, 0.6f, 1.2f), Z);
        const float3 cw(0, 0.6f + 1.2f + rw, 1.2f);
        m.hole("6541", 0, cw + X * 1.2f, X, &Z);
        m.axle("3706", cw + X * 0.4f, X);
        const int worm = m.gear("4716", cw, X);
        run("11. vis sans fin et 24", m, -1, 1, {{worm, 1.0}, {wheel, 1.0 / 24, true}}, "(seul |rapport| = 1/24 est attendu)");
    }
    // 12. rack and pinion: a 16 over a rack 1 x 8, a quarter turn (the rack slides, guided by the pinion support)
    {
        Mount m = mk();
        m.hole("6541", 0, float3(0, 2.0f, 0), Z, &X);
        m.axle("3706", float3(0, 2.0f, 0.8f), Z);
        const int pin = m.gear("94925", float3(0, 2.0f, 1.2f), Z);
        const int rp = m.partOf("6630");
        const MechMeta& rm = m_mech.meta(rp);
        int rack = -1;
        if (rp >= 0 && !rm.gears.empty()) {
            const float4 q = Mount::rotFor(rm.gears[0].axis, X);
            rack = m.add(rp, q, float3(0, 2.0f - (0.8f + 0.25f), 1.2f) - qRot(q, rm.gears[0].c));
        }
        run("12. crémaillère et pignon (16, un quart de tour)", m, -1, 1, {{pin, 1.0}, {rack, 0.8, true}}, "(cm par radian : rayon primitif 0,8 cm)", 3.14159265f * 0.5f);
    }
    // 17. shock absorber: cylinder 2909 and piston rod 2910 (a slide, travel from the mesh)
    auto actuator = [&](const char* name, const char* body, const char* rodName) {
        Mount m = mk();
        const int bp = m.partOf(body), rp = m.partOf(rodName);
        const MechMeta& bm = m_mech.meta(bp);
        const MechMeta& rmm = m_mech.meta(rp);
        const float4 qb = Mount::rotFor(bm.cylAxis, X), qr = Mount::rotFor(rmm.cylAxis, X);
        m.add(bp, qb, float3(0, 2.0f, 0) - qRot(qb, bm.cylC));
        const int rod = m.add(rp, qr, float3(bm.cylLen * 0.5f, 2.0f, 0) - qRot(qr, rmm.cylC));
        run(name, m, 2, 1, {{rod, 1.0}}, "(commande en cm, course estimée sur le maillage)", 10.0f);
    };
    actuator("17. vérin : amortisseur 2909 + tige 2910", "2909", "2910");
    actuator("18. vérin : actionneur linéaire 92695 + piston 92696", "92695", "92696");
    // 19-21. actuators closing a loop (excavator): the actuator length is the command, the arm follows the loop.
    // Layers: z of the hole centres 0.4 / 1.2 / 2.0, pins between two layers at 0.8 / 1.6 (a pin centred on a layer
    // joins the parts of that layer). The arm angles are set so each actuator starts at its natural length.
    const double MOVE = std::nan("");
    const float L0 = 0.4f, L1 = 1.2f, L2 = 2.0f;
    {
        // 19. an arm lifted by an actuator: upright chassis beam (32525, 10 holes), the arm pivoting on its last hole,
        // actuator from a lower chassis hole to an arm hole (chosen near 90 degrees). Then the stops: past the end of
        // its travel the actuator is refused.
        Mount m = mk();
        const int ch = m.beamAt("32525", 0, float3(0, 0, L0), float3(0, 1, 0));
        const float3 O = m.holeW(ch, 9);
        const float La = m.actLen("2909", "2910");
        int e = 0, k = 4;
        float3 d(1, 0, 0);
        Mount::bestReach([&](int a) { return m.holeW(ch, a); }, 0, 7, O, 1, 9, La, float3(1, 0, 0), e, k, d);
        const float3 E1 = m.holeW(ch, e);
        const int arm = m.beamAt("32525", 0, float3(O.x, O.y, L1), d);
        const float3 E2 = m.holeW(arm, k);
        m.pinAt(float3(O.x, O.y, 0.8f));
        auto cyl = m.actuator("2909", "2910", float3(E1.x, E1.y, L1), E2);
        m.pinAt(float3(E1.x, E1.y, 0.8f));
        m.pinAt(E2);
        run("19. bras levé par un vérin (boucle)", m, 4, 1, {{cyl.second, MOVE}, {arm, MOVE}}, "(le bras suit la longueur du vérin ; la boucle reste fermée)", 10.0f);
        // stops: the command at its ends is followed, 0.3 cm past them it is refused (the pose stays)
        MechModel M;
        if (m_mech.build(m.b, M) && !M.cmds.empty()) {
            ++total;
            MechCmd& c = M.cmds[0];
            bool okEnds = true;
            for (int s2 = 1; s2 <= 24 && okEnds; ++s2) { c.value = c.hi * s2 / 24.0f; okEnds = m_mech.solve(M); }
            const double qHi = M.q[c.qi];
            c.value = c.hi + 0.3f;
            const bool past = m_mech.solve(M);
            const double qAfter = M.q[c.qi];
            const bool pass = okEnds && !past && std::fabs(qAfter - qHi) < 1e-3 && M.warnings.empty();
            passed += pass;
            fprintf(f, "[%s] 19b. butées du vérin : course %.2f..%.2f cm, bout de course %s, au-delà %s (vérin %.3f cm)%s\n", pass ? "OK" : "ÉCHEC", c.lo, c.hi,
                    okEnds ? "atteint" : "NON atteint", past ? "ACCEPTÉ" : "refusé", qAfter, M.warnings.empty() ? "" : " (avertissements)");
        }
    }
    {
        // 20. boom and stick with two actuators (two shock absorbers 2909 + 2910): boom on the chassis (actuator 1
        // chassis -> boom), stick (layer 2) on the last hole of the boom (actuator 2 from the boom, layer 2, to the stick).
        // Actuator 1 driven: the boom moves, the stick keeps its angle on the boom; then actuator 2 driven: the stick
        // moves, the boom stays.
        Mount m = mk();
        const int ch = m.beamAt("32525", 0, float3(0, 0, L0), float3(0, 1, 0));
        const float3 O = m.holeW(ch, 9);
        const float La = m.actLen("2909", "2910");
        int e = 0, k = 4;
        float3 d(1, 0, 0);
        Mount::bestReach([&](int a) { return m.holeW(ch, a); }, 0, 7, O, 1, 8, La, float3(1, 0, 0), e, k, d);
        const float3 E1 = m.holeW(ch, e);
        const int boom = m.beamAt("32525", 0, float3(O.x, O.y, L1), d);
        m.pinAt(float3(O.x, O.y, 0.8f));
        const float3 Bk = m.holeW(boom, k);
        auto c1 = m.actuator("2909", "2910", float3(E1.x, E1.y, L1), Bk);
        m.pinAt(float3(E1.x, E1.y, 0.8f));
        m.pinAt(Bk);
        const float3 P = m.holeW(boom, 9);
        int h = 4, sh = 4;
        float3 ds(0, 1, 0);
        Mount::bestReach([&](int a) { return m.holeW(boom, a); }, 1, 7, P, 1, 9, La, float3(0, 1, 0), h, sh, ds);   // the stick raised: in pure Technic (no studs) the ground is the lowest body
        const int stick = m.beamAt("32525", 0, float3(P.x, P.y, L2), ds);
        m.pinAt(float3(P.x, P.y, 1.6f));
        const float3 Bh = m.holeW(boom, h), Ss = m.holeW(stick, sh);
        auto c2 = m.actuator("2909", "2910", float3(Bh.x, Bh.y, L2), Ss);
        m.pinAt(float3(Bh.x, Bh.y, 1.6f));
        m.pinAt(Ss);
        (void)c1; (void)c2;
        run("20. flèche + balancier, vérin 1 (flèche) piloté", m, 7, 2, {{boom, MOVE}, {stick, 0.0}}, "(le balancier garde son angle sur la flèche : le vérin 2 garde sa longueur)", 10.0f);
        drive = 1;
        run("20b. flèche + balancier, vérin 2 (balancier) piloté", m, 7, 2, {{stick, MOVE}, {boom, 0.0}}, "(la flèche ne bouge pas)", 10.0f);
        drive = 0;
    }
    {
        // 21. bucket on a four-bar linkage driven by an actuator: upright stick (chassis), bucket beam on its hole 0,
        // link on its hole 3, both at the same angle, coupler (layer 2) from the bucket end to the link end (a
        // parallelogram), actuator from a stick hole to the link end
        Mount m = mk();
        const int st = m.beamAt("32525", 0, float3(0, 0, L0), float3(0, 1, 0));
        const float3 A = m.holeW(st, 0), Bp = m.holeW(st, 3);
        const float La = m.actLen("2909", "2910");
        int c = 5;
        float3 d(1, 0, 0);
        int three = 3;
        Mount::bestReach([&](int a) { return m.holeW(st, a); }, 5, 10, Bp, 3, 3, La, float3(1, -1, 0), c, three, d);
        const float3 C = m.holeW(st, c);
        const int bucket = m.beamAt("32524", 0, float3(A.x, A.y, L1), d);
        const int link = m.beamAt("32524", 0, float3(Bp.x, Bp.y, L1), d);
        m.pinAt(float3(A.x, A.y, 0.8f));
        m.pinAt(float3(Bp.x, Bp.y, 0.8f));
        const float3 bE = m.holeW(bucket, 3), lE = m.holeW(link, 3);
        m.beamAt("32524", 0, float3(bE.x, bE.y, L2), float3(0, 1, 0));   // coupler, 3 holes = 2.4 cm = B - A
        m.pinAt(float3(bE.x, bE.y, 1.6f));
        auto cyl = m.actuator("2909", "2910", float3(C.x, C.y, L1), lE);
        m.pinAt(float3(C.x, C.y, 0.8f));
        m.pinAt(float3(lE.x, lE.y, 1.6f));   // link, rod (layer 1) and coupler (layer 2)
        run("21. godet sur quadrilatère piloté par vérin + biellette", m, 6, 1, {{cyl.second, MOVE}, {bucket, MOVE}, {link, MOVE}}, "(le godet et la biellette suivent ; deux boucles fermées)", 10.0f);
    }
    {
        // 22. built on the grid, as in the editor (rotations of 90 degrees only): an L chassis (bent liftarm 32526 + an
        // upright beam 32525 held by two pins), a horizontal arm on the upright hole 6, a vertical actuator from the short leg of the L to the arm, at
        // 90 degrees to the arm. Saved as meca_pelleteuse.json next to the exe (BrickIt scene).
        Mount m = mk();
        const int lp = m.partOf("32526");
        const MechMeta& lm = m_mech.meta(lp);
        const float4 ql = Mount::rotFor(lm.holes[0].axis, Z, &Z, &Y);   // holes along Z, the long leg up (Y), the short leg towards -X
        const int ell = m.add(lp, ql, float3(0, 0, L0) - qRot(ql, lm.holes[0].p));
        const int up = m.beamAt("32525", 0, float3(0, 0, L1), float3(0, 1, 0));
        m.pinAt(float3(0, 0, 0.8f));
        m.pinAt(float3(0, 0.8f, 0.8f));   // two pins: the L and the upright are one part
        const float3 O = m.holeW(up, 6), Bk = m.holeW(ell, 2);
        const int arm = m.beamAt("32525", 0, float3(O.x, O.y, L0), float3(-1, 0, 0));
        m.pinAt(float3(O.x, O.y, 0.8f));
        const float3 Ak = m.holeW(arm, 2);
        auto cyl = m.actuator("2909", "2910", float3(Bk.x, Bk.y, L1), float3(Ak.x, Ak.y, L1));
        m.pinAt(float3(Bk.x, Bk.y, 0.8f));
        m.pinAt(float3(Ak.x, Ak.y, 0.8f));
        run("22. bras horizontal + vérin vertical (sur la grille, comme dans l'éditeur)", m, 4, 1, {{cyl.second, MOVE}, {arm, MOVE}}, "(le vérin à 90° du bras, levier 1,6 cm ; scène enregistrée : docs/brickit/tests/meca_pelleteuse.json)", 10.0f);
        // sign: lengthening the actuator (positive command) raises the end of the arm
        {
            MechModel M;
            ++total;
            bool pass = false;
            float y0 = 0, y1 = 0;
            if (m_mech.build(m.b, M) && !M.cmds.empty()) {
                const float3 tip = m.holeW(arm, 10);
                y0 = M.mats[M.brickBody[arm]].point(tip).y;
                for (int s2 = 1; s2 <= 12; ++s2) { M.cmds[0].value = 0.8f * s2 / 12; m_mech.solve(M); }
                y1 = M.mats[M.brickBody[arm]].point(tip).y;
                pass = y1 > y0 + 0.2f;
            }
            passed += pass;
            fprintf(f, "[%s] 22b. sens : vérin + 0,8 cm, bout du bras %.2f -> %.2f cm (doit monter)\n", pass ? "OK" : "ÉCHEC", y0, y1);
        }
        // the scene, to open it in BrickIt
        const std::vector<Brick> keep = m_build.bricks;
        m_build.bricks = m.b;
        auto col2 = [&](const char* a2, const char* b2) { for (size_t c = 0; c < m_lib.palette.size(); ++c) if (m_lib.palette[c].name == a2 || m_lib.palette[c].name == b2) return (int)c; return 0; };
        for (size_t i = 0; i < m_build.bricks.size(); ++i) {
            const std::string& pn = m_lib.parts[m_build.bricks[i].part].name;
            m_build.bricks[i].color = pn == "3673" ? col2("Light Bluish Grey", "Light Bluish Gray") : pn == "2909" ? col2("Black", "Black") : pn == "2910" ? col2("Light Bluish Grey", "Light Bluish Gray") : (int)i == arm ? col2("Yellow", "Yellow") : col2("Dark Bluish Grey", "Dark Bluish Gray");
        }
        const std::string text = m_build.toJson(m_scene.camera);
        m_build.bricks = keep;
        FILE* sf = nullptr;
        if (_wfopen_s(&sf, (m_exeDir + L"\\meca_pelleteuse.json").c_str(), L"wb") == 0 && sf) { fwrite(text.data(), 1, text.size(), sf); fclose(sf); }
    }
    // 23-28. case of Wzx (capture meca_cas2_engrenages_cote): two Technic bricks 1 x 2 with a hole (3700) side by side,
    // an axle in each hole, a gear on each axle, the two gears meshing between the bricks
    auto cas2 = [&](const char* name, const char* ga, const char* gb, bool plate, float turnY, double ra, double rc) {   // ratios relative to the motor (the smallest gear)
        Mount m = mk();
        const int bp = m.partOf("3700");
        const MechMeta& bm = m_mech.meta(bp);
        const float4 q0(0, 0, 0, 1);
        const int A = m.add(bp, q0, float3(0, 0, 0));
        const int B = m.add(bp, q0, float3(1.6f, 0, 0));   // touching, holes 1.6 cm apart (not linked)
        (void)bm;
        if (plate) {   // both on a plate 1 x 4 (or 2 x 4)
            int pp = m.partOf("3710");
            if (pp < 0) pp = m.partOf("3020");
            if (pp >= 0) m.add(pp, q0, float3(0, -0.32f, 0));
        }
        const float3 pa = m.holeW(A, 0), pb = m.holeW(B, 0);
        m.axle("3706", pa + Z * 0.8f, Z);
        m.axle("3706", pb + Z * 0.8f, Z);
        const int a = m.gear(ga, pa + Z * 1.6f, Z), c = m.gear(gb, pb + Z * 1.6f, Z);
        if (turnY != 0) m.rotateAll(qAxisAngle(Y, turnY));
        run(name, m, plate ? 3 : 4, 1, {{a, ra, true}, {c, rc, true}});
        if (std::string(name).rfind("23.", 0) == 0) {   // the scene, to open it in BrickIt (meca_cas2.json next to the exe)
            const std::vector<Brick> keep = m_build.bricks;
            m_build.bricks = m.b;
            auto col2 = [&](const char* a2, const char* b2) { for (size_t ci = 0; ci < m_lib.palette.size(); ++ci) if (m_lib.palette[ci].name == a2 || m_lib.palette[ci].name == b2) return (int)ci; return 0; };
            for (size_t i = 0; i < m_build.bricks.size(); ++i) {
                const std::string& pn = m_lib.parts[m_build.bricks[i].part].name;
                m_build.bricks[i].color = pn == "3700" ? col2("Medium Azure", "Medium Blue") : pn == "3706" ? col2("Black", "Black") : col2("Light Bluish Grey", "Light Bluish Gray");
            }
            const std::string text = m_build.toJson(m_scene.camera);
            m_build.bricks = keep;
            FILE* sf = nullptr;
            if (_wfopen_s(&sf, (m_exeDir + L"\\meca_cas2.json").c_str(), L"wb") == 0 && sf) { fwrite(text.data(), 1, text.size(), sf); fclose(sf); }
        }
    };
    cas2("23. cas 2 : deux briques 3700 côte à côte, doubles coniques 12 / 20", "32270", "32269", false, 0, 1.0, 12.0 / 20);
    cas2("24. cas 2 : deux briques 3700 côte à côte, 16 / 16 (4019)", "4019", "4019", false, 0, 1.0, 1.0);
    cas2("25. cas 2 : conique simple 20 (32198) / double conique 12", "32198", "32270", false, 0, 12.0 / 20, 1.0);
    cas2("26. cas 2 : 12 / 20 sur une plaque 1 x 4 (un seul corps fixe)", "32270", "32269", true, 0, 1.0, 12.0 / 20);
    cas2("27. cas 2 : 12 / 20, montage tourné de 90° autour de la verticale", "32270", "32269", false, 3.14159265f * 0.5f, 1.0, 12.0 / 20);
    {
        // 28. the two bricks at 90 degrees: double bevels 20 / 12 with axes crossing (one brick turned a quarter turn)
        Mount m = mk();
        const int bp = m.partOf("3700");
        const float rA = m.gearR("32269"), rB = m.gearR("32270");
        const float3 P(0, 0.56f, 0);
        const float4 q0(0, 0, 0, 1), q90 = qAxisAngle(Y, 3.14159265f * 0.5f);
        const MechMeta& bm = m_mech.meta(bp);
        const float3 hA = P + Z * (rB + 2.0f), hB = P + X * (rA + 2.0f);
        m.add(bp, q0, hA - qRot(q0, bm.holes[0].p));
        m.add(bp, q90, hB - qRot(q90, bm.holes[0].p));
        m.axle("3706", P + Z * (rB + 1.6f), Z);
        m.axle("3706", P + X * (rA + 1.6f), X);
        const int a = m.gear("32269", P + Z * rB, Z), c = m.gear("32270", P + X * rA, X);
        run("28. cas 2 : 3700 à 90°, doubles coniques 20 / 12 (axes qui se croisent)", m, 4, 1, {{c, 1.0, true}, {a, 12.0 / 20, true}});
    }
    // 29. every grid placement of two bevel gears at 90 degrees (axes in one plane or a plate apart): the detection
    // against the real contact of the teeth (LDraw meshes: closest vertices of the two gears). Gear centres on the
    // 0.4 cm grid from the crossing point, both sides, the single bevel flipped or not.
    {
        struct G { const char* name; };
        const char* names[] = {"32270", "32269", "32198", "4019"};
        int cases = 0, both = 0, falseNeg = 0, falsePos = 0;
        std::map<std::string, std::array<int, 3>> perPair;   // in mesh and seen, missed, extra
        std::string ex;
        auto verts = [&](int part) -> const std::vector<float3>& {
            static std::map<int, std::vector<float3>> cache;
            auto it = cache.find(part);
            if (it != cache.end()) return it->second;
            std::vector<float3>& v = cache[part];
            const Part* pp = m_lib.get(m_lib.parts[part].name);
            if (pp) for (const auto& vx : pp->mesh.vertices) v.push_back(vx.pos);
            return v;
        };
        for (int ga = 0; ga < 4; ++ga)
            for (int gb = ga; gb < 4; ++gb)
                for (int ia = 1; ia <= 4; ++ia)
                    for (int ib = 1; ib <= 4; ++ib)
                        for (int sideA = -1; sideA <= 1; sideA += 2)
                            for (int dyk = 0; dyk <= 2; dyk += 2)
                                for (int flip = 0; flip < 4; ++flip) {
                                    Mount m = mk();
                                    const float a = 0.4f * ia, b = 0.4f * ib, dy = dyk * 0.16f;
                                    const float3 cA(a * sideA, 1.0f, 0), cB(0, 1.0f + dy, b);
                                    const float3 uA = X * ((flip & 1) ? -1.0f : 1.0f), uB = Z * ((flip & 2) ? -1.0f : 1.0f);
                                    const int A = m.gear(names[ga], cA, uA), B = m.gear(names[gb], cB, uB);
                                    if (A < 0 || B < 0) continue;
                                    MechModel M;
                                    if (!m_mech.build(m.b, M)) continue;
                                    const bool det = !M.meshes.empty();
                                    // contact: closest vertices of the two gears (teeth) under 0.06 cm, and no deep overlap
                                    // (a vertex of one gear inside the other's hub radius)
                                    const std::vector<float3>& va = verts(m.b[A].part);
                                    const std::vector<float3>& vb = verts(m.b[B].part);
                                    float best = 1e9f;
                                    for (size_t k = 0; k < va.size(); k += 2) {
                                        const float3 pa = qRot(m.b[A].rot, va[k]) + m.b[A].pos;
                                        for (size_t l = 0; l < vb.size(); l += 2) {
                                            const float3 pb = qRot(m.b[B].rot, vb[l]) + m.b[B].pos;
                                            best = std::min(best, length(pa - pb));
                                        }
                                    }
                                    const MechMeta& ma = m_mech.meta(m.b[A].part);
                                    const MechMeta& mb = m_mech.meta(m.b[B].part);
                                    // the gear bodies must not go through each other: centre distances to the crossing
                                    // at least the hub of the other
                                    // no deep overlap: no vertex of one gear inside the solid core of the other (inside
                                    // its root circle, pitch radius - 0.12 cm, and its thickness)
                                    auto inside = [&](int G, const MechMeta& gm, int O, const std::vector<float3>& vo) {
                                        const MechGear& g = gm.gears[0];
                                        const float3 c = qRot(m.b[G].rot, g.c) + m.b[G].pos, u = qRot(m.b[G].rot, g.axis);
                                        const Part& pp = m_lib.parts[m.b[G].part];
                                        const float3 ext = pp.bmax - pp.bmin;
                                        const float halfT = std::min(ext.x, std::min(ext.y, ext.z)) * 0.5f - 0.05f, rr = g.teeth * 0.05f - 0.12f;
                                        int n = 0;
                                        for (size_t k = 0; k < vo.size(); ++k) {
                                            const float3 q = qRot(m.b[O].rot, vo[k]) + m.b[O].pos - c;
                                            const float ax = dot(q, u);
                                            if (std::fabs(ax) < halfT && length(q - u * ax) < rr) ++n;
                                        }
                                        return n;
                                    };
                                    const int deep = inside(A, ma, B, vb) + inside(B, mb, A, va);
                                    const bool touch = best < 0.06f && deep == 0;
                                    ++cases;
                                    std::array<int, 3>& pc = perPair[std::string(names[ga]) + " / " + names[gb] + (dy > 0 ? " (décalé)" : "")];
                                    if (det && touch) { ++both; ++pc[0]; }
                                    else if (touch && !det) {
                                        ++falseNeg; ++pc[1];
                                        if (falseNeg <= 25) { char bf[200]; snprintf(bf, sizeof(bf), "      raté : %s / %s, centres à %.1f / %.1f cm du croisement, décalage %.2f, côté %d, sens %d (contact %.3f)\n", names[ga], names[gb], b, a, dy, sideA, flip, best); ex += bf; }
                                    } else if (det && !touch) {
                                        ++falsePos; ++pc[2];
                                        if (falsePos <= 10) { char bf[200]; snprintf(bf, sizeof(bf), "      en trop : %s / %s, centres à %.1f / %.1f cm, décalage %.2f, côté %d, sens %d (écart %.3f)\n", names[ga], names[gb], b, a, dy, sideA, flip, best); ex += bf; }
                                    }
                                }
        const bool pass = falseNeg == 0;   // (diagnostic, not counted: the vertex criterion is wrong for bevel teeth)
        fprintf(f, "[%s] (diagnostic) 29. engrenages à 90° sur la grille : %d placements, %d en prise et détectés, %d en contact non détectés, %d détectés sans contact\n%s",
                pass ? "OK" : "ÉCHEC", cases, both, falseNeg, falsePos, ex.c_str());
        for (auto& kv : perPair) fprintf(f, "      %s : %d en prise vus, %d ratés, %d en trop\n", kv.first.c_str(), kv.second[0], kv.second[1], kv.second[2]);
        fflush(f);
    }
    // 13. one axle through three supports, a 24 on it driven by an 8
    {
        Mount m = mk();
        for (int k = 0; k < 3; ++k) m.hole("6541", 0, float3(0, 0.6f, k * 0.8f), Z, &X);
        m.axle("3707", float3(0, 0.6f, 1.6f), Z);
        const int a = m.gear("3648b", float3(0, 0.6f, 3.2f), Z);
        m.hole("6541", 0, float3(1.6f, 0.6f, 2.0f), Z, &X);
        m.axle("3705", float3(1.6f, 0.6f, 2.8f), Z);
        const int c = m.gear("3647", float3(1.6f, 0.6f, 3.2f), Z);
        run("13. axe traversant trois supports", m, -1, 1, {{c, 1.0}, {a, -8.0 / 24}});
    }
    // 14. the mount of test 1 turned 90 degrees around Y, then 180 around X
    {
        Mount m = mk();
        auto g = beamPair(m, "3647", 0, "3648b", 2);
        m.rotateAll(qMul(qAxisAngle(X, 3.14159265f), qAxisAngle(Y, 3.14159265f * 0.5f)));
        run("14. montage 1 tourné de 90° puis 180°", m, 3, 1, {{g.first, 1.0}, {g.second, -8.0 / 24}});
    }
    // 15. two beams joined by two pins = one rigid body
    {
        Mount m = mk();
        const int A = m.hole("32524", 0, float3(0, 0.6f, 0), Z, &X);
        const float3 h0 = m.holeW(A, 0), h1 = m.holeW(A, 1);
        m.hole("32524", 0, h0 + Z * 0.8f, Z, &X);
        m.axle("3673", h0 + Z * 0.4f, Z);
        m.axle("3673", h1 + Z * 0.4f, Z);
        run("15. deux poutres reliées par deux pins (un seul corps)", m, 1, 0, {});
    }
    // 16. the scene of Wzx (docs/brickit/tests/meca_16_24.json)
    {
        std::wstring path = m_exeDir + L"\\..\\..\\docs\\brickit\\tests\\meca_16_24.json";
        std::vector<Brick> keep = m_bricks;
        Mount m = mk();
        if (openScene(path)) {
            m.b = m_bricks;
            int g16 = -1, g24 = -1;
            for (size_t i = 0; i < m.b.size(); ++i) {
                const std::string& n = m_lib.parts[m.b[i].part].name;
                if (n == "94925") g16 = (int)i;
                if (n == "3648a" || n == "3648b") g24 = (int)i;
            }
            run("16. scène de Wzx (16 / 24 sur deux piles)", m, -1, 1, {{g16, 1.0}, {g24, -16.0 / 24}});
        } else { ++total; fprintf(f, "[ÉCHEC] 16. scène introuvable\n"); }
        m_bricks = keep;
    }
    fprintf(f, "\n%d / %d réussis\n", passed, total);
    fclose(f);
}

} // namespace brickit
