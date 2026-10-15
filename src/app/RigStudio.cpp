// Rig module of the Prefab studio (centre tab "Rig"): skeleton editing, binding, pose, keys, weight painting and the
// viewport input (joint picking, move / rotate gizmo, brush). Panels: RigUI.inl. Core maths: Rig.cpp.
#include "App.h"
#include "FbxImport.h"
#include <filesystem>

namespace app {

namespace fs = std::filesystem;

namespace {
float4 qInv(const float4& q) { return float4(-q.x, -q.y, -q.z, q.w); }
}

// --------------------------------------------------------------------------- skeleton editing

void App::rigAddJoint(bool child) {
    PrefabStudio& P = m_pf;
    PrefabAsset& A = P.asset;
    prefabPushUndo();
    PrefabJoint j;
    const bool hasSel = P.selJoint >= 0 && P.selJoint < (int)A.joints.size();
    if (child && hasSel) {
        const PrefabJoint& s = A.joints[P.selJoint];
        j.parent = s.id;
        // along the bone that ends at the selected joint (its own local offset), else up
        float3 d = s.position;
        float l = length(d);
        j.position = l > 1e-4f ? d : float3(0, 0.5f, 0);
        j.radius = s.radius * 0.9f;
    } else if (hasSel) {
        const PrefabJoint& s = A.joints[P.selJoint];
        j.parent = s.parent;
        j.position = s.position + float3(0.3f, 0, 0);
    }
    std::string base = "joint";
    int n = (int)A.joints.size() + 1;
    for (;; ++n) {
        std::string name = base + std::to_string(n);
        bool used = false;
        for (const PrefabJoint& q : A.joints) used |= q.name == name;
        if (!used) { j.name = name; break; }
    }
    A.addJoint(j);
    P.selJoint = (int)A.joints.size() - 1;
    P.weightsVersion++;
}

void App::rigAddChain(int count, float length) {
    PrefabStudio& P = m_pf;
    PrefabAsset& A = P.asset;
    prefabPushUndo();
    uint32_t parent = P.selJoint >= 0 && P.selJoint < (int)A.joints.size() ? A.joints[P.selJoint].id : 0;
    for (int i = 0; i < count; ++i) {
        PrefabJoint j;
        j.parent = parent;
        j.position = parent || i ? float3(0, length / std::max(1, count - 1), 0) : float3(0);
        j.name = "chain_" + std::to_string(i + 1);
        for (int n = 2; ; ++n) {
            bool used = false;
            for (const PrefabJoint& q : A.joints) used |= q.name == j.name;
            if (!used) break;
            j.name = "chain_" + std::to_string(i + 1) + "_" + std::to_string(n);
        }
        parent = A.addJoint(j);
    }
    P.selJoint = (int)A.joints.size() - 1;
    P.weightsVersion++;
}

void App::rigDeleteJoint() {
    PrefabStudio& P = m_pf;
    PrefabAsset& A = P.asset;
    if (P.selJoint < 0 || P.selJoint >= (int)A.joints.size()) return;
    prefabPushUndo();
    std::string name = A.joints[P.selJoint].name;
    A.removeJoint(A.joints[P.selJoint].id);
    P.selJoint = std::min(P.selJoint, (int)A.joints.size() - 1);
    P.weightsVersion++;
    P.rebuild = true;
    status(fromUtf8("Joint deleted: " + name));
}

void App::rigBind(int part) {
    PrefabStudio& P = m_pf;
    PrefabAsset& A = P.asset;
    if (part < 0 || part >= (int)A.parts.size() || A.parts[part].isPivot() || A.joints.empty()) return;
    MeshData mesh;
    std::string err;
    if (!prefabMeshFor(A.parts[part], mesh, &err)) { status(L"No mesh to bind", 2); return; }
    prefabPushUndo();
    // the part at its rest transform places the mesh in the bind pose
    std::vector<PartPose> pose;
    std::vector<PartWorld> world;
    evaluatePose(A, nullptr, 0, pose);
    worldTransforms(A, pose, world);
    std::vector<uint32_t> joints;
    for (const PrefabJoint& j : A.joints) joints.push_back(j.id);
    WeldGraph g;
    g.build(mesh);
    PrefabSkin* s = A.skinOf(A.parts[part].id);
    if (!s) {
        A.skins.emplace_back();
        s = &A.skins.back();
        s->part = A.parts[part].id;
    }
    bindSkin(*s, A, mesh, toAffine(world[part]), joints, P.bindOpt, g);
    P.poseOverride.clear();
    P.weightsVersion++;
    P.rebuild = true;
    char b[160];
    snprintf(b, sizeof(b), "Bound %s to %zu joints (%u vertices, %s, %d influences max)", A.parts[part].name.c_str(), joints.size(), s->vertexCount,
             P.bindOpt.method == 0 ? "closest bone" : "smooth", P.bindOpt.maxInfluences);
    P.rigStatus = b;
    status(fromUtf8(b), 1);
}

void App::rigUnbind(int part) {
    PrefabStudio& P = m_pf;
    PrefabAsset& A = P.asset;
    if (part < 0 || part >= (int)A.parts.size()) return;
    prefabPushUndo();
    uint32_t id = A.parts[part].id;
    A.skins.erase(std::remove_if(A.skins.begin(), A.skins.end(), [&](const PrefabSkin& s) { return s.part == id; }), A.skins.end());
    P.rebuild = true;
    status(fromUtf8("Skin removed: " + A.parts[part].name));
}

void App::rigJointPose(int joint, PartPose& local) const {
    const PrefabStudio& P = m_pf;
    if (joint >= 0 && joint < (int)P.jointPose.size()) { local = P.jointPose[joint]; return; }
    const PrefabJoint& j = P.asset.joints[joint];
    local = {j.position, j.rotation, j.scale};
}

// Skeleton mode edits the rest (bind) transform; the other modes pose the joint: a key when the clip animates the
// channel or Auto Key is on, else a pose override (not saved, Reset Pose clears it).
void App::rigSetJointLocal(int joint, int channel, const float3& v) {
    PrefabStudio& P = m_pf;
    PrefabAsset& A = P.asset;
    if (joint < 0 || joint >= (int)A.joints.size()) return;
    PrefabJoint& j = A.joints[joint];
    if (P.rigMode == 0) {
        (channel == CH_POSITION ? j.position : channel == CH_ROTATION ? j.rotation : j.scale) = v;
        return;
    }
    PrefabClip* c = prefabClip();
    PrefabTrack* t = c ? c->jointTrack(j.id, channel, P.autoKey) : nullptr;
    if (t) {
        t->setKey(std::round(P.time * c->fps) / c->fps, v, INTERP_SMOOTH, 0.5f / c->fps);
        P.poseOverride.erase(j.id);
        return;
    }
    PartPose pp;
    rigJointPose(joint, pp);
    (channel == CH_POSITION ? pp.position : channel == CH_ROTATION ? pp.rotation : pp.scale) = v;
    P.poseOverride[j.id] = pp;
}

void App::prefabKeyJoint(int channel) {
    PrefabStudio& P = m_pf;
    PrefabAsset& A = P.asset;
    PrefabClip* c = prefabClip();
    if (!c || P.selJoint < 0 || P.selJoint >= (int)A.joints.size()) return;
    prefabPushUndo();
    const float t = std::round(P.time * c->fps) / c->fps;
    PartPose pp;
    rigJointPose(P.selJoint, pp);
    for (int ch = 0; ch < CH_COUNT; ++ch) {
        if (channel >= 0 && ch != channel) continue;
        c->jointTrack(A.joints[P.selJoint].id, ch, true)->setKey(t, ch == CH_POSITION ? pp.position : ch == CH_ROTATION ? pp.rotation : pp.scale,
                                                                  INTERP_SMOOTH, 0.5f / c->fps);
    }
    P.poseOverride.erase(A.joints[P.selJoint].id);
    status(fromUtf8("Key set: " + A.joints[P.selJoint].name), 1);
}

void App::rigLoadSample() {
    PrefabStudio& P = m_pf;
    P.asset = makeRigSample();
    P.path.clear();
    P.savedJson.clear();
    P.sel = 0;
    P.selJoint = 1;
    P.skinPart = 0;
    P.clip = 0;
    P.time = 0;
    P.playing = false;
    P.poseOverride.clear();
    P.undo.clear();
    P.redo.clear();
    P.rebuild = true;
    P.centerTab = 2;
    prefabFrameAll();
    status(L"Rig sample: a cylinder with three joints, smooth skin, Bend clip", 1);
}

// --------------------------------------------------------------------------- FBX import

void App::rigImportFbx() {
    std::wstring path = pickFileDialog(m_hwnd, false, L"FBX (*.fbx)\0*.fbx\0All files\0*.*\0", L"fbx", projectRoot() + L"\\assets", L"");
    if (!path.empty()) rigImportFile(path);
}

// Adds the FBX as a part (mesh read from the file), its skeleton as joints and its skin clusters as the skin of the
// part. A large file (centimetres) is scaled to about two cells: the joints and the bind matrices are scaled with it.
bool App::rigImportFile(const std::wstring& path) {
    PrefabStudio& P = m_pf;
    PrefabAsset& A = P.asset;
    FbxScene fbx;
    std::string err;
    if (!loadFbx(path, fbx, &err)) {
        P.rigStatus = "FBX not imported: " + err;
        status(fromUtf8(P.rigStatus), 2);
        return false;
    }
    prefabPushUndo();
    float3 mn, mx;
    fbx.mesh.bounds(mn, mx);
    float ext = std::max(mx.x - mn.x, std::max(mx.y - mn.y, mx.z - mn.z));
    const float f = ext > 8.0f ? 2.0f / ext : 1.0f;
    PrefabPart part;
    part.name = toUtf8(fs::path(path).stem().wstring());
    part.meshPath = relPath(path);
    part.recenter = false;
    part.scale = float3(f);
    part.material.baseColor = float3(0.6f, 0.6f, 0.62f);
    part.material.roughness = 0.6f;
    part.material.name = part.name;
    uint32_t partId = A.addPart(part);
    // joints: the FBX ids -> ours; translations scaled by f (a uniform scale of the whole scene)
    std::map<int, uint32_t> jointId;
    for (size_t i = 0; i < fbx.joints.size(); ++i) {
        const FbxJoint& fj = fbx.joints[i];
        PrefabJoint j;
        j.name = fj.name;
        j.parent = fj.parent >= 0 && jointId.count(fj.parent) ? jointId[fj.parent] : 0;
        j.position = fj.translation * f;
        j.rotation = quatToEuler(fj.rotation);
        j.scale = fj.scale;
        j.radius = std::max(0.015f, ext * f * 0.012f);
        jointId[(int)i] = A.addJoint(j);
    }
    auto scaled = [&](const Affine& m) {   // S(f) * m * S(1/f)
        Affine r = m;
        for (int k = 0; k < 3; ++k) r.m[k][3] *= f;
        return r;
    };
    int bound = 0;
    if (!fbx.clusters.empty()) {
        PrefabSkin s;
        s.part = partId;
        s.vertexCount = (uint32_t)fbx.mesh.vertices.size();
        Affine meshBind;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 4; ++c) meshBind.m[r][c] = (r == c ? f : 0.0f);   // vertices are baked in the FBX world (bind)
        s.bindMesh = meshBind;
        for (const FbxCluster& cl : fbx.clusters) {
            if (!jointId.count(cl.joint)) continue;
            s.joints.push_back(jointId[cl.joint]);
            s.locked.push_back(0);
            s.bindJoint.push_back(scaled(cl.bindJoint));
        }
        s.weights.assign((size_t)s.vertexCount * s.joints.size(), 0.0f);
        size_t k = 0;
        for (const FbxCluster& cl : fbx.clusters) {
            if (!jointId.count(cl.joint)) continue;
            for (size_t e = 0; e < cl.vertices.size(); ++e)
                if (cl.vertices[e] < s.vertexCount) s.w(cl.vertices[e], k) += (float)cl.weights[e];
            ++k;
        }
        normalizeWeights(s);
        s.maxInfluences = 4;
        bound = (int)s.joints.size();
        if (bound) A.skins.push_back(std::move(s));
    }
    // animation stacks -> clips: one key per frame (linear), only the channels that move; Euler angles unwrapped
    const float scaleF = f;
    int clipsAdded = 0;
    for (const FbxClip& fc : fbx.clips) {
        PrefabClip clip;
        clip.name = fc.name.empty() ? "FBX Clip" : fc.name;
        clip.fps = fc.fps;
        clip.length = std::max(0.05f, (fc.frames - 1) / fc.fps);
        clip.loop = true;
        for (size_t j = 0; j < fbx.joints.size(); ++j) {
            if (!jointId.count((int)j)) continue;
            for (int ch = 0; ch < CH_COUNT; ++ch) {
                std::vector<float3> v(fc.frames);
                for (int f = 0; f < fc.frames; ++f) {
                    if (ch == CH_POSITION) v[f] = fc.translation[j][f] * scaleF;
                    else if (ch == CH_ROTATION) v[f] = quatToEuler(fc.rotation[j][f]);
                    else v[f] = fc.scale[j][f];
                }
                if (ch == CH_ROTATION)
                    for (int f = 1; f < fc.frames; ++f)
                        for (int a = 0; a < 3; ++a) {
                            while (v[f][a] - v[f - 1][a] > 180.0f) v[f][a] -= 360.0f;
                            while (v[f][a] - v[f - 1][a] < -180.0f) v[f][a] += 360.0f;
                        }
                float spread = 0;
                for (int f = 1; f < fc.frames; ++f) spread = std::max(spread, length(v[f] - v[0]));
                const float eps = ch == CH_ROTATION ? 0.05f : ch == CH_POSITION ? 1e-4f : 1e-4f;
                if (spread < eps) continue;   // still channel: no track
                PrefabTrack t;
                t.part = jointId[(int)j];
                t.joint = true;
                t.channel = ch;
                for (int f = 0; f < fc.frames; ++f) {
                    PrefabKey k;
                    k.time = f / fc.fps;
                    k.value = v[f];
                    k.interp = INTERP_LINEAR;
                    t.keys.push_back(k);
                }
                clip.tracks.push_back(std::move(t));
            }
        }
        if (!clip.tracks.empty()) { A.clips.push_back(std::move(clip)); ++clipsAdded; }
    }
    if (clipsAdded) { P.clip = (int)A.clips.size() - clipsAdded; P.time = 0; }
    P.sel = A.findPart(partId);
    P.skinPart = P.sel;
    P.selJoint = A.joints.empty() ? -1 : (int)A.joints.size() - (int)fbx.joints.size();
    P.poseOverride.clear();
    P.weightsVersion++;
    P.rebuild = true;
    char b[256];
    snprintf(b, sizeof(b), "FBX imported: %zu vertices, %zu triangles, %zu joints, %d skin influences, %d UV sets, %d clips (scale %.4g)",
             fbx.mesh.vertices.size(), fbx.mesh.triangleCount(), fbx.joints.size(), bound, (int)fbx.uvSets.size(), clipsAdded, f);
    P.rigStatus = b;
    log(b, 1);
    status(fromUtf8(b), 1);
    prefabFrameAll();
    return true;
}

// --------------------------------------------------------------------------- viewport: joints, gizmo, brush

void App::rigPaintStamp(bool first) {
    PrefabStudio& P = m_pf;
    PrefabAsset& A = P.asset;
    if (P.skinPart < 0 || P.skinPart >= (int)A.parts.size() || !P.brushHitValid) return;
    PrefabSkin* sk = A.skinOf(A.parts[P.skinPart].id);
    int oi = P.skinPart < (int)P.partObject.size() ? P.partObject[P.skinPart] : -1;
    if (!sk || oi < 0 || P.selJoint < 0 || P.selJoint >= (int)A.joints.size()) return;
    const int inf = sk->influence(A.joints[P.selJoint].id);
    if (inf < 0) { status(L"The selected joint is not an influence of this skin (Skin tab: Rebind)", 2); return; }
    if (first) prefabPushUndo();
    const MeshData& m = P.scene.meshes[P.scene.objects[oi].mesh];   // deformed, world space
    std::vector<float> factor(sk->vertexCount, 0.0f);
    bool any = false;
    for (size_t v = 0; v < m.vertices.size() && v < factor.size(); ++v) {
        float d = length(m.vertices[v].pos - P.brushHit);
        if (d > P.brushRadius) continue;
        float x = d / std::max(1e-5f, P.brushRadius);
        float fall = P.brushFalloff == 2 ? 1.0f : P.brushFalloff == 1 ? 1.0f - x : (1.0f - x * x) * (1.0f - x * x);
        factor[v] = P.brushStrength * fall;
        any = true;
    }
    if (!any) return;
    int pj = A.joints[P.selJoint].parent ? A.findJoint(A.joints[P.selJoint].parent) : -1;
    int fallback = pj >= 0 ? sk->influence(A.joints[pj].id) : -1;
    auto g = P.weld.find(sk->part);
    if (g == P.weld.end()) return;
    paintWeights(*sk, g->second, inf, P.brushMode, P.brushMode == PAINT_REPLACE ? P.brushValue : P.brushValue * 0.5f, factor, fallback);
    P.weightsVersion++;
}

void App::rigViewInput(bool hovered) {
    PrefabStudio& P = m_pf;
    PrefabAsset& A = P.asset;
    ImGuiIO& io = ImGui::GetIO();
    const Camera& cam = P.camera;
    auto ray = [&](float px, float py, float3& o, float3& d) {
        float3 r, u, f;
        cam.basis(r, u, f);
        float uu = (px - P.rcView.x) / P.rcView.w, vv = (py - P.rcView.y) / P.rcView.h;
        float t = std::tan(cam.fovY * 0.5f), a = P.rcView.w / P.rcView.h;
        o = cam.eye();
        d = normalize(f + r * ((2 * uu - 1) * a * t) + u * ((1 - 2 * vv) * t));
    };
    auto project = [&](const float3& p, ImVec2& s) {
        float3 r, u, f;
        cam.basis(r, u, f);
        float3 v = p - cam.eye();
        float z = dot(v, f);
        if (z < 1e-3f) return false;
        float t = std::tan(cam.fovY * 0.5f), a = P.rcView.w / P.rcView.h;
        s = ImVec2(P.rcView.x + (0.5f + 0.5f * dot(v, r) / (z * t * a)) * P.rcView.w, P.rcView.y + (0.5f - 0.5f * dot(v, u) / (z * t)) * P.rcView.h);
        return true;
    };
    const ImVec2 mouse = io.MousePos;
    const bool selValid = P.selJoint >= 0 && P.selJoint < (int)A.joints.size() && P.selJoint < (int)P.jointNow.size();
    const std::vector<PartWorld>& shown = P.rigMode == 0 ? P.jointRest : P.jointNow;

    // brush: hit on the deformed skinned mesh under the cursor
    P.brushHitValid = false;
    if (P.rigMode == 2 && hovered && P.skinPart >= 0 && P.skinPart < (int)P.partObject.size() && P.partObject[P.skinPart] >= 0) {
        const MeshData& m = P.scene.meshes[P.scene.objects[P.partObject[P.skinPart]].mesh];
        float3 o, d;
        ray(mouse.x, mouse.y, o, d);
        float best = 1e30f;
        for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
            const float3 &a = m.vertices[m.indices[t]].pos, &b = m.vertices[m.indices[t + 1]].pos, &c = m.vertices[m.indices[t + 2]].pos;
            float3 e1 = b - a, e2 = c - a, pv = cross(d, e2);
            float det = dot(e1, pv);
            if (std::fabs(det) < 1e-12f) continue;
            float inv = 1.0f / det;
            float3 tv = o - a;
            float uu = dot(tv, pv) * inv;
            if (uu < 0 || uu > 1) continue;
            float3 qv = cross(tv, e1);
            float vv = dot(d, qv) * inv;
            if (vv < 0 || uu + vv > 1) continue;
            float tt = dot(e2, qv) * inv;
            if (tt > 1e-4f && tt < best) { best = tt; P.brushNormal = normalize(cross(e1, e2)); }
        }
        if (best < 1e29f) { P.brushHitValid = true; P.brushHit = o + d * best; }
    }
    if (P.rigMode == 2) {
        if (io.KeyCtrl && io.MouseWheel != 0 && hovered) P.brushRadius = clampf(P.brushRadius * std::pow(1.1f, io.MouseWheel), 0.01f, 10.0f);
        if (P.painting) {
            if (!io.MouseDown[0]) P.painting = false;
            else if ((float)ImGui::GetTime() - P.brushLastStamp > 1.0f / 30.0f) {
                P.brushLastStamp = (float)ImGui::GetTime();
                rigPaintStamp(false);
            }
            return;
        }
        if (hovered && ImGui::IsMouseClicked(0) && !io.KeyAlt && P.brushHitValid) {
            P.painting = true;
            P.brushLastStamp = (float)ImGui::GetTime();
            rigPaintStamp(true);
            return;
        }
    }

    // gizmo drag (move: world axis; rotate: angle around a world axis, from the screen)
    if (P.jgAxis >= 0) {
        if (!io.MouseDown[0] || !selValid) { P.jgAxis = -1; return; }
        const PrefabJoint& j = A.joints[P.selJoint];
        int pj = j.parent ? A.findJoint(j.parent) : -1;
        float3 ppos(0), pscale(1);
        float4 prot = qIdentity();
        if (pj >= 0 && pj < (int)shown.size()) { ppos = shown[pj].position; prot = shown[pj].rot; pscale = shown[pj].scale; }
        if (P.jgAxis < 3) {
            float3 o, d;
            ray(mouse.x, mouse.y, o, d);
            float3 w = P.jgOrigin - o;
            float b = dot(P.jgDir, d), dd = dot(P.jgDir, w), e = dot(d, w), den = 1 - b * b;
            float t = std::fabs(den) < 1e-6f ? 0.0f : (b * e - dd) / den;
            float delta = t - P.jgT0;
            if (io.KeyCtrl) delta = std::round(delta * 10.0f) / 10.0f;
            float3 world = P.jgOrigin + P.jgDir * delta;
            float3 local = qRot(qInv(prot), world - ppos);
            local = float3(local.x / std::max(1e-5f, pscale.x), local.y / std::max(1e-5f, pscale.y), local.z / std::max(1e-5f, pscale.z));
            rigSetJointLocal(P.selJoint, CH_POSITION, local);
        } else {
            ImVec2 c;
            if (!project(P.jgOrigin, c)) return;
            float ang = std::atan2(mouse.y - c.y, mouse.x - c.x);
            float delta = ang - P.jgAngle0;
            float3 r, u, f;
            cam.basis(r, u, f);
            if (dot(P.jgDir, f) > 0) delta = -delta;   // screen y goes down: the sign follows the view
            delta = -delta;
            if (io.KeyCtrl) delta = radians(std::round(degrees(delta) / 15.0f) * 15.0f);
            float4 start = qMul(prot, eulerToQuat(P.jgStartRot));   // world rotation at the press
            float4 now = qNormalize(qMul(qAxisAngle(P.jgDir, delta), start));
            float4 local = qNormalize(qMul(qInv(prot), now));
            rigSetJointLocal(P.selJoint, CH_ROTATION, quatToEuler(local));
        }
        P.weightsVersion++;
        return;
    }
    if (!(hovered && ImGui::IsMouseClicked(0) && !io.KeyAlt)) return;
    // press on the gizmo
    if (P.jgHover >= 0 && selValid && P.rigMode != 2) {
        prefabPushUndo();
        P.jgAxis = P.jgHover;
        P.jgOrigin = shown[P.selJoint].position;
        int a = P.jgAxis % 3;
        P.jgDir = float3(a == 0 ? 1.0f : 0.0f, a == 1 ? 1.0f : 0.0f, a == 2 ? 1.0f : 0.0f);
        PartPose pp;
        if (P.rigMode == 0) { const PrefabJoint& j = A.joints[P.selJoint]; pp = {j.position, j.rotation, j.scale}; }
        else rigJointPose(P.selJoint, pp);
        P.jgStartPos = pp.position;
        P.jgStartRot = pp.rotation;
        if (P.jgAxis < 3) {
            float3 o, d;
            ray(mouse.x, mouse.y, o, d);
            float3 w = P.jgOrigin - o;
            float b = dot(P.jgDir, d), dd = dot(P.jgDir, w), e = dot(d, w), den = 1 - b * b;
            P.jgT0 = std::fabs(den) < 1e-6f ? 0.0f : (b * e - dd) / den;
        } else {
            ImVec2 c;
            project(P.jgOrigin, c);
            P.jgAngle0 = std::atan2(mouse.y - c.y, mouse.x - c.x);
        }
        return;
    }
    // pick a joint (screen distance), else a part (its skin becomes the shown skin)
    int best = -1;
    float bd = 12.0f * m_dpi;
    for (size_t j = 0; j < shown.size(); ++j) {
        ImVec2 s;
        if (!project(shown[j].position, s)) continue;
        float d = std::sqrt((s.x - mouse.x) * (s.x - mouse.x) + (s.y - mouse.y) * (s.y - mouse.y));
        if (d < bd) { bd = d; best = (int)j; }
    }
    if (best >= 0) { P.selJoint = best; return; }
    float3 o, d;
    ray(mouse.x, mouse.y, o, d);
    PickResult pr = pick(P.scene, o, d);
    for (size_t i = 0; i < P.partObject.size(); ++i)
        if (pr.object >= 0 && P.partObject[i] == pr.object) { P.sel = (int)i; if (A.skinOf(A.parts[i].id)) P.skinPart = (int)i; }
}

} // namespace app
