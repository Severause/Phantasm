#pragma once
// MutationLane — how Phantasm's own tasks rebuild a LIVE actor's 3D or settle
// the player's world.
//
// Why: an equip or a 3D rebuild issued from inside SKSE's task drain (AddTask,
// which is how the API's work reaches the game thread) runs RaceMenu VR's (skeevr.dll)
// armor-change hook while the drain holds the task-queue lock, and that hook
// waits on SKEE workers blocked in AddTask on the same lock: a deadlock
// (expired6978/SKSE64Plugins #63). Deferring with AddTask does not help: a
// deferred Update3DModel hangs the same way. AddUITask feeds SKSE's second
// queue, drained from the UI update under its own lock (on the main thread in
// SE during play; on a game job thread in VR, and during a loading screen on
// AE), so the task-queue lock is free while SKEE's hook runs.
//
// Contract: chain continuations that must follow a mutation (reclaims,
// bookkeeping, the next drain step) INSIDE the job, never through AddTask
// behind it: ordering between the two queues is undefined.

#include <functional>
#include <utility>
#include <SKSE/SKSE.h>

#include "PresentHandoff.h"

namespace Phantasm::MutationLane
{
    /// Run fn on the game thread OUTSIDE the SKSE task-queue drain. Any
    /// thread may call (inside a render-thread scope the post goes through
    /// PresentHandoff's pump); the first job logs its thread.
    inline void Run(std::function<void()> fn)
    {
        if (!fn) return;
        if (!SKSE::GetTaskInterface()) { fn(); return; }   // pre-init: nothing to marshal through yet
        PresentHandoff::AddUITask([fn = std::move(fn)]() {
            static bool s_announced = false;
            if (!s_announced) {
                s_announced = true;
                SKSE::log::info("MutationLane: first job running on thread {} (SKSE UI-task queue — outside the task drain)",
                                REX::W32::GetCurrentThreadId());
            }
            fn();
        });
    }
}
