#include "MayaLink.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

namespace mayashader {

static const uintptr_t kNone = ~(uintptr_t)0;

std::string pyStr(const std::string& s) {
    std::string r = "'";
    for (char c : s) {
        if (c == '\\' || c == '\'') r += '\\';
        r += c;
    }
    return r + "'";
}

void MayaLink::start(int port) {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    m_port = port;
    m_retryAt = 0;
}

void MayaLink::close() {
    if (m_sock != kNone) closesocket((SOCKET)m_sock);
    m_sock = kNone;
    m_connecting = m_connected = false;
    m_retryAt = GetTickCount64() + 2000;
}

void MayaLink::stop() {
    close();
    WSACleanup();
}

void MayaLink::poll() {
    if (m_sock == kNone) {
        if (GetTickCount64() < m_retryAt || m_port <= 0) return;
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) { m_retryAt = GetTickCount64() + 2000; return; }
        u_long nb = 1;
        ioctlsocket(s, FIONBIO, &nb);
        sockaddr_in a = {};
        a.sin_family = AF_INET;
        a.sin_port = htons((u_short)m_port);
        inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
        connect(s, (sockaddr*)&a, sizeof a);   // non blocking: WSAEWOULDBLOCK, finished below
        m_sock = (uintptr_t)s;
        m_connecting = true;
    }
    SOCKET s = (SOCKET)m_sock;
    if (m_connecting) {
        fd_set w, e;
        FD_ZERO(&w);
        FD_ZERO(&e);
        FD_SET(s, &w);
        FD_SET(s, &e);
        timeval tv = {0, 0};
        if (select(0, nullptr, &w, &e, &tv) <= 0) return;
        if (FD_ISSET(s, &e)) { close(); return; }
        m_connecting = false;
        m_connected = true;
    }
    char buf[4096];
    for (;;) {
        int n = recv(s, buf, sizeof buf, 0);
        if (n > 0) {
            m_buffer.append(buf, n);
            continue;
        }
        if (n == 0 || WSAGetLastError() != WSAEWOULDBLOCK) { close(); return; }
        break;
    }
    // keep the last non empty reply line (Maya ends each reply with a newline and a NUL)
    size_t end;
    while ((end = m_buffer.find_first_of(std::string("\n\0", 2))) != std::string::npos) {
        std::string line = m_buffer.substr(0, end);
        m_buffer.erase(0, end + 1);
        if (!line.empty() && line != "None") m_reply = line.size() > 160 ? line.substr(0, 160) : line;
    }
}

bool MayaLink::send(const std::string& line) {
    if (!m_connected) return false;
    std::string msg = line + "\n";
    const char* p = msg.data();
    int left = (int)msg.size();
    while (left > 0) {
        int n = ::send((SOCKET)m_sock, p, left, 0);
        if (n > 0) { p += n; left -= n; continue; }
        if (WSAGetLastError() == WSAEWOULDBLOCK) { Sleep(1); continue; }
        close();
        return false;
    }
    return true;
}

} // namespace mayashader
