#pragma once

// Neural in-process: PhantasmNR.h's functions run inside the game's own process, from PhantasmNRInProc.dll in
// Phantasm's own folder. No game state: Load, Start, Process, Trim, LiveAttach, LiveDetach and Stop come from the NR
// worker thread (NeuralStills), LiveEvaluate from the render thread (NeuralLive).
//
// NR's start loads some seventy libraries, and each starts inside the Windows loader lock, held by the thread that
// loads it. Other mods' code on the game's presenting thread asks for that lock (a module list taken after every
// library load, as the Skyrim Upscaler does), so whatever a library takes to start is how long the game stands
// still. Three things keep that to a few milliseconds here:
//   - the Vulkan instance is made with no implicit layer (PhantasmNR_FlagNoLayers): other programs' overlays and
//     capture hooks, and a global ReShade, would otherwise start inside the game, and one that hooks the game from
//     there can hold the lock for many seconds;
//   - the library NR runs from is a private copy (NeuralSource::PrivateCopy), a module of its own;
//   - NeuralStills starts NR as the game's data finishes loading, and nothing is loaded after the start.
// Start watches the lock while it runs and says in the log which library held it, so a slow one is named.

#include "../nr/PhantasmNR.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace Phantasm::NeuralInProc
{
    /// Loads PhantasmNRInProc.dll. log takes NR's own lines (any thread). False and why when it cannot.
    bool Load(const std::filesystem::path& library, PhantasmNR_LogFn log, std::string& why);

    /// PhantasmNR.h's functions. Start adds PhantasmNR_FlagNoLayers to flags; libraryFile is the private copy.
    int  Start(const wchar_t* libraryFile, const wchar_t* dataFolder, std::uint32_t luidLow, std::int32_t luidHigh,
               std::uint32_t flags, std::uint32_t* how, char* error, std::uint32_t errorCap);
    int  Process(const std::uint8_t* rgbaIn, const float* depth, std::uint32_t width, std::uint32_t height,
                 const PhantasmNR_Settings* settings, std::uint8_t* rgbaOut, char* error, std::uint32_t errorCap);
    void Trim();
    /// The handles stay the caller's.
    int  LiveAttach(std::uint32_t width, std::uint32_t height, void* colorIn, void* depthIn, void* colorOut, void* fence,
                    char* error, std::uint32_t errorCap);
    /// Render thread.
    int  LiveEvaluate(std::uint64_t waitValue, std::uint64_t signalValue, int reset, const PhantasmNR_Settings* settings,
                      char* error, std::uint32_t errorCap);
    void LiveDetach();

    /// Ends NGX and Vulkan. The libraries stay loaded: unloading them would start the loader lock's work again.
    void Stop();
}
