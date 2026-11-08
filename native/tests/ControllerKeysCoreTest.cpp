// Phantasm - native self-tests: ControllerKeysCore (a shader-property controller's timing, keys and lanes).
//
// Author: Severause

#include "ControllerKeysCore.h"
#include "TestHarness.h"

#include <cmath>
#include <cstring>
#include <vector>

namespace Phantasm::Tests
{
    namespace
    {
        namespace CK = Phantasm::ControllerKeysCore;

        bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

        // Keys laid out as the engine stores them: `floats` per key, then `pad` bytes of derived values.
        std::vector<std::uint8_t> Raw(const std::vector<std::vector<float>>& keys, std::uint32_t pad = 0)
        {
            std::vector<std::uint8_t> out;
            for (const auto& k : keys) {
                const auto at = out.size();
                out.resize(at + k.size() * 4 + pad, 0);
                std::memcpy(out.data() + at, k.data(), k.size() * 4);
            }
            return out;
        }
    }

    void RunControllerKeysCoreTests()
    {
        // Timing: the controller's own clock (weightedLastTime) plus the app time since, times the frequency, plus the
        // phase, cycled. Flags 0x48 (active, scaled time, loop) are what the staff and torch meshes carry.
        CK::Timing loop{ 0x48, 1.0f, 0.0f, 0.0f, 2.0f };
        Check(Near(CK::ScaledTime(loop, 0.5f, 0.0f, 99.0f), 0.5f) && Near(CK::ScaledTime(loop, 5.0f, 0.5f, 99.0f), 1.5f) &&
                  Near(CK::ScaledTime(loop, -0.5f, 0.0f, 99.0f), 1.5f),
              "keys timing: the clock loops over its key range, before it too");
        CK::Timing fast{ 0x48, 2.0f, 0.25f, 0.0f, 1.0f };
        Check(Near(CK::ScaledTime(fast, 0.5f, 0.25f, 99.0f), 0.25f),
              "keys timing: the time since runs at the frequency, then the phase, then the loop");
        CK::Timing pingpong{ 0x4A, 1.0f, 0.0f, 0.0f, 1.0f };
        Check(Near(CK::ScaledTime(pingpong, 0.25f, 0.0f, 0.0f), 0.25f) &&
                  Near(CK::ScaledTime(pingpong, 1.25f, 0.0f, 0.0f), 0.75f) &&
                  Near(CK::ScaledTime(pingpong, 2.0f, 0.25f, 0.0f), 0.25f),
              "keys timing: reverse runs back down the range, then up again");
        CK::Timing clamp{ 0x4C, 1.0f, 0.0f, 1.0f, 3.0f };
        Check(Near(CK::ScaledTime(clamp, 0.0f, 0.0f, 0.0f), 1.0f) && Near(CK::ScaledTime(clamp, 8.0f, 1.0f, 0.0f), 3.0f) &&
                  Near(CK::ScaledTime(clamp, 2.0f, 0.0f, 0.0f), 2.0f),
              "keys timing: clamp holds the ends");
        CK::Timing backwards{ 0x58, 1.0f, 0.0f, 0.0f, 2.0f };
        Check(Near(CK::ScaledTime(backwards, 1.0f, 0.25f, 0.0f), 0.75f), "keys timing: a backwards controller runs back");
        CK::Timing raw{ 0x08, 1.0f, 0.0f, 0.0f, 1.0f };
        Check(Near(CK::ScaledTime(raw, 0.5f, 0.25f, 7.0f), 7.0f), "keys timing: without scaled time the app time passes as it is");
        CK::Timing flat{ 0x48, 1.0f, 0.0f, 2.0f, 2.0f };
        Check(Near(CK::ScaledTime(flat, 9.0f, 0.0f, 0.0f), 2.0f), "keys timing: an empty range stays at its low key");

        // Linear float keys.
        CK::Curve lin;
        auto      lraw = Raw({ { 0.0f, 0.0f }, { 1.0f, 10.0f }, { 2.0f, 0.0f } });
        Check(CK::ParseKeys(lraw.data(), 3, 8, 1, 1, lin), "keys: linear float keys parse");
        float v[4]{};
        CK::Evaluate(lin, 0.5f, v);
        Check(Near(v[0], 5.0f), "keys: linear interpolates");
        CK::Evaluate(lin, -1.0f, v);
        const bool before = Near(v[0], 0.0f);
        CK::Evaluate(lin, 5.0f, v);
        Check(before && Near(v[0], 0.0f), "keys: held at the first and last key outside them");
        CK::Evaluate(lin, 1.0f, v);
        Check(Near(v[0], 10.0f), "keys: exactly on a key is that key");

        // A stride larger than the key (derived values after it) is skipped over.
        CK::Curve padded;
        auto      praw = Raw({ { 0.0f, 1.0f }, { 1.0f, 3.0f } }, 8);
        Check(CK::ParseKeys(praw.data(), 2, 16, 1, 1, padded), "keys: a padded stride parses");
        CK::Evaluate(padded, 0.5f, v);
        Check(Near(v[0], 2.0f), "keys: a padded stride reads the right keys");

        // Quadratic: Hermite over the normalised segment with the stored tangents.
        CK::Curve quad;
        auto      qraw = Raw({ { 0.0f, 0.0f, 0.0f, 0.0f }, { 2.0f, 1.0f, 0.0f, 0.0f } });
        Check(CK::ParseKeys(qraw.data(), 2, 16, 2, 1, quad), "keys: quadratic keys parse");
        CK::Evaluate(quad, 1.0f, v);
        Check(Near(v[0], 0.5f), "keys: flat tangents ease through the midpoint");
        CK::Curve slope;
        auto      sraw = Raw({ { 0.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 0.0f } });
        Check(CK::ParseKeys(sraw.data(), 2, 16, 2, 1, slope), "keys: quadratic keys with slopes parse");
        CK::Evaluate(slope, 0.25f, v);
        Check(Near(v[0], 0.25f), "keys: matching tangents make a straight line");

        // TCB with no tension, continuity or bias over even keys is Catmull-Rom.
        CK::Curve tcb;
        auto traw = Raw({ { 0.0f, 0.0f, 0, 0, 0 }, { 1.0f, 1.0f, 0, 0, 0 }, { 2.0f, 2.0f, 0, 0, 0 }, { 3.0f, 3.0f, 0, 0, 0 } });
        Check(CK::ParseKeys(traw.data(), 4, 20, 3, 1, tcb), "keys: TCB keys parse");
        CK::Evaluate(tcb, 1.5f, v);
        Check(Near(v[0], 1.5f), "keys: TCB through evenly rising keys is a straight line");
        Check(Near(tcb.keys[1].outTan[0], 1.0f) && Near(tcb.keys[1].inTan[0], 1.0f),
              "keys: TCB tangents are the neighbours' slope");

        // Step, no-interp: held until the next key.
        CK::Curve step;
        auto      straw = Raw({ { 0.0f, 4.0f }, { 1.0f, 8.0f } });
        Check(CK::ParseKeys(straw.data(), 2, 8, 5, 1, step), "keys: step keys parse");
        CK::Evaluate(step, 0.9f, v);
        Check(Near(v[0], 4.0f), "keys: a step key holds until the next");

        // Colours: three (point) and four (RGBA) values a key.
        CK::Curve rgb;
        auto      craw = Raw({ { 0.0f, 0.0f, 0.5f, 1.0f }, { 1.0f, 1.0f, 0.5f, 0.0f } });
        Check(CK::ParseKeys(craw.data(), 2, 16, 1, 3, rgb), "keys: point3 colour keys parse");
        CK::Evaluate(rgb, 0.5f, v);
        Check(Near(v[0], 0.5f) && Near(v[1], 0.5f) && Near(v[2], 0.5f), "keys: each channel interpolates");
        CK::Curve rgba;
        auto      aaraw = Raw({ { 0.0f, 1, 1, 1, 0 }, { 1.0f, 1, 1, 1, 1 } });
        Check(CK::ParseKeys(aaraw.data(), 2, 20, 1, 4, rgba), "keys: RGBA colour keys parse");
        CK::Evaluate(rgba, 0.25f, v);
        Check(Near(v[3], 0.25f), "keys: the alpha channel interpolates");

        // Which checks prove anything.
        Check(CK::CheckIsTelling(lin, 0.5f) && !CK::CheckIsTelling(lin, 0.0f) && !CK::CheckIsTelling(lin, 2.0f),
              "self-check: a changing curve tells only strictly inside its keys");
        Check(!CK::CheckIsTelling(step, 0.5f), "self-check: a step curve tells nothing about the time between keys");
        CK::Curve still;
        auto      stillRaw = Raw({ { 0.0f, 2.0f }, { 1.0f, 2.0f } });
        Check(CK::ParseKeys(stillRaw.data(), 2, 8, 1, 1, still) && !CK::CheckIsTelling(still, 0.5f),
              "self-check: keys that never change tell nothing");

        // Refusals.
        CK::Curve bad;
        Check(!CK::ParseKeys(lraw.data(), 3, 4, 1, 1, bad), "keys: a stride too small for the type is refused");
        Check(!CK::ParseKeys(qraw.data(), 2, 8, 2, 1, bad), "keys: quadratic keys need room for both tangents");
        Check(!CK::ParseKeys(lraw.data(), 3, 8, 9, 1, bad), "keys: an unknown key type is refused");
        auto back = Raw({ { 1.0f, 0.0f }, { 0.5f, 1.0f } });
        Check(!CK::ParseKeys(back.data(), 2, 8, 1, 1, bad), "keys: times that go back are refused");
        auto nan = Raw({ { 0.0f, std::nanf("") } });
        Check(!CK::ParseKeys(nan.data(), 1, 8, 1, 1, bad), "keys: a NaN value is refused");
        Check(!CK::ParseKeys(lraw.data(), 0, 8, 1, 1, bad), "keys: no keys is not a curve");

        // A curve without keys is its constant.
        CK::Curve pose;
        pose.dims        = 1;
        pose.constant[0] = 0.7f;
        CK::Evaluate(pose, 3.0f, v);
        Check(Near(v[0], 0.7f), "keys: an interpolator without keys holds its value");

        // Lanes.
        using L = CK::Lane;
        using C = CK::ControllerClass;
        Check(CK::ClassOf("BSEffectShaderPropertyFloatController") == C::kEffectFloat &&
                  CK::ClassOf("BSLightingShaderPropertyColorController") == C::kLitColor &&
                  CK::ClassOf("NiTransformController") == C::kOther && CK::ClassOf(nullptr) == C::kOther,
              "lanes: controller classes by name");
        Check(CK::LaneOf(C::kEffectFloat, 0) == L::kEffectEmissiveMult && CK::LaneOf(C::kEffectFloat, 5) == L::kEffectAlpha &&
                  CK::LaneOf(C::kEffectFloat, 6) == L::kEffectUOffset && CK::LaneOf(C::kEffectFloat, 9) == L::kEffectVScale &&
                  CK::LaneOf(C::kEffectFloat, 10) == L::kNone,
              "lanes: the effect float variables");
        Check(CK::LaneOf(C::kEffectColor, 0) == L::kEffectColor && CK::LaneOf(C::kEffectColor, 1) == L::kNone,
              "lanes: the effect colour variable");
        Check(CK::LaneOf(C::kLitFloat, 11) == L::kLitEmissiveMult && CK::LaneOf(C::kLitFloat, 12) == L::kLitAlpha &&
                  CK::LaneOf(C::kLitFloat, 20) == L::kLitUOffset && CK::LaneOf(C::kLitFloat, 23) == L::kLitVScale &&
                  CK::LaneOf(C::kLitFloat, 9) == L::kUndrawn && CK::LaneOf(C::kLitFloat, 5) == L::kNone,
              "lanes: the lighting float variables, glossiness and the like undrawn");
        Check(CK::LaneOf(C::kLitColor, 1) == L::kLitEmissiveColor && CK::LaneOf(C::kLitColor, 0) == L::kUndrawn &&
                  CK::LaneOf(C::kLitColor, 2) == L::kNone,
              "lanes: the lighting colour variables");
        Check(CK::LaneDims(L::kEffectColor) == 3 && CK::LaneDims(L::kLitAlpha) == 1, "lanes: a colour has three values");

        CK::EffectLanes e;
        CK::LitLanes    l;
        const float     scroll[3]{ 0.3f, 0.0f, 0.0f };
        CK::SetLane(e, l, L::kEffectUOffset, scroll);
        float got[3]{};
        Check(CK::LaneValue(e, l, L::kEffectUOffset, got) && Near(got[0], 0.3f), "lanes: a set lane reads back");

        // Falloff angles: the curve is in degrees, the engine's material holds their cosine.
        float angle[3]{ 60.0f, 0.0f, 0.0f };
        CK::ToLaneUnits(L::kEffectFalloffStartAngle, angle);
        Check(Near(angle[0], 0.5f), "lanes: a falloff angle of 60 degrees is stored as its cosine");
        CK::FromLaneUnits(L::kEffectFalloffStartAngle, angle);
        Check(Near(angle[0], 60.0f), "lanes: a stored falloff cosine reads back as degrees");
        float opacity[3]{ 0.25f, 0.0f, 0.0f };
        CK::ToLaneUnits(L::kEffectFalloffStartOpacity, opacity);
        CK::FromLaneUnits(L::kEffectFalloffStopOpacity, opacity);
        Check(Near(opacity[0], 0.25f) && CK::IsAngleLane(L::kEffectFalloffStopAngle) &&
                  !CK::IsAngleLane(L::kEffectFalloffStartOpacity),
              "lanes: only the two angle lanes are converted");
        float outOfRange[3]{ 1.0001f, 0.0f, 0.0f };
        CK::FromLaneUnits(L::kEffectFalloffStopAngle, outOfRange);
        Check(Near(outOfRange[0], 0.0f), "lanes: a cosine just over 1 reads as 0 degrees, not NaN");
        const float red[3]{ 1.0f, 0.0f, 0.0f };
        CK::SetLane(e, l, L::kLitEmissiveColor, red);
        Check(!CK::LaneValue(e, l, L::kLitEmissiveColor, got),
              "lanes: a property without an emissive colour has no colour lane");
        l.hasEmissive = true;
        CK::SetLane(e, l, L::kLitEmissiveColor, red);
        l.emissiveMult = 2.0f;
        float glow[3]{};
        CK::LitEmissive(l, true, glow);
        Check(Near(glow[0], 2.0f) && Near(glow[1], 0.0f), "lanes: the emissive is the colour times the multiple");
        CK::LitEmissive(l, false, glow);
        Check(Near(glow[0], 0.0f), "lanes: a piece that cannot glow draws none");
        l.emissiveMult = 0.0005f;
        CK::LitEmissive(l, true, glow);
        Check(Near(glow[0], 0.0f), "lanes: a multiple of 0.001 or less is none");

        Check(CK::Agrees(1.0f, 1.0005f) && !CK::Agrees(1.0f, 1.01f) && CK::Agrees(1000.0f, 1000.5f),
              "self-check: agreement is within 1e-3, relative above 1");

        // The staff's swirl: V offset t/16 over a 16 s loop; the game drew 0.4923 at its update of 7.911 (35 ms old).
        CK::Curve  swirl;
        auto       swRaw = Raw({ { 0.0f, 0.0f, 1.0f, 1.0f }, { 16.0f, 1.0f, 1.0f, 1.0f } });
        CK::Timing swirlT{ 0x48, 1.0f, 0.0f, 0.0f, 16.0f };
        Check(CK::ParseKeys(swRaw.data(), 2, 16, 2, 1, swirl), "self-check: the swirl's keys parse");
        float       lag   = -1.0f;
        const float drawn = 0.4923f;
        Check(CK::AgreesWithLag(swirl, swirlT, 7.911f, 7.911f, &drawn, 1, lag) && lag > 0.02f && lag < 0.05f,
              "self-check: a lane a frame or two behind its update agrees, with the lag found");
        const float exact = 7.911f / 16.0f;
        Check(CK::AgreesWithLag(swirl, swirlT, 7.911f, 7.911f, &exact, 1, lag) && lag == 0.0f,
              "self-check: a lane at its update agrees with no lag");
        const float stale = 0.45f;
        Check(!CK::AgreesWithLag(swirl, swirlT, 7.911f, 7.911f, &stale, 1, lag),
              "self-check: a lane further behind than the window differs");

        // A falloff angle opening from 0 to 1.6 degrees over 16 s: at 0.5 s the lane holds cos(0.05 degrees).
        CK::Curve   opening;
        auto        opRaw = Raw({ { 0.0f, 0.0f, 1.0f, 1.0f }, { 16.0f, 1.6f, 1.0f, 1.0f } });
        Check(CK::ParseKeys(opRaw.data(), 2, 16, 2, 1, opening), "self-check: the opening angle's keys parse");
        const float nearZero = std::cos(0.05f * CK::kDegToRad);
        Check(CK::AgreesWithLag(opening, swirlT, 0.5f, 0.5f, &nearZero, 1, lag, true) && lag == 0.0f,
              "self-check: an angle just above 0 degrees agrees, compared as its cosine");
        const float degrees = 0.05f;
        Check(!CK::AgreesWithLag(opening, swirlT, 0.5f, 0.5f, &degrees, 1, lag, true),
              "self-check: an angle lane holding degrees, not a cosine, differs");
    }
}
