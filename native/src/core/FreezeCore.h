#pragma once

// Phantasm - FreezeCore: when an actor view takes or lets go of its frozen look (the "freeze" hint), engine-free
// (native/build.ps1 -Tests). TendActorLive in MannequinRenderer.cpp asks it once per tend.
//
// A frozen view draws a still copy of the look it held when it froze (FrozenCopy: no live bone, anchor or effect
// handles), so the actor can be dressed otherwise under it while another view of them draws live.
//
// Author: Severause

namespace Phantasm::FreezeCore
{
    /// What the view does this tend.
    enum class Step
    {
        kNone,     // not frozen and not asked to be: the view draws live
        kFreeze,   // take the drawn look as the frozen one now
        kWait,     // asked to freeze, but nothing worth keeping is drawn yet: tend live until there is
        kHold,     // frozen: draw the frozen look, collect nothing
        kThaw,     // frozen and no longer asked to be: back to live (through a kept look's upgrade)
    };

    /// The view's state for the decision.
    struct State
    {
        bool want         = false;   // the hint is on
        bool frozen       = false;   // the view draws a frozen look
        bool hasGeometry  = false;   // it draws something
        bool showingKept  = false;   // what it draws is a kept look (already a still copy)
        bool lookOwed     = false;   // the live look is not complete yet: a texture still owed, or pieces skipped
                                     // because their buffers were not uploaded (collectComplete false)
        bool startPending = false;   // a start still riding out a missing 3D
    };

    /// A live look freezes only once complete (nothing owed), so the copy never keeps a placeholder or misses a piece; a
    /// kept look is already complete as kept. Nothing freezes while the start waits for the 3D.
    inline Step Decide(const State& s) noexcept
    {
        if (s.frozen) return s.want ? Step::kHold : Step::kThaw;
        if (!s.want) return Step::kNone;
        if (s.startPending || !s.hasGeometry) return Step::kWait;
        if (!s.showingKept && s.lookOwed) return Step::kWait;
        return Step::kFreeze;
    }
}
