#include "UiDoc.h"
#include "app/Json.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

using app::Json;

namespace wzui {

static const char* kNames[] = {"Window", "Panel", "Row", "Column", "Button", "Label", "Checkbox", "Slider", "Input",
                               "Combo", "Color", "Progress", "Separator", "Spacer", "Component"};
static_assert(sizeof(kNames) / sizeof(kNames[0]) == (int)Kind::Count, "kind names");

const char* kindName(Kind k) { return (int)k >= 0 && k < Kind::Count ? kNames[(int)k] : "?"; }
bool kindFromName(const std::string& n, Kind& out) {
    for (int i = 0; i < (int)Kind::Count; ++i)
        if (n == kNames[i]) { out = (Kind)i; return true; }
    return false;
}
bool isContainer(Kind k) { return k == Kind::Window || k == Kind::Panel || k == Kind::Row || k == Kind::Column; }
bool isFreeLayout(Kind k) { return k == Kind::Window || k == Kind::Panel; }

// ---------- Doc ----------
std::string Doc::uniqueId(const std::string& base) {
    auto taken = [&](const std::string& id) {
        bool found = false;
        std::vector<const Node*> stack;
        for (auto& w : windows) stack.push_back(&w);
        while (!stack.empty() && !found) {
            const Node* n = stack.back();
            stack.pop_back();
            if (n->id == id) found = true;
            for (auto& c : n->children) stack.push_back(&c);
        }
        return found;
    };
    if (!taken(base)) return base;
    for (int i = 2;; ++i) {
        std::string t = base + std::to_string(i);
        if (!taken(t)) return t;
    }
}

Node Doc::makeNode(Kind k) {
    Node n;
    n.kind = k;
    n.uid = nextUid++;
    std::string base = kindName(k);
    base[0] = (char)tolower(base[0]);
    n.id = uniqueId(base);
    switch (k) {
    case Kind::Window: n.text = "Fenêtre"; n.w = 360; n.h = 260; break;
    case Kind::Panel: n.text = ""; n.w = 220; n.h = 140; n.hasBg = true; break;
    case Kind::Row: case Kind::Column: break;
    case Kind::Button: n.text = "Bouton"; break;
    case Kind::Label: n.text = "Texte"; break;
    case Kind::Checkbox: n.text = "Option"; break;
    case Kind::Slider: n.text = "Valeur"; n.w = 180; break;
    case Kind::Input: n.text = "Nom"; n.w = 180; break;
    case Kind::Combo: n.text = "Choix"; n.w = 180; n.items = {"Premier", "Deuxième", "Troisième"}; break;
    case Kind::Color: n.text = "Couleur"; n.w = 180; break;
    case Kind::Progress: n.w = 180; break;
    case Kind::Separator: n.w = 180; break;
    case Kind::Spacer: n.w = 16; n.h = 16; break;
    case Kind::Component: n.text = "Bouton"; n.component = "bouton_ambre"; break;
    default: break;
    }
    return n;
}

static void assign(Node& n, int& next) {
    n.uid = next++;
    for (auto& c : n.children) assign(c, next);
}
void Doc::assignUids() {
    nextUid = 1;
    for (auto& w : windows) assign(w, nextUid);
}

static Node* findIn(Node& n, int uid, Node** parent, Node* p) {
    if (n.uid == uid) { if (parent) *parent = p; return &n; }
    for (auto& c : n.children)
        if (Node* r = findIn(c, uid, parent, &n)) return r;
    return nullptr;
}
Node* Doc::find(int uid) {
    for (auto& w : windows)
        if (Node* r = findIn(w, uid, nullptr, nullptr)) return r;
    return nullptr;
}
Node* Doc::parentOf(int uid) {
    Node* p = nullptr;
    for (auto& w : windows)
        if (findIn(w, uid, &p, nullptr)) return p;
    return nullptr;
}
bool Doc::contains(int ancestorUid, int uid) {
    Node* a = find(ancestorUid);
    return a && findIn(*a, uid, nullptr, nullptr) != nullptr;
}

// ---------- JSON ----------
static std::string q(const std::string& s) { return "\"" + app::jsonEscape(s) + "\""; }
static std::string num(float v) {
    char b[32];
    snprintf(b, sizeof b, "%g", v);
    return b;
}
static std::string col(const ImVec4& c) { return "[" + num(c.x) + ", " + num(c.y) + ", " + num(c.z) + ", " + num(c.w) + "]"; }

static void writeNode(std::string& o, const Node& n, int depth) {
    std::string pad(depth * 2, ' ');
    o += pad + "{\"type\": " + q(kindName(n.kind)) + ", \"id\": " + q(n.id);
    if (!n.text.empty()) o += ", \"text\": " + q(n.text);
    if (n.x || n.y) o += ", \"x\": " + num(n.x) + ", \"y\": " + num(n.y);
    if (n.w || n.h) o += ", \"w\": " + num(n.w) + ", \"h\": " + num(n.h);
    if (!n.bind.empty()) o += ", \"bind\": " + q(n.bind);
    if (!n.component.empty()) o += ", \"component\": " + q(n.component);
    if (!n.tooltip.empty()) o += ", \"tooltip\": " + q(n.tooltip);
    if (n.kind == Kind::Slider || n.kind == Kind::Progress) o += ", \"min\": " + num(n.minV) + ", \"max\": " + num(n.maxV);
    if (!n.items.empty()) {
        o += ", \"items\": [";
        for (size_t i = 0; i < n.items.size(); ++i) o += (i ? ", " : "") + q(n.items[i]);
        o += "]";
    }
    if (n.hasColor) o += ", \"color\": " + col(n.color);
    if (n.hasBg) o += ", \"bg\": " + col(n.bg);
    if (isContainer(n.kind)) {
        if (n.spacing != 6) o += ", \"spacing\": " + num(n.spacing);
        if (n.padding != 8) o += ", \"padding\": " + num(n.padding);
    }
    if (n.rounding != 4) o += ", \"rounding\": " + num(n.rounding);
    if (!n.visible) o += ", \"visible\": false";
    if (!n.children.empty()) {
        o += ", \"children\": [\n";
        for (size_t i = 0; i < n.children.size(); ++i) {
            writeNode(o, n.children[i], depth + 1);
            o += i + 1 < n.children.size() ? ",\n" : "\n";
        }
        o += pad + "]";
    }
    o += "}";
}

std::string toJson(const Doc& d) {
    std::string o = "{\n  \"format\": \"wzui\", \"version\": 1,\n  \"name\": " + q(d.name) +
                    ",\n  \"showInEditor\": " + (d.showInEditor ? "true" : "false") + ",\n  \"windows\": [\n";
    for (size_t i = 0; i < d.windows.size(); ++i) {
        writeNode(o, d.windows[i], 2);
        o += i + 1 < d.windows.size() ? ",\n" : "\n";
    }
    return o + "  ]\n}\n";
}

static bool readCol(const Json& j, ImVec4& c) {
    if (j.type != Json::Array || j.a.size() < 3) return false;
    c = ImVec4((float)j[0].num(), (float)j[1].num(), (float)j[2].num(), (float)j[3].num(1));
    return true;
}
static bool readNode(const Json& j, Node& n, std::string* err) {
    if (!kindFromName(j["type"].str(), n.kind)) {
        if (err) *err = "type inconnu : \"" + j["type"].str() + "\"";
        return false;
    }
    n.id = j["id"].str();
    n.text = j["text"].str();
    n.tooltip = j["tooltip"].str();
    n.bind = j["bind"].str();
    n.component = j["component"].str();
    n.x = (float)j["x"].num(); n.y = (float)j["y"].num();
    n.w = (float)j["w"].num(); n.h = (float)j["h"].num();
    n.minV = (float)j["min"].num(0); n.maxV = (float)j["max"].num(1);
    for (const Json& it : j["items"].a) n.items.push_back(it.str());
    n.hasColor = readCol(j["color"], n.color);
    n.hasBg = readCol(j["bg"], n.bg);
    n.spacing = (float)j["spacing"].num(6);
    n.padding = (float)j["padding"].num(8);
    n.rounding = (float)j["rounding"].num(4);
    n.visible = j["visible"].type == Json::Bool ? j["visible"].b : true;
    for (const Json& c : j["children"].a) {
        n.children.emplace_back();
        if (!readNode(c, n.children.back(), err)) return false;
    }
    return true;
}

bool fromJson(const std::string& text, Doc& doc, std::string* err) {
    Json j;
    if (!Json::parse(text, j) || j.type != Json::Object) {
        if (err) *err = "JSON invalide";
        return false;
    }
    Doc d;
    d.name = j["name"].str("Interface");
    d.showInEditor = j["showInEditor"].b;
    for (const Json& w : j["windows"].a) {
        d.windows.emplace_back();
        if (!readNode(w, d.windows.back(), err)) return false;
        d.windows.back().kind = Kind::Window;
    }
    d.assignUids();
    doc = std::move(d);
    return true;
}

bool load(const std::string& path, Doc& doc, std::string* err) {
    std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
    if (!f) {
        if (err) *err = "fichier introuvable";
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return fromJson(ss.str(), doc, err);
}

bool save(const std::string& path, const Doc& doc) {
    std::ofstream f(std::filesystem::u8path(path), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << toJson(doc);
    return (bool)f;
}

} // namespace wzui
