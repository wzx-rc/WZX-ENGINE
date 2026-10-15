// Prefab asset: hierarchy, animation sampling and the .prefab file (see Prefab.h).
#include "Prefab.h"
#include "Content.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <functional>

namespace app {

const char* channelName(int c) {
    static const char* n[] = {"Position", "Rotation", "Scale"};
    return c >= 0 && c < CH_COUNT ? n[c] : "?";
}
const char* interpName(int i) {
    static const char* n[] = {"Linear", "Step", "Smooth"};
    return i >= 0 && i < 3 ? n[i] : "?";
}

// --------------------------------------------------------------------------- tracks

float3 PrefabTrack::sample(float t) const {
    if (keys.empty()) return float3(0);
    if (t <= keys.front().time) return keys.front().value;
    if (t >= keys.back().time) return keys.back().value;
    size_t i = 1;
    while (i < keys.size() && keys[i].time < t) ++i;
    const PrefabKey& a = keys[i - 1];
    const PrefabKey& b = keys[i];
    float span = b.time - a.time;
    float u = span > 1e-6f ? (t - a.time) / span : 1.0f;
    if (a.interp == INTERP_STEP) return a.value;
    if (a.interp == INTERP_SMOOTH) u = u * u * (3 - 2 * u);
    return lerp(a.value, b.value, u);
}

int PrefabTrack::keyAt(float t, float eps) const {
    for (size_t i = 0; i < keys.size(); ++i)
        if (std::fabs(keys[i].time - t) <= eps) return (int)i;
    return -1;
}

void PrefabTrack::setKey(float t, const float3& v, int interp, float eps) {
    int k = keyAt(t, eps);
    if (k >= 0) { keys[k].value = v; return; }
    PrefabKey key;
    key.time = t;
    key.value = v;
    key.interp = interp;
    keys.push_back(key);
    sortKeys();
}

void PrefabTrack::sortKeys() {
    std::stable_sort(keys.begin(), keys.end(), [](const PrefabKey& a, const PrefabKey& b) { return a.time < b.time; });
}

PrefabTrack* PrefabClip::track(uint32_t part, int channel, bool create) {
    for (PrefabTrack& t : tracks)
        if (t.part == part && t.channel == channel) return &t;
    if (!create) return nullptr;
    PrefabTrack t;
    t.part = part;
    t.channel = channel;
    tracks.push_back(t);
    return &tracks.back();
}

const PrefabTrack* PrefabClip::track(uint32_t part, int channel) const {
    for (const PrefabTrack& t : tracks)
        if (t.part == part && t.channel == channel) return &t;
    return nullptr;
}

// --------------------------------------------------------------------------- hierarchy

int PrefabAsset::findPart(uint32_t id) const {
    for (size_t i = 0; i < parts.size(); ++i)
        if (parts[i].id == id) return (int)i;
    return -1;
}

uint32_t PrefabAsset::addPart(const PrefabPart& p) {
    for (const PrefabPart& q : parts) nextId = std::max(nextId, q.id + 1);
    PrefabPart n = p;
    n.id = nextId++;
    parts.push_back(n);
    return n.id;
}

void PrefabAsset::removePart(uint32_t id) {
    std::vector<uint32_t> gone{id};
    for (size_t k = 0; k < gone.size(); ++k)
        for (const PrefabPart& p : parts)
            if (p.parent == gone[k]) gone.push_back(p.id);
    auto dead = [&](uint32_t i) { return std::find(gone.begin(), gone.end(), i) != gone.end(); };
    parts.erase(std::remove_if(parts.begin(), parts.end(), [&](const PrefabPart& p) { return dead(p.id); }), parts.end());
    for (PrefabClip& c : clips)
        c.tracks.erase(std::remove_if(c.tracks.begin(), c.tracks.end(), [&](const PrefabTrack& t) { return dead(t.part); }), c.tracks.end());
}

bool PrefabAsset::isAncestor(uint32_t ancestor, uint32_t id) const {
    for (int guard = 0; guard < 256 && id; ++guard) {
        int i = findPart(id);
        if (i < 0) return false;
        id = parts[i].parent;
        if (id == ancestor) return true;
    }
    return false;
}

std::vector<int> PrefabAsset::childrenOf(uint32_t parent) const {
    std::vector<int> v;
    for (size_t i = 0; i < parts.size(); ++i)
        if (parts[i].parent == parent || (parent == 0 && parts[i].parent && findPart(parts[i].parent) < 0)) v.push_back((int)i);
    return v;
}

int PrefabAsset::depthOf(uint32_t id) const {
    int d = 0;
    for (int i = findPart(id); i >= 0 && parts[i].parent && d < 64; i = findPart(parts[i].parent)) ++d;
    return d;
}

// --------------------------------------------------------------------------- pose

float4 eulerToQuat(const float3& deg) {
    float4 qy = qAxisAngle(float3(0, 1, 0), radians(deg.y)), qx = qAxisAngle(float3(1, 0, 0), radians(deg.x));
    float4 qz = qAxisAngle(float3(0, 0, 1), radians(deg.z));
    return qNormalize(qMul(qMul(qy, qx), qz));
}

float3 quatToEuler(const float4& q) {
    float m02 = 2 * (q.x * q.z + q.w * q.y), m12 = 2 * (q.y * q.z - q.w * q.x), m22 = 1 - 2 * (q.x * q.x + q.y * q.y);
    float m10 = 2 * (q.x * q.y + q.w * q.z), m11 = 1 - 2 * (q.x * q.x + q.z * q.z);
    return float3(degrees(std::asin(clampf(-m12, -1, 1))), degrees(std::atan2(m02, m22)), degrees(std::atan2(m10, m11)));
}

void evaluatePose(const PrefabAsset& a, const PrefabClip* clip, float time, std::vector<PartPose>& out) {
    out.resize(a.parts.size());
    for (size_t i = 0; i < a.parts.size(); ++i) {
        const PrefabPart& p = a.parts[i];
        PartPose& o = out[i];
        o.position = p.position;
        o.rotation = p.rotation;
        o.scale = p.scale;
        if (!clip) continue;
        for (int c = 0; c < CH_COUNT; ++c) {
            const PrefabTrack* t = clip->track(p.id, c);
            if (!t || t->keys.empty()) continue;
            float3 v = t->sample(time);
            (c == CH_POSITION ? o.position : c == CH_ROTATION ? o.rotation : o.scale) = v;
        }
    }
}

void worldTransforms(const PrefabAsset& a, const std::vector<PartPose>& pose, std::vector<PartWorld>& out) {
    out.assign(a.parts.size(), PartWorld{});
    std::vector<int> state(a.parts.size(), 0);   // 0 todo, 1 in progress (cycle guard), 2 done
    std::function<void(int)> solve = [&](int i) {
        if (state[i] == 2) return;
        PartWorld& w = out[i];
        const PartPose& p = pose[i];
        float4 q = eulerToQuat(p.rotation);
        int pi = a.parts[i].parent ? a.findPart(a.parts[i].parent) : -1;
        if (pi >= 0 && state[pi] != 1) {
            state[i] = 1;
            solve(pi);
            const PartWorld& pw = out[pi];
            w.position = pw.position + qRot(pw.rot, p.position * pw.scale);
            w.rot = qNormalize(qMul(pw.rot, q));
            w.scale = pw.scale * p.scale;
        } else {
            w.position = p.position;
            w.rot = q;
            w.scale = p.scale;
        }
        state[i] = 2;
    };
    for (size_t i = 0; i < a.parts.size(); ++i) solve((int)i);
}

// --------------------------------------------------------------------------- file

namespace {

std::string fnum(float v) {
    if (!std::isfinite(v)) v = 0.0f;
    char b[32];
    auto r = std::to_chars(b, b + sizeof(b), v);
    return std::string(b, r.ptr);
}
std::string esc(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o.push_back('\\'); o.push_back((char)c); }
        else if (c == '\n') o += "\\n";
        else if (c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
        else o.push_back((char)c);
    }
    return o;
}

// pretty writer: one value per line, small vectors on one line (same layout as the .wzx scene)
class W {
public:
    std::string out;
    void beginObject(const char* k = nullptr) { item(k); out += '{'; m_first.push_back(true); }
    void endObject() { close('}'); }
    void beginArray(const char* k = nullptr) { item(k); out += '['; m_first.push_back(true); }
    void endArray() { close(']'); }
    void raw(const char* k, const std::string& v) { item(k); out += v; }
    void str(const char* k, const std::string& v) { raw(k, "\"" + esc(v) + "\""); }
    void num(const char* k, float v) { raw(k, fnum(v)); }
    void integer(const char* k, int v) { raw(k, std::to_string(v)); }
    void boolean(const char* k, bool v) { raw(k, v ? "true" : "false"); }
    void vec(const char* k, const float3& v) { raw(k, "[" + fnum(v.x) + ", " + fnum(v.y) + ", " + fnum(v.z) + "]"); }

private:
    std::vector<bool> m_first;
    void item(const char* k) {
        if (!m_first.empty()) {
            if (!m_first.back()) out += ',';
            m_first.back() = false;
            out += '\n';
            out.append(m_first.size() * 2, ' ');
        }
        if (k) { out += '"'; out += esc(k); out += "\": "; }
    }
    void close(char c) {
        bool empty = m_first.back();
        m_first.pop_back();
        if (!empty) { out += '\n'; out.append(m_first.size() * 2, ' '); }
        out += c;
    }
};

float3 vec3(const Json& j, const float3& def) {
    if (j.type != Json::Array || j.a.size() < 3) return def;
    return float3((float)j[0].num(def.x), (float)j[1].num(def.y), (float)j[2].num(def.z));
}
float fl(const Json& j, float def) { return (float)j.num(def); }
bool flag(const Json& j, bool def) { return j.type == Json::Bool ? j.b : def; }

int primType(const std::string& n) {
    for (int t = PRIM_CUBE; t <= PRIM_DISK; ++t)
        if (_stricmp(n.c_str(), primitiveName(t)) == 0) return t;
    return PRIM_NONE;
}

} // namespace

std::string prefabToJson(const PrefabAsset& a) {
    W w;
    w.beginObject();
    w.str("format", kPrefabFormat);
    w.integer("version", 1);
    w.str("name", a.name);

    w.beginObject("game");
    w.str("category", a.game.category);
    w.integer("footprintX", a.game.footprintX);
    w.integer("footprintZ", a.game.footprintZ);
    w.num("selectionRadius", a.game.selectionRadius);
    w.num("height", a.game.height);
    w.boolean("blocksPath", a.game.blocksPath);
    w.endObject();

    w.beginArray("parts");
    for (const PrefabPart& p : a.parts) {
        w.beginObject();
        w.integer("id", (int)p.id);
        w.integer("parent", (int)p.parent);
        w.str("name", p.name);
        if (!p.meshPath.empty()) {
            w.str("mesh", p.meshPath);
            w.boolean("recenter", p.recenter);
        } else if (p.prim.type != PRIM_NONE) {
            w.beginObject("shape");
            w.str("primitive", primitiveName(p.prim.type));
            w.vec("size", p.prim.size);
            w.num("radius", p.prim.radius);
            w.num("radius2", p.prim.radius2);
            w.integer("segments", p.prim.segments);
            w.integer("rings", p.prim.rings);
            w.integer("subdiv", p.prim.subdiv);
            w.endObject();
        }
        w.vec("position", p.position);
        w.vec("rotation", p.rotation);
        w.vec("scale", p.scale);
        w.boolean("visible", p.visible);
        w.boolean("collapsed", p.collapsed);
        if (!p.isPivot()) {
            const Material& m = p.material;
            w.beginObject("material");
            w.str("name", m.name);
            w.vec("baseColor", m.baseColor);
            if (p.texture.empty()) w.raw("texture", "null");
            else w.str("texture", p.texture);
            w.num("roughness", m.roughness);
            w.num("metallic", m.metallic);
            w.num("specular", m.specular);
            w.num("transmission", m.transmission);
            w.num("ior", m.ior);
            w.num("coat", m.coat);
            w.num("coatRoughness", m.coatRoughness);
            w.vec("emission", m.emission);
            w.num("emissionStrength", m.emissionStrength);
            w.integer("preset", m.preset);
            w.endObject();
        }
        w.endObject();
    }
    w.endArray();

    w.beginArray("clips");
    for (const PrefabClip& c : a.clips) {
        w.beginObject();
        w.str("name", c.name);
        w.num("length", c.length);
        w.num("fps", c.fps);
        w.boolean("loop", c.loop);
        w.beginArray("tracks");
        for (const PrefabTrack& t : c.tracks) {
            if (t.keys.empty()) continue;
            w.beginObject();
            w.integer("part", (int)t.part);
            w.str("channel", channelName(t.channel));
            w.beginArray("keys");
            for (const PrefabKey& k : t.keys)
                w.raw(nullptr, "{\"t\": " + fnum(k.time) + ", \"v\": [" + fnum(k.value.x) + ", " + fnum(k.value.y) + ", " + fnum(k.value.z) +
                                   "], \"interp\": \"" + interpName(k.interp) + "\"}");
            w.endArray();
            w.endObject();
        }
        w.endArray();
        w.endObject();
    }
    w.endArray();
    w.endObject();
    w.out += '\n';
    return w.out;
}

bool prefabFromJson(const Json& j, PrefabAsset& a, std::string* error) {
    if (j.type != Json::Object || j["format"].str() != kPrefabFormat) {
        if (error) *error = "not a prefab file";
        return false;
    }
    a = PrefabAsset();
    a.name = j["name"].str("Prefab");
    const Json& g = j["game"];
    a.game.category = g["category"].str(a.game.category);
    a.game.footprintX = (int)g["footprintX"].num(a.game.footprintX);
    a.game.footprintZ = (int)g["footprintZ"].num(a.game.footprintZ);
    a.game.selectionRadius = fl(g["selectionRadius"], a.game.selectionRadius);
    a.game.height = fl(g["height"], a.game.height);
    a.game.blocksPath = flag(g["blocksPath"], a.game.blocksPath);
    for (const Json& jp : j["parts"].a) {
        PrefabPart p;
        p.id = (uint32_t)jp["id"].num(0);
        p.parent = (uint32_t)jp["parent"].num(0);
        p.name = jp["name"].str("Part");
        p.meshPath = jp["mesh"].str();
        p.recenter = flag(jp["recenter"], true);
        const Json& sh = jp["shape"];
        if (sh.type == Json::Object) {
            p.prim.type = primType(sh["primitive"].str());
            p.prim.size = vec3(sh["size"], p.prim.size);
            p.prim.radius = fl(sh["radius"], p.prim.radius);
            p.prim.radius2 = fl(sh["radius2"], p.prim.radius2);
            p.prim.segments = (int)sh["segments"].num(p.prim.segments);
            p.prim.rings = (int)sh["rings"].num(p.prim.rings);
            p.prim.subdiv = (int)sh["subdiv"].num(p.prim.subdiv);
        }
        p.position = vec3(jp["position"], p.position);
        p.rotation = vec3(jp["rotation"], p.rotation);
        p.scale = vec3(jp["scale"], p.scale);
        p.visible = flag(jp["visible"], true);
        p.collapsed = flag(jp["collapsed"], false);
        const Json& jm = jp["material"];
        if (jm.type == Json::Object) {
            Material& m = p.material;
            m.name = jm["name"].str(m.name);
            m.baseColor = vec3(jm["baseColor"], m.baseColor);
            p.texture = jm["texture"].str();
            m.roughness = fl(jm["roughness"], m.roughness);
            m.metallic = fl(jm["metallic"], m.metallic);
            m.specular = fl(jm["specular"], m.specular);
            m.transmission = fl(jm["transmission"], m.transmission);
            m.ior = fl(jm["ior"], m.ior);
            m.coat = fl(jm["coat"], m.coat);
            m.coatRoughness = fl(jm["coatRoughness"], m.coatRoughness);
            m.emission = vec3(jm["emission"], m.emission);
            m.emissionStrength = fl(jm["emissionStrength"], m.emissionStrength);
            m.preset = (int)jm["preset"].num(m.preset);
        }
        if (!p.id || a.findPart(p.id) >= 0) p.id = 0;   // missing or duplicate: a new id below
        a.parts.push_back(p);
        a.nextId = std::max(a.nextId, p.id + 1);
    }
    for (PrefabPart& p : a.parts)
        if (!p.id) p.id = a.nextId++;
    for (const Json& jc : j["clips"].a) {
        PrefabClip c;
        c.name = jc["name"].str("Clip");
        c.length = std::max(0.05f, fl(jc["length"], c.length));
        c.fps = std::clamp(fl(jc["fps"], c.fps), 1.0f, 240.0f);
        c.loop = flag(jc["loop"], true);
        for (const Json& jt : jc["tracks"].a) {
            PrefabTrack t;
            t.part = (uint32_t)jt["part"].num(0);
            std::string ch = jt["channel"].str();
            t.channel = ch == "Rotation" ? CH_ROTATION : ch == "Scale" ? CH_SCALE : CH_POSITION;
            for (const Json& jk : jt["keys"].a) {
                PrefabKey k;
                k.time = fl(jk["t"], 0);
                k.value = vec3(jk["v"], float3(0));
                std::string in = jk["interp"].str("Smooth");
                k.interp = in == "Linear" ? INTERP_LINEAR : in == "Step" ? INTERP_STEP : INTERP_SMOOTH;
                t.keys.push_back(k);
            }
            t.sortKeys();
            if (a.findPart(t.part) >= 0 && !t.keys.empty()) c.tracks.push_back(t);
        }
        a.clips.push_back(c);
    }
    return true;
}

// --------------------------------------------------------------------------- demo

PrefabAsset makeDemoPrefab() {
    PrefabAsset a;
    a.name = "Outpost";
    a.game.category = "Building";
    a.game.footprintX = 2;
    a.game.footprintZ = 2;
    a.game.selectionRadius = 1.45f;
    a.game.height = 2.9f;
    auto mat = [](const char* name, float3 srgb, float rough, float metal = 0) {
        Material m;
        m.name = name;
        m.baseColor = float3(srgbToLinear(srgb.x), srgbToLinear(srgb.y), srgbToLinear(srgb.z));
        m.roughness = rough;
        m.metallic = metal;
        return m;
    };
    auto prim = [](int type) { PrimitiveParams p; p.type = type; return p; };

    PrefabPart house;
    house.name = "House";
    house.meshPath = "assets/HOUSE/House01_low.obj";
    house.texture = "assets/HOUSE/cozy+cottage+3d+model_basecolor.jpg";
    house.material = mat("House", float3(1, 1, 1), 0.75f);
    house.material.specular = 0.3f;
    house.scale = float3(0.034f);
    uint32_t houseId = a.addPart(house);

    PrefabPart pole;
    pole.name = "Flag Pole";
    pole.prim = prim(PRIM_CYLINDER);
    pole.prim.radius = 0.035f;
    pole.prim.size = float3(1, 2.8f, 1);
    pole.prim.segments = 16;
    pole.material = mat("Iron", float3(0.42f, 0.43f, 0.45f), 0.35f, 1.0f);
    pole.position = float3(1.12f, 0, -0.55f);
    uint32_t poleId = a.addPart(pole);

    PrefabPart pivot;
    pivot.name = "Flag Pivot";
    pivot.parent = poleId;
    pivot.position = float3(0, 2.62f, 0);
    uint32_t pivotId = a.addPart(pivot);

    PrefabPart flagPart;
    flagPart.name = "Flag";
    flagPart.parent = pivotId;
    flagPart.prim = prim(PRIM_CUBE);
    flagPart.prim.size = float3(0.72f, 0.42f, 0.025f);
    flagPart.material = mat("Banner Red", float3(0.78f, 0.16f, 0.12f), 0.8f);
    flagPart.position = float3(0.38f, -0.21f, 0);
    uint32_t flagId = a.addPart(flagPart);

    PrefabPart lantern;
    lantern.name = "Lantern";
    lantern.parent = poleId;
    lantern.prim = prim(PRIM_SPHERE);
    lantern.prim.radius = 0.08f;
    lantern.material = mat("Lantern Glow", float3(1.0f, 0.72f, 0.3f), 0.4f);
    lantern.material.emission = float3(1.0f, 0.62f, 0.22f);
    lantern.material.emissionStrength = 6.0f;
    lantern.position = float3(0, 2.8f, 0);
    uint32_t lanternId = a.addPart(lantern);

    PrefabPart crate;
    crate.name = "Crate";
    crate.prim = prim(PRIM_CUBE);
    crate.prim.size = float3(0.36f);
    crate.material = mat("Wood", float3(0.55f, 0.38f, 0.22f), 0.85f);
    crate.position = float3(-1.08f, 0, 0.62f);
    crate.rotation = float3(0, 18, 0);
    uint32_t crateId = a.addPart(crate);

    PrefabPart crate2 = crate;
    crate2.name = "Crate Small";
    crate2.parent = crateId;
    crate2.prim.size = float3(0.24f);
    crate2.position = float3(0.02f, 0.36f, 0);
    crate2.rotation = float3(0, -27, 0);
    uint32_t crate2Id = a.addPart(crate2);

    // Idle: the flag waves, the lantern breathes
    PrefabClip idle;
    idle.name = "Idle";
    idle.length = 2.0f;
    auto keys = [](PrefabClip& c, uint32_t part, int ch, std::initializer_list<std::pair<float, float3>> ks, int interp = INTERP_SMOOTH) {
        PrefabTrack* t = c.track(part, ch, true);
        for (auto& k : ks) t->setKey(k.first, k.second, interp, 1e-4f);
    };
    keys(idle, pivotId, CH_ROTATION, {{0.0f, float3(0, -14, 0)}, {1.0f, float3(0, 14, 0)}, {2.0f, float3(0, -14, 0)}});
    keys(idle, flagId, CH_SCALE, {{0.0f, float3(1)}, {0.5f, float3(0.9f, 1, 1)}, {1.0f, float3(1)}, {1.5f, float3(0.9f, 1, 1)}, {2.0f, float3(1)}});
    keys(idle, lanternId, CH_SCALE, {{0.0f, float3(1)}, {1.0f, float3(1.3f)}, {2.0f, float3(1)}});
    a.clips.push_back(idle);

    // Build: the parts rise into place (construction of the building)
    PrefabClip build;
    build.name = "Build";
    build.length = 1.6f;
    build.loop = false;
    keys(build, houseId, CH_SCALE, {{0.0f, float3(0.034f, 0.0f, 0.034f)}, {0.9f, float3(0.034f, 0.037f, 0.034f)}, {1.1f, float3(0.034f)}});
    keys(build, poleId, CH_POSITION, {{0.0f, float3(1.12f, -2.9f, -0.55f)}, {0.6f, float3(1.12f, -2.9f, -0.55f)}, {1.2f, float3(1.12f, 0, -0.55f)}});
    keys(build, crateId, CH_SCALE, {{0.0f, float3(0)}, {1.1f, float3(0)}, {1.4f, float3(1.15f)}, {1.6f, float3(1)}});
    keys(build, crate2Id, CH_SCALE, {{0.0f, float3(0)}, {1.25f, float3(0)}, {1.6f, float3(1)}});
    a.clips.push_back(build);
    return a;
}

} // namespace app
