// WZX Live: the editor sends the game it is editing to a device (iPhone, iPad, or the Windows player) over the local
// network, the device plays it at once, and sends its log and screenshots back. Shared by both sides:
//   - the device (player/src/LiveHost.cpp) is the server: it listens on TCP (port 47800) and advertises itself with
//     Bonjour (_wzx-live._tcp); a 4-digit pairing code, shown on its screen, is asked once by the editor;
//   - the editor (src/app/LiveClient.cpp) connects to it (no Windows firewall prompt on the PC), sends a manifest of
//     the exported game (paths, sizes, hashes), the device answers with the files it lacks, the editor sends them in
//     chunks, then RELOAD: the device writes them into its own live folder only, and restarts the game from it.
// Messages: [u32 length, little endian][u8 type][payload], length = 1 + payload size. Payloads are JSON (UTF-8),
// except Chunk ([u32 header size][JSON header {p, o, t}][bytes]) and Shot ([u32 width][u32 height][JPEG]).
// Compatibility: the device sends in Info its protocol, its content version, the graph node types and the content
// features its build knows; the editor compares them with what the game needs (contentNeeds) before sending.
// A new node type or a new scene feature is known by the editor at once, but by the app only once it is rebuilt.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace live {

constexpr int kProtocol = 1;          // the messages below: a change here = a new IPA
constexpr int kContentVersion = 1;    // what a game folder may hold (scene.wzx, graph, prefabs): bump on a new format
constexpr int kDefaultPort = 47800;
constexpr const char* kService = "_wzx-live._tcp";
constexpr size_t kChunkSize = 256 * 1024;
constexpr size_t kMaxMessage = 16u << 20;

enum Msg : uint8_t {
    Hello = 1,      // editor -> device  {protocol, editor, code}
    Info,           // device -> editor  {protocol, content, signature, built, device, model, name, id, nodes, features, auth, state, game}
    Manifest,       // editor -> device  {files: [{p, s, h}], title}
    Need,           // device -> editor  {paths: [...]}
    Chunk,          // editor -> device  [u32 n][{p, o, t}][bytes]
    Reload,         // editor -> device  {}
    Status,         // device -> editor  {event: received | reloaded | error | auth, ok, message}
    Log,            // device -> editor  {lines: [...]}
    ShotRequest,    // editor -> device  {maxWidth}
    Shot,           // device -> editor  [u32 w][u32 h][JPEG]
};

// A TCP socket (Winsock on Windows, BSD sockets on iOS). Blocking, with timeouts through select/poll.
class Socket {
public:
    Socket() = default;
    explicit Socket(intptr_t s) : m_s(s) {}
    ~Socket() { close(); }
    Socket(Socket&& o) noexcept : m_s(o.m_s) { o.m_s = -1; }
    Socket& operator=(Socket&& o) noexcept;
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    static bool startup();   // WSAStartup once (Windows)
    bool valid() const { return m_s != -1; }
    void close();
    // Server: listen on the port (0.0.0.0, or 127.0.0.1 only). port 0 = any free port.
    bool listen(int port, bool loopbackOnly, std::string* err);
    int localPort() const;
    Socket accept(int timeoutMs);   // invalid on timeout
    // Client: host name or IPv4 address
    bool connect(const std::string& host, int port, int timeoutMs, std::string* err);
    int waitReadable(int timeoutMs);   // 1 readable, 0 timeout, -1 error
    bool sendAll(const void* data, size_t n);
    bool recvAll(void* data, size_t n, int timeoutMs);
    std::string peerAddress() const;

private:
    intptr_t m_s = -1;
};

bool sendMessage(Socket& s, uint8_t type, const void* data, size_t size);
bool sendJson(Socket& s, uint8_t type, const std::string& json);
// Waits up to timeoutMs for a message (0: only if one is there). 1: a message, 0: none yet, -1: closed or broken.
int recvMessage(Socket& s, uint8_t& type, std::vector<uint8_t>& payload, int timeoutMs);

uint64_t fnv1a(const void* data, size_t n, uint64_t h = 1469598103934665603ull);
bool hashFile(const std::filesystem::path& file, uint64_t& hash, uint64_t& size);
std::string hex64(uint64_t v);
// A path the device may write: relative, '/' separators, no "..", no drive, no empty or hidden part.
bool safeRelativePath(const std::string& p);

// What this build can play: the node types of the graph, the content features (see engineFeatures in LiveLink.cpp),
// and a signature of both with the versions (same signature = same capabilities).
std::vector<std::string> nodeTypes();
std::vector<std::string> engineFeatures();
std::string engineSignature();
std::string featureLabel(const std::string& id);   // in French, for the warnings of the editor

// What a game folder (game.json, scene.wzx, graph.json, prefabs) needs: content features and graph node types.
void contentNeeds(const std::filesystem::path& folder, std::vector<std::string>& features, std::vector<std::string>& nodes);

// The IPv4 addresses of this device on the local network (shown on the waiting screen).
std::vector<std::string> localAddresses();

std::string jsonList(const std::vector<std::string>& v);   // ["a", "b"]

} // namespace live
