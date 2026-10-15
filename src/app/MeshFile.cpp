// Mesh files: OBJ through vp3d, glTF 2.0 here (see MeshFile.h).
#include "MeshFile.h"
#include "FbxImport.h"
#include "Json.h"
#include "vp3d/AssetImport.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>

namespace app {

namespace fs = std::filesystem;

namespace {

bool readBytes(const fs::path& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    out.resize((size_t)f.tellg());
    f.seekg(0);
    if (!out.empty()) f.read((char*)out.data(), (std::streamsize)out.size());
    return (bool)f;
}

bool base64(const std::string& s, size_t from, std::vector<uint8_t>& out) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+' || c == '-') return 62;
        if (c == '/' || c == '_') return 63;
        return -1;
    };
    out.clear();
    out.reserve((s.size() - from) * 3 / 4);
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = from; i < s.size(); ++i) {
        int v = val(s[i]);
        if (v < 0) { if (s[i] == '=') break; continue; }
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back((uint8_t)((acc >> bits) & 0xFF)); }
    }
    return true;
}

// column-major 4x4 (glTF)
struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    Mat4 operator*(const Mat4& b) const {
        Mat4 r;
        for (int c = 0; c < 4; ++c)
            for (int rr = 0; rr < 4; ++rr) {
                float v = 0;
                for (int k = 0; k < 4; ++k) v += m[k * 4 + rr] * b.m[c * 4 + k];
                r.m[c * 4 + rr] = v;
            }
        return r;
    }
    float3 point(const float3& p) const { return {m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12], m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13], m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]}; }
    float3 vec(const float3& p) const { return {m[0] * p.x + m[4] * p.y + m[8] * p.z, m[1] * p.x + m[5] * p.y + m[9] * p.z, m[2] * p.x + m[6] * p.y + m[10] * p.z}; }
};

Mat4 nodeMatrix(const Json& n) {
    Mat4 r;
    if (n["matrix"].type == Json::Array && n["matrix"].a.size() == 16) {
        for (int i = 0; i < 16; ++i) r.m[i] = (float)n["matrix"][i].num(r.m[i]);
        return r;
    }
    const Json& t = n["translation"];
    const Json& q = n["rotation"];
    const Json& s = n["scale"];
    float tx = (float)t[0].num(0), ty = (float)t[1].num(0), tz = (float)t[2].num(0);
    float x = (float)q[0].num(0), y = (float)q[1].num(0), z = (float)q[2].num(0), w = (float)q[3].num(1);
    float sx = (float)s[0].num(1), sy = (float)s[1].num(1), sz = (float)s[2].num(1);
    float R[9] = {1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w),
                  2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w),
                  2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)};   // columns
    for (int c = 0; c < 3; ++c) {
        float k = c == 0 ? sx : c == 1 ? sy : sz;
        for (int rr = 0; rr < 3; ++rr) r.m[c * 4 + rr] = R[c * 3 + rr] * k;
    }
    r.m[12] = tx; r.m[13] = ty; r.m[14] = tz;
    return r;
}

} // namespace

bool loadGltf(const std::wstring& path, MeshData& out, std::vector<UvSet>* sets, std::string* error) {
    auto fail = [&](const std::string& e) { if (error) *error = e; return false; };
    std::vector<uint8_t> file;
    if (!readBytes(path, file)) return fail("file not found");
    std::string jsonText;
    std::vector<uint8_t> glbBin;
    if (file.size() >= 12 && memcmp(file.data(), "glTF", 4) == 0) {   // binary container: JSON chunk, then BIN chunk
        size_t off = 12;
        while (off + 8 <= file.size()) {
            uint32_t len, type;
            memcpy(&len, &file[off], 4);
            memcpy(&type, &file[off + 4], 4);
            if (off + 8 + len > file.size()) break;
            if (type == 0x4E4F534A) jsonText.assign((const char*)&file[off + 8], len);
            else if (type == 0x004E4942) glbBin.assign(file.begin() + off + 8, file.begin() + off + 8 + len);
            off += 8 + ((len + 3) & ~3u);
        }
    } else {
        jsonText.assign((const char*)file.data(), file.size());
    }
    Json j;
    if (!Json::parse(jsonText, j) || j.type != Json::Object) return fail("not a glTF file");
    // buffers
    std::vector<std::vector<uint8_t>> buffers;
    for (size_t i = 0; i < j["buffers"].a.size(); ++i) {
        const Json& b = j["buffers"][i];
        std::vector<uint8_t> data;
        std::string uri = b["uri"].str();
        if (uri.empty()) data = glbBin;
        else if (uri.rfind("data:", 0) == 0) {
            size_t comma = uri.find(',');
            if (comma == std::string::npos) return fail("bad data uri");
            base64(uri, comma + 1, data);
        } else if (!readBytes(fs::path(path).parent_path() / fs::u8path(uri), data)) return fail("buffer not found: " + uri);
        buffers.push_back(std::move(data));
    }
    // accessor -> floats (count x components)
    auto accessor = [&](int index, std::vector<float>& outv, int& comps) -> bool {
        const Json& a = j["accessors"][(size_t)index];
        if (a.type != Json::Object) return false;
        std::string type = a["type"].str();
        comps = type == "SCALAR" ? 1 : type == "VEC2" ? 2 : type == "VEC3" ? 3 : type == "VEC4" ? 4 : 0;
        if (!comps) return false;
        size_t count = (size_t)a["count"].num(0);
        int ct = (int)a["componentType"].num(5126);
        bool norm = a["normalized"].type == Json::Bool && a["normalized"].b;
        int csize = ct == 5126 || ct == 5125 ? 4 : ct == 5123 || ct == 5122 ? 2 : 1;
        outv.assign(count * comps, 0.0f);
        if (a["bufferView"].type != Json::Number) return true;   // all zeros (sparse only: not supported)
        const Json& bv = j["bufferViews"][(size_t)a["bufferView"].num(0)];
        size_t bi = (size_t)bv["buffer"].num(0);
        if (bi >= buffers.size()) return false;
        const std::vector<uint8_t>& buf = buffers[bi];
        size_t base = (size_t)bv["byteOffset"].num(0) + (size_t)a["byteOffset"].num(0);
        size_t stride = (size_t)bv["byteStride"].num(0);
        if (!stride) stride = (size_t)csize * comps;
        for (size_t e = 0; e < count; ++e)
            for (int c = 0; c < comps; ++c) {
                size_t o = base + e * stride + (size_t)c * csize;
                if (o + csize > buf.size()) return false;
                float v = 0;
                switch (ct) {
                case 5126: memcpy(&v, &buf[o], 4); break;
                case 5125: { uint32_t u; memcpy(&u, &buf[o], 4); v = (float)u; break; }
                case 5123: { uint16_t u; memcpy(&u, &buf[o], 2); v = norm ? u / 65535.0f : (float)u; break; }
                case 5122: { int16_t u; memcpy(&u, &buf[o], 2); v = norm ? std::max(u / 32767.0f, -1.0f) : (float)u; break; }
                case 5121: v = norm ? buf[o] / 255.0f : (float)buf[o]; break;
                case 5120: v = norm ? std::max((int8_t)buf[o] / 127.0f, -1.0f) : (float)(int8_t)buf[o]; break;
                }
                outv[e * comps + c] = v;
            }
        return true;
    };

    out = MeshData();
    std::vector<std::vector<float2>> extra;   // sets 1..n
    std::vector<std::string> names;
    bool missingNormals = false;
    auto addMesh = [&](int meshIndex, const Mat4& m) -> bool {
        const Json& mesh = j["meshes"][(size_t)meshIndex];
        const Json& extrasNames = mesh["extras"]["uvSetNames"];
        for (size_t k = 0; k < extrasNames.a.size(); ++k)
            if (k >= names.size()) names.push_back(extrasNames[k].str());
        for (const Json& prim : mesh["primitives"].a) {
            if ((int)prim["mode"].num(4) != 4) continue;   // triangles only
            const Json& at = prim["attributes"];
            if (at["POSITION"].type != Json::Number) continue;
            std::vector<float> pos, nrm, idx;
            int pc = 0, nc = 0, ic = 0;
            if (!accessor((int)at["POSITION"].num(0), pos, pc) || pc != 3) return false;
            size_t n = pos.size() / 3;
            bool hasN = at["NORMAL"].type == Json::Number && accessor((int)at["NORMAL"].num(0), nrm, nc) && nc == 3;
            missingNormals |= !hasN;
            std::vector<std::vector<float>> uvs;
            for (int t = 0; t < 8; ++t) {
                const Json& a = at["TEXCOORD_" + std::to_string(t)];
                if (a.type != Json::Number) break;
                std::vector<float> uv;
                int c = 0;
                if (!accessor((int)a.num(0), uv, c) || c != 2) break;
                uvs.push_back(std::move(uv));
            }
            const uint32_t base = (uint32_t)out.vertices.size();
            for (size_t i = 0; i < n; ++i) {
                Vertex v;
                v.pos = m.point(float3(pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2]));
                v.nrm = hasN ? normalize(m.vec(float3(nrm[i * 3], nrm[i * 3 + 1], nrm[i * 3 + 2]))) : float3(0);
                if (!uvs.empty()) v.uv = float2(uvs[0][i * 2], uvs[0][i * 2 + 1]);   // glTF: v down, as the engine (row 0 on top)
                out.vertices.push_back(v);
            }
            // extra sets: grow to the number of sets of this primitive, fill the vertices of earlier primitives with 0
            while (extra.size() + 1 < uvs.size()) extra.emplace_back(base, float2(0, 0));
            for (size_t s = 0; s < extra.size(); ++s) {
                extra[s].resize(base, float2(0, 0));
                for (size_t i = 0; i < n; ++i)
                    extra[s].push_back(s + 1 < uvs.size() ? float2(uvs[s + 1][i * 2], 1.0f - uvs[s + 1][i * 2 + 1]) : float2(0, 0));
            }
            const size_t i0 = out.indices.size();
            if (prim["indices"].type == Json::Number && accessor((int)prim["indices"].num(0), idx, ic)) {
                for (size_t i = 0; i + 2 < idx.size(); i += 3) out.addTri(base + (uint32_t)idx[i], base + (uint32_t)idx[i + 1], base + (uint32_t)idx[i + 2]);
            } else {
                for (uint32_t i = 0; i + 2 < n; i += 3) out.addTri(base + i, base + i + 1, base + i + 2);
            }
            // a mirrored node (negative determinant) flips the winding
            float3 c0 = m.vec(float3(1, 0, 0)), c1 = m.vec(float3(0, 1, 0)), c2 = m.vec(float3(0, 0, 1));
            if (dot(cross(c0, c1), c2) < 0)
                for (size_t i = i0; i + 2 < out.indices.size(); i += 3) std::swap(out.indices[i + 1], out.indices[i + 2]);
        }
        return true;
    };
    bool any = false;
    std::function<bool(int, const Mat4&, int)> walk = [&](int node, const Mat4& parent, int depth) -> bool {
        if (depth > 64) return true;
        const Json& n = j["nodes"][(size_t)node];
        Mat4 m = parent * nodeMatrix(n);
        if (n["mesh"].type == Json::Number) { if (!addMesh((int)n["mesh"].num(0), m)) return false; any = true; }
        for (const Json& c : n["children"].a)
            if (!walk((int)c.num(0), m, depth + 1)) return false;
        return true;
    };
    const Json& scenes = j["scenes"];
    if (scenes.type == Json::Array && !scenes.a.empty()) {
        const Json& sc = scenes[(size_t)j["scene"].num(0)];
        for (const Json& n : sc["nodes"].a)
            if (!walk((int)n.num(0), Mat4(), 0)) return fail("bad accessor");
    }
    if (!any)
        for (size_t i = 0; i < j["meshes"].a.size(); ++i)
            if (!addMesh((int)i, Mat4())) return fail("bad accessor");
    if (out.indices.empty()) return fail("no triangle mesh");
    for (auto& e : extra) e.resize(out.vertices.size(), float2(0, 0));
    if (missingNormals) {   // smooth normals from the faces (only the vertices without a normal)
        std::vector<float3> acc(out.vertices.size(), float3(0));
        for (size_t i = 0; i + 2 < out.indices.size(); i += 3) {
            const float3 &a = out.vertices[out.indices[i]].pos, &b = out.vertices[out.indices[i + 1]].pos, &c = out.vertices[out.indices[i + 2]].pos;
            float3 fn = cross(b - a, c - a);
            for (int k = 0; k < 3; ++k) acc[out.indices[i + k]] += fn;
        }
        for (size_t i = 0; i < out.vertices.size(); ++i)
            if (dot(out.vertices[i].nrm, out.vertices[i].nrm) < 0.25f) out.vertices[i].nrm = normalize(acc[i]);
    }
    if (sets) {
        sets->clear();
        UvSet s0;
        s0.name = names.size() > 0 && !names[0].empty() ? names[0] : "UV0";
        for (const Vertex& v : out.vertices) s0.uv.push_back(float2(v.uv.x, 1.0f - v.uv.y));
        sets->push_back(std::move(s0));
        for (size_t s = 0; s < extra.size(); ++s) {
            UvSet e;
            e.name = names.size() > s + 1 && !names[s + 1].empty() ? names[s + 1] : "UV" + std::to_string(s + 1);
            e.uv = std::move(extra[s]);
            sets->push_back(std::move(e));
        }
    }
    return true;
}

bool isMeshFile(const std::wstring& path) {
    std::wstring e = fs::path(path).extension().wstring();
    for (auto& c : e) c = (wchar_t)towlower(c);
    return e == L".obj" || e == L".gltf" || e == L".glb" || e == L".fbx";
}

bool loadMeshFile(const std::wstring& path, MeshData& out, std::vector<UvSet>* sets, std::string* error) {
    std::wstring e = fs::path(path).extension().wstring();
    for (auto& c : e) c = (wchar_t)towlower(c);
    if (e == L".gltf" || e == L".glb") return loadGltf(path, out, sets, error);
    if (e == L".fbx") {   // the mesh of the FBX (all its meshes merged, bind pose); joints and skin: Rig module
        FbxScene fbx;
        if (!loadFbx(path, fbx, error)) return false;
        out = std::move(fbx.mesh);
        if (sets) *sets = std::move(fbx.uvSets);
        return true;
    }
    if (!loadObj(path, out, nullptr, error)) return false;
    if (sets) {
        sets->clear();
        UvSet s0;
        s0.name = "UV0";
        for (const Vertex& v : out.vertices) s0.uv.push_back(float2(v.uv.x, 1.0f - v.uv.y));
        sets->push_back(std::move(s0));
    }
    return true;
}

} // namespace app
