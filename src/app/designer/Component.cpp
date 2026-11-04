#include "Component.h"
#include "app/Json.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using app::Json;

namespace wzui {

static const char* kShapes[] = {"rect", "ellipse", "text", "line"};
static const char* kStates[] = {"normal", "hover", "active", "disabled"};
static const char* kStateLabels[] = {"Normal", "Survol", "Appuyé", "Désactivé"};
const char* shapeName(Shape s) { return (int)s >= 0 && s < Shape::Count ? kShapes[(int)s] : "rect"; }
const char* stateKey(int st) { return st >= 0 && st < StCount ? kStates[st] : "normal"; }
const char* stateLabel(int st) { return st >= 0 && st < StCount ? kStateLabels[st] : "Normal"; }

Layer* Component::find(int uid) {
    for (auto& l : layers) if (l.uid == uid) return &l;
    return nullptr;
}

Layer Component::makeLayer(Shape s) {
    Layer l;
    l.uid = nextUid++;
    l.shape = s;
    static const char* names[] = {"Rectangle", "Ellipse", "Texte", "Ligne"};
    l.name = names[(int)s] + std::string(" ") + std::to_string(l.uid);
    if (s == Shape::Text) {
        l.look[StNormal].fill.on = false;
        l.text = "{text}";
    }
    if (s == Shape::Line) {
        l.look[StNormal].fill.a = ImVec4(1, 1, 1, 0.6f);
        l.look[StNormal].strokeWidth = 1;
    }
    return l;
}

Component defaultButton(const std::string& name) {
    Component c;
    c.name = name;
    c.w = 140;
    c.h = 40;
    Layer bg = c.makeLayer(Shape::Rect);
    bg.name = "Fond";
    bg.w = 140; bg.h = 40; bg.radius = 8;
    Look& n = bg.look[StNormal];
    n.fill.gradient = true;
    n.fill.a = ImVec4(0.93f, 0.66f, 0.33f, 1);
    n.fill.b = ImVec4(0.78f, 0.47f, 0.18f, 1);
    n.stroke = true;
    n.strokeColor = ImVec4(1, 0.85f, 0.6f, 0.45f);
    n.shadow = true;
    bg.hasState[StHover] = true;
    bg.look[StHover] = n;
    bg.look[StHover].fill.a = ImVec4(1.0f, 0.75f, 0.42f, 1);
    bg.look[StHover].fill.b = ImVec4(0.86f, 0.55f, 0.24f, 1);
    bg.look[StHover].shadowBlur = 10;
    bg.hasState[StActive] = true;
    bg.look[StActive] = n;
    bg.look[StActive].fill.a = ImVec4(0.72f, 0.43f, 0.16f, 1);
    bg.look[StActive].fill.b = ImVec4(0.82f, 0.52f, 0.22f, 1);
    bg.look[StActive].shadowOffset = ImVec2(0, 1);
    bg.look[StActive].shadowBlur = 3;
    bg.look[StActive].offset = ImVec2(0, 1);
    bg.hasState[StDisabled] = true;
    bg.look[StDisabled] = n;
    bg.look[StDisabled].fill.a = bg.look[StDisabled].fill.b = ImVec4(0.30f, 0.32f, 0.35f, 1);
    bg.look[StDisabled].stroke = false;
    bg.look[StDisabled].shadow = false;
    c.layers.push_back(bg);

    Layer t = c.makeLayer(Shape::Text);
    t.name = "Libellé";
    t.w = 140; t.h = 40; t.fontSize = 15;
    t.look[StNormal].textColor = ImVec4(0.16f, 0.09f, 0.03f, 1);
    t.hasState[StActive] = true;
    t.look[StActive] = t.look[StNormal];
    t.look[StActive].offset = ImVec2(0, 1);
    t.hasState[StDisabled] = true;
    t.look[StDisabled] = t.look[StNormal];
    t.look[StDisabled].textColor = ImVec4(0.6f, 0.62f, 0.65f, 1);
    c.layers.push_back(t);
    return c;
}

// ---------- JSON ----------
namespace {
std::string q(const std::string& s) { return "\"" + app::jsonEscape(s) + "\""; }
std::string num(float v) { char b[32]; snprintf(b, sizeof b, "%g", v); return b; }
std::string col(const ImVec4& c) { return "[" + num(c.x) + ", " + num(c.y) + ", " + num(c.z) + ", " + num(c.w) + "]"; }
std::string v2(const ImVec2& v) { return "[" + num(v.x) + ", " + num(v.y) + "]"; }

std::string lookJson(const Look& l) {
    std::string o = "{\"fill\": {\"on\": " + std::string(l.fill.on ? "true" : "false") + ", \"gradient\": " + (l.fill.gradient ? "true" : "false") +
                    ", \"a\": " + col(l.fill.a) + ", \"b\": " + col(l.fill.b) + ", \"angle\": " + num(l.fill.angle) + "}";
    o += ", \"stroke\": " + std::string(l.stroke ? "true" : "false") + ", \"strokeColor\": " + col(l.strokeColor) + ", \"strokeWidth\": " + num(l.strokeWidth);
    o += ", \"shadow\": " + std::string(l.shadow ? "true" : "false") + ", \"shadowColor\": " + col(l.shadowColor) + ", \"shadowOffset\": " + v2(l.shadowOffset) +
         ", \"shadowBlur\": " + num(l.shadowBlur);
    o += ", \"textColor\": " + col(l.textColor) + ", \"opacity\": " + num(l.opacity) + ", \"offset\": " + v2(l.offset) + "}";
    return o;
}
bool rc(const Json& j, ImVec4& c) {
    if (j.type != Json::Array || j.a.size() < 3) return false;
    c = ImVec4((float)j[0].num(), (float)j[1].num(), (float)j[2].num(), (float)j[3].num(1));
    return true;
}
bool rv(const Json& j, ImVec2& v) {
    if (j.type != Json::Array || j.a.size() < 2) return false;
    v = ImVec2((float)j[0].num(), (float)j[1].num());
    return true;
}
bool rb(const Json& j, bool def) { return j.type == Json::Bool ? j.b : def; }
void readLook(const Json& j, Look& l) {
    const Json& f = j["fill"];
    l.fill.on = rb(f["on"], l.fill.on);
    l.fill.gradient = rb(f["gradient"], l.fill.gradient);
    rc(f["a"], l.fill.a);
    rc(f["b"], l.fill.b);
    l.fill.angle = (float)f["angle"].num(l.fill.angle);
    l.stroke = rb(j["stroke"], l.stroke);
    rc(j["strokeColor"], l.strokeColor);
    l.strokeWidth = (float)j["strokeWidth"].num(l.strokeWidth);
    l.shadow = rb(j["shadow"], l.shadow);
    rc(j["shadowColor"], l.shadowColor);
    rv(j["shadowOffset"], l.shadowOffset);
    l.shadowBlur = (float)j["shadowBlur"].num(l.shadowBlur);
    rc(j["textColor"], l.textColor);
    l.opacity = (float)j["opacity"].num(l.opacity);
    rv(j["offset"], l.offset);
}
} // namespace

bool saveComponent(const fs::path& file, const Component& c) {
    std::string o = "{\n  \"format\": \"wzcomp\", \"version\": 1,\n  \"name\": " + q(c.name) + ", \"w\": " + num(c.w) + ", \"h\": " + num(c.h) + ",\n  \"layers\": [\n";
    for (size_t i = 0; i < c.layers.size(); ++i) {
        const Layer& l = c.layers[i];
        o += "    {\"shape\": " + q(shapeName(l.shape)) + ", \"name\": " + q(l.name) + ", \"x\": " + num(l.x) + ", \"y\": " + num(l.y) +
             ", \"w\": " + num(l.w) + ", \"h\": " + num(l.h);
        if (l.shape == Shape::Rect) o += ", \"radius\": " + num(l.radius);
        if (l.shape == Shape::Text) o += ", \"text\": " + q(l.text) + ", \"fontSize\": " + num(l.fontSize) + ", \"align\": " + std::to_string(l.align);
        if (!l.visible) o += ", \"visible\": false";
        o += ",\n     \"look\": " + lookJson(l.look[StNormal]);
        bool any = false;
        for (int s = 1; s < StCount; ++s)
            if (l.hasState[s]) {
                o += std::string(any ? ",\n       " : ",\n     \"states\": {\n       ") + q(stateKey(s)) + ": " + lookJson(l.look[s]);
                any = true;
            }
        if (any) o += "}";
        o += "}" + std::string(i + 1 < c.layers.size() ? ",\n" : "\n");
    }
    o += "  ]\n}\n";
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    f << o;
    return (bool)f;
}

bool loadComponent(const fs::path& file, Component& out, std::string* err) {
    std::ifstream f(file, std::ios::binary);
    if (!f) { if (err) *err = "fichier introuvable"; return false; }
    std::stringstream ss;
    ss << f.rdbuf();
    Json j;
    if (!Json::parse(ss.str(), j) || j.type != Json::Object) { if (err) *err = "JSON invalide"; return false; }
    Component c;
    c.name = j["name"].str(file.stem().stem().u8string());
    c.w = (float)j["w"].num(140);
    c.h = (float)j["h"].num(40);
    for (const Json& lj : j["layers"].a) {
        Layer l;
        l.uid = c.nextUid++;
        std::string sh = lj["shape"].str("rect");
        for (int i = 0; i < (int)Shape::Count; ++i) if (sh == kShapes[i]) l.shape = (Shape)i;
        l.name = lj["name"].str();
        l.x = (float)lj["x"].num(); l.y = (float)lj["y"].num();
        l.w = (float)lj["w"].num(40); l.h = (float)lj["h"].num(20);
        l.radius = (float)lj["radius"].num(0);
        l.text = lj["text"].str("{text}");
        l.fontSize = (float)lj["fontSize"].num(14);
        l.align = (int)lj["align"].num(1);
        l.visible = rb(lj["visible"], true);
        if (l.shape == Shape::Text) l.look[StNormal].fill.on = false;
        readLook(lj["look"], l.look[StNormal]);
        for (int s = 1; s < StCount; ++s) {
            const Json& sj = lj["states"][stateKey(s)];
            if (sj.type != Json::Object) continue;
            l.hasState[s] = true;
            l.look[s] = l.look[StNormal];
            readLook(sj, l.look[s]);
        }
        c.layers.push_back(l);
    }
    out = std::move(c);
    return true;
}

// ---------- drawing ----------
namespace {
ImU32 u32(ImVec4 c, float alpha) { c.w *= alpha; return ImGui::ColorConvertFloat4ToU32(c); }

void pathShape(ImDrawList* dl, Shape s, ImVec2 a, ImVec2 b, float radius) {
    if (s == Shape::Ellipse) {
        ImVec2 c((a.x + b.x) / 2, (a.y + b.y) / 2), r((b.x - a.x) / 2, (b.y - a.y) / 2);
        dl->PathEllipticalArcTo(c, ImVec2(std::max(0.5f, r.x), std::max(0.5f, r.y)), 0, 0, IM_PI * 2, 48);
    } else {
        dl->PathRect(a, b, std::min(radius, std::min(b.x - a.x, b.y - a.y) * 0.5f));
    }
}

void fillShape(ImDrawList* dl, Shape s, ImVec2 a, ImVec2 b, float radius, const Fill& f, float alpha) {
    if (!f.on || b.x <= a.x || b.y <= a.y) return;
    int v0 = dl->VtxBuffer.Size;
    pathShape(dl, s, a, b, radius);
    dl->PathFillConvex(u32(f.a, alpha));
    if (f.gradient) {
        ImVec2 c((a.x + b.x) / 2, (a.y + b.y) / 2);
        float rad = f.angle * IM_PI / 180, dx = std::cos(rad), dy = std::sin(rad);
        float len = (std::fabs((b.x - a.x) * dx) + std::fabs((b.y - a.y) * dy)) / 2;
        ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, ImVec2(c.x - dx * len, c.y - dy * len), ImVec2(c.x + dx * len, c.y + dy * len),
                                                      u32(f.a, alpha), u32(f.b, alpha));
    }
}
} // namespace

void drawComponent(ImDrawList* dl, const Component& c, ImVec2 pos, ImVec2 size, float scale, int st, const std::string& text) {
    const float sx = c.w > 0 ? size.x / c.w : 1, sy = c.h > 0 ? size.y / c.h : 1;
    ImFont* font = ImGui::GetFont();
    for (const Layer& l : c.layers) {
        if (!l.visible) continue;
        const Look& k = l.lookFor(st);
        float alpha = k.opacity * ((st == StDisabled && !l.hasState[StDisabled]) ? 0.45f : 1.0f);
        ImVec2 a(pos.x + (l.x + k.offset.x) * sx, pos.y + (l.y + k.offset.y) * sy);
        ImVec2 b(a.x + l.w * sx, a.y + l.h * sy);
        const float rad = l.radius * scale;
        switch (l.shape) {
        case Shape::Rect:
        case Shape::Ellipse: {
            if (k.shadow && k.shadowColor.w > 0) {   // soft shadow: rings growing and fading
                const int n = 8;
                ImVec2 o(k.shadowOffset.x * scale, k.shadowOffset.y * scale);
                for (int i = n; i >= 1; --i) {
                    float e = k.shadowBlur * scale * i / n;
                    float w = k.shadowColor.w * alpha * 2.2f / n * (1.0f - (float)(i - 1) / n);
                    pathShape(dl, l.shape, ImVec2(a.x + o.x - e, a.y + o.y - e), ImVec2(b.x + o.x + e, b.y + o.y + e), rad + e);
                    dl->PathFillConvex(ImGui::ColorConvertFloat4ToU32(ImVec4(k.shadowColor.x, k.shadowColor.y, k.shadowColor.z, w)));
                }
            }
            fillShape(dl, l.shape, a, b, rad, k.fill, alpha);
            if (k.stroke && k.strokeWidth > 0) {
                float t = k.strokeWidth * scale, h = t * 0.5f;
                pathShape(dl, l.shape, ImVec2(a.x + h, a.y + h), ImVec2(b.x - h, b.y - h), std::max(0.0f, rad - h));
                dl->PathStroke(u32(k.strokeColor, alpha), ImDrawFlags_Closed, t);
            }
            break;
        }
        case Shape::Text: {
            if (k.fill.on) fillShape(dl, Shape::Rect, a, b, 0, k.fill, alpha);
            std::string t = l.text;
            for (size_t p; (p = t.find("{text}")) != std::string::npos;) t.replace(p, 6, text);
            float fs = l.fontSize * scale;
            ImVec2 z = font->CalcTextSizeA(fs, FLT_MAX, 0, t.c_str());
            float tx = l.align == 0 ? a.x + 2 * scale : l.align == 2 ? b.x - z.x - 2 * scale : (a.x + b.x - z.x) / 2;
            dl->AddText(font, fs, ImVec2(std::floor(tx), std::floor((a.y + b.y - z.y) / 2)), u32(k.textColor, alpha), t.c_str());
            break;
        }
        case Shape::Line:
            dl->AddLine(a, b, u32(k.fill.a, alpha), std::max(1.0f, k.strokeWidth * scale));
            break;
        default:
            break;
        }
    }
}

// ---------- library ----------
ComponentLibrary& components() {
    static ComponentLibrary lib;
    return lib;
}
void ComponentLibrary::setFolder(const fs::path& f) {
    folder = f;
    std::error_code ec;
    fs::create_directories(folder, ec);
    items.clear();
    times.clear();
    refresh();
}
void ComponentLibrary::refresh() {
    std::error_code ec;
    std::map<std::string, bool> seen;
    for (auto& e : fs::directory_iterator(folder, ec)) {
        std::string n = e.path().filename().u8string();
        if (n.size() <= 12 || n.compare(n.size() - 12, 12, ".wzcomp.json") != 0) continue;
        std::string key = n.substr(0, n.size() - 12);
        seen[key] = true;
        auto t = fs::last_write_time(e.path(), ec);
        auto it = times.find(key);
        if (it != times.end() && it->second == t) continue;
        Component c;
        if (loadComponent(e.path(), c)) { c.name = key; items[key] = std::move(c); times[key] = t; }
    }
    for (auto it = items.begin(); it != items.end();)
        if (!seen.count(it->first)) { times.erase(it->first); it = items.erase(it); }
        else ++it;
}
const Component* ComponentLibrary::find(const std::string& name) const {
    auto it = items.find(name);
    return it == items.end() ? nullptr : &it->second;
}

} // namespace wzui
