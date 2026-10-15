// Prefab studio panels (workspace 3), drawn with the Atelier helpers of AtelierUI.cpp (this file is included at its
// end, so the theme colours, buttons, sliders and panels are the same as the other workspaces):
// Parts (hierarchy) | viewport (debug views, overlays, move gizmo) | Inspector (Part, Light, Debug, Asset)
//                   | Timeline (clips, tracks, keys) / UV Layout
// The logic (scene, files, undo, playback) is in PrefabStudio.cpp.

#include <filesystem>
#include <functional>

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

// UV layout of the selected part: rasterised on the CPU once (wireframe over the texture, coverage and overlap)
struct UvCache {
    uint32_t part = 0, serial = 0;
    ImTextureID tex = 0;
    int size = 0;
    int tris = 0, outside = 0, degenerate = 0;
    float coverage = 0, overlap = 0;
    bool hasUv = false;
    float2 uvMin, uvMax;
} g_uv;

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
    pfParts(X + 5 * s, Y, lw, H - 10 * s);
    pfViewport(cx, Y, cw, H - bh - 18 * s);
    if (F.bottomTab == 1) pfUvLayout(cx, Y + H - bh - 8 * s, cw, bh);
    else pfTimeline(cx, Y + H - bh - 8 * s, cw, bh);
    pfInspector(X + W - rw - 6 * s, Y, rw, H - 10 * s);
    dl = ImGui::GetWindowDrawList();
    Splitter("pfSplitLeft", {X + lw + 5 * s, Y, 7 * s, H - 10 * s}, F.leftW, 200, maxL, true);
    Splitter("pfSplitRight", {X + W - rw - 12 * s, Y, 6 * s, H - 10 * s}, F.rightW, 270, maxR, true, true);
    Splitter("pfSplitBottom", {cx, Y + H - bh - 13 * s, cw, 5 * s}, F.bottomH, 150, maxB, false, true);
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
            for (const PrefabTrack& t : clip->tracks) animated |= t.part == p.id && !t.keys.empty();
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
                        std::wstring path = pickFileDialog(m_hwnd, false, L"Meshes (*.obj)\0*.obj\0All files\0*.*\0", L"obj", projectRoot() + L"\\assets", L"");
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
            if (Button("pfOpenUv", {bx, yy, bw, 26 * s}, "Open UV Layout", Icon::Grid, F.bottomTab == 1)) F.bottomTab = 1;
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
    // tabs: Timeline / UV Layout (top right of the panel)
    {
        float tx = r.x + r.w - 12 * s - 2 * 104 * s;
        if (TabButton("pfBottomTl", {tx, r.y + 6 * s, 100 * s, 24 * s}, "Timeline", Icon::Animation, F.bottomTab == 0)) F.bottomTab = 0;
        if (TabButton("pfBottomUv", {tx + 104 * s, r.y + 6 * s, 100 * s, 24 * s}, "UV Layout", Icon::Grid, F.bottomTab == 1)) F.bottomTab = 1;
    }
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
            const bool has = F.sel >= 0 && F.sel < (int)A.parts.size();
            if (Button("pfKeyBtn", {bx, py, 64 * s, 26 * s}, "Key", Icon::Pin, false, false, has)) prefabKeySelected();
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
    struct Row { int part; int channel; int track; };   // channel -1 = part summary row
    std::vector<Row> rows;
    std::vector<int> order;
    std::function<void(uint32_t)> walk = [&](uint32_t parent) {
        for (int i : A.childrenOf(parent)) { order.push_back(i); if (order.size() < 4096) walk(A.parts[i].id); }
    };
    walk(0);
    for (int i : order) {
        const PrefabPart& p = A.parts[i];
        bool any = false;
        for (const PrefabTrack& t : clip->tracks) any |= t.part == p.id && !t.keys.empty();
        if (!any && i != F.sel) continue;
        rows.push_back({i, -1, -1});
        for (int ch = 0; ch < CH_COUNT; ++ch) {
            int ti = -1;
            for (size_t k = 0; k < clip->tracks.size(); ++k)
                if (clip->tracks[k].part == p.id && clip->tracks[k].channel == ch) ti = (int)k;
            if (ti >= 0 || i == F.sel) rows.push_back({i, ch, ti});
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
        const PrefabPart& p = A.parts[row.part];
        ImGui::PushID((int)ri);
        R nr = {px, yy, nameW - 6 * s, rowH - 1 * s};
        R tr = {ax, yy, aw, rowH - 1 * s};
        Box({px, yy, pw, rowH - 1 * s}, ri % 2 ? Fade(P().panel, 255) : Fade(P().bg, 160), 0);
        if (row.channel < 0) {
            if (Hit("partRow", nr)) { F.sel = row.part; }
            TextBold(px + 6 * s, yy + 4 * s, Fit(p.name, nameW - 16 * s, 11), row.part == F.sel ? P().accent : P().text, 11);
            // summary: every key time of the part
            std::vector<float> times;
            for (const PrefabTrack& t : clip->tracks)
                if (t.part == p.id)
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
                F.sel = row.part;
                float t = snap(xTime(mouse.x));
                std::vector<PartPose> pose;
                evaluatePose(A, clip, t, pose);
                const PartPose& pp = pose[row.part];
                float3 v = row.channel == CH_POSITION ? pp.position : row.channel == CH_ROTATION ? pp.rotation : pp.scale;
                clip->track(p.id, row.channel, true)->setKey(t, v, INTERP_SMOOTH, 0.5f / clip->fps);
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
        for (size_t i = 0; i < A.parts.size(); ++i)
            if (A.parts[i].id == clip->tracks[hoverTrack].part) F.sel = (int)i;
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

// --------------------------------------------------------------------------- UV layout

void App::pfUvLayout(float x, float y, float w, float h) {
    PrefabStudio& F = m_pf;
    PrefabAsset& A = F.asset;
    R r = {x, y, w, h};
    Panel(r, Icon::Grid, "UV Layout", "", P().accent);
    {
        float tx = r.x + r.w - 12 * s - 2 * 104 * s;
        if (TabButton("pfBottomTl2", {tx, r.y + 6 * s, 100 * s, 24 * s}, "Timeline", Icon::Animation, F.bottomTab == 0)) F.bottomTab = 0;
        if (TabButton("pfBottomUv2", {tx + 104 * s, r.y + 6 * s, 100 * s, 24 * s}, "UV Layout", Icon::Grid, F.bottomTab == 1)) F.bottomTab = 1;
    }
    float px = r.x + 10 * s, py = r.y + 40 * s, ph = r.h - 50 * s;
    int oi = F.sel >= 0 && F.sel < (int)F.partObject.size() ? F.partObject[F.sel] : -1;
    if (oi < 0 || oi >= (int)F.scene.objects.size()) {
        Text(px, py + 6 * s, "Select a part with a mesh to see its UVs.", P().muted, 11);
        return;
    }
    const PrefabPart& part = A.parts[F.sel];
    const MeshData& m = F.scene.meshes[F.scene.objects[oi].mesh];
    // CPU raster of the layout (once per part and scene build): texture (dimmed) or checker, the triangle edges, coverage
    if (g_uv.part != part.id || g_uv.serial != F.buildSerial || !g_uv.tex) {
        if (g_uv.tex) m_gui.releaseTexture(g_uv.tex);
        const int N = 1024;
        std::vector<uint8_t> img((size_t)N * N * 4);
        std::vector<uint8_t> cover((size_t)N * N, 0);
        auto tc = F.texCache.find(part.texture);
        const Texture* tex = tc != F.texCache.end() && tc->second.width ? &tc->second : nullptr;
        for (int yy = 0; yy < N; ++yy)
            for (int xx = 0; xx < N; ++xx) {
                uint8_t* d = &img[((size_t)yy * N + xx) * 4];
                if (tex) {
                    uint32_t tx = std::min(tex->width - 1, (uint32_t)((float)xx / N * tex->width));
                    uint32_t ty = std::min(tex->height - 1, (uint32_t)((float)yy / N * tex->height));
                    const uint8_t* sp = &tex->rgba[((size_t)ty * tex->width + tx) * 4];
                    for (int c = 0; c < 3; ++c) d[c] = (uint8_t)(sp[c] * 0.45f);
                } else {
                    bool chk = ((xx / (N / 8)) + (yy / (N / 8))) & 1;
                    d[0] = d[1] = d[2] = chk ? 46 : 34;
                }
                d[3] = 255;
            }
        // filled triangles: coverage count (overlap = texels covered twice or more)
        g_uv = UvCache();
        g_uv.part = part.id;
        g_uv.serial = F.buildSerial;
        g_uv.size = N;
        g_uv.tris = (int)m.triangleCount();
        g_uv.uvMin = float2(1e30f, 1e30f);
        g_uv.uvMax = float2(-1e30f, -1e30f);
        for (const Vertex& v : m.vertices) {
            g_uv.hasUv |= v.uv.x != 0 || v.uv.y != 0;
            g_uv.uvMin = float2(std::min(g_uv.uvMin.x, v.uv.x), std::min(g_uv.uvMin.y, v.uv.y));
            g_uv.uvMax = float2(std::max(g_uv.uvMax.x, v.uv.x), std::max(g_uv.uvMax.y, v.uv.y));
        }
        auto toPx = [&](float2 uv) { return float2((uv.x - std::floor(uv.x)) * N, (uv.y - std::floor(uv.y)) * N); };
        const size_t triCount = m.triangleCount();
        for (size_t t = 0; t < triCount; ++t) {
            float2 a = m.vertices[m.indices[t * 3]].uv, b = m.vertices[m.indices[t * 3 + 1]].uv, c = m.vertices[m.indices[t * 3 + 2]].uv;
            // a triangle crossing a tile border is drawn in the tile of its first vertex
            float2 o(std::floor(a.x), std::floor(a.y));
            a = (a - o) * (float)N; b = (b - o) * (float)N; c = (c - o) * (float)N;
            float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
            if (std::fabs(area) < 1e-6f) { ++g_uv.degenerate; continue; }
            int x0 = std::max(0, (int)std::floor(std::min(a.x, std::min(b.x, c.x)))), x1 = std::min(N - 1, (int)std::ceil(std::max(a.x, std::max(b.x, c.x))));
            int y0 = std::max(0, (int)std::floor(std::min(a.y, std::min(b.y, c.y)))), y1 = std::min(N - 1, (int)std::ceil(std::max(a.y, std::max(b.y, c.y))));
            if ((size_t)(x1 - x0 + 1) * (y1 - y0 + 1) > (size_t)N * N / 2) continue;   // broken UVs: skip huge triangles
            for (int yy = y0; yy <= y1; ++yy)
                for (int xx = x0; xx <= x1; ++xx) {
                    float qx = xx + 0.5f, qy = yy + 0.5f;
                    float w0 = (b.x - a.x) * (qy - a.y) - (b.y - a.y) * (qx - a.x);
                    float w1 = (c.x - b.x) * (qy - b.y) - (c.y - b.y) * (qx - b.x);
                    float w2 = (a.x - c.x) * (qy - c.y) - (a.y - c.y) * (qx - c.x);
                    if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0)) {
                        uint8_t& cv = cover[(size_t)yy * N + xx];
                        if (cv < 255) ++cv;
                    }
                }
        }
        (void)toPx;
        size_t covered = 0, over = 0;
        for (size_t i = 0; i < cover.size(); ++i) {
            if (!cover[i]) continue;
            ++covered;
            uint8_t* d = &img[i * 4];
            if (cover[i] > 1) { ++over; d[0] = (uint8_t)std::min(255, d[0] + 90); d[1] = (uint8_t)(d[1] * 0.7f); d[2] = (uint8_t)std::min(255, d[2] + 60); }
            else { d[0] = (uint8_t)std::min(255, d[0] + 22); d[1] = (uint8_t)std::min(255, d[1] + 34); d[2] = (uint8_t)std::min(255, d[2] + 44); }
        }
        g_uv.coverage = (float)covered / (N * N);
        g_uv.overlap = covered ? (float)over / covered : 0.0f;
        // edges (Bresenham), light lines
        auto plot = [&](int xx, int yy) {
            if (xx < 0 || yy < 0 || xx >= N || yy >= N) return;
            uint8_t* d = &img[((size_t)yy * N + xx) * 4];
            d[0] = (uint8_t)std::min(255, d[0] / 2 + 105); d[1] = (uint8_t)std::min(255, d[1] / 2 + 120); d[2] = (uint8_t)std::min(255, d[2] / 2 + 130);
        };
        auto lineTo = [&](float2 a, float2 b) {
            int x0 = (int)a.x, y0 = (int)a.y, x1 = (int)b.x, y1 = (int)b.y;
            int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
            for (int guard = 0; guard < 4 * N; ++guard) {
                plot(x0, y0);
                if (x0 == x1 && y0 == y1) break;
                int e2 = 2 * err;
                if (e2 >= dy) { err += dy; x0 += sx; }
                if (e2 <= dx) { err += dx; y0 += sy; }
            }
        };
        const bool quads = !m.edges.empty();
        if (quads) {
            for (size_t e = 0; e + 1 < m.edges.size(); e += 2) {
                float2 a = m.vertices[m.edges[e]].uv, b = m.vertices[m.edges[e + 1]].uv;
                float2 o(std::floor(a.x), std::floor(a.y));
                lineTo((a - o) * (float)N, (b - o) * (float)N);
            }
        } else {
            for (size_t t = 0; t < triCount; ++t) {
                float2 a = m.vertices[m.indices[t * 3]].uv, b = m.vertices[m.indices[t * 3 + 1]].uv, c = m.vertices[m.indices[t * 3 + 2]].uv;
                float2 o(std::floor(a.x), std::floor(a.y));
                a = (a - o) * (float)N; b = (b - o) * (float)N; c = (c - o) * (float)N;
                lineTo(a, b); lineTo(b, c); lineTo(c, a);
            }
        }
        g_uv.tex = m_gui.createTexture(img.data(), N, N);
    }
    // the square (v up = image top, as the texture) and the statistics on its right
    float side = std::max(40.0f, ph);
    R sq = {px, py, side, side};
    Box({sq.x - 1, sq.y - 1, sq.w + 2, sq.h + 2}, P().border, 0);
    if (g_uv.tex) dl->AddImage(g_uv.tex, sq.min(), sq.max());
    float tx = px + side + 16 * s, ty = py + 2 * s;
    TextBold(tx, ty, part.name, P().text, 13);
    ty += 22 * s;
    auto line = [&](bool ok, const std::string& t) {
        Glyph(ok ? Icon::Check : Icon::Info, tx, ty + 1 * s, 12, ok ? kGreen : kAmber);
        Text(tx + 20 * s, ty, t, ok ? P().text : kAmber, 10.5f);
        ty += 20 * s;
    };
    char b[128];
    snprintf(b, sizeof(b), "%d triangles", g_uv.tris);
    line(true, b);
    line(g_uv.hasUv, g_uv.hasUv ? "UVs present" : "No UVs");
    if (g_uv.hasUv) {
        snprintf(b, sizeof(b), "UV range %.2f..%.2f  x  %.2f..%.2f", g_uv.uvMin.x, g_uv.uvMax.x, g_uv.uvMin.y, g_uv.uvMax.y);
        bool inside = g_uv.uvMin.x >= -0.001f && g_uv.uvMin.y >= -0.001f && g_uv.uvMax.x <= 1.001f && g_uv.uvMax.y <= 1.001f;
        line(inside, b);
        snprintf(b, sizeof(b), "Coverage %.0f %% of the square", g_uv.coverage * 100.0f);
        line(g_uv.coverage > 0.35f, b);
        snprintf(b, sizeof(b), "Overlap %.1f %% (pink)", g_uv.overlap * 100.0f);
        line(g_uv.overlap < 0.02f, b);
        snprintf(b, sizeof(b), g_uv.degenerate ? "%d zero-area UV triangles" : "No zero-area UV triangle", g_uv.degenerate);
        line(g_uv.degenerate == 0, b);
    }
    ty += 6 * s;
    Text(tx, ty, part.texture.empty() ? "Background: checker (no texture)" : "Background: the base color texture", P().muted, 9.5f);
    ty += 16 * s;
    Text(tx, ty, "Overlap is normal for mirrored or tiled parts.", P().muted, 9.5f);
}

} // namespace app
