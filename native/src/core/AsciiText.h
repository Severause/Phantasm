#pragma once

// Phantasm - AsciiText: ASCII case folding for names and paths (EditorIDs, node names, texture and clip paths), the
// one copy every core rule and the renderer use. Only A-Z fold; every other byte (UTF-8 included) compares as it is,
// as std::tolower does in the "C" locale Phantasm runs in. Tested by AsciiTextTest.cpp.
//
// Author: Severause

#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>

namespace Phantasm::AsciiText
{
    constexpr char Lower(char c) noexcept
    {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }

    inline void LowerInPlace(std::string& s) noexcept
    {
        for (auto& c : s) c = Lower(c);
    }

    inline std::string Lowered(std::string_view s)
    {
        std::string out(s);
        LowerInPlace(out);
        return out;
    }

    inline bool EqualsI(std::string_view a, std::string_view b) noexcept
    {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (Lower(a[i]) != Lower(b[i])) return false;
        }
        return true;
    }

    inline bool StartsWithI(std::string_view s, std::string_view prefix) noexcept
    {
        return s.size() >= prefix.size() && EqualsI(s.substr(0, prefix.size()), prefix);
    }

    inline bool EndsWithI(std::string_view s, std::string_view suffix) noexcept
    {
        return s.size() >= suffix.size() && EqualsI(s.substr(s.size() - suffix.size()), suffix);
    }

    /// s without the white space at either end.
    inline std::string_view Trim(std::string_view s) noexcept
    {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
        return s;
    }

    /// Whether `part` occurs in s, any case; an empty part occurs everywhere.
    inline bool ContainsI(std::string_view s, std::string_view part) noexcept
    {
        if (part.empty()) return true;
        for (std::size_t i = 0; i + part.size() <= s.size(); ++i) {
            if (EqualsI(s.substr(i, part.size()), part)) return true;
        }
        return false;
    }
}
