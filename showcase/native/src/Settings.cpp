// Phantasm Showcase - Settings: see Settings.h.

#include "PCH.h"

#include "Settings.h"

#include <fstream>

namespace Showcase::Settings
{
    namespace
    {
        Values     g_values;
        std::mutex g_mutex;

        std::filesystem::path FilePath() { return Folder() / "settings.json"; }

        void Write(const Values& v)
        {
            std::error_code ec;
            const auto path = FilePath();
            std::filesystem::create_directories(path.parent_path(), ec);
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            if (!out) {
                SKSE::log::warn("settings: cannot write {}", Utf8(path));
                return;
            }
            out << nlohmann::json{ { "menuKey", v.menuKey },
                                   { "uiScale", v.uiScale },
                                   { "shotSize", v.shotSize },
                                   { "hiddenAnimFolders", v.hiddenAnimFolders },
                                   { "showFps", v.showFps },
                                   { "freezeWorld", v.freezeWorld },
                                   { "uiHost", v.uiHost } }
                       .dump(2);
        }
    }

    std::string Utf8(const std::filesystem::path& p)
    {
        const auto u8 = p.u8string();
        return std::string(reinterpret_cast<const char*>(u8.c_str()), u8.size());
    }

    std::filesystem::path Folder()
    {
        auto dir = SKSE::log::log_directory();
        return (dir ? *dir : std::filesystem::path("Data/SKSE/Plugins")) / "PhantasmShowcase";
    }

    void Load()
    {
        const auto path = FilePath();
        Values v;
        std::ifstream in(path, std::ios::binary);
        if (in) {
            try {
                const auto j = nlohmann::json::parse(in);
                const auto key = j.value("menuKey", v.menuKey);
                // A scan code the keyboard can send; anything else keeps the default.
                if (key > 0 && key < 0x100) v.menuKey = key;
                const double scale = j.value("uiScale", v.uiScale);
                if (std::isfinite(scale)) v.uiScale = std::clamp(scale, 0.7, 1.6);
                v.shotSize = std::clamp<std::uint32_t>(j.value("shotSize", v.shotSize), 512u, 4096u);
                if (const auto it = j.find("showFps"); it != j.end() && it->is_boolean()) v.showFps = it->get<bool>();
                if (const auto it = j.find("freezeWorld"); it != j.end() && it->is_boolean()) v.freezeWorld = it->get<bool>();
                if (const auto it = j.find("uiHost"); it != j.end() && it->is_string()) v.uiHost = it->get<std::string>();
                if (const auto it = j.find("hiddenAnimFolders"); it != j.end() && it->is_array()) {
                    for (const auto& e : *it) {
                        if (e.is_string() && !e.get<std::string>().empty()) v.hiddenAnimFolders.push_back(e.get<std::string>());
                    }
                }
            } catch (const std::exception& e) {
                SKSE::log::warn("settings: {} does not parse ({}); using the defaults", Utf8(path), e.what());
            }
        } else {
            Write(v);
        }
        std::lock_guard lock(g_mutex);
        g_values = v;
        SKSE::log::info("settings: menu key {}, {} hidden animation folder(s), UI host {}", KeyName(v.menuKey),
                        v.hiddenAnimFolders.size(), v.uiHost);
    }

    Values Get()
    {
        std::lock_guard lock(g_mutex);
        return g_values;
    }

    void SetShowFps(bool on)
    {
        Values v;
        {
            std::lock_guard lock(g_mutex);
            if (g_values.showFps == on) return;
            g_values.showFps = on;
            v                = g_values;
        }
        Write(v);
    }

    void SetFreezeWorld(bool on)
    {
        Values v;
        {
            std::lock_guard lock(g_mutex);
            if (g_values.freezeWorld == on) return;
            g_values.freezeWorld = on;
            v                    = g_values;
        }
        Write(v);
    }

    std::string KeyName(std::uint32_t code)
    {
        // F1-F10 are 0x3B-0x44, F11 and F12 0x57 and 0x58.
        if (code >= 0x3B && code <= 0x44) return std::format("F{}", code - 0x3B + 1);
        if (code == 0x57) return "F11";
        if (code == 0x58) return "F12";
        return std::format("key 0x{:02X}", code);
    }
}
