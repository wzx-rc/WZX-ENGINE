// BrickIt - PDF instructions of the manual (the web uses pdf.js): the Windows PDF renderer (Windows.Data.Pdf) on a
// worker thread. Open a file, read the page sizes, render pages at a given width into RGBA8 images.
#pragma once
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace brickit {

class PdfReader {
public:
    PdfReader();
    ~PdfReader();
    // Opens a file (asynchronous): ready() turns true with the page sizes, or failed() with a message.
    void open(const std::wstring& path);
    bool ready() const { std::lock_guard<std::mutex> l(m_mx); return m_ready; }
    bool failed(std::string* msg = nullptr) const { std::lock_guard<std::mutex> l(m_mx); if (msg) *msg = m_error; return m_failed; }
    int pageCount() const { std::lock_guard<std::mutex> l(m_mx); return (int)m_sizes.size(); }
    std::vector<std::pair<float, float>> sizes() const { std::lock_guard<std::mutex> l(m_mx); return m_sizes; }
    // Asks for page n (0-based) rendered `width` pixels wide; the image comes back through take().
    void request(int page, int width);
    struct Image { int page = -1, w = 0, h = 0; std::vector<uint8_t> rgba; };
    bool take(Image& out);
    int generation() const { return m_gen; }

private:
    void run();
    struct Job { int kind; std::wstring path; int page, width, gen; };
    mutable std::mutex m_mx;
    std::condition_variable m_cv;
    std::deque<Job> m_jobs;
    std::deque<Image> m_done;
    std::vector<std::pair<float, float>> m_sizes;
    bool m_ready = false, m_failed = false, m_quit = false;
    std::string m_error;
    int m_gen = 0;
    std::thread m_thread;
};

} // namespace brickit
