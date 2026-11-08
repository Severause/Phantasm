#pragma once

// Phantasm Showcase - HostCore: which UI host the page runs on, and where an under-drawn view lands on the render
// target. Engine-free (native/build.ps1 -Tests).
//
// On PrismaUI the page cannot show a texture, so the DLL draws each view's picture into the render target before
// PrismaUI composites its page over it (Underlay.cpp), in the box the page reports. The page reports boxes in its own
// device pixels; the render target may be another size, so a box is scaled by the target over the page.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string_view>

namespace Showcase::HostCore
{
    enum class Kind
    {
        None,
        Magelight,
        Prisma,
    };

    /// settings.json's uiHost: "auto" (Magelight, else PrismaUI), "magelight", "prisma". Anything else is auto.
    enum class Preference
    {
        Auto,
        Magelight,
        Prisma,
    };

    inline Preference ParsePreference(std::string_view s) noexcept
    {
        if (s == "magelight") return Preference::Magelight;
        if (s == "prisma") return Preference::Prisma;
        return Preference::Auto;
    }

    inline const char* PreferenceName(Preference p) noexcept
    {
        return p == Preference::Magelight ? "magelight" : p == Preference::Prisma ? "prisma" : "auto";
    }

    /// The host to run on. `magelight` takes Magelight or nothing; `prisma` takes PrismaUI when it is usable, else
    /// Magelight; `auto` takes Magelight, else PrismaUI. PrismaUI is flat only here (its VR pages are overlays, which a
    /// picture drawn into the game's frame would not reach).
    inline Kind Choose(Preference pref, bool magelightOk, bool prismaOk, bool vr) noexcept
    {
        const bool prisma = prismaOk && !vr;
        if (pref == Preference::Magelight) return magelightOk ? Kind::Magelight : Kind::None;
        if (pref == Preference::Prisma && prisma) return Kind::Prisma;
        if (magelightOk) return Kind::Magelight;
        if (prisma) return Kind::Prisma;
        return Kind::None;
    }

    struct Rect
    {
        float x = 0, y = 0, w = 0, h = 0;   // pixels, top left origin
    };

    /// A view's box: the element's whole rect (the picture fills it), the part of it the page shows (scrolled or
    /// clipped by an ancestor), and its corner radius, all in pixels.
    struct Box
    {
        Rect  rect;
        Rect  clip;
        float radius = 0;
    };

    inline bool Finite(const Rect& r) noexcept
    {
        return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.w) && std::isfinite(r.h);
    }

    /// A page box (the page pageW x pageH pixels) on a target of targetW x targetH: scaled per axis, the clip cut to
    /// the rect and the target. False when nothing of it shows (or a number is not finite, or a size not positive).
    inline bool ToTarget(const Box& page, float pageW, float pageH, float targetW, float targetH, Box& out) noexcept
    {
        const auto pos = [](float v) { return std::isfinite(v) && v > 0.0f; };
        if (!pos(pageW) || !pos(pageH) || !pos(targetW) || !pos(targetH) || !Finite(page.rect) || !Finite(page.clip) ||
            !pos(page.rect.w) || !pos(page.rect.h))
            return false;
        const float sx = targetW / pageW, sy = targetH / pageH;
        out.rect = { page.rect.x * sx, page.rect.y * sy, page.rect.w * sx, page.rect.h * sy };
        const float x0 = std::max({ 0.0f, page.clip.x * sx, out.rect.x });
        const float y0 = std::max({ 0.0f, page.clip.y * sy, out.rect.y });
        const float x1 = std::min({ targetW, (page.clip.x + page.clip.w) * sx, out.rect.x + out.rect.w });
        const float y1 = std::min({ targetH, (page.clip.y + page.clip.h) * sy, out.rect.y + out.rect.h });
        if (x1 - x0 < 1.0f || y1 - y0 < 1.0f) return false;
        out.clip = { x0, y0, x1 - x0, y1 - y0 };
        const float r = std::isfinite(page.radius) ? std::max(0.0f, page.radius) * std::min(sx, sy) : 0.0f;
        out.radius = std::min(r, std::min(out.rect.w, out.rect.h) * 0.5f);
        return true;
    }

    /// At most this many boxes are drawn (the page has a handful of view slots; more is a page bug).
    inline constexpr std::size_t kMaxBoxes = 8;
}
