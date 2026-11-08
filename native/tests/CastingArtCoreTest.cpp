// Phantasm - native self-tests: CastingArtCore (a readied spell's casting art in the actor view).
//
// Author: Severause

#include "CastingArtCore.h"
#include "TestHarness.h"

#include <array>

namespace Phantasm::Tests
{
    void RunCastingArtCoreTests()
    {
        namespace CA = Phantasm::CastingArtCore;
        using V      = CA::Verdict;

        CA::HandFacts fire;
        fire.caster    = true;
        fire.attached  = true;
        fire.artFormId = 0x0001B1F3;
        fire.artRoot   = 0x1000;
        fire.magicNode = 0x2000;

        Check(CA::HandVerdict(fire, true, true, true) == V::kDraw, "casting art: a readied hand's art is drawn");
        Check(CA::HandVerdict(CA::HandFacts{}, true, true, true) == V::kNoCaster, "casting art: no caster, no art");
        auto loading     = fire;
        loading.attached = false;
        Check(CA::HandVerdict(loading, true, true, true) == V::kNotReadied,
              "casting art: art not attached yet (or put away) is not drawn");
        auto noForm      = fire;
        noForm.artFormId = 0;
        Check(CA::HandVerdict(noForm, true, true, true) == V::kNotReadied, "casting art: a flag with no art form is none");
        Check(CA::HandVerdict(fire, false, true, true) == V::kWeaponsOff,
              "casting art: a view drawing no weapons draws no spell");
        Check(CA::HandVerdict(fire, true, false, true) == V::kNotOnDrawn3D,
              "casting art: a magic node off the drawn 3D (first-person hands) is not drawn");
        auto noNode      = fire;
        noNode.magicNode = 0;
        Check(CA::HandVerdict(noNode, true, true, true) == V::kNotOnDrawn3D, "casting art: no magic node, not drawn");
        Check(CA::HandVerdict(fire, true, true, false) == V::kNoRoot, "casting art: a root not found is not drawn");

        using Hands = std::array<CA::HandFacts, 2>;
        const auto signature = [](const Hands& h, bool weapons) { return CA::Signature(h.data(), h.size(), weapons); };
        Hands none{};
        none[0].caster = none[1].caster = true;
        Check(signature(none, true) == 0, "casting art signature: empty hands are 0");
        const Hands right{ none[0], fire };
        const auto  sig = signature(right, true);
        Check(sig != 0, "casting art signature: a readied hand is not 0");
        Check(signature(right, false) == 0, "casting art signature: 0 while the view draws no weapons");
        Check(signature(right, true) == sig, "casting art signature: the same hands, the same signature");
        const Hands left{ fire, none[1] };
        Check(signature(left, true) != sig, "casting art signature: the other hand differs");
        Hands swapped        = right;
        swapped[1].artFormId = 0x0001B1F4;
        Check(signature(swapped, true) != sig, "casting art signature: another spell's art differs");
        Hands clone      = right;
        clone[1].artRoot = 0x3000;
        Check(signature(clone, true) != sig, "casting art signature: a new clone of the same art differs");
        Hands moved        = right;
        moved[1].magicNode = 0x2100;
        Check(signature(moved, true) != sig, "casting art signature: the same art on another magic node differs");
        Hands away       = right;
        away[1].attached = false;
        Check(signature(away, true) == 0, "casting art signature: put away is 0 again");
        Hands stale      = none;
        stale[0].artRoot = 0x1234;   // a root left over with no flag is not drawn, so it changes nothing
        Check(signature(stale, true) == 0, "casting art signature: a stale root without the flag is 0");
        Check(CA::Signature(nullptr, 0, true) == 0, "casting art signature: no hands, 0");
    }
}
