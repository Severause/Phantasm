#pragma once

// Phantasm - SettingsCore: Phantasm.ini as values, engine-free (native/build.ps1 -Tests). Parse reads the file's
// text; Rewrite puts changed values back into that same text, so a key's line stays where it is and every comment
// stays: the settings pages save without taking the file's explanations away.
//
// Author: Severause

#include "LiveViewBudgetCore.h"
#include "NeuralSourceCore.h"
#include "NeuralStillCore.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace Phantasm::SettingsCore
{
    /// Every setting of Phantasm.ini. The defaults are those of a file that does not name the key.
    struct Values
    {
        // [LiveViews]
        std::string budget   = "smooth";   // smooth | balanced | light
        double      budgetMs = 0.0;        // 0: the mode's own; else 0.5..50 milliseconds
        // [NeuralRendering]
        bool        enabled        = true;   // Enabled. Off keeps Folder for later. A file that names Folder and no
                                             // Enabled reads as on
        std::string folder;                  // no source ("": off), "auto", or a folder or a library file
        bool        unsignedCopies = true;
        bool        follow         = true;
        int         model          = 1;      // 0..2
        float       intensity      = 1.0f;   // the four strengths: 0..1
        float       tone           = 1.0f;
        float       structure      = 1.0f;
        float       skin           = 1.0f;
        int         passes         = 4;      // 1..8
        bool        live           = false;
        float       liveScale      = 0.5f;   // 0.25..1
        bool        prestart       = true;
        // [SettingsPage]
        std::uint32_t pageKey = 0x46;        // DirectInput scan code that opens the Magelight page (Scroll Lock);
                                             // 0: no key

        bool operator==(const Values&) const = default;
    };

    /// Whether Folder names a source for neural rendering's library: NeuralSourceCore's rule, by which "", "off",
    /// "none" and "0" name none.
    inline bool SourceNamed(std::string_view folder)
    {
        return NeuralSourceCore::ParseFolder(folder).mode != NeuralSourceCore::Mode::Off;
    }

    /// Whether the file asks for neural rendering: the switch is on and a source is named.
    inline bool NeuralWanted(const Values& v) { return v.enabled && SourceNamed(v.folder); }

    inline constexpr double kBudgetMsMin = 0.5;
    inline constexpr double kBudgetMsMax = 50.0;
    inline constexpr float  kLiveScaleMin = 0.25f;
    inline constexpr float  kLiveScaleMax = 1.0f;

    /// Whether a DirectInput scan code may open the Magelight page. Not Escape (the host closes the page with it),
    /// not Enter, and not a modifier or a Windows key alone.
    inline constexpr bool KeyUsable(std::uint32_t dik) noexcept
    {
        if (dik == 0 || dik > 0xFF) return false;
        switch (dik) {
        case 0x01:   // Escape
        case 0x1C:   // Enter
        case 0x9C:   // Numpad Enter
        case 0x1D:   // Left Ctrl
        case 0x9D:   // Right Ctrl
        case 0x2A:   // Left Shift
        case 0x36:   // Right Shift
        case 0x38:   // Left Alt
        case 0xB8:   // Right Alt
        case 0xDB:   // Left Windows
        case 0xDC:   // Right Windows
            return false;
        default:
            return true;
        }
    }

    /// The DirectInput scan code of a Windows scan code as MapVirtualKey's MAPVK_VK_TO_VSC_EX gives it (0xE0xx for
    /// an extended key); 0 for none, and for the 0xE1 keys (Pause), which the caller names by virtual key.
    inline constexpr std::uint32_t KeyFromScan(std::uint32_t scanEx) noexcept
    {
        const std::uint32_t low  = scanEx & 0xFF;
        const std::uint32_t high = scanEx >> 8;
        if (low == 0 || low > 0x7F) return 0;
        if (high == 0) return low;
        if (high == 0xE0) return low | 0x80;
        return 0;
    }

    /// The name of a key that is the same on every keyboard layout; "" for the rest (letters, digits, punctuation),
    /// which the layout names.
    inline constexpr std::string_view KeyFixedName(std::uint32_t dik) noexcept
    {
        constexpr std::string_view kFunction[] = { "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10" };
        if (dik >= 0x3B && dik <= 0x44) return kFunction[dik - 0x3B];
        switch (dik) {
        case 0x57: return "F11";
        case 0x58: return "F12";
        case 0x64: return "F13";
        case 0x65: return "F14";
        case 0x66: return "F15";
        case 0x46: return "Scroll Lock";
        case 0x45: return "Num Lock";
        case 0x3A: return "Caps Lock";
        case 0xC5: return "Pause";
        case 0xB7: return "Print Screen";
        case 0xD2: return "Insert";
        case 0xD3: return "Delete";
        case 0xC7: return "Home";
        case 0xCF: return "End";
        case 0xC9: return "Page Up";
        case 0xD1: return "Page Down";
        case 0xC8: return "Up";
        case 0xD0: return "Down";
        case 0xCB: return "Left";
        case 0xCD: return "Right";
        case 0x0F: return "Tab";
        case 0x39: return "Space";
        case 0x0E: return "Backspace";
        case 0x52: return "Numpad 0";
        case 0x4F: return "Numpad 1";
        case 0x50: return "Numpad 2";
        case 0x51: return "Numpad 3";
        case 0x4B: return "Numpad 4";
        case 0x4C: return "Numpad 5";
        case 0x4D: return "Numpad 6";
        case 0x47: return "Numpad 7";
        case 0x48: return "Numpad 8";
        case 0x49: return "Numpad 9";
        case 0x53: return "Numpad .";
        case 0x4E: return "Numpad +";
        case 0x4A: return "Numpad -";
        case 0x37: return "Numpad *";
        case 0xB5: return "Numpad /";
        default: return {};
        }
    }

    namespace detail
    {
        inline std::string Trim(std::string_view s)
        {
            const auto a = s.find_first_not_of(" \t\r");
            const auto b = s.find_last_not_of(" \t\r");
            return a == std::string_view::npos ? std::string{} : std::string(s.substr(a, b - a + 1));
        }

        inline std::string Lower(std::string s)
        {
            for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        /// A number that is the whole of `text`.
        inline bool ToNumber(const std::string& text, double& out)
        {
            if (text.empty()) return false;
            char*        end = nullptr;
            const double x   = std::strtod(text.c_str(), &end);
            if (end == text.c_str() || *end != '\0' || !std::isfinite(x)) return false;
            out = x;
            return true;
        }

        /// What a line of the file is. `key` is lower case; `value` is the text after '=' without a comment that
        /// follows it and without the quotes around it; `eq` is the '=' and `comment` where that comment starts
        /// (npos for none).
        struct Line
        {
            enum class Kind { Other, Section, Key } kind = Kind::Other;
            std::string section;   // Kind::Section: its name, lower case
            std::string key;
            std::string value;
            std::size_t eq      = std::string::npos;
            std::size_t comment = std::string::npos;
        };

        inline Line Read(std::string_view raw)
        {
            Line        out;
            std::string line = Trim(raw);
            if (line.empty() || line[0] == ';' || line[0] == '#') return out;
            if (line.front() == '[') {
                // A comment after the header is allowed ("[LiveViews] ; ...").
                if (const auto close = line.find(']'); close != std::string::npos) {
                    out.kind    = Line::Kind::Section;
                    out.section = Lower(Trim(std::string_view(line).substr(1, close - 1)));
                    return out;
                }
            }
            const auto eq = raw.find('=');
            if (eq == std::string_view::npos) return out;
            out.kind = Line::Kind::Key;
            out.eq   = eq;
            out.key  = Lower(Trim(raw.substr(0, eq)));
            auto rest = raw.substr(eq + 1);
            // A value in quotes runs to its closing quote, so a path may hold ';' or '#'. Outside quotes either
            // starts a comment.
            std::size_t from = 0;
            if (const auto open = rest.find_first_not_of(" \t"); open != std::string_view::npos && rest[open] == '"') {
                if (const auto close = rest.find('"', open + 1); close != std::string_view::npos) from = close + 1;
            }
            if (const auto c = rest.find_first_of(";#", from); c != std::string_view::npos) {
                out.comment = eq + 1 + c;
                rest        = rest.substr(0, c);
            }
            out.value = Trim(rest);
            if (from != 0 && out.value.size() >= 2 && out.value.front() == '"' && out.value.back() == '"') {
                out.value = out.value.substr(1, out.value.size() - 2);
            }
            return out;
        }

        /// The file's lines without their ends, a UTF-8 byte order mark taken off the first.
        inline std::vector<std::string> Lines(std::string_view text, bool& bom, bool& endsWithNewline)
        {
            bom = text.starts_with("\xEF\xBB\xBF");
            if (bom) text.remove_prefix(3);
            endsWithNewline = !text.empty() && text.back() == '\n';
            std::vector<std::string> lines;
            std::size_t              at = 0;
            while (at < text.size()) {
                const auto nl  = text.find('\n', at);
                auto       one = text.substr(at, nl == std::string_view::npos ? std::string_view::npos : nl - at);
                if (!one.empty() && one.back() == '\r') one.remove_suffix(1);
                lines.emplace_back(one);
                if (nl == std::string_view::npos) break;
                at = nl + 1;
            }
            return lines;
        }

        /// Two decimals at most and one at least: 1.0, 0.5, 0.75.
        inline std::string Decimal(double x)
        {
            char buf[32]{};
            std::snprintf(buf, sizeof(buf), "%.2f", x);
            std::string s = buf;
            while (s.size() > 1 && s.back() == '0' && s[s.size() - 2] != '.') s.pop_back();
            return s;
        }

        inline std::string Whole(long long x) { return std::to_string(x); }

        /// A number read from text or a page as T. A whole number far outside any setting's range is brought near
        /// it first: converting such a double is undefined.
        template <class T>
        T As(double x) noexcept
        {
            if constexpr (std::is_floating_point_v<T>) {
                return static_cast<T>(x);
            } else if constexpr (std::is_unsigned_v<T>) {
                return static_cast<T>(std::clamp(x, 0.0, 1.0e9));
            } else {
                return static_cast<T>(std::clamp(x, -1.0e9, 1.0e9));
            }
        }

        /// A whole number plainly, anything else as a Decimal: 0, 6, 6.5.
        inline std::string Plain(double x)
        {
            return std::floor(x) == x ? Whole(static_cast<long long>(x)) : Decimal(x);
        }
    }

    /// Brings every value inside its range. A budget that is no mode is smooth; a number that is not one is the
    /// default; a key that cannot open the page is no key.
    inline void Clamp(Values& v)
    {
        bool known = false;
        v.budget   = LiveViewBudgetCore::ModeName(LiveViewBudgetCore::ParseMode(v.budget, &known));
        v.budgetMs = std::isfinite(v.budgetMs) && v.budgetMs > 0.0 ? std::clamp(v.budgetMs, kBudgetMsMin, kBudgetMsMax) : 0.0;
        // Quotes are the file's way of writing a path, not part of one: no Windows path holds a quote, and one
        // inside a value would end the quoted text early when it is read back.
        std::erase(v.folder, '"');
        v.folder = detail::Trim(v.folder);
        v.model     = NeuralStillCore::ClampModel(v.model);
        v.intensity = NeuralStillCore::ClampStrength(v.intensity);
        v.tone      = NeuralStillCore::ClampStrength(v.tone);
        v.structure = NeuralStillCore::ClampStrength(v.structure);
        v.skin      = NeuralStillCore::ClampStrength(v.skin);
        v.passes    = NeuralStillCore::ClampPasses(v.passes);
        v.liveScale = std::isfinite(v.liveScale) ? std::clamp(v.liveScale, kLiveScaleMin, kLiveScaleMax) : 0.5f;
        if (!KeyUsable(v.pageKey)) v.pageKey = 0;
    }

    struct Parsed
    {
        Values                   values;         // as written: Clamp is the caller's
        std::vector<std::string> warnings;       // a value that is not a number, each as one line for the log
        bool                     sawLiveViews = false;
    };

    /// Reads Phantasm.ini's text. A key under no section, or under one Phantasm does not know, is ignored, and the
    /// last of a key named twice wins.
    inline Parsed Parse(std::string_view text)
    {
        Parsed      out;
        Values&     v = out.values;
        bool        bom = false, tail = false;
        std::string section;
        for (const auto& raw : detail::Lines(text, bom, tail)) {
            const auto line = detail::Read(raw);
            if (line.kind == detail::Line::Kind::Section) {
                section = line.section;
                out.sawLiveViews |= section == "liveviews";
                continue;
            }
            if (line.kind != detail::Line::Kind::Key) continue;
            const auto& key   = line.key;
            const auto& value = line.value;
            auto number = [&](auto& target) {
                double x = 0.0;
                if (detail::ToNumber(value, x)) {
                    target = detail::As<std::remove_reference_t<decltype(target)>>(x);
                } else {
                    out.warnings.push_back("Phantasm.ini: [" + section + "] " + key + " '" + value + "' is not a number");
                }
            };
            auto flag = [&](bool& target) {
                double x = target ? 1.0 : 0.0;
                number(x);
                target = x != 0.0;
            };
            if (section == "liveviews") {
                if (key == "budget") v.budget = value;
                else if (key == "budgetms") number(v.budgetMs);
            } else if (section == "neuralrendering") {
                if (key == "enabled") flag(v.enabled);
                else if (key == "folder") v.folder = value;
                else if (key == "unsigned") flag(v.unsignedCopies);
                else if (key == "follow") flag(v.follow);
                else if (key == "model") number(v.model);
                else if (key == "intensity") number(v.intensity);
                else if (key == "tone") number(v.tone);
                else if (key == "structure") number(v.structure);
                else if (key == "skin") number(v.skin);
                else if (key == "passes") number(v.passes);
                else if (key == "liveviews") flag(v.live);
                else if (key == "livescale") number(v.liveScale);
                else if (key == "prestart") flag(v.prestart);
            } else if (section == "settingspage") {
                if (key == "key") number(v.pageKey);
            }
        }
        return out;
    }

    namespace detail
    {
        struct Entry
        {
            const char* section;     // as written into a file that lacks it
            const char* key;
            std::string text;        // the value to write
            enum class Kind { Text, TextAnyCase, Number, Flag } kind;
            double      number = 0.0;
            bool        placed = false;
        };

        inline std::vector<Entry> Entries(const Values& v)
        {
            using K = Entry::Kind;
            auto flag = [](const char* s, const char* k, bool on) { return Entry{ s, k, on ? "1" : "0", K::Flag, on ? 1.0 : 0.0 }; };
            auto whole = [](const char* s, const char* k, long long x) { return Entry{ s, k, Whole(x), K::Number, static_cast<double>(x) }; };
            auto strength = [](const char* s, const char* k, float x) { return Entry{ s, k, Decimal(x), K::Number, x }; };
            return {
                Entry{ "LiveViews", "Budget", v.budget, K::TextAnyCase },
                Entry{ "LiveViews", "BudgetMs", Plain(v.budgetMs), K::Number, v.budgetMs },
                flag("NeuralRendering", "Enabled", v.enabled),
                Entry{ "NeuralRendering", "Folder", v.folder, K::Text },
                flag("NeuralRendering", "Unsigned", v.unsignedCopies),
                flag("NeuralRendering", "Follow", v.follow),
                whole("NeuralRendering", "Model", v.model),
                strength("NeuralRendering", "Intensity", v.intensity),
                strength("NeuralRendering", "Tone", v.tone),
                strength("NeuralRendering", "Structure", v.structure),
                strength("NeuralRendering", "Skin", v.skin),
                whole("NeuralRendering", "Passes", v.passes),
                flag("NeuralRendering", "LiveViews", v.live),
                strength("NeuralRendering", "LiveScale", v.liveScale),
                flag("NeuralRendering", "Prestart", v.prestart),
                whole("SettingsPage", "Key", v.pageKey),
            };
        }

        /// The entry's value as a line holds it: a text with ';' or '#' in it goes in quotes, or Read would take
        /// the rest of it for a comment.
        inline std::string Written(const Entry& e)
        {
            const bool text = e.kind == Entry::Kind::Text || e.kind == Entry::Kind::TextAnyCase;
            return text && e.text.find_first_of(";#") != std::string::npos ? "\"" + e.text + "\"" : e.text;
        }

        /// Whether the value a line holds already says what the entry would write.
        inline bool Says(const Entry& e, const std::string& value)
        {
            double x = 0.0;
            switch (e.kind) {
            case Entry::Kind::Text: return value == e.text;
            case Entry::Kind::TextAnyCase: return Lower(value) == Lower(e.text);
            case Entry::Kind::Number: return ToNumber(value, x) && std::fabs(x - e.number) < 1e-6;
            case Entry::Kind::Flag: return ToNumber(value, x) && (x != 0.0) == (e.number != 0.0);
            }
            return false;
        }
    }

    /// The file's text with `v` in it. A line whose value already says so is left as it is; a changed one keeps its
    /// place, its key as written and a comment after the value. A key the file lacks is added only when its value is
    /// not the default the file's silence already means: at the end of its section, or with its section at the end
    /// of the file. Every line of a key named twice is changed. The line ends and a byte order mark stay as they were.
    inline std::string Rewrite(std::string_view text, const Values& v)
    {
        bool              bom = false, tail = false;
        auto              lines    = detail::Lines(text, bom, tail);
        const std::string newline  = text.find("\r\n") != std::string_view::npos || text.empty() ? "\r\n" : "\n";
        auto              entries  = detail::Entries(v);
        const auto        defaults = detail::Entries(Values{});

        // Each known section's last line that holds something, to add a missing key after.
        struct Seen
        {
            std::string name;
            std::size_t last = 0;
        };
        std::vector<Seen> sections;
        std::string       section;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const auto line = detail::Read(lines[i]);
            if (line.kind == detail::Line::Kind::Section) {
                section = line.section;
                sections.push_back({ section, i });
                continue;
            }
            if (!sections.empty() && !detail::Trim(lines[i]).empty()) sections.back().last = i;
            if (line.kind != detail::Line::Kind::Key) continue;
            for (auto& e : entries) {
                if (detail::Lower(e.section) != section || detail::Lower(e.key) != line.key) continue;
                e.placed = true;
                if (detail::Says(e, line.value)) break;
                std::string changed = lines[i].substr(0, line.eq + 1) + " " + detail::Written(e);
                if (line.comment != std::string::npos) changed += "  " + lines[i].substr(line.comment);
                lines[i] = std::move(changed);
                break;
            }
        }

        // The keys the file lacks, each after the last line of its section (the first section of that name).
        for (std::size_t n = 0; n < entries.size(); ++n) {
            auto& e = entries[n];
            if (e.placed || detail::Says(e, defaults[n].text)) continue;
            const auto name = detail::Lower(e.section);
            const auto it   = std::find_if(sections.begin(), sections.end(), [&](const Seen& s) { return s.name == name; });
            const std::string added = std::string(e.key) + " = " + detail::Written(e);
            if (it != sections.end()) {
                const auto at = it->last + 1;
                lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(at), added);
                for (auto& s : sections) {
                    if (s.last >= at) ++s.last;
                }
                it->last = at;
            } else {
                if (!lines.empty() && !detail::Trim(lines.back()).empty()) lines.emplace_back();
                lines.push_back(std::string("[") + e.section + "]");
                lines.push_back(added);
                sections.push_back({ name, lines.size() - 1 });
            }
            tail = true;
        }

        std::string out = bom ? "\xEF\xBB\xBF" : "";
        for (std::size_t i = 0; i < lines.size(); ++i) {
            out += lines[i];
            if (i + 1 < lines.size() || tail) out += newline;
        }
        return out;
    }
}
