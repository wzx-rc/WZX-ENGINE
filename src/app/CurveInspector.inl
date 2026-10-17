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
