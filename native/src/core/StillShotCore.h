#pragma once

// Phantasm - StillShotCore: the shots' still hints (RequestStillHinted, RequestItemStill) and the head-and-shoulders
// camera (Framing::Portrait), engine-free (native/build.ps1 -Tests).
//
// Author: Severause

#include "AsciiText.h"
#include "MirrorLightingCore.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace Phantasm::StillShotCore
{
    /// Which model an armor's item shot draws: the ground model, or the worn model of a man or a woman.
    enum class Worn : std::uint8_t
    {
        kGround = 0,
        kMale   = 1,
        kFemale = 2,
    };

    /// A `worn` value (a still's hint or an item view's): male, female, or off / ground; any case. False for another
    /// word, which leaves `out` alone.
    inline bool ParseWorn(std::string_view value, Worn& out) noexcept
    {
        if (AsciiText::EqualsI(value, "male")) out = Worn::kMale;
        else if (AsciiText::EqualsI(value, "female")) out = Worn::kFemale;
        else if (AsciiText::EqualsI(value, "off") || AsciiText::EqualsI(value, "ground")) out = Worn::kGround;
        else return false;
        return true;
    }

    /// What a still's hints asked for. A field the hints did not set keeps the shot's own default.
    struct Hints
    {
        bool                      hasTurn     = false;
        float                     turnDeg     = 0.0f;   // from the front, (-180, 180]
        bool                      hasLighting = false;
        MirrorLightingCore::Preset lighting   = MirrorLightingCore::kDefaultPreset;
        bool                      hasWeapons  = false;
        bool                      weapons     = false;
        bool                      hasPitch    = false;
        float                     pitchDeg    = 0.0f;   // camera above level, [-kMaxPitchDeg, kMaxPitchDeg]
        Worn                      worn        = Worn::kGround;   // an item shot's model (only an armor's changes)
    };

    /// The steepest a shot's camera looks down or up.
    inline constexpr float kMaxPitchDeg = 80.0f;

    /// deg folded into (-180, 180]; 0 for a value that is not finite.
    inline float WrapTurn(float deg) noexcept
    {
        if (!std::isfinite(deg)) return 0.0f;
        float d = std::fmod(deg, 360.0f);
        if (d <= -180.0f) d += 360.0f;
        if (d > 180.0f) d -= 360.0f;
        return d;
    }

    inline std::string_view Trim(std::string_view s) noexcept
    {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
        return s;
    }

    /// "key=value" pairs separated by ';', keys any case. Known keys: `turn` (degrees from the front; a camera on the
    /// figure's left at 90), `pitch` (degrees above level, clamped to kMaxPitchDeg), `lighting` (a lighting hint
    /// preset id, any case, but not "here": a shot reads no game light), `weapons` (on / off: draw what the actor
    /// carries) and `worn` (male / female / off: an armor's item shot draws the armor as worn by a man or a woman, off
    /// its ground model). An unknown key, a pair with no '=', a number that is not finite, an unknown preset and an unknown
    /// on/off word are ignored, so a newer client's hint costs nothing on an older build.
    inline Hints Parse(std::string_view text) noexcept
    {
        Hints h;
        while (!text.empty()) {
            const auto       semi = text.find(';');
            std::string_view pair = text.substr(0, semi);
            text                  = semi == std::string_view::npos ? std::string_view{} : text.substr(semi + 1);
            const auto eq = pair.find('=');
            if (eq == std::string_view::npos) continue;
            const std::string_view key   = Trim(pair.substr(0, eq));
            const std::string_view value = Trim(pair.substr(eq + 1));
            const auto number = [&value](float& out) {
                const auto* end = value.data() + value.size();
                const auto  r   = std::from_chars(value.data(), end, out);
                return r.ec == std::errc{} && r.ptr == end && std::isfinite(out);
            };
            if (AsciiText::EqualsI(key, "turn")) {
                float v = 0.0f;
                if (number(v)) {
                    h.hasTurn = true;
                    h.turnDeg = WrapTurn(v);
                }
            } else if (AsciiText::EqualsI(key, "pitch")) {
                float v = 0.0f;
                if (number(v)) {
                    h.hasPitch = true;
                    h.pitchDeg = std::clamp(v, -kMaxPitchDeg, kMaxPitchDeg);
                }
            } else if (AsciiText::EqualsI(key, "weapons")) {
                const bool on = AsciiText::EqualsI(value, "on") || AsciiText::EqualsI(value, "true") ||
                                AsciiText::EqualsI(value, "1") || AsciiText::EqualsI(value, "yes");
                const bool off = AsciiText::EqualsI(value, "off") || AsciiText::EqualsI(value, "false") ||
                                 AsciiText::EqualsI(value, "0") || AsciiText::EqualsI(value, "no");
                if (on || off) {
                    h.hasWeapons = true;
                    h.weapons    = on;
                }
            } else if (AsciiText::EqualsI(key, "worn")) {
                ParseWorn(value, h.worn);
            } else if (AsciiText::EqualsI(key, "lighting")) {
                for (std::size_t i = 0; i < MirrorLightingCore::kPresetIds.size(); ++i) {
                    if (static_cast<MirrorLightingCore::Preset>(i) == MirrorLightingCore::Preset::kHere) continue;
                    if (AsciiText::EqualsI(value, MirrorLightingCore::kPresetIds[i])) {
                        h.hasLighting = true;
                        h.lighting    = static_cast<MirrorLightingCore::Preset>(i);
                    }
                }
            }
        }
        return h;
    }

    // ── The head-and-shoulders shot (Framing::Portrait) ──
    // In figure units (MirrorCameraCore::FigureScale), measured from the actor view's chest anchor: the head bone
    // stands kFigureHeadUnits (120) over the feet and the anchor 75, so the frame centres between the crown and the
    // upper chest, and at the actor view's 35 degree vertical FOV the distance shows about 44 units of height.
    inline constexpr float kHeadTargetLift = 39.0f;
    inline constexpr float kHeadDistance   = 70.0f;
    /// The head shot's view, in view pixels (square; the offscreen supersamples it).
    inline constexpr float kHeadShotRect = 768.0f;
    /// A head this far (figure units) or less from straight over the feet is a humanoid's, framed from
    /// kHeadTargetLift; one further forward (a horse's, a dragon's) is aimed at, kHeadBelowBone under the bone.
    inline constexpr float kHumanoidHeadReach = 15.0f;
    inline constexpr float kHeadBelowBone     = 6.0f;

    struct V3
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
    };

    /// Where a head shot's camera looks, from the chest anchor (which stands chestUnits x figure over the feet), in
    /// world units. headOffset: the drawn head bone from the actor's position, NaN when it has none.
    inline V3 HeadShotTarget(V3 headOffset, float figure, float chestUnits) noexcept
    {
        const V3 humanoid{ 0.0f, 0.0f, kHeadTargetLift * figure };
        if (!std::isfinite(headOffset.x) || !std::isfinite(headOffset.y) || !std::isfinite(headOffset.z)) return humanoid;
        if (std::hypot(headOffset.x, headOffset.y) <= kHumanoidHeadReach * figure) return humanoid;
        return { headOffset.x, headOffset.y, headOffset.z - (chestUnits + kHeadBelowBone) * figure };
    }

    /// How long the actor view stays up after a shot, so a following shot of the same actor (another turn, the head)
    /// reuses its collect instead of loading them again.
    inline constexpr int kShotLingerMs = 1500;

    /// The visible height at `distance` for a vertical FOV of fovDeg (both in the same units).
    inline float VisibleHeight(float distance, float fovDeg) noexcept
    {
        constexpr float kPi = 3.14159265358979f;
        return 2.0f * distance * std::tan(fovDeg * 0.5f * kPi / 180.0f);
    }

    // ── The item shot (RequestItemStill) ──
    /// How long an item shot's model, effects and particles get before it is taken, and when it is given up.
    inline constexpr int kItemSettleMs  = 400;
    inline constexpr int kItemTimeoutMs = 6000;
    /// The item shot's view, in view pixels.
    inline constexpr float kItemShotRect = 768.0f;
    /// How long a later angle of the same item gets to settle on the view the last shot left up (its effects have run).
    inline constexpr int kItemReshotSettleMs = 50;
    /// How long the item view stays up after a shot for the item's next one. Short: a client asks every angle at once,
    /// so the next is already queued, and the item menu the view runs in refuses saving while it is open.
    inline constexpr int kItemShotLingerMs = 300;

    // ── The shot queue ──
    /// What a shot draws: shots with equal keys share one load (the view a shot leaves up, kShotLingerMs).
    struct ShotKey
    {
        bool          item   = false;
        std::uint32_t formId = 0;
        bool          weapons = false;   // an actor shot's
        Worn          worn    = Worn::kGround;   // an item shot's

        bool operator==(const ShotKey&) const = default;
    };

    /// How many shots in a row the queue lets one subject take ahead of older ones.
    inline constexpr int kMaxSameSubjectRun = 16;

    /// The queued shot to start next: the oldest of the same subject as the shot just taken (`last`, `run` shots of it
    /// in a row), so its load is reused, unless that subject has had kMaxSameSubjectRun in a row; else the oldest.
    /// `queue` oldest first, not empty.
    inline std::size_t NextShotIndex(const std::vector<ShotKey>& queue, const ShotKey& last, int run) noexcept
    {
        if (last.formId != 0 && run < kMaxSameSubjectRun) {
            for (std::size_t i = 0; i < queue.size(); ++i) {
                if (queue[i] == last) return i;
            }
        }
        return 0;
    }
}
