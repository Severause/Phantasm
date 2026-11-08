#pragma once

// Phantasm Showcase - Lane: the only place the plugin equips, adds or removes items.
//
// An inventory change issued from inside SKSE's task drain can run another mod's hook while the drain holds the task
// queue's lock, and a hook that waits on workers blocked in AddTask on the same lock deadlocks. SKSE's UI-task queue
// is drained under its own lock, so the work runs there. Never call Run from a Magelight callback (the render thread,
// inside Present): post to the game thread with PostGameTask first.

#include <atomic>
#include <cstdint>
#include <functional>
#include <utility>

namespace Showcase::Lane
{
    /// Run fn on the game thread outside the SKSE task drain.
    inline void Run(std::function<void()> fn)
    {
        if (!fn) return;
        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            fn();
            return;
        }
        tasks->AddUITask(std::move(fn));
    }

    /// The save session: a load bumps it, and work posted in an older session does nothing.
    inline std::atomic<std::uint32_t> g_generation{ 0 };
    inline std::uint32_t Generation() { return g_generation.load(); }
}
