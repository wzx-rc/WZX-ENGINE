#include "ComponentEditor.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace fs = std::filesystem;

namespace wzui {

namespace {

const char* kExt = ".wzcomp.json";

int resizeCb(ImGuiInputTextCallbackData* d) {
    if (d->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* s = (std::string*)d->UserData;
        s->resize(d->BufTextLen);
        d->Buf = s->data();
    }
    return 0;
}
bool inputStr(const char* label, std::string& s, const char* hint = nullptr) {
    if (s.capacity() < 64) s.reserve(64);
    if (hint) return ImGui::InputTextWithHint(label, hint, s.data(), s.capacity() + 1, ImGuiInputTextFlags_CallbackResize, resizeCb, &s);
    return ImGui::InputText(label, s.data(), s.capacity() + 1, ImGuiInputTextFlags_CallbackResize, resizeCb, &s);
}
std::string stemOf(const fs::path& p) {
    std::string n = p.filename().u8string();
    return n.size() > 12 ? n.substr(0, n.size() - 12) : n;
}
float rnd(float v) { return ImGui::GetIO().KeyAlt ? std::round(v * 10) / 10 : std::round(v); }

// a button drawn "on" when `on` (tools, states)
bool toggleButton(const char* label, bool on, ImVec2 size = ImVec2(0, 0)) {
    if (on) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.55f, 0.25f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.92f, 0.62f, 0.30f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.10f, 0.06f, 0.02f, 1));
    }
    bool r = ImGui::Button(label, size);
    if (on) ImGui::PopStyleColor(3);
    return r;
}

const int kHx[8] = {-1, 0, 1, 1, 1, 0, -1, -1};
const int kHy[8] = {-1, -1, -1, 0, 1, 1, 1, 0};
const char* kShapeLabels[] = {"Rectangle", "Ellipse", "Texte", "Ligne"};

} // namespace

// ---------- files ----------
void ComponentEditor::setFolder(const fs::path& folder) {
    m_folder = folder;
    components().setFolder(folder);
    if (components().items.empty()) {
        saveComponent(folder / fs::u8path(std::string("bouton_ambre") + kExt), defaultButton("bouton_ambre"));
        components().refresh();
    }
    auto f = files();
    if (!f.empty()) open(f.front());
}

std::vector<fs::path> ComponentEditor::files() const {
    std::vector<fs::path> out;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(m_folder, ec)) {
        std::string n = e.path().filename().u8string();
        if (n.size() > 12 && n.compare(n.size() - 12, 12, kExt) == 0) out.push_back(e.path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool ComponentEditor::open(const fs::path& file) {
    if (m_dirty) saveNow();
    Component c;
    std::string err;
    if (!loadComponent(file, c, &err)) { toast("Ouverture impossible : " + err); return false; }
    c.name = stemOf(file);
    m_c = m_before = std::move(c);
    m_file = file;
    std::error_code ec;
    m_fileTime = fs::last_write_time(file, ec);
    m_undo.clear();
    m_redo.clear();
    m_sel = 0;
    m_dirty = false;
    m_center = true;
    return true;
}

bool ComponentEditor::openByName(const std::string& name) { return open(m_folder / fs::u8path(name + kExt)); }

bool ComponentEditor::create(const std::string& name, bool asButton) {
    if (name.empty()) return false;
    fs::path f = m_folder / fs::u8path(name + kExt);
    std::error_code ec;
    if (fs::exists(f, ec)) return open(f);
    Component c;
    if (asButton) c = defaultButton(name);
    else {
        c.name = name;
        Layer bg = c.makeLayer(Shape::Rect);
        bg.name = "Fond";
        bg.w = c.w; bg.h = c.h; bg.radius = 6;
        bg.look[StNormal].fill.a = ImVec4(0.22f, 0.24f, 0.28f, 1);
        c.layers.push_back(bg);
    }
    if (!saveComponent(f, c)) return false;
    components().refresh();
    return open(f);
}

bool ComponentEditor::saveNow() {
    if (m_file.empty()) return false;
    if (!saveComponent(m_file, m_c)) { toast("Écriture impossible"); return false; }
    std::error_code ec;
    m_fileTime = fs::last_write_time(m_file, ec);
    m_dirty = false;
    m_saveAt = -1;
    components().refresh();   // interfaces using it see the change at once
    return true;
}

void ComponentEditor::tick() {
    double t = ImGui::GetTime();
    if (m_saveAt >= 0 && t >= m_saveAt && m_drag == Drag::None && !ImGui::IsAnyItemActive()) saveNow();
    if (t < m_nextPoll || m_file.empty()) return;
    m_nextPoll = t + 0.5;
    std::error_code ec;
    auto mt = fs::last_write_time(m_file, ec);
    if (ec || mt == m_fileTime || m_dirty) return;
    Component c;
    if (!loadComponent(m_file, c)) { m_fileTime = mt; return; }
    c.name = stemOf(m_file);
    m_undo.push_back(m_c);
    m_c = m_before = std::move(c);
    m_fileTime = mt;
    if (!m_c.find(m_sel)) m_sel = 0;
    toast("Rechargé : modifié sur le disque");
}

// ---------- edits ----------
void ComponentEditor::edited() {
    if (!m_gesture) {
        m_undo.push_back(m_before);
        if (m_undo.size() > 200) m_undo.erase(m_undo.begin());
        m_redo.clear();
        m_gesture = true;
    }
    m_dirty = true;
    m_saveAt = ImGui::GetTime() + 0.4;
}
void ComponentEditor::undo() {
    if (m_undo.empty()) return;
    m_redo.push_back(m_c);
    m_c = m_before = m_undo.back();
    m_undo.pop_back();
    if (!m_c.find(m_sel)) m_sel = 0;
    m_dirty = true;
    m_saveAt = ImGui::GetTime() + 0.4;
}
void ComponentEditor::redo() {
    if (m_redo.empty()) return;
    m_undo.push_back(m_c);
    m_c = m_before = m_redo.back();
    m_redo.pop_back();
    if (!m_c.find(m_sel)) m_sel = 0;
    m_dirty = true;
    m_saveAt = ImGui::GetTime() + 0.4;
}
void ComponentEditor::removeLayer(int uid) {
    auto& v = m_c.layers;
    v.erase(std::remove_if(v.begin(), v.end(), [&](const Layer& l) { return l.uid == uid; }), v.end());
    if (m_sel == uid) m_sel = 0;
    edited();
}
void ComponentEditor::duplicateLayer(int uid) {
    auto& v = m_c.layers;
    for (size_t i = 0; i < v.size(); ++i)
        if (v[i].uid == uid) {
            Layer l = v[i];
            l.uid = m_c.nextUid++;
            l.name += " copie";
            l.x += 4; l.y += 4;
            v.insert(v.begin() + i + 1, l);
            m_sel = l.uid;
            edited();
            return;
        }
}
void ComponentEditor::moveLayer(int uid, int dir) {
    auto& v = m_c.layers;
    for (size_t i = 0; i < v.size(); ++i)
        if (v[i].uid == uid) {
            size_t j = i + dir;
            if (j < v.size()) { std::swap(v[i], v[j]); edited(); }
            return;
        }
}
void ComponentEditor::toast(const std::string& t) {
    m_toast = t;
    m_toastUntil = ImGui::GetTime() + 2.5;
}

// ---------- geometry on the canvas ----------
ImRect ComponentEditor::layerRect(const Layer& l) const {
    ImVec2 a = toScreen(ImVec2(l.x, l.y)), b = toScreen(ImVec2(l.x + l.w, l.y + l.h));
    return ImRect(ImVec2(std::min(a.x, b.x), std::min(a.y, b.y)), ImVec2(std::max(a.x, b.x), std::max(a.y, b.y)));
}
int ComponentEditor::layerAt(ImVec2 m) const {
    for (auto it = m_c.layers.rbegin(); it != m_c.layers.rend(); ++it) {
        if (!it->visible) continue;
        ImRect r = layerRect(*it);
        if (it->shape == Shape::Line) r.Expand(4);
        if (r.Contains(m)) return it->uid;
    }
    return 0;
}
ImVec2 ComponentEditor::handlePos(const Layer& l, int i) const {
    float x = l.x + l.w * (kHx[i] + 1) * 0.5f, y = l.y + l.h * (kHy[i] + 1) * 0.5f;
    return toScreen(ImVec2(x, y));
}
int ComponentEditor::handleAt(ImVec2 m) const {
    const Layer* l = const_cast<Component&>(m_c).find(m_sel);
    if (!l) return -1;
    for (int i = 0; i < 8; ++i) {
        if (l->shape == Shape::Line && i != 0 && i != 4) continue;
        ImVec2 p = handlePos(*l, i);
        if (std::fabs(m.x - p.x) <= 6 && std::fabs(m.y - p.y) <= 6) return i;
    }
    return -1;
}

// ---------- panels ----------
void ComponentEditor::toolbar(float ui) {
    m_ui = ui;
    std::string cur = m_file.empty() ? "(aucun)" : stemOf(m_file);
    ImGui::SetNextItemWidth(200 * ui);
    if (ImGui::BeginCombo("##comp", cur.c_str())) {
        for (auto& f : files())
            if (ImGui::Selectable(stemOf(f).c_str(), f == m_file)) open(f);
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Nouveau composant")) ImGui::OpenPopup("##newcomp");
    if (ImGui::BeginPopup("##newcomp")) {
        ImGui::SetNextItemWidth(200 * ui);
        ImGui::InputTextWithHint("##n", "nom (ex. bouton_bleu)", m_newName, sizeof m_newName);
        if (ImGui::Button("Vide") && m_newName[0]) { create(m_newName, false); m_newName[0] = 0; ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("À partir d'un bouton") && m_newName[0]) { create(m_newName, true); m_newName[0] = 0; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Enregistrer##c") && saveNow()) toast("Enregistré");
    ImGui::SameLine();
    ImGui::BeginDisabled(m_undo.empty());
    if (ImGui::Button("Annuler##c")) undo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(m_redo.empty());
    if (ImGui::Button("Rétablir##c")) redo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    static const struct { Tool t; const char* label; const char* tip; } tools[] = {
        {Tool::Select, "Sélection", "Sélectionner, déplacer, redimensionner (V)"},
        {Tool::Rect, "Rectangle", "Dessiner un rectangle (R) ; Maj : carré"},
        {Tool::Ellipse, "Ellipse", "Dessiner une ellipse (O) ; Maj : cercle"},
        {Tool::Text, "Texte", "Poser un texte (T) ; {text} = texte de l'instance"},
        {Tool::Line, "Ligne", "Tracer une ligne (L)"},
    };
    for (auto& t : tools) {
        ImGui::SameLine();
        if (toggleButton(t.label, m_tool == t.t)) { m_tool = t.t; m_test = false; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", t.tip);
    }
}

void ComponentEditor::layers() {
    ImGui::SeparatorText("Calques");
    if (m_c.layers.empty()) ImGui::TextDisabled("Aucun calque : dessinez avec un outil.");
    int moveUid = 0, moveDir = 0;
    for (int i = (int)m_c.layers.size() - 1; i >= 0; --i) {   // top first, like Figma
        Layer& l = m_c.layers[i];
        ImGui::PushID(l.uid);
        if (ImGui::Checkbox("##vis", &l.visible)) edited();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Visible");
        ImGui::SameLine();
        std::string lbl = l.name + "##sel";
        if (ImGui::Selectable(lbl.c_str(), l.uid == m_sel, 0, ImVec2(ImGui::GetContentRegionAvail().x - 70 * m_ui, 0))) m_sel = l.uid;
        if (ImGui::IsItemActive() && !ImGui::IsItemHovered()) {   // drag to reorder
            float dy = ImGui::GetMouseDragDelta(0).y;
            if (std::fabs(dy) > ImGui::GetFrameHeight() * 0.6f) {
                moveUid = l.uid;
                moveDir = dy < 0 ? 1 : -1;
                ImGui::ResetMouseDragDelta();
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", kShapeLabels[(int)l.shape]);
        ImGui::PopID();
    }
    if (moveUid) moveLayer(moveUid, moveDir);
    ImGui::BeginDisabled(!m_c.find(m_sel));
    const ImVec2 bw((ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2, 0);
    if (ImGui::Button("Monter", bw)) moveLayer(m_sel, 1);
    ImGui::SameLine();
    if (ImGui::Button("Descendre", bw)) moveLayer(m_sel, -1);
    if (ImGui::Button("Dupliquer##l", bw)) duplicateLayer(m_sel);
    ImGui::SameLine();
    if (ImGui::Button("Supprimer##l", bw)) removeLayer(m_sel);
    ImGui::EndDisabled();
}

void ComponentEditor::componentProps() {
    ImGui::SeparatorText("Composant");
    ImGui::Text("%s", m_c.name.c_str());
    float sz[2] = {m_c.w, m_c.h};
    if (ImGui::DragFloat2("Taille", sz, 0.5f, 4, 4000, "%.0f")) {
        float fx = sz[0] / std::max(1.0f, m_c.w), fy = sz[1] / std::max(1.0f, m_c.h);
        for (auto& l : m_c.layers) { l.x *= fx; l.w *= fx; l.y *= fy; l.h *= fy; }
        m_c.w = std::max(4.0f, sz[0]);
        m_c.h = std::max(4.0f, sz[1]);
        edited();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Taille de référence ; les calques suivent.\nUne interface peut étirer le composant à n'importe quelle taille.");
    inputStr("Texte d'aperçu", m_text);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remplace {text} dans l'aperçu (dans une interface : le texte du widget)");
    ImGui::Spacing();
    ImGui::TextWrapped("Dans « Interfaces », ajoutez un widget Component et choisissez « %s » : le clic appelle l'action de sa liaison.", m_c.name.c_str());
}

void ComponentEditor::stateBar() {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("État");
    for (int s = 0; s < StCount; ++s) {
        ImGui::SameLine();
        bool own = false;
        for (auto& l : m_c.layers) own |= s != StNormal && l.hasState[s];
        std::string lbl = std::string(stateLabel(s)) + (own ? " •" : "");
        if (toggleButton(lbl.c_str(), m_state == s && !m_test)) { m_state = s; m_test = false; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(s == StNormal ? "Style de base" : own ? "Cet état a son propre style (•)" : "Reprend le style Normal tant qu'il n'est pas modifié");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    if (toggleButton("Tester", m_test)) { m_test = !m_test; m_tool = Tool::Select; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Survolez et cliquez le composant comme dans le logiciel");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120 * m_ui);
    int z = (int)std::round(m_zoom * 100);
    if (ImGui::SliderInt("##zoomc", &z, 50, 800, "Zoom %d%%")) m_zoom = z / 100.0f;
    ImGui::SameLine();
    if (ImGui::Button("Centrer")) m_center = true;
}

void ComponentEditor::properties() {
    Layer* l = m_c.find(m_sel);
    ImGui::SeparatorText("Calque");
    if (!l) {
        ImGui::TextWrapped("Choisissez un outil (Rectangle, Ellipse, Texte, Ligne) et dessinez dans le cadre, ou cliquez un calque pour le modifier.");
        return;
    }
    bool ch = false;
    ch |= inputStr("Nom", l->name);
    int shape = (int)l->shape;
    if (ImGui::Combo("Forme", &shape, kShapeLabels, 4)) { l->shape = (Shape)shape; ch = true; }
    float p[2] = {l->x, l->y}, z[2] = {l->w, l->h};
    if (ImGui::DragFloat2("Position", p, 0.5f, -4000, 4000, "%.0f")) { l->x = p[0]; l->y = p[1]; ch = true; }
    if (ImGui::DragFloat2("Taille##l", z, 0.5f, l->shape == Shape::Line ? -4000.0f : 0.0f, 4000, "%.0f")) { l->w = z[0]; l->h = z[1]; ch = true; }
    if (l->shape == Shape::Rect) ch |= ImGui::DragFloat("Arrondi", &l->radius, 0.2f, 0, 500, "%.0f");
    if (l->shape == Shape::Text) {
        ch |= inputStr("Texte", l->text);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("{text} = le texte donné par l'interface");
        ch |= ImGui::DragFloat("Taille du texte", &l->fontSize, 0.2f, 4, 200, "%.0f");
        static const char* al[] = {"Gauche", "Centre", "Droite"};
        ch |= ImGui::Combo("Alignement", &l->align, al, 3);
    }

    // look of the current state, edited on a copy: the first change of a state without its own look creates it
    std::string title = std::string("Apparence · ") + stateLabel(m_state);
    ImGui::SeparatorText(title.c_str());
    if (m_state != StNormal) {
        if (l->hasState[m_state]) {
            ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.40f, 1), "Style propre à cet état");
            ImGui::SameLine();
            if (ImGui::SmallButton("Réinitialiser")) { l->hasState[m_state] = false; ch = true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Revenir au style Normal pour cet état");
        } else {
            ImGui::TextDisabled("Reprend le style Normal ; une modification crée le style de cet état.");
        }
    }
    Look k = l->lookFor(m_state);
    bool lc = false;
    const ImGuiColorEditFlags cf = ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf;
    if (l->shape == Shape::Line) {
        lc |= ImGui::ColorEdit4("Couleur", &k.fill.a.x, cf);
        lc |= ImGui::DragFloat("Épaisseur", &k.strokeWidth, 0.1f, 0.5f, 50, "%.1f");
    } else {
        ImGui::TextDisabled("Remplissage");
        lc |= ImGui::Checkbox("##fill", &k.fill.on);
        ImGui::SameLine();
        ImGui::BeginDisabled(!k.fill.on);
        lc |= ImGui::Checkbox("Dégradé", &k.fill.gradient);
        lc |= ImGui::ColorEdit4(k.fill.gradient ? "Couleur 1" : "Couleur", &k.fill.a.x, cf);
        if (k.fill.gradient) {
            lc |= ImGui::ColorEdit4("Couleur 2", &k.fill.b.x, cf);
            lc |= ImGui::SliderFloat("Angle", &k.fill.angle, 0, 360, "%.0f°");
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled("Contour");
        lc |= ImGui::Checkbox("##stroke", &k.stroke);
        ImGui::SameLine();
        ImGui::BeginDisabled(!k.stroke);
        lc |= ImGui::ColorEdit4("##sc", &k.strokeColor.x, cf | ImGuiColorEditFlags_NoInputs);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        lc |= ImGui::DragFloat("##sw", &k.strokeWidth, 0.1f, 0.5f, 50, "épaisseur %.1f");
        ImGui::EndDisabled();
        ImGui::TextDisabled("Ombre");
        lc |= ImGui::Checkbox("##shadow", &k.shadow);
        ImGui::SameLine();
        ImGui::BeginDisabled(!k.shadow);
        lc |= ImGui::ColorEdit4("##shc", &k.shadowColor.x, cf | ImGuiColorEditFlags_NoInputs);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        lc |= ImGui::DragFloat("##shb", &k.shadowBlur, 0.2f, 0, 100, "flou %.0f");
        lc |= ImGui::DragFloat2("Décalage ombre", &k.shadowOffset.x, 0.2f, -100, 100, "%.0f");
        ImGui::EndDisabled();
    }
    if (l->shape == Shape::Text) lc |= ImGui::ColorEdit4("Couleur du texte", &k.textColor.x, cf);
    lc |= ImGui::SliderFloat("Opacité", &k.opacity, 0, 1, "%.2f");
    lc |= ImGui::DragFloat2("Décalage", &k.offset.x, 0.1f, -100, 100, "%.0f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Déplacement dans cet état (ex. Appuyé : 0, 1)");
    if (lc) {
        l->look[m_state] = k;
        l->hasState[m_state] = true;
        ch = true;
    }
    if (ch) edited();
}

void ComponentEditor::canvas() {
    ImGuiIO& io = ImGui::GetIO();
    const float stripH = (std::min(m_c.h, 80.0f) + 34) * m_ui + ImGui::GetStyle().ItemSpacing.y;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##ccanvas", ImVec2(0, -stripH), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleVar();
    ImVec2 p0 = ImGui::GetCursorScreenPos(), avail = ImGui::GetContentRegionAvail();
    m_canvasRect = ImRect(p0, ImVec2(p0.x + avail.x, p0.y + avail.y));
    m_s = m_zoom * m_ui;
    if (m_center && avail.x > 0) {
        m_pan = ImVec2(std::round((avail.x - m_c.w * m_s) / 2), std::round((avail.y - m_c.h * m_s) / 2));
        m_center = false;
    }
    m_origin = ImVec2(p0.x + m_pan.x, p0.y + m_pan.y);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(m_canvasRect.Min, m_canvasRect.Max, IM_COL32(30, 31, 36, 255));
    for (float x = p0.x + std::fmod(m_pan.x, 20.0f); x < m_canvasRect.Max.x; x += 20)
        for (float y = p0.y + std::fmod(m_pan.y, 20.0f); y < m_canvasRect.Max.y; y += 20) dl->AddRectFilled(ImVec2(x, y), ImVec2(x + 1, y + 1), IM_COL32(62, 64, 72, 255));

    // frame: checkerboard = transparent
    const ImRect F(m_origin, toScreen(ImVec2(m_c.w, m_c.h)));
    dl->PushClipRect(F.Min, F.Max, true);
    const float cs = 8;
    for (float y = F.Min.y; y < F.Max.y; y += cs)
        for (float x = F.Min.x; x < F.Max.x; x += cs)
            if (((int)((x - F.Min.x) / cs) + (int)((y - F.Min.y) / cs)) & 1) dl->AddRectFilled(ImVec2(x, y), ImVec2(x + cs, y + cs), IM_COL32(52, 54, 60, 255));
            else dl->AddRectFilled(ImVec2(x, y), ImVec2(x + cs, y + cs), IM_COL32(44, 46, 51, 255));
    dl->PopClipRect();
    char lbl[128];
    snprintf(lbl, sizeof lbl, "%s   %.0f × %.0f", m_c.name.c_str(), m_c.w, m_c.h);
    dl->AddText(ImVec2(F.Min.x, F.Min.y - ImGui::GetFontSize() - 4), IM_COL32(170, 172, 180, 255), lbl);

    // the component itself (Tester: live states from the mouse)
    int st = m_state;
    if (m_test) {
        ImGui::SetCursorScreenPos(F.Min);
        if (ImGui::InvisibleButton("##try", ImVec2(std::max(1.0f, F.GetWidth()), std::max(1.0f, F.GetHeight()))) && m_state != StDisabled) {
            m_lastClick = m_c.name;
            toast("Clic sur « " + m_c.name + " »");
        }
        if (m_state != StDisabled) st = ImGui::IsItemActive() ? StActive : ImGui::IsItemHovered() ? StHover : StNormal;
    }
    drawComponent(dl, m_c, m_origin, F.GetSize(), m_s, st, m_text);
    dl->AddRect(ImVec2(F.Min.x - 1, F.Min.y - 1), ImVec2(F.Max.x + 1, F.Max.y + 1), IM_COL32(130, 132, 145, 160), 0.0f, 0, 1.0f);

    const ImVec2 m = io.MousePos;
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && m_canvasRect.Contains(m) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
    if (hovered && io.MouseWheel != 0 && io.KeyCtrl) {   // zoom around the mouse
        ImVec2 u((m.x - m_origin.x) / m_s, (m.y - m_origin.y) / m_s);
        m_zoom = std::clamp(m_zoom * (io.MouseWheel > 0 ? 1.15f : 1 / 1.15f), 0.5f, 8.0f);
        m_s = m_zoom * m_ui;
        m_pan = ImVec2(m.x - p0.x - u.x * m_s, m.y - p0.y - u.y * m_s);
    } else if (hovered && (io.MouseWheel != 0 || io.MouseWheelH != 0)) {
        float k = 40;
        if (io.KeyShift) m_pan.x += io.MouseWheel * k;
        else { m_pan.y += io.MouseWheel * k; m_pan.x += io.MouseWheelH * k; }
    }

    const Layer* hoverL = nullptr;
    if (!m_test) {
        ImGui::SetCursorScreenPos(p0);
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##cbg", ImVec2(std::max(1.0f, avail.x), std::max(1.0f, avail.y)), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
        const bool leftClick = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        const bool midClick = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
        const bool down = ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Middle);
        ImVec2 u((m.x - m_origin.x) / m_s, (m.y - m_origin.y) / m_s);
        const ImRect frameHandle(ImVec2(F.Max.x - 5, F.Max.y - 5), ImVec2(F.Max.x + 5, F.Max.y + 5));

        m_hover = hovered && m_drag == Drag::None ? layerAt(m) : 0;
        if (midClick) { m_drag = Drag::Pan; m_dragStart = m; }
        else if (leftClick) {
            m_dragStart = m;
            if (m_tool != Tool::Select) {
                static const Shape shapes[] = {Shape::Rect, Shape::Rect, Shape::Ellipse, Shape::Text, Shape::Line};
                Layer l = m_c.makeLayer(shapes[(int)m_tool]);
                if (l.shape == Shape::Text) l.text = "Texte";
                l.x = rnd(u.x); l.y = rnd(u.y); l.w = l.h = 0;
                m_drawStart = ImVec2(l.x, l.y);
                m_c.layers.push_back(l);
                m_sel = l.uid;
                m_drag = Drag::Draw;
                edited();
            } else if (int h = handleAt(m); h >= 0) {
                m_drag = Drag::Handle;
                m_handle = h;
                m_startL = *m_c.find(m_sel);
            } else if (frameHandle.Contains(m)) {
                m_drag = Drag::Frame;
                m_startC = m_c;
            } else {
                m_sel = layerAt(m);
                m_drag = m_sel ? Drag::Pending : Drag::None;
                if (Layer* l = m_c.find(m_sel)) m_startL = *l;
            }
        } else if (m_drag != Drag::None && down) {
            ImVec2 d((m.x - m_dragStart.x) / m_s, (m.y - m_dragStart.y) / m_s);
            Layer* l = m_c.find(m_sel);
            switch (m_drag) {
            case Drag::Pan: m_pan.x += io.MouseDelta.x; m_pan.y += io.MouseDelta.y; break;
            case Drag::Pending:
                if (std::fabs(m.x - m_dragStart.x) + std::fabs(m.y - m_dragStart.y) > 3) m_drag = Drag::Move;
                break;
            case Drag::Move:
                if (l) { l->x = rnd(m_startL.x + d.x); l->y = rnd(m_startL.y + d.y); edited(); }
                break;
            case Drag::Draw:
                if (l) {
                    ImVec2 e(rnd(u.x), rnd(u.y));
                    if (l->shape == Shape::Line) {
                        l->w = e.x - m_drawStart.x; l->h = e.y - m_drawStart.y;
                        if (io.KeyShift) { if (std::fabs(l->w) > std::fabs(l->h)) l->h = 0; else l->w = 0; }
                    } else {
                        float w = std::fabs(e.x - m_drawStart.x), h = std::fabs(e.y - m_drawStart.y);
                        if (io.KeyShift) w = h = std::max(w, h);
                        l->x = e.x < m_drawStart.x ? m_drawStart.x - w : m_drawStart.x;
                        l->y = e.y < m_drawStart.y ? m_drawStart.y - h : m_drawStart.y;
                        l->w = w; l->h = h;
                    }
                    edited();
                }
                break;
            case Drag::Handle:
                if (l) {
                    const Layer& s = m_startL;
                    if (l->shape == Shape::Line) {
                        if (m_handle == 0) { l->x = rnd(s.x + d.x); l->y = rnd(s.y + d.y); l->w = s.x + s.w - l->x; l->h = s.y + s.h - l->y; }
                        else { l->w = rnd(s.w + d.x); l->h = rnd(s.h + d.y); }
                    } else {
                        float x0 = s.x, y0 = s.y, x1 = s.x + s.w, y1 = s.y + s.h;
                        if (kHx[m_handle] < 0) x0 = rnd(x0 + d.x);
                        if (kHx[m_handle] > 0) x1 = rnd(x1 + d.x);
                        if (kHy[m_handle] < 0) y0 = rnd(y0 + d.y);
                        if (kHy[m_handle] > 0) y1 = rnd(y1 + d.y);
                        l->x = std::min(x0, x1); l->w = std::fabs(x1 - x0);
                        l->y = std::min(y0, y1); l->h = std::fabs(y1 - y0);
                    }
                    edited();
                }
                break;
            case Drag::Frame: {
                float w = std::max(4.0f, rnd(m_startC.w + d.x)), h = std::max(4.0f, rnd(m_startC.h + d.y));
                bool stretch = !io.KeyAlt;
                m_c.w = w; m_c.h = h;
                for (size_t i = 0; i < m_c.layers.size() && i < m_startC.layers.size(); ++i) {
                    const Layer& s = m_startC.layers[i];
                    Layer& t = m_c.layers[i];
                    float fx = stretch ? w / m_startC.w : 1, fy = stretch ? h / m_startC.h : 1;
                    t.x = std::round(s.x * fx * 10) / 10; t.w = std::round(s.w * fx * 10) / 10;
                    t.y = std::round(s.y * fy * 10) / 10; t.h = std::round(s.h * fy * 10) / 10;
                }
                edited();
                break;
            }
            default: break;
            }
        } else if (m_drag != Drag::None) {   // released
            if (m_drag == Drag::Draw) {
                if (Layer* l = m_c.find(m_sel); l && std::fabs(l->w) < 2 && std::fabs(l->h) < 2) {   // a click: default size
                    if (l->shape == Shape::Text) { l->w = std::max(40.0f, m_c.w - l->x); l->h = 20; }
                    else if (l->shape == Shape::Line) { l->w = 40; l->h = 0; }
                    else { l->w = 40; l->h = 24; }
                    edited();
                }
                m_tool = Tool::Select;
            }
            m_drag = Drag::None;
        }
        if (Layer* hl = m_c.find(m_hover)) hoverL = hl;
    }

    // overlays
    dl->PushClipRect(m_canvasRect.Min, m_canvasRect.Max, true);
    const ImU32 blue = IM_COL32(80, 160, 255, 255);
    if (!m_test) {
        if (hoverL && hoverL->uid != m_sel) { ImRect r = layerRect(*hoverL); dl->AddRect(r.Min, r.Max, IM_COL32(80, 160, 255, 150), 0.0f, 0, 1.0f); }
        if (const Layer* l = m_c.find(m_sel)) {
            ImRect r = layerRect(*l);
            if (l->shape == Shape::Line) dl->AddLine(toScreen(ImVec2(l->x, l->y)), toScreen(ImVec2(l->x + l->w, l->y + l->h)), blue, 1.0f);
            else dl->AddRect(r.Min, r.Max, blue, 0.0f, 0, 1.5f);
            for (int i = 0; i < 8; ++i) {
                if (l->shape == Shape::Line && i != 0 && i != 4) continue;
                ImVec2 p = handlePos(*l, i);
                dl->AddRectFilled(ImVec2(p.x - 4, p.y - 4), ImVec2(p.x + 4, p.y + 4), IM_COL32(255, 255, 255, 255));
                dl->AddRect(ImVec2(p.x - 4, p.y - 4), ImVec2(p.x + 4, p.y + 4), blue, 0.0f, 0, 1.5f);
            }
            char b[96];
            snprintf(b, sizeof b, "%.0f × %.0f", std::fabs(l->w), std::fabs(l->h));
            ImVec2 z = ImGui::CalcTextSize(b);
            ImVec2 a((r.Min.x + r.Max.x - z.x) / 2 - 4, r.Max.y + 8);
            dl->AddRectFilled(a, ImVec2(a.x + z.x + 8, a.y + z.y + 2), blue, 3);
            dl->AddText(ImVec2(a.x + 4, a.y + 1), IM_COL32(255, 255, 255, 255), b);
        }
        dl->AddRectFilled(ImVec2(F.Max.x - 4, F.Max.y - 4), ImVec2(F.Max.x + 4, F.Max.y + 4), IM_COL32(240, 170, 80, 255));
    }
    if (!m_toast.empty() && ImGui::GetTime() < m_toastUntil) {
        ImVec2 z = ImGui::CalcTextSize(m_toast.c_str());
        ImVec2 a(m_canvasRect.GetCenter().x - z.x / 2 - 10, m_canvasRect.Max.y - z.y - 24);
        dl->AddRectFilled(a, ImVec2(a.x + z.x + 20, a.y + z.y + 10), IM_COL32(20, 22, 26, 235), 5);
        dl->AddText(ImVec2(a.x + 10, a.y + 5), IM_COL32(240, 200, 140, 255), m_toast.c_str());
    }
    const char* hint = m_test ? "Test : survolez et cliquez le composant" :
                       m_tool == Tool::Select ? "Clic : sélectionner · glisser : déplacer · poignées : redimensionner · coin orange : taille du composant · Ctrl+molette : zoom" :
                       "Glissez dans le canevas pour dessiner (Maj : proportions fixes)";
    dl->AddText(ImVec2(m_canvasRect.Min.x + 8, m_canvasRect.Min.y + 6), IM_COL32(150, 152, 160, 255), hint);
    dl->PopClipRect();
    ImGui::EndChild();
}

void ComponentEditor::strip() {
    ImGui::BeginChild("##strip", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    ImVec2 p0 = ImGui::GetCursorScreenPos(), avail = ImGui::GetContentRegionAvail();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float cell = avail.x / StCount;
    float th = ImGui::GetFontSize() + 4;
    float sc = std::min(m_ui, std::min((cell - 20) / std::max(1.0f, m_c.w), (avail.y - th - 4) / std::max(1.0f, m_c.h)));
    for (int s = 0; s < StCount; ++s) {
        float x = p0.x + cell * s;
        dl->AddText(ImVec2(x + 10, p0.y), ImGui::GetColorU32(s == m_state ? ImGuiCol_Text : ImGuiCol_TextDisabled), stateLabel(s));
        ImVec2 z(m_c.w * sc, m_c.h * sc);
        ImVec2 a(std::round(x + (cell - z.x) / 2), std::round(p0.y + th + (avail.y - th - z.y) / 2));
        drawComponent(dl, m_c, a, z, sc, s, m_text);
        ImGui::SetCursorScreenPos(ImVec2(x, p0.y));
        ImGui::PushID(s);
        if (ImGui::InvisibleButton("##st", ImVec2(std::max(1.0f, cell), std::max(1.0f, avail.y)))) { m_state = s; m_test = false; }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void ComponentEditor::draw(float ui) {
    m_ui = ui;
    if (!ImGui::IsAnyItemActive() && m_drag == Drag::None && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {   // between gestures
        m_gesture = false;
        m_before = m_c;
    }
    const float left = 260 * ui, right = 320 * ui;
    ImGui::BeginChild("##cleft", ImVec2(left, 0), ImGuiChildFlags_Borders);
    layers();
    ImGui::Spacing();
    componentProps();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##cmid", ImVec2(-right - ImGui::GetStyle().ItemSpacing.x, 0));
    stateBar();
    canvas();
    strip();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##cright", ImVec2(0, 0), ImGuiChildFlags_Borders);
    properties();
    ImGui::EndChild();
}

void ComponentEditor::handleKeys() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;
    bool ctrl = io.KeyCtrl;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) { if (io.KeyShift) redo(); else undo(); }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) redo();
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false) && saveNow()) toast("Enregistré");
    if (!ctrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_V, false)) m_tool = Tool::Select;
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) { m_tool = Tool::Rect; m_test = false; }
        if (ImGui::IsKeyPressed(ImGuiKey_O, false)) { m_tool = Tool::Ellipse; m_test = false; }
        if (ImGui::IsKeyPressed(ImGuiKey_T, false)) { m_tool = Tool::Text; m_test = false; }
        if (ImGui::IsKeyPressed(ImGuiKey_L, false)) { m_tool = Tool::Line; m_test = false; }
    }
    Layer* l = m_c.find(m_sel);
    if (!l) return;
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) { removeLayer(m_sel); return; }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) { duplicateLayer(m_sel); return; }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { m_sel = 0; return; }
    float st = io.KeyShift ? 10.0f : 1.0f, dx = 0, dy = 0;
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) dx = -st;
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) dx = st;
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) dy = -st;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) dy = st;
    if (dx || dy) { l->x += dx; l->y += dy; edited(); }
}

} // namespace wzui
