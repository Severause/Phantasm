#pragma once

// Neural source: finds the NR libraries a neural rendering mod installed (nvngx_dlssnr.dll, or the mod's own names
// for its copies). Files and loaded modules only, no game state: any thread (NeuralStills calls it on its worker).
// The rules are NeuralSourceCore's.

#include "NeuralSourceCore.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Phantasm::NeuralSource
{
    std::wstring Widen(std::string_view utf8);
    std::string  Narrow(std::wstring_view wide);

    /// A loaded module's file (an HMODULE; null: the game's exe). Empty when it cannot be read.
    std::filesystem::path ModuleFile(void* module);

    /// The copies of the library in reach, in NeuralSourceCore::Rank's order with no hints: the nvngx_dlssnr.dll a mod
    /// has loaded in this process, then those in the folders of `game`, read as the game sees them (a mod manager's
    /// virtual Data folder included). A few seconds at worst on a large plugins folder.
    std::vector<NeuralSourceCore::Found> Discover(const std::filesystem::path& game);

    /// Where a file really is on disk (under a mod manager the game's path to it is virtual). Empty when the file
    /// cannot be opened.
    std::filesystem::path RealPath(const std::filesystem::path& file);

    /// A private copy of a library file, under its own name, in a folder under the user's local application data
    /// (under `fallback` when that is unknown), made once per version of the file. A file of its own loads as a module
    /// of its own, so a mod that has the library loaded in the game and NR running inside the game never share one
    /// loaded module and its state (a hard link would not do: Windows gives it the module already loaded from the
    /// file's other name). The copy's file, with `note` saying what was made (empty when the copy was there already);
    /// or empty and `why`.
    std::filesystem::path PrivateCopy(const std::filesystem::path& file, const std::filesystem::path& fallback,
                                      std::string& why, std::string& note);

    /// Removes every private copy but the one in the folder `keep` (empty: all of them). One that is loaded stays.
    void PruneCopies(const std::filesystem::path& fallback, const std::filesystem::path& keep);
}
