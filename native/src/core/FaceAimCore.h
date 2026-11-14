#pragma once

// Phantasm - FaceAimCore: the camera of an actor view or a Portrait still whose `aim` is the face (MannequinRenderer.cpp):
// which way the drawn head's face looks and the orbit around it, engine-free (native/build.ps1 -Tests).
//
// Author: Severause

#include "MirrorCameraCore.h"
#include "StillShotCore.h"

#include <algorithm>
#include <cmath>

namespace Phantasm::FaceAimCore
{
    using V3 = MirrorCameraCore::V3;
    using MirrorCameraCore::Add;
    using MirrorCameraCore::Dot;
    using MirrorCameraCore::Length;
    using MirrorCameraCore::Scale;
    using MirrorCameraCore::Sub;

    inline constexpr float kDeg = 3.14159265358979f / 180.0f;

    inline V3 Cross(V3 a, V3 b) noexcept { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
    inline bool Finite(V3 a) noexcept { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }

    /// The angle between two unit vectors, degrees.
    inline float AngleDeg(V3 a, V3 b) noexcept { return std::acos(std::clamp(Dot(a, b), -1.0f, 1.0f)) / kDeg; }

    /// A 3x3 transform in DirectXMath's row-vector order (v' = v M, as NiTransformToXM builds it), which may carry a
    /// uniform scale.
    struct M3
    {
        float m[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
    };

    inline V3 Times(V3 v, const M3& a) noexcept
    {
        return { v.x * a.m[0][0] + v.y * a.m[1][0] + v.z * a.m[2][0], v.x * a.m[0][1] + v.y * a.m[1][1] + v.z * a.m[2][1],
                 v.x * a.m[0][2] + v.y * a.m[1][2] + v.z * a.m[2][2] };
    }

    /// v M^T: for a rotation with a uniform scale, the inverse up to that scale.
    inline V3 TimesTransposed(V3 v, const M3& a) noexcept
    {
        return { v.x * a.m[0][0] + v.y * a.m[0][1] + v.z * a.m[0][2], v.x * a.m[1][0] + v.y * a.m[1][1] + v.z * a.m[1][2],
                 v.x * a.m[2][0] + v.y * a.m[2][1] + v.z * a.m[2][2] };
    }

    /// Where the face looks and where its crown is, unit and orthogonal, in the world.
    struct Frame
    {
        V3 forward{ 0, 1, 0 };
        V3 up{ 0, 0, 1 };
    };

    /// A frame from a forward and an up that need not be orthogonal (the up is made so). False, and `out` untouched,
    /// when either is not finite or nearly zero, or they are nearly parallel.
    inline bool MakeFrame(V3 forward, V3 up, Frame& out) noexcept
    {
        if (!Finite(forward) || !Finite(up)) return false;
        const float fl = Length(forward);
        if (!(fl > 1e-6f)) return false;
        const V3    f  = Scale(forward, 1.0f / fl);
        const V3    u0 = Sub(up, Scale(f, Dot(up, f)));
        const float ul = Length(u0);
        if (!(ul > 1e-3f * std::max(Length(up), 1e-6f))) return false;
        out = { f, Scale(u0, 1.0f / ul) };
        return true;
    }

    /// The drawn head's face. restHead: the head bone relative to the actor's root at rest (the race skeleton NIF's
    /// chain), where the face looks along the root's +Y with the crown toward +Z, as skeletons are authored; head: the
    /// head bone as drawn, in the world. The face turns with whatever the animation did to the head, however the
    /// skeleton's bones are oriented. False for a degenerate input.
    inline bool HeadFrame(const M3& restHead, const M3& head, Frame& out) noexcept
    {
        const V3 forwardInHead = TimesTransposed({ 0, 1, 0 }, restHead);
        const V3 upInHead      = TimesTransposed({ 0, 0, 1 }, restHead);
        return MakeFrame(Times(forwardInHead, head), Times(upInHead, head), out);
    }

    /// An upright figure facing the camera at orbit yaw frontYawDeg (MirrorCameraCore::FacingYawDeg; the orbit puts
    /// the camera at (sin yaw, -cos yaw) from the target): the face aim's frame when the head's is not known.
    inline Frame FigureFrame(float frontYawDeg) noexcept
    {
        const float a = (std::isfinite(frontYawDeg) ? frontYawDeg : 0.0f) * kDeg;
        return { { std::sin(a), -std::cos(a), 0.0f }, { 0.0f, 0.0f, 1.0f } };
    }

    /// The camera about a face: `dir` from the target toward the camera and the camera's `up`, unit and orthogonal.
    /// Yaw 0 and pitch 0 look the face in the eye with the crown up; yawDeg turns the camera toward the face's left,
    /// as the figure orbit's yaw and a still's turn do; pitchDeg raises it toward the crown (clamped to 80 either way,
    /// as the orbit is). For an upright face looking along the heading this is the figure orbit's camera exactly.
    struct Camera
    {
        V3 dir;
        V3 up;
    };

    inline Camera Orbit(const Frame& f, float yawDeg, float pitchDeg) noexcept
    {
        const float y     = (std::isfinite(yawDeg) ? yawDeg : 0.0f) * kDeg;
        const float p     = std::clamp(std::isfinite(pitchDeg) ? pitchDeg : 0.0f, -80.0f, 80.0f) * kDeg;
        const V3    left  = Cross(f.up, f.forward);
        const V3    level = Add(Scale(f.forward, std::cos(y)), Scale(left, std::sin(y)));
        return { Add(Scale(level, std::cos(p)), Scale(f.up, std::sin(p))), Sub(Scale(f.up, std::cos(p)), Scale(level, std::sin(p))) };
    }

    /// Where the face camera looks: belowBone figure units below the head bone along the face's up, as a Portrait
    /// shot frames a head (StillShotCore::HeadBelowBone for a shot's zoom).
    inline V3 Target(V3 headBone, const Frame& f, float figure, float belowBone = StillShotCore::kHeadBelowBone) noexcept
    {
        return Sub(headBone, Scale(f.up, belowBone * figure));
    }

    /// A live view follows the face with a lag, so a nod turns the head in the picture instead of swinging the world
    /// about a face nailed to the frame: the time constants (seconds) of its turn and of its move, and the turn past
    /// which it jumps rather than swinging through the side of the head.
    inline constexpr float kTurnLagSec = 0.30f;
    inline constexpr float kMoveLagSec = 0.10f;
    inline constexpr float kJumpDeg    = 120.0f;
    /// Closer than this it settles exactly, and the view stops drawing for it.
    inline constexpr float kSettleDeg   = 0.05f;
    inline constexpr float kSettleUnits = 0.02f;

    struct Follow
    {
        bool  have = false;
        Frame frame;
        V3    target;
    };

    /// One step of `follow` toward the face as drawn now after dtSec. The first step, a step with `snap`, and a turn
    /// past kJumpDeg take it at once; a dtSec that is not finite or not positive holds. A frame or target that is not
    /// finite changes nothing. Returns whether it is still on its way (the view draws again until it is not). `target`
    /// must be measured from a point near it (the chest anchor): far out in the world a float's step is coarser than
    /// kSettleUnits, and the follow would never settle.
    inline bool Step(Follow& follow, const Frame& now, V3 target, float dtSec, bool snap) noexcept
    {
        if (!Finite(target) || !Finite(now.forward) || !Finite(now.up)) return false;
        if (!follow.have || snap || AngleDeg(follow.frame.forward, now.forward) > kJumpDeg ||
            AngleDeg(follow.frame.up, now.up) > kJumpDeg) {
            follow = { true, now, target };
            return false;
        }
        if (std::isfinite(dtSec) && dtSec > 0.0f) {
            const float kt = 1.0f - std::exp(-dtSec / kTurnLagSec);
            const float km = 1.0f - std::exp(-dtSec / kMoveLagSec);
            Frame       next;
            if (!MakeFrame(Add(follow.frame.forward, Scale(Sub(now.forward, follow.frame.forward), kt)),
                           Add(follow.frame.up, Scale(Sub(now.up, follow.frame.up), kt)), next)) {
                next = now;
            }
            follow.frame  = next;
            follow.target = Add(follow.target, Scale(Sub(target, follow.target), km));
        }
        if (AngleDeg(follow.frame.forward, now.forward) <= kSettleDeg && AngleDeg(follow.frame.up, now.up) <= kSettleDeg &&
            Length(Sub(follow.target, target)) <= kSettleUnits) {
            follow.frame  = now;
            follow.target = target;
            return false;
        }
        return true;
    }
}
