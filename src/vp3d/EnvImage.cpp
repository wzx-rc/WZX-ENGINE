#include "EnvImage.h"
#include "AssetImport.h"
#include <cstdio>
#include <cstring>
#include <fstream>

namespace vp3d {

namespace {

bool fail(std::string* e, const std::string& m) {
    if (e) *e = m;
    return false;
}

bool readAll(const std::wstring& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    out.resize((size_t)f.tellg());
    f.seekg(0);
    f.read((char*)out.data(), out.size());
    return (bool)f;
}

std::wstring lowerExt(const std::wstring& path) {
    size_t d = path.find_last_of(L'.');
    if (d == std::wstring::npos) return L"";
    std::wstring e = path.substr(d);
    for (auto& c : e) c = (wchar_t)towlower(c);
    return e;
}

std::string fileName(const std::wstring& path) {
    size_t s = path.find_last_of(L"\\/");
    std::wstring n = s == std::wstring::npos ? path : path.substr(s + 1);
    std::string o;
    for (wchar_t c : n) {   // UTF-8
        if (c < 0x80) o.push_back((char)c);
        else if (c < 0x800) { o.push_back((char)(0xC0 | (c >> 6))); o.push_back((char)(0x80 | (c & 0x3F))); }
        else { o.push_back((char)(0xE0 | (c >> 12))); o.push_back((char)(0x80 | ((c >> 6) & 0x3F))); o.push_back((char)(0x80 | (c & 0x3F))); }
    }
    return o;
}

float halfToFloat(uint16_t h) {
    uint32_t s = (h >> 15) & 1, e = (h >> 10) & 0x1F, m = h & 0x3FF;
    float v;
    if (e == 0) v = std::ldexp((float)m, -24);
    else if (e == 31) v = m ? 0.0f : 65504.0f;   // NaN -> 0, inf -> max half
    else v = std::ldexp((float)(m | 0x400), (int)e - 25);
    return s ? -v : v;
}

float srgbDecode(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }

// ------------------------------------------------------------------ Radiance RGBE (.hdr)

bool loadRgbe(const std::vector<uint8_t>& d, EnvImage& img, std::string* err) {
    size_t p = 0;
    auto line = [&]() {
        std::string s;
        while (p < d.size() && d[p] != '\n') s.push_back((char)d[p++]);
        if (p < d.size()) ++p;
        return s;
    };
    std::string first = line();
    if (first.rfind("#?", 0) != 0) return fail(err, "not a Radiance .hdr file");
    for (;;) {
        if (p >= d.size()) return fail(err, "truncated .hdr header");
        std::string s = line();
        if (s.empty()) break;
        if (s.rfind("FORMAT=", 0) == 0 && s.find("32-bit_rle_rgbe") == std::string::npos)
            return fail(err, "unsupported .hdr format (" + s + ")");
    }
    std::string res = line();
    char ya[3] = {}, xa[3] = {};
    int h = 0, w = 0;
    if (sscanf_s(res.c_str(), "%2s %d %2s %d", ya, 3, &h, xa, 3, &w) != 4 || w <= 0 || h <= 0)
        return fail(err, "unsupported .hdr orientation (" + res + ")");
    bool flipY = ya[0] == '+', flipX = xa[0] == '-';
    img.width = (uint32_t)w;
    img.height = (uint32_t)h;
    img.rgb.assign((size_t)w * h * 3, 0.0f);
    std::vector<uint8_t> scan((size_t)w * 4);
    for (int y = 0; y < h; ++y) {
        if (p + 4 <= d.size() && w >= 8 && w < 32768 && d[p] == 2 && d[p + 1] == 2 && ((d[p + 2] << 8) | d[p + 3]) == w) {
            p += 4;   // new RLE: the four components one after the other
            for (int c = 0; c < 4; ++c) {
                int x = 0;
                while (x < w) {
                    if (p >= d.size()) return fail(err, "truncated .hdr data");
                    int n = d[p++];
                    if (n > 128) {
                        n -= 128;
                        if (x + n > w || p >= d.size()) return fail(err, "bad .hdr run");
                        uint8_t v = d[p++];
                        for (int k = 0; k < n; ++k) scan[(size_t)(x++) * 4 + c] = v;
                    } else {
                        if (n == 0 || x + n > w || p + n > d.size()) return fail(err, "bad .hdr run");
                        for (int k = 0; k < n; ++k) scan[(size_t)(x++) * 4 + c] = d[p++];
                    }
                }
            }
        } else {   // flat
            if (p + (size_t)w * 4 > d.size()) return fail(err, "truncated .hdr data");
            memcpy(scan.data(), &d[p], (size_t)w * 4);
            p += (size_t)w * 4;
        }
        int ty = flipY ? h - 1 - y : y;
        for (int x = 0; x < w; ++x) {
            const uint8_t* s = &scan[(size_t)x * 4];
            float f = s[3] ? std::ldexp(1.0f, (int)s[3] - 136) : 0.0f;
            int tx = flipX ? w - 1 - x : x;
            float* o = &img.rgb[((size_t)ty * w + tx) * 3];
            o[0] = s[0] * f;
            o[1] = s[1] * f;
            o[2] = s[2] * f;
        }
    }
    return true;
}

// ------------------------------------------------------------------ inflate (zlib streams of the EXR ZIP compression)

struct Bits {
    const uint8_t* d;
    size_t n, pos = 0;
    uint32_t buf = 0;
    int cnt = 0;
    bool bad = false;
    int bit() {
        if (cnt == 0) {
            if (pos >= n) { bad = true; return 0; }
            buf = d[pos++];
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

struct Huff {
    uint16_t count[16];
    uint16_t symbol[320];
};

void buildHuff(Huff& h, const uint8_t* len, int n) {
    memset(h.count, 0, sizeof(h.count));
    for (int i = 0; i < n; ++i) h.count[len[i]]++;
    h.count[0] = 0;
    uint16_t offs[16] = {};
    for (int l = 1; l < 15; ++l) offs[l + 1] = offs[l] + h.count[l];
    for (int i = 0; i < n; ++i)
        if (len[i]) h.symbol[offs[len[i]]++] = (uint16_t)i;
}

int decodeSym(Bits& s, const Huff& h) {
    int code = 0, first = 0, index = 0;
    for (int l = 1; l < 16; ++l) {
        code |= s.bit();
        int count = h.count[l];
        if (code - count < first) return h.symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

bool inflateZlib(const uint8_t* src, size_t n, std::vector<uint8_t>& out, size_t expected) {
    static const uint16_t lbase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const uint8_t lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const uint16_t dbase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const uint8_t dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    if (n < 2) return false;
    out.clear();
    out.reserve(expected);
    Bits s{src + 2, n - 2};   // zlib header (CMF, FLG); the Adler-32 checksum at the end is not checked
    Huff lit, dist;
    int last;
    do {
        last = s.bit();
        int type = (int)s.bits(2);
        if (type == 0) {   // stored
            s.cnt = 0;
            if (s.pos + 4 > s.n) return false;
            uint32_t len = s.d[s.pos] | (s.d[s.pos + 1] << 8);
            s.pos += 4;
            if (s.pos + len > s.n) return false;
            out.insert(out.end(), s.d + s.pos, s.d + s.pos + len);
            s.pos += len;
            continue;
        }
        uint8_t lens[320] = {};
        if (type == 1) {   // fixed codes
            int i = 0;
            for (; i < 144; ++i) lens[i] = 8;
            for (; i < 256; ++i) lens[i] = 9;
            for (; i < 280; ++i) lens[i] = 7;
            for (; i < 288; ++i) lens[i] = 8;
            buildHuff(lit, lens, 288);
            for (i = 0; i < 30; ++i) lens[i] = 5;
            buildHuff(dist, lens, 30);
        } else if (type == 2) {   // dynamic codes
            int nlen = (int)s.bits(5) + 257, ndist = (int)s.bits(5) + 1, ncode = (int)s.bits(4) + 4;
            if (nlen > 286 || ndist > 30) return false;
            uint8_t cl[19] = {};
            for (int i = 0; i < ncode; ++i) cl[order[i]] = (uint8_t)s.bits(3);
            Huff lencode;
            buildHuff(lencode, cl, 19);
            int i = 0;
            while (i < nlen + ndist) {
                int sym = decodeSym(s, lencode);
                if (sym < 0 || s.bad) return false;
                if (sym < 16) { lens[i++] = (uint8_t)sym; continue; }
                int rep = 0;
                uint8_t v = 0;
                if (sym == 16) { if (i == 0) return false; v = lens[i - 1]; rep = 3 + (int)s.bits(2); }
                else if (sym == 17) rep = 3 + (int)s.bits(3);
                else rep = 11 + (int)s.bits(7);
                if (i + rep > nlen + ndist) return false;
                while (rep--) lens[i++] = v;
            }
            buildHuff(lit, lens, nlen);
            buildHuff(dist, lens + nlen, ndist);
        } else {
            return false;
        }
        for (;;) {
            int sym = decodeSym(s, lit);
            if (sym < 0 || s.bad) return false;
            if (sym < 256) { out.push_back((uint8_t)sym); continue; }
            if (sym == 256) break;
            sym -= 257;
            if (sym >= 29) return false;
            size_t len = lbase[sym] + s.bits(lext[sym]);
            int ds = decodeSym(s, dist);
            if (ds < 0 || ds >= 30) return false;
            size_t back = dbase[ds] + s.bits(dext[ds]);
            if (back > out.size()) return false;
            size_t from = out.size() - back;
            for (size_t k = 0; k < len; ++k) out.push_back(out[from + k]);
        }
    } while (!last && !s.bad);
    return !s.bad;
}

// EXR ZIP / RLE post-processing: undo the byte predictor, then re-interleave the two halves.
void exrUnpredict(std::vector<uint8_t>& t, std::vector<uint8_t>& out) {
    for (size_t i = 1; i < t.size(); ++i) t[i] = (uint8_t)(t[i - 1] + t[i] - 128);
    out.resize(t.size());
    size_t half = (t.size() + 1) / 2;
    for (size_t i = 0, a = 0, b = half; i < t.size(); ++i) out[i] = (i & 1) ? t[b++] : t[a++];
}

bool exrRle(const uint8_t* s, size_t n, std::vector<uint8_t>& out, size_t expected) {
    out.clear();
    size_t p = 0;
    while (p < n) {
        int c = (int8_t)s[p++];
        if (c < 0) {
            size_t k = (size_t)(-c);
            if (p + k > n) return false;
            out.insert(out.end(), s + p, s + p + k);
            p += k;
        } else {
            if (p >= n) return false;
            out.insert(out.end(), (size_t)c + 1, s[p++]);
        }
        if (out.size() > expected) return false;
    }
    return out.size() == expected;
}

// ------------------------------------------------------------------ OpenEXR (scanline, single part)

bool loadExr(const std::vector<uint8_t>& d, EnvImage& img, std::string* err) {
    auto rd32 = [&](size_t p) { uint32_t v; memcpy(&v, &d[p], 4); return v; };
    if (d.size() < 8 || rd32(0) != 20000630u) return fail(err, "not an OpenEXR file");
    uint32_t ver = rd32(4);
    if (ver & 0x200) return fail(err, "tiled EXR is not supported (save it as scanline or .hdr)");
    if (ver & 0x1800) return fail(err, "multi-part / deep EXR is not supported");
    struct Chan { std::string name; int type; int xs, ys; };
    std::vector<Chan> chans;
    int compression = 0;
    int xmin = 0, ymin = 0, xmax = -1, ymax = -1;
    size_t p = 8;
    auto cstr = [&](std::string& s) {
        s.clear();
        while (p < d.size() && d[p]) s.push_back((char)d[p++]);
        if (p >= d.size()) return false;
        ++p;
        return true;
    };
    for (;;) {
        std::string name, type;
        if (!cstr(name)) return fail(err, "truncated EXR header");
        if (name.empty()) break;
        if (!cstr(type) || p + 4 > d.size()) return fail(err, "truncated EXR header");
        uint32_t size = rd32(p);
        p += 4;
        if (p + size > d.size()) return fail(err, "truncated EXR header");
        size_t end = p + size;
        if (name == "channels" && type == "chlist") {
            while (p < end && d[p]) {
                Chan c;
                cstr(c.name);
                if (p + 16 > end) return fail(err, "bad EXR channel list");
                c.type = (int)rd32(p);
                c.xs = (int)rd32(p + 8);
                c.ys = (int)rd32(p + 12);
                p += 16;
                chans.push_back(c);
            }
        } else if (name == "compression") {
            compression = d[p];
        } else if (name == "dataWindow" && size >= 16) {
            xmin = (int)rd32(p); ymin = (int)rd32(p + 4); xmax = (int)rd32(p + 8); ymax = (int)rd32(p + 12);
        }
        p = end;
    }
    int w = xmax - xmin + 1, h = ymax - ymin + 1;
    if (w <= 0 || h <= 0 || chans.empty()) return fail(err, "empty EXR image");
    int lines = 0;
    switch (compression) {
    case 0: case 1: case 2: lines = 1; break;   // NONE, RLE, ZIPS
    case 3: lines = 16; break;                  // ZIP
    case 4: return fail(err, "EXR PIZ compression is not supported: re-save as ZIP or .hdr");
    default: return fail(err, "EXR compression " + std::to_string(compression) + " is not supported: re-save as ZIP or .hdr");
    }
    // Channel layout of one scanline (channels are stored in the chlist order, which is sorted).
    std::vector<size_t> chOffset(chans.size());
    size_t lineBytes = 0;
    for (size_t i = 0; i < chans.size(); ++i) {
        if (chans[i].xs != 1 || chans[i].ys != 1) return fail(err, "sub-sampled EXR channels are not supported");
        chOffset[i] = lineBytes;
        lineBytes += (size_t)w * (chans[i].type == 1 ? 2 : 4);
    }
    // R, G, B: plain names first, then any layer ("xxx.R"); a single luminance channel is used as grey.
    auto find = [&](const char* n) -> int {
        for (size_t i = 0; i < chans.size(); ++i) if (chans[i].name == n) return (int)i;
        std::string suf = std::string(".") + n;
        for (size_t i = 0; i < chans.size(); ++i) {
            const std::string& cn = chans[i].name;
            if (cn.size() > suf.size() && cn.compare(cn.size() - suf.size(), suf.size(), suf) == 0) return (int)i;
        }
        return -1;
    };
    int cr = find("R"), cg = find("G"), cb = find("B");
    if (cr < 0 || cg < 0 || cb < 0) {
        int y = find("Y");
        if (y < 0) return fail(err, "EXR without R, G, B or Y channels");
        cr = cg = cb = y;
    }
    img.width = (uint32_t)w;
    img.height = (uint32_t)h;
    img.rgb.assign((size_t)w * h * 3, 0.0f);
    size_t chunks = ((size_t)h + lines - 1) / lines;
    if (p + chunks * 8 > d.size()) return fail(err, "truncated EXR offset table");
    std::vector<uint8_t> raw, tmp;
    const int cidx[3] = {cr, cg, cb};
    for (size_t c = 0; c < chunks; ++c) {
        uint64_t off;
        memcpy(&off, &d[p + c * 8], 8);
        if (off + 8 > d.size()) return fail(err, "bad EXR chunk offset");
        int y0 = (int)rd32((size_t)off) - ymin;
        uint32_t size = rd32((size_t)off + 4);
        const uint8_t* src = &d[(size_t)off + 8];
        if (off + 8 + size > d.size()) return fail(err, "truncated EXR chunk");
        int nl = std::min(lines, h - y0);
        if (y0 < 0 || nl <= 0) continue;
        size_t expected = lineBytes * nl;
        const uint8_t* px = src;
        if (size < expected) {   // compressed (a chunk that did not shrink is stored as is)
            if (compression == 1) {
                if (!exrRle(src, size, tmp, expected)) return fail(err, "bad EXR RLE data");
            } else {
                if (!inflateZlib(src, size, tmp, expected) || tmp.size() != expected) return fail(err, "bad EXR ZIP data");
            }
            exrUnpredict(tmp, raw);
            px = raw.data();
        } else if (size != expected) {
            return fail(err, "bad EXR chunk size");
        }
        for (int l = 0; l < nl; ++l) {
            const uint8_t* line = px + lineBytes * l;
            float* o = &img.rgb[(size_t)(y0 + l) * w * 3];
            for (int k = 0; k < 3; ++k) {
                const Chan& ch = chans[cidx[k]];
                const uint8_t* cp = line + chOffset[cidx[k]];
                for (int x = 0; x < w; ++x) {
                    float v;
                    if (ch.type == 1) { uint16_t hv; memcpy(&hv, cp + x * 2, 2); v = halfToFloat(hv); }
                    else if (ch.type == 2) memcpy(&v, cp + x * 4, 4);
                    else { uint32_t u; memcpy(&u, cp + x * 4, 4); v = (float)u; }
                    o[x * 3 + k] = std::isfinite(v) ? std::max(v, 0.0f) : 0.0f;
                }
            }
        }
    }
    return true;
}

void downsize(EnvImage& img, uint32_t maxWidth) {
    if (maxWidth == 0 || img.width <= maxWidth) return;
    uint32_t k = (img.width + maxWidth - 1) / maxWidth;
    uint32_t w = std::max(1u, img.width / k), h = std::max(1u, img.height / k);
    std::vector<float> o((size_t)w * h * 3, 0.0f);
    float inv = 1.0f / (k * k);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            float s[3] = {};
            for (uint32_t j = 0; j < k; ++j)
                for (uint32_t i = 0; i < k; ++i) {
                    const float* p = &img.rgb[(((size_t)(y * k + j)) * img.width + x * k + i) * 3];
                    s[0] += p[0]; s[1] += p[1]; s[2] += p[2];
                }
            float* d = &o[((size_t)y * w + x) * 3];
            d[0] = s[0] * inv; d[1] = s[1] * inv; d[2] = s[2] * inv;
        }
    img.rgb.swap(o);
    img.width = w;
    img.height = h;
}

} // namespace

bool isEnvImageFile(const std::wstring& path) {
    std::wstring e = lowerExt(path);
    return e == L".hdr" || e == L".pic" || e == L".exr" || e == L".jpg" || e == L".jpeg" || e == L".png" ||
           e == L".bmp" || e == L".tif" || e == L".tiff";
}

bool loadEnvImage(const std::wstring& path, EnvImage& out, std::string* error, uint32_t maxWidth) {
    EnvImage img;
    std::wstring ext = lowerExt(path);
    if (ext == L".hdr" || ext == L".pic" || ext == L".exr") {
        std::vector<uint8_t> data;
        if (!readAll(path, data)) return fail(error, "cannot read " + fileName(path));
        bool ok = ext == L".exr" ? loadExr(data, img, error) : loadRgbe(data, img, error);
        if (!ok) return false;
    } else {
        Texture t;   // LDR image: sRGB to linear, no real dynamic range (the sun is clipped)
        if (!loadImage(path, t, error, maxWidth)) return false;
        img.width = t.width;
        img.height = t.height;
        img.rgb.resize((size_t)t.width * t.height * 3);
        float lut[256];
        for (int i = 0; i < 256; ++i) lut[i] = srgbDecode(i / 255.0f);
        for (size_t i = 0; i < (size_t)t.width * t.height; ++i)
            for (int c = 0; c < 3; ++c) img.rgb[i * 3 + c] = lut[t.rgba[i * 4 + c]];
    }
    img.sourceWidth = img.width;
    img.sourceHeight = img.height;
    downsize(img, maxWidth);
    img.name = fileName(path);
    img.path.clear();
    for (wchar_t c : path) {
        if (c < 0x80) img.path.push_back((char)c);
        else if (c < 0x800) { img.path.push_back((char)(0xC0 | (c >> 6))); img.path.push_back((char)(0x80 | (c & 0x3F))); }
        else { img.path.push_back((char)(0xE0 | (c >> 12))); img.path.push_back((char)(0x80 | ((c >> 6) & 0x3F))); img.path.push_back((char)(0x80 | (c & 0x3F))); }
    }
    out = std::move(img);
    return true;
}

bool saveHdr(const std::wstring& path, const EnvImage& img) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    char head[128];
    int n = snprintf(head, sizeof(head), "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y %u +X %u\n", img.height, img.width);
    f.write(head, n);
    std::vector<uint8_t> row((size_t)img.width * 4);
    for (uint32_t y = 0; y < img.height; ++y) {
        for (uint32_t x = 0; x < img.width; ++x) {
            float3 c = img.texel(x, y);
            float m = std::max(c.x, std::max(c.y, c.z));
            uint8_t* o = &row[(size_t)x * 4];
            if (m < 1e-32f) { o[0] = o[1] = o[2] = o[3] = 0; continue; }
            int e;
            float s = std::frexp(m, &e) * 256.0f / m;
            o[0] = (uint8_t)std::min(255.0f, c.x * s);
            o[1] = (uint8_t)std::min(255.0f, c.y * s);
            o[2] = (uint8_t)std::min(255.0f, c.z * s);
            o[3] = (uint8_t)(e + 128);
        }
        f.write((const char*)row.data(), row.size());   // flat scanlines (valid RGBE, no RLE)
    }
    return (bool)f;
}

void makeProceduralSky(EnvImage& out, uint32_t width) {
    out = EnvImage();
    out.name = "Procedural Sky";
    out.width = std::max(16u, width);
    out.height = out.width / 2;
    out.sourceWidth = out.width;
    out.sourceHeight = out.height;
    out.rgb.resize((size_t)out.width * out.height * 3);
    // sun: 40 degrees high, front right of the default view; a disc of about 2 degrees
    float se = radians(40.0f), sa = radians(35.0f);
    float3 sun(std::cos(se) * std::sin(sa), std::sin(se), -std::cos(se) * std::cos(sa));
    const float cosSun = std::cos(radians(1.0f));
    const float3 sunL = float3(1.0f, 0.93f, 0.82f) * 3200.0f;   // ~3 of irradiance, like the studio sun
    const float3 zenith(0.18f, 0.32f, 0.72f), horizon(0.85f, 0.82f, 0.78f), ground(0.10f, 0.09f, 0.08f);
    for (uint32_t y = 0; y < out.height; ++y) {
        float th = (y + 0.5f) / out.height * kPi;
        for (uint32_t x = 0; x < out.width; ++x) {
            float ph = ((x + 0.5f) / out.width - 0.5f) * 2.0f * kPi;
            float3 d(std::sin(th) * std::sin(ph), std::cos(th), -std::sin(th) * std::cos(ph));
            float3 c;
            if (d.y >= 0) {
                c = lerp(horizon, zenith, std::pow(d.y, 0.45f)) * 0.9f;
                float cs = dot(d, sun);
                c += float3(1.0f, 0.8f, 0.55f) * (0.6f * std::pow(std::max(cs, 0.0f), 48.0f));   // glow around the sun
                if (cs > cosSun) c = sunL;
            } else {
                c = lerp(horizon * 0.35f, ground, std::min(1.0f, -d.y * 4.0f));
            }
            float* o = &out.rgb[((size_t)y * out.width + x) * 3];
            o[0] = c.x; o[1] = c.y; o[2] = c.z;
        }
    }
}

} // namespace vp3d
