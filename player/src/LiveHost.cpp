// WZX Live, the device side (see LiveHost.h and src/game/LiveLink.h).
#include "LiveHost.h"
#include "Game.h"
#include "app/Json.h"
#include "imgui.h"

#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "Graphics/GraphicsEngine/interface/Texture.h"
#include "Common/interface/RefCntAutoPtr.hpp"

#define STB_IMAGE_WRITE_STATIC   // private copy: the headless player has its own (PNG)
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#ifdef _WIN32
#include <windows.h>
#include <windns.h>
#pragma comment(lib, "dnsapi.lib")
#elif defined(__APPLE__)
#include <arpa/inet.h>
#include <dispatch/dispatch.h>
#include <dns_sd.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <sstream>

namespace fs = std::filesystem;
using app::Json;
using app::jsonEscape;

namespace player {

namespace {

double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

std::string readText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return std::string();
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string q(const std::string& s) { return "\"" + jsonEscape(s) + "\""; }

int64_t fileTime(const fs::path& p) {
    std::error_code ec;
    return (int64_t)fs::last_write_time(p, ec).time_since_epoch().count();
}

#ifdef _WIN32
std::wstring widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}
struct WinDns { DNS_SERVICE_REGISTER_REQUEST rq = {}; PDNS_SERVICE_INSTANCE inst = nullptr; };
void WINAPI dnsRegistered(DWORD, PVOID, PDNS_SERVICE_INSTANCE inst) { if (inst) DnsServiceFreeInstance(inst); }
#elif defined(__APPLE__)
void DNSSD_API dnsRegistered(DNSServiceRef, DNSServiceFlags, DNSServiceErrorType, const char*, const char*, const char*, void*) {}
#endif

} // namespace

// ---------------------------------------------------------------------------- start / stop

bool LiveHost::start(const LiveConfig& cfg, std::string* err) {
    stop();
    m_cfg = cfg;
    std::error_code ec;
    fs::create_directories(fs::u8path(cfg.liveDir), ec);
    // identity of this device: an id and a 4-digit pairing code, kept so the editor asks for the code only once
    Json st;
    Json::parse(readText(fs::u8path(cfg.stateFile)), st);
    m_id = st["id"].str();
    m_code = st["code"].str();
    if (m_id.size() != 16 || m_code.size() != 4) {
        std::random_device rd;
        std::mt19937_64 rng(((uint64_t)rd() << 32) ^ rd() ^ (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count());
        m_id = live::hex64(rng());
        char c[8];
        std::snprintf(c, sizeof c, "%04d", (int)(rng() % 10000));
        m_code = c;
        fs::create_directories(fs::u8path(cfg.stateFile).parent_path(), ec);
        std::ofstream(fs::u8path(cfg.stateFile), std::ios::binary) << "{\"id\": " << q(m_id) << ", \"code\": " << q(m_code) << "}\n";
    }
    // the port: the usual one, else the next ones (another player on the same PC)
    std::string e;
    for (int k = 0; k < 10 && !m_listen.valid(); ++k) m_listen.listen(cfg.port + k, cfg.loopbackOnly, &e);
    if (!m_listen.valid()) {
        if (err) *err = "WZX Live : " + e;
        return false;
    }
    m_port = m_listen.localPort();
    m_addresses = cfg.loopbackOnly ? std::vector<std::string>{"127.0.0.1"} : live::localAddresses();
    m_running = true;
    m_thread = std::thread([this] { loop(); });
    if (cfg.advertise) advertise();
    log("WZX Live : en attente du PC sur le port " + std::to_string(m_port) + (cfg.loopbackOnly ? " (ce PC seulement)" : ""));
    return true;
}

void LiveHost::stop() {
    if (!m_running) return;
    m_running = false;
    unadvertise();
    if (m_thread.joinable()) m_thread.join();
    m_listen.close();
}

void LiveHost::advertise() {
#ifdef _WIN32
    WinDns* d = new WinDns();
    wchar_t comp[256] = {};
    DWORD n = 256;
    GetComputerNameW(comp, &n);
    const std::wstring inst = widen(m_cfg.name) + L"._wzx-live._tcp.local", host = std::wstring(comp) + L".local";
    const std::wstring id = widen(m_id), type = widen(m_cfg.deviceType);
    PCWSTR keys[] = {L"id", L"type"}, values[] = {id.c_str(), type.c_str()};
    d->inst = DnsServiceConstructInstance(inst.c_str(), host.c_str(), nullptr, nullptr, (WORD)m_port, 0, 0, 2, keys, values);
    d->rq.Version = DNS_QUERY_REQUEST_VERSION1;
    d->rq.pServiceInstance = d->inst;
    d->rq.pRegisterCompletionCallback = dnsRegistered;
    if (!d->inst || DnsServiceRegister(&d->rq, nullptr) != DNS_REQUEST_PENDING) {
        if (d->inst) DnsServiceFreeInstance(d->inst);
        delete d;
        log("WZX Live : annonce Bonjour impossible (l'adresse se tape à la main dans l'éditeur)");
        return;
    }
    m_dnsRef = d;
#elif defined(__APPLE__)
    TXTRecordRef txt;
    TXTRecordCreate(&txt, 0, nullptr);
    TXTRecordSetValue(&txt, "id", (uint8_t)m_id.size(), m_id.c_str());
    TXTRecordSetValue(&txt, "type", (uint8_t)m_cfg.deviceType.size(), m_cfg.deviceType.c_str());
    DNSServiceRef ref = nullptr;
    DNSServiceErrorType e = DNSServiceRegister(&ref, 0, 0, m_cfg.name.c_str(), live::kService, nullptr, nullptr, htons((uint16_t)m_port),
                                               TXTRecordGetLength(&txt), TXTRecordGetBytesPtr(&txt), dnsRegistered, nullptr);
    TXTRecordDeallocate(&txt);
    if (e != kDNSServiceErr_NoError || !ref) {
        log("WZX Live : annonce Bonjour impossible (erreur " + std::to_string((int)e) + ")");
        return;
    }
    DNSServiceSetDispatchQueue(ref, dispatch_get_main_queue());
    m_dnsRef = ref;
#endif
}

void LiveHost::unadvertise() {
    if (!m_dnsRef) return;
#ifdef _WIN32
    WinDns* d = (WinDns*)m_dnsRef;
    DnsServiceDeRegister(&d->rq, nullptr);
    // (the instance stays allocated: the deregistration completes asynchronously, at exit)
#elif defined(__APPLE__)
    DNSServiceRefDeallocate((DNSServiceRef)m_dnsRef);
#endif
    m_dnsRef = nullptr;
}

// ---------------------------------------------------------------------------- main thread

bool LiveHost::hasLastGame() const { return fs::exists(fs::u8path(m_cfg.liveDir) / "game.json"); }

void LiveHost::log(const std::string& line) {
    std::fprintf(stderr, "%s\n", line.c_str());
    std::lock_guard<std::mutex> lk(m_mx);
    m_history.push_back(line);
    while (m_history.size() > 300) m_history.pop_front();
    if (m_connected) {   // (before the pairing, the editor gets the history instead)
        const std::string j = "{\"lines\": [" + q(line) + "]}";
        m_out.push_back({live::Log, std::vector<uint8_t>(j.begin(), j.end())});
    }
}

void LiveHost::pumpGameLog(const Game* game) {
    if (game != m_gameSeen) { m_gameSeen = game; m_gameLogSeen = 0; }
    if (!game || game->logCount() == m_gameLogSeen) return;
    const auto& lines = game->log();
    const uint64_t fresh = std::min<uint64_t>(game->logCount() - m_gameLogSeen, lines.size());
    for (size_t i = lines.size() - (size_t)fresh; i < lines.size(); ++i) log("[graphe] " + lines[i]);
    m_gameLogSeen = game->logCount();
}

void LiveHost::reloaded(bool ok, const std::string& title, const std::vector<std::string>& warnings, const std::string& error) {
    for (const std::string& w : warnings) log("[chargement] " + w);
    if (ok) log("Jeu rechargé : " + title + (warnings.empty() ? "" : " (" + std::to_string(warnings.size()) + " avertissement(s))"));
    else log("Erreur au chargement du jeu : " + error);
    {
        std::lock_guard<std::mutex> lk(m_mx);
        m_banner = ok ? "Jeu reçu du PC : " + title : "Erreur : " + error;
        m_bannerError = !ok;
        m_bannerTime = now();
    }
    status("reloaded", ok, ok ? title : error);
    setStatus(ok ? "Jeu reçu et lancé" : "Erreur au chargement");
}

void LiveHost::captureShot(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context, Diligent::ITexture* backBuffer) {
    using namespace Diligent;
    m_shotWanted = false;
    if (!device || !context || !backBuffer) return;
    TextureDesc td = backBuffer->GetDesc();
    td.Name = "Live screenshot";
    td.BindFlags = BIND_NONE;
    td.Usage = USAGE_STAGING;
    td.CPUAccessFlags = CPU_ACCESS_READ;
    td.MipLevels = 1;
    td.SampleCount = 1;
    RefCntAutoPtr<ITexture> staging;
    device->CreateTexture(td, nullptr, &staging);
    if (!staging) { log("WZX Live : capture impossible"); return; }
    CopyTextureAttribs copy(backBuffer, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, staging, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->CopyTexture(copy);
    context->WaitForIdle();
    MappedTextureSubresource m;
    context->MapTextureSubresource(staging, 0, 0, MAP_READ, MAP_FLAG_DO_NOT_WAIT, nullptr, m);
    if (!m.pData) { log("WZX Live : lecture de la capture impossible"); return; }
    const bool bgra = td.Format == TEX_FORMAT_BGRA8_UNORM || td.Format == TEX_FORMAT_BGRA8_UNORM_SRGB;
    std::vector<uint8_t> px((size_t)td.Width * td.Height * 4);
    for (uint32_t y = 0; y < td.Height; ++y) {
        const uint8_t* src = (const uint8_t*)m.pData + (size_t)y * m.Stride;
        uint8_t* dst = &px[(size_t)y * td.Width * 4];
        for (uint32_t x = 0; x < td.Width; ++x) {
            dst[x * 4 + 0] = src[x * 4 + (bgra ? 2 : 0)];
            dst[x * 4 + 1] = src[x * 4 + 1];
            dst[x * 4 + 2] = src[x * 4 + (bgra ? 0 : 2)];
            dst[x * 4 + 3] = 255;
        }
    }
    context->UnmapTextureSubresource(staging, 0, 0);
    std::lock_guard<std::mutex> lk(m_mx);
    m_shotPixels.swap(px);
    m_shotW = td.Width;
    m_shotH = td.Height;
}

// ---------------------------------------------------------------------------- overlay (ImGui)

void LiveHost::setStatus(const std::string& s) {
    std::lock_guard<std::mutex> lk(m_mx);
    m_status = s;
}

std::string LiveHost::statusText() const {
    std::lock_guard<std::mutex> lk(m_mx);
    return m_status;
}

void LiveHost::drawOverlay(float k, float safeLeft, float safeTop) {
    ImGuiIO& io = ImGui::GetIO();
    const std::string st = statusText();
    const bool conn = m_connected;
    const ImVec4 green(0.35f, 0.85f, 0.45f, 1.0f), amber(0.95f, 0.75f, 0.3f, 1.0f), red(0.95f, 0.42f, 0.38f, 1.0f);
    auto code = [&] {
        std::string s;
        for (char c : m_code) s += std::string(s.empty() ? "" : " ") + c;
        return s;
    };
    auto details = [&] {
        ImGui::TextDisabled("Appareil");
        ImGui::SameLine(110 * k);
        ImGui::TextUnformatted(m_cfg.name.c_str());
        ImGui::TextDisabled("Adresse");
        ImGui::SameLine(110 * k);
        std::string a;
        for (const std::string& ad : m_addresses) a += (a.empty() ? "" : "  ") + ad;
        ImGui::Text("%s   port %d", a.empty() ? "(pas de réseau)" : a.c_str(), m_port);
        ImGui::TextDisabled("État");
        ImGui::SameLine(110 * k);
        ImGui::TextColored(conn ? green : amber, "%s", st.c_str());
    };
    if (!m_playing) {
        // the waiting screen: what to type in the editor
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowBgAlpha(0.92f);
        const ImGuiWindowFlags f = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings;
        if (ImGui::Begin("##livewait", nullptr, f)) {
            ImGui::TextColored(amber, "WZX LIVE");
            ImGui::SetWindowFontScale(1.6f);
            ImGui::TextUnformatted(conn ? "PC connecté" : "En attente du PC");
            ImGui::SetWindowFontScale(1.0f);
            ImGui::Separator();
            ImGui::TextDisabled("Code d'appairage");
            ImGui::SetWindowFontScale(3.2f);
            ImGui::TextColored(amber, "%s", code().c_str());
            ImGui::SetWindowFontScale(1.0f);
            ImGui::Spacing();
            details();
            ImGui::Spacing();
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 430 * k);
            ImGui::TextDisabled("Sur le PC : éditeur WZX Engine, menu Tools > Appareils (WZX Live). Choisir cet appareil, "
                                "taper le code une fois, puis \"Envoyer sur l'appareil\".");
            ImGui::PopTextWrapPos();
            {
                std::lock_guard<std::mutex> lk(m_mx);
                if (!m_banner.empty() && m_bannerError) ImGui::TextColored(red, "%s", m_banner.c_str());
            }
            ImGui::Spacing();
            if (hasLastGame() && ImGui::Button("Reprendre le dernier envoi")) m_action = PlayLast;
            if (m_cfg.hasBundledGame) {
                if (hasLastGame()) ImGui::SameLine();
                if (ImGui::Button("Jouer le jeu intégré")) m_action = PlayBundled;
            }
        }
        ImGui::End();
        return;
    }
    // over the game: a small LIVE badge (tap: the code and the state), and a banner when a game arrives
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, safeTop + 4 * k), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowBgAlpha(m_panel ? 0.9f : 0.45f);
    const ImGuiWindowFlags f = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
    if (ImGui::Begin("##livebadge", nullptr, f)) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        if (ImGui::SmallButton(conn ? "LIVE  PC" : "LIVE")) m_panel = !m_panel;
        ImGui::PopStyleColor();
        {
            std::lock_guard<std::mutex> lk(m_mx);
            if (!m_banner.empty() && (m_bannerError || now() - m_bannerTime < 4.0)) {
                ImGui::TextColored(m_bannerError ? red : green, "%s", m_banner.c_str());
            }
        }
        if (m_panel) {
            ImGui::Separator();
            ImGui::TextDisabled("Code");
            ImGui::SameLine(110 * k);
            ImGui::TextColored(amber, "%s", m_code.c_str());
            details();
            if (ImGui::Button("Écran d'attente")) { m_action = ShowWaiting; m_panel = false; }
            ImGui::SameLine();
            if (ImGui::Button("Fermer")) m_panel = false;
        }
    }
    ImGui::End();
    (void)safeLeft;
}

// ---------------------------------------------------------------------------- network thread

void LiveHost::queue(uint8_t type, const std::string& json) {
    std::lock_guard<std::mutex> lk(m_mx);
    m_out.push_back({type, std::vector<uint8_t>(json.begin(), json.end())});
}

void LiveHost::status(const std::string& event, bool ok, const std::string& message) {
    queue(live::Status, "{\"event\": " + q(event) + ", \"ok\": " + (ok ? "true" : "false") + ", \"message\": " + q(message) + "}");
}

std::string LiveHost::infoJson(const std::string& auth) const {
    std::ostringstream s;
    // tests (--live-simulate-old): an older app, without the node types and features listed in the config
    std::vector<std::string> nodes = live::nodeTypes(), features = live::engineFeatures();
    auto drop = [](std::vector<std::string>& v, const std::vector<std::string>& gone) {
        for (const std::string& g : gone) v.erase(std::remove(v.begin(), v.end(), g), v.end());
    };
    drop(nodes, m_cfg.simulateMissing);
    drop(features, m_cfg.simulateMissing);
    const int protocol = m_cfg.simulateProtocol ? m_cfg.simulateProtocol : live::kProtocol;
    const int content = m_cfg.simulateMissing.empty() ? live::kContentVersion : live::kContentVersion - 1;
    s << "{\"protocol\": " << protocol << ", \"content\": " << content << ", \"signature\": "
      << q(m_cfg.simulateMissing.empty() && !m_cfg.simulateProtocol ? live::engineSignature() : std::string("simulated-old"))
      << ", \"built\": " << q(m_cfg.built) << ", \"device\": " << q(m_cfg.deviceType) << ", \"model\": " << q(m_cfg.model)
      << ", \"name\": " << q(m_cfg.name) << ", \"id\": " << q(m_id) << ", \"auth\": " << q(auth)
      << ", \"state\": " << q(m_playing ? "playing" : "waiting") << ", \"hasGame\": " << (hasLastGame() ? "true" : "false")
      << ", \"nodes\": " << live::jsonList(nodes) << ", \"features\": " << live::jsonList(features) << "}";
    return s.str();
}

void LiveHost::loop() {
    while (m_running) {
        live::Socket c = m_listen.accept(200);
        if (!c.valid()) continue;
        serve(c);
        {
            std::lock_guard<std::mutex> lk(m_mx);
            m_connected = false;
            m_out.clear();
        }
        setStatus("En attente du PC");
    }
}

std::vector<std::string> LiveHost::manifest(const std::string& text, std::string& err) {
    Json j;
    std::vector<std::string> need;
    m_manifest.clear();
    if (!Json::parse(text, j) || j.type != Json::Object) { err = "manifeste illisible"; return need; }
    m_title = j["title"].str("WZX Game");
    const fs::path root = fs::u8path(m_cfg.liveDir);
    for (const Json& f : j["files"].a) {
        const std::string p = f["p"].str();
        if (!live::safeRelativePath(p)) { err = "chemin refusé : " + p; m_manifest.clear(); return {}; }
        uint64_t h = 0;
        std::istringstream hs(f["h"].str());
        hs >> std::hex >> h;
        const uint64_t size = (uint64_t)f["s"].num(0);
        m_manifest[p] = {size, h};
        // already here with the same content? (hashes kept by size and date, so a big file is read once)
        const fs::path file = root / fs::u8path(p);
        std::error_code ec;
        const uint64_t onDisk = fs::exists(file, ec) ? (uint64_t)fs::file_size(file, ec) : UINT64_MAX;
        bool same = false;
        if (onDisk == size) {
            Cached& c = m_cache[p];
            const int64_t t = fileTime(file);
            if (c.size != size || c.time != t) {
                uint64_t hh = 0, ss = 0;
                live::hashFile(file, hh, ss);
                c = {ss, hh, t};
            }
            same = c.hash == h;
        }
        if (!same) need.push_back(p);
    }
    // files of an older game that this one does not have: removed (the folder holds exactly the game sent)
    std::error_code ec;
    std::vector<fs::path> stale;
    for (auto e = fs::recursive_directory_iterator(root, ec); !ec && e != fs::recursive_directory_iterator(); e.increment(ec)) {
        if (!e->is_regular_file()) continue;
        const std::string rel = fs::relative(e->path(), root, ec).generic_u8string();
        if (!m_manifest.count(rel)) stale.push_back(e->path());
    }
    for (const fs::path& p : stale) fs::remove(p, ec);
    return need;
}

bool LiveHost::chunk(const std::vector<uint8_t>& payload, std::string& err) {
    if (payload.size() < 4) { err = "bloc vide"; return false; }
    const uint32_t hn = payload[0] | payload[1] << 8 | payload[2] << 16 | (uint32_t)payload[3] << 24;
    if (4 + (size_t)hn > payload.size()) { err = "bloc tronqué"; return false; }
    Json h;
    if (!Json::parse(std::string((const char*)&payload[4], hn), h)) { err = "en-tete de bloc illisible"; return false; }
    const std::string p = h["p"].str();
    auto it = m_manifest.find(p);
    if (!live::safeRelativePath(p) || it == m_manifest.end()) { err = "fichier hors du manifeste : " + p; return false; }
    const uint64_t off = (uint64_t)h["o"].num(0), total = it->second.first;
    const size_t n = payload.size() - 4 - hn;
    if (n == 0 && off > 0) return true;   // (nothing to add to a file already complete)
    if (off + n > total) { err = "fichier plus long que prévu : " + p; return false; }
    const fs::path file = fs::u8path(m_cfg.liveDir) / fs::u8path(p);
    const fs::path part = fs::path(file).concat(".part");
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    {
        std::ofstream f(part, off == 0 ? std::ios::binary | std::ios::trunc : std::ios::binary | std::ios::app);
        if (!f) { err = "écriture impossible : " + p; return false; }
        if (n) f.write((const char*)&payload[4 + hn], (std::streamsize)n);
        if (!f) { err = "écriture impossible (place ?) : " + p; return false; }
    }
    if (off + n >= total) {
        fs::remove(file, ec);
        fs::rename(part, file, ec);
        if (ec) { err = "écriture impossible : " + p + " (" + ec.message() + ")"; return false; }
        m_cache[p] = {total, it->second.second, fileTime(file)};
        ++m_doneCount;
        setStatus("Réception " + std::to_string(m_doneCount) + " / " + std::to_string(m_needCount) + " fichiers");
    }
    return true;
}

void LiveHost::encodeShot() {
    std::vector<uint8_t> px;
    uint32_t w = 0, h = 0;
    {
        std::lock_guard<std::mutex> lk(m_mx);
        if (m_shotPixels.empty()) return;
        px.swap(m_shotPixels);
        w = m_shotW;
        h = m_shotH;
    }
    // smaller for the network: at most maxWidth pixels wide (box filter)
    const int maxW = std::max(160, m_shotWidth.load());
    int f = 1;
    while (w / f > (uint32_t)maxW) ++f;
    const uint32_t ow = w / f, oh = h / f;
    std::vector<uint8_t> out((size_t)ow * oh * 3);
    for (uint32_t y = 0; y < oh; ++y)
        for (uint32_t x = 0; x < ow; ++x) {
            uint32_t s[3] = {0, 0, 0};
            for (int dy = 0; dy < f; ++dy)
                for (int dx = 0; dx < f; ++dx) {
                    const uint8_t* p = &px[((size_t)(y * f + dy) * w + x * f + dx) * 4];
                    s[0] += p[0]; s[1] += p[1]; s[2] += p[2];
                }
            for (int c = 0; c < 3; ++c) out[((size_t)y * ow + x) * 3 + c] = (uint8_t)(s[c] / (f * f));
        }
    std::vector<uint8_t> msg(8);
    for (int i = 0; i < 4; ++i) { msg[i] = (uint8_t)(ow >> (8 * i)); msg[4 + i] = (uint8_t)(oh >> (8 * i)); }
    stbi_write_jpg_to_func([](void* ctx, void* data, int size) {
        auto* v = (std::vector<uint8_t>*)ctx;
        v->insert(v->end(), (uint8_t*)data, (uint8_t*)data + size);
    }, &msg, (int)ow, (int)oh, 3, out.data(), 85);
    std::lock_guard<std::mutex> lk(m_mx);
    m_out.push_back({live::Shot, std::move(msg)});
}

void LiveHost::serve(live::Socket& c) {
    bool authed = false;
    int failures = 0;
    m_peer = c.peerAddress();
    setStatus("PC connecté (" + m_peer + "), code attendu");
    std::vector<uint8_t> payload;
    while (m_running) {
        // to the editor: the log, the state of a reception, screenshots (once paired)
        if (authed) {
            encodeShot();
            std::deque<Out> out;
            {
                std::lock_guard<std::mutex> lk(m_mx);
                out.swap(m_out);
            }
            for (const Out& o : out)
                if (!live::sendMessage(c, o.type, o.data.data(), o.data.size())) return;
        }
        uint8_t type = 0;
        const int r = live::recvMessage(c, type, payload, 30);
        if (r < 0) return;
        if (r == 0) continue;
        const std::string text(payload.begin(), payload.end());
        if (type == live::Hello) {
            Json j;
            Json::parse(text, j);
            const bool ok = j["code"].str() == m_code;
            if (!ok && ++failures > 3) return;   // guessing the code: the connection is closed
            if (!ok) std::this_thread::sleep_for(std::chrono::milliseconds(800));
            if (!live::sendJson(c, live::Info, infoJson(ok ? "ok" : (j["code"].str().empty() ? "needed" : "bad")))) return;
            if (ok && !authed) {
                authed = true;
                setStatus("PC connecté (" + m_peer + ")");
                std::string lines;
                {
                    std::lock_guard<std::mutex> lk(m_mx);   // (the lines logged from now on go to m_out)
                    m_connected = true;
                    m_out.clear();
                    for (const std::string& l : m_history) lines += (lines.empty() ? "" : ", ") + q(l);
                }
                if (!live::sendJson(c, live::Log, "{\"history\": true, \"lines\": [" + lines + "]}")) return;
            }
            continue;
        }
        if (!authed) continue;   // nothing else before the code
        std::string err;
        switch (type) {
        case live::Manifest: {
            setStatus("Comparaison des fichiers...");
            std::vector<std::string> need = manifest(text, err);
            if (!err.empty()) {
                status("error", false, err);
                log("WZX Live : " + err);
                break;
            }
            m_needCount = need.size();
            m_doneCount = 0;
            setStatus(need.empty() ? "Rien de nouveau à recevoir" : "Réception de " + std::to_string(need.size()) + " fichiers");
            if (!live::sendJson(c, live::Need, "{\"paths\": " + live::jsonList(need) + "}")) return;
            break;
        }
        case live::Chunk:
            if (!chunk(payload, err)) {
                status("error", false, err);
                log("WZX Live : " + err);
            }
            break;
        case live::Reload: {
            // every file of the manifest must be there
            std::string missing;
            for (const auto& kv : m_manifest)
                if (!fs::exists(fs::u8path(m_cfg.liveDir) / fs::u8path(kv.first))) { missing = kv.first; break; }
            if (m_manifest.empty() || !missing.empty()) {
                status("error", false, m_manifest.empty() ? "aucun jeu reçu" : "fichier manquant : " + missing);
                break;
            }
            status("received", true, std::to_string(m_needCount) + " fichier(s) reçu(s)");
            setStatus("Chargement du jeu...");
            m_reload = true;
            break;
        }
        case live::ShotRequest: {
            Json j;
            Json::parse(text, j);
            m_shotWidth = (int)j["maxWidth"].num(1280);
            m_shotWanted = true;
            break;
        }
        default: break;
        }
    }
}

} // namespace player
