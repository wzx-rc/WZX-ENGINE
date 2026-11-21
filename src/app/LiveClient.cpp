// WZX Live, the editor side (see LiveClient.h and src/game/LiveLink.h).
#include "LiveClient.h"
#include "game/LiveLink.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windns.h>
#pragma comment(lib, "dnsapi.lib")

#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>
#include <fstream>
#include <memory>
#include <sstream>

namespace fs = std::filesystem;

namespace app {

namespace {

double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
std::string q(const std::string& s) { return "\"" + jsonEscape(s) + "\""; }

std::string narrow(const wchar_t* w) {
    if (!w) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}

} // namespace

// ---------------------------------------------------------------------------- Bonjour (DNS-SD of Windows)

// Shared with the system callbacks, which may come after the client stops browsing.
struct LiveDnsShared {
    std::mutex mx;
    bool alive = true;
    std::vector<LiveDevice> found;
    std::vector<std::string> gone;   // instance names said gone (TTL 0)
};

struct LiveDns {
    std::shared_ptr<LiveDnsShared> shared = std::make_shared<LiveDnsShared>();
    DNS_SERVICE_CANCEL cancel = {};
    bool browsing = false;

    struct Resolve { std::shared_ptr<LiveDnsShared> shared; std::wstring name; DNS_SERVICE_CANCEL cancel = {}; };

    static void WINAPI resolved(DWORD status, PVOID ctx, PDNS_SERVICE_INSTANCE inst) {
        std::unique_ptr<Resolve> r((Resolve*)ctx);
        if (status != ERROR_SUCCESS || !inst) { if (inst) DnsServiceFreeInstance(inst); return; }
        LiveDevice d;
        d.found = true;
        d.port = inst->wPort;
        std::string name = narrow(inst->pszInstanceName);
        const size_t cut = name.find("._wzx-live");
        d.name = cut == std::string::npos ? name : name.substr(0, cut);
        for (DWORD i = 0; i < inst->dwPropertyCount; ++i) {
            const std::string k = narrow(inst->keys[i]), v = narrow(inst->values[i]);
            if (k == "id") d.id = v;
            if (k == "type") d.type = v;
        }
        if (inst->ip4Address) {
            char b[64] = {};
            inet_ntop(AF_INET, inst->ip4Address, b, sizeof b);
            d.host = b;
        } else {
            d.host = narrow(inst->pszHostName);   // name.local: Windows resolves it by mDNS
        }
        DnsServiceFreeInstance(inst);
        std::lock_guard<std::mutex> lk(r->shared->mx);
        if (!r->shared->alive) return;
        auto& f = r->shared->found;
        auto it = std::find_if(f.begin(), f.end(), [&](const LiveDevice& e) { return (!d.id.empty() && e.id == d.id) || e.name == d.name; });
        if (it != f.end()) *it = d;
        else f.push_back(d);
    }

    static void WINAPI browsed(DWORD status, PVOID ctx, PDNS_RECORD rec) {
        std::shared_ptr<LiveDnsShared> sh = *(std::shared_ptr<LiveDnsShared>*)ctx;
        for (PDNS_RECORD r = rec; r && status == ERROR_SUCCESS; r = r->pNext) {
            if (r->wType != DNS_TYPE_PTR || !r->Data.PTR.pNameHost) continue;
            if (r->dwTtl == 0) {   // the device left
                std::string name = narrow(r->Data.PTR.pNameHost);
                const size_t cut = name.find("._wzx-live");
                if (cut != std::string::npos) name = name.substr(0, cut);
                std::lock_guard<std::mutex> lk(sh->mx);
                sh->found.erase(std::remove_if(sh->found.begin(), sh->found.end(), [&](const LiveDevice& d) { return d.name == name; }), sh->found.end());
                continue;
            }
            auto* res = new Resolve();
            res->shared = sh;
            res->name = r->Data.PTR.pNameHost;
            DNS_SERVICE_RESOLVE_REQUEST rq = {};
            rq.Version = DNS_QUERY_REQUEST_VERSION1;
            rq.QueryName = &res->name[0];
            rq.pResolveCompletionCallback = resolved;
            rq.pQueryContext = res;
            if (DnsServiceResolve(&rq, &res->cancel) != DNS_REQUEST_PENDING) delete res;
        }
        if (rec) DnsRecordListFree(rec, DnsFreeRecordList);
    }
};

void LiveClient::startBrowse() {
    if (m_browse) return;
    auto* d = new LiveDns();
    auto* ctx = new std::shared_ptr<LiveDnsShared>(d->shared);   // kept until exit: the system may call back late
    DNS_SERVICE_BROWSE_REQUEST rq = {};
    rq.Version = DNS_QUERY_REQUEST_VERSION1;
    rq.QueryName = L"_wzx-live._tcp.local";
    rq.pBrowseCallback = LiveDns::browsed;
    rq.pQueryContext = ctx;
    d->browsing = DnsServiceBrowse(&rq, &d->cancel) == DNS_REQUEST_PENDING;
    m_browse = d;
}

void LiveClient::stopBrowse() {
    if (!m_browse) return;
    LiveDns* d = (LiveDns*)m_browse;
    if (d->browsing) DnsServiceBrowseCancel(&d->cancel);
    {
        std::lock_guard<std::mutex> lk(d->shared->mx);
        d->shared->alive = false;
    }
    delete d;
    m_browse = nullptr;
}

std::vector<LiveDevice> LiveClient::found() const {
    if (!m_browse) return {};
    LiveDns* d = (LiveDns*)m_browse;
    std::lock_guard<std::mutex> lk(d->shared->mx);
    return d->shared->found;
}

// ---------------------------------------------------------------------------- connection

LiveClient::~LiveClient() {
    disconnect();
    stopBrowse();
}

void LiveClient::connect(const std::string& host, int port, const std::string& code) {
    disconnect();
    {
        std::lock_guard<std::mutex> lk(m_mx);
        m_error.clear();
        m_host = host;
        m_port = port;
        m_info = Json();
        m_sendQueue.clear();
        m_progress = Progress();
    }
    m_stop = false;
    m_state = Connecting;
    m_thread = std::thread([this, host, port, code] { run(host, port, code); });
}

void LiveClient::disconnect() {
    m_stop = true;
    if (m_thread.joinable()) m_thread.join();
    State s = m_state;
    if (s == Connecting || s == Connected) m_state = Off;
}

std::string LiveClient::error() const { std::lock_guard<std::mutex> lk(m_mx); return m_error; }
std::string LiveClient::host() const { std::lock_guard<std::mutex> lk(m_mx); return m_host; }
int LiveClient::port() const { std::lock_guard<std::mutex> lk(m_mx); return m_port; }
Json LiveClient::info() const { std::lock_guard<std::mutex> lk(m_mx); return m_info; }
LiveClient::Progress LiveClient::progress() const { std::lock_guard<std::mutex> lk(m_mx); return m_progress; }

void LiveClient::send(const fs::path& folder) {
    std::lock_guard<std::mutex> lk(m_mx);
    m_sendQueue.push_back(folder);
    m_progress.active = true;
    m_progress.done = false;
    m_progress.phase = "En attente";
    m_progress.message.clear();
}

void LiveClient::requestShot(int maxWidth) {
    std::lock_guard<std::mutex> lk(m_mx);
    m_shotWanted = maxWidth;
}

std::vector<uint8_t> LiveClient::shot(int& w, int& h) const {
    std::lock_guard<std::mutex> lk(m_mx);
    w = m_shotW;
    h = m_shotH;
    return m_shot;
}

std::vector<std::string> LiveClient::log() const {
    std::lock_guard<std::mutex> lk(m_mx);
    return std::vector<std::string>(m_log.begin(), m_log.end());
}

void LiveClient::clearLog() {
    std::lock_guard<std::mutex> lk(m_mx);
    m_log.clear();
}

void LiveClient::addLog(const std::string& line) {
    std::lock_guard<std::mutex> lk(m_mx);
    m_log.push_back(line);
    while (m_log.size() > 500) m_log.pop_front();
    ++m_logCount;
}

// ---------------------------------------------------------------------------- network thread

void LiveClient::run(std::string host, int port, std::string code) {
    live::Socket s;
    m_sock = &s;
    std::string err;
    m_waitReload = false;
    auto fail = [&](State st, const std::string& e) {
        {
            std::lock_guard<std::mutex> lk(m_mx);
            m_error = e;
            if (m_progress.active && !m_progress.done) {
                m_progress.done = true;
                m_progress.ok = false;
                m_progress.active = false;
                m_progress.message = e;
                ++m_progress.serial;
            }
        }
        m_state = st;
        m_sock = nullptr;
    };
    if (!s.connect(host, port, 4000, &err)) { fail(Failed, err); return; }
    const std::string hello = "{\"protocol\": " + std::to_string(live::kProtocol) + ", \"editor\": \"WZX Engine\", \"code\": " + q(code) + "}";
    if (!live::sendJson(s, live::Hello, hello)) { fail(Failed, "connexion refusée"); return; }
    const double t0 = now();
    while (m_state == Connecting && now() - t0 < 8.0 && !m_stop)
        if (!pump(100)) { fail(Failed, "l'appareil a fermé la connexion (code refusé trop de fois ?)"); return; }
    if (m_state == Connecting) { fail(Failed, "pas de réponse de l'appareil"); return; }
    if (m_state != Connected) { m_sock = nullptr; return; }   // NeedCode / BadCode: told by handle()
    while (!m_stop) {
        fs::path folder;
        int shotW = 0;
        {
            std::lock_guard<std::mutex> lk(m_mx);
            if (!m_sendQueue.empty() && !m_waitReload) { folder = m_sendQueue.front(); m_sendQueue.pop_front(); }
            shotW = m_shotWanted;
            m_shotWanted = 0;
        }
        if (!folder.empty() && !doSend(folder)) { fail(Failed, "connexion perdue pendant l'envoi"); return; }
        if (shotW && !live::sendJson(s, live::ShotRequest, "{\"maxWidth\": " + std::to_string(shotW) + "}")) { fail(Failed, "connexion perdue"); return; }
        if (!pump(50)) { fail(Failed, "connexion perdue"); return; }
        if (m_waitReload && now() - m_reloadSince > 120.0) finishSend(false, "l'appareil n'a pas répondu après l'envoi");
    }
    m_sock = nullptr;
}

bool LiveClient::pump(int timeoutMs) {
    live::Socket& s = *(live::Socket*)m_sock;
    std::vector<uint8_t> payload;
    for (int k = 0; k < 64; ++k) {
        uint8_t type = 0;
        const int r = live::recvMessage(s, type, payload, k == 0 ? timeoutMs : 0);
        if (r < 0) return false;
        if (r == 0) return true;
        if (!handle(type, payload)) return false;
    }
    return true;
}

bool LiveClient::handle(uint8_t type, const std::vector<uint8_t>& payload) {
    const std::string text(payload.begin(), payload.end());
    switch (type) {
    case live::Info: {
        Json j;
        Json::parse(text, j);
        {
            std::lock_guard<std::mutex> lk(m_mx);
            m_info = j;
        }
        const std::string auth = j["auth"].str();
        if (auth == "ok") m_state = Connected;
        else {
            std::lock_guard<std::mutex> lk(m_mx);
            m_error = auth == "needed" ? "code d'appairage demandé" : "code d'appairage incorrect";
            m_state = auth == "needed" ? NeedCode : BadCode;
        }
        return true;
    }
    case live::Need: {
        Json j;
        Json::parse(text, j);
        m_need.clear();
        for (const Json& p : j["paths"].a) m_need.push_back(p.str());
        m_gotNeed = true;
        return true;
    }
    case live::Status: {
        Json j;
        Json::parse(text, j);
        const std::string ev = j["event"].str(), msg = j["message"].str();
        if (ev == "received") {
            std::lock_guard<std::mutex> lk(m_mx);
            m_progress.phase = "Chargement sur l'appareil";
        } else if (ev == "reloaded" || ev == "error") {
            if (m_waitReload || (ev == "error" && progress().active)) finishSend(ev == "reloaded" && j["ok"].b, msg);
        }
        return true;
    }
    case live::Log: {
        Json j;
        Json::parse(text, j);
        if (j["history"].b && !j["lines"].a.empty()) addLog("--- journal de l'appareil depuis son démarrage ---");
        for (const Json& l : j["lines"].a) addLog(l.str());
        if (j["history"].b && !j["lines"].a.empty()) addLog("---");
        return true;
    }
    case live::Shot: {
        if (payload.size() < 8) return true;
        int w = 0, h = 0;
        for (int i = 0; i < 4; ++i) { w |= payload[i] << (8 * i); h |= payload[4 + i] << (8 * i); }
        {
            std::lock_guard<std::mutex> lk(m_mx);
            m_shot.assign(payload.begin() + 8, payload.end());
            m_shotW = w;
            m_shotH = h;
        }
        ++m_shotSerial;
        return true;
    }
    default: return true;
    }
}

void LiveClient::finishSend(bool ok, const std::string& message) {
    m_waitReload = false;
    std::lock_guard<std::mutex> lk(m_mx);
    m_progress.done = true;
    m_progress.active = false;
    m_progress.ok = ok;
    m_progress.phase = ok ? "Terminé" : "Échec";
    m_progress.message = message;
    ++m_progress.serial;
}

bool LiveClient::doSend(const fs::path& folder) {
    live::Socket& s = *(live::Socket*)m_sock;
    auto phase = [&](const std::string& p) { std::lock_guard<std::mutex> lk(m_mx); m_progress.phase = p; };
    phase("Préparation");
    std::error_code ec;
    std::ostringstream man;
    man << "{\"title\": ";
    {
        std::ifstream f(folder / "game.json", std::ios::binary);
        std::string t((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        Json g;
        Json::parse(t, g);
        man << q(g["title"].str("WZX Game"));
    }
    man << ", \"files\": [";
    std::map<std::string, uint64_t> sizes;
    bool first = true;
    for (auto e = fs::recursive_directory_iterator(folder, ec); !ec && e != fs::recursive_directory_iterator(); e.increment(ec)) {
        if (!e->is_regular_file()) continue;
        const std::string rel = fs::relative(e->path(), folder, ec).generic_u8string();
        if (!live::safeRelativePath(rel)) continue;
        uint64_t h = 0, size = 0;
        if (!live::hashFile(e->path(), h, size)) continue;
        sizes[rel] = size;
        man << (first ? "" : ", ") << "{\"p\": " << q(rel) << ", \"s\": " << size << ", \"h\": \"" << live::hex64(h) << "\"}";
        first = false;
    }
    man << "]}";
    m_gotNeed = false;
    m_sendFolder = folder;
    if (!live::sendJson(s, live::Manifest, man.str())) return false;
    phase("Comparaison avec l'appareil");
    const double t0 = now();
    while (!m_gotNeed && now() - t0 < 60.0 && !m_stop)
        if (!pump(100)) return false;
    if (!m_gotNeed) { finishSend(false, "l'appareil n'a pas répondu au manifeste"); return true; }
    {
        std::lock_guard<std::mutex> lk(m_mx);
        m_progress.files = m_need.size();
        m_progress.filesDone = 0;
        m_progress.bytes = 0;
        m_progress.bytesDone = 0;
        for (const std::string& p : m_need) m_progress.bytes += sizes[p];
        m_progress.phase = m_need.empty() ? "Rien de changé" : "Envoi des fichiers";
    }
    if (!streamFiles(m_need)) return false;
    if (!progress().active) return true;   // (an error of the device ended it)
    m_waitReload = true;
    m_reloadSince = now();
    phase("Chargement sur l'appareil");
    return live::sendJson(s, live::Reload, "{}");
}

bool LiveClient::streamFiles(const std::vector<std::string>& paths) {
    live::Socket& s = *(live::Socket*)m_sock;
    std::vector<char> buf(live::kChunkSize);
    for (const std::string& p : paths) {
        std::ifstream f(m_sendFolder / fs::u8path(p), std::ios::binary);
        if (!f) { finishSend(false, "fichier illisible : " + p); return true; }
        std::error_code sec;
        const uint64_t size = (uint64_t)fs::file_size(m_sendFolder / fs::u8path(p), sec);
        uint64_t off = 0;
        do {
            f.read(buf.data(), (std::streamsize)buf.size());
            const size_t n = (size_t)f.gcount();
            const std::string head = "{\"p\": " + q(p) + ", \"o\": " + std::to_string(off) + "}";
            std::vector<uint8_t> msg(4 + head.size() + n);
            const uint32_t hn = (uint32_t)head.size();
            for (int i = 0; i < 4; ++i) msg[i] = (uint8_t)(hn >> (8 * i));
            memcpy(&msg[4], head.data(), head.size());
            if (n) memcpy(&msg[4 + head.size()], buf.data(), n);
            if (!live::sendMessage(s, live::Chunk, msg.data(), msg.size())) return false;
            off += n;
            {
                std::lock_guard<std::mutex> lk(m_mx);
                m_progress.bytesDone += n;
            }
            if (!pump(0)) return false;   // the device's log meanwhile (no full buffers on both sides)
            if (m_stop) return true;
        } while (off < size && f);
        std::lock_guard<std::mutex> lk(m_mx);
        ++m_progress.filesDone;
    }
    return true;
}

} // namespace app
