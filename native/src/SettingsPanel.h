#pragma once

// Settings panel: Phantasm's settings (Settings.h) as a page of SKSE Menu Framework's Mod Control Panel, "Phantasm >
// Settings". The framework is optional: nothing is registered without it, or with one whose exports or ImGui build the
// panel was not made for.
//
// Threads: the framework draws the panel on its render thread, inside the frame's Present. The draw reads and changes
// the settings through Settings alone (Get, Apply, TakePageKey, GetStatus), which any thread may call, and reads the
// keyboard with GetAsyncKeyState while it catches the Magelight page's key; it touches no RE:: state and posts no
// SKSE task.

namespace Phantasm::SettingsPanel
{
    /// Registers the panel with SKSE Menu Framework when it is installed and usable. kDataLoaded, once.
    void Initialize();
}
