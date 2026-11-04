// Mesh files: OBJ through vp3d, glTF 2.0 here (see MeshFile.h).
#include "MeshFile.h"
#include "FbxImport.h"
#include "Json.h"
#include "vp3d/AssetImport.h"
#include <algorithm>
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
    Mat4 inverse() const {   // affine inverse
        Affine a;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 4; ++c) a.m[r][c] = m[c * 4 + r];
        Affine i = a.inverse();
        Mat4 o;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 4; ++c) o.m[c * 4 + r] = i.m[r][c];
        return o;
    }
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

// glTF document: JSON and buffers (.gltf with data URIs or .bin files, .glb), accessors read as floats
struct GltfDoc {
    Json j;
    std::vector<std::vector<uint8_t>> buffers;
    std::string error;
    bool load(const std::wstring& path) {
        std::vector<uint8_t> file;
        if (!readBytes(path, file)) { error = "file not found"; return false; }
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
        if (!Json::parse(jsonText, j) || j.type != Json::Object) { error = "not a glTF file"; return false; }
        for (size_t i = 0; i < j["buffers"].a.size(); ++i) {
            const Json& b = j["buffers"][i];
            std::vector<uint8_t> data;
            std::string uri = b["uri"].str();
            if (uri.empty()) data = glbBin;
            else if (uri.rfind("data:", 0) == 0) {
                size_t comma = uri.find(',');
                if (comma == std::string::npos) { error = "bad data uri"; return false; }
                base64(uri, comma + 1, data);
            } else if (!readBytes(fs::path(path).parent_path() / fs::u8path(uri), data)) { error = "buffer not found: " + uri; return false; }
            buffers.push_back(std::move(data));
        }
        return true;
    }
    // accessor -> floats (count x components)
    bool accessor(int index, std::vector<float>& outv, int& comps) const {
        const Json& a = j["accessors"][(size_t)index];
        if (a.type != Json::Object) return false;
        std::string type = a["type"].str();
        comps = type == "SCALAR" ? 1 : type == "VEC2" ? 2 : type == "VEC3" ? 3 : type == "VEC4" ? 4 : type == "MAT4" ? 16 : 0;
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
    }
};

bool loadGltf(const std::wstring& path, MeshData& out, std::vector<UvSet>* sets, std::string* error) {
    auto fail = [&](const std::string& e) { if (error) *error = e; return false; };
    GltfDoc doc;
    if (!doc.load(path)) return fail(doc.error);
    const Json& j = doc.j;
    auto accessor = [&](int index, std::vector<float>& outv, int& comps) { return doc.accessor(index, outv, comps); };

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

// glTF skin (Rig module): the mesh (as loadGltf), the joints of the first skin (scene pose), their bind matrices
// (inverse of inverseBindMatrices), the JOINTS_0 / WEIGHTS_0 weights and the animations sampled at 30 frames per second.
bool loadGltfRig(const std::wstring& path, FbxScene& out, std::string* error) {
    auto fail = [&](const std::string& e) { if (error) *error = e; return false; };
    out = FbxScene();
    if (!loadGltf(path, out.mesh, &out.uvSets, error)) return false;
    GltfDoc doc;
    if (!doc.load(path)) return fail(doc.error);
    const Json& j = doc.j;
    const size_t N = j["nodes"].a.size();
    std::vector<int> parent(N, -1);
    for (size_t n = 0; n < N; ++n)
        for (const Json& c : j["nodes"][n]["children"].a)
            if ((size_t)c.num(0) < N) parent[(size_t)c.num(0)] = (int)n;
    struct TRS { float3 t{0, 0, 0}; float4 r{0, 0, 0, 1}; float3 s{1, 1, 1}; };
    std::vector<TRS> rest(N);
    for (size_t n = 0; n < N; ++n) {
        const Json& nd = j["nodes"][n];
        Mat4 m = nodeMatrix(nd);
        float3 X(m.m[0], m.m[1], m.m[2]), Y(m.m[4], m.m[5], m.m[6]), Z(m.m[8], m.m[9], m.m[10]);
        rest[n].t = float3(m.m[12], m.m[13], m.m[14]);
        rest[n].s = float3(length(X), length(Y), length(Z));
        rest[n].r = qFromBasis(normalize(X), normalize(Y), normalize(Z));
    }
    auto toMat = [](const TRS& x) {
        Mat4 m;
        float3 X = qRot(x.r, float3(1, 0, 0)) * x.s.x, Y = qRot(x.r, float3(0, 1, 0)) * x.s.y, Z = qRot(x.r, float3(0, 0, 1)) * x.s.z;
        m.m[0] = X.x; m.m[1] = X.y; m.m[2] = X.z; m.m[4] = Y.x; m.m[5] = Y.y; m.m[6] = Y.z; m.m[8] = Z.x; m.m[9] = Z.y; m.m[10] = Z.z;
        m.m[12] = x.t.x; m.m[13] = x.t.y; m.m[14] = x.t.z;
        return m;
    };
    auto globals = [&](const std::vector<TRS>& local, std::vector<Mat4>& g) {
        g.assign(N, Mat4());
        std::vector<char> done(N, 0);
        std::function<void(size_t, int)> solve = [&](size_t n, int depth) {
            if (done[n]) return;
            Mat4 l = toMat(local[n]);
            if (parent[n] >= 0 && depth < 256) { solve((size_t)parent[n], depth + 1); g[n] = g[(size_t)parent[n]] * l; }
            else g[n] = l;
            done[n] = 1;
        };
        for (size_t n = 0; n < N; ++n) solve(n, 0);
    };
    auto toAffineM = [](const Mat4& m) {
        Affine a;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 4; ++c) a.m[r][c] = m.m[c * 4 + r];
        return a;
    };
    const Json& skin = j["skins"][0];
    if (skin.type != Json::Object) return true;   // a mesh without skin
    std::vector<int> joints;
    for (const Json& jn : skin["joints"].a) joints.push_back((int)jn.num(0));
    std::vector<int> jointOf(N, -1);
    for (size_t k = 0; k < joints.size(); ++k) if ((size_t)joints[k] < N) jointOf[joints[k]] = (int)k;
    // joints in parent-first order
    std::vector<Mat4> g;
    globals(rest, g);
    std::vector<int> order;
    std::vector<char> added(joints.size(), 0);
    std::function<void(int)> add = [&](int k) {
        if (added[k]) return;
        int p = parent[joints[k]];
        while (p >= 0 && jointOf[p] < 0) p = parent[p];
        if (p >= 0) add(jointOf[p]);
        added[k] = 1;
        order.push_back(k);
    };
    for (size_t k = 0; k < joints.size(); ++k) add((int)k);
    std::vector<int> outIndex(joints.size(), -1);
    std::vector<int> jointParentNode(joints.size(), -1);
    for (int k : order) {
        int p = parent[joints[k]];
        while (p >= 0 && jointOf[p] < 0) p = parent[p];
        jointParentNode[k] = p;
        FbxJoint fj;
        const Json& nd = j["nodes"][(size_t)joints[k]];
        fj.name = nd["name"].str("joint" + std::to_string(k));
        fj.parent = p >= 0 ? outIndex[jointOf[p]] : -1;
        Mat4 local = p >= 0 ? g[(size_t)p].inverse() * g[(size_t)joints[k]] : g[(size_t)joints[k]];
        float3 X(local.m[0], local.m[1], local.m[2]), Y(local.m[4], local.m[5], local.m[6]), Z(local.m[8], local.m[9], local.m[10]);
        fj.translation = float3(local.m[12], local.m[13], local.m[14]);
        fj.scale = float3(length(X), length(Y), length(Z));
        fj.rotation = qFromBasis(normalize(X), normalize(Y), normalize(Z));
        outIndex[k] = (int)out.joints.size();
        out.joints.push_back(fj);
    }
    // bind matrices
    std::vector<float> ibm;
    int comps = 0;
    const bool hasIbm = skin["inverseBindMatrices"].type == Json::Number && doc.accessor((int)skin["inverseBindMatrices"].num(0), ibm, comps) && comps == 16;
    out.clusters.resize(joints.size());
    for (size_t k = 0; k < joints.size(); ++k) {
        out.clusters[k].joint = outIndex[k];
        Mat4 inv;
        if (hasIbm && ibm.size() >= (k + 1) * 16) for (int e = 0; e < 16; ++e) inv.m[e] = ibm[k * 16 + e];
        out.clusters[k].bindJoint = hasIbm ? toAffineM(inv).inverse() : toAffineM(g[(size_t)joints[k]]);
    }
    // weights, in the vertex order of loadGltf (scene nodes depth first, primitives in order)
    uint32_t base = 0;
    bool any = false;
    auto addMesh = [&](int meshIndex, bool skinned) {
        for (const Json& prim : j["meshes"][(size_t)meshIndex]["primitives"].a) {
            if ((int)prim["mode"].num(4) != 4) continue;
            const Json& at = prim["attributes"];
            if (at["POSITION"].type != Json::Number) continue;
            std::vector<float> pos, jw, ww;
            int pc = 0, jc = 0, wc = 0;
            if (!doc.accessor((int)at["POSITION"].num(0), pos, pc)) continue;
            size_t n = pos.size() / 3;
            if (skinned && at["JOINTS_0"].type == Json::Number && at["WEIGHTS_0"].type == Json::Number && doc.accessor((int)at["JOINTS_0"].num(0), jw, jc) &&
                doc.accessor((int)at["WEIGHTS_0"].num(0), ww, wc) && jc == 4 && wc == 4)
                for (size_t v = 0; v < n; ++v)
                    for (int c = 0; c < 4; ++c) {
                        float w = ww[v * 4 + c];
                        size_t k = (size_t)jw[v * 4 + c];
                        if (w <= 0 || k >= joints.size()) continue;
                        out.clusters[k].vertices.push_back(base + (uint32_t)v);
                        out.clusters[k].weights.push_back(w);
                    }
            base += (uint32_t)n;
        }
    };
    std::function<void(int, int)> walk = [&](int node, int depth) {
        if (depth > 64 || (size_t)node >= N) return;
        const Json& nd = j["nodes"][(size_t)node];
        if (nd["mesh"].type == Json::Number) { addMesh((int)nd["mesh"].num(0), nd["skin"].type == Json::Number); any = true; }
        for (const Json& c : nd["children"].a) walk((int)c.num(0), depth + 1);
    };
    const Json& scenes = j["scenes"];
    if (scenes.type == Json::Array && !scenes.a.empty())
        for (const Json& n : scenes[(size_t)j["scene"].num(0)]["nodes"].a) walk((int)n.num(0), 0);
    if (!any)
        for (size_t m = 0; m < j["meshes"].a.size(); ++m) addMesh((int)m, true);
    // animations: node channels sampled per frame, the joint locals rebuilt from the globals
    for (const Json& an : j["animations"].a) {
        struct Ch { int node; int path; std::vector<float> t, v; bool step = false, cubic = false; };
        std::vector<Ch> chans;
        float t0 = 1e30f, t1 = -1e30f;
        for (const Json& c : an["channels"].a) {
            const Json& sm = an["samplers"][(size_t)c["sampler"].num(0)];
            std::string p = c["target"]["path"].str();
            Ch ch;
            ch.node = (int)c["target"]["node"].num(-1);
            ch.path = p == "translation" ? 0 : p == "rotation" ? 1 : p == "scale" ? 2 : -1;
            if (ch.node < 0 || (size_t)ch.node >= N || ch.path < 0) continue;
            int ic = 0, oc = 0;
            if (!doc.accessor((int)sm["input"].num(0), ch.t, ic) || !doc.accessor((int)sm["output"].num(0), ch.v, oc) || ch.t.empty()) continue;
            std::string in = sm["interpolation"].str("LINEAR");
            ch.step = in == "STEP";
            ch.cubic = in == "CUBICSPLINE";
            t0 = std::min(t0, ch.t.front());
            t1 = std::max(t1, ch.t.back());
            chans.push_back(std::move(ch));
        }
        if (chans.empty() || t1 <= t0) continue;
        FbxClip clip;
        clip.name = an["name"].str("Animation " + std::to_string(out.clips.size() + 1));
        clip.frames = std::clamp((int)std::lround((t1 - t0) * clip.fps) + 1, 2, 3000);
        const size_t J = out.joints.size();
        clip.translation.assign(J, std::vector<float3>(clip.frames));
        clip.rotation.assign(J, std::vector<float4>(clip.frames));
        clip.scale.assign(J, std::vector<float3>(clip.frames));
        for (int f = 0; f < clip.frames; ++f) {
            const float t = t0 + f / clip.fps;
            std::vector<TRS> local = rest;
            for (const Ch& ch : chans) {
                const int comp = ch.path == 1 ? 4 : 3;
                const size_t stride = ch.cubic ? 3 : 1;   // cubic spline: in-tangent, value, out-tangent
                const size_t K = ch.t.size();
                size_t i = (size_t)(std::upper_bound(ch.t.begin(), ch.t.end(), t) - ch.t.begin());
                size_t a = i == 0 ? 0 : i - 1, b = std::min(i, K - 1);
                float u = b == a ? 0.0f : clampf((t - ch.t[a]) / std::max(1e-6f, ch.t[b] - ch.t[a]), 0, 1);
                if (ch.step) u = 0;
                auto val = [&](size_t k, int c) { size_t at = (k * stride + (ch.cubic ? 1 : 0)) * comp + c; return at < ch.v.size() ? ch.v[at] : 0.0f; };
                TRS& L = local[(size_t)ch.node];
                if (ch.path == 1) {
                    float4 qa(val(a, 0), val(a, 1), val(a, 2), val(a, 3)), qb(val(b, 0), val(b, 1), val(b, 2), val(b, 3));
                    L.r = qNormalize(qSlerp(qa, qb, u));
                } else {
                    float3 va(val(a, 0), val(a, 1), val(a, 2)), vb(val(b, 0), val(b, 1), val(b, 2));
                    (ch.path == 0 ? L.t : L.s) = lerp(va, vb, u);
                }
            }
            std::vector<Mat4> gf;
            globals(local, gf);
            for (size_t k = 0; k < joints.size(); ++k) {
                int p = jointParentNode[k];
                Mat4 l = p >= 0 ? gf[(size_t)p].inverse() * gf[(size_t)joints[k]] : gf[(size_t)joints[k]];
                float3 X(l.m[0], l.m[1], l.m[2]), Y(l.m[4], l.m[5], l.m[6]), Z(l.m[8], l.m[9], l.m[10]);
                int o = outIndex[k];
                clip.translation[o][f] = float3(l.m[12], l.m[13], l.m[14]);
                clip.scale[o][f] = float3(length(X), length(Y), length(Z));
                clip.rotation[o][f] = qFromBasis(normalize(X), normalize(Y), normalize(Z));
            }
        }
        out.clips.push_back(std::move(clip));
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
