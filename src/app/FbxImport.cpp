// FBX import (see FbxImport.h): binary node tree reader, zlib inflate, scene interpretation (geometry, models,
// skin clusters, connections).
#include "FbxImport.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <unordered_map>

namespace app {

// --------------------------------------------------------------------------- inflate (RFC 1950 / 1951)

namespace {

struct BitReader {
    const uint8_t* p;
    size_t n, pos = 0;
    uint32_t buf = 0;
    int cnt = 0;
    bool bad = false;
    int bit() {
        if (!cnt) {
            if (pos >= n) { bad = true; return 0; }
            buf = p[pos++];
            cnt = 8;
        }
        int b = buf & 1;
        buf >>= 1;
        --cnt;
        return b;
    }
    uint32_t bits(int k) {
        uint32_t v = 0;
        for (int i = 0; i < k; ++i) v |= (uint32_t)bit() << i;
        return v;
    }
};

struct Huffman {
    uint16_t counts[16] = {};
    uint16_t symbols[320] = {};
    void build(const uint8_t* lengths, int n) {
        memset(counts, 0, sizeof(counts));
        for (int i = 0; i < n; ++i) counts[lengths[i]]++;
        counts[0] = 0;
        uint16_t offs[16] = {};
        for (int i = 1; i < 15; ++i) offs[i + 1] = offs[i] + counts[i];
        for (int i = 0; i < n; ++i)
            if (lengths[i]) symbols[offs[lengths[i]]++] = (uint16_t)i;
    }
    int decode(BitReader& b) const {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; ++len) {
            code |= b.bit();
            int count = counts[len];
            if (code - count < first) return symbols[index + (code - first)];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
            if (b.bad) return -1;
        }
        return -1;
    }
};

const uint16_t kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool inflateBlock(BitReader& b, const Huffman& lit, const Huffman& dist, std::vector<uint8_t>& out) {
    for (;;) {
        int sym = lit.decode(b);
        if (sym < 0 || b.bad) return false;
        if (sym < 256) { out.push_back((uint8_t)sym); continue; }
        if (sym == 256) return true;
        sym -= 257;
        if (sym >= 29) return false;
        size_t len = kLenBase[sym] + b.bits(kLenExtra[sym]);
        int ds = dist.decode(b);
        if (ds < 0 || ds >= 30) return false;
        size_t d = kDistBase[ds] + b.bits(kDistExtra[ds]);
        if (d > out.size()) return false;
        size_t from = out.size() - d;
        for (size_t i = 0; i < len; ++i) out.push_back(out[from + i]);
    }
}

} // namespace

bool zlibInflate(const uint8_t* src, size_t size, std::vector<uint8_t>& out, size_t expected) {
    out.clear();
    out.reserve(expected);
    if (size < 2) return false;
    BitReader b{src + 2, size - 2};   // skip the zlib header (CMF, FLG)
    int final = 0;
    do {
        final = b.bit();
        int type = (int)b.bits(2);
        if (type == 0) {   // stored
            b.cnt = 0;
            if (b.pos + 4 > b.n) return false;
            uint16_t len = (uint16_t)(b.p[b.pos] | (b.p[b.pos + 1] << 8));
            b.pos += 4;
            if (b.pos + len > b.n) return false;
            out.insert(out.end(), b.p + b.pos, b.p + b.pos + len);
            b.pos += len;
        } else if (type == 1) {   // fixed codes
            uint8_t l[288], d[30];
            for (int i = 0; i < 144; ++i) l[i] = 8;
            for (int i = 144; i < 256; ++i) l[i] = 9;
            for (int i = 256; i < 280; ++i) l[i] = 7;
            for (int i = 280; i < 288; ++i) l[i] = 8;
            for (int i = 0; i < 30; ++i) d[i] = 5;
            Huffman lit, dist;
            lit.build(l, 288);
            dist.build(d, 30);
            if (!inflateBlock(b, lit, dist, out)) return false;
        } else if (type == 2) {   // dynamic codes
            int hlit = (int)b.bits(5) + 257, hdist = (int)b.bits(5) + 1, hclen = (int)b.bits(4) + 4;
            static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            uint8_t cl[19] = {};
            for (int i = 0; i < hclen; ++i) cl[order[i]] = (uint8_t)b.bits(3);
            Huffman clh;
            clh.build(cl, 19);
            uint8_t lens[320] = {};
            int n = 0;
            while (n < hlit + hdist) {
                int sym = clh.decode(b);
                if (sym < 0 || b.bad) return false;
                if (sym < 16) lens[n++] = (uint8_t)sym;
                else {
                    int rep = 0;
                    uint8_t val = 0;
                    if (sym == 16) { if (!n) return false; val = lens[n - 1]; rep = 3 + (int)b.bits(2); }
                    else if (sym == 17) rep = 3 + (int)b.bits(3);
                    else rep = 11 + (int)b.bits(7);
                    if (n + rep > hlit + hdist) return false;
                    while (rep--) lens[n++] = val;
                }
            }
            Huffman lit, dist;
            lit.build(lens, hlit);
            dist.build(lens + hlit, hdist);
            if (!inflateBlock(b, lit, dist, out)) return false;
        } else return false;
    } while (!final && !b.bad);
    return !b.bad;
}

// --------------------------------------------------------------------------- binary node tree

namespace {

struct Prop {
    char type = 0;
    int64_t i = 0;
    double d = 0;
    std::string s;
    std::vector<double> ad;      // f / d arrays
    std::vector<int64_t> ai;     // i / l / b arrays
};

struct Node {
    std::string name;
    std::vector<Prop> props;
    std::vector<Node> kids;
    const Node* child(const char* n) const {
        for (const Node& k : kids) if (k.name == n) return &k;
        return nullptr;
    }
};

class Parser {
public:
    const std::vector<uint8_t>& d;
    uint32_t version = 0;
    std::string err;
    explicit Parser(const std::vector<uint8_t>& data) : d(data) {}

    bool parse(Node& root) {
        static const char magic[] = "Kaydara FBX Binary  ";
        if (d.size() < 27 || memcmp(d.data(), magic, 20) != 0) { err = "not a binary FBX (ASCII FBX is not supported)"; return false; }
        memcpy(&version, &d[23], 4);
        size_t pos = 27;
        while (pos < d.size()) {
            Node n;
            bool end = false;
            if (!node(pos, n, end)) return false;
            if (end) break;
            root.kids.push_back(std::move(n));
        }
        return true;
    }

private:
    template <class T> bool rd(size_t& pos, T& v) {
        if (pos + sizeof(T) > d.size()) { err = "truncated file"; return false; }
        memcpy(&v, &d[pos], sizeof(T));
        pos += sizeof(T);
        return true;
    }
    bool node(size_t& pos, Node& n, bool& isNull) {
        uint64_t endOff = 0, numProps = 0, listLen = 0;
        if (version >= 7500) {
            if (!rd(pos, endOff) || !rd(pos, numProps) || !rd(pos, listLen)) return false;
        } else {
            uint32_t a, b, c;
            if (!rd(pos, a) || !rd(pos, b) || !rd(pos, c)) return false;
            endOff = a; numProps = b; listLen = c;
        }
        uint8_t nameLen;
        if (!rd(pos, nameLen)) return false;
        if (endOff == 0) { isNull = true; return true; }
        if (pos + nameLen > d.size() || endOff > d.size()) { err = "bad node"; return false; }
        n.name.assign((const char*)&d[pos], nameLen);
        pos += nameLen;
        size_t propEnd = pos + listLen;
        for (uint64_t k = 0; k < numProps; ++k) {
            Prop p;
            if (!prop(pos, p)) return false;
            n.props.push_back(std::move(p));
        }
        pos = propEnd;
        while (pos < endOff) {
            Node c;
            bool cn = false;
            if (!node(pos, c, cn)) return false;
            if (cn) break;
            n.kids.push_back(std::move(c));
        }
        pos = endOff;
        return true;
    }
    bool prop(size_t& pos, Prop& p) {
        if (pos >= d.size()) { err = "truncated property"; return false; }
        p.type = (char)d[pos++];
        switch (p.type) {
        case 'Y': { int16_t v; if (!rd(pos, v)) return false; p.i = v; p.d = v; return true; }
        case 'C': { uint8_t v; if (!rd(pos, v)) return false; p.i = v; p.d = v; return true; }
        case 'I': { int32_t v; if (!rd(pos, v)) return false; p.i = v; p.d = v; return true; }
        case 'F': { float v; if (!rd(pos, v)) return false; p.d = v; p.i = (int64_t)v; return true; }
        case 'D': { double v; if (!rd(pos, v)) return false; p.d = v; p.i = (int64_t)v; return true; }
        case 'L': { int64_t v; if (!rd(pos, v)) return false; p.i = v; p.d = (double)v; return true; }
        case 'S': case 'R': {
            uint32_t len;
            if (!rd(pos, len) || pos + len > d.size()) { err = "bad string"; return false; }
            p.s.assign((const char*)&d[pos], len);
            pos += len;
            return true;
        }
        case 'f': case 'd': case 'l': case 'i': case 'b': {
            uint32_t count, enc, clen;
            if (!rd(pos, count) || !rd(pos, enc) || !rd(pos, clen) || pos + clen > d.size()) { err = "bad array"; return false; }
            const size_t esz = p.type == 'd' || p.type == 'l' ? 8 : p.type == 'b' ? 1 : 4;
            std::vector<uint8_t> raw;
            const uint8_t* src = &d[pos];
            if (enc == 1) {
                if (!zlibInflate(src, clen, raw, (size_t)count * esz)) { err = "bad compressed array"; return false; }
                src = raw.data();
            }
            if ((enc == 1 ? raw.size() : clen) < (size_t)count * esz) { err = "short array"; return false; }
            pos += clen;
            if (p.type == 'f' || p.type == 'd') {
                p.ad.resize(count);
                for (uint32_t k = 0; k < count; ++k) {
                    if (p.type == 'f') { float v; memcpy(&v, src + k * 4, 4); p.ad[k] = v; }
                    else memcpy(&p.ad[k], src + k * 8, 8);
                }
            } else {
                p.ai.resize(count);
                for (uint32_t k = 0; k < count; ++k) {
                    if (p.type == 'i') { int32_t v; memcpy(&v, src + k * 4, 4); p.ai[k] = v; }
                    else if (p.type == 'l') memcpy(&p.ai[k], src + k * 8, 8);
                    else p.ai[k] = src[k];
                }
            }
            return true;
        }
        default: err = std::string("unknown property type ") + p.type; return false;
        }
    }
};

// --------------------------------------------------------------------------- scene

// 4 x 4 column-major (FBX), column vectors
struct M4 {
    double m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    M4 operator*(const M4& b) const {
        M4 r;
        for (int c = 0; c < 4; ++c)
            for (int rr = 0; rr < 4; ++rr) {
                double v = 0;
                for (int k = 0; k < 4; ++k) v += m[k * 4 + rr] * b.m[c * 4 + k];
                r.m[c * 4 + rr] = v;
            }
        return r;
    }
    float3 point(const float3& p) const {
        return float3((float)(m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12]), (float)(m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13]),
                      (float)(m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]));
    }
    float3 vec(const float3& p) const {
        return float3((float)(m[0] * p.x + m[4] * p.y + m[8] * p.z), (float)(m[1] * p.x + m[5] * p.y + m[9] * p.z), (float)(m[2] * p.x + m[6] * p.y + m[10] * p.z));
    }
    M4 inverse() const {   // affine inverse
        Affine a = affine();
        Affine i = a.inverse();
        M4 r;
        for (int rr = 0; rr < 3; ++rr)
            for (int c = 0; c < 4; ++c) r.m[c * 4 + rr] = i.m[rr][c];
        return r;
    }
    Affine affine() const {
        Affine a;
        for (int rr = 0; rr < 3; ++rr)
            for (int c = 0; c < 4; ++c) a.m[rr][c] = (float)m[c * 4 + rr];
        return a;
    }
};

M4 translate(double x, double y, double z) { M4 r; r.m[12] = x; r.m[13] = y; r.m[14] = z; return r; }
M4 scaleM(double x, double y, double z) { M4 r; r.m[0] = x; r.m[5] = y; r.m[10] = z; return r; }
M4 rotAxis(int axis, double deg) {
    double a = deg * 3.14159265358979323846 / 180.0, c = std::cos(a), s = std::sin(a);
    M4 r;
    int i = (axis + 1) % 3, j = (axis + 2) % 3;
    r.m[i * 4 + i] = c; r.m[j * 4 + j] = c;
    r.m[i * 4 + j] = s; r.m[j * 4 + i] = -s;
    return r;
}
// FBX Euler order (0 XYZ ... 5 ZYX): the first letter is applied first
M4 eulerM(const double* deg, int order) {
    static const int seq[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 2, 0}, {1, 0, 2}, {2, 0, 1}, {2, 1, 0}};
    const int* q = seq[std::clamp(order, 0, 5)];
    return rotAxis(q[2], deg[q[2]]) * rotAxis(q[1], deg[q[1]]) * rotAxis(q[0], deg[q[0]]);
}

std::string objName(const std::string& s) {
    size_t z = s.find(std::string("\0\x01", 2));
    return z == std::string::npos ? s : s.substr(0, z);
}

struct Model {
    int64_t id = 0;
    std::string name, type;
    double t[3] = {0, 0, 0}, r[3] = {0, 0, 0}, s[3] = {1, 1, 1}, pre[3] = {0, 0, 0}, post[3] = {0, 0, 0};
    double rp[3] = {0, 0, 0}, sp[3] = {0, 0, 0}, roff[3] = {0, 0, 0}, soff[3] = {0, 0, 0};
    double gt[3] = {0, 0, 0}, gr[3] = {0, 0, 0}, gs[3] = {1, 1, 1};
    int order = 0;
    int64_t parent = 0;
    M4 local() const {
        M4 R = eulerM(r, order), Pre = eulerM(pre, 0), Post = eulerM(post, 0);
        M4 PostInv = Post.inverse();
        return translate(t[0], t[1], t[2]) * translate(roff[0], roff[1], roff[2]) * translate(rp[0], rp[1], rp[2]) * Pre * R * PostInv *
               translate(-rp[0], -rp[1], -rp[2]) * translate(soff[0], soff[1], soff[2]) * translate(sp[0], sp[1], sp[2]) * scaleM(s[0], s[1], s[2]) *
               translate(-sp[0], -sp[1], -sp[2]);
    }
    M4 geometric() const { return translate(gt[0], gt[1], gt[2]) * eulerM(gr, 0) * scaleM(gs[0], gs[1], gs[2]); }
};

void readProps70(const Node& n, Model& m) {
    const Node* p70 = n.child("Properties70");
    if (!p70) return;
    for (const Node& p : p70->kids) {
        if (p.props.empty()) continue;
        const std::string& k = p.props[0].s;
        auto v3 = [&](double* out) { for (int i = 0; i < 3 && 4 + i < (int)p.props.size(); ++i) out[i] = p.props[4 + i].d; };
        if (k == "Lcl Translation") v3(m.t);
        else if (k == "Lcl Rotation") v3(m.r);
        else if (k == "Lcl Scaling") v3(m.s);
        else if (k == "PreRotation") v3(m.pre);
        else if (k == "PostRotation") v3(m.post);
        else if (k == "RotationPivot") v3(m.rp);
        else if (k == "ScalingPivot") v3(m.sp);
        else if (k == "RotationOffset") v3(m.roff);
        else if (k == "ScalingOffset") v3(m.soff);
        else if (k == "GeometricTranslation") v3(m.gt);
        else if (k == "GeometricRotation") v3(m.gr);
        else if (k == "GeometricScaling") v3(m.gs);
        else if (k == "RotationOrder" && p.props.size() > 4) m.order = (int)p.props[4].i;
    }
}

// decompose an affine matrix into translation, rotation, scale (no shear)
void decompose(const M4& m, float3& t, float4& q, float3& s) {
    t = float3((float)m.m[12], (float)m.m[13], (float)m.m[14]);
    float3 X((float)m.m[0], (float)m.m[1], (float)m.m[2]), Y((float)m.m[4], (float)m.m[5], (float)m.m[6]), Z((float)m.m[8], (float)m.m[9], (float)m.m[10]);
    s = float3(length(X), length(Y), length(Z));
    if (dot(cross(X, Y), Z) < 0) { s.x = -s.x; X = -X; }
    q = qFromBasis(normalize(X), normalize(Y), normalize(Z));
}

const Prop* firstArray(const Node* n) {
    if (!n) return nullptr;
    for (const Prop& p : n->props)
        if (!p.ad.empty() || !p.ai.empty()) return &p;
    return n->props.empty() ? nullptr : &n->props[0];
}

} // namespace

bool loadFbx(const std::wstring& path, FbxScene& out, std::string* error) {
    auto fail = [&](const std::string& e) { if (error) *error = e; return false; };
    std::ifstream f(path, std::ios::binary);
    if (!f) return fail("file not found");
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    Parser parser(data);
    Node root;
    if (!parser.parse(root)) return fail(parser.err);
    out = FbxScene();
    out.version = (int)parser.version;
    const Node* objects = root.child("Objects");
    const Node* connections = root.child("Connections");
    if (!objects) return fail("no Objects");

    std::map<int64_t, const Node*> geometries, deformers;
    std::map<int64_t, Model> models;
    for (const Node& o : objects->kids) {
        if (o.props.size() < 3) continue;
        int64_t id = o.props[0].i;
        if (o.name == "Geometry" && o.props[2].s == "Mesh") geometries[id] = &o;
        else if (o.name == "Deformer") deformers[id] = &o;
        else if (o.name == "Model") {
            Model m;
            m.id = id;
            m.name = objName(o.props[1].s);
            m.type = o.props[2].s;
            readProps70(o, m);
            models[id] = m;
        }
    }
    // connections: object -> object (child, parent)
    std::multimap<int64_t, int64_t> parentsOf, childrenOf;
    if (connections)
        for (const Node& c : connections->kids)
            if (c.name == "C" && c.props.size() >= 3 && c.props[0].s == "OO") {
                parentsOf.emplace(c.props[1].i, c.props[2].i);
                childrenOf.emplace(c.props[2].i, c.props[1].i);
            }
    for (auto& kv : models) {
        auto range = parentsOf.equal_range(kv.first);
        for (auto it = range.first; it != range.second; ++it)
            if (models.count(it->second)) kv.second.parent = it->second;
    }
    std::map<int64_t, M4> global;
    std::function<M4(int64_t, int)> globalOf = [&](int64_t id, int depth) -> M4 {
        auto g = global.find(id);
        if (g != global.end()) return g->second;
        const Model& m = models[id];
        M4 r = m.local();
        if (m.parent && depth < 256) r = globalOf(m.parent, depth + 1) * r;
        global[id] = r;
        return r;
    };

    // skin clusters: geometry -> skin -> clusters (joint model, indexes, weights, matrices)
    struct ClusterData { int64_t joint = 0; const Prop* idx = nullptr; const Prop* w = nullptr; M4 transform, link; };
    std::map<int64_t, std::vector<ClusterData>> clustersOfGeometry;
    for (auto& g : geometries) {
        auto sr = childrenOf.equal_range(g.first);
        for (auto si = sr.first; si != sr.second; ++si) {
            auto sd = deformers.find(si->second);
            if (sd == deformers.end() || sd->second->props.size() < 3 || sd->second->props[2].s != "Skin") continue;
            auto cr = childrenOf.equal_range(si->second);
            for (auto ci = cr.first; ci != cr.second; ++ci) {
                auto cd = deformers.find(ci->second);
                if (cd == deformers.end() || cd->second->props.size() < 3 || cd->second->props[2].s != "Cluster") continue;
                ClusterData c;
                auto jr = childrenOf.equal_range(ci->second);
                for (auto ji = jr.first; ji != jr.second; ++ji)
                    if (models.count(ji->second)) c.joint = ji->second;
                c.idx = firstArray(cd->second->child("Indexes"));
                c.w = firstArray(cd->second->child("Weights"));
                const Prop* t = firstArray(cd->second->child("Transform"));
                const Prop* l = firstArray(cd->second->child("TransformLink"));
                if (t && t->ad.size() == 16) for (int k = 0; k < 16; ++k) c.transform.m[k] = t->ad[k];
                if (l && l->ad.size() == 16) for (int k = 0; k < 16; ++k) c.link.m[k] = l->ad[k];
                if (c.joint) clustersOfGeometry[g.first].push_back(c);
            }
        }
    }

    // joints: LimbNode / Root models, the cluster links, and the models between them (their parents)
    std::map<int64_t, bool> isJoint;
    for (auto& kv : models)
        if (kv.second.type == "LimbNode" || kv.second.type == "Root") isJoint[kv.first] = true;
    for (auto& g : clustersOfGeometry)
        for (auto& c : g.second) isJoint[c.joint] = true;
    std::map<int64_t, int> jointIndex;
    std::function<void(int64_t)> addJoint = [&](int64_t id) {
        if (jointIndex.count(id)) return;
        const Model& m = models[id];
        int64_t pj = m.parent;
        while (pj && !isJoint.count(pj)) pj = models[pj].parent;   // nearest joint ancestor
        if (pj) addJoint(pj);
        FbxJoint j;
        j.name = m.name;
        j.parent = pj ? jointIndex[pj] : -1;
        M4 local = pj ? globalOf(pj, 0).inverse() * globalOf(id, 0) : globalOf(id, 0);
        decompose(local, j.translation, j.rotation, j.scale);
        jointIndex[id] = (int)out.joints.size();
        out.joints.push_back(j);
    };
    for (auto& kv : isJoint) addJoint(kv.first);

    // meshes: geometry + its model, baked at the bind pose
    std::map<int, size_t> clusterOfJoint;   // joint -> out.clusters index
    std::vector<std::vector<float2>> extraUv;
    std::vector<std::string> uvNames;
    for (auto& g : geometries) {
        int64_t modelId = 0;
        auto mr = parentsOf.equal_range(g.first);
        for (auto it = mr.first; it != mr.second; ++it)
            if (models.count(it->second)) modelId = it->second;
        if (!modelId) continue;
        const Node& geo = *g.second;
        const Prop* vp = firstArray(geo.child("Vertices"));
        const Prop* ip = firstArray(geo.child("PolygonVertexIndex"));
        if (!vp || !ip || vp->ad.size() < 3 || ip->ai.empty()) continue;
        const auto& clusters = clustersOfGeometry[g.first];
        // mesh at bind time: the cluster's Transform is the mesh global matrix for the FBX SDK, but many files store
        // it relative to the joint (Transform = TransformLink^-1 * mesh global). The reading closest to the mesh
        // model's own global transform wins.
        M4 meshGlobal = globalOf(modelId, 0);
        M4 bindGlobal = meshGlobal;
        if (!clusters.empty()) {
            M4 a = clusters[0].transform, b = clusters[0].link * clusters[0].transform;
            auto diff = [&](const M4& m) { double d = 0; for (int k = 0; k < 16; ++k) d += std::fabs(m.m[k] - meshGlobal.m[k]); return d; };
            bindGlobal = diff(b) < diff(a) ? b : a;
        }
        M4 bake = bindGlobal * models[modelId].geometric();
        // layers: normals and UV sets
        struct Layer { std::vector<double> data; std::vector<int64_t> index; bool byPolyVertex = true; bool indexed = false; int comps = 3; std::string name; };
        auto layer = [&](const Node* ln, const char* dataName, const char* indexName, int comps) {
            Layer L;
            L.comps = comps;
            if (!ln) return L;
            const Prop* dp = firstArray(ln->child(dataName));
            if (dp) L.data = dp->ad;
            const Prop* xp = firstArray(ln->child(indexName));
            if (xp) L.index = xp->ai;
            const Node* mi = ln->child("MappingInformationType");
            const Node* ri = ln->child("ReferenceInformationType");
            std::string map = mi && !mi->props.empty() ? mi->props[0].s : "ByPolygonVertex";
            std::string ref = ri && !ri->props.empty() ? ri->props[0].s : "Direct";
            L.byPolyVertex = map == "ByPolygonVertex";
            L.indexed = ref == "IndexToDirect" || ref == "Index";
            const Node* nm = ln->child("Name");
            if (nm && !nm->props.empty()) L.name = nm->props[0].s;
            return L;
        };
        Layer normals = layer(geo.child("LayerElementNormal"), "Normals", "NormalsIndex", 3);
        std::vector<Layer> uvs;
        for (const Node& k : geo.kids)
            if (k.name == "LayerElementUV") uvs.push_back(layer(&k, "UV", "UVIndex", 2));
        auto fetch = [&](const Layer& L, size_t polyVertex, int cp, int c) -> double {
            size_t e = L.byPolyVertex ? polyVertex : (size_t)cp;
            if (L.indexed) { if (e >= L.index.size()) return 0; e = (size_t)L.index[e]; }
            size_t at = e * L.comps + c;
            return at < L.data.size() ? L.data[at] : 0.0;
        };
        // polygon vertices -> unique vertices (control point, normal, uvs)
        const uint32_t base = (uint32_t)out.mesh.vertices.size();
        std::unordered_map<std::string, uint32_t> unique;
        std::vector<int> cpOfVertex;
        std::vector<uint32_t> poly;
        const size_t cpCount = vp->ad.size() / 3;
        auto emit = [&](size_t pv, int cp) -> uint32_t {
            float3 n = normals.data.empty() ? float3(0) : float3((float)fetch(normals, pv, cp, 0), (float)fetch(normals, pv, cp, 1), (float)fetch(normals, pv, cp, 2));
            std::vector<float2> t;
            for (const Layer& L : uvs) t.push_back(float2((float)fetch(L, pv, cp, 0), (float)fetch(L, pv, cp, 1)));
            std::string key((const char*)&cp, sizeof(cp));
            key.append((const char*)&n, sizeof(n));
            for (auto& x : t) key.append((const char*)&x, sizeof(x));
            auto it = unique.find(key);
            if (it != unique.end()) return it->second;
            Vertex v;
            v.pos = bake.point(float3((float)vp->ad[cp * 3], (float)vp->ad[cp * 3 + 1], (float)vp->ad[cp * 3 + 2]));
            v.nrm = normalize(bake.vec(n));
            if (!t.empty()) v.uv = float2(t[0].x, 1.0f - t[0].y);   // FBX: v up -> engine v down
            uint32_t idx = (uint32_t)out.mesh.vertices.size();
            out.mesh.vertices.push_back(v);
            cpOfVertex.push_back(cp);
            for (size_t s = 1; s < t.size(); ++s) {
                if (extraUv.size() < s) { extraUv.emplace_back(); }
                extraUv[s - 1].resize(idx, float2(0, 0));
                extraUv[s - 1].push_back(t[s]);
            }
            unique.emplace(std::move(key), idx);
            return idx;
        };
        for (size_t k = 0; k < uvs.size(); ++k)
            if (uvNames.size() <= k) uvNames.push_back(uvs[k].name.empty() ? "UV" + std::to_string(k) : uvs[k].name);
        for (size_t pv = 0; pv < ip->ai.size(); ++pv) {
            int64_t raw = ip->ai[pv];
            bool last = raw < 0;
            int cp = (int)(last ? ~raw : raw);
            if (cp < 0 || (size_t)cp >= cpCount) return fail("bad polygon index");
            poly.push_back(emit(pv, cp));
            if (last) {
                for (size_t k = 1; k + 1 < poly.size(); ++k) out.mesh.addTri(poly[0], poly[k], poly[k + 1]);
                poly.clear();
            }
        }
        out.meshes++;
        // missing normals: smooth, from the faces
        if (normals.data.empty()) {
            std::vector<float3> acc(out.mesh.vertices.size() - base, float3(0));
            for (size_t t = 0; t + 2 < out.mesh.indices.size(); t += 3) {
                uint32_t a = out.mesh.indices[t], b = out.mesh.indices[t + 1], c = out.mesh.indices[t + 2];
                if (a < base) continue;
                float3 fn = cross(out.mesh.vertices[b].pos - out.mesh.vertices[a].pos, out.mesh.vertices[c].pos - out.mesh.vertices[a].pos);
                acc[a - base] += fn; acc[b - base] += fn; acc[c - base] += fn;
            }
            for (size_t i = 0; i < acc.size(); ++i) out.mesh.vertices[base + i].nrm = normalize(acc[i]);
        }
        // weights: control point -> every vertex made from it
        std::vector<std::vector<uint32_t>> verticesOfCp(cpCount);
        for (size_t i = 0; i < cpOfVertex.size(); ++i) verticesOfCp[cpOfVertex[i]].push_back(base + (uint32_t)i);
        for (const ClusterData& c : clusters) {
            if (!jointIndex.count(c.joint) || !c.idx || !c.w) continue;
            int j = jointIndex[c.joint];
            auto ci = clusterOfJoint.find(j);
            if (ci == clusterOfJoint.end()) {
                FbxCluster fc;
                fc.joint = j;
                fc.bindJoint = c.link.affine();
                fc.bindMesh = c.transform.affine();
                ci = clusterOfJoint.emplace(j, out.clusters.size()).first;
                out.clusters.push_back(fc);
            }
            FbxCluster& fc = out.clusters[ci->second];
            for (size_t e = 0; e < c.idx->ai.size() && e < c.w->ad.size(); ++e) {
                int64_t cp = c.idx->ai[e];
                if (cp < 0 || (size_t)cp >= cpCount) continue;
                for (uint32_t v : verticesOfCp[(size_t)cp]) { fc.vertices.push_back(v); fc.weights.push_back(c.w->ad[e]); }
            }
        }
    }
    if (out.mesh.indices.empty()) return fail("no mesh in the file");
    // Z-up files (Blender, 3ds Max): turned to Y-up (-90 degrees around X) - mesh, root joints, bind matrices
    int upAxis = 1, upSign = 1;
    if (const Node* gs = root.child("GlobalSettings"))
        if (const Node* p70 = gs->child("Properties70"))
            for (const Node& p : p70->kids)
                if (p.props.size() > 4 && p.props[0].s == "UpAxis") upAxis = (int)p.props[4].i;
                else if (p.props.size() > 4 && p.props[0].s == "UpAxisSign") upSign = (int)p.props[4].i;
    if (upAxis == 2) {
        const float sg = upSign < 0 ? -1.0f : 1.0f;
        auto conv = [&](const float3& v) { return float3(v.x, v.z * sg, -v.y * sg); };
        const float4 qc = qAxisAngle(float3(1, 0, 0), sg * -kPi * 0.5f);
        for (Vertex& v : out.mesh.vertices) { v.pos = conv(v.pos); v.nrm = conv(v.nrm); }
        for (FbxJoint& j : out.joints)
            if (j.parent < 0) { j.translation = conv(j.translation); j.rotation = qNormalize(qMul(qc, j.rotation)); }
        Affine C = Affine::trsQuat(float3(0), qc, float3(1));
        auto mulA = [](const Affine& a, const Affine& b) {
            Affine r;
            for (int i = 0; i < 3; ++i) {
                for (int j = 0; j < 3; ++j) r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
                r.m[i][3] = a.m[i][0] * b.m[0][3] + a.m[i][1] * b.m[1][3] + a.m[i][2] * b.m[2][3] + a.m[i][3];
            }
            return r;
        };
        for (FbxCluster& c : out.clusters) { c.bindJoint = mulA(C, c.bindJoint); c.bindMesh = mulA(C, c.bindMesh); }
    }
    // UV sets (v up)
    {
        UvSet s0;
        s0.name = uvNames.empty() ? "UV0" : uvNames[0];
        for (const Vertex& v : out.mesh.vertices) s0.uv.push_back(float2(v.uv.x, 1.0f - v.uv.y));
        out.uvSets.push_back(std::move(s0));
        for (size_t s = 0; s < extraUv.size(); ++s) {
            UvSet e;
            e.name = s + 1 < uvNames.size() ? uvNames[s + 1] : "UV" + std::to_string(s + 1);
            e.uv = std::move(extraUv[s]);
            e.uv.resize(out.mesh.vertices.size(), float2(0, 0));
            out.uvSets.push_back(std::move(e));
        }
    }
    return true;
}

} // namespace app
