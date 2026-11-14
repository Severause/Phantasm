#pragma once

// Phantasm - NeuralSourceCore: which NR library (nvngx_dlssnr.dll, or a mod's own name for one) NR runs from,
// engine-free (native/build.ps1 -Tests). NeuralStills.cpp reads [NeuralRendering] Folder through it and orders the
// copies it finds in the game's folders.
//
// Author: Severause

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Phantasm::NeuralSourceCore
{
    inline constexpr std::string_view kLibrary     = "nvngx_dlssnr.dll";
    inline constexpr std::string_view kLibraryStem = "nvngx_dlssnr";

    /// How many found copies are tried before NR gives up for the session: each failed start costs seconds.
    inline constexpr std::size_t kMaxTries = 4;

    inline char LowerAscii(char c) noexcept { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

    inline std::string Lower(std::string_view text)
    {
        std::string out(text);
        std::transform(out.begin(), out.end(), out.begin(), LowerAscii);
        return out;
    }

    inline std::string_view Trim(std::string_view text) noexcept
    {
        constexpr std::string_view kSpace = " \t\r\n";
        const auto                 first  = text.find_first_not_of(kSpace);
        if (first == std::string_view::npos) return {};
        return text.substr(first, text.find_last_not_of(kSpace) - first + 1);
    }

    /// The file-name part of a path, either slash.
    inline std::string_view FileName(std::string_view path) noexcept
    {
        const auto slash = path.find_last_of("\\/");
        return slash == std::string_view::npos ? path : path.substr(slash + 1);
    }

    /// True for a name an NR library goes by: nvngx_dlssnr.dll, the one NVIDIA's loader knows, or a mod's own name
    /// for a copy (nvngx_dlssnr*.dll, as Theo's Render Pipeline ships its builds).
    inline bool IsLibraryName(std::string_view name)
    {
        const std::string n = Lower(name);
        return n.size() >= kLibrary.size() && n.starts_with(kLibraryStem) && n.ends_with(".dll");
    }

    inline bool IsStandardName(std::string_view name) { return Lower(name) == kLibrary; }

    /// The version in a name of the form nvngx_dlssnr_<numbers and dots>.dll ("nvngx_dlssnr_310.9.16.dll" is
    /// 310, 9, 16); empty for any other name.
    inline std::vector<int> NameVersion(std::string_view name)
    {
        const std::string n = Lower(name);
        if (!IsLibraryName(n) || n.size() <= kLibrary.size() || n[kLibraryStem.size()] != '_') return {};
        std::string_view body = std::string_view(n).substr(kLibraryStem.size() + 1);
        body.remove_suffix(4);
        std::vector<int> version;
        int              part = -1;
        for (const char c : body) {
            if (c >= '0' && c <= '9') {
                part = (part < 0 ? 0 : part) * 10 + (c - '0');
                if (part > 1'000'000) return {};
            } else if (c == '.' && part >= 0) {
                version.push_back(part);
                part = -1;
            } else {
                return {};
            }
        }
        if (part < 0) return {};
        version.push_back(part);
        return version;
    }

    /// The GeForce RTX generation an adapter's description names ("NVIDIA GeForce RTX 5080" is 50, "... RTX 3060 Ti"
    /// is 30); 0 when it names none (a workstation card, another maker's).
    inline int GpuSeries(std::string_view description)
    {
        constexpr std::string_view kBrand = "geforce rtx ";
        const std::string          d      = Lower(description);
        const auto                 at     = d.find(kBrand);
        if (at == std::string::npos) return 0;
        int model = 0, digits = 0;
        for (std::size_t i = at + kBrand.size(); i < d.size() && d[i] >= '0' && d[i] <= '9' && digits < 6; ++i, ++digits) {
            model = model * 10 + (d[i] - '0');
        }
        return digits == 4 ? model / 100 : 0;
    }

    /// Whether a library under this name runs as it is on disk on a GeForce RTX `series` card (0, unknown: every name
    /// is tried). These are Theo's Render Pipeline's rules for the files it ships: its INT8 build is not for RTX 40,
    /// and an NVIDIA build under its version number runs as it is on RTX 50 and later only (on an older card Theo's
    /// patches it in memory from a kernel pack, which Phantasm does not do).
    inline bool Suits(std::string_view name, int series)
    {
        if (series == 0) return true;
        const std::string n = Lower(name);
        if (n.find("int8") != std::string::npos) return series != 40;
        if (!NameVersion(n).empty()) return series >= 50;
        return true;
    }

    enum class Mode
    {
        Off,
        Auto,     // the copy a neural rendering mod installed, found in the game's folders
        Folder,   // the folder the player named
    };

    struct Choice
    {
        Mode        mode = Mode::Off;
        std::string folder;   // Mode::Folder only, without a trailing slash
        std::string file;     // Mode::Folder only: the library's file name in it
    };

    /// [NeuralRendering] Folder as typed. Empty, "off", "none" and "0" keep NR off; "auto" looks for the player's
    /// copy; anything else is a folder, with quotes dropped, holding nvngx_dlssnr.dll, or a path to a library file
    /// itself, under any name a library goes by (IsLibraryName).
    inline Choice ParseFolder(std::string_view value)
    {
        value = Trim(value);
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = Trim(value.substr(1, value.size() - 2));
        const std::string lower = Lower(value);
        if (lower.empty() || lower == "off" || lower == "none" || lower == "0") return {};
        if (lower == "auto") return { Mode::Auto, {} };
        std::string folder(value);
        std::string file(kLibrary);
        if (const auto name = FileName(value); name.size() < value.size() && IsLibraryName(name)) {
            file = std::string(name);
            folder.resize(folder.size() - name.size());
        }
        // A drive's root keeps its slash ("C:\").
        while (folder.size() > 1 && (folder.back() == '\\' || folder.back() == '/') && folder[folder.size() - 2] != ':') {
            folder.pop_back();
        }
        return { Mode::Folder, std::move(folder), std::move(file) };
    }

    /// Where a copy was found, the strongest claim first.
    enum class Place : int
    {
        Loaded = 0,   // a mod has it loaded in the game right now
        Plugins,      // under Data/SKSE/Plugins
        Shaders,      // under Data/Shaders
        Game,         // the game's own folder, or an upscaler's folder beside the plugins
    };

    struct Found
    {
        std::string file;    // UTF-8 path of the library
        Place       place = Place::Game;
        int         depth = 0;   // folders below the place's root
    };

    /// What Rank knows beyond the files themselves.
    struct Hints
    {
        std::string chosen;       // the file the player's mod is set to run from (a path); empty when it names none
        int         series = 0;   // GpuSeries of the game's GPU
    };

    /// Orders found copies for trying: by place, then the shallowest, then by folder; one entry per file, compared
    /// without ASCII case and with either slash. Within a folder: the file the player's mod is set to run from, then
    /// on RTX 50 and later the NVIDIA builds under a version number, the newest first (what Theo's Render Pipeline's
    /// Automatic runs there), then nvngx_dlssnr.dll, then the other names.
    inline void Rank(std::vector<Found>& found, const Hints& hints = {})
    {
        const auto norm = [](std::string_view path) {
            std::string k = Lower(path);
            std::replace(k.begin(), k.end(), '/', '\\');
            return k;
        };
        const auto        key    = [&](const Found& f) { return norm(f.file); };
        const std::string chosen = norm(hints.chosen);
        const auto        folder = [](const std::string& k) { return k.substr(0, k.size() - FileName(k).size()); };
        const auto        order  = [&](const std::string& k) {
            if (!chosen.empty() && k == chosen) return 0;
            if (hints.series >= 50 && !NameVersion(FileName(k)).empty()) return 1;
            return IsStandardName(FileName(k)) ? 2 : 3;
        };
        std::stable_sort(found.begin(), found.end(), [&](const Found& a, const Found& b) {
            if (a.place != b.place) return static_cast<int>(a.place) < static_cast<int>(b.place);
            if (a.depth != b.depth) return a.depth < b.depth;
            const std::string ka = key(a), kb = key(b);
            if (const std::string fa = folder(ka), fb = folder(kb); fa != fb) return fa < fb;
            const int oa = order(ka), ob = order(kb);
            if (oa != ob) return oa < ob;
            if (oa == 1) {
                const auto va = NameVersion(FileName(ka)), vb = NameVersion(FileName(kb));
                if (va != vb) return vb < va;
            }
            return ka < kb;
        });
        std::vector<Found>       kept;
        std::vector<std::string> seen;
        for (auto& f : found) {
            std::string k = key(f);
            if (std::find(seen.begin(), seen.end(), k) != seen.end()) continue;
            seen.push_back(std::move(k));
            kept.push_back(std::move(f));
        }
        found = std::move(kept);
    }

    /// The folder name of the private copy of a library (NeuralSource::PrivateCopy): another one when the file's
    /// path, size or write time changes, so an updated library is copied again and the old copy can be told apart
    /// and removed.
    inline std::string CopyName(std::string_view file, std::uint64_t size, std::uint64_t writeTime)
    {
        std::uint64_t h   = 14695981039346656037ull;   // FNV-1a
        const auto    mix = [&h](std::uint8_t b) {
            h ^= b;
            h *= 1099511628211ull;
        };
        for (const char c : file) mix(static_cast<std::uint8_t>(c == '/' ? '\\' : LowerAscii(c)));
        for (int i = 0; i < 8; ++i) mix(static_cast<std::uint8_t>(size >> (i * 8)));
        for (int i = 0; i < 8; ++i) mix(static_cast<std::uint8_t>(writeTime >> (i * 8)));
        static constexpr char kHex[] = "0123456789abcdef";
        std::string           name(16, '0');
        for (int i = 0; i < 16; ++i) name[15 - i] = kHex[(h >> (i * 4)) & 0xf];
        return name;
    }
}
