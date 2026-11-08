// Phantasm - native self-tests: ImageHostProbeCore (whether Magelight takes the live views' texture images).
//
// Author: Severause

#include "ImageHostProbeCore.h"
#include "TestHarness.h"

namespace Phantasm::Tests
{
    namespace
    {
        namespace IHP = Phantasm::ImageHostProbeCore;
    }

    void RunImageHostProbeCoreTests()
    {
        // 1. No host: never asks the GPU question, answers NoHost, logs once
        {
            IHP::Latch l;
            int        asks = 0;
            bool       changed = false;
            const auto ask = [&] { ++asks; return true; };
            Check(l.Probe(false, ask, changed) == IHP::Answer::NoHost && changed, "image host: no host answers NoHost, logged");
            Check(l.Probe(false, ask, changed) == IHP::Answer::NoHost && !changed, "image host: the same answer is not logged again");
            Check(asks == 0, "image host: no host is never asked about its GPU");
        }

        // 2. A start before Magelight's first frame is refused, and a later one is not
        {
            IHP::Latch l;
            bool       gpu = false, changed = false;
            int        asks = 0;
            const auto ask = [&] { ++asks; return gpu; };
            Check(l.Probe(true, ask, changed) == IHP::Answer::NotYet && changed, "image host: before the renderer is up, NotYet");
            Check(l.Probe(true, ask, changed) == IHP::Answer::NotYet && !changed && asks == 2,
                  "image host: a no is asked again, not kept");
            gpu = true;
            Check(l.Probe(true, ask, changed) == IHP::Answer::Available && changed, "image host: once up, Available, logged");
            gpu = false;   // Magelight never takes a yes back; the latch does not ask again
            Check(l.Probe(true, ask, changed) == IHP::Answer::Available && !changed && asks == 3,
                  "image host: a yes is kept without asking again");
        }

        // 3. The CPU path stays NotYet for good, logged once
        {
            IHP::Latch l;
            bool       changed = false;
            int        logs    = 0;
            for (int i = 0; i < 10; ++i) {
                l.Probe(true, [] { return false; }, changed);
                logs += changed ? 1 : 0;
            }
            Check(l.last == IHP::Answer::NotYet && logs == 1, "image host: the CPU path is NotYet, logged once");
        }

        // 4. Only no Magelight at all draws hostless: a Magelight not up yet, or on the CPU, keeps refusing
        Check(IHP::DrawsHostless(IHP::Answer::NoHost) && !IHP::DrawsHostless(IHP::Answer::NotYet) &&
                  !IHP::DrawsHostless(IHP::Answer::Available),
              "image host: views draw with no image only when no Magelight can take one");
    }
}
