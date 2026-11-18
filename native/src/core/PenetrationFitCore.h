#pragma once

// Phantasm - PenetrationFitCore: fits a shaft into an opening between two actors a scene's clips pose, engine-free
// (native/build.ps1 -Tests). A sex scene's clips are authored on reference bodies, so on the actors drawn the shaft
// (NPC GenitalsBase to Gen06) misses the opening it is meant to be in. Once every actor of the scene is posed, the view
// picks for each shaft the opening of another actor it is aimed at (ChooseOpening), bends the shaft's joints along a
// path to the entrance and on inward (FitShaft) and spreads the opening by how deep it reaches (Spread)
// (MannequinRenderer's FitScenePenetration writes the bent bones into the drawn palettes; the clip's own pose is kept).
//
// Author: Severause

#include <algorithm>
#include <cmath>
#include <vector>

namespace Phantasm::PenetrationFitCore
{
    struct V3
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
    };

    inline V3    Add(V3 a, V3 b) noexcept { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
    inline V3    Sub(V3 a, V3 b) noexcept { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
    inline V3    Scale(V3 a, float s) noexcept { return { a.x * s, a.y * s, a.z * s }; }
    inline float Dot(V3 a, V3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
    inline float Length(V3 a) noexcept { return std::sqrt(Dot(a, a)); }
    inline bool  Finite(V3 a) noexcept { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }
    inline V3    Lerp(V3 a, V3 b, float t) noexcept { return Add(a, Scale(Sub(b, a), t)); }
    /// a made unit; false (and `out` untouched) when it is not finite or nearly zero.
    inline bool Normalize(V3 a, V3& out) noexcept
    {
        const float l = Length(a);
        if (!std::isfinite(l) || l < 1e-5f) return false;
        out = Scale(a, 1.0f / l);
        return true;
    }

    enum class Kind
    {
        kVagina,
        kAnus,
        kMouth,
    };

    /// An opening on an actor: where it starts and the way into the body (unit).
    struct Opening
    {
        Kind kind = Kind::kVagina;
        V3   entrance;
        V3   inward{ 0.0f, 1.0f, 0.0f };
    };

    /// How far past its tip a shaft reaches an opening's entrance, in game units at the shaft's own scale 1.
    inline constexpr float kReachMargin = 6.0f;
    /// The aim, degrees between the shaft (base to tip as posed) and the line from its base to the entrance: fitted in
    /// full up to kFullAimDeg, fading out to none at kMaxAimDeg.
    inline constexpr float kFullAimDeg = 20.0f;
    inline constexpr float kMaxAimDeg  = 55.0f;
    /// The shaft comes at the opening from outside: degrees between the shaft and the way in, at most.
    inline constexpr float kMaxApproachDeg = 100.0f;
    /// The corner the path takes at the entrance, degrees between the line from the base to it and the way in: fitted
    /// in full up to kFullCornerDeg, fading out to none at kMaxCornerDeg (a sharper turn would fold the shaft at the
    /// body's surface).
    inline constexpr float kFullCornerDeg = 45.0f;
    inline constexpr float kMaxCornerDeg  = 90.0f;
    /// A shaft keeps the opening it took last frame unless another scores below this share of it (the openings of
    /// one body are a few units apart, and a clip's thrust moves the tip between them).
    inline constexpr float kSwitchRatio = 0.75f;
    /// A mouth's entrance off the drawn head bone, in the face's frame (forward, down), game units at head scale 1.
    /// An estimate for human skeletons, to tune against a scene in game.
    inline constexpr float kMouthForward = 8.0f;
    inline constexpr float kMouthDown    = 3.5f;
    /// Spread: how far the opening's bones move out at full depth, and the depth past the entrance that is full,
    /// both game units at the shaft's own scale 1.
    inline constexpr float kSpread      = 1.2f;
    inline constexpr float kSpreadDepth = 3.0f;

    inline float AngleDeg(V3 a, V3 b) noexcept
    {
        V3 ua, ub;
        if (!Normalize(a, ua) || !Normalize(b, ub)) return 180.0f;
        return std::acos(std::clamp(Dot(ua, ub), -1.0f, 1.0f)) * 57.2957795f;
    }

    /// The shaft's length: the sum of its joints' segments.
    inline float ShaftLength(const std::vector<V3>& joints) noexcept
    {
        float l = 0.0f;
        for (std::size_t i = 1; i < joints.size(); ++i) l += Length(Sub(joints[i], joints[i - 1]));
        return l;
    }

    /// 1 at or below `full`, 0 at or past `none`, smooth between.
    inline float Fade(float v, float full, float none) noexcept
    {
        if (!std::isfinite(v) || v >= none) return 0.0f;
        if (v <= full) return 1.0f;
        const float t = (none - v) / (none - full);
        return t * t * (3.0f - 2.0f * t);
    }

    /// How fully a shaft aimed at an opening is fitted, 0 to 1: by its aim, by its reach (full within its length,
    /// fading out over kReachMargin x scale past its tip) and by the corner at the entrance; 0 coming from inside.
    /// scale: the shaft's own (a body mod's size).
    inline float Engagement(const std::vector<V3>& joints, const Opening& o, float scale) noexcept
    {
        if (joints.size() < 2 || !Finite(o.entrance) || !(scale > 0.0f)) return 0.0f;
        const V3    base   = joints.front();
        const V3    axis   = Sub(joints.back(), base);
        const V3    toward = Sub(o.entrance, base);
        const float length = ShaftLength(joints);
        if (AngleDeg(axis, o.inward) > kMaxApproachDeg) return 0.0f;
        const float reach  = Fade(Length(toward), length, length + kReachMargin * scale);
        const float aim    = Fade(AngleDeg(axis, toward), kFullAimDeg, kMaxAimDeg);
        const float corner = Length(toward) < 1e-3f ? 1.0f : Fade(AngleDeg(toward, o.inward), kFullCornerDeg, kMaxCornerDeg);
        return reach * aim * corner;
    }

    struct Choice
    {
        int   opening = -1;   // index into the candidates, -1: none
        float weight  = 0.0f;
    };

    /// The opening a shaft is aimed at: of those it engages, the one whose entrance is nearest its tip, a weaker
    /// engagement counted farther. keep: the one it took last frame (-1: none), which it keeps while it engages
    /// unless another scores below kSwitchRatio of it.
    inline Choice ChooseOpening(const std::vector<V3>& joints, const std::vector<Opening>& openings, float scale,
                                int keep = -1) noexcept
    {
        Choice best, kept;
        float  bestScore = 0.0f, keptScore = 0.0f;
        for (std::size_t i = 0; i < openings.size(); ++i) {
            const float w = Engagement(joints, openings[i], scale);
            if (w <= 0.0f) continue;
            const float score = (Length(Sub(openings[i].entrance, joints.back())) + 1e-3f) / w;
            if (static_cast<int>(i) == keep) {
                kept      = { keep, w };
                keptScore = score;
            }
            if (best.opening < 0 || score < bestScore) {
                best      = { static_cast<int>(i), w };
                bestScore = score;
            }
        }
        if (kept.opening >= 0 && best.opening != kept.opening && !(bestScore < keptScore * kSwitchRatio)) return kept;
        return best;
    }

    /// The point `s` along the path from `base` straight to the entrance and on along the way in.
    inline V3 PathAt(V3 base, const Opening& o, float s) noexcept
    {
        const V3    toward = Sub(o.entrance, base);
        const float d      = Length(toward);
        V3          dir;
        if (s <= d && Normalize(toward, dir)) return Add(base, Scale(dir, s));
        return Add(o.entrance, Scale(o.inward, s - (d > 0.0f ? d : 0.0f)));
    }

    /// The shaft's joints fitted to an opening: each laid on the path at its own distance along the shaft from the
    /// base (which stays), then moved weight of the way there from where the clip put it. The input when there are
    /// fewer than two joints or weight is not above 0.
    inline std::vector<V3> FitShaft(const std::vector<V3>& joints, const Opening& o, float weight)
    {
        std::vector<V3> out = joints;
        if (joints.size() < 2 || !(weight > 0.0f)) return out;
        const float w = std::min(weight, 1.0f);
        float       s = 0.0f;
        for (std::size_t i = 1; i < joints.size(); ++i) {
            s += Length(Sub(joints[i], joints[i - 1]));
            out[i] = Lerp(joints[i], PathAt(joints.front(), o, s), w);
        }
        return out;
    }

    /// How deep a fitted shaft's tip is past the entrance along the way in, 0 when short of it.
    inline float Depth(const std::vector<V3>& fitted, const Opening& o) noexcept
    {
        if (fitted.empty()) return 0.0f;
        return std::max(Dot(Sub(fitted.back(), o.entrance), o.inward), 0.0f);
    }

    /// How far the opening's bones move out from its centre: kSpread x scale at kSpreadDepth x scale and deeper.
    inline float Spread(float depth, float scale) noexcept
    {
        if (!(scale > 0.0f) || !(depth > 0.0f)) return 0.0f;
        return kSpread * scale * std::min(depth / (kSpreadDepth * scale), 1.0f);
    }

    /// A bone of an opening moved out from its centre by `spread`, along the side it already lies on (across the way
    /// in). The bone as it is when it lies on the centre line.
    inline V3 SpreadBone(V3 bone, const Opening& o, float spread) noexcept
    {
        const V3 off    = Sub(bone, o.entrance);
        const V3 across = Sub(off, Scale(o.inward, Dot(off, o.inward)));
        V3       dir;
        if (!(spread > 0.0f) || !Normalize(across, dir)) return bone;
        return Add(bone, Scale(dir, spread));
    }

    /// A mouth from the drawn head bone and the face's frame (forward, up, unit): kMouthForward ahead and kMouthDown
    /// below the bone, at the head's scale; the way in is back along the face.
    inline Opening MouthOf(V3 headBone, V3 forward, V3 up, float headScale) noexcept
    {
        const float s = headScale > 0.0f ? headScale : 1.0f;
        Opening     o;
        o.kind     = Kind::kMouth;
        o.entrance = Add(headBone, Add(Scale(forward, kMouthForward * s), Scale(up, -kMouthDown * s)));
        o.inward   = Scale(forward, -1.0f);
        return o;
    }

    /// An opening at the centre of its rim bones, its way in toward `deep` (a bone inside the body behind it). False
    /// with no rim bones or no way in.
    inline bool OpeningOf(Kind kind, const std::vector<V3>& rim, V3 deep, Opening& out) noexcept
    {
        if (rim.empty()) return false;
        V3 c;
        for (const auto& r : rim) c = Add(c, r);
        c = Scale(c, 1.0f / static_cast<float>(rim.size()));
        V3 in;
        if (!Finite(c) || !Normalize(Sub(deep, c), in)) return false;
        out = { kind, c, in };
        return true;
    }
}
