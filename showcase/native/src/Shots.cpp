// Phantasm Showcase - Shots: see Shots.h.

#include "PCH.h"

#include "Shots.h"

#include "ImageCore.h"
#include "Settings.h"

#include <condition_variable>
#include <wincodec.h>

namespace Showcase::Shots
{
    namespace
    {
        std::mutex                         g_mutex;
        std::condition_variable            g_wake;
        std::deque<std::function<void()>>  g_jobs;
        bool                               g_started = false;

        template <class T>
        struct Com
        {
            T* p = nullptr;
            ~Com()
            {
                if (p) p->Release();
            }
            T** operator&() { return &p; }
            T*  operator->() const { return p; }
        };

        void Worker()
        {
            // WIC needs COM on the thread that uses it; the worker lives as long as the process.
            const HRESULT co = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (FAILED(co)) SKSE::log::error("shots: COM did not start ({:08X})", static_cast<std::uint32_t>(co));
            for (;;) {
                std::function<void()> job;
                {
                    std::unique_lock lock(g_mutex);
                    g_wake.wait(lock, [] { return !g_jobs.empty(); });
                    job = std::move(g_jobs.front());
                    g_jobs.pop_front();
                }
                try {
                    job();
                } catch (const std::exception& e) {
                    SKSE::log::error("shots: a job threw: {}", e.what());
                }
            }
        }

        void Post(std::function<void()> job)
        {
            std::lock_guard lock(g_mutex);
            if (!g_started) {
                std::thread(Worker).detach();
                g_started = true;
            }
            g_jobs.push_back(std::move(job));
            g_wake.notify_one();
        }

        /// The pixels as PNG bytes (BGRA through WIC's PNG encoder), empty on a failure. Worker thread.
        std::vector<std::uint8_t> EncodePng(std::vector<std::uint8_t>& rgba, std::uint32_t w, std::uint32_t h,
                                            bool premultiplied)
        {
            const std::size_t pixels = std::size_t(w) * h;
            if (rgba.size() < pixels * 4) return {};
            if (premultiplied) ImageCore::UnpremultiplySrgb(rgba.data(), pixels);
            ImageCore::SwapRedBlue(rgba.data(), pixels);

            Com<IWICImagingFactory> factory;
            if (FAILED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
                return {};
            Com<IStream> stream;
            if (FAILED(::CreateStreamOnHGlobal(nullptr, TRUE, &stream))) return {};
            Com<IWICBitmapEncoder> encoder;
            if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder))) return {};
            if (FAILED(encoder->Initialize(stream.p, WICBitmapEncoderNoCache))) return {};
            Com<IWICBitmapFrameEncode> frame;
            if (FAILED(encoder->CreateNewFrame(&frame, nullptr))) return {};
            if (FAILED(frame->Initialize(nullptr))) return {};
            if (FAILED(frame->SetSize(w, h))) return {};
            WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
            if (FAILED(frame->SetPixelFormat(&format)) || format != GUID_WICPixelFormat32bppBGRA) return {};
            if (FAILED(frame->WritePixels(h, w * 4, static_cast<UINT>(pixels * 4), rgba.data()))) return {};
            if (FAILED(frame->Commit()) || FAILED(encoder->Commit())) return {};

            STATSTG stat{};
            if (FAILED(stream->Stat(&stat, STATFLAG_NONAME))) return {};
            std::vector<std::uint8_t> out(static_cast<std::size_t>(stat.cbSize.QuadPart));
            LARGE_INTEGER zero{};
            if (FAILED(stream->Seek(zero, STREAM_SEEK_SET, nullptr))) return {};
            ULONG read = 0;
            if (FAILED(stream->Read(out.data(), static_cast<ULONG>(out.size()), &read)) || read != out.size()) return {};
            return out;
        }

        /// A file name part: letters, digits, '-' and '_' kept, everything else '_'.
        std::string Clean(std::string_view s)
        {
            std::string out;
            for (char c : s) {
                const bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
                out.push_back(keep ? c : '_');
                if (out.size() >= 48) break;
            }
            return out.empty() ? "shot" : out;
        }
    }

    std::filesystem::path Folder() { return Settings::Folder() / "Shots"; }

    void Save(std::vector<std::uint8_t> rgba, std::uint32_t width, std::uint32_t height, bool premultiplied,
              std::string stem, std::function<void(bool, const std::string&)> done)
    {
        Post([rgba = std::move(rgba), width, height, premultiplied, stem = std::move(stem), done = std::move(done)]() mutable {
            const auto png = EncodePng(rgba, width, height, premultiplied);
            if (png.empty()) {
                done(false, "encode");
                return;
            }
            std::error_code ec;
            std::filesystem::create_directories(Folder(), ec);
            SYSTEMTIME t{};
            ::GetLocalTime(&t);
            const auto name = std::format("{}_{:04}{:02}{:02}-{:02}{:02}{:02}-{:03}.png", Clean(stem), t.wYear, t.wMonth,
                                          t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
            const auto path = Folder() / name;
            std::FILE* f = _wfopen(path.c_str(), L"wb");
            if (!f) {
                done(false, "write");
                return;
            }
            const bool ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
            std::fclose(f);
            SKSE::log::info("shots: {} {} ({}x{}, {} bytes)", ok ? "saved" : "could not write", Settings::Utf8(path),
                            width, height, png.size());
            done(ok, ok ? Settings::Utf8(path) : std::string("write"));
        });
    }

    void DataUrl(std::vector<std::uint8_t> rgba, std::uint32_t width, std::uint32_t height, bool premultiplied,
                 std::function<void(bool, std::string)> done)
    {
        Post([rgba = std::move(rgba), width, height, premultiplied, done = std::move(done)]() mutable {
            const auto png = EncodePng(rgba, width, height, premultiplied);
            if (png.empty()) {
                done(false, {});
                return;
            }
            done(true, "data:image/png;base64," + ImageCore::Base64(png.data(), png.size()));
        });
    }
}
