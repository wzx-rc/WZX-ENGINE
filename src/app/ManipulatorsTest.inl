// Command line support of the manipulators (included by Manipulators.cpp): frozen drags / marking menu for captures
// (--gizmo-demo "tool handle pixels menuDir") and a self-test that writes a PASS / FAIL report (--gizmo-test file).

// screen point on a handle of the current manipulator
float2 App::gizmoHandlePoint(int h) {
    float3 pv = m_scene.objects[m_selected].position, ax[3];
    gizmoFrame(m_selected, m_tool, ax);
    float s = gizmoSize(pv);
    float2 pc, p(0, 0);
    project(pv, pc);
    float3 toCam = normalize(m_scene.camera.eye() - pv);
    if (m_tool == TOOL_ROTATE) {
        float rPx = 92.0f * m_dpi;
        if (h == MH_FREE) return float2(pc.x + 0.3f * rPx, pc.y - 0.2f * rPx);
        if (h == MH_VIEW) return float2(pc.x + kViewRing * rPx * 0.7071f, pc.y - kViewRing * rPx * 0.7071f);
        // the ring point facing the camera most, turned a little so the other rings do not cover it
        float3 n = ax[h], t = toCam - n * dot(toCam, n);
        float3 dir = length(t) > 1e-3f ? normalize(t) : ax[(h + 1) % 3];
        dir = qRot(qAxisAngle(n, 0.6f), dir);
        project(pv + dir * (kRing * s), p);
        return p;
    }
    if (h == MH_CENTER) return pc;
    if (h <= MH_Z) { project(pv + ax[h] * (0.6f * s), p); return p; }
    int u, v;
    planeAxes(h, u, v);
    project(pv + (ax[u] + ax[v]) * ((kPlane0 + kPlane1) * 0.5f * s), p);
    return p;
}

void App::gizmoScript() {
    const bool demo = !m_opt.gizmoDemo.empty(), test = !m_opt.gizmoTest.empty();
    if ((!demo && !test) || m_frameCounter != 8) return;
    // screen direction to drag a handle along
    auto handleDir = [&](int h, float2 S) {
        float2 pc;
        project(m_scene.objects[m_selected].position, pc);
        float dx = S.x - pc.x, dy = S.y - pc.y, l = std::max(std::hypot(dx, dy), 1e-3f);
        if (m_tool == TOOL_ROTATE) return h == MH_FREE ? float2(1, 0) : float2(-dy / l, dx / l);
        if (h == MH_CENTER) return m_tool == TOOL_SCALE ? float2(1, 0) : float2(0.8f, -0.6f);
        return float2(dx / l, dy / l);
    };
    auto drag = [&](int tool, int h, float px, bool snap, bool keep) {
        m_tool = (Tool)tool;
        if (!gizmoShown()) return -2;
        float2 S = gizmoHandlePoint(h), dir = handleDir(h, S);
        int hit = gizmoHit(S.x, S.y);
        gizmoBegin(h, 0, S.x, S.y);
        // several steps, like a real drag (the rotate angle and the trackball accumulate)
        for (int i = 1; i <= 8; ++i) {
            float2 m(S.x + dir.x * px * i / 8, S.y + dir.y * px * i / 8);
            gizmoDrag(m.x, m.y, snap);
            m_gz.prevMouse = m;
        }
        if (!keep) m_gz.active = false;
        return hit;
    };

    if (demo) {
        int tool = 1, h = -1, mmDir = -1;
        float px = 0;
        swscanf_s(m_opt.gizmoDemo.c_str(), L"%d %d %f %d", &tool, &h, &px, &mmDir);
        m_gzDemo = true;
        m_tool = (Tool)tool;
        if (mmDir >= 0) {
            m_mm.open = true;
            m_mm.tool = tool;
            m_mm.x = m_rcView.x + m_rcView.w * 0.5f;
            m_mm.y = m_rcView.y + m_rcView.h * 0.5f;
            m_mm.hover = mmDir;
            float a = mmDir * kPi / 4;
            m_mm.mx = m_mm.x + std::sin(a) * 60 * m_dpi;
            m_mm.my = m_mm.y - std::cos(a) * 50 * m_dpi;
        } else if (h >= 0 && gizmoShown()) {
            if (px != 0) drag(tool, h, px, false, true);
            else m_gzHover = h;
        }
        return;
    }

    // self-test: every check writes PASS / FAIL
    std::string rep;
    int fails = 0;
    auto check = [&](const char* name, bool ok, const std::string& info) {
        rep += std::string(ok ? "PASS " : "FAIL ") + name + "  " + info + "\n";
        if (!ok) ++fails;
    };
    auto fmt = [](const char* f, double a = 0, double b = 0, double c = 0) { char t[160]; snprintf(t, sizeof t, f, a, b, c); return std::string(t); };
    std::vector<int> cand;
    for (size_t i = 0; i < m_scene.objects.size(); ++i) {
        const Object& o = m_scene.objects[i];
        if (o.visible && o.selectable && !o.group && o.mesh >= 0 && o.part != "GROUND" && o.part != "WALL") cand.push_back((int)i);
    }
    if (cand.size() < 2) check("scene", false, "fewer than 2 objects");
    else {
        const int A = cand[0], B = cand[1];
        auto selSet = [&](std::vector<int> v) { select(-1); select(v[0]); m_multi.assign(v.begin() + 1, v.end()); };
        auto P = [&](int i) { return m_scene.objects[i].position; };
        m_axisMode = 0;
        m_magnet = false;
        rep += "objects: " + m_scene.objects[A].name + ", " + m_scene.objects[B].name + "\n";

        selSet({A});
        float3 p0 = P(A);
        int hit = drag(TOOL_MOVE, MH_X, 80, false, false);
        float3 d = P(A) - p0;
        check("move X: hit test", hit == MH_X, fmt("hit=%g", hit));
        check("move X: only X changes", d.x > 0.05f && std::fabs(d.y) < 1e-4f && std::fabs(d.z) < 1e-4f, fmt("d=%.3f %.3f %.3f", d.x, d.y, d.z));

        p0 = P(A);
        drag(TOOL_MOVE, MH_X, 130, true, false);
        d = P(A) - p0;
        check("move X + Shift: whole steps", std::fabs(d.x - std::round(d.x)) < 1e-4f && std::fabs(d.x) >= 1.0f, fmt("dx=%.4f", d.x));

        p0 = P(A);
        hit = drag(TOOL_MOVE, MH_XZ, 70, false, false);
        d = P(A) - p0;
        check("move XZ square: hit test", hit == MH_XZ, fmt("hit=%g", hit));
        check("move XZ square: Y unchanged, X and Z move", std::fabs(d.y) < 1e-4f && std::fabs(d.x) > 1e-3f && std::fabs(d.z) > 1e-3f, fmt("d=%.3f %.3f %.3f", d.x, d.y, d.z));

        p0 = P(A);
        hit = drag(TOOL_MOVE, MH_CENTER, 60, false, false);
        d = P(A) - p0;
        float3 r, u, f;
        m_scene.camera.basis(r, u, f);
        check("move centre: hit test", hit == MH_CENTER, fmt("hit=%g", hit));
        check("move centre: in the camera plane", length(d) > 1e-3f && std::fabs(dot(d, f)) < 1e-3f, fmt("|d|=%.3f along view=%.5f", length(d), dot(d, f)));

        // multi-selection rotate: each object on its own pivot
        selSet({A, B});
        float3 pa = P(A), pb = P(B);
        hit = drag(TOOL_ROTATE, MH_Y, 90, true, false);
        float ang = m_gz.value.x;
        check("rotate Y: hit test", hit == MH_Y, fmt("hit=%g", hit));
        check("rotate Y + Shift: 15 degree steps", std::fabs(ang) >= 15 && std::fmod(std::fabs(ang) + 1e-3f, 15.0f) < 2e-3f, fmt("angle=%.3f", ang));
        float3 xa = qRot(m_scene.objects[A].rot, float3(1, 0, 0)), xb = qRot(m_scene.objects[B].rot, float3(1, 0, 0));
        check("rotate 2 objects: pivots unchanged", length(P(A) - pa) < 1e-5f && length(P(B) - pb) < 1e-5f, "");
        check("rotate 2 objects: both turned around Y", m_scene.objects[A].useRot && m_scene.objects[B].useRot && std::fabs(xa.y) < 1e-4f && std::fabs(xb.y) < 1e-4f,
              fmt("xA=%.3f %.3f %.3f", xa.x, xa.y, xa.z));

        hit = drag(TOOL_ROTATE, MH_VIEW, 60, false, false);
        check("rotate view ring: hit test", hit == MH_VIEW, fmt("hit=%g", hit));
        hit = drag(TOOL_ROTATE, MH_FREE, 60, false, false);
        check("rotate trackball: hit test", hit == MH_FREE, fmt("hit=%g", hit));

        // object axes: the move follows the object's own X
        selSet({A});
        m_axisMode = 1;
        p0 = P(A);
        drag(TOOL_MOVE, MH_X, 80, false, false);
        d = P(A) - p0;
        float3 lx = normalize(qRot(objRot(m_scene.objects[A]), float3(1, 0, 0)));
        check("move Object axes: along the local X", length(d) > 1e-3f && length(cross(normalize(d), lx)) < 1e-3f, fmt("|d|=%.3f", length(d)));
        m_axisMode = 0;

        // scale
        float3 s0 = m_scene.objects[A].scale;
        hit = drag(TOOL_SCALE, MH_X, 60, false, false);
        float3 s1 = m_scene.objects[A].scale;
        check("scale X: hit test", hit == MH_X, fmt("hit=%g", hit));
        check("scale X: only X", s1.x > s0.x * 1.05f && std::fabs(s1.y - s0.y) < 1e-5f && std::fabs(s1.z - s0.z) < 1e-5f, fmt("s=%.3f %.3f %.3f", s1.x, s1.y, s1.z));
        s0 = s1;
        hit = drag(TOOL_SCALE, MH_YZ, 50, false, false);
        s1 = m_scene.objects[A].scale;
        check("scale YZ square: hit test", hit == MH_YZ, fmt("hit=%g", hit));
        check("scale YZ square: Y and Z, not X", std::fabs(s1.x - s0.x) < 1e-5f && s1.y > s0.y && s1.z > s0.z, fmt("s=%.3f %.3f %.3f", s1.x, s1.y, s1.z));
        selSet({A, B});
        float3 sa = m_scene.objects[A].scale, sb = m_scene.objects[B].scale;
        pa = P(A);
        pb = P(B);
        hit = drag(TOOL_SCALE, MH_CENTER, 46, true, false);
        float ka = m_scene.objects[A].scale.y / sa.y, kb = m_scene.objects[B].scale.x / sb.x;
        check("scale centre cube: hit test", hit == MH_CENTER, fmt("hit=%g", hit));
        check("scale centre, 2 objects, Shift: same 0.1 step, pivots kept",
              std::fabs(ka - kb) < 1e-4f && ka > 1.05f && std::fabs(ka * 10 - std::round(ka * 10)) < 1e-3f && length(P(A) - pa) < 1e-5f && length(P(B) - pb) < 1e-5f,
              fmt("k=%.4f %.4f", ka, kb));

        // parent + child both selected: the child moves once (with its parent)
        m_scene.objects[B].parent = m_scene.objects[A].id;
        outlinerSync();
        selSet({A, B});
        check("parent + child selected: one target", gizmoTargets().size() == 1, fmt("targets=%g", (double)gizmoTargets().size()));
        pa = P(A);
        pb = P(B);
        drag(TOOL_MOVE, MH_X, 80, false, false);
        outlinerSync();
        outlinerSync();
        float3 da = P(A) - pa, db = P(B) - pb;
        check("parent + child move: child follows once", length(da) > 1e-3f && length(da - db) < 1e-4f, fmt("dA=%.3f dB=%.3f", da.x, db.x));
        selSet({A});
        float dist0 = length(P(B) - P(A));
        float3 pb1 = P(B);
        drag(TOOL_ROTATE, MH_Y, 90, true, false);
        outlinerSync();
        check("parent rotate: child turns around the parent", std::fabs(length(P(B) - P(A)) - dist0) < 1e-3f && length(P(B) - pb1) > 1e-3f,
              fmt("dist %.3f -> %.3f", dist0, length(P(B) - P(A))));
        m_scene.objects[B].parent = 0;
        outlinerSync();

        // undo of a drag
        selSet({A});
        p0 = P(A);
        drag(TOOL_MOVE, MH_Y, 60, false, false);
        undo();
        check("undo after a move", length(P(A) - p0) < 1e-5f, fmt("back by %.5f", length(P(A) - p0)));

        // middle button: the last used handle, from anywhere in the view
        m_gzLast[TOOL_MOVE] = MH_Z;
        m_tool = TOOL_MOVE;
        p0 = P(A);
        gizmoBegin(m_gzLast[TOOL_MOVE], 2, m_rcView.x + 40, m_rcView.y + 40);
        gizmoDrag(m_rcView.x + 40, m_rcView.y + 140, false);
        m_gz.active = false;
        d = P(A) - p0;
        check("middle button: last handle (Z) from a corner", std::fabs(d.x) < 1e-4f && std::fabs(d.y) < 1e-4f && std::fabs(d.z) > 1e-3f, fmt("d=%.3f %.3f %.3f", d.x, d.y, d.z));

        // marking menu actions
        markingReset(2);
        check("marking menu R: Reset Scale", length(m_scene.objects[A].scale - float3(1, 1, 1)) < 1e-6f, "");
        markingSelect(0);
        check("marking menu Q: Select All", (int)selectionAll().size() >= 2, fmt("n=%g", (double)selectionAll().size()));
        markingSelect(1);
        check("marking menu Q: Deselect", m_selected < 0, "");
    }
    rep += fails ? "FAILED " + std::to_string(fails) + "\n" : std::string("ALL PASSED\n");
    writeFileSafe(m_opt.gizmoTest, rep);
    m_running = false;
}
