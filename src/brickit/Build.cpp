#include "Build.h"
#include "app/Json.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_set>

namespace brickit {

using namespace vp3d;

float4 snapQuat(const float4& q) { return qNormalize(qSnap90(q)); }

// --------------------------------------------------------------------------- spatial hash of boxes

static int64_t cellKey(int x, int y, int z) {
    return ((int64_t)(x + (1 << 20)) << 42) | ((int64_t)(y + (1 << 20)) << 21) | (int64_t)(z + (1 << 20));
}

void BoxHash::insert(int id, const Box& b) {
    const int idx = (int)m_boxes.size();
    m_boxes.push_back(b);
    m_ids.push_back(id);
    const int x0 = (int)std::floor(b.mn.x / kCell), x1 = (int)std::floor(b.mx.x / kCell);
    const int y0 = (int)std::floor(b.mn.y / kCell), y1 = (int)std::floor(b.mx.y / kCell);
    const int z0 = (int)std::floor(b.mn.z / kCell), z1 = (int)std::floor(b.mx.z / kCell);
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z) m_cells[cellKey(x, y, z)].push_back(idx);
}

float BoxHash::worstOverlap(const Box& a) const {
    const float3 sa = a.mx - a.mn;
    const float va = std::max(sa.x * sa.y * sa.z, 1e-6f);
    float worst = 0;
    const int x0 = (int)std::floor(a.mn.x / kCell), x1 = (int)std::floor(a.mx.x / kCell);
    const int y0 = (int)std::floor(a.mn.y / kCell), y1 = (int)std::floor(a.mx.y / kCell);
    const int z0 = (int)std::floor(a.mn.z / kCell), z1 = (int)std::floor(a.mx.z / kCell);
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z) {
                auto it = m_cells.find(cellKey(x, y, z));
                if (it == m_cells.end()) continue;
                for (int i : it->second) {
                    const Box& b = m_boxes[i];
                    if (!a.overlaps(b)) continue;
                    float3 lo = maxv(a.mn, b.mn), hi = minv(a.mx, b.mx);
                    float3 s = hi - lo;
                    worst = std::max(worst, s.x * s.y * s.z / va);
                }
            }
    return worst;
}

// --------------------------------------------------------------------------- view

bool View::project(const float3& p, float& sx, float& sy) const {
    float2 ndc;
    float depth;
    if (!cam.project(p, w / h, ndc, &depth) || depth <= 1e-4f) return false;
    sx = ox + (ndc.x * 0.5f + 0.5f) * w;
    sy = oy + (0.5f - ndc.y * 0.5f) * h;
    return true;
}

void View::ray(float sx, float sy, float3& o, float3& d) const { cam.ray(2.0f * (sx - ox) / w - 1.0f, 1.0f - 2.0f * (sy - oy) / h, w / h, o, d, 1e4f); }

// --------------------------------------------------------------------------- geometry

float3 Builder::grabOf(const Brick& b) const {
    const Part& p = m_lib.parts[b.part];
    if (b.grab >= 0 && b.grab < (int)p.voxels.size()) return p.voxels[b.grab];
    // parts without studs (pins, axles, Technic): the voxel nearest to the centre of the voxels
    bool anyStud = false;
    for (uint8_t c : p.conn) anyStud |= (c == CONN_STUD || c == CONN_ANTISTUD);
    if (!p.voxels.empty() && !anyStud) {
        float3 c(0);
        for (const float3& v : p.voxels) c = c + v;
        c = c * (1.0f / p.voxels.size());
        float3 best = p.voxels[0];
        for (const float3& v : p.voxels)
            if (length(v - c) < length(best - c)) best = v;
        return best;
    }
    return float3(p.grab[0], 0, p.grab[1]);
}

Box Builder::bodyBox(int part, const float4& rot, const float3& pos) const {
    const Part& p = m_lib.parts[part];
    const float top = p.hasStuds ? std::min(p.bmax.y, p.topY) : p.bmax.y;
    const float s = 0.03f;
    Box b{float3(1e30f), float3(-1e30f)};
    for (int i = 0; i < 8; ++i) {
        float3 v((i & 1) ? p.bmax.x - s : p.bmin.x + s, (i & 2) ? top - s : p.bmin.y + s, (i & 4) ? p.bmax.z - s : p.bmin.z + s);
        v = qRot(rot, v) + pos;
        b.mn = minv(b.mn, v);
        b.mx = maxv(b.mx, v);
    }
    return b;
}

Box Builder::aabbOf(int part, const float4& rot, const float3& pos) const {
    const Part& p = m_lib.parts[part];
    Box b{float3(1e30f), float3(-1e30f)};
    for (int i = 0; i < 8; ++i) {
        float3 v((i & 1) ? p.bmax.x : p.bmin.x, (i & 2) ? p.bmax.y : p.bmin.y, (i & 4) ? p.bmax.z : p.bmin.z);
        v = qRot(rot, v) + pos;
        b.mn = minv(b.mn, v);
        b.mx = maxv(b.mx, v);
    }
    return b;
}

float3 Builder::worldVoxel(const Brick& b, const float3& v) const { return qRot(b.rot, v) + b.pos; }

// --------------------------------------------------------------------------- hold

void Builder::beginHold(Held& H, int brick, bool isNew) const {
    H = Held();
    H.brick = brick;
    H.isNew = isNew;
    H.original = bricks[brick];
    H.rot = snapQuat(bricks[brick].rot);
    H.grab = grabOf(bricks[brick]);
    H.target = bricks[brick].pos;
    H.hasTarget = !isNew;
}

// Free voxels of the other bricks (a voxel shared by two bricks, stud in anti-stud, is joined: no longer a target)
// and their body boxes in the hash.
void Builder::buildCache(Held& H) const {
    H.vox.clear();
    H.conn.clear();
    H.boxes.clear();
    struct Owner { float3 w; uint8_t conn; int count; };
    std::unordered_map<int64_t, Owner> owners;
    owners.reserve(bricks.size() * 8);
    for (int i = 0; i < (int)bricks.size(); ++i) {
        if (i == H.brick) continue;
        const Brick& b = bricks[i];
        if (b.part < 0 || !m_lib.parts[b.part].loaded) continue;
        H.boxes.insert(i, bodyBox(b.part, b.rot, b.pos));
        const Part& p = m_lib.parts[b.part];
        for (size_t v = 0; v < p.voxels.size(); ++v) {
            float3 w = worldVoxel(b, p.voxels[v]);
            int64_t k = cellKey((int)std::lround(w.x * 100), (int)std::lround(w.y * 100), (int)std::lround(w.z * 100));
            auto it = owners.find(k);
            if (it == owners.end()) owners.emplace(k, Owner{w, p.conn[v], 1});
            else ++it->second.count;
        }
    }
    // a free stud covered by another brick (a curved slope sits on it with its anti-stud higher up) or a free anti-stud
    // resting on one is not a target either: the point just above the stud (below the anti-stud) is inside a body
    for (auto& kv : owners) {
        if (kv.second.count >= 2) continue;
        const Owner& ow = kv.second;
        float dy = ow.conn == CONN_STUD ? 0.1f : ow.conn == CONN_ANTISTUD ? -0.1f : 0.0f;
        if (dy != 0.0f) {
            float3 q = ow.w + float3(0, dy, 0);
            Box probe{q - float3(0.02f), q + float3(0.02f)};
            if (H.boxes.any(probe)) continue;
        }
        H.vox.push_back(ow.w);
        H.conn.push_back(ow.conn);
    }
    H.cacheValid = true;
}

// Typed snap: stud on anti-stud, pin in a pin hole, axle in an axle hole or a pin hole, never an axle on a stud.
static bool snapCompatible(uint8_t a, uint8_t b) {
    switch (a) {
    case CONN_STUD: return b == CONN_ANTISTUD;
    case CONN_ANTISTUD: return b == CONN_STUD;
    case CONN_PIN: return b == CONN_PINHOLE;
    case CONN_PINHOLE: return b == CONN_PIN || b == CONN_AXLE;
    case CONN_AXLE: return b == CONN_AXLEHOLE || b == CONN_PINHOLE;
    case CONN_AXLEHOLE: return b == CONN_AXLE;
    }
    return false;
}

void Builder::computeTarget(Held& H, const View& view, float sx, float sy, const float3* hit) const {
    const bool overBrick = hit != nullptr;
    if (!H.cacheValid) buildCache(H);
    const Brick& hb = bricks[H.brick];
    const Part& hp = m_lib.parts[hb.part];
    // connector type of the pivot (voxel nearest to it)
    auto nearestOf = [&](int type, float3& v) {
        float bd = 1e9f;
        bool ok = false;
        for (size_t i = 0; i < hp.voxels.size(); ++i) {
            if (type >= 0 && hp.conn[i] != type) continue;
            const float d = length(hp.voxels[i] - H.grab);
            if (d < bd) { bd = d; v = hp.voxels[i]; ok = true; }
        }
        return ok;
    };
    float3 gv;
    int gT = -1;
    {
        int gi = -1;
        float gd = 1e9f;
        for (size_t i = 0; i < hp.voxels.size(); ++i) {
            float d = length(hp.voxels[i] - H.grab);
            if (d < gd) { gd = d; gi = (int)i; }
        }
        gT = gi >= 0 ? hp.conn[gi] : -1;
        (void)gv;
    }
    const bool studPivot = gT < 0 || gT == CONN_STUD || gT == CONN_ANTISTUD;
    // Correction (Wzx, 03/11/2026): a part with studs / anti-studs taken by a hole or by the middle of a hole (typed pin
    // by the converter: a Technic brick picked up by its side, its grab point saved there) only looked for pin holes
    // and axles, so it no longer stacked nor followed the floor grid. Studs and anti-studs stay targets whatever the point
    // taken: the pivot moves to the anti-stud (stud) of the part nearest to the grab point for a stud (anti-stud) target.
    // The typed targets (holes, axles, pins) stay; the candidate nearest to the cursor wins, whatever its kind.
    float3 pivStud, pivAnti;
    const bool hasStud = nearestOf(CONN_STUD, pivStud), hasAnti = nearestOf(CONN_ANTISTUD, pivAnti);
    auto pivotFor = [&](uint8_t target, float3& piv, int& pt) -> bool {
        if (gT < 0 || snapCompatible((uint8_t)gT, target)) { piv = H.grab; pt = gT; return true; }
        // (also a part taken by a stud: onto studs through its nearest anti-stud, under anti-studs through its nearest stud)
        if (target == CONN_STUD && hasAnti) { piv = pivAnti; pt = CONN_ANTISTUD; return true; }
        if (target == CONN_ANTISTUD && hasStud) { piv = pivStud; pt = CONN_STUD; return true; }
        return false;
    };
    // voxels attract strongly over a brick, only very close otherwise (free move on the floor)
    const float radius = overBrick ? 60.0f : 22.0f;
    float best = radius * radius;
    bool found = false;
    float3 chosen, pivot = H.grab;
    int pivotType = gT;
    float3 eye = view.cam.eye();
    if (hit) {   // over a brick: the free compatible voxel nearest to the point under the cursor (within 2 studs)
        float bw = 1.6f * 1.6f;
        for (size_t ci = 0; ci < H.vox.size(); ++ci) {
            float3 pv;
            int pt;
            if (!pivotFor(H.conn[ci], pv, pt)) continue;
            float d = dot(H.vox[ci] - *hit, H.vox[ci] - *hit);
            if (d < bw) { bw = d; chosen = H.vox[ci]; pivot = pv; pivotType = pt; found = true; }
        }
    }
    for (size_t ci = 0; ci < H.vox.size() && !found; ++ci) {
        float3 pv;
        int pt;
        if (!pivotFor(H.conn[ci], pv, pt)) continue;
        float px, py;
        if (!view.project(H.vox[ci], px, py)) continue;
        float d = (px - sx) * (px - sx) + (py - sy) * (py - sy) + dot(H.vox[ci] - eye, H.vox[ci] - eye) * 1e-4f;
        if (d < best) { best = d; chosen = H.vox[ci]; pivot = pv; pivotType = pt; found = true; }
    }
    if (!found) {   // the floor: stud lattice (grid on, stud pivots; a part taken by a hole: its nearest anti-stud), else under the cursor
        float3 o, d;
        view.ray(sx, sy, o, d);
        float3 p;
        if (d.y < -1e-4f) p = o + d * (-o.y / d.y);
        else {   // above the horizon: a vertical plane through the view target
            float3 n = normalize(float3(d.x, 0, d.z));
            float t = dot(view.cam.target - o, n) / std::max(dot(d, n), 1e-4f);
            p = o + d * t;
            p.y = 0;
        }
        const bool onGrid = gridSnap && (studPivot || hasAnti || hasStud);
        if (!studPivot && hasAnti) { pivot = pivAnti; pivotType = CONN_ANTISTUD; }
        else if (!studPivot && hasStud) { pivot = pivStud; pivotType = CONN_STUD; }   // (studs only: aligned by a stud)
        chosen = onGrid ? float3(std::floor(p.x / kPitch) * kPitch + 0.4f, 0, std::floor(p.z / kPitch) * kPitch + 0.4f)
                        : float3(p.x, 0, p.z);
    }
    const float3 gW = qRot(H.rot, pivot);
    const Box rel = aabbOf(hb.part, H.rot, float3(0));
    auto floorClamp = [&](float3 pos) {
        if (pos.y + rel.mn.y < 0) pos.y = -rel.mn.y;   // never below the floor
        return pos;
    };
    float3 pos = floorClamp(chosen - gW);
    H.blocked = H.suggested = false;
    auto overlap = [&](const float3& at) { return H.boxes.worstOverlap(bodyBox(hb.part, H.rot, at)); };
    if (safety && overlap(pos) > 0.5f) {
        // refused: the aimed voxel stays, another pivot of the same type is tried (about 1, 2 then 3 studs away)
        struct Cand { float3 v; float d; };
        std::vector<Cand> cands;
        for (size_t i = 0; i < hp.voxels.size(); ++i) {
            float d = length(hp.voxels[i] - pivot);
            if (d > 1e-3f && d <= 3.6f && (pivotType < 0 || hp.conn[i] == pivotType)) cands.push_back({hp.voxels[i], d});
        }
        std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.d < b.d; });
        bool ok = false;
        for (const Cand& c : cands) {
            float3 p2 = floorClamp(chosen - qRot(H.rot, c.v));
            if (overlap(p2) <= 0.5f) { pos = p2; ok = true; break; }
        }
        H.suggested = ok;
        H.blocked = !ok;
    }
    H.snapVoxel = chosen;
    H.target = pos;
    H.hasTarget = true;
}

void Builder::rotateHeld(Held& H, const float3& axis, int dir) const {
    H.rot = snapQuat(qMul(qAxisAngle(axis, dir * kPi * 0.5f), H.rot));
}

// --------------------------------------------------------------------------- edits

int Builder::stackedCopy(int src) {
    const Brick s = bricks[src];
    const Box body = bodyBox(s.part, s.rot, s.pos);
    const float h = std::max(kPlate, std::round((body.mx.y - body.mn.y + 0.06f) / kPlate) * kPlate);
    BoxHash others;
    for (int i = 0; i < (int)bricks.size(); ++i) others.insert(i, bodyBox(bricks[i].part, bricks[i].rot, bricks[i].pos));
    float3 pos = s.pos;
    for (int k = 0; k < 60; ++k) {
        pos.y += h;
        if (!others.any(bodyBox(s.part, s.rot, pos))) {
            Brick c = s;
            c.pos = pos;
            bricks.push_back(c);
            return (int)bricks.size() - 1;
        }
    }
    return -1;
}

void Builder::rotatePlacedAxis(int i, const float3& axis, int dir) {
    Brick& b = bricks[i];
    float3 pivot = worldVoxel(b, grabOf(b));
    float4 r = qAxisAngle(axis, dir * kPi * 0.5f);
    b.rot = snapQuat(qMul(r, b.rot));
    b.pos = pivot + qRot(r, b.pos - pivot);
}

void Builder::rotatePlaced(int i, int dir) {
    Brick& b = bricks[i];
    float3 pivot = worldVoxel(b, grabOf(b));
    float4 r = qAxisAngle(float3(0, 1, 0), dir * kPi * 0.5f);
    b.rot = snapQuat(qMul(r, b.rot));
    b.pos = pivot + qRot(r, b.pos - pivot);
}

// --------------------------------------------------------------------------- scene file

static std::string jsonEscape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') o.push_back('\\');
        o.push_back(c);
    }
    return o;
}

std::string Builder::toJson(const Camera& c) const {
    std::string out;
    char buf[512];
    const float deg = 180.0f / kPi;
    snprintf(buf, sizeof(buf),
             "{\n \"format\": \"vp3d-scene\",\n \"version\": 1,\n \"units\": \"cm\",\n \"camera\": {\"target\": [%.4f, %.4f, %.4f], \"yaw\": %.3f, \"pitch\": %.3f, \"distance\": %.3f, \"fov\": %.3f},\n \"bricks\": [",
             c.target.x, c.target.y, c.target.z, c.yaw * deg, c.pitch * deg, c.distance, c.fovY * deg);
    out += buf;
    for (size_t i = 0; i < bricks.size(); ++i) {
        const Brick& b = bricks[i];
        const Part& p = m_lib.parts[b.part];
        const Color& col = m_lib.palette[std::clamp(b.color, 0, (int)m_lib.palette.size() - 1)];
        const int mat = col.group == "transparent" ? 1 : col.group == "metallic" ? 9 : 0;
        float3 f = qRot(b.rot, float3(1, 0, 0));
        float yaw = std::atan2(-f.z, f.x) * deg;
        float3 g = grabOf(b);
        snprintf(buf, sizeof(buf),
                 "%s\n  {\"lib\": \"brickit\", \"part\": \"%s\", \"color\": %d, \"rgb\": [%.4f, %.4f, %.4f], \"material\": %d, \"pos\": [%.4f, %.4f, %.4f], \"yaw\": %.3f, \"rot\": [%.6f, %.6f, %.6f, %.6f], \"grabVoxel\": [%.3f, %.3f, %.3f]}",
                 i ? "," : "", jsonEscape(p.name).c_str(), col.id, col.srgb.x, col.srgb.y, col.srgb.z, mat, b.pos.x, b.pos.y, b.pos.z, yaw,
                 b.rot.x, b.rot.y, b.rot.z, b.rot.w, g.x, g.y, g.z);
        out += buf;
    }
    out += "\n ]\n}\n";
    return out;
}

bool Builder::fromJson(const std::string& text, Camera* cam, std::string* message) {
    app::Json j;
    if (!app::Json::parse(text, j)) { if (message) *message = "JSON invalide"; return false; }
    if (j["format"].str() != "vp3d-scene") { if (message) *message = "Ce fichier n'est pas une scène vp3d-scene"; return false; }
    std::vector<Brick> out;
    int skipped = 0;
    for (const app::Json& s : j["bricks"].a) {
        int pi = s["lib"].str() == "brickit" ? m_lib.find(s["part"].str()) : -1;
        if (pi < 0 || !m_lib.get(m_lib.parts[pi].name)) { ++skipped; continue; }
        Brick b;
        b.part = pi;
        int cid = (int)s["color"].num(15);
        b.color = 0;
        for (int k = 0; k < (int)m_lib.palette.size(); ++k)
            if (m_lib.palette[k].id == cid) b.color = k;
        b.pos = float3((float)s["pos"][0].num(), (float)s["pos"][1].num(), (float)s["pos"][2].num());
        if (s["rot"].type == app::Json::Array && s["rot"].a.size() == 4)
            b.rot = float4((float)s["rot"][0].num(), (float)s["rot"][1].num(), (float)s["rot"][2].num(), (float)s["rot"][3].num(1));
        else b.rot = qAxisAngle(float3(0, 1, 0), (float)s["yaw"].num() * kPi / 180.0f);
        b.rot = snapQuat(b.rot);
        if (s["grabVoxel"].type == app::Json::Array) {
            float3 g((float)s["grabVoxel"][0].num(), (float)s["grabVoxel"][1].num(), (float)s["grabVoxel"][2].num());
            const Part& p = m_lib.parts[pi];
            for (size_t v = 0; v < p.voxels.size(); ++v)
                if (length(p.voxels[v] - g) < 0.02f) b.grab = (int)v;
        }
        out.push_back(b);
    }
    bricks.swap(out);
    if (cam && j["camera"].type == app::Json::Object) {
        const app::Json& c = j["camera"];
        cam->target = float3((float)c["target"][0].num(), (float)c["target"][1].num(), (float)c["target"][2].num());
        cam->yaw = (float)c["yaw"].num() * kPi / 180.0f;
        cam->pitch = (float)c["pitch"].num() * kPi / 180.0f;
        cam->distance = (float)c["distance"].num(cam->distance);
    }
    if (message) {
        char buf[160];
        snprintf(buf, sizeof(buf), "Scène chargée : %d pièces%s", (int)bricks.size(), skipped ? " (des pièces hors bibliothèque ignorées)" : "");
        *message = buf;
    }
    return true;
}

} // namespace brickit
