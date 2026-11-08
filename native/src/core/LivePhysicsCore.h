#pragma once

// Phantasm - LivePhysicsCore: the actor view's own physics while it follows the game (the live simulation in
// MannequinRenderer.cpp, StepLiveSim), and how a piece's SMP file is found from the armour it came from,
// engine-free (native/build.ps1 -Tests). During play the view cannot see FSMP's output (FSMP writes a bone's world
// at a point of the frame the view never reads), so the bones an SMP file or a CBPC config names are simulated by
// the clip player's own springs (MannequinSpringCore, MannequinPhysicsCore), driven by the live skeleton. The rules
// here: which nodes the simulation poses and in what order, when its springs start again from rest, where their
// state is kept, and when the game's own physics is seen moving a bone so the view leaves it alone.
//
// Author: Severause

#include "MannequinPhysicsCore.h"
#include "RestPoseCore.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>

namespace Phantasm::LivePhysicsCore
{
    using Xf = RestPoseCore::Xf;
    namespace MS = MannequinSpringCore;
    namespace MP = MannequinPhysicsCore;

    /// How far a transform may move and still count as the same: per rotation entry, in game units, and in scale.
    /// A float world 70000 units from the origin holds its translation to about 0.01.
    inline constexpr float kRotTolerance   = 1e-3f;
    inline constexpr float kMoveTolerance  = 0.05f;
    inline constexpr float kScaleTolerance = 1e-3f;

    inline bool NearXf(const Xf& a, const Xf& b) noexcept
    {
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                if (!(std::fabs(a.rot[r][c] - b.rot[r][c]) <= kRotTolerance)) return false;
            }
            if (!(std::fabs(a.t[r] - b.t[r]) <= kMoveTolerance)) return false;
        }
        return std::fabs(a.s - b.s) <= kScaleTolerance;
    }

    /// True when `world` is not what the engine's update gives a node with this parent world and local: something
    /// wrote the world over it (FSMP writes a simulated bone's world, never its local). A non-finite input reads as
    /// written, so the caller keeps the live world.
    inline bool WrittenOverLocal(const Xf& world, const Xf& parentWorld, const Xf& local) noexcept
    {
        if (!RestPoseCore::IsFinite(world) || !RestPoseCore::IsFinite(parentWorld) || !RestPoseCore::IsFinite(local)) {
            return true;
        }
        return !NearXf(world, RestPoseCore::Compose(parentWorld, local));
    }

    // ── Which nodes the simulation poses ──

    /// A node's part in the simulation: kLive keeps the world the game gives it; kSimulated takes its rest on the
    /// node above it (its local on that node's posed world) displaced by its spring; kCarried takes only that rest,
    /// so a node under a simulated one (a bone no file simulates, an SMP anchor) moves with it.
    enum class NodeRole : std::uint8_t { kLive, kCarried, kSimulated };

    struct ChainPlan
    {
        std::vector<NodeRole> role;     // per node
        std::vector<int>      order;    // the nodes to pose, parents first (roles other than kLive only)
        int                   chains = 0;   // simulated nodes with no simulated node above them
    };

    /// Plans the nodes `parent` describes (each node's parent as an index into the same list, -1 for one whose
    /// parent is not in it; a parent loop is cut). `simulated` flags the nodes a physics file names.
    inline ChainPlan PlanChains(const std::vector<int>& parent, const std::vector<std::uint8_t>& simulated)
    {
        const std::size_t n = parent.size();
        ChainPlan         plan;
        plan.role.assign(n, NodeRole::kLive);
        std::vector<int> depth(n, 0);
        for (std::size_t i = 0; i < n; ++i) {
            const bool self = i < simulated.size() && simulated[i];
            bool       above = false;
            int        d     = 0;
            for (int p = parent[i], guard = 0; p >= 0 && static_cast<std::size_t>(p) < n && guard < 256; p = parent[static_cast<std::size_t>(p)], ++guard) {
                ++d;
                above = above || (static_cast<std::size_t>(p) < simulated.size() && simulated[static_cast<std::size_t>(p)]);
            }
            depth[i] = d;
            if (self) plan.role[i] = NodeRole::kSimulated;
            else if (above) plan.role[i] = NodeRole::kCarried;
            if (self && !above) ++plan.chains;
            if (plan.role[i] != NodeRole::kLive) plan.order.push_back(static_cast<int>(i));
        }
        std::stable_sort(plan.order.begin(), plan.order.end(),
                         [&](int a, int b) { return depth[static_cast<std::size_t>(a)] < depth[static_cast<std::size_t>(b)]; });
        return plan;
    }

    // ── When the springs start again ──

    /// A root move between two ticks longer than this (game units) is a teleport (fast travel, a door, a
    /// MoveTo), not motion: the springs start again from rest. A rider on a sprinting horse covers about 20 a tick.
    inline constexpr float kTeleportDistance = 200.0f;
    /// The state is kept relative to a fixed origin, so the actor's own movement swings what hangs on them; the
    /// origin follows them once they are this far from it (floats far from the origin are coarse).
    inline constexpr float kRebaseDistance = 2048.0f;

    struct StepPlan
    {
        bool  reset = false;   ///< every spring back at rest this tick
        float dt    = 0.0f;    ///< seconds to step (0: pose only)
    };

    /// The tick's step: `elapsed` seconds since the last one (negative: none yet), `jump` how far the root moved.
    /// A gap the springs would not catch up on (MannequinSpringCore::kResetGap) resets them, as a teleport does.
    inline StepPlan PlanStep(float elapsed, float jump) noexcept
    {
        StepPlan p;
        if (!(elapsed >= 0.0f) || elapsed > MS::kResetGap || !(jump <= kTeleportDistance)) {
            p.reset = true;
            return p;
        }
        p.dt = elapsed;
        return p;
    }

    inline bool NeedsRebase(float distanceFromOrigin) noexcept { return !(distanceFromOrigin <= kRebaseDistance); }

    /// Moves the springs' state to an origin `delta` further on (state relative to the old origin, minus delta): a
    /// pure change of frame, so nothing swings.
    inline void Rebase(MS::SpringState& st, MP::CbpcState& cs, MS::Vec3 delta) noexcept
    {
        st.tip    = st.tip - delta;
        cs.pos    = cs.pos - delta;
        cs.posRot = cs.posRot - delta;
    }

    inline void ResetSpring(MS::SpringState& st, MP::CbpcState& cs) noexcept
    {
        st.primed = false;
        cs.primed = false;
    }

    // ── The game's own physics ──

    /// How many ticks a CBPC bone's local must hold still before the view takes it over: CBPC moves a bone by its
    /// local, which the live read already carries, so a bone whose local moved recently is CBPC's, and one CBPC
    /// leaves (out of its range, paused, or not running for this actor) settles into the view's own spring.
    inline constexpr int kCbpcQuietTicks = 30;
    /// How many ticks in a row its local must move before a bone the view took over is the game's again, so one
    /// tick's noise at the tolerance's edge does not flip it.
    inline constexpr int kCbpcMovingTicks = 3;

    struct LocalWatch
    {
        Xf   last;
        bool seen   = false;
        bool game   = true;   // the game's CBPC drives it (an unwatched bone is the game's until shown otherwise)
        int  quiet  = 0;      // ticks since the local last moved
        int  moving = 0;      // ticks in a row it has moved
    };

    /// Notes a CBPC bone's local this tick; true while the game's CBPC moves it.
    inline bool GameMovesByLocal(LocalWatch& w, const Xf& local) noexcept
    {
        const bool moved = w.seen && !NearXf(w.last, local);
        w.last           = local;
        w.seen           = true;
        if (moved) {
            w.quiet = 0;
            if (w.moving < kCbpcMovingTicks) ++w.moving;
        } else {
            w.moving = 0;
            if (w.quiet < kCbpcQuietTicks) ++w.quiet;
        }
        if (w.game && w.quiet >= kCbpcQuietTicks) w.game = false;
        else if (!w.game && w.moving >= kCbpcMovingTicks) w.game = true;
        return w.game;
    }

    /// Whether a spring's state carries from one player to the other (the live simulation and the clip player
    /// share the model): the same kind of spring, already moving.
    inline bool CarriesState(bool sameSource, bool primed) noexcept { return sameSource && primed; }

    /// Whether the live simulation's state still stands for the actor, `sinceStep` seconds after its last step
    /// (negative: it never stepped): past the gap a step would reset at, it is stale and is not carried.
    inline bool StateFresh(float sinceStep) noexcept { return sinceStep >= 0.0f && sinceStep <= MS::kResetGap; }

    // ── An armour piece's SMP file ──

    /// The forms the engine names an attached armour piece's node after: `<addon> (<ARMA id>)[<n>]/<armour>
    /// (<ARMO id>) [<weight>%]`, each id 8 hex digits (the names may be empty, as without EditorIDs, or hold
    /// parentheses of their own). ok when the addon id was found; armor is 0 when its part is missing.
    struct AttachedArmorNode
    {
        bool          ok    = false;
        std::uint32_t addon = 0;
        std::uint32_t armor = 0;
    };

    namespace detail
    {
        // True, with the id in out, when s[at] is '(' followed by 8 hex digits and then `close`.
        inline bool HexIdAt(std::string_view s, std::size_t at, std::string_view close, std::uint32_t& out)
        {
            if (at >= s.size() || s[at] != '(' || at + 9 + close.size() > s.size()) return false;
            std::uint32_t v = 0;
            for (std::size_t i = at + 1; i < at + 9; ++i) {
                const unsigned char ch = static_cast<unsigned char>(s[i]);
                if (!std::isxdigit(ch)) return false;
                v = (v << 4) | static_cast<std::uint32_t>(std::isdigit(ch) ? ch - '0' : (std::tolower(ch) - 'a' + 10));
            }
            if (s.substr(at + 9, close.size()) != close) return false;
            out = v;
            return true;
        }
    }

    inline AttachedArmorNode ParseAttachedArmorNode(std::string_view name)
    {
        AttachedArmorNode out;
        std::size_t       after = std::string_view::npos;
        for (std::size_t at = name.find('('); at != std::string_view::npos; at = name.find('(', at + 1)) {
            if (detail::HexIdAt(name, at, ")[", out.addon)) {
                out.ok = true;
                after  = at + 11;
                break;
            }
        }
        if (!out.ok) return out;
        for (std::size_t at = name.find('(', after); at != std::string_view::npos; at = name.find('(', at + 1)) {
            if (detail::HexIdAt(name, at, ") [", out.armor)) break;
        }
        return out;
    }
}
