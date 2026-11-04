#include "Library.h"
#include "app/Json.h"
#include "vp3d/EnvImage.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace brickit {

using app::Json;

float srgbToLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }

static bool readFile(const std::wstring& path, std::vector<uint8_t>& out) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? (size_t)n : 0);
    size_t got = n > 0 ? fread(out.data(), 1, out.size(), f) : 0;
    fclose(f);
    return got == out.size();
}

static bool fail(std::string* e, const std::string& m) {
    if (e) *e = m;
    return false;
}

static Conn connOf(const std::string& t) {
    if (t == "stud") return CONN_STUD;
    if (t == "antistud") return CONN_ANTISTUD;
    if (t == "pin") return CONN_PIN;
    if (t == "pinhole") return CONN_PINHOLE;
    if (t == "axle") return CONN_AXLE;
    if (t == "axlehole") return CONN_AXLEHOLE;
    return CONN_STUD;
}

// Part metas shared by base.json, index.json and the pack metas (fields that are present overwrite).
static void readMeta(const Json& j, Part& p) {
    if (j["label"].type == Json::String) p.label = j["label"].s;
    const Json& info = j["info"];
    if (info["category"].type == Json::String) p.category = info["category"].s;
    if (j["sub"].type == Json::String) p.sub = j["sub"].s;
    else if (info["sub"].type == Json::String) p.sub = info["sub"].s;
    if (j["heightPlates"].type == Json::Number) p.heightPlates = (int)j["heightPlates"].n;
    if (j["topY"].type == Json::Number) p.topY = (float)j["topY"].n;
    if (j["hasStuds"].type == Json::Bool) p.hasStuds = j["hasStuds"].b;
    if (info["family"].type == Json::String) p.family = info["family"].s;
    if (info["ldraw"].type == Json::String) p.ldraw = info["ldraw"].s;
    if (info["ldrawOrigin"].type == Json::Array) {
        p.ldrawOrigin = float3((float)info["ldrawOrigin"][0].num(), (float)info["ldrawOrigin"][1].num(), (float)info["ldrawOrigin"][2].num());
        p.hasOrigin = true;
    }
    if (info["title"].type == Json::String) p.title = info["title"].s;
    if (info["author"].type == Json::String) p.author = info["author"].s;
    if (info["license"].type == Json::String) p.license = info["license"].s;
    if (info["release"].type == Json::String) p.release = info["release"].s;
    if (info["keywords"].type == Json::String) p.keywords = info["keywords"].s;
    if (info["history"].type == Json::Number) p.history = (int)info["history"].n;
    if (info["triangles"].type == Json::Number) p.triangles = (int)info["triangles"].n;
    if (info["vertices"].type == Json::Number) p.vertices = (int)info["vertices"].n;
    if (info["sizeCm"].type == Json::Array) { for (int k = 0; k < 3; ++k) p.sizeCm[k] = (float)info["sizeCm"][k].num(); p.hasSizeCm = true; }
    if (info["sizeStuds"].type == Json::Array) { for (int k = 0; k < 2; ++k) p.sizeStuds[k] = (float)info["sizeStuds"][k].num(); p.hasSizeStuds = true; }
    if (info["connectors"].type == Json::Object) {
        p.connectors.clear();
        for (auto& [k, v] : info["connectors"].o) p.connectors.push_back({k, (int)v.num()});
    }
    if (j["grab"].type == Json::Array) { p.grab[0] = (float)j["grab"][0].num(); p.grab[1] = (float)j["grab"][1].num(); }
    if (j["voxels"].type == Json::Array) {
        p.voxels.clear();
        for (const Json& v : j["voxels"].a) p.voxels.push_back(float3((float)v[0].num(), (float)v[1].num(), (float)v[2].num()));
    }
    if (j["connectorTypes"].type == Json::Array) {
        p.conn.clear();
        for (const Json& t : j["connectorTypes"].a) p.conn.push_back(connOf(t.str()));
    }
    if (p.conn.size() != p.voxels.size()) p.conn.assign(p.voxels.size(), CONN_STUD);
    if (j["columns"].type == Json::Array) {
        p.columns.clear();
        for (const Json& c : j["columns"].a) p.columns.push_back({(float)c[0].num(), (float)c[1].num()});
    }
}

static void finishMesh(Part& p) {
    p.mesh.bounds(p.bmin, p.bmax);
    p.loaded = !p.mesh.indices.empty();
}

// The real colours of each part (tools/brickit/part_colors.py): "name code code ..." per line
static void loadPartColors(const std::wstring& path, std::unordered_map<std::string, std::vector<int>>& out) {
    std::vector<uint8_t> t;
    out.clear();
    if (!readFile(path, t)) return;
    size_t i = 0;
    while (i < t.size()) {
        size_t e = i;
        while (e < t.size() && t[e] != '\n') ++e;
        if (e > i && t[i] != '#') {
            size_t sp = i;
            while (sp < e && t[sp] != ' ') ++sp;
            std::string name((const char*)&t[i], sp - i);
            std::vector<int>& v = out[name];
            int x = 0;
            bool any = false;
            for (size_t k = sp; k <= e; ++k) {
                if (k < e && t[k] >= '0' && t[k] <= '9') { x = x * 10 + (t[k] - '0'); any = true; }
                else { if (any) v.push_back(x); x = 0; any = false; }
            }
        }
        i = e + 1;
    }
}

const std::vector<int>* Library::colorsOf(int part) const {
    if (part < 0 || part >= (int)parts.size() || m_partColors.empty()) return nullptr;
    std::string n = parts[part].name;
    for (auto& c : n) c = (char)tolower((unsigned char)c);
    auto it = m_partColors.find(n);
    if (it == m_partColors.end()) {   // a mould letter the table does not know (3062 for 3062b...)
        std::string base = n;
        while (!base.empty() && base.back() >= 'a' && base.back() <= 'z') base.pop_back();
        if (base != n) it = m_partColors.find(base);
        if (it == m_partColors.end()) for (char m : std::string("abcd")) { it = m_partColors.find(base + m); if (it != m_partColors.end()) break; }
    }
    return it == m_partColors.end() ? nullptr : &it->second;
}

bool Library::hasColorTable(int part) const { return colorsOf(part) != nullptr; }

bool Library::colorExists(int part, int paletteIndex) const {
    const std::vector<int>* v = colorsOf(part);
    if (!v || paletteIndex < 0 || paletteIndex >= (int)palette.size()) return true;
    return std::binary_search(v->begin(), v->end(), palette[paletteIndex].id);
}

bool Library::load(const std::wstring& dir, std::string* error) {
    m_dir = dir;
    loadPartColors(dir + L"\\part_colors.txt", m_partColors);
    parts.clear();
    palette.clear();
    m_byName.clear();
    std::vector<uint8_t> text, bin;
    if (!readFile(dir + L"\\base.json", text) || !readFile(dir + L"\\base.bin", bin))
        return fail(error, "assets/brickit/base.json or base.bin missing (tools/brickit/extract_base.py)");
    Json lib;
    if (!Json::parse(std::string(text.begin(), text.end()), lib)) return fail(error, "base.json: bad JSON");
    for (const Json& c : lib["palette"].a) {
        Color col;
        col.id = (int)c["id"].num();
        col.name = c["name"].str();
        col.srgb = float3((float)c["rgb"][0].num() / 255.0f, (float)c["rgb"][1].num() / 255.0f, (float)c["rgb"][2].num() / 255.0f);
        col.group = c["group"].str("standard");
        palette.push_back(col);
    }
    for (const Json& j : lib["parts"].a) {
        Part p;
        p.name = j["name"].str();
        p.base = true;
        readMeta(j, p);
        size_t vo = (size_t)j["vertexOffset"].num(), vc = (size_t)j["vertexCount"].num();
        size_t io = (size_t)j["indexOffset"].num(), ic = (size_t)j["indexCount"].num();
        if (vo + vc * 24 <= bin.size() && io + ic * 4 <= bin.size()) {
            const float* v = (const float*)(bin.data() + vo);
            p.mesh.vertices.resize(vc);
            for (size_t i = 0; i < vc; ++i)
                p.mesh.vertices[i] = {float3(v[i * 6], v[i * 6 + 1], v[i * 6 + 2]), float3(v[i * 6 + 3], v[i * 6 + 4], v[i * 6 + 5]), {0, 0}};
            p.mesh.indices.resize(ic);
            memcpy(p.mesh.indices.data(), bin.data() + io, ic * 4);
        }
        finishMesh(p);
        m_byName[p.name] = (int)parts.size();
        parts.push_back(std::move(p));
    }

    // Technic index (optional: the base set works alone)
    if (readFile(dir + L"\\technic\\index.json", text)) {
        Json idx;
        if (Json::parse(std::string(text.begin(), text.end()), idx)) {
            packCount = (int)idx["packs"].a.size();
            m_packLoaded.assign(packCount, false);
            for (const Json& j : idx["parts"].a) {
                std::string name = j["name"].str();
                if (m_byName.count(name)) continue;
                Part p;
                p.name = name;
                p.pack = (int)j["pack"].num(-1);
                readMeta(j, p);
                if (p.category.empty()) p.category = "Technic";
                m_byName[p.name] = (int)parts.size();
                parts.push_back(std::move(p));
            }
        }
    }
    return true;
}

bool Library::loadPack(int no, std::string* error) {
    if (no < 0 || no >= packCount) return fail(error, "bad pack number");
    if (m_packLoaded[no]) return true;
    m_packLoaded[no] = true;   // tried once
    std::vector<uint8_t> z, raw;
    wchar_t name[64];
    swprintf_s(name, L"\\technic\\g%d.wasm", no);
    if (!readFile(m_dir + name, z)) return fail(error, "technic pack missing");
    if (!vp3d::inflateZlibStream(z.data(), z.size(), raw, z.size() * 3) || raw.size() < 4) return fail(error, "technic pack: bad data");
    uint32_t jl;
    memcpy(&jl, raw.data(), 4);
    if (4 + (size_t)jl > raw.size()) return fail(error, "technic pack: bad header");
    Json metas;
    if (!Json::parse(std::string((const char*)raw.data() + 4, jl), metas)) return fail(error, "technic pack: bad JSON");
    const uint8_t* data = raw.data() + 4 + jl;
    const size_t dataSize = raw.size() - 4 - jl;
    for (const Json& m : metas.a) {
        auto it = m_byName.find(m["name"].str());
        if (it == m_byName.end()) continue;
        Part& p = parts[it->second];
        if (p.loaded) continue;
        readMeta(m, p);
        const Json& q = m["q"];
        size_t off = (size_t)q["off"].num(), vc = (size_t)q["vc"].num(), ic = (size_t)q["ic"].num();
        bool big = q["big"].type == Json::Bool && q["big"].b;
        float mn[3], sc[3];
        for (int k = 0; k < 3; ++k) { mn[k] = (float)q["mn"][k].num(); sc[k] = (float)q["sc"][k].num(); }
        size_t pb = (vc * 6 + 3) / 4 * 4;
        size_t end = off + pb + vc * 4 + ic * (big ? 4 : 2);
        if (end > dataSize) continue;
        const uint16_t* pos = (const uint16_t*)(data + off);
        const int8_t* nrm = (const int8_t*)(data + off + pb);
        p.mesh.vertices.resize(vc);
        for (size_t i = 0; i < vc; ++i) {
            float3 n(nrm[i * 4] / 127.0f, nrm[i * 4 + 1] / 127.0f, nrm[i * 4 + 2] / 127.0f);
            float l = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
            n = l > 0 ? n * (1.0f / l) : float3(0, 1, 0);
            p.mesh.vertices[i] = {float3(mn[0] + pos[i * 3] * sc[0], mn[1] + pos[i * 3 + 1] * sc[1], mn[2] + pos[i * 3 + 2] * sc[2]), n, {0, 0}};
        }
        p.mesh.indices.resize(ic);
        const uint8_t* ib = data + off + pb + vc * 4;
        if (big) memcpy(p.mesh.indices.data(), ib, ic * 4);
        else
            for (size_t i = 0; i < ic; ++i) p.mesh.indices[i] = ((const uint16_t*)ib)[i];
        finishMesh(p);
    }
    return true;
}

bool Library::loadAll(std::string* error) {
    bool ok = true;
    for (int i = 0; i < packCount; ++i) ok &= loadPack(i, error);
    return ok;
}

int Library::find(const std::string& name) const {
    auto it = m_byName.find(name);
    return it == m_byName.end() ? -1 : it->second;
}

Part* Library::get(const std::string& name) {
    int i = find(name);
    if (i < 0) return nullptr;
    Part& p = parts[i];
    if (!p.loaded && p.pack >= 0) loadPack(p.pack);
    return p.loaded ? &p : nullptr;
}

} // namespace brickit
