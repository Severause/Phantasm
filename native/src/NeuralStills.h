#pragma once

// Neural stills: NVIDIA DLSS NR run on an actor's Cutout and Portrait stills (and, through NeuralLive, on live actor
// views), when Phantasm.ini [NeuralRendering] Enabled is on and Folder names a folder holding nvngx_dlssnr.dll, a
// library file, or is "auto" (the copy a neural rendering mod installed in the game's folders; NeuralSourceCore). The
// work runs inside the game, from PhantasmNRInProc.dll in Phantasm's own folder (NeuralInProc, which says what keeps
// its start from holding the game), started as the game's data finishes loading, or when a settings page turns it on,
// and driven from one worker thread. A still NR cannot take is answered as drawn.

#include "MannequinRenderer.h"
#include "NeuralSourceCore.h"
#include "NeuralStillCore.h"

#include "../nr/PhantasmNR.h"

#include <functional>
#include <string>

namespace Phantasm::NeuralStills
{
    struct Config
    {
        bool                      enabled = true;   // [NeuralRendering] Enabled: off keeps the folder for later
        std::string               folder;   // UTF-8, as typed: empty = off, "auto", or a folder (NeuralSourceCore)
        bool                      unsignedCopies = true;   // a copy NVIDIA's loader refuses is loaded directly
        NeuralStillCore::Settings settings;
        bool                      live      = false;   // actor live views too (NeuralLive)
        float                     liveScale = 0.5f;    // NR's size against a live view's offscreen, 0.25..1
        // NR that could not start at kDataLoaded (the followed mod had its own NR off) starts as soon as that mod
        // turns NR on, not at the first picture after that.
        bool                      prestart  = true;
        // NR's switch, style and strengths are those the player set in the mod the library belongs to
        // (NeuralSettingsCore), read again as that mod saves them; `settings` stands where the mod sets nothing.
        bool                      follow    = true;
    };

    /// Phantasm.ini's choice, at kDataLoaded. With NR turned on the worker starts it at once.
    void Configure(Config config);

    /// A changed choice while the game runs (the settings pages). Any thread: the worker takes it up ahead of its
    /// jobs. The switch, live views and their size, the settings and Follow hold at once; turning NR on starts it as
    /// a followed mod turning its own on does. Where the library comes from (folder, unsignedCopies) is settled once
    /// NR has looked for it: a change after that waits for the next start of the game (Status::restartNeeded).
    void Reconfigure(Config config);

    /// True while NR runs: Enabled and Folder turn it on, the followed mod has not turned its own NR off, a library
    /// was found where Folder looks, and its start has not failed for good. Any thread.
    bool Enabled();

    /// True once NR's start failed for good: off for the session. Any thread.
    bool StartFailed();

    /// Whether actor live views go through NR too, and the size NR works at against a view's picture. Any thread.
    bool  LiveOn();
    float LiveScale();

    /// How NR stands, for a settings page. Any thread.
    struct Status
    {
        bool        wanted  = false;   // the settings ask for NR
        bool        modOn   = true;    // the followed mod has its own NR on (true with none followed)
        bool        running = false;   // NR has started
        bool        starting = false;  // a start is under way or queued; false with NR wanted and not running: it
                                       // starts with the next shot or view
        bool        failed  = false;   // it could not start: off for the session
        bool        sourceMissing = false;   // no library where Folder looks: looked for again when Folder changes
        bool        restartNeeded = false;
        bool        ownLibrary    = true;   // PhantasmNRInProc.dll is installed
        std::string failure;           // why, when failed or sourceMissing
        std::string library;           // the copy it started from
        std::string followed;          // the mod whose settings it takes, "" for none
    };
    Status GetStatus();

    /// Runs NR on the shot on the NR worker and calls done there with the result, or with the shot unchanged when NR
    /// fails. done must not touch game state: post to the game thread from it.
    void Submit(MannequinRenderer::PortraitShot shot, std::function<void(MannequinRenderer::PortraitShot)> done);

    /// NR's live entry points.
    struct LiveApi
    {
        PhantasmNR_LiveAttachFn   attach   = nullptr;
        PhantasmNR_LiveEvaluateFn evaluate = nullptr;
        PhantasmNR_LiveDetachFn   detach   = nullptr;
    };

    /// The live entry points once NR has started, else null. Any thread.
    const LiveApi* Live();

    /// Runs fn on the NR worker after starting NR if it has not started; fn hears whether it runs.
    void Post(std::function<void(bool started)> fn);

    /// The settings in force, as NR takes them: the followed mod's over Phantasm.ini's. Any thread.
    PhantasmNR_Settings CurrentSettings();
}
