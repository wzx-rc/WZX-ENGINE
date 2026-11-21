// WZX Live, the device side: the player listens for the editor on the local network (see src/game/LiveLink.h).
// The network runs on a thread of its own; the platform file (main_win32.cpp, ios/main_ios.mm) asks each frame
// what to do on the main thread: restart the game from the live folder, take a screenshot, show the waiting screen.
// The editor can only write inside the live folder (relative paths, no ".."), and only after the pairing code.
#pragma once
#include "game/LiveLink.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Diligent { struct IDeviceContext; struct IRenderDevice; struct ITexture; }

namespace player {

class Game;

struct LiveConfig {
    std::string liveDir;           // writable folder of the received game (iOS: Documents/live)
    std::string stateFile;         // id and pairing code of this device, kept across launches
    std::string deviceType = "Desktop";   // Desktop, Tablet (iPad), Phone (iPhone)
    std::string model;             // iPhone15,2, PC...
    std::string name;              // shown in the editor
    std::string built;             // date of the build of the app
    int port = live::kDefaultPort;
    bool loopbackOnly = false;     // Windows player: 127.0.0.1 only unless --live-lan (no firewall prompt in tests)
    bool advertise = true;         // Bonjour _wzx-live._tcp
    bool hasBundledGame = false;   // a game inside the app (the "Jouer le jeu integre" button)
    // tests of the editor's warnings: claim to be an older app (these node types / features unknown, content version - 1)
    // or another protocol
    std::vector<std::string> simulateMissing;
    int simulateProtocol = 0;
};

class LiveHost {
public:
    enum Action { None, PlayLast, PlayBundled, ShowWaiting };
    ~LiveHost() { stop(); }
    bool start(const LiveConfig& cfg, std::string* err);
    void stop();

    // ---- main thread
    bool takeReload() { return m_reload.exchange(false); }   // the editor sent a game and asked to play it
    void reloaded(bool ok, const std::string& title, const std::vector<std::string>& warnings, const std::string& error);
    Action takeAction() { Action a = m_action; m_action = None; return a; }
    bool hasLastGame() const;      // a game received earlier is in the live folder
    void setPlaying(bool playing) { m_playing = playing; }   // a game runs (else: the waiting screen)
    void log(const std::string& line);
    void pumpGameLog(const Game* game);   // the new lines of the graph's log
    bool wantsShot() const { return m_shotWanted.load(); }
    // After the frame is drawn into the back buffer, before Present: read it back, the network thread encodes the JPEG.
    void captureShot(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context, Diligent::ITexture* backBuffer);
    // Inside the ImGui frame (Player::overlay): the waiting screen, or the LIVE badge over the game.
    void drawOverlay(float uiScale, float safeLeft, float safeTop);

    const std::string& code() const { return m_code; }
    int port() const { return m_port; }
    bool connected() const { return m_connected.load(); }
    const LiveConfig& config() const { return m_cfg; }

private:
    struct Out { uint8_t type; std::vector<uint8_t> data; };
    struct Cached { uint64_t size = 0, hash = 0; int64_t time = 0; };
    void loop();
    void serve(live::Socket& c);
    void queue(uint8_t type, const std::string& json);
    void status(const std::string& event, bool ok, const std::string& message);
    std::string infoJson(const std::string& auth) const;
    std::vector<std::string> manifest(const std::string& json, std::string& err);
    bool chunk(const std::vector<uint8_t>& payload, std::string& err);
    void encodeShot();
    void setStatus(const std::string& s);
    std::string statusText() const;
    void advertise();
    void unadvertise();

    LiveConfig m_cfg;
    std::string m_id, m_code;
    int m_port = 0;
    live::Socket m_listen;
    std::thread m_thread;
    std::atomic<bool> m_running{false}, m_connected{false}, m_reload{false}, m_shotWanted{false};
    std::atomic<int> m_shotWidth{1280};
    Action m_action = None;
    std::atomic<bool> m_playing{false};
    bool m_panel = false;
    uint64_t m_gameLogSeen = 0;
    const Game* m_gameSeen = nullptr;

    mutable std::mutex m_mx;              // guards everything below
    std::deque<Out> m_out;                // to the editor (sent by the network thread once paired)
    std::deque<std::string> m_history;    // the last log lines, sent to an editor when it pairs
    std::string m_status = "En attente du PC";
    std::string m_banner;                 // shown a few seconds over the game (game received, error)
    bool m_bannerError = false;
    double m_bannerTime = 0;
    std::vector<uint8_t> m_shotPixels;    // RGBA read back, encoded by the network thread
    uint32_t m_shotW = 0, m_shotH = 0;
    std::string m_peer;
    std::vector<std::string> m_addresses;

    // network thread only: the game being received
    std::map<std::string, std::pair<uint64_t, uint64_t>> m_manifest;   // path -> size, hash
    std::map<std::string, Cached> m_cache;                             // path -> what was hashed
    std::string m_title;
    size_t m_needCount = 0, m_doneCount = 0;
    void* m_dnsRef = nullptr;   // Bonjour registration
};

} // namespace player
