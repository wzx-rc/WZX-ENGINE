#include "CommandServer.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <algorithm>
#include <chrono>

#pragma comment(lib, "ws2_32.lib")

namespace game {

bool CommandServer::start(int port) {
    if (m_running) return true;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons((u_short)port);
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);   // local machine only
    if (bind(s, (sockaddr*)&a, sizeof a) != 0 || listen(s, 8) != 0) {
        closesocket(s);
        return false;
    }
    m_listen = (uintptr_t)s;
    m_port = port;
    m_running = true;
    m_thread = std::thread([this] { loop(); });
    return true;
}

void CommandServer::stop() {
    if (!m_running) return;
    m_running = false;
    closesocket((SOCKET)m_listen);
    m_cv.notify_all();
    if (m_thread.joinable()) m_thread.join();
    WSACleanup();
}

std::shared_ptr<CommandRequest> CommandServer::next() {
    std::lock_guard<std::mutex> lk(m_mx);
    for (auto& r : m_pending)
        if (!r->done && r->status != -1) { r->status = -1; return r; }   // -1 = taken by the main thread
    return nullptr;
}

void CommandServer::complete(const std::shared_ptr<CommandRequest>& r) {
    {
        std::lock_guard<std::mutex> lk(m_mx);
        if (r->status == -1) r->status = 200;
        r->done = true;
    }
    m_cv.notify_all();
}

void CommandServer::loop() {
    while (m_running) {
        SOCKET c = accept((SOCKET)m_listen, nullptr, nullptr);
        if (c == INVALID_SOCKET) continue;
        serve((uintptr_t)c);
        closesocket(c);
    }
}

static std::string statusText(int s) {
    switch (s) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 503: return "Service Unavailable";
    default: return "Error";
    }
}

void CommandServer::serve(uintptr_t client) {
    SOCKET c = (SOCKET)client;
    DWORD tmo = 5000;
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tmo, sizeof tmo);
    std::string data;
    char buf[8192];
    size_t headerEnd = std::string::npos;
    size_t contentLength = 0;
    for (;;) {
        int n = recv(c, buf, sizeof buf, 0);
        if (n <= 0) return;
        data.append(buf, n);
        if (headerEnd == std::string::npos) {
            headerEnd = data.find("\r\n\r\n");
            if (headerEnd != std::string::npos) {
                std::string h = data.substr(0, headerEnd);
                for (char& ch : h) if (ch >= 'A' && ch <= 'Z') ch = (char)(ch + 32);
                size_t p = h.find("content-length:");
                if (p != std::string::npos) contentLength = (size_t)atoll(h.c_str() + p + 15);
            }
        }
        if (headerEnd != std::string::npos && data.size() >= headerEnd + 4 + contentLength) break;
        if (data.size() > (16u << 20)) return;
    }
    auto req = std::make_shared<CommandRequest>();
    size_t sp1 = data.find(' '), sp2 = data.find(' ', sp1 + 1);
    req->method = data.substr(0, sp1);
    req->path = data.substr(sp1 + 1, sp2 - sp1 - 1);
    req->body = data.substr(headerEnd + 4, contentLength);
    {
        std::unique_lock<std::mutex> lk(m_mx);
        m_pending.push_back(req);
        // The main thread answers within a frame; long builds may take a while.
        bool ok = m_cv.wait_for(lk, std::chrono::seconds(120), [&] { return req->done || !m_running; });
        m_pending.erase(std::remove(m_pending.begin(), m_pending.end(), req), m_pending.end());
        if (!ok || !req->done) {
            req->status = 503;
            req->response = "{\"ok\": false, \"error\": \"l'editeur ne repond pas\"}";
        }
    }
    std::string out = "HTTP/1.1 " + std::to_string(req->status) + " " + statusText(req->status) +
                      "\r\nContent-Type: application/json; charset=utf-8\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\nContent-Length: " +
                      std::to_string(req->response.size()) + "\r\n\r\n" + req->response;
    send(c, out.data(), (int)out.size(), 0);
    shutdown(c, SD_SEND);
}

} // namespace game
