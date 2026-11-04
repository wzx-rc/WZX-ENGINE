#include "Designer.h"
#include <algorithm>
#include <cmath>

namespace fs = std::filesystem;

namespace wzui {

Designer& designer() {
    static Designer d;
    return d;
}

namespace {

enum Op { OpNone, OpDelete, OpDuplicate, OpUp, OpDown, OpWrapRow, OpWrapColumn, OpReparent };

std::string u8(const fs::path& p) { return p.u8string(); }

int resizeCb(ImGuiInputTextCallbackData* d) {
    if (d->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* s = (std::string*)d->UserData;
        s->resize(d->BufTextLen);
        d->Buf = s->data();
    }
    return 0;
}
bool inputStr(const char* label, std::string& s, const char* hint = nullptr, bool multiline = false) {
    if (s.capacity() < 64) s.reserve(64);
    if (multiline) return ImGui::InputTextMultiline(label, s.data(), s.capacity() + 1, ImVec2(-1, ImGui::GetTextLineHeight() * 5), ImGuiInputTextFlags_CallbackResize, resizeCb, &s);
    if (hint) return ImGui::InputTextWithHint(label, hint, s.data(), s.capacity() + 1, ImGuiInputTextFlags_CallbackResize, resizeCb, &s);
    return ImGui::InputText(label, s.data(), s.capacity() + 1, ImGuiInputTextFlags_CallbackResize, resizeCb, &s);
}

Doc exampleDoc() {
    Doc d;
    d.name = "Exemple";
    Node w = d.makeNode(Kind::Window);
    w.id = "inspecteur"; w.text = "Inspecteur"; w.x = 0; w.y = 0; w.w = 320; w.h = 330;
    d.windows.push_back(w);
    Node col = d.makeNode(Kind::Column);
    Node title = d.makeNode(Kind::Label); title.id = "titre"; title.text = "Objet sélectionné"; title.hasColor = true; title.color = ImVec4(0.95f, 0.72f, 0.40f, 1);
    Node name = d.makeNode(Kind::Input); name.id = "nom"; name.text = "Nom"; name.bind = "objectName";
    Node inten = d.makeNode(Kind::Slider); inten.id = "intensite"; inten.text = "Intensité"; inten.bind = "lightIntensity"; inten.maxV = 10;
    Node shadows = d.makeNode(Kind::Checkbox); shadows.id = "ombres"; shadows.text = "Ombres"; shadows.bind = "shadows";
    Node quality = d.makeNode(Kind::Combo); quality.id = "qualite"; quality.text = "Qualité"; quality.items = {"Brouillon", "Normale", "Finale"};
    Node tint = d.makeNode(Kind::Color); tint.id = "teinte"; tint.text = "Teinte";
    Node sep = d.makeNode(Kind::Separator); sep.w = 280;
    Node row = d.makeNode(Kind::Row);
    Node apply = d.makeNode(Kind::Button); apply.id = "appliquer"; apply.text = "Appliquer"; apply.bind = "apply"; apply.w = 120; apply.hasBg = true; apply.bg = ImVec4(0.85f, 0.55f, 0.25f, 1);
    Node reset = d.makeNode(Kind::Button); reset.id = "reinitialiser"; reset.text = "Réinitialiser"; reset.bind = "reset"; reset.w = 120;
    row.children = {apply, reset};
    col.children = {title, name, inten, shadows, quality, tint, sep, row};
    d.windows[0].children.push_back(col);

    Node bar = d.makeNode(Kind::Window);
    bar.id = "barre"; bar.text = "Barre d'outils"; bar.x = 360; bar.y = 0; bar.w = 380; bar.h = 90;
    Node brow = d.makeNode(Kind::Row);
    const char* names[][2] = {{"play", "Play"}, {"stop", "Stop"}, {"build", "Build"}};
    for (auto& nm : names) {
        Node b = d.makeNode(Kind::Button);
        b.id = nm[0]; b.text = nm[1]; b.bind = nm[0]; b.w = 100; b.h = 32;
        brow.children.push_back(b);
    }
    bar.children.push_back(brow);
    d.windows.push_back(bar);
    d.assignUids();
    return d;
}

} // namespace

// ---------- files ----------
void Designer::setFolder(const fs::path& folder) {
    m_folder = folder;
    std::error_code ec;
    fs::create_directories(m_folder, ec);
    refreshFiles();
    if (m_files.empty()) {
        save(u8(m_folder / "exemple.wzui.json"), exampleDoc());
        refreshFiles();
    }
    if (!m_files.empty()) open(m_files.front());
    refreshLive();
}

void Designer::refreshFiles() {
    m_files.clear();
    std::error_code ec;
    for (auto& e : fs::directory_iterator(m_folder, ec)) {
        std::string n = u8(e.path().filename());
        if (n.size() > 10 && n.compare(n.size() - 10, 10, ".wzui.json") == 0) m_files.push_back(e.path());
    }
    std::sort(m_files.begin(), m_files.end());
}

void Designer::refreshLive() {
    m_live.clear();
    for (auto& f : m_files) {
        if (f == m_file) continue;
        Doc d;
        if (load(u8(f), d) && d.showInEditor) m_live.push_back(std::move(d));
    }
}

bool Designer::open(const fs::path& file) {
    if (m_dirty) saveNow();
    Doc d;
    std::string err;
    if (!load(u8(file), d, &err)) { toast("Ouverture impossible : " + err); return false; }
    m_doc = std::move(d);
    m_file = file;
    std::error_code ec;
    m_fileTime = fs::last_write_time(m_file, ec);
    m_undo.clear();
    m_redo.clear();
    m_sel = 0;
    m_dirty = m_diskChanged = false;
    return true;
}

bool Designer::saveNow() {
    if (m_file.empty()) return false;
    if (!save(u8(m_file), m_doc)) { toast("Écriture impossible : " + u8(m_file)); return false; }
    std::error_code ec;
    m_fileTime = fs::last_write_time(m_file, ec);
    m_dirty = m_diskChanged = false;
    m_saveAt = -1;
    return true;
}

void Designer::pollDisk() {
    double t = ImGui::GetTime();
    if (t >= m_nextLive) { m_nextLive = t + 2.0; refreshFiles(); refreshLive(); }
    if (t < m_nextPoll || m_file.empty()) return;
    m_nextPoll = t + 0.5;
    std::error_code ec;
    auto mt = fs::last_write_time(m_file, ec);
    if (ec || mt == m_fileTime) return;
    if (m_dirty) { m_diskChanged = true; return; }
    std::string selId;
    if (Node* n = m_doc.find(m_sel)) selId = n->id;
    Doc d;
    if (!load(u8(m_file), d)) { m_fileTime = mt; return; }   // half-written file: retried at the next change
    pushUndo();
    m_doc = std::move(d);
    m_fileTime = mt;
    m_sel = 0;
    std::vector<Node*> stack;
    for (auto& w : m_doc.windows) stack.push_back(&w);
    while (!stack.empty()) {
        Node* n = stack.back();
        stack.pop_back();
        if (!selId.empty() && n->id == selId) m_sel = n->uid;
        for (auto& c : n->children) stack.push_back(&c);
    }
    toast("Rechargé : modifié sur le disque");
}

// ---------- edits ----------
void Designer::pushUndo() {
    m_undo.push_back(m_doc);
    if (m_undo.size() > 200) m_undo.erase(m_undo.begin());
    m_redo.clear();
}
void Designer::changed() {
    m_dirty = true;
    if (m_autosave) m_saveAt = ImGui::GetTime() + 0.3;
}
void Designer::undo() {
    if (m_undo.empty()) return;
    m_redo.push_back(m_doc);
    m_doc = m_undo.back();
    m_undo.pop_back();
    if (!m_doc.find(m_sel)) m_sel = 0;
    changed();
}
void Designer::redo() {
    if (m_redo.empty()) return;
    m_undo.push_back(m_doc);
    m_doc = m_redo.back();
    m_redo.pop_back();
    if (!m_doc.find(m_sel)) m_sel = 0;
    changed();
}
float Designer::snap(float v) const {
    if (!m_grid || ImGui::GetIO().KeyAlt) return std::round(v);
    return std::round(v / 4.0f) * 4.0f;
}
const DrawCtx::Hit* Designer::hit(int uid) const {
    for (auto& h : m_ctx.hits) if (h.uid == uid) return &h;
    return nullptr;
}
int Designer::containerFor(int uid) {
    Node* n = m_doc.find(uid);
    if (!n) return m_doc.windows.empty() ? 0 : m_doc.windows.front().uid;
    if (isContainer(n->kind)) return uid;
    Node* p = m_doc.parentOf(uid);
    return p ? p->uid : 0;
}

void Designer::addNode(Kind k, int containerUid, ImVec2 ref, bool atPos, int index) {
    pushUndo();
    Node n = m_doc.makeNode(k);
    if (k == Kind::Window) {
        n.x = atPos ? snap(ref.x) : 20.0f * m_doc.windows.size();
        n.y = atPos ? snap(ref.y) : 20.0f * m_doc.windows.size();
        m_doc.windows.push_back(n);
        m_sel = n.uid;
        changed();
        return;
    }
    Node* c = m_doc.find(containerUid);
    if (!c) {   // nothing to put it in: a new window around it
        Node w = m_doc.makeNode(Kind::Window);
        w.x = atPos ? snap(ref.x) : 20; w.y = atPos ? snap(ref.y) : 20;
        m_doc.windows.push_back(w);
        c = &m_doc.windows.back();
        atPos = false;
    }
    if (isFreeLayout(c->kind)) {
        if (atPos) { n.x = snap(ref.x); n.y = snap(ref.y); }
        else {   // below the existing children
            float bottom = 0;
            const DrawCtx::Hit* ch = hit(c->uid);
            for (auto& o : c->children)
                if (const DrawCtx::Hit* h = hit(o.uid); h && ch) bottom = std::max(bottom, (h->rect.Max.y - ch->content.Min.y) / m_scale + c->spacing);
            n.y = snap(bottom);
        }
    }
    int uid = n.uid;
    if (index < 0 || index > (int)c->children.size()) index = (int)c->children.size();
    c->children.insert(c->children.begin() + index, std::move(n));
    m_sel = uid;
    changed();
}

void Designer::removeNode(int uid) {
    Node* p = m_doc.parentOf(uid);
    auto& list = p ? p->children : m_doc.windows;
    list.erase(std::remove_if(list.begin(), list.end(), [&](const Node& n) { return n.uid == uid; }), list.end());
    if (m_sel == uid) m_sel = p ? p->uid : 0;
}

void Designer::duplicate(int uid) {
    Node* n = m_doc.find(uid);
    if (!n) return;
    Node copy = *n;
    std::vector<Node*> stack{&copy};
    while (!stack.empty()) {   // new uids and ids
        Node* c = stack.back();
        stack.pop_back();
        c->uid = m_doc.nextUid++;
        c->id = m_doc.uniqueId(c->id.empty() ? kindName(c->kind) : c->id);
        for (auto& ch : c->children) stack.push_back(&ch);
    }
    Node* p = m_doc.parentOf(uid);
    if (!p || isFreeLayout(p->kind)) { copy.x += 16; copy.y += 16; }
    auto& list = p ? p->children : m_doc.windows;
    auto it = std::find_if(list.begin(), list.end(), [&](const Node& x) { return x.uid == uid; });
    int nu = copy.uid;
    list.insert(it == list.end() ? list.end() : it + 1, std::move(copy));
    m_sel = nu;
}

void Designer::moveSibling(int uid, int dir) {
    Node* p = m_doc.parentOf(uid);
    auto& list = p ? p->children : m_doc.windows;
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].uid == uid) {
            size_t j = i + dir;
            if (j < list.size()) std::swap(list[i], list[j]);
            return;
        }
}

void Designer::wrap(int uid, Kind k) {
    Node* p = m_doc.parentOf(uid);
    if (!p) return;   // windows are not wrapped
    Node box = m_doc.makeNode(k);
    for (auto& c : p->children)
        if (c.uid == uid) {
            box.x = c.x; box.y = c.y;
            Node inner = c;
            inner.x = inner.y = 0;
            box.children.push_back(std::move(inner));
            int bu = box.uid;
            c = std::move(box);
            m_sel = bu;
            return;
        }
}

bool Designer::reparent(int uid, int containerUid, int index, ImVec2 ref, bool atPos) {
    if (m_doc.contains(uid, containerUid)) return false;   // into itself
    Node* n = m_doc.find(uid);
    if (!n || n->kind == Kind::Window) return false;
    Node copy = *n;
    removeNode(uid);
    Node* c = m_doc.find(containerUid);
    if (!c) return false;
    if (isFreeLayout(c->kind) && atPos) { copy.x = snap(ref.x); copy.y = snap(ref.y); }
    if (index < 0 || index > (int)c->children.size()) index = (int)c->children.size();
    c->children.insert(c->children.begin() + index, std::move(copy));
    m_sel = uid;
    return true;
}

int Designer::dropContainerAt(ImVec2 m, int exclude) {
    for (auto it = m_ctx.hits.rbegin(); it != m_ctx.hits.rend(); ++it) {
        if (!it->rect.Contains(m)) continue;
        Node* n = m_doc.find(it->uid);
        if (!n || !isContainer(n->kind)) continue;
        if (exclude && m_doc.contains(exclude, n->uid)) continue;
        return n->uid;
    }
    return 0;
}

int Designer::insertIndex(int containerUid, ImVec2 m, int exclude) {
    Node* c = m_doc.find(containerUid);
    if (!c) return -1;
    bool row = c->kind == Kind::Row;
    int idx = 0;
    for (auto& ch : c->children) {
        if (ch.uid == exclude) continue;
        const DrawCtx::Hit* h = hit(ch.uid);
        if (!h) continue;
        ImVec2 ctr = h->rect.GetCenter();
        if (row ? m.x > ctr.x : m.y > ctr.y) ++idx;
    }
    return idx;
}

void Designer::toast(const std::string& t) {
    m_toast = t;
    m_toastUntil = ImGui::GetTime() + 2.5;
}

// ---------- panels ----------
void Designer::toolbar() {
    std::string cur = m_file.empty() ? "(aucun)" : u8(m_file.filename());
    ImGui::SetNextItemWidth(220 * m_scale);
    if (ImGui::BeginCombo("##file", cur.c_str())) {
        for (auto& f : m_files)
            if (ImGui::Selectable(u8(f.filename()).c_str(), f == m_file)) open(f);
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Nouveau")) ImGui::OpenPopup("##new");
    if (ImGui::BeginPopup("##new")) {
        ImGui::SetNextItemWidth(200 * m_scale);
        bool go = ImGui::InputTextWithHint("##name", "nom du fichier", m_newName, sizeof m_newName, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        go |= ImGui::Button("Créer");
        if (go && m_newName[0]) {
            if (m_dirty) saveNow();
            fs::path f = m_folder / fs::u8path(std::string(m_newName) + ".wzui.json");
            Doc d;
            d.name = m_newName;
            d.windows.push_back(d.makeNode(Kind::Window));
            save(u8(f), d);
            refreshFiles();
            open(f);
            m_newName[0] = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Enregistrer")) { if (saveNow()) toast("Enregistré"); }
    ImGui::SameLine();
    ImGui::BeginDisabled(m_undo.empty());
    if (ImGui::Button("Annuler")) undo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(m_redo.empty());
    if (ImGui::Button("Rétablir")) redo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Checkbox("Aperçu", &m_preview);
    ImGui::SameLine();
    ImGui::Checkbox("Grille", &m_grid);
    ImGui::SameLine();
    if (ImGui::Checkbox("Dans l'éditeur", &m_doc.showInEditor)) changed();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Afficher les fenêtres de ce document dans l'éditeur WZX, reliées au code");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110 * m_scale);
    int z = (int)std::round(m_zoom * 100);
    if (ImGui::SliderInt("##zoom", &z, 50, 200, "Zoom %d%%")) m_zoom = z / 100.0f;
    ImGui::SameLine(ImGui::GetWindowWidth() - 130 * m_scale);
    if (ImGui::Button("Fermer (F8)")) toggle();
}

void Designer::palette() {
    ImGui::SeparatorText("Widgets");
    static const Kind kinds[] = {Kind::Window, Kind::Panel, Kind::Row, Kind::Column, Kind::Button, Kind::Label, Kind::Checkbox,
                                 Kind::Slider, Kind::Input, Kind::Combo, Kind::Color, Kind::Progress, Kind::Separator, Kind::Spacer};
    float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
    for (int i = 0; i < (int)(sizeof kinds / sizeof kinds[0]); ++i) {
        Kind k = kinds[i];
        if (i % 2) ImGui::SameLine();
        if (ImGui::Button(kindName(k), ImVec2(w, 0))) addNode(k, containerFor(m_sel), ImVec2(0, 0), false);
        m_paletteRects[(int)k] = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        if (ImGui::IsItemHovered() && !ImGui::IsMouseDown(0)) ImGui::SetTooltip("Clic : ajouter dans la sélection\nGlisser : poser sur le canevas");
        if (ImGui::BeginDragDropSource()) {
            int v = (int)k;
            ImGui::SetDragDropPayload("WZUI_KIND", &v, sizeof v);
            ImGui::Text("%s", kindName(k));
            ImGui::EndDragDropSource();
        }
    }
}

void Designer::hierarchyNode(Node& n, int depth) {
    ImGuiTreeNodeFlags fl = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
    if (!isContainer(n.kind)) fl |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (n.uid == m_sel) fl |= ImGuiTreeNodeFlags_Selected;
    ImGui::PushID(n.uid);
    std::string lbl = std::string(kindName(n.kind)) + "  " + n.id;
    if (!n.text.empty() && n.kind != Kind::Row && n.kind != Kind::Column) lbl += "  \"" + n.text + "\"";
    if (!n.visible) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    bool open = ImGui::TreeNodeEx("##n", fl, "%s", lbl.c_str());
    if (!n.visible) ImGui::PopStyleColor();
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) m_sel = n.uid;
    if (n.kind != Kind::Window && ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload("WZUI_NODE", &n.uid, sizeof n.uid);
        ImGui::Text("%s", lbl.c_str());
        ImGui::EndDragDropSource();
    }
    if (isContainer(n.kind) && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("WZUI_NODE")) m_pending = {OpReparent, *(const int*)p->Data, n.uid};
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("WZUI_KIND")) { m_sel = n.uid; m_pending = {100 + *(const int*)p->Data, 0, n.uid}; }
        ImGui::EndDragDropTarget();
    }
    if (ImGui::BeginPopupContextItem("##ctx")) {
        m_sel = n.uid;
        if (ImGui::MenuItem("Dupliquer", "Ctrl+D")) m_pending = {OpDuplicate, n.uid};
        if (ImGui::MenuItem("Supprimer", "Suppr")) m_pending = {OpDelete, n.uid};
        ImGui::Separator();
        if (ImGui::MenuItem("Monter")) m_pending = {OpUp, n.uid};
        if (ImGui::MenuItem("Descendre")) m_pending = {OpDown, n.uid};
        if (n.kind != Kind::Window) {
            ImGui::Separator();
            if (ImGui::MenuItem("Mettre dans une Row")) m_pending = {OpWrapRow, n.uid};
            if (ImGui::MenuItem("Mettre dans une Column")) m_pending = {OpWrapColumn, n.uid};
        }
        ImGui::EndPopup();
    }
    if (open && isContainer(n.kind)) {
        for (auto& c : n.children) hierarchyNode(c, depth + 1);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void Designer::hierarchy() {
    ImGui::SeparatorText("Arborescence");
    m_pending = {};
    for (auto& w : m_doc.windows) hierarchyNode(w, 0);
    Pending p = m_pending;
    m_pending = {};
    if (!p.op) return;
    if (p.op >= 100) { addNode((Kind)(p.op - 100), p.target, ImVec2(0, 0), false); return; }
    pushUndo();
    switch (p.op) {
    case OpDelete: removeNode(p.uid); break;
    case OpDuplicate: duplicate(p.uid); break;
    case OpUp: moveSibling(p.uid, -1); break;
    case OpDown: moveSibling(p.uid, 1); break;
    case OpWrapRow: wrap(p.uid, Kind::Row); break;
    case OpWrapColumn: wrap(p.uid, Kind::Column); break;
    case OpReparent: reparent(p.uid, p.target, -1, ImVec2(0, 0), false); break;
    }
    changed();
}

void Designer::properties() {
    ImGui::SeparatorText("Propriétés");
    auto track = [&](bool edited) {
        if (ImGui::IsItemActivated()) pushUndo();
        if (edited) changed();
    };
    Node* n = m_doc.find(m_sel);
    if (!n) {
        ImGui::TextDisabled("Document");
        track(inputStr("Nom", m_doc.name));
        track(ImGui::Checkbox("Afficher dans l'éditeur", &m_doc.showInEditor));
        ImGui::Spacing();
        ImGui::TextWrapped("Cliquez un élément du canevas ou de l'arborescence pour le modifier. Glissez un widget de la palette sur le canevas pour l'ajouter.");
        ImGui::Spacing();
        ImGui::TextDisabled("Fichier : %s", u8(m_file).c_str());
        return;
    }
    Node* parent = m_doc.parentOf(n->uid);
    ImGui::Text("%s", kindName(n->kind));
    ImGui::SameLine();
    ImGui::TextDisabled("(#%d)", n->uid);
    track(inputStr("id", n->id));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Nom de l'élément pour le code et pour Claude");
    if (n->kind != Kind::Row && n->kind != Kind::Column && n->kind != Kind::Spacer && n->kind != Kind::Separator)
        track(inputStr(n->kind == Kind::Window ? "Titre" : "Texte", n->text));
    if (n->kind != Kind::Window && isContainer(n->kind) == false && n->kind != Kind::Separator && n->kind != Kind::Spacer) {
        track(inputStr("Liaison", n->bind, n->kind == Kind::Button ? "action (ex. play)" : "valeur (ex. lightIntensity)"));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(n->kind == Kind::Button ? "Nom de l'action appelée au clic (Bindings::actions)" : "Nom de la valeur du programme liée au widget (Bindings)");
        track(inputStr("Info-bulle", n->tooltip));
    }
    ImGui::SeparatorText("Disposition");
    bool freePos = n->kind == Kind::Window || (parent && isFreeLayout(parent->kind));
    float pos[2] = {n->x, n->y};
    ImGui::BeginDisabled(!freePos);
    if (ImGui::DragFloat2("Position", pos, 1, -10000, 10000, "%.0f")) { n->x = pos[0]; n->y = pos[1]; changed(); }
    track(false);
    ImGui::EndDisabled();
    if (!freePos && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Placé par sa Row / Column parente");
    float size[2] = {n->w, n->h};
    if (ImGui::DragFloat2("Taille", size, 1, 0, 10000, "%.0f")) { n->w = std::max(0.0f, size[0]); n->h = std::max(0.0f, size[1]); changed(); }
    track(false);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 = automatique");
    if (isContainer(n->kind)) {
        track(ImGui::DragFloat("Espacement", &n->spacing, 0.5f, 0, 100, "%.0f"));
        if (n->kind == Kind::Panel) track(ImGui::DragFloat("Marge", &n->padding, 0.5f, 0, 100, "%.0f"));
    }
    if (n->kind == Kind::Slider || n->kind == Kind::Progress) {
        float r[2] = {n->minV, n->maxV};
        if (ImGui::DragFloat2("Min / Max", r, 0.1f)) { n->minV = r[0]; n->maxV = r[1]; changed(); }
        track(false);
    }
    if (n->kind == Kind::Combo) {
        std::string all;
        for (size_t i = 0; i < n->items.size(); ++i) all += (i ? "\n" : "") + n->items[i];
        ImGui::TextDisabled("Choix (un par ligne)");
        if (inputStr("##items", all, nullptr, true)) {
            n->items.clear();
            size_t a = 0;
            while (a <= all.size()) {
                size_t b = all.find('\n', a);
                if (b == std::string::npos) b = all.size();
                if (b > a) n->items.push_back(all.substr(a, b - a));
                a = b + 1;
            }
            changed();
        }
        track(false);
    }
    ImGui::SeparatorText("Apparence");
    track(ImGui::Checkbox("Visible", &n->visible));
    track(ImGui::Checkbox("##hc", &n->hasColor));
    ImGui::SameLine();
    ImGui::BeginDisabled(!n->hasColor);
    track(ImGui::ColorEdit4("Couleur du texte", &n->color.x, ImGuiColorEditFlags_NoInputs));
    ImGui::EndDisabled();
    if (n->kind == Kind::Button || n->kind == Kind::Panel || n->kind == Kind::Window) {
        track(ImGui::Checkbox("##hb", &n->hasBg));
        ImGui::SameLine();
        ImGui::BeginDisabled(!n->hasBg);
        track(ImGui::ColorEdit4("Fond", &n->bg.x, ImGuiColorEditFlags_NoInputs));
        ImGui::EndDisabled();
    }
    if (n->kind == Kind::Button || n->kind == Kind::Panel) track(ImGui::DragFloat("Arrondi", &n->rounding, 0.2f, 0, 40, "%.0f"));
    ImGui::Spacing();
    if (ImGui::Button("Dupliquer")) { pushUndo(); duplicate(n->uid); changed(); }
    ImGui::SameLine();
    if (ImGui::Button("Supprimer")) { pushUndo(); removeNode(n->uid); changed(); }
}

void Designer::canvas(float s) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##canvas", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleVar();
    ImVec2 p0 = ImGui::GetCursorScreenPos(), avail = ImGui::GetContentRegionAvail();
    m_canvasRect = ImRect(p0, ImVec2(p0.x + avail.x, p0.y + avail.y));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(m_canvasRect.Min, m_canvasRect.Max, IM_COL32(24, 25, 29, 255));
    const ImVec2 origin(p0.x + m_pan.x, p0.y + m_pan.y);
    if (m_grid) {
        float step = 16 * s;
        float sx = std::fmod(origin.x - p0.x, step), sy = std::fmod(origin.y - p0.y, step);
        for (float x = p0.x + sx; x < m_canvasRect.Max.x; x += step)
            for (float y = p0.y + sy; y < m_canvasRect.Max.y; y += step) dl->AddRectFilled(ImVec2(x, y), ImVec2(x + 1, y + 1), IM_COL32(70, 72, 80, 255));
    }

    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##bg", avail, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    // own mouse logic (the disabled widgets drawn over the background button make ImGui's item state unreliable here)
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && m_canvasRect.Contains(io.MousePos) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
    const bool leftClick = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !m_preview;
    const bool midClick = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
    const bool activated = leftClick || midClick;
    const bool active = m_drag != Drag::None && (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Middle));
    const bool released = m_drag != Drag::None && !active;

    // the document
    m_ctx.hits.clear();
    m_ctx.design = !m_preview;
    m_ctx.scale = s;
    m_ctx.bindings = &bindings;
    // zoom: text and widget metrics scaled with the layout, so the canvas looks like the real UI at any zoom
    const ImGuiStyle& st = ImGui::GetStyle();
    ImGui::SetWindowFontScale(m_zoom);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(st.FramePadding.x * m_zoom, st.FramePadding.y * m_zoom));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(st.ItemInnerSpacing.x * m_zoom, st.ItemInnerSpacing.y * m_zoom));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(st.WindowPadding.x * m_zoom, st.WindowPadding.y * m_zoom));
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, st.GrabMinSize * m_zoom);
    if (!m_preview) {   // inert widgets drawn at full opacity (DisabledAlpha is read by BeginDisabled)
        ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 1.0f);
        ImGui::BeginDisabled();
    }
    for (Node& w : m_doc.windows) drawWindowFrame(w, ImVec2(origin.x + w.x * s, origin.y + w.y * s), m_ctx, w.uid == m_sel);
    if (!m_preview) {
        ImGui::EndDisabled();
        ImGui::PopStyleVar();
    }
    ImGui::PopStyleVar(4);
    ImGui::SetWindowFontScale(1.0f);

    ImVec2 m = io.MousePos;
    auto ref = [&](ImVec2 screen, ImVec2 base) { return ImVec2((screen.x - base.x) / s, (screen.y - base.y) / s); };

    // widgets dragged from the palette
    m_dropTarget = 0;
    if (const ImGuiPayload* pl = ImGui::GetDragDropPayload(); pl && pl->IsDataType("WZUI_KIND") && m_canvasRect.Contains(m))
        m_dropTarget = dropContainerAt(m, 0);
    if (ImGui::BeginDragDropTargetCustom(m_canvasRect, ImGui::GetID("##drop"))) {
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("WZUI_KIND", ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
            Kind k = (Kind)*(const int*)pl->Data;
            int c = k == Kind::Window ? 0 : dropContainerAt(m, 0);
            const DrawCtx::Hit* h = hit(c);
            if (c && h) addNode(k, c, ref(m, h->content.Min), true, insertIndex(c, m, 0));
            else addNode(k, 0, ref(m, origin), true);
        }
        ImGui::EndDragDropTarget();
    }

    if (hovered && io.KeyCtrl && io.MouseWheel != 0) m_zoom = std::clamp(m_zoom * (io.MouseWheel > 0 ? 1.1f : 1 / 1.1f), 0.5f, 2.0f);

    m_hover = 0;
    if (hovered && !m_preview)
        for (auto it = m_ctx.hits.rbegin(); it != m_ctx.hits.rend(); ++it)
            if (it->rect.Contains(m)) { m_hover = it->uid; break; }

    // mouse: select, move, resize, pan
    if (activated) {
        m_dragStartMouse = m;
        if (midClick) m_drag = Drag::Pan;
        else {
            const DrawCtx::Hit* sh = hit(m_sel);
            Node* sn = m_doc.find(m_sel);
            ImRect handle = sh ? ImRect(ImVec2(sh->rect.Max.x - 6, sh->rect.Max.y - 6), ImVec2(sh->rect.Max.x + 6, sh->rect.Max.y + 6)) : ImRect();
            if (sh && sn && handle.Contains(m)) {
                m_drag = Drag::Resize;
                m_startW = sn->w > 0 ? sn->w : sh->rect.GetWidth() / s;
                m_startH = sn->h > 0 ? sn->h : sh->rect.GetHeight() / s;
            } else {
                m_sel = m_hover;
                Node* n = m_doc.find(m_sel);
                const DrawCtx::Hit* h = hit(m_sel);
                m_drag = n ? Drag::Pending : Drag::None;
                if (n && h) {
                    m_startX = n->x; m_startY = n->y;
                    m_grab = ImVec2(m.x - h->rect.Min.x, m.y - h->rect.Min.y);
                }
            }
        }
    }
    if (active && !activated) {
        ImVec2 d((m.x - m_dragStartMouse.x) / s, (m.y - m_dragStartMouse.y) / s);
        if (m_drag == Drag::Pan) { m_pan.x += io.MouseDelta.x; m_pan.y += io.MouseDelta.y; }
        if (m_drag == Drag::Pending && (std::fabs(d.x) + std::fabs(d.y)) * s > 3) { pushUndo(); m_drag = Drag::Move; }
        if (Node* n = m_doc.find(m_sel)) {
            Node* p = m_doc.parentOf(m_sel);
            if (m_drag == Drag::Move) {
                if (!p || isFreeLayout(p->kind)) { n->x = snap(m_startX + d.x); n->y = snap(m_startY + d.y); }
                if (p) m_dropTarget = dropContainerAt(m, m_sel);
            } else if (m_drag == Drag::Resize) {
                float mn = n->kind == Kind::Window ? 80.0f : 8.0f;
                n->w = std::max(mn, snap(m_startW + d.x));
                n->h = std::max(n->kind == Kind::Window ? 60.0f : 8.0f, snap(m_startH + d.y));
            }
        }
    }
    if (activated && m_drag == Drag::Resize) pushUndo();
    if (released) {
        if (m_drag == Drag::Move) {
            Node* p = m_doc.parentOf(m_sel);
            if (p && m_dropTarget && m_dropTarget != p->uid) {
                const DrawCtx::Hit* h = hit(m_dropTarget);
                ImVec2 r = h ? ref(ImVec2(m.x - m_grab.x, m.y - m_grab.y), h->content.Min) : ImVec2(0, 0);
                reparent(m_sel, m_dropTarget, insertIndex(m_dropTarget, m, m_sel), r, true);
            } else if (p && !isFreeLayout(p->kind)) {
                reparent(m_sel, p->uid, insertIndex(p->uid, m, m_sel), ImVec2(0, 0), false);
            }
            changed();
        }
        if (m_drag == Drag::Resize) changed();
        m_drag = Drag::None;
        m_dropTarget = 0;
    }

    // overlays
    ImDrawList* fg = ImGui::GetWindowDrawList();
    fg->PushClipRect(m_canvasRect.Min, m_canvasRect.Max, true);
    if (!m_preview) {
        if (m_hover && m_hover != m_sel)
            if (const DrawCtx::Hit* h = hit(m_hover)) fg->AddRect(h->rect.Min, h->rect.Max, IM_COL32(255, 180, 80, 200), 2.0f, 0, 1.0f);
        if (m_dropTarget) {
            if (const DrawCtx::Hit* h = hit(m_dropTarget)) {
                fg->AddRectFilled(h->rect.Min, h->rect.Max, IM_COL32(90, 150, 255, 30), 3);
                fg->AddRect(h->rect.Min, h->rect.Max, IM_COL32(90, 150, 255, 220), 3.0f, 0, 2.0f);
                Node* t = m_doc.find(m_dropTarget);
                if (t && !isFreeLayout(t->kind)) {   // insertion line
                    int idx = insertIndex(t->uid, m, m_drag == Drag::Move ? m_sel : 0), k = 0;
                    bool row = t->kind == Kind::Row;
                    ImVec2 a = h->rect.Min, b = row ? ImVec2(a.x, h->rect.Max.y) : ImVec2(h->rect.Max.x, a.y);
                    for (auto& ch : t->children) {
                        if (m_drag == Drag::Move && ch.uid == m_sel) continue;
                        const DrawCtx::Hit* chh = hit(ch.uid);
                        if (!chh) continue;
                        if (k++ == idx) break;
                        if (row) { a = ImVec2(chh->rect.Max.x + 2, h->rect.Min.y); b = ImVec2(a.x, h->rect.Max.y); }
                        else { a = ImVec2(h->rect.Min.x, chh->rect.Max.y + 2); b = ImVec2(h->rect.Max.x, a.y); }
                    }
                    fg->AddLine(a, b, IM_COL32(90, 150, 255, 255), 3);
                }
            }
        }
        if (const DrawCtx::Hit* h = hit(m_sel)) {
            fg->AddRect(h->rect.Min, h->rect.Max, IM_COL32(110, 220, 140, 255), 2.0f, 0, 2.0f);
            fg->AddRectFilled(ImVec2(h->rect.Max.x - 4, h->rect.Max.y - 4), ImVec2(h->rect.Max.x + 4, h->rect.Max.y + 4), IM_COL32(110, 220, 140, 255));
            if (Node* n = m_doc.find(m_sel)) {
                char b[96];
                snprintf(b, sizeof b, "%s  %.0f, %.0f  %.0f x %.0f", n->id.c_str(), n->x, n->y, h->rect.GetWidth() / s, h->rect.GetHeight() / s);
                ImVec2 z = ImGui::CalcTextSize(b);
                ImVec2 a(h->rect.Min.x, h->rect.Min.y - z.y - 4);
                if (a.y < m_canvasRect.Min.y) a.y = h->rect.Max.y + 2;
                fg->AddRectFilled(a, ImVec2(a.x + z.x + 8, a.y + z.y + 2), IM_COL32(30, 60, 40, 230), 3);
                fg->AddText(ImVec2(a.x + 4, a.y + 1), IM_COL32(200, 255, 210, 255), b);
            }
        }
    }
    if (!m_toast.empty() && ImGui::GetTime() < m_toastUntil) {
        ImVec2 z = ImGui::CalcTextSize(m_toast.c_str());
        ImVec2 a(m_canvasRect.GetCenter().x - z.x / 2 - 10, m_canvasRect.Max.y - z.y - 24);
        fg->AddRectFilled(a, ImVec2(a.x + z.x + 20, a.y + z.y + 10), IM_COL32(20, 22, 26, 235), 5);
        fg->AddText(ImVec2(a.x + 10, a.y + 5), IM_COL32(240, 200, 140, 255), m_toast.c_str());
    }
    if (m_diskChanged) {
        ImGui::SetCursorScreenPos(ImVec2(m_canvasRect.Min.x + 8, m_canvasRect.Min.y + 8));
        ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "Le fichier a changé sur le disque.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Recharger")) { m_dirty = false; m_fileTime = {}; m_nextPoll = 0; }
        ImGui::SameLine();
        if (ImGui::SmallButton("Garder ma version")) saveNow();
    }
    fg->PopClipRect();
    ImGui::EndChild();
}

void Designer::statusBar() {
    ImGui::TextDisabled("%s%s", u8(m_file).c_str(), m_dirty ? "  (modifié)" : "");
    ImGui::SameLine();
    if (!m_lastAction.empty()) ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.40f, 1), "   action : %s", m_lastAction.c_str());
    else ImGui::TextDisabled("   Glisser : déplacer · coin : redimensionner · molette milieu : défiler · Ctrl+molette : zoom · Suppr · Ctrl+D · Ctrl+Z");
}

void Designer::draw(float uiScale) {
    pollDisk();
    if (!bindings.onUnboundAction) bindings.onUnboundAction = [this](const std::string& a) { m_lastAction = a + " (pas encore reliée au code)"; };
    if (m_saveAt >= 0 && ImGui::GetTime() >= m_saveAt && !ImGui::IsAnyItemActive()) saveNow();
    if (ImGui::IsKeyPressed(ImGuiKey_F8, false)) toggle();
    if (!m_visible) return;

    m_scale = uiScale * m_zoom;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    if (m_focusOnShow) { ImGui::SetNextWindowFocus(); m_focusOnShow = false; }
    ImGui::Begin("UI Designer##wzui", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    toolbar();
    ImGui::Separator();
    const float left = 260 * uiScale, right = 320 * uiScale;
    ImGui::BeginChild("##left", ImVec2(left, -ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_Borders);
    palette();
    ImGui::Spacing();
    hierarchy();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##mid", ImVec2(-right - ImGui::GetStyle().ItemSpacing.x, 0));
    canvas(m_scale);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##right", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_Borders);
    properties();
    ImGui::EndChild();
    statusBar();

    // keyboard
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput) {
        bool ctrl = io.KeyCtrl;
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) { if (io.KeyShift) redo(); else undo(); }
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) redo();
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false) && saveNow()) toast("Enregistré");
        if (m_doc.find(m_sel)) {
            if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) { pushUndo(); removeNode(m_sel); changed(); }
            else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) { pushUndo(); duplicate(m_sel); changed(); }
            else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) m_sel = 0;
            else {
                Node* n = m_doc.find(m_sel);
                Node* p = m_doc.parentOf(m_sel);
                float st = io.KeyShift ? 10.0f : 1.0f, dx = 0, dy = 0;
                if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) dx = -st;
                if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) dx = st;
                if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) dy = -st;
                if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) dy = st;
                if ((dx || dy) && (!p || isFreeLayout(p->kind))) { pushUndo(); n->x += dx; n->y += dy; changed(); }
            }
        }
    }
    ImGui::End();
}

void Designer::drawLiveDocs(float uiScale) {
    if (m_visible) return;
    if (!bindings.onUnboundAction) bindings.onUnboundAction = [this](const std::string& a) { toast("Action « " + a + " » : pas encore reliée au code"); };
    if (m_doc.showInEditor) drawLive(m_doc, bindings, uiScale);
    for (Doc& d : m_live) drawLive(d, bindings, uiScale);
}

} // namespace wzui
