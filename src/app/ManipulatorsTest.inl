// Command line support of the manipulators (included by Manipulators.cpp): frozen drags / marking menu for captures
// (--gizmo-demo "tool handle pixels menuDir") and the logic checks of the manipulators and of Advanced Scale, written to
// a PASS / FAIL report by --gizmo-test file and run by the editor self-test (--self-test, SelfTest.cpp).

// screen point on a handle of the current manipulator
float2 App::gizmoHandlePoint(int h) {
    float3 pv = m_scene.objects[m_selected].position, ax[3];
    gizmoFrame(m_selected, m_tool, ax);
    float s = gizmoSize(pv);
    float2 pc, p(0, 0);
    project(pv, pc);
    float3 toCam = m_scene.camera.toViewer(pv);
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

// screen direction to drag a handle along (away from the centre; rotate: along the ring)
float2 App::gizmoHandleDir(int h, float2 S) {
    float2 pc;
    project(m_scene.objects[m_selected].position, pc);
    float dx = S.x - pc.x, dy = S.y - pc.y, l = std::max(std::hypot(dx, dy), 1e-3f);
    if (m_tool == TOOL_ROTATE) return h == MH_FREE ? float2(1, 0) : float2(-dy / l, dx / l);
    if (h == MH_CENTER) return m_tool == TOOL_SCALE ? float2(1, 0) : float2(0.8f, -0.6f);
    return float2(dx / l, dy / l);
}

// scripted drag of a handle by px pixels, in several steps like a real drag; returns the handle under the press point
int App::gizmoTestDrag(int tool, int h, float px, bool snap, bool keep) {
    m_tool = (Tool)tool;
    if (!gizmoShown()) return -2;
    float2 S = gizmoHandlePoint(h), dir = gizmoHandleDir(h, S);
    int hit = gizmoHit(S.x, S.y);
    gizmoBegin(h, 0, S.x, S.y);
    for (int i = 1; i <= 8; ++i) {   // (the rotate angle and the trackball accumulate)
        float2 m(S.x + dir.x * px * i / 8, S.y + dir.y * px * i / 8);
        gizmoDrag(m.x, m.y, snap);
        m_gz.prevMouse = m;
    }
    if (!keep) m_gz.active = false;
    return hit;
}

void App::gizmoScript() {
    const bool demo = !m_opt.gizmoDemo.empty(), test = !m_opt.gizmoTest.empty();
    if ((!demo && !test) || m_frameCounter != 8) return;
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
            if (px != 0) gizmoTestDrag(tool, h, px, false, true);
            else m_gzHover = h;
        }
        return;
    }
    int fails = 0;
    std::string rep = gizmoLogicTests(fails);
    rep += fails ? "FAILED " + std::to_string(fails) + "\n" : std::string("ALL PASSED\n");
    writeFileSafe(m_opt.gizmoTest, rep);
    m_running = false;
}

// Logic checks of the manipulators and of Advanced Scale on the current scene (two objects at least): every check
// writes a PASS / FAIL line. The scene is modified (objects moved, two cubes added): reload it afterwards.
std::string App::gizmoLogicTests(int& fails) {
    std::string rep;
    auto check = [&](const std::string& name, bool ok, const std::string& info) {
        rep += std::string(ok ? "PASS " : "FAIL ") + name + "  " + info + "\n";
        if (!ok) ++fails;
    };
    auto fmt = [](const char* f, double a = 0, double b = 0, double c = 0) { char t[160]; snprintf(t, sizeof t, f, a, b, c); return std::string(t); };
    auto conj = [](const float4& q) { return float4(-q.x, -q.y, -q.z, q.w); };
    auto drag = [&](int tool, int h, float px, bool snap, bool keep) { return gizmoTestDrag(tool, h, px, snap, keep); };
    static const char* hname[] = {"X", "Y", "Z", "YZ square", "XZ square", "XY square", "centre"};
    outlinerIds();   // (the hierarchy needs the node ids, given once a frame by the Scene Manager)
    std::vector<int> cand;
    for (size_t i = 0; i < m_scene.objects.size(); ++i) {
        const Object& o = m_scene.objects[i];
        if (o.visible && o.selectable && !o.group && o.mesh >= 0 && o.part != "GROUND" && o.part != "WALL") cand.push_back((int)i);
    }
    const bool magnet = m_magnet;
    const float stepMove = m_stepMove;
    if (cand.size() < 2) check("scene", false, "fewer than 2 objects");
    else {
        const int A = cand[0], B = cand[1];
        auto selSet = [&](std::vector<int> v) { select(-1); select(v[0]); m_multi.assign(v.begin() + 1, v.end()); };
        auto P = [&](int i) { return m_scene.objects[i].position; };
        m_axisMode = 0;
        m_magnet = false;
        m_stepMove = 1.0f;
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

        // the other axes and squares of Move: only the axes of the handle change
        for (int h : {MH_Y, MH_Z, MH_YZ, MH_XY}) {
            bool on[3] = {false, false, false};
            if (h <= MH_Z) on[h] = true;
            else { int u, v; planeAxes(h, u, v); on[u] = on[v] = true; }
            p0 = P(A);
            hit = drag(TOOL_MOVE, h, 70, false, false);
            d = P(A) - p0;
            bool ok = true;
            for (int k = 0; k < 3; ++k) ok = ok && (on[k] ? std::fabs(d[k]) > 1e-3f : std::fabs(d[k]) < 1e-4f);
            check(std::string("move ") + hname[h] + ": hit test", hit == h, fmt("hit=%g", hit));
            check(std::string("move ") + hname[h] + ": only its axes", ok, fmt("d=%.3f %.3f %.3f", d.x, d.y, d.z));
        }

        p0 = P(A);
        hit = drag(TOOL_MOVE, MH_CENTER, 60, false, false);
        d = P(A) - p0;
        float3 r, u, f;
        m_scene.camera.basis(r, u, f);
        check("move centre: hit test", hit == MH_CENTER, fmt("hit=%g", hit));
        check("move centre: in the camera plane", length(d) > 1e-3f && std::fabs(dot(d, f)) < 1e-3f, fmt("|d|=%.3f along view=%.5f", length(d), dot(d, f)));

        // steps: Shift moves by the step of the marking menu; absolute: the pivot lands on the grid of the step
        m_stepMove = 0.25f;
        p0 = P(A);
        drag(TOOL_MOVE, MH_X, 90, true, false);
        d = P(A) - p0;
        check("move X + Shift, step 0.25: multiples of 0.25", std::fabs(d.x * 4 - std::round(d.x * 4)) < 1e-3f && std::fabs(d.x) >= 0.25f, fmt("dx=%.4f", d.x));
        m_stepMove = 1.0f;
        m_scene.objects[A].position.x += 0.37f;
        m_snapAbsolute = true;
        drag(TOOL_MOVE, MH_X, 90, true, false);
        m_snapAbsolute = false;
        check("move snap to grid (absolute): the pivot lands on the grid", std::fabs(P(A).x - std::round(P(A).x)) < 1e-4f, fmt("x=%.4f", P(A).x));

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

        // the X and Z rings: the rotation turns around that world axis
        selSet({A});
        for (int h : {MH_X, MH_Z}) {
            const float4 q0 = objRot(m_scene.objects[A]);
            hit = drag(TOOL_ROTATE, h, 70, false, false);
            const float4 dq = qMul(objRot(m_scene.objects[A]), conj(q0));
            const float3 ax = unitAxis(h), kept = qRot(dq, ax);
            const float turned = 2.0f * std::acos(clampf(std::fabs(dq.w), 0.0f, 1.0f));
            check(std::string("rotate ") + hname[h] + " ring: hit test", hit == h, fmt("hit=%g", hit));
            check(std::string("rotate ") + hname[h] + " ring: turns around " + hname[h], length(kept - ax) < 1e-3f && turned > 0.05f, fmt("angle=%.1f deg", degrees(turned)));
        }

        selSet({A, B});
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
        for (int h : {MH_Y, MH_Z, MH_XZ, MH_XY}) {
            bool on[3] = {false, false, false};
            if (h <= MH_Z) on[h] = true;
            else { int a, b; planeAxes(h, a, b); on[a] = on[b] = true; }
            s0 = m_scene.objects[A].scale;
            hit = drag(TOOL_SCALE, h, 50, false, false);
            s1 = m_scene.objects[A].scale;
            bool ok = true;
            for (int k = 0; k < 3; ++k) ok = ok && (on[k] ? s1[k] > s0[k] * 1.02f : std::fabs(s1[k] - s0[k]) < 1e-5f);
            check(std::string("scale ") + hname[h] + ": hit test", hit == h, fmt("hit=%g", hit));
            check(std::string("scale ") + hname[h] + ": only its axes", ok, fmt("s=%.3f %.3f %.3f", s1.x, s1.y, s1.z));
        }
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
        // parent axes: the child moves along the X of its (turned) parent
        selSet({B});
        m_axisMode = 2;
        pb = P(B);
        drag(TOOL_MOVE, MH_X, 70, false, false);
        outlinerSync();
        db = P(B) - pb;
        const float3 parentX = normalize(qRot(objRot(m_scene.objects[A]), float3(1, 0, 0)));
        check("move Parent axes: along the parent's X", length(db) > 1e-3f && length(cross(normalize(db), parentX)) < 1e-3f && std::fabs(parentX.x) < 0.9999f,
              fmt("|d|=%.3f", length(db)));
        m_axisMode = 0;
        // marking menu: Select Hierarchy adds the children of the selection
        selSet({A});
        markingSelect(3);
        {
            std::vector<int> all = selectionAll();
            check("marking menu Q: Select Hierarchy", all.size() == 2 && std::find(all.begin(), all.end(), B) != all.end(), fmt("n=%g", (double)all.size()));
        }
        m_scene.objects[B].parent = 0;
        outlinerSync();

        // undo of a drag, for each tool
        selSet({A});
        p0 = P(A);
        drag(TOOL_MOVE, MH_Y, 60, false, false);
        undo();
        check("undo after a move", length(P(A) - p0) < 1e-5f, fmt("back by %.5f", length(P(A) - p0)));
        selSet({A});
        {
            const float4 q0 = objRot(m_scene.objects[A]);
            drag(TOOL_ROTATE, MH_Y, 60, false, false);
            undo();
            const float4 q1 = objRot(m_scene.objects[A]);
            check("undo after a rotate", std::fabs(q0.x - q1.x) + std::fabs(q0.y - q1.y) + std::fabs(q0.z - q1.z) + std::fabs(q0.w - q1.w) < 1e-5f, "");
        }
        selSet({A});
        s0 = m_scene.objects[A].scale;
        drag(TOOL_SCALE, MH_X, 50, false, false);
        undo();
        check("undo after a scale", length(m_scene.objects[A].scale - s0) < 1e-6f, "");
        selSet({A});

        // middle button: the last used handle, from anywhere in the view
        m_gzLast[TOOL_MOVE] = MH_Z;
        m_tool = TOOL_MOVE;
        p0 = P(A);
        gizmoBegin(m_gzLast[TOOL_MOVE], 2, m_rcView.x + 40, m_rcView.y + 40);
        gizmoDrag(m_rcView.x + 40, m_rcView.y + 140, false);
        m_gz.active = false;
        d = P(A) - p0;
        check("middle button: last handle (Z) from a corner", std::fabs(d.x) < 1e-4f && std::fabs(d.y) < 1e-4f && std::fabs(d.z) > 1e-3f, fmt("d=%.3f %.3f %.3f", d.x, d.y, d.z));

        // size on screen: an axis is 92 pixels long whatever the distance
        {
            const float3 pv = P(A);
            auto axisPx = [&]() { float2 a, b; project(pv, a); project(pv + r * gizmoSize(pv), b); return std::hypot(b.x - a.x, b.y - a.y); };
            const float dist = m_scene.camera.distance, l0 = axisPx();
            m_scene.camera.distance = dist * 2.5f;
            const float l1 = axisPx();
            m_scene.camera.distance = dist;
            const float want = 92.0f * m_dpi;
            check("manipulator size constant on screen", std::fabs(l0 - want) < 0.02f * want && std::fabs(l1 - want) < 0.02f * want,
                  fmt("%.1f px, %.1f px at 2.5x the distance (expected %.1f)", l0, l1, want));
        }
        // the Select tool has no manipulator
        m_tool = TOOL_SELECT;
        {
            float2 pc;
            project(P(A), pc);
            check("Select tool (Q): no manipulator", !gizmoShown() && gizmoHit(pc.x, pc.y) == MH_NONE, "");
        }
        m_tool = TOOL_MOVE;

        // marking menu actions
        selSet({A});
        markingReset(0);
        check("marking menu W: Reset Position", length(P(A)) < 1e-6f, "");
        markingReset(1);
        {
            const float4 q = objRot(m_scene.objects[A]);
            check("marking menu E: Reset Rotation", std::fabs(q.w) > 0.999999f, "");
        }
        markingReset(2);
        check("marking menu R: Reset Scale", length(m_scene.objects[A].scale - float3(1, 1, 1)) < 1e-6f, "");
        markingSelect(2);
        {
            std::vector<int> all = selectionAll();
            check("marking menu Q: Invert Selection", !all.empty() && std::find(all.begin(), all.end(), A) == all.end(), fmt("n=%g", (double)all.size()));
        }
        markingSelect(0);
        check("marking menu Q: Select All", (int)selectionAll().size() >= 2, fmt("n=%g", (double)selectionAll().size()));
        markingSelect(1);
        check("marking menu Q: Deselect", m_selected < 0, "");
        markingSelect(4);
        check("marking menu R: Advanced Scale", m_advScale, "");
        setAdvScale(false);
    }

    // Advanced Scale: two new cubes (1 unit), the box handles dragged by script
    {
        const int A = app::addPrimitive(m_scene, PRIM_CUBE, float3(30.0f, 0.0f, 0.0f));
        const int B = app::addPrimitive(m_scene, PRIM_CUBE, float3(32.0f, 0.0f, 0.0f));
        outlinerIds();
        m_renderer.setScene(m_scene);
        const Camera camSaved = m_scene.camera;
        m_scene.camera.target = float3(31.0f, 0.5f, 0.0f);   // the cubes in the view
        m_scene.camera.distance = 9.0f;
        auto selSet = [&](std::vector<int> v) { select(-1); select(v[0]); m_multi.assign(v.begin() + 1, v.end()); };
        auto handleIndex = [](int sx, int sy, int sz) { return (sx + 1) * 9 + (sy + 1) * 3 + (sz + 1); };
        // drag of a handle by px pixels away from the box centre on screen (mods: 1 Shift, 2 Alt, 4 V, -1 the keyboard)
        auto drag = [&](int h, float px, int mods) {
            AdvBox b;
            if (!advScaleBox(b)) return false;
            int s[3] = {h / 9 - 1, (h / 3) % 3 - 1, h % 3 - 1};
            float3 p = b.c;
            for (int k = 0; k < 3; ++k) p = p + b.ax[k] * (s[k] * b.h[k]);
            float2 sp, sc;
            if (!project(p, sp) || !project(b.c, sc)) return false;
            float dx = sp.x - sc.x, dy = sp.y - sc.y, l = std::max(std::hypot(dx, dy), 1e-3f);
            m_advMods = mods;
            advScaleBegin(h, b, sp.x, sp.y);
            for (int i = 1; i <= 8; ++i) advScaleDrag(sp.x + dx / l * px * i / 8, sp.y + dy / l * px * i / 8);
            m_adv.active = false;
            m_advMods = -1;
            return true;
        };
        auto box = [&]() { AdvBox b; advScaleBox(b); return b; };
        auto reset = [&](int i, float3 pos) {
            Object& o = m_scene.objects[i];
            o.position = pos;
            o.scale = float3(1, 1, 1);
            o.rot = qIdentity();
            o.useRot = true;
            o.yaw = 0;
        };
        m_advScale = true;
        m_magnet = false;

        selSet({A});
        AdvBox b0 = box();
        const float minX0 = b0.c.x - b0.h.x;
        drag(handleIndex(1, 0, 0), 60, 0);
        AdvBox b1 = box();
        const float3 s1 = m_scene.objects[A].scale;
        check("adv scale +X face: only X grows", s1.x > 1.05f && std::fabs(s1.y - 1) < 1e-4f && std::fabs(s1.z - 1) < 1e-4f, fmt("s=%.3f %.3f %.3f", s1.x, s1.y, s1.z));
        check("adv scale +X face: the -X face stays", std::fabs((b1.c.x - b1.h.x) - minX0) < 1e-3f, fmt("minX %.4f -> %.4f", minX0, b1.c.x - b1.h.x));

        b0 = box();
        drag(handleIndex(1, 0, 0), 40, 2);   // Alt: both sides
        b1 = box();
        check("adv scale Alt: the centre stays", length(b1.c - b0.c) < 1e-3f && b1.h.x > b0.h.x * 1.02f, fmt("dc=%.5f", length(b1.c - b0.c)));

        b0 = box();
        const float3 sBefore = m_scene.objects[A].scale;
        drag(handleIndex(0, 1, 0), 50, 1);   // Shift: uniform, from the top face
        b1 = box();
        const float3 s2 = m_scene.objects[A].scale;
        const float kx = s2.x / sBefore.x, ky = s2.y / sBefore.y, kz = s2.z / sBefore.z;
        check("adv scale Shift: uniform", ky > 1.02f && std::fabs(kx - ky) < 1e-3f && std::fabs(kz - ky) < 1e-3f, fmt("k=%.3f %.3f %.3f", kx, ky, kz));
        check("adv scale Shift: the bottom face stays", std::fabs((b1.c.y - b1.h.y) - (b0.c.y - b0.h.y)) < 1e-3f, "");

        b0 = box();
        const float3 opp0 = b0.c - b0.ax[0] * b0.h.x - b0.ax[1] * b0.h.y - b0.ax[2] * b0.h.z;
        drag(handleIndex(1, 1, 1), 50, 0);   // corner
        b1 = box();
        const float3 opp1 = b1.c - b1.ax[0] * b1.h.x - b1.ax[1] * b1.h.y - b1.ax[2] * b1.h.z;
        check("adv scale corner: the opposite corner stays", length(opp1 - opp0) < 2e-3f && b1.h.x > b0.h.x && b1.h.y > b0.h.y,
              fmt("moved %.5f", length(opp1 - opp0)));
        b0 = box();
        drag(handleIndex(1, 1, 1), 40, 2);   // corner + Alt: around the centre
        b1 = box();
        check("adv scale corner + Alt: the centre stays", length(b1.c - b0.c) < 1e-3f && b1.h.x > b0.h.x * 1.02f, fmt("dc=%.5f", length(b1.c - b0.c)));

        // edge: the two axes of the edge grow, the opposite edge stays
        reset(A, float3(30.0f, 0.0f, 0.0f));
        selSet({A});
        b0 = box();
        const float3 e0 = b0.c - b0.ax[0] * b0.h.x - b0.ax[1] * b0.h.y;
        drag(handleIndex(1, 1, 0), 50, 0);
        b1 = box();
        const float3 e1 = b1.c - b1.ax[0] * b1.h.x - b1.ax[1] * b1.h.y;
        const float3 se = m_scene.objects[A].scale;
        check("adv scale edge: X and Y grow, Z kept", se.x > 1.02f && se.y > 1.02f && std::fabs(se.z - 1) < 1e-4f, fmt("s=%.3f %.3f %.3f", se.x, se.y, se.z));
        check("adv scale edge: the opposite edge stays", length(e1 - e0) < 1e-3f, fmt("moved %.5f", length(e1 - e0)));

        // V: the face goes to the level of the vertex under the cursor (here a corner of the other cube)
        reset(A, float3(30.0f, 0.0f, 0.0f));
        selSet({A});
        {
            const Object& ob = m_scene.objects[B];
            const Affine T = ob.transform();
            float3 w(-1e30f);
            for (const Vertex& v : m_scene.meshes[ob.mesh].vertices) {
                const float3 p = T.point(v.pos);
                if (p.x > w.x + 1e-5f || (std::fabs(p.x - w.x) < 1e-5f && p.y > w.y)) w = p;
            }
            b0 = box();
            float2 sp, q;
            const bool seen = project(b0.c + b0.ax[0] * b0.h.x, sp) && project(w, q);
            if (seen) {
                m_advMods = 4;
                advScaleBegin(handleIndex(1, 0, 0), b0, sp.x, sp.y);
                for (int i = 1; i <= 8; ++i) advScaleDrag(sp.x + (q.x - sp.x) * i / 8, sp.y + (q.y - sp.y) * i / 8);
                m_adv.active = false;
                m_advMods = -1;
            }
            b1 = box();
            check("adv scale V: the face goes to the vertex", seen && m_adv.snapped && std::fabs(b1.c.x + b1.h.x - w.x) < 1e-3f,
                  fmt("maxX %.4f, vertex x %.4f", b1.c.x + b1.h.x, w.x));
        }
        // Snapping on: the face lands on the grid of the move step
        reset(A, float3(30.0f, 0.0f, 0.0f));
        selSet({A});
        m_magnet = true;
        drag(handleIndex(1, 0, 0), 75, -1);
        m_magnet = false;
        b1 = box();
        check("adv scale Snapping: the face lands on the grid", std::fabs((b1.c.x + b1.h.x) - std::round(b1.c.x + b1.h.x)) < 1e-3f && std::fabs(b1.c.x + b1.h.x - 30.5f) > 0.25f,
              fmt("maxX %.4f", b1.c.x + b1.h.x));

        // flat objects: the thin side under the threshold (5 % of the largest side)
        reset(A, float3(30.0f, 0.0f, 0.0f));
        m_scene.objects[A].scale = float3(1.0f, 1.0f, 0.02f);
        selSet({A});
        check("adv scale flat mode: a panel (2 % thick)", box().flatAxis == 2, fmt("flatAxis=%g", box().flatAxis));
        m_scene.objects[A].scale = float3(1.0f, 1.0f, 0.2f);
        check("adv scale flat mode: not for a slab (20 % thick)", box().flatAxis == -1, fmt("flatAxis=%g", box().flatAxis));

        // a mirrored object grows and stays mirrored, its -X face stays
        reset(A, float3(30.0f, 0.0f, 0.0f));
        m_scene.objects[A].scale = float3(-1.0f, 1.0f, 1.0f);
        selSet({A});
        b0 = box();
        drag(handleIndex(1, 0, 0), 50, 0);
        b1 = box();
        {
            const float3 sm = m_scene.objects[A].scale;
            check("adv scale mirrored object: grows, stays mirrored", sm.x < -1.02f && std::fabs(sm.y - 1) < 1e-4f && std::fabs(sm.z - 1) < 1e-4f, fmt("s=%.3f %.3f %.3f", sm.x, sm.y, sm.z));
            check("adv scale mirrored object: the -X face stays", std::fabs((b1.c.x - b1.h.x) - (b0.c.x - b0.h.x)) < 1e-3f, "");
        }

        // two objects, one turned by 90 degrees: world box, each one along its own axes
        reset(A, float3(30.0f, 0.0f, 0.0f));
        reset(B, float3(32.0f, 0.0f, 0.0f));
        Object& oa = m_scene.objects[A];
        oa.rot = qAxisAngle(float3(0, 1, 0), kPi * 0.5f);
        selSet({A, B});
        b0 = box();
        const float minX = b0.c.x - b0.h.x;
        drag(handleIndex(1, 0, 0), 60, 0);
        b1 = box();
        const float3 sa = m_scene.objects[A].scale, sb = m_scene.objects[B].scale;
        check("adv scale 2 objects: world box, -X face stays", std::fabs((b1.c.x - b1.h.x) - minX) < 1e-3f, fmt("minX %.4f -> %.4f", minX, b1.c.x - b1.h.x));
        check("adv scale 2 objects: the turned one grows along its local Z", sa.z > 1.02f && std::fabs(sa.x - 1) < 1e-4f && sb.x > 1.02f && std::fabs(sb.z - 1) < 1e-4f,
              fmt("A=%.3f %.3f %.3f", sa.x, sa.y, sa.z));
        check("adv scale 2 objects: the same factor", std::fabs(sa.z - sb.x) < 1e-3f, fmt("%.4f %.4f", sa.z, sb.x));

        // orientation object (eyedropper): the box of several objects takes its axes
        reset(A, float3(30.0f, 0.0f, 0.0f));
        reset(B, float3(32.0f, 0.0f, 0.0f));
        m_scene.objects[A].rot = qAxisAngle(float3(0, 1, 0), radians(30.0f));
        selSet({A, B});
        m_advRefId = m_scene.objects[A].id;
        {
            const float3 ax = normalize(qRot(objRot(m_scene.objects[A]), float3(1, 0, 0)));
            const float k = dot(box().ax[0], ax);
            m_advRefId = 0;
            const float kw = box().ax[0].x;
            check("adv scale orientation object: the box takes its axes, x: back to the world", m_scene.objects[A].id != 0 && k > 0.9999f && kw > 0.9999f,
                  fmt("dot=%.5f, world %.5f", k, kw));
        }

        // a group: the box around its children, both scaled with it
        reset(A, float3(30.0f, 0.0f, 0.0f));
        reset(B, float3(32.0f, 0.0f, 0.0f));
        selSet({A, B});
        const size_t before = m_scene.objects.size();
        groupSelection();
        int G = -1;
        for (size_t i = before; i < m_scene.objects.size(); ++i) if (m_scene.objects[i].group) G = (int)i;
        if (G >= 0) {
            outlinerSync();
            selSet({G});
            b0 = box();
            drag(handleIndex(1, 0, 0), 60, 0);
            outlinerSync();   // (the children follow their group)
            outlinerSync();
            b1 = box();
            const float3 ga = m_scene.objects[A].scale, gb = m_scene.objects[B].scale;
            check("adv scale group: the box spans both children", std::fabs(b0.h.x * 2 - 3.0f) < 1e-3f, fmt("width %.4f", b0.h.x * 2));
            check("adv scale group: both children scaled, the -X face stays", ga.x > 1.02f && std::fabs(ga.x - gb.x) < 1e-3f && std::fabs((b1.c.x - b1.h.x) - (b0.c.x - b0.h.x)) < 1e-3f,
                  fmt("sA=%.3f sB=%.3f", ga.x, gb.x));
        } else check("adv scale group: Ctrl+G made a group", false, "");

        // undo / redo of a drag
        reset(A, float3(30.0f, 0.0f, 0.0f));
        selSet({A});
        const float3 su = m_scene.objects[A].scale;
        drag(handleIndex(0, 0, 1), 40, 0);
        const float3 sd = m_scene.objects[A].scale;
        undo();
        check("adv scale undo", length(m_scene.objects[A].scale - su) < 1e-5f, "");
        redo();
        check("adv scale redo", length(m_scene.objects[A].scale - sd) < 1e-5f, "");
        m_advScale = false;
        m_scene.camera = camSaved;
    }
    m_magnet = magnet;
    m_stepMove = stepMove;
    {   // viewport anti-aliasing: the MSAA sample counts this GPU supports
        std::string aa = "msaa supported:";
        for (uint32_t n : {2u, 4u, 8u, 16u}) if (m_renderer.msaaSupported(n)) aa += " x" + std::to_string(n);
        rep += aa + "\n";
    }
    return rep;
}
