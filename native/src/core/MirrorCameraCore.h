#pragma once

// Phantasm - MirrorCameraCore: the orbit camera of the live views (the actor view and the item view,
// MannequinRenderer.cpp) zooms toward the point under the pointer, engine-free (native/build.ps1 -Tests).
//
// Author: Severause

#include <algorithm>
#include <cmath>

namespace Phantasm::MirrorCameraCore
{
    struct V3
    {
        float x = 0, y = 0, z = 0;
    };

    inline V3    Add(V3 a, V3 b) noexcept { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
    inline V3    Sub(V3 a, V3 b) noexcept { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
    inline V3    Scale(V3 a, float s) noexcept { return { a.x * s, a.y * s, a.z * s }; }
    inline float Dot(V3 a, V3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
    inline float Length(V3 a) noexcept { return std::sqrt(Dot(a, a)); }

    inline constexpr float kZoomMin = 0.45f;
    inline constexpr float kZoomMax = 2.6f;

    /// The zoom multiplier after a wheel step of dz (the client's Zoom delta, a wheel's deltaY), clamped to the
    /// views' range. A dz that is not finite leaves the zoom as it was: a NaN would pass std::clamp and stay in the
    /// camera until the view restarts.
    inline float NextZoom(float zoom, float dz) noexcept
    {
        if (!std::isfinite(dz)) return zoom;
        const float z = std::clamp(zoom * std::pow(1.0015f, dz), kZoomMin, kZoomMax);
        return std::isfinite(z) ? z : zoom;
    }

    /// The orbit after a drag of (dx, dy) view pixels: one degree of yaw a pixel, wrapped to [-180, 180] so a long
    /// drag cannot grow it past float precision, and half a degree of pitch, clamped to +-80. A drag to the right
    /// turns the figure's front to the right with the pointer (the camera orbits clockwise seen from above, the yaw
    /// falls); a drag down tilts the camera up over the figure. False, and the orbit untouched, when an input is not
    /// finite (a NaN passes std::clamp).
    inline bool Drag(float& yawDeg, float& pitchDeg, float dx, float dy) noexcept
    {
        if (!std::isfinite(dx) || !std::isfinite(dy)) return false;
        const float yaw   = std::remainder(yawDeg - dx, 360.0f);
        const float pitch = std::clamp(pitchDeg + dy * 0.5f, -80.0f, 80.0f);
        if (!std::isfinite(yaw) || !std::isfinite(pitch)) return false;
        yawDeg   = yaw;
        pitchDeg = pitch;
        return true;
    }

    /// The camera's turn in degrees from yaw `fromDeg` to `toDeg`, the short way round: the yaw wraps at +-180, so a
    /// plain difference across the wrap reads as a full turn.
    inline float YawTurnDeg(float fromDeg, float toDeg) noexcept
    {
        return std::remainder(toDeg - fromDeg, 360.0f);
    }

    /// The orbit yaw (degrees) that faces a figure whose heading is `headingRad` (Skyrim's angle Z, radians): the
    /// camera sits at (sin a, -cos a) from the target and the figure faces (sin h, cos h), so a = 180 - h.
    inline float FacingYawDeg(float headingRad) noexcept
    {
        return 180.0f - headingRad * (180.0f / 3.14159265358979f);
    }

    /// A pointer coordinate of the view (0..1): clamped, and the middle when it is not finite.
    inline float PointerCoord(float t) noexcept
    {
        return std::isfinite(t) ? std::clamp(t, 0.0f, 1.0f) : 0.5f;
    }

    /// The camera's basis for an orbit at yaw / pitch (degrees, Skyrim's Z up): the camera sits at
    /// target + (sin yaw cos pitch, -cos yaw cos pitch, sin pitch) * distance and looks at the target, as the live
    /// views' unmirrored view matrix builds it (MannequinRenderer.cpp's LiveViewMatrix: right = forward x up, screen
    /// up = right x forward), so looking north the world's east is on the picture's right, as in the game.
    struct Basis
    {
        V3 forward, right, up;
    };

    inline Basis OrbitBasis(float yawDeg, float pitchDeg) noexcept
    {
        constexpr float kDeg = 3.14159265358979f / 180.0f;
        const float     y    = yawDeg * kDeg;
        const float     p    = std::clamp(pitchDeg, -80.0f, 80.0f) * kDeg;
        const V3        f{ -std::sin(y) * std::cos(p), std::cos(y) * std::cos(p), -std::sin(p) };
        // f x (0,0,1), normalised (|f.xy| = cos p > 0 inside the pitch clamp).
        const float h = std::sqrt(f.x * f.x + f.y * f.y);
        const V3    r{ f.y / h, -f.x / h, 0.0f };
        const V3    u{ r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x };
        return { f, r, u };
    }

    /// The winding of a front face on the live views' image. A NIF triangle's front runs counterclockwise seen
    /// from the side its normals point to (the right-hand rule in the game's right-handed world); the views draw
    /// the world unmirrored (OrbitBasis), so it runs counterclockwise on the image too, which D3D's default
    /// (FrontCounterClockwise FALSE) would take for a back face.
    inline constexpr bool kFrontCounterClockwise = true;

    /// Where the pointer at (u, v) of the view (0..1 from the top left) lands on the plane through the target
    /// facing the camera, as an offset from the target, for a vertical field of view of fovYDeg at that distance.
    /// A u or v that is not finite is the view's middle (PointerCoord).
    inline V3 PointerOffset(const Basis& b, float u, float v, float fovYDeg, float aspect, float distance) noexcept
    {
        const float tanH = std::tan(fovYDeg * 0.5f * 3.14159265358979f / 180.0f);
        const float sx   = (2.0f * PointerCoord(u) - 1.0f) * tanH * aspect * distance;
        const float sy   = (1.0f - 2.0f * PointerCoord(v)) * tanH * distance;
        return Add(Scale(b.right, sx), Scale(b.up, sy));
    }

    /// The target's pan from the figure's centre after a zoom from zoom0 to zoom1. Zooming in moves the target
    /// toward the point under the pointer (pointerOffset, PointerOffset at zoom0) so that point stays where it is on
    /// screen; zooming out eases the pan back so the default framing (zoom 1, or the far limit from beyond it) is
    /// centred again. The pan never exceeds maxPan. A result that is not finite (an input that was not) is the pan
    /// unchanged.
    inline V3 PanAfterZoom(V3 pan, V3 pointerOffset, float zoom0, float zoom1, float maxPan) noexcept
    {
        const auto finite = [](V3 a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); };
        const V3   kept   = pan;
        V3         out    = pan;
        if (zoom1 < zoom0) {
            out = Add(pan, Scale(pointerOffset, 1.0f - zoom1 / zoom0));
        } else if (zoom1 > zoom0) {
            const float home = zoom0 < 1.0f ? 1.0f : kZoomMax;
            const float span = home - zoom0;
            out = span > 1e-6f ? Scale(pan, std::clamp((home - zoom1) / span, 0.0f, 1.0f)) : V3{};
        }
        const float len = Length(out);
        if (len > maxPan && len > 0.0f) out = Scale(out, maxPan / len);
        return finite(out) ? out : kept;
    }

    /// The actor view's framing (its chest anchor, camera distance, floor and shadow frustum) is authored for a
    /// humanoid whose head bone stands this far above the feet; the figure scale measures the actor against it.
    inline constexpr float kFigureHeadUnits = 120.0f;
    inline constexpr float kFigureScaleMin  = 0.5f;
    inline constexpr float kFigureScaleMax  = 2.5f;

    /// One actor's figure scale from what is drawn: the larger of its head bone's height above the feet as the view
    /// draws it (the rest or bind head in the classic pose, the live head as captured) over kFigureHeadUnits, and the
    /// actor root's world scale (race height x the NPC's and the reference's scale: never multiply it by the race
    /// height again). The head carries what the root does not (a race skeleton, a giant, RaceMenu's height); the
    /// root is the floor, because a head can only read low for the figure's size: a quadruped's or a skeever's sits
    /// forward, and a seated, lying or bleeding-out actor's (as captured) is low while the body still spreads.
    /// Clamped, so a bad reading cannot throw the camera away; 1 when neither is usable.
    inline float FigureScale(float headAboveFeet, float rootScale) noexcept
    {
        const float head = std::isfinite(headAboveFeet) && headAboveFeet > 0.0f ? headAboveFeet / kFigureHeadUnits : 0.0f;
        const float root = std::isfinite(rootScale) && rootScale > 0.0f ? rootScale : 0.0f;
        const float s    = std::max(head, root);
        return s > 0.0f ? std::clamp(s, kFigureScaleMin, kFigureScaleMax) : 1.0f;
    }

    /// The figure scale a recollect keeps: the previous one while the new measure is within kFigureScaleSettle of
    /// it, so a captured idle that bobs the head does not move the camera at each recollect. A previous scale that is
    /// not usable takes the measure.
    inline constexpr float kFigureScaleSettle = 0.05f;

    inline float SettledFigureScale(float previous, float measured) noexcept
    {
        if (!(previous > 0.0f) || !std::isfinite(previous)) return measured;
        return std::fabs(measured - previous) <= kFigureScaleSettle * previous ? previous : measured;
    }
}
