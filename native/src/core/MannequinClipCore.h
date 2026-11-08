#pragma once

// Phantasm - MannequinClipCore: the actor view's clip player (MannequinRenderer.cpp), engine-free
// (native/build.ps1 -Tests): clip names, idle families and animation replacer sets, a take's baked window and its
// clock, the idle cycle's choices, the follow of the engine's clip, and the shared idle pool's keys and limits.
//
// Author: Severause

#include "AsciiText.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace Phantasm::MannequinClipCore
{
    /// How a take plays, in hkbClipGenerator::PlaybackMode's order: kToEnd plays its window once and the cycle moves
    /// on, kHeld stays on one frame (the engine's user-controlled clip).
    enum class ClipMode : std::uint8_t
    {
        kToEnd    = 0,
        kLoop     = 1,
        kHeld     = 2,
        kPingPong = 3,
    };

    /// An engine mode byte as a ClipMode; anything past kPingPong loops.
    inline ClipMode FromEngineMode(std::uint8_t m) noexcept
    {
        return m <= 3 ? static_cast<ClipMode>(m) : ClipMode::kLoop;
    }

    /// Whether the graph loops a clip in this mode (a base idle).
    inline bool IsLooping(ClipMode m) noexcept { return m == ClipMode::kLoop || m == ClipMode::kPingPong; }

    inline const char* ModeName(ClipMode m) noexcept
    {
        switch (m) {
        case ClipMode::kToEnd:    return "to its end";
        case ClipMode::kLoop:     return "looping";
        case ClipMode::kHeld:     return "held";
        case ClipMode::kPingPong: return "ping-pong";
        }
        return "?";
    }

    // ── Names (ASCII case-insensitive; clip paths use either slash) ──

    using AsciiText::ContainsI;
    using AsciiText::EndsWithI;
    using AsciiText::EqualsI;
    using AsciiText::StartsWithI;
    inline std::string Lower(std::string_view v) { return AsciiText::Lowered(v); }
    /// The file name of a path.
    inline std::string_view BaseName(std::string_view v) noexcept
    {
        if (const auto slash = v.find_last_of("\\/"); slash != std::string_view::npos) v.remove_prefix(slash + 1);
        return v;
    }
    /// The folder a path names its file in ("" for a bare name).
    inline std::string_view DirOf(std::string_view v) noexcept
    {
        const auto slash = v.find_last_of("\\/");
        return slash == std::string_view::npos ? std::string_view{} : v.substr(0, slash);
    }
    inline std::string_view StripHkx(std::string_view v) noexcept
    {
        if (v.size() > 4 && EndsWithI(v, ".hkx")) v.remove_suffix(4);
        return v;
    }
    inline std::string_view TrimDigits(std::string_view v) noexcept
    {
        while (!v.empty() && std::isdigit(static_cast<unsigned char>(v.back()))) v.remove_suffix(1);
        return v;
    }

    /// Whether a clip is a transition into, out of or between idles: its file name, less the extension and a
    /// trailing number, ends Start, Exit, Enter, Stop or Trans (MT_Idle_A_to_B_Trans), or holds IdleTo (the Vampire
    /// Lord's MT_IdleToAttackCombatState). The idle cycle never breaks into one and the pool never keeps one.
    inline bool IsTransitionClip(std::string_view name) noexcept
    {
        const auto v = TrimDigits(StripHkx(BaseName(name)));
        return EndsWithI(v, "start") || EndsWithI(v, "exit") || EndsWithI(v, "enter") || EndsWithI(v, "stop") ||
               EndsWithI(v, "trans") || ContainsI(v, "idleto");
    }

    /// Whether a behavior project is the humanoid character's (DefaultMale, DefaultFemale), whose idles have long
    /// names; a creature's (HorseProject: Idle.hkx) may name a family in a few letters.
    inline bool IsCharacterProject(std::string_view project) noexcept { return StartsWithI(project, "default"); }

    /// The shortest family name: a character's, and a creature's.
    inline constexpr std::size_t kMinFamily         = 6;
    inline constexpr std::size_t kMinCreatureFamily = 4;

    /// Whether any '_'-separated word of a clip's file name (less the extension and a trailing number) starts with
    /// one of `words`.
    inline bool AnyWordStarts(std::string_view name, std::initializer_list<std::string_view> words) noexcept
    {
        std::string_view v = TrimDigits(StripHkx(BaseName(name)));
        while (!v.empty()) {
            const auto       cut  = v.find('_');
            std::string_view word = v.substr(0, cut);
            for (const auto w : words) {
                if (!word.empty() && StartsWithI(word, w)) return true;
            }
            if (cut == std::string_view::npos) break;
            v.remove_prefix(cut + 1);
        }
        return false;
    }

    /// Whether a clip moves the actor about (a creature's WalkForward, RunForwardL, TrotForward_Wolf, TurnCannedR90,
    /// SprintForward, SwimF, JumpStart; a character's MT_WalkForward): never an idle.
    inline bool IsLocomotionClip(std::string_view name) noexcept
    {
        return AnyWordStarts(name, { "walk", "run", "trot", "sprint", "gallop", "turn", "swim", "jump" });
    }

    /// Whether a creature's idle answers someone (the horse's Idle_Pet, played as the player pets it, a feed or a
    /// mount): never one the view's own cycle plays.
    inline bool IsInteractionIdle(std::string_view name) noexcept
    {
        return AnyWordStarts(name, { "pet", "feed", "mount", "dismount", "saddle" });
    }

    /// The file name, lower case, of the lead-in a looping middle part needs (Idle_Graze_Loop: idle_graze_start.hkx);
    /// "" for a clip whose name does not end Loop.
    inline std::string LeadInOf(std::string_view name)
    {
        const std::string v = Lower(StripHkx(BaseName(name)));
        if (v.size() <= 4 || v.compare(v.size() - 4, 4, "loop") != 0) return {};
        return v.substr(0, v.size() - 4) + "start.hkx";
    }

    /// The family of idles a clip's file belongs to, lower case: "mt_idle" for a standing idle (MT_Idle* or a
    /// replacer's _variants_mt_idle* folder), else its name less the extension, a trailing number, Start, Exit,
    /// Enter, Stop or Trans, Var and Base (Chair_IdleBaseVar1: chair_idle; a replacer's _variants_<clip> folder names
    /// the clip). Empty when that leaves under kMinFamily characters (kMinCreatureFamily for a creature's project:
    /// !IsCharacterProject), which names no family, and for a creature's locomotion clip.
    inline std::string IdleFamily(std::string_view file, bool creature = false)
    {
        if (creature && IsLocomotionClip(file)) return {};
        const std::string f   = Lower(file);
        std::string_view  v   = BaseName(f);
        const auto        dir = BaseName(DirOf(f));
        if (dir.size() > 10 && dir.substr(0, 10) == "_variants_") v = dir.substr(10);
        else v = StripHkx(v);
        if (v.substr(0, 7) == "mt_idle") return "mt_idle";
        const auto trim = [&](std::string_view suf) {
            if (v.size() >= suf.size() && v.substr(v.size() - suf.size()) == suf) {
                v.remove_suffix(suf.size());
                return true;
            }
            return false;
        };
        v = TrimDigits(v);
        if (trim("start") || trim("exit") || trim("enter") || trim("stop") || trim("trans")) v = TrimDigits(v);
        trim("var");
        v = TrimDigits(v);
        trim("base");
        while (!v.empty() && (v.back() == '_' || v.back() == ' ')) v.remove_suffix(1);
        return v.size() < (creature ? kMinCreatureFamily : kMinFamily) ? std::string{} : std::string(v);
    }

    // ── Animation replacers (OAR, and the DAR folders OAR reads) ──

    /// The set a replacer animation belongs to, lower case with backslashes: the path up to and including
    /// `openanimationreplacer\<mod>\<submod>\`, `dynamicanimationreplacer\_customconditions\<id>\` or
    /// `dynamicanimationreplacer\<plugin>\<formid>\`, whose files share one condition set (the submod's sex and
    /// _variants_ folders included). A path inside a replacer folder too short for that is its own folder; "" for a
    /// file of no replacer.
    inline std::string ReplacerSetOf(std::string_view path)
    {
        std::string p = Lower(path);
        for (auto& ch : p) {
            if (ch == '/') ch = '\\';
        }
        const auto component = [&](std::string_view name) -> std::size_t {
            const std::string key = std::string(name) + "\\";
            for (std::size_t at = p.find(key); at != std::string::npos; at = p.find(key, at + 1)) {
                if (at == 0 || p[at - 1] == '\\') return at + key.size();
            }
            return std::string::npos;
        };
        // The end of `count` more folders after `from`; npos when the path runs out first.
        const auto skip = [&](std::size_t from, int count) -> std::size_t {
            for (int i = 0; i < count && from != std::string::npos; ++i) {
                const auto slash = p.find('\\', from);
                from             = (slash == std::string::npos || slash == from) ? std::string::npos : slash + 1;
            }
            return from;
        };
        // Two folders under either root: OAR's mod and submod, DAR's _customconditions and its id, or its plugin and form.
        std::size_t base = component("openanimationreplacer");
        if (base == std::string::npos) base = component("dynamicanimationreplacer");
        if (base == std::string::npos) return {};
        const std::size_t end = skip(base, 2);
        if (end != std::string::npos) return p.substr(0, end);
        const auto slash = p.find_last_of('\\');
        return p.substr(0, slash + 1);
    }

    /// A take's set when the binding it plays could not be read: no set ReplacerSetOf gives, and never pooled.
    inline constexpr std::string_view kSetUnknown = "?";

    /// A set's display name: its last folder (the OAR submod, the DAR condition id or form); "" for no set.
    inline std::string ReplacerSetName(std::string_view set)
    {
        if (set.empty() || set == kSetUnknown) return {};
        std::string_view v = set;
        while (!v.empty() && (v.back() == '\\' || v.back() == '/')) v.remove_suffix(1);
        return std::string(BaseName(v));
    }

    /// How an entry of the character's animation set joins the idle cycle of the clip that plays.
    enum class Member : std::uint8_t
    {
        kNone,
        kLoop,
        kShot,
    };

    /// file: the entry's path as the set names it; family: the playing clip's (IdleFamily); playingStem: the playing
    /// clip's file name less the extension, lower case (its variants folder is _variants_<stem>); playingSet: the
    /// replacer set of the file the playing clip is bound to (ReplacerSetOf, "" for none). A replacer's entry joins
    /// only from the set that plays, so another set's conditional idles (a child's, an armour's, one follower's)
    /// never do; a replacer's folder for the other sex never does; a transition never does; a combat clip only a
    /// combat family. For the standing family the MT_Idle* files and _variants_mt_idle* folders join (MT_Idle and
    /// its variants as base loops, the rest as one-shots); for another, its family's files and its variants folder,
    /// all base loops. A creature (!IsCharacterProject) loops only the clip that plays, so every member joins as a
    /// one-shot, never a locomotion or interaction clip, nor a looping middle part whose lead-in the animation set
    /// holds (leadInPresent: the set names LeadInOf(file) beside it), which plays only after that lead-in.
    inline Member FamilyMember(std::string_view family, std::string_view playingStem, std::string_view playingSet,
                               std::string_view file, bool female, bool creature = false, bool leadInPresent = false)
    {
        if (family.empty()) return Member::kNone;
        std::string n = Lower(file);
        for (auto& ch : n) {
            if (ch == '/') ch = '\\';
        }
        if (IsTransitionClip(n)) return Member::kNone;
        const std::string set = ReplacerSetOf(n);
        if (!set.empty() && set != playingSet) return Member::kNone;
        const bool femaleDir = n.find("\\female\\") != std::string::npos;
        const bool maleDir   = n.find("\\male\\") != std::string::npos;
        if (female ? (maleDir && !femaleDir && n.find("openanimationreplacer") != std::string::npos) : femaleDir) {
            return Member::kNone;
        }
        const auto bn   = BaseName(n);
        const auto last = BaseName(DirOf(n));
        if ((ContainsI(bn, "combat") || ContainsI(last, "combat")) && !ContainsI(family, "combat")) return Member::kNone;
        if (creature && (IsLocomotionClip(bn) || IsInteractionIdle(bn) || (leadInPresent && !LeadInOf(bn).empty()))) {
            return Member::kNone;
        }
        Member joins = Member::kNone;
        if (family == "mt_idle") {
            const bool inVariants = StartsWithI(last, "_variants_mt_idle");
            if (!inVariants && !StartsWithI(bn, "mt_idle")) return Member::kNone;
            joins = (EqualsI(last, "_variants_mt_idle") || EqualsI(bn, "mt_idle.hkx")) ? Member::kLoop : Member::kShot;
        } else {
            if (!StartsWithI(bn, family) && !EqualsI(last, std::string("_variants_") + std::string(playingStem))) {
                return Member::kNone;
            }
            joins = Member::kLoop;
        }
        return creature ? Member::kShot : joins;
    }

    // ── A take's window and clock ──

    inline constexpr float kBakeRate   = 20.0f;   ///< baked frames per second (posing interpolates between them)
    inline constexpr float kBakeMaxSec = 60.0f;   ///< the most of a clip one take bakes
    inline constexpr float kBlendSec   = 0.4f;    ///< a change of clip crossfades over this

    /// Where a take's window sits in its clip's cropped range: windowSec long from windowStart, its clock starting
    /// startSec into the window. kLoop and kPingPong only when the window is the whole clip.
    struct Window
    {
        ClipMode mode        = ClipMode::kLoop;
        bool     baseLoop    = false;   ///< the graph loops the clip (a base idle), whatever the window
        float    windowStart = 0.0f, windowSec = 0.0f, startSec = 0.0f;
    };

    /// The window for a clip of D cropped seconds now at s0, in the graph's mode: a base loop of at most kBakeMaxSec
    /// whole; a longer one kBakeMaxSec from s0 (its end then crossfades into its start); a one-shot the kBakeMaxSec
    /// ending at its end, or from s0 when more than that is left; a held one its current frame.
    inline Window ChooseWindow(ClipMode engine, float D, float s0) noexcept
    {
        Window w;
        w.baseLoop = IsLooping(engine);
        if (engine == ClipMode::kHeld) {
            w.mode        = ClipMode::kHeld;
            w.windowStart = s0;
            return w;
        }
        if (w.baseLoop && D <= kBakeMaxSec) {
            w.mode      = engine;
            w.windowSec = D;
            w.startSec  = s0;
            return w;
        }
        w.mode        = ClipMode::kToEnd;
        w.windowSec   = (std::min)(D, kBakeMaxSec);
        w.windowStart = (w.baseLoop || D - s0 >= w.windowSec) ? s0 : D - w.windowSec;
        w.startSec    = std::clamp(s0 - w.windowStart, 0.0f, w.windowSec);
        return w;
    }

    /// How many frames a window bakes: evenly over it, both ends included; a held one 2 of the same instant.
    inline int FrameCount(float windowSec) noexcept
    {
        return windowSec > 0.0f ? (std::max)(2, static_cast<int>(std::ceil(windowSec * kBakeRate)) + 1) : 2;
    }

    /// The cropped-range time frame f of frameCount samples; a long base loop's window wraps past D once at most.
    inline float FrameTime(float windowStart, float windowSec, int f, int frameCount, float D) noexcept
    {
        float u = windowStart + (frameCount > 1 ? windowSec * static_cast<float>(f) / static_cast<float>(frameCount - 1) : 0.0f);
        if (u > D) u -= D;
        return std::clamp(u, 0.0f, (std::max)(D, 0.0f));
    }

    /// The time into the window (seconds) an unwrapped clock is at, by the take's mode.
    inline float WindowTime(ClipMode mode, float windowSec, float startSec, float clock) noexcept
    {
        const float len = windowSec;
        if (mode == ClipMode::kHeld || !(len > 0.0f)) return std::clamp(startSec, 0.0f, (std::max)(len, 0.0f));
        switch (mode) {
        case ClipMode::kToEnd:
            return std::clamp(clock, 0.0f, len);
        case ClipMode::kPingPong: {
            float m = std::fmod(clock, 2.0f * len);
            if (m < 0.0f) m += 2.0f * len;
            return m <= len ? m : 2.0f * len - m;
        }
        default: {
            float m = std::fmod(clock, len);
            if (m < 0.0f) m += len;
            return m;
        }
        }
    }

    /// The animation's own time (seconds) a window time is at.
    inline float AnimTime(float lo, float croppedSec, float windowStart, float windowTime) noexcept
    {
        float u = windowStart + windowTime;
        if (u > croppedSec) u -= croppedSec;
        return lo + std::clamp(u, 0.0f, (std::max)(croppedSec, 0.0f));
    }

    /// The window time an animation time is at: AnimTime's inverse, clamped to the window.
    inline float WindowTimeAt(float lo, float croppedSec, float windowStart, float windowSec, float animTime) noexcept
    {
        float w = animTime - lo - windowStart;
        if (w < 0.0f && croppedSec > 0.0f) w += croppedSec;
        return std::clamp(w, 0.0f, (std::max)(windowSec, 0.0f));
    }

    /// The window time an animation time is at, unclamped: outside [0, windowSec] it is outside the window. A base
    /// loop's window may run on past the clip's end into its start (a long loop's, ChooseWindow), so for one an
    /// animation time before windowStart reads on from there.
    inline float WindowOffset(float lo, float croppedSec, float windowStart, bool baseLoop, float animTime) noexcept
    {
        float w = animTime - lo - windowStart;
        if (baseLoop && w < 0.0f && croppedSec > 0.0f) w += croppedSec;
        return w;
    }

    /// Whether a kToEnd take's clock has run its window out (backwards: down to its start).
    inline bool TakeEnded(ClipMode mode, float windowSec, float clock, float speed) noexcept
    {
        if (mode != ClipMode::kToEnd || !(windowSec > 0.0f)) return false;
        return speed >= 0.0f ? clock >= windowSec : clock <= 0.0f;
    }

    /// The two baked frames a window time falls between, and how far along (0-1).
    struct FramePos
    {
        int   f0 = 0, f1 = 0;
        float u  = 0.0f;
    };
    inline FramePos FrameAt(float windowTime, float windowSec, int frameCount) noexcept
    {
        FramePos p;
        if (frameCount < 1) return p;
        const float fpos = windowSec > 0.0f ? windowTime / windowSec * static_cast<float>(frameCount - 1) : 0.0f;
        p.f0             = std::clamp(static_cast<int>(std::floor(fpos)), 0, frameCount - 1);
        p.f1             = (std::min)(p.f0 + 1, frameCount - 1);
        p.u              = std::clamp(fpos - static_cast<float>(p.f0), 0.0f, 1.0f);
        return p;
    }

    /// The crossfade's weight of the new pose, elapsed seconds into kBlendSec (smoothstep; 1 once it is over).
    inline float BlendWeight(float elapsedSec) noexcept
    {
        const float c = std::clamp(elapsedSec / kBlendSec, 0.0f, 1.0f);
        return c * c * (3.0f - 2.0f * c);
    }

    /// A playback speed the view takes as given (finite, 0.01-10 either way); others play at 1.
    inline bool PlausibleSpeed(float s) noexcept
    {
        return std::isfinite(s) && std::fabs(s) >= 0.01f && std::fabs(s) <= 10.0f;
    }

    // ── The idle cycle ──

    /// The paused base loop breaks into a one-shot this long after it starts (uniform between the two).
    inline constexpr float kShotGapMinSec = 8.0f;
    inline constexpr float kShotGapMaxSec = 20.0f;

    /// Whether a take that played to its end plays again rather than giving way to a random other base loop variant:
    /// a pinned idle always, and while the game runs a base loop, which is the engine's own (it loops what it plays,
    /// and the follow of its clip keeps the view's clock on it). A one-shot that ends goes back to a base loop.
    inline bool ReplayAtEnd(bool pinned, bool paused, bool baseLoop) noexcept { return pinned || (!paused && baseLoop); }

    /// Whether the paused cycle may break into a take: a one-shot that plays on its own, never a transition.
    inline bool CanBreakInto(bool baseLoop, ClipMode mode, std::string_view name) noexcept
    {
        return !baseLoop && mode != ClipMode::kHeld && !IsTransitionClip(name);
    }

    /// What the follow of the engine's clip does (while the game runs and the NPC's bones are frozen).
    enum class Follow
    {
        kKeep,       ///< leave the view's own cycle alone
        kResync,     ///< the current take: set its clock to the engine's
        kSwitch,     ///< another take: crossfade to it at the engine's time
        kResample,   ///< a held take whose frame the engine moved: sample that frame again
        kRebake,     ///< a clip the view does not hold (or past its window): bake it
    };

    /// found: a take holds the engine's clip (isCurrent: the one playing); moved: the engine's time for it changed
    /// since the last check; inWindow: that time is inside the take's window; refused: this clip is the last one
    /// whose bake was refused (the caller remembers one). A graph that stands still never pulls the cycle back to its clip.
    inline Follow DecideFollow(bool found, bool isCurrent, bool moved, ClipMode mode, bool inWindow, bool refused) noexcept
    {
        if (!found) return refused ? Follow::kKeep : Follow::kRebake;
        if (!moved) return Follow::kKeep;
        if (mode == ClipMode::kHeld) return Follow::kResample;
        if (inWindow) return isCurrent ? Follow::kResync : Follow::kSwitch;
        return refused ? Follow::kKeep : Follow::kRebake;
    }

    // ── Layered loops ──
    // The graph can play several loops at once, each driving part of the body (a readied spell's idle in each hand):
    // the view bakes each and gives every driven node to one of them.

    /// Whether a clip the graph plays beside the base clip (otherName, in mode other) is one of its layers: it loops,
    /// does not move the actor about, and the base loops or is a one-shot played to its end over it (an idle over a
    /// shield or weapon stance, whose loop keeps the arm it holds). A held clip or a walk, run or turn blend never is.
    inline bool IsLayer(ClipMode base, ClipMode other, std::string_view otherName) noexcept
    {
        return (IsLooping(base) || base == ClipMode::kToEnd) && IsLooping(other) && !IsLocomotionClip(otherName);
    }

    inline constexpr std::size_t kMaxLayers = 3;   ///< loops layered beside the base loop, at most

    /// The fraction (0-1) of its cropped range an animation time is at.
    inline float PhaseOf(float lo, float croppedSec, float animTime) noexcept
    {
        if (!(croppedSec > 0.0f) || !std::isfinite(animTime)) return 0.0f;
        float f = std::fmod((animTime - lo) / croppedSec, 1.0f);
        if (f < 0.0f) f += 1.0f;
        return f;
    }

    inline constexpr float kPhaseLockTol = 0.01f;   ///< a phase gap this small reads as the graph's sync

    /// Whether a layer at phase other is held to the base loop's phase by the graph (a synced blend runs its clips at
    /// one normalized time): then it plays at the base's phase, never on a clock of its own. Wraps at 1.
    inline bool PhaseLocked(float base, float other) noexcept
    {
        const float gap = std::fabs(base - other);
        return (std::min)(gap, 1.0f - gap) < kPhaseLockTol;
    }

    inline constexpr float kPhaseDriftTol = 0.05f;   ///< a locked layer this far from the base's phase was never synced

    /// Whether a layer held to the base loop's phase has drifted from it in the graph (two loops that matched by chance
    /// when the lock was read): then it takes its own clock. Wider than kPhaseLockTol, so a synced pair never flaps.
    inline bool PhaseDrifted(float base, float other) noexcept
    {
        const float gap = std::fabs(base - other);
        return !((std::min)(gap, 1.0f - gap) <= kPhaseDriftTol);
    }

    /// How a take's local for a node compares with the node's live local (the self-check's gaps: the largest rotation
    /// matrix entry gap, the same if the convention were transposed, and the translation gap in game units). driven
    /// false: the take does not drive the node.
    struct NodeError
    {
        float rot = 0.0f, rotT = 0.0f, move = 0.0f;
        bool  driven = false;
    };

    inline constexpr float kCloseRot  = 0.05f;   ///< the self-check's rotation tolerance
    inline constexpr float kCloseMove = 1.0f;    ///< the self-check's translation tolerance, game units

    /// A gap in self-check tolerances: below 1 the node is close. Infinite for a gap that is not a number.
    inline float ToleranceUnits(const NodeError& e) noexcept
    {
        const float u = (std::max)(e.rot / kCloseRot, e.move / kCloseMove);
        return std::isfinite(u) && u >= 0.0f ? u : std::numeric_limits<float>::infinity();
    }

    /// A layer takes a node from the base loop only when it is closer by more than this many tolerances, so the body
    /// both clips pose alike stays with the base loop.
    inline constexpr float kLayerMargin = 1.0f;

    /// Per node, the take that drives it, from err[take][node] (take 0 the base loop, the rest its layers): the base
    /// loop where it drives the node unless a layer is closer by more than kLayerMargin; where it does not, the closest
    /// layer that does (the first of equals). -1 where no take drives the node.
    inline std::vector<int> AssignLayers(const std::vector<std::vector<NodeError>>& err)
    {
        std::size_t nodes = 0;
        for (const auto& t : err) nodes = (std::max)(nodes, t.size());
        std::vector<int> out(nodes, -1);
        for (std::size_t n = 0; n < nodes; ++n) {
            const auto at = [&](std::size_t t) -> const NodeError* {
                return n < err[t].size() && err[t][n].driven ? &err[t][n] : nullptr;
            };
            int   best  = -1;
            float bestU = std::numeric_limits<float>::infinity();
            for (std::size_t t = 1; t < err.size(); ++t) {
                const NodeError* e = at(t);
                if (!e) continue;
                const float u = ToleranceUnits(*e);
                if (best < 0 || u < bestU) {
                    best  = static_cast<int>(t);
                    bestU = u;
                }
            }
            if (!err.empty() && at(0)) {
                const float baseU = ToleranceUnits(*at(0));
                out[n]            = (best > 0 && bestU + kLayerMargin < baseU) ? best : 0;
            } else {
                out[n] = best;
            }
        }
        return out;
    }

    /// The fewest nodes a layer must drive to be kept (a hand's idle drives an arm and its fingers): a loop that would
    /// drive a stray bone or two leaves them to the base loop, so it neither holds the idle cycle nor plays alone.
    inline constexpr int kMinLayerNodes = 6;

    /// Gives the nodes of every layer with fewer than minNodes (AssignLayers' assign) to the base loop where it drives
    /// them (err[0]), else to none. err[take][node] as AssignLayers'. Returns how many nodes each take keeps.
    inline std::vector<int> DropThinLayers(std::vector<int>& assign, const std::vector<std::vector<NodeError>>& err,
                                           int minNodes = kMinLayerNodes)
    {
        std::vector<int> drives(err.empty() ? 1 : err.size(), 0);
        for (const int t : assign) {
            if (t >= 0 && static_cast<std::size_t>(t) < drives.size()) ++drives[static_cast<std::size_t>(t)];
        }
        for (std::size_t n = 0; n < assign.size(); ++n) {
            const int t = assign[n];
            if (t <= 0 || static_cast<std::size_t>(t) >= drives.size() || drives[static_cast<std::size_t>(t)] >= minNodes) {
                continue;
            }
            const bool baseDrives = !err.empty() && n < err[0].size() && err[0][n].driven;
            assign[n]             = baseDrives ? 0 : -1;
        }
        std::fill(drives.begin(), drives.end(), 0);
        for (const int t : assign) {
            if (t >= 0 && static_cast<std::size_t>(t) < drives.size()) ++drives[static_cast<std::size_t>(t)];
        }
        return drives;
    }

    /// The self-check over the pose the takes draw together: each node by the take that drives it (assign, from
    /// AssignLayers or carried), how many sit within tolerance and the worst gaps.
    struct PoseSummary
    {
        int   compared = 0, close = 0;
        float worstRot = 0.0f, worstRotT = 0.0f, worstMove = 0.0f;
    };
    inline PoseSummary SummarizeAssigned(const std::vector<std::vector<NodeError>>& err, const std::vector<int>& assign)
    {
        PoseSummary s;
        for (std::size_t n = 0; n < assign.size(); ++n) {
            const int t = assign[n];
            if (t < 0 || static_cast<std::size_t>(t) >= err.size() || n >= err[static_cast<std::size_t>(t)].size()) continue;
            const NodeError& e = err[static_cast<std::size_t>(t)][n];
            if (!e.driven) continue;
            ++s.compared;
            if (e.rot < kCloseRot && e.move < kCloseMove) ++s.close;
            s.worstRot  = (std::max)(s.worstRot, e.rot);
            s.worstRotT = (std::max)(s.worstRotT, e.rotT);
            s.worstMove = (std::max)(s.worstMove, e.move);
        }
        return s;
    }

    /// How well a node of the actor's 3D serves as the bone whose pose tells when the game stops animating them, for
    /// a skeleton with no "NPC Head" (a creature's): 4 a head (Canine_Head, Sabrecat_Head [Head]), 3 a skull (the
    /// horse's HorseScull), 2 a neck, 1 a spine; 0 none. The caller takes the shallowest of the best rank.
    inline int SignatureNodeRank(std::string_view name) noexcept
    {
        if (ContainsI(name, "head")) return 4;
        if (ContainsI(name, "skull") || ContainsI(name, "scull")) return 3;
        if (ContainsI(name, "neck")) return 2;
        if (ContainsI(name, "spine")) return 1;
        return 0;
    }

    // ── The idle hint ──

    /// The choice that holds the pose while the game does not animate the NPC ("" runs the idle cycle; any other
    /// value is a clip name, which no clip file is called: those end .hkx).
    inline constexpr std::string_view kIdleStill = "still";

    /// The choice that loops an animation file in place of the graph's clips: "file:" then the file's path under
    /// Data (meshes\ may be left off). Played paused or not, with no idle cycle and no hand-back.
    inline constexpr std::string_view kIdleFilePrefix = "file:";

    /// The Data-relative path (backslashes, "meshes\..." prefixed) a file choice names; "" for any other choice, a
    /// path that is not an .hkx, and one that could leave Data (a drive, a root, or a ".." part).
    inline std::string IdleFilePath(std::string_view idle)
    {
        if (!StartsWithI(idle, kIdleFilePrefix)) return {};
        std::string p(AsciiText::Trim(idle.substr(kIdleFilePrefix.size())));
        std::replace(p.begin(), p.end(), '/', '\\');
        if (p.empty() || p.front() == '\\' || p.find(':') != std::string::npos || !EndsWithI(p, ".hkx")) return {};
        for (std::size_t at = 0; at <= p.size();) {
            const std::size_t end = (std::min)(p.find('\\', at), p.size());
            if (end - at == 2 && p.compare(at, 2, "..") == 0) return {};
            at = end + 1;
        }
        if (!StartsWithI(p, "meshes\\")) p.insert(0, "meshes\\");
        return p;
    }

    /// Whether the idles query offers a clip: one that plays on its own, never a transition.
    inline bool PickerOffers(ClipMode mode, std::string_view name) noexcept
    {
        const auto file = BaseName(name);
        return mode != ClipMode::kHeld && !IsTransitionClip(name) && !file.empty() && !EqualsI(file, ".hkx");
    }

    /// The idles query's label for a clip: its file name less the extension, underscores as spaces.
    inline std::string IdleLabel(std::string_view name)
    {
        std::string l(StripHkx(BaseName(name)));
        for (auto& ch : l) {
            if (ch == '_') ch = ' ';
        }
        while (!l.empty() && l.front() == ' ') l.erase(l.begin());
        while (!l.empty() && l.back() == ' ') l.pop_back();
        return l;
    }

    // ── The shared idle pool ──

    inline constexpr std::size_t kPoolBytes       = std::size_t{ 48 } << 20;   ///< every key's frames together
    inline constexpr std::size_t kPoolLoopsPerKey = 8;                         ///< base loop variants per key
    inline constexpr std::size_t kPoolShotsPerKey = 16;                        ///< one-shots per key

    /// Whether the pool keeps a take: one the cycle plays on its own (not held), of a family (creature as
    /// IdleFamily), not a transition.
    inline bool PoolAccepts(ClipMode mode, std::string_view name, bool creature = false)
    {
        return mode != ClipMode::kHeld && !IsTransitionClip(name) && !IdleFamily(name, creature).empty();
    }

    /// The pool's key: the behavior project (the skeleton the frames fit), sex, character scale, the replacer set
    /// the frames came from (ReplacerSetOf; "" for none: a replacer's conditional idles stay with NPCs who play from
    /// that set) and idle family. "" for a take of an unknown set (kSetUnknown), which the pool never keeps.
    inline std::string PoolKey(std::string_view project, bool female, float scale, std::string_view set,
                               std::string_view family)
    {
        if (set == kSetUnknown) return {};
        char sc[32];
        std::snprintf(sc, sizeof(sc), "%.2f", static_cast<double>(scale));
        std::string k = Lower(project);
        k += '|';
        k += female ? 'f' : 'm';
        k += '|';
        k += sc;
        k += '|';
        k += Lower(set);
        k += '|';
        k += family;
        return k;
    }

    /// Whether a pool key holds idles an NPC of this project, sex and scale playing from `set` can play: its own
    /// set's, or those of no replacer (any family).
    inline bool PoolKeyFits(std::string_view key, std::string_view project, bool female, float scale, std::string_view set)
    {
        if (set == kSetUnknown) set = {};
        const std::string base = PoolKey(project, female, scale, "", "");   // ends "||"
        const std::string head = base.substr(0, base.size() - 1);          // project|sex|scale|
        if (key.size() < head.size() || key.compare(0, head.size(), head) != 0) return false;
        const auto rest = key.substr(head.size());
        const auto bar  = rest.find('|');
        if (bar == std::string_view::npos) return false;
        const auto keySet = rest.substr(0, bar);
        return keySet.empty() || keySet == Lower(set);
    }

    /// One pooled take, as the pool's limits see it.
    struct PoolTake
    {
        bool        baseLoop = false;
        std::size_t bytes    = 0;
    };

    /// After the newest take (the last) joined a key: which of the key's takes to drop, ascending. Past its kind's
    /// cap the oldest of that kind goes, then the oldest others while the key alone holds more than budget bytes.
    /// The newest is never dropped.
    inline std::vector<std::size_t> EvictWithinKey(const std::vector<PoolTake>& takes, std::size_t budget)
    {
        std::vector<std::size_t> drop;
        if (takes.size() < 2) return drop;
        const std::size_t newest = takes.size() - 1;
        const bool        kind   = takes[newest].baseLoop;
        const std::size_t cap    = kind ? kPoolLoopsPerKey : kPoolShotsPerKey;
        std::size_t       same   = 0;
        for (const auto& t : takes) same += t.baseLoop == kind ? 1 : 0;
        for (std::size_t i = 0; i < newest && same > cap; ++i) {
            if (takes[i].baseLoop == kind) {
                drop.push_back(i);
                --same;
            }
        }
        std::size_t bytes = 0;
        for (std::size_t i = 0; i < takes.size(); ++i) {
            if (std::find(drop.begin(), drop.end(), i) == drop.end()) bytes += takes[i].bytes;
        }
        for (std::size_t i = 0; i < newest && bytes > budget; ++i) {
            if (std::find(drop.begin(), drop.end(), i) != drop.end()) continue;
            drop.push_back(i);
            bytes -= takes[i].bytes;
        }
        std::sort(drop.begin(), drop.end());
        return drop;
    }

    /// Whether a key holding sameKind takes of a take's kind (base loops or one-shots) takes another. A full kind
    /// keeps what it has: a new idle is not added in place of an old one.
    inline bool PoolHasRoom(std::size_t sameKind, bool baseLoop) noexcept
    {
        return sameKind < (baseLoop ? kPoolLoopsPerKey : kPoolShotsPerKey);
    }

    /// Whether the running game's idle is worth a harvest into the pool: the pool would keep it, lacks it, has room
    /// for its kind, and it was not tried this view session (a failed try is not repeated every few seconds).
    inline bool HarvestWants(ClipMode mode, std::string_view name, bool pooled, bool triedThisSession, bool room,
                             bool creature = false)
    {
        return room && !pooled && !triedThisSession && PoolAccepts(mode, name, creature);
    }

    /// Marks moving every rig node that composes on a moving one. from(j) is the node j composes on (the node it is
    /// attached to, else its parent; 0 or less for the root or none). The rig is parents first, so one sweep reaches
    /// every node under a moving one; a node attached to a later one takes another. Sweeps until nothing changes, at
    /// most maxSweeps times. Node 0 (the root) is never marked.
    template <class From>
    inline void PropagateMoves(std::vector<std::uint8_t>& moves, From&& from, int maxSweeps = 8)
    {
        for (int sweep = 0; sweep < maxSweeps; ++sweep) {
            bool changed = false;
            for (std::size_t j = 1; j < moves.size(); ++j) {
                if (moves[j]) continue;
                const std::int32_t f = from(j);
                if (f > 0 && static_cast<std::size_t>(f) < moves.size() && moves[static_cast<std::size_t>(f)]) {
                    moves[j] = 1;
                    changed  = true;
                }
            }
            if (!changed) return;
        }
    }
}
