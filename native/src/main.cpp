// Phantasm - the SKSE entry point: the log, the plugin declaration, the revert hook and the SKSE messages.

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <spdlog/sinks/base_sink.h>

#include "Host.h"
#include "MannequinRenderer.h"
#include "PresentHandoff.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

SKSEPluginInfo(
    .Version = { PLUGIN_VERSION_MAJOR, PLUGIN_VERSION_MINOR, PLUGIN_VERSION_PATCH, 0 },
    .Name = "Phantasm",
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
        const auto path = (dir ? *dir : std::filesystem::path("Data/SKSE/Plugins")) / "Phantasm.log";
        try {
            auto log = std::make_shared<spdlog::logger>("global log", std::make_shared<WideFileSink>(path));
            log->set_level(spdlog::level::info);
            log->flush_on(spdlog::level::trace);   // every line, so a crash keeps it
            spdlog::set_default_logger(std::move(log));
            spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%L] [%t] %v");
        } catch (const std::exception& e) {
            SKSE::stl::report_and_fail(std::format("Phantasm: failed to create its log file: {}", e.what()));
        }
    }

    // Phantasm writes nothing into the save; the record id exists for the revert callback.
    constexpr std::uint32_t kSerializationId = 'PHTM';

    void OnRevert(SKSE::SerializationInterface*) { Phantasm::Host::OnRevert(); }

    // Data/SKSE/Plugins/Phantasm.ini, [LiveViews]: Budget = smooth | balanced | light, BudgetMs = a number (0 = the
    // mode's). The path is from the game's own folder, whatever the working folder a launcher left; a plain file read,
    // so MO2's virtual folder serves it. A missing file keeps the defaults.
    std::filesystem::path SettingsPath()
    {
        std::wstring buf(MAX_PATH, L'\0');
        for (;;) {
            const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
            if (n == 0) return "Data/SKSE/Plugins/Phantasm.ini";
            if (n < buf.size()) {
                buf.resize(n);
                break;
            }
            buf.resize(buf.size() * 2);
        }
        return std::filesystem::path(buf).parent_path() / L"Data" / L"SKSE" / L"Plugins" / L"Phantasm.ini";
    }

    void LoadSettings()
    {
        std::string mode;
        double      ms = 0.0;
        const auto  path = SettingsPath();
        if (std::ifstream in{ path }) {
            std::string line, section;
            bool        sawSection = false;
            bool        first      = true;
            auto        trim = [](std::string s) {
                const auto a = s.find_first_not_of(" \t\r");
                const auto b = s.find_last_not_of(" \t\r");
                return a == std::string::npos ? std::string{} : s.substr(a, b - a + 1);
            };
            auto lower = [](std::string s) {
                for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                return s;
            };
            while (std::getline(in, line)) {
                if (std::exchange(first, false) && line.starts_with("\xEF\xBB\xBF")) line.erase(0, 3);   // a UTF-8 BOM
                line = trim(line);
                if (line.empty() || line[0] == ';' || line[0] == '#') continue;
                // A section header, a comment after it allowed ("[LiveViews] ; ...").
                if (line.front() == '[') {
                    const auto close = line.find(']');
                    if (close != std::string::npos) {
                        section = lower(trim(line.substr(1, close - 1)));
                        sawSection |= section == "liveviews";
                        continue;
                    }
                }
                const auto eq = line.find('=');
                if (section != "liveviews" || eq == std::string::npos) continue;
                const auto key   = lower(trim(line.substr(0, eq)));
                auto       value = trim(line.substr(eq + 1));
                if (const auto c = value.find_first_of(";#"); c != std::string::npos) value = trim(value.substr(0, c));
                if (key == "budget") mode = value;
                else if (key == "budgetms") {
                    try {
                        ms = std::stod(value);
                    } catch (...) {
                        SKSE::log::warn("Phantasm.ini: BudgetMs '{}' is not a number", value);
                    }
                }
            }
            if (!sawSection) SKSE::log::warn("Phantasm.ini has no [LiveViews] section: the default budget");
        } else {
            // u8string: string() throws on a game folder name the ANSI code page cannot spell.
            const auto shown = path.u8string();
            SKSE::log::info("Phantasm.ini not found at {}: the default budget",
                            std::string(reinterpret_cast<const char*>(shown.c_str()), shown.size()));
        }
        Phantasm::MannequinRenderer::GetSingleton()->SetLiveBudget(mode, ms);
    }

    void OnMessage(SKSE::MessagingInterface::Message* m)
    {
        switch (m->type) {
        case SKSE::MessagingInterface::kDataLoaded:
            LoadSettings();
            Phantasm::MannequinRenderer::RegisterCarriers();
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
    SKSE::log::info("Phantasm {} loaded (built {} {}), {}", PLUGIN_VERSION, __DATE__, __TIME__,
                    REL::Module::IsVR() ? "VR" : (REL::Module::IsAE() ? "AE" : "SE"));

    // Before any frame: the pump's thread is never created inside Present.
    Phantasm::PresentHandoff::Start();
    Phantasm::Host::Initialize();

    if (auto* ser = SKSE::GetSerializationInterface()) {
        ser->SetUniqueID(kSerializationId);
        ser->SetRevertCallback(OnRevert);
    }
    if (!SKSE::GetMessagingInterface()->RegisterListener(OnMessage)) {
        SKSE::log::critical("could not register for SKSE messages");
        return false;
    }
    return true;
}
