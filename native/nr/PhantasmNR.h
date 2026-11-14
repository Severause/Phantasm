#pragma once

// NR's work: NVIDIA DLSS NR (NGX feature 18) through NVIDIA's loader, the graphics driver's NGX core (loaded here and
// called by export name, Ngx.h: nothing of NVIDIA's is built in), on a Vulkan device of its own on the game's GPU
// (NR is a Vulkan feature). The loader takes only an NVIDIA-signed nvngx_dlssnr.dll; a copy it refuses is loaded and
// called directly when PhantasmNR_FlagUnsigned allows, and so is a library under any other file name, which the
// loader cannot find. PhantasmNR.cpp is built into PhantasmNRInProc.dll, which Phantasm.dll loads into the game
// (NeuralInProc) only when Phantasm.ini turns NR on.
//
// Threads: Start, Process, Trim, LiveAttach, LiveDetach and Shutdown come from one thread (Phantasm's NR worker);
// LiveEvaluate from another (the game's render thread), only between a LiveAttach that succeeded and the next
// LiveDetach.

#include <cstdint>

extern "C"
{
    struct PhantasmNR_Settings
    {
        std::uint32_t size;        // sizeof(PhantasmNR_Settings)
        std::int32_t  model;       // NR style 0..2
        float         intensity;   // 0..1
        float         tone;        // 0..1
        float         structure;   // 0..1
        float         skin;        // 0..1, applied through NR's own skin mask
        std::int32_t  passes;      // a still's evaluations of the same frame, 1..8 (a live frame runs one)
    };

    /// A line for Phantasm's log.
    typedef void (*PhantasmNR_LogFn)(const char* line);

    /// Start's flags.
    enum : std::uint32_t
    {
        PhantasmNR_FlagUnsigned = 1u,   // a copy NVIDIA's loader refuses may be loaded and called directly
        // No implicit Vulkan layer (another program's overlay or capture hook, a global ReShade) is loaded with NR's
        // Vulkan instance, and Start fails on a Vulkan loader too old to leave them out: a layer would start inside
        // the game.
        PhantasmNR_FlagNoLayers = 2u,
    };

    /// How Start got NR running (its how).
    enum : std::uint32_t
    {
        PhantasmNR_HowLoader       = 1u,   // NVIDIA's loader took the copy (signed)
        PhantasmNR_HowDirect       = 2u,   // the copy was loaded directly (unsigned)
        PhantasmNR_HowDirectSigned = 3u,   // an NVIDIA-signed copy under another file name was loaded directly
    };

    /// Starts Vulkan and NGX on the GPU with this adapter LUID (both 0: the first NVIDIA GPU) and gets NR running from
    /// the library libraryFile (a full path; any file name). dataFolder: a writable folder NGX may use. 1 on success,
    /// with how set; else 0 and why in error, and nothing is left running (Start may be called again with another file).
    typedef int (*PhantasmNR_StartFn)(const wchar_t* libraryFile, const wchar_t* dataFolder, std::uint32_t luidLow,
                                      std::int32_t luidHigh, std::uint32_t flags, PhantasmNR_LogFn log,
                                      std::uint32_t* how, char* error, std::uint32_t errorCap);

    /// Runs NR on an opaque picture: rgbaIn and rgbaOut are width x height RGBA8, sRGB, alpha ignored on the way in
    /// and copied on the way out; depth is width x height floats (0 near, 1 far) or null. Blocks until done. 1 on
    /// success.
    typedef int (*PhantasmNR_ProcessFn)(const std::uint8_t* rgbaIn, const float* depth, std::uint32_t width,
                                        std::uint32_t height, const PhantasmNR_Settings* settings,
                                        std::uint8_t* rgbaOut, char* error, std::uint32_t errorCap);

    /// Frees what NR keeps for the last still size (Process builds it again when needed).
    typedef void (*PhantasmNR_TrimFn)();

    /// Takes the game's D3D11 objects for a live view, as NT handles it does not close: colour in and out
    /// (R16G16B16A16_FLOAT) and depth (R32_FLOAT, 0 near), width x height each, and a shared D3D11 fence. 1 on success.
    typedef int (*PhantasmNR_LiveAttachFn)(std::uint32_t width, std::uint32_t height, void* colorIn, void* depthIn,
                                           void* colorOut, void* fence, char* error, std::uint32_t errorCap);

    /// Queues NR over the attached colour into the attached output on the GPU: it starts once the fence reaches
    /// waitValue and sets the fence to signalValue when done. Never waits on the GPU. reset: the frame does not follow
    /// the last one (no history). 1 when queued; 0 (nothing queued, the fence untouched) otherwise.
    typedef int (*PhantasmNR_LiveEvaluateFn)(std::uint64_t waitValue, std::uint64_t signalValue, int reset,
                                             const PhantasmNR_Settings* settings, char* error, std::uint32_t errorCap);

    /// Lets the live view's objects go, once the GPU is done with them.
    typedef void (*PhantasmNR_LiveDetachFn)();

    /// Ends NGX and Vulkan.
    typedef void (*PhantasmNR_ShutdownFn)();

    // The functions themselves (PhantasmNR.cpp), as the types above describe them.
    int  PhantasmNR_Start(const wchar_t* libraryFile, const wchar_t* dataFolder, std::uint32_t luidLow, std::int32_t luidHigh,
                          std::uint32_t flags, PhantasmNR_LogFn log, std::uint32_t* how, char* error, std::uint32_t errorCap);
    int  PhantasmNR_Process(const std::uint8_t* rgbaIn, const float* depth, std::uint32_t width, std::uint32_t height,
                            const PhantasmNR_Settings* settings, std::uint8_t* rgbaOut, char* error, std::uint32_t errorCap);
    void PhantasmNR_Trim();
    int  PhantasmNR_LiveAttach(std::uint32_t width, std::uint32_t height, void* colorIn, void* depthIn, void* colorOut,
                               void* fence, char* error, std::uint32_t errorCap);
    int  PhantasmNR_LiveEvaluate(std::uint64_t waitValue, std::uint64_t signalValue, int reset,
                                 const PhantasmNR_Settings* settings, char* error, std::uint32_t errorCap);
    void PhantasmNR_LiveDetach();
    void PhantasmNR_Shutdown();
}
