#pragma once

// Phantasm Showcase - TurnCore: the angles of the wardrobe's turntable of stills and which one a drag shows.
// Engine-free.

#include <cmath>
#include <cstdint>

namespace Showcase::TurnCore
{
    inline constexpr std::uint32_t kMinFrames = 4;
    inline constexpr std::uint32_t kMaxFrames = 32;   // Phantasm's shot queue holds 32 before it answers "busy"

    constexpr std::uint32_t ClampFrames(std::uint32_t n) { return n < kMinFrames ? kMinFrames : n > kMaxFrames ? kMaxFrames : n; }

    /// Frame i's `turn` hint in degrees, (-180, 180]: frame 0 is the front and the frames go round the figure's left.
    inline float TurnFor(std::uint32_t i, std::uint32_t frames)
    {
        if (frames == 0) return 0.0f;
        float t = 360.0f * static_cast<float>(i % frames) / static_cast<float>(frames);
        if (t > 180.0f) t -= 360.0f;
        return t;
    }

    /// The frame a drag of `dx` CSS pixels from `start` shows, `pxPerFrame` a frame, wrapping both ways.
    inline std::uint32_t FrameAt(std::uint32_t start, double dx, double pxPerFrame, std::uint32_t frames)
    {
        if (frames == 0) return 0;
        if (!(pxPerFrame > 0.0) || !std::isfinite(dx)) return start % frames;
        const auto step = static_cast<long long>(std::floor(dx / pxPerFrame));
        const long long n = static_cast<long long>(frames);
        return static_cast<std::uint32_t>(((static_cast<long long>(start % frames) + step) % n + n) % n);
    }
}
