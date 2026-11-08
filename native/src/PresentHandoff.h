#pragma once
// PresentHandoff - how code that can run inside a render phase posts game-thread work.
//
// Magelight calls its clients' JS listeners and other callbacks on its render thread (the game's main thread) from
// inside Present, and the engine runs the carriers' PostDisplay in its own render phase. On Skyrim VR, SKSE also runs
// its task queues on game job threads and holds a queue's lock until it is empty, so an AddTask there can wait forever
// on a job that waits for the frame to finish.
//
// So a post made inside a render-thread scope goes to a FIFO that one pump thread hands to SKSE; anywhere else it
// posts directly. The carriers' PostDisplay enters the scope; an API call, which may come from inside Magelight's
// Present without Phantasm's scope, always posts through the pump (PumpTask). Pumped posts of one lane keep their
// order but can land after a direct post another thread makes meanwhile, so a pumped job captures what it needs when
// it is posted rather than reading it when it runs.

#include <chrono>
#include <functional>
#include <utility>
#include <SKSE/SKSE.h>

#include "NeverDestroyed.h"
#include "PresentHandoffCore.h"

namespace Phantasm::PresentHandoff
{
    using Scope = PresentHandoffCore::Scope;

    /// True while the calling thread is inside a render-thread callback scope.
    inline bool Inside() noexcept { return PresentHandoffCore::Inside(); }

    /// The scope without RAII, for a trampoline that leaves it in a __finally: an SEH fault unwinds past a
    /// destructor without running it.
    inline void Enter() noexcept { ++PresentHandoffCore::Depth(); }
    inline void Leave() noexcept { --PresentHandoffCore::Depth(); }

    namespace detail
    {
        inline PresentHandoffCore::Pump& Pump()
        {
            // The gather lets one frame's posts go out as one task.
            static auto& pump = NeverDestroyed<PresentHandoffCore::Pump>(
                [](PresentHandoffCore::Job&& job) {
                    auto* ti = SKSE::GetTaskInterface();
                    if (!ti) {
                        SKSE::log::error("PresentHandoff: no SKSE task interface - a UI post was dropped");
                        return;
                    }
                    if (job.lane == PresentHandoffCore::Lane::UITask) ti->AddUITask(std::move(job.fn));
                    else ti->AddTask(std::move(job.fn));
                },
                std::chrono::milliseconds(2));
            return pump;
        }
    }

    /// Start the pump thread before the first frame (at plugin load), so no thread is created inside Present.
    inline void Start() { detail::Pump().Start(); }

    /// SKSE AddTask; through the pump inside a render-thread scope.
    inline void AddTask(std::function<void()> fn)
    {
        if (!fn) return;
        if (Inside()) {
            detail::Pump().Push({ PresentHandoffCore::Lane::Task, std::move(fn) });
            return;
        }
        SKSE::GetTaskInterface()->AddTask(std::move(fn));
    }

    /// SKSE AddUITask; through the pump inside a render-thread scope.
    inline void AddUITask(std::function<void()> fn)
    {
        if (!fn) return;
        if (Inside()) {
            detail::Pump().Push({ PresentHandoffCore::Lane::UITask, std::move(fn) });
            return;
        }
        SKSE::GetTaskInterface()->AddUITask(std::move(fn));
    }

    /// SKSE AddTask through the pump whatever the scope: for a caller that may be on the render thread but holds no
    /// scope (every API call).
    inline void PumpTask(std::function<void()> fn)
    {
        if (!fn) return;
        detail::Pump().Push({ PresentHandoffCore::Lane::Task, std::move(fn) });
    }
}
