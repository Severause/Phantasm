// Phantasm - native self-tests: FreezeCore (the "freeze" hint's steps).
//
// Author: Severause

#include "FreezeCore.h"
#include "TestHarness.h"

namespace Phantasm::Tests
{
    void RunFreezeCoreTests()
    {
        namespace FC = Phantasm::FreezeCore;
        using S      = FC::Step;

        FC::State live;
        live.hasGeometry = true;
        Check(FC::Decide(live) == S::kNone, "freeze: a live view not asked to freeze draws live");

        FC::State want = live;
        want.want      = true;
        Check(FC::Decide(want) == S::kFreeze, "freeze: a complete live look freezes at once");

        FC::State owed  = want;
        owed.lookOwed   = true;
        Check(FC::Decide(owed) == S::kWait,
              "freeze: a look still owed textures or skipped pieces (no buffers yet) waits until it is complete");

        FC::State keptOwed   = owed;
        keptOwed.showingKept = true;
        Check(FC::Decide(keptOwed) == S::kFreeze, "freeze: a kept look freezes as it is (it was complete when kept)");

        FC::State empty    = want;
        empty.hasGeometry = false;
        Check(FC::Decide(empty) == S::kWait, "freeze: a view that draws nothing yet waits");

        FC::State starting    = want;
        starting.startPending = true;
        Check(FC::Decide(starting) == S::kWait, "freeze: a start waiting for the 3D waits");

        FC::State frozen = want;
        frozen.frozen    = true;
        Check(FC::Decide(frozen) == S::kHold, "freeze: a frozen view asked to stay frozen holds");
        frozen.lookOwed     = true;
        frozen.startPending = true;
        Check(FC::Decide(frozen) == S::kHold, "freeze: a frozen view holds whatever the live state says");

        FC::State thaw = frozen;
        thaw.want      = false;
        Check(FC::Decide(thaw) == S::kThaw, "freeze: a frozen view no longer asked to be thaws");
    }
}
