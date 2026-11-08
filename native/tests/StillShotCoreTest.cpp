// Engine-free tests for StillShotCore (the still hints and the head-and-shoulders camera).

#include "TestHarness.h"

#include "StillShotCore.h"

#include <cmath>

namespace Phantasm::Tests
{
    void RunStillShotCoreTests()
    {
        namespace SS = Phantasm::StillShotCore;
        namespace ML = Phantasm::MirrorLightingCore;

        // ── Hints ──
        {
            const auto h = SS::Parse("");
            Check(!h.hasTurn && !h.hasLighting, "still: no hints set nothing");
        }
        {
            const auto h = SS::Parse("turn=90;lighting=daylight");
            Check(h.hasTurn && h.turnDeg == 90.0f, "still: a turn is read");
            Check(h.hasLighting && h.lighting == ML::Preset::kDaylight, "still: a lighting preset is read");
        }
        {
            const auto h = SS::Parse(" TURN = -45 ; Lighting = Studio ;");
            Check(h.hasTurn && h.turnDeg == -45.0f, "still: keys any case, spaces trimmed");
            Check(h.hasLighting && h.lighting == ML::Preset::kStudio, "still: preset ids any case");
        }
        {
            const auto h = SS::Parse("turn=270");
            Check(h.hasTurn && h.turnDeg == -90.0f, "still: a turn wraps into (-180, 180]");
        }
        {
            const auto h = SS::Parse("turn=abc;lighting=candles;zoom=2;nonsense");
            Check(!h.hasTurn && !h.hasLighting, "still: bad values and unknown keys are ignored");
        }
        {
            const auto h = SS::Parse("lighting=here");
            Check(!h.hasLighting, "still: 'here' is no shot light (a shot reads no game light)");
        }
        {
            const auto h = SS::Parse("turn=12x");
            Check(!h.hasTurn, "still: a turn with trailing text is ignored");
        }
        {
            const auto h = SS::Parse("turn=nan");
            Check(!h.hasTurn, "still: a turn that is not finite is ignored");
        }
        {
            const auto h = SS::Parse("turn=30;turn=60");
            Check(h.hasTurn && h.turnDeg == 60.0f, "still: the last turn wins");
        }
        {
            const auto h = SS::Parse("pitch=30;weapons=on");
            Check(h.hasPitch && h.pitchDeg == 30.0f, "still: a pitch is read");
            Check(h.hasWeapons && h.weapons, "still: weapons on is read");
        }
        {
            const auto h = SS::Parse("pitch=120;weapons=OFF");
            Check(h.hasPitch && h.pitchDeg == SS::kMaxPitchDeg, "still: a pitch is clamped");
            Check(h.hasWeapons && !h.weapons, "still: weapons off is read, any case");
        }
        {
            const auto h = SS::Parse("pitch=up;weapons=maybe");
            Check(!h.hasPitch && !h.hasWeapons, "still: a bad pitch or on/off word is ignored");
        }
        {
            Check(SS::Parse("").worn == SS::Worn::kGround, "still: an item shot draws the ground model by default");
            Check(SS::Parse("worn=Female").worn == SS::Worn::kFemale, "still: worn female is read, any case");
            Check(SS::Parse("worn=male").worn == SS::Worn::kMale, "still: worn male is read");
            Check(SS::Parse("worn=male;worn=off").worn == SS::Worn::kGround, "still: worn off is the ground model");
            Check(SS::Parse("worn=child").worn == SS::Worn::kGround, "still: an unknown worn word is ignored");
            SS::Worn w = SS::Worn::kMale;
            Check(!SS::ParseWorn("", w) && w == SS::Worn::kMale, "view hint: an empty worn value changes nothing");
            Check(SS::ParseWorn("GROUND", w) && w == SS::Worn::kGround, "view hint: worn ground, any case");
        }
        {
            const SS::ShotKey sword{ true, 0x1D4ED };
            const SS::ShotKey cuirassM{ true, 0x136D5, false, SS::Worn::kMale };
            const SS::ShotKey cuirassF{ true, 0x136D5, false, SS::Worn::kFemale };
            const SS::ShotKey aevar{ false, 0x14 };
            const std::vector<SS::ShotKey> queue{ aevar, cuirassF, sword, cuirassM, sword };
            Check(SS::NextShotIndex(queue, sword, 1) == 2, "queue: the same item's next angle goes ahead of older shots");
            Check(SS::NextShotIndex(queue, cuirassM, 1) == 3, "queue: the same armor worn alike is the same subject");
            Check(SS::NextShotIndex(queue, SS::ShotKey{ true, 0x999 }, 1) == 0, "queue: no shot of it, the oldest");
            Check(SS::NextShotIndex(queue, SS::ShotKey{}, 0) == 0, "queue: nothing taken yet, the oldest");
            Check(SS::NextShotIndex(queue, sword, SS::kMaxSameSubjectRun) == 0,
                  "queue: a subject that has had its run waits behind older shots");
            Check(!(SS::ShotKey{ false, 0x14, true } == aevar), "queue: an actor shot with weapons is another load");
        }
        Check(SS::WrapTurn(180.0f) == 180.0f && SS::WrapTurn(-180.0f) == 180.0f, "still: 180 and -180 are one turn");
        Check(SS::WrapTurn(720.0f) == 0.0f, "still: whole turns fold away");

        // ── The head-and-shoulders camera ──
        const float shown = SS::VisibleHeight(SS::kHeadDistance, 35.0f);
        Check(shown > 40.0f && shown < 50.0f, "still: the head shot shows about 44 units of height");
        const float top    = 75.0f + SS::kHeadTargetLift + shown * 0.5f;
        const float bottom = 75.0f + SS::kHeadTargetLift - shown * 0.5f;
        Check(top > 128.0f, "still: the head shot's top clears the crown");
        Check(bottom < 100.0f && bottom > 80.0f, "still: the head shot's bottom is the upper chest");

        // ── The head shot's aim ──
        {
            const auto t = SS::HeadShotTarget({ 3.0f, -2.0f, 120.0f }, 1.0f, 75.0f);
            Check(t.x == 0.0f && t.y == 0.0f && t.z == SS::kHeadTargetLift, "still: a humanoid head is framed from the lift");
        }
        {
            const auto t = SS::HeadShotTarget({ 0.0f, 0.0f, 120.0f }, 2.0f, 75.0f);
            Check(t.z == SS::kHeadTargetLift * 2.0f, "still: the lift scales with the figure");
        }
        {
            const auto t = SS::HeadShotTarget({ 60.0f, 0.0f, 150.0f }, 1.2f, 75.0f);
            const float wantZ = 150.0f - (75.0f + SS::kHeadBelowBone) * 1.2f;
            Check(t.x == 60.0f && t.y == 0.0f && std::fabs(t.z - wantZ) < 0.001f,
                  "still: a creature's head ahead of its feet is aimed at, under the bone");
        }
        {
            const auto t = SS::HeadShotTarget({ NAN, 0.0f, 0.0f }, 1.0f, 75.0f);
            Check(t.z == SS::kHeadTargetLift, "still: no head bone frames as a humanoid");
        }
    }
}
