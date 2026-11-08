#pragma once

// Phantasm - ViewStatsCore: how often one live view drew in the last second, engine-free (native/build.ps1 -Tests).
// Every carrier frame a view is in one of four states: it drew a new picture, it had its turn but its picture could
// not have changed (skipped: a held pose, a still item), it had its turn but could not draw (stalled: a 3D rebuild, a
// start still loading, the GPU bundle not ready), or it waited for its turn (the budget, LiveViewBudgetCore). The rates
// are per second over a window of about one second; `fps` counts drawn and skipped, the frames the view was up to
// date, so a held pose does not read as slow and a stalled view does. QueryHint("stats") answers the last finished
// window.
//
// Author: Severause

#include <cstdint>

namespace Phantasm::ViewStatsCore
{
    inline constexpr double kWindowMs = 1000.0;

    struct Sample
    {
        double drawnFps   = 0.0;   // frames the view drew a new picture
        double skippedFps = 0.0;   // frames it had its turn with nothing to redraw
        double waitedFps  = 0.0;   // frames it waited for a turn
        double stalledFps = 0.0;   // frames it had its turn but could not draw
        double cpuMs      = 0.0;   // average CPU time of a drawn frame (ms)
        bool   valid      = false; // a full window has passed
        /// The rate the view kept up at: drawn plus skipped frames per second.
        double Fps() const noexcept { return drawnFps + skippedFps; }
    };

    struct Counter
    {
        double        windowStartMs = -1.0;
        std::uint32_t drawn         = 0;
        std::uint32_t skipped       = 0;
        std::uint32_t waited        = 0;
        std::uint32_t stalled       = 0;
        double        cpuMs         = 0.0;   // summed over the drawn frames
        double        lastNoteMs    = -1.0;
        Sample        last;

        enum class Frame
        {
            kDrawn,
            kSkipped,
            kWaited,
            kStalled,
        };

        /// Starts over: nothing counted, no window open, no rates (a view starting again).
        void Reset() noexcept { *this = Counter{}; }

        /// Counts one carrier frame at `nowMs` (a steady clock); `cpuMs` is the frame's draw time (drawn only). True
        /// when a window finished and `last` is new. The first frame only opens the window (a frame ends a span, so
        /// counting the opening one too would read 61 at a steady 60), and so does one after a gap of over two
        /// windows (the carrier did not run: the view was not drawn at all, which no rate here describes).
        bool Note(Frame f, double nowMs, double frameCpuMs = 0.0) noexcept
        {
            const bool gap = lastNoteMs >= 0.0 && nowMs - lastNoteMs > 2.0 * kWindowMs;
            lastNoteMs     = nowMs;
            if (windowStartMs < 0.0 || gap) {
                windowStartMs = nowMs;
                drawn = skipped = waited = stalled = 0;
                cpuMs = 0.0;
                return false;
            }
            switch (f) {
            case Frame::kDrawn:
                ++drawn;
                cpuMs += frameCpuMs;
                break;
            case Frame::kSkipped: ++skipped; break;
            case Frame::kStalled: ++stalled; break;
            default: ++waited; break;
            }
            const double span = nowMs - windowStartMs;
            if (span < kWindowMs) return false;
            const double perSec = 1000.0 / span;
            last.drawnFps   = drawn * perSec;
            last.skippedFps = skipped * perSec;
            last.waitedFps  = waited * perSec;
            last.stalledFps = stalled * perSec;
            last.cpuMs      = drawn ? cpuMs / drawn : 0.0;
            last.valid      = true;
            windowStartMs   = nowMs;
            drawn = skipped = waited = stalled = 0;
            cpuMs = 0.0;
            return true;
        }
    };
}
