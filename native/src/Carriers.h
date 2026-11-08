#pragma once
// Phantasm - the carrier menus: invisible engine menus that exist for their PostDisplay, the engine's per-frame
// UI-render callback, where each live frame is drawn (MannequinRenderer.cpp). The actor carrier is open while a
// client holds a drawing actor view, during play as much as in a menu; the item carrier while the item view runs.
// Both load a shipped blank movie (Interface/phantasm.swf).

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include "MagelightUI_API.h"

#include <cstdint>
#include <string>
#include <unordered_set>

namespace Phantasm
{
    // The VR object tail every carrier needs: RE::IMenu is 0x30 bytes on SE/AE but 0x40 on VR, where the engine
    // reads unk30, unk34 and a BSFixedString menuName at 0x38 on every menu, which CommonLibSSE-NG's cross-runtime
    // build does not declare. At the SE size the VR engine reads garbage past the object (a CTD on open).
    template <class Self>
    class CarrierMenu : public RE::IMenu
    {
    public:
        // A carrier receives user events (movement, Toggle POV, Backspace, arrows...) and never hands one to its
        // blank movie, which froze the cursor. While a Magelight page holds UI mode the page already got them through
        // its host, so the carrier swallows them; otherwise (in play) they pass on, or the player could not move.
        RE::UI_MESSAGE_RESULTS ProcessMessage(RE::UIMessage& msg) override
        {
            if (msg.type != RE::UI_MESSAGE_TYPE::kUserEvent) return RE::IMenu::ProcessMessage(msg);
            static const auto* s_magelight = MAGELIGHT_API::RequestApi4();
            if (!s_magelight || !s_magelight->IsUIModeActive()) return RE::UI_MESSAGE_RESULTS::kPassOn;
            if (auto* d = static_cast<RE::BSUIMessageData*>(msg.data)) {
                static std::unordered_set<std::string> s_seen;   // one line per event name (the UI thread's)
                const std::string ev = d->fixedStr.c_str() ? d->fixedStr.c_str() : "";
                if (s_seen.insert(ev).second) SKSE::log::info("{}: swallowed user event '{}'", Self::MENU_NAME, ev);
            }
            return RE::UI_MESSAGE_RESULTS::kHandled;
        }

        REX::EnumSet<RE::UI_MENU_Unk09, std::uint32_t> vrUnk30{ RE::UI_MENU_Unk09::kNone };   // 30
        std::byte                                      vrUnk34{ 1 };                          // 34
        RE::BSFixedString                              vrMenuName{ Self::MENU_NAME };         // 38

#ifdef ENABLE_SKYRIM_VR
        // The VR setter of unk30. CommonLib's body writes through GetVRRuntimeData(), a zero VR offset from `this`,
        // so it would land on the vtable pointer: it must never run on these classes.
        void Unk_09(RE::UI_MENU_Unk09 a_unk) override { vrUnk30 = a_unk; }
#endif

    protected:
        CarrierMenu()
        {
            depthPriority = 0;   // under everything that draws
            if (auto* sm = RE::BSScaleformManager::GetSingleton()) {
                sm->LoadMovie(this, uiMovie, "phantasm");
            }
        }
    };

    // The actor views' carrier. Not an item menu (it may stay open during play, where the item-menu count would
    // change how the game treats the open menus) and it allows saving. kDontHideCursorWhenTopmost: under Magelight
    // (no engine menu of its own) the carrier is the topmost engine menu while a page shows a view, and without the
    // flag the engine stops updating the cursor.
    class ActorCarrierMenu : public CarrierMenu<ActorCarrierMenu>
    {
    public:
        static constexpr const char* MENU_NAME = "PhantasmActorCarrier";

        ActorCarrierMenu()
        {
            menuFlags.set(RE::UI_MENU_FLAGS::kCustomRendering,
                          RE::UI_MENU_FLAGS::kRendersUnderPauseMenu,
                          RE::UI_MENU_FLAGS::kAdvancesUnderPauseMenu,
                          RE::UI_MENU_FLAGS::kAllowSaving,
                          RE::UI_MENU_FLAGS::kDontHideCursorWhenTopmost);
        }

        static RE::IMenu* Create() { return new ActorCarrierMenu(); }

        void PostDisplay() override;   // MannequinRenderer.cpp
    };
    static_assert(sizeof(ActorCarrierMenu) == 0x40, "a carrier must match the VR IMenu size (0x40)");

    // The item view's carrier. It carries the item-menu flag: the Inventory3DManager the item view borrows is an item
    // menu's, and opening with the flag keeps UI::numItemMenus engine-managed (EngineItemMenuOpen discounts it).
    class ItemCarrierMenu : public CarrierMenu<ItemCarrierMenu>
    {
    public:
        static constexpr const char* MENU_NAME = "PhantasmItemCarrier";

        ItemCarrierMenu()
        {
            menuFlags.set(RE::UI_MENU_FLAGS::kInventoryItemMenu,
                          RE::UI_MENU_FLAGS::kCustomRendering,
                          RE::UI_MENU_FLAGS::kRendersUnderPauseMenu,
                          RE::UI_MENU_FLAGS::kAdvancesUnderPauseMenu,
                          RE::UI_MENU_FLAGS::kDontHideCursorWhenTopmost);
        }

        static RE::IMenu* Create() { return new ItemCarrierMenu(); }

        void PostDisplay() override;   // MannequinRenderer.cpp
    };
    static_assert(sizeof(ItemCarrierMenu) == 0x40, "a carrier must match the VR IMenu size (0x40)");
}
