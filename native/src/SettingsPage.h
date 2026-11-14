#pragma once

// Settings page: Phantasm's settings in the game on Magelight UI. One fullscreen page
// (Data/Magelight/Phantasm/views/settings/index.html) that Phantasm.ini's [SettingsPage] Key opens and closes with
// the game paused. Without Magelight UI, or without the page's file (a copy of Phantasm bundled inside another mod
// ships none), there is no page and nothing else changes.
//
// The page's protocol: the page sends JSON text to the listener "phantasmSettings": {"op":"get"},
// {"op":"set","values":{...}} (Settings::FromJson's members) or {"op":"key","vk":N} (the key the player pressed for
// the page, a Windows virtual-key code). Each is answered by a call of the page's function "phantasmSettingsEvent"
// with Settings::ToJson's text.

#include <cstdint>
#include <string>

namespace Phantasm::SettingsPage
{
    /// Registers the page with Magelight UI when the host and the page's file are installed. kDataLoaded, once.
    void Initialize();

    /// Whether the page exists. Any thread.
    bool Ready();

    /// The key the host holds for the page: 0 for none, also when it refused the one asked for. Any thread.
    std::uint32_t BoundKey();

    /// Makes `key` (a DirectInput scan code; 0 for none) the page's key in place of the one it has. Returns "" when
    /// it is bound, or there is no page to bind it for; else why the host refused it, with the key before bound
    /// again. Settings::Apply's, which calls it one at a time.
    std::string Rebind(std::uint32_t key);
}
