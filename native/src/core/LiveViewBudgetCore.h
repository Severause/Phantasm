#pragma once

// Phantasm - LiveViewBudgetCore: which of the drawing views a carrier draws in a frame, engine-free (native/build.ps1
// -Tests). A carrier draws its views one after another on the main thread, inside the game's frame, so the views share
// a time budget per frame: the newest draws every frame, the others take turns while the frame's draws stay under the
// budget, the one that has waited longest first, and one that has waited the mode's maxWait frames draws whatever the
// budget says. The budget is the player's choice (Phantasm.ini [LiveViews] Budget, see Policy).
//
// Author: Severause

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace Phantasm::LiveViewBudgetCore
{
    /// The CPU time a carrier's draws may take in one frame (ms) in the balanced mode: flat, and VR, whose frame is
    /// shorter.
    inline constexpr double kFrameBudgetMs   = 6.0;
    inline constexpr double kFrameBudgetMsVR = 3.0;
    /// A view that has not drawn for this many frames draws in the next, over the budget or not (balanced).
    inline constexpr std::uint32_t kMaxWaitFrames = 6;

    /// How much of a frame the views may take. smooth: room for every view of a typical page to draw every frame while
    /// the game is paused (a menu holding the world leaves the frame time to spare), balanced while it runs; balanced:
    /// the budget 0.4.0 shipped; light: for a machine where the views cost the game's frame rate.
    enum class Mode
    {
        kSmooth,
        kBalanced,
        kLight,
    };

    struct Policy
    {
        Mode          mode     = Mode::kSmooth;   // the setting
        Mode          applied  = Mode::kSmooth;   // the mode whose budget is in force (smooth runs balanced unpaused)
        double        budgetMs = 14.0;
        std::uint32_t maxWait  = 2;
    };

    /// The mode a setting names (any case; "default" and "" are smooth); unknown text is smooth too, `known` false.
    inline Mode ParseMode(std::string_view text, bool* known = nullptr) noexcept
    {
        std::string_view t = text;
        while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front()))) t.remove_prefix(1);
        while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back()))) t.remove_suffix(1);
        auto eq = [&](std::string_view w) {
            if (t.size() != w.size()) return false;
            for (std::size_t i = 0; i < t.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(t[i])) != w[i]) return false;
            }
            return true;
        };
        if (known) *known = true;
        if (eq("balanced")) return Mode::kBalanced;
        if (eq("light")) return Mode::kLight;
        if (t.empty() || eq("smooth") || eq("default")) return Mode::kSmooth;
        if (known) *known = false;
        return Mode::kSmooth;
    }

    inline const char* ModeName(Mode m) noexcept
    {
        switch (m) {
        case Mode::kBalanced: return "balanced";
        case Mode::kLight: return "light";
        default: return "smooth";
        }
    }

    /// The mode's budget and wait for the platform. smooth takes balanced's while the game runs (not paused): the views
    /// may then share the frame with play, as in SA's main view or a HUD popup. overrideMs > 0 replaces the budget
    /// (clamped to 0.5-50 ms) paused or not; the wait stays the applied mode's.
    inline Policy PolicyFor(Mode mode, bool vr, double overrideMs = 0.0, bool paused = true) noexcept
    {
        Policy p;
        p.mode    = mode;
        p.applied = mode == Mode::kSmooth && !paused ? Mode::kBalanced : mode;
        switch (p.applied) {
        case Mode::kBalanced:
            p.budgetMs = vr ? kFrameBudgetMsVR : kFrameBudgetMs;
            p.maxWait  = kMaxWaitFrames;
            break;
        case Mode::kLight:
            p.budgetMs = vr ? 1.5 : 3.0;
            p.maxWait  = 10;
            break;
        default:
            p.budgetMs = vr ? 4.0 : 14.0;   // VR's frame is 11 ms at 90 Hz, and the headset needs every one
            p.maxWait  = 2;
            break;
        }
        if (overrideMs > 0.0) p.budgetMs = std::clamp(overrideMs, 0.5, 50.0);
        return p;
    }

    /// The order a frame offers its views a turn: drawers newest first, as ids. The newest first, then the others
    /// starting after `lastServed` (the last of them given a turn), wrapping; when lastServed is not among them, from
    /// the second newest.
    template <class Id>
    std::vector<Id> TurnOrder(const std::vector<Id>& newestFirst, Id lastServed)
    {
        std::vector<Id> out;
        if (newestFirst.empty()) return out;
        out.reserve(newestFirst.size());
        out.push_back(newestFirst.front());
        const std::size_t others = newestFirst.size() - 1;
        if (others == 0) return out;
        std::size_t start = 0;   // among the others (newestFirst[1..])
        for (std::size_t i = 0; i < others; ++i) {
            if (newestFirst[i + 1] == lastServed) {
                start = (i + 1) % others;
                break;
            }
        }
        for (std::size_t k = 0; k < others; ++k) out.push_back(newestFirst[1 + (start + k) % others]);
        return out;
    }

    /// TurnOrder with the others re-ordered by how long each has waited, longest first (the round-robin order breaks
    /// ties), so a view that missed frames is offered the next spare time before one that just drew: the views even
    /// out instead of one starving. `waited[i]` is newestFirst[i]'s frames without a turn.
    template <class Id>
    std::vector<Id> FairTurnOrder(const std::vector<Id>& newestFirst, const std::vector<std::uint32_t>& waited,
                                  Id lastServed)
    {
        auto out = TurnOrder(newestFirst, lastServed);
        if (out.size() < 3 || waited.size() != newestFirst.size()) return out;
        auto waitOf = [&](Id id) {
            for (std::size_t i = 0; i < newestFirst.size(); ++i) {
                if (newestFirst[i] == id) return waited[i];
            }
            return std::uint32_t{ 0 };
        };
        std::stable_sort(out.begin() + 1, out.end(), [&](Id a, Id b) { return waitOf(a) > waitOf(b); });
        return out;
    }

    /// Whether the view at `turn` (0 = the newest) draws, `spentMs` into the frame's draws, having not drawn for
    /// `waitedFrames` frames.
    inline bool DrawsTurn(std::size_t turn, double spentMs, double budgetMs, std::uint32_t waitedFrames,
                          std::uint32_t maxWait = kMaxWaitFrames) noexcept
    {
        return turn == 0 || spentMs < budgetMs || waitedFrames >= maxWait;
    }
}
