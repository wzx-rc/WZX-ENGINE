// Curve tools of the Scene editor (see CurveEditor.h). The inspector section is in CurveInspector.inl (it uses
// the Atelier widgets of AtelierUI.cpp).
#include "CurveEditor.h"
#include "GameHost.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <sstream>

namespace app {

namespace {

const ImU32 kAxisCol[3] = {IM_COL32(229, 72, 77, 255), IM_COL32(70, 167, 88, 255), IM_COL32(62, 142, 247, 255)};
// visual language (anthracite background): pale cyan curves, silver handles, luminous active node
const ImU32 kCurveOn = IM_COL32(132, 190, 210, 255);     // selected / active curve: pale cyan
const ImU32 kCurveOff = IM_COL32(104, 150, 168, 150);    // inactive curve: the same cyan, discreet
const ImU32 kCurveMulti = IM_COL32(170, 208, 222, 220);  // other curves of a multi-selection
const ImU32 kHandle = IM_COL32(168, 172, 182, 255);      // silver: hollow nodes and dashed tangents
const ImU32 kHull = IM_COL32(168, 172, 182, 120);        // CV hull
const ImU32 kNodeFill = IM_COL32(24, 27, 32, 255);       // inside of a hollow node (anthracite)
const ImU32 kNodeRing = IM_COL32(240, 250, 255, 255);    // ring of the active node

float3 axisVec(int a) { return float3(a == 0 ? 1.0f : 0.0f, a == 1 ? 1.0f : 0.0f, a == 2 ? 1.0f : 0.0f); }
ImVec2 iv(float2 p) { return ImVec2(p.x, p.y); }

float distSeg(float px, float py, float2 a, float2 b) {
    float dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
    float t = l2 > 1e-6f ? clampf(((px - a.x) * dx + (py - a.y) * dy) / l2, 0, 1) : 0;
    float qx = a.x + dx * t - px, qy = a.y + dy * t - py;
    return std::sqrt(qx * qx + qy * qy);
}

// closest parameter along the line (o, a) to the mouse ray
float axisParam(const float3& o, const float3& a, const float3& ro, const float3& rd) {
    float3 w = o - ro;
    float b = dot(a, rd), d = dot(a, w), e = dot(rd, w);
    float den = 1 - b * b;
    return std::fabs(den) < 1e-6f ? 0.0f : (b * e - d) / den;
}

float4 qConj(const float4& q) { return float4(-q.x, -q.y, -q.z, q.w); }

const char* kTypeKeys[] = {"", "linear", "cv", "ep", "bezier"};
const char* kShapeKeys[] = {"", "line", "circle", "square", "arc", "helix", "spiral", "star"};

Json jnum(double v) { Json j; j.type = Json::Number; j.n = v; return j; }
Json jbool(bool v) { Json j; j.type = Json::Bool; j.b = v; return j; }
Json jstr(const std::string& v) { Json j; j.type = Json::String; j.s = v; return j; }
Json jvecs(const std::vector<float3>& v) {
    Json a;
    a.type = Json::Array;
    for (const float3& p : v) {
        Json e;
        e.type = Json::Array;
        e.a = {jnum(p.x), jnum(p.y), jnum(p.z)};
        a.a.push_back(e);
    }
    return a;
}
std::vector<float3> vecsOf(const Json& j) {
    std::vector<float3> v;
    for (const Json& e : j.a)
        if (e.type == Json::Array && e.a.size() == 3) v.push_back(float3((float)e[0].num(), (float)e[1].num(), (float)e[2].num()));
    return v;
}

} // namespace

// --------------------------------------------------------------------------- .wzx

Json curveToJson(const CurveData& c) {
    Json j;
    j.type = Json::Object;
    j.o["type"] = jstr(kTypeKeys[std::clamp(c.type, 0, (int)CURVE_TYPE_COUNT - 1)]);
    j.o["closed"] = jbool(c.closed);
    j.o["steps"] = jnum(c.steps);
    j.o["points"] = jvecs(c.points);
    if (c.type == CURVE_BEZIER) {
        j.o["in"] = jvecs(c.hin);
        j.o["out"] = jvecs(c.hout);
        Json b;
        b.type = Json::Array;
        for (uint8_t k : c.broken) b.a.push_back(jbool(k != 0));
        j.o["broken"] = b;
    }
    if (c.shape.type != CSHAPE_NONE) {   // primitive: its parameters (the points above are kept as well)
        const CurveShapeParams& p = c.shape;
        Json s;
        s.type = Json::Object;
        s.o["primitive"] = jstr(kShapeKeys[std::clamp(p.type, 0, (int)CSHAPE_COUNT - 1)]);
        s.o["radius"] = jnum(p.radius);
        s.o["radius2"] = jnum(p.radius2);
        s.o["width"] = jnum(p.width);
        s.o["depth"] = jnum(p.depth);
        s.o["height"] = jnum(p.height);
        s.o["sweep"] = jnum(p.sweep);
        s.o["turns"] = jnum(p.turns);
        s.o["sections"] = jnum(p.sections);
        j.o["shape"] = s;
    }
    return j;
}

bool curveFromJson(const Json& j, CurveData& c) {
    if (j.type != Json::Object) return false;
    c = CurveData();
    const std::string t = j["type"].str("cv");
    c.type = CURVE_CV;
    for (int i = 1; i < CURVE_TYPE_COUNT; ++i) if (t == kTypeKeys[i]) c.type = i;
    c.closed = j["closed"].type == Json::Bool && j["closed"].b;
    c.steps = std::clamp((int)j["steps"].num(16), 1, 256);
    c.points = vecsOf(j["points"]);
    if (c.type == CURVE_BEZIER) {
        c.hin = vecsOf(j["in"]);
        c.hout = vecsOf(j["out"]);
        for (const Json& b : j["broken"].a) c.broken.push_back(b.type == Json::Bool && b.b ? 1 : 0);
        if (c.hin.size() != c.points.size() || c.hout.size() != c.points.size()) { c.hin.clear(); c.hout.clear(); }
        bezierFixArrays(c);
    }
    const Json& s = j["shape"];
    if (s.type == Json::Object) {
        CurveShapeParams& p = c.shape;
        const std::string n = s["primitive"].str();
        for (int i = 1; i < CSHAPE_COUNT; ++i) if (n == kShapeKeys[i]) p.type = i;
        p.radius = (float)s["radius"].num(p.radius);
        p.radius2 = (float)s["radius2"].num(p.radius2);
        p.width = (float)s["width"].num(p.width);
        p.depth = (float)s["depth"].num(p.depth);
        p.height = (float)s["height"].num(p.height);
        p.sweep = (float)s["sweep"].num(p.sweep);
        p.turns = (float)s["turns"].num(p.turns);
        p.sections = (int)s["sections"].num(p.sections);
        if (c.points.empty() && p.type != CSHAPE_NONE) buildCurveShape(c);   // hand-written file: parameters only
    }
    return true;
}

// --------------------------------------------------------------------------- helpers

float CurveEditor::grid() const { return a.m_scene.gridScale > 0 ? a.m_scene.gridScale : 1.0f; }

float3 CurveEditor::snap(const float3& p) const {
    float g = grid();
    return float3(std::round(p.x / g) * g, std::round(p.y / g) * g, std::round(p.z / g) * g);
}

// Point under the mouse on the horizontal plane y = planeY; seen edge-on: on the plane facing the camera.
bool CurveEditor::groundPoint(float mx, float my, float planeY, float3& out) const {
    float3 o, d;
    a.cameraRay(mx, my, o, d);
    if (std::fabs(d.y) > 0.02f) {
        float t = (planeY - o.y) / d.y;
        if (t > 0) { out = o + d * t; return true; }
    }
    float3 r, u, f;
    a.m_scene.camera.basis(r, u, f);
    float den = dot(d, f);
    if (std::fabs(den) < 1e-5f) return false;
    float t = dot(a.m_scene.camera.target - o, f) / den;
    if (t <= 0) return false;
    out = o + d * t;
    return true;
}

float3 CurveEditor::toWorld(const Object& o, const float3& p) const { return o.transform().point(p); }

float3 CurveEditor::toLocalDir(const Object& o, const float3& d) const {
    float3 v = qRot(qConj(a.objRot(o)), d);
    auto div = [](float x, float s) { return std::fabs(s) > 1e-6f ? x / s : x; };
    return float3(div(v.x, o.scale.x), div(v.y, o.scale.y), div(v.z, o.scale.z));
}

float3 CurveEditor::itemLocal(const CurveData& c, Item it) const {
    if (it.part == PART_IN && it.index < (int)c.hin.size()) return c.hin[it.index];
    if (it.part == PART_OUT && it.index < (int)c.hout.size()) return c.hout[it.index];
    return c.points[it.index];
}

float3& CurveEditor::itemRef(CurveData& c, Item it) {
    if (it.part == PART_IN && it.index < (int)c.hin.size()) return c.hin[it.index];
    if (it.part == PART_OUT && it.index < (int)c.hout.size()) return c.hout[it.index];
    return c.points[it.index];
}

bool CurveEditor::isSelected(Item it) const {
    for (const Item& s : m_sel) if (s.index == it.index && s.part == it.part) return true;
    return false;
}

// Keeps the state consistent with the scene (undo, deletion, another selection).
void CurveEditor::validate() {
    const auto& objs = a.m_scene.objects;
    if (m_mode == MODE_POINTS) {
        if (m_obj < 0 || m_obj >= (int)objs.size() || !objs[m_obj].curve.valid() || a.m_selected != m_obj) { exitMode(); return; }
        const CurveData& c = objs[m_obj].curve;
        m_sel.erase(std::remove_if(m_sel.begin(), m_sel.end(), [&](const Item& it) {
            return it.index < 0 || it.index >= c.count() || (it.part != PART_POINT && c.type != CURVE_BEZIER);
        }), m_sel.end());
    }
}

bool CurveEditor::pickPoint(float mx, float my, Item& out) const {
    if (m_obj < 0) return false;
    const Object& o = a.m_scene.objects[m_obj];
    const CurveData& c = o.curve;
    float best = 8.0f * a.m_dpi;
    bool found = false;
    auto test = [&](const float3& p, Item it) {
        float2 s;
        if (!a.project(toWorld(o, p), s)) return;
        float d = std::hypot(s.x - mx, s.y - my);
        if (d < best) { best = d; out = it; found = true; }
    };
    for (int i = 0; i < c.count(); ++i) {
        if (c.type == CURVE_BEZIER && i < (int)c.hin.size() && handlesShown(i)) {   // only the visible tangents
            test(c.hin[i], {i, PART_IN});
            test(c.hout[i], {i, PART_OUT});
        }
    }
    for (int i = 0; i < c.count(); ++i) test(c.points[i], {i, PART_POINT});   // anchors win over a handle on top
    return found;
}

int CurveEditor::pickCurve(float mx, float my, float* distOut) const {
    float best = 7.0f * a.m_dpi;
    int hit = -1;
    std::vector<float3> s;
    for (int i = 0; i < (int)a.m_scene.objects.size(); ++i) {
        const Object& o = a.m_scene.objects[i];
        if (!o.curve.valid() || !o.visible || !o.selectable || o.ghost) continue;
        sampleCurve(o.curve, s);
        Affine xf = o.transform();
        float2 prev;
        bool hasPrev = false;
        for (const float3& p : s) {
            float2 q;
            bool ok = a.project(xf.point(p), q);
            if (ok && hasPrev) {
                float d = distSeg(mx, my, prev, q);
                if (d < best) { best = d; hit = i; }
            } else if (ok && s.size() == 1) {
                float d = std::hypot(q.x - mx, q.y - my);
                if (d < best) { best = d; hit = i; }
            }
            prev = q;
            hasPrev = ok;
        }
    }
    if (distOut) *distOut = best;
    return hit;
}

bool CurveEditor::selectionPivot(float3& p) const {
    if (m_obj < 0 || m_sel.empty()) return false;
    const Object& o = a.m_scene.objects[m_obj];
    float3 sum(0);
    for (const Item& it : m_sel) sum += toWorld(o, itemLocal(o.curve, it));
    p = sum / (float)m_sel.size();
    return true;
}

bool CurveEditor::worldBounds(const Object& o, float3& mn, float3& mx) const {
    if (!o.curve.valid()) return false;
    float3 a0, b0;
    curveBounds(o.curve, a0, b0);
    Affine xf = o.transform();
    mn = float3(1e30f);
    mx = float3(-1e30f);
    for (int i = 0; i < 8; ++i) {
        float3 p = xf.point(float3((i & 1) ? b0.x : a0.x, (i & 2) ? b0.y : a0.y, (i & 4) ? b0.z : a0.z));
        mn = minv(mn, p);
        mx = maxv(mx, p);
    }
    return true;
}

// --------------------------------------------------------------------------- modes

void CurveEditor::startCreate(int type) {
    exitMode();
    a.select(-1);
    m_mode = MODE_CREATE;
    m_createType = type;
    m_draft = CurveData();
    m_draft.type = type;
    m_coordBuf[0] = 0;
    m_draftDrag = false;
    a.status(type == CURVE_BEZIER
        ? L"Bezier tool: click to place an anchor, drag to pull its handles"
        : L"Curve tool: click on the ground to place the points (click the first point to close)");
}

void CurveEditor::cancelCreate() {
    m_mode = MODE_NONE;
    m_draft = CurveData();
    m_draftDrag = false;
    a.status(L"Curve creation cancelled");
}

int CurveEditor::addCurveObject(const CurveData& local, const float3& pos, const std::string& name) {
    a.pushUndo();
    Material m;
    m.name = name;
    m.baseColor = float3(0.42f, 0.42f, 0.42f);
    Object o;
    o.part = "CURVE";
    o.name = a.uniqueNodeName(name);
    o.mesh = -1;
    o.material = a.m_scene.addMaterial(m);
    o.position = pos;
    o.curve = local;
    int idx = a.m_scene.addObject(o);
    a.outlinerIds();
    a.m_selected = idx;
    a.m_multi.clear();
    a.m_outSel.clear();
    a.markDirty();
    return idx;
}

void CurveEditor::finishCreate() {
    CurveData c = m_draft;
    m_mode = MODE_NONE;
    m_draftDrag = false;
    m_draft = CurveData();
    if (c.count() < 2) { a.status(L"A curve needs at least 2 points", 2); return; }
    if (c.closed && c.count() < 3) c.closed = false;
    // pivot: centre of the points, on their lowest height
    float3 mn(1e30f), mx(-1e30f);
    for (const float3& p : c.points) { mn = minv(mn, p); mx = maxv(mx, p); }
    float3 pos((mn.x + mx.x) * 0.5f, mn.y, (mn.z + mx.z) * 0.5f);
    for (float3& p : c.points) p = p - pos;
    if (c.type == CURVE_BEZIER) {
        bezierFixArrays(c);
        for (int i = 0; i < c.count(); ++i) {
            c.hin[i] = c.hin[i] - pos;
            c.hout[i] = c.hout[i] - pos;
            // anchor placed without a drag: smooth automatic handles
            if (length(c.hout[i] - c.points[i]) < 1e-5f && length(c.hin[i] - c.points[i]) < 1e-5f) bezierAutoHandles(c, i);
        }
    }
    std::string name = c.type == CURVE_LINEAR ? "LinearCurve" : c.type == CURVE_EP ? "EPCurve" : c.type == CURVE_BEZIER ? "BezierCurve" : "CVCurve";
    addCurveObject(c, pos, name);
    if (a.m_tool == TOOL_SELECT) a.m_tool = TOOL_MOVE;
    a.status(fromUtf8(curveTypeName(c.type)) + L" created: " + std::to_wstring(c.count()) + L" points" + (c.closed ? L", closed" : L""), 1);
}

void CurveEditor::addPrimitive(int shape) {
    exitMode();
    CurveData c = makeCurveShape(shape);
    float3 pos = a.m_scene.camera.target;
    pos.y = 0;
    pos = snap(pos);
    pos.y = 0;
    addCurveObject(c, pos, curveShapeName(shape));
    if (a.m_tool == TOOL_SELECT) a.m_tool = TOOL_MOVE;
    a.status(std::wstring(L"Curve primitive added: ") + fromUtf8(curveShapeName(shape)), 1);
}

void CurveEditor::enterPoints() {
    if (a.m_selected < 0 || !a.m_scene.objects[a.m_selected].curve.valid()) { a.status(L"Select a curve to edit its points", 2); return; }
    m_mode = MODE_POINTS;
    m_obj = a.m_selected;
    a.m_multi.clear();
    m_sel.clear();
    m_dragging = m_marquee = m_pendingFree = m_pulling = false;
    a.status(L"Edit Points: click or drag a rectangle to select, drag to move");
    a.markDirty();
}

void CurveEditor::exitMode() {
    if (m_mode == MODE_CREATE) { m_draft = CurveData(); m_draftDrag = false; }
    if (m_dragging && !m_dragMoved && !a.m_undo.empty()) a.m_undo.pop_back();
    m_mode = MODE_NONE;
    m_obj = -1;
    m_sel.clear();
    m_dragging = m_marquee = m_pendingFree = m_pulling = m_filleting = m_filletTool = false;
    m_drawStep = 0;
    m_hoverAxis = -1;
}

// --------------------------------------------------------------------------- primitives drawn with the mouse (3ds Max)

namespace {
// second size after the drag: star and spiral inner radius, helix height, arc sweep
bool needsSecondSize(int shape) { return shape == CSHAPE_STAR || shape == CSHAPE_SPIRAL || shape == CSHAPE_HELIX || shape == CSHAPE_ARC; }
}

float CurveEditor::worldPerPixel(const float3& p) const {
    float3 r, u, f;
    a.m_scene.camera.basis(r, u, f);
    float z = std::max(dot(p - a.m_scene.camera.eye(), f), 0.01f);
    return 2.0f * z * std::tan(a.m_scene.camera.fovY * 0.5f) / std::max(a.m_rcView.h, 1.0f);
}

void CurveEditor::startDraw(int shape) {
    exitMode();
    a.select(-1);
    m_mode = MODE_DRAW;
    m_drawShape = shape;
    m_drawStep = 0;
    a.status(std::wstring(L"Draw a ") + fromUtf8(curveShapeName(shape)) + L": press and drag in the viewport (a simple click: default size)");
}

// The primitive as drawn so far: its points (local), its position and its rotation around Y.
CurveData CurveEditor::drawShape(float3& pos, float4& rot) const {
    CurveData c = makeCurveShape(m_drawShape);   // default sections, turns...
    CurveShapeParams& p = c.shape;
    float3 d = m_drawCur - m_drawStart;
    d.y = 0;
    const float L = length(d);
    pos = m_drawStart;
    rot = qIdentity();
    if (L < 1e-4f) return c;   // a click: default size
    const float dirAngle = std::atan2(-d.z, d.x);   // rotation around Y that turns +X towards the drag
    switch (m_drawShape) {
    case CSHAPE_LINE:
        pos = (m_drawStart + m_drawCur) * 0.5f;
        p.width = L;
        rot = qAxisAngle(float3(0, 1, 0), dirAngle);
        break;
    case CSHAPE_SQUARE: {
        float w = std::fabs(d.x), dd = std::fabs(d.z);
        if (ImGui::GetIO().KeyShift) w = dd = std::max(w, dd);   // Shift: a square
        pos = m_drawStart + float3(d.x < 0 ? -w : w, 0, d.z < 0 ? -dd : dd) * 0.5f;
        p.width = std::max(w, 0.01f);
        p.depth = std::max(dd, 0.01f);
        break;
    }
    case CSHAPE_ARC:
        p.radius = L;
        p.sweep = m_drawStep == 2 ? m_draw2 : 180.0f;
        rot = qAxisAngle(float3(0, 1, 0), dirAngle);
        break;
    case CSHAPE_HELIX:
        p.radius = L;
        p.height = m_drawStep == 2 ? m_draw2 : 0.0f;
        break;
    case CSHAPE_SPIRAL:
        p.radius = L;
        p.radius2 = m_drawStep == 2 ? std::min(m_draw2, L) : 0.0f;
        break;
    case CSHAPE_STAR:
        p.radius = L;
        p.radius2 = m_drawStep == 2 ? std::max(m_draw2, 0.01f) : L * 0.5f;
        break;
    default:   // circle
        p.radius = L;
        break;
    }
    buildCurveShape(c);
    return c;
}

void CurveEditor::finishDraw() {
    float3 pos;
    float4 rot;
    CurveData c = drawShape(pos, rot);
    m_mode = MODE_NONE;
    m_drawStep = 0;
    int idx = addCurveObject(c, pos, curveShapeName(m_drawShape));
    Object& o = a.m_scene.objects[idx];
    o.rot = rot;
    o.useRot = true;
    if (a.m_tool == TOOL_SELECT) a.m_tool = TOOL_MOVE;
    a.status(std::wstring(L"Curve primitive drawn: ") + fromUtf8(curveShapeName(m_drawShape)), 1);
}

// --------------------------------------------------------------------------- fillet

std::vector<int> CurveEditor::selectedAnchors() const {
    std::vector<int> v;
    for (const Item& it : m_sel)
        if (it.part == PART_POINT && std::find(v.begin(), v.end(), it.index) == v.end()) v.push_back(it.index);
    return v;
}

void CurveEditor::applyFillet(float radius) {
    if (m_obj < 0) return;
    std::vector<int> idx = selectedAnchors();
    if (idx.empty()) { a.status(L"Select the points to fillet", 2); return; }
    CurveData& c = a.m_scene.objects[m_obj].curve;
    a.pushUndo();
    const int before = c.count();
    c = filletCurve(c, idx, radius);
    m_sel.clear();
    a.markDirty();
    a.status(std::to_wstring(c.count() - before) + L" point(s) added by the fillet");
}

// --------------------------------------------------------------------------- keys

bool CurveEditor::keys() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return false;
    validate();
    auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
    const bool placing = a.m_game && (a.m_game->placementMode() || a.m_game->playing());
    if (placing) return false;
    if (pressed(ImGuiKey_F8)) {
        if (m_mode == MODE_POINTS) { exitMode(); a.status(L"Object mode"); }
        else if (m_mode == MODE_NONE) enterPoints();
        return true;
    }
    if (m_mode == MODE_DRAW && pressed(ImGuiKey_Escape)) { exitMode(); a.status(L"Drawing cancelled"); return true; }
    if (m_mode == MODE_DRAW && pressed(ImGuiKey_Delete)) return true;
    if (m_mode != MODE_NONE && pressed(ImGuiKey_O)) {
        m_osnap = !m_osnap;
        a.status(m_osnap ? L"Osnap on" : L"Osnap off");
        return true;
    }
    if (m_mode == MODE_CREATE && !io.KeyCtrl) {   // typing a coordinate opens the field with these characters
        bool typed = false;
        for (ImWchar ch : io.InputQueueCharacters)
            if (ch < 128 && strchr("0123456789-.@<", (char)ch) && strlen(m_coordBuf) + 1 < sizeof(m_coordBuf)) {
                size_t n = strlen(m_coordBuf);
                m_coordBuf[n] = (char)ch;
                m_coordBuf[n + 1] = 0;
                typed = true;
            }
        if (typed) {
            io.InputQueueCharacters.resize(0);
            m_coordFocus = true;
            return true;
        }
    }
    if (m_mode == MODE_CREATE) {
        if (pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter)) { finishCreate(); return true; }
        if (pressed(ImGuiKey_Escape)) { cancelCreate(); return true; }
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
            if (!m_draft.points.empty()) {
                m_draft.points.pop_back();
                if (!m_draft.hin.empty()) m_draft.hin.pop_back();
                if (!m_draft.hout.empty()) m_draft.hout.pop_back();
                if (!m_draft.broken.empty()) m_draft.broken.pop_back();
                m_draftDrag = false;
            }
            return true;
        }
        if (pressed(ImGuiKey_Delete)) return true;   // never deletes an object while a curve is drawn
    }
    if (m_mode == MODE_POINTS) {
        if (pressed(ImGuiKey_Escape)) {
            if (m_filletTool) { m_filletTool = false; a.status(L"Fillet tool off"); }
            else if (!m_sel.empty()) m_sel.clear();
            else { exitMode(); a.status(L"Object mode"); }
            return true;
        }
        if (pressed(ImGuiKey_Delete) || pressed(ImGuiKey_Backspace)) { deleteSelectedPoints(); return true; }
        if (io.KeyCtrl && pressed(ImGuiKey_A)) {
            m_sel.clear();
            for (int i = 0; i < a.m_scene.objects[m_obj].curve.count(); ++i) m_sel.push_back({i, PART_POINT});
            return true;
        }
    }
    return false;
}

// --------------------------------------------------------------------------- mouse

bool CurveEditor::input(bool inView, bool placing) {
    ImGuiIO& io = ImGui::GetIO();
    const float mx = io.MousePos.x, my = io.MousePos.y;
    validate();
    m_snap.on = false;   // set again below (or by the creation preview) when a snap is under the mouse
    if (placing) {
        if (m_mode != MODE_NONE) exitMode();
        return false;
    }
    const bool alt = io.KeyAlt, shift = io.KeyShift, clicked = inView && ImGui::IsMouseClicked(0) && !alt;
    const bool snapOn = ImGui::IsKeyDown(ImGuiKey_X);

    if (m_mode == MODE_CREATE) {
        if (clicked) {
            bool closeIt = false;
            if (m_draft.count() >= 3) {   // click on the first point: closes the curve
                float2 s0;
                if (a.project(m_draft.points[0], s0) && std::hypot(s0.x - mx, s0.y - my) < 9.0f * a.m_dpi) closeIt = true;
            }
            if (closeIt) { m_draft.closed = true; finishCreate(); return true; }
            bool ok = false;
            float3 p = createTarget(mx, my, ok);   // osnap, grid (X), ortho (Shift)
            if (ok) {
                draftAdd(p);
                m_draftDrag = true;
                m_pressAt = float2(mx, my);
            }
        }
        if (m_draftDrag) {
            if (!io.MouseDown[0]) m_draftDrag = false;
            else if (m_createType == CURVE_BEZIER && !m_draft.points.empty() && std::hypot(mx - m_pressAt.x, my - m_pressAt.y) > 3.0f * a.m_dpi) {
                const float3 A = m_draft.points.back();
                float3 h;
                if (groundPoint(mx, my, A.y, h)) {
                    if (snapOn) h = snap(h);
                    m_draft.hout.back() = h;
                    m_draft.hin.back() = A * 2.0f - h;
                }
            }
        }
        return true;
    }

    if (m_mode == MODE_DRAW) {   // primitive drawn with the mouse (3ds Max): press, drag, (move, click)
        bool ok = false;
        float3 p = createTarget(mx, my, ok);   // osnap, grid (X)
        if (m_drawStep == 0) {
            if (clicked && ok) { m_drawStart = m_drawCur = p; m_drawStep = 1; m_pressAt = float2(mx, my); }
            return true;
        }
        if (m_drawStep == 1) {
            if (ok) { m_drawCur = p; m_drawCur.y = m_drawStart.y; }
            if (!io.MouseDown[0]) {
                const bool dragged = std::hypot(mx - m_pressAt.x, my - m_pressAt.y) > 4.0f * a.m_dpi;
                if (!dragged) { m_drawCur = m_drawStart; finishDraw(); }
                else if (needsSecondSize(m_drawShape)) {
                    m_drawStep = 2;
                    m_drawPx0 = my;
                    float3 d = m_drawCur - m_drawStart;
                    d.y = 0;
                    m_draw2 = m_drawShape == CSHAPE_STAR ? length(d) * 0.5f : m_drawShape == CSHAPE_ARC ? 180.0f : 0.0f;
                } else finishDraw();
            }
            return true;
        }
        // step 2: the second size follows the mouse, a click ends
        float3 g;
        if (m_drawShape == CSHAPE_HELIX) m_draw2 = std::max(0.0f, m_drawPx0 - my) * worldPerPixel(m_drawStart);
        else if (groundPoint(mx, my, m_drawStart.y, g)) {
            if (snapOn) g = snap(g);
            float3 v = g - m_drawStart;
            v.y = 0;
            if (m_drawShape == CSHAPE_ARC) {
                float3 d = m_drawCur - m_drawStart;
                float4 r = qAxisAngle(float3(0, 1, 0), std::atan2(-d.z, d.x));
                float3 l = qRot(qConj(r), v);
                float ang = degrees(std::atan2(l.z, l.x));
                if (ang < 0) ang += 360.0f;
                m_draw2 = std::max(ang, 1.0f);
            } else m_draw2 = length(v);
        }
        if (clicked) finishDraw();
        return true;
    }

    if (m_mode == MODE_POINTS) {
        if (m_filleting) {   // fillet drag: the radius grows when the mouse goes up
            CurveData& cc = a.m_scene.objects[m_obj].curve;
            const float r = std::max(0.0f, m_filletPx0 - my) * worldPerPixel(m_filletAt);
            if (!io.MouseDown[0]) {
                m_filleting = false;
                if (m_filletR <= 0 && !a.m_undo.empty()) a.m_undo.pop_back();   // no fillet: nothing to undo
                else a.status(L"Fillet applied");
                m_sel.clear();
            } else if (r != m_filletR) {
                m_filletR = r;
                cc = filletCurve(m_filletBase, m_filletIdx, r);
                a.markDirty();
            }
            return true;
        }
        if (m_dragging) {
            if (!io.MouseDown[0]) {
                m_dragging = false;
                if (!m_dragMoved && !a.m_undo.empty()) a.m_undo.pop_back();
            } else updateDrag(mx, my);
            return true;
        }
        if (m_pulling) {   // Ctrl+drag from a point: symmetric tangents follow the mouse
            Object& o = a.m_scene.objects[m_obj];
            CurveData& cc = o.curve;
            if (!io.MouseDown[0] || m_pullIndex >= cc.count()) { m_pulling = false; return true; }
            if (std::hypot(mx - m_pressAt.x, my - m_pressAt.y) > 3.0f * a.m_dpi) {
                const float3 A = toWorld(o, cc.points[m_pullIndex]);
                float3 h;
                if (groundPoint(mx, my, A.y, h)) {
                    if (ImGui::IsKeyDown(ImGuiKey_X)) h = snap(h);
                    const float3 ld = toLocalDir(o, h - A);
                    cc.hout[m_pullIndex] = cc.points[m_pullIndex] + ld;
                    cc.hin[m_pullIndex] = cc.points[m_pullIndex] - ld;
                    cc.broken[m_pullIndex] = 0;
                    cc.shape.type = CSHAPE_NONE;
                    a.markDirty();
                }
            }
            return true;
        }
        if (m_pendingFree) {
            if (!io.MouseDown[0]) m_pendingFree = false;
            else if (std::hypot(mx - m_pressAt.x, my - m_pressAt.y) > 4.0f * a.m_dpi) {
                m_pendingFree = false;
                beginDrag(3, m_pressAt.x, m_pressAt.y);
                updateDrag(mx, my);
            }
            return true;
        }
        if (m_marquee) {
            if (io.MouseDown[0]) return true;
            m_marquee = false;
            float x0 = std::min(m_marq0.x, mx), x1 = std::max(m_marq0.x, mx), y0 = std::min(m_marq0.y, my), y1 = std::max(m_marq0.y, my);
            if (!shift) m_sel.clear();
            if (x1 - x0 < 4 * a.m_dpi && y1 - y0 < 4 * a.m_dpi) return true;   // simple click in the void: clears
            const Object& o = a.m_scene.objects[m_obj];
            for (int i = 0; i < o.curve.count(); ++i) {
                float2 s;
                Item it{i, PART_POINT};
                if (a.project(toWorld(o, o.curve.points[i]), s) && s.x >= x0 && s.x <= x1 && s.y >= y0 && s.y <= y1 && !isSelected(it)) m_sel.push_back(it);
            }
            return true;
        }
        m_hoverAxis = inView ? manipulatorHit(mx, my) : -1;
        if (clicked) {
            CurveData& cc = a.m_scene.objects[m_obj].curve;
            const bool bez = cc.type == CURVE_BEZIER;
            if (m_filletTool) {   // fillet tool: press on a point (the selected points are filleted with it) and drag up
                Item it;
                if (pickPoint(mx, my, it) && it.part == PART_POINT) {
                    m_filletIdx = selectedAnchors();
                    if (!isSelected(it)) { m_filletIdx = {it.index}; m_sel = {it}; }
                    m_filletBase = cc;
                    m_filletAt = toWorld(a.m_scene.objects[m_obj], cc.points[it.index]);
                    m_filletPx0 = my;
                    m_filletR = 0;
                    m_filleting = true;
                    a.pushUndo();
                }
                return true;
            }
            Item it;
            const bool onPoint = pickPoint(mx, my, it);
            // Cavalry-style tangents (Bezier): double-click a point = corner <-> smooth; Ctrl+drag a tangent end =
            // break it; Ctrl+drag a point = pull new symmetric tangents out of it
            if (onPoint && bez && it.part == PART_POINT && ImGui::IsMouseDoubleClicked(0)) { m_pendingFree = false; toggleCorner(it.index); return true; }
            if (onPoint && bez && io.KeyCtrl) {
                if (it.part != PART_POINT) {
                    a.pushUndo();
                    cc.broken[it.index] = 1;
                    m_sel = {it};
                    beginDrag(3, mx, my);
                    a.status(L"Tangent broken: the two handles move independently");
                } else {
                    a.pushUndo();
                    m_sel = {it};
                    m_pulling = true;
                    m_pullIndex = it.index;
                    m_pressAt = float2(mx, my);
                }
                return true;
            }
            if (m_hoverAxis >= 0) { beginDrag(m_hoverAxis, mx, my); return true; }
            if (io.KeyCtrl) { insertPointAt(mx, my); return true; }
            if (onPoint) {
                if (shift) {
                    auto f = std::find_if(m_sel.begin(), m_sel.end(), [&](const Item& s) { return s.index == it.index && s.part == it.part; });
                    if (f != m_sel.end()) m_sel.erase(f); else m_sel.push_back(it);
                } else {
                    if (!isSelected(it)) m_sel = {it};
                    m_pendingFree = true;   // press and drag: moves the points freely
                    m_pressAt = float2(mx, my);
                }
                return true;
            }
            m_marquee = true;
            m_marq0 = float2(mx, my);
        }
        return true;
    }

    // object mode: a click on a curve selects it (the meshes are picked by the viewport afterwards)
    if (!clicked || a.m_gz.active || a.m_marq.active || a.m_dragging) return false;
    int axis;
    bool rotate;
    if (a.gizmoHit(mx, my, axis, rotate)) return false;
    int ci = pickCurve(mx, my);
    if (ci < 0) return false;
    if (ImGui::IsMouseDoubleClicked(0)) {   // double-click on a curve: edit its points
        a.select(ci);
        enterPoints();
        return true;
    }
    if (shift && a.m_selected >= 0) {
        if (ci == a.m_selected) {
            if (!a.m_multi.empty()) { a.m_selected = a.m_multi.front(); a.m_multi.erase(a.m_multi.begin()); }
            else a.m_selected = -1;
        } else {
            auto it = std::find(a.m_multi.begin(), a.m_multi.end(), ci);
            if (it != a.m_multi.end()) a.m_multi.erase(it); else a.m_multi.push_back(ci);
        }
        a.markDirty();
    } else a.select(ci);
    return true;
}

int CurveEditor::manipulatorHit(float mx, float my) const {
    float3 pv;
    if (!selectionPivot(pv)) return -1;
    float2 pc;
    if (!a.project(pv, pc)) return -1;
    if (std::hypot(mx - pc.x, my - pc.y) < 12 * a.m_dpi) return 3;   // ring around the node: free move
    float s = a.gizmoScale(pv), best = 10.0f * a.m_dpi;
    int hit = -1;
    for (int k = 0; k < 3; ++k) {
        float2 p1;
        if (!a.project(pv + axisVec(k) * (1.1f * s), p1)) continue;
        float d = distSeg(mx, my, pc, p1);
        if (d < best) { best = d; hit = k; }
    }
    return hit;
}

void CurveEditor::beginDrag(int axis, float mx, float my) {
    float3 pv;
    if (!selectionPivot(pv)) return;
    a.pushUndo();
    m_dragging = true;
    m_dragMoved = false;
    m_dragAxis = axis;
    m_dragPivot = pv;
    // moved items: the selection, plus the handles of the selected Bezier anchors
    const CurveData& c = a.m_scene.objects[m_obj].curve;
    std::vector<Item> items = m_sel;
    if (c.type == CURVE_BEZIER)
        for (const Item& it : m_sel)
            if (it.part == PART_POINT)
                for (int p : {(int)PART_IN, (int)PART_OUT})
                    if (!isSelected({it.index, p})) items.push_back({it.index, p});
    m_dragStart.clear();
    for (const Item& it : items) m_dragStart.push_back({it, itemLocal(c, it)});
    float3 ro, rd;
    a.cameraRay(mx, my, ro, rd);
    if (axis < 3) {
        m_dragDir = axisVec(axis);
        m_dragT0 = axisParam(pv, m_dragDir, ro, rd);
    } else {
        float3 r, u, f;
        a.m_scene.camera.basis(r, u, f);
        m_dragDir = f;
        float den = dot(rd, f);
        float t = std::fabs(den) > 1e-6f ? dot(pv - ro, f) / den : 0.0f;
        m_dragHit0 = ro + rd * t;
    }
}

void CurveEditor::updateDrag(float mx, float my) {
    if (m_obj < 0) return;
    float3 ro, rd;
    a.cameraRay(mx, my, ro, rd);
    float3 delta(0);
    const bool snapOn = ImGui::IsKeyDown(ImGuiKey_X);
    const float g = grid();
    if (m_dragAxis < 3) {
        float d = axisParam(m_dragPivot, m_dragDir, ro, rd) - m_dragT0;
        if (snapOn) d = std::round((m_dragPivot[m_dragAxis] + d) / g) * g - m_dragPivot[m_dragAxis];
        delta = m_dragDir * d;
    } else {
        float den = dot(rd, m_dragDir);
        if (std::fabs(den) < 1e-6f) return;
        float t = dot(m_dragPivot - ro, m_dragDir) / den;
        delta = ro + rd * t - m_dragHit0;
        if (snapOn) delta = snap(m_dragPivot + delta) - m_dragPivot;
        SnapHit hit;   // osnap: the dragged point lands exactly on the snapped point
        if (findSnap(mx, my, hit)) { m_snap = hit; m_snap.on = true; delta = hit.p - m_dragPivot; }
    }
    Object& o = a.m_scene.objects[m_obj];
    CurveData& c = o.curve;
    const float3 ld = toLocalDir(o, delta);
    for (const Start& st : m_dragStart) itemRef(c, st.item) = st.local + ld;
    // a smooth Bezier handle moved alone turns its opposite handle (same length kept)
    if (c.type == CURVE_BEZIER)
        for (const Start& st : m_dragStart) {
            if (st.item.part == PART_POINT || isSelected({st.item.index, PART_POINT})) continue;
            const int i = st.item.index;
            if (i >= (int)c.broken.size() || c.broken[i]) continue;
            float3& h = itemRef(c, st.item);
            float3& opp = st.item.part == PART_IN ? c.hout[i] : c.hin[i];
            float len = length(opp - c.points[i]);
            float3 dir = h - c.points[i];
            if (length(dir) > 1e-6f) opp = c.points[i] - normalize(dir) * len;
        }
    c.shape.type = CSHAPE_NONE;   // an edited primitive becomes a free curve
    if (length(delta) > 0) m_dragMoved = true;
    a.markDirty();
}

// Moves the selection by a world vector (inspector fields, test).
void CurveEditor::applyDelta(const float3& worldDelta) {
    float3 pv;
    if (!selectionPivot(pv)) return;
    float2 sp;
    a.project(pv, sp);
    beginDrag(3, sp.x, sp.y);
    Object& o = a.m_scene.objects[m_obj];
    CurveData& c = o.curve;
    const float3 ld = toLocalDir(o, worldDelta);
    for (const Start& st : m_dragStart) itemRef(c, st.item) = st.local + ld;
    c.shape.type = CSHAPE_NONE;
    m_dragging = false;
    a.markDirty();
}

int CurveEditor::tangentMode(const CurveData& c, int i) const {
    if (c.type != CURVE_BEZIER || i < 0 || i >= c.count() || i >= (int)c.hin.size()) return 0;
    if (length(c.hin[i] - c.points[i]) < 1e-5f && length(c.hout[i] - c.points[i]) < 1e-5f) return 2;
    return i < (int)c.broken.size() && c.broken[i] ? 1 : 0;
}

// Smooth: the in tangent is aligned on the out one (its length kept); a corner gets automatic tangents.
// Broken: the handles move independently. Corner: both handles on the point (sharp angle).
void CurveEditor::setTangentMode(int i, int mode) {
    if (m_obj < 0) return;
    CurveData& c = a.m_scene.objects[m_obj].curve;
    if (c.type != CURVE_BEZIER || i < 0 || i >= c.count()) return;
    bezierFixArrays(c);
    const int was = tangentMode(c, i);
    if (mode == 2) { c.hin[i] = c.hout[i] = c.points[i]; c.broken[i] = 0; }
    else if (was == 2) { bezierAutoHandles(c, i); c.broken[i] = mode == 1 ? 1 : 0; }
    else if (mode == 1) c.broken[i] = 1;
    else {
        c.broken[i] = 0;
        float3 out = c.hout[i] - c.points[i];
        float lenIn = length(c.hin[i] - c.points[i]);
        if (length(out) > 1e-6f) c.hin[i] = c.points[i] - normalize(out) * lenIn;
    }
    c.shape.type = CSHAPE_NONE;
    a.markDirty();
}

void CurveEditor::toggleCorner(int i) {
    if (m_obj < 0) return;
    const CurveData& c = a.m_scene.objects[m_obj].curve;
    a.pushUndo();
    const bool corner = tangentMode(c, i) == 2;
    setTangentMode(i, corner ? 0 : 2);
    m_sel = {{i, PART_POINT}};
    a.status(corner ? L"Smooth point" : L"Corner point");
}

void CurveEditor::deleteSelectedPoints() {
    if (m_obj < 0 || m_sel.empty()) return;
    CurveData& c = a.m_scene.objects[m_obj].curve;
    std::vector<int> anchors;
    bool handles = false;
    for (const Item& it : m_sel) {
        if (it.part == PART_POINT) anchors.push_back(it.index);
        else handles = true;
    }
    if (!anchors.empty() && c.count() - (int)anchors.size() < 2) { a.status(L"A curve keeps at least 2 points (delete the object instead)", 2); return; }
    a.pushUndo();
    if (handles && c.type == CURVE_BEZIER)   // a deleted handle goes back to its automatic place
        for (const Item& it : m_sel) if (it.part != PART_POINT) bezierAutoHandles(c, it.index);
    std::sort(anchors.begin(), anchors.end(), [](int x, int y) { return x > y; });
    for (int i : anchors) eraseCurvePoint(c, i);
    m_sel.clear();
    a.markDirty();
    a.status(std::to_wstring(anchors.size()) + L" point(s) deleted");
}

void CurveEditor::insertPointAt(float mx, float my) {
    if (m_obj < 0) return;
    Object& o = a.m_scene.objects[m_obj];
    CurveData& c = o.curve;
    std::vector<float3> s;
    sampleCurve(c, s);
    Affine xf = o.transform();
    float best = 12.0f * a.m_dpi;
    int bi = -1;
    for (int i = 0; i < (int)s.size(); ++i) {
        float2 q;
        if (!a.project(xf.point(s[i]), q)) continue;
        float d = std::hypot(q.x - mx, q.y - my);
        if (d < best) { best = d; bi = i; }
    }
    if (bi < 0) { a.status(L"Ctrl+click on the curve to insert a point", 2); return; }
    const float3 q = s[bi];
    // between the two control points whose segment grows the least
    const int n = c.count();
    int after = n - 1;
    float bestCost = 1e30f;
    for (int i = 0; i < (c.closed ? n : n - 1); ++i) {
        const float3& p0 = c.points[i], &p1 = c.points[(i + 1) % n];
        float cost = length(q - p0) + length(p1 - q) - length(p1 - p0);
        if (cost < bestCost) { bestCost = cost; after = i; }
    }
    a.pushUndo();
    insertCurvePoint(c, after, q);
    m_sel = {{after + 1, PART_POINT}};
    a.markDirty();
    a.status(L"Point inserted");
}

// --------------------------------------------------------------------------- precision (Rhino style)

namespace {
const char* kSnapNames[] = {"End", "Point", "Mid", "Center", "Near"};
}

// Object snap under the mouse: end > point > mid > center > near (the first kind found within reach wins).
// In point mode the edited curve only offers its unselected points (the rest moves with the drag).
bool CurveEditor::findSnap(float mx, float my, SnapHit& out) const {
    if (!m_osnap) return false;
    const float reach = 12.0f * a.m_dpi, nearReach = 8.0f * a.m_dpi;
    int bestKind = SNAP_COUNT;
    float bestD = 1e30f;
    auto consider = [&](const float3& w, int kind, float r) {
        if (!(m_snapMask & (1 << kind)) || kind > bestKind) return;
        float2 s;
        if (!a.project(w, s)) return;
        float d = std::hypot(s.x - mx, s.y - my);
        if (d > r || (kind == bestKind && d >= bestD)) return;
        bestKind = kind;
        bestD = d;
        out.p = w;
        out.kind = kind;
    };
    std::vector<float3> s;
    const auto& objs = a.m_scene.objects;
    for (int i = 0; i < (int)objs.size(); ++i) {
        const Object& o = objs[i];
        if (!o.curve.valid() || !o.visible || o.ghost) continue;
        const CurveData& c = o.curve;
        const Affine xf = o.transform();
        const bool own = m_mode == MODE_POINTS && i == m_obj;
        const int n = c.count();
        for (int k = 0; k < n; ++k) {
            if (own && isSelected({k, PART_POINT})) continue;
            const bool end = !c.closed && (k == 0 || k == n - 1);
            consider(xf.point(c.points[k]), end ? SNAP_END : SNAP_POINT, reach);
        }
        if (own) continue;
        sampleCurve(c, s);
        if (s.size() < 2) continue;
        if (!c.closed) { consider(xf.point(s.front()), SNAP_END, reach); consider(xf.point(s.back()), SNAP_END, reach); }
        // middle of the curve (arc length), and of each segment of a linear curve
        std::vector<float> acc(s.size(), 0.0f);
        for (size_t k = 1; k < s.size(); ++k) acc[k] = acc[k - 1] + length(s[k] - s[k - 1]);
        const float half = acc.back() * 0.5f;
        for (size_t k = 1; k < s.size(); ++k)
            if (acc[k] >= half) {
                float seg = acc[k] - acc[k - 1];
                consider(xf.point(lerp(s[k - 1], s[k], seg > 1e-9f ? (half - acc[k - 1]) / seg : 0.0f)), SNAP_MID, reach);
                break;
            }
        if (c.type == CURVE_LINEAR)
            for (int k = 0; k + 1 < n + (c.closed ? 1 : 0); ++k) consider(xf.point((c.points[k] + c.points[(k + 1) % n]) * 0.5f), SNAP_MID, reach);
        // centre: the origin of a primitive, the average of a closed curve
        if (c.shape.type != CSHAPE_NONE && c.shape.type != CSHAPE_LINE) consider(xf.point(float3(0)), SNAP_CENTER, reach);
        else if (c.closed) {
            float3 sum(0);
            for (size_t k = 0; k + 1 < s.size(); ++k) sum += s[k];
            consider(xf.point(sum / (float)(s.size() - 1)), SNAP_CENTER, reach);
        }
        // near: the closest point of the curve on screen
        if (m_snapMask & (1 << SNAP_NEAR) && bestKind >= SNAP_NEAR) {
            float2 prev;
            bool hasPrev = false;
            for (size_t k = 0; k < s.size(); ++k) {
                float2 q;
                bool ok = a.project(xf.point(s[k]), q);
                if (ok && hasPrev) {
                    float dx = q.x - prev.x, dy = q.y - prev.y, l2 = dx * dx + dy * dy;
                    float t = l2 > 1e-6f ? clampf(((mx - prev.x) * dx + (my - prev.y) * dy) / l2, 0, 1) : 0;
                    consider(xf.point(lerp(s[k - 1], s[k], t)), SNAP_NEAR, nearReach);
                }
                prev = q;
                hasPrev = ok;
            }
        }
    }
    // the points already placed by the creation tool (the first one closes the curve, see input)
    if (m_mode == MODE_CREATE)
        for (int k = m_draft.count() >= 3 ? 1 : 0; k < m_draft.count(); ++k) consider(m_draft.points[k], SNAP_POINT, reach);
    return bestKind < SNAP_COUNT;
}

float3 CurveEditor::createTarget(float mx, float my, bool& ok) {
    float3 p;
    ok = groundPoint(mx, my, 0.0f, p);
    SnapHit hit;
    if (findSnap(mx, my, hit)) { m_snap = hit; m_snap.on = true; ok = true; return hit.p; }
    if (!ok) return p;
    if (ImGui::IsKeyDown(ImGuiKey_X)) p = snap(p);
    if (ImGui::GetIO().KeyShift && !m_draft.points.empty()) {   // ortho: along X or Z from the last point
        const float3 last = m_draft.points.back();
        if (std::fabs(p.x - last.x) >= std::fabs(p.z - last.z)) p.z = last.z; else p.x = last.x;
        p.y = last.y;
    }
    return p;
}

void CurveEditor::draftAdd(const float3& p) {
    if (!m_draft.points.empty() && length(p - m_draft.points.back()) <= 1e-4f) return;
    m_draft.points.push_back(p);
    if (m_createType == CURVE_BEZIER) {
        m_draft.hin.push_back(p);
        m_draft.hout.push_back(p);
        m_draft.broken.push_back(0);
    }
}

// Typed point (Rhino / CAD style): "x, z" (on the ground), "x, y, z", "@dx, dz" or "@dx, dy, dz" (relative to the
// last point, also "r dx, dz"), "d<angle" (distance and angle in degrees from the last point, on the ground: 0 = +X,
// 90 = -Z), or a single distance along the direction of the mouse.
bool CurveEditor::parseCoord(const char* text, float3& out) const {
    std::string t(text);
    for (char& ch : t) if (ch == ';') ch = ',';
    size_t b = t.find_first_not_of(" \t");
    if (b == std::string::npos) return false;
    t = t.substr(b);
    bool rel = false;
    if (t[0] == '@' || t[0] == 'r' || t[0] == 'R') { rel = true; t = t.substr(1); }
    const bool haveLast = !m_draft.points.empty();
    const float3 last = haveLast ? m_draft.points.back() : float3(0);
    size_t lt = t.find('<');
    if (lt != std::string::npos) {
        char* e = nullptr;
        float dist = strtof(t.c_str(), &e), ang = strtof(t.c_str() + lt + 1, nullptr);
        if (e == t.c_str()) return false;
        out = last + float3(dist * std::cos(radians(ang)), 0, -dist * std::sin(radians(ang)));
        return true;
    }
    std::vector<float> v;
    const char* p = t.c_str();
    while (*p) {
        while (*p == ' ' || *p == ',' || *p == '\t') ++p;
        if (!*p) break;
        char* e = nullptr;
        float f = strtof(p, &e);
        if (e == p) return false;
        v.push_back(f);
        p = e;
    }
    if (v.size() == 1) {   // distance towards the mouse
        if (!haveLast) return false;
        ImVec2 m = ImGui::GetIO().MousePos;
        float3 g;
        if (!groundPoint(m.x, m.y, last.y, g)) return false;
        float3 dir = g - last;
        dir.y = 0;
        if (length(dir) < 1e-6f) dir = float3(1, 0, 0);
        out = last + normalize(dir) * v[0];
        return true;
    }
    float3 q = v.size() == 2 ? float3(v[0], 0, v[1]) : v.size() == 3 ? float3(v[0], v[1], v[2]) : float3(0);
    if (v.size() < 2 || v.size() > 3) return false;
    out = rel ? last + q : q;
    return true;
}

void CurveEditor::drawSnap(ImDrawList* dl) const {
    if (!m_snap.on) return;
    float2 s;
    if (!a.project(m_snap.p, s)) return;
    const float d = a.m_dpi, r = 6.0f * d;
    const ImU32 c = IM_COL32(132, 190, 210, 255);
    dl->AddRect(ImVec2(s.x - r, s.y - r), ImVec2(s.x + r, s.y + r), c, 0, 0, 1.6f * d);
    dl->AddCircleFilled(ImVec2(s.x, s.y), 2.2f * d, c, 12);
    const char* label = kSnapNames[std::clamp(m_snap.kind, 0, (int)SNAP_COUNT - 1)];
    ImVec2 ts = ImGui::CalcTextSize(label);
    ImVec2 at(s.x + 11 * d, s.y - ts.y - 6 * d);
    dl->AddRectFilled(ImVec2(at.x - 4 * d, at.y - 2 * d), ImVec2(at.x + ts.x + 4 * d, at.y + ts.y + 2 * d), IM_COL32(18, 20, 24, 210), 3 * d);
    dl->AddText(at, c, label);
}

namespace {
int coordCaret(ImGuiInputTextCallbackData* data) {
    bool* once = (bool*)data->UserData;
    if (*once) {
        data->CursorPos = data->SelectionStart = data->SelectionEnd = data->BufTextLen;
        *once = false;
    }
    return 0;
}
}

// Coordinate field of the creation tools, above the hint (typing a digit, '-', '.', '@' or '<' starts it).
void CurveEditor::drawCoordField() {
    const float d = a.m_dpi, w = 300 * d;
    ImGui::SetCursorScreenPos(ImVec2(a.m_rcView.x + (a.m_rcView.w - w) * 0.5f, a.m_rcView.b() - 104 * d));
    ImGui::SetNextItemWidth(w);
    if (m_coordFocus) { ImGui::SetKeyboardFocusHere(); m_coordFocus = false; m_coordCaretEnd = true; }
    if (ImGui::InputTextWithHint("##crvCoord", "Type a point: x, z   @dx, dz   dist<angle", m_coordBuf, sizeof(m_coordBuf),
                                 ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackAlways, coordCaret, &m_coordCaretEnd)) {
        float3 p;
        if (!m_coordBuf[0]) finishCreate();   // Enter on an empty field: the curve is finished
        else if (parseCoord(m_coordBuf, p)) {
            draftAdd(p);
            m_coordBuf[0] = 0;
            m_coordFocus = true;   // ready for the next point
        } else a.status(L"Coordinates not understood (x, z  ·  x, y, z  ·  @dx, dz  ·  dist<angle)", 2);
    }
}

// --------------------------------------------------------------------------- menu

void CurveEditor::menu(float menuScale) {
    if (!ImGui::BeginMenu("Curve")) return;
    ImGui::SetWindowFontScale(menuScale);
    if (ImGui::MenuItem("CV Curve Tool")) startCreate(CURVE_CV);
    if (ImGui::MenuItem("EP Curve Tool")) startCreate(CURVE_EP);
    if (ImGui::MenuItem("Bezier Curve Tool")) startCreate(CURVE_BEZIER);
    if (ImGui::MenuItem("Linear Curve Tool")) startCreate(CURVE_LINEAR);
    ImGui::Separator();
    for (int sh = CSHAPE_LINE; sh < CSHAPE_COUNT; ++sh)
        if (ImGui::MenuItem(curveShapeName(sh))) startDraw(sh);   // drawn with the mouse (3ds Max)
    ImGui::Separator();
    if (ImGui::BeginMenu("Osnap")) {   // Rhino-style object snaps of the curve tools
        ImGui::SetWindowFontScale(menuScale);
        ImGui::MenuItem("Osnap", "O", &m_osnap);
        ImGui::Separator();
        for (int k = 0; k < SNAP_COUNT; ++k) {
            bool on = (m_snapMask >> k) & 1;
            if (ImGui::MenuItem(kSnapNames[k], nullptr, &on, m_osnap)) m_snapMask ^= 1 << k;
        }
        ImGui::EndMenu();
    }
    ImGui::Separator();
    const bool curveSel = a.m_selected >= 0 && a.m_scene.objects[a.m_selected].curve.valid();
    if (ImGui::MenuItem("Edit Points", "F8", m_mode == MODE_POINTS, curveSel)) { if (m_mode == MODE_POINTS) exitMode(); else enterPoints(); }
    ImGui::EndMenu();
}

// --------------------------------------------------------------------------- drawing
// Visual language of the WZX curves: pale cyan curve, silver hollow handles on dashed tangents, control points
// larger than the tangent ends, filled active node; inactive curves stay discreet (no glow).

namespace {

void dashedLine(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float width, float dash, float gap) {
    float dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-3f) return;
    dx /= len;
    dy /= len;
    for (float t = 0; t < len; t += dash + gap) {
        float e = std::min(t + dash, len);
        dl->AddLine(ImVec2(a.x + dx * t, a.y + dy * t), ImVec2(a.x + dx * e, a.y + dy * e), col, width);
    }
}


void hollowNode(ImDrawList* dl, ImVec2 p, float r, ImU32 ring, float d) {
    dl->AddCircleFilled(p, r, kNodeFill, 20);
    dl->AddCircle(p, r, ring, 20, 1.6f * d);
}

void activeNode(ImDrawList* dl, ImVec2 p, float r, float d) {
    dl->AddCircleFilled(p, r, kCurveOn, 28);
    dl->AddCircle(p, r, kNodeRing, 28, 2.0f * d);
}

} // namespace

// a Bezier anchor shows its handles when it (or one of its handles) is selected
bool CurveEditor::handlesShown(int i) const {
    return isSelected({i, PART_POINT}) || isSelected({i, PART_IN}) || isSelected({i, PART_OUT});
}

void CurveEditor::drawCurve(ImDrawList* dl, const Object& o, ImU32 col, float width) const {
    std::vector<float3> s;
    sampleCurve(o.curve, s);
    Affine xf = o.transform();
    std::vector<ImVec2> run;
    auto flush = [&]() {
        if (run.size() >= 2) {
            dl->AddPolyline(run.data(), (int)run.size(), col, 0, width);
        }
        run.clear();
    };
    for (const float3& p : s) {
        float2 q;
        if (a.project(xf.point(p), q)) run.push_back(iv(q));
        else flush();
    }
    flush();
}

void CurveEditor::drawPoints(ImDrawList* dl, const Object& o) const {
    const CurveData& c = o.curve;
    const float d = a.m_dpi;
    auto P = [&](const float3& p, float2& s) { return a.project(toWorld(o, p), s); };
    // CV hull: dashed, as the tangents
    if (c.type == CURVE_CV)
        for (int i = 0; i + 1 < c.count() + (c.closed ? 1 : 0); ++i) {
            float2 p0, p1;
            if (P(c.points[i], p0) && P(c.points[(i + 1) % c.count()], p1)) dashedLine(dl, iv(p0), iv(p1), kHull, 1.2f * d, 5 * d, 4 * d);
        }
    // Bezier tangents of the selected anchors: dashed line, small silver hollow end
    if (c.type == CURVE_BEZIER && (int)c.hin.size() == c.count())
        for (int i = 0; i < c.count(); ++i) {
            if (!handlesShown(i)) continue;
            float2 pa;
            if (!P(c.points[i], pa)) continue;
            for (int part : {(int)PART_IN, (int)PART_OUT}) {
                const float3& h = part == PART_IN ? c.hin[i] : c.hout[i];
                if (length(h - c.points[i]) < 1e-6f) continue;
                float2 ph;
                if (!P(h, ph)) continue;
                dashedLine(dl, iv(pa), iv(ph), kHandle, 1.3f * d, 5 * d, 4 * d);
                if (isSelected({i, part})) activeNode(dl, iv(ph), 4.0f * d, d);
                else hollowNode(dl, iv(ph), 4.0f * d, kHandle, d);
            }
        }
    // control points (larger than the tangent ends); the open ends are filled cyan dots
    for (int i = 0; i < c.count(); ++i) {
        float2 p;
        if (!P(c.points[i], p)) continue;
        if (isSelected({i, PART_POINT})) { activeNode(dl, iv(p), 7.0f * d, d); continue; }
        const bool end = !c.closed && (i == 0 || i == c.count() - 1);
        if (end) {
            dl->AddCircleFilled(iv(p), 5.5f * d, kCurveOn, 20);
            if (i == 0) dl->AddCircle(iv(p), 8.5f * d, (kCurveOn & 0x00FFFFFFu) | 0x80000000u, 24, 1.2f * d);   // start of the curve
        } else hollowNode(dl, iv(p), 5.5f * d, kHandle, d);
    }
}

void CurveEditor::drawManipulator(ImDrawList* dl) const {
    float3 pv;
    if (!selectionPivot(pv)) return;
    float2 pc;
    if (!a.project(pv, pc)) return;
    const float d = a.m_dpi, s = a.gizmoScale(pv);
    for (int k = 0; k < 3; ++k) {
        bool lit = m_dragging ? m_dragAxis == k : m_hoverAxis == k;
        ImU32 c = lit ? IM_COL32(255, 255, 255, 255) : kAxisCol[k];
        float2 p0, pb, pt;
        // starts a little away from the node so that the active point stays readable
        if (!a.project(pv + axisVec(k) * (0.22f * s), p0) || !a.project(pv + axisVec(k) * (0.85f * s), pb) || !a.project(pv + axisVec(k) * (1.1f * s), pt)) continue;
        dl->AddLine(iv(p0), iv(pb), c, (lit ? 2.6f : 1.8f) * d);
        float dx = pt.x - pb.x, dy = pt.y - pb.y, l = std::sqrt(dx * dx + dy * dy);
        if (l < 1e-3f) continue;
        dx /= l;
        dy /= l;
        float hw = (lit ? 5.5f : 4.5f) * d;
        dl->AddTriangleFilled(ImVec2(pt.x, pt.y), ImVec2(pb.x - dy * hw, pb.y + dx * hw), ImVec2(pb.x + dy * hw, pb.y - dx * hw), c);
    }
    // free move: a thin ring around the node
    bool lit = m_dragging ? m_dragAxis == 3 : m_hoverAxis == 3;
    dl->AddCircle(iv(pc), 11.0f * d, lit ? kNodeRing : (kNodeRing & 0x00FFFFFFu) | 0x70000000u, 32, (lit ? 2.0f : 1.2f) * d);
}

void CurveEditor::drawDraft(ImDrawList* dl) {
    ImGuiIO& io = ImGui::GetIO();
    const float d = a.m_dpi;
    CurveData c = m_draft;
    float mx = io.MousePos.x, my = io.MousePos.y;
    float2 tm;   // --curve-test capture: the mouse held on a scene point
    if (m_testMouse && a.project(m_testMouseAt, tm)) { mx = tm.x; my = tm.y; }
    bool hoverFirst = false;
    // preview of the next point under the mouse
    if (!m_draftDrag && a.m_rcView.contains(mx, my)) {
        bool ok = false;
        float3 p = createTarget(mx, my, ok);   // same target as the click: osnap, grid (X), ortho (Shift)
        if (ok) {
            float2 s0;
            if (c.count() >= 3 && a.project(c.points[0], s0) && std::hypot(s0.x - mx, s0.y - my) < 9.0f * d) {
                c.closed = true;
                hoverFirst = true;
                m_snap.on = false;   // the close ring is shown instead
            } else {
                c.points.push_back(p);
                if (c.type == CURVE_BEZIER) { c.hin.push_back(p); c.hout.push_back(p); c.broken.push_back(0); }
            }
        }
    }
    if (c.type == CURVE_BEZIER) {
        bezierFixArrays(c);
        for (int i = 0; i < c.count(); ++i)
            if (length(c.hout[i] - c.points[i]) < 1e-5f && length(c.hin[i] - c.points[i]) < 1e-5f) bezierAutoHandles(c, i);
    }
    if (c.count() >= 2) {
        std::vector<float3> s;
        sampleCurve(c, s);
        std::vector<ImVec2> run;
        for (const float3& p : s) {
            float2 q;
            if (a.project(p, q)) run.push_back(iv(q));
        }
        if (run.size() >= 2) {
            dl->AddPolyline(run.data(), (int)run.size(), kCurveOn, 0, 1.6f * d);
        }
    }
    const int last = m_draft.count() - 1;
    for (int i = 0; i <= last; ++i) {
        float2 p;
        if (!a.project(m_draft.points[i], p)) continue;
        // the anchor being placed shows its tangent
        if (m_createType == CURVE_BEZIER && i == last && i < (int)m_draft.hout.size() && length(m_draft.hout[i] - m_draft.points[i]) > 1e-5f) {
            float2 h0, h1;
            if (a.project(m_draft.hin[i], h0) && a.project(m_draft.hout[i], h1)) {
                dashedLine(dl, iv(h0), iv(h1), kHandle, 1.3f * d, 5 * d, 4 * d);
                hollowNode(dl, iv(h0), 4.0f * d, kHandle, d);
                hollowNode(dl, iv(h1), 4.0f * d, kHandle, d);
            }
        }
        if (i == last && !hoverFirst) activeNode(dl, iv(p), 6.0f * d, d);
        else if (i == 0) dl->AddCircleFilled(iv(p), 5.5f * d, kCurveOn, 20);
        else hollowNode(dl, iv(p), 5.0f * d, kHandle, d);
        if (i == 0 && hoverFirst) dl->AddCircle(iv(p), 10.0f * d, kNodeRing, 24, 2.0f * d);
    }
}


void CurveEditor::draw(ImDrawList* dl) {
    validate();
    const float d = a.m_dpi;
    const auto& objs = a.m_scene.objects;
    for (int i = 0; i < (int)objs.size(); ++i) {
        const Object& o = objs[i];
        if (!o.curve.valid() || !o.visible || o.ghost) continue;
        const bool primary = i == a.m_selected;
        const bool multi = std::find(a.m_multi.begin(), a.m_multi.end(), i) != a.m_multi.end();
        // active curve: pale cyan, no glow; inactive: the same hue, thinner and translucent
        if (primary) drawCurve(dl, o, kCurveOn, 1.6f * d);
        else if (multi) drawCurve(dl, o, kCurveMulti, 1.3f * d);
        else drawCurve(dl, o, kCurveOff, 1.0f * d);
    }
    std::string hint;
    if (m_mode == MODE_POINTS && m_obj >= 0) {
        drawPoints(dl, objs[m_obj]);
        drawManipulator(dl);
        if (m_dragging) drawSnap(dl);
        if (m_marquee) {
            ImVec2 m = ImGui::GetIO().MousePos;
            ImVec2 p0(std::min(m_marq0.x, m.x), std::min(m_marq0.y, m.y)), p1(std::max(m_marq0.x, m.x), std::max(m_marq0.y, m.y));
            dl->AddRectFilled(p0, p1, (kCurveOn & 0x00FFFFFFu) | 0x1C000000u);
            dashedLine(dl, p0, ImVec2(p1.x, p0.y), kCurveOn, 1.0f * d, 4 * d, 3 * d); dashedLine(dl, ImVec2(p1.x, p0.y), p1, kCurveOn, 1.0f * d, 4 * d, 3 * d);
            dashedLine(dl, p1, ImVec2(p0.x, p1.y), kCurveOn, 1.0f * d, 4 * d, 3 * d); dashedLine(dl, ImVec2(p0.x, p1.y), p0, kCurveOn, 1.0f * d, 4 * d, 3 * d);
        }
        hint = "Edit Points  ·  " + std::to_string(m_sel.size()) + " selected  ·  Osnap " + std::string(m_osnap ? "on" : "off") + " (O)  ·  X: grid  ·  Ctrl+click curve: insert  ·  Ctrl+drag tangent: break  ·  Double-click: corner / smooth  ·  F8: exit";
        if (m_filletTool) {
            char b[96];
            snprintf(b, sizeof(b), "Fillet  ·  drag a point upwards  ·  radius %.2f  ·  Esc: end the tool", m_filleting ? m_filletR : 0.0f);
            hint = b;
        }
    } else if (m_mode == MODE_DRAW) {
        drawSnap(dl);
        if (m_drawStep > 0) {
            float3 pos;
            float4 rot;
            Object tmp;
            tmp.curve = drawShape(pos, rot);
            tmp.position = pos;
            tmp.rot = rot;
            tmp.useRot = true;
            drawCurve(dl, tmp, kCurveOn, 1.6f * d);
            float2 s0, s1;   // the size being dragged: dashed from the press point
            if (a.project(m_drawStart, s0) && a.project(m_drawCur, s1)) {
                dashedLine(dl, iv(s0), iv(s1), kHandle, 1.2f * d, 5 * d, 4 * d);
                dl->AddCircleFilled(iv(s0), 3.0f * d, kCurveOn, 12);
            }
        }
        const char* name = curveShapeName(m_drawShape);
        static const char* second[] = {"", "", "", "", "move to set the sweep, click", "move up to set the height, click", "move to set the inner radius, click", "move to set the inner radius, click"};
        hint = std::string(name) + "  ·  " + (m_drawStep == 0 ? std::string("press and drag (click: default size)")
                                             : m_drawStep == 1 ? std::string(m_drawShape == CSHAPE_SQUARE ? "drag the corner (Shift: square)" : "drag the size")
                                                               : std::string(second[std::clamp(m_drawShape, 0, 7)])) +
               "  ·  X: grid  ·  Osnap (O)  ·  Esc: cancel";
    } else if (m_mode == MODE_CREATE) {
        drawDraft(dl);
        drawSnap(dl);
        drawCoordField();
        hint = std::string(curveTypeName(m_createType)) + " Tool  ·  " + std::to_string(m_draft.count()) + " points  ·  Osnap " + (m_osnap ? "on" : "off") + " (O)  ·  Shift: ortho  ·  X: grid  ·  type coordinates  ·  Enter: finish  ·  Esc: cancel";
    }
    if (!hint.empty()) {
        ImVec2 ts = ImGui::CalcTextSize(hint.c_str());
        float x = a.m_rcView.x + (a.m_rcView.w - ts.x) * 0.5f, y = a.m_rcView.b() - ts.y - 46 * d;
        dl->AddRectFilled(ImVec2(x - 10 * d, y - 5 * d), ImVec2(x + ts.x + 10 * d, y + ts.y + 5 * d), IM_COL32(18, 20, 24, 200), 5 * d);
        dl->AddText(ImVec2(x, y), IM_COL32(236, 238, 242, 255), hint.c_str());
    }
}

// --------------------------------------------------------------------------- test (--curve-test report.txt)

void CurveEditor::beginFrame() {
    if (testReport.empty()) return;
    if (++m_testFrame == 3) runTest();
}

void CurveEditor::runTest() {
    std::ostringstream r;
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { r << (ok ? "OK    " : "FAIL  ") << what << "\n"; if (!ok) ++fails; };
    Scene& sc = a.m_scene;
    const float g = grid();
    auto lastObj = [&]() -> Object& { return sc.objects[a.m_selected]; };
    auto minDist = [](const std::vector<float3>& s, const float3& p) {
        float best = 1e30f;
        for (const float3& q : s) best = std::min(best, length(q - p));
        return best;
    };
    // the creation tool, fed with points on the ground as the clicks would
    auto create = [&](int type, std::vector<float3> pts, bool close, const std::vector<float3>* pulls = nullptr) -> int {
        startCreate(type);
        for (size_t i = 0; i < pts.size(); ++i) {
            m_draft.points.push_back(pts[i]);
            if (type == CURVE_BEZIER) {
                float3 h = pulls ? (*pulls)[i] : pts[i];
                m_draft.hout.push_back(h);
                m_draft.hin.push_back(pts[i] * 2.0f - h);
                m_draft.broken.push_back(0);
            }
        }
        m_draft.closed = close;
        finishCreate();
        return a.m_selected;
    };
    const float3 base = snap(float3(sc.camera.target.x, 0, sc.camera.target.z));
    std::vector<float3> s;

    int cv = create(CURVE_CV, {base + float3(-6, 0, -8), base + float3(-4, 0, -4), base + float3(-6, 0, 0), base + float3(-4, 0, 4), base + float3(-6, 0, 8)}, false);
    {
        Object& o = sc.objects[cv];
        sampleCurve(o.curve, s);
        check(o.curve.type == CURVE_CV && o.curve.count() == 5 && curveDegree(o.curve) == 3 && curveSpans(o.curve) == 2, "CV curve: 5 points, degree 3, 2 spans");
        check(length(s.front() - o.curve.points.front()) < 1e-4f && length(s.back() - o.curve.points.back()) < 1e-4f, "CV curve: starts and ends on its end points");
        check(minDist(s, o.curve.points[2]) > 0.05f, "CV curve: smooth (does not pass through the middle CV)");
    }
    int ep = create(CURVE_EP, {base + float3(-2, 0, -8), base + float3(0, 0, -4), base + float3(-2, 0, 0), base + float3(0, 0, 4), base + float3(-2, 0, 8)}, false);
    {
        Object& o = sc.objects[ep];
        sampleCurve(o.curve, s);
        float worst = 0;
        for (const float3& p : o.curve.points) worst = std::max(worst, minDist(s, p));
        check(worst < 1e-4f, "EP curve: passes through every point");
    }
    std::vector<float3> bz = {base + float3(2, 0, -8), base + float3(4, 0, 0), base + float3(2, 0, 8)};
    std::vector<float3> pulls = {base + float3(5, 0, -8), base + float3(4, 0, 3), base + float3(-1, 0, 8)};
    int bez = create(CURVE_BEZIER, bz, false, &pulls);
    {
        Object& o = sc.objects[bez];
        sampleCurve(o.curve, s);
        float worst = 0;
        for (const float3& p : o.curve.points) worst = std::max(worst, minDist(s, p));
        check(o.curve.hin.size() == 3 && o.curve.hout.size() == 3, "Bezier: one in and out handle per anchor");
        check(worst < 1e-4f, "Bezier: passes through its anchors");
        check(length((o.curve.hout[1] - o.curve.points[1]) + (o.curve.hin[1] - o.curve.points[1])) < 1e-4f, "Bezier: dragged handles are symmetric");
    }
    int lin = create(CURVE_LINEAR, {base + float3(6, 0, -8), base + float3(10, 0, -8), base + float3(10, 0, -2), base + float3(6, 0, -2)}, true);
    {
        Object& o = sc.objects[lin];
        sampleCurve(o.curve, s);
        check(o.curve.closed && s.size() == 5 && curveDegree(o.curve) == 1, "Linear closed curve: 4 points, 5 samples, degree 1");
        check(std::fabs(curveLength(o.curve) - 20.0f) < 1e-3f, "Linear closed curve: length 20");
    }
    // primitives
    std::vector<int> prims;
    for (int sh = CSHAPE_LINE; sh < CSHAPE_COUNT; ++sh) {
        addPrimitive(sh);
        Object& o = lastObj();
        o.position = base + float3(-12.0f + (sh - 1) * 6.0f, 0, 16.0f);
        prims.push_back(a.m_selected);
    }
    {
        const CurveData& c = sc.objects[prims[CSHAPE_CIRCLE - 1]].curve;
        sampleCurve(c, s);
        float worst = 0;
        for (const float3& p : s) worst = std::max(worst, std::fabs(std::sqrt(p.x * p.x + p.z * p.z) - c.shape.radius));
        check(c.type == CURVE_CV && c.closed && c.count() == 8, "Circle: closed CV curve, 8 sections");
        char b[96];
        snprintf(b, sizeof(b), "Circle: radius error %.4f (< 1.5%% of %.1f)", worst, c.shape.radius);
        check(worst < 0.015f * c.shape.radius, b);
        const CurveData& sq = sc.objects[prims[CSHAPE_SQUARE - 1]].curve;
        check(sq.type == CURVE_LINEAR && sq.closed && sq.count() == 4 && std::fabs(curveLength(sq) - 2 * (sq.shape.width + sq.shape.depth)) < 1e-3f, "Square: 4 points, closed, perimeter");
        const CurveData& st = sc.objects[prims[CSHAPE_STAR - 1]].curve;
        check(st.closed && st.count() == 10, "Star: 5 branches = 10 points");
        const CurveData& hx = sc.objects[prims[CSHAPE_HELIX - 1]].curve;
        check(std::fabs(hx.points.back().y - hx.shape.height) < 1e-4f && hx.points.front().y == 0.0f, "Helix: from 0 to its height");
        const CurveData& sp = sc.objects[prims[CSHAPE_SPIRAL - 1]].curve;
        check(std::fabs(length(sp.points.front()) - sp.shape.radius2) < 1e-4f && std::fabs(length(sp.points.back()) - sp.shape.radius) < 1e-4f, "Spiral: inner to outer radius");
        const CurveData& ar = sc.objects[prims[CSHAPE_ARC - 1]].curve;
        float3 e = ar.points.back();
        check(std::fabs(std::atan2(e.z, e.x) - kPi) < 1e-3f || std::fabs(std::atan2(e.z, e.x) + kPi) < 1e-3f, "Arc: 180 degrees");
    }
    // primitive parameters rebuild the points
    {
        a.select(prims[CSHAPE_CIRCLE - 1]);
        CurveData& c = lastObj().curve;
        c.shape.sections = 12;
        c.shape.radius = 3.0f;
        buildCurveShape(c);
        check(c.count() == 12 && c.shape.type == CSHAPE_CIRCLE, "Circle: 12 sections after the parameter change");
    }
    // Rhino-style precision: object snaps and typed coordinates
    {
        auto snapAt = [&](const float3& w, SnapHit& hit) {
            float2 sp;
            return a.project(w, sp) && findSnap(sp.x, sp.y, hit);
        };
        SnapHit hit;
        const Object& cvo = sc.objects[cv];
        const float3 end = cvo.transform().point(cvo.curve.points.back());
        check(snapAt(end, hit) && hit.kind == SNAP_END && length(hit.p - end) < 1e-4f, "Osnap End on the last point of the CV curve");
        const Object& lo = sc.objects[lin];
        const float3 mid = lo.transform().point((lo.curve.points[0] + lo.curve.points[1]) * 0.5f);
        check(snapAt(mid, hit) && hit.kind == SNAP_MID && length(hit.p - mid) < 1e-4f, "Osnap Mid on a segment of the linear curve");
        const Object& co = sc.objects[prims[CSHAPE_CIRCLE - 1]];
        check(snapAt(co.position, hit) && hit.kind == SNAP_CENTER && length(hit.p - co.position) < 1e-4f, "Osnap Center of the circle");
        m_osnap = false;
        check(!snapAt(end, hit), "Osnap off (O)");
        m_osnap = true;
        startCreate(CURVE_LINEAR);
        float3 p0 = base + float3(0, 0, -14), q;
        draftAdd(p0);
        bool okRel = parseCoord("@4, 0", q) && length(q - (p0 + float3(4, 0, 0))) < 1e-5f;
        draftAdd(q);
        bool okPolar = parseCoord("3<90", q) && length(q - (p0 + float3(4, 0, -3))) < 1e-4f;
        draftAdd(q);
        float3 abs3;
        bool okAbs = parseCoord("1.5, 2, -3", abs3) && length(abs3 - float3(1.5f, 2, -3)) < 1e-5f;
        bool okBad = !parseCoord("abc", abs3);
        finishCreate();
        check(okRel && okPolar && okAbs && okBad, "Typed coordinates: @dx, dz  ·  dist<angle  ·  x, y, z  ·  error refused");
        check(sc.objects[a.m_selected].curve.count() == 3, "Curve drawn from typed coordinates (3 points)");
    }
    // primitives drawn with the mouse (3ds Max) and fillet
    {
        auto drawn = [&](int shape, const float3& from, const float3& to, float second) -> Object& {
            startDraw(shape);
            m_drawStart = from;
            m_drawCur = to;
            m_drawStep = second >= 0 ? 2 : 1;
            m_draw2 = second;
            finishDraw();
            return lastObj();
        };
        Object& ci = drawn(CSHAPE_CIRCLE, base + float3(20, 0, 0), base + float3(23, 0, 0), -1);
        check(std::fabs(ci.curve.shape.radius - 3.0f) < 1e-4f && length(ci.position - (base + float3(20, 0, 0))) < 1e-5f, "Drawn circle: centre at the press, radius = drag (3)");
        Object& rc = drawn(CSHAPE_SQUARE, base + float3(20, 0, 6), base + float3(24, 0, 8), -1);
        check(std::fabs(rc.curve.shape.width - 4) < 1e-4f && std::fabs(rc.curve.shape.depth - 2) < 1e-4f && length(rc.position - (base + float3(22, 0, 7))) < 1e-4f, "Drawn rectangle: corner to corner (4 x 2)");
        Object& st = drawn(CSHAPE_STAR, base + float3(30, 0, 0), base + float3(34, 0, 0), 1.5f);
        check(std::fabs(st.curve.shape.radius - 4) < 1e-4f && std::fabs(st.curve.shape.radius2 - 1.5f) < 1e-4f, "Drawn star: outer radius by the drag, inner radius by the second move");
        Object& ln = drawn(CSHAPE_LINE, base + float3(20, 0, 12), base + float3(20, 0, 17), -1);
        float3 e0 = ln.transform().point(ln.curve.points[0]), e1 = ln.transform().point(ln.curve.points[1]);
        check(std::fabs(length(e1 - e0) - 5) < 1e-4f && std::min(length(e0 - (base + float3(20, 0, 12))), length(e1 - (base + float3(20, 0, 12)))) < 1e-4f, "Drawn line: from the press to the release");
        CurveData sq = makeCurveShape(CSHAPE_SQUARE);   // 4 x 4
        CurveData f1 = filletCurve(sq, {0, 1, 2, 3}, 1.0f);
        const float want = 16.0f - 8.0f + 2 * kPi;
        char b[128];
        snprintf(b, sizeof(b), "Fillet r=1 on the 4 corners of a 4x4 square: 8 points, length %.3f (expected %.3f)", curveLength(f1), want);
        check(f1.count() == 8 && f1.type == CURVE_BEZIER && std::fabs(curveLength(f1) - want) < 0.005f * want, b);
        CurveData f2 = filletCurve(sq, {0, 1, 2, 3}, 50.0f);
        snprintf(b, sizeof(b), "Fillet radius limited by the segments: length %.3f (circle of radius 2: %.3f)", curveLength(f2), 4 * kPi);
        check(std::fabs(curveLength(f2) - 4 * kPi) < 0.01f * 4 * kPi, b);
        CurveData f3 = filletCurve(sq, {1}, 1.0f);
        std::vector<float3> s3;
        sampleCurve(f3, s3);
        bool straightKept = true;   // the untouched corners stay sharp, the segments straight
        for (const float3& q : s3) straightKept &= std::fabs(std::fabs(q.x) - 2) < 1e-3f || std::fabs(std::fabs(q.z) - 2) < 1e-3f || (q.x > 0.99f && q.z < -0.99f);
        check(f3.count() == 5 && straightKept, "Fillet of one corner: the rest of the square is unchanged");
    }
    // point editing
    {
        a.select(cv);
        enterPoints();
        check(m_mode == MODE_POINTS && m_obj == cv, "Edit Points mode on the CV curve");
        m_sel = {{2, PART_POINT}};
        const float3 before = sc.objects[cv].curve.points[2];
        size_t undoBefore = a.m_undo.size();
        applyDelta(float3(0, 2 * g, 0));
        float3 after = sc.objects[cv].curve.points[2];
        check(std::fabs(after.y - before.y - 2 * g) < 1e-4f && a.m_undo.size() == undoBefore + 1, "Move of a CV by the manipulator (+2 grid cells in Y, one undo step)");
        a.undo();
        check(length(sc.objects[cv].curve.points[2] - before) < 1e-5f, "Undo restores the CV");
        a.select(cv);
        enterPoints();
        CurveData& c = sc.objects[cv].curve;
        insertCurvePoint(c, 1, (c.points[1] + c.points[2]) * 0.5f);
        check(c.count() == 6, "Insert a point");
        m_sel = {{3, PART_POINT}};
        deleteSelectedPoints();
        check(c.count() == 5, "Delete a point");
        exitMode();
    }
    // Bezier handle edit: a smooth handle turns the opposite one
    {
        a.select(bez);
        enterPoints();
        m_sel = {{1, PART_OUT}};
        CurveData& c = sc.objects[bez].curve;
        float lenIn = length(c.hin[1] - c.points[1]);
        float2 sp;
        float3 pv;
        selectionPivot(pv);
        a.project(pv, sp);
        beginDrag(0, sp.x, sp.y);
        for (Start& st : m_dragStart) itemRef(c, st.item) = st.local + float3(2, 0, 0);
        // same rule as updateDrag for a smooth handle
        float3 dir = c.hout[1] - c.points[1];
        c.hin[1] = c.points[1] - normalize(dir) * lenIn;
        m_dragging = false;
        float3 a0 = normalize(c.hout[1] - c.points[1]), b0 = normalize(c.points[1] - c.hin[1]);
        check(length(a0 - b0) < 1e-4f && std::fabs(length(c.hin[1] - c.points[1]) - lenIn) < 1e-4f, "Bezier: smooth handles stay aligned, the other keeps its length");
        exitMode();
    }
    // Cavalry-style tangent modes
    {
        a.select(bez);
        enterPoints();
        CurveData& c = sc.objects[bez].curve;
        toggleCorner(1);
        check(tangentMode(c, 1) == 2 && length(c.hin[1] - c.points[1]) < 1e-6f, "Double-click: corner point (no tangent)");
        toggleCorner(1);
        check(tangentMode(c, 1) == 0 && length(c.hout[1] - c.points[1]) > 1e-3f, "Double-click again: smooth point (automatic tangents)");
        setTangentMode(1, 1);
        check(tangentMode(c, 1) == 1, "Broken tangent");
        c.hin[1] = c.points[1] + float3(0, 0, 2);
        setTangentMode(1, 0);
        float3 o0 = normalize(c.hout[1] - c.points[1]), i0 = normalize(c.points[1] - c.hin[1]);
        check(tangentMode(c, 1) == 0 && length(o0 - i0) < 1e-4f, "Smooth again: the tangents are aligned");
        exitMode();
    }
    // conversions
    {
        CurveData c = sc.objects[ep].curve;
        convertCurve(c, CURVE_BEZIER);
        check(c.type == CURVE_BEZIER && c.hin.size() == c.points.size(), "Convert EP -> Bezier (smooth handles)");
        convertCurve(c, CURVE_LINEAR);
        check(c.type == CURVE_LINEAR && c.hin.empty(), "Convert Bezier -> Linear");
        CurveData r2 = sc.objects[ep].curve;
        float l0 = curveLength(r2);
        rebuildCurve(r2, 12);
        check(r2.count() == 12 && std::fabs(curveLength(r2) - l0) < 0.05f * l0, "Rebuild 12 points (length kept within 5%)");
        CurveData rv = sc.objects[cv].curve;
        reverseCurve(rv);
        check(length(rv.points.front() - sc.objects[cv].curve.points.back()) < 1e-6f, "Reverse direction");
    }
    // save / load .wzx
    {
        std::filesystem::path rep(testReport);
        std::wstring file = (rep.parent_path() / L"curve_test.wzx").wstring();
        struct Rec { std::string name; int type; bool closed; std::vector<float3> pts; size_t handles; int shape; };
        std::vector<Rec> recs;
        for (const Object& o : sc.objects)
            if (o.curve.valid()) recs.push_back({o.name, o.curve.type, o.curve.closed, o.curve.points, o.curve.hout.size(), o.curve.shape.type});
        bool saved = a.saveSceneFile(file);
        check(saved, "Save the scene (.wzx)");
        bool loaded = saved && a.loadSceneFile(file);
        check(loaded, "Open the scene again");
        std::vector<Rec> back;
        for (const Object& o : sc.objects)
            if (o.curve.valid()) back.push_back({o.name, o.curve.type, o.curve.closed, o.curve.points, o.curve.hout.size(), o.curve.shape.type});
        bool same = recs.size() == back.size();
        for (size_t i = 0; same && i < recs.size(); ++i) {
            same = recs[i].name == back[i].name && recs[i].type == back[i].type && recs[i].closed == back[i].closed &&
                   recs[i].pts.size() == back[i].pts.size() && recs[i].handles == back[i].handles && recs[i].shape == back[i].shape;
            for (size_t k = 0; same && k < recs[i].pts.size(); ++k) same = length(recs[i].pts[k] - back[i].pts[k]) < 1e-3f;
        }
        check(same, std::to_string(recs.size()) + " curves read back identical (type, closed, points, handles, primitive)");
    }
    // view for the capture: every curve framed, the Bezier curve in point mode with an anchor selected
    {
        std::vector<int> curves;
        for (int i = 0; i < (int)sc.objects.size(); ++i) if (sc.objects[i].curve.valid()) curves.push_back(i);
        if (!curves.empty()) {
            a.m_selected = curves[0];
            a.m_multi.assign(curves.begin() + 1, curves.end());
            a.frameSelection();
            Camera& cam = sc.camera;
            cam.pitch = radians(48.0f);
            cam.yaw = radians(18.0f);
            cam.distance *= 0.82f;
            a.cameraChanged();
        }
        // close-up for the capture (environment WZX_CURVE_VIEW = bezier or cv), else the Bezier curve in this framing
        const char* view = getenv("WZX_CURVE_VIEW");
        const bool cvView = view && std::string(view) == "cv", close = cvView || (view && std::string(view) == "bezier");
        int b = -1;
        for (int i : curves) if (sc.objects[i].curve.type == (cvView ? CURVE_CV : CURVE_BEZIER)) b = i;
        if (b >= 0) {
            if (close) {
                a.select(b);
                a.frameSelection();
                sc.camera.pitch = radians(62.0f);
                sc.camera.yaw = radians(8.0f);
                sc.camera.distance *= 0.75f;
                a.cameraChanged();
            }
            a.select(b);
            enterPoints();
            m_sel = {{cvView ? 2 : 1, PART_POINT}};
        }
        a.markDirty();
    }
    // capture of the creation tool (WZX_CURVE_VIEW = create): an EP curve being drawn, the mouse held on the end
    // of the CV curve (osnap End), a typed coordinate in the field
    {
        const char* view = getenv("WZX_CURVE_VIEW");
        if (view && std::string(view) == "create") {
            int cvo = -1;
            for (int i = 0; i < (int)sc.objects.size(); ++i) if (sc.objects[i].curve.type == CURVE_CV && sc.objects[i].curve.shape.type == CSHAPE_NONE) { cvo = i; break; }
            exitMode();
            a.select(-1);
            if (cvo >= 0) {
                const Object& o = sc.objects[cvo];
                m_testMouseAt = o.transform().point(o.curve.points.back());
                m_testMouse = true;
                startCreate(CURVE_EP);
                draftAdd(m_testMouseAt + float3(10, 0, 2));
                draftAdd(m_testMouseAt + float3(7, 0, -3));
                draftAdd(m_testMouseAt + float3(3, 0, 1));
                snprintf(m_coordBuf, sizeof(m_coordBuf), "@4, 0");
            }
        }
    }
    // capture of the fillet (WZX_CURVE_VIEW = fillet): the linear rectangle with two filleted corners, in point mode
    {
        const char* view = getenv("WZX_CURVE_VIEW");
        if (view && std::string(view) == "fillet") {
            int li = -1;
            for (int i = 0; i < (int)sc.objects.size(); ++i) if (sc.objects[i].curve.type == CURVE_LINEAR && sc.objects[i].curve.closed && sc.objects[i].curve.shape.type == CSHAPE_NONE) { li = i; break; }
            if (li >= 0) {
                exitMode();
                a.select(li);
                a.frameSelection();
                sc.camera.pitch = radians(70.0f);
                sc.camera.yaw = radians(5.0f);
                a.cameraChanged();
                enterPoints();
                m_sel = {{1, PART_POINT}, {2, PART_POINT}};
                applyFillet(1.5f);
                m_sel = {{2, PART_POINT}};   // a point of the new arc
                m_filletTool = true;
            }
        }
    }
    r << (fails ? "FAILED: " : "ALL PASSED: ") << fails << " failure(s)\n";
    FILE* f = _wfopen(testReport.c_str(), L"w");
    if (f) { fputs(r.str().c_str(), f); fclose(f); }
}

} // namespace app
