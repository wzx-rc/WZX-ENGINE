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
const ImU32 kCurve = IM_COL32(86, 152, 255, 255);        // unselected curve (Maya: dark blue, lighter here)
const ImU32 kCurveSel = IM_COL32(98, 240, 150, 255);     // selected curve (Maya: light green)
const ImU32 kCurveMulti = IM_COL32(236, 238, 242, 255);  // other curves of a multi-selection (Maya: white)
const ImU32 kCv = IM_COL32(222, 86, 226, 255);           // control vertices (Maya: magenta)
const ImU32 kEp = IM_COL32(110, 190, 255, 255);          // edit points
const ImU32 kHandle = IM_COL32(176, 180, 192, 255);      // Bezier handles
const ImU32 kPicked = IM_COL32(255, 214, 10, 255);       // selected points (Maya: yellow)
const ImU32 kDraft = IM_COL32(255, 196, 64, 255);        // curve being created

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
        if (c.type == CURVE_BEZIER && i < (int)c.hin.size()) {
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
    m_dragging = m_marquee = m_pendingFree = false;
    a.status(L"Edit Points: click or drag a rectangle to select, drag to move");
    a.markDirty();
}

void CurveEditor::exitMode() {
    if (m_mode == MODE_CREATE) { m_draft = CurveData(); m_draftDrag = false; }
    if (m_dragging && !m_dragMoved && !a.m_undo.empty()) a.m_undo.pop_back();
    m_mode = MODE_NONE;
    m_obj = -1;
    m_sel.clear();
    m_dragging = m_marquee = m_pendingFree = false;
    m_hoverAxis = -1;
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
            if (!m_sel.empty()) m_sel.clear();
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
    if (placing) {
        if (m_mode != MODE_NONE) exitMode();
        return false;
    }
    const bool alt = io.KeyAlt, shift = io.KeyShift, clicked = inView && ImGui::IsMouseClicked(0) && !alt;
    const bool snapOn = ImGui::IsKeyDown(ImGuiKey_X);

    if (m_mode == MODE_CREATE) {
        if (clicked) {
            float3 p;
            if (groundPoint(mx, my, 0.0f, p)) {
                if (snapOn) p = snap(p);
                bool closeIt = false;
                if (m_draft.count() >= 3) {   // click on the first point: closes the curve
                    float2 s0;
                    if (a.project(m_draft.points[0], s0) && std::hypot(s0.x - mx, s0.y - my) < 9.0f * a.m_dpi) closeIt = true;
                }
                if (closeIt) { m_draft.closed = true; finishCreate(); return true; }
                if (m_draft.points.empty() || length(p - m_draft.points.back()) > 1e-4f) {
                    m_draft.points.push_back(p);
                    if (m_createType == CURVE_BEZIER) {
                        m_draft.hin.push_back(p);
                        m_draft.hout.push_back(p);
                        m_draft.broken.push_back(0);
                    }
                }
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

    if (m_mode == MODE_POINTS) {
        if (m_dragging) {
            if (!io.MouseDown[0]) {
                m_dragging = false;
                if (!m_dragMoved && !a.m_undo.empty()) a.m_undo.pop_back();
            } else updateDrag(mx, my);
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
            if (m_hoverAxis >= 0) { beginDrag(m_hoverAxis, mx, my); return true; }
            if (io.KeyCtrl) { insertPointAt(mx, my); return true; }
            Item it;
            if (pickPoint(mx, my, it)) {
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
    if (std::fabs(mx - pc.x) < 7 * a.m_dpi && std::fabs(my - pc.y) < 7 * a.m_dpi) return 3;
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
        if (ImGui::MenuItem(curveShapeName(sh))) addPrimitive(sh);
    ImGui::Separator();
    const bool curveSel = a.m_selected >= 0 && a.m_scene.objects[a.m_selected].curve.valid();
    if (ImGui::MenuItem("Edit Points", "F8", m_mode == MODE_POINTS, curveSel)) { if (m_mode == MODE_POINTS) exitMode(); else enterPoints(); }
    ImGui::EndMenu();
}

// --------------------------------------------------------------------------- drawing

void CurveEditor::drawCurve(ImDrawList* dl, const Object& o, ImU32 col, float width) const {
    std::vector<float3> s;
    sampleCurve(o.curve, s);
    Affine xf = o.transform();
    std::vector<ImVec2> run;
    auto flush = [&]() {
        if (run.size() >= 2) dl->AddPolyline(run.data(), (int)run.size(), col, 0, width);
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
    // CV hull
    if (c.type == CURVE_CV)
        for (int i = 0; i + 1 < c.count() + (c.closed ? 1 : 0); ++i) {
            float2 p0, p1;
            if (P(c.points[i], p0) && P(c.points[(i + 1) % c.count()], p1)) dl->AddLine(iv(p0), iv(p1), (kCv & 0x00FFFFFF) | 0x70000000, 1.0f * d);
        }
    // Bezier handles
    if (c.type == CURVE_BEZIER && (int)c.hin.size() == c.count())
        for (int i = 0; i < c.count(); ++i) {
            float2 pa;
            if (!P(c.points[i], pa)) continue;
            for (int part : {(int)PART_IN, (int)PART_OUT}) {
                const float3& h = part == PART_IN ? c.hin[i] : c.hout[i];
                if (!c.closed && ((i == 0 && part == PART_IN) || (i == c.count() - 1 && part == PART_OUT)) && length(h - c.points[i]) < 1e-6f) continue;
                float2 ph;
                if (!P(h, ph)) continue;
                dl->AddLine(iv(pa), iv(ph), kHandle, 1.0f * d);
                ImU32 hc = isSelected({i, part}) ? kPicked : kHandle;
                dl->AddCircleFilled(iv(ph), 3.6f * d, hc, 12);
                dl->AddCircle(iv(ph), 3.6f * d, IM_COL32(20, 20, 24, 200), 12, 1.0f);
            }
        }
    for (int i = 0; i < c.count(); ++i) {
        float2 p;
        if (!P(c.points[i], p)) continue;
        const bool sel = isSelected({i, PART_POINT});
        const float r = (sel ? 4.2f : 3.4f) * d;
        ImU32 col = sel ? kPicked : (c.type == CURVE_EP ? kEp : kCv);
        if (c.type == CURVE_EP && !sel) {   // edit points: crosses
            dl->AddLine(ImVec2(p.x - r, p.y - r), ImVec2(p.x + r, p.y + r), col, 1.8f * d);
            dl->AddLine(ImVec2(p.x - r, p.y + r), ImVec2(p.x + r, p.y - r), col, 1.8f * d);
        } else {
            dl->AddRectFilled(ImVec2(p.x - r, p.y - r), ImVec2(p.x + r, p.y + r), col);
            dl->AddRect(ImVec2(p.x - r, p.y - r), ImVec2(p.x + r, p.y + r), IM_COL32(20, 20, 24, 200), 0, 0, 1.0f);
        }
        if (i == 0) {   // first point: hollow box around it (curve direction)
            float R = r + 3.0f * d;
            dl->AddRect(ImVec2(p.x - R, p.y - R), ImVec2(p.x + R, p.y + R), col, 0, 0, 1.4f * d);
        }
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
        float2 pb, pt;
        if (!a.project(pv + axisVec(k) * (0.85f * s), pb) || !a.project(pv + axisVec(k) * (1.1f * s), pt)) continue;
        dl->AddLine(iv(pc), iv(pb), c, (lit ? 3.0f : 2.2f) * d);
        float dx = pt.x - pb.x, dy = pt.y - pb.y, l = std::sqrt(dx * dx + dy * dy);
        if (l < 1e-3f) continue;
        dx /= l;
        dy /= l;
        float hw = (lit ? 6.0f : 5.0f) * d;
        dl->AddTriangleFilled(ImVec2(pt.x, pt.y), ImVec2(pb.x - dy * hw, pb.y + dx * hw), ImVec2(pb.x + dy * hw, pb.y - dx * hw), c);
    }
    bool lit = m_dragging ? m_dragAxis == 3 : m_hoverAxis == 3;
    float hs = 5.5f * d;
    dl->AddRectFilled(ImVec2(pc.x - hs, pc.y - hs), ImVec2(pc.x + hs, pc.y + hs), lit ? IM_COL32(255, 230, 120, 120) : IM_COL32(255, 214, 10, 60));
    dl->AddRect(ImVec2(pc.x - hs, pc.y - hs), ImVec2(pc.x + hs, pc.y + hs), lit ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 214, 10, 230), 0, 0, 1.5f * d);
}

void CurveEditor::drawDraft(ImDrawList* dl) {
    ImGuiIO& io = ImGui::GetIO();
    const float d = a.m_dpi;
    CurveData c = m_draft;
    const float mx = io.MousePos.x, my = io.MousePos.y;
    bool hoverFirst = false;
    // preview of the next point under the mouse
    if (!m_draftDrag && a.m_rcView.contains(mx, my)) {
        float3 p;
        if (groundPoint(mx, my, 0.0f, p)) {
            if (ImGui::IsKeyDown(ImGuiKey_X)) p = snap(p);
            float2 s0;
            if (c.count() >= 3 && a.project(c.points[0], s0) && std::hypot(s0.x - mx, s0.y - my) < 9.0f * d) {
                c.closed = true;
                hoverFirst = true;
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
        if (run.size() >= 2) dl->AddPolyline(run.data(), (int)run.size(), kDraft, 0, 2.0f * d);
    }
    for (int i = 0; i < m_draft.count(); ++i) {
        float2 p;
        if (!a.project(m_draft.points[i], p)) continue;
        if (m_createType == CURVE_BEZIER && i < (int)m_draft.hout.size() && length(m_draft.hout[i] - m_draft.points[i]) > 1e-5f) {
            float2 h0, h1;
            if (a.project(m_draft.hin[i], h0) && a.project(m_draft.hout[i], h1)) {
                dl->AddLine(iv(h0), iv(h1), kHandle, 1.0f * d);
                dl->AddCircleFilled(iv(h0), 3.2f * d, kHandle, 12);
                dl->AddCircleFilled(iv(h1), 3.2f * d, kHandle, 12);
            }
        }
        float r = 3.4f * d;
        dl->AddRectFilled(ImVec2(p.x - r, p.y - r), ImVec2(p.x + r, p.y + r), m_createType == CURVE_EP ? kEp : kCv);
        if (i == 0 && hoverFirst) dl->AddCircle(iv(p), 9.0f * d, kPicked, 20, 2.0f * d);
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
        ImU32 col = primary ? kCurveSel : multi ? kCurveMulti : kCurve;
        if (m_mode == MODE_POINTS && i == m_obj) col = IM_COL32(150, 214, 255, 255);
        drawCurve(dl, o, col, (primary || multi ? 2.6f : 2.0f) * d);
    }
    std::string hint;
    if (m_mode == MODE_POINTS && m_obj >= 0) {
        drawPoints(dl, objs[m_obj]);
        drawManipulator(dl);
        if (m_marquee) {
            ImVec2 m = ImGui::GetIO().MousePos;
            ImVec2 p0(std::min(m_marq0.x, m.x), std::min(m_marq0.y, m.y)), p1(std::max(m_marq0.x, m.x), std::max(m_marq0.y, m.y));
            dl->AddRectFilled(p0, p1, IM_COL32(255, 214, 10, 28));
            dl->AddRect(p0, p1, IM_COL32(255, 214, 10, 200), 0, 0, 1.0f * d);
        }
        hint = "Edit Points  ·  " + std::to_string(m_sel.size()) + " selected  ·  X: grid snap  ·  Ctrl+click: insert  ·  Del: delete  ·  F8: exit";
    } else if (m_mode == MODE_CREATE) {
        drawDraft(dl);
        hint = std::string(curveTypeName(m_createType)) + " Tool  ·  " + std::to_string(m_draft.count()) + " points  ·  X: grid snap  ·  Enter: finish  ·  Backspace: undo point  ·  Esc: cancel";
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
        int b = -1;
        for (int i : curves) if (sc.objects[i].curve.type == CURVE_BEZIER) b = i;
        if (b >= 0) {
            a.select(b);
            enterPoints();
            m_sel = {{1, PART_POINT}};
        }
        a.markDirty();
    }
    r << (fails ? "FAILED: " : "ALL PASSED: ") << fails << " failure(s)\n";
    FILE* f = _wfopen(testReport.c_str(), L"w");
    if (f) { fputs(r.str().c_str(), f); fclose(f); }
}

} // namespace app
