// Settings: see Settings.h.

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include "Settings.h"

#include "MannequinRenderer.h"
#include "NeuralStills.h"
#include "SettingsPage.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>

namespace Phantasm::Settings
{
    namespace
    {
        // One Apply at a time: two pages are open to the same file.
        std::mutex g_applyMx;
        // Guards the values below; a leaf lock.
        std::mutex    g_mx;
        Values        g_values;
        std::string   g_saveError;
        std::string   g_keyError;

        // Data/SKSE/Plugins/Phantasm.ini, from the game's own folder, whatever working folder a launcher left; a
        // plain file read and write, so MO2's virtual folder serves it.
        std::filesystem::path FilePath()
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

        // u8string: string() throws on a game folder name the ANSI code page cannot spell.
        std::string Utf8(const std::filesystem::path& path)
        {
            const auto shown = path.u8string();
            return std::string(reinterpret_cast<const char*>(shown.c_str()), shown.size());
        }

        bool ReadFile(const std::filesystem::path& path, std::string& text)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in) return false;
            text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            return !in.bad();
        }

        bool WriteFile(const std::filesystem::path& path, const std::string& text)
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
            out.flush();
            return static_cast<bool>(out);
        }

        NeuralStills::Config ToConfig(const Values& v)
        {
            NeuralStills::Config c;
            c.enabled            = v.enabled;
            c.folder             = v.folder;
            c.unsignedCopies     = v.unsignedCopies;
            c.follow             = v.follow;
            c.settings.model     = v.model;
            c.settings.intensity = v.intensity;
            c.settings.tone      = v.tone;
            c.settings.structure = v.structure;
            c.settings.skin      = v.skin;
            c.settings.passes    = v.passes;
            c.live               = v.live;
            c.liveScale          = v.liveScale;
            c.prestart           = v.prestart;
            return c;
        }

        bool NeuralDiffers(const Values& a, const Values& b)
        {
            return a.enabled != b.enabled || a.folder != b.folder || a.unsignedCopies != b.unsignedCopies || a.follow != b.follow ||
                   a.model != b.model || a.intensity != b.intensity || a.tone != b.tone || a.structure != b.structure ||
                   a.skin != b.skin || a.passes != b.passes || a.live != b.live || a.liveScale != b.liveScale ||
                   a.prestart != b.prestart;
        }

        const char* NeuralName(Status::Neural n)
        {
            switch (n) {
            case Status::Neural::Waiting: return "waiting";
            case Status::Neural::Starting: return "starting";
            case Status::Neural::Running: return "running";
            case Status::Neural::Failed: return "failed";
            default: return "off";
            }
        }
    }

    void Load()
    {
        Values     v;
        const auto path = FilePath();
        if (std::string text; ReadFile(path, text)) {
            auto parsed = SettingsCore::Parse(text);
            for (const auto& warning : parsed.warnings) SKSE::log::warn("{}", warning);
            if (!parsed.sawLiveViews) SKSE::log::warn("Phantasm.ini has no [LiveViews] section: the default budget");
            v = std::move(parsed.values);
        } else {
            SKSE::log::info("Phantasm.ini not found at {}: the defaults", Utf8(path));
        }
        // As written, before Clamp makes an unknown mode smooth: SetLiveBudget says so in the log.
        MannequinRenderer::GetSingleton()->SetLiveBudget(v.budget, v.budgetMs);
        SettingsCore::Clamp(v);
        NeuralStills::Configure(ToConfig(v));
        std::lock_guard lock(g_mx);
        g_values = std::move(v);
    }

    Values Get()
    {
        std::lock_guard lock(g_mx);
        return g_values;
    }

    namespace
    {
        // Apply's work. g_applyMx is held. `keyPicked`: the player picked the page's key just now, so one the host
        // does not hold is asked for again even when it is the key in force (a key refused when the game started).
        std::string ApplyLocked(Values values, bool keyPicked = false);
    }

    std::string Apply(Values values)
    {
        std::lock_guard apply(g_applyMx);
        return ApplyLocked(std::move(values));
    }

    bool ApplyJson(std::string_view json)
    {
        // Read, changed and applied under the one lock: another page's change in between is not undone.
        std::lock_guard apply(g_applyMx);
        auto            values = Get();
        if (!FromJson(json, values)) return false;
        ApplyLocked(std::move(values));
        return true;
    }

    namespace
    {
        std::string ApplyLocked(Values values, bool keyPicked)
        {
            SettingsCore::Clamp(values);
            Values before;
            {
                std::lock_guard lock(g_mx);
                before = g_values;
            }
            // The key first: one the host refuses is not taken, and neither the session nor the file gets it.
            const bool  keyAsked = values.pageKey != before.pageKey ||
                                   (keyPicked && SettingsPage::Ready() && SettingsPage::BoundKey() != values.pageKey);
            std::string keyError;
            if (keyAsked) {
                keyError = SettingsPage::Rebind(values.pageKey);
                if (!keyError.empty()) values.pageKey = before.pageKey;
            }
            {
                std::lock_guard lock(g_mx);
                g_values = values;
                if (keyAsked) g_keyError = keyError;
            }
            if (values.budget != before.budget || values.budgetMs != before.budgetMs) {
                MannequinRenderer::GetSingleton()->SetLiveBudget(values.budget, values.budgetMs);
            }
            if (NeuralDiffers(values, before)) NeuralStills::Reconfigure(ToConfig(values));

            // The file as it is now, with the values in it: someone may have edited it since the game started.
            std::string     why;
            const auto      path = FilePath();
            std::string     text;
            std::error_code ec;
            const bool      read = ReadFile(path, text);
            if (!read && std::filesystem::exists(path, ec)) {
                // Never written over what could not be read: the file's own text would be lost.
                why = "Phantasm.ini could not be read (" + Utf8(path) + ").";
            } else if (const std::string changed = SettingsCore::Rewrite(read ? text : std::string{}, values); changed != text) {
                if (WriteFile(path, changed)) {
                    SKSE::log::info("Phantasm: settings saved to {}", Utf8(path));
                } else {
                    why = "Phantasm.ini could not be written (" + Utf8(path) + ").";
                    // A write that failed part way: the file's text from before, if that will go in.
                    if (read) WriteFile(path, text);
                }
            }
            if (!why.empty()) SKSE::log::warn("Phantasm: settings changed but not saved: {}", why);
            std::lock_guard lock(g_mx);
            g_saveError = why;
            return why;
        }
    }

    void TakePageKey(std::uint32_t virtualKey)
    {
        // A new catch, or a key that can open the page: what the last one was refused for no longer stands.
        NoteKeyError({});
        if (virtualKey == 0) return;
        // The three keys whose scan code MapVirtualKey gives wrongly or as 0xE1xx.
        std::uint32_t key = 0;
        switch (virtualKey) {
        case VK_PAUSE: key = 0xC5; break;
        case VK_SNAPSHOT: key = 0xB7; break;
        case VK_NUMLOCK: key = 0x45; break;
        default: key = SettingsCore::KeyFromScan(::MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC_EX)); break;
        }
        if (!SettingsCore::KeyUsable(key)) {
            std::lock_guard lock(g_mx);
            g_keyError = "That key cannot open the page.";
            return;
        }
        std::lock_guard apply(g_applyMx);
        auto            values = Get();
        values.pageKey         = key;
        ApplyLocked(std::move(values), true);
    }

    std::string KeyName(std::uint32_t dik)
    {
        if (dik == 0) return "none";
        if (const auto fixed = SettingsCore::KeyFixedName(dik); !fixed.empty()) return std::string(fixed);
        // The layout's own name: bit 24 marks an extended key.
        wchar_t    wide[64]{};
        const LONG lParam = static_cast<LONG>(((dik & 0x7F) << 16) | ((dik & 0x80) ? 0x01000000 : 0));
        if (const int n = ::GetKeyNameTextW(lParam, wide, 64); n > 0) {
            char      narrow[256]{};
            const int m = ::WideCharToMultiByte(CP_UTF8, 0, wide, n, narrow, sizeof(narrow), nullptr, nullptr);
            if (m > 0) return std::string(narrow, static_cast<std::size_t>(m));
        }
        return std::format("key 0x{:02X}", dik);
    }

    void NoteKeyError(std::string why)
    {
        std::lock_guard lock(g_mx);
        g_keyError = std::move(why);
    }

    Status GetStatus()
    {
        const auto    n = NeuralStills::GetStatus();
        Status        s;
        std::uint32_t pageKey = 0;
        {
            std::lock_guard lock(g_mx);
            s.saveError = g_saveError;
            s.keyError  = g_keyError;
            pageKey     = g_values.pageKey;
        }
        s.pageReady = SettingsPage::Ready();
        // The key that opens the page, not the one asked for: the host may have refused that one.
        s.pageKeyName = KeyName(s.pageReady ? SettingsPage::BoundKey() : pageKey);
        s.restartNeeded = n.restartNeeded;
        s.library    = n.library;
        s.followed   = n.followed;
        s.ownLibrary = n.ownLibrary;
        s.version    = PLUGIN_VERSION;
        s.file       = Utf8(FilePath());
        if (!n.wanted) {
            s.neural     = Status::Neural::Off;
            s.neuralText = n.running ? "Neural rendering is off. It stays loaded until the game closes." : "Neural rendering is off.";
        } else if (n.failed) {
            s.neural     = Status::Neural::Failed;
            s.neuralText = "Neural rendering could not start, and stays off until the game is started again. " + n.failure;
        } else if (n.sourceMissing) {
            s.neural     = Status::Neural::Failed;
            s.neuralText = "Neural rendering has no library to run. " + n.failure;
        } else if (!n.modOn) {
            s.neural     = Status::Neural::Waiting;
            s.neuralText = (n.followed.empty() ? std::string("The mod it follows") : n.followed) +
                           " has its own neural rendering switched off, so Phantasm's is off too.";
        } else if (n.running) {
            s.neural     = Status::Neural::Running;
            s.neuralText = "Neural rendering is running.";
        } else {
            s.neural     = Status::Neural::Starting;
            s.neuralText = n.starting ? "Neural rendering is starting." : "Neural rendering starts with the next picture of a character.";
        }
        return s;
    }

    std::string ToJson()
    {
        const Values v = Get();
        const Status s = GetStatus();
        const nlohmann::json j = {
            { "values",
              {
                  { "budget", v.budget },
                  { "budgetMs", v.budgetMs },
                  { "enabled", v.enabled },
                  { "folder", v.folder },
                  { "unsigned", v.unsignedCopies },
                  { "follow", v.follow },
                  { "model", v.model },
                  { "intensity", v.intensity },
                  { "tone", v.tone },
                  { "structure", v.structure },
                  { "skin", v.skin },
                  { "passes", v.passes },
                  { "liveViews", v.live },
                  { "liveScale", v.liveScale },
                  { "prestart", v.prestart },
                  { "pageKey", v.pageKey },
              } },
            { "status",
              {
                  { "neural", NeuralName(s.neural) },
                  { "neuralText", s.neuralText },
                  { "library", s.library },
                  { "followed", s.followed },
                  { "ownLibrary", s.ownLibrary },
                  { "restartNeeded", s.restartNeeded },
                  { "saveError", s.saveError },
                  { "pageReady", s.pageReady },
                  { "pageKeyName", s.pageKeyName },
                  { "keyError", s.keyError },
                  { "version", s.version },
                  { "file", s.file },
              } },
        };
        // replace: a path the ANSI code page spelled is not always UTF-8.
        return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    bool FromJson(std::string_view json, Values& io)
    {
        const auto j = nlohmann::json::parse(json, nullptr, false);
        if (!j.is_object()) return false;
        const auto it = j.find("values");
        if (it == j.end() || !it->is_object()) return false;
        const auto& v = *it;
        auto flag = [&](const char* key, bool& out) {
            const auto f = v.find(key);
            if (f == v.end()) return;
            if (f->is_boolean()) out = f->get<bool>();
            else if (f->is_number()) out = f->get<double>() != 0.0;
        };
        auto number = [&](const char* key, auto& out) {
            const auto f = v.find(key);
            if (f != v.end() && f->is_number()) out = SettingsCore::detail::As<std::remove_reference_t<decltype(out)>>(f->get<double>());
        };
        auto text = [&](const char* key, std::string& out) {
            const auto f = v.find(key);
            if (f != v.end() && f->is_string()) out = f->get<std::string>();
        };
        text("budget", io.budget);
        number("budgetMs", io.budgetMs);
        flag("enabled", io.enabled);
        text("folder", io.folder);
        flag("unsigned", io.unsignedCopies);
        flag("follow", io.follow);
        number("model", io.model);
        number("intensity", io.intensity);
        number("tone", io.tone);
        number("structure", io.structure);
        number("skin", io.skin);
        number("passes", io.passes);
        flag("liveViews", io.live);
        number("liveScale", io.liveScale);
        flag("prestart", io.prestart);
        number("pageKey", io.pageKey);
        return true;
    }
}
