#pragma once

// Phantasm - MannequinSpringCore: secondary motion for the actor view's clip player
// (MannequinRenderer.cpp), engine-free (native/build.ps1 -Tests). A bone the clip does not drive that an SMP XML
// simulates swings (MannequinPhysicsCore::SmpSpringParams): its tip is a damped spring pulled toward where the pose
// puts it, held at the bone's length and inside a cone around that rest direction. An approximation of SMP for a
// view, not a replica; a bone no physics setup drives stays still.
//
// Author: Severause

#include <algorithm>
#include <cmath>

namespace Phantasm::MannequinSpringCore
{
    struct Vec3
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
    };
    inline Vec3  operator+(Vec3 a, Vec3 b) noexcept { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
    inline Vec3  operator-(Vec3 a, Vec3 b) noexcept { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
    inline Vec3  operator*(Vec3 a, float s) noexcept { return { a.x * s, a.y * s, a.z * s }; }
    inline float Dot(Vec3 a, Vec3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
    inline float Length(Vec3 a) noexcept { return std::sqrt(Dot(a, a)); }

    struct SpringParams
    {
        float stiffness = 0.0f;   ///< pull toward the rest tip (1/s^2); 0 never moves
        float damping   = 0.0f;   ///< velocity loss (1/s)
        float maxAngle  = 0.0f;   ///< the widest swing from the rest direction (radians)
    };

    /// The longest integration step; a frame's time is split into steps no longer than this.
    inline constexpr float kMaxStep = 1.0f / 120.0f;
    /// A gap longer than this (a hitch, a menu, a long pause) resets the spring to rest instead of catching up.
    inline constexpr float kResetGap = 0.25f;
    /// Below these the spring counts as at rest (game units, game units per second).
    inline constexpr float kRestDistance = 0.02f;
    inline constexpr float kRestSpeed    = 0.5f;

    struct SpringState
    {
        Vec3 tip, vel;
        bool primed = false;
    };

    /// v clamped to within maxAngle of `toward` (both from the same origin, v's length kept); unchanged inside the cone.
    inline Vec3 ClampToCone(Vec3 v, Vec3 toward, float maxAngle) noexcept
    {
        const float lv = Length(v), lt = Length(toward);
        if (lv < 1e-6f || lt < 1e-6f) return toward;
        const Vec3  a   = v * (1.0f / lv);
        const Vec3  b   = toward * (1.0f / lt);
        const float cosAngle = std::clamp(Dot(a, b), -1.0f, 1.0f);
        const float angle    = std::acos(cosAngle);
        if (angle <= maxAngle) return v;
        // Slerp from b toward a by maxAngle.
        const Vec3  perp = a - b * cosAngle;
        const float lp   = Length(perp);
        if (lp < 1e-6f) return toward;
        const Vec3 dir = b * std::cos(maxAngle) + perp * (std::sin(maxAngle) / lp);
        return dir * lv;
    }

    /// Advances the spring by dt seconds: the tip is pulled toward target (the rest tip the pose gives, origin the
    /// bone's own position), kept at the bone's length and inside the cone. A first step, or one after a gap longer
    /// than kResetGap, puts the tip at rest. Returns whether it is still moving.
    inline bool Step(SpringState& s, Vec3 origin, Vec3 target, SpringParams p, float dt) noexcept
    {
        if (!s.primed || !(dt > 0.0f) || dt > kResetGap || p.stiffness <= 0.0f) {
            s.tip    = target;
            s.vel    = {};
            s.primed = true;
            return false;
        }
        const int   n = std::clamp(static_cast<int>(std::ceil(dt / kMaxStep)), 1, 32);
        const float h = dt / static_cast<float>(n);
        const float len = Length(target - origin);
        for (int i = 0; i < n; ++i) {
            const Vec3 acc = (target - s.tip) * p.stiffness - s.vel * p.damping;
            s.vel = s.vel + acc * h;
            s.tip = s.tip + s.vel * h;
            Vec3 d = s.tip - origin;
            const float ld = Length(d);
            d = ld > 1e-6f ? d * (len / ld) : target - origin;
            const Vec3 clamped = ClampToCone(d, target - origin, p.maxAngle);
            if (Length(clamped - d) > 1e-4f) s.vel = s.vel * 0.5f;   // a tip at the cone's edge loses half its speed
            s.tip = origin + clamped;
        }
        if (!std::isfinite(s.tip.x) || !std::isfinite(s.tip.y) || !std::isfinite(s.tip.z)) {
            s.tip = target;
            s.vel = {};
            return false;
        }
        return Length(s.vel) > kRestSpeed || Length(s.tip - target) > kRestDistance;
    }

    /// p turned about the vertical axis through pivot by angle radians, counter-clockwise seen from above (+Z).
    inline Vec3 RotateAboutZ(Vec3 p, Vec3 pivot, float angle) noexcept
    {
        const float c = std::cos(angle), s = std::sin(angle);
        const float x = p.x - pivot.x, y = p.y - pivot.y;
        return { pivot.x + x * c - y * s, pivot.y + x * s + y * c, p.z };
    }

    /// A direction turned the same way (no pivot).
    inline Vec3 RotateDirAboutZ(Vec3 v, float angle) noexcept { return RotateAboutZ(v, {}, angle); }
}
