// Rig module panels (Prefab studio, centre tab "Rig"), drawn with the Atelier helpers of AtelierUI.cpp (included at
// its end, after PrefabUI.inl): Skeleton panel (left), Rig viewport (modes, overlays: weights, bones, joints, gizmo,
// brush), Rig inspector (Joint, Skin, Paint). Logic: RigStudio.cpp, maths: Rig.cpp.

namespace app {

namespace {
const char* kRigModes[] = {"Skeleton", "Skin", "Paint Weights", "Pose"};
const char* kRigModeTips[] = {"Edit the joints at rest (the bind pose): create, parent, move, rotate",
                              "Bind parts to the skeleton, check and clean their weights, see the influences",
                              "Paint the weights of the selected joint on the skinned part (left drag, Ctrl+wheel: radius)",
                              "Pose the skeleton (keys with Auto Key or on animated channels) and see the skin deform"};
const char* kPaintModes[] = {"Add", "Subtract", "Replace", "Smooth"};
const char* kFalloffs[] = {"Smooth", "Linear", "Constant"};

ImU32 jointColor(uint32_t id, int alpha = 255) {
    float hue = std::fmod(id * 0.61803f + 0.08f, 1.0f);
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(hue, 0.6f, 0.95f, r, g, b);
    return IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), alpha);
}
// weight heat: 0 dark blue, 0.5 green, 1 red (Maya-like)
ImU32 heat(float w, int alpha) {
    w = clampf(w, 0, 1);
    if (w <= 0.0001f) return IM_COL32(22, 26, 60, alpha);
    float r = w < 0.5f ? 0.0f : (w - 0.5f) * 2.0f, g = w < 0.5f ? w * 2.0f : 1.0f - (w - 0.5f) * 1.6f, b = w < 0.5f ? 1.0f - w * 2.0f : 0.0f;
    return IM_COL32((int)(r * 255), (int)(clampf(g, 0, 1) * 255), (int)(b * 255), alpha);
}
} // namespace

// --------------------------------------------------------------------------- skeleton panel (left)

void App::pfSkeleton(float x, float y, float w, float h) {
    PrefabStudio& F = m_pf;
    PrefabAsset& A = F.asset;
    R r = {x, y, w, h};
    Panel(r, Icon::Link, "Skeleton", "RIG", P().accent);
    float px = r.x + 10 * s, py = r.y + 44 * s, pw = r.w - 20 * s;
    g_labelRight = px + pw;
    {
        Box({px, py, pw, 50 * s}, Fade(P().accent, 26), 4, true);
        Glyph(Icon::Link, px + 10 * s, py + 9 * s, 16, P().accent);
        TextBold(px + 34 * s, py + 7 * s, A.name, P().text, 13);
        char t[96];
        snprintf(t, sizeof(t), "%zu joints  ·  %zu skins", A.joints.size(), A.skins.size());
        Text(px + 34 * s, py + 28 * s, t, P().muted, 9.5f);
        py += 58 * s;
        float bw = (pw - 2 * 5 * s) / 3;
        if (Button("rigImport", {px, py, bw, 25 * s}, "FBX", Icon::FolderOpen)) rigImportFbx();
        Tip("Import an FBX: mesh, joints and skin weights (added to this prefab)");
        if (Button("rigSample", {px + bw + 5 * s, py, bw, 25 * s}, "Sample", Icon::Cube)) rigLoadSample();
        Tip("Load the rig sample: a cylinder with three joints, a smooth skin and a Bend clip");
        if (PrimaryButton("rigSaveBtn", {px + 2 * (bw + 5 * s), py, bw, 25 * s}, "Save", Icon::Save)) prefabSave(false);
        Tip("Save the prefab with its skeleton and skins (Ctrl+S)");
        py += 35 * s;
    }
    dl->AddLine(ImVec2(px, py), ImVec2(px + pw, py), P().border);
    py += 12 * s;
    Text(px, py, "JOINTS", P().text, 11);
    py += 22 * s;
    Search("##rigSearch", {px, py, pw, 27 * s}, F.jointSearch, sizeof(F.jointSearch), "Search joints...");
    py += 34 * s;
    {
        float bw = (pw - 3 * 5 * s) / 4;
        const bool has = F.selJoint >= 0 && F.selJoint < (int)A.joints.size();
        if (Button("rigAdd", {px, py, bw, 24 * s}, "Joint", Icon::Plus)) rigAddJoint(false);
        Tip("New joint beside the selected one (same parent), or at the origin");
        if (Button("rigChild", {px + bw + 5 * s, py, bw, 24 * s}, "Child", Icon::Link, false, false, has)) rigAddJoint(true);
        Tip("New child of the selected joint, along its bone");
        if (Button("rigChain", {px + 2 * (bw + 5 * s), py, bw, 24 * s}, "Chain", Icon::List)) ImGui::OpenPopup("rigChainPopup");
        Tip("A chain of joints (from the selected joint, going up)");
        if (ImGui::BeginPopup("rigChainPopup")) {
            ImGui::SetWindowFontScale(1.4f * ts());
            for (int n : {2, 3, 4, 5, 8}) {
                char label[32];
                snprintf(label, sizeof(label), "%d joints (2 units)", n);
                if (ImGui::MenuItem(label)) rigAddChain(n, 2.0f);
            }
            ImGui::EndPopup();
        }
        if (Button("rigDel", {px + 3 * (bw + 5 * s), py, bw, 24 * s}, "", Icon::Trash, false, true, has)) rigDeleteJoint();
        Tip("Delete the joint, its children and their weights (Del)");
        py += 32 * s;
    }
    // tree
    const float listH = std::max(60.0f, (r.y + r.h - py) * 0.62f);
    ImGui::SetCursorScreenPos(ImVec2(px, py));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("rigTree", ImVec2(pw, listH), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImGui::SetWindowFontScale(atelier::Style().layoutScale);
    ImGui::PopStyleVar();
    dl = ImGui::GetWindowDrawList();
    float yy = ImGui::GetCursorScreenPos().y;
    const float rowH = 24 * s, indentW = 13 * s;
    std::vector<std::pair<int, int>> rows;
    std::function<void(uint32_t, int)> walk = [&](uint32_t parent, int depth) {
        for (int i : A.jointChildren(parent)) {
            if (depth > 64) return;
            bool show = matches(A.joints[i].name, F.jointSearch);
            if (show) rows.push_back({i, *F.jointSearch ? 0 : depth});
            if (!A.joints[i].collapsed || *F.jointSearch) walk(A.joints[i].id, depth + 1);
        }
    };
    walk(0, 0);
    uint32_t drop = 0;
    bool dropRoot = false;
    for (auto [i, depth] : rows) {
        PrefabJoint& j = A.joints[i];
        R row = {px, yy, pw - 6 * s, rowH - 2 * s};
        ImGui::PushID((int)j.id);
        if (Hit("row", row)) F.selJoint = i;
        const bool hovered = ImGui::IsItemHovered();
        if (hovered && ImGui::IsMouseDoubleClicked(0)) {
            F.jointRename = j.id;
            snprintf(F.jointRenameBuf, sizeof(F.jointRenameBuf), "%s", j.name.c_str());
            F.jointRenameFocus = true;
        }
        if (ImGui::BeginDragDropSource()) {
            uint32_t id = j.id;
            ImGui::SetDragDropPayload("WZX_JOINT", &id, sizeof(id));
            ImGui::Text("%s", j.name.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("WZX_JOINT")) {
                uint32_t id = *(const uint32_t*)pl->Data;
                if (id != j.id && !A.jointAncestor(id, j.id)) { drop = id; F.jointDragTarget = j.id; }
            }
            ImGui::EndDragDropTarget();
        }
        if (i == F.selJoint) SelectedRow(row);
        else if (hovered) Box(row, P().hover, 3);
        float ix = px + 6 * s + depth * indentW;
        if (!A.jointChildren(j.id).empty()) {
            if (Hit("fold", {ix - 2 * s, yy + 3 * s, 14 * s, 16 * s})) j.collapsed = !j.collapsed;
            Glyph(j.collapsed ? Icon::ChevronRight : Icon::ChevronDown, ix, yy + 7 * s, 9, P().muted);
        }
        ix += 13 * s;
        dl->AddCircleFilled(ImVec2(ix + 5 * s, yy + rowH / 2 - 1 * s), 4.5f * s, jointColor(j.id));
        dl->AddCircle(ImVec2(ix + 5 * s, yy + rowH / 2 - 1 * s), 4.5f * s, IM_COL32(20, 20, 20, 160), 12, 1.0f);
        ix += 16 * s;
        if (F.jointRename == j.id) {
            ImGui::SetCursorScreenPos(ImVec2(ix, yy + 1 * s));
            ImGui::SetNextItemWidth(row.x + row.w - ix - 8 * s);
            if (F.jointRenameFocus) { ImGui::SetKeyboardFocusHere(); F.jointRenameFocus = false; }
            bool done = ImGui::InputText("##jrename", F.jointRenameBuf, sizeof(F.jointRenameBuf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            if (done || ImGui::IsItemDeactivated()) {
                if (*F.jointRenameBuf && j.name != F.jointRenameBuf) { prefabPushUndo(); A.joints[i].name = F.jointRenameBuf; }
                F.jointRename = 0;
            }
        } else {
            dl->PushClipRect(ImVec2(ix, yy), ImVec2(row.x + row.w - 30 * s, yy + rowH), true);
            Text(ix, yy + (rowH - 2 * s - 12 * s * ts()) / 2, j.name, P().text, 11);
            dl->PopClipRect();
        }
        // animated in the clip / posed (override)
        const PrefabClip* clip = prefabClip();
        bool animated = false;
        if (clip) for (const PrefabTrack& t : clip->tracks) animated |= t.joint && t.part == j.id && !t.keys.empty();
        if (animated) {
            ImVec2 c(row.x + row.w - 14 * s, yy + rowH / 2 - 1 * s);
            float k = 4 * s;
            dl->AddQuadFilled(ImVec2(c.x, c.y - k), ImVec2(c.x + k, c.y), ImVec2(c.x, c.y + k), ImVec2(c.x - k, c.y), kAmber);
        } else if (F.poseOverride.count(j.id)) {
            dl->AddCircle(ImVec2(row.x + row.w - 14 * s, yy + rowH / 2 - 1 * s), 4 * s, kAmber, 12, 1.4f);
        }
        ImGui::PopID();
        yy += rowH;
    }
    float rest = std::max(rowH, ImGui::GetWindowPos().y + ImGui::GetWindowHeight() - yy - 2 * s);
    ImGui::SetCursorScreenPos(ImVec2(px, yy));
    ImGui::InvisibleButton("rigTreeEmpty", ImVec2(pw - 6 * s, rest));
    if (ImGui::IsItemClicked()) F.selJoint = -1;
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("WZX_JOINT")) { drop = *(const uint32_t*)pl->Data; dropRoot = true; }
        ImGui::EndDragDropTarget();
    }
    if (A.joints.empty()) {
        Text(px + 6 * s, yy + 6 * s, "No joint: Joint / Chain to start,", P().muted, 10);
        Text(px + 6 * s, yy + 22 * s, "FBX to import a rig, Sample to try.", P().muted, 10);
    }
    // re-parenting keeps the joint where it is (rest world transform)
    if (drop) {
        int di = A.findJoint(drop);
        if (di >= 0) {
            prefabPushUndo();
            std::vector<PartWorld> rw;
            restJointWorld(A, rw);
            uint32_t np = dropRoot ? 0 : F.jointDragTarget;
            int pi = np ? A.findJoint(np) : -1;
            PartWorld me = rw[di];
            float3 pos = me.position;
            float4 rot = me.rot;
            if (pi >= 0) {
                float4 inv(-rw[pi].rot.x, -rw[pi].rot.y, -rw[pi].rot.z, rw[pi].rot.w);
                pos = qRot(inv, me.position - rw[pi].position);
                pos = float3(pos.x / rw[pi].scale.x, pos.y / rw[pi].scale.y, pos.z / rw[pi].scale.z);
                rot = qNormalize(qMul(inv, me.rot));
            }
            A.joints[di].parent = np;
            A.joints[di].position = pos;
            A.joints[di].rotation = quatToEuler(rot);
            F.weightsVersion++;
        }
    }
    ImGui::EndChild();
    dl = ImGui::GetWindowDrawList();
    py += listH + 10 * s;
    // skinned parts
    Label(px, py, "SKINNED PARTS");
    py += 22 * s;
    int shown = 0;
    for (size_t i = 0; i < A.parts.size(); ++i) {
        const PrefabSkin* sk = A.skinOf(A.parts[i].id);
        if (!sk) continue;
        R row = {px, py, pw, 24 * s};
        ImGui::PushID((int)i + 10000);
        if (Hit("skinRow", row)) { F.skinPart = (int)i; F.sel = (int)i; }
        if ((int)i == F.skinPart) SelectedRow(row);
        Glyph(Icon::Cube, px + 6 * s, py + 5 * s, 12, srgbU32(A.parts[i].material.baseColor));
        Text(px + 26 * s, py + 5 * s, Fit(A.parts[i].name, pw - 120 * s, 11), P().text, 11);
        char t[48];
        snprintf(t, sizeof(t), "%zu inf · %u v", sk->joints.size(), sk->vertexCount);
        Text(px + pw - TextSize(t, 9).x - 6 * s, py + 7 * s, t, P().muted, 9);
        ImGui::PopID();
        py += 27 * s;
        if (py > r.y + r.h - 30 * s) break;
        ++shown;
    }
    if (!A.skins.size()) Text(px, py, "None yet: Skin tab > Bind Skin.", P().muted, 10);
}

// --------------------------------------------------------------------------- rig viewport

void App::pfRigViewport(float x, float y, float w, float h) {
    PrefabStudio& F = m_pf;
    const float tbH = 34 * s;
    Box({x, y, w, tbH}, P().panel, 4, true);
    float bx = x + 6 * s, by = y + 5 * s;
    pfCenterTabs(F.centerTab, bx, by);
    for (int i = 0; i < 4; ++i) {
        float bw = TextSize(kRigModes[i], 11).x + 13 * s * P().iconScale + 30 * s;
        char id[24];
        snprintf(id, sizeof(id), "rigMode%d", i);
        Icon ic = i == 0 ? Icon::Link : i == 1 ? Icon::Material : i == 2 ? Icon::Pin : Icon::Move;
        if (TabButton(id, {bx, by, bw, 24 * s}, kRigModes[i], ic, F.rigMode == i)) {
            F.rigMode = i;
            F.rigTab = i == 0 || i == 3 ? 0 : i == 1 ? 1 : 2;
        }
        Tip(kRigModeTips[i]);
        bx += bw + 3 * s;
    }
    bx += 6 * s;
    dl->AddLine(ImVec2(bx, y + 8 * s), ImVec2(bx, y + tbH - 8 * s), P().border);
    bx += 8 * s;
    if (F.rigMode != 2) {
        if (Button("rigToolMove", {bx, by, 28 * s, 24 * s}, "", Icon::Move, F.rigTool == 1, true)) F.rigTool = 1;
        Tip("Move the selected joint (W)");
        bx += 31 * s;
        if (Button("rigToolRot", {bx, by, 28 * s, 24 * s}, "", Icon::Rotate, F.rigTool == 2, true)) F.rigTool = 2;
        Tip("Rotate the selected joint (E); Ctrl: 15 degree steps");
        bx += 37 * s;
    }
    float rx = x + w - 6 * s - 28 * s;
    if (Button("rigFrame", {rx, by, 26 * s, 24 * s}, "", Icon::Home, false, true)) prefabFrameAll();
    Tip("Frame the prefab (F)");
    rx -= 30 * s;
    if (Button("rigWeights", {rx, by, 26 * s, 24 * s}, "", Icon::Material, F.showWeights, true)) F.showWeights = !F.showWeights;
    Tip("Weights on the skinned part (Skin and Paint modes)");
    rx -= 30 * s;
    if (Button("rigNames", {rx, by, 26 * s, 24 * s}, "", Icon::List, F.showJointNames, true)) F.showJointNames = !F.showJointNames;
    Tip("Joint names");
    rx -= 30 * s;
    if (Button("rigBones", {rx, by, 26 * s, 24 * s}, "", Icon::Link, F.showBones, true)) F.showBones = !F.showBones;
    Tip("Bones and joints (always drawn on top)");

    const float iy = y + tbH + 4 * s, ih = h - tbH - 4 * s;
    F.rcView = {x, iy, w, ih};
    F.vpPx = {0, 0, std::max(16, (int)w), std::max(16, (int)ih)};
    ImGui::SetCursorScreenPos(ImVec2(x, iy));
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##rigView", ImVec2(w, ih), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    dl->AddImage(m_gui.viewportTexture(), ImVec2(x, iy), ImVec2(x + w, iy + ih));
    dl->PushClipRect(ImVec2(x, iy), ImVec2(x + w, iy + ih), true);
    pfRigOverlay(x, iy, w, ih);
    prefabViewInput(hovered);   // camera (the left button belongs to the rig)
    rigViewInput(hovered);
    dl->PopClipRect();
}

void App::pfRigOverlay(float x, float y, float w, float h) {
    PrefabStudio& F = m_pf;
    PrefabAsset& A = F.asset;
    const Camera& cam = F.camera;
    const ScreenRect& rc = F.rcView;
    float3 cr, cu, cf;
    cam.basis(cr, cu, cf);
    const float3 eye = cam.eye();
    auto pxPerUnit = [&](const float3& p) {
        float z = std::max(1e-3f, dot(p - eye, cf));
        return rc.h / (2.0f * z * std::tan(cam.fovY * 0.5f));
    };
    const std::vector<PartWorld>& J = F.rigMode == 0 ? F.jointRest : F.jointNow;

    // weights on the skinned part (Skin / Paint): projected triangles, per-vertex colours, back to front
    const bool weightsOn = F.showWeights && (F.rigMode == 1 || F.rigMode == 2);
    if (weightsOn && F.skinPart >= 0 && F.skinPart < (int)A.parts.size() && F.skinPart < (int)F.partObject.size() && F.partObject[F.skinPart] >= 0) {
        const PrefabSkin* sk = A.skinOf(A.parts[F.skinPart].id);
        const MeshData& m = F.scene.meshes[F.scene.objects[F.partObject[F.skinPart]].mesh];
        if (sk && sk->vertexCount == m.vertices.size() && m.indices.size() / 3 <= 120000) {
            const int inf = F.selJoint >= 0 && F.selJoint < (int)A.joints.size() ? sk->influence(A.joints[F.selJoint].id) : -1;
            std::vector<ImVec2> sp(m.vertices.size());
            std::vector<float> depth(m.vertices.size());
            std::vector<ImU32> col(m.vertices.size());
            std::vector<char> ok(m.vertices.size());
            const float3 L = normalize(float3(0.4f, 0.8f, 0.45f));
            for (size_t v = 0; v < m.vertices.size(); ++v) {
                ok[v] = pfProject(cam, rc, m.vertices[v].pos, sp[v]);
                depth[v] = dot(m.vertices[v].pos - eye, cf);
                float shade = 0.55f + 0.45f * std::fabs(dot(m.vertices[v].nrm, L));
                ImU32 c;
                if (F.weightView == 1 || inf < 0) {   // blend of the joint colours
                    float rr = 0, gg = 0, bb = 0;
                    for (size_t k = 0; k < sk->joints.size(); ++k) {
                        float wv = sk->w(v, k);
                        if (wv <= 0) continue;
                        ImU32 jc = jointColor(sk->joints[k]);
                        rr += wv * ((jc >> 0) & 255); gg += wv * ((jc >> 8) & 255); bb += wv * ((jc >> 16) & 255);
                    }
                    c = IM_COL32((int)(rr * shade), (int)(gg * shade), (int)(bb * shade), 225);
                } else {
                    ImU32 hc = heat(sk->w(v, (size_t)inf), 255);
                    c = IM_COL32((int)(((hc >> 0) & 255) * shade), (int)(((hc >> 8) & 255) * shade), (int)(((hc >> 16) & 255) * shade), 225);
                }
                col[v] = c;
            }
            struct Tri { float z; uint32_t a, b, c; };
            std::vector<Tri> tris;
            tris.reserve(m.indices.size() / 3);
            for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
                uint32_t a = m.indices[t], b = m.indices[t + 1], c = m.indices[t + 2];
                if (!ok[a] || !ok[b] || !ok[c]) continue;
                float3 n = cross(m.vertices[b].pos - m.vertices[a].pos, m.vertices[c].pos - m.vertices[a].pos);
                if (dot(n, eye - m.vertices[a].pos) <= 0) continue;   // back face
                tris.push_back({depth[a] + depth[b] + depth[c], a, b, c});
            }
            std::sort(tris.begin(), tris.end(), [](const Tri& p, const Tri& q) { return p.z > q.z; });
            const ImVec2 white = ImGui::GetFontTexUvWhitePixel();
            const ImDrawListFlags old = dl->Flags;
            dl->Flags &= ~ImDrawListFlags_AntiAliasedFill;
            for (const Tri& t : tris) {
                dl->PrimReserve(3, 3);
                dl->PrimVtx(sp[t.a], white, col[t.a]);
                dl->PrimVtx(sp[t.b], white, col[t.b]);
                dl->PrimVtx(sp[t.c], white, col[t.c]);
            }
            if (tris.size() < 6000)
                for (const Tri& t : tris) {
                    dl->AddLine(sp[t.a], sp[t.b], IM_COL32(0, 0, 0, 40), 1.0f);
                    dl->AddLine(sp[t.b], sp[t.c], IM_COL32(0, 0, 0, 40), 1.0f);
                }
            dl->Flags = old;
        }
    }

    // bones (parent -> child, tapered) and joints
    if (F.showBones && !J.empty()) {
        const int sel = F.selJoint;
        for (size_t i = 0; i < A.joints.size() && i < J.size(); ++i) {
            int pi = A.joints[i].parent ? A.findJoint(A.joints[i].parent) : -1;
            if (pi < 0 || pi >= (int)J.size()) continue;
            ImVec2 a, b;
            if (!pfProject(cam, rc, J[pi].position, a) || !pfProject(cam, rc, J[i].position, b)) continue;
            float dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx * dx + dy * dy);
            if (len < 1) continue;
            float nx = -dy / len, ny = dx / len;
            float wd = clampf(len * 0.11f, 3.0f * s, 11.0f * s);
            ImVec2 m(a.x + dx * 0.18f, a.y + dy * 0.18f);
            ImVec2 q[4] = {a, ImVec2(m.x + nx * wd, m.y + ny * wd), b, ImVec2(m.x - nx * wd, m.y - ny * wd)};
            const bool hot = pi == sel;
            ImU32 fill = hot ? Fade(P().accent, F.xray ? 150 : 120) : IM_COL32(150, 190, 235, F.xray ? 105 : 80);
            dl->AddConvexPolyFilled(q, 4, fill);
            dl->AddPolyline(q, 4, hot ? P().accent : IM_COL32(200, 225, 255, 200), ImDrawFlags_Closed, 1.2f * s);
        }
        for (size_t i = 0; i < A.joints.size() && i < J.size(); ++i) {
            ImVec2 p;
            if (!pfProject(cam, rc, J[i].position, p)) continue;
            float rad = clampf(A.joints[i].radius * pxPerUnit(J[i].position), 3.5f * s, 16 * s);
            const bool selJ = (int)i == F.selJoint;
            dl->AddCircleFilled(p, rad, selJ ? Fade(P().accent, 200) : IM_COL32(25, 30, 40, 170), 20);
            dl->AddCircle(p, rad, selJ ? IM_COL32(255, 255, 255, 255) : jointColor(A.joints[i].id), 20, (selJ ? 2.2f : 1.6f) * s);
            if (F.showJointNames && (selJ || A.joints.size() <= 40)) {
                Text(p.x + rad + 4 * s, p.y - 7 * s, A.joints[i].name, selJ ? P().accent : IM_COL32(210, 225, 245, 220), 9.5f);
            }
        }
    }

    // gizmo of the selected joint: arrows (move) or rings (rotate), world axes
    F.jgHover = -1;
    const bool selValid = F.selJoint >= 0 && F.selJoint < (int)J.size();
    if (selValid && F.rigMode != 2) {
        ImVec2 o;
        const float3 c = J[F.selJoint].position;
        if (pfProject(cam, rc, c, o)) {
            const ImVec2 mouse = ImGui::GetMousePos();
            const float len = 62 * s;
            if (F.rigTool == 1) {
                for (int a = 0; a < 3; ++a) {
                    float3 av(a == 0 ? 1.0f : 0.0f, a == 1 ? 1.0f : 0.0f, a == 2 ? 1.0f : 0.0f);
                    ImVec2 d(dot(av, cr), -dot(av, cu));
                    float l = std::sqrt(d.x * d.x + d.y * d.y);
                    if (l < 0.15f) continue;
                    ImVec2 tip(o.x + d.x * len, o.y + d.y * len);
                    if (F.jgAxis < 0 && pfDistSeg(mouse, o, tip) < 7 * s && rc.contains(mouse.x, mouse.y)) F.jgHover = a;
                    bool hot = F.jgHover == a || F.jgAxis == a;
                    ImU32 col = hot ? IM_COL32(255, 230, 120, 255) : kAxis[a];
                    dl->AddLine(o, tip, col, (hot ? 3.2f : 2.2f) * s);
                    dl->AddCircleFilled(tip, (hot ? 6.0f : 4.5f) * s, col);
                }
            } else {
                // rings in world planes, radius fixed on screen
                const float ringW = len / std::max(1e-4f, pxPerUnit(c));
                for (int a = 0; a < 3; ++a) {
                    float3 ax(a == 0 ? 1.0f : 0.0f, a == 1 ? 1.0f : 0.0f, a == 2 ? 1.0f : 0.0f);
                    float3 u = a == 0 ? float3(0, 1, 0) : float3(1, 0, 0), v = cross(ax, u);
                    ImVec2 pts[49];
                    bool all = true;
                    for (int k = 0; k <= 48; ++k) {
                        float t = k * 2 * kPi / 48;
                        all &= pfProject(cam, rc, c + (u * std::cos(t) + v * std::sin(t)) * ringW, pts[k]);
                    }
                    if (!all) continue;
                    if (F.jgAxis < 0 && rc.contains(mouse.x, mouse.y))
                        for (int k = 0; k < 48; ++k)
                            if (pfDistSeg(mouse, pts[k], pts[k + 1]) < 6 * s) { F.jgHover = 3 + a; break; }
                    bool hot = F.jgHover == 3 + a || F.jgAxis == 3 + a;
                    dl->AddPolyline(pts, 49, hot ? IM_COL32(255, 230, 120, 255) : kAxis[a], 0, (hot ? 3.0f : 2.0f) * s);
                }
            }
        }
    }
    // brush
    if (F.rigMode == 2 && F.brushHitValid) {
        float3 n = F.brushNormal, u = normalize(std::fabs(n.y) < 0.9f ? cross(n, float3(0, 1, 0)) : cross(n, float3(1, 0, 0))), v = cross(n, u);
        ImVec2 pts[41];
        bool all = true;
        for (int k = 0; k <= 40; ++k) {
            float t = k * 2 * kPi / 40;
            all &= pfProject(cam, rc, F.brushHit + (u * std::cos(t) + v * std::sin(t)) * F.brushRadius, pts[k]);
        }
        if (all) {
            dl->AddPolyline(pts, 41, IM_COL32(255, 255, 255, 230), 0, 2.0f * s);
            dl->AddPolyline(pts, 41, IM_COL32(0, 0, 0, 120), 0, 0.8f * s);
        }
        ImVec2 cp;
        if (pfProject(cam, rc, F.brushHit, cp)) {
            dl->AddCircleFilled(cp, 2.5f * s, IM_COL32(255, 255, 255, 230));
            // weight of the painted joint at the closest vertex
            const PrefabSkin* sk = F.skinPart >= 0 && F.skinPart < (int)A.parts.size() ? A.skinOf(A.parts[F.skinPart].id) : nullptr;
            const int inf = sk && F.selJoint >= 0 && F.selJoint < (int)A.joints.size() ? sk->influence(A.joints[F.selJoint].id) : -1;
            if (inf >= 0 && F.partObject[F.skinPart] >= 0) {
                const MeshData& m = F.scene.meshes[F.scene.objects[F.partObject[F.skinPart]].mesh];
                size_t best = 0;
                float bd = 1e30f;
                for (size_t v = 0; v < m.vertices.size(); ++v) {
                    float3 d = m.vertices[v].pos - F.brushHit;
                    float dd = dot(d, d);
                    if (dd < bd) { bd = dd; best = v; }
                }
                if (best < sk->vertexCount) {
                    char t[32];
                    snprintf(t, sizeof(t), "%.2f", sk->w(best, (size_t)inf));
                    R wr = {cp.x + 10 * s, cp.y - 24 * s, TextSize(t, 10).x + 12 * s, 18 * s};
                    Box(wr, P().overlay, 3);
                    Text(wr.x + 6 * s, wr.y + 3 * s, t, P().text, 10);
                }
            }
        }
    }
    // mode / joint label (top left), status of the last bind / import
    {
        std::string t = std::string(kRigModes[F.rigMode]);
        if (F.selJoint >= 0 && F.selJoint < (int)A.joints.size()) t += "  ·  " + A.joints[F.selJoint].name;
        if (F.rigMode == 2) t += std::string("  ·  ") + kPaintModes[F.brushMode];
        if (!F.poseOverride.empty() && F.rigMode != 0) t += "  ·  posed";
        R lr = {x + 8 * s, y + 8 * s, TextSize(t, 10.5f).x + 18 * s, 22 * s};
        Box(lr, P().overlay, 4);
        Text(lr.x + 9 * s, lr.y + 5 * s, t, P().text, 10.5f);
        if (F.rigMode == 0) {
            R br = {x + w - 132 * s, y + 8 * s, 124 * s, 22 * s};
            Box(br, IM_COL32(40, 90, 150, 200), 4);
            Center(br, "BIND POSE (REST)", IM_COL32(230, 240, 255, 255), 9.5f);
        }
        if (!F.rigStatus.empty()) {
            R sr = {x + 8 * s, y + h - 30 * s, std::min(w - 16 * s, TextSize(F.rigStatus, 10).x + 18 * s), 22 * s};
            Box(sr, P().overlay, 4);
            dl->PushClipRect(sr.min(), sr.max(), true);
            Text(sr.x + 9 * s, sr.y + 5 * s, F.rigStatus, P().muted, 10);
            dl->PopClipRect();
        }
    }
}

// --------------------------------------------------------------------------- rig inspector (right)

void App::pfRigInspector(float x, float y, float w, float h) {
    PrefabStudio& F = m_pf;
    PrefabAsset& A = F.asset;
    R r = {x, y, w, h};
    F.rigTab = std::clamp(F.rigTab, 0, 2);
    const char* tags[] = {"JOINT", "SKIN", "PAINT"};
    Panel(r, Icon::Info, "Rig Inspector", tags[F.rigTab], P().text);
    const char* tabs[] = {"Joint", "Skin", "Paint"};
    Icon is[] = {Icon::Link, Icon::Material, Icon::Pin};
    float tw = (r.w - 26 * s - 10 * s) / 3, tx = r.x + 13 * s;
    for (int i = 0; i < 3; ++i) {
        char id[24];
        snprintf(id, sizeof(id), "rigTab%d", i);
        if (TabButton(id, {tx, r.y + 40 * s, tw, (P().skin ? 29 : 25) * s}, tabs[i], is[i], F.rigTab == i)) F.rigTab = i;
        tx += tw + 5 * s;
    }
    if (P().skin) dl->AddLine(ImVec2(r.x + 13 * s, r.y + 69 * s), ImVec2(r.x + r.w - 13 * s, r.y + 69 * s), P().border);
    float by = r.y + 76 * s;
    g_labelRight = r.x + r.w - 21 * s;
    ImGui::SetCursorScreenPos(ImVec2(r.x + 13 * s, by));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("rigInspectorBody", ImVec2(r.w - 26 * s, r.y + r.h - by - 9 * s), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImGui::SetWindowFontScale(atelier::Style().layoutScale);
    ImGui::PopStyleVar();
    dl = ImGui::GetWindowDrawList();
    ImVec2 c0 = ImGui::GetCursorScreenPos();
    const float bx = c0.x, bw = r.w - 26 * s - 8 * s;
    float yy = c0.y;
    PrefabClip* clip = prefabClip();
    const bool hasJ = F.selJoint >= 0 && F.selJoint < (int)A.joints.size();
    // skinned part chosen for the Skin / Paint tabs: the selected part when it can be skinned
    if (F.skinPart < 0 || F.skinPart >= (int)A.parts.size() || A.parts[F.skinPart].isPivot()) {
        F.skinPart = -1;
        for (size_t i = 0; i < A.parts.size() && F.skinPart < 0; ++i)
            if (A.skinOf(A.parts[i].id)) F.skinPart = (int)i;
        if (F.skinPart < 0 && F.sel >= 0 && F.sel < (int)A.parts.size() && !A.parts[F.sel].isPivot()) F.skinPart = F.sel;
    }
    PrefabSkin* sk = F.skinPart >= 0 ? A.skinOf(A.parts[F.skinPart].id) : nullptr;
    auto influenceTable = [&](bool pick) {
        if (!sk) return;
        const size_t J = sk->joints.size();
        if (sk->locked.size() != J) sk->locked.resize(J, 0);
        Text(bx, yy, "Joint", P().muted, 9.5f);
        Text(bx + bw * 0.55f, yy, "Verts", P().muted, 9.5f);
        Text(bx + bw * 0.72f, yy, "Max", P().muted, 9.5f);
        yy += 16 * s;
        for (size_t k = 0; k < J; ++k) {
            int ji = A.findJoint(sk->joints[k]);
            if (ji < 0) continue;
            size_t verts = 0;
            float mx = 0;
            for (uint32_t v = 0; v < sk->vertexCount; ++v) { float x = sk->w(v, k); if (x > 0.01f) ++verts; mx = std::max(mx, x); }
            ImGui::PushID((int)k + 5000);
            R row = {bx, yy, bw, 22 * s};
            if (Hit("inf", {bx, yy, bw - 28 * s, 22 * s})) F.selJoint = ji;
            if (ji == F.selJoint) SelectedRow(row);
            else if (ImGui::IsItemHovered()) Box(row, P().hover, 3);
            dl->AddRectFilled(ImVec2(bx + 5 * s, yy + 6 * s), ImVec2(bx + 15 * s, yy + 16 * s), jointColor(sk->joints[k]), 2 * s);
            dl->PushClipRect(ImVec2(bx, yy), ImVec2(bx + bw * 0.54f, yy + 22 * s), true);
            Text(bx + 21 * s, yy + 4 * s, A.joints[ji].name, P().text, 10.5f);
            dl->PopClipRect();
            Text(bx + bw * 0.55f, yy + 4 * s, std::to_string(verts), verts ? P().text : P().muted, 10.5f);
            char m[16];
            snprintf(m, sizeof(m), "%.2f", mx);
            Text(bx + bw * 0.72f, yy + 4 * s, m, P().text, 10.5f);
            R lk = {bx + bw - 24 * s, yy + 2 * s, 20 * s, 18 * s};
            if (Hit("lock", lk)) { prefabPushUndo(); sk->locked[k] = !sk->locked[k]; }
            Tip("Lock: painting and smoothing keep this weight");
            Glyph(Icon::Lock, lk.x + 3 * s, lk.y + 2 * s, 12, sk->locked[k] ? kAmber : Fade(P().muted, 120));
            ImGui::PopID();
            yy += 24 * s;
        }
        (void)pick;
    };

    if (F.rigTab == 0) {
        // ------------------------------------------------------------ Joint
        if (!hasJ) {
            Text(bx, yy, "No joint selected", P().muted, 11);
            yy += 26 * s;
            Box({bx, yy, bw, 98 * s}, P().bg, 4, true);
            Text(bx + 12 * s, yy + 12 * s, "SKELETON", P().accent, 9);
            Text(bx + 12 * s, yy + 35 * s, "Click a joint in the view or the list,", P().muted, 10);
            Text(bx + 12 * s, yy + 54 * s, "Joint / Child / Chain to create them,", P().muted, 10);
            Text(bx + 12 * s, yy + 73 * s, "drag a joint on another to parent it.", P().muted, 10);
            yy += 110 * s;
        } else {
            PrefabJoint& j = A.joints[F.selJoint];
            Box({bx, yy, bw, 56 * s}, Fade(P().accent, 30), 4, true);
            dl->AddCircleFilled(ImVec2(bx + 24 * s, yy + 28 * s), 10 * s, jointColor(j.id));
            dl->PushClipRect(ImVec2(bx, yy), ImVec2(bx + bw - 8 * s, yy + 56 * s), true);
            Text(bx + 48 * s, yy + 11 * s, j.name, P().text, 14);
            int pj = j.parent ? A.findJoint(j.parent) : -1;
            std::string type = pj >= 0 ? "Joint  ·  child of " + A.joints[pj].name : std::string("Root joint");
            Text(bx + 48 * s, yy + 33 * s, type, P().accent, 10);
            dl->PopClipRect();
            yy += 68 * s;
            {
                char name[96];
                snprintf(name, sizeof(name), "%s", j.name.c_str());
                Text(bx, yy, "Name", P().muted, 11);
                yy += 19 * s;
                ImGui::SetCursorScreenPos(ImVec2(bx, yy));
                ImGui::SetNextItemWidth(bw);
                if (ImGui::InputText("##jName", name, sizeof(name), ImGuiInputTextFlags_EnterReturnsTrue) && *name) { prefabPushUndo(); j.name = name; }
                yy += 36 * s;
            }
            // rest (bind pose): skeleton edits
            Label(bx, yy, F.rigMode == 0 ? "REST (BIND POSE) · EDITING" : "REST (BIND POSE)");
            yy += 24 * s;
            const char* chn[] = {"Position", "Rotation", "Scale"};
            for (int ch = 0; ch < CH_COUNT; ++ch) {
                Text(bx, yy, chn[ch], P().muted, 11);
                yy += 19 * s;
                float3 v = ch == CH_POSITION ? j.position : ch == CH_ROTATION ? j.rotation : j.scale;
                bool a = false;
                char id[24];
                snprintf(id, sizeof(id), "jRest%d", ch);
                if (AxisFields(id, bx, yy, bw, &v.x, ch == CH_ROTATION ? 0.5f : 0.01f, ch == CH_ROTATION ? "%.1f" : "%.3f", a)) {
                    if (a) prefabPushUndo();
                    (ch == CH_POSITION ? j.position : ch == CH_ROTATION ? j.rotation : j.scale) = v;
                    F.weightsVersion++;
                } else if (a) prefabPushUndo();
                yy += 36 * s;
            }
            {
                bool a = false;
                SliderRow("##jRadius", "Display radius", bx, yy, bw, &j.radius, 0.005f, 0.5f, "%.3f", a);
                if (a) prefabPushUndo();
                float b2 = (bw - 5 * s) / 2;
                if (Button("jOrient", {bx, yy, b2, 26 * s}, "Orient to Child", Icon::Rotate, false, false, !A.jointChildren(j.id).empty())) rigOrientJoint();
                Tip("The joint aims +Y at its first child; the children keep their place (rest pose)");
                if (Button("jMirror", {bx + b2 + 5 * s, yy, b2, 26 * s}, "Mirror Joints", Icon::Link)) rigMirrorJoints();
                Tip("Creates the other side of the joints named Left / _L / l_ (or Right / _R / r_), mirrored on X");
                yy += 36 * s;
            }
            // pose (clip value, override): keys with the diamond, else a pose override
            Label(bx, yy, "POSE");
            yy += 24 * s;
            PartPose pp;
            rigJointPose(F.selJoint, pp);
            const float snapT = clip ? std::round(F.time * clip->fps) / clip->fps : 0.0f;
            for (int ch = 0; ch < CH_COUNT; ++ch) {
                const PrefabTrack* t = clip ? clip->jointTrack(j.id, ch) : nullptr;
                const bool animated = t && !t->keys.empty();
                const bool onKey = animated && t->keyAt(snapT, 0.5f / clip->fps) >= 0;
                Text(bx, yy, chn[ch], animated ? kAmber : P().muted, 11);
                R kr = {bx + bw - 22 * s, yy - 3 * s, 20 * s, 18 * s};
                char kid[24];
                snprintf(kid, sizeof(kid), "jKey%d", ch);
                if (Hit(kid, kr, clip != nullptr)) prefabKeyJoint(ch);
                Tip(clip ? "Key this channel of the joint at the current frame" : "Add a clip to animate (Asset tab)");
                {
                    ImVec2 c(kr.x + kr.w / 2, kr.y + kr.h / 2);
                    float k = 5 * s;
                    ImU32 kc = onKey ? kAmber : animated ? Fade(kAmber, 200) : P().muted;
                    if (onKey) dl->AddQuadFilled(ImVec2(c.x, c.y - k), ImVec2(c.x + k, c.y), ImVec2(c.x, c.y + k), ImVec2(c.x - k, c.y), kc);
                    else dl->AddQuad(ImVec2(c.x, c.y - k), ImVec2(c.x + k, c.y), ImVec2(c.x, c.y + k), ImVec2(c.x - k, c.y), kc, 1.3f * s);
                }
                yy += 19 * s;
                float3 v = ch == CH_POSITION ? pp.position : ch == CH_ROTATION ? pp.rotation : pp.scale;
                bool a = false;
                char id[24];
                snprintf(id, sizeof(id), "jPose%d", ch);
                ImGui::BeginDisabled(F.rigMode == 0);
                if (AxisFields(id, bx, yy, bw, &v.x, ch == CH_ROTATION ? 0.5f : 0.01f, ch == CH_ROTATION ? "%.1f" : "%.3f", a)) {
                    if (a) prefabPushUndo();
                    rigSetJointLocal(F.selJoint, ch, v);
                    F.weightsVersion++;
                } else if (a) prefabPushUndo();
                ImGui::EndDisabled();
                yy += 36 * s;
            }
            if (F.rigMode == 0) { Text(bx, yy, "Pose mode (or Skin / Paint) to pose the joint.", P().muted, 9.5f); yy += 20 * s; }
            float b2 = (bw - 5 * s) / 2;
            if (Button("jResetPose", {bx, yy, b2, 26 * s}, "Reset Pose", Icon::Refresh, false, false, !F.poseOverride.empty())) F.poseOverride.clear();
            Tip("Back to the rest pose (the pose overrides are cleared; keys stay)");
            if (Button("jKeyAll", {bx + b2 + 5 * s, yy, b2, 26 * s}, "Key Joint", Icon::Pin, false, false, clip != nullptr)) prefabKeyJoint();
            Tip("Key position, rotation and scale of the joint at this frame (K)");
            yy += 34 * s;
            Label(bx, yy, "WORLD");
            yy += 22 * s;
            if (F.selJoint < (int)F.jointNow.size()) {
                const PartWorld& wv = F.jointNow[F.selJoint];
                char t[96];
                snprintf(t, sizeof(t), "Position  %.3f  %.3f  %.3f", wv.position.x, wv.position.y, wv.position.z);
                Text(bx, yy, t, P().text, 10.5f);
                yy += 18 * s;
                float3 e = quatToEuler(wv.rot);
                snprintf(t, sizeof(t), "Rotation  %.1f  %.1f  %.1f", e.x, e.y, e.z);
                Text(bx, yy, t, P().text, 10.5f);
                yy += 24 * s;
            }
        }
    } else if (F.rigTab == 1) {
        // ------------------------------------------------------------ Skin: bind, clean, influences
        std::vector<std::string> names;
        std::vector<int> idx;
        int cur = 0;
        for (size_t i = 0; i < A.parts.size(); ++i) {
            if (A.parts[i].isPivot()) continue;
            if ((int)i == F.skinPart) cur = (int)names.size();
            names.push_back(A.parts[i].name + (A.skinOf(A.parts[i].id) ? "  (skinned)" : ""));
            idx.push_back((int)i);
        }
        std::vector<const char*> opts;
        for (auto& n : names) opts.push_back(n.c_str());
        Text(bx, yy, "Part", P().muted, 11);
        yy += 19 * s;
        if (!opts.empty() && Combo("skinPartCombo", {bx, yy, bw, 26 * s}, cur, opts.data(), (int)opts.size(), Icon::Cube)) { F.skinPart = idx[cur]; F.sel = idx[cur]; }
        yy += 36 * s;
        Label(bx, yy, "BIND");
        yy += 24 * s;
        static const char* methods[] = {"Closest Bone (rigid)", "Smooth (distance falloff)"};
        Text(bx, yy, "Method", P().muted, 11);
        yy += 19 * s;
        Combo("bindMethod", {bx, yy, bw, 26 * s}, F.bindOpt.method, methods, 2, Icon::Link);
        yy += 36 * s;
        bool a = false;
        float mi = (float)F.bindOpt.maxInfluences, sp = (float)F.bindOpt.smoothPasses;
        SliderRow("##bindMax", "Max influences", bx, yy, bw, &mi, 1, 8, "%.0f", a);
        if (F.bindOpt.method == 1) {
            SliderRow("##bindFall", "Falloff", bx, yy, bw, &F.bindOpt.falloff, 1, 12, "%.1f", a);
            SliderRow("##bindSmooth", "Smoothing passes", bx, yy, bw, &sp, 0, 20, "%.0f", a);
        }
        F.bindOpt.maxInfluences = (int)std::lround(mi);
        F.bindOpt.smoothPasses = (int)std::lround(sp);
        const bool canBind = F.skinPart >= 0 && !A.joints.empty();
        if (PrimaryButton("bindBtn", {bx, yy, bw, 28 * s}, sk ? "Rebind Skin" : "Bind Skin", Icon::Link, canBind)) rigBind(F.skinPart);
        Tip("Binds the part to every joint at the rest pose (weights from the bones, see the method)");
        yy += 36 * s;
        if (sk) {
            if (Button("bindPoseBtn", {bx, yy, bw, 26 * s}, "Update Bind Pose (keep weights)", Icon::Refresh)) {
                prefabPushUndo();
                std::vector<PartWorld> rest;
                restJointWorld(A, rest);
                for (size_t k = 0; k < sk->joints.size(); ++k) {
                    int ji = A.findJoint(sk->joints[k]);
                    if (ji >= 0) sk->bindJoint[k] = toAffine(rest[ji]);
                }
                F.weightsVersion++;
                status(L"Bind pose updated: the joints at rest no longer deform the skin", 1);
            }
            Tip("After moving joints in Skeleton mode: their rest pose becomes the bind pose again, the weights stay");
            yy += 32 * s;
            if (Button("exportGltfBtn", {bx, yy, bw, 26 * s}, "Export glTF (mesh, skeleton, skin, clips)", Icon::Save)) {
                std::wstring p = pickFileDialog(m_hwnd, true, L"glTF (*.gltf)\0*.gltf\0", L"gltf", projectRoot() + L"\assets", fromUtf8(A.parts[F.skinPart].name) + L".gltf");
                if (!p.empty()) rigExportGltf(F.skinPart, p);
            }
            Tip("Writes a .gltf (one file, embedded data) that other tools (Blender, Unity, Godot) read with the rig and the animations");
            yy += 32 * s;
            if (Button("unbindBtn", {bx, yy, bw, 26 * s}, "Unbind (remove the skin)", Icon::Trash)) { rigUnbind(F.skinPart); sk = nullptr; }
            yy += 36 * s;
        }
        if (sk) {
            static const char* deformers[] = {"Linear (classic)", "Dual Quaternion"};
            int dm = sk->dualQuaternion ? 1 : 0;
            Text(bx, yy, "Deformation", P().muted, 11);
            yy += 19 * s;
            if (Combo("skinDeform", {bx, yy, bw, 26 * s}, dm, deformers, 2, Icon::Rotate)) { prefabPushUndo(); sk->dualQuaternion = dm == 1; F.weightsVersion++; }
            Tip("Linear blend skinning (classic, the volume shrinks at twisted joints) or dual quaternion (keeps the volume)");
            yy += 36 * s;
            Label(bx, yy, "WEIGHTS");
            yy += 24 * s;
            {
                // statistics: influences per vertex, unnormalised vertices
                int maxPer = 0, over = 0, unnorm = 0, zero = 0;
                for (uint32_t v = 0; v < sk->vertexCount; ++v) {
                    int n = 0;
                    float sum = 0;
                    for (size_t k = 0; k < sk->joints.size(); ++k) { float x = sk->w(v, k); if (x > 0.001f) ++n; sum += x; }
                    maxPer = std::max(maxPer, n);
                    over += n > sk->maxInfluences;
                    unnorm += std::fabs(sum - 1) > 0.01f && sum > 0;
                    zero += sum <= 0.0001f;
                }
                char t[96];
                snprintf(t, sizeof(t), "%u vertices  ·  %zu influences", sk->vertexCount, sk->joints.size());
                Text(bx, yy, t, P().text, 10.5f);
                yy += 18 * s;
                auto line = [&](bool ok, const std::string& s2) {
                    Glyph(ok ? Icon::Check : Icon::Info, bx, yy + 1 * s, 12, ok ? kGreen : kAmber);
                    Text(bx + 18 * s, yy, s2, ok ? P().text : kAmber, 10.5f);
                    yy += 18 * s;
                };
                snprintf(t, sizeof(t), "Up to %d influences per vertex (limit %d)", maxPer, sk->maxInfluences);
                line(over == 0, t);
                snprintf(t, sizeof(t), unnorm ? "%d vertices not normalised" : "Weights normalised", unnorm);
                line(unnorm == 0, t);
                snprintf(t, sizeof(t), zero ? "%d vertices without weight" : "Every vertex weighted", zero);
                line(zero == 0, t);
                yy += 6 * s;
            }
            float b2 = (bw - 5 * s) / 2;
            if (Button("wNorm", {bx, yy, b2, 25 * s}, "Normalize", Icon::Check)) { prefabPushUndo(); normalizeWeights(*sk); F.weightsVersion++; }
            Tip("Every vertex: weights sum to 1");
            if (Button("wPrune", {bx + b2 + 5 * s, yy, b2, 25 * s}, "Prune < 0.02", Icon::Filter)) { prefabPushUndo(); pruneWeights(*sk, 0.02f); F.weightsVersion++; }
            Tip("Removes the tiny weights, then normalises");
            yy += 30 * s;
            if (Button("wLimit", {bx, yy, b2, 25 * s}, "Limit to Max", Icon::List)) { prefabPushUndo(); limitInfluences(*sk, F.bindOpt.maxInfluences); F.weightsVersion++; }
            Tip("Keeps the largest weights per vertex (Max influences above)");
            if (Button("wSmooth", {bx + b2 + 5 * s, yy, b2, 25 * s}, "Smooth All", Icon::Refresh)) {
                prefabPushUndo();
                auto g = F.weld.find(sk->part);
                if (g != F.weld.end()) smoothWeights(*sk, g->second, 2, 0.6f);
                F.weightsVersion++;
            }
            Tip("Two smoothing passes over the whole mesh (locked influences kept)");
            yy += 30 * s;
            if (Button("wMirror", {bx, yy, bw, 25 * s}, "Mirror  +X  to  -X", Icon::ArrowLeft)) {
                prefabPushUndo();
                int oi = F.partObject.size() > (size_t)F.skinPart ? F.partObject[F.skinPart] : -1;
                int n = oi >= 0 && F.skinPart < (int)F.partRest.size() ? mirrorWeights(*sk, A, F.partRest[F.skinPart], true) : 0;
                F.weightsVersion++;
                status(fromUtf8("Weights mirrored on " + std::to_string(n) + " vertices (joints matched by name: Left / Right, _L / _R)"), 1);
            }
            Tip("Copies the weights of the +X side to the -X side, the joints swapped by name (Left / Right, _L / _R, l_ / r_)");
            yy += 34 * s;
            Label(bx, yy, "INFLUENCES");
            yy += 22 * s;
            influenceTable(true);
            yy += 6 * s;
            static const char* views[] = {"Selected joint (heat)", "All joints (colours)"};
            Text(bx, yy, "Display", P().muted, 11);
            yy += 19 * s;
            Combo("weightView", {bx, yy, bw, 26 * s}, F.weightView, views, 2, Icon::Material);
            yy += 36 * s;
        } else if (F.skinPart >= 0) {
            Text(bx, yy, A.joints.empty() ? "Create joints first (Skeleton panel)." : "Not skinned: Bind Skin above.", P().muted, 10.5f);
            yy += 24 * s;
        }
    } else {
        // ------------------------------------------------------------ Paint
        if (!sk) {
            Text(bx, yy, "No skin on this part: Skin tab, Bind Skin.", P().muted, 10.5f);
            yy += 24 * s;
        } else {
            Label(bx, yy, "BRUSH");
            yy += 24 * s;
            float pw4 = (bw - 15 * s) / 4;
            for (int i = 0; i < 4; ++i) {
                char id[24];
                snprintf(id, sizeof(id), "paintMode%d", i);
                if (Button(id, {bx + i * (pw4 + 5 * s), yy, pw4, 26 * s}, kPaintModes[i], i == 3 ? Icon::Refresh : i == 1 ? Icon::ArrowLeft : i == 2 ? Icon::Check : Icon::Plus, F.brushMode == i))
                    F.brushMode = i;
            }
            yy += 34 * s;
            bool a = false;
            if (F.brushMode != PAINT_SMOOTH) SliderRow("##brushValue", F.brushMode == PAINT_REPLACE ? "Value" : "Amount", bx, yy, bw, &F.brushValue, 0, 1, "%.2f", a);
            SliderRow("##brushRadius", "Radius (Ctrl+wheel)", bx, yy, bw, &F.brushRadius, 0.01f, 3.0f, "%.3f", a);
            SliderRow("##brushStrength", "Strength", bx, yy, bw, &F.brushStrength, 0.01f, 1.0f, "%.2f", a);
            Text(bx, yy, "Falloff", P().muted, 11);
            yy += 19 * s;
            Combo("brushFalloff", {bx, yy, bw, 26 * s}, F.brushFalloff, kFalloffs, 3, Icon::Filter);
            yy += 36 * s;
            float b2 = (bw - 5 * s) / 2;
            const int inf = hasJ ? sk->influence(A.joints[F.selJoint].id) : -1;
            if (Button("flood", {bx, yy, b2, 25 * s}, "Flood", Icon::Grid, false, false, inf >= 0)) {
                prefabPushUndo();
                std::vector<float> all(sk->vertexCount, 1.0f);
                auto g = F.weld.find(sk->part);
                int pj = A.joints[F.selJoint].parent ? A.findJoint(A.joints[F.selJoint].parent) : -1;
                if (g != F.weld.end())
                    paintWeights(*sk, g->second, inf, F.brushMode == PAINT_SMOOTH ? PAINT_SMOOTH : PAINT_REPLACE, F.brushValue, all, pj >= 0 ? sk->influence(A.joints[pj].id) : -1);
                F.weightsVersion++;
            }
            Tip("Applies the value (Replace) or a smoothing pass to every vertex for the selected joint");
            static const char* views[] = {"Heat", "Colours"};
            int wv = F.weightView;
            if (Combo("paintView", {bx + b2 + 5 * s, yy, b2, 25 * s}, wv, views, 2, Icon::Material)) F.weightView = wv;
            yy += 34 * s;
            Label(bx, yy, "PAINTED JOINT");
            yy += 22 * s;
            if (inf < 0) { Text(bx, yy, hasJ ? "The selected joint is not an influence." : "Select a joint below or in the view.", kAmber, 10); yy += 18 * s; }
            influenceTable(true);
            yy += 8 * s;
            Text(bx, yy, "Left drag on the part paints, right drag orbits.", P().muted, 9.5f);
            yy += 15 * s;
            Text(bx, yy, "Heat: blue 0, green 0.5, red 1.", P().muted, 9.5f);
            yy += 18 * s;
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(bx, yy));
    ImGui::Dummy(ImVec2(1, 8 * s));
    ImGui::EndChild();
    dl = ImGui::GetWindowDrawList();
}

} // namespace app
