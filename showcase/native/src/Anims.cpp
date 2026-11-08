// Phantasm Showcase - Anims: see Anims.h.

#include "PCH.h"

#include "Anims.h"

#include "AnimPathCore.h"
#include "Bridge.h"
#include "Settings.h"
#include "Text.h"

namespace Showcase::Anims
{
    namespace
    {
        enum class State
        {
            kIdle,
            kScanning,
            kReady,
        };

        std::mutex               g_mutex;
        State                    g_state = State::kIdle;
        std::vector<std::string> g_paths;    // as found (case kept), sorted by their folded form
        std::vector<std::string> g_folded;   // folded, same order

        std::filesystem::path MeshesFolder()
        {
            wchar_t buf[MAX_PATH]{};
            ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
            return std::filesystem::path(buf).parent_path() / L"Data" / L"meshes";
        }

        void Scan()
        {
            const auto started = std::chrono::steady_clock::now();
            const auto root = MeshesFolder();
            std::vector<std::string> hidden;
            for (const auto& h : Settings::Get().hiddenAnimFolders) hidden.push_back(Text::Fold(AnimPathCore::Normalize(h)));
            std::vector<std::pair<std::string, std::string>> found;   // folded, as found
            std::error_code ec;
            const auto opts = std::filesystem::directory_options::skip_permission_denied;
            for (std::filesystem::recursive_directory_iterator it(root / L"actors", opts, ec), end; !ec && it != end;
                 it.increment(ec)) {
                if (!it->is_regular_file(ec)) continue;
                // The path under meshes, cut from the file's own path: under MO2 the walk yields its real place in a
                // mod folder, which a relative() to Data/meshes turns into "..\..\<mod>\meshes\...".
                auto path = AnimPathCore::UnderMeshes(Settings::Utf8(it->path()));
                if (path.empty()) continue;
                auto folded = Text::Fold(path);
                if (!AnimPathCore::IsAnimation(folded) || AnimPathCore::Hidden(folded, hidden)) continue;
                found.emplace_back(std::move(folded), std::move(path));
            }
            if (ec) SKSE::log::warn("anims: the walk stopped early: {}", ec.message());
            std::sort(found.begin(), found.end());
            std::vector<std::string> paths, folded;
            paths.reserve(found.size());
            folded.reserve(found.size());
            for (auto& [f, p] : found) folded.push_back(std::move(f)), paths.push_back(std::move(p));
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
            SKSE::log::info("anims: {} loose animation(s) under meshes/actors in {} ms", paths.size(), ms.count());
            {
                std::lock_guard lock(g_mutex);
                g_paths = std::move(paths);
                g_folded = std::move(folded);
                g_state = State::kReady;
            }
            Bridge::Push({ { "event", "anims" }, { "state", "ready" } });
        }

        /// Starts the scan once. Under g_mutex.
        void EnsureScan()
        {
            if (g_state != State::kIdle) return;
            g_state = State::kScanning;
            std::thread([] {
                try {
                    Scan();
                } catch (const std::exception& e) {
                    SKSE::log::error("anims: the scan failed: {}", e.what());
                    std::lock_guard lock(g_mutex);
                    g_state = State::kReady;
                }
            }).detach();
        }

        nlohmann::json File(std::size_t i)
        {
            return { { "name", std::string(AnimPathCore::Stem(g_paths[i])) }, { "path", g_paths[i] } };
        }
    }

    nlohmann::json List(std::string_view folder)
    {
        std::lock_guard lock(g_mutex);
        EnsureScan();
        if (g_state != State::kReady) return { { "state", "scanning" } };
        const auto f = Text::Fold(AnimPathCore::Normalize(folder));
        const auto listing = AnimPathCore::List(g_folded, f);
        auto folders = nlohmann::json::array();
        // Folder names come folded (lower case): the folded path is what finds the folder again.
        for (const auto& d : listing.folders) folders.push_back({ { "name", d.name }, { "count", d.count } });
        auto files = nlohmann::json::array();
        for (const auto i : listing.files) files.push_back(File(i));
        return { { "state", "ready" }, { "total", g_paths.size() }, { "folder", f },
                 { "folders", std::move(folders) }, { "files", std::move(files) } };
    }

    void Prewarm()
    {
        std::lock_guard lock(g_mutex);
        EnsureScan();
    }

    nlohmann::json Search(std::string_view text, std::size_t limit)
    {
        std::lock_guard lock(g_mutex);
        EnsureScan();
        if (g_state != State::kReady) return { { "state", "scanning" } };
        auto files = nlohmann::json::array();
        for (const auto i : AnimPathCore::Search(g_folded, Text::Words(text), limit)) files.push_back(File(i));
        return { { "state", "ready" }, { "total", g_paths.size() }, { "files", std::move(files) } };
    }
}
