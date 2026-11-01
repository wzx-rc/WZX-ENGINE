// BrickIt - Mechanical mode in the app: enter / exit, commands (keys, sliders), the bricks drawn at the pose of their
// body, the panel and the joint overlay; a Technic gear train demo.
#include "BrickApp.h"
#include "app/Theme.h"
#include <algorithm>
#include <cmath>

namespace brickit {

namespace th = app::th;

void BrickApp::mechEnter() {
    if (m_holding) cancelHeld();
    m_selected = -1;
    m_multi.clear();
    LARGE_INTEGER t0, t1;
    QueryPerformanceCounter(&t0);
    m_mechOn = m_mech.build(m_bricks, m_mm);
    QueryPerformanceCounter(&t1);
    m_mm.buildMs = (t1.QuadPart - t0.QuadPart) * 1000.0 / m_qpf.QuadPart;
    if (!m_mechOn) { toast("Scène vide : construis un mécanisme avec des pièces Technic, puis repasse en mode Mécanique (M)"); return; }
    syncObjects();
    mechApplyPose();
    toast("Mode Mécanique : A/Q, Z/S… pilotent les moteurs, M ou Échap pour sortir");
}

void BrickApp::mechExit() {
    if (!m_mechOn) return;
    m_mechOn = false;
    m_mm = MechModel();
    syncObjects();   // back to the building pose
}

// Brick objects at body matrix * brick pose (position + quaternion from the matrix columns).
void BrickApp::mechApplyPose() {
    for (size_t i = 0; i < m_bricks.size(); ++i) {
        Affine a = m_mech.brickMatrix(m_mm, m_bricks[i], (int)i);
        Object& o = m_scene.objects[m_firstBrickObject + i];
        o.position = float3(a.m[0][3], a.m[1][3], a.m[2][3]);
        o.rot = qNormalize(qFromBasis(float3(a.m[0][0], a.m[1][0], a.m[2][0]), float3(a.m[0][1], a.m[1][1], a.m[2][1]),
                                      float3(a.m[0][2], a.m[1][2], a.m[2][2])));
        o.useRot = true;
    }
    ++m_scene.objectsVersion;
    m_renderer.resetAccumulation();
    m_lastMove = ImGui::GetTime();
}

void BrickApp::mechUpdate(float dt) {
    if (!m_mechOn) return;
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape)) { mechExit(); return; }
    // left drag on a moving part: the point grabbed follows the cursor on the plane facing the camera
    if (!m_overUi && !io.WantCaptureMouse && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt) {
        float3 hit;
        int b = pickBrick(io.MousePos.x, io.MousePos.y, &hit);
        if (b >= 0) {
            int body = m_mm.brickBody[b];
            if (std::find(m_mm.roots.begin(), m_mm.roots.end(), body) == m_mm.roots.end()) {
                m_mechDrag.body = body;
                m_mechDrag.local = m_mm.mats[body].inverse().point(hit);
                m_mechDrag.target = hit;
                float3 r, u, f;
                m_scene.camera.basis(r, u, f);
                m_mechDragPlaneN = f;
                m_mechDragging = true;
            }
        }
    }
    if (m_mechDragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) m_mechDragging = false;
    if (m_mechDragging && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) {
        float3 o, d;
        view().ray(io.MousePos.x, io.MousePos.y, o, d);
        float3 p0 = m_mm.mats[m_mechDrag.body].point(m_mechDrag.local);
        float den = dot(d, m_mechDragPlaneN);
        if (std::fabs(den) > 1e-5f) {
            m_mechDrag.target = o + d * (dot(p0 - o, m_mechDragPlaneN) / den);
            m_mech.solve(m_mm, &m_mechDrag);
            mechApplyPose();
        }
    }
    std::vector<float> before;
    bool moved = false;
    for (MechCmd& c : m_mm.cmds) {
        before.push_back(c.value);
        int dir = 0;
        if (!io.WantTextInput) {
            if (c.keyPlus && ImGui::IsKeyDown((ImGuiKey)c.keyPlus)) ++dir;
            if (c.keyMinus && ImGui::IsKeyDown((ImGuiKey)c.keyMinus)) --dir;
        }
        if (dir) { c.value += dir * c.speed * dt; moved = true; }
    }
    static std::vector<float> lastSlider;
    if (lastSlider.size() != m_mm.cmds.size()) lastSlider.assign(m_mm.cmds.size(), 0);
    for (size_t k = 0; k < m_mm.cmds.size(); ++k)
        if (m_mm.cmds[k].value != lastSlider[k]) moved = true;
    if (!moved) return;
    bool ok = m_mech.solve(m_mm);
    if (!ok) {   // big jump: approach in small steps, keep the last reachable pose
        std::vector<float> target, good = before;
        for (MechCmd& c : m_mm.cmds) target.push_back(c.value);
        ok = true;
        for (int s = 1; s <= 12 && ok; ++s) {
            for (size_t k = 0; k < m_mm.cmds.size(); ++k) m_mm.cmds[k].value = before[k] + (target[k] - before[k]) * s / 12;
            if (m_mech.solve(m_mm)) for (size_t k = 0; k < m_mm.cmds.size(); ++k) good[k] = m_mm.cmds[k].value;
            else ok = false;
        }
        for (size_t k = 0; k < m_mm.cmds.size(); ++k) m_mm.cmds[k].value = good[k];
        if (!ok) { m_mechFlash = "Butée : le mécanisme ne peut pas aller plus loin"; m_mechFlashTime = ImGui::GetTime(); }
    }
    for (size_t k = 0; k < m_mm.cmds.size(); ++k) lastSlider[k] = m_mm.cmds[k].value;
    mechApplyPose();
}

void BrickApp::drawMechOverlay() {
    if (!m_mechOn || !m_mechJoints) return;
    const float s = m_ui;
    // overlay: pivots (yellow rings), loop pivots (orange), gear meshes (pink lines with the ratio)
    View v = view();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();   // over the 3D view (drawn after it), under the panels
    auto ring = [&](const float3& p, const float3& u, int body, ImU32 col) {
        const Affine& M = m_mm.mats[body];
        float3 c = M.point(p), uu = normalize(M.vector(u));
        float3 a = std::fabs(uu.y) < 0.9f ? normalize(cross(uu, float3(0, 1, 0))) : normalize(cross(uu, float3(1, 0, 0))), b = cross(uu, a);
        ImVec2 pts[24];
        int n = 0;
        for (int i = 0; i < 24; ++i) {
            float t = i * 2 * kPi / 24;
            float3 w = c + (a * std::cos(t) + b * std::sin(t)) * 0.46f;
            float sx, sy;
            if (v.project(w, sx, sy)) pts[n++] = ImVec2(sx, sy);
        }
        if (n > 2) dl->AddPolyline(pts, n, col, ImDrawFlags_Closed, 2.0f * s);
    };
    for (const MechTreeJ& t : m_mm.tree) ring(t.j.p, t.j.u, t.parent, IM_COL32(247, 197, 30, 220));
    for (const MechJoint& c : m_mm.closures) ring(c.p, c.u, c.a, IM_COL32(255, 122, 69, 230));
    for (auto& ms : m_mm.meshes) {
        const MechGearW &A = m_mm.gears[ms.first], &B = m_mm.gears[ms.second];
        float3 a = m_mm.mats[A.body].point(A.c), b = m_mm.mats[B.body].point(B.c);
        float ax, ay, bx, by;
        if (!v.project(a, ax, ay) || !v.project(b, bx, by)) continue;
        dl->AddLine(ImVec2(ax, ay), ImVec2(bx, by), IM_COL32(255, 95, 210, 230), 2.0f * s);
        char lab[16];
        snprintf(lab, sizeof(lab), "%d:%d", A.teeth, B.teeth);
        dl->AddText(ImVec2((ax + bx) * 0.5f, (ay + by) * 0.5f - 16 * s), IM_COL32(255, 95, 210, 255), lab);
    }
}

// Gear train on a Technic frame: four 1 x 1 bricks with a hole on a 1 x 8 brick, axles 4 through the holes,
// 16 / 16 / 16 + 8 / 24 tooth gears (2 studs between the axles). Positions are computed from the connectors of the
// parts, so the detection finds the pivots and the meshes.
void BrickApp::buildMechDemo() {
    auto partOf = [&](const char* name) { int i = m_lib.find(name); return i >= 0 && m_lib.get(name) ? i : -1; };
    const int gear24 = partOf("3648b");   // Technic Gear 24 Tooth with Single Axle Hole
    const int frame = partOf("3008"), hole = partOf("6541"), axle = partOf("3705"), g16 = partOf("94925"), g8 = partOf("3647");
    if (frame < 0 || hole < 0 || axle < 0 || g16 < 0 || g8 < 0) { toast("Pièces Technic manquantes pour la démo"); return; }
    const float4 id(0, 0, 0, 1), alongZ = qAxisAngle(float3(0, 1, 0), -kPi * 0.5f);   // axle: local X -> world Z
    auto add = [&](int part, int color, const float3& pos, const float4& rot) {
        Brick b;
        b.part = part;
        b.color = colorIndex(color);
        b.pos = pos;
        b.rot = rot;
        m_bricks.push_back(b);
    };
    add(frame, 72, float3(0, 0, 0), id);
    // gear: its hole centre (local) put at a point of the axle
    auto addGear = [&](int part, int color, const float3& at) {
        const MechMeta& mt = m_mech.meta(part);
        float3 c = mt.gears.empty() ? float3(0) : mt.gears[0].c;
        add(part, color, at - c, id);
    };
    const MechMeta& hm = m_mech.meta(hole);
    const float3 holeC = hm.holes.empty() ? float3(0.4f, 0.56f, 0.4f) : hm.holes[0].p;
    for (int k = 0; k < 4; ++k) {
        float3 hpos(k * 2 * kPitch, 0.96f, 0);
        add(hole, 0, hpos, id);
        float3 hc = hpos + holeC;   // hole centre, axis Z
        // axle 4 (3.2 cm) from z = -0.4 to 2.8: its centre (local 1.6, 0.24, 0.24) at (hc.x, hc.y, 1.2)
        float3 axleC = qRot(alongZ, float3(1.6f, 0.24f, 0.24f));
        add(axle, 71, float3(hc.x, hc.y, 1.2f) - axleC, alongZ);
        if (k <= 2) addGear(g16, 14, float3(hc.x, hc.y, 1.6f));
        if (k == 2) addGear(g8, 4, float3(hc.x, hc.y, 2.4f));
        if (k == 3 && gear24 >= 0) addGear(gear24, 1, float3(hc.x, hc.y, 2.4f));
    }
    m_status = "Démo mécanique : train d'engrenages";
}

} // namespace brickit
