// BrickIt - scripted mouse tests (--mouse-test marquee | gizmo | ring | mech): real ImGui mouse events injected before
// each frame (ImGuiLayer::beforeNewFrame), so the input paths of the selection rectangle, the move / rotate gizmo and
// the drag of a moving part run exactly as with a mouse. The result goes to mouse_test.txt next to the exe.
#include "BrickApp.h"
#include <cmath>
#include <cstdio>

namespace brickit {

void BrickApp::mouseTestInput() {
    ImGuiIO& io = ImGui::GetIO();
    for (const MouseStep& s : m_mouseScript) {
        if (s.frame != m_frame) continue;
        io.AddMousePosEvent(s.x, s.y);
        if (s.btn == 0) io.AddMouseButtonEvent(0, true);
        if (s.btn == 1) io.AddMouseButtonEvent(0, false);
    }
}

// a press at a, a drag to b over 12 frames, a release
static void dragScript(std::vector<BrickApp::MouseStep>& out, int f0, ImVec2 a, ImVec2 b) {
    out.push_back({f0, a.x, a.y, -1});
    out.push_back({f0 + 1, a.x, a.y, 0});
    for (int k = 1; k <= 12; ++k) out.push_back({f0 + 1 + k, a.x + (b.x - a.x) * k / 12.0f, a.y + (b.y - a.y) * k / 12.0f, -1});
    out.push_back({f0 + 14, b.x, b.y, 1});
}

void BrickApp::mouseTestFrame() {
    const std::string& t = m_opt.mouseTest;
    if (t.empty()) return;
    const float sw = m_stage.z - m_stage.x, sh = m_stage.w - m_stage.y;
    View v = view();
    if (m_frame == 10) {
        m_mouseScript.clear();
        if (t == "marquee") {
            m_mode = 0;
            select(-1);
            dragScript(m_mouseScript, 12, ImVec2(m_stage.x + sw * 0.08f, m_stage.y + sh * 0.12f), ImVec2(m_stage.x + sw * 0.92f, m_stage.y + sh * 0.9f));
        } else if (t == "gizmo" || t == "ring") {
            m_mode = t == "gizmo" ? 2 : 3;
            select(3);
            m_mtBefore = m_bricks[3];
            const float3 piv = gizmoPivot(3);
            const float k = gizmoScale(piv);
            float ax, ay, bx, by;
            if (t == "gizmo") {   // the red X arrow, from near its tip, 3 studs further along it on screen
                v.project(piv + float3(0.9f * k, 0, 0), ax, ay);
                v.project(piv + float3(0.9f * k + 2.4f, 0, 0), bx, by);
                dragScript(m_mouseScript, 12, ImVec2(ax, ay), ImVec2(bx, by));
            } else {   // the red ring (around X): a quarter turn along the ring
                // from 45 degrees between +Y and -Z (a point of the X ring only: the Z ring also passes through +Y)
                auto ringPt = [&](float ang) { return piv + (float3(0, 1, 0) * std::cos(ang) + float3(0, 0, -1) * std::sin(ang)) * (0.78f * k); };
                v.project(ringPt(0.785398f), ax, ay);
                m_mouseScript.push_back({12, ax, ay, -1});
                m_mouseScript.push_back({13, ax, ay, 0});
                for (int s = 1; s <= 12; ++s) {
                    const float a = 0.785398f + s / 12.0f * 3.14159265f * 0.5f;
                    v.project(ringPt(a), bx, by);
                    m_mouseScript.push_back({13 + s, bx, by, -1});
                }
                m_mouseScript.push_back({26, bx, by, 1});
            }
        } else if (t == "mech") {
            mechEnter();
            m_mtQ = m_mm.q;
            // a visible point of a moving body (not a root): the first brick whose centre picks a brick of that body
            for (int i = 0; i < (int)m_bricks.size() && m_mouseScript.empty(); ++i) {
                bool root = false;
                for (int r : m_mm.roots) root |= m_mm.brickBody[i] == r;
                if (root) continue;
                Box bx = m_build.aabbOf(m_bricks[i].part, m_bricks[i].rot, m_bricks[i].pos);
                float ax, ay;
                if (!v.project((bx.mn + bx.mx) * 0.5f, ax, ay)) continue;
                const int hit = pickBrick(ax, ay);
                if (hit < 0 || m_mm.brickBody[hit] != m_mm.brickBody[i]) continue;
                dragScript(m_mouseScript, 12, ImVec2(ax, ay), ImVec2(ax + 140 * m_ui, ay + 60 * m_ui));
                m_mtBrick = hit;
            }
        }
    }
    if (m_frame == 16) {   // state in the middle of the drag (diagnostic)
        m_mtDiag = "dragging=" + std::to_string(m_gizmoDragging) + " kind=" + std::to_string(m_gz.h.kind) + " axis=" + std::to_string(m_gz.h.axis) +
                   " steps=" + std::to_string(m_gz.steps) + " hasV0=" + std::to_string(m_gz.hasV0) + " mechDragging=" + std::to_string(m_mechDragging) +
                   " mouse=" + std::to_string((int)ImGui::GetIO().MousePos.x) + "," + std::to_string((int)ImGui::GetIO().MousePos.y) + " down=" + std::to_string(ImGui::IsMouseDown(0)) +
                   " overUi=" + std::to_string(m_overUi);
    }
    if (m_frame == 34) {
        FILE* f = nullptr;
        if (_wfopen_s(&f, (m_exeDir + L"\\mouse_test.txt").c_str(), L"w") != 0 || !f) return;
        if (t == "marquee") fprintf(f, "marquee: selected=%zu of %zu bricks (expected all)\n", selection().size(), m_bricks.size());
        else if (t == "gizmo") {
            const float3 d = m_bricks[3].pos - m_mtBefore.pos;
            fprintf(f, "gizmo move X: delta=(%.3f, %.3f, %.3f) cm, studs=%.2f (expected a whole number > 0 along X only)\n", d.x, d.y, d.z, d.x / kPitch);
        } else if (t == "ring") {
            const float4 a = m_mtBefore.rot, b = m_bricks[3].rot;
            const float c = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
            fprintf(f, "gizmo ring X: angle=%.1f deg (expected 90)\n", 2 * std::acos(std::min(1.0f, c)) * 57.2958f);
        } else if (t == "mech") {
            double mx = 0;
            for (size_t i = 0; i < m_mm.q.size() && i < m_mtQ.size(); ++i) mx = std::max(mx, std::fabs(m_mm.q[i] - m_mtQ[i]));
            fprintf(f, "mech drag: brick=%d joints=%zu max joint change=%.1f deg (expected > 0)\n", m_mtBrick, m_mm.q.size(), mx * 57.2958);
        }
        fprintf(f, "  %s\n", m_mtDiag.c_str());
        fclose(f);
    }
}

} // namespace brickit
