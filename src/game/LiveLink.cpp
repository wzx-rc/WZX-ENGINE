// WZX Live: socket, messages and compatibility shared by the editor and the device (see LiveLink.h).
// Portable C++17: Winsock on Windows, BSD sockets on iOS (no std::to_chars(float), no _stricmp: the iOS 15 target).
#include "LiveLink.h"
#include "Graph.h"
#include "app/Json.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using sock_t = SOCKET;
#define LIVE_BAD_SOCKET INVALID_SOCKET
static void closeSock(sock_t s) { closesocket(s); }
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
using sock_t = int;
#define LIVE_BAD_SOCKET (-1)
static void closeSock(sock_t s) { ::close(s); }
#endif

#include <algorithm>
#include <cstring>
#include <fstream>
#include <mutex>
#include <set>

namespace fs = std::filesystem;

namespace live {

namespace {

sock_t S(intptr_t s) { return (sock_t)s; }

void noDelay(sock_t s) {
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof one);
#ifdef SO_NOSIGPIPE
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);   // a closed peer must not kill the app (iOS)
#endif
}

int waitFor(sock_t s, bool write, int timeoutMs) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(s, &set);
    timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    int r = select((int)s + 1, write ? nullptr : &set, write ? &set : nullptr, nullptr, timeoutMs < 0 ? nullptr : &tv);
    return r > 0 ? 1 : (r == 0 ? 0 : -1);
}

std::string readText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return std::string();
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

} // namespace

Socket& Socket::operator=(Socket&& o) noexcept {
    if (this != &o) {
        close();
        m_s = o.m_s;
        o.m_s = -1;
    }
    return *this;
}

bool Socket::startup() {
#ifdef _WIN32
    static std::once_flag once;
    static bool ok = false;
    std::call_once(once, [] {
        WSADATA wsa;
        ok = WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    });
    return ok;
#else
    return true;
#endif
}

void Socket::close() {
    if (m_s != -1) closeSock(S(m_s));
    m_s = -1;
}

bool Socket::listen(int port, bool loopbackOnly, std::string* err) {
    startup();
    close();
    sock_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == LIVE_BAD_SOCKET) { if (err) *err = "socket() failed"; return false; }
    int one = 1;
#ifndef _WIN32
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);   // restart at once after a crash (TIME_WAIT)
#else
    (void)one;
#endif
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)port);
    a.sin_addr.s_addr = htonl(loopbackOnly ? INADDR_LOOPBACK : INADDR_ANY);
    if (bind(s, (sockaddr*)&a, sizeof a) != 0 || ::listen(s, 4) != 0) {
        closeSock(s);
        if (err) *err = "port " + std::to_string(port) + " busy";
        return false;
    }
    m_s = (intptr_t)s;
    return true;
}

int Socket::localPort() const {
    if (!valid()) return 0;
    sockaddr_in a = {};
    socklen_t n = sizeof a;
    if (getsockname(S(m_s), (sockaddr*)&a, &n) != 0) return 0;
    return ntohs(a.sin_port);
}

Socket Socket::accept(int timeoutMs) {
    if (!valid() || waitFor(S(m_s), false, timeoutMs) != 1) return Socket();
    sock_t c = ::accept(S(m_s), nullptr, nullptr);
    if (c == LIVE_BAD_SOCKET) return Socket();
    noDelay(c);
    return Socket((intptr_t)c);
}

bool Socket::connect(const std::string& host, int port, int timeoutMs, std::string* err) {
    startup();
    close();
    addrinfo hints = {}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) {
        if (err) *err = "adresse introuvable : " + host;
        return false;
    }
    sock_t s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s == LIVE_BAD_SOCKET) { freeaddrinfo(res); if (err) *err = "socket() failed"; return false; }
    // non-blocking connect, so that an absent device fails after timeoutMs instead of the system's 20 s
#ifdef _WIN32
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
#else
    int fl = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, fl | O_NONBLOCK);
#endif
    int r = ::connect(s, res->ai_addr, (int)res->ai_addrlen);
    freeaddrinfo(res);
    bool ok = r == 0;
    if (!ok) {
        if (waitFor(s, true, timeoutMs) == 1) {
            int e = 0;
            socklen_t n = sizeof e;
            getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&e, &n);
            ok = e == 0;
        }
    }
#ifdef _WIN32
    nb = 0;
    ioctlsocket(s, FIONBIO, &nb);
#else
    fcntl(s, F_SETFL, fl);
#endif
    if (!ok) {
        closeSock(s);
        if (err) *err = "pas de réponse de " + host + ":" + std::to_string(port);
        return false;
    }
    noDelay(s);
    m_s = (intptr_t)s;
    return true;
}

int Socket::waitReadable(int timeoutMs) { return valid() ? waitFor(S(m_s), false, timeoutMs) : -1; }

bool Socket::sendAll(const void* data, size_t n) {
    const char* p = (const char*)data;
    while (n > 0 && valid()) {
#if defined(MSG_NOSIGNAL)
        const int flags = MSG_NOSIGNAL;
#else
        const int flags = 0;
#endif
        int w = (int)::send(S(m_s), p, (int)std::min<size_t>(n, 1 << 20), flags);
        if (w <= 0) return false;
        p += w;
        n -= (size_t)w;
    }
    return n == 0;
}

bool Socket::recvAll(void* data, size_t n, int timeoutMs) {
    char* p = (char*)data;
    while (n > 0 && valid()) {
        if (waitFor(S(m_s), false, timeoutMs) != 1) return false;
        int r = (int)::recv(S(m_s), p, (int)std::min<size_t>(n, 1 << 20), 0);
        if (r <= 0) return false;
        p += r;
        n -= (size_t)r;
    }
    return n == 0;
}

std::string Socket::peerAddress() const {
    sockaddr_in a = {};
    socklen_t n = sizeof a;
    if (!valid() || getpeername(S(m_s), (sockaddr*)&a, &n) != 0) return std::string();
    char b[64] = {};
    inet_ntop(AF_INET, &a.sin_addr, b, sizeof b);
    return b;
}

bool sendMessage(Socket& s, uint8_t type, const void* data, size_t size) {
    if (size + 1 > kMaxMessage) return false;
    const uint32_t len = (uint32_t)(size + 1);
    uint8_t head[5] = {(uint8_t)(len & 255), (uint8_t)(len >> 8 & 255), (uint8_t)(len >> 16 & 255), (uint8_t)(len >> 24), type};
    return s.sendAll(head, 5) && (size == 0 || s.sendAll(data, size));
}

bool sendJson(Socket& s, uint8_t type, const std::string& json) { return sendMessage(s, type, json.data(), json.size()); }

int recvMessage(Socket& s, uint8_t& type, std::vector<uint8_t>& payload, int timeoutMs) {
    const int w = s.waitReadable(timeoutMs);
    if (w == 0) return 0;
    if (w < 0) return -1;
    uint8_t head[5];
    if (!s.recvAll(head, 5, 15000)) return -1;
    const uint32_t len = head[0] | head[1] << 8 | head[2] << 16 | (uint32_t)head[3] << 24;
    if (len < 1 || len > kMaxMessage) return -1;
    type = head[4];
    payload.resize(len - 1);
    if (len > 1 && !s.recvAll(payload.data(), len - 1, 15000)) return -1;
    return 1;
}

uint64_t fnv1a(const void* data, size_t n, uint64_t h) {
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

bool hashFile(const fs::path& file, uint64_t& hash, uint64_t& size) {
    std::ifstream f(file, std::ios::binary);
    if (!f) return false;
    std::vector<char> buf(1 << 20);
    hash = 1469598103934665603ull;
    size = 0;
    while (f) {
        f.read(buf.data(), (std::streamsize)buf.size());
        const std::streamsize n = f.gcount();
        if (n <= 0) break;
        hash = fnv1a(buf.data(), (size_t)n, hash);
        size += (uint64_t)n;
    }
    return true;
}

std::string hex64(uint64_t v) {
    static const char* d = "0123456789abcdef";
    std::string s(16, '0');
    for (int i = 15; i >= 0; --i, v >>= 4) s[i] = d[v & 15];
    return s;
}

bool safeRelativePath(const std::string& p) {
    if (p.empty() || p.size() > 400 || p[0] == '/' || p.find('\\') != std::string::npos || p.find(':') != std::string::npos) return false;
    size_t i = 0;
    while (i <= p.size()) {
        size_t e = p.find('/', i);
        if (e == std::string::npos) e = p.size();
        const std::string part = p.substr(i, e - i);
        if (part.empty() || part == "." || part == ".." || part[0] == '.') return false;   // (.part files are the device's own)
        for (char c : part) if ((unsigned char)c < 0x20) return false;
        i = e + 1;
    }
    return true;
}

// ---------------------------------------------------------------------------- compatibility

namespace {

struct Feature { const char* id; const char* label; };
// The content features this build reads. A new scene, prefab or game feature that an older player would not
// understand gets an id here and a detector in contentNeeds: the editor then warns before sending to an older app.
const Feature kFeatures[] = {
    {"scene-v1", "scène .wzx version 1"},
    {"mesh-obj", "maillages importés (OBJ)"},
    {"material-texture", "textures de matériau"},
    {"hdri", "HDRI de la scène"},
    {"baked-probes", "sondes de lumière cuites"},
    {"prefab-instance", "instances de prefab"},
    {"prefab-clip", "animations de prefab (clips)"},
    {"curve", "objets courbe"},
    {"camera-object", "objets caméra"},
    {"group", "groupes"},
    {"village", "village RTS (maisons)"},
};

} // namespace

std::vector<std::string> nodeTypes() {
    std::vector<std::string> v;
    for (const game::NodeDef& d : game::nodeDefs()) v.push_back(d.id);
    std::sort(v.begin(), v.end());
    return v;
}

std::vector<std::string> engineFeatures() {
    std::vector<std::string> v;
    for (const Feature& f : kFeatures) v.push_back(f.id);
    return v;
}

std::string engineSignature() {
    std::string s = "p" + std::to_string(kProtocol) + "c" + std::to_string(kContentVersion);
    for (const std::string& n : nodeTypes()) s += "|" + n;
    s += "#";
    for (const std::string& f : engineFeatures()) s += "|" + f;
    return hex64(fnv1a(s.data(), s.size())).substr(0, 12);
}

std::string featureLabel(const std::string& id) {
    for (const Feature& f : kFeatures) if (id == f.id) return f.label;
    if (id == "mesh-gltf") return "maillages glTF (le player lit seulement l'OBJ)";
    if (id.rfind("scene-v", 0) == 0) return "scène .wzx version " + id.substr(7);
    return id;
}

void contentNeeds(const fs::path& folder, std::vector<std::string>& features, std::vector<std::string>& nodes) {
    using app::Json;
    std::set<std::string> f, n;
    Json cfg;
    Json::parse(readText(folder / "game.json"), cfg);
    Json scene;
    Json::parse(readText(folder / fs::u8path(cfg["scene"].str("scene.wzx"))), scene);
    f.insert("scene-v" + std::to_string((int)scene["version"].num(1)));
    const Json& hdri = scene["environment"]["hdri"];
    if (hdri.type == Json::String && !hdri.s.empty() && hdri.s != "sky") f.insert("hdri");
    if (scene["probes"].type == Json::String) f.insert("baked-probes");
    if (!scene["village"].a.empty()) f.insert("village");
    for (const Json& m : scene["materials"].a)
        if (m["baseColorTexture"].type == Json::String) f.insert("material-texture");
    for (const Json& o : scene["objects"].a) {
        if (!o["prefab"].str().empty()) f.insert("prefab-instance");
        if (!o["clip"].str().empty()) f.insert("prefab-clip");
        if (o["curve"].type == Json::Object) f.insert("curve");
        if (o["camera"].type == Json::Object) f.insert("camera-object");
        if (o["group"].type == Json::Bool && o["group"].b) f.insert("group");
        if (o["mesh"].type == Json::String) {
            std::string ext = fs::u8path(o["mesh"].s).extension().u8string();
            for (char& c : ext) c = (char)tolower((unsigned char)c);
            f.insert(ext == ".obj" ? "mesh-obj" : "mesh-gltf");
        }
    }
    std::error_code ec;
    for (auto e = fs::recursive_directory_iterator(folder, ec); !ec && e != fs::recursive_directory_iterator(); e.increment(ec)) {
        if (!e->is_regular_file() || e->path().extension() != ".prefab") continue;
        Json p;
        if (Json::parse(readText(e->path()), p) && !p["clips"].a.empty()) f.insert("prefab-clip");
    }
    // graph: graph.json, else the graph inside the scene
    Json graph;
    if (!Json::parse(readText(folder / fs::u8path(cfg["graph"].str("graph.json"))), graph) || graph.type != Json::Object) graph = scene["graph"];
    for (const Json& node : graph["nodes"].a)
        if (node["type"].type == Json::String) n.insert(node["type"].s);
    features.assign(f.begin(), f.end());
    nodes.assign(n.begin(), n.end());
}

std::vector<std::string> localAddresses() {
    std::vector<std::string> out;
#ifdef _WIN32
    Socket::startup();
    char host[256] = {};
    if (gethostname(host, sizeof host) == 0) {
        addrinfo hints = {}, *res = nullptr;
        hints.ai_family = AF_INET;
        if (getaddrinfo(host, nullptr, &hints, &res) == 0) {
            for (addrinfo* r = res; r; r = r->ai_next) {
                char b[64] = {};
                inet_ntop(AF_INET, &((sockaddr_in*)r->ai_addr)->sin_addr, b, sizeof b);
                if (std::find(out.begin(), out.end(), b) == out.end()) out.push_back(b);
            }
            freeaddrinfo(res);
        }
    }
#else
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) == 0) {
        for (ifaddrs* i = list; i; i = i->ifa_next) {
            if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || (i->ifa_flags & IFF_LOOPBACK) || !(i->ifa_flags & IFF_UP)) continue;
            char b[64] = {};
            inet_ntop(AF_INET, &((sockaddr_in*)i->ifa_addr)->sin_addr, b, sizeof b);
            // Wi-Fi first (en0 on iPhone / iPad), cellular (pdp_ip) is no use for a PC on the local network
            const std::string name = i->ifa_name ? i->ifa_name : "";
            if (name.rfind("pdp_ip", 0) == 0 || name.rfind("utun", 0) == 0) continue;
            if (name == "en0") out.insert(out.begin(), b);
            else out.push_back(b);
        }
        freeifaddrs(list);
    }
#endif
    return out;
}

std::string jsonList(const std::vector<std::string>& v) {
    std::string s = "[";
    for (size_t i = 0; i < v.size(); ++i) s += (i ? ", \"" : "\"") + app::jsonEscape(v[i]) + "\"";
    return s + "]";
}

} // namespace live
