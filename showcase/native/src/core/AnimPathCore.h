#pragma once

// Phantasm Showcase - AnimPathCore: which loose files under Data/meshes are animations the browser lists, the
// folders the player hid, and the folder and search views over the sorted list. Engine-free.
//
// Paths are relative to Data/meshes with backslashes ("actors\character\animations\x.hkx"): Phantasm's `idle` hint
// takes `file:<path>` with `meshes\` left off.

#include "Text.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Showcase::AnimPathCore
{
    /// Forward slashes become backslashes; doubled and leading separators go.
    inline std::string Normalize(std::string_view path)
    {
        std::string out;
        out.reserve(path.size());
        for (char c : path) {
            if (c == '/') c = '\\';
            if (c == '\\' && (out.empty() || out.back() == '\\')) continue;
            out.push_back(c);
        }
        if (!out.empty() && out.back() == '\\') out.pop_back();
        return out;
    }

    /// The part of a found file's path after its last "meshes" folder (case-insensitive), normalized; "" when it has
    /// none. Under MO2 a walk of Data/meshes yields each file at its real place in a mod folder, so the path under
    /// meshes is cut from the file's own path rather than taken relative to Data/meshes.
    inline std::string UnderMeshes(std::string_view path)
    {
        const auto n = Normalize(path);
        std::size_t start = 0, after = std::string::npos;
        for (auto end = n.find('\\'); end != std::string::npos; start = end + 1, end = n.find('\\', start)) {
            if (Text::Fold(std::string_view(n).substr(start, end - start)) == "meshes") after = end + 1;
        }
        return after == std::string::npos ? std::string() : n.substr(after);
    }

    /// The path's folders and file name.
    inline std::vector<std::string_view> Segments(std::string_view path)
    {
        std::vector<std::string_view> out;
        std::size_t start = 0;
        while (start <= path.size()) {
            const auto end = path.find('\\', start);
            const auto seg = path.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
            if (!seg.empty()) out.push_back(seg);
            if (end == std::string_view::npos) break;
            start = end + 1;
        }
        return out;
    }

    /// An animation the browser lists: a .hkx under a folder named "animations", never a behaviour graph, a character
    /// project or a skeleton (those sit in behaviors, characters and "character assets"). `folded` is the folded path.
    inline bool IsAnimation(std::string_view folded)
    {
        if (folded.size() < 5 || folded.substr(folded.size() - 4) != ".hkx") return false;
        const auto segs = Segments(folded);
        if (segs.size() < 2) return false;
        bool underAnimations = false;
        for (std::size_t i = 0; i + 1 < segs.size(); ++i) {
            const auto s = segs[i];
            if (s == "behaviors" || s == "behaviours" || s == "characters" || s == "character assets") return false;
            if (s == "animations") underAnimations = true;
        }
        return underAnimations;
    }

    /// The path is under a hidden folder: an entry without a separator hides every folder of that name; one with a
    /// separator hides that folder path from the top (both folded, compared whole, never part of a name).
    inline bool Hidden(std::string_view folded, const std::vector<std::string>& hiddenFolded)
    {
        if (hiddenFolded.empty()) return false;
        const auto segs = Segments(folded);
        for (const auto& h : hiddenFolded) {
            if (h.empty()) continue;
            if (h.find('\\') == std::string::npos) {
                for (std::size_t i = 0; i + 1 < segs.size(); ++i) {
                    if (segs[i] == h) return true;
                }
            } else if (folded.size() > h.size() && folded.substr(0, h.size()) == h && folded[h.size()] == '\\') {
                return true;
            }
        }
        return false;
    }

    struct Folder
    {
        std::string name;
        std::size_t count = 0;   // animations anywhere under it
    };

    struct Listing
    {
        std::vector<Folder>      folders;   // in name order
        std::vector<std::size_t> files;     // indices of the files directly in the folder, in path order
    };

    /// What one folder holds, from the folded paths sorted in byte order. `folder` is folded, normalized, "" for the
    /// top.
    inline Listing List(const std::vector<std::string>& sortedFolded, std::string_view folder)
    {
        Listing out;
        std::string prefix(folder);
        if (!prefix.empty()) prefix.push_back('\\');
        auto it = std::lower_bound(sortedFolded.begin(), sortedFolded.end(), prefix);
        for (; it != sortedFolded.end(); ++it) {
            const std::string_view p = *it;
            if (p.substr(0, prefix.size()) != prefix) break;
            const auto rest = p.substr(prefix.size());
            const auto slash = rest.find('\\');
            if (slash == std::string_view::npos) {
                out.files.push_back(static_cast<std::size_t>(it - sortedFolded.begin()));
                continue;
            }
            const auto name = rest.substr(0, slash);
            if (out.folders.empty() || out.folders.back().name != name) out.folders.push_back({ std::string(name), 0 });
            ++out.folders.back().count;
        }
        return out;
    }

    /// Indices of the paths whose folded text has every word, at most `limit`, in path order.
    inline std::vector<std::size_t> Search(const std::vector<std::string>& sortedFolded,
                                           const std::vector<std::string>& words, std::size_t limit)
    {
        std::vector<std::size_t> out;
        if (words.empty()) return out;
        for (std::size_t i = 0; i < sortedFolded.size() && out.size() < limit; ++i) {
            if (Text::MatchesAll(sortedFolded[i], words)) out.push_back(i);
        }
        return out;
    }

    /// The file name without its folder and extension, for a row.
    inline std::string_view Stem(std::string_view path)
    {
        const auto slash = path.rfind('\\');
        auto name = slash == std::string_view::npos ? path : path.substr(slash + 1);
        if (name.size() > 4 && (name.substr(name.size() - 4) == ".hkx" || name.substr(name.size() - 4) == ".HKX"))
            name = name.substr(0, name.size() - 4);
        return name;
    }

    /// A path the page sent may become the idle hint: relative, no "..", no drive, a .hkx.
    inline bool SafeToPlay(std::string_view path)
    {
        if (path.empty() || path.size() > 1000 || path.find('\0') != std::string_view::npos) return false;
        if (path.front() == '\\' || path.front() == '/' || path.find(':') != std::string_view::npos) return false;
        const auto normalized = Normalize(path);   // kept: Segments returns views into it
        for (auto s : Segments(normalized)) {
            if (s == "..") return false;
        }
        const auto f = Text::Fold(path);
        return f.size() > 4 && f.substr(f.size() - 4) == ".hkx";
    }
}
