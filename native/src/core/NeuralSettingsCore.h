#pragma once

// Phantasm - NeuralSettingsCore: the NR settings a player chose in their neural rendering mod, engine-free
// (native/build.ps1 -Tests). NeuralStills.cpp follows the mod whose NR library NR runs from: it reads that mod's
// settings file through this and lays what it finds over Phantasm.ini's values.
//
// Author: Severause

#include "NeuralSourceCore.h"
#include "NeuralStillCore.h"

#include <charconv>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>

namespace Phantasm::NeuralSettingsCore
{
    using NeuralSourceCore::Lower;
    using NeuralSourceCore::Trim;

    /// The mod whose settings NR follows.
    enum class Mod
    {
        None,             // Phantasm.ini's own values
        SkyrimUpscaler,   // SkyrimUpscaler.ini, [DLSS NR]
        Theos,            // TheosRenderPipeline.ini under TheosRenderPipeline.User.ini, [SourceDLSSG]
        OpenShaders,      // CommunityShaders/SettingsUser.json, Upscaling
    };

    /// The mod a library file belongs to, by the folder it was found in (the path as the game sees it, either slash,
    /// any case).
    inline Mod ModOf(std::string_view libraryFile)
    {
        std::string p = Lower(libraryFile);
        for (char& c : p) {
            if (c == '/') c = '\\';
        }
        if (p.find("\\upscalerbaseplugin\\") != std::string::npos) return Mod::SkyrimUpscaler;
        if (p.find("\\theosrenderpipeline\\") != std::string::npos) return Mod::Theos;
        if (p.find("\\shaders\\upscaling\\") != std::string::npos) return Mod::OpenShaders;
        return Mod::None;
    }

    inline const char* ModName(Mod m) noexcept
    {
        switch (m) {
        case Mod::SkyrimUpscaler: return "Skyrim Upscaler";
        case Mod::Theos: return "Theo's Render Pipeline";
        case Mod::OpenShaders: return "Open Shaders";
        default: return "Phantasm.ini";
        }
    }

    /// What a mod's settings say; a field it does not set keeps Phantasm.ini's value.
    struct Followed
    {
        std::optional<bool>  enabled;   // the mod's own NR switch
        std::optional<int>   style;
        std::optional<float> intensity, tone, structure, skin;

        bool Any() const noexcept { return enabled || style || intensity || tone || structure || skin; }
    };

    inline std::optional<float> Number(std::string_view text) noexcept
    {
        text = Trim(text);
        float       v   = 0.0f;
        const auto* end = text.data() + text.size();
        const auto  r   = std::from_chars(text.data(), end, v);
        if (r.ec != std::errc{} || r.ptr != end || !std::isfinite(v)) return std::nullopt;
        return v;
    }

    inline std::optional<bool> Bool(std::string_view text)
    {
        const std::string t = Lower(Trim(text));
        if (t == "true" || t == "1" || t == "yes" || t == "on") return true;
        if (t == "false" || t == "0" || t == "no" || t == "off") return false;
        return std::nullopt;
    }

    /// The value of `key` in [section] of an ini text, names in any case; a line starting with ';' or '#' is a
    /// comment, and so is the rest of a value's line after either. The last one wins. Null when it is not there.
    inline std::optional<std::string> IniValue(std::string_view text, std::string_view section, std::string_view key)
    {
        const std::string          wantSection = Lower(section), wantKey = Lower(key);
        std::string                current;
        std::optional<std::string> found;
        while (!text.empty()) {
            const auto       eol  = text.find('\n');
            std::string_view line = Trim(text.substr(0, eol));
            text                  = eol == std::string_view::npos ? std::string_view{} : text.substr(eol + 1);
            if (line.starts_with("\xEF\xBB\xBF")) line = Trim(line.substr(3));
            if (line.empty() || line.front() == ';' || line.front() == '#') continue;
            if (line.front() == '[') {
                const auto close = line.find(']');
                if (close != std::string_view::npos) current = Lower(Trim(line.substr(1, close - 1)));
                continue;
            }
            const auto eq = line.find('=');
            if (eq == std::string_view::npos || current != wantSection || Lower(Trim(line.substr(0, eq))) != wantKey) continue;
            std::string_view value = line.substr(eq + 1);
            if (const auto c = value.find_first_of(";#"); c != std::string_view::npos) value = value.substr(0, c);
            found = std::string(Trim(value));
        }
        return found;
    }

    /// SkyrimUpscaler.ini.
    inline Followed ParseSkyrimUpscaler(std::string_view ini)
    {
        constexpr std::string_view s = "DLSS NR";
        const auto num = [&](std::string_view key) {
            const auto v = IniValue(ini, s, key);
            return v ? Number(*v) : std::nullopt;
        };
        Followed f;
        if (const auto v = IniValue(ini, s, "mEnableDLSSNR")) f.enabled = Bool(*v);
        if (const auto v = num("mDLSSNRStyle")) f.style = static_cast<int>(*v);
        f.intensity = num("mDLSSNRIntensity");
        f.tone      = num("mDLSSNRLocalToneStrength");
        f.structure = num("mDLSSNRLocalStructureStrength");
        f.skin      = num("mDLSSNRSkinStructureStrength");
        return f;
    }

    /// Theo's Render Pipeline: the shipped file's values under the user file's (a key the user file lacks falls back).
    inline Followed ParseTheos(std::string_view defaults, std::string_view user)
    {
        constexpr std::string_view s = "SourceDLSSG";
        const auto value = [&](std::string_view key) {
            auto v = IniValue(user, s, key);
            return v ? v : IniValue(defaults, s, key);
        };
        const auto num = [&](std::string_view key) {
            const auto v = value(key);
            return v ? Number(*v) : std::nullopt;
        };
        Followed f;
        if (const auto v = value("NeuralRenderingEnabled")) f.enabled = Bool(*v);
        if (const auto v = num("NRStyle")) f.style = static_cast<int>(*v);
        f.intensity = num("NRIntensity");
        f.tone      = num("NRLocalTone");
        f.structure = num("NRLocalStructure");
        f.skin      = num("NRSkinStructure");
        return f;
    }

    /// The NR library Theo's Render Pipeline is set to run from ([Experimental] NeuralRenderingRuntimePath, the user
    /// file's over the shipped one's): its file name, or empty for Automatic, an unset key, or a value that names no
    /// library.
    inline std::string TheosRuntime(std::string_view defaults, std::string_view user)
    {
        constexpr std::string_view s = "Experimental", k = "NeuralRenderingRuntimePath";
        auto                       v = IniValue(user, s, k);
        if (!v) v = IniValue(defaults, s, k);
        if (!v) return {};
        std::string_view value = Trim(*v);
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = Trim(value.substr(1, value.size() - 2));
        const std::string_view name = NeuralSourceCore::FileName(value);
        return NeuralSourceCore::IsLibraryName(name) ? std::string(name) : std::string{};
    }

    /// The raw text of `key`'s value inside the JSON object `object` (its own keys only, not a nested object's): a
    /// number or a word as written, a string without its quotes, an object or array with its brackets. Null when the
    /// object has no such key. Tolerant: no validation beyond what finding the key needs.
    inline std::optional<std::string_view> JsonMember(std::string_view object, std::string_view key)
    {
        const auto skipString = [&](std::size_t i) {   // i at the opening quote; the index past the closing one
            for (++i; i < object.size(); ++i) {
                if (object[i] == '\\') ++i;
                else if (object[i] == '"') return i + 1;
            }
            return object.size();
        };
        const auto skipValue = [&](std::size_t i) {   // the index past the value starting at i
            if (i >= object.size()) return i;
            if (object[i] == '"') return skipString(i);
            if (object[i] == '{' || object[i] == '[') {
                int depth = 0;
                for (; i < object.size(); ++i) {
                    if (object[i] == '"') {
                        i = skipString(i) - 1;
                    } else if (object[i] == '{' || object[i] == '[') {
                        ++depth;
                    } else if (object[i] == '}' || object[i] == ']') {
                        if (--depth == 0) return i + 1;
                    }
                }
                return object.size();
            }
            while (i < object.size() && object[i] != ',' && object[i] != '}' && object[i] != ']') ++i;
            return i;
        };
        std::size_t i = object.find('{');
        if (i == std::string_view::npos) return std::nullopt;
        for (++i; i < object.size();) {
            const char c = object[i];
            if (c == '}') break;
            if (c != '"') {
                ++i;
                continue;
            }
            const std::size_t      nameEnd = skipString(i);
            const std::string_view name    = object.substr(i + 1, nameEnd > i + 1 ? nameEnd - i - 2 : 0);
            std::size_t            v       = object.find(':', nameEnd);
            if (v == std::string_view::npos) break;
            ++v;
            while (v < object.size() && (object[v] == ' ' || object[v] == '\t' || object[v] == '\r' || object[v] == '\n')) ++v;
            const std::size_t end = skipValue(v);
            if (name == key) {
                std::string_view value = Trim(object.substr(v, end - v));
                if (value.size() >= 2 && value.front() == '"') value = value.substr(1, value.size() - 2);
                return value;
            }
            i = end;
        }
        return std::nullopt;
    }

    /// Open Shaders' SettingsUser.json: Upscaling.neuralRenderingEnabled and Upscaling.neuralRenderingTuning.
    inline Followed ParseOpenShaders(std::string_view json)
    {
        Followed   f;
        const auto upscaling = JsonMember(json, "Upscaling");
        if (!upscaling) return f;
        if (const auto v = JsonMember(*upscaling, "neuralRenderingEnabled")) f.enabled = Bool(*v);
        const auto tuning = JsonMember(*upscaling, "neuralRenderingTuning");
        if (!tuning) return f;
        const auto num = [&](std::string_view key) {
            const auto v = JsonMember(*tuning, key);
            return v ? Number(*v) : std::nullopt;
        };
        if (const auto v = num("style")) f.style = static_cast<int>(*v);
        f.intensity = num("intensity");
        f.tone      = num("localToneStrength");
        f.structure = num("localStructureStrength");
        f.skin      = num("skinStructureStrength");
        return f;
    }

    /// `base` (Phantasm.ini's) with what the mod set laid over it, in Phantasm's ranges. A style past 2 is 2 (the
    /// libraries of today give 2's picture for any higher one), and a negative skin strength, the mods' "as the
    /// structure" mark, takes the structure's.
    inline NeuralStillCore::Settings Merge(NeuralStillCore::Settings base, const Followed& f)
    {
        if (f.style) base.model = NeuralStillCore::ClampModel(*f.style);
        if (f.intensity) base.intensity = NeuralStillCore::ClampStrength(*f.intensity);
        if (f.tone) base.tone = NeuralStillCore::ClampStrength(*f.tone);
        if (f.structure) base.structure = NeuralStillCore::ClampStrength(*f.structure);
        if (f.skin) base.skin = *f.skin < 0.0f ? base.structure : NeuralStillCore::ClampStrength(*f.skin);
        return base;
    }
}
