#include "UiRuntime.h"
#include "Component.h"
#include <algorithm>

namespace wzui {

bool& Bindings::b(const std::string& k) { auto it = bools.find(k); return it != bools.end() && it->second ? *it->second : m_b[k]; }
float& Bindings::f(const std::string& k) { auto it = floats.find(k); return it != floats.end() && it->second ? *it->second : m_f[k]; }
int& Bindings::i(const std::string& k) { auto it = ints.find(k); return it != ints.end() && it->second ? *it->second : m_i[k]; }
std::string& Bindings::s(const std::string& k) { auto it = strings.find(k); return it != strings.end() && it->second ? *it->second : m_s[k]; }
ImVec4& Bindings::c(const std::string& k) {
    auto it = colors.find(k);
    if (it != colors.end() && it->second) return *it->second;
    auto m = m_c.find(k);
    if (m == m_c.end()) m = m_c.emplace(k, ImVec4(0.40f, 0.73f, 0.82f, 1)).first;
    return m->second;
}

namespace {

int stringResize(ImGuiInputTextCallbackData* d) {
    if (d->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* s = (std::string*)d->UserData;
        s->resize(d->BufTextLen);
        d->Buf = s->data();
    }
    return 0;
}
bool inputString(const char* label, std::string& s) {
    if (s.capacity() < 64) s.reserve(64);
    return ImGui::InputText(label, s.data(), s.capacity() + 1, ImGuiInputTextFlags_CallbackResize, stringResize, &s);
}
const std::string& key(const Node& n) { return n.bind.empty() ? n.id : n.bind; }
const char* label(const Node& n) { return n.text.empty() ? "##w" : n.text.c_str(); }

} // namespace

float titleBarHeight(float) { return ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2; }

ImVec2 drawNode(Node& n, ImVec2 pos, DrawCtx& c) {
    if (!n.visible && !c.design) return ImVec2(0, 0);
    const float s = c.scale;
    const float W = n.w * s, H = n.h * s;
    Bindings& B = *c.bindings;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    size_t hitIndex = c.hits.size();
    c.hits.push_back({n.uid, ImRect(pos, pos), ImRect(pos, pos)});

    ImGui::PushID(n.id.empty() ? std::to_string(n.uid).c_str() : n.id.c_str());
    if (!n.visible) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.35f);
    if (n.hasColor) ImGui::PushStyleColor(ImGuiCol_Text, n.color);
    ImVec2 size(0, 0);
    bool item = true;   // the rectangle comes from the last ImGui item

    switch (n.kind) {
    case Kind::Window:   // as a child of something else: drawn like a panel
    case Kind::Panel: {
        item = false;
        size = ImVec2(W > 0 ? W : 200 * s, H > 0 ? H : 120 * s);
        ImVec2 mx(pos.x + size.x, pos.y + size.y);
        if (n.hasBg) dl->AddRectFilled(pos, mx, ImGui::GetColorU32(n.bg), n.rounding * s);
        dl->AddRect(pos, mx, ImGui::GetColorU32(ImGuiCol_Border), n.rounding * s);
        ImVec2 origin(pos.x + n.padding * s, pos.y + n.padding * s);
        if (!n.text.empty()) {
            dl->AddText(origin, ImGui::GetColorU32(ImGuiCol_Text), n.text.c_str());
            origin.y += ImGui::GetFontSize() + n.spacing * s;
        }
        c.hits[hitIndex].content = ImRect(origin, mx);
        dl->PushClipRect(pos, mx, true);
        ImGui::PushClipRect(pos, mx, true);
        for (Node& ch : n.children) drawNode(ch, ImVec2(origin.x + ch.x * s, origin.y + ch.y * s), c);
        ImGui::PopClipRect();
        dl->PopClipRect();
        ImGui::SetCursorScreenPos(pos);
        ImGui::Dummy(size);   // reserves the area (window scrolling)
        break;
    }
    case Kind::Row:
    case Kind::Column: {
        item = false;
        const bool row = n.kind == Kind::Row;
        ImVec2 cur = pos;
        float extent = 0, along = 0;
        int count = 0;
        for (Node& ch : n.children) {
            ImVec2 z = drawNode(ch, cur, c);
            if (z.x == 0 && z.y == 0) continue;
            ++count;
            if (row) { cur.x += z.x + n.spacing * s; extent = std::max(extent, z.y); along = cur.x - pos.x - n.spacing * s; }
            else { cur.y += z.y + n.spacing * s; extent = std::max(extent, z.x); along = cur.y - pos.y - n.spacing * s; }
        }
        if (!count) along = extent = 24 * s;   // empty: still visible / droppable in the Designer
        size = row ? ImVec2(std::max(W, along), std::max(H, extent)) : ImVec2(std::max(W, extent), std::max(H, along));
        c.hits[hitIndex].content = ImRect(pos, ImVec2(pos.x + size.x, pos.y + size.y));
        if (c.design) dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(120, 160, 255, 70), 2 * s);
        break;
    }
    case Kind::Button: {
        ImGui::SetCursorScreenPos(pos);
        if (n.hasBg) {
            ImGui::PushStyleColor(ImGuiCol_Button, n.bg);
            ImVec4 h(n.bg.x * 1.2f, n.bg.y * 1.2f, n.bg.z * 1.2f, n.bg.w);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, h);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, h);
        }
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, n.rounding * s);
        if (ImGui::Button(label(n), ImVec2(W, H)) && !c.design) {
            auto it = B.actions.find(key(n));
            if (it != B.actions.end() && it->second) it->second();
            else if (B.onUnboundAction) B.onUnboundAction(key(n));
        }
        ImGui::PopStyleVar();
        if (n.hasBg) ImGui::PopStyleColor(3);
        break;
    }
    case Kind::Label: {
        ImGui::SetCursorScreenPos(pos);
        if (W > 0) ImGui::PushTextWrapPos(pos.x + W - ImGui::GetWindowPos().x);
        const std::string& t = n.bind.empty() ? n.text : B.s(n.bind);
        ImGui::TextUnformatted(t.empty() && c.design ? "(vide)" : t.c_str());
        if (W > 0) ImGui::PopTextWrapPos();
        break;
    }
    case Kind::Checkbox:
        ImGui::SetCursorScreenPos(pos);
        ImGui::Checkbox(label(n), &B.b(key(n)));
        break;
    case Kind::Slider:
        ImGui::SetCursorScreenPos(pos);
        ImGui::SetNextItemWidth(W > 0 ? W : 160 * s);
        ImGui::SliderFloat(label(n), &B.f(key(n)), n.minV, n.maxV);
        break;
    case Kind::Input:
        ImGui::SetCursorScreenPos(pos);
        ImGui::SetNextItemWidth(W > 0 ? W : 160 * s);
        inputString(label(n), B.s(key(n)));
        break;
    case Kind::Combo: {
        ImGui::SetCursorScreenPos(pos);
        ImGui::SetNextItemWidth(W > 0 ? W : 160 * s);
        int& v = B.i(key(n));
        const char* preview = v >= 0 && v < (int)n.items.size() ? n.items[v].c_str() : "";
        if (ImGui::BeginCombo(label(n), preview)) {
            for (int k = 0; k < (int)n.items.size(); ++k)
                if (ImGui::Selectable(n.items[k].c_str(), v == k)) v = k;
            ImGui::EndCombo();
        }
        break;
    }
    case Kind::Color:
        ImGui::SetCursorScreenPos(pos);
        ImGui::SetNextItemWidth(W > 0 ? W : 160 * s);
        ImGui::ColorEdit4(label(n), &B.c(key(n)).x, ImGuiColorEditFlags_NoInputs);
        break;
    case Kind::Progress: {
        ImGui::SetCursorScreenPos(pos);
        float v = B.f(key(n));
        float t = n.maxV != n.minV ? (v - n.minV) / (n.maxV - n.minV) : 0;
        ImGui::ProgressBar(std::clamp(t, 0.0f, 1.0f), ImVec2(W > 0 ? W : 160 * s, H > 0 ? H : 0), n.text.empty() ? nullptr : n.text.c_str());
        break;
    }
    case Kind::Separator: {
        ImGui::SetCursorScreenPos(pos);
        float w = W > 0 ? W : 160 * s, h = H > 0 ? H : 9 * s;
        ImGui::Dummy(ImVec2(w, h));
        dl->AddLine(ImVec2(pos.x, pos.y + h / 2), ImVec2(pos.x + w, pos.y + h / 2), ImGui::GetColorU32(ImGuiCol_Separator));
        break;
    }
    case Kind::Spacer:
        ImGui::SetCursorScreenPos(pos);
        ImGui::Dummy(ImVec2(W > 0 ? W : 16 * s, H > 0 ? H : 16 * s));
        if (c.design) ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(150, 150, 150, 60));
        break;
    case Kind::Component: {
        ImGui::SetCursorScreenPos(pos);
        const Component* comp = components().find(n.component);
        ImVec2 z(W > 0 ? W : (comp ? comp->w : 140) * s, H > 0 ? H : (comp ? comp->h : 40) * s);
        bool pressed = ImGui::InvisibleButton("##comp", z);
        int st = StNormal;
        if (!c.design) st = ImGui::IsItemActive() ? StActive : ImGui::IsItemHovered() ? StHover : StNormal;
        if (comp) drawComponent(dl, *comp, pos, z, s, st, n.text);
        else {
            dl->AddRect(pos, ImVec2(pos.x + z.x, pos.y + z.y), IM_COL32(230, 90, 90, 200), 4 * s, 0, 1.0f);
            dl->AddText(ImVec2(pos.x + 6 * s, pos.y + 4 * s), IM_COL32(230, 90, 90, 255), ("? " + n.component).c_str());
        }
        if (pressed && !c.design) {
            auto it = B.actions.find(key(n));
            if (it != B.actions.end() && it->second) it->second();
            else if (B.onUnboundAction) B.onUnboundAction(key(n));
        }
        break;
    }
    default:
        break;
    }
    if (item) {
        size = ImGui::GetItemRectSize();
        if (!c.design && !n.tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", n.tooltip.c_str());
    }
    c.hits[hitIndex].rect = ImRect(pos, ImVec2(pos.x + size.x, pos.y + size.y));
    if (n.hasColor) ImGui::PopStyleColor();
    if (!n.visible) ImGui::PopStyleVar();
    ImGui::PopID();
    return size;
}

ImRect drawWindowFrame(Node& w, ImVec2 pos, DrawCtx& c, bool selected) {
    const float s = c.scale;
    ImVec2 size(std::max(80 * s, w.w * s), std::max(60 * s, w.h * s));
    ImVec2 mx(pos.x + size.x, pos.y + size.y);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float tb = titleBarHeight(s);
    dl->AddRectFilled(pos, mx, ImGui::GetColorU32(w.hasBg ? w.bg : ImGui::GetStyleColorVec4(ImGuiCol_WindowBg)), ImGui::GetStyle().WindowRounding);
    dl->AddRectFilled(pos, ImVec2(mx.x, pos.y + tb), ImGui::GetColorU32(selected ? ImGuiCol_TitleBgActive : ImGuiCol_TitleBg), ImGui::GetStyle().WindowRounding, ImDrawFlags_RoundCornersTop);
    dl->AddText(ImVec2(pos.x + ImGui::GetStyle().FramePadding.x, pos.y + ImGui::GetStyle().FramePadding.y), ImGui::GetColorU32(ImGuiCol_Text), w.text.c_str());
    dl->AddRect(pos, mx, ImGui::GetColorU32(ImGuiCol_Border), ImGui::GetStyle().WindowRounding);

    size_t hitIndex = c.hits.size();
    ImVec2 origin(pos.x + ImGui::GetStyle().WindowPadding.x, pos.y + tb + ImGui::GetStyle().WindowPadding.y);   // same place as in a real window
    c.hits.push_back({w.uid, ImRect(pos, mx), ImRect(origin, mx)});
    ImGui::PushID(w.id.c_str());
    dl->PushClipRect(ImVec2(pos.x, pos.y + tb), mx, true);
    ImGui::PushClipRect(ImVec2(pos.x, pos.y + tb), mx, true);
    for (Node& ch : w.children) drawNode(ch, ImVec2(origin.x + ch.x * s, origin.y + ch.y * s), c);
    ImGui::PopClipRect();
    dl->PopClipRect();
    ImGui::PopID();
    (void)hitIndex;
    return ImRect(pos, mx);
}

void drawLive(Doc& doc, Bindings& b, float scale) {
    DrawCtx c;
    c.scale = scale;
    c.bindings = &b;
    for (Node& w : doc.windows) {
        if (!w.visible) continue;
        ImGui::SetNextWindowPos(ImVec2(w.x * scale, w.y * scale), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(w.w * scale, w.h * scale), ImGuiCond_FirstUseEver);
        std::string title = (w.text.empty() ? w.id : w.text) + "###wzui." + doc.name + "." + w.id;
        if (w.hasBg) ImGui::PushStyleColor(ImGuiCol_WindowBg, w.bg);
        bool open = ImGui::Begin(title.c_str());
        if (w.hasBg) ImGui::PopStyleColor();
        if (open) {
            ImVec2 origin = ImGui::GetCursorScreenPos();
            ImVec2 far = origin;
            for (Node& ch : w.children) {
                ImVec2 p(origin.x + ch.x * scale, origin.y + ch.y * scale);
                ImVec2 z = drawNode(ch, p, c);
                far.x = std::max(far.x, p.x + z.x);
                far.y = std::max(far.y, p.y + z.y);
            }
            ImGui::SetCursorScreenPos(far);
            ImGui::Dummy(ImVec2(0, 0));
        }
        ImGui::End();
    }
}

} // namespace wzui
