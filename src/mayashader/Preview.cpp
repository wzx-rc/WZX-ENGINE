#include "Preview.h"
#include "app/Theme.h"
#include "imgui_internal.h"
#include "vp3d/EnvImage.h"
#include "vp3d/shaders/Shared.h"
#include <wincodec.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <map>
#include <sstream>

namespace mayashader {

using namespace vp3d;
using game::Graph;
using game::Link;
using game::Node;
using game::PinType;

namespace {

// ------------------------------------------------------------------------------------------------ graph evaluation

float frac(float x) { return x - std::floor(x); }
float lum(float3 c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }
float3 lerp3(float3 a, float3 b, float t) { return a + (b - a) * t; }
float sat(float x) { return std::min(std::max(x, 0.0f), 1.0f); }

uint32_t hash3(int x, int y, int z) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)z * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
float grad(int x, int y, int z, float fx, float fy, float fz) {
    switch (hash3(x, y, z) % 12) {
    case 0: return fx + fy; case 1: return -fx + fy; case 2: return fx - fy; case 3: return -fx - fy;
    case 4: return fx + fz; case 5: return -fx + fz; case 6: return fx - fz; case 7: return -fx - fz;
    case 8: return fy + fz; case 9: return -fy + fz; case 10: return fy - fz; default: return -fy - fz;
    }
}
// Perlin gradient noise, about -1..1
float perlin(float3 p) {
    int x = (int)std::floor(p.x), y = (int)std::floor(p.y), z = (int)std::floor(p.z);
    float fx = p.x - x, fy = p.y - y, fz = p.z - z;
    auto fade = [](float t) { return t * t * t * (t * (t * 6 - 15) + 10); };
    float u = fade(fx), v = fade(fy), w = fade(fz);
    auto L = [](float a, float b, float t) { return a + (b - a) * t; };
    float x00 = L(grad(x, y, z, fx, fy, fz), grad(x + 1, y, z, fx - 1, fy, fz), u);
    float x10 = L(grad(x, y + 1, z, fx, fy - 1, fz), grad(x + 1, y + 1, z, fx - 1, fy - 1, fz), u);
    float x01 = L(grad(x, y, z + 1, fx, fy, fz - 1), grad(x + 1, y, z + 1, fx - 1, fy, fz - 1), u);
    float x11 = L(grad(x, y + 1, z + 1, fx, fy - 1, fz - 1), grad(x + 1, y + 1, z + 1, fx - 1, fy - 1, fz - 1), u);
    return L(L(x00, x10, v), L(x01, x11, v), w);
}

struct Image { int w = 0, h = 0; std::vector<float3> px; };

const Image& loadImage(const std::string& utf8) {
    static std::map<std::string, Image> cache;
    auto it = cache.find(utf8);
    if (it != cache.end()) return it->second;
    Image& img = cache[utf8];
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &w[0], n);
    Microsoft::WRL::ComPtr<IWICImagingFactory> f;
    Microsoft::WRL::ComPtr<IWICBitmapDecoder> dec;
    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> fr;
    Microsoft::WRL::ComPtr<IWICFormatConverter> conv;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f))) &&
        SUCCEEDED(f->CreateDecoderFromFilename(w.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) &&
        SUCCEEDED(dec->GetFrame(0, &fr)) && SUCCEEDED(f->CreateFormatConverter(&conv)) &&
        SUCCEEDED(conv->Initialize(fr.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
        UINT iw = 0, ih = 0;
        conv->GetSize(&iw, &ih);
        std::vector<uint8_t> raw((size_t)iw * ih * 4);
        conv->CopyPixels(nullptr, iw * 4, (UINT)raw.size(), raw.data());
        img.w = (int)iw;
        img.h = (int)ih;
        img.px.resize((size_t)iw * ih);
        auto lin = [](uint8_t c) { float x = c / 255.0f; return x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f); };
        for (size_t i = 0; i < img.px.size(); ++i) img.px[i] = float3(lin(raw[i * 4]), lin(raw[i * 4 + 1]), lin(raw[i * 4 + 2]));
    }
    return img;
}

struct Shade {
    float3 base{0.8f, 0.8f, 0.8f};   // base weight x base colour (linear)
    float rough = 0.2f, metal = 0, spec = 1, ior = 1.5f, trans = 0, sss = 0, coat = 0, coatRough = 0.1f, emis = 0;
    float3 emisColor{1, 1, 1}, sssColor{1, 1, 1}, transColor{1, 1, 1};
};

// Evaluation of the graph at a surface point (uv in Maya convention, v up; P object space of the unit sphere).
class Eval {
public:
    explicit Eval(const Graph& g) : m_g(g) {}

    const Node* outputNode() const {
        for (const Node& n : m_g.nodes) if (n.type == "out.material") return &n;
        return nullptr;
    }
    const Link* link(const Node& n, const char* pin) const {
        int i = n.def ? n.def->findIn(pin) : -1;
        return i < 0 ? nullptr : m_g.inputLink(n.id, i);
    }
    // Value of an input pin as a colour (floats are replicated).
    float3 color(const Node& n, const char* pin, float2 uv, float3 P, int depth = 0) const {
        int i = n.def ? n.def->findIn(pin) : -1;
        if (i < 0) return float3(0, 0, 0);
        if (const Link* l = m_g.inputLink(n.id, i)) {
            const Node* src = m_g.node(l->fromNode);
            if (src && depth < 32) return output(*src, l->fromPin, uv, P, depth + 1);
        }
        const game::Value& v = n.inputs[i];
        float3 c = v.type == PinType::Vec3 ? v.v : float3(v.asFloat());
        // channel pins (R, G, B) connected on their own
        for (int k = 0; k < (int)n.def->ins.size(); ++k) {
            if (n.def->ins[k].parent != i) continue;
            if (const Link* l = m_g.inputLink(n.id, k))
                if (const Node* src = m_g.node(l->fromNode))
                    if (depth < 32) c[n.def->ins[k].channel] = lum(output(*src, l->fromPin, uv, P, depth + 1));
        }
        return c;
    }
    float value(const Node& n, const char* pin, float2 uv, float3 P, int depth = 0) const {
        int i = n.def ? n.def->findIn(pin) : -1;
        if (i < 0) return 0;
        if (const Link* l = m_g.inputLink(n.id, i)) {
            const Node* src = m_g.node(l->fromNode);
            if (src && depth < 32) return lum(output(*src, l->fromPin, uv, P, depth + 1));
        }
        return n.inputs[i].asFloat();
    }
    float3 output(const Node& n, int outPin, float2 uv, float3 P, int depth) const {
        if (n.def && outPin >= 0 && outPin < (int)n.def->outs.size() && n.def->outs[outPin].parent >= 0) {   // R, G or B of a colour
            const game::PinDef& p = n.def->outs[outPin];
            return float3(output(n, p.parent, uv, P, depth)[p.channel]);
        }
        const std::string& t = n.type;
        const bool alpha = n.def && outPin >= 0 && outPin < (int)n.def->outs.size() && n.def->outs[outPin].name == "Alpha";
        float3 c(0, 0, 0);
        auto C = [&](const char* p) { return color(n, p, uv, P, depth); };
        auto F = [&](const char* p) { return value(n, p, uv, P, depth); };
        if (t == "const.color") c = C("Color");
        else if (t == "const.float") c = float3(F("Value"));
        else if (t == "tex.ramp") {
            float rep = std::max(0.001f, F("Repeat"));
            float x = frac((F("U Direction") > 0.5f ? uv.x : uv.y) * rep);
            float pa = F("Position A"), pb = F("Position B");
            float3 ca = C("Color A"), cb = C("Color B");
            if (pa > pb) { std::swap(pa, pb); std::swap(ca, cb); }
            float k = pb > pa ? sat((x - pa) / (pb - pa)) : (x < pa ? 0.0f : 1.0f);
            c = lerp3(ca, cb, k);
        } else if (t == "tex.checker") {
            float rep = F("Repeat");
            int cell = ((int)std::floor(uv.x * rep * 2) + (int)std::floor(uv.y * rep * 2)) & 1;   // 2 x 2 cells per tile
            float3 c1 = C("Color 1"), c2 = C("Color 2"), avg = (c1 + c2) * 0.5f;
            float k = sat(F("Contrast"));
            c = lerp3(avg, cell ? c2 : c1, k);
        } else if (t == "tex.noise") {
            float sc = std::max(1e-4f, F("Scale"));
            int oct = std::max(1, std::min(8, (int)F("Octaves")));
            float dist = F("Distortion");
            float3 p = P * sc;   // aiNoise: P x scale (object space, unit sphere)
            if (dist != 0) p = p + float3(perlin(p + float3(5.2f, 1.3f, 7.1f)), perlin(p + float3(1.7f, 9.2f, 3.3f)), perlin(p + float3(8.3f, 2.8f, 4.1f))) * dist;
            float sum = 0, amp = 1, norm = 0;
            for (int o = 0; o < oct; ++o) { sum += perlin(p) * amp; norm += amp; amp *= 0.5f; p = p * 1.92f; }
            float nv = sat(0.5f + 0.5f * (sum / norm) * F("Amplitude") * 1.6f);
            c = lerp3(C("Color 1"), C("Color 2"), nv);
        } else if (t == "tex.image") {
            int fi = n.def->findIn("File");
            const Image& img = loadImage(fi >= 0 ? n.inputs[fi].s : std::string());
            if (img.w > 0) {
                float rep = std::max(0.001f, F("Repeat"));
                int x = (int)(frac(uv.x * rep) * img.w) % img.w, y = (int)((1.0f - frac(uv.y * rep)) * img.h) % img.h;
                c = img.px[(size_t)y * img.w + x];
            } else c = float3(0.5f, 0.0f, 0.5f);
        } else if (t == "util.colorCorrect") {
            c = C("Input");
            float g = std::max(0.01f, F("Gamma"));
            c = float3(std::pow(std::max(c.x, 0.0f), 1.0f / g), std::pow(std::max(c.y, 0.0f), 1.0f / g), std::pow(std::max(c.z, 0.0f), 1.0f / g));
            float hs = F("Hue Shift");
            if (hs != 0) {   // rotation around the grey axis
                float a = hs * 2.0f * 3.14159265f, cs = std::cos(a), sn = std::sin(a);
                float3 k = float3(0.57735f, 0.57735f, 0.57735f);
                float d = k.x * c.x + k.y * c.y + k.z * c.z;
                float3 cr(k.y * c.z - k.z * c.y, k.z * c.x - k.x * c.z, k.x * c.y - k.y * c.x);
                c = c * cs + cr * sn + k * (d * (1 - cs));
            }
            float l = lum(c);
            c = lerp3(float3(l), c, F("Saturation")) * std::pow(2.0f, F("Exposure"));
        } else if (t == "util.blend") c = lerp3(C("Color 2"), C("Color 1"), F("Blender"));
        else if (t == "util.multiply") { float3 a = C("A"), b = C("B"); c = float3(a.x * b.x, a.y * b.y, a.z * b.z); }
        return alpha ? float3(lum(c)) : c;
    }

    // Shading parameters of a shader node; textured values are read at (uv, P).
    Shade shade(const Node* n, float2 uv, float3 P, int depth = 0) const {
        Shade s;
        if (!n || depth > 16) return s;
        auto C = [&](const char* p) { return color(*n, p, uv, P); };
        auto F = [&](const char* p) { return value(*n, p, uv, P); };
        if (n->type == "shader.standardSurface") {
            s.base = C("Base Color") * F("Base");
            s.metal = F("Metalness");
            s.spec = F("Specular");
            s.rough = F("Specular Roughness");
            s.ior = F("Specular IOR");
            s.trans = F("Transmission");
            s.transColor = C("Transmission Color");
            s.sss = F("Subsurface");
            s.sssColor = C("Subsurface Color");
            s.coat = F("Coat");
            s.coatRough = F("Coat Roughness");
            s.emis = F("Emission");
            s.emisColor = C("Emission Color");
        } else if (n->type == "shader.flat") {
            s.base = float3(0, 0, 0);
            s.spec = 0;
            s.emis = 1;
            s.emisColor = C("Color");
        } else if (n->type == "shader.mix") {
            auto sub = [&](const char* pin) {
                const Link* l = link(*n, pin);
                return shade(l ? m_g.node(l->fromNode) : nullptr, uv, P, depth + 1);
            };
            Shade a = sub("Shader 1"), b = sub("Shader 2");
            float k = sat(F("Mix"));
            auto L = [&](float x, float y) { return x + (y - x) * k; };
            s.base = lerp3(a.base, b.base, k);
            s.rough = L(a.rough, b.rough); s.metal = L(a.metal, b.metal); s.spec = L(a.spec, b.spec); s.ior = L(a.ior, b.ior);
            s.trans = L(a.trans, b.trans); s.sss = L(a.sss, b.sss); s.coat = L(a.coat, b.coat); s.coatRough = L(a.coatRough, b.coatRough);
            s.emis = 1;
            s.emisColor = lerp3(a.emisColor * a.emis, b.emisColor * b.emis, k);
            s.sssColor = lerp3(a.sssColor, b.sssColor, k);
            s.transColor = lerp3(a.transColor, b.transColor, k);
        }
        return s;
    }
    const Node* surface() const {
        const Node* o = outputNode();
        const Link* l = o ? link(*o, "Surface") : nullptr;
        return l ? m_g.node(l->fromNode) : nullptr;
    }
    // True when the base colour of the shader depends on a texture (so it must be baked).
    bool textured(const Node* n, int depth = 0) const {
        if (!n || depth > 32) return false;
        if (n->type.rfind("tex.", 0) == 0) return true;
        for (const Link& l : m_g.links)
            if (l.toNode == n->id && textured(m_g.node(l.fromNode), depth + 1)) return true;
        return false;
    }

private:
    const Graph& m_g;
};

uint8_t toSrgb8(float x) {
    x = sat(x);
    float s = x <= 0.0031308f ? x * 12.92f : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
    return (uint8_t)std::lround(s * 255.0f);
}

// UV sphere with the texture coordinates of a Maya polySphere (20 axis divisions): u turns from +X towards -Z,
// starting one division (1/20) before +X; v goes from the bottom pole to the top.
const float kSeam = 0.05f;
MeshData uvSphere(float r, int segU, int segV) {
    MeshData m;
    for (int j = 0; j <= segV; ++j) {
        float v = (float)j / segV;
        float th = v * kPi;
        for (int i = 0; i <= segU; ++i) {
            float u = (float)i / segU;
            float ph = (u + kSeam) * 2.0f * kPi;
            float3 n(std::sin(th) * std::cos(ph), std::cos(th), -std::sin(th) * std::sin(ph));
            m.vertices.push_back({n * r, n, float2(u, v)});   // texture row 0 = top = Maya v 1
        }
    }
    for (int j = 0; j < segV; ++j)
        for (int i = 0; i < segU; ++i) {
            uint32_t a = j * (segU + 1) + i, b = a + segU + 1;
            m.addTri(a, a + 1, b);
            m.addTri(a + 1, b + 1, b);
        }
    m.quadEdges();
    return m;
}

} // namespace

// ------------------------------------------------------------------------------------------------ preview

// ------------------------------------------------------------------------------------------------ node thumbnails

static float3 acesToRec709(float3 c) {
    float3 r(1.70505f * c.x - 0.62179f * c.y - 0.08326f * c.z, -0.13026f * c.x + 1.14080f * c.y - 0.01055f * c.z,
             -0.02400f * c.x - 0.12897f * c.y + 1.15297f * c.z);
    return float3(std::max(r.x, 0.0f), std::max(r.y, 0.0f), std::max(r.z, 0.0f));
}

// Surface point of the unit sphere for Maya UVs (as the baked texture of the preview).
static float3 spherePoint(float2 uv) {
    float th = (1.0f - uv.y) * kPi, ph = (uv.x + kSeam) * 2.0f * kPi;
    return float3(std::sin(th) * std::cos(ph), std::cos(th), -std::sin(th) * std::sin(ph));
}

bool sampleValue(const Graph& flat, int node, int pin, float3& out) {
    const Node* n = flat.node(node);
    if (!n || !n->def || pin < 0 || pin >= (int)n->def->outs.size()) return false;
    if (n->def->outs[pin].type == PinType::Object) return false;
    Eval ev(flat);
    out = ev.output(*n, pin, float2(0.5f, 0.5f), spherePoint(float2(0.5f, 0.5f)), 0);
    return true;
}

bool renderThumb(const Graph& flat, int node, int pin, const game::Value* constant, bool acescg, int S, std::vector<uint8_t>& rgba) {
    rgba.assign((size_t)S * S * 4, 255);
    Eval ev(flat);
    const Node* n = node > 0 ? flat.node(node) : nullptr;
    auto put = [&](int x, int y, float3 c) {
        if (acescg) c = acesToRec709(c);
        uint8_t* d = &rgba[((size_t)y * S + x) * 4];
        d[0] = toSrgb8(c.x);
        d[1] = toSrgb8(c.y);
        d[2] = toSrgb8(c.z);
        d[3] = 255;
    };
    if (constant) {   // unconnected compound input: its value
        float3 c = constant->asVec3();
        for (int y = 0; y < S; ++y) for (int x = 0; x < S; ++x) put(x, y, c);
        return true;
    }
    if (!n || !n->def || pin < 0 || pin >= (int)n->def->outs.size()) return false;
    if (n->def->outs[pin].type != PinType::Object) {   // a colour / value: the texture over the UVs
        for (int y = 0; y < S; ++y)
            for (int x = 0; x < S; ++x) {
                float2 uv((x + 0.5f) / S, 1.0f - (y + 0.5f) / S);
                put(x, y, ev.output(*n, pin, uv, spherePoint(uv), 0));
            }
        return true;
    }
    // a shader: small shaded ball (Hypershade swatch style)
    const float3 L = normalize(float3(-0.55f, 0.6f, 0.75f)), V(0, 0, 1);
    const float3 H = normalize(L + V);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x) {
            float sx = (x + 0.5f) / S * 2 - 1, sy = 1 - (y + 0.5f) / S * 2;
            float r2 = (sx * sx + sy * sy) / 0.86f;
            if (r2 >= 1) {   // background: dark checker
                bool k = ((x / 6) + (y / 6)) & 1;
                float g = k ? 0.035f : 0.02f;
                uint8_t* d = &rgba[((size_t)y * S + x) * 4];
                d[0] = d[1] = d[2] = toSrgb8(g);
                continue;
            }
            float3 N(sx / std::sqrt(0.86f), sy / std::sqrt(0.86f), std::sqrt(1 - r2));
            // object point and Maya UVs of the visible sphere (camera on +Z)
            float th = std::acos(std::min(1.0f, std::max(-1.0f, N.y))), ph = std::atan2(-N.z, N.x);
            float u = ph / (2 * kPi) - kSeam;
            u -= std::floor(u);
            float2 uv(u, 1 - th / kPi);
            Shade s = ev.shade(n, uv, N);
            float ndl = std::max(0.0f, dot(N, L)), ndh = std::max(0.0f, dot(N, H)), ndv = std::max(0.0f, N.z);
            float rough = std::max(0.04f, s.rough), p = 2.0f / (rough * rough * rough * rough + 1e-4f);
            float fres = 0.04f + 0.96f * std::pow(1 - ndv, 5.0f);
            float3 base = s.base;
            float3 diff = base * ((1 - s.metal) * (1 - s.trans) * (0.16f + 0.84f * ndl));
            float3 f0 = lerp3(float3(0.04f * s.spec), base, s.metal);
            float spec = std::pow(ndh, std::min(p, 4000.0f)) * (std::min(p, 4000.0f) + 2) / 8.0f * ndl;
            float3 c = diff + f0 * spec * 0.6f + float3(0.18f) * fres * (s.spec * (1 - rough)) + s.emisColor * s.emis;
            c = c + float3(0.9f) * (s.coat * std::pow(ndh, 600.0f) * 3.0f);
            put(x, y, c);
        }
    return true;
}

void Preview::init(vp3d::Renderer& r, app::ImGuiLayer& gui, const std::wstring& hdri) {
    m_r = &r;
    m_gui = &gui;
    namespace fs = std::filesystem;
    if (!hdri.empty()) m_hdris.push_back(hdri);
    // HDRIs: assets/hdri next to the exe, then in the parent folders (project, main checkout of a worktree)
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    fs::path p = fs::path(exe).parent_path();
    for (int up = 0; up < 6 && !p.empty(); ++up, p = p.parent_path()) {
        std::error_code ec;
        fs::path dir = p / "assets" / "hdri";
        if (!fs::is_directory(dir, ec)) continue;
        for (auto& e : fs::directory_iterator(dir, ec)) {
            std::wstring ext = e.path().extension().wstring();
            for (auto& ch : ext) ch = (wchar_t)towlower(ch);
            if (ext != L".hdr" && ext != L".exr") continue;
            bool dup = false;
            for (auto& h : m_hdris) dup |= fs::path(h).filename() == e.path().filename();
            if (!dup) m_hdris.push_back(e.path().wstring());
        }
        if (p.parent_path() == p) break;
    }
    // studio HDRI first when there is one (closest to the Hypershade swatch lighting)
    std::stable_sort(m_hdris.begin(), m_hdris.end(), [](const std::wstring& a, const std::wstring& b) {
        return (a.find(L"studio") != std::wstring::npos) > (b.find(L"studio") != std::wstring::npos);
    });

    Scene& s = m_scene;
    s.name = "Shader Preview";
    Material m;
    m.name = "Preview";
    Object o;
    o.name = "Sphere";
    o.mesh = s.addMesh("sphere", uvSphere(1.0f, 128, 64));
    o.material = s.addMaterial(m);
    o.position = float3(0, 1, 0);
    s.addObject(o);
    s.env.top = float3(0.32f, 0.34f, 0.38f);
    s.env.horizon = float3(0.18f, 0.19f, 0.21f);
    s.env.bottom = float3(0.04f, 0.04f, 0.045f);
    s.env.intensity = 0.6f;
    setHdri(m_hdris.empty() ? -1 : 0);

    m_rs.raster = false;
    m_rs.maxBounces = 8;
    m_rs.maxSamples = 1024;
    m_rs.samplesPerFrame = 2;
    m_rs.vignette = 0.0f;
    m_rs.denoise = true;
    m_rs.bevel = false;
    m_rs.tonemapper = 0;   // ACES, as the Maya default view transform
    m_rs.fireflyClamp = 25.0f;
}

void Preview::setHdri(int index) {
    m_hdri = index;
    m_scene.env.hdri.reset();
    if (index >= 0 && index < (int)m_hdris.size()) {
        auto img = std::make_shared<EnvImage>();
        std::string err;
        if (loadEnvImage(m_hdris[index], *img, &err, 2048)) m_scene.env.hdri = img;
    }
    m_viewKey.clear();
}

void Preview::applyGraph(const Graph& g) {
    // Maya colours are in its rendering space (ACEScg by default): vp3d renders linear Rec.709
    auto toRender = [&](float3 c) {
        if (!m_acescg) return c;
        float3 r(1.70505f * c.x - 0.62179f * c.y - 0.08326f * c.z, -0.13026f * c.x + 1.14080f * c.y - 0.01055f * c.z,
                 -0.02400f * c.x - 0.12897f * c.y + 1.15297f * c.z);
        return float3(std::max(r.x, 0.0f), std::max(r.y, 0.0f), std::max(r.z, 0.0f));
    };
    Eval ev(g);
    const Node* sn = ev.surface();
    Shade sh = ev.shade(sn, float2(0.5f, 0.5f), float3(0, 0, 1));
    Material& m = m_scene.materials[m_scene.objects[0].material];
    m.roughness = sat(sh.rough);
    m.metallic = sat(sh.metal);
    // vp3d specular 0.5 = F0 0.04 (IOR 1.5): scaled by the Arnold specular weight and the IOR
    float f0 = std::pow((sh.ior - 1) / (sh.ior + 1), 2.0f);
    m.specular = sat(sh.spec * f0 / 0.08f);
    m.ior = std::max(1.0f, sh.ior);
    m.transmission = sat(sh.trans);
    m.subsurface = sat(sh.sss);
    m.sssRadius = float3(1.0f, 0.35f, 0.15f) * 0.3f;
    m.coat = sat(sh.coat);
    m.coatRoughness = sat(sh.coatRough);
    m.emission = toRender(sh.emisColor);
    m.emissionStrength = sh.emis;
    m_scene.textures.clear();
    m.baseColorTex = -1;
    if (sn && ev.textured(sn)) {
        // base colour baked into a texture (uv sphere: u around, v up)
        const int W = 512, H = 256;
        Texture t;
        t.name = "baseColor";
        t.width = W;
        t.height = H;
        t.rgba.resize((size_t)W * H * 4);
        for (int y = 0; y < H; ++y) {
            float v = 1.0f - (y + 0.5f) / H;
            float th = (1.0f - v) * kPi;
            for (int x = 0; x < W; ++x) {
                float u = (x + 0.5f) / W, ph = (u + kSeam) * 2.0f * kPi;
                float3 P(std::sin(th) * std::cos(ph), std::cos(th), -std::sin(th) * std::sin(ph));
                Shade px = ev.shade(sn, float2(u, v), P);
                // the transmission colour tints the glass: shown through the base colour slot
                float3 c = m.transmission > 0.5f ? px.transColor : px.base;
                uint8_t* d = &t.rgba[((size_t)y * W + x) * 4];
                c = toRender(c);
                d[0] = toSrgb8(c.x);
                d[1] = toSrgb8(c.y);
                d[2] = toSrgb8(c.z);
                d[3] = 255;
            }
        }
        m_scene.textures.push_back(std::move(t));
        m.baseColorTex = 0;
        m.baseColor = float3(1, 1, 1);
    } else {
        m.baseColor = toRender(m.transmission > 0.5f ? sh.transColor : sh.base);
    }
    if (m.subsurface > 0) m.baseColor = lerp3(m.baseColor, toRender(sh.sssColor), m.subsurface);
    m_sceneDirty = true;
}

bool Preview::draw(const Graph& g, ImVec2 pos, ImVec2 size, float dpi) {
    // the graph changed: re-evaluate the material (kept for later while paused)
    std::string key = g.toJson();
    if (key != m_graphKey && !m_paused) {
        m_graphKey = key;
        applyGraph(g);
    }

    const float topH = 28 * dpi, barH = 30 * dpi;
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar;
    ImGui::Begin("##preview", nullptr, flags);
    ImGui::PopStyleVar();
    // the graph window covers the whole workspace: the preview stays in front of it (a click on the graph raised it)
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 imgPos(pos.x, pos.y + topH);
    ImVec2 img(size.x, std::max(16.0f, size.y - barH - topH));
    int w = std::max(16, (int)img.x), h = std::max(16, (int)img.y);

    // top bar: play / pause of the rendering, sample count, reset of the lighting rotation
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + topH), app::th::bar());
    dl->AddLine(ImVec2(pos.x, pos.y + topH - 0.5f), ImVec2(pos.x + size.x, pos.y + topH - 0.5f), app::th::border());
    ImGui::SetCursorScreenPos(ImVec2(pos.x + 5 * dpi, pos.y + 3 * dpi));
    if (ImGui::Button(m_paused ? "\xEE\x9D\xA8##pvplay" : "\xEE\x9D\xA9##pvplay", ImVec2(26 * dpi, topH - 6 * dpi))) m_paused = !m_paused;   // play / pause glyphs
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(m_paused ? "Resume the preview" : "Pause the preview (frees the GPU for Maya)");
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Material Preview");
    char info[64];
    snprintf(info, sizeof info, m_paused ? "paused  %u spp" : "%u spp", m_r->sampleCount());
    ImVec2 ts = ImGui::CalcTextSize(info);
    ImGui::SameLine(size.x - ts.x - 40 * dpi);
    ImGui::TextColored(app::th::dimV(), "%s", info);
    ImGui::SameLine(size.x - 31 * dpi);
    if (ImGui::Button("\xEE\x9C\xAC##pvreset", ImVec2(26 * dpi, topH - 6 * dpi))) m_envRot = 0;   // refresh glyph
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset the lighting rotation");

    // left drag: turns the HDRI and the lights 360 degrees around the sphere (Substance Painter style); the camera stays
    ImGui::SetCursorScreenPos(imgPos);
    ImGui::InvisibleButton("##pvdrag", img, ImGuiButtonFlags_MouseButtonLeft);
    if (ImGui::IsItemActive()) {
        m_envRot += ImGui::GetIO().MouseDelta.x * (2.0f * kPi / std::max(1.0f, img.x));   // the width of the image = one turn
        m_envRot = std::fmod(m_envRot, 2.0f * kPi);
    }
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    Scene& s = m_scene;
    Camera& c = s.camera;
    c.target = float3(0, 1, 0);
    c.yaw = m_yaw;
    c.pitch = m_pitch;
    c.distance = m_dist;
    c.fovY = radians(30.0f);
    c.dof = false;
    s.env.hdriRotation = m_envRot;
    s.env.hdriIntensity = 1.0f;
    s.env.visibleToCamera = m_background;
    s.lights.clear();
    if (m_lights) {
        auto light = [&](const char* name, float az, float el, float dist, float sz, float3 col, float k) {
            AreaLight l;
            l.name = name;
            float a = az + m_envRot;
            l.position = float3(std::sin(a) * std::cos(el) * dist, 1.0f + std::sin(el) * dist, std::cos(a) * std::cos(el) * dist);
            l.target = float3(0, 1, 0);
            l.width = l.height = sz;
            l.color = col;
            l.intensity = k;
            l.visibleToCamera = false;
            s.lights.push_back(l);
        };
        // three point rig relative to the default camera (looking down -Z from +Z)
        light("Key", radians(-45.0f), radians(35.0f), 5.0f, 2.0f, float3(1.0f, 0.96f, 0.9f), 6.0f);
        light("Fill", radians(60.0f), radians(10.0f), 5.0f, 3.0f, float3(0.85f, 0.9f, 1.0f), 2.0f);
        light("Rim", radians(165.0f), radians(25.0f), 5.0f, 1.5f, float3(0.95f, 0.97f, 1.0f), 8.0f);
    }
    // -0.6 EV: measured against Arnold (Maya ACES 1.0 SDR view, same HDRI), so EV 0 matches a Maya render
    m_rs.exposure = std::pow(2.0f, m_exposure - 0.6f);

    std::ostringstream vk;
    vk << m_yaw << ' ' << m_pitch << ' ' << m_dist << ' ' << m_envRot << ' ' << m_exposure << ' ' << m_lights << m_background << ' ' << m_hdri << ' ' << w << 'x' << h;
    if (!m_paused) {
        if (m_sceneDirty) {
            m_r->setScene(s);   // also uploads the baked texture
            m_r->resetAccumulation();
            m_sceneDirty = false;
        }
        if (vk.str() != m_viewKey) {
            m_viewKey = vk.str();
            m_r->resetAccumulation();
        }
    }
    // paused: no new samples (the image is only tonemapped again), changes wait for Play
    m_rs.maxSamples = m_paused ? std::max(1u, m_r->sampleCount()) : 1024;
    ViewRect v;
    v.w = w;
    v.h = h;
    if (m_paused) v = m_lastView;   // same size as the image kept
    m_lastView = v;
    m_r->renderToTexture(s, m_rs, v, -1);
    m_busy = !m_paused && !m_r->converged(m_rs);
    dl->AddImage(m_gui->viewportTexture(), imgPos, ImVec2(imgPos.x + img.x, imgPos.y + img.y));

    // bottom bar: HDRI, lights, background, exposure
    ImGui::SetCursorScreenPos(ImVec2(pos.x + 6 * dpi, imgPos.y + img.y + 4 * dpi));
    ImGui::SetNextItemWidth(size.x * 0.36f);
    std::string cur = m_hdri >= 0 ? std::filesystem::path(m_hdris[m_hdri]).stem().string() : std::string("Gradient");
    if (ImGui::BeginCombo("##hdri", cur.c_str())) {
        if (ImGui::Selectable("Gradient", m_hdri < 0)) setHdri(-1);
        for (int i = 0; i < (int)m_hdris.size(); ++i)
            if (ImGui::Selectable(std::filesystem::path(m_hdris[i]).stem().string().c_str(), m_hdri == i)) setHdri(i);
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Environment (HDRI). Drag left / right on the preview to turn it.");
    ImGui::SameLine();
    ImGui::Checkbox("3pt", &m_lights);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Key / fill / rim area lights");
    ImGui::SameLine();
    ImGui::Checkbox("BG", &m_background);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::DragFloat("##exp", &m_exposure, 0.02f, -6, 6, "EV %.1f");
    ImGui::End();
    return true;
}

} // namespace mayashader
