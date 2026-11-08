#pragma once

// Phantasm - LiveViewSizingCore: the size of the live views' supersampled offscreen, engine-free
// (native/build.ps1 -Tests). The live views size theirs through PlanImage (PrepareLiveFrame in MannequinRenderer.cpp).
//
// The projection takes its aspect from the offscreen, and the client's image is the whole offscreen stretched over its
// view, so the offscreen must keep the view's aspect: both sides scale by ONE factor.
//
// Author: Severause

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Phantasm::LiveViewSizingCore
{
    /// Largest offscreen side in texels. A view whose longer side passes kMaxSide / kSupersample supersamples less.
    inline constexpr std::uint32_t kMaxSide = 2048;
    /// Smallest offscreen side in texels.
    inline constexpr std::uint32_t kMinSide = 16;
    /// Offscreen texels per view pixel while neither side is capped.
    inline constexpr float kSupersample = 2.0f;
    /// A view narrower or shorter than this, in view pixels, is not drawn.
    inline constexpr float kMinDrawPx = 4.0f;

    /// A view side as the renderer stores it: at least kMinDrawPx, so a view the client sized 1-3 px still draws (and
    /// so answers: its first frame is what sends Shown) instead of waiting forever for a size it never gets. A side
    /// that is not finite is kMinDrawPx too.
    inline float ViewPx(float v) noexcept
    {
        return std::isfinite(v) && v > kMinDrawPx ? v : kMinDrawPx;
    }

    /// One live frame's plan. ok false = draw nothing this frame.
    struct Plan
    {
        bool          ok   = false;
        std::uint32_t offW = 0;
        std::uint32_t offH = 0;
    };

    /// The offscreen for a view of rectW x rectH pixels: min(kSupersample, kMaxSide / longer side) times each side,
    /// rounded and clamped to [kMinSide, kMaxSide]. The offscreen IS the client's image, stretched over the rect, so it
    /// is sized from the rect (view px) alone - never from the target bound when the carrier menu draws, whose aspect
    /// need not be the view's (a VR eye target is square).
    inline Plan PlanImage(float rectW, float rectH) noexcept
    {
        Plan p;
        if (!std::isfinite(rectW) || !std::isfinite(rectH) || rectW < kMinDrawPx || rectH < kMinDrawPx) return p;
        const float s = std::min(kSupersample, static_cast<float>(kMaxSide) / std::max(rectW, rectH));
        const auto side = [s](float v) {
            const float t = std::clamp(std::round(v * s), static_cast<float>(kMinSide), static_cast<float>(kMaxSide));
            return static_cast<std::uint32_t>(t);
        };
        p.ok = true;
        p.offW = side(rectW);
        p.offH = side(rectH);
        return p;
    }
}
