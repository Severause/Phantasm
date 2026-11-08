#pragma once

// Phantasm Showcase - Text: ASCII case folding and word search, shared by the item and animation searches.
// Engine-free.

#include <string>
#include <string_view>
#include <vector>

namespace Showcase::Text
{
    /// ASCII lower case; other bytes (UTF-8 included) are kept as they are.
    inline std::string Fold(std::string_view s)
    {
        std::string out(s);
        for (auto& c : out) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
        return out;
    }

    /// The query's words, folded, split on spaces; empty words dropped.
    inline std::vector<std::string> Words(std::string_view query)
    {
        std::vector<std::string> out;
        std::string cur;
        for (char c : query) {
            if (c == ' ' || c == '\t') {
                if (!cur.empty()) out.push_back(Fold(cur)), cur.clear();
            } else {
                cur.push_back(c);
            }
        }
        if (!cur.empty()) out.push_back(Fold(cur));
        return out;
    }

    /// Every word occurs in `folded` (an already folded text). No words matches everything.
    inline bool MatchesAll(std::string_view folded, const std::vector<std::string>& words)
    {
        for (const auto& w : words) {
            if (folded.find(w) == std::string_view::npos) return false;
        }
        return true;
    }
}
