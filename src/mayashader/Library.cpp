#include "Library.h"
#include "Flatten.h"
#include "MayaLink.h"
#include "Preview.h"
#include "app/Theme.h"
#include "game/NodeEditor.h"
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace mayashader {

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;
using app::Json;

namespace {

std::string utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}
std::string readText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
long long fileTime(const std::wstring& p) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &a)) return 0;
    return ((long long)a.ftLastWriteTime.dwHighDateTime << 32) | a.ftLastWriteTime.dwLowDateTime;
}
std::string safeName(std::string s) {
    for (char& c : s)
        if (strchr("<>:\"/\\|?*", c) || (unsigned char)c < 32) c = '_';
    while (!s.empty() && (s.back() == ' ' || s.back() == '.')) s.pop_back();
    return s.empty() ? "Material" : s;
}

bool savePng(const std::wstring& path, const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h) {
    ComPtr<IWICImagingFactory> f;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) return false;
    ComPtr<IWICStream> st;
    f->CreateStream(&st);
    if (FAILED(st->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) return false;
    ComPtr<IWICBitmapEncoder> enc;
    f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
    enc->Initialize(st.Get(), WICBitmapEncoderNoCache);
    ComPtr<IWICBitmapFrameEncode> fr;
    ComPtr<IPropertyBag2> props;
    enc->CreateNewFrame(&fr, &props);
    fr->Initialize(props.Get());
    fr->SetSize(w, h);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppRGBA;
    fr->SetPixelFormat(&fmt);
    std::vector<uint8_t> px = rgba;
    if (fmt != GUID_WICPixelFormat32bppRGBA)
        for (size_t i = 0; i + 3 < px.size(); i += 4) std::swap(px[i], px[i + 2]);
    fr->WritePixels(h, w * 4, (UINT)px.size(), px.data());
    fr->Commit();
    enc->Commit();
    return true;
}

// Thumbnail of a graph: its Material Output (the shader ball), or nothing.
bool graphImage(const game::Graph& g, bool acescg, int size, std::vector<uint8_t>& px) {
    for (const game::Node& n : g.nodes) {
        if (n.type != "out.material" || !n.def) continue;
        game::Graph flat = flatten(g);
        FlatRef r = resolveIn(g, {}, n.id, n.def->findIn("Surface"));
        if (!r.valid()) return false;
        return renderThumb(flat, r.node, r.pin, r.constant ? &r.value : nullptr, acescg, size, px);
    }
    return false;
}

std::wstring pickFolder(const std::wstring& start) {
    ComPtr<IFileOpenDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return {};
    DWORD o = 0;
    dlg->GetOptions(&o);
    dlg->SetOptions(o | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    ComPtr<IShellItem> si;
    if (!start.empty() && SUCCEEDED(SHCreateItemFromParsingName(start.c_str(), nullptr, IID_PPV_ARGS(&si)))) dlg->SetFolder(si.Get());
    if (FAILED(dlg->Show(GetActiveWindow()))) return {};
    ComPtr<IShellItem> res;
    if (FAILED(dlg->GetResult(&res))) return {};
    PWSTR p = nullptr;
    std::wstring out;
    if (SUCCEEDED(res->GetDisplayName(SIGDN_FILESYSPATH, &p))) { out = p; CoTaskMemFree(p); }
    return out;
}

} // namespace

void Library::init(app::ImGuiLayer& gui, const std::wstring& settingsFile, Host host) {
    m_gui = &gui;
    m_host = std::move(host);
    m_settingsFile = settingsFile;
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    m_scenePath = std::wstring(tmp) + L"wzx_shader_scene.json";
    m_importPath = std::wstring(tmp) + L"wzx_shader_import.json";
    // library folder: the last one used, else Documents\WZX_ShaderLibrary
    std::string saved = readText(m_settingsFile);
    while (!saved.empty() && (saved.back() == '\n' || saved.back() == '\r')) saved.pop_back();
    if (!saved.empty()) m_root = wide(saved);
    else {
        PWSTR docs = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs))) { m_root = std::wstring(docs) + L"\\WZX_ShaderLibrary"; CoTaskMemFree(docs); }
    }
    std::error_code ec;
    fs::create_directories(fs::path(m_root) / "Default", ec);
    strncpy_s(m_rootBuf, utf8(m_root).c_str(), _TRUNCATE);
    scan();
}

ImTextureID Library::loadThumb(const std::wstring& png) {
    ComPtr<IWICImagingFactory> f;
    ComPtr<IWICBitmapDecoder> dec;
    ComPtr<IWICBitmapFrameDecode> fr;
    ComPtr<IWICFormatConverter> conv;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f))) ||
        FAILED(f->CreateDecoderFromFilename(png.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) ||
        FAILED(dec->GetFrame(0, &fr)) || FAILED(f->CreateFormatConverter(&conv)) ||
        FAILED(conv->Initialize(fr.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)))
        return 0;
    UINT w = 0, h = 0;
    conv->GetSize(&w, &h);
    if (!w || !h || w > 2048 || h > 2048) return 0;
    std::vector<uint8_t> px((size_t)w * h * 4);
    conv->CopyPixels(nullptr, w * 4, (UINT)px.size(), px.data());
    return m_gui->createTexture(px.data(), w, h);
}

ImTextureID Library::graphThumb(const std::string& graphJson, int size) {
    game::Graph g;
    if (graphJson.empty() || !g.fromJson(graphJson)) return 0;
    g.refreshDefs();
    std::vector<uint8_t> px;
    if (!graphImage(g, m_host.acescg, size, px)) return 0;
    return m_gui->createTexture(px.data(), size, size);
}

void Library::scan() {
    for (Item& it : m_items) if (it.thumb) m_gui->releaseTexture(it.thumb);
    m_items.clear();
    m_categories.clear();
    std::error_code ec;
    for (auto& cat : fs::directory_iterator(m_root, ec)) {
        if (!cat.is_directory(ec)) continue;
        std::string catName = utf8(cat.path().filename().wstring());
        m_categories.push_back(catName);
        for (auto& mat : fs::directory_iterator(cat.path(), ec)) {
            if (!mat.is_directory(ec) || !fs::exists(mat.path() / "material.graph.json", ec)) continue;
            Item it;
            it.folder = mat.path().wstring();
            it.category = catName;
            it.name = utf8(mat.path().filename().wstring());
            Json meta;
            if (Json::parse(readText(mat.path() / "metadata.json"), meta)) {
                it.name = meta["name"].str(it.name);
                it.tags = meta["tags"].str();
            }
            fs::path png = mat.path() / "thumbnail.png";
            if (fs::exists(png, ec)) it.thumb = loadThumb(png.wstring());
            m_items.push_back(it);
        }
    }
    std::sort(m_categories.begin(), m_categories.end());
    std::sort(m_items.begin(), m_items.end(), [](const Item& a, const Item& b) { return a.category != b.category ? a.category < b.category : a.name < b.name; });
    if (m_category >= (int)m_categories.size()) m_category = -1;
}

bool Library::saveCurrent(const std::string& name, const std::string& category, const std::string& tags) {
    std::error_code ec;
    fs::path dir = fs::path(m_root) / wide(safeName(category)) / wide(safeName(name));
    fs::create_directories(dir, ec);
    if (ec) { m_status = "Cannot create " + utf8(dir.wstring()); return false; }
    const game::Graph& g = m_host.graph();
    {
        std::ofstream f(dir / "material.graph.json", std::ios::binary);
        if (!f) { m_status = "Cannot write the graph"; return false; }
        f << g.toJson();
    }
    {
        char date[32];
        time_t t = time(nullptr);
        tm lt;
        localtime_s(&lt, &t);
        strftime(date, sizeof date, "%Y-%m-%d %H:%M", &lt);
        std::ofstream f(dir / "metadata.json", std::ios::binary);
        f << "{\n  \"name\": \"" << app::jsonEscape(name) << "\",\n  \"category\": \"" << app::jsonEscape(category) << "\",\n  \"tags\": \""
          << app::jsonEscape(tags) << "\",\n  \"saved\": \"" << date << "\",\n  \"format\": \"wzx-graph\"\n}\n";
    }
    std::vector<uint8_t> px;
    const int S = 256;
    if (graphImage(g, m_host.acescg, S, px)) savePng((dir / "thumbnail.png").wstring(), px, S, S);
    m_status = "Saved in " + utf8(dir.wstring());
    scan();
    return true;
}

void Library::freeSceneThumbs() {
    for (SceneItem& s : m_scene) if (s.thumb) m_gui->releaseTexture(s.thumb);
}

void Library::pollScene() {
    if (m_scenePending && fileTime(m_scenePath) > m_sceneTime) {
        Json j;
        if (Json::parse(readText(m_scenePath), j) && j.type == Json::Array) {
            freeSceneThumbs();
            m_scene.clear();
            for (const Json& m : j.a) {
                SceneItem s;
                s.sg = m["sg"].str();
                s.shader = m["shader"].str();
                s.shaderType = m["shaderType"].str();
                s.wzx = m["wzx"].str();
                s.graph = m["graph"].str();
                s.objects = (int)m["objects"].num();
                if (!s.graph.empty()) s.thumb = graphThumb(s.graph, 96);
                m_scene.push_back(s);
            }
            m_scenePending = false;
            m_status = std::to_string(m_scene.size()) + " material(s) in the scene";
        }
    }
    if (m_importPending && fileTime(m_importPath) > m_importTime) {
        Json j;
        if (Json::parse(readText(m_importPath), j) && j.type == Json::Object) {
            m_importPending = false;
            std::string key = j["key"].str();
            std::string msg = "Converted to " + key + " (" + std::to_string((int)j["nodes"].num()) + " nodes)";
            if (!j["unknown"].a.empty()) {
                msg += ", no rule for:";
                for (const Json& u : j["unknown"].a) msg += " " + u["node"].str() + " (" + u["type"].str() + ")";
            }
            m_status = msg;
            if (!key.empty()) m_host.send("import wzx_shader_panel as _p; _p.open_graph(" + pyStr(key) + ")");
        }
    }
}

void Library::drawDisk(float dpi) {
    // library folder
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Folder");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-260 * dpi);
    if (ImGui::InputText("##libroot", m_rootBuf, sizeof m_rootBuf, ImGuiInputTextFlags_EnterReturnsTrue)) {
        m_root = wide(m_rootBuf);
        std::ofstream(m_settingsFile, std::ios::binary) << m_rootBuf;
        scan();
    }
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
        std::wstring p = pickFolder(m_root);
        if (!p.empty()) {
            m_root = p;
            strncpy_s(m_rootBuf, utf8(p).c_str(), _TRUNCATE);
            std::ofstream(m_settingsFile, std::ios::binary) << m_rootBuf;
            scan();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Rescan")) scan();
    ImGui::SameLine();
    if (ImGui::Button("Explorer")) ShellExecuteW(nullptr, L"open", m_root.c_str(), nullptr, nullptr, SW_SHOWNORMAL);

    // save the current material
    if (ImGui::CollapsingHeader("Save the current material in the library")) {
        if (!m_saveName[0]) strncpy_s(m_saveName, m_host.graph().name.c_str(), _TRUNCATE);
        ImGui::SetNextItemWidth(200 * dpi);
        ImGui::InputText("Name", m_saveName, sizeof m_saveName);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160 * dpi);
        ImGui::InputText("##cat", m_saveCat, sizeof m_saveCat);
        ImGui::SameLine();
        if (ImGui::BeginCombo("Category", nullptr, ImGuiComboFlags_NoPreview)) {
            for (const std::string& c : m_categories)
                if (ImGui::Selectable(c.c_str())) strncpy_s(m_saveCat, c.c_str(), _TRUNCATE);
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(220 * dpi);
        ImGui::InputTextWithHint("##tags", "tags (metal, worn...)", m_saveTags, sizeof m_saveTags);
        ImGui::SameLine();
        if (ImGui::Button("Save")) saveCurrent(m_saveName, m_saveCat[0] ? m_saveCat : "Default", m_saveTags);
    }
    ImGui::Separator();

    // categories (left), materials (grid)
    ImGui::BeginChild("##cats", ImVec2(170 * dpi, 0), ImGuiChildFlags_Borders);
    if (ImGui::Selectable("All", m_category < 0)) m_category = -1;
    for (int i = 0; i < (int)m_categories.size(); ++i) {
        int count = 0;
        for (const Item& it : m_items) count += it.category == m_categories[i];
        char lbl[160];
        snprintf(lbl, sizeof lbl, "%s  (%d)", m_categories[i].c_str(), count);
        if (ImGui::Selectable(lbl, m_category == i)) m_category = i;
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##grid", ImVec2(0, 0));
    ImGui::SetNextItemWidth(260 * dpi);
    ImGui::InputTextWithHint("##libsearch", "Search name or tag...", m_search, sizeof m_search);
    std::string q = m_search;
    for (char& c : q) c = (char)tolower((unsigned char)c);
    const float card = 150 * dpi, gap = 10 * dpi;
    int perRow = std::max(1, (int)((ImGui::GetContentRegionAvail().x + gap) / (card + gap)));
    int col = 0, shown = 0;
    for (size_t i = 0; i < m_items.size(); ++i) {
        const Item& it = m_items[i];
        if (m_category >= 0 && it.category != m_categories[m_category]) continue;
        std::string hay = it.name + " " + it.tags;
        for (char& c : hay) c = (char)tolower((unsigned char)c);
        if (!q.empty() && hay.find(q) == std::string::npos) continue;
        if (col) ImGui::SameLine(0, gap);
        ImGui::PushID((int)i);
        ImGui::BeginGroup();
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGui::InvisibleButton("##card", ImVec2(card, card + 44 * dpi));
        bool hov = ImGui::IsItemHovered();
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            m_host.openInGraph(readText(fs::path(it.folder) / "material.graph.json"));
            m_status = "Opened " + it.name + " in the current material";
        }
        dl->AddRectFilled(p, ImVec2(p.x + card, p.y + card + 44 * dpi), hov ? app::th::hover() : app::th::card(), 5 * dpi);
        if (it.thumb) dl->AddImageRounded(it.thumb, ImVec2(p.x + 6 * dpi, p.y + 6 * dpi), ImVec2(p.x + card - 6 * dpi, p.y + card - 6 * dpi),
                                          ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 4 * dpi);
        else dl->AddCircleFilled(ImVec2(p.x + card * 0.5f, p.y + card * 0.5f), card * 0.3f, app::th::field());
        dl->AddText(ImVec2(p.x + 8 * dpi, p.y + card), app::th::text(), it.name.c_str());
        std::string sub = it.category + (it.tags.empty() ? "" : "  -  " + it.tags);
        dl->AddText(ImVec2(p.x + 8 * dpi, p.y + card + 18 * dpi), app::th::dim(), sub.c_str());
        if (hov) ImGui::SetTooltip("%s\nDouble click: open in the current material\nRight click: more", it.name.c_str());
        if (ImGui::BeginPopupContextItem("##ctx")) {
            if (ImGui::MenuItem("Open in the current material")) m_host.openInGraph(readText(fs::path(it.folder) / "material.graph.json"));
            if (ImGui::MenuItem("Show in Explorer")) ShellExecuteW(nullptr, L"open", it.folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            ImGui::EndPopup();
        }
        ImGui::EndGroup();
        ImGui::PopID();
        if (++col >= perRow) col = 0;
        ++shown;
    }
    if (!shown) ImGui::TextDisabled(m_items.empty() ? "The library is empty: save the current material with the panel above." : "No material matches.");
    ImGui::EndChild();
}

void Library::drawScene(float dpi) {
    const bool connected = m_host.mayaConnected();
    if (!m_sceneAsked && connected) {   // first visit: ask Maya
        m_sceneAsked = true;
        m_sceneTime = fileTime(m_scenePath);
        m_scenePending = m_host.send("import wzx_shader_bridge as _w; _w.scene_materials(" + pyStr(utf8(m_scenePath)) + ")");
    }
    ImGui::BeginDisabled(!connected);
    if (ImGui::Button("Refresh")) {
        m_sceneTime = fileTime(m_scenePath);
        m_scenePending = m_host.send("import wzx_shader_bridge as _w; _w.scene_materials(" + pyStr(utf8(m_scenePath)) + ")");
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled(connected ? (m_scenePending ? "asking Maya..." : "Materials of the Maya scene") : "Maya not connected");
    ImGui::Separator();
    const std::string current = m_host.currentKey();
    ImGui::BeginChild("##scenelist", ImVec2(0, 0));
    const float th = 72 * dpi;
    for (size_t i = 0; i < m_scene.size(); ++i) {
        const SceneItem& s = m_scene[i];
        ImGui::PushID((int)i);
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        bool isCurrent = !s.wzx.empty() && s.wzx == current;
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + th + 12 * dpi), isCurrent ? app::th::select() : app::th::card(), 5 * dpi);
        if (s.thumb) dl->AddImageRounded(s.thumb, ImVec2(p.x + 6 * dpi, p.y + 6 * dpi), ImVec2(p.x + 6 * dpi + th, p.y + 6 * dpi + th), ImVec2(0, 0), ImVec2(1, 1),
                                         IM_COL32_WHITE, 4 * dpi);
        else dl->AddCircleFilled(ImVec2(p.x + 6 * dpi + th * 0.5f, p.y + 6 * dpi + th * 0.5f), th * 0.36f, app::th::field());
        float tx = p.x + th + 18 * dpi;
        dl->AddText(ImVec2(tx, p.y + 8 * dpi), app::th::text(), s.sg.c_str());
        std::string sub = (s.shader.empty() ? std::string("(no shader)") : s.shader + "  (" + s.shaderType + ")") + "   " + std::to_string(s.objects) + " object(s)";
        dl->AddText(ImVec2(tx, p.y + 28 * dpi), app::th::dim(), sub.c_str());
        if (!s.wzx.empty()) dl->AddText(ImVec2(tx, p.y + 48 * dpi), app::th::accent(), isCurrent ? "WZX graph (open)" : ("WZX graph: " + s.wzx).c_str());
        // actions on the right
        ImGui::SetCursorScreenPos(ImVec2(p.x + w - 420 * dpi, p.y + 22 * dpi));
        ImGui::BeginDisabled(!connected);
        if (!s.wzx.empty()) {
            ImGui::BeginDisabled(isCurrent);
            if (ImGui::Button("Open graph", ImVec2(130 * dpi, 0))) m_host.send("import wzx_shader_panel as _p; _p.open_graph(" + pyStr(s.wzx) + ")");
            ImGui::EndDisabled();
        } else {
            ImGui::BeginDisabled(s.shader.empty());
            if (ImGui::Button("Convert to graph", ImVec2(130 * dpi, 0))) {
                m_importTime = fileTime(m_importPath);
                m_importPending = m_host.send("import wzx_shader_bridge as _w; _w.import_material(" + pyStr(s.sg) + ", " + pyStr(utf8(m_importPath)) + ")");
                m_status = "Converting " + s.sg + "...";
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Builds a WZX graph from this material with the equivalences (the original stays as it is);\nnodes without a rule are listed");
        }
        ImGui::SameLine();
        if (ImGui::Button("Select objects", ImVec2(130 * dpi, 0))) m_host.send("import wzx_shader_bridge as _w; _w.select_members(" + pyStr(s.sg) + ")");
        ImGui::SameLine();
        if (ImGui::Button("Assign", ImVec2(130 * dpi, 0))) m_host.send("import wzx_shader_bridge as _w; _w.assign_sg(" + pyStr(s.sg) + ")");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Assigns this material to the objects selected in Maya");
        ImGui::EndDisabled();
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + th + 18 * dpi));
        ImGui::Dummy(ImVec2(1, 1));
        ImGui::PopID();
    }
    if (m_scene.empty()) ImGui::TextDisabled(connected ? "No material listed yet (Refresh)." : "Open the panel from Maya to list the scene materials.");
    ImGui::EndChild();
}

void Library::draw(ImVec2 pos, ImVec2 size, float dpi) {
    pollScene();
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("##library_view", nullptr, flags);
    ImGui::TextUnformatted("Shader Library");
    ImGui::SameLine();
    if (!m_status.empty()) ImGui::TextDisabled("%s", m_status.c_str());
    if (ImGui::BeginTabBar("##libtabs")) {
        const int force = m_forceTab;
        m_forceTab = -1;
        if (ImGui::BeginTabItem("Disk library", nullptr, force == 0 ? ImGuiTabItemFlags_SetSelected : 0)) { m_tab = 0; drawDisk(dpi); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Scene", nullptr, force == 1 ? ImGuiTabItemFlags_SetSelected : 0)) { m_tab = 1; drawScene(dpi); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

} // namespace mayashader

namespace mayashader {
std::string Library::utf8Root() const { return utf8(m_root); }
} // namespace mayashader
