// game - local HTTP command API (127.0.0.1 only) so tools such as Claude can drive the editor.
// The socket runs on a background thread; requests are queued and executed on the main
// thread by the host (pollRequests / reply), so handlers can touch the scene safely.
#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace game {

struct CommandRequest {
    std::string method;   // GET / POST
    std::string path;     // e.g. /cmd
    std::string body;
    // filled by the main thread
    std::string response;
    int status = 200;
    bool done = false;
};

class CommandServer {
public:
    ~CommandServer() { stop(); }
    bool start(int port);
    void stop();
    bool running() const { return m_running; }
    int port() const { return m_port; }

    // Main thread: next pending request (nullptr if none). Call complete() once handled.
    std::shared_ptr<CommandRequest> next();
    void complete(const std::shared_ptr<CommandRequest>& r);

private:
    void loop();
    void serve(uintptr_t client);

    std::thread m_thread;
    std::atomic<bool> m_running{false};
    uintptr_t m_listen = ~(uintptr_t)0;
    int m_port = 0;
    std::mutex m_mx;
    std::condition_variable m_cv;
    std::deque<std::shared_ptr<CommandRequest>> m_pending;
};

} // namespace game
