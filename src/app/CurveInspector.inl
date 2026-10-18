// Actor tab of a curve object (Create > Curve). Included by AtelierUI.cpp inside namespace app, after its widgets
// (Box, Text, Label, SliderRow, AxisFields, Combo, Button, Check): kept in its own file with the curve tools.

void CurveEditor::inspector(float x, float& y, float w) {
    App& A = a;
    Object* o = &A.m_scene.objects[A.m_selected];
    CurveData& c = o->curve;
    bool act = false;

    // header card
    Box({x, y, w, 56 * s}, Fade(P().accent, 30), 4, true);
    Glyph(Icon::Curve, x + 12 * s, y + 15 * s, 24, IM_COL32(98, 200, 150, 255));
    dl->PushClipRect(ImVec2(x, y), ImVec2(x + w - 8 * s, y + 56 * s), true);
    Text(x + 48 * s, y + 11 * s, o->name, P().text, 14);
    std::string type = c.shape.type != CSHAPE_NONE ? std::string("Curve primitive · ") + curveShapeName(c.shape.type)
                                                   : std::string("Curve · ") + curveTypeName(c.type);
    if (!A.m_multi.empty()) type += "  (+" + std::to_string(A.m_multi.size()) + ")";
    Text(x + 48 * s, y + 33 * s, type, P().accent, 10);
    dl->PopClipRect();
    y += 69 * s;

    Label(x, y, "TRANSFORM");
    y += 24 * s;
    Text(x, y, "Position", P().muted, 11);
    y += 19 * s;
    if (AxisFields("cpos", x, y, w, &o->position.x, 0.1f, "%.3f", act)) A.markDirty();
    y += 35 * s;
    float3 e = eulerYXZ(A.objRot(*o));
    Text(x, y, "Rotation", P().muted, 11);
    y += 19 * s;
    if (AxisFields("crot", x, y, w, &e.x, 0.5f, "%.1f", act)) { o->rot = quatYXZ(e); o->useRot = true; o->yaw = radians(e.y); A.markDirty(); }
    y += 35 * s;
    Text(x, y, "Scale", P().muted, 11);
    y += 19 * s;
    if (AxisFields("cscl", x, y, w, &o->scale.x, 0.01f, "%.2f", act)) { o->scale = maxv(o->scale, float3(0.01f)); A.markDirty(); }
    y += 41 * s;

    Label(x, y, "CURVE");
    y += 24 * s;
    Text(x, y, "Type", P().muted, 11);
    y += 19 * s;
    {
        static const char* types[] = {"Linear", "CV Curve", "EP Curve", "Bezier"};
        int t = std::clamp(c.type - 1, 0, 3);
        if (Combo("##crvType", {x, y, w, 27 * s}, t, types, 4, Icon::Curve) && t + 1 != c.type) {
            A.pushUndo();
            convertCurve(c, t + 1);
            A.markDirty();
        }
    }
    y += 37 * s;
    bool closed = c.closed;
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    if (Check("Closed", &closed)) {
        if (closed && c.count() < 3) A.status(L"A closed curve needs at least 3 points", 2);
        else { A.pushUndo(); c.closed = closed; c.shape.type = CSHAPE_NONE; A.markDirty(); }
    }
    y += 32 * s;
    {
        char info[128];
        snprintf(info, sizeof(info), "Degree %d  ·  %d points  ·  %d spans  ·  length %.2f", curveDegree(c), c.count(), curveSpans(c), curveLength(c));
        Text(x, y, info, P().muted, 10);
        y += 22 * s;
    }
    {
        float st = (float)c.steps;
        if (SliderRow("##crvSteps", "Display Smoothness", x, y, w, &st, 2, 64, "%.0f", act)) { c.steps = std::clamp((int)std::lround(st), 2, 64); A.markDirty(); }
    }
    float bw = (w - 6 * s) / 2;
    if (Button("crvEdit", {x, y, bw, 27 * s}, m_mode == MODE_POINTS ? "Exit Points (F8)" : "Edit Points (F8)", Icon::Move, m_mode == MODE_POINTS)) {
        if (m_mode == MODE_POINTS) exitMode(); else enterPoints();
    }
    if (Button("crvRev", {x + bw + 6 * s, y, bw, 27 * s}, "Reverse", Icon::Refresh)) { A.pushUndo(); reverseCurve(c); m_sel.clear(); A.markDirty(); }
    y += 35 * s;
    if (m_mode == MODE_POINTS) {   // fillet (3ds Max Editable Spline): rounds the corners of the points
        Label(x, y, "FILLET");
        y += 24 * s;
        if (Button("crvFilletTool", {x, y, w, 27 * s}, m_filletTool ? "Fillet Tool: drag a point up (Esc ends)" : "Fillet Tool (drag a point up)", Icon::Curve, m_filletTool))
            m_filletTool = !m_filletTool;
        y += 37 * s;
        bool fa = false;
        SliderRow("##crvFilletR", "Radius", x, y, w, &m_filletRadius, 0.01f, 20.0f, "%.2f", fa);
        if (Button("crvFilletApply", {x, y, w, 27 * s}, "Fillet Selected Points", Icon::Curve, false, false, !selectedAnchors().empty()))
            applyFillet(m_filletRadius);
        y += 39 * s;
    }
    {
        static float rebuildN = 8;
        SliderRow("##crvRebuildN", "Rebuild Points", x, y, w, &rebuildN, 2, 64, "%.0f", act);
        if (Button("crvRebuild", {x, y, w, 27 * s}, "Rebuild (even spacing)", Icon::Refresh)) {
            A.pushUndo();
            rebuildCurve(c, (int)std::lround(rebuildN));
            m_sel.clear();
            A.markDirty();
        }
        y += 39 * s;
    }

    if (c.shape.type != CSHAPE_NONE) {   // primitive parameters: the points are rebuilt from them
        Label(x, y, "PRIMITIVE");
        y += 24 * s;
        CurveShapeParams& p = c.shape;
        bool ch = false;
        auto len = [&](const char* id, const char* label, float& v, float lo, float hi) {
            float f = v;
            if (SliderRow(id, label, x, y, w, &f, lo, hi, "%.2f", act) && f != v) { v = f; ch = true; }
        };
        auto count = [&](const char* id, const char* label, int& v, int lo, int hi) {
            float f = (float)v;
            if (SliderRow(id, label, x, y, w, &f, (float)lo, (float)hi, "%.0f", act)) {
                int n = std::clamp((int)std::lround(f), lo, hi);
                if (n != v) { v = n; ch = true; }
            }
        };
        switch (p.type) {
        case CSHAPE_LINE: len("##cpLen", "Length", p.width, 0.1f, 100); break;
        case CSHAPE_CIRCLE: len("##cpR", "Radius", p.radius, 0.05f, 100); count("##cpSec", "Sections", p.sections, 3, 64); break;
        case CSHAPE_SQUARE: len("##cpW", "Width", p.width, 0.1f, 100); len("##cpD", "Depth", p.depth, 0.1f, 100); break;
        case CSHAPE_ARC:
            len("##cpR", "Radius", p.radius, 0.05f, 100);
            len("##cpSweep", "Sweep (degrees)", p.sweep, 1, 360);
            count("##cpSec", "Sections", p.sections, 2, 64);
            break;
        case CSHAPE_HELIX:
            len("##cpR", "Radius", p.radius, 0.05f, 100);
            len("##cpH", "Height", p.height, 0.0f, 100);
            len("##cpT", "Turns", p.turns, 0.1f, 20);
            count("##cpSec", "Sections per Turn", p.sections, 3, 64);
            break;
        case CSHAPE_SPIRAL:
            len("##cpR2", "Inner Radius", p.radius2, 0.0f, 100);
            len("##cpR", "Outer Radius", p.radius, 0.05f, 100);
            len("##cpT", "Turns", p.turns, 0.1f, 20);
            count("##cpSec", "Sections per Turn", p.sections, 3, 64);
            break;
        case CSHAPE_STAR:
            len("##cpR", "Outer Radius", p.radius, 0.05f, 100);
            len("##cpR2", "Inner Radius", p.radius2, 0.05f, 100);
            count("##cpSec", "Branches", p.sections, 3, 32);
            break;
        default: break;
        }
        if (ch) { buildCurveShape(c); m_sel.clear(); A.markDirty(); }
        y += 4 * s;
    }

    if (m_mode != MODE_POINTS) {   // Boolean (3ds Max Editable Spline): with a second closed curve
        Label(x, y, "BOOLEAN");
        y += 24 * s;
        if (!c.closed) {
            Text(x, y, "Close the curve to use it in a Boolean", P().muted, 10);
            y += 22 * s;
        } else {
            static const char* ops[] = {"Union", "Subtract", "Intersect"};
            static const Icon icons[] = {Icon::BoolUnion, Icon::BoolSubtract, Icon::BoolIntersect};
            const float tw = (w - 12 * s) / 3;
            for (int k = 0; k < 3; ++k) {
                std::string id = std::string("crvBool") + ops[k];
                if (Button(id.c_str(), {x + k * (tw + 6 * s), y, tw, 27 * s}, ops[k], icons[k], m_mode == MODE_PICK && m_boolOp == k)) startBoolean(k);
            }
            y += 33 * s;
            Text(x, y, m_mode == MODE_PICK ? "Pick the second curve in the viewport (Esc: cancel)" : "With a 2nd selected curve, or pick it after", P().muted, 10);
            y += 24 * s;
        }
    }

    {   // tube along the curve (3ds Max spline "Rendering": Enable In Viewport, thickness, sides, angle)
        Label(x, y, "MESH");
        y += 24 * s;
        CurveMesh& m = c.mesh;
        bool on = m.enabled;
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        if (Check("Enable in Viewport", &on)) { A.pushUndo(); m.enabled = on; A.markDirty(); }
        y += 32 * s;
        if (m.enabled) {
            float r = m.radius, sd = (float)m.sides, ang = m.angle;
            if (SliderRow("##crvMeshR", "Radius", x, y, w, &r, 0.005f, 5.0f, "%.3f", act) && r != m.radius) { m.radius = std::max(r, 0.001f); A.markDirty(); }
            if (SliderRow("##crvMeshSides", "Sides", x, y, w, &sd, 3, 48, "%.0f", act)) {
                const int n = std::clamp((int)std::lround(sd), 3, 48);
                if (n != m.sides) { m.sides = n; A.markDirty(); }
            }
            if (SliderRow("##crvMeshAngle", "Angle", x, y, w, &ang, -180, 180, "%.0f", act) && ang != m.angle) { m.angle = ang; A.markDirty(); }
            // rings along the curve (corners are always kept, with a mitre joint)
            Text(x, y, "Distribution", P().muted, 11);
            y += 19 * s;
            {
                static const char* dist[] = {"Curve Steps", "Even Spacing", "Adaptive"};
                int dm = std::clamp(m.distribution, 0, 2);
                if (Combo("##crvMeshDist", {x, y, w, 27 * s}, dm, dist, 3, Icon::Curve) && dm != m.distribution) { A.pushUndo(); m.distribution = dm; A.markDirty(); }
            }
            y += 37 * s;
            if (m.distribution == 1) {
                float sg = (float)m.segments;
                if (SliderRow("##crvMeshSeg", "Segments", x, y, w, &sg, 2, 512, "%.0f", act)) {
                    const int n = std::clamp((int)std::lround(sg), 2, 512);
                    if (n != m.segments) { m.segments = n; A.markDirty(); }
                }
            } else if (m.distribution == 2) {
                float aa = m.adaptiveAngle;
                if (SliderRow("##crvMeshAdapt", "Angle per Segment", x, y, w, &aa, 1, 45, "%.1f", act) && aa != m.adaptiveAngle) { m.adaptiveAngle = aa; A.markDirty(); }
            } else {
                Text(x, y, "Rings follow Display Smoothness", P().muted, 10);
                y += 22 * s;
            }
            if (!c.closed) {
                bool caps = m.caps;
                ImGui::SetCursorScreenPos(ImVec2(x, y));
                if (Check("Cap Ends", &caps)) { A.pushUndo(); m.caps = caps; A.markDirty(); }
                y += 32 * s;
            }
            if (o->material >= 0 && o->material < (int)A.m_scene.materials.size()) {   // colour of the tube (sRGB shown)
                Material& mat = A.m_scene.materials[o->material];
                auto toS = [](float v) { return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f; };
                float col[3] = {toS(mat.baseColor.x), toS(mat.baseColor.y), toS(mat.baseColor.z)};
                Text(x, y, "Colour", P().muted, 11);
                ImGui::SetCursorScreenPos(ImVec2(x + w - 150 * s, y - 3 * s));
                ImGui::SetNextItemWidth(150 * s);
                if (ImGui::ColorEdit3("##crvMeshCol", col, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
                    static int lastEdit = -100;   // one undo step per colour change (the picker sends a change every frame)
                    if (ImGui::GetFrameCount() - lastEdit > 30) A.pushUndo();
                    lastEdit = ImGui::GetFrameCount();
                    mat.baseColor = float3(srgbToLinear(col[0]), srgbToLinear(col[1]), srgbToLinear(col[2]));
                    A.markDirty();
                }
                y += 30 * s;
            }
        }
    }

    {   // frames along the curve: the rotation an object following it would have
        Label(x, y, "NORMALS");
        y += 24 * s;
        CurveFrames& f = c.frames;
        bool show = f.show;
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        if (Check("Show Normals and Rotation", &show)) { f.show = show; A.markDirty(); }
        y += 32 * s;
        if (f.show) {
            float len = f.length, cnt = (float)f.count;
            if (SliderRow("##crvFrLen", "Vector Length", x, y, w, &len, 0.05f, 10.0f, "%.2f", act)) { f.length = len; A.markDirty(); }
            if (SliderRow("##crvFrCount", "Count", x, y, w, &cnt, 2, 128, "%.0f", act)) { f.count = std::clamp((int)std::lround(cnt), 2, 128); A.markDirty(); }
            Text(x, y, "X side (red)  ·  Y up (green)  ·  Z forward (blue)", P().muted, 10);
            y += 22 * s;
        }
    }

    if (m_mode == MODE_POINTS && !m_sel.empty()) {   // selected point: exact position (local), applied to the selection
        Label(x, y, "SELECTED POINTS");
        y += 24 * s;
        const Item first = m_sel.front();
        float3 v = itemLocal(c, first), v0 = v;
        Text(x, y, m_sel.size() == 1 ? (first.part == PART_POINT ? "Position (local)" : "Handle (local)") : "Position of the first (moves all)", P().muted, 11);
        y += 19 * s;
        bool pact = false;
        if (AxisFields("cpt", x, y, w, &v.x, 0.05f, "%.3f", pact)) {
            float3 ld = v - v0;
            if (pact) A.pushUndo();
            std::vector<Item> items = m_sel;
            if (c.type == CURVE_BEZIER)
                for (const Item& it : m_sel)
                    if (it.part == PART_POINT)
                        for (int pp : {(int)PART_IN, (int)PART_OUT})
                            if (!isSelected({it.index, pp})) items.push_back({it.index, pp});
            for (const Item& it : items) itemRef(c, it) = itemRef(c, it) + ld;
            c.shape.type = CSHAPE_NONE;
            A.markDirty();
        } else if (pact) A.pushUndo();
        y += 41 * s;
        if (c.type == CURVE_BEZIER) {
            // tangent of the selected points (Cavalry style): Smooth, Broken or Corner
            Text(x, y, "Tangent", P().muted, 11);
            y += 19 * s;
            const int cur = tangentMode(c, first.index);
            const char* names[] = {"Smooth", "Broken", "Corner"};
            const float tw = (w - 12 * s) / 3;
            for (int m = 0; m < 3; ++m) {
                std::string id = std::string("crvTan") + names[m];
                if (Button(id.c_str(), {x + m * (tw + 6 * s), y, tw, 27 * s}, names[m], m == 2 ? Icon::Move : Icon::Curve, cur == m)) {
                    A.pushUndo();
                    for (const Item& it : m_sel) setTangentMode(it.index, m);
                }
            }
            y += 37 * s;
            if (Button("crvSmooth", {x, y, w, 27 * s}, "Auto Tangents", Icon::Refresh)) {
                A.pushUndo();
                for (const Item& it : m_sel) { bezierAutoHandles(c, it.index); if (it.index < (int)c.broken.size()) c.broken[it.index] = 0; }
                c.shape.type = CSHAPE_NONE;
                A.markDirty();
            }
            y += 35 * s;
        }
        if (Button("crvDelPts", {x, y, w, 27 * s}, "Delete Selected Points", Icon::Trash)) deleteSelectedPoints();
        y += 39 * s;
    }

    Label(x, y, "DISPLAY");
    y += 24 * s;
    bool vis = o->visible;
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    if (Check("Visible", &vis)) { A.pushUndo(); o->visible = vis; A.markDirty(); }
    y += 32 * s;
    bool selb = o->selectable;
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    if (Check("Selectable", &selb)) o->selectable = selb;
    y += 38 * s;

    if (act) A.pushUndo();
    dl->AddLine(ImVec2(x, y), ImVec2(x + w, y), P().border);
    y += 14 * s;
    if (Button("crvDup", {x, y, bw, 27 * s}, "Duplicate", Icon::Plus)) { exitMode(); A.duplicateSelected(); }
    if (Button("crvDel", {x + bw + 6 * s, y, bw, 27 * s}, "Delete", Icon::Trash)) { exitMode(); A.deleteSelected(); }
    ImGui::SetCursorScreenPos(ImVec2(x, y + 40 * s));
    ImGui::Dummy(ImVec2(1, 1));
}
