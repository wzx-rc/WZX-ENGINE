// WZX Live, the editor side: finds the devices (Bonjour _wzx-live._tcp through the DNS-SD API of Windows 10+, or an
// address typed by hand), connects to one with its pairing code, sends it a game folder (only the files it lacks),
// and gets back its log and its screenshots. The protocol: src/game/LiveLink.h. The network runs on a thread of its
// own; the editor (AppLive.cpp) reads the state each frame.
#pragma once
#include "Json.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace app {

struct LiveDevice {
    std::string id;       // from Bonjour (TXT id) or from the device once connected; "" for a typed address not yet reached
    std::string name;
    std::string type;     // Desktop, Tablet, Phone
    std::string host;     // IPv4 address
    int port = 0;
    bool found = false;   // seen by Bonjour (else typed by hand)
};

class LiveClient {
public:
    enum State { Off, Connecting, Connected, NeedCode, BadCode, Failed };
    struct Progress {
        bool active = false, done = false, ok = false;
        std::string phase, message;
        size_t files = 0, filesDone = 0;
        uint64_t bytes = 0, bytesDone = 0;
        uint64_t serial = 0;   // +1 per send finished
    };

    ~LiveClient();

    // Bonjour discovery (Windows 10 1809+): running while the Devices window is open
    void startBrowse();
    void stopBrowse();
    std::vector<LiveDevice> found() const;

    void connect(const std::string& host, int port, const std::string& code);
    void disconnect();
    State state() const { return m_state.load(); }
    std::string error() const;
    std::string host() const;
    int port() const;
    Json info() const;               // what the device said about itself (LiveLink.h, Info)

    // Sends a game folder (game.json, scene.wzx...): manifest, the files the device lacks, then reload.
    void send(const std::filesystem::path& folder);
    Progress progress() const;

    void requestShot(int maxWidth = 1280);
    uint64_t shotSerial() const { return m_shotSerial.load(); }
    std::vector<uint8_t> shot(int& w, int& h) const;   // JPEG

    uint64_t logCount() const { return m_logCount.load(); }   // lines ever received
    std::vector<std::string> log() const;                     // the last 500 lines
    void clearLog();

private:
    void run(std::string host, int port, std::string code);
    bool handle(uint8_t type, const std::vector<uint8_t>& payload);
    bool pump(int timeoutMs);
    bool doSend(const std::filesystem::path& folder);
    bool streamFiles(const std::vector<std::string>& paths);
    void finishSend(bool ok, const std::string& message);
    void addLog(const std::string& line);

    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    std::atomic<State> m_state{Off};
    std::atomic<uint64_t> m_shotSerial{0}, m_logCount{0};
    mutable std::mutex m_mx;
    std::string m_error, m_host;
    int m_port = 0;
    Json m_info;
    Progress m_progress;
    std::deque<std::filesystem::path> m_sendQueue;
    int m_shotWanted = 0;
    std::vector<uint8_t> m_shot;
    int m_shotW = 0, m_shotH = 0;
    std::deque<std::string> m_log;
    // network thread
    void* m_sock = nullptr;                    // live::Socket*
    std::vector<std::string> m_need;
    bool m_gotNeed = false, m_waitReload = false;
    double m_reloadSince = 0;
    std::filesystem::path m_sendFolder;

    // Bonjour
    mutable std::mutex m_dnsMx;
    std::vector<LiveDevice> m_found;
    void* m_browse = nullptr;
    friend struct LiveDns;
};

} // namespace app
