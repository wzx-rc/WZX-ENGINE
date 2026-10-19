#include "Equivalences.h"
#include "MayaLink.h"
#include "app/Theme.h"
#include "game/Graph.h"
#include "game/NodeEditor.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace mayashader {

using app::Json;
using game::NodeDef;
using game::PinDef;
using game::PinType;

namespace {

std::string utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

long long fileTime(const std::wstring& p) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &a)) return 0;
    return ((long long)a.ftLastWriteTime.dwHighDateTime << 32) | a.ftLastWriteTime.dwLowDateTime;
}

Json num(double v) { Json j; j.type = Json::Number; j.n = v; return j; }
Json str(const std::string& s) { Json j; j.type = Json::String; j.s = s; return j; }
Json boolean(bool b) { Json j; j.type = Json::Bool; j.b = b; return j; }

// pins shown in the dashboard: no channel (R, G, B) pins, no compound "+" pins
bool listed(const PinDef& p) { return p.parent < 0 && !p.wildcard; }

// attribute text of a rule entry: "attr", or the attributes of "attrs" joined by commas
std::string attrText(const Json& e) {
    if (e.type == Json::String) return e.s;
    if (e.type != Json::Object) return "";
    if (e["attrs"].type == Json::Array) {
        std::string s;
        for (const Json& a : e["attrs"].a) s += (s.empty() ? "" : ", ") + a.str();
        return s;
    }
    return e["attr"].str();
}

bool hasRules(const Json& e) {
    if (e.type != Json::Object) return false;
    for (const char* k : {"scale", "offset", "invert01", "clamp", "power", "value_map", "link_only", "vector"})
        if (e.o.count(k)) return true;
    return false;
}

} // namespace

std::string jsonText(const Json& j, int indent) {
    std::string pad(indent + 1, ' '), end(indent, ' ');
    char b[64];
    switch (j.type) {
    case Json::Null: return "null";
    case Json::Bool: return j.b ? "true" : "false";
    case Json::Number:
        snprintf(b, sizeof b, "%.6g", j.n);
        return b;
    case Json::String: return "\"" + app::jsonEscape(j.s) + "\"";
    case Json::Array: {
        if (j.a.empty()) return "[]";
        bool flat = true;
        for (const Json& v : j.a) flat &= v.type != Json::Object && v.type != Json::Array;
        std::string s = "[";
        for (size_t i = 0; i < j.a.size(); ++i) s += (i ? (flat ? ", " : ",\n" + pad) : (flat ? "" : "\n" + pad)) + jsonText(j.a[i], indent + 1);
        return s + (flat ? "]" : "\n" + end + "]");
    }
    case Json::Object: {
        if (j.o.empty()) return "{}";
        std::string s = "{";
        bool first = true;
        for (const auto& kv : j.o) {
            s += (first ? "\n" : ",\n") + pad + "\"" + app::jsonEscape(kv.first) + "\": " + jsonText(kv.second, indent + 1);
            first = false;
        }
        return s + "\n" + end + "}";
    }
    }
    return "null";
}

bool Equivalences::load(const std::wstring& path) {
    m_path = path;
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    m_checkPath = std::wstring(tmp) + L"wzx_shader_check.json";
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    m_loaded = f.good() || !ss.str().empty() ? Json::parse(ss.str(), m_doc) && m_doc.type == Json::Object : false;
    if (!m_loaded) {   // a new file: the three targets, no rule yet
        m_doc = Json();
        m_doc.type = Json::Object;
        m_doc.o["format"] = str("wzx-equivalences");
        m_doc.o["version"] = num(1);
        Json targets;
        targets.type = Json::Array;
        for (auto t : {std::make_pair("maya", "Maya / Arnold"), std::make_pair("blender", "Blender / Cycles"), std::make_pair("unreal", "Unreal")}) {
            Json o;
            o.type = Json::Object;
            o.o["id"] = str(t.first);
            o.o["label"] = str(t.second);
            targets.a.push_back(o);
        }
        m_doc.o["targets"] = targets;
        m_doc.o["nodes"].type = Json::Object;
        m_status = "New equivalences file";
    }
    m_dirty = false;
    return m_loaded;
}

bool Equivalences::save() {
    std::ofstream f(m_path, std::ios::binary);
    if (!f) { m_status = "Save failed: " + utf8(m_path); return false; }
    f << jsonText(m_doc) << "\n";
    m_dirty = false;
    m_status = "Saved";
    return true;
}

Json& Equivalences::rule(const std::string& nodeType, const std::string& target) {
    Json& nodes = m_doc.o["nodes"];
    nodes.type = Json::Object;
    Json& n = nodes.o[nodeType];
    n.type = Json::Object;
    Json& r = n.o[target];
    if (r.type != Json::Object) r.type = Json::Object;
    return r;
}

int Equivalences::coverage(const std::string& nodeType, const std::string& target) const {
    const Json& r = m_doc["nodes"][nodeType][target];
    if (r.type != Json::Object || r["type"].str().empty()) return 0;
    const NodeDef* d = game::findNodeDef(nodeType);
    if (!d) return 0;
    int total = 0, mapped = 0;
    for (int side = 0; side < 2; ++side)
        for (const PinDef& p : side ? d->outs : d->ins) {
            if (!listed(p)) continue;
            ++total;
            if (!attrText(r[side ? "outs" : "ins"][p.name]).empty()) ++mapped;
        }
    return mapped == total ? 2 : 1;
}

void Equivalences::pollCheck() {
    if (!m_checkPending) return;
    long long t = fileTime(m_checkPath);
    if (t <= m_checkTime) return;
    std::ifstream f(m_checkPath, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    Json j;
    if (!Json::parse(ss.str(), j)) return;   // still being written
    m_check = j;
    m_checkPending = false;
    int bad = 0;
    for (const auto& kv : m_check.o)
        for (const char* side : {"ins", "outs"})
            for (const auto& p : kv.second[side].o) bad += p.second.type == Json::Bool && !p.second.b;
    m_status = bad ? "Maya check: " + std::to_string(bad) + " attribute(s) not found (red)" : "Maya check: every attribute exists";
}

void Equivalences::drawPinCell(const std::string& nodeType, const std::string& target, const std::string& pin, bool output, float w,
                               float dpi) {
    Json& r = rule(nodeType, target);
    Json& side = r.o[output ? "outs" : "ins"];
    if (side.type != Json::Object) side.type = Json::Object;
    auto it = side.o.find(pin);
    Json empty;
    Json& e = it != side.o.end() ? it->second : empty;
    // Maya check result: red when the attribute does not exist
    if (target == "maya" && m_check.type == Json::Object) {
        const Json& c = m_check[nodeType][output ? "outs" : "ins"][pin];
        if (c.type == Json::Bool)
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, c.b ? IM_COL32(60, 140, 80, 50) : IM_COL32(200, 60, 60, 90));
    }
    char buf[256] = {};
    strncpy_s(buf, attrText(e).c_str(), _TRUNCATE);
    ImGui::SetNextItemWidth(output ? -FLT_MIN : w - 26 * dpi);
    if (ImGui::InputTextWithHint("##attr", "-", buf, sizeof buf)) {
        std::string s = buf;
        m_dirty = true;
        if (e.type == Json::Object) {
            if (e.o.count("attrs") || s.find(',') != std::string::npos) {   // several attributes: comma separated
                Json arr;
                arr.type = Json::Array;
                std::stringstream in(s);
                std::string part;
                while (std::getline(in, part, ',')) {
                    part.erase(0, part.find_first_not_of(' '));
                    part.erase(part.find_last_not_of(' ') + 1);
                    if (!part.empty()) arr.a.push_back(str(part));
                }
                e.o.erase("attr");
                e.o["attrs"] = arr;
            } else e.o["attr"] = str(s);
            side.o[pin] = e;
        } else if (s.empty()) side.o.erase(pin);
        else side.o[pin] = str(s);
    }
    if (output) return;
    // value rules of the pin
    ImGui::SameLine(0, 2 * dpi);
    const bool rules = hasRules(e);
    if (rules) ImGui::PushStyleColor(ImGuiCol_Text, app::th::accentTextV());
    if (ImGui::Button("f##rules", ImVec2(22 * dpi, 0))) ImGui::OpenPopup("##rulespop");
    if (rules) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Value rules: scale, offset, invert, clamp, power, connection only");
    if (ImGui::BeginPopup("##rulespop")) {
        Json o = e;
        if (o.type != Json::Object) {   // a plain attribute becomes {"attr": ...}
            std::string a = attrText(e);
            o = Json();
            o.type = Json::Object;
            if (!a.empty()) o.o["attr"] = str(a);
        }
        bool changed = false;
        ImGui::TextDisabled("%s  >  %s", pin.c_str(), target.c_str());
        float scale = (float)o["scale"].num(1), offset = (float)o["offset"].num(0);
        ImGui::SetNextItemWidth(160 * dpi);
        if (ImGui::DragFloat("Scale", &scale, 0.01f)) { o.o["scale"] = num(scale); changed = true; }
        ImGui::SetNextItemWidth(160 * dpi);
        if (ImGui::DragFloat("Offset", &offset, 0.01f)) { o.o["offset"] = num(offset); changed = true; }
        bool inv = o["invert01"].type == Json::Bool && o["invert01"].b;
        if (ImGui::Checkbox("Invert (1 - x)", &inv)) { if (inv) o.o["invert01"] = boolean(true); else o.o.erase("invert01"); changed = true; }
        bool clamp = o["clamp"].type == Json::Array;
        if (ImGui::Checkbox("Clamp", &clamp)) {
            if (clamp) { Json a; a.type = Json::Array; a.a = {num(0), num(1)}; o.o["clamp"] = a; }
            else o.o.erase("clamp");
            changed = true;
        }
        if (clamp) {
            float lo = (float)o["clamp"][0].num(0), hi = (float)o["clamp"][1].num(1);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110 * dpi);
            float v2[2] = {lo, hi};
            if (ImGui::DragFloat2("##clamp", v2, 0.01f)) { Json a; a.type = Json::Array; a.a = {num(v2[0]), num(v2[1])}; o.o["clamp"] = a; changed = true; }
        }
        bool pw = o["power"].type == Json::Number;
        if (ImGui::Checkbox("Power", &pw)) { if (pw) o.o["power"] = num(1); else o.o.erase("power"); changed = true; }
        if (pw) {
            float p = (float)o["power"].num(1);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110 * dpi);
            if (ImGui::DragFloat("##pow", &p, 0.01f, 0.01f, 10.0f)) { o.o["power"] = num(p); changed = true; }
        }
        bool lo = o["link_only"].type == Json::Bool && o["link_only"].b;
        if (ImGui::Checkbox("Connection only (no value)", &lo)) { if (lo) o.o["link_only"] = boolean(true); else o.o.erase("link_only"); changed = true; }
        bool vec = o["vector"].type == Json::Bool && o["vector"].b;
        if (ImGui::Checkbox("Value copied to X, Y, Z", &vec)) { if (vec) o.o["vector"] = boolean(true); else o.o.erase("vector"); changed = true; }
        if (o["value_map"].type == Json::Object) ImGui::TextDisabled("value map: %d entries (edit in the JSON)", (int)o["value_map"].o.size());
        if (ImGui::Button("Reset rules")) {
            std::string a = attrText(o);
            o = Json();
            o.type = Json::Object;
            if (!a.empty()) o.o["attr"] = str(a);
            changed = true;
        }
        if (changed) {
            // back to a plain attribute when no rule is left
            if (o.o.size() == 1 && o.o.count("attr")) side.o[pin] = o.o["attr"];
            else side.o[pin] = o;
            m_dirty = true;
        }
        ImGui::EndPopup();
    }
}

void Equivalences::draw(ImVec2 pos, ImVec2 size, float dpi, const std::function<bool(const std::string&)>& send, bool mayaConnected) {
    pollCheck();
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("##equivalences", nullptr, flags);
    const Json& targets = m_doc["targets"];
    std::vector<std::pair<std::string, std::string>> tg;
    for (const Json& t : targets.a) tg.push_back({t["id"].str(), t["label"].str()});

    // header: file, save / reload / check, coverage per target
    ImGui::TextUnformatted("Equivalences");
    ImGui::SameLine();
    ImGui::TextDisabled("%s%s", utf8(m_path).c_str(), m_dirty ? "  (modified)" : "");
    if (ImGui::Button("Save")) save();
    ImGui::SameLine();
    if (ImGui::Button("Reload")) { load(m_path); m_status = "Reloaded"; }
    ImGui::SameLine();
    ImGui::BeginDisabled(!mayaConnected);
    if (ImGui::Button("Check in Maya")) {
        requestCheck(send);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Creates each Maya node once and checks that every mapped attribute exists (red = not found)");
    for (auto& t : tg) {
        int full = 0, part = 0, n = 0;
        for (const NodeDef& d : game::nodeDefs()) {
            if (d.category == "Compounds") continue;
            ++n;
            int c = coverage(d.id, t.first);
            full += c == 2;
            part += c == 1;
        }
        ImGui::SameLine(0, 18 * dpi);
        ImGui::TextColored(app::th::dimV(), "%s", t.second.c_str());
        ImGui::SameLine();
        ImGui::Text("%d / %d", full, n);
        if (part) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.86f, 0.65f, 0.39f, 1), "+%d partial", part); }
    }
    if (!m_status.empty()) { ImGui::SameLine(0, 18 * dpi); ImGui::TextDisabled("%s", m_status.c_str()); }
    ImGui::Separator();

    // node list (left) with a coverage dot per target
    const float listW = 230 * dpi;
    ImGui::BeginChild("##eqnodes", ImVec2(listW, 0), ImGuiChildFlags_Borders);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##eqfilter", "Filter nodes...", m_filter, sizeof m_filter);
    std::string q = m_filter;
    for (char& c : q) c = (char)tolower((unsigned char)c);
    std::string lastCat;
    for (const NodeDef& d : game::nodeDefs()) {
        if (d.category == "Compounds") continue;
        std::string l = d.label;
        for (char& c : l) c = (char)tolower((unsigned char)c);
        if (!q.empty() && l.find(q) == std::string::npos) continue;
        if (d.category != lastCat) { ImGui::TextColored(ImColor(game::categoryAccentColor(d.category)), "%s", d.category.c_str()); lastCat = d.category; }
        ImGui::PushID(d.id.c_str());
        ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable("##n", m_selected == d.id, 0, ImVec2(0, 20 * dpi))) m_selected = d.id;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddText(ImVec2(p.x + 6 * dpi, p.y + 2 * dpi), app::th::text(), d.label.c_str());
        float x = p.x + listW - 20 * dpi - 14 * dpi * tg.size();
        for (auto& t : tg) {
            int c = coverage(d.id, t.first);
            ImU32 col = c == 2 ? IM_COL32(110, 190, 120, 255) : c == 1 ? IM_COL32(220, 165, 90, 255) : IM_COL32(110, 110, 115, 255);
            dl->AddCircleFilled(ImVec2(x, p.y + 10 * dpi), 4 * dpi, col);
            x += 14 * dpi;
        }
        ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Dots: one per target");
    ImGui::TextDisabled("green full, orange partial, grey none");
    ImGui::EndChild();
    ImGui::SameLine();

    // rules of the selected node: a column per target
    ImGui::BeginChild("##eqrules", ImVec2(0, 0));
    const NodeDef* d = game::findNodeDef(m_selected);
    if (d) {
        ImGui::TextColored(ImColor(game::categoryAccentColor(d->category)), "%s", d->label.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s  -  %s", d->id.c_str(), d->help.c_str());
        const int cols = 1 + (int)tg.size();
        if (ImGui::BeginTable("##eqtable", cols, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterH |
                                                     ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupScrollFreeze(1, 2);
            ImGui::TableSetupColumn("Pin", ImGuiTableColumnFlags_WidthFixed, 170 * dpi);
            for (auto& t : tg) ImGui::TableSetupColumn(t.second.c_str(), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            // node type (and Maya creation class)
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("NODE TYPE");
            for (size_t k = 0; k < tg.size(); ++k) {
                ImGui::TableSetColumnIndex((int)k + 1);
                ImGui::PushID((int)k);
                Json& r = rule(d->id, tg[k].first);
                if (tg[k].first == "maya" && m_check.type == Json::Object && m_check[d->id]["type_ok"].type == Json::Bool)
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, m_check[d->id]["type_ok"].b ? IM_COL32(60, 140, 80, 50) : IM_COL32(200, 60, 60, 90));
                char buf[128] = {};
                strncpy_s(buf, r["type"].str().c_str(), _TRUNCATE);
                ImGui::SetNextItemWidth(tg[k].first == "maya" ? -96 * dpi : -FLT_MIN);
                if (ImGui::InputTextWithHint("##type", "not mapped", buf, sizeof buf)) {
                    if (buf[0]) r.o["type"] = str(buf);
                    else r.o.erase("type");
                    m_dirty = true;
                }
                if (tg[k].first == "maya") {   // how Hypershade creates it
                    static const char* classes[] = {"shader", "texture", "texture2d", "utility", "sg"};
                    std::string cur = r["class"].str("utility");
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::BeginCombo("##class", cur.c_str())) {
                        for (const char* c : classes)
                            if (ImGui::Selectable(c, cur == c)) { r.o["class"] = str(c); m_dirty = true; }
                        ImGui::EndCombo();
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Creation in Maya: shader, texture, 2D texture (with place2dTexture), utility, shading group");
                }
                ImGui::PopID();
            }
            // pins: inputs, then outputs
            for (int side = 0; side < 2; ++side) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled(side ? "OUTPUTS" : "INPUTS");
                for (const PinDef& p : side ? d->outs : d->ins) {
                    if (!listed(p)) continue;
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::PushID((p.name + (side ? "#o" : "#i")).c_str());
                    const char* tn = p.type == PinType::Vec3 ? "color" : p.type == PinType::Object ? "shader" : game::pinTypeName(p.type);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(p.name.c_str());
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", tn);
                    for (size_t k = 0; k < tg.size(); ++k) {
                        ImGui::TableSetColumnIndex((int)k + 1);
                        ImGui::PushID((int)k);
                        drawPinCell(d->id, tg[k].first, p.name, side == 1, ImGui::GetContentRegionAvail().x, dpi);
                        ImGui::PopID();
                    }
                    ImGui::PopID();
                }
            }
            // constant attributes of the rule (read only here)
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("FIXED VALUES");
            for (size_t k = 0; k < tg.size(); ++k) {
                ImGui::TableSetColumnIndex((int)k + 1);
                const Json& fx = m_doc["nodes"][d->id][tg[k].first]["fixed"];
                std::string s;
                for (const auto& kv : fx.o) s += (s.empty() ? "" : ", ") + kv.first + " = " + jsonText(kv.second);
                ImGui::TextDisabled("%s", s.empty() ? "-" : s.c_str());
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace mayashader

namespace mayashader {
bool Equivalences::requestCheck(const std::function<bool(const std::string&)>& send) {
    if (m_dirty) save();
    m_checkTime = fileTime(m_checkPath);
    m_checkPending = send("import wzx_shader_bridge as _w; _w.check_mappings(" + pyStr(utf8(m_checkPath)) + ", " + pyStr(utf8(m_path)) + ")");
    m_status = m_checkPending ? "Checking in Maya..." : "Maya not connected";
    return m_checkPending;
}
} // namespace mayashader
