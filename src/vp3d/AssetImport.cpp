#include "AssetImport.h"
#include "FilePath.h"
#ifdef _WIN32
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#endif
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace vp3d {

namespace {

std::string narrowPath(const std::wstring& w) {
    std::string s;
    for (wchar_t c : w) s.push_back(c < 128 ? (char)c : '?');
    return s;
}

bool readAll(const std::wstring& path, std::vector<char>& data) {
    std::ifstream f(nativePath(path), std::ios::binary | std::ios::ate);
    if (!f) return false;
    data.resize((size_t)f.tellg());
    f.seekg(0);
    f.read(data.data(), data.size());
    data.push_back('\0');
    return true;
}

const char* skipSpaces(const char* p) {
    while (*p == ' ' || *p == '\t') ++p;
    return p;
}

const char* nextLine(const char* p) {
    while (*p && *p != '\n') ++p;
    return *p ? p + 1 : p;
}

// OBJ indices are 1-based, negative = relative to the end of the list.
int fixIndex(long v, size_t count) { return v > 0 ? (int)v - 1 : (v < 0 ? (int)count + (int)v : -1); }

struct Corner { int v, t, n; };

} // namespace

bool loadObj(const std::wstring& path, MeshData& out, ObjInfo* info, std::string* error, float smoothAngleDeg) {
    std::vector<char> data;
    if (!readAll(path, data)) {
        if (error) *error = "Cannot read " + narrowPath(path);
        return false;
    }
    std::vector<float3> pos, nrm;
    std::vector<float2> uv;
    std::vector<Corner> corners;   // 3 per triangle
    std::string mtlFile;

    const char* p = data.data();
    std::vector<Corner> poly;
    while (*p) {
        p = skipSpaces(p);
        char* e = nullptr;
        if (p[0] == 'v' && (p[1] == ' ' || p[1] == '\t')) {
            float3 v;
            v.x = strtof(p + 2, &e);
            v.y = strtof(e, &e);
            v.z = strtof(e, &e);
            pos.push_back(v);
        } else if (p[0] == 'v' && p[1] == 't') {
            float2 t;
            t.x = strtof(p + 2, &e);
            t.y = strtof(e, &e);
            uv.push_back(t);
        } else if (p[0] == 'v' && p[1] == 'n') {
            float3 n;
            n.x = strtof(p + 2, &e);
            n.y = strtof(e, &e);
            n.z = strtof(e, &e);
            nrm.push_back(n);
        } else if (p[0] == 'f' && (p[1] == ' ' || p[1] == '\t')) {
            poly.clear();
            const char* q = p + 2;
            for (;;) {
                q = skipSpaces(q);
                if (*q == '\0' || *q == '\n' || *q == '\r' || *q == '#') break;
                Corner c = {-1, -1, -1};
                c.v = fixIndex(strtol(q, &e, 10), pos.size());
                if (e == q) break;
                q = e;
                if (*q == '/') {
                    ++q;
                    if (*q != '/') { c.t = fixIndex(strtol(q, &e, 10), uv.size()); q = e; }
                    if (*q == '/') { ++q; c.n = fixIndex(strtol(q, &e, 10), nrm.size()); q = e; }
                }
                while (*q && *q != ' ' && *q != '\t' && *q != '\n' && *q != '\r') ++q;
                poly.push_back(c);
            }
            for (size_t i = 2; i < poly.size(); ++i) {
                corners.push_back(poly[0]);
                corners.push_back(poly[i - 1]);
                corners.push_back(poly[i]);
            }
        } else if (strncmp(p, "mtllib", 6) == 0) {
            const char* q = skipSpaces(p + 6);
            const char* end = q;
            while (*end && *end != '\n' && *end != '\r') ++end;
            mtlFile.assign(q, end);
        }
        p = nextLine(p);
    }
    for (const Corner& c : corners)
        if (c.v < 0 || c.v >= (int)pos.size()) {
            if (error) *error = "Invalid face index in " + narrowPath(path);
            return false;
        }
    size_t triCount = corners.size() / 3;
    bool hasNormals = !nrm.empty();
    for (const Corner& c : corners) if (c.n < 0 || c.n >= (int)nrm.size()) { hasNormals = false; break; }

    // Face normals (area weighted) and, without file normals, smoothing between faces closer than the angle.
    std::vector<float3> faceN(triCount);
    for (size_t f = 0; f < triCount; ++f) {
        const float3& a = pos[corners[f * 3].v];
        const float3& b = pos[corners[f * 3 + 1].v];
        const float3& c = pos[corners[f * 3 + 2].v];
        faceN[f] = cross(b - a, c - a);
    }
    std::vector<uint32_t> start, faces;   // faces around each position (CSR)
    if (!hasNormals) {
        start.assign(pos.size() + 1, 0);
        for (const Corner& c : corners) start[c.v + 1]++;
        for (size_t i = 0; i < pos.size(); ++i) start[i + 1] += start[i];
        faces.resize(corners.size());
        std::vector<uint32_t> fill(start.begin(), start.end() - 1);
        for (size_t i = 0; i < corners.size(); ++i) faces[fill[corners[i].v]++] = (uint32_t)(i / 3);
    }
    float cosLimit = std::cos(radians(smoothAngleDeg));

    // Unique vertices per (position, uv, normal).
    out.vertices.clear();
    out.indices.clear();
    out.vertices.reserve(corners.size() / 2);
    out.indices.reserve(corners.size());
    std::vector<int> head(pos.size(), -1);   // first emitted vertex of each position, chained by `link`
    std::vector<int> link;
    for (size_t i = 0; i < corners.size(); ++i) {
        const Corner& c = corners[i];
        size_t f = i / 3;
        float3 n;
        if (hasNormals) n = normalize(nrm[c.n]);
        else {
            float3 fn = normalize(faceN[f]);
            float3 sum(0.0f);
            for (uint32_t k = start[c.v]; k < start[c.v + 1]; ++k) {
                const float3& o = faceN[faces[k]];
                if (dot(normalize(o), fn) >= cosLimit) sum += o;
            }
            n = dot(sum, sum) > 1e-20f ? normalize(sum) : fn;
        }
        float2 t = (c.t >= 0 && c.t < (int)uv.size()) ? uv[c.t] : float2(0, 0);
        t.y = 1.0f - t.y;   // OBJ: v up, textures: row 0 on top
        int found = -1;
        for (int k = head[c.v]; k >= 0; k = link[k]) {
            const Vertex& v = out.vertices[k];
            if (v.uv.x == t.x && v.uv.y == t.y && dot(v.nrm, n) > 0.9995f) { found = k; break; }
        }
        if (found < 0) {
            found = (int)out.vertices.size();
            Vertex v;
            v.pos = pos[c.v];
            v.nrm = n;
            v.uv = t;
            out.vertices.push_back(v);
            link.push_back(head[c.v]);
            head[c.v] = found;
        }
        out.indices.push_back((uint32_t)found);
    }

    if (info) {
        info->positions = pos.size();
        info->uvs = uv.size();
        info->normals = nrm.size();
        info->faces = triCount;
        info->hadNormals = hasNormals;
        info->diffuseMap.clear();
        if (!mtlFile.empty()) {
            std::wstring dir = path.substr(0, path.find_last_of(L"\\/") + 1);
            std::vector<char> mtl;
            if (readAll(dir + std::wstring(mtlFile.begin(), mtlFile.end()), mtl)) {
                const char* m = mtl.data();
                while (*m) {
                    m = skipSpaces(m);
                    if (strncmp(m, "map_Kd", 6) == 0) {
                        const char* q = skipSpaces(m + 6);
                        const char* end = q;
                        while (*end && *end != '\n' && *end != '\r') ++end;
                        info->diffuseMap.assign(q, end);
                        break;
                    }
                    m = nextLine(m);
                }
            }
        }
    }
    return !out.indices.empty();
}

bool loadImage(const std::wstring& path, Texture& out, std::string* error, uint32_t maxSize) {
    auto fail = [&](const char* what) {
        if (error) *error = std::string(what) + ": " + narrowPath(path);
        return false;
    };
#ifndef _WIN32
    (void)out;
    (void)maxSize;
    return fail("Image decoding (WIC) is only available on Windows for now");
#else
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> f;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) return fail("WIC unavailable");
    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(f->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)))
        return fail("Cannot open image");
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(dec->GetFrame(0, &frame))) return fail("Cannot decode image");
    UINT w = 0, h = 0;
    frame->GetSize(&w, &h);
    ComPtr<IWICBitmapSource> src = frame;
    if (maxSize > 0 && (w > maxSize || h > maxSize)) {
        float k = (float)maxSize / (float)std::max(w, h);
        UINT nw = std::max(1u, (UINT)(w * k)), nh = std::max(1u, (UINT)(h * k));
        ComPtr<IWICBitmapScaler> sc;
        f->CreateBitmapScaler(&sc);
        if (sc && SUCCEEDED(sc->Initialize(src.Get(), nw, nh, WICBitmapInterpolationModeFant))) {
            src = sc;
            w = nw;
            h = nh;
        }
    }
    ComPtr<IWICFormatConverter> conv;
    f->CreateFormatConverter(&conv);
    if (FAILED(conv->Initialize(src.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)))
        return fail("Unsupported pixel format");
    out.width = w;
    out.height = h;
    out.rgba.resize((size_t)w * h * 4);
    if (FAILED(conv->CopyPixels(nullptr, w * 4, (UINT)out.rgba.size(), out.rgba.data()))) return fail("Cannot read pixels");
    size_t slash = path.find_last_of(L"\\/");
    out.name = narrowPath(slash == std::wstring::npos ? path : path.substr(slash + 1));
    return true;
#endif
}

} // namespace vp3d
