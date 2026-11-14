#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "NeuralInProc.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <thread>

namespace Phantasm::NeuralInProc
{
    namespace
    {
        HMODULE                   g_module = nullptr;
        std::atomic<bool>         g_running{ false };
        PhantasmNR_LogFn          g_log      = nullptr;
        PhantasmNR_StartFn        g_start    = nullptr;
        PhantasmNR_ProcessFn      g_process  = nullptr;
        PhantasmNR_TrimFn         g_trim     = nullptr;
        PhantasmNR_LiveAttachFn   g_attach   = nullptr;
        PhantasmNR_LiveEvaluateFn g_evaluate = nullptr;
        PhantasmNR_LiveDetachFn   g_detach   = nullptr;
        PhantasmNR_ShutdownFn     g_shutdown = nullptr;

        void SetError(char* error, std::uint32_t cap, const char* text)
        {
            if (error && cap) {
                std::strncpy(error, text, cap - 1);
                error[cap - 1] = '\0';
            }
        }

        void Say(const char* fmt, ...)
        {
            if (!g_log) return;
            char    buf[1024];
            va_list a;
            va_start(a, fmt);
            std::vsnprintf(buf, sizeof(buf), fmt, a);
            va_end(a);
            g_log(buf);
        }

        // ---- which libraries NR loads, and how long each keeps the loader lock ------------------------------------

        // The libraries the watched thread has loaded, in order. OnLibrary writes them, on that thread alone and from
        // inside the loader: it takes no lock, allocates nothing and calls nothing. Any thread reads the names below
        // g_nameCount.
        constexpr int      kNames   = 192;
        constexpr int      kNameLen = 40;
        char               g_names[kNames][kNameLen]{};
        std::atomic<int>   g_nameCount{ 0 };
        std::atomic<DWORD> g_watched{ 0 };   // the thread whose loads are noted (the NR worker); 0 for none
        int                g_reported = 0;   // names already in the log (NR worker only)

        // ntdll's notification of a library load, as Windows documents it (LdrRegisterDllNotification).
        struct LoaderString
        {
            USHORT Length, MaximumLength;
            PWSTR  Buffer;
        };
        struct LoadedLibrary
        {
            ULONG               Flags;
            const LoaderString* FullDllName;
            const LoaderString* BaseDllName;
            PVOID               DllBase;
            ULONG               SizeOfImage;
        };
        constexpr ULONG kLibraryLoaded = 1;

        void CALLBACK OnLibrary(ULONG reason, const LoadedLibrary* data, PVOID)
        {
            if (reason != kLibraryLoaded || !data || !data->BaseDllName || !data->BaseDllName->Buffer) return;
            if (::GetCurrentThreadId() != g_watched.load(std::memory_order_relaxed)) return;
            const int at = g_nameCount.load(std::memory_order_relaxed);
            if (at >= kNames) return;
            const int chars = std::min<int>(data->BaseDllName->Length / sizeof(wchar_t), kNameLen - 1);
            for (int i = 0; i < chars; ++i) {
                const wchar_t c = data->BaseDllName->Buffer[i];
                g_names[at][i]  = c < 0x80 ? static_cast<char>(c) : '?';
            }
            g_names[at][chars] = '\0';
            g_nameCount.store(at + 1, std::memory_order_release);
        }

        const char* LastName()
        {
            const int n = g_nameCount.load(std::memory_order_acquire);
            return n > 0 ? g_names[n - 1] : "a library";
        }

        // The process's loader lock (PEB.LoaderLock): the thread that owns it is running a library's start.
        const RTL_CRITICAL_SECTION* LoaderLock()
        {
            const auto* peb = reinterpret_cast<const std::uint8_t*>(__readgsqword(0x60));
            return peb ? *reinterpret_cast<RTL_CRITICAL_SECTION* const*>(peb + 0x110) : nullptr;
        }

        // A hold this long is said at once, while it lasts: if the game stands still, the log already names the
        // library.
        constexpr auto kSlowHold = std::chrono::milliseconds(500);

        // Watches the loader lock for the thread `tid` until stop: how long that thread held it in all, and the
        // longest hold with the library loaded last when it began.
        struct LockWatch
        {
            std::atomic<bool> stop{ false };
            DWORD             tid = 0;
            double            totalMs = 0.0, longestMs = 0.0;
            char              longestAt[kNameLen]{};

            void Run()
            {
                using Clock = std::chrono::steady_clock;
                const auto*       lock = LoaderLock();
                bool              held = false, said = false;
                Clock::time_point since;
                char              at[kNameLen]{};
                const auto        ended = [&](Clock::time_point now) {
                    const double ms = std::chrono::duration<double, std::milli>(now - since).count();
                    totalMs += ms;
                    if (ms > longestMs) {
                        longestMs = ms;
                        std::memcpy(longestAt, at, sizeof(at));
                    }
                };
                while (!stop.load(std::memory_order_relaxed)) {
                    const bool mine = lock && static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(lock->OwningThread)) == tid;
                    const auto now  = Clock::now();
                    if (mine && !held) {
                        held  = true;
                        said  = false;
                        since = now;
                        strncpy_s(at, LastName(), _TRUNCATE);
                    } else if (!mine && held) {
                        held = false;
                        ended(now);
                    }
                    if (held && !said && now - since >= kSlowHold) {
                        said = true;
                        Say("in the game's process: '%s' (or a library loaded with it) has held the loader lock for %lld ms "
                            "while it starts, and the game waits for it",
                            LastName(), static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(now - since).count()));
                    }
                    ::Sleep(1);
                }
                if (held) ended(Clock::now());
            }
        };

        // The libraries loaded on the NR worker since the last report, as one line.
        void ReportLibraries(const char* when)
        {
            const int n = g_nameCount.load(std::memory_order_acquire);
            if (n <= g_reported) return;
            std::string line = std::string(when) + ": ";
            for (int i = g_reported; i < n; ++i) {
                if (i > g_reported) line += ", ";
                line += g_names[i];
            }
            if (n >= kNames) line += ", and more";
            g_reported = n;
            if (g_log) g_log(line.c_str());
        }

        const char* kNotLoaded = "NR's library for the game's process is not loaded";
    }

    bool Load(const std::filesystem::path& library, PhantasmNR_LogFn log, std::string& why)
    {
        if (g_running.load()) return true;
        g_log = log;
        if (!g_module) g_module = ::LoadLibraryW(library.c_str());
        if (!g_module) {
            why = "would not load (error " + std::to_string(::GetLastError()) + ")";
            return false;
        }
#define PNR_FN(field, type, name) field = reinterpret_cast<type>(::GetProcAddress(g_module, name));
        PNR_FN(g_start, PhantasmNR_StartFn, "PhantasmNR_Start")
        PNR_FN(g_process, PhantasmNR_ProcessFn, "PhantasmNR_Process")
        PNR_FN(g_trim, PhantasmNR_TrimFn, "PhantasmNR_Trim")
        PNR_FN(g_attach, PhantasmNR_LiveAttachFn, "PhantasmNR_LiveAttach")
        PNR_FN(g_evaluate, PhantasmNR_LiveEvaluateFn, "PhantasmNR_LiveEvaluate")
        PNR_FN(g_detach, PhantasmNR_LiveDetachFn, "PhantasmNR_LiveDetach")
        PNR_FN(g_shutdown, PhantasmNR_ShutdownFn, "PhantasmNR_Shutdown")
#undef PNR_FN
        if (!g_start || !g_process || !g_trim || !g_attach || !g_evaluate || !g_detach || !g_shutdown) {
            why = "lacks its entry points (not from the same build as Phantasm.dll)";
            return false;
        }
        // Without the notification the lock is still watched; a slow library is then not named.
        using RegisterFn = LONG(NTAPI*)(ULONG, void(CALLBACK*)(ULONG, const LoadedLibrary*, PVOID), PVOID, PVOID*);
        static PVOID s_cookie = nullptr;
        if (!s_cookie) {
            if (const auto reg = reinterpret_cast<RegisterFn>(::GetProcAddress(::GetModuleHandleW(L"ntdll.dll"), "LdrRegisterDllNotification"))) {
                reg(0, &OnLibrary, nullptr, &s_cookie);
            }
        }
        g_running = true;
        return true;
    }

    int Start(const wchar_t* libraryFile, const wchar_t* dataFolder, std::uint32_t luidLow, std::int32_t luidHigh,
              std::uint32_t flags, std::uint32_t* how, char* error, std::uint32_t errorCap)
    {
        if (!g_running.load()) {
            SetError(error, errorCap, kNotLoaded);
            return 0;
        }
        LockWatch watch;
        watch.tid = ::GetCurrentThreadId();
        g_watched = watch.tid;
        std::thread watcher([&watch] { watch.Run(); });
        const int   before = g_nameCount.load();
        const int   ok     = g_start(libraryFile, dataFolder, luidLow, luidHigh, flags | PhantasmNR_FlagNoLayers, g_log, how, error, errorCap);
        watch.stop = true;
        watcher.join();
        Say("in the game's process: this start loaded %d libraries; their starts held the loader lock for %.0f ms in all, "
            "%.0f ms at the longest ('%s')",
            g_nameCount.load() - before, watch.totalMs, watch.longestMs, watch.longestMs > 0.0 ? watch.longestAt : "none");
        ReportLibraries("libraries loaded by the start");
        return ok;
    }

    int Process(const std::uint8_t* rgbaIn, const float* depth, std::uint32_t width, std::uint32_t height,
                const PhantasmNR_Settings* settings, std::uint8_t* rgbaOut, char* error, std::uint32_t errorCap)
    {
        if (!g_running.load()) {
            SetError(error, errorCap, kNotLoaded);
            return 0;
        }
        const int ok = g_process(rgbaIn, depth, width, height, settings, rgbaOut, error, errorCap);
        ReportLibraries("in the game's process: a still loaded");
        return ok;
    }

    void Trim()
    {
        if (g_running.load()) g_trim();
    }

    int LiveAttach(std::uint32_t width, std::uint32_t height, void* colorIn, void* depthIn, void* colorOut, void* fence,
                   char* error, std::uint32_t errorCap)
    {
        if (!g_running.load()) {
            SetError(error, errorCap, kNotLoaded);
            return 0;
        }
        const int ok = g_attach(width, height, colorIn, depthIn, colorOut, fence, error, errorCap);
        ReportLibraries("in the game's process: a live view's attach loaded");
        return ok;
    }

    int LiveEvaluate(std::uint64_t waitValue, std::uint64_t signalValue, int reset, const PhantasmNR_Settings* settings,
                     char* error, std::uint32_t errorCap)
    {
        if (!g_running.load()) {
            SetError(error, errorCap, kNotLoaded);
            return 0;
        }
        return g_evaluate(waitValue, signalValue, reset, settings, error, errorCap);
    }

    void LiveDetach()
    {
        if (g_running.load()) g_detach();
    }

    void Stop()
    {
        if (g_running.load()) g_shutdown();
    }
}
