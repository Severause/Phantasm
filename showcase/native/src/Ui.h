#pragma once

// Phantasm Showcase - Ui: the UI host, the one view and its key. The host is Magelight, or PrismaUI where Magelight is
// absent or settings.json's uiHost says prisma (HostCore::Choose). Without either the player is told once, at the main
// menu, and the menu never opens.

#include <cstdint>

namespace Showcase::Ui
{
    /// The oldest Magelight the page runs on.
    inline constexpr std::uint32_t kMinMajor = 1;
    inline constexpr std::uint32_t kMinMinor = 5;
    inline constexpr std::uint32_t kMinPatch = 0;

    /// Picks the host, creates the view and binds its key. Game thread, at kDataLoaded.
    void Initialize();

    /// Opens the menu as its key does. Nothing when it is open already or there is no view; a
    /// refusal (another mod holds UI mode) is told on the HUD. Game thread.
    void Open();

    /// Closes the menu (the page asked: Escape on PrismaUI). Game thread.
    void Close();

    /// True while the PrismaUI menu is open (always false on Magelight, which says so through UI mode). Game thread.
    bool IsOpen();

    /// True when the page runs on PrismaUI: the views are drawn under it (Underlay.h). Any thread.
    bool IsPrisma();

    /// "magelight", "prisma" or "" (no host). Any thread.
    const char* HostName();

    /// Magelight can stop the game's 3D world render behind the page (SetViewFreezeWorld, 0.31.0, flat only, and not
    /// turned off in Magelight.json). False on PrismaUI, which has no such call. Any thread.
    bool FreezeWorldAvailable();

    /// Gives Magelight the freezeWorld setting for the view. Magelight applies it only while the view holds UI mode
    /// with the game paused, and drops it itself on close, a load or another view taking UI mode. Any thread.
    void ApplyFreezeWorld();
}
