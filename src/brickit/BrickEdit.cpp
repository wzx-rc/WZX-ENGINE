// BrickIt - viewport editing: the brick in hand (typed snap), selection, duplicate, rotate, colours, undo, files.
#include "BrickApp.h"
#include <algorithm>
#include <commdlg.h>
#include <cstdio>

namespace brickit {

View BrickApp::view() const {
    View v;
    v.cam = m_scene.camera;
    v.ox = m_stage.x;
    v.oy = m_stage.y;
    v.w = std::max(1.0f, m_stage.z - m_stage.x);
    v.h = std::max(1.0f, m_stage.w - m_stage.y);
    return v;
}

// Ray picking for tens of thousands of bricks: world boxes cached per scene revision, slab test on every box (about
// 0.3 ms at 50k), then the triangles of the boxes hit, nearest box first, stopping once a box starts beyond the best hit.
static bool rayTriangle(const float3& o, const float3& d, const float3& a, const float3& b, const float3& c, float& t) {
    float3 e1 = b - a, e2 = c - a, p = cross(d, e2);
    float det = dot(e1, p);
    if (std::fabs(det) < 1e-12f) return false;
    float inv = 1.0f / det;
    float3 s = o - a;
    float u = dot(s, p) * inv;
    if (u < 0 || u > 1) return false;
    float3 q = cross(s, e1);
    float v = dot(d, q) * inv;
    if (v < 0 || u + v > 1) return false;
    float tt = dot(e2, q) * inv;
    if (tt <= 1e-5f || tt >= t) return false;
    t = tt;
    return true;
}

int BrickApp::pickBrick(float sx, float sy, float3* point, int exclude) {
    float3 o, d;
    view().ray(sx, sy, o, d);
    const int n = (int)m_bricks.size();
    if (m_pickVersion != m_scene.objectsVersion || (int)m_pickBoxes.size() != n) {
        m_pickVersion = m_scene.objectsVersion;
        m_pickBoxes.resize(n);
        for (int i = 0; i < n; ++i) {
            const Object& ob = m_scene.objects[m_firstBrickObject + i];
            const Part& p = m_lib.parts[m_bricks[i].part];
            Affine xf = ob.transform();
            Box bx{float3(1e30f), float3(-1e30f)};
            for (int c = 0; c < 8; ++c) {
                float3 w = xf.point(float3((c & 1) ? p.bmax.x : p.bmin.x, (c & 2) ? p.bmax.y : p.bmin.y, (c & 4) ? p.bmax.z : p.bmin.z));
                bx.mn = minv(bx.mn, w);
                bx.mx = maxv(bx.mx, w);
            }
            m_pickBoxes[i] = bx;
        }
    }
    const float3 inv(1.0f / (std::fabs(d.x) > 1e-12f ? d.x : 1e-12f), 1.0f / (std::fabs(d.y) > 1e-12f ? d.y : 1e-12f), 1.0f / (std::fabs(d.z) > 1e-12f ? d.z : 1e-12f));
    std::vector<std::pair<float, int>> hits;
    for (int i = 0; i < n; ++i) {
        if (i == exclude || !m_scene.objects[m_firstBrickObject + i].visible) continue;
        const Box& b = m_pickBoxes[i];
        float t0 = 0, t1 = 1e30f;
        for (int k = 0; k < 3; ++k) {
            float a = ((&b.mn.x)[k] - (&o.x)[k]) * (&inv.x)[k], c = ((&b.mx.x)[k] - (&o.x)[k]) * (&inv.x)[k];
            if (a > c) std::swap(a, c);
            t0 = std::max(t0, a);
            t1 = std::min(t1, c);
        }
        if (t0 <= t1) hits.push_back({t0, i});
    }
    std::sort(hits.begin(), hits.end());
    float best = 1e30f;
    int bestI = -1;
    for (auto& h : hits) {
        if (h.first > best) break;
        const int i = h.second;
        const Part& p = m_lib.parts[m_bricks[i].part];
        Affine xi = m_scene.objects[m_firstBrickObject + i].transform().inverse();
        float3 lo = xi.point(o), ld = xi.vector(d);   // t stays in world units (rigid transforms)
        const std::vector<Vertex>& V = p.mesh.vertices;
        const std::vector<uint32_t>& I = p.mesh.indices;
        for (size_t t = 0; t + 2 < I.size(); t += 3)
            if (rayTriangle(lo, ld, V[I[t]].pos, V[I[t + 1]].pos, V[I[t + 2]].pos, best)) bestI = i;
    }
    if (bestI >= 0 && point) *point = o + d * best;
    return bestI;
}

// --------------------------------------------------------------------------- brick in hand

void BrickApp::spawn(int part, float sx, float sy) {
    if (part < 0 || !m_lib.get(m_lib.parts[part].name)) return;
    if (m_holding) cancelHeld();
    pushUndo();
    Brick b;
    b.part = part;
    b.color = m_color;
    b.pos = m_scene.camera.target;
    m_bricks.push_back(b);
    rebuildScene();
    m_build.beginHold(m_held, (int)m_bricks.size() - 1, true);
    m_holding = true;
    m_selected = (int)m_bricks.size() - 1;
    m_multi.clear();
    noteRecent(part);
    syncObjects();   // focus mode: the rest dims
    updateHeld(sx, sy);
    m_held.vpos = m_held.target + float3(0, 1.2f, 0);
    m_held.vrot = m_held.rot;
    m_held.squash = 0.55f;
    toast("Brique en main : clic pour poser, R tourner, T basculer, clic droit annuler");
}

void BrickApp::beginMove(int brick, float sx, float sy) {
    if (m_holding) cancelHeld();
    pushUndo();
    m_build.beginHold(m_held, brick, false);
    // Auto Pivot: the voxel of the brick nearest to where it was grabbed becomes its pivot
    const Part& p = m_lib.parts[m_bricks[brick].part];
    float best = m_autoPivot ? 1e30f : -1.0f;
    for (size_t v = 0; v < p.voxels.size(); ++v) {
        float d = length(m_build.worldVoxel(m_bricks[brick], p.voxels[v]) - m_pressPoint);
        if (d < best) { best = d; m_held.grab = p.voxels[v]; m_bricks[brick].grab = (int)v; }
    }
    m_holding = true;
    m_selected = brick;
    m_multi.clear();
    m_held.vpos = m_bricks[brick].pos;
    m_held.vrot = m_bricks[brick].rot;
    m_held.vvel = float3(0, 8, 0);   // a placed brick jumps into the hand
    m_held.squash = 1.12f;
    syncObjects();
    updateHeld(sx, sy);
}

void BrickApp::updateHeld(float sx, float sy) {
    if (!m_holding) return;
    float3 hit;
    const bool over = pickBrick(sx, sy, &hit, m_held.brick) >= 0;
    const bool wasBlocked = m_held.blocked;
    m_build.computeTarget(m_held, view(), sx, sy, over ? &hit : nullptr);
    Brick& b = m_bricks[m_held.brick];
    b.pos = m_held.target;
    b.rot = m_held.rot;
    if (m_held.snapVoxel.x != m_held.lastSnap.x || m_held.snapVoxel.y != m_held.lastSnap.y || m_held.snapVoxel.z != m_held.lastSnap.z) {
        m_held.snapT = 0;   // new voxel: replay the highlight
        m_held.lastSnap = m_held.snapVoxel;
    }
    if (wasBlocked != m_held.blocked) syncObjects();
    m_lastMove = ImGui::GetTime();
}

void BrickApp::dropHeld(bool keepCopy) {
    if (!m_holding) return;
    if (m_held.blocked) {
        toast("Placement impossible : plus de 50 % de la brique est dans une autre");
        return;
    }
    if (m_held.dropping) return;
    m_held.dropping = true;   // drop animation (0.15 s), finishDrop at its end
    m_held.dropT = 0;
    m_held.dropFrom = m_held.vpos;
    m_held.dropRotFrom = m_held.vrot;
    m_held.keepCopy = keepCopy;
    if (!m_animOn) finishDrop();
}

void BrickApp::cancelHeld() {
    if (!m_holding) return;
    m_holding = false;
    if (m_held.isNew) {
        m_bricks.erase(m_bricks.begin() + m_held.brick);
        m_selected = -1;
        rebuildScene();
    } else {
        m_bricks[m_held.brick] = m_held.original;
        syncObjects();
    }
    if (!m_undo.empty()) m_undo.pop_back();
}

// --------------------------------------------------------------------------- selection and edits

std::vector<int> BrickApp::selection() const {
    std::vector<int> s;
    if (m_selected >= 0) s.push_back(m_selected);
    for (int i : m_multi)
        if (i != m_selected) s.push_back(i);
    return s;
}

void BrickApp::select(int brick, bool add) {
    if (add && brick >= 0) {
        if (m_selected < 0) m_selected = brick;
        else if (brick == m_selected) { m_selected = m_multi.empty() ? -1 : m_multi.back(); if (!m_multi.empty()) m_multi.pop_back(); }
        else {
            auto it = std::find(m_multi.begin(), m_multi.end(), brick);
            if (it != m_multi.end()) m_multi.erase(it);
            else m_multi.push_back(brick);
        }
    } else {
        m_selected = brick;
        m_multi.clear();
    }
    syncObjects();
}

void BrickApp::deleteSelected() {
    std::vector<int> s = selection();
    if (s.empty()) return;
    pushUndo();
    std::sort(s.begin(), s.end(), std::greater<int>());
    for (int i : s) m_bricks.erase(m_bricks.begin() + i);
    m_selected = -1;
    m_multi.clear();
    rebuildScene();
}

// Ctrl+D: a copy stacked right above (first free place), selected and in place; the original stays.
void BrickApp::duplicateSelected() {
    if (m_holding) return;
    std::vector<int> s = selection();
    if (s.empty()) return;
    pushUndo();
    int last = -1;
    for (int i : s) {
        int c = m_build.stackedCopy(i);
        if (c >= 0) last = c;
    }
    if (last < 0) { m_undo.pop_back(); toast("Pas de place libre au-dessus de cette brique"); return; }
    m_selected = last;
    m_multi.clear();
    rebuildScene();
}

void BrickApp::rotateSelected(int dir) {
    if (m_holding) {
        m_build.rotateHeld(m_held, float3(0, 1, 0), dir);
        ImVec2 mp = ImGui::GetIO().MousePos;
        updateHeld(mp.x, mp.y);
        return;
    }
    std::vector<int> s = selection();
    if (s.empty()) return;
    pushUndo();
    for (int i : s) m_build.rotatePlaced(i, dir);
    syncObjects();
}

// Radial menu: tilt towards the viewer (around the camera right axis snapped to world X or Z), about the pivot.
void BrickApp::tiltSelected(int dir) {
    float3 r, u, f;
    m_scene.camera.basis(r, u, f);
    float3 axis = std::fabs(r.x) > std::fabs(r.z) ? float3(r.x > 0 ? 1.0f : -1.0f, 0, 0) : float3(0, 0, r.z > 0 ? 1.0f : -1.0f);
    if (m_holding) {
        m_build.rotateHeld(m_held, axis, -dir);
        ImVec2 mp = ImGui::GetIO().MousePos;
        updateHeld(mp.x, mp.y);
        syncObjects();
        return;
    }
    std::vector<int> sel = selection();
    if (sel.empty()) return;
    pushUndo();
    for (int i : sel) m_build.rotatePlacedAxis(i, axis, -dir);
    syncObjects();
}

// Voxels shown as dots: studs and anti-studs yellow, pins and pin holes cyan, axles and axle holes magenta.
void BrickApp::drawVoxels() {
    if (!m_showVox) return;
    static const ImU32 col[3] = {IM_COL32(0xff, 0xc2, 0x1a, 230), IM_COL32(0x38, 0xd0, 0xe8, 230), IM_COL32(0xe0, 0x5a, 0xd7, 230)};
    View v = view();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float r = 2.5f * m_ui;
    int drawn = 0;
    const int fb = focusBrick();   // as the web: the voxels of the selected / held brick only
    for (size_t i = 0; i < m_bricks.size() && drawn < 20000; ++i) {
        if ((int)i != fb) continue;
        const Object& o = m_scene.objects[m_firstBrickObject + i];
        Affine xf = o.transform();
        const Part& p = m_lib.parts[m_bricks[i].part];
        for (size_t k = 0; k < p.voxels.size(); ++k) {
            float sx, sy;
            if (!v.project(xf.point(p.voxels[k]), sx, sy)) continue;
            uint8_t c = p.conn[k];
            int t = (c == CONN_PIN || c == CONN_PINHOLE) ? 1 : (c == CONN_AXLE || c == CONN_AXLEHOLE) ? 2 : 0;
            dl->AddCircleFilled(ImVec2(sx, sy), r, col[t], 8);
            ++drawn;
        }
    }
}

void BrickApp::applyColor(int color) {
    m_color = color;
    std::vector<int> s = selection();
    if (m_holding) s.push_back(m_held.brick);
    if (s.empty()) return;
    if (!m_holding) pushUndo();
    for (int i : s) m_bricks[i].color = color;
    syncObjects();
}

void BrickApp::pushUndo() {
    m_undo.push_back(m_bricks);
    if (m_undo.size() > 100) m_undo.erase(m_undo.begin());
    m_redo.clear();
}

void BrickApp::restore(const std::vector<Brick>& snap) {
    m_bricks = snap;
    m_selected = -1;
    m_multi.clear();
    rebuildScene();
}

void BrickApp::undo() {
    if (m_holding) { cancelHeld(); return; }
    if (m_undo.empty()) return;
    m_redo.push_back(m_bricks);
    restore(m_undo.back());
    m_undo.pop_back();
}

void BrickApp::redo() {
    if (m_holding || m_redo.empty()) return;
    m_undo.push_back(m_bricks);
    restore(m_redo.back());
    m_redo.pop_back();
}

void BrickApp::noteRecent(int part) {
    m_recent.erase(std::remove(m_recent.begin(), m_recent.end(), part), m_recent.end());
    m_recent.insert(m_recent.begin(), part);
    if (m_recent.size() > 24) m_recent.resize(24);
}

// --------------------------------------------------------------------------- files

bool BrickApp::saveScene(const std::wstring& path) {
    std::string text = m_build.toJson(m_scene.camera);
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) { toast("Impossible d'écrire le fichier"); return false; }
    fwrite(text.data(), 1, text.size(), f);
    fclose(f);
    m_scenePath = path;
    toast("Scène enregistrée");
    return true;
}

bool BrickApp::openScene(const std::wstring& path) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) { toast("Fichier introuvable"); return false; }
    std::string text;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    fclose(f);
    std::vector<Brick> before = m_bricks;
    std::string msg;
    if (!m_build.fromJson(text, &m_scene.camera, &msg)) { toast(msg); return false; }
    m_undo.push_back(before);
    m_redo.clear();
    m_selected = -1;
    m_multi.clear();
    m_scenePath = path;
    rebuildScene();
    toast(msg);
    return true;
}

static std::wstring fileDialog(HWND hwnd, bool save, const std::wstring& current) {
    wchar_t file[MAX_PATH] = L"";
    if (!current.empty()) wcsncpy_s(file, current.c_str(), _TRUNCATE);
    OPENFILENAMEW ofn = {sizeof(ofn)};
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"Scène BrickIt (*.v3d.json)\0*.v3d.json;*.json\0Tous les fichiers\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"v3d.json";
    ofn.Flags = save ? (OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST) : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST);
    BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    return ok ? std::wstring(file) : std::wstring();
}

void BrickApp::saveDialog() {
    std::wstring p = fileDialog(m_hwnd, true, m_scenePath);
    if (!p.empty()) saveScene(p);
}

void BrickApp::openDialog() {
    std::wstring p = fileDialog(m_hwnd, false, m_scenePath);
    if (!p.empty()) openScene(p);
}

// --------------------------------------------------------------------------- input

void BrickApp::editInput() {
    ImGuiIO& io = ImGui::GetIO();
    const bool keys = !io.WantTextInput;
    if (keys) {
        const bool ctrl = io.KeyCtrl;
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z)) undo();
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y)) redo();
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D)) duplicateSelected();
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S)) saveDialog();
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_O)) openDialog();
        // keys of the web: R or E = turn +90, Q = -90, C = pivot voxel, V = voxels, P = pivot edit, A = animations,
        // F = focus mode, Space = take the selected brick in hand
        if (!ctrl && (ImGui::IsKeyPressed(ImGuiKey_R) || ImGui::IsKeyPressed(ImGuiKey_E))) rotateSelected(1);
        if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_Q)) rotateSelected(-1);
        if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_C)) cycleGrab();
        if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_V)) { m_showVox = !m_showVox; syncObjects(); toast(m_showVox ? "Voxels : affichés (tenons jaunes, pins cyan, axles magenta)" : "Voxels : masqués"); }
        if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_P)) m_pivotEdit = !m_pivotEdit;
        if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_A)) m_animOn = !m_animOn;
        if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_F)) { m_focusDim = !m_focusDim; syncObjects(); toast(m_focusDim ? "Mode focus : les autres briques à 50 %" : "Mode focus désactivé"); }
        if (ImGui::IsKeyPressed(ImGuiKey_Space)) pickUpSelected();
        if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_T) && m_holding) {   // tilt towards the viewer
            float3 r, u, f;
            m_scene.camera.basis(r, u, f);
            float3 axis = std::fabs(r.x) > std::fabs(r.z) ? float3(r.x > 0 ? 1.0f : -1.0f, 0, 0) : float3(0, 0, r.z > 0 ? 1.0f : -1.0f);
            m_build.rotateHeld(m_held, axis, io.KeyShift ? 1 : -1);
            updateHeld(io.MousePos.x, io.MousePos.y);
            syncObjects();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
            if (m_holding) { bool isNew = m_held.isNew; cancelHeld(); if (!isNew) deleteSelected(); }
            else deleteSelected();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            if (m_holding) cancelHeld();
            else select(-1);
        }
        if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_G)) { m_build.gridSnap = !m_build.gridSnap; toast(m_build.gridSnap ? "Grille : oui" : "Grille : non"); }
    }

    const ImVec2 mp = io.MousePos;
    const bool inView = !io.WantCaptureMouse && !m_overUi;
    // the brick in hand follows the cursor
    // pivot edit: the cursor aims a voxel of the focus brick (no exact click needed); a click does not drop
    if (m_pivotEdit && focusBrick() >= 0 && inView) {
        if (io.MouseDelta.x != 0 || io.MouseDelta.y != 0 || ImGui::IsMouseClicked(ImGuiMouseButton_Left)) aimPivot(mp.x, mp.y);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Left)) return;
    }
    if (m_holding && inView && m_opt.spawnPart.empty() && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) updateHeld(mp.x, mp.y);

    // right button: a click without moving cancels the brick in hand (a drag orbits the camera)
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) { m_rmbPos = mp; m_rmbMoved = false; }
    if (ImGui::IsMouseDown(ImGuiMouseButton_Right) && (std::fabs(mp.x - m_rmbPos.x) + std::fabs(mp.y - m_rmbPos.y)) > 4) m_rmbMoved = true;
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !m_rmbMoved && inView) {
        if (m_holding) cancelHeld();
    }
    if (!inView) return;
    if (io.KeyAlt) return;   // Alt + left drag orbits

    if (!m_holding && !m_gizmoDragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) m_gizmoHover = pickGizmo(mp.x, mp.y);
    if (m_gizmoDragging) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) { m_gizmoDragging = false; autosave(); }
        else gizmoDragTo(mp.x, mp.y);
        return;
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (m_holding) { dropHeld(io.KeyCtrl); return; }
        if (!m_pivotEdit) {
            GizmoHandle gh = pickGizmo(mp.x, mp.y);
            if (gh.kind == 2) {   // centre sphere: free move, the brick goes in hand and drops on release
                m_pressPoint = gizmoPivot(m_selected);
                const bool ap = m_autoPivot;
                m_autoPivot = false;
                beginMove(m_selected, mp.x, mp.y);
                m_autoPivot = ap;
                m_dragStarted = true;
                return;
            }
            if (gh.kind >= 0) { gizmoStart(gh, mp.x, mp.y); return; }
        }
        if (m_mode == 2 || m_mode == 3) {   // Move / Rotate: a tap selects (Auto Pivot moves the pivot), dragging orbits
            float3 pt;
            int hit = pickBrick(mp.x, mp.y, &pt);
            if (hit >= 0) {
                if (m_autoPivot) {
                    const Part& p = m_lib.parts[m_bricks[hit].part];
                    float best = 1e30f;
                    for (size_t v = 0; v < p.voxels.size(); ++v) {
                        float d = length(m_build.worldVoxel(m_bricks[hit], p.voxels[v]) - pt);
                        if (d < best) { best = d; m_bricks[hit].grab = (int)v; }
                    }
                }
                select(hit);
            }
            m_leftOrbit = true;
            return;
        }
        m_pressed = true;
        m_dragStarted = false;
        m_pressPos = mp;
        m_pressBrick = pickBrick(mp.x, mp.y, &m_pressPoint);
        if (m_mode == 0 && m_pressBrick < 0) { m_marquee = true; m_marquee0 = m_marquee1 = mp; m_pressed = false; }
        if (m_mode == 1 && m_pressBrick < 0) m_leftOrbit = true;   // Visor: dragging the empty space orbits
    }
    if (m_marquee) {   // selection rectangle (mode Sélection): bricks whose box centre falls inside, Shift / Ctrl adds
        m_marquee1 = mp;
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            m_marquee = false;
            const float x0 = std::min(m_marquee0.x, m_marquee1.x), x1 = std::max(m_marquee0.x, m_marquee1.x);
            const float y0 = std::min(m_marquee0.y, m_marquee1.y), y1 = std::max(m_marquee0.y, m_marquee1.y);
            if (x1 - x0 < 4 && y1 - y0 < 4) { if (!io.KeyShift && !io.KeyCtrl) select(-1); return; }
            std::vector<int> all = (io.KeyShift || io.KeyCtrl) ? selection() : std::vector<int>();
            View v = view();
            int hits = 0;
            for (int i = 0; i < (int)m_bricks.size(); ++i) {
                Box bx = m_build.aabbOf(m_bricks[i].part, m_bricks[i].rot, m_bricks[i].pos);
                float sx, sy;
                if (!v.project((bx.mn + bx.mx) * 0.5f, sx, sy)) continue;
                if (sx >= x0 && sx <= x1 && sy >= y0 && sy <= y1) { ++hits; if (std::find(all.begin(), all.end(), i) == all.end()) all.push_back(i); }
            }
            m_selected = all.empty() ? -1 : all[0];
            m_multi.assign(all.size() > 1 ? all.begin() + 1 : all.end(), all.end());
            syncObjects();
            char t[64];
            snprintf(t, sizeof(t), "%d briques sélectionnées", (int)all.size());
            if (!all.empty()) toast(t);
        }
        return;
    }
    if (m_pressed && m_mode == 1 && ImGui::IsMouseDown(ImGuiMouseButton_Left) && !m_dragStarted && m_pressBrick >= 0 &&
        (std::fabs(mp.x - m_pressPos.x) + std::fabs(mp.y - m_pressPos.y)) > 6) {
        m_dragStarted = true;
        m_pressed = false;
        beginMove(m_pressBrick, mp.x, mp.y);   // drag a placed brick: it comes into the hand
    }
    if (m_pressed && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        m_pressed = false;
        const bool moved = (std::fabs(mp.x - m_pressPos.x) + std::fabs(mp.y - m_pressPos.y)) > 6;
        if (!(m_leftOrbit && moved)) select(m_pressBrick, io.KeyShift || io.KeyCtrl);
    }
    // the brick dragged into the hand drops where the button is released
    if (m_holding && m_dragStarted && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        m_dragStarted = false;
        dropHeld();
    }
}

} // namespace brickit
