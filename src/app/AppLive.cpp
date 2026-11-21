// WZX Live in the editor: Tools > Appareils (WZX Live). The game being edited is exported (as Build does) into
// %LOCALAPPDATA%\WZX Engine\live\export, compared with what the app on the device can play (its protocol, content
// version, graph node types and content features, sent when it connects), then sent: only the files it lacks.
// The device plays it at once, sends its log back, and screenshots on request. Command API: device.* (and the MCP
// tools wzx_device_*), so that Claude can check what the device shows. Protocol and device side: src/game/LiveLink.h.
#include "AiControl.h"
#include "App.h"
#include "GameHost.h"
#include "LiveClient.h"
#include "Theme.h"
#include "game/LiveLink.h"
#include "vp3d/AssetImport.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace app {

namespace fs = std::filesystem;

namespace {

std::string jq(const std::string& s) { return "\"" + jsonEscape(s) + "\""; }

fs::path liveRoot() { return fs::path(apiFilePath()).parent_path() / L"live"; }
fs::path devicesFile() { return fs::path(apiFilePath()).parent_path() / L"live_devices.json"; }

std::string readAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// "l'iPhone", "l'iPad", "le player Windows": the device in the warnings
std::string deviceWord(const Json& info) {
    const std::string t = info["device"].str();
    return t == "Phone" ? "l'iPhone" : t == "Tablet" ? "l'iPad" : "le player Windows";
}

// "192.168.1.20:47800" -> host, port
bool parseAddress(const std::string& text, std::string& host, int& port) {
    std::string t = text;
    t.erase(std::remove(t.begin(), t.end(), ' '), t.end());
    if (t.empty()) return false;
    const size_t c = t.rfind(':');
    port = live::kDefaultPort;
    if (c != std::string::npos) {
        port = atoi(t.c_str() + c + 1);
        t = t.substr(0, c);
    }
    host = t;
    return !host.empty() && port > 0 && port < 65536;
}

} // namespace

// ---------------------------------------------------------------------------- remembered devices

void GameHost::liveLoadDevices() {
    m_liveKnown.clear();
    Json j;
    if (!Json::parse(readAll(devicesFile()), j)) return;
    for (const Json& d : j["devices"].a) {
        LiveKnown k;
        k.id = d["id"].str();
        k.name = d["name"].str();
        k.host = d["host"].str();
        k.port = (int)d["port"].num(live::kDefaultPort);
        k.code = d["code"].str();
        k.type = d["type"].str();
        m_liveKnown.push_back(k);
    }
}

void GameHost::liveSaveDevices() {
    std::ostringstream s;
    s << "{\n  \"devices\": [";
    for (size_t i = 0; i < m_liveKnown.size(); ++i) {
        const LiveKnown& k = m_liveKnown[i];
        s << (i ? ",\n    " : "\n    ") << "{\"id\": " << jq(k.id) << ", \"name\": " << jq(k.name) << ", \"type\": " << jq(k.type)
          << ", \"host\": " << jq(k.host) << ", \"port\": " << k.port << ", \"code\": " << jq(k.code) << "}";
    }
    s << "\n  ]\n}\n";
    std::error_code ec;
    fs::create_directories(devicesFile().parent_path(), ec);
    std::ofstream(devicesFile(), std::ios::binary) << s.str();
}

// The devices of the list: this PC's player, the remembered ones, the ones Bonjour sees (merged by id, else address).
std::vector<GameHost::LiveKnown> GameHost::liveDevices() const {
    std::vector<LiveKnown> out;
    auto add = [&](const LiveKnown& d) {
        for (LiveKnown& e : out)
            if ((!d.id.empty() && e.id == d.id) || (e.host == d.host && e.port == d.port)) {
                if (!d.host.empty()) { e.host = d.host; e.port = d.port; }
                if (!d.name.empty()) e.name = d.name;
                if (e.id.empty()) e.id = d.id;
                if (e.code.empty()) e.code = d.code;
                if (!d.type.empty()) e.type = d.type;
                e.found |= d.found;
                return;
            }
        out.push_back(d);
    };
    LiveKnown local;
    local.name = "Ce PC (WZXPlayer --live)";
    local.host = "127.0.0.1";
    local.port = live::kDefaultPort;
    local.type = "Desktop";
    add(local);
    for (const LiveKnown& k : m_liveKnown) add(k);
    if (m_live)
        for (const LiveDevice& d : m_live->found()) {
            LiveKnown k;
            k.id = d.id;
            k.name = d.name;
            k.host = d.host;
            k.port = d.port;
            k.type = d.type;
            k.found = true;
            add(k);
        }
    return out;
}

void GameHost::liveRemember(const Json& info, const std::string& code) {
    LiveKnown k;
    k.id = info["id"].str();
    k.name = info["name"].str();
    k.type = info["device"].str();
    k.host = m_live->host();
    k.port = m_live->port();
    k.code = code;
    m_liveKnown.erase(std::remove_if(m_liveKnown.begin(), m_liveKnown.end(), [&](const LiveKnown& e) {
        return (!k.id.empty() && e.id == k.id) || (e.host == k.host && e.port == k.port);
    }), m_liveKnown.end());
    m_liveKnown.insert(m_liveKnown.begin(), k);
    liveSaveDevices();
}

// ---------------------------------------------------------------------------- export and compatibility

// The game as the device plays it: scene.wzx and its files, graph.json, game.json, and the houses of the village
// (assets/HOUSE: the live folder of the device has nothing of the app's own data).
bool GameHost::exportLive(fs::path& out, std::string& err) {
    if (m_player) { err = "pas d'envoi depuis un jeu exporté"; return false; }
    if (m_playing) stop();
    out = liveRoot() / L"export";
    std::error_code ec;
    fs::remove_all(out, ec);
    fs::create_directories(out, ec);
    if (ec) { err = "création du dossier impossible : " + ec.message(); return false; }
    if (!exportScene(out, err)) return false;
    if (!m_graph.save((out / "graph.json").wstring())) { err = "écriture du graphe impossible"; return false; }
    const fs::path house = fs::path(m_app.m_exeDir) / L"assets" / L"HOUSE";
    if (fs::exists(house)) {
        fs::create_directories(out / L"assets", ec);
        fs::copy(house, out / L"assets" / L"HOUSE", fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        if (ec) { err = "copie de assets/HOUSE impossible : " + ec.message(); return false; }
    }
    std::string title = "WZX Game";
    if (!m_profiles.empty()) title = m_profiles[std::min<size_t>(m_profileIdx, m_profiles.size() - 1)].title;
    std::ofstream f(out / "game.json", std::ios::binary);
    f << "{\n  \"format\": \"wzx-game\",\n  \"platform\": \"live\",\n  \"title\": " << jq(title + " (" + m_app.m_scene.name + ")")
      << ",\n  \"scene\": \"scene.wzx\",\n  \"graph\": \"graph.json\"\n}\n";
    if (!f) { err = "écriture de game.json impossible"; return false; }
    return true;
}

GameHost::LiveCheck GameHost::liveCheck(const fs::path& folder, const Json& info) const {
    LiveCheck c;
    const std::string dev = deviceWord(info);
    const bool ios = info["device"].str() != "Desktop";
    const std::string rebuild = ios ? " Il faut recompiler et réinstaller l'IPA (Build > iOS, GitHub Actions, Sideloadly)."
                                    : " Il faut recompiler le player (WZXPlayer).";
    const int proto = (int)info["protocol"].num(0), content = (int)info["content"].num(0);
    if (proto != live::kProtocol) {
        c.blocked = true;
        c.warnings.push_back("L'app sur " + dev + " parle une autre version de WZX Live (protocole " + std::to_string(proto) + ", éditeur " +
                             std::to_string(live::kProtocol) + ") : l'envoi est impossible." + rebuild);
        return c;
    }
    if (content < live::kContentVersion)
        c.warnings.push_back("L'app sur " + dev + " est trop ancienne : format de contenu " + std::to_string(content) + ", l'éditeur écrit le format " +
                             std::to_string(live::kContentVersion) + "." + rebuild);
    else if (content > live::kContentVersion)
        c.infos.push_back("L'app sur " + dev + " est plus récente que cet éditeur (format " + std::to_string(content) + ").");
    std::vector<std::string> features, nodes;
    live::contentNeeds(folder, features, nodes);
    std::set<std::string> devNodes, devFeatures;
    for (const Json& n : info["nodes"].a) devNodes.insert(n.str());
    for (const Json& f : info["features"].a) devFeatures.insert(f.str());
    const std::vector<std::string> mine = live::engineFeatures();
    for (const std::string& n : nodes) {
        if (devNodes.count(n)) continue;
        const game::NodeDef* d = game::findNodeDef(n);
        c.warnings.push_back("L'app sur " + dev + " est trop ancienne : le n\xC5\x93ud \"" + (d ? d->label + "\" (" + n + ")" : n + "\"") +
                             " n'existe pas dans sa version, le jeu ne démarrera pas." + rebuild);
        c.missingNodes.push_back(n);
    }
    for (const std::string& f : features) {
        if (devFeatures.count(f)) continue;
        if (std::find(mine.begin(), mine.end(), f) != mine.end())
            c.warnings.push_back("L'app sur " + dev + " est trop ancienne : elle ne lit pas " + live::featureLabel(f) + "." + rebuild);
        else
            c.infos.push_back("Non lu par le player (toutes versions) : " + live::featureLabel(f) + ".");
    }
    if (c.warnings.empty() && info["signature"].str() == live::engineSignature()) c.infos.push_back("App à jour : même version du moteur que l'éditeur.");
    return c;
}

// Export, check, send. force: send even with warnings (never when blocked).
bool GameHost::liveSend(bool force, std::string& err) {
    if (!m_live || m_live->state() != LiveClient::Connected) { err = "aucun appareil connecté"; return false; }
    if (m_live->progress().active) { err = "un envoi est déjà en cours"; return false; }
    fs::path out;
    if (!exportLive(out, err)) return false;
    m_liveCheck = liveCheck(out, m_live->info());
    m_liveChecked = true;
    if (m_liveCheck.blocked) { err = m_liveCheck.warnings.empty() ? "envoi impossible" : m_liveCheck.warnings[0]; return false; }
    if (!m_liveCheck.warnings.empty() && !force) {
        m_liveConfirm = true;   // the window asks: send anyway?
        err = "l'app de l'appareil est trop ancienne pour ce contenu (voir les avertissements)";
        return false;
    }
    m_liveConfirm = false;
    m_live->send(out);
    m_app.log("WZX Live : envoi à " + m_live->info()["name"].str());
    return true;
}

void GameHost::openLiveWindow() {
    if (m_player) return;
    if (!m_live) m_live = std::make_unique<LiveClient>();
    if (m_liveKnown.empty()) liveLoadDevices();
    m_live->startBrowse();
    m_showLive = true;
}

// ---------------------------------------------------------------------------- each frame

void GameHost::liveUpdate() {
    if (!m_live) return;
    const LiveClient::State st = m_live->state();
    // a connection that succeeded: the code is remembered for this device
    if (st == LiveClient::Connected && m_liveRememberPending) {
        m_liveRememberPending = false;
        liveRemember(m_live->info(), m_liveCode);
    }
    // a send finished
    const LiveClient::Progress p = m_live->progress();
    if (p.serial != m_liveSendSerial) {
        m_liveSendSerial = p.serial;
        toast(fromUtf8(p.ok ? "WZX Live : jeu lancé sur l'appareil" : "WZX Live : échec de l'envoi : " + p.message));
        m_app.log("WZX Live : " + std::string(p.ok ? "jeu lancé sur l'appareil" : "échec : " + p.message), p.ok ? 0 : 2);
        if (p.ok && m_liveAutoShot) m_live->requestShot(1280);
        if (m_liveSendReq) {
            std::ostringstream js;
            js << "{\"ok\": " << (p.ok ? "true" : "false") << ", \"message\": " << jq(p.message) << ", \"files\": " << p.files
               << ", \"bytes\": " << p.bytes << ", \"warnings\": " << live::jsonList(m_liveCheck.warnings) << "}";
            m_liveSendReq->response = js.str();
            m_liveSendReq->status = 200;
            m_server.complete(m_liveSendReq);
            m_liveSendReq.reset();
        }
    }
    // a screenshot arrived: saved as a JPEG, shown in the window
    const uint64_t shot = m_live->shotSerial();
    if (shot != m_liveShotSerial) {
        m_liveShotSerial = shot;
        int w = 0, h = 0;
        std::vector<uint8_t> jpg = m_live->shot(w, h);
        std::error_code ec;
        fs::create_directories(liveRoot() / L"shots", ec);
        const fs::path file = liveRoot() / L"shots" / L"device.jpg";
        std::ofstream(file, std::ios::binary).write((const char*)jpg.data(), (std::streamsize)jpg.size());
        m_liveShotPath = toUtf8(file.wstring());
        vp3d::Texture t;
        if (vp3d::loadImage(file.wstring(), t, nullptr) && t.width > 0) {
            if (m_liveShotTex) m_app.m_gui.releaseTexture(m_liveShotTex);
            m_liveShotTex = m_app.m_gui.createTexture(t.rgba.data(), t.width, t.height);
            m_liveShotW = (int)t.width;
            m_liveShotH = (int)t.height;
        }
        if (m_liveShotReq) {
            m_liveShotReq->response = "{\"ok\": true, \"path\": " + jq(m_liveShotPath) + ", \"width\": " + std::to_string(w) + ", \"height\": " +
                                      std::to_string(h) + "}";
            m_liveShotReq->status = 200;
            m_server.complete(m_liveShotReq);
            m_liveShotReq.reset();
        }
    }
    // API requests waiting on the device
    const double now = (double)GetTickCount64() / 1000.0;
    auto expire = [&](std::shared_ptr<game::CommandRequest>& r, const char* what) {
        if (!r) return;
        const bool lost = st != LiveClient::Connected && st != LiveClient::Connecting;
        if (!lost && now - m_liveReqTime < 100.0) return;
        r->response = std::string("{\"ok\": false, \"error\": ") + jq(lost ? "appareil déconnecté : " + m_live->error() : std::string(what) + " : pas de réponse") + "}";
        r->status = 200;
        m_server.complete(r);
        r.reset();
    };
    expire(m_liveSendReq, "envoi");
    expire(m_liveShotReq, "capture");
    if (m_liveConnectReq && st != LiveClient::Connecting) {
        m_liveConnectReq->response = liveStateJson();
        m_liveConnectReq->status = 200;
        m_server.complete(m_liveConnectReq);
        m_liveConnectReq.reset();
    }
}

std::string GameHost::liveStateJson() const {
    static const char* kStates[] = {"off", "connecting", "connected", "code-needed", "bad-code", "failed"};
    std::ostringstream js;
    const LiveClient::State st = m_live ? m_live->state() : LiveClient::Off;
    js << "{\"ok\": " << (st == LiveClient::Connected ? "true" : "false") << ", \"state\": " << jq(kStates[st]);
    if (m_live) {
        const Json info = m_live->info();
        js << ", \"host\": " << jq(m_live->host()) << ", \"port\": " << m_live->port() << ", \"error\": " << jq(m_live->error());
        if (info.type == Json::Object)
            js << ", \"device\": {\"name\": " << jq(info["name"].str()) << ", \"type\": " << jq(info["device"].str()) << ", \"model\": " << jq(info["model"].str())
               << ", \"built\": " << jq(info["built"].str()) << ", \"protocol\": " << (int)info["protocol"].num() << ", \"content\": " << (int)info["content"].num()
               << ", \"signature\": " << jq(info["signature"].str()) << ", \"editorSignature\": " << jq(live::engineSignature())
               << ", \"state\": " << jq(info["state"].str()) << "}";
    }
    js << "}";
    return js.str();
}

// ---------------------------------------------------------------------------- command API: device.*

// Returns true when the request waits for the device (completed later by liveUpdate).
bool GameHost::liveRequest(const Json& j, const std::shared_ptr<game::CommandRequest>& r) {
    const std::string cmd = j["cmd"].str();
    auto done = [&](const std::string& resp, int status = 200) { r->response = resp; r->status = status; return false; };
    auto fail = [&](const std::string& e) { return done("{\"ok\": false, \"error\": " + jq(e) + "}"); };
    if (!m_live) m_live = std::make_unique<LiveClient>();
    if (m_liveKnown.empty()) liveLoadDevices();
    if (cmd == "device.list") {
        m_live->startBrowse();
        std::ostringstream js;
        js << "{\"ok\": true, \"devices\": [";
        const std::vector<LiveKnown> list = liveDevices();
        for (size_t i = 0; i < list.size(); ++i) {
            const LiveKnown& d = list[i];
            js << (i ? ", " : "") << "{\"name\": " << jq(d.name) << ", \"type\": " << jq(d.type) << ", \"host\": " << jq(d.host) << ", \"port\": " << d.port
               << ", \"id\": " << jq(d.id) << ", \"bonjour\": " << (d.found ? "true" : "false") << ", \"paired\": " << (d.code.empty() ? "false" : "true") << "}";
        }
        js << "], \"connection\": " << liveStateJson() << "}";
        return done(js.str());
    }
    if (cmd == "device.connect") {
        // a device of the list (name or id), or an address; the code: given, else the remembered one
        std::string host = j["host"].str(), code = j["code"].str();
        int port = (int)j["port"].num(live::kDefaultPort);
        const std::string want = j["name"].str(j["id"].str());
        if (host.empty() || code.empty())
            for (const LiveKnown& d : liveDevices())
                if ((!want.empty() && (d.name == want || d.id == want)) || (want.empty() && d.host == host && d.port == port) || (want.empty() && host.empty())) {
                    if (host.empty()) { host = d.host; port = d.port; }
                    if (code.empty()) code = d.code;
                    break;
                }
        if (host.empty()) return fail("appareil introuvable : " + want);
        m_liveCode = code;
        m_liveRememberPending = true;
        m_liveChecked = false;
        m_liveConfirm = false;
        m_live->connect(host, port, code);
        m_liveConnectReq = r;
        return true;
    }
    if (cmd == "device.disconnect") {
        m_live->disconnect();
        return done(liveStateJson());
    }
    if (cmd == "device.send") {
        std::string err;
        if (!liveSend(j["force"].b, err)) {
            std::ostringstream js;
            js << "{\"ok\": false, \"error\": " << jq(err) << ", \"blocked\": " << (m_liveCheck.blocked ? "true" : "false")
               << ", \"warnings\": " << live::jsonList(m_liveCheck.warnings) << ", \"infos\": " << live::jsonList(m_liveCheck.infos)
               << ", \"hint\": \"force: true sends anyway (not when blocked)\"}";
            return done(js.str());
        }
        m_liveSendReq = r;
        m_liveReqTime = (double)GetTickCount64() / 1000.0;
        return true;
    }
    if (cmd == "device.check") {   // the warnings, without sending
        if (m_live->state() != LiveClient::Connected) return fail("aucun appareil connecté");
        fs::path out;
        std::string err;
        if (!exportLive(out, err)) return fail(err);
        m_liveCheck = liveCheck(out, m_live->info());
        m_liveChecked = true;
        return done("{\"ok\": true, \"blocked\": " + std::string(m_liveCheck.blocked ? "true" : "false") + ", \"warnings\": " +
                    live::jsonList(m_liveCheck.warnings) + ", \"infos\": " + live::jsonList(m_liveCheck.infos) + "}");
    }
    if (cmd == "device.screenshot") {
        if (m_live->state() != LiveClient::Connected) return fail("aucun appareil connecté");
        m_live->requestShot((int)j["maxWidth"].num(1280));
        m_liveShotReq = r;
        m_liveReqTime = (double)GetTickCount64() / 1000.0;
        return true;
    }
    if (cmd == "device.log") {
        const std::vector<std::string> lines = m_live->log();
        const size_t n = std::min<size_t>(lines.size(), (size_t)std::max(1.0, j["lines"].num(100)));
        return done("{\"ok\": true, \"total\": " + std::to_string(m_live->logCount()) + ", \"lines\": " +
                    live::jsonList(std::vector<std::string>(lines.end() - n, lines.end())) + "}");
    }
    if (cmd == "device.window") {
        if (j["open"].type == Json::Bool && !j["open"].b) m_showLive = false;
        else openLiveWindow();
        return done("{\"ok\": true}");
    }
    return fail("commande inconnue : " + cmd);
}

// ---------------------------------------------------------------------------- Tools > Appareils (WZX Live)

void GameHost::drawLiveWindow() {
    const float d = m_app.m_dpi;
    ImGui::SetNextWindowSize(ImVec2(560 * d, 640 * d), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Appareils (WZX Live)###livewin", &m_showLive, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking)) {
        ImGui::End();
        return;
    }
    if (!m_showLive && m_live) m_live->stopBrowse();
    const ImVec4 green(0.35f, 0.78f, 0.45f, 1), amber(0.95f, 0.72f, 0.30f, 1), red(0.95f, 0.45f, 0.40f, 1);
    const LiveClient::State st = m_live->state();
    const Json info = m_live->info();

    ImGui::TextWrapped("Le jeu en cours d'édition, envoyé à un iPhone, un iPad ou au player Windows sur le réseau local. "
                       "L'app doit avoir été construite avec l'option WZX Live (profil de build) et être ouverte sur son écran d'attente.");
    ImGui::SeparatorText("Appareils");
    const std::vector<LiveKnown> list = liveDevices();
    if (m_liveSel >= (int)list.size()) m_liveSel = 0;
    if (ImGui::BeginListBox("##livelist", ImVec2(-1, 110 * d))) {
        for (int i = 0; i < (int)list.size(); ++i) {
            const LiveKnown& k = list[i];
            const char* icon = k.type == "Phone" ? "iPhone" : k.type == "Tablet" ? "iPad" : "PC";
            const bool cur = st != LiveClient::Off && m_live->host() == k.host && m_live->port() == k.port;
            std::string label = std::string(cur && st == LiveClient::Connected ? "[connecté]  " : "") + k.name + "   (" + icon + ", " + k.host + ":" +
                                std::to_string(k.port) + (k.found ? ", vu sur le réseau" : "") + ")##" + std::to_string(i);
            if (ImGui::Selectable(label.c_str(), m_liveSel == i)) {
                m_liveSel = i;
                snprintf(m_liveCodeBuf, sizeof m_liveCodeBuf, "%s", k.code.c_str());
            }
        }
        ImGui::EndListBox();
    }
    ImGui::SetNextItemWidth(200 * d);
    ImGui::InputTextWithHint("##liveaddr", "adresse IP (ex. 192.168.1.20)", m_liveAddrBuf, sizeof m_liveAddrBuf);
    ImGui::SameLine();
    if (ImGui::Button("Ajouter l'adresse")) {
        std::string host;
        int port = 0;
        if (parseAddress(m_liveAddrBuf, host, port)) {
            LiveKnown k;
            k.name = "Appareil " + host;
            k.host = host;
            k.port = port;
            m_liveKnown.push_back(k);
            liveSaveDevices();
            m_liveAddrBuf[0] = 0;
            m_liveSel = (int)liveDevices().size() - 1;
            m_liveCodeBuf[0] = 0;
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Si l'appareil n'apparait pas (réseau sans Bonjour) : l'adresse et le port sont affiches sur son écran d'attente.");

    ImGui::SeparatorText("Connexion");
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Code d'appairage");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70 * d);
    ImGui::InputText("##livecode", m_liveCodeBuf, sizeof m_liveCodeBuf, ImGuiInputTextFlags_CharsDecimal);
    ImGui::SameLine();
    const bool busy = st == LiveClient::Connecting;
    if (st == LiveClient::Connected) {
        if (ImGui::Button("Déconnecter")) m_live->disconnect();
    } else {
        ImGui::BeginDisabled(busy || list.empty());
        if (ImGui::Button(busy ? "Connexion..." : "Connecter") && !list.empty()) {
            const LiveKnown& k = list[m_liveSel];
            m_liveCode = m_liveCodeBuf;
            m_liveRememberPending = true;
            m_liveChecked = false;
            m_liveConfirm = false;
            m_live->connect(k.host, k.port, m_liveCode);
        }
        ImGui::EndDisabled();
    }
    ImGui::TextDisabled("Le code est affiché sur l'écran d'attente de l'appareil ; il est retenu ensuite.");
    if (st == LiveClient::Connected) {
        ImGui::TextColored(green, "Connecté à %s (%s)", info["name"].str().c_str(), info["model"].str().c_str());
        ImGui::TextDisabled("App compilée le %s, moteur %s (éditeur %s)", info["built"].str().c_str(), info["signature"].str().c_str(),
                            live::engineSignature().c_str());
    } else if (st == LiveClient::BadCode || st == LiveClient::NeedCode) {
        ImGui::TextColored(red, "Code d'appairage %s : celui affiché sur l'appareil.", st == LiveClient::BadCode ? "incorrect" : "nécessaire");
    } else if (st == LiveClient::Failed) {
        ImGui::TextColored(red, "Pas de connexion : %s", m_live->error().c_str());
    }

    ImGui::SeparatorText("Envoi");
    const LiveClient::Progress p = m_live->progress();
    if (m_liveChecked && (!m_liveCheck.warnings.empty() || !m_liveCheck.infos.empty())) {
        ImGui::PushTextWrapPos(0);
        for (const std::string& w : m_liveCheck.warnings) ImGui::TextColored(m_liveCheck.blocked ? red : amber, "/!\\ %s", w.c_str());
        for (const std::string& w : m_liveCheck.infos) ImGui::TextDisabled("%s", w.c_str());
        ImGui::PopTextWrapPos();
    }
    if (m_liveConfirm && st == LiveClient::Connected) {
        ImGui::TextColored(amber, "L'app risque de ne pas lancer ce jeu (elle affichera l'erreur et restera en attente).");
        if (ImGui::Button("Envoyer quand même")) {
            std::string err;
            if (!liveSend(true, err)) toast(fromUtf8("WZX Live : " + err));
        }
        ImGui::SameLine();
        if (ImGui::Button("Annuler")) m_liveConfirm = false;
    } else {
        ImGui::BeginDisabled(st != LiveClient::Connected || p.active);
        if (ImGui::Button("Envoyer sur l'appareil", ImVec2(220 * d, 34 * d))) {
            std::string err;
            if (!liveSend(false, err) && !m_liveConfirm) toast(fromUtf8("WZX Live : " + err));
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Checkbox("Capture après l'envoi", &m_liveAutoShot);
    }
    if (p.active) {
        const float f = p.bytes ? (float)p.bytesDone / (float)p.bytes : 0.0f;
        char t[160];
        snprintf(t, sizeof t, "%s  %zu / %zu fichiers  %.1f / %.1f Mo", p.phase.c_str(), p.filesDone, p.files, p.bytesDone / 1048576.0, p.bytes / 1048576.0);
        ImGui::ProgressBar(f, ImVec2(-1, 0), t);
    } else if (p.done) {
        ImGui::TextColored(p.ok ? green : red, p.ok ? "Jeu lancé sur l'appareil (%zu fichier(s) envoyé(s))" : "Échec : %s", p.ok ? p.files : 0, p.message.c_str());
    }

    ImGui::SeparatorText("Écran de l'appareil");
    ImGui::BeginDisabled(st != LiveClient::Connected);
    if (ImGui::Button("Capture")) m_live->requestShot(1280);
    ImGui::EndDisabled();
    if (!m_liveShotPath.empty()) {
        ImGui::SameLine();
        if (ImGui::Button("Ouvrir l'image")) ShellExecuteW(nullptr, L"open", fromUtf8(m_liveShotPath).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    if (m_liveShotTex && m_liveShotW > 0) {
        float w = ImGui::GetContentRegionAvail().x, h = w * m_liveShotH / (float)m_liveShotW;
        if (h > 320 * d) { w *= 320 * d / h; h = 320 * d; }   // (the aspect of the device kept)
        ImGui::Image(m_liveShotTex, ImVec2(w, h));
    }

    ImGui::SeparatorText("Journal de l'appareil");
    if (ImGui::SmallButton("Effacer")) m_live->clearLog();
    ImGui::BeginChild("##livelog", ImVec2(-1, 160 * d), ImGuiChildFlags_Borders);
    for (const std::string& l : m_live->log()) {
        const bool err = l.find("Erreur") != std::string::npos || l.find("Error") != std::string::npos;
        if (err) ImGui::TextColored(red, "%s", l.c_str());
        else ImGui::TextUnformatted(l.c_str());
    }
    if (m_live->logCount() != m_liveLogSeen) {
        m_liveLogSeen = m_live->logCount();
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace app
