#pragma once

// Phantasm Showcase - Settings: per install (every save and profile shares them), in
// My Games/<game>/SKSE/PhantasmShowcase/settings.json. Read once at kDataLoaded. Any thread.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Showcase::Settings
{
    struct Values
    {
        std::uint32_t            menuKey = 0x44;    // DirectInput scan code; F10
        double                   uiScale = 1.0;     // the page's size over its fit to the screen height, 0.7 to 1.6
        std::uint32_t            shotSize = 2160;   // a saved picture's long side in pixels, 512 to 4096
        std::vector<std::string> hiddenAnimFolders; // folder names (or paths under meshes) the animation browser hides
        bool                     showFps = true;    // each view's frame rate in its corner (the dock's FPS switch)
        bool                     freezeWorld = false;  // the game skips its 3D world render behind the open menu
        std::string              uiHost = "auto";   // auto (Magelight, else PrismaUI), magelight or prisma (HostCore)
    };

    void Load();
    Values Get();

    /// The dock's FPS switch: kept and written to the file.
    void SetShowFps(bool on);

    /// The docks' Freeze world switch: kept and written to the file.
    void SetFreezeWorld(bool on);

    /// The folder the settings file and the saved pictures live in (My Games/<game>/SKSE/PhantasmShowcase).
    std::filesystem::path Folder();

    /// The key's name for the page ("F10"), or its code in hex for a key without a short name.
    std::string KeyName(std::uint32_t code);

    /// A path as UTF-8 (path::string() converts through the ANSI code page and throws for a folder it cannot spell).
    std::string Utf8(const std::filesystem::path& p);
}
