// Prefab studio panels (workspace 3), drawn with the Atelier helpers of AtelierUI.cpp (this file is included at its
// end, so the theme colours, buttons, sliders and panels are the same as the other workspaces):
// Parts (hierarchy) | viewport (debug views, overlays, move gizmo) | Inspector (Part, Light, Debug, Asset)
//                   | Timeline (clips, tracks, keys) / UV Layout
// The logic (scene, files, undo, playback) is in PrefabStudio.cpp.

#include <filesystem>
#include <functional>
#include <sstream>

namespace app {

namespace {

bool pfProject(const Camera& c, const ScreenRect& rc, const float3& p, ImVec2& out) {
    float3 r, u, f;
    c.basis(r, u, f);
    float3 v = p - c.eye();
    float z = dot(v, f);
    if (z < 1e-3f) return false;
    float t = std::tan(c.fovY * 0.5f), a = rc.w / rc.h;
    out = ImVec2(rc.x + (0.5f + 0.5f * dot(v, r) / (z * t * a)) * rc.w, rc.y + (0.5f - 0.5f * dot(v, u) / (z * t)) * rc.h);
    return true;
}

void pfLine3(ImDrawList* d, const Camera& c, const ScreenRect& rc, const float3& a, const float3& b, ImU32 col, float th) {
    ImVec2 pa, pb;
    if (pfProject(c, rc, a, pa) && pfProject(c, rc, b, pb)) d->AddLine(pa, pb, col, th);
}

float pfDistSeg(ImVec2 p, ImVec2 a, ImVec2 b) {
    float dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
    float t = l2 > 1e-6f ? clampf(((p.x - a.x) * dx + (p.y - a.y) * dy) / l2, 0, 1) : 0;
    float qx = a.x + dx * t - p.x, qy = a.y + dy * t - p.y;
    return std::sqrt(qx * qx + qy * qy);
}

const ImU32 kAxis[3] = {IM_COL32(229, 72, 77, 255), IM_COL32(70, 180, 88, 255), IM_COL32(62, 142, 247, 255)};
const char* kDebugViews[] = {"Lit", "Albedo", "Normals", "UV Checker", "Clay", "Part Colors"};
const char* kDebugTips[] = {"Final look: materials, textures and the studio lights (1)",
                            "Base colour only, no light: checks the textures and colours (2)",
                            "World normals as colours (X red, Y green, Z blue): flipped or broken normals stand out (3)",
                            "8 x 8 checker over the UV square: stretching, seams and scale of the UVs; magenta = outside 0..1 (4)",
                            "Neutral grey material under the same lights: reads the shapes and the silhouette (5)",
                            "One colour per part: shows how the asset is split (6)"};
const char* kLightPresets[] = {"Studio", "Daylight", "Sunset", "Night", "HDRI"};
const char* kCategories[] = {"Building", "Unit", "Prop", "Resource", "Effect"};

// centre column tabs of the studio: 3D View | UV Editor (left of the view and UV Editor toolbars)
void pfCenterTabs(int& tab, float& bx, float by) {
    if (TabButton("pfCenter3d", {bx, by, 96 * s, 24 * s}, "3D View", Icon::Cube, tab == 0)) tab = 0;
    Tip("3D view of the prefab");
    bx += 99 * s;
    if (TabButton("pfCenterUv", {bx, by, 108 * s, 24 * s}, "UV Editor", Icon::Grid, tab == 1)) tab = 1;
    Tip("UVs of the selected part, large: UDIM tiles, checks, UV set comparison");
    bx += 111 * s;
    if (TabButton("pfCenterRig", {bx, by, 70 * s, 24 * s}, "Rig", Icon::Link, tab == 2)) tab = 2;
    Tip("Rig module: skeleton (joints), skinning, weight painting, pose");
    bx += 78 * s;
    dl->AddLine(ImVec2(bx - 4 * s, by + 3 * s), ImVec2(bx - 4 * s, by + 21 * s), P().border);
    bx += 6 * s;
}


} // namespace

// --------------------------------------------------------------------------- layout

void App::drawPrefabStudio(float X, float Y, float W, float H) {
    PrefabStudio& F = m_pf;
    prefabInit();
    float maxL = std::max(200.0f, std::min(380.0f, W / s - 720)), maxR = std::max(270.0f, std::min(420.0f, W / s - 700));
    float maxB = std::max(160.0f, H / s - 300);
    F.leftW = std::clamp(F.leftW, 200.0f, maxL);
    F.rightW = std::clamp(F.rightW, 270.0f, maxR);
    F.bottomH = std::clamp(F.bottomH, 150.0f, maxB);
    float lw = F.leftW * s, rw = F.rightW * s, bh = F.bottomH * s;
    float cx = X + lw + 12 * s, cw = W - lw - 12 * s - rw - 12 * s;
    if (F.centerTab == 2) pfSkeleton(X + 5 * s, Y, lw, H - 10 * s);
    else pfParts(X + 5 * s, Y, lw, H - 10 * s);
    if (F.centerTab == 2) {   // Rig module: its viewport and the timeline (joint tracks)
        pfRigViewport(cx, Y, cw, H - bh - 18 * s);
        pfTimeline(cx, Y + H - bh - 8 * s, cw, bh);
    } else if (F.centerTab == 1) pfUvEditor(cx, Y, cw, H - 10 * s);   // the UV Editor takes the whole centre column
    else {
        pfViewport(cx, Y, cw, H - bh - 18 * s);
        pfTimeline(cx, Y + H - bh - 8 * s, cw, bh);
    }
    if (F.centerTab == 2) pfRigInspector(X + W - rw - 6 * s, Y, rw, H - 10 * s);
    else pfInspector(X + W - rw - 6 * s, Y, rw, H - 10 * s);
    dl = ImGui::GetWindowDrawList();
    Splitter("pfSplitLeft", {X + lw + 5 * s, Y, 7 * s, H - 10 * s}, F.leftW, 200, maxL, true);
    Splitter("pfSplitRight", {X + W - rw - 12 * s, Y, 6 * s, H - 10 * s}, F.rightW, 270, maxR, true, true);
    if (F.centerTab != 1) Splitter("pfSplitBottom", {cx, Y + H - bh - 13 * s, cw, 5 * s}, F.bottomH, 150, maxB, false, true);
}

// --------------------------------------------------------------------------- parts (hierarchy)

void App::pfParts(float x, float y, float w, float h) {
    PrefabStudio& F = m_pf;
    PrefabAsset& A = F.asset;
    R r = {x, y, w, h};
    Panel(r, Icon::Collection, kPrefabNoun, "ASSET", P().accent);
    float px = r.x + 10 * s, py = r.y + 44 * s, pw = r.w - 20 * s;
    g_labelRight = px + pw;
    // asset card: name, file, modified state
    {
        const bool dirty = prefabDirty();
        Box({px, py, pw, 50 * s}, Fade(P().accent, 26), 4, true);
        Glyph(Icon::Cube, px + 10 * s, py + 9 * s, 16, P().accent);
        dl->PushClipRect(ImVec2(px, py), ImVec2(px + pw - 6 * s, py + 50 * s), true);
        TextBold(px + 34 * s, py + 7 * s, A.name + (dirty ? " *" : ""), P().text, 13);
        std::string file = F.path.empty() ? std::string("Not saved yet") : toUtf8(std::filesystem::path(F.path).filename().wstring());
        Text(px + 34 * s, py + 28 * s, A.game.category + "  ·  " + file, P().muted, 9.5f);
        dl->PopClipRect();
        py += 58 * s;
        float bw = (pw - 3 * 5 * s) / 4;
        if (Button("pfNew", {px, py, bw, 25 * s}, "New", Icon::Plus)) prefabNew(false);
        Tip("New empty prefab (Shift+click: the sample Outpost)");
        if (ImGui::IsItemClicked() && ImGui::GetIO().KeyShift) prefabNew(true);
        if (Button("pfOpen", {px + bw + 5 * s, py, bw, 25 * s}, "Open", Icon::FolderOpen)) prefabOpenDialog();
        Tip("Open a .prefab file (Ctrl+O)");
        if (Button("pfSave", {px + 2 * (bw + 5 * s), py, bw, 25 * s}, "Save", Icon::Save, dirty)) prefabSave(false);
        Tip("Save the prefab (Ctrl+S, Ctrl+Shift+S: save as)");
        if (PrimaryButton("pfPlace", {px + 3 * (bw + 5 * s), py, bw, 25 * s}, "Place", Icon::Scene)) prefabAddToScene();
        Tip("Add the prefab to the scene (Scene workspace) as a group");
        py += 35 * s;
    }
    dl->AddLine(ImVec2(px, py), ImVec2(px + pw, py), P().border);
    py += 12 * s;
    Text(px, py, "PARTS", P().text, 11);
    std::string cnt = std::to_string(A.parts.size()) + " parts";
    Text(px + pw - TextSize(cnt, 9).x, py + 2 * s, cnt, P().muted, 9);
    py += 22 * s;
    Search("##pfSearch", {px, py, pw, 27 * s}, F.search, sizeof(F.search), "Search parts...");
    py += 34 * s;
    {
        float bw = (pw - 2 * 5 * s) / 3;
        if (Button("pfAdd", {px, py, bw, 24 * s}, "Add", Icon::Plus)) ImGui::OpenPopup("pfAddPart");
        Tip("Add a part: primitive, mesh file (OBJ) or empty pivot (child of the selected pivot)");
        if (ImGui::BeginPopup("pfAddPart")) {
            ImGui::SetWindowFontScale(1.4f * ts());
            if (ImGui::MenuItem("Mesh File (OBJ)…")) prefabAddPart(100);
            ImGui::Separator();
            for (int t : {PRIM_CUBE, PRIM_SPHERE, PRIM_PLANE, PRIM_CYLINDER, PRIM_TORUS, PRIM_DISK})
                if (ImGui::MenuItem(primitiveName(t))) prefabAddPart(t);
            ImGui::Separator();
            if (ImGui::MenuItem("Empty Pivot")) prefabAddPart(0);
            ImGui::EndPopup();
        }
        const bool has = F.sel >= 0 && F.sel < (int)A.parts.size();
        if (Button("pfDup", {px + bw + 5 * s, py, bw, 24 * s}, "Copy", Icon::Link, false, false, has)) prefabDuplicate();
        Tip("Duplicate the part with its children and its animation (Ctrl+D)");
        if (Button("pfDel", {px + 2 * (bw + 5 * s), py, bw, 24 * s}, "Delete", Icon::Trash, false, false, has)) prefabDelete();
        Tip("Delete the part, its children and their tracks (Del)");
        py += 32 * s;
    }

    // tree
    ImGui::SetCursorScreenPos(ImVec2(px, py));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("pfTree", ImVec2(pw, std::max(20.0f, r.y + r.h - py - 9 * s)), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImGui::SetWindowFontScale(atelier::Style().layoutScale);
    ImGui::PopStyleVar();
    dl = ImGui::GetWindowDrawList();
    float yy = ImGui::GetCursorScreenPos().y;
    const float rowH = 26 * s, indentW = 14 * s;
    const PrefabClip* clip = prefabClip();
    std::vector<std::pair<int, int>> rows;   // part index, depth
    std::function<void(uint32_t, int)> walk = [&](uint32_t parent, int depth) {
        for (int i : A.childrenOf(parent)) {
            if (depth > 32) return;
            bool show = matches(A.parts[i].name, F.search);
            if (show || *F.search) {
                if (show) rows.push_back({i, *F.search ? 0 : depth});
            }
            if (!A.parts[i].collapsed || *F.search) walk(A.parts[i].id, depth + 1);
        }
    };
    walk(0, 0);
    uint32_t dropOn = 0;
    bool dropRoot = false;
    for (auto [i, depth] : rows) {
        PrefabPart& p = A.parts[i];
        R row = {px, yy, pw - 6 * s, rowH - 2 * s};
        ImGui::PushID((int)p.id);
        bool clicked = Hit("row", row);
        bool hovered = ImGui::IsItemHovered();
        if (clicked) { F.sel = i; F.keyTrack = F.keyIndex = -1; }
        if (hovered && ImGui::IsMouseDoubleClicked(0)) {
            F.renameId = p.id;
            snprintf(F.renameBuf, sizeof(F.renameBuf), "%s", p.name.c_str());
            F.renameFocus = true;
        }
        if (ImGui::BeginDragDropSource()) {
            uint32_t id = p.id;
            ImGui::SetDragDropPayload("WZX_PREFAB_PART", &id, sizeof(id));
            ImGui::Text("%s", p.name.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("WZX_PREFAB_PART")) {
                uint32_t id = *(const uint32_t*)pl->Data;
                if (id != p.id && !A.isAncestor(id, p.id)) dropOn = id, F.dragPart = p.id;
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::BeginPopupContextItem("pfRowMenu")) {
            ImGui::SetWindowFontScale(1.4f * ts());
            F.sel = i;
            if (ImGui::MenuItem("Rename", "F2")) { F.renameId = p.id; snprintf(F.renameBuf, sizeof(F.renameBuf), "%s", p.name.c_str()); F.renameFocus = true; }
            if (ImGui::MenuItem("Duplicate", "Ctrl+D")) prefabDuplicate();
            if (ImGui::MenuItem("Key Transform", "K")) prefabKeySelected();
            if (p.parent && ImGui::MenuItem("Unparent")) { prefabPushUndo(); p.parent = 0; }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete", "Del")) prefabDelete();
            ImGui::EndPopup();
        }
        if (i == F.sel) SelectedRow(row);
        else if (hovered) Box(row, P().hover, 3);
        float ix = px + 6 * s + depth * indentW;
        const bool hasKids = !A.childrenOf(p.id).empty();
        if (hasKids) {
            if (Hit("fold", {ix - 2 * s, yy + 4 * s, 14 * s, 16 * s})) p.collapsed = !p.collapsed;
            Glyph(p.collapsed ? Icon::ChevronRight : Icon::ChevronDown, ix, yy + 8 * s, 9, P().muted);
        }
        ix += 13 * s;
        Icon icon = p.isPivot() ? Icon::Move : !p.meshPath.empty() ? Icon::Cube : Icon::Grid;
        ImU32 ic = p.isPivot() ? P().muted : srgbU32(p.material.baseColor);
        if (!p.meshPath.empty() && m_pf.meshErrors.count(p.meshPath)) ic = IM_COL32(230, 80, 70, 255);
        Glyph(icon, ix, yy + 6 * s, 13, ic);
        ix += 20 * s;
        if (F.renameId == p.id) {
            ImGui::SetCursorScreenPos(ImVec2(ix, yy + 1 * s));
            ImGui::SetNextItemWidth(row.x + row.w - ix - 30 * s);
            if (F.renameFocus) { ImGui::SetKeyboardFocusHere(); F.renameFocus = false; }
            bool done = ImGui::InputText("##rename", F.renameBuf, sizeof(F.renameBuf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            if (done || ImGui::IsItemDeactivated()) {
                if (ImGui::IsKeyPressed(ImGuiKey_Escape) == false && *F.renameBuf && p.name != F.renameBuf) { prefabPushUndo(); p.name = F.renameBuf; }
                F.renameId = 0;
            }
        } else {
            dl->PushClipRect(ImVec2(ix, yy), ImVec2(row.x + row.w - 44 * s, yy + rowH), true);
            Text(ix, yy + (rowH - 2 * s - 12 * s * ts()) / 2, p.name, p.visible ? P().text : P().muted, 11.5f);
            dl->PopClipRect();
        }
        // animated in the current clip: a small diamond
        bool animated = false;
        if (clip)
            for (const PrefabTrack& t : clip->tracks) animated |= !t.joint && t.part == p.id && !t.keys.empty();
        if (animated) {
            ImVec2 c(row.x + row.w - 34 * s, yy + rowH / 2 - 1 * s);
            float k = 4 * s;
            dl->AddQuadFilled(ImVec2(c.x, c.y - k), ImVec2(c.x + k, c.y), ImVec2(c.x, c.y + k), ImVec2(c.x - k, c.y), kAmber);
        }
        if (Hit("eye", {row.x + row.w - 22 * s, yy + 3 * s, 20 * s, 18 * s})) { prefabPushUndo(); p.visible = !p.visible; }
        Tip(p.visible ? "Hide" : "Show");
        Glyph(Icon::Eye, row.x + row.w - 19 * s, yy + 6 * s, 12, p.visible ? P().muted : Fade(P().muted, 90));
        if (!p.visible) dl->AddLine(ImVec2(row.x + row.w - 20 * s, yy + 19 * s), ImVec2(row.x + row.w - 6 * s, yy + 5 * s), P().muted, 1.2f * s);
        ImGui::PopID();
        yy += rowH;
    }
    // empty area: drop = back to the root, click = no selection
    float rest = std::max(rowH, ImGui::GetWindowPos().y + ImGui::GetWindowHeight() - yy - 2 * s);
    ImGui::SetCursorScreenPos(ImVec2(px, yy));
    ImGui::InvisibleButton("pfTreeEmpty", ImVec2(pw - 6 * s, rest));
    if (ImGui::IsItemClicked()) F.sel = -1;
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("WZX_PREFAB_PART")) { dropOn = *(const uint32_t*)pl->Data; dropRoot = true; }
        ImGui::EndDragDropTarget();
    }
    if (A.parts.empty()) {
        Text(px + 8 * s, yy + 6 * s, "No part yet: Add a mesh file,", P().muted, 10);
        Text(px + 8 * s, yy + 22 * s, "a primitive or a pivot.", P().muted, 10);
    }
    if (dropOn) {
        int di = A.findPart(dropOn);
        if (di >= 0) {
            prefabPushUndo();
            A.parts[di].parent = dropRoot ? 0 : F.dragPart;
            F.rebuild = true;
        }
    }
    ImGui::EndChild();
    dl = ImGui::GetWindowDrawList();
}

// --------------------------------------------------------------------------- viewport

void App::pfViewport(float x, float y, float w, float h) {
    PrefabStudio& F = m_pf;
    PrefabAsset& A = F.asset;
    R r = {x, y, w, h};
    // toolbar over the view: debug views, wireframe, frame, turntable
    const float tbH = 34 * s;
    Box({x, y, w, tbH}, P().panel, 4, true);
    float bx = x + 6 * s, by = y + 5 * s;
    pfCenterTabs(F.centerTab, bx, by);
    for (int i = 0; i < 6; ++i) {
        float bw = TextSize(kDebugViews[i], 11).x + 13 * s * P().iconScale + 30 * s;
        char id[24];
        snprintf(id, sizeof(id), "pfView%d", i);
        if (TabButton(id, {bx, by, bw, 24 * s}, kDebugViews[i], i == 0 ? Icon::Sun : i == 3 ? Icon::Grid : i == 2 ? Icon::World : Icon::Material, F.debugView == i)) F.debugView = i;
        Tip(kDebugTips[i]);
        bx += bw + 3 * s;
    }
    bx += 6 * s;
    dl->AddLine(ImVec2(bx, y + 8 * s), ImVec2(bx, y + tbH - 8 * s), P().border);
    bx += 8 * s;
    static const char* wires[] = {"Wire Off", "Wire", "Wire Overlay"};
    if (Button("pfWire", {bx, by, 98 * s, 24 * s}, wires[F.wireframe], Icon::Grid, F.wireframe != 0)) F.wireframe = (F.wireframe + 1) % 3;
    Tip("Wireframe: off, lines only, lines over the shaded view (Z)");
    float rx = x + w - 6 * s;
    rx -= 28 * s;
    if (Button("pfFrame", {rx, by, 26 * s, 24 * s}, "", Icon::Home, false, true)) prefabFrameAll();
    Tip("Frame the whole prefab (F)");
    rx -= 30 * s;
    if (Button("pfTurn", {rx, by, 26 * s, 24 * s}, "", Icon::Refresh, F.turntable, true)) F.turntable = !F.turntable;
    Tip("Turntable: the camera turns around the prefab");

    // the image
    const float iy = y + tbH + 4 * s, ih = h - tbH - 4 * s;
    F.rcView = {x, iy, w, ih};
    F.vpPx = {0, 0, std::max(16, (int)w), std::max(16, (int)ih)};
    ImGui::SetCursorScreenPos(ImVec2(x, iy));
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##pfView", ImVec2(w, ih), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    dl->AddImage(m_gui.viewportTexture(), ImVec2(x, iy), ImVec2(x + w, iy + ih));
    dl->PushClipRect(ImVec2(x, iy), ImVec2(x + w, iy + ih), true);
    const Camera& cam = F.camera;
    const ScreenRect& rc = F.rcView;

    // RTS footprint: the cells the prefab occupies, centred on its pivot, and the selection circle
    if (F.showFootprint) {
        const int fx = std::max(1, A.game.footprintX), fz = std::max(1, A.game.footprintZ);
        const float x0 = -fx * 0.5f, z0 = -fz * 0.5f, yv = 0.004f;
        ImU32 fc = Fade(P().accent, 200), fl = Fade(P().accent, 90);
        for (int i = 0; i <= fx; ++i) pfLine3(dl, cam, rc, float3(x0 + i, yv, z0), float3(x0 + i, yv, z0 + fz), i == 0 || i == fx ? fc : fl, (i == 0 || i == fx ? 2.0f : 1.0f) * s);
        for (int k = 0; k <= fz; ++k) pfLine3(dl, cam, rc, float3(x0, yv, z0 + k), float3(x0 + fx, yv, z0 + k), k == 0 || k == fz ? fc : fl, (k == 0 || k == fz ? 2.0f : 1.0f) * s);
        const float rr = A.game.selectionRadius;
        for (int k = 0; k < 64; ++k) {
            float a0 = k * 2 * kPi / 64, a1 = (k + 1) * 2 * kPi / 64;
            pfLine3(dl, cam, rc, float3(std::cos(a0) * rr, yv, std::sin(a0) * rr), float3(std::cos(a1) * rr, yv, std::sin(a1) * rr), IM_COL32(120, 200, 140, 150), 1.4f * s);
        }
        pfLine3(dl, cam, rc, float3(0, 0, 0), float3(0, A.game.height, 0), IM_COL32(120, 200, 140, 120), 1.0f * s);
        pfLine3(dl, cam, rc, float3(-0.15f, A.game.height, 0), float3(0.15f, A.game.height, 0), IM_COL32(120, 200, 140, 200), 2.0f * s);
        ImVec2 lp;
        if (pfProject(cam, rc, float3(x0, yv, z0 + fz), lp)) {
            char t[64];
            snprintf(t, sizeof(t), "%d x %d cells", fx, fz);
            Text(lp.x + 4 * s, lp.y + 2 * s, t, fc, 10);
        }
    }
    // bounds of the selected part (oriented box) and pivots
    const bool selValid = F.sel >= 0 && F.sel < (int)A.parts.size() && F.sel < (int)F.world.size();
    if (F.showPivots)
        for (size_t i = 0; i < F.world.size(); ++i) {
            ImVec2 sp;
            if ((int)i == F.sel || !pfProject(cam, rc, F.world[i].position, sp)) continue;
            dl->AddCircleFilled(sp, 2.6f * s, A.parts[i].isPivot() ? IM_COL32(230, 230, 230, 170) : IM_COL32(200, 200, 200, 90));
        }
    if (F.showBounds && selValid) {
        int oi = F.sel < (int)F.partObject.size() ? F.partObject[F.sel] : -1;
        const PartWorld& wv = F.world[F.sel];
        if (oi >= 0 && oi < (int)F.scene.objects.size()) {
            int mi = F.scene.objects[oi].mesh;
            if (mi >= 0 && mi < (int)F.meshMin.size()) {
                float3 a = F.meshMin[mi], b = F.meshMax[mi];
                float3 cn[8];
                for (int k = 0; k < 8; ++k)
                    cn[k] = wv.position + qRot(wv.rot, float3((k & 1) ? b.x : a.x, (k & 2) ? b.y : a.y, (k & 4) ? b.z : a.z) * wv.scale);
                const int e[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
                for (auto& ed : e) pfLine3(dl, cam, rc, cn[ed[0]], cn[ed[1]], IM_COL32(255, 214, 120, 150), 1.0f * s);
                float3 d = (b - a) * wv.scale;
                char t[96];
                snprintf(t, sizeof(t), "%.2f x %.2f x %.2f", std::fabs(d.x), std::fabs(d.y), std::fabs(d.z));
                ImVec2 tp;
                if (pfProject(cam, rc, cn[7], tp)) Text(tp.x + 5 * s, tp.y - 14 * s, t, IM_COL32(255, 214, 120, 220), 9.5f);
            }
        }
    }
    // move gizmo on the selected part (world axes; Ctrl = quarter cell steps)
    F.gizmoHover = -1;
    if (selValid) {
        ImVec2 o;
        if (pfProject(cam, rc, F.world[F.sel].position, o)) {
            const float len = 70 * s;
            float3 rr, uu, ff;
            cam.basis(rr, uu, ff);
            ImVec2 tips[3];
            ImVec2 m = ImGui::GetMousePos();
            for (int a = 0; a < 3; ++a) {
                float3 av(a == 0 ? 1.0f : 0.0f, a == 1 ? 1.0f : 0.0f, a == 2 ? 1.0f : 0.0f);
                ImVec2 d(dot(av, rr), -dot(av, uu));
                float l = std::sqrt(d.x * d.x + d.y * d.y);
                if (l < 0.15f) { tips[a] = o; continue; }
                tips[a] = ImVec2(o.x + d.x * len, o.y + d.y * len);
                if (hovered && F.gizmoAxis < 0 && pfDistSeg(m, o, tips[a]) < 7 * s) F.gizmoHover = a;
            }
            for (int a = 0; a < 3; ++a) {
                if (tips[a].x == o.x && tips[a].y == o.y) continue;
                bool hot = F.gizmoHover == a || F.gizmoAxis == a;
                ImU32 c = hot ? IM_COL32(255, 230, 120, 255) : kAxis[a];
                dl->AddLine(o, tips[a], c, (hot ? 3.2f : 2.2f) * s);
                dl->AddCircleFilled(tips[a], (hot ? 6.0f : 4.5f) * s, c);
            }
            dl->AddCircleFilled(o, 4 * s, IM_COL32(240, 240, 240, 230));
        }
    }
    prefabViewInput(hovered);

    // statistics (top left) and the clip / time (top right)
    {
        size_t tris = 0, verts = 0;
        int drawn = 0;
        for (size_t i = 0; i < F.partObject.size(); ++i) {
            int oi = F.partObject[i];
            if (oi < 0 || oi >= (int)F.scene.objects.size() || !F.scene.objects[oi].visible) continue;
            const MeshData& m = F.scene.meshes[F.scene.objects[oi].mesh];
            tris += m.triangleCount();
            verts += m.vertices.size();
            ++drawn;
        }
        char t[160];
        snprintf(t, sizeof(t), "%d parts  ·  %zu tris  ·  %zu verts  ·  %d textures", drawn, tris, verts, (int)F.scene.textures.size());
        R sr = {x + 8 * s, iy + 8 * s, TextSize(t, 10).x + 16 * s, 22 * s};
        Box(sr, P().overlay, 4);
        Text(sr.x + 8 * s, sr.y + 5 * s, t, P().text, 10);
        const PrefabClip* c = prefabClip();
        std::string ct = c ? c->name + (F.playing ? "  ▶  " : "  ❚❚  ") : std::string("Rest Pose");
        if (c) { char b[48]; snprintf(b, sizeof(b), "%.2f s", F.time); ct += b; }
        if (F.debugView) ct += std::string("  ·  ") + kDebugViews[F.debugView];
        R cr = {x + w - TextSize(ct, 10).x - 24 * s, iy + 8 * s, TextSize(ct, 10).x + 16 * s, 22 * s};
        Box(cr, P().overlay, 4);
        Text(cr.x + 8 * s, cr.y + 5 * s, ct, F.playing ? P().accent : P().text, 10);
        if (F.autoKey) {
            R ar = {cr.x - 84 * s, cr.y, 76 * s, 22 * s};
            Box(ar, IM_COL32(170, 40, 40, 220), 4);
            Center(ar, "AUTO KEY", IM_COL32(255, 235, 235, 255), 9.5f);
        }
    }
    // axis triad (bottom left)
    {
        float3 rr, uu, ff;
        cam.basis(rr, uu, ff);
        ImVec2 g(x + 40 * s, iy + ih - 40 * s);
        const char* names[] = {"X", "Y", "Z"};
        for (int a = 0; a < 3; ++a) {
            float3 av(a == 0 ? 1.0f : 0.0f, a == 1 ? 1.0f : 0.0f, a == 2 ? 1.0f : 0.0f);
            ImVec2 e(g.x + dot(av, rr) * 24 * s, g.y - dot(av, uu) * 24 * s);
            dl->AddLine(g, e, kAxis[a], 2 * s);
            Text(e.x - 3 * s, e.y - 7 * s, names[a], kAxis[a], 9);
        }
    }
    dl->PopClipRect();
}

// --------------------------------------------------------------------------- inspector

void App::pfInspector(float x, float y, float w, float h) {
    PrefabStudio& F = m_pf;
    PrefabAsset& A = F.asset;
    R r = {x, y, w, h};
    F.inspectorTab = std::clamp(F.inspectorTab, 0, 3);
    const char* tags[] = {"PART", "LIGHTING", "DEBUG", "ASSET"};
    Panel(r, Icon::Info, "Inspector", tags[F.inspectorTab], P().text);
    const char* tabs[] = {"Part", "Light", "Debug", "Asset"};
    Icon is[] = {Icon::Cube, Icon::Sun, Icon::Monitor, Icon::Collection};
    float tw = (r.w - 26 * s - 15 * s) / 4, tx = r.x + 13 * s;
    for (int i = 0; i < 4; ++i) {
        char id[24];
        snprintf(id, sizeof(id), "pfTab%d", i);
        if (TabButton(id, {tx, r.y + 40 * s, tw, (P().skin ? 29 : 25) * s}, tabs[i], is[i], F.inspectorTab == i)) F.inspectorTab = i;
        tx += tw + 5 * s;
    }
    if (P().skin) dl->AddLine(ImVec2(r.x + 13 * s, r.y + 69 * s), ImVec2(r.x + r.w - 13 * s, r.y + 69 * s), P().border);
    float by = r.y + 76 * s;
    g_labelRight = r.x + r.w - 21 * s;
    ImGui::SetCursorScreenPos(ImVec2(r.x + 13 * s, by));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("pfInspectorBody", ImVec2(r.w - 26 * s, r.y + r.h - by - 9 * s), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImGui::SetWindowFontScale(atelier::Style().layoutScale);
    ImGui::PopStyleVar();
    dl = ImGui::GetWindowDrawList();
    ImVec2 c0 = ImGui::GetCursorScreenPos();
    const float bx = c0.x, bw = r.w - 26 * s - 8 * s;
    float yy = c0.y;
    bool act = false;
    PrefabClip* clip = prefabClip();
    const float snapT = clip ? std::round(F.time * clip->fps) / clip->fps : 0.0f;
    auto field = [&](const char* id, char* buf, size_t n, float fy) {
        ImGui::SetCursorScreenPos(ImVec2(bx, fy));
        ImGui::SetNextItemWidth(bw);
        return ImGui::InputText(id, buf, n, ImGuiInputTextFlags_EnterReturnsTrue);
    };

    if (F.inspectorTab == 0) {
        // ------------------------------------------------------------ Part
        if (F.sel < 0 || F.sel >= (int)A.parts.size()) {
            Text(bx, yy, A.name, P().text, 15);
            yy += 25 * s;
            Text(bx, yy, "No part selected", P().muted, 11);
            yy += 34 * s;
            Box({bx, yy, bw, 98 * s}, P().bg, 4, true);
            Text(bx + 12 * s, yy + 12 * s, "ASSEMBLY", P().accent, 9);
            Text(bx + 12 * s, yy + 35 * s, "Click a part in the view or the list,", P().muted, 10);
            Text(bx + 12 * s, yy + 54 * s, "Add: mesh file, primitive or pivot,", P().muted, 10);
            Text(bx + 12 * s, yy + 73 * s, "drag a part on another to parent it.", P().muted, 10);
            yy += 110 * s;
        } else {
            PrefabPart& p = A.parts[F.sel];
            // header card
            Box({bx, yy, bw, 56 * s}, Fade(P().accent, 30), 4, true);
            Glyph(p.isPivot() ? Icon::Move : Icon::Cube, bx + 12 * s, yy + 15 * s, 24, p.isPivot() ? P().muted : srgbU32(p.material.baseColor));
            dl->PushClipRect(ImVec2(bx, yy), ImVec2(bx + bw - 8 * s, yy + 56 * s), true);
            Text(bx + 48 * s, yy + 11 * s, p.name, P().text, 14);
            std::string type = p.isPivot() ? "Pivot · transform only" : !p.meshPath.empty() ? "Mesh · " + std::filesystem::path(fromUtf8(p.meshPath)).filename().string()
                                                                                           : std::string("Primitive · ") + primitiveName(p.prim.type);
            int parent = p.parent ? A.findPart(p.parent) : -1;
            if (parent >= 0) type += "  ·  in " + A.parts[parent].name;
            Text(bx + 48 * s, yy + 33 * s, type, P().accent, 10);
            dl->PopClipRect();
            yy += 68 * s;
            {
                char name[96];
                snprintf(name, sizeof(name), "%s", p.name.c_str());
                Text(bx, yy, "Name", P().muted, 11);
                yy += 19 * s;
                if (field("##pfName", name, sizeof(name), yy) && *name) { prefabPushUndo(); p.name = name; }
                yy += 36 * s;
            }

            // transform (animated channels show the pose of the clip; editing them sets a key)
            Label(bx, yy, "TRANSFORM");
            yy += 24 * s;
            const PartPose& pose = F.sel < (int)F.pose.size() ? F.pose[F.sel] : PartPose{p.position, p.rotation, p.scale};
            const char* chNames[] = {"Position", "Rotation", "Scale"};
            for (int ch = 0; ch < CH_COUNT; ++ch) {
                const PrefabTrack* t = clip ? clip->track(p.id, ch) : nullptr;
                const bool animated = t && !t->keys.empty();
                const bool onKey = animated && t->keyAt(snapT, 0.5f / clip->fps) >= 0;
                Text(bx, yy, chNames[ch], animated ? kAmber : P().muted, 11);
                // key button: a diamond (filled = key at this frame)
                R kr = {bx + bw - 22 * s, yy - 3 * s, 20 * s, 18 * s};
                char kid[24];
                snprintf(kid, sizeof(kid), "pfKey%d", ch);
                if (Hit(kid, kr, clip != nullptr)) prefabKeySelected(ch);
                Tip(clip ? "Set a key for this channel at the current frame" : "Add a clip to animate (Asset tab)");
                {
                    ImVec2 c(kr.x + kr.w / 2, kr.y + kr.h / 2);
                    float k = 5 * s;
                    ImU32 kc = onKey ? kAmber : animated ? Fade(kAmber, 200) : P().muted;
                    if (onKey) dl->AddQuadFilled(ImVec2(c.x, c.y - k), ImVec2(c.x + k, c.y), ImVec2(c.x, c.y + k), ImVec2(c.x - k, c.y), kc);
                    else dl->AddQuad(ImVec2(c.x, c.y - k), ImVec2(c.x + k, c.y), ImVec2(c.x, c.y + k), ImVec2(c.x - k, c.y), kc, 1.3f * s);
                }
                yy += 19 * s;
                float3 v = ch == CH_POSITION ? pose.position : ch == CH_ROTATION ? pose.rotation : pose.scale;
                bool a2 = false;
                char fid[24];
                snprintf(fid, sizeof(fid), "pfCh%d", ch);
                if (AxisFields(fid, bx, yy, bw, &v.x, ch == CH_ROTATION ? 0.5f : ch == CH_SCALE ? 0.01f : 0.01f, ch == CH_ROTATION ? "%.1f" : "%.3f", a2)) {
                    if (a2) prefabPushUndo();
                    PrefabTrack* tr = clip ? clip->track(p.id, ch, F.autoKey) : nullptr;
                    if (tr) tr->setKey(snapT, v, INTERP_SMOOTH, 0.5f / clip->fps);
                    else (ch == CH_POSITION ? p.position : ch == CH_ROTATION ? p.rotation : p.scale) = v;
                } else if (a2) prefabPushUndo();
                yy += 36 * s;
            }
            yy += 4 * s;

            // geometry
            if (!p.isPivot()) {
                Label(bx, yy, "GEOMETRY");
                yy += 24 * s;
                if (!p.meshPath.empty()) {
                    Text(bx, yy, "Mesh file", P().muted, 11);
                    yy += 19 * s;
                    Box({bx, yy, bw, 26 * s}, P().bg, 3, true);
                    dl->PushClipRect(ImVec2(bx, yy), ImVec2(bx + bw - 70 * s, yy + 26 * s), true);
                    bool missing = m_pf.meshErrors.count(p.meshPath) > 0;
                    Text(bx + 8 * s, yy + 6 * s, p.meshPath, missing ? IM_COL32(240, 100, 90, 255) : P().text, 10);
                    dl->PopClipRect();
                    if (Button("pfMeshBrowse", {bx + bw - 64 * s, yy + 1 * s, 64 * s, 24 * s}, "Browse", Icon::FolderOpen)) {
                        std::wstring path = pickFileDialog(m_hwnd, false, L"Meshes (*.obj;*.gltf;*.glb;*.fbx)\0*.obj;*.gltf;*.glb;*.fbx\0All files\0*.*\0", L"obj", projectRoot() + L"\\assets", L"");
                        if (!path.empty()) { prefabPushUndo(); p.meshPath = relPath(path); m_pf.meshErrors.erase(p.meshPath); F.rebuild = true; }
                    }
                    yy += 34 * s;
                    ImGui::SetCursorScreenPos(ImVec2(bx, yy));
                    bool rc = p.recenter;
                    if (Check("Pivot at the base centre##pfRecenter", &rc)) { prefabPushUndo(); p.recenter = rc; F.rebuild = true; }
                    Tip("Moves the pivot of the mesh to the centre of its base (as the RTS buildings)");
                    yy += 32 * s;
                } else {
                    static const char* prims[] = {"Cube", "Sphere", "Plane", "Cylinder", "Torus", "Disk"};
                    int t = std::clamp(p.prim.type - 1, 0, 5);
                    Text(bx, yy, "Primitive", P().muted, 11);
                    yy += 19 * s;
                    if (Combo("pfPrim", {bx, yy, bw, 26 * s}, t, prims, 6, Icon::Grid)) { prefabPushUndo(); p.prim.type = t + 1; F.rebuild = true; }
                    yy += 34 * s;
                    PrimitiveParams q = p.prim;
                    bool ch = false, a3 = false;
                    if (q.type == PRIM_CUBE || q.type == PRIM_PLANE || q.type == PRIM_CYLINDER) {
                        Text(bx, yy, q.type == PRIM_CYLINDER ? "Size (height = Y)" : "Size", P().muted, 11);
                        yy += 19 * s;
                        ch |= AxisFields("pfSize", bx, yy, bw, &q.size.x, 0.01f, "%.3f", a3);
                        q.size = maxv(q.size, float3(0.001f));
                        yy += 38 * s;
                    }
                    if (q.type == PRIM_SPHERE || q.type == PRIM_CYLINDER || q.type == PRIM_TORUS || q.type == PRIM_DISK)
                        ch |= SliderRow("##pfRadius", "Radius", bx, yy, bw, &q.radius, 0.01f, 5.0f, "%.3f", a3);
                    if (q.type == PRIM_TORUS) ch |= SliderRow("##pfRadius2", "Tube radius", bx, yy, bw, &q.radius2, 0.005f, 2.0f, "%.3f", a3);
                    if (q.type != PRIM_CUBE && q.type != PRIM_PLANE) {
                        float seg = (float)q.segments;
                        if (SliderRow("##pfSeg", "Segments", bx, yy, bw, &seg, 3, 128, "%.0f", a3)) { q.segments = (int)seg; ch = true; }
                    }
                    if (q.type == PRIM_SPHERE || q.type == PRIM_TORUS) {
                        float rings = (float)q.rings;
                        if (SliderRow("##pfRings", "Rings", bx, yy, bw, &rings, 3, 64, "%.0f", a3)) { q.rings = (int)rings; ch = true; }
                    }
                    if (a3) prefabPushUndo();
                    if (ch) { p.prim = q; F.rebuild = true; }
                }
            }

            // material
            if (!p.isPivot()) {
                Label(bx, yy, "MATERIAL");
                yy += 24 * s;
                Material m = p.material;
                bool ch = false, a4 = false;
                {
                    std::vector<std::string> names;
                    for (const std::wstring& n : presetNames()) names.push_back(toUtf8(n));
                    std::vector<const char*> opts;
                    for (auto& n : names) opts.push_back(n.c_str());
                    int pr = std::clamp(m.preset, 0, (int)opts.size() - 1);
                    Text(bx, yy, "Preset", P().muted, 11);
                    yy += 19 * s;
                    if (Combo("pfPreset", {bx, yy, bw, 26 * s}, pr, opts.data(), (int)opts.size(), Icon::Material)) {
                        prefabPushUndo();
                        float3 keep = m.baseColor;
                        applyPreset(m, pr, 1.0f);
                        m.baseColor = keep;
                        m.preset = pr;
                        p.material = m;
                    }
                    yy += 34 * s;
                }
                Text(bx, yy, "Base color", P().muted, 11);
                yy += 19 * s;
                float c3[3] = {linToSrgb(m.baseColor.x), linToSrgb(m.baseColor.y), linToSrgb(m.baseColor.z)};
                ImGui::SetCursorScreenPos(ImVec2(bx, yy));
                ImGui::SetNextItemWidth(bw);
                if (ImGui::ColorEdit3("##pfBase", c3)) { m.baseColor = float3(srgbToLinear(c3[0]), srgbToLinear(c3[1]), srgbToLinear(c3[2])); ch = true; }
                a4 |= ImGui::IsItemActivated();
                yy += 34 * s;
                ch |= SliderRow("##pfRough", "Roughness", bx, yy, bw, &m.roughness, 0, 1, "%.2f", a4);
                ch |= SliderRow("##pfMetal", "Metallic", bx, yy, bw, &m.metallic, 0, 1, "%.2f", a4);
                ch |= SliderRow("##pfSpec", "Specular", bx, yy, bw, &m.specular, 0, 1, "%.2f", a4);
                ch |= SliderRow("##pfCoat", "Clear coat", bx, yy, bw, &m.coat, 0, 1, "%.2f", a4);
                Text(bx, yy, "Emission", P().muted, 11);
                yy += 19 * s;
                float e3[3] = {linToSrgb(m.emission.x), linToSrgb(m.emission.y), linToSrgb(m.emission.z)};
                ImGui::SetCursorScreenPos(ImVec2(bx, yy));
                ImGui::SetNextItemWidth(bw);
                if (ImGui::ColorEdit3("##pfEmis", e3)) { m.emission = float3(srgbToLinear(e3[0]), srgbToLinear(e3[1]), srgbToLinear(e3[2])); ch = true; }
                a4 |= ImGui::IsItemActivated();
                yy += 34 * s;
                ch |= SliderRow("##pfEmisK", "Emission strength", bx, yy, bw, &m.emissionStrength, 0, 20, "%.1f", a4);
                if (a4) prefabPushUndo();
                if (ch) p.material = m;
                // base colour image
                Text(bx, yy, "Base color texture", P().muted, 11);
                yy += 19 * s;
                Box({bx, yy, bw, 26 * s}, P().bg, 3, true);
                dl->PushClipRect(ImVec2(bx, yy), ImVec2(bx + bw - 100 * s, yy + 26 * s), true);
                Text(bx + 8 * s, yy + 6 * s, p.texture.empty() ? std::string("None") : p.texture, p.texture.empty() ? P().muted : P().text, 10);
                dl->PopClipRect();
                if (Button("pfTexBrowse", {bx + bw - 94 * s, yy + 1 * s, 64 * s, 24 * s}, "Browse", Icon::FolderOpen)) {
                    std::wstring path = pickFileDialog(m_hwnd, false, L"Images (*.png;*.jpg;*.jpeg;*.bmp;*.tga)\0*.png;*.jpg;*.jpeg;*.bmp;*.tga\0All files\0*.*\0", L"png",
                                                   projectRoot() + L"\\assets", L"");
                    if (!path.empty()) { prefabPushUndo(); p.texture = relPath(path); p.material.baseColor = float3(1); F.rebuild = true; }
                }
                if (Button("pfTexClear", {bx + bw - 26 * s, yy + 1 * s, 26 * s, 24 * s}, "", Icon::Trash, false, true, !p.texture.empty())) {
                    prefabPushUndo();
                    p.texture.clear();
                    F.rebuild = true;
                }
                yy += 36 * s;
            }
            ImGui::SetCursorScreenPos(ImVec2(bx, yy));
            bool vis = p.visible;
            if (Check("Visible##pfVis", &vis)) { prefabPushUndo(); p.visible = vis; }
            yy += 34 * s;
        }
    } else if (F.inspectorTab == 1) {
        // ------------------------------------------------------------ Lighting (the studio world, never saved in the scene)
        Text(bx, yy, "Studio world", P().text, 14);
        yy += 21 * s;
        Text(bx, yy, "Lighting of this workspace only: the main", P().muted, 10);
        yy += 16 * s;
        Text(bx, yy, "scene keeps its own lights.", P().muted, 10);
        yy += 26 * s;
        Label(bx, yy, "LIGHTING");
        yy += 24 * s;
        Text(bx, yy, "Preset", P().muted, 11);
        yy += 19 * s;
        Combo("pfLightPreset", {bx, yy, bw, 26 * s}, F.lightPreset, kLightPresets, 5, Icon::Sun);
        yy += 34 * s;
        bool a = false;
        if (F.lightPreset == 4) {
            std::vector<std::wstring> files = hdriFiles();
            files.insert(files.begin(), L"sky");
            std::vector<std::string> names;
            int cur = 0;
            for (size_t i = 0; i < files.size(); ++i) {
                names.push_back(files[i] == L"sky" ? std::string("Procedural Sky") : toUtf8(std::filesystem::path(files[i]).filename().wstring()));
                if (files[i] == F.hdri) cur = (int)i;
            }
            std::vector<const char*> opts;
            for (auto& n : names) opts.push_back(n.c_str());
            Text(bx, yy, "HDRI", P().muted, 11);
            yy += 19 * s;
            if (Combo("pfHdri", {bx, yy, bw, 26 * s}, cur, opts.data(), (int)opts.size(), Icon::World)) F.hdri = files[cur];
            yy += 34 * s;
            SliderRow("##pfHdriRot", "HDRI rotation", bx, yy, bw, &F.sunAzimuth, -180, 180, "%.0f°", a);
            SliderRow("##pfEnvK", "HDRI intensity", bx, yy, bw, &F.envIntensity, 0, 4, "%.2f", a);
        } else {
            SliderRow("##pfKey", F.lightPreset == 0 ? "Key light" : "Sun", bx, yy, bw, &F.keyIntensity, 0, 4, "%.2f", a);
            if (F.lightPreset == 0) {
                SliderRow("##pfFill", "Fill light", bx, yy, bw, &F.fillIntensity, 0, 4, "%.2f", a);
                SliderRow("##pfRim", "Rim light", bx, yy, bw, &F.rimIntensity, 0, 4, "%.2f", a);
            }
            SliderRow("##pfEnv", "Sky and ambient", bx, yy, bw, &F.envIntensity, 0, 4, "%.2f", a);
            SliderRow("##pfAz", F.lightPreset == 0 ? "Light rotation" : "Sun azimuth", bx, yy, bw, &F.sunAzimuth, -180, 180, "%.0f°", a);
            if (F.lightPreset != 2) SliderRow("##pfEl", F.lightPreset == 0 ? "Key height" : "Sun elevation", bx, yy, bw, &F.sunElevation, 5, 89, "%.0f°", a);
        }
        SliderRow("##pfExp", "Exposure (EV)", bx, yy, bw, &F.exposure, -4, 4, "%.1f", a);
        yy += 4 * s;
        Label(bx, yy, "STAGE");
        yy += 24 * s;
        static const char* floors[] = {"RTS Grid (1 cell)", "Plain Floor", "No Floor"};
        Text(bx, yy, "Floor", P().muted, 11);
        yy += 19 * s;
        Combo("pfFloor", {bx, yy, bw, 26 * s}, F.floorStyle, floors, 3, Icon::Grid);
        yy += 36 * s;
        ImGui::SetCursorScreenPos(ImVec2(bx, yy));
        Check("Turntable##pfTurnChk", &F.turntable);
        yy += 32 * s;
        if (Button("pfLightReset", {bx, yy, bw, 26 * s}, "Reset Lighting", Icon::Refresh)) {
            F.keyIntensity = F.fillIntensity = F.rimIntensity = F.envIntensity = 1.0f;
            F.sunAzimuth = 35.0f;
            F.sunElevation = 48.0f;
            F.exposure = 0.0f;
        }
        yy += 38 * s;
    } else if (F.inspectorTab == 2) {
        // ------------------------------------------------------------ Debug / inspection
        Label(bx, yy, "VIEW MODE");
        yy += 24 * s;
        for (int i = 0; i < 6; ++i) {
            char id[24];
            snprintf(id, sizeof(id), "pfDbg%d", i);
            R row = {bx, yy, bw, 26 * s};
            if (Button(id, row, kDebugViews[i], i == 3 ? Icon::Grid : i == 2 ? Icon::World : i == 0 ? Icon::Sun : Icon::Material, F.debugView == i)) F.debugView = i;
            Tip(kDebugTips[i]);
            char kb[4];
            snprintf(kb, sizeof(kb), "%d", i + 1);
            Text(row.x + row.w - 18 * s, row.y + 7 * s, kb, P().muted, 9);
            yy += 30 * s;
        }
        yy += 6 * s;
        Label(bx, yy, "OVERLAYS");
        yy += 24 * s;
        static const char* wires[] = {"Off", "Wireframe", "Wireframe Overlay"};
        Text(bx, yy, "Wireframe", P().muted, 11);
        yy += 19 * s;
        Combo("pfWireCombo", {bx, yy, bw, 26 * s}, F.wireframe, wires, 3, Icon::Grid);
        yy += 36 * s;
        auto chk = [&](const char* label, bool* v, const char* tip) {
            ImGui::SetCursorScreenPos(ImVec2(bx, yy));
            Check(label, v);
            Tip(tip);
            yy += 30 * s;
        };
        chk("RTS footprint and selection##pfFoot", &F.showFootprint, "Cells occupied, selection circle and height marker (Asset tab)");
        chk("Bounds of the selected part##pfBounds", &F.showBounds, "Oriented box and size of the selected part, in cells");
        chk("Pivots##pfPivots", &F.showPivots, "Origins of the parts (white = empty pivot)");
        chk("Selection outline##pfOutline", &F.showSelection, "Outline of the selected part (a pivot outlines its children)");
        chk("Floor grid##pfGrid", &F.showGrid, "RTS grid on the studio floor (1 line = 1 cell)");
        yy += 4 * s;
        // checks of the selected part's mesh
        Label(bx, yy, "MESH CHECK");
        yy += 24 * s;
        int oi = F.sel >= 0 && F.sel < (int)F.partObject.size() ? F.partObject[F.sel] : -1;
        if (oi < 0 || oi >= (int)F.scene.objects.size()) {
            Text(bx, yy, "Select a part with a mesh.", P().muted, 10);
            yy += 24 * s;
        } else {
            const MeshData& m = F.scene.meshes[F.scene.objects[oi].mesh];
            size_t degenerate = 0, zeroNormal = 0, outside = 0;
            bool anyUv = false;
            for (const Vertex& v : m.vertices) {
                if (dot(v.nrm, v.nrm) < 0.25f) ++zeroNormal;
                if (v.uv.x != 0 || v.uv.y != 0) anyUv = true;
                if (v.uv.x < -0.001f || v.uv.y < -0.001f || v.uv.x > 1.001f || v.uv.y > 1.001f) ++outside;
            }
            for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
                const float3 &a0 = m.vertices[m.indices[t]].pos, &a1 = m.vertices[m.indices[t + 1]].pos, &a2 = m.vertices[m.indices[t + 2]].pos;
                if (length(cross(a1 - a0, a2 - a0)) < 1e-9f) ++degenerate;
            }
            float3 d = (F.meshMax[F.scene.objects[oi].mesh] - F.meshMin[F.scene.objects[oi].mesh]) * F.world[F.sel].scale;
            auto line = [&](bool ok, const std::string& t) {
                Glyph(ok ? Icon::Check : Icon::Info, bx, yy + 1 * s, 12, ok ? kGreen : kAmber);
                Text(bx + 20 * s, yy, t, ok ? P().text : kAmber, 10.5f);
                yy += 21 * s;
            };
            char b[128];
            snprintf(b, sizeof(b), "%zu triangles, %zu vertices", m.triangleCount(), m.vertices.size());
            line(true, b);
            snprintf(b, sizeof(b), "Size %.2f x %.2f x %.2f cells", std::fabs(d.x), std::fabs(d.y), std::fabs(d.z));
            line(true, b);
            line(anyUv, anyUv ? "Has UVs" : "No UVs: textures will not map");
            snprintf(b, sizeof(b), outside ? "%zu UVs outside 0..1 (tiling)" : "UVs inside 0..1", outside);
            line(outside == 0, b);
            snprintf(b, sizeof(b), degenerate ? "%zu degenerate triangles" : "No degenerate triangles", degenerate);
            line(degenerate == 0, b);
            snprintf(b, sizeof(b), zeroNormal ? "%zu vertices without normal" : "Normals OK", zeroNormal);
            line(zeroNormal == 0, b);
            yy += 4 * s;
            if (Button("pfOpenUv", {bx, yy, bw, 26 * s}, "Open UV Editor", Icon::Grid, F.centerTab == 1)) F.centerTab = 1;
            yy += 36 * s;
        }
    } else {
        // ------------------------------------------------------------ Asset: name, RTS data, clips
        Label(bx, yy, "ASSET");
        yy += 24 * s;
        {
            char name[96];
            snprintf(name, sizeof(name), "%s", A.name.c_str());
            Text(bx, yy, "Name", P().muted, 11);
            yy += 19 * s;
            if (field("##pfAssetName", name, sizeof(name), yy) && *name) { prefabPushUndo(); A.name = name; }
            yy += 36 * s;
            int cat = 0;
            for (int i = 0; i < 5; ++i) if (A.game.category == kCategories[i]) cat = i;
            Text(bx, yy, "Category", P().muted, 11);
            yy += 19 * s;
            if (Combo("pfCat", {bx, yy, bw, 26 * s}, cat, kCategories, 5, Icon::Filter)) { prefabPushUndo(); A.game.category = kCategories[cat]; }
            yy += 36 * s;
        }
        Label(bx, yy, "RTS");
        yy += 24 * s;
        {
            PrefabGameData g = A.game;
            bool a5 = false, ch = false;
            float fx = (float)g.footprintX, fz = (float)g.footprintZ;
            ch |= SliderRow("##pfFootX", "Footprint X (cells)", bx, yy, bw, &fx, 1, 12, "%.0f", a5);
            ch |= SliderRow("##pfFootZ", "Footprint Z (cells)", bx, yy, bw, &fz, 1, 12, "%.0f", a5);
            ch |= SliderRow("##pfSelR", "Selection radius", bx, yy, bw, &g.selectionRadius, 0.1f, 8, "%.2f", a5);
            ch |= SliderRow("##pfH", "Height (health bar)", bx, yy, bw, &g.height, 0.1f, 10, "%.2f", a5);
            ImGui::SetCursorScreenPos(ImVec2(bx, yy));
            bool bp = g.blocksPath;
            if (Check("Blocks the path##pfBlocks", &bp)) { a5 = true; ch = true; g.blocksPath = bp; }
            Tip("Units walk around it (the cells of the footprint are not walkable)");
            yy += 32 * s;
            g.footprintX = (int)std::lround(fx);
            g.footprintZ = (int)std::lround(fz);
            if (a5) prefabPushUndo();
            if (ch) A.game = g;
        }
        Label(bx, yy, "CLIPS");
        yy += 24 * s;
        for (int i = 0; i < (int)A.clips.size(); ++i) {
            PrefabClip& c = A.clips[i];
            ImGui::PushID(i);
            R row = {bx, yy, bw, 26 * s};
            if (Hit("clipRow", row)) { F.clip = i; F.time = std::min(F.time, c.length); F.keyTrack = F.keyIndex = -1; }
            if (i == F.clip) SelectedRow(row);
            else if (ImGui::IsItemHovered()) Box(row, P().hover, 3);
            Glyph(Icon::Animation, bx + 8 * s, yy + 6 * s, 13, i == F.clip ? P().accent : P().muted);
            Text(bx + 28 * s, yy + 6 * s, c.name, P().text, 11);
            char info[48];
            snprintf(info, sizeof(info), "%.2f s%s", c.length, c.loop ? " · loop" : "");
            Text(bx + bw - TextSize(info, 9).x - 8 * s, yy + 8 * s, info, P().muted, 9);
            ImGui::PopID();
            yy += 29 * s;
        }
        {
            float b3 = (bw - 10 * s) / 3;
            if (Button("pfClipAdd", {bx, yy, b3, 25 * s}, "New", Icon::Plus)) {
                prefabPushUndo();
                PrefabClip c;
                c.name = "Clip " + std::to_string(A.clips.size() + 1);
                A.clips.push_back(c);
                F.clip = (int)A.clips.size() - 1;
                F.time = 0;
            }
            if (Button("pfClipDup", {bx + b3 + 5 * s, yy, b3, 25 * s}, "Copy", Icon::Link, false, false, clip != nullptr)) {
                prefabPushUndo();
                PrefabClip c = *clip;
                c.name += " Copy";
                A.clips.push_back(c);
                F.clip = (int)A.clips.size() - 1;
            }
            if (Button("pfClipDel", {bx + 2 * (b3 + 5 * s), yy, b3, 25 * s}, "Delete", Icon::Trash, false, false, clip != nullptr)) {
                prefabPushUndo();
                A.clips.erase(A.clips.begin() + F.clip);
                F.clip = std::min(F.clip, (int)A.clips.size() - 1);
                F.keyTrack = F.keyIndex = -1;
                clip = prefabClip();
            }
            yy += 35 * s;
        }
        if (clip) {
            char name[64];
            snprintf(name, sizeof(name), "%s", clip->name.c_str());
            Text(bx, yy, "Clip name", P().muted, 11);
            yy += 19 * s;
            if (field("##pfClipName", name, sizeof(name), yy) && *name) { prefabPushUndo(); clip->name = name; }
            yy += 36 * s;
            bool a6 = false;
            float len = clip->length, fps = clip->fps;
            SliderRow("##pfLen", "Length (s)", bx, yy, bw, &len, 0.1f, 20, "%.2f", a6);
            SliderRow("##pfFps", "Frames per second", bx, yy, bw, &fps, 6, 60, "%.0f", a6);
            if (a6) prefabPushUndo();
            clip->length = std::max(0.05f, len);
            clip->fps = std::round(std::clamp(fps, 1.0f, 120.0f));
            ImGui::SetCursorScreenPos(ImVec2(bx, yy));
            bool loop = clip->loop;
            if (Check("Loop##pfLoop", &loop)) { prefabPushUndo(); clip->loop = loop; }
            yy += 34 * s;
        }
        Label(bx, yy, "FILE");
        yy += 24 * s;
        {
            std::string f = F.path.empty() ? std::string("Not saved yet") : toUtf8(F.path);
            Box({bx, yy, bw, 26 * s}, P().bg, 3, true);
            dl->PushClipRect(ImVec2(bx, yy), ImVec2(bx + bw - 4 * s, yy + 26 * s), true);
            Text(bx + 8 * s, yy + 6 * s, f, P().muted, 9.5f);
            dl->PopClipRect();
            yy += 34 * s;
            float b2 = (bw - 5 * s) / 2;
            if (Button("pfSaveAs", {bx, yy, b2, 25 * s}, "Save As…", Icon::Save)) prefabSave(true);
            if (Button("pfDemo", {bx + b2 + 5 * s, yy, b2, 25 * s}, "Sample", Icon::Cube)) prefabNew(true);
            Tip("Load the sample prefab (Outpost: house mesh, flag pole, crates, Idle and Build clips)");
            yy += 36 * s;
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(bx, yy));
    ImGui::Dummy(ImVec2(1, 8 * s));
    ImGui::EndChild();
    dl = ImGui::GetWindowDrawList();
}

// --------------------------------------------------------------------------- timeline

void App::pfTimeline(float x, float y, float w, float h) {
    PrefabStudio& F = m_pf;
    PrefabAsset& A = F.asset;
    R r = {x, y, w, h};
    Panel(r, Icon::Animation, "Timeline", "", P().accent);
    PrefabClip* clip = prefabClip();
    float px = r.x + 10 * s, py = r.y + 40 * s, pw = r.w - 20 * s;
    // toolbar
    {
        std::vector<std::string> names{"Rest Pose"};
        for (auto& c : A.clips) names.push_back(c.name);
        std::vector<const char*> opts;
        for (auto& n : names) opts.push_back(n.c_str());
        int cur = clip ? F.clip + 1 : 0;
        if (Combo("pfClipCombo", {px, py, 150 * s, 26 * s}, cur, opts.data(), (int)opts.size(), Icon::Animation)) {
            F.clip = cur - 1;
            F.keyTrack = F.keyIndex = -1;
            if (clip = prefabClip(); clip) F.time = std::min(F.time, clip->length);
        }
        Tip("Clip shown and edited (Rest Pose: the parts without animation)");
        float bx = px + 156 * s;
        if (Button("pfStart", {bx, py, 28 * s, 26 * s}, "", Icon::ArrowLeft, false, true, clip != nullptr)) prefabSetTime(0);
        Tip("Go to the start (Home)");
        bx += 31 * s;
        if (Button("pfPlay", {bx, py, 28 * s, 26 * s}, "", F.playing ? Icon::Pause : Icon::Play, F.playing, true, clip != nullptr)) {
            if (clip && !F.playing && !clip->loop && F.time >= clip->length - 1e-4f) F.time = 0;
            F.playing = !F.playing;
        }
        Tip("Play / pause (Space)");
        bx += 31 * s;
        if (Button("pfEnd", {bx, py, 28 * s, 26 * s}, "", Icon::ArrowRight, false, true, clip != nullptr)) prefabSetTime(clip->length);
        Tip("Go to the end (End)");
        bx += 34 * s;
        if (clip) {
            if (Button("pfLoopBtn", {bx, py, 62 * s, 26 * s}, "Loop", Icon::Refresh, clip->loop)) { prefabPushUndo(); clip->loop = !clip->loop; }
            Tip("The clip loops (Idle, Walk) or plays once (Build, Death)");
            bx += 66 * s;
            static const float speeds[] = {0.25f, 0.5f, 1.0f, 2.0f};
            static const char* speedNames[] = {"0.25x", "0.5x", "1x", "2x"};
            int sp = 2;
            for (int i = 0; i < 4; ++i) if (std::fabs(F.speed - speeds[i]) < 1e-3f) sp = i;
            if (Combo("pfSpeed", {bx, py, 74 * s, 26 * s}, sp, speedNames, 4, Icon::Play)) F.speed = speeds[sp];
            Tip("Playback speed");
            bx += 80 * s;
            dl->AddLine(ImVec2(bx, py + 4 * s), ImVec2(bx, py + 22 * s), P().border);
            bx += 6 * s;
            const bool rigKey = F.centerTab == 2 && F.selJoint >= 0 && F.selJoint < (int)A.joints.size();
            const bool has = rigKey || (F.sel >= 0 && F.sel < (int)A.parts.size());
            if (Button("pfKeyBtn", {bx, py, 64 * s, 26 * s}, "Key", Icon::Pin, false, false, has)) { if (rigKey) prefabKeyJoint(); else prefabKeySelected(); }
            Tip("Key the position, rotation and scale of the selected part at this frame (K)");
            bx += 68 * s;
            // auto key: red when on
            R ak = {bx, py, 86 * s, 26 * s};
            if (Hit("pfAutoKey", ak)) F.autoKey = !F.autoKey;
            Tip("Auto Key: any transform edit (inspector, gizmo) sets a key at the current frame");
            Box(ak, F.autoKey ? IM_COL32(170, 40, 40, 255) : (ImGui::IsItemHovered() ? P().hover : P().raised), 4, true);
            dl->AddCircleFilled(ImVec2(ak.x + 13 * s, ak.y + 13 * s), 4.5f * s, F.autoKey ? IM_COL32(255, 220, 220, 255) : IM_COL32(200, 70, 70, 255));
            Text(ak.x + 24 * s, ak.y + 6 * s, "Auto Key", F.autoKey ? IM_COL32(255, 240, 240, 255) : P().text, 11);
            bx += 92 * s;
            char t[64];
            int frame = (int)std::lround(F.time * clip->fps), frames = (int)std::lround(clip->length * clip->fps);
            snprintf(t, sizeof(t), "%.2f s  ·  frame %d / %d", F.time, frame, frames);
            float tw2 = TextSize(t, 11).x;
            if (bx + tw2 < px + pw - 220 * s) Text(px + pw - 220 * s - tw2 - 8 * s, py + 6 * s, t, P().text, 11);
            else Text(bx + 4 * s, py + 6 * s, t, P().text, 11);
        } else {
            Text(bx, py + 6 * s, "Rest pose: choose a clip, or create one in the Asset tab.", P().muted, 11);
        }
        py += 34 * s;
    }
    if (!clip) return;

    // rows: the selected part (its three channels) and every part with tracks (its animated channels)
    struct Row { int part; int channel; int track; bool joint = false; };   // channel -1 = summary row; joint: part = joint index
    std::vector<Row> rows;
    std::vector<int> order;
    std::function<void(uint32_t)> walk = [&](uint32_t parent) {
        for (int i : A.childrenOf(parent)) { order.push_back(i); if (order.size() < 4096) walk(A.parts[i].id); }
    };
    walk(0);
    for (int i : order) {
        const PrefabPart& p = A.parts[i];
        bool any = false;
        for (const PrefabTrack& t : clip->tracks) any |= !t.joint && t.part == p.id && !t.keys.empty();
        if (!any && i != F.sel) continue;
        rows.push_back({i, -1, -1});
        for (int ch = 0; ch < CH_COUNT; ++ch) {
            int ti = -1;
            for (size_t k = 0; k < clip->tracks.size(); ++k)
                if (!clip->tracks[k].joint && clip->tracks[k].part == p.id && clip->tracks[k].channel == ch) ti = (int)k;
            if (ti >= 0 || i == F.sel) rows.push_back({i, ch, ti});
        }
    }
    // joints (Rig module): the selected joint and every joint with tracks, in skeleton order
    {
        std::vector<int> jorder;
        std::function<void(uint32_t)> jwalk = [&](uint32_t parent) {
            for (int i : A.jointChildren(parent)) { jorder.push_back(i); if (jorder.size() < 4096) jwalk(A.joints[i].id); }
        };
        jwalk(0);
        const int selJ = F.centerTab == 2 ? F.selJoint : -1;
        for (int i : jorder) {
            const PrefabJoint& jn = A.joints[i];
            bool any = false;
            for (const PrefabTrack& t : clip->tracks) any |= t.joint && t.part == jn.id && !t.keys.empty();
            if (!any && i != selJ) continue;
            rows.push_back({i, -1, -1, true});
            for (int ch = 0; ch < CH_COUNT; ++ch) {
                int ti = -1;
                for (size_t k = 0; k < clip->tracks.size(); ++k)
                    if (clip->tracks[k].joint && clip->tracks[k].part == jn.id && clip->tracks[k].channel == ch) ti = (int)k;
                if (ti >= 0 || i == selJ) rows.push_back({i, ch, ti, true});
            }
        }
    }
    const float nameW = 170 * s, rowH = 22 * s, rulerH = 24 * s;
    const float ax = px + nameW, aw = pw - nameW - 10 * s;   // time area
    const float pps = aw / clip->length;                      // pixels per second
    auto timeX = [&](float t) { return ax + t * pps; };
    auto xTime = [&](float sx) { return clampf((sx - ax) / pps, 0, clip->length); };
    auto snap = [&](float t) { return std::round(t * clip->fps) / clip->fps; };
    // ruler (click / drag: scrub)
    R ruler = {ax, py, aw, rulerH};
    Box({px, py, pw, rulerH}, P().bg, 3, true);
    Text(px + 8 * s, py + 5 * s, std::to_string(rows.empty() ? 0 : (int)rows.size()) + " rows", P().muted, 9.5f);
    if (Hit("pfRuler", ruler)) {}
    if (ImGui::IsItemActive()) { F.scrubbing = true; F.playing = false; F.time = snap(xTime(ImGui::GetMousePos().x)); }
    else F.scrubbing = false;
    {
        // ticks: every frame when there is room, labels on whole / half seconds
        const float fpx = pps / clip->fps;
        int step = fpx >= 6 * s ? 1 : fpx >= 3 * s ? 2 : 5;
        int frames = (int)std::lround(clip->length * clip->fps);
        for (int f = 0; f <= frames; f += step) {
            float tx = timeX(f / clip->fps);
            bool major = f % (int)clip->fps == 0, half = (f * 2) % (int)clip->fps == 0;
            dl->AddLine(ImVec2(tx, py + rulerH - (major ? 10 : half ? 7 : 4) * s), ImVec2(tx, py + rulerH), major ? P().text : P().muted, 1.0f);
            if (major || (half && pps > 120 * s)) {
                char b[16];
                snprintf(b, sizeof(b), major ? "%.0fs" : "%.1f", f / clip->fps);
                Text(tx + 3 * s, py + 3 * s, b, major ? P().text : P().muted, 9);
            }
        }
    }
    py += rulerH + 2 * s;
    // track rows (scrollable)
    ImGui::SetCursorScreenPos(ImVec2(px, py));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("pfTracks", ImVec2(pw, std::max(10.0f, r.y + r.h - py - 9 * s)), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImGui::SetWindowFontScale(atelier::Style().layoutScale);
    ImGui::PopStyleVar();
    dl = ImGui::GetWindowDrawList();
    float yy = ImGui::GetCursorScreenPos().y;
    const float top = yy;
    const ImVec2 mouse = ImGui::GetMousePos();
    int hoverTrack = -1, hoverKey = -1;
    bool keyClicked = false;
    for (size_t ri = 0; ri < rows.size(); ++ri) {
        const Row& row = rows[ri];
        const bool isJ = row.joint;
        const uint32_t itemId = isJ ? A.joints[row.part].id : A.parts[row.part].id;
        const std::string& itemName = isJ ? A.joints[row.part].name : A.parts[row.part].name;
        ImGui::PushID((int)ri);
        R nr = {px, yy, nameW - 6 * s, rowH - 1 * s};
        R tr = {ax, yy, aw, rowH - 1 * s};
        Box({px, yy, pw, rowH - 1 * s}, ri % 2 ? Fade(P().panel, 255) : Fade(P().bg, 160), 0);
        if (row.channel < 0) {
            if (Hit("partRow", nr)) { if (isJ) F.selJoint = row.part; else F.sel = row.part; }
            const bool selected = isJ ? row.part == F.selJoint : row.part == F.sel;
            if (isJ) Glyph(Icon::Link, px + 4 * s, yy + 4 * s, 11, IM_COL32(150, 200, 255, 255));
            TextBold(px + (isJ ? 20 : 6) * s, yy + 4 * s, Fit(itemName, nameW - 26 * s, 11), selected ? P().accent : (isJ ? IM_COL32(170, 210, 255, 255) : P().text), 11);
            // summary: every key time of the part
            std::vector<float> times;
            for (const PrefabTrack& t : clip->tracks)
                if (t.joint == isJ && t.part == itemId)
                    for (const PrefabKey& k : t.keys) times.push_back(k.time);
            for (float t : times) {
                ImVec2 c(timeX(t), yy + rowH / 2);
                float k = 3.5f * s;
                dl->AddQuadFilled(ImVec2(c.x, c.y - k), ImVec2(c.x + k, c.y), ImVec2(c.x, c.y + k), ImVec2(c.x - k, c.y), P().muted);
            }
        } else {
            static const ImU32 chCol[3] = {IM_COL32(222, 110, 121, 255), IM_COL32(136, 199, 150, 255), IM_COL32(113, 168, 233, 255)};
            dl->AddRectFilled(ImVec2(px + 14 * s, yy + 6 * s), ImVec2(px + 17 * s, yy + rowH - 7 * s), chCol[row.channel]);
            Text(px + 24 * s, yy + 4 * s, channelName(row.channel), P().text, 10.5f);
            // key area: double-click adds a key with the current pose value; keys: click / drag / right-click
            if (Hit("keyArea", tr)) {}
            const bool areaHovered = ImGui::IsItemHovered();
            if (areaHovered && ImGui::IsMouseDoubleClicked(0)) {
                prefabPushUndo();
                if (isJ) F.selJoint = row.part; else F.sel = row.part;
                float t = snap(xTime(mouse.x));
                std::vector<PartPose> pose;
                if (isJ) evaluateJoints(A, clip, t, pose);
                else evaluatePose(A, clip, t, pose);
                const PartPose& pp = pose[row.part];
                float3 v = row.channel == CH_POSITION ? pp.position : row.channel == CH_ROTATION ? pp.rotation : pp.scale;
                (isJ ? clip->jointTrack(itemId, row.channel, true) : clip->track(itemId, row.channel, true))->setKey(t, v, INTERP_SMOOTH, 0.5f / clip->fps);
                F.time = t;
            }
            if (row.track >= 0 && row.track < (int)clip->tracks.size()) {
                const PrefabTrack& t = clip->tracks[row.track];
                // curve hint: a thin line between keys
                for (size_t k = 0; k + 1 < t.keys.size(); ++k)
                    dl->AddLine(ImVec2(timeX(t.keys[k].time), yy + rowH / 2), ImVec2(timeX(t.keys[k + 1].time), yy + rowH / 2),
                                t.keys[k].interp == INTERP_STEP ? Fade(chCol[row.channel], 60) : Fade(chCol[row.channel], 130), (t.keys[k].interp == INTERP_STEP ? 1.0f : 2.0f) * s);
                for (size_t k = 0; k < t.keys.size(); ++k) {
                    ImVec2 c(timeX(t.keys[k].time), yy + rowH / 2);
                    float kk = 5.5f * s;
                    bool hot = std::fabs(mouse.x - c.x) < kk + 2 * s && std::fabs(mouse.y - c.y) < kk + 2 * s && ImGui::IsWindowHovered();
                    bool selected = F.keyTrack == row.track && F.keyIndex == (int)k;
                    if (hot) { hoverTrack = row.track; hoverKey = (int)k; }
                    ImU32 fill = selected ? IM_COL32(255, 214, 120, 255) : hot ? IM_COL32(255, 240, 200, 255) : chCol[row.channel];
                    if (t.keys[k].interp == INTERP_STEP)
                        dl->AddRectFilled(ImVec2(c.x - kk * 0.8f, c.y - kk * 0.8f), ImVec2(c.x + kk * 0.8f, c.y + kk * 0.8f), fill);
                    else
                        dl->AddQuadFilled(ImVec2(c.x, c.y - kk), ImVec2(c.x + kk, c.y), ImVec2(c.x, c.y + kk), ImVec2(c.x - kk, c.y), fill);
                    if (selected) dl->AddQuad(ImVec2(c.x, c.y - kk - 2 * s), ImVec2(c.x + kk + 2 * s, c.y), ImVec2(c.x, c.y + kk + 2 * s), ImVec2(c.x - kk - 2 * s, c.y), IM_COL32(255, 255, 255, 200), 1.2f * s);
                }
            }
        }
        ImGui::PopID();
        yy += rowH;
    }
    if (rows.empty()) Text(px + 8 * s, yy + 8 * s, "Select a part, then Key (K) or double-click a channel row to add a key.", P().muted, 10.5f);
    // key interaction (after the rows so the hover is known)
    if (hoverKey >= 0 && ImGui::IsMouseClicked(0)) {
        F.keyTrack = hoverTrack;
        F.keyIndex = hoverKey;
        F.keyDrag = true;
        F.keyDragStart = clip->tracks[hoverTrack].keys[hoverKey].time;
        F.time = F.keyDragStart;
        F.playing = false;
        const PrefabTrack& ht = clip->tracks[hoverTrack];
        if (ht.joint) F.selJoint = A.findJoint(ht.part);
        else F.sel = A.findPart(ht.part);
        prefabPushUndo();
        keyClicked = true;
    }
    if (F.keyDrag) {
        if (!ImGui::IsMouseDown(0)) {
            F.keyDrag = false;
            if (F.keyTrack >= 0 && F.keyTrack < (int)clip->tracks.size()) {
                PrefabTrack& t = clip->tracks[F.keyTrack];
                // the moved key may now sit on another one: keep the moved one
                if (F.keyIndex >= 0 && F.keyIndex < (int)t.keys.size()) {
                    PrefabKey moved = t.keys[F.keyIndex];
                    t.keys.erase(t.keys.begin() + F.keyIndex);
                    int other = t.keyAt(moved.time, 0.5f / clip->fps);
                    if (other >= 0) t.keys.erase(t.keys.begin() + other);
                    t.keys.push_back(moved);
                    t.sortKeys();
                    F.keyIndex = t.keyAt(moved.time, 1e-5f);
                }
            }
        } else if (!keyClicked && F.keyTrack >= 0 && F.keyTrack < (int)clip->tracks.size()) {
            PrefabTrack& t = clip->tracks[F.keyTrack];
            if (F.keyIndex >= 0 && F.keyIndex < (int)t.keys.size()) {
                float nt = snap(xTime(mouse.x));
                t.keys[F.keyIndex].time = nt;
                F.time = nt;
                PrefabKey moved = t.keys[F.keyIndex];
                t.sortKeys();
                for (size_t k = 0; k < t.keys.size(); ++k)
                    if (t.keys[k].time == moved.time && t.keys[k].value.x == moved.value.x && t.keys[k].value.y == moved.value.y && t.keys[k].value.z == moved.value.z) F.keyIndex = (int)k;
            }
        }
    }
    if (hoverKey >= 0 && ImGui::IsMouseClicked(1)) {
        F.keyTrack = hoverTrack;
        F.keyIndex = hoverKey;
        ImGui::OpenPopup("pfKeyMenu");
    }
    if (ImGui::BeginPopup("pfKeyMenu")) {
        ImGui::SetWindowFontScale(1.4f * ts());
        if (F.keyTrack >= 0 && F.keyTrack < (int)clip->tracks.size() && F.keyIndex >= 0 && F.keyIndex < (int)clip->tracks[F.keyTrack].keys.size()) {
            PrefabTrack& t = clip->tracks[F.keyTrack];
            PrefabKey& k = t.keys[F.keyIndex];
            ImGui::TextDisabled("%s · %.2f s", channelName(t.channel), k.time);
            for (int i = 0; i < 3; ++i)
                if (ImGui::MenuItem(interpName(i), nullptr, k.interp == i)) { prefabPushUndo(); k.interp = i; }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete Key", "Del")) {
                prefabPushUndo();
                t.keys.erase(t.keys.begin() + F.keyIndex);
                if (t.keys.empty()) clip->tracks.erase(clip->tracks.begin() + F.keyTrack);
                F.keyTrack = F.keyIndex = -1;
            }
        }
        ImGui::EndPopup();
    }
    // playhead over the rows and the ruler
    {
        ImDrawList* fg = ImGui::GetWindowDrawList();
        float hx = timeX(F.time);
        fg->AddLine(ImVec2(hx, top), ImVec2(hx, std::max(yy, ImGui::GetWindowPos().y + ImGui::GetWindowHeight())), P().accent, 1.6f * s);
    }
    ImGui::SetCursorScreenPos(ImVec2(px, yy));
    ImGui::Dummy(ImVec2(1, 4 * s));
    ImGui::EndChild();
    dl = ImGui::GetWindowDrawList();
    {
        float hx = timeX(F.time);
        dl->AddLine(ImVec2(hx, r.y + 74 * s), ImVec2(hx, r.y + 74 * s + rulerH), P().accent, 1.6f * s);
        dl->AddTriangleFilled(ImVec2(hx - 6 * s, r.y + 74 * s), ImVec2(hx + 6 * s, r.y + 74 * s), ImVec2(hx, r.y + 82 * s), P().accent);
    }
}

// --------------------------------------------------------------------------- UV Editor (view only)
// The UVs of the selected part, large: UDIM tiles (0-1 = 1001, the other valid tiles greyed, outside the UDIM range
// hatched), checks of the set (fills), a second set in overlay or side by side, and the check table on the right.
// Pan: right / middle drag, zoom: wheel, F: frame the UVs, Home: frame the 0-1 square.

namespace {
const char* kUvCompare[] = {"One Set", "Overlay", "Side by Side"};
const char* kUvFill[] = {"Wireframe", "Islands", "Overlap", "Flipped", "Texel Density", "Stretch"};
const char* kUvFillTips[] = {"Edges of the UV triangles (seams once per side)",
                             "One colour per UV island (a connected shell)",
                             "Red: triangles that share texels with another one (normal for mirrored or stacked shells)",
                             "Red: mirrored triangles (wound the other way), orange: zero-area triangles",
                             "Texels per scene unit compared with the median: blue = less, green = even, red = more",
                             "Distortion of the mapping: green = none, yellow = 1.5x, red = 3x and more"};
const char* kUvBackground[] = {"No Background", "Checker", "Texture"};
const ImU32 kSetB = IM_COL32(255, 150, 60, 230);

ImU32 ramp(float t, int alpha) {   // 0 blue, 0.5 green, 1 red
    t = clampf(t, 0, 1);
    float r = t < 0.5f ? 0.15f : (t - 0.5f) * 2.0f, g = t < 0.5f ? 0.3f + t * 1.3f : 0.95f - (t - 0.5f) * 1.5f, b = t < 0.5f ? 1.0f - t * 1.8f : 0.1f;
    return IM_COL32((int)(clampf(r, 0, 1) * 255), (int)(clampf(g, 0, 1) * 255), (int)(clampf(b, 0, 1) * 255), alpha);
}
}

void App::pfUvEditor(float x, float y, float w, float h) {
    PrefabStudio& F = m_pf;
    const float tbH = 34 * s;
    Box({x, y, w, tbH}, P().panel, 4, true);
    float bx = x + 6 * s, by = y + 5 * s;
    pfCenterTabs(F.centerTab, bx, by);
    const int part = F.sel >= 0 && F.sel < (int)F.partUv.size() && !F.partUv[F.sel].empty() ? F.sel : -1;
    const int nsets = part >= 0 ? (int)F.partUv[part].size() : 0;
    if (nsets) {   // (no clamp before the scene exists: the sets of the command line stay)
        F.uvSetA = std::clamp(F.uvSetA, 0, nsets - 1);
        F.uvSetB = std::clamp(F.uvSetB, 0, nsets - 1);
    }
    std::vector<std::string> names;
    for (int i = 0; i < nsets; ++i) names.push_back(std::to_string(i) + "  " + F.partUv[part][i].name);
    std::vector<const char*> opts;
    for (auto& n : names) opts.push_back(n.c_str());
    if (nsets) {
        Combo("uvSetA", {bx, by, 150 * s, 24 * s}, F.uvSetA, opts.data(), nsets, Icon::Grid);
        Tip("UV set shown (A)");
        bx += 154 * s;
        Combo("uvCompare", {bx, by, 132 * s, 24 * s}, F.uvCompare, kUvCompare, 3, Icon::Link);
        Tip("Compare with a second UV set (B, orange): drawn over A, or in a second view");
        bx += 136 * s;
        if (F.uvCompare) {
            Combo("uvSetB", {bx, by, 150 * s, 24 * s}, F.uvSetB, opts.data(), nsets, Icon::Grid);
            Tip("Second UV set (B)");
            bx += 154 * s;
        }
    }
    Combo("uvFill", {bx, by, 136 * s, 24 * s}, F.uvFill, kUvFill, 6, Icon::Filter);
    Tip(kUvFillTips[F.uvFill]);
    bx += 140 * s;
    Combo("uvBg", {bx, by, 136 * s, 24 * s}, F.uvBackground, kUvBackground, 3, Icon::Material);
    Tip("Background of the 0-1 square: none, checker or the base color texture of the part");
    float rx = x + w - 6 * s - 60 * s;
    if (Button("uvFrameAll", {rx, by, 60 * s, 24 * s}, "All", Icon::Home)) F.uvFrame = 2;
    Tip("Frame all the UVs (F)");
    rx -= 64 * s;
    if (Button("uvFrame01", {rx, by, 60 * s, 24 * s}, "0-1", Icon::Grid)) F.uvFrame = 1;
    Tip("Frame the 0-1 square (Home)");

    const float side = std::min(300 * s, w * 0.36f);
    const float cy = y + tbH + 4 * s, ch = h - tbH - 4 * s, cw = w - side - 6 * s;
    if (part < 0) {
        Box({x, cy, cw, ch}, P().canvas, 4, true);
        Center({x, cy, cw, ch}, "Select a part with a mesh (Parts list or 3D view) to see its UVs.", P().muted, 12);
    } else if (F.uvCompare == 2) {
        const float half = (cw - 6 * s) / 2;
        pfUvCanvas(x, cy, half, ch, part, F.uvSetA, -1, "uvCanvasA");
        pfUvCanvas(x + half + 6 * s, cy, half, ch, part, F.uvSetB, -2, "uvCanvasB");
    } else {
        pfUvCanvas(x, cy, cw, ch, part, F.uvSetA, F.uvCompare == 1 ? F.uvSetB : -1, "uvCanvas");
    }
    pfUvSidebar(x + cw + 6 * s, cy, side, ch, part);
}

// other: -1 none, >= 0 a second set drawn over (overlay), -2 this canvas shows the set B (side by side: orange wire)
void App::pfUvCanvas(float x, float y, float w, float h, int part, int set, int other, const char* id) {
    PrefabStudio& F = m_pf;
    const PrefabPart& pp = F.asset.parts[part];
    const UvAnalysis& an = prefabUvAnalysis(part, set);
    const std::vector<float2>& uv = F.partUv[part][set].uv;
    const MeshData& mesh = F.scene.meshes[F.scene.objects[F.partObject[part]].mesh];
    Box({x, y, w, h}, P().canvas, 4, true);
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    ImGui::InvisibleButton(id, ImVec2(w, h), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    ImGuiIO& io = ImGui::GetIO();
    const float cx = x + w / 2, cyy = y + h / 2;
    // framing (first draw, buttons, F / Home)
    if (F.uvFrame || F.uvZoom <= 0) {
        float2 mn(0, 0), mx(1, 1);
        if (F.uvFrame == 2 && an.valid) {
            mn = float2(std::min(0.0f, an.uvMin.x), std::min(0.0f, an.uvMin.y));
            mx = float2(std::max(1.0f, an.uvMax.x), std::max(1.0f, an.uvMax.y));
        }
        float zx = w * 0.84f / std::max(1e-3f, mx.x - mn.x), zy = h * 0.84f / std::max(1e-3f, mx.y - mn.y);
        F.uvZoom = std::min(zx, zy);
        F.uvPan = float2((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
        F.uvFrame = 0;
    }
    auto toS = [&](float u, float v) { return ImVec2(cx + (u - F.uvPan.x) * F.uvZoom, cyy - (v - F.uvPan.y) * F.uvZoom); };
    auto toUv = [&](float sx, float sy) { return float2(F.uvPan.x + (sx - cx) / F.uvZoom, F.uvPan.y - (sy - cyy) / F.uvZoom); };
    if (hovered && io.MouseWheel != 0) {
        float2 before = toUv(io.MousePos.x, io.MousePos.y);
        F.uvZoom = clampf(F.uvZoom * std::pow(1.18f, io.MouseWheel), 8.0f, 400000.0f);
        float2 after = toUv(io.MousePos.x, io.MousePos.y);
        F.uvPan = float2(F.uvPan.x + before.x - after.x, F.uvPan.y + before.y - after.y);
    }
    if (hovered && (ImGui::IsMouseClicked(1) || ImGui::IsMouseClicked(2) || (io.KeyAlt && ImGui::IsMouseClicked(0)))) F.uvPanning = true;
    if (F.uvPanning) {
        if (!(io.MouseDown[1] || io.MouseDown[2] || io.MouseDown[0])) F.uvPanning = false;
        else F.uvPan = float2(F.uvPan.x - io.MouseDelta.x / F.uvZoom, F.uvPan.y + io.MouseDelta.y / F.uvZoom);
    }
    dl->PushClipRect(ImVec2(x + 1, y + 1), ImVec2(x + w - 1, y + h - 1), true);

    // UDIM tiles: 1001 (the 0-1 square) lighter, the other valid tiles greyed, outside the UDIM range hatched
    const float2 lo = toUv(x, y + h), hi = toUv(x + w, y);
    const int tu0 = std::max(-12, (int)std::floor(lo.x)), tu1 = std::min(22, (int)std::floor(hi.x));
    const int tv0 = std::max(-12, (int)std::floor(lo.y)), tv1 = std::min(22, (int)std::floor(hi.y));
    const float tilePx = F.uvZoom;
    for (int tv = tv0; tv <= tv1; ++tv)
        for (int tu = tu0; tu <= tu1; ++tu) {
            ImVec2 a = toS((float)tu, (float)tv + 1), b = toS((float)tu + 1, (float)tv);
            int udim = udimTile(tu + 0.5f, tv + 0.5f);
            auto used = an.tiles.find(udim);
            const bool isUsed = udim > 0 && used != an.tiles.end();
            if (udim == 1001) dl->AddRectFilled(a, b, Fade(P().text, 30));
            else if (udim > 0) dl->AddRectFilled(a, b, Fade(P().text, 13));
            else if (tilePx > 18) {   // outside the UDIM range: hatching
                dl->PushClipRect(ImVec2(std::max(a.x, x), std::max(a.y, y)), ImVec2(std::min(b.x, x + w), std::min(b.y, y + h)), true);
                const float step = 14 * s;
                for (float d = -(b.y - a.y); d < (b.x - a.x); d += step) dl->AddLine(ImVec2(a.x + d, b.y), ImVec2(a.x + d + (b.y - a.y), a.y), Fade(P().border, 90), 1.0f);
                dl->PopClipRect();
            }
            if (isUsed && udim != 1001) dl->AddRectFilled(a, b, Fade(P().accent, 16));
            dl->AddRect(a, b, udim > 0 ? Fade(P().text, 55) : Fade(P().border, 160), 0, 0, 1.0f);
            if (tilePx > 46 && udim > 0) {
                char t[16];
                snprintf(t, sizeof(t), "%d", udim);
                Text(a.x + 5 * s, b.y - 17 * s, t, isUsed ? P().accent : Fade(P().muted, 200), 10);
                if (isUsed && tilePx > 130) {
                    char c[32];
                    snprintf(c, sizeof(c), "%d tris", used->second);
                    Text(a.x + 5 * s, b.y - 31 * s, c, Fade(P().accent, 170), 9);
                }
            }
        }
    // sub-grid (tenths) when zoomed in
    if (tilePx > 280) {
        int k0 = (int)std::floor(lo.x * 10), k1 = (int)std::ceil(hi.x * 10);
        for (int k = k0; k <= k1 && k1 - k0 < 400; ++k) if (k % 10) { ImVec2 a = toS(k / 10.0f, lo.y); dl->AddLine(ImVec2(a.x, y), ImVec2(a.x, y + h), Fade(P().text, 14)); }
        k0 = (int)std::floor(lo.y * 10); k1 = (int)std::ceil(hi.y * 10);
        for (int k = k0; k <= k1 && k1 - k0 < 400; ++k) if (k % 10) { ImVec2 a = toS(lo.x, k / 10.0f); dl->AddLine(ImVec2(x, a.y), ImVec2(x + w, a.y), Fade(P().text, 14)); }
    }
    // background of the 0-1 square
    {
        ImVec2 a = toS(0, 1), b = toS(1, 0);
        if (F.uvBackground == 1) {
            for (int j = 0; j < 8; ++j)
                for (int i = 0; i < 8; ++i)
                    if ((i + j) & 1) dl->AddRectFilled(toS(i / 8.0f, (j + 1) / 8.0f), toS((i + 1) / 8.0f, j / 8.0f), Fade(P().text, 22));
        } else if (F.uvBackground == 2 && !pp.texture.empty()) {
            if (ImTextureID t = prefabUvTexture(pp.texture)) dl->AddImage(t, a, b, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 170));
        }
        // the 0-1 square
        dl->AddRect(ImVec2(a.x - 1, a.y - 1), ImVec2(b.x + 1, b.y + 1), P().accent, 0, 0, 2.0f * s);
        TextBold(b.x - TextSize("0-1", 10.5f).x - 2 * s, a.y - 18 * s, "0-1", P().accent, 10.5f);
    }
    // axis values (tile borders) along the bottom and the left edge
    {
        int step = tilePx > 60 ? 1 : tilePx > 25 ? 2 : 5;
        for (int u = (int)std::ceil(lo.x); u <= (int)std::floor(hi.x); ++u)
            if (u % step == 0) { ImVec2 p = toS((float)u, 0); Text(p.x + 3 * s, y + h - 16 * s, std::to_string(u), P().muted, 9); }
        for (int v = (int)std::ceil(lo.y); v <= (int)std::floor(hi.y); ++v)
            if (v % step == 0) { ImVec2 p = toS(0, (float)v); Text(x + 5 * s, p.y - 13 * s, std::to_string(v), P().muted, 9); }
    }

    // fills (checks)
    const ImDrawListFlags oldFlags = dl->Flags;
    dl->Flags &= ~(ImDrawListFlags_AntiAliasedFill | (an.edges.size() > 20000 ? ImDrawListFlags_AntiAliasedLines : 0));
    const size_t T = an.valid ? (size_t)an.tris : 0;
    auto visible = [&](const ImVec2& a, const ImVec2& b, const ImVec2& c) {
        return !((a.x < x && b.x < x && c.x < x) || (a.x > x + w && b.x > x + w && c.x > x + w) || (a.y < y && b.y < y && c.y < y) || (a.y > y + h && b.y > y + h && c.y > y + h));
    };
    for (size_t t = 0; t < T; ++t) {
        ImU32 col = 0;
        switch (F.uvFill) {
        case 0: col = Fade(P().text, 18); break;
        case 1: { float hue = std::fmod(an.island[t] * 0.61803f, 1.0f); ImVec4 c; ImGui::ColorConvertHSVtoRGB(hue, 0.55f, 0.95f, c.x, c.y, c.z); col = IM_COL32((int)(c.x * 255), (int)(c.y * 255), (int)(c.z * 255), 120); break; }
        case 2: col = (an.flags[t] & UVF_OVERLAP) ? IM_COL32(235, 60, 60, 150) : Fade(P().text, 14); break;
        case 3: col = (an.flags[t] & UVF_FLIPPED) ? IM_COL32(235, 60, 60, 160) : (an.flags[t] & UVF_ZERO) ? IM_COL32(240, 150, 40, 200) : IM_COL32(90, 190, 110, 50); break;
        case 4: col = ramp(an.densityLog[t] * 0.25f + 0.5f, 140); break;
        case 5: col = ramp(0.5f + clampf((an.stretch[t] - 1.0f) / 4.0f, 0, 0.5f), 140); break;
        }
        if (!col) continue;
        const float2 &a = uv[mesh.indices[t * 3]], &b = uv[mesh.indices[t * 3 + 1]], &c = uv[mesh.indices[t * 3 + 2]];
        ImVec2 sa = toS(a.x, a.y), sb = toS(b.x, b.y), sc = toS(c.x, c.y);
        if (visible(sa, sb, sc)) dl->AddTriangleFilled(sa, sb, sc, col);
    }
    // wireframe: the set (B in orange when this canvas shows it), then the overlay set in orange
    auto wire = [&](const UvAnalysis& A, const std::vector<float2>& U, ImU32 col) {
        for (size_t e = 0; e + 1 < A.edges.size(); e += 2) {
            const float2 &a = U[A.edges[e]], &b = U[A.edges[e + 1]];
            ImVec2 sa = toS(a.x, a.y), sb = toS(b.x, b.y);
            if ((sa.x < x && sb.x < x) || (sa.x > x + w && sb.x > x + w) || (sa.y < y && sb.y < y) || (sa.y > y + h && sb.y > y + h)) continue;
            dl->AddLine(sa, sb, col, 1.0f);
        }
    };
    wire(an, uv, other == -2 ? kSetB : Fade(P().text, 215));
    if (other >= 0 && other < (int)F.partUv[part].size()) wire(prefabUvAnalysis(part, other), F.partUv[part][other].uv, kSetB);
    dl->Flags = oldFlags;

    // labels: the set(s) shown, the UV under the mouse
    {
        std::string label = (other == -2 ? "B  " : "A  ") + F.partUv[part][set].name;
        if (other >= 0) label += "   +   B  " + F.partUv[part][other].name;
        R lr = {x + 8 * s, y + 8 * s, TextSize(label, 10.5f).x + 18 * s, 22 * s};
        Box(lr, P().overlay, 4);
        Text(lr.x + 9 * s, lr.y + 5 * s, label, other == -2 ? kSetB : P().text, 10.5f);
        if (hovered) {
            float2 m = toUv(io.MousePos.x, io.MousePos.y);
            int tile = udimTile(m.x, m.y);
            char t[96];
            if (tile > 0) snprintf(t, sizeof(t), "U %.3f   V %.3f   ·   tile %d", m.x, m.y, tile);
            else snprintf(t, sizeof(t), "U %.3f   V %.3f   ·   outside the UDIM range", m.x, m.y);
            R mr = {x + 8 * s, y + h - 30 * s, TextSize(t, 10).x + 18 * s, 22 * s};
            Box(mr, P().overlay, 4);
            Text(mr.x + 9 * s, mr.y + 5 * s, t, P().text, 10);
        }
    }
    dl->PopClipRect();
}

void App::pfUvSidebar(float x, float y, float w, float h, int part) {
    PrefabStudio& F = m_pf;
    R r = {x, y, w, h};
    Panel(r, Icon::Check, "UV Check", "", P().accent);
    if (part < 0) { Text(r.x + 14 * s, r.y + 48 * s, "No part selected.", P().muted, 11); return; }
    const PrefabPart& pp = F.asset.parts[part];
    float px = r.x + 12 * s, pw = r.w - 24 * s;
    g_labelRight = px + pw - 8 * s;
    ImGui::SetCursorScreenPos(ImVec2(px, r.y + 40 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("uvSide", ImVec2(pw, r.h - 48 * s), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImGui::SetWindowFontScale(atelier::Style().layoutScale);
    ImGui::PopStyleVar();
    dl = ImGui::GetWindowDrawList();
    float yy = ImGui::GetCursorScreenPos().y;
    const float bw = pw - 10 * s;
    TextBold(px, yy, pp.name, P().text, 13);
    yy += 19 * s;
    Text(px, yy, Fit(pp.meshPath.empty() ? std::string("Primitive · ") + primitiveName(pp.prim.type) : pp.meshPath, bw, 9.5f), P().muted, 9.5f);
    yy += 24 * s;
    // UV sets: click = A, the B pill = second set
    const int nsets = (int)F.partUv[part].size();
    Label(px, yy, "UV SETS");
    yy += 22 * s;
    for (int i = 0; i < nsets; ++i) {
        ImGui::PushID(i);
        R row = {px, yy, bw, 24 * s};
        if (Hit("set", row)) F.uvSetA = i;
        if (i == F.uvSetA) SelectedRow(row);
        else if (ImGui::IsItemHovered()) Box(row, P().hover, 3);
        Text(px + 8 * s, yy + 5 * s, std::to_string(i) + "   " + F.partUv[part][i].name, P().text, 11);
        R pb = {px + bw - 26 * s, yy + 3 * s, 22 * s, 18 * s};
        const bool isB = F.uvCompare && i == F.uvSetB;
        if (Hit("b", pb)) { F.uvSetB = i; if (!F.uvCompare) F.uvCompare = 1; }
        Tip("Compare with this set (B)");
        Box(pb, isB ? kSetB : P().raised, 3, !isB);
        Center(pb, "B", isB ? IM_COL32(30, 20, 10, 255) : P().muted, 9.5f);
        if (i == F.uvSetA) { R pa = {pb.x - 26 * s, pb.y, 22 * s, 18 * s}; Box(pa, P().accent, 3); Center(pa, "A", IM_COL32(20, 20, 24, 255), 9.5f); }
        ImGui::PopID();
        yy += 27 * s;
    }
    if (nsets < 2) { Text(px, yy, "One UV set (OBJ files and primitives have one;", P().muted, 9.5f); yy += 15 * s; Text(px, yy, "glTF files can carry several).", P().muted, 9.5f); yy += 18 * s; }
    yy += 8 * s;
    // checks: A, and B in a second column when comparing
    const bool cmp = F.uvCompare != 0 && nsets > 1;
    const UvAnalysis& A = prefabUvAnalysis(part, F.uvSetA);
    const UvAnalysis* B = cmp ? &prefabUvAnalysis(part, F.uvSetB) : nullptr;
    Label(px, yy, "CHECKS");
    yy += 22 * s;
    const float colA = px + bw * (cmp ? 0.52f : 0.62f), colB = px + bw * 0.78f;
    if (cmp) {
        Text(colA, yy, "A", P().accent, 10);
        Text(colB, yy, "B", kSetB, 10);
        yy += 17 * s;
    }
    auto row = [&](const char* label, const std::function<std::string(const UvAnalysis&)>& value, const std::function<bool(const UvAnalysis&)>& ok, const char* tip) {
        R rr = {px, yy - 2 * s, bw, 20 * s};
        Hit(label, rr);
        Tip(tip);
        Text(px, yy, label, P().muted, 10.5f);
        Text(colA, yy, value(A), ok(A) ? P().text : kAmber, 10.5f);
        if (B) Text(colB, yy, value(*B), ok(*B) ? P().text : kAmber, 10.5f);
        yy += 20 * s;
    };
    auto num = [](int v) { return std::to_string(v); };
    auto pct = [](float v) { char b[16]; snprintf(b, sizeof(b), "%.1f %%", v * 100.0f); return std::string(b); };
    auto yes = [](const UvAnalysis&) { return true; };
    row("Triangles", [&](const UvAnalysis& a) { return num(a.tris); }, yes, "Triangles of the mesh");
    row("Islands", [&](const UvAnalysis& a) { return num(a.islands); }, yes, "UV shells (connected pieces)");
    row("UDIM tiles", [&](const UvAnalysis& a) { return num((int)a.tiles.size() - (a.tiles.count(-1) ? 1 : 0)); }, [](const UvAnalysis& a) { return !a.tiles.count(-1); }, "Tiles used (tile of each triangle centre)");
    row("Outside 0-1", [&](const UvAnalysis& a) { return num(a.outside01); }, yes, "Triangles with a vertex outside the 0-1 square (tiling or UDIM)");
    row("Out of UDIM", [&](const UvAnalysis& a) { return num(a.badTile); }, [](const UvAnalysis& a) { return a.badTile == 0; }, "Triangles in negative tiles or beyond u = 10: no UDIM tile can hold them");
    row("Flipped", [&](const UvAnalysis& a) { return num(a.flipped); }, [](const UvAnalysis& a) { return a.flipped == 0; }, "Mirrored triangles (wound the other way): wrong normal maps");
    row("Zero area", [&](const UvAnalysis& a) { return num(a.zeroArea); }, [](const UvAnalysis& a) { return a.zeroArea == 0; }, "Triangles collapsed in UV: no texel");
    row("Overlap", [&](const UvAnalysis& a) { return pct(a.overlap); }, [](const UvAnalysis& a) { return a.overlap < 0.02f; }, "Texels used by two triangles or more (fine for tiling, a problem for lightmaps and bakes)");
    row("Coverage 0-1", [&](const UvAnalysis& a) { return pct(a.coverage01); }, [](const UvAnalysis& a) { return a.coverage01 > 0.4f || a.coverage01 == 0; }, "Part of the 0-1 square covered by the UVs");
    row("Texel density", [&](const UvAnalysis& a) { char b[24]; snprintf(b, sizeof(b), "%.0f px", a.density); return std::string(b); }, yes, "Median pixels per scene unit (1 cell) with a 1024 texture");
    row("Density spread", [&](const UvAnalysis& a) { char b[24]; snprintf(b, sizeof(b), "x %.2f", a.densitySpread); return std::string(b); }, [](const UvAnalysis& a) { return a.densitySpread < 2.0f; }, "90th / 10th percentile of the density: 1 = even texel size everywhere");
    row("Stretch avg", [&](const UvAnalysis& a) { char b[24]; snprintf(b, sizeof(b), "%.2f", a.stretchAvg); return std::string(b); }, [](const UvAnalysis& a) { return a.stretchAvg < 1.25f; }, "Mean distortion (area weighted): 1 = none");
    row("Stretch max", [&](const UvAnalysis& a) { char b[24]; snprintf(b, sizeof(b), "%.2f", a.stretchMax); return std::string(b); }, [](const UvAnalysis& a) { return a.stretchMax < 3.0f; }, "Worst triangle distortion");
    // difference between the two sets
    if (B) {
        const std::vector<float2>& ua = F.partUv[part][F.uvSetA].uv;
        const std::vector<float2>& ub = F.partUv[part][F.uvSetB].uv;
        size_t diff = 0;
        for (size_t i = 0; i < ua.size() && i < ub.size(); ++i) diff += std::fabs(ua[i].x - ub[i].x) > 1e-5f || std::fabs(ua[i].y - ub[i].y) > 1e-5f;
        yy += 6 * s;
        Label(px, yy, "A / B");
        yy += 22 * s;
        if (F.uvSetA == F.uvSetB) Text(px, yy, "Same set selected twice.", P().muted, 10.5f);
        else if (!diff) { Glyph(Icon::Check, px, yy + 1 * s, 12, kGreen); Text(px + 18 * s, yy, "Identical layouts", P().text, 10.5f); }
        else {
            char t[96];
            snprintf(t, sizeof(t), "%zu of %zu vertices differ (%.0f %%)", diff, ua.size(), 100.0 * diff / std::max<size_t>(1, ua.size()));
            Text(px, yy, t, P().text, 10.5f);
        }
        yy += 22 * s;
    }
    // tiles used
    yy += 6 * s;
    Label(px, yy, "TILES");
    yy += 22 * s;
    auto tiles = [&](const UvAnalysis& a, ImU32 col, float tx) {
        float ty = yy;
        for (auto& kv : a.tiles) {
            char t[48];
            if (kv.first > 0) snprintf(t, sizeof(t), "%d  ·  %d tris", kv.first, kv.second);
            else snprintf(t, sizeof(t), "outside  ·  %d", kv.second);
            Text(tx, ty, t, kv.first > 0 ? col : kAmber, 10.5f);
            ty += 17 * s;
        }
        return ty;
    };
    float endA = tiles(A, P().text, px);
    float endB = B ? tiles(*B, kSetB, px + bw * 0.52f) : endA;
    yy = std::max(endA, endB) + 8 * s;
    // legend of the display
    Label(px, yy, "DISPLAY");
    yy += 22 * s;
    Text(px, yy, kUvFill[F.uvFill], P().text, 11);
    yy += 18 * s;
    {
        // the tip, wrapped on the width
        std::string line, word;
        std::istringstream ss(kUvFillTips[F.uvFill]);
        while (ss >> word) {
            std::string test = line.empty() ? word : line + " " + word;
            if (TextSize(test, 9.5f).x > bw && !line.empty()) { Text(px, yy, line, P().muted, 9.5f); yy += 14 * s; line = word; }
            else line = test;
        }
        if (!line.empty()) { Text(px, yy, line, P().muted, 9.5f); yy += 14 * s; }
    }
    if (F.uvFill == 4 || F.uvFill == 5) {
        yy += 4 * s;
        for (int i = 0; i < 40; ++i) dl->AddRectFilled(ImVec2(px + i * bw / 40, yy), ImVec2(px + (i + 1) * bw / 40 + 1, yy + 10 * s), ramp(F.uvFill == 4 ? i / 39.0f : 0.5f + i / 78.0f, 255));
        yy += 13 * s;
        Text(px, yy, F.uvFill == 4 ? "x0.25" : "1.0", P().muted, 9);
        const char* mid = F.uvFill == 4 ? "median" : "2.0";
        Text(px + bw / 2 - TextSize(mid, 9).x / 2, yy, mid, P().muted, 9);
        const char* end = F.uvFill == 4 ? "x4" : "3.0+";
        Text(px + bw - TextSize(end, 9).x, yy, end, P().muted, 9);
        yy += 16 * s;
    }
    ImGui::SetCursorScreenPos(ImVec2(px, yy));
    ImGui::Dummy(ImVec2(1, 6 * s));
    ImGui::EndChild();
    dl = ImGui::GetWindowDrawList();
}

} // namespace app
