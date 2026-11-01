// C++/WinRT under /std:c++17 includes <experimental/coroutine> (only the blocking .get() is used here)
#define _SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS
#include "Pdf.h"
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Data.Pdf.h>
#include <winrt/Windows.Graphics.Imaging.h>

#pragma comment(lib, "windowsapp")

using namespace winrt;
using namespace winrt::Windows::Data::Pdf;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::Storage::Streams;
using namespace winrt::Windows::Graphics::Imaging;

namespace brickit {

PdfReader::PdfReader() { m_thread = std::thread([this] { run(); }); }

PdfReader::~PdfReader() {
    {
        std::lock_guard<std::mutex> l(m_mx);
        m_quit = true;
    }
    m_cv.notify_all();
    if (m_thread.joinable()) m_thread.join();
}

void PdfReader::open(const std::wstring& path) {
    std::lock_guard<std::mutex> l(m_mx);
    ++m_gen;
    m_jobs.clear();
    m_done.clear();
    m_sizes.clear();
    m_ready = m_failed = false;
    m_error.clear();
    m_jobs.push_back({0, path, 0, 0, m_gen});
    m_cv.notify_all();
}

void PdfReader::request(int page, int width) {
    std::lock_guard<std::mutex> l(m_mx);
    for (const Job& j : m_jobs)
        if (j.kind == 1 && j.page == page && j.width == width) return;
    m_jobs.push_back({1, L"", page, width, m_gen});
    m_cv.notify_all();
}

bool PdfReader::take(Image& out) {
    std::lock_guard<std::mutex> l(m_mx);
    if (m_done.empty()) return false;
    out = std::move(m_done.front());
    m_done.pop_front();
    return true;
}

void PdfReader::run() {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    PdfDocument doc{nullptr};
    int docGen = -1;
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> l(m_mx);
            m_cv.wait(l, [this] { return m_quit || !m_jobs.empty(); });
            if (m_quit) break;
            job = m_jobs.front();
            m_jobs.pop_front();
            if (job.gen != m_gen) continue;
        }
        try {
            if (job.kind == 0) {
                StorageFile f = StorageFile::GetFileFromPathAsync(job.path).get();
                doc = PdfDocument::LoadFromFileAsync(f).get();
                docGen = job.gen;
                std::vector<std::pair<float, float>> sizes;
                for (uint32_t i = 0; i < doc.PageCount(); ++i) {
                    auto s = doc.GetPage(i).Size();
                    sizes.push_back({s.Width, s.Height});
                }
                std::lock_guard<std::mutex> l(m_mx);
                if (job.gen == m_gen) { m_sizes = sizes; m_ready = true; }
            } else if (doc && docGen == job.gen && job.page >= 0 && (uint32_t)job.page < doc.PageCount()) {
                PdfPage page = doc.GetPage((uint32_t)job.page);
                InMemoryRandomAccessStream stream;
                PdfPageRenderOptions opt;
                opt.DestinationWidth((uint32_t)std::max(16, job.width));
                page.RenderToStreamAsync(stream, opt).get();
                BitmapDecoder dec = BitmapDecoder::CreateAsync(stream).get();
                PixelDataProvider px = dec.GetPixelDataAsync(BitmapPixelFormat::Rgba8, BitmapAlphaMode::Straight, BitmapTransform(),
                                                             ExifOrientationMode::IgnoreExifOrientation, ColorManagementMode::DoNotColorManage).get();
                com_array<uint8_t> data = px.DetachPixelData();
                Image img;
                img.page = job.page;
                img.w = (int)dec.PixelWidth();
                img.h = (int)dec.PixelHeight();
                img.rgba.assign(data.begin(), data.end());
                std::lock_guard<std::mutex> l(m_mx);
                if (job.gen == m_gen) m_done.push_back(std::move(img));
            }
        } catch (const winrt::hresult_error& e) {
            if (job.kind == 0) {
                std::lock_guard<std::mutex> l(m_mx);
                m_failed = true;
                m_error = winrt::to_string(e.message());
            }
        } catch (...) {
            if (job.kind == 0) {
                std::lock_guard<std::mutex> l(m_mx);
                m_failed = true;
                m_error = "erreur inconnue";
            }
        }
    }
    doc = nullptr;
    winrt::uninit_apartment();
}

} // namespace brickit
