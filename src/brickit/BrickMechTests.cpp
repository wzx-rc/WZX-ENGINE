// BrickIt - automatic tests of the mechanics (--mechtests): mounts built by aligning the connectors of real LDraw parts
// (holes, axles, gear centres), then for each: bodies, commands, and a full turn of the command (36 solver steps) that
// must move every gear by its expected ratio, the meshes held (solver residuals under tolerance). Results in
// mech_tests.txt next to the exe.
#include "BrickApp.h"
#include <cmath>
#include <cstdio>
#include <functional>

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
    auto run = [&](const char* name, Mount& mt, int expBodies, int expCmds, const std::vector<Expect>& ex, const char* note = nullptr) {
        ++total;
        MechModel M;
        const bool built = m_mech.build(mt.b, M);
        std::string why;
        if (!built) why = "construction impossible";
        if (built && expBodies >= 0 && M.bodyCount != expBodies) why += "corps " + std::to_string(M.bodyCount) + " au lieu de " + std::to_string(expBodies) + "; ";
        if (built && expCmds >= 0 && (int)M.cmds.size() != expCmds) why += "commandes " + std::to_string(M.cmds.size()) + " au lieu de " + std::to_string(expCmds) + "; ";
        std::string got;
        if (built && !M.cmds.empty() && !ex.empty()) {
            // a full turn of the first command, in 36 steps (the solver follows the same branch of the loops)
            const int steps = 36;
            bool ok = true;
            int failStep = -1;
            for (int s = 1; s <= steps && ok; ++s) {
                M.cmds[0].value = 2.0f * 3.14159265f * s / steps;
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
            const double turn = M.q[M.cmds[0].qi];
            for (const Expect& e : ex) {
                const int body = M.brickBody[e.brick];
                int qi = -1;
                for (const MechTreeJ& t : M.tree) if (t.child == body) qi = t.qi;
                const double r = qi >= 0 ? M.q[qi] / turn : 0.0;
                char b[96];
                snprintf(b, sizeof(b), "%s%.4f", got.empty() ? "" : ", ", r);
                got += b;
                if (std::fabs((e.absOnly ? std::fabs(r) : r) - e.ratio) > 0.02 * std::max(1.0, std::fabs(e.ratio)) + 1e-3) {
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
        if (note) fprintf(f, "      %s\n", note);
        fflush(f);
    };
    auto mk = [&]() { Mount m; m.app = this; m.lib = &m_lib; m.mech = &m_mech; return m; };
    for (const char* pn : {"32524", "3673", "73230", "6541"}) {   // connectors of the parts used (diagnostic)
        const int pi = m_lib.find(pn);
        if (pi < 0) continue;
        m_lib.get(pn);
        const MechMeta& mm = m_mech.meta(pi);
        fprintf(f, "# %s : %zu trous, %zu mâles, %zu tenons, %zu anti-tenons\n", pn, mm.holes.size(), mm.males.size(), mm.top.size(), mm.bottom.size());
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
    // 12. rack and pinion: a slide, not simulated (no prismatic joint yet)
    {
        ++total;
        fprintf(f, "[NON PRIS EN CHARGE] 12. crémaillère et pignon : une crémaillère avance en ligne droite (liaison glissière) ; le solveur n'a que des pivots pour l'instant\n");
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
    fprintf(f, "\n%d / %d réussis (sur %d montages, la crémaillère n'est pas encore prise en charge)\n", passed, total - 1, total);
    fclose(f);
}

} // namespace brickit
