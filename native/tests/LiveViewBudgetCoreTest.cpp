// Phantasm - native self-tests: LiveViewBudgetCore (which of the drawing views a carrier draws in a frame) and
// ViewStatsCore (a view's frame rates).
//
// Author: Severause

#include "LiveViewBudgetCore.h"
#include "TestHarness.h"
#include "ViewStatsCore.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace Phantasm::Tests
{
    void RunLiveViewBudgetCoreTests()
    {
        namespace LB = Phantasm::LiveViewBudgetCore;
        using V      = std::vector<std::uint32_t>;

        Check(LB::TurnOrder<std::uint32_t>({}, 0).empty(), "turns: no views, no turns");
        Check(LB::TurnOrder<std::uint32_t>({ 7 }, 0) == V{ 7 }, "turns: one view is the whole order");
        Check(LB::TurnOrder<std::uint32_t>({ 9, 8, 7, 6 }, 0) == V{ 9, 8, 7, 6 },
              "turns: with none served yet, the newest then the others newest first");
        Check(LB::TurnOrder<std::uint32_t>({ 9, 8, 7, 6 }, 8) == V{ 9, 7, 6, 8 },
              "turns: the others start after the last one served");
        Check(LB::TurnOrder<std::uint32_t>({ 9, 8, 7, 6 }, 6) == V{ 9, 8, 7, 6 }, "turns: after the oldest, they wrap");
        Check(LB::TurnOrder<std::uint32_t>({ 9, 8, 7 }, 9) == V{ 9, 8, 7 },
              "turns: the newest served last time changes nothing, it draws first anyway");
        Check(LB::TurnOrder<std::uint32_t>({ 9, 8, 7 }, 42) == V{ 9, 8, 7 }, "turns: a served view now closed starts over");

        Check(LB::FairTurnOrder<std::uint32_t>({ 9, 8, 7, 6 }, { 0, 0, 3, 1 }, 0) == V{ 9, 7, 6, 8 },
              "fair: the others are offered a turn longest-waited first, the newest still first");
        Check(LB::FairTurnOrder<std::uint32_t>({ 9, 8, 7, 6 }, { 0, 2, 2, 2 }, 8) == V{ 9, 7, 6, 8 },
              "fair: equal waits keep the round-robin order");
        Check(LB::FairTurnOrder<std::uint32_t>({ 9, 8 }, { 0, 5 }, 0) == V{ 9, 8 }, "fair: two views, nothing to sort");
        Check(LB::FairTurnOrder<std::uint32_t>({ 9, 8, 7 }, { 0, 1 }, 0) == V{ 9, 8, 7 },
              "fair: a wait list that does not match falls back to the round robin");

        Check(LB::DrawsTurn(0, 100.0, LB::kFrameBudgetMs, 0), "budget: the newest draws however long the frame took");
        Check(LB::DrawsTurn(1, 2.0, LB::kFrameBudgetMs, 0), "budget: another draws while the frame is under budget");
        Check(!LB::DrawsTurn(1, LB::kFrameBudgetMs, LB::kFrameBudgetMs, 0), "budget: none past it");
        Check(LB::DrawsTurn(2, 50.0, LB::kFrameBudgetMs, LB::kMaxWaitFrames),
              "budget: a view that waited too long draws past it");
        Check(!LB::DrawsTurn(2, 50.0, LB::kFrameBudgetMs, LB::kMaxWaitFrames - 1), "budget: and not a frame sooner");
        Check(LB::DrawsTurn(2, 50.0, 14.0, 2, 2) && !LB::DrawsTurn(2, 50.0, 14.0, 1, 2),
              "budget: the wait is the mode's");
        Check(LB::kFrameBudgetMsVR < LB::kFrameBudgetMs, "budget: VR's frame is shorter");

        bool known = false;
        Check(LB::ParseMode("Balanced", &known) == LB::Mode::kBalanced && known, "mode: balanced, any case");
        Check(LB::ParseMode(" light ", &known) == LB::Mode::kLight && known, "mode: light, spaces trimmed");
        Check(LB::ParseMode("", &known) == LB::Mode::kSmooth && known, "mode: empty is the default, smooth");
        Check(LB::ParseMode("turbo", &known) == LB::Mode::kSmooth && !known, "mode: unknown text is smooth, flagged");

        const auto smooth = LB::PolicyFor(LB::Mode::kSmooth, false);
        const auto bal    = LB::PolicyFor(LB::Mode::kBalanced, false);
        const auto light  = LB::PolicyFor(LB::Mode::kLight, false);
        Check(smooth.budgetMs > bal.budgetMs && bal.budgetMs > light.budgetMs, "policy: smooth > balanced > light");
        Check(smooth.maxWait < bal.maxWait && bal.maxWait < light.maxWait, "policy: smooth waits least, light most");
        Check(bal.budgetMs == LB::kFrameBudgetMs && bal.maxWait == LB::kMaxWaitFrames,
              "policy: balanced is the budget 0.4.0 shipped");
        Check(LB::PolicyFor(LB::Mode::kSmooth, true).budgetMs < smooth.budgetMs, "policy: VR's budgets are smaller");
        Check(LB::PolicyFor(LB::Mode::kSmooth, true).budgetMs <= 4.0, "policy: VR's smooth stays within 4 ms");
        {
            const auto running = LB::PolicyFor(LB::Mode::kSmooth, false, 0.0, false);
            Check(running.mode == LB::Mode::kSmooth && running.applied == LB::Mode::kBalanced &&
                      running.budgetMs == bal.budgetMs && running.maxWait == bal.maxWait,
                  "policy: smooth takes balanced's budget while the game runs");
            Check(LB::PolicyFor(LB::Mode::kLight, false, 0.0, false).budgetMs == light.budgetMs &&
                      LB::PolicyFor(LB::Mode::kSmooth, false, 9.0, false).budgetMs == 9.0,
                  "policy: the pause changes only smooth; an explicit budget holds either way");
        }
        Check(LB::PolicyFor(LB::Mode::kLight, false, 8.0).budgetMs == 8.0 &&
                  LB::PolicyFor(LB::Mode::kLight, false, 8.0).maxWait == light.maxWait,
              "policy: an explicit budget replaces the mode's, the wait stays");
        Check(LB::PolicyFor(LB::Mode::kSmooth, false, 500.0).budgetMs == 50.0 &&
                  LB::PolicyFor(LB::Mode::kSmooth, false, 0.1).budgetMs == 0.5,
              "policy: an explicit budget is clamped");

        namespace VS = Phantasm::ViewStatsCore;
        using F      = VS::Counter::Frame;
        VS::Counter c;
        bool        rolled = false;
        for (int i = 0; i < 60; ++i) {
            const auto f = i % 3 == 0 ? F::kDrawn : i % 3 == 1 ? F::kSkipped : F::kWaited;
            rolled |= c.Note(f, i * (1000.0 / 60.0), 2.0);
        }
        Check(!rolled && !c.last.valid, "stats: nothing is reported before a full second");
        rolled = c.Note(F::kDrawn, 1000.0, 4.0);
        Check(rolled && c.last.valid, "stats: a second's window reports");
        Check(std::abs(c.last.drawnFps - 20.0) < 0.01 && std::abs(c.last.skippedFps - 20.0) < 0.01 &&
                  std::abs(c.last.waitedFps - 20.0) < 0.01,
              "stats: the kinds of frame are counted apart, and a steady 60 reads 60 (the opening frame only opens)");
        Check(std::abs(c.last.Fps() - 40.0) < 0.01, "stats: fps counts drawn and skipped, not waited");
        Check(std::abs(c.last.cpuMs - (19 * 2.0 + 4.0) / 20.0) < 0.001, "stats: cpu is the average over drawn frames");
        Check(c.drawn == 0 && c.skipped == 0 && c.waited == 0, "stats: a new window starts empty");
        {
            VS::Counter s;
            for (int i = 0; i <= 60; ++i) s.Note(i % 2 ? F::kStalled : F::kDrawn, i * (1000.0 / 60.0), 1.0);
            Check(s.last.valid && std::abs(s.last.stalledFps - 30.0) < 0.01 && std::abs(s.last.Fps() - 30.0) < 0.01,
                  "stats: a stalled turn is not counted as keeping up");
            VS::Counter g;
            g.Note(F::kDrawn, 0.0);
            g.Note(F::kDrawn, 100.0);
            Check(!g.Note(F::kDrawn, 5000.0) && g.drawn == 0 && g.windowStartMs == 5000.0,
                  "stats: a gap of over two windows starts a new window instead of reporting a low rate");
            g.Reset();
            Check(g.windowStartMs < 0.0 && !g.last.valid, "stats: Reset forgets the window and the last rates");
        }
    }
}
