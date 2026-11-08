// Phantasm Showcase - Ui: see Ui.h.

#include "PCH.h"

#include "Ui.h"

#include "Bridge.h"
#include "HostCore.h"
#include "MagelightUI_Mod.h"
#include "PrismaUI_API.h"
#include "Settings.h"
#include "Underlay.h"

namespace Showcase::Ui
{
    namespace
    {
        std::atomic<HostCore::Kind> g_host{ HostCore::Kind::None };

        // Magelight
        MAGELIGHT_API::MagelightMod g_mod;
        MAGELIGHT_API::ViewId       g_view = 0;

        // PrismaUI. g_prismaOpen is the game thread's.
        PRISMA_UI_API::IVPrismaUI1* g_prisma = nullptr;
        PrismaView                  g_prismaView = 0;
        bool                        g_prismaOpen = false;

        std::filesystem::path DataFolder()
        {
            wchar_t buf[MAX_PATH]{};
            ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
            return std::filesystem::path(buf).parent_path() / L"Data";
        }

        std::filesystem::path MagelightPage()
        {
            return DataFolder() / L"Magelight" / L"PhantasmShowcase" / L"views" / L"main" / L"index.html";
        }

        // PrismaUI loads pages from Data/PrismaUI/views; CreateView takes the path under it.
        constexpr const char* kPrismaPage = "PhantasmShowcase/index.html";
        std::filesystem::path PrismaPage()
        {
            return DataFolder() / L"PrismaUI" / L"views" / L"PhantasmShowcase" / L"index.html";
        }

        void TellPlayer(std::string text)
        {
            // Posted: at kDataLoaded the main menu is not up yet, and a message box shown now is lost.
            SKSE::GetTaskInterface()->AddTask([text = std::move(text)]() { RE::DebugMessageBox(text.c_str()); });
        }

        /// The controller (Magelight 1.6.0's gamepad layer): the page has no highlight of its own, so the pad starts as a
        /// cursor (left stick, A clicks). The capability is 0 on VR (the laser) and where the player switched the layer
        /// off, and an older host has no SetViewGamepad to call.
        void OptInGamepad()
        {
            const auto* v4 = g_mod.v4();
            if (v4->hostVersionNumber < 10600 || v4->QueryCapability("gamepad") != 1) return;
            MAGELIGHT_API::GamepadConfig pad{};
            pad.size = sizeof(pad);
            pad.enabled = true;
            pad.style = MAGELIGHT_API::kGamepadStyleNavigation;
            pad.allowCursor = true;
            pad.startInCursorMode = true;
            if (v4->SetViewGamepad(g_view, &pad) != MAGELIGHT_API::Result::Ok)
                SKSE::log::warn("the controller layer refused the view: {}", v4->GetLastErrorMessage(g_mod.id()));
            else
                SKSE::log::info("controller: on");
        }

        bool StartMagelight()
        {
            const auto page = MagelightPage();
            if (!std::filesystem::exists(page)) {
                SKSE::log::error("the page is not installed (Data/Magelight/PhantasmShowcase/views/main/index.html)");
                TellPlayer("Phantasm Showcase's menu files are missing. Please reinstall Phantasm Showcase.");
                return false;
            }
            // UTF-8, not path::string(): that converts through the ANSI code page and throws for a folder it cannot spell.
            const auto pageText = Settings::Utf8(page);
            g_view = g_mod.createView("main", pageText.c_str(), MAGELIGHT_API::Layer::Panel, { .fullscreen = true });
            if (!g_view) {
                SKSE::log::error("Magelight refused the view: {}", g_mod.v4()->GetLastErrorMessage(g_mod.id()));
                return false;
            }
            if (g_mod.v4()->QueryCapability("loadonshow") == 1) g_mod.v4()->SetViewLoadOnShow(g_view, true);
            OptInGamepad();
            ApplyFreezeWorld();
            Bridge::Attach(g_mod.v4(), g_view);
            // Every way the menu closes (its key, Escape, a load). Game thread.
            g_mod.onClose(g_view, [](MAGELIGHT_API::ViewId) { Bridge::OnMenuClosed(); });

            const auto key = Settings::Get().menuKey;
            const auto r = g_mod.v4()->BindHotkey(g_view, key, MAGELIGHT_API::kHotkeyActionToggleUIModePaused);
            if (r != MAGELIGHT_API::Result::Ok) {
                SKSE::log::warn("the menu key {} is taken by another mod ({}): {}", Settings::KeyName(key),
                                static_cast<int>(r), g_mod.v4()->GetLastErrorMessage(g_mod.id()));
            }
            SKSE::log::info("Magelight {} ready, menu on {}", g_mod.v4()->hostVersionNumber, Settings::KeyName(key));
            return true;
        }

        // ── PrismaUI ─────────────────────────────────────────────────────────────────────────────────────────────────

        void PrismaSend(const char* text)
        {
            if (g_prisma && g_prismaView) g_prisma->InteropCall(g_prismaView, "showcaseEvent", text);
        }

        /// The page's requests, on PrismaUI's own thread: copied and posted to the game thread.
        void OnPrismaRequest(const char* arg)
        {
            SKSE::GetTaskInterface()->AddTask([text = std::string(arg ? arg : "")]() { Bridge::HandleText(text); });
        }

        /// Whether the menu may open now: in play, with no game menu open (the underlay's backdrop would cover it, and
        /// the wardrobe would act on an actor under it), nothing pausing the game, and no other PrismaUI view focused
        /// (SkyrimNet's chat, say: opening would take its focus).
        bool MayOpen()
        {
            auto* ui = RE::UI::GetSingleton();
            if (!ui || !RE::PlayerCharacter::GetSingleton() || ui->GameIsPaused()) return false;
            for (const std::string_view name :
                 { RE::MainMenu::MENU_NAME, RE::LoadingMenu::MENU_NAME, RE::Console::MENU_NAME, RE::FaderMenu::MENU_NAME,
                   RE::InventoryMenu::MENU_NAME, RE::ContainerMenu::MENU_NAME, RE::BarterMenu::MENU_NAME,
                   RE::DialogueMenu::MENU_NAME, RE::MapMenu::MENU_NAME, RE::JournalMenu::MENU_NAME,
                   RE::CraftingMenu::MENU_NAME, RE::BookMenu::MENU_NAME, RE::LockpickingMenu::MENU_NAME,
                   RE::MagicMenu::MENU_NAME, RE::TweenMenu::MENU_NAME, RE::FavoritesMenu::MENU_NAME,
                   RE::SleepWaitMenu::MENU_NAME, RE::StatsMenu::MENU_NAME, RE::TrainingMenu::MENU_NAME,
                   RE::GiftMenu::MENU_NAME, RE::MessageBoxMenu::MENU_NAME }) {
                if (ui->IsMenuOpen(name)) return false;
            }
            return !(g_prisma && g_prisma->HasAnyActiveFocus());
        }

        /// The menu key on PrismaUI, which binds no keys for its clients: the key's press opens or closes the menu.
        class KeySink final : public RE::BSTEventSink<RE::InputEvent*>
        {
        public:
            RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* events, RE::BSTEventSource<RE::InputEvent*>*) override
            {
                const auto key = Settings::Get().menuKey;
                for (auto* e = events ? *events : nullptr; e; e = e->next) {
                    if (e->GetEventType() != RE::INPUT_EVENT_TYPE::kButton || e->GetDevice() != RE::INPUT_DEVICE::kKeyboard)
                        continue;
                    const auto* b = e->AsButtonEvent();
                    if (!b || !b->IsDown() || b->GetIDCode() != key) continue;
                    // Posted: the sink runs inside the engine's input dispatch, and opening the page sends menu messages.
                    SKSE::GetTaskInterface()->AddTask([]() {
                        if (g_prismaOpen) Close();
                        else if (MayOpen()) Open();
                    });
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };

        bool StartPrisma()
        {
            if (!std::filesystem::exists(PrismaPage())) {
                SKSE::log::error("the PrismaUI page is not installed (Data/PrismaUI/views/PhantasmShowcase/index.html)");
                TellPlayer("Phantasm Showcase's menu files are missing. Please reinstall Phantasm Showcase.");
                return false;
            }
            g_prismaView = g_prisma->CreateView(kPrismaPage, [](PrismaView) { SKSE::log::info("PrismaUI page loaded"); });
            if (!g_prismaView || !g_prisma->IsValid(g_prismaView)) {
                SKSE::log::error("PrismaUI refused the view");
                return false;
            }
            g_prisma->RegisterJSListener(g_prismaView, "showcase", &OnPrismaRequest);
            g_prisma->Hide(g_prismaView);
            Bridge::AttachSender(&PrismaSend);
            Underlay::Register();
            static KeySink s_sink;
            if (auto* input = RE::BSInputDeviceManager::GetSingleton()) input->AddEventSink(&s_sink);
            SKSE::log::info("PrismaUI ready, menu on {} (the views draw under the page)",
                            Settings::KeyName(Settings::Get().menuKey));
            return true;
        }
    }

    void Initialize()
    {
        const auto pref = HostCore::ParsePreference(Settings::Get().uiHost);
        const bool magelight = g_mod.acquire("PhantasmShowcase", "Phantasm Showcase", kMinMajor, kMinMinor, kMinPatch);
        g_prisma = static_cast<PRISMA_UI_API::IVPrismaUI1*>(PRISMA_UI_API::RequestPluginAPI());
        const bool vr = REL::Module::IsVR();
        const auto kind = HostCore::Choose(pref, magelight, g_prisma != nullptr, vr);
        SKSE::log::info("UI host: Magelight {}, PrismaUI {}, setting {} -> {}", magelight ? "yes" : "no",
                        g_prisma ? "yes" : "no", HostCore::PreferenceName(pref),
                        kind == HostCore::Kind::Magelight ? "Magelight" : kind == HostCore::Kind::Prisma ? "PrismaUI" : "none");
        if (kind == HostCore::Kind::None) {
            const bool tooOld = g_mod.v1() != nullptr;
            SKSE::log::error("no UI host - Phantasm Showcase has no interface this session");
            TellPlayer(tooOld ? "Phantasm Showcase needs Magelight UI 1.5.0 or newer. Please update Magelight UI."
                       : vr   ? "Phantasm Showcase needs Magelight UI 1.5.0 or newer on VR, which is not installed."
                              : "Phantasm Showcase needs Magelight UI 1.5.0 or newer, or PrismaUI, and neither is "
                                "installed.");
            return;
        }
        const bool ok = kind == HostCore::Kind::Magelight ? StartMagelight() : StartPrisma();
        if (ok) g_host.store(kind);
    }

    bool FreezeWorldAvailable()
    {
        const auto* v4 = g_mod.v4();
        return !IsPrisma() && g_view && v4 && v4->hostVersionNumber >= 3100 && v4->QueryCapability("freezeworld") == 1;
    }

    void ApplyFreezeWorld()
    {
        if (!FreezeWorldAvailable()) return;
        const bool on = Settings::Get().freezeWorld;
        const auto r = g_mod.v4()->SetViewFreezeWorld(g_view, on);
        if (r != MAGELIGHT_API::Result::Ok)
            SKSE::log::warn("Magelight refused the world freeze: {}", g_mod.v4()->GetLastErrorMessage(g_mod.id()));
        else
            SKSE::log::info("world freeze behind the menu: {}", on ? "on" : "off");
    }

    void Open()
    {
        if (g_host.load() == HostCore::Kind::Prisma) {
            if (g_prismaOpen || !g_prisma || !g_prismaView) return;
            g_prisma->Show(g_prismaView);
            if (!g_prisma->Focus(g_prismaView, true)) {
                g_prisma->Hide(g_prismaView);
                SKSE::log::info("the menu could not open: PrismaUI refused the focus");
                RE::SendHUDMessage::ShowHUDMessage("Phantasm Showcase can't open right now.");
                return;
            }
            g_prismaOpen = true;
            Underlay::Show(true);
            Bridge::Push({ { "event", "uiMode" }, { "on", true } });
            return;
        }
        auto* v4 = g_mod.v4();
        if (!g_view || !v4) {
            RE::SendHUDMessage::ShowHUDMessage("Phantasm Showcase's menu is not available (see PhantasmShowcase.log).");
            return;
        }
        if (v4->GetUIModeOwner() == g_mod.id()) return;
        std::uint32_t flags = MAGELIGHT_API::kUIModeFlagPause;
        // On VR the engine's text-entry gate makes OpenComposite raise its own keyboard over the page; Magelight's
        // keyboard follows the page's text fields instead.
        if (REL::Module::IsVR()) flags |= MAGELIGHT_API::kUIModeFlagNoTextEntry;
        v4->ShowView(g_view, true);
        if (v4->RequestUIMode(g_view, flags) != MAGELIGHT_API::Result::Ok) {
            v4->ShowView(g_view, false);
            SKSE::log::info("the menu could not open: {}", v4->GetLastErrorMessage(g_mod.id()));
            RE::SendHUDMessage::ShowHUDMessage("Phantasm Showcase can't open right now.");
        }
    }

    void Close()
    {
        if (g_host.load() == HostCore::Kind::Prisma) {
            if (!g_prismaOpen) return;
            g_prismaOpen = false;
            // The page hears it first, so it closes its views and stops reporting stages before the page hides.
            Bridge::Push({ { "event", "uiMode" }, { "on", false } });
            g_prisma->Unfocus(g_prismaView);
            g_prisma->Hide(g_prismaView);
            Underlay::Show(false);
            Bridge::OnMenuClosed();
            return;
        }
        if (auto* v4 = g_mod.v4(); v4 && v4->GetUIModeOwner() == g_mod.id()) v4->ReleaseUIMode(g_mod.id());
    }

    bool IsOpen() { return g_prismaOpen; }

    bool IsPrisma() { return g_host.load() == HostCore::Kind::Prisma; }

    const char* HostName()
    {
        switch (g_host.load()) {
        case HostCore::Kind::Magelight: return "magelight";
        case HostCore::Kind::Prisma: return "prisma";
        default: return "";
        }
    }
}
