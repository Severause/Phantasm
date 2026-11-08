#pragma once

// Phantasm - LiveViewLifecycleCore: whether the live actor view (MannequinRenderer.cpp) collects its actor
// again, waits for the actor's 3D to come back, or ends, and whether a live view has its GPU bundle, engine-free
// (native/build.ps1 -Tests).
//
// Author: Severause

#include <chrono>
#include <cstdint>

namespace Phantasm::LiveViewLifecycleCore
{
    /// The view's start retries while the actor's 3D is missing: kStartAttempts tries kStartRetry apart, inside the
    /// 5 s the API tells a client waiting on Shown to allow.
    inline constexpr int                       kStartAttempts = 30;
    inline constexpr std::chrono::milliseconds kStartRetry{ 120 };
    /// How long a running view waits for its actor's 3D to come back before it ends: the start's own budget.
    inline constexpr std::chrono::milliseconds kAbsentBudget = kStartRetry * kStartAttempts;

    enum class Recollect
    {
        Collect,       ///< the 3D is loaded: collect it
        Wait,          ///< alive with no 3D for less than kAbsentBudget: keep the last frame, look again later
        EndGone,       ///< the actor is missing or dead
        EndNotLoaded,  ///< the 3D stayed away for kAbsentBudget
    };

    /// A pending recollect's verdict. `absentFor` is how long the 3D has been missing so far, zero on the first miss.
    inline Recollect DecideRecollect(bool actorFound, bool dead, bool loaded,
                                     std::chrono::milliseconds absentFor) noexcept
    {
        if (!actorFound || dead) return Recollect::EndGone;
        if (loaded) return Recollect::Collect;
        return absentFor >= kAbsentBudget ? Recollect::EndNotLoaded : Recollect::Wait;
    }

    /// The Ended reason for a view whose actor cannot be drawn: "missing" (no such actor), "dead", else
    /// "unrenderable".
    inline const char* GoneReason(bool actorFound, bool dead) noexcept
    {
        if (!actorFound) return "missing";
        return dead ? "dead" : "unrenderable";
    }

    /// Whether a view's end forgets the look kept for `formId`: a dead actor's, and a missing runtime actor's
    /// (0xFF......), whose FormID the engine hands to another actor later. A missing plugin actor may come back.
    inline bool ForgetsKeptLook(bool actorFound, bool dead, std::uint32_t formId) noexcept
    {
        if (actorFound) return dead;
        return (formId >> 24) == 0xFF;
    }

    /// The equip / 3D-swap debounce a recollect waits out (TendActorLive).
    inline constexpr std::chrono::milliseconds kRecollectDebounce{ 250 };

    /// How many recollects in a row may fail on one 3D root before a running view gives up on it. The 3D-swap watch
    /// re-arms the debounce after each, so this spans at least kAbsentBudget: the same room a start gives a 3D whose
    /// buffers are not uploaded yet.
    inline constexpr int kRecollectFailLimit = static_cast<int>(kAbsentBudget / kRecollectDebounce) + 1;

    /// A running view's failed recollects in a row, on one 3D root (any id the caller picks for it): a failure on
    /// another root, or Reset after a successful collect, starts the count again.
    class RecollectFailures
    {
    public:
        /// Notes a failure on `root`; true when the view gives up (kRecollectFailLimit in a row on it).
        bool NoteFailure(std::uintptr_t root) noexcept
        {
            if (root != m_root) {
                m_root  = root;
                m_count = 0;
            }
            return ++m_count >= kRecollectFailLimit;
        }

        void Reset() noexcept
        {
            m_root  = 0;
            m_count = 0;
        }

        int Count() const noexcept { return m_count; }

    private:
        std::uintptr_t m_root  = 0;
        int            m_count = 0;
    };

    /// A shader stage's one compile per process, on a worker thread: the live views' shaders (begun by the first
    /// live start) and the item view's particle shaders (FxStage).
    enum class ShaderBuild
    {
        NotStarted,
        Compiling,
        Ready,
        Failed,  ///< final for the process: the source is a constant
    };

    /// How long a device that refused the live views' GPU objects is left alone before they are built on it again.
    inline constexpr std::chrono::milliseconds kGpuRetryDelay{ 10000 };

    enum class GpuBundle
    {
        Ready,    ///< built on this device: draw
        Build,    ///< the shaders are compiled: build the device objects now
        Pending,  ///< the shaders are still compiling: draw nothing this frame
        Failed,   ///< unavailable: end the view, refuse a start
    };

    /// What a live frame or a live start gets from the GPU bundle. `bundleOnDevice`: one is already built on the
    /// engine's current device. `refusedOnDevice`: that device refused a live GPU object `sinceRefusal` ago.
    inline GpuBundle DecideGpuBundle(bool bundleOnDevice, ShaderBuild shaders, bool refusedOnDevice,
                                     std::chrono::milliseconds sinceRefusal) noexcept
    {
        if (shaders == ShaderBuild::Failed) return GpuBundle::Failed;
        if (refusedOnDevice && sinceRefusal < kGpuRetryDelay) return GpuBundle::Failed;
        if (bundleOnDevice) return GpuBundle::Ready;
        return shaders == ShaderBuild::Ready ? GpuBundle::Build : GpuBundle::Pending;
    }

    /// What a drawing view's frame gets: DecideGpuBundle's answer, except that inside a refusal window a view that
    /// already holds the bundle and its own targets at their size goes on drawing (`holdsTargets`), as its frame
    /// asks the device for nothing new. Only a view that needs a new device object is refused, so one refusal ends
    /// one view, not every view drawing.
    inline GpuBundle DecideGpuFrame(bool bundleOnDevice, ShaderBuild shaders, bool refusedOnDevice,
                                    std::chrono::milliseconds sinceRefusal, bool holdsTargets) noexcept
    {
        if (shaders != ShaderBuild::Failed && bundleOnDevice && holdsTargets) return GpuBundle::Ready;
        return DecideGpuBundle(bundleOnDevice, shaders, refusedOnDevice, sinceRefusal);
    }

    /// The item view's particle shaders: a second compile and a second set of device objects, kept apart from the
    /// bundle so that neither can refuse or hold back a live view. Off only drops the item view's particles.
    enum class FxStage
    {
        Ready,    ///< built on this device: particles draw
        Build,    ///< compiled: build the device objects now
        Pending,  ///< not compiled yet (the caller begins it): the frame draws without particles
        Off,      ///< failed to compile, or this device refused them: no particles here
    };

    /// `builtOnDevice` / `refusedOnDevice`: the bundle of the current device has them built, or was refused them.
    inline FxStage DecideFxStage(bool builtOnDevice, ShaderBuild fxShaders, bool refusedOnDevice) noexcept
    {
        if (builtOnDevice) return FxStage::Ready;
        if (fxShaders == ShaderBuild::Failed || refusedOnDevice) return FxStage::Off;
        return fxShaders == ShaderBuild::Ready ? FxStage::Build : FxStage::Pending;
    }
}
