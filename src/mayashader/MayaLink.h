// Client of the Maya commandPort (sourceType python): one Python line per message, replies drained and kept.
// The connection is retried every 2 s, so the graph can be opened before Maya listens.
#pragma once
#include <cstdint>
#include <string>

namespace mayashader {

class MayaLink {
public:
    void start(int port);
    void stop();
    void poll();                       // connects / reads the replies; call every frame
    bool send(const std::string& pythonLine);
    bool connected() const { return m_connected; }
    const std::string& lastReply() const { return m_reply; }

private:
    void close();
    uintptr_t m_sock = ~(uintptr_t)0;
    int m_port = 0;
    bool m_connecting = false, m_connected = false;
    unsigned long long m_retryAt = 0;
    std::string m_reply, m_buffer;
};

// Python string literal of a UTF-8 text.
std::string pyStr(const std::string& s);

} // namespace mayashader
