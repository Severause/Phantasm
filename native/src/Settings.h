#pragma once

// Settings: Phantasm.ini in force. The file is read at kDataLoaded; after that the settings pages (SettingsPanel, the
// SKSE Menu Framework one, and SettingsPage, Magelight's) read the values, change them while the game runs and have
// them written back into the file, whose comments stay (SettingsCore::Rewrite).
//
// What takes a change at once: the live views' budget, the Magelight page's key, and neural rendering's switch, its
// live views and their size, Follow, Prestart, and its style, strengths and passes. A change of where neural
// rendering's library comes from (Folder, Unsigned), made after it has found one, is written to the file and waits
// for the next start of the game: Status::restartNeeded says so.

#include "SettingsCore.h"

#include <string>
#include <string_view>

namespace Phantasm::Settings
{
    using Values = SettingsCore::Values;

    /// Reads Phantasm.ini and puts it in force. kDataLoaded, once.
    void Load();

    /// The values in force. Any thread.
    Values Get();

    /// Puts `values` in force, clamped, and writes them into Phantasm.ini. Any thread; it touches no game state.
    /// Returns "" or why the file could not be written: the values are in force for this session all the same.
    /// A page key Magelight UI refuses (another mod holds it) is not taken: the key before stays, and
    /// Status::keyError says why.
    std::string Apply(Values values);

    /// Apply, with the members a page's {"values": {...}} names laid over the values in force (FromJson). False,
    /// with nothing changed, for text that is not such an object. Any thread.
    bool ApplyJson(std::string_view json);

    /// Takes the key a page caught, a Windows virtual-key code, as the Magelight page's key (Apply). A key that
    /// cannot open the page changes nothing, and Status::keyError says so. 0, sent as a page begins to catch a key,
    /// only forgets the last refusal. Any thread.
    void TakePageKey(std::uint32_t virtualKey);

    /// A key's name for a page to show: "Scroll Lock", "F11", the layout's own name for a letter; "none" for 0.
    std::string KeyName(std::uint32_t dik);

    /// Records why the Magelight page's key could not be bound when the page was made. SettingsPage's.
    void NoteKeyError(std::string why);

    /// How things stand, for a page to show. Any thread.
    struct Status
    {
        enum class Neural
        {
            Off,        // the settings do not ask for it
            Waiting,    // the mod it follows has its own neural rendering switched off
            Starting,   // asked for and not yet running: starting now, or with the next picture
            Running,
            Failed,     // could not start: neuralText says why, and whether it is off for this session
        };
        Neural      neural = Neural::Off;
        std::string neuralText;         // one line a player can read: the state, and why for Waiting and Failed
        std::string library;            // the nvngx_dlssnr copy it runs from; "" before it started
        std::string followed;           // the mod whose settings it takes; "" for none
        bool        ownLibrary = true;  // PhantasmNRInProc.dll is installed (SKSE/Plugins/Phantasm)
        bool        restartNeeded = false;
        std::string saveError;          // why the last Apply could not write the file; "" when it did
        bool        pageReady = false;  // the Magelight page exists, so its key opens something
        std::string pageKeyName;        // KeyName of the key that opens the page: "none" when the host holds none
        std::string keyError;           // why the last key asked for was not taken; "" when it was
        std::string version;            // Phantasm's own (PLUGIN_VERSION)
        std::string file;               // Phantasm.ini's path, UTF-8
    };
    Status GetStatus();

    /// The Magelight page's protocol. ToJson: {"values": {...}, "status": {...}} of now. FromJson: `io` with the
    /// members the page's {"values": {...}} names laid over it; false for text that is not such an object.
    ///
    /// values: budget ("smooth" | "balanced" | "light"), budgetMs, enabled, folder, unsigned, follow, model,
    /// intensity, tone, structure, skin, passes, liveViews, liveScale, prestart, pageKey.
    /// status: neural ("off" | "waiting" | "starting" | "running" | "failed"), neuralText, library, followed,
    /// ownLibrary, restartNeeded, saveError, pageReady, pageKeyName, keyError, version, file.
    std::string ToJson();
    bool        FromJson(std::string_view json, Values& io);
}
