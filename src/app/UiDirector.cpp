// UI Director: see UiDirector.h.
#include "UiDirector.h"
#include "Json.h"
#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;
using app::Json;
using app::jsonEscape;

namespace uidir {
namespace {

const char* kOverlay = "##UiDirector";

struct Override {
    std::string label;       // human readable name, kept to find the item again in the sources
    float dx = 0, dy = 0, dw = 0, dh = 0;   // reference pixels
    bool hasText = false;
    std::string text;
    bool empty() const { return dx == 0 && dy == 0 && dw == 0 && dh == 0 && !hasText; }
};

struct Item {
    ImGuiID id = 0;
    ImRect bb, clip;
    std::string label, window;
    bool placeable = false;
    float scale = 1;
};

struct Designation {
    std::string id, label, window, note, at;
    float x = 0, y = 0, w = 0, h = 0;
};

struct Placed { ImRect in, out; int frame = -1; float scale = 1; std::string text; };

struct State {
    bool active = false;
    fs::path root, layoutFile, designFile, designMd;
    std::map<ImGuiID, Override> layout;
    std::vector<Designation> designations;
    std::vector<Item> items, lastItems;                 // this frame / previous frame (complete)
    std::unordered_map<ImGuiID, Placed> placed;          // place() calls of the current frame
    ImGuiID hovered = 0, selected = 0;
    std::string selectedLabel, selectedWindow;
    std::vector<std::map<ImGuiID, Override>> undo;
    bool dragging = false, dirty = false;
    fs::file_time_type layoutTime{};
    double nextPoll = 0;
    char note[512] = "";
    char text[256] = "";
    ImGuiID textFor = 0;
    std::string toast;
    double toastUntil = 0;
} S;

std::string hex(ImGuiID id) { char b[16]; snprintf(b, sizeof b, "0x%08X", id); return b; }
ImGuiID parseId(const std::string& t) { return (ImGuiID)strtoul(t.c_str(), nullptr, 0); }
std::string q(const std::string& s) { return "\"" + jsonEscape(s) + "\""; }
std::string num(float v) { char b[32]; snprintf(b, sizeof b, "%g", v); return b; }
std::string readAll(const fs::path& p) { std::ifstream f(p, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }
void writeAll(const fs::path& p, const std::string& t) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << t;
}
std::string now() {
    std::time_t t = std::time(nullptr);
    char b[32];
    std::strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S", std::localtime(&t));
    return b;
}
void toast(const std::string& t) { S.toast = t; S.toastUntil = ImGui::GetTime() + 2.5; }

// ---------- files ----------
std::string layoutJson() {
    std::string o = "{\n  \"version\": 1,\n  \"note\": \"WZX UI Director: overrides of the editor UI, by ImGui ID (dx/dy/dw/dh in reference pixels).\",\n  \"items\": {";
    bool first = true;
    for (auto& [id, v] : S.layout) {
        if (v.empty()) continue;
        o += std::string(first ? "" : ",") + "\n    " + q(hex(id)) + ": {\"label\": " + q(v.label);
        if (v.dx) o += ", \"dx\": " + num(v.dx);
        if (v.dy) o += ", \"dy\": " + num(v.dy);
        if (v.dw) o += ", \"dw\": " + num(v.dw);
        if (v.dh) o += ", \"dh\": " + num(v.dh);
        if (v.hasText) o += ", \"text\": " + q(v.text);
        o += "}";
        first = false;
    }
    return o + "\n  }\n}\n";
}
void readLayout(const Json& j) {
    S.layout.clear();
    for (auto& [k, v] : j["items"].o) {
        Override o;
        o.label = v["label"].str();
        o.dx = (float)v["dx"].num(); o.dy = (float)v["dy"].num();
        o.dw = (float)v["dw"].num(); o.dh = (float)v["dh"].num();
        if (v["text"].type == Json::String) { o.hasText = true; o.text = v["text"].s; }
        S.layout[parseId(k)] = o;
    }
}
void loadLayout() {
    std::error_code ec;
    if (!fs::exists(S.layoutFile, ec)) { S.layout.clear(); return; }
    Json j;
    if (Json::parse(readAll(S.layoutFile), j)) readLayout(j);
    S.layoutTime = fs::last_write_time(S.layoutFile, ec);
}
void saveLayout() {
    writeAll(S.layoutFile, layoutJson());
    std::error_code ec;
    S.layoutTime = fs::last_write_time(S.layoutFile, ec);
    S.dirty = false;
}
void pushUndo() { S.undo.push_back(S.layout); if (S.undo.size() > 100) S.undo.erase(S.undo.begin()); }

std::string designationsJson() {
    std::string o = "[";
    for (size_t i = 0; i < S.designations.size(); ++i) {
        const Designation& d = S.designations[i];
        o += std::string(i ? "," : "") + "\n  {\"id\": " + q(d.id) + ", \"label\": " + q(d.label) + ", \"window\": " + q(d.window) +
             ", \"rect\": [" + num(d.x) + ", " + num(d.y) + ", " + num(d.w) + ", " + num(d.h) + "], \"note\": " + q(d.note) + ", \"at\": " + q(d.at) + "}";
    }
    return o + "\n]\n";
}
void saveDesignations() {
    writeAll(S.designFile, designationsJson());
    std::string md = "# Éléments d'interface désignés (WZX UI Director)\n\n"
                     "> Généré par le mode édition de l'interface (F9). Chaque entrée est un élément de l'éditeur désigné par l'utilisateur, avec sa consigne.\n"
                     "> Pour retrouver l'élément dans le code : chercher son libellé (`label`) dans `src/` ; le libellé est l'identifiant ImGui passé au widget\n"
                     "> (`Button(\"##play\", …)`, `InvisibleButton`, titre de panneau…). L'ID ImGui est le hash de ce libellé dans la pile d'ID de la fenêtre.\n"
                     "> Les déplacements faits à la souris sont dans `ui/layout.json` : pour les rendre définitifs, reporter les décalages dans le code et vider l'entrée.\n\n";
    if (S.designations.empty()) md += "_Aucun élément désigné._\n";
    for (size_t i = 0; i < S.designations.size(); ++i) {
        const Designation& d = S.designations[i];
        md += "## " + std::to_string(i + 1) + ". `" + d.label + "`\n\n";
        md += "- Consigne : " + (d.note.empty() ? std::string("(aucune, élément simplement désigné)") : d.note) + "\n";
        md += "- Fenêtre ImGui : `" + d.window + "` · ID : `" + d.id + "`\n";
        md += "- Rectangle à l'écran : x " + num(d.x) + ", y " + num(d.y) + ", " + num(d.w) + " × " + num(d.h) + " px\n";
        md += "- Désigné le " + d.at + "\n\n";
    }
    writeAll(S.designMd, md);
}
void loadDesignations() {
    Json j;
    std::error_code ec;
    if (!fs::exists(S.designFile, ec) || !Json::parse(readAll(S.designFile), j)) return;
    S.designations.clear();
    for (const Json& v : j.a) {
        Designation d;
        d.id = v["id"].str(); d.label = v["label"].str(); d.window = v["window"].str();
        d.note = v["note"].str(); d.at = v["at"].str();
        d.x = (float)v["rect"][0].num(); d.y = (float)v["rect"][1].num(); d.w = (float)v["rect"][2].num(); d.h = (float)v["rect"][3].num();
        S.designations.push_back(d);
    }
}

const Item* findItem(const std::vector<Item>& v, ImGuiID id) {
    for (auto it = v.rbegin(); it != v.rend(); ++it) if (it->id == id) return &*it;
    return nullptr;
}
void designate(const Item& it, const std::string& note) {
    Designation d;
    d.id = hex(it.id); d.label = it.label; d.window = it.window; d.note = note; d.at = now();
    ImRect r = it.clip;
    d.x = r.Min.x; d.y = r.Min.y; d.w = r.GetWidth(); d.h = r.GetHeight();
    S.designations.push_back(d);
    saveDesignations();
}
std::string markdownForClaude() { return readAll(S.designMd); }

bool isOwn(ImGuiContext* ctx) {
    ImGuiWindow* w = ctx->CurrentWindow;
    return w && w->RootWindow && strncmp(w->RootWindow->Name, kOverlay, strlen(kOverlay)) == 0;
}

// smallest visible item under the mouse (the most specific one)
ImGuiID pick(ImVec2 m, ImRect* out) {
    const Item* best = nullptr;
    float area = 1e30f;
    for (const Item& it : S.items) {
        if (!it.clip.Contains(m)) continue;
        float a = it.clip.GetWidth() * it.clip.GetHeight();
        if (a <= area) { area = a; best = &it; }   // <= : later (drawn on top) wins ties
    }
    if (best && out) *out = best->clip;
    return best ? best->id : 0;
}

Override& ov(const Item& it) {
    Override& o = S.layout[it.id];
    if (o.label.empty()) o.label = it.label;
    return o;
}

void drawInspector(const Item* sel) {
    ImGuiIO& io = ImGui::GetIO();
    float w = 340 * std::max(1.0f, io.FontGlobalScale);
    ImGui::SetCursorScreenPos(ImVec2(io.DisplaySize.x - w - 12, 48));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.09f, 0.11f, 0.96f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6);
    ImGui::BeginChild("##inspector", ImVec2(w, std::min(io.DisplaySize.y - 60, 620.0f)), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.40f, 1), "UI DIRECTOR");
    ImGui::SameLine();
    ImGui::TextDisabled("F9 pour quitter");
    ImGui::Separator();
    if (!sel) {
        ImGui::TextWrapped("Survolez puis cliquez un élément de l'interface pour le sélectionner.");
        ImGui::BulletText("Glisser : déplacer");
        ImGui::BulletText("Maj + glisser : redimensionner");
        ImGui::BulletText("Flèches : 1 px (Maj : 10 px)");
        ImGui::BulletText("Ctrl+Z : annuler");
    } else {
        ImGui::Text("%s", sel->label.empty() ? "(sans libellé)" : sel->label.c_str());
        ImGui::TextDisabled("%s · %s", hex(sel->id).c_str(), sel->window.c_str());
        ImGui::TextDisabled("%.0f, %.0f · %.0f × %.0f px", sel->clip.Min.x, sel->clip.Min.y, sel->clip.GetWidth(), sel->clip.GetHeight());
        ImGui::Spacing();
        if (sel->placeable) {
            Override& o = ov(*sel);
            bool edited = false;
            float pos[2] = {o.dx, o.dy}, size[2] = {o.dw, o.dh};
            if (ImGui::DragFloat2("Décalage", pos, 0.5f, -4000, 4000, "%.0f")) { o.dx = pos[0]; o.dy = pos[1]; edited = true; }
            if (ImGui::IsItemActivated()) pushUndo();
            if (ImGui::DragFloat2("Taille +", size, 0.5f, -4000, 4000, "%.0f")) { o.dw = size[0]; o.dh = size[1]; edited = true; }
            if (ImGui::IsItemActivated()) pushUndo();
            auto pl = S.placed.find(sel->id);
            if (pl != S.placed.end() && !pl->second.text.empty()) {
                if (S.textFor != sel->id) {
                    S.textFor = sel->id;
                    snprintf(S.text, sizeof S.text, "%s", o.hasText ? o.text.c_str() : pl->second.text.c_str());
                }
                if (ImGui::InputText("Texte", S.text, sizeof S.text)) { o.hasText = true; o.text = S.text; edited = true; }
            }
            if (ImGui::Button("Réinitialiser")) { pushUndo(); S.layout.erase(sel->id); S.textFor = 0; edited = true; }
            if (edited) S.dirty = true;   // written once the field is released (end of endFrame)
        } else {
            ImGui::TextWrapped("Élément non déplaçable : son code ne passe pas encore par uidir::place(). Il peut quand même être désigné pour Claude.");
        }
        ImGui::Spacing();
        ImGui::SeparatorText("Désigner pour Claude");
        ImGui::InputTextMultiline("##note", S.note, sizeof S.note, ImVec2(-1, 70));
        if (ImGui::Button("Désigner")) {
            designate(*sel, S.note);
            S.note[0] = 0;
            toast("Désigné : ui/DESIGNATIONS.md");
        }
    }
    ImGui::Spacing();
    ImGui::SeparatorText(("Désignations (" + std::to_string(S.designations.size()) + ")").c_str());
    int remove = -1;
    for (size_t i = 0; i < S.designations.size(); ++i) {
        const Designation& d = S.designations[i];
        ImGui::PushID((int)i);
        if (ImGui::SmallButton("x")) remove = (int)i;
        ImGui::SameLine();
        if (ImGui::Selectable((std::to_string(i + 1) + ". " + d.label).c_str(), hex(S.selected) == d.id)) S.selected = parseId(d.id);
        if (!d.note.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", d.note.c_str());
        ImGui::PopID();
    }
    if (remove >= 0) { S.designations.erase(S.designations.begin() + remove); saveDesignations(); }
    if (!S.designations.empty()) {
        if (ImGui::Button("Copier pour Claude")) { ImGui::SetClipboardText(markdownForClaude().c_str()); toast("Copié dans le presse-papiers"); }
        ImGui::SameLine();
        if (ImGui::Button("Tout effacer")) { S.designations.clear(); saveDesignations(); }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

} // namespace

void init(const std::wstring& projectRoot) {
    S.root = projectRoot;
    S.layoutFile = S.root / "ui" / "layout.json";
    S.designFile = S.root / "ui" / "designations.json";
    S.designMd = S.root / "ui" / "DESIGNATIONS.md";
    loadLayout();
    loadDesignations();
}
bool active() { return S.active; }
void setActive(bool on) {
    S.active = on;
    S.dragging = false;
    if (!on && S.dirty) saveLayout();
}

ImRect place(ImGuiID id, const ImRect& r, float scale, const char** text) {
    int frame = ImGui::GetFrameCount();
    auto pit = S.placed.find(id);
    if (pit != S.placed.end() && pit->second.frame == frame && pit->second.out.Min.x == r.Min.x && pit->second.out.Min.y == r.Min.y &&
        pit->second.out.Max.x == r.Max.x && pit->second.out.Max.y == r.Max.y) {
        auto o = S.layout.find(id);
        if (text && o != S.layout.end() && o->second.hasText) *text = o->second.text.c_str();
        return r;   // already placed by an outer helper
    }
    Placed& p = S.placed[id];
    p.frame = frame;
    p.in = r;
    p.scale = scale;
    p.text = text && *text ? *text : "";
    ImRect out = r;
    auto o = S.layout.find(id);
    if (o != S.layout.end()) {
        const Override& v = o->second;
        out.Min.x += v.dx * scale; out.Min.y += v.dy * scale;
        out.Max.x += (v.dx + v.dw) * scale; out.Max.y += (v.dy + v.dh) * scale;
        if (out.Max.x < out.Min.x + 1) out.Max.x = out.Min.x + 1;
        if (out.Max.y < out.Min.y + 1) out.Max.y = out.Min.y + 1;
        if (text && v.hasText) *text = v.text.c_str();
    }
    p.out = out;
    return out;
}

void declare(ImGuiID id, const ImRect& r, const char* label) {
    ImGuiContext* g = ImGui::GetCurrentContext();
    Item it;
    it.id = id;
    it.bb = it.clip = r;
    it.label = label ? label : "";
    it.window = g->CurrentWindow ? g->CurrentWindow->Name : "";
    if (g->CurrentWindow) it.clip.ClipWithFull(g->CurrentWindow->ClipRect);
    if (it.clip.GetWidth() > 0 && it.clip.GetHeight() > 0) S.items.push_back(it);
}

void beginFrame() {
    S.lastItems.swap(S.items);
    S.items.clear();
    ImGui::GetCurrentContext()->TestEngineHookItems = true;
}

void endFrame() {
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsKeyPressed(ImGuiKey_F9, false)) setActive(!S.active);

    // hot reload: ui/layout.json changed by another tool (Claude, an editor, git)
    double t = ImGui::GetTime();
    if (t >= S.nextPoll && !S.dragging) {
        S.nextPoll = t + 0.5;
        std::error_code ec;
        auto mt = fs::last_write_time(S.layoutFile, ec);
        if (!ec && mt != S.layoutTime) { loadLayout(); toast("ui/layout.json rechargé"); }
    }

    ImDrawList* fg = ImGui::GetForegroundDrawList();
    if (!S.toast.empty() && t < S.toastUntil) {
        ImVec2 z = ImGui::CalcTextSize(S.toast.c_str());
        ImVec2 a(io.DisplaySize.x * 0.5f - z.x * 0.5f - 12, io.DisplaySize.y - z.y - 40);
        fg->AddRectFilled(a, ImVec2(a.x + z.x + 24, a.y + z.y + 14), IM_COL32(20, 22, 26, 230), 6);
        fg->AddText(ImVec2(a.x + 12, a.y + 7), IM_COL32(240, 200, 140, 255), S.toast.c_str());
    }
    if (!S.active) return;

    // full-window overlay in front of everything: it takes the mouse, so the editor does not react while editing
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::SetNextWindowFocus();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin(kOverlay, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    ImGui::SetCursorScreenPos(ImVec2(0, 0));
    ImGui::InvisibleButton("##canvas", io.DisplaySize);
    bool canvasHovered = ImGui::IsItemHovered(), activated = ImGui::IsItemActivated(), held = ImGui::IsItemActive();
    bool released = ImGui::IsItemDeactivated();

    ImRect hr;
    S.hovered = canvasHovered ? pick(io.MousePos, &hr) : 0;
    if (activated) {
        S.selected = S.hovered;
        S.dragging = false;
        if (const Item* it = findItem(S.items, S.selected)) { S.selectedLabel = it->label; S.selectedWindow = it->window; }
    }
    const Item* sel = findItem(S.items, S.selected);
    if (!sel) sel = findItem(S.lastItems, S.selected);

    // drag = move, Shift+drag = resize
    if (sel && sel->placeable && held && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
        if (!S.dragging) { pushUndo(); S.dragging = true; }
        auto pl = S.placed.find(sel->id);
        float k = pl != S.placed.end() && pl->second.scale > 0 ? pl->second.scale : 1;
        Override& o = ov(*sel);
        if (io.KeyShift) { o.dw += io.MouseDelta.x / k; o.dh += io.MouseDelta.y / k; }
        else { o.dx += io.MouseDelta.x / k; o.dy += io.MouseDelta.y / k; }
        S.dirty = true;
    }
    if (released && S.dragging) {
        S.dragging = false;
        Override& o = S.layout[sel ? sel->id : S.selected];
        o.dx = std::round(o.dx); o.dy = std::round(o.dy); o.dw = std::round(o.dw); o.dh = std::round(o.dh);
        saveLayout();
    }
    if (sel && sel->placeable && !ImGui::IsAnyItemActive() && !io.WantTextInput) {
        float step = io.KeyShift ? 10.0f : 1.0f, dx = 0, dy = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) dx = -step;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) dx = step;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) dy = -step;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) dy = step;
        if (dx || dy) { pushUndo(); Override& o = ov(*sel); o.dx += dx; o.dy += dy; saveLayout(); }
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false) && !io.WantTextInput && !S.undo.empty()) {
        S.layout = S.undo.back();
        S.undo.pop_back();
        S.textFor = 0;
        saveLayout();
        toast("Annulé");
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) S.selected = 0;

    // designated items: numbered pins
    for (size_t i = 0; i < S.designations.size(); ++i) {
        const Item* it = findItem(S.items, parseId(S.designations[i].id));
        if (!it) continue;
        ImVec2 c(it->clip.Max.x, it->clip.Min.y);
        fg->AddRect(it->clip.Min, it->clip.Max, IM_COL32(120, 200, 255, 160), 3, 0, 1.0f);
        fg->AddCircleFilled(c, 8, IM_COL32(60, 140, 230, 255));
        std::string n = std::to_string(i + 1);
        ImVec2 z = ImGui::CalcTextSize(n.c_str());
        fg->AddText(ImVec2(c.x - z.x / 2, c.y - z.y / 2), IM_COL32_WHITE, n.c_str());
    }
    // hover and selection frames
    if (S.hovered && S.hovered != S.selected) {
        fg->AddRectFilled(hr.Min, hr.Max, IM_COL32(255, 180, 80, 28), 2);
        fg->AddRect(hr.Min, hr.Max, IM_COL32(255, 180, 80, 200), 2, 0, 1.0f);
        if (const Item* it = findItem(S.items, S.hovered)) {
            std::string tip = (it->label.empty() ? std::string("(sans libellé)") : it->label) + (it->placeable ? "  [déplaçable]" : "");
            ImVec2 z = ImGui::CalcTextSize(tip.c_str());
            ImVec2 a(hr.Min.x, std::max(0.0f, hr.Min.y - z.y - 6));
            fg->AddRectFilled(a, ImVec2(a.x + z.x + 10, a.y + z.y + 4), IM_COL32(30, 24, 16, 235), 3);
            fg->AddText(ImVec2(a.x + 5, a.y + 2), IM_COL32(255, 210, 150, 255), tip.c_str());
        }
    }
    if (sel) {
        ImU32 c = sel->placeable ? IM_COL32(110, 220, 140, 255) : IM_COL32(150, 170, 255, 255);
        fg->AddRect(sel->clip.Min, sel->clip.Max, c, 2, 0, 2.0f);
        if (sel->placeable) fg->AddRectFilled(ImVec2(sel->clip.Max.x - 5, sel->clip.Max.y - 5), ImVec2(sel->clip.Max.x + 5, sel->clip.Max.y + 5), c);
    }
    // banner
    const char* banner = "MODE ÉDITION UI  ·  F9 pour quitter";
    ImVec2 z = ImGui::CalcTextSize(banner);
    fg->AddRectFilled(ImVec2(io.DisplaySize.x * 0.5f - z.x * 0.5f - 14, 6), ImVec2(io.DisplaySize.x * 0.5f + z.x * 0.5f + 14, 12 + z.y), IM_COL32(230, 150, 50, 235), 4);
    fg->AddText(ImVec2(io.DisplaySize.x * 0.5f - z.x * 0.5f, 9), IM_COL32(25, 18, 8, 255), banner);

    drawInspector(sel);
    ImGui::End();
    if (S.dirty && !S.dragging && !ImGui::IsAnyItemActive()) saveLayout();
}

std::string command(const Json& j, int& status) {
    std::string cmd = j["cmd"].str();
    auto fail = [&](const std::string& e) { status = 400; return "{\"ok\": false, \"error\": " + q(e) + "}"; };
    if (cmd == "ui.edit") {
        if (j["on"].type == Json::Bool) setActive(j["on"].b);
        return std::string("{\"ok\": true, \"active\": ") + (S.active ? "true" : "false") + "}";
    }
    if (cmd == "ui.items") {
        // items of the last complete frame; optional filter on the label or window
        std::string f = j["filter"].str();
        std::string o = "{\"ok\": true, \"items\": [";
        bool first = true;
        int n = 0;
        for (const Item& it : S.lastItems) {
            if (!f.empty() && it.label.find(f) == std::string::npos && it.window.find(f) == std::string::npos) continue;
            if (++n > 2000) break;
            o += std::string(first ? "" : ",") + "{\"id\": " + q(hex(it.id)) + ", \"label\": " + q(it.label) + ", \"window\": " + q(it.window) +
                 ", \"rect\": [" + num(it.clip.Min.x) + ", " + num(it.clip.Min.y) + ", " + num(it.clip.GetWidth()) + ", " + num(it.clip.GetHeight()) +
                 "], \"placeable\": " + (it.placeable ? "true" : "false") + "}";
            first = false;
        }
        return o + "]}";
    }
    if (cmd == "ui.layout.get") return "{\"ok\": true, \"path\": " + q(S.layoutFile.u8string()) + ", \"layout\": " + layoutJson() + "}";
    if (cmd == "ui.layout.set") {
        // {id: "0x...", dx?, dy?, dw?, dh?, text?, reset?}
        ImGuiID id = parseId(j["id"].str());
        if (!id) return fail("id manquant (\"0x...\", voir ui.items)");
        pushUndo();
        if (j["reset"].b) S.layout.erase(id);
        else {
            Override& o = S.layout[id];
            if (const Item* it = findItem(S.lastItems, id)) if (o.label.empty()) o.label = it->label;
            if (j["dx"].type == Json::Number) o.dx = (float)j["dx"].n;
            if (j["dy"].type == Json::Number) o.dy = (float)j["dy"].n;
            if (j["dw"].type == Json::Number) o.dw = (float)j["dw"].n;
            if (j["dh"].type == Json::Number) o.dh = (float)j["dh"].n;
            if (j["text"].type == Json::String) { o.hasText = true; o.text = j["text"].s; }
        }
        saveLayout();
        return "{\"ok\": true}";
    }
    if (cmd == "ui.designations") {
        if (j["clear"].b) { S.designations.clear(); saveDesignations(); }
        return "{\"ok\": true, \"path\": " + q(S.designMd.u8string()) + ", \"designations\": " + designationsJson() + "}";
    }
    if (cmd == "ui.designate") {
        ImGuiID id = parseId(j["id"].str());
        const Item* it = findItem(S.lastItems, id);
        if (!it) return fail("élément introuvable dans la dernière image (voir ui.items)");
        designate(*it, j["note"].str());
        return "{\"ok\": true, \"count\": " + std::to_string(S.designations.size()) + "}";
    }
    return "";
}

} // namespace uidir

// ---------- Dear ImGui test-engine hooks (IMGUI_ENABLE_TEST_ENGINE): every item of every window ----------
void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx, ImGuiID id, const ImRect& bb, const ImGuiLastItemData*) {
    using namespace uidir;
    if (!id || uidir::isOwn(ctx) || !ctx->CurrentWindow) return;
    Item it;
    it.id = id;
    it.bb = it.clip = bb;
    it.clip.ClipWithFull(ctx->CurrentWindow->ClipRect);
    if (it.clip.GetWidth() <= 0 || it.clip.GetHeight() <= 0) return;   // scrolled out / clipped away
    it.window = ctx->CurrentWindow->Name;
    auto p = S.placed.find(id);
    if (p != S.placed.end() && p->second.frame == ctx->FrameCount) { it.placeable = true; it.scale = p->second.scale; }
    S.items.push_back(std::move(it));
}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext*, ImGuiID id, const char* label, ImGuiItemStatusFlags) {
    using namespace uidir;
    if (!label || S.items.empty() || S.items.back().id != id) return;
    S.items.back().label = label;
}
void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID id) {
    for (auto it = uidir::S.lastItems.rbegin(); it != uidir::S.lastItems.rend(); ++it)
        if (it->id == id) return it->label.c_str();
    return nullptr;
}
