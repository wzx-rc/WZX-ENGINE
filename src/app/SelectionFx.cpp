// Selection overlay of the editor, over the soft blue edge glow of the tonemapper (Tonemap.hlsl):
//   - a small soft glint on each vertex of the outline,
//   - the outline seen in the reflection of a glossy floor (the selection mirrored under y = 0).
// The outline is the set of silhouette edges of the selected meshes, found on the CPU: an edge between a face turned
// to the camera and a face turned away (or a border edge). Edges are welded by position and cached per mesh.
#include "App.h"
#include "GameHost.h"
#include <array>
#include <map>
#include <set>

namespace app {

namespace {

using PosKey = std::array<int64_t, 3>;
PosKey posKey(const float3& p) { return {std::llround(p.x * 1e4), std::llround(p.y * 1e4), std::llround(p.z * 1e4)}; }

} // namespace

const std::vector<App::SilEdge>& App::silhouetteEdges(int mesh) {
    SilCache& c = m_silCache[mesh];
    const MeshData& m = m_scene.meshes[mesh];
    const float3 first = m.vertices.empty() ? float3(0, 0, 0) : m.vertices.front().pos;
    if (c.built && c.version == m_scene.geometryVersion && c.vertexCount == m.vertices.size() && c.indexCount == m.indices.size() &&
        c.first.x == first.x && c.first.y == first.y && c.first.z == first.z)
        return c.edges;
    c = SilCache();
    c.built = true;
    c.version = m_scene.geometryVersion;
    c.vertexCount = m.vertices.size();
    c.indexCount = m.indices.size();
    c.first = first;
    if (m.indices.size() / 3 > 60000) return c.edges;   // too heavy for a per-frame CPU outline
    std::map<std::array<int64_t, 6>, int> byKey;
    for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        const Vertex* v[3] = {&m.vertices[m.indices[t]], &m.vertices[m.indices[t + 1]], &m.vertices[m.indices[t + 2]]};
        float3 n = cross(v[1]->pos - v[0]->pos, v[2]->pos - v[0]->pos);
        if (length(n) < 1e-12f) continue;
        n = normalize(n);
        if (dot(n, v[0]->nrm + v[1]->nrm + v[2]->nrm) < 0) n = -n;   // the winding of some meshes is clockwise
        for (int k = 0; k < 3; ++k) {
            const float3 &a = v[k]->pos, &b = v[(k + 1) % 3]->pos;
            PosKey ka = posKey(a), kb = posKey(b);
            if (kb < ka) std::swap(ka, kb);
            std::array<int64_t, 6> key = {ka[0], ka[1], ka[2], kb[0], kb[1], kb[2]};
            auto it = byKey.find(key);
            if (it == byKey.end()) {
                byKey[key] = (int)c.edges.size();
                SilEdge e;
                e.a = a;
                e.b = b;
                e.n0 = n;
                c.edges.push_back(e);
            } else if (c.edges[it->second].open) {
                c.edges[it->second].n1 = n;
                c.edges[it->second].open = false;
            }
        }
    }
    // the diagonal of a flat quad (same normal on both sides) can never be on the outline
    std::vector<SilEdge> kept;
    for (const SilEdge& e : c.edges)
        if (e.open || dot(e.n0, e.n1) < 0.9999f) kept.push_back(e);
    c.edges.swap(kept);
    return c.edges;
}

void App::drawSelectionFx(ImDrawList* dl) {
    m_fxStats = FxStats();
    if (m_rs.selectionStyle != 1 || m_selected < 0) return;
    if (m_game && (m_game->playing() || m_game->playerMode())) return;
    const Camera& cam = m_scene.camera;
    const float3 eye = cam.eye();
    float3 cr, cu, cf;
    cam.basis(cr, cu, cf);
    const float d = m_dpi;
    // reflection strength: a glossy ground lying at y = 0
    float refl = 0;
    for (const Object& o : m_scene.objects)
        if (o.part == "GROUND" && o.visible && o.material >= 0 && o.material < (int)m_scene.materials.size())
            refl = std::max(refl, clampf(1.0f - m_scene.materials[o.material].roughness / 0.45f, 0.0f, 1.0f));

    // occluders: the boxes of the visible objects (an overlay point behind one of them is not drawn)
    struct Box { int obj; float3 mn, mx; };
    std::vector<Box> boxes;
    for (size_t k = 0; k < m_scene.objects.size(); ++k) {
        const Object& o = m_scene.objects[k];
        if (!o.visible || o.ghost || o.group || o.mesh < 0 || o.part == "GROUND") continue;
        Box b;
        b.obj = (int)k;
        aabbOf(o, o.position, objRot(o), b.mn, b.mx);
        boxes.push_back(b);
    }
    // (from p towards the viewer; orthographic: along the view, the whole scene)
    auto hidden = [&](const float3& p, int skip) {
        const float3 dir = cam.toViewer(p);
        const float tp = cam.ortho ? 1e6f : length(eye - p);
        if (tp < 1e-5f) return false;
        const float eps = cam.ortho ? 0.01f : tp * 0.005f;   // (p lies on the surface of its own object)
        for (const Box& b : boxes) {
            if (b.obj == skip) continue;
            float t0 = eps, t1 = tp;
            bool miss = false;
            for (int a = 0; a < 3 && !miss; ++a) {
                const float o = p[a], dd = dir[a];
                if (std::fabs(dd) < 1e-8f) { miss = o < b.mn[a] || o > b.mx[a]; continue; }
                float ta = (b.mn[a] - o) / dd, tb = (b.mx[a] - o) / dd;
                if (ta > tb) std::swap(ta, tb);
                t0 = std::max(t0, ta);
                t1 = std::min(t1, tb);
                miss = t0 > t1;
            }
            if (!miss) return true;
        }
        return false;
    };
    // a point of the reflection is seen where its camera ray meets the floor
    auto floorPoint = [&](const float3& m) {
        if (cam.ortho) return m - cf * (m.y / (std::fabs(cf.y) > 1e-4f ? cf.y : -1e-4f));
        return eye + (m - eye) * (eye.y / std::max(eye.y - m.y, 1e-6f));
    };

    std::vector<float2> dots;
    std::vector<std::pair<float2, float2>> creases;
    struct MirrorDot { float2 p; float a; };
    std::vector<MirrorDot> mdots;
    std::vector<std::pair<MirrorDot, MirrorDot>> msegs;
    for (int i : selectionAll()) {
        if (i < 0 || i >= (int)m_scene.objects.size()) continue;
        const Object& o = m_scene.objects[i];
        if (o.mesh < 0 || o.mesh >= (int)m_scene.meshes.size() || !o.visible) continue;
        const std::vector<SilEdge>& edges = silhouetteEdges(o.mesh);
        const Affine T = o.transform();
        const float4 q = objRot(o);
        const float3 sc = o.scale;
        auto toWorldN = [&](const float3& n) { return normalize(qRot(q, float3(n.x / sc.x, n.y / sc.y, n.z / sc.z))); };
        float3 mn, mx;
        aabbOf(o, o.position, q, mn, mx);
        const float fadeH = std::max(mx.y - mn.y, 1e-3f) * 0.7f;   // the reflection fades with its depth under the floor
        for (const SilEdge& e : edges) {
            const float3 a = T.point(e.a), b = T.point(e.b);
            const float3 n0 = toWorldN(e.n0), n1 = e.open ? n0 : toWorldN(e.n1);
            const float3 va = cam.toViewer(a);
            if (e.open || ((dot(n0, va) > 0) != (dot(n1, va) > 0))) {
                float2 pa, pb;
                if (!hidden(a, i) && project(a, pa)) dots.push_back(pa);
                if (!hidden(b, i) && project(b, pb)) dots.push_back(pb);
            } else if (dot(n0, n1) < 0.85f && dot(n0, va) > 0 && dot(n1, va) > 0) {
                // a crease turned to the camera (both faces seen, sharp angle): a thin glowing line, antialiased
                float2 pa, pb;
                if (!hidden((a + b) * 0.5f, i) && project(a, pa) && project(b, pb)) creases.push_back({pa, pb});
            }
            if (refl <= 0) continue;
            // mirrored under the floor: its silhouette as seen by the camera is the outline in the reflection
            const float3 ma(a.x, -a.y, a.z), mb(b.x, -b.y, b.z), m0(n0.x, -n0.y, n0.z), m1(n1.x, -n1.y, n1.z);
            if (ma.y > 0 && mb.y > 0) continue;   // part of the object under the floor
            const float3 vm = cam.toViewer(ma);
            if (!(e.open || ((dot(m0, vm) > 0) != (dot(m1, vm) > 0)))) continue;
            if (cam.ortho ? cf.y > -1e-3f : eye.y <= 0) continue;   // (the floor seen from under it, or edge-on)
            // in pieces: the parts hidden by an object (the selection itself included) are left out
            const int n = 6;
            for (int s = 0; s < n; ++s) {
                const float3 p0 = lerp(ma, mb, (float)s / n), p1 = lerp(ma, mb, (float)(s + 1) / n);
                if (hidden(floorPoint((p0 + p1) * 0.5f), -1)) continue;
                MirrorDot da, db;
                if (!project(p0, da.p) || !project(p1, db.p)) continue;
                da.a = std::exp(-std::fabs(p0.y) / fadeH);
                db.a = std::exp(-std::fabs(p1.y) / fadeH);
                msegs.push_back({da, db});
            }
            for (const float3& v : {ma, mb}) {
                MirrorDot dv;
                if (hidden(floorPoint(v), -1) || !project(v, dv.p)) continue;
                dv.a = std::exp(-std::fabs(v.y) / fadeH);
                mdots.push_back(dv);
            }
        }
    }
    // one glow per vertex on screen (welded vertices, shared edges)
    auto unique = [](std::vector<float2>& pts) {
        std::set<std::pair<int, int>> seen;
        std::vector<float2> out;
        for (const float2& p : pts)
            if (seen.insert({(int)std::lround(p.x * 0.5f), (int)std::lround(p.y * 0.5f)}).second) out.push_back(p);
        pts.swap(out);
    };
    // 3: the reflection, faint, under everything
    for (const auto& s : msegs) {
        int al = (int)(70 * refl * 0.5f * (s.first.a + s.second.a));
        if (al > 2) {
            dl->AddLine(ImVec2(s.first.p.x, s.first.p.y), ImVec2(s.second.p.x, s.second.p.y), IM_COL32(70, 150, 255, al), 1.5f * d);
            ++m_fxStats.mirrorLines;
        }
    }
    {
        std::set<std::pair<int, int>> seen;
        for (const MirrorDot& m : mdots) {
            if (!seen.insert({(int)std::lround(m.p.x * 0.5f), (int)std::lround(m.p.y * 0.5f)}).second) continue;
            float k = refl * m.a;
            if (k < 0.02f) continue;
            dl->AddCircleFilled(ImVec2(m.p.x, m.p.y), 2.0f * d, IM_COL32(200, 225, 255, (int)(24 * k)), 10);
            dl->AddCircleFilled(ImVec2(m.p.x, m.p.y), 0.8f * d, IM_COL32(225, 238, 255, (int)(110 * k)), 8);
            ++m_fxStats.mirrorGlints;
        }
    }
    // creases of the selection, with the light running along the contour (as the outline of Tonemap.hlsl)
    {
        const float kx = m_rcView.w / std::max(1, m_vpPx.w), ky = m_rcView.h / std::max(1, m_vpPx.h);
        const ImVec2 sc(m_rcView.x + m_rs.selectionCenter.x * kx, m_rcView.y + m_rs.selectionCenter.y * ky);
        for (const auto& s : creases) {
            const ImVec2 a(s.first.x, s.first.y), b(s.second.x, s.second.y);
            const float ang = std::atan2((a.y + b.y) * 0.5f - sc.y, (a.x + b.x) * 0.5f - sc.x);
            float fl = ang * (4.0f / 6.2831853f) - m_rs.selectionAge * 0.6f;
            fl -= std::floor(fl);
            const float k = 0.75f + 0.25f * fl;
            dl->AddLine(a, b, IM_COL32(77, 158, 255, (int)(38 * k)), 3.0f * d);
            dl->AddLine(a, b, IM_COL32(150, 205, 255, (int)(150 * k)), 1.0f * d);
        }
        m_fxStats.creases = (int)creases.size();
    }
    // small soft glints on the vertices of the outline (about 2.5 px)
    unique(dots);
    m_fxStats.glints = (int)dots.size();
    for (const float2& p : dots) {
        ImVec2 c(p.x, p.y);
        dl->AddCircleFilled(c, 2.6f * d, IM_COL32(190, 220, 255, 26), 12);
        dl->AddCircleFilled(c, 1.7f * d, IM_COL32(210, 230, 255, 60), 10);
        dl->AddCircleFilled(c, 0.9f * d, IM_COL32(235, 244, 255, 190), 8);
    }
}

} // namespace app
