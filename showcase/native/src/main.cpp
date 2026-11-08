// Phantasm Showcase - the SKSE entry point: the log, the plugin declaration and the SKSE messages.

#include "PCH.h"

#include "Runtime.h"

#include <cstdio>
#include <stdexcept>

SKSEPluginInfo(
    .Version = { PLUGIN_VERSION_MAJOR, PLUGIN_VERSION_MINOR, PLUGIN_VERSION_PATCH, 0 },
    .Name = "PhantasmShowcase",
    .Author = "Severause",
    .SupportEmail = "",
    .StructCompatibility = SKSE::StructCompatibility::Independent,
    .RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary,
    .MinimumSKSEVersion = { 2, 0, 0, 0 }
)

namespace
{
    // A file sink opened by its wide path: spdlog's own file sinks open a narrow name through the ANSI code page,
    // which cannot spell every profile folder.
    class WideFileSink final : public spdlog::sinks::base_sink<std::mutex>
    {
    public:
        explicit WideFileSink(const std::filesystem::path& path) : m_file(_wfsopen(path.c_str(), L"wb", _SH_DENYNO))
        {
            if (!m_file) throw std::runtime_error("cannot open the log file");
        }
        ~WideFileSink() override { std::fclose(m_file); }

    protected:
        void sink_it_(const spdlog::details::log_msg& msg) override
        {
            spdlog::memory_buf_t buf;
            formatter_->format(msg, buf);
            std::fwrite(buf.data(), 1, buf.size(), m_file);
        }
        void flush_() override { std::fflush(m_file); }

    private:
        std::FILE* m_file;
    };

    void InitializeLogging()
    {
        auto dir = SKSE::log::log_directory();
        const auto path = (dir ? *dir : std::filesystem::path("Data/SKSE/Plugins")) / "PhantasmShowcase.log";
        try {
            auto log = std::make_shared<spdlog::logger>("global log", std::make_shared<WideFileSink>(path));
            log->set_level(spdlog::level::info);
            log->flush_on(spdlog::level::trace);   // every line, so a crash keeps it
            spdlog::set_default_logger(std::move(log));
            spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%L] [%t] %v");
        } catch (const std::exception& e) {
            SKSE::stl::report_and_fail(std::format("Phantasm Showcase: failed to create its log file: {}", e.what()));
        }
    }

    void OnMessage(SKSE::MessagingInterface::Message* m)
    {
        switch (m->type) {
        case SKSE::MessagingInterface::kDataLoaded:
            Showcase::Runtime::OnDataLoaded();
            break;
        case SKSE::MessagingInterface::kPostLoadGame:
        case SKSE::MessagingInterface::kNewGame:
            Showcase::Runtime::OnPostLoad();
            break;
        case SKSE::MessagingInterface::kPreLoadGame:
            Showcase::Runtime::OnPreLoad();
            break;
        default:
            break;
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    InitializeLogging();
    // false: CommonLib's own log setup would reopen (truncate) this log and replace the logger above.
    SKSE::Init(a_skse, false);
    SKSE::log::info("Phantasm Showcase {} loaded (built {} {}), {}", PLUGIN_VERSION, __DATE__, __TIME__,
                    REL::Module::IsVR() ? "VR" : (REL::Module::IsAE() ? "AE" : "SE"));
    if (!SKSE::GetMessagingInterface()->RegisterListener(OnMessage)) {
        SKSE::log::critical("could not register for SKSE messages");
        return false;
    }
    return true;
}
