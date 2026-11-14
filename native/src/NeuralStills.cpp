#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include "NeuralStills.h"

#include "NeuralInProc.h"
#include "NeuralLive.h"
#include "NeuralSettingsCore.h"
#include "NeuralSource.h"

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace Phantasm::NeuralStills
{
    namespace
    {
        namespace NS  = NeuralStillCore;
        namespace NC  = NeuralSettingsCore;
        namespace Src = NeuralSourceCore;

        // Set at kDataLoaded, before the worker runs; the worker's own after that (Reconfigure's work is done there).
        Config      g_config;
        Src::Choice g_choice;   // g_config.folder, read
        // What other threads read of them.
        std::atomic<bool>  g_userOn{ false };   // Enabled, with a source named
        std::atomic<bool>  g_live{ false };
        std::atomic<float> g_liveScale{ 0.5f };
        // The folder or Unsigned in Phantasm.ini is no longer the one NR looked for its library with.
        std::atomic<bool> g_restartNeeded{ false };
        // NR could not start (its own library missing, no copy of NVIDIA's it can run, no GPU for it): stills are
        // answered as drawn.
        std::atomic<bool> g_failed{ false };
        // No library where the settings look for one. Not for good: a changed Folder is looked at again.
        std::atomic<bool> g_sourceMissing{ false };
        // Starts the worker has been asked for and not yet got to (ArmStart, Reconfigure), and one it is in.
        std::atomic<int>  g_startsQueued{ 0 };
        std::atomic<bool> g_inStart{ false };

        std::mutex                                   g_mx;
        std::condition_variable                      g_cv;
        std::deque<std::function<void(bool)>>        g_jobs;
        // Reconfigure's work: run ahead of the jobs, and without starting NR as a job does.
        std::deque<std::function<void()>>            g_control;
        bool                                         g_workerStarted = false;

        // NR's own library, in the folder "Phantasm" beside Phantasm.dll (NeuralInProc loads it into the game).
        constexpr const wchar_t* kOwnLibrary = L"PhantasmNRInProc.dll";

        // What a settings page is told (GetStatus), written by the worker.
        std::mutex  g_statusMx;
        std::string g_statusLibrary, g_statusFailure, g_statusFollowed;

        void NoteFailure(std::string why)
        {
            std::lock_guard lock(g_statusMx);
            g_statusFailure = std::move(why);
        }

        // NR as started, worker thread only (g_liveApi is published once it has started).
        bool                     g_ready   = false;
        bool                     g_holding = false;   // NR keeps a feature and images for the last still size
        LiveApi                  g_liveApi;
        std::atomic<const LiveApi*> g_livePublished{ nullptr };

        // The library files to try, found once (worker thread).
        bool                               g_filesFound = false;
        std::vector<std::filesystem::path> g_files;

        // The settings in force: Phantasm.ini's, with the followed mod's laid over them (NC::Merge). Written by the
        // worker, read by the render thread for every live frame.
        std::mutex   g_settingsMx;
        NS::Settings g_effective;
        // The followed mod's own NR switch: off there is off here. True with no mod followed.
        std::atomic<bool> g_modOn{ true };
        // The mod followed and its settings files, worker thread only.
        NC::Mod                            g_followMod = NC::Mod::None;
        std::vector<std::filesystem::path> g_followFiles;
        std::uint64_t                      g_followStamp = 0;

        // NR's memory goes back to the driver once no still has come for this long.
        constexpr auto kIdleTrim = std::chrono::seconds(30);
        // How often the worker looks at the followed mod's settings files when it has nothing to do.
        constexpr auto kFollowTick = std::chrono::seconds(1);

        // Set at kDataLoaded: the worker starts NR on its next idle tick, unless a picture starts it sooner.
        std::atomic<bool> g_startDue{ false };

        void NrLog(const char* line) { SKSE::log::info("Phantasm NR: {}", line ? line : ""); }

        std::filesystem::path OwnFolder()
        {
            HMODULE self = nullptr;
            ::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                 reinterpret_cast<LPCWSTR>(&OwnFolder), &self);
            return self ? NeuralSource::ModuleFile(self).parent_path() : std::filesystem::path{};
        }

        // The adapter the game renders on: NR's Vulkan device must be on it for a live view's textures to share.
        // `name`, when asked for, takes its description.
        LUID GameAdapterLuid(std::string* name = nullptr)
        {
            LUID  luid{};
            auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
            auto* device   = renderer ? reinterpret_cast<ID3D11Device*>(renderer->GetRuntimeData().forwarder) : nullptr;
            Microsoft::WRL::ComPtr<IDXGIDevice>  dxgi;
            Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
            DXGI_ADAPTER_DESC                    desc{};
            if (device && SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dxgi))) && SUCCEEDED(dxgi->GetAdapter(&adapter)) &&
                SUCCEEDED(adapter->GetDesc(&desc))) {
                luid = desc.AdapterLuid;
                if (name) *name = NeuralSource::Narrow(desc.Description);
            }
            return luid;
        }

        std::string ReadText(const std::filesystem::path& file)
        {
            std::ifstream in(file, std::ios::binary);
            if (!in) return {};
            return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }

        // Changes when any of the followed files is written, appears or goes.
        std::uint64_t FollowStamp()
        {
            std::uint64_t stamp = 1469598103934665603ull;
            for (const auto& f : g_followFiles) {
                std::error_code ec;
                const auto      t = std::filesystem::last_write_time(f, ec);
                const auto      n = ec ? 0 : static_cast<std::uint64_t>(t.time_since_epoch().count());
                const auto      s = static_cast<std::uint64_t>(std::filesystem::file_size(f, ec));
                stamp             = (stamp ^ n) * 1099511628211ull;
                stamp             = (stamp ^ (ec ? 0 : s)) * 1099511628211ull;
            }
            return stamp;
        }

        // Reads the followed mod's settings when its files changed (or always, on `first`) and puts them in force.
        // True when it read them. Worker thread.
        bool RefreshFollowed(bool first)
        {
            if (g_followMod == NC::Mod::None) return false;
            const std::uint64_t stamp = FollowStamp();
            if (!first && stamp == g_followStamp) return false;
            g_followStamp = stamp;
            NC::Followed f;
            switch (g_followMod) {
            case NC::Mod::SkyrimUpscaler: f = NC::ParseSkyrimUpscaler(ReadText(g_followFiles[0])); break;
            case NC::Mod::Theos: f = NC::ParseTheos(ReadText(g_followFiles[0]), ReadText(g_followFiles[1])); break;
            case NC::Mod::OpenShaders: f = NC::ParseOpenShaders(ReadText(g_followFiles[0])); break;
            default: break;
            }
            const NS::Settings s  = NC::Merge(g_config.settings, f);
            const bool         on = f.enabled.value_or(true);
            {
                std::lock_guard lock(g_settingsMx);
                g_effective = s;
            }
            g_modOn = on;
            if (f.Any()) {
                SKSE::log::info("Phantasm NR: following {}'s settings: NR {}, style {}, intensity {:.2f}, tone {:.2f}, "
                                "structure {:.2f}, skin {:.2f}",
                                NC::ModName(g_followMod), on ? "on" : "off", s.model, s.intensity, s.tone, s.structure, s.skin);
            } else {
                SKSE::log::info("Phantasm NR: {}'s settings were not found: Phantasm.ini's are used", NC::ModName(g_followMod));
            }
            return true;
        }

        // Follows the mod `library` (as the game sees it) belongs to, when [NeuralRendering] Follow allows.
        void Follow(const std::filesystem::path& library)
        {
            g_followMod = g_config.follow ? NC::ModOf(NeuralSource::Narrow(library.wstring())) : NC::Mod::None;
            {
                std::lock_guard lock(g_statusMx);
                g_statusFollowed = g_followMod == NC::Mod::None ? std::string{} : std::string(NC::ModName(g_followMod));
            }
            g_followFiles.clear();
            const auto plugins = NeuralSource::ModuleFile(nullptr).parent_path() / L"Data" / L"SKSE" / L"Plugins";
            switch (g_followMod) {
            case NC::Mod::SkyrimUpscaler: g_followFiles = { plugins / L"SkyrimUpscaler.ini" }; break;
            case NC::Mod::Theos:
                g_followFiles = { plugins / L"TheosRenderPipeline.ini", plugins / L"TheosRenderPipeline.User.ini" };
                break;
            case NC::Mod::OpenShaders: g_followFiles = { plugins / L"CommunityShaders" / L"SettingsUser.json" }; break;
            default: break;
            }
            if (g_followMod == NC::Mod::None) {
                // Nobody's switch or settings stand over Phantasm.ini's any more.
                {
                    std::lock_guard lock(g_settingsMx);
                    g_effective = g_config.settings;
                }
                g_modOn = true;
            } else {
                RefreshFollowed(true);
            }
        }

        enum class Started
        {
            Yes,
            No,        // for good: NR stays off for the session
            Missing,   // no library where the settings look: looked for again when they change
            Waiting,   // the followed mod has NR turned off: tried again when it is turned on
        };

        // Starts NR inside the game (NeuralInProc). Worker thread.
        Started StartNR()
        {
            if (g_ready) return Started::Yes;

            // The library files to try, the first that starts wins.
            if (!g_filesFound) {
                g_files.clear();
                if (g_choice.mode == Src::Mode::Folder) {
                    auto            file = std::filesystem::path(NeuralSource::Widen(g_choice.folder)) / NeuralSource::Widen(g_choice.file);
                    std::error_code ec;
                    if (std::filesystem::exists(file, ec)) g_files.push_back(std::move(file));
                } else {
                    const auto game  = NeuralSource::ModuleFile(nullptr).parent_path();
                    auto       found = NeuralSource::Discover(game);
                    // A mod that ships several libraries says which it runs: its own setting, and its rules by GPU.
                    Src::Hints  hints;
                    std::string gpu;
                    GameAdapterLuid(&gpu);
                    hints.series = Src::GpuSeries(gpu);
                    for (const auto& f : found) {
                        if (NC::ModOf(f.file) != NC::Mod::Theos) continue;
                        const auto        plugins = game / L"Data" / L"SKSE" / L"Plugins";
                        const std::string name    = NC::TheosRuntime(ReadText(plugins / L"TheosRenderPipeline.ini"),
                                                                     ReadText(plugins / L"TheosRenderPipeline.User.ini"));
                        if (!name.empty()) {
                            const auto folder = std::filesystem::path(NeuralSource::Widen(f.file)).parent_path();
                            hints.chosen      = NeuralSource::Narrow((folder / NeuralSource::Widen(name)).wstring());
                        }
                        break;
                    }
                    Src::Rank(found, hints);
                    for (const auto& f : found) {
                        const bool suits = Src::Suits(Src::FileName(f.file), hints.series);
                        SKSE::log::info("Phantasm NR: found '{}'{}{}", f.file, f.place == Src::Place::Loaded ? " (a mod has it loaded)" : "",
                                        suits ? "" : " (not for this GPU as it is on disk: left out)");
                        if (suits) g_files.push_back(std::filesystem::path(NeuralSource::Widen(f.file)));
                    }
                    if (g_files.size() > Src::kMaxTries) g_files.resize(Src::kMaxTries);
                }
                if (g_files.empty()) {
                    // Not settled: a Folder that names a library is taken as soon as it is set.
                    if (g_choice.mode == Src::Mode::Folder) {
                        SKSE::log::warn("Phantasm NR: no {} in '{}': NR waits for [NeuralRendering] Folder to name one",
                                        g_choice.file, g_choice.folder);
                        NoteFailure(std::format("There is no {} in {}.", g_choice.file, g_choice.folder));
                    } else {
                        SKSE::log::warn("Phantasm NR: no {} in the game's folders (a neural rendering mod installs one): NR "
                                        "waits for [NeuralRendering] Folder to name a folder that holds one",
                                        Src::kLibrary);
                        NoteFailure(std::format("No {} was found: a neural rendering mod installs one.", Src::kLibrary));
                    }
                    g_sourceMissing = true;
                    return Started::Missing;
                }
                g_filesFound = true;
                // The settings are those of the mod the first copy belongs to.
                Follow(g_files.front());
            }
            if (!g_modOn.load()) return Started::Waiting;

            const auto  own     = OwnFolder() / L"Phantasm" / kOwnLibrary;
            const auto  realOwn = NeuralSource::RealPath(own);
            std::string notLoaded;
            if (realOwn.empty() || !NeuralInProc::Load(realOwn, &NrLog, notLoaded)) {
                SKSE::log::warn("Phantasm NR: {} {}: NR stays off", reinterpret_cast<const char*>(own.u8string().c_str()),
                                realOwn.empty() ? "is not installed" : notLoaded.c_str());
                NoteFailure(realOwn.empty() ? "PhantasmNRInProc.dll is not installed (it belongs in SKSE/Plugins/Phantasm)."
                                            : "PhantasmNRInProc.dll could not be loaded: " + notLoaded);
                return Started::No;
            }

            std::filesystem::path data;
            if (const auto logs = SKSE::log::log_directory()) data = *logs / "Phantasm";
            std::error_code ec;
            std::filesystem::create_directories(data, ec);
            const LUID          luid  = GameAdapterLuid();
            const std::uint32_t flags = g_config.unsignedCopies ? std::uint32_t(PhantasmNR_FlagUnsigned) : 0u;
            for (const auto& file : g_files) {
                const std::string shown = NeuralSource::Narrow(file.wstring());
                // NR runs from a private copy, a module apart from the one a mod has loaded in the game.
                std::string                 noCopy, note;
                const std::filesystem::path real = NeuralSource::PrivateCopy(file, data, noCopy, note);
                if (real.empty()) {
                    SKSE::log::warn("Phantasm NR: not started from '{}': {}", shown, noCopy);
                    continue;
                }
                if (!note.empty()) SKSE::log::info("Phantasm NR: {}", note);
                char          why[512]{};
                std::uint32_t how = 0;
                const auto    t0  = std::chrono::steady_clock::now();
                if (!NeuralInProc::Start(real.c_str(), data.c_str(), luid.LowPart, luid.HighPart, flags, &how, why, sizeof(why))) {
                    SKSE::log::warn("Phantasm NR: not started from '{}': {}", shown, why);
                    continue;
                }
                const PhantasmNR_Settings now = CurrentSettings();
                SKSE::log::info("Phantasm NR: started in the game's own process from '{}' ({}) in {} ms (settings from {}: style {}, "
                                "intensity {:.2f}, tone {:.2f}, structure {:.2f}, skin {:.2f}; {} passes; live views {})",
                                shown,
                                how == PhantasmNR_HowDirect         ? "an unsigned copy, loaded directly"
                                : how == PhantasmNR_HowDirectSigned ? "NVIDIA-signed under a name of the mod's own, loaded directly"
                                                                    : "NVIDIA-signed, through NVIDIA's loader",
                                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count(),
                                NC::ModName(g_followMod), now.model, now.intensity, now.tone, now.structure, now.skin, now.passes,
                                g_config.live ? "on" : "off");
                NeuralSource::PruneCopies(data, real.parent_path());
                {
                    std::lock_guard lock(g_statusMx);
                    g_statusLibrary = shown;
                }
                g_liveApi = { &NeuralInProc::LiveAttach, &NeuralInProc::LiveEvaluate, &NeuralInProc::LiveDetach };
                g_ready   = true;
                g_livePublished.store(&g_liveApi, std::memory_order_release);
                return Started::Yes;
            }
            NeuralInProc::Stop();
            NeuralSource::PruneCopies(data, {});
            SKSE::log::warn("Phantasm NR: no copy of {} started: NR stays off", Src::kLibrary);
            NoteFailure(std::format("No copy of {} could be started (Phantasm.log says why).", Src::kLibrary));
            return Started::No;
        }

        // StartNR, with a start that failed for good recorded. True when NR is running.
        bool EnsureStarted()
        {
            if (g_failed.load() || g_sourceMissing.load()) return false;
            if (g_ready) return true;
            g_inStart       = true;
            const Started s = StartNR();
            g_inStart       = false;
            if (s == Started::No) g_failed = true;
            return s == Started::Yes;
        }

        void RunStill(MannequinRenderer::PortraitShot& s)
        {
            std::vector<std::uint8_t> picture, answer;
            std::vector<float>        depth;
            NS::ToInput(s.rgba, s.width, s.height, picture, depth);
            answer.resize(picture.size());
            const PhantasmNR_Settings ps = CurrentSettings();
            char                      why[512]{};
            const auto                t0 = std::chrono::steady_clock::now();
            if (NeuralInProc::Process(picture.data(), depth.data(), s.width, s.height, &ps, answer.data(), why, sizeof(why))) {
                NS::FromOutput(s.rgba, s.width, s.height, answer);
                g_holding = true;
                SKSE::log::info("Phantasm NR: still {}x{} in {} ms", s.width, s.height,
                                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
            } else {
                SKSE::log::warn("Phantasm NR: still {}x{} left as drawn: {}", s.width, s.height, why);
            }
        }

        void Worker();

        // Has the worker start NR on its next idle tick.
        void ArmStart()
        {
            if (!g_startDue.exchange(true)) ++g_startsQueued;
            std::lock_guard lock(g_mx);
            if (!g_workerStarted) {
                g_workerStarted = true;
                std::thread(Worker).detach();
            }
        }

        void ClampConfig(Config& config)
        {
            config.settings.model     = NS::ClampModel(config.settings.model);
            config.settings.intensity = NS::ClampStrength(config.settings.intensity);
            config.settings.tone      = NS::ClampStrength(config.settings.tone);
            config.settings.structure = NS::ClampStrength(config.settings.structure);
            config.settings.skin      = NS::ClampStrength(config.settings.skin);
            config.settings.passes    = NS::ClampPasses(config.settings.passes);
            config.liveScale          = std::isfinite(config.liveScale) ? std::clamp(config.liveScale, 0.25f, 1.0f) : 0.5f;
        }

        // Reconfigure's work. Worker thread.
        void Reapply(Config c)
        {
            // The switch goes by the Folder asked for: one that names no source is off, whatever was found before.
            const auto asked       = Src::ParseFolder(c.folder);
            const bool sourceNamed = asked.mode != Src::Mode::Off;
            const bool wanted      = c.enabled && sourceNamed;
            // Where the library comes from is settled once NR has found it. The same source spelled another way
            // ("Auto", the other slash, the library file in the folder) is no change.
            if (g_filesFound) {
                const auto spelled = [](std::string_view path) {
                    std::string s = Src::Lower(path);
                    std::replace(s.begin(), s.end(), '/', '\\');
                    return s;
                };
                const bool sameSource = asked.mode == g_choice.mode && spelled(asked.folder) == spelled(g_choice.folder) &&
                                        spelled(asked.file) == spelled(g_choice.file);
                g_restartNeeded  = sourceNamed && (!sameSource || c.unsignedCopies != g_config.unsignedCopies);
                c.folder         = g_config.folder;
                c.unsignedCopies = g_config.unsignedCopies;
            }
            g_config = std::move(c);
            g_choice = Src::ParseFolder(g_config.folder);
            g_userOn = wanted;
            // A library that was missing is looked for again with the changed settings.
            if (g_sourceMissing.exchange(false)) NoteFailure({});
            // The followed mod's settings laid over the changed ones, or let go of with Follow off.
            if (g_filesFound && !g_files.empty()) {
                Follow(g_files.front());
            } else {
                std::lock_guard lock(g_settingsMx);
                g_effective = g_config.settings;
            }
            SKSE::log::info("Phantasm NR: settings changed: {}, live views {} at {:.2f} of their size, Follow {}",
                            g_userOn.load() ? "on" : "off", g_config.live ? "on" : "off", g_config.liveScale,
                            g_config.follow ? "on" : "off");
            if (Enabled()) EnsureStarted();
            --g_startsQueued;
        }

        void Worker()
        {
            auto lastJob = std::chrono::steady_clock::now();
            for (;;) {
                std::function<void()>     control;
                std::function<void(bool)> job;
                {
                    std::unique_lock lock(g_mx);
                    if (!g_cv.wait_for(lock, kFollowTick, [] { return !g_jobs.empty() || !g_control.empty(); })) {
                        lock.unlock();
                        // Nothing to do: a setting changed in the followed mod's menu is taken up, a mod that turned
                        // NR back on gets NR started, and what NR holds for nobody goes back to the driver.
                        if (RefreshFollowed(false) && Enabled() && g_config.prestart) EnsureStarted();
                        if (g_startDue.exchange(false)) {
                            if (g_userOn.load()) EnsureStarted();
                            --g_startsQueued;
                        }
                        if (g_holding && std::chrono::steady_clock::now() - lastJob >= kIdleTrim) {
                            NeuralInProc::Trim();
                            g_holding = false;
                        }
                        NeuralLive::OnWorkerIdle();
                        continue;
                    }
                    if (!g_control.empty()) {
                        control = std::move(g_control.front());
                        g_control.pop_front();
                    } else {
                        job = std::move(g_jobs.front());
                        g_jobs.pop_front();
                    }
                }
                if (control) {
                    try {
                        control();
                    } catch (...) {
                        SKSE::log::error("Phantasm NR: a settings change threw");
                    }
                    continue;
                }
                // A job posted before the switch went off does not start NR after it.
                const bool started = g_ready || (g_userOn.load() && EnsureStarted());
                try {
                    job(started);
                } catch (...) {
                    SKSE::log::error("Phantasm NR: a job threw");
                }
                lastJob = std::chrono::steady_clock::now();
            }
        }
    }

    void Configure(Config config)
    {
        ClampConfig(config);
        g_config    = std::move(config);
        g_effective = g_config.settings;
        g_choice    = Src::ParseFolder(g_config.folder);
        g_userOn    = g_config.enabled && g_choice.mode != Src::Mode::Off;
        g_live      = g_config.live;
        g_liveScale = g_config.liveScale;
        if (!g_userOn.load()) {
            if (g_choice.mode != Src::Mode::Off) SKSE::log::info("Phantasm NR: off ([NeuralRendering] Enabled = 0)");
        } else if (g_choice.mode == Src::Mode::Folder) {
            SKSE::log::info("Phantasm NR: actor stills{} go through DLSS NR from '{}', run in the game's own process",
                            g_config.live ? " and live views" : "", g_choice.folder);
        } else if (g_choice.mode == Src::Mode::Auto) {
            SKSE::log::info("Phantasm NR: actor stills{} go through DLSS NR from the copy a neural rendering mod installed, run in "
                            "the game's own process",
                            g_config.live ? " and live views" : "");
        }
        // NR starts now, while the game is still loading its data: its libraries start inside the loader lock, and
        // here a wait for that lock is loading time, not a frozen game.
        if (Enabled()) ArmStart();
    }

    void Reconfigure(Config config)
    {
        ClampConfig(config);
        g_live      = config.live;
        g_liveScale = config.liveScale;
        ++g_startsQueued;
        std::lock_guard lock(g_mx);
        g_control.push_back([config = std::move(config)]() mutable { Reapply(std::move(config)); });
        if (!g_workerStarted) {
            g_workerStarted = true;
            std::thread(Worker).detach();
        }
        g_cv.notify_one();
    }

    bool Enabled() { return g_userOn && !g_failed && !g_sourceMissing && g_modOn; }

    bool StartFailed() { return g_failed; }

    bool  LiveOn() { return g_live; }
    float LiveScale() { return g_liveScale; }

    Status GetStatus()
    {
        Status s;
        s.wanted        = g_userOn;
        s.modOn         = g_modOn;
        s.running       = g_livePublished.load(std::memory_order_acquire) != nullptr;
        s.starting      = g_inStart || g_startsQueued.load() > 0;
        s.failed        = g_failed;
        s.sourceMissing = g_sourceMissing;
        s.restartNeeded = g_restartNeeded;
        // Asked for on each call: it is a file's presence, and a page asks about twice a second.
        std::error_code ec;
        s.ownLibrary = std::filesystem::exists(OwnFolder() / L"Phantasm" / kOwnLibrary, ec);
        std::lock_guard lock(g_statusMx);
        s.failure  = g_statusFailure;
        s.library  = g_statusLibrary;
        s.followed = g_statusFollowed;
        return s;
    }

    void Post(std::function<void(bool started)> fn)
    {
        std::lock_guard lock(g_mx);
        g_jobs.push_back(std::move(fn));
        if (!g_workerStarted) {
            g_workerStarted = true;
            std::thread(Worker).detach();
        }
        g_cv.notify_one();
    }

    void Submit(MannequinRenderer::PortraitShot shot, std::function<void(MannequinRenderer::PortraitShot)> done)
    {
        Post([shot = std::move(shot), done = std::move(done)](bool started) mutable {
            if (started) RunStill(shot);
            try {
                done(std::move(shot));
            } catch (...) {
                SKSE::log::error("Phantasm NR: a still's answer threw");
            }
        });
    }

    const LiveApi* Live() { return g_livePublished.load(std::memory_order_acquire); }

    PhantasmNR_Settings CurrentSettings()
    {
        std::lock_guard    lock(g_settingsMx);
        const NS::Settings s = g_effective;
        return PhantasmNR_Settings{ sizeof(PhantasmNR_Settings), s.model, s.intensity, s.tone, s.structure, s.skin, s.passes };
    }
}
