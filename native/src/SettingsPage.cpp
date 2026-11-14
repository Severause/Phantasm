// Phantasm - SettingsPage: see SettingsPage.h.

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include "SettingsPage.h"

#include "AsciiText.h"
#include "MagelightUI_Mod.h"
#include "NeverDestroyed.h"
#include "Settings.h"

#include <atomic>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

namespace Phantasm::SettingsPage
{
    namespace
    {
        constexpr const char* kModId = "Phantasm";   // also the page's folder under Data/Magelight
        constexpr const char* kViewName = "settings";
        // Two names: a listener on a name makes the host define window.<name> in the page, and InteropCall on that
        // name would call it, so an answer on the request's name would come back here instead of reaching the page.
        constexpr const char* kListener = "phantasmSettings";            // page -> DLL
        constexpr const char* kPageFunction = "phantasmSettingsEvent";   // DLL -> page

        // The oldest Magelight the page is offered on, the Showcase's floor. Every call made here is older than
        // that, except the controller layer, which is gated on its own.
        constexpr std::uint32_t kMinMajor = 1, kMinMinor = 5, kMinPatch = 0;

        std::atomic<MAGELIGHT_API::ViewId> g_view{ 0 };       // the page's view; 0 until Initialize made it
        std::atomic<std::uint32_t>         g_boundKey{ 0 };   // the key the host holds for it

        /// The registration with the host. Never destroyed: its destructor would call into Magelight at process
        /// detach, and the host may enter the listener until then.
        MAGELIGHT_API::MagelightMod& Mod()
        {
            static auto& mod = NeverDestroyed<MAGELIGHT_API::MagelightMod>();
            return mod;
        }

        /// The page's file under the game's own folder, whatever working folder a launcher left. Empty when the
        /// game's path cannot be read.
        std::filesystem::path PagePath()
        {
            std::wstring buf(MAX_PATH, L'\0');
            for (;;) {
                const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
                if (n == 0) return {};
                if (n < buf.size()) {
                    buf.resize(n);
                    break;
                }
                buf.resize(buf.size() * 2);
            }
            return std::filesystem::path(buf).parent_path() / L"Data" / L"Magelight" / L"Phantasm" / L"views" /
                   L"settings" / L"index.html";
        }

        // u8string, not string(): that converts through the ANSI code page and throws for a folder it cannot spell.
        std::string Utf8(const std::filesystem::path& path)
        {
            const auto text = path.u8string();
            return std::string(reinterpret_cast<const char*>(text.c_str()), text.size());
        }

        /// Answers the page with the state. InteropCall hands the text to the page's function as a string argument,
        /// never as script source, so nothing in a path or a message can run as script. Any thread.
        void SendState(MAGELIGHT_API::ViewId view)
        {
            const auto state = Settings::ToJson();
            Mod().v4()->InteropCall(view, kPageFunction, state.c_str());
        }

        /// A request from the page. It runs on Magelight's render thread: the game's main thread inside Present, or
        /// the host's own page thread beside the game thread. So nothing here reads game state or posts an SKSE task;
        /// Settings and InteropCall are any-thread.
        void OnRequest(MAGELIGHT_API::ViewId view, const char* argument, void*)
        {
            // An exception must not cross the host's C callback.
            try {
                const std::string_view text = argument ? argument : "";
                const auto             request = nlohmann::json::parse(text, nullptr, false);
                std::string            op;
                if (request.is_object()) {
                    if (const auto it = request.find("op"); it != request.end() && it->is_string()) {
                        op = it->get<std::string>();
                    }
                }
                if (AsciiText::EqualsI(op, "set")) {
                    // Why the file could not be written, when it could not, reaches the page as the state's saveError.
                    if (!Settings::ApplyJson(text)) {
                        SKSE::log::warn("settings page: a set request without values changed nothing");
                    }
                } else if (AsciiText::EqualsI(op, "key")) {
                    // Why a key was not taken reaches the page as the state's keyError.
                    const auto it = request.find("vk");
                    if (it != request.end() && it->is_number_unsigned()) {
                        Settings::TakePageKey(it->get<std::uint32_t>());
                    } else {
                        SKSE::log::warn("settings page: a key request without a key changed nothing");
                    }
                } else if (!AsciiText::EqualsI(op, "get")) {
                    SKSE::log::warn("settings page: request ignored: {}", text.substr(0, 120));
                    return;
                }
                SendState(view);
            } catch (const std::exception& e) {
                SKSE::log::error("settings page: a request failed: {}", e.what());
            } catch (...) {
                SKSE::log::error("settings page: a request failed");
            }
        }

        /// The controller: the page has no highlight of its own, so the pad starts as a cursor (left stick, A clicks).
        /// The capability is 0 on VR (the laser) and where the player switched the layer off, and a host older than
        /// 1.6.0 has no SetViewGamepad to call.
        void OptInGamepad(const MAGELIGHT_API::MagelightApi4& v4, MAGELIGHT_API::ViewId view, MAGELIGHT_API::ModId mod)
        {
            if (v4.hostVersionNumber < 10600 || v4.QueryCapability("gamepad") != 1) return;
            MAGELIGHT_API::GamepadConfig pad{};
            pad.size              = sizeof(pad);
            pad.enabled           = true;
            pad.style             = MAGELIGHT_API::kGamepadStyleNavigation;
            pad.allowCursor       = true;
            pad.startInCursorMode = true;
            if (v4.SetViewGamepad(view, &pad) != MAGELIGHT_API::Result::Ok) {
                SKSE::log::warn("settings page: the controller layer refused the page: {}", v4.GetLastErrorMessage(mod));
            } else {
                SKSE::log::info("settings page: controller on");
            }
        }
    }

    void Initialize()
    {
        // The file first: a copy of Phantasm without its page then never registers with the host, and Magelight
        // fills a view whose file is missing with its developer page.
        const auto      page = PagePath();
        std::error_code ec;
        if (page.empty() || !std::filesystem::exists(page, ec)) {
            SKSE::log::info("settings page: its file is not installed "
                            "(Data/Magelight/Phantasm/views/settings/index.html): no page in the game");
            return;
        }

        auto& mod = Mod();
        if (!mod.acquire(kModId, kModId, kMinMajor, kMinMinor, kMinPatch)) {
            // v1 answers on every host: with it, the host is there and refused the registration.
            if (const auto* v1 = mod.v1()) {
                SKSE::log::info("settings page: Magelight UI {} did not take the page, which needs {}.{}.{} or newer "
                                "(Magelight.log says more): no page in the game",
                                v1->hostVersion ? v1->hostVersion : "?", kMinMajor, kMinMinor, kMinPatch);
            } else {
                SKSE::log::info("settings page: Magelight UI is not installed: no page in the game");
            }
            return;
        }
        const auto* v4 = mod.v4();

        const auto pageText = Utf8(page);
        const auto view =
            mod.createView(kViewName, pageText.c_str(), MAGELIGHT_API::Layer::Panel, { .fullscreen = true });
        if (!view) {
            SKSE::log::warn("settings page: Magelight UI refused the page: {}", v4->GetLastErrorMessage(mod.id()));
            return;
        }
        if (v4->QueryCapability("loadonshow") == 1) v4->SetViewLoadOnShow(view, true);
        OptInGamepad(*v4, view, mod.id());

        // Before the key: a page that opens with nobody listening shows nothing and saves nothing.
        if (v4->RegisterJSListenerEx(view, kListener, &OnRequest, nullptr) != MAGELIGHT_API::Result::Ok) {
            SKSE::log::warn("settings page: Magelight UI refused the page's listener, so it gets no key: {}",
                            v4->GetLastErrorMessage(mod.id()));
            return;
        }

        g_view.store(view);

        const char* host = v4->hostVersion ? v4->hostVersion : "?";
        const auto  key = Settings::Get().pageKey;
        if (key == 0) {
            SKSE::log::info("settings page: ready on Magelight UI {}, with no key (Phantasm.ini [SettingsPage] "
                            "Key = 0): nothing opens it",
                            host);
            return;
        }
        if (auto why = Rebind(key); !why.empty()) {
            SKSE::log::warn("settings page: no key opens the page: {}", why);
            Settings::NoteKeyError(std::move(why));
            return;
        }
        SKSE::log::info("settings page: ready on Magelight UI {}, opened by {}", host, Settings::KeyName(key));
    }

    bool Ready() { return g_view.load() != 0; }

    std::uint32_t BoundKey() { return g_boundKey.load(); }

    std::string Rebind(std::uint32_t key)
    {
        const auto view = g_view.load();
        if (!view) return {};
        auto&       mod = Mod();
        const auto* v4 = mod.v4();
        const auto  bind = [&](std::uint32_t code) {
            return v4->BindHotkey(view, code, MAGELIGHT_API::kHotkeyActionToggleUIModePaused) == MAGELIGHT_API::Result::Ok;
        };
        const auto before = g_boundKey.exchange(0);
        v4->BindHotkey(view, 0, MAGELIGHT_API::kHotkeyActionUnbind);   // scan code 0: every key of the view
        if (key == 0) return {};
        if (bind(key)) {
            g_boundKey.store(key);
            return {};
        }
        // Read before the next call: a later failure would replace it.
        const char* said = v4->GetLastErrorMessage(mod.id());
        std::string why = Settings::KeyName(key) + " could not be taken";
        why += said && *said ? std::string(": ") + said : std::string(": another mod may hold it");
        if (before != 0 && bind(before)) g_boundKey.store(before);
        return why;
    }
}
