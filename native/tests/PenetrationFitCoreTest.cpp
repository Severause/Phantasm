// Phantasm - native self-tests: PenetrationFitCore (a shaft fitted into an opening between a scene's actors).

#include "TestHarness.h"

#include "PenetrationFitCore.h"

#include <cmath>
#include <limits>

namespace Phantasm::Tests
{
    namespace
    {
        namespace PF = Phantasm::PenetrationFitCore;

        bool Near(float a, float b, float tol = 0.01f) { return std::fabs(a - b) <= tol; }
        bool Near(PF::V3 a, PF::V3 b, float tol = 0.01f) { return Near(a.x, b.x, tol) && Near(a.y, b.y, tol) && Near(a.z, b.z, tol); }

        // A straight shaft along +Y from the origin: seven joints two units apart, twelve long.
        std::vector<PF::V3> Shaft()
        {
            std::vector<PF::V3> j;
            for (int i = 0; i < 7; ++i) j.push_back({ 0.0f, 2.0f * static_cast<float>(i), 0.0f });
            return j;
        }
    }

    void RunPenetrationFitCoreTests()
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const auto  shaft = Shaft();
        Check(Near(PF::ShaftLength(shaft), 12.0f), "fit: a shaft's length is its segments'");

        // An opening straight ahead, 10 out, the way in along +Y.
        const PF::Opening ahead{ PF::Kind::kVagina, { 0.0f, 10.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } };
        Check(Near(PF::Engagement(shaft, ahead, 1.0f), 1.0f), "fit: a shaft aimed at an opening in reach engages fully");
        {
            const PF::Opening far{ PF::Kind::kVagina, { 0.0f, 30.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } };
            Check(Near(PF::Engagement(shaft, far, 1.0f), 0.0f), "fit: an opening out of reach is left");
            const PF::Opening edge{ PF::Kind::kVagina, { 0.0f, 20.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } };
            const float w2 = PF::Engagement(shaft, edge, 2.0f);
            Check(Near(PF::Engagement(shaft, edge, 1.0f), 0.0f) && w2 > 0.0f && w2 < 1.0f,
                  "fit: the reach past the tip grows with the shaft's scale, fading out");
        }
        {
            const PF::Opening behind{ PF::Kind::kAnus, { 0.0f, 10.0f, 0.0f }, { 0.0f, -1.0f, 0.0f } };
            Check(Near(PF::Engagement(shaft, behind, 1.0f), 0.0f), "fit: an opening facing the shaft's way from inside is left");
        }
        {
            // 30 degrees off: between the full and the most aim, part engaged.
            const float       a  = 30.0f / 57.2957795f;
            const PF::Opening off{ PF::Kind::kMouth, { 10.0f * std::sin(a), 10.0f * std::cos(a), 0.0f }, { 0.0f, 1.0f, 0.0f } };
            const float       w  = PF::Engagement(shaft, off, 1.0f);
            Check(w > 0.0f && w < 1.0f, "fit: an opening off the shaft's aim engages in part");
            const float       b  = 60.0f / 57.2957795f;
            const PF::Opening wide{ PF::Kind::kMouth, { 10.0f * std::sin(b), 10.0f * std::cos(b), 0.0f }, { 0.0f, 1.0f, 0.0f } };
            Check(Near(PF::Engagement(shaft, wide, 1.0f), 0.0f), "fit: an opening past the most aim is left");
        }
        {
            const PF::Opening near{ PF::Kind::kVagina, { 0.0f, 8.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } };
            const PF::Opening farther{ PF::Kind::kMouth, { 0.0f, 17.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } };
            const auto        c = PF::ChooseOpening(shaft, { farther, near }, 1.0f);
            Check(c.opening == 1 && c.weight > 0.9f, "fit: of two openings the one nearest the tip is chosen");
            Check(PF::ChooseOpening(shaft, {}, 1.0f).opening == -1, "fit: no opening, no choice");
            const PF::Opening a{ PF::Kind::kVagina, { 0.0f, 10.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } };
            const PF::Opening b{ PF::Kind::kAnus, { 0.0f, 10.4f, 0.2f }, { 0.0f, 1.0f, 0.0f } };
            Check(PF::ChooseOpening(shaft, { a, b }, 1.0f).opening == 1, "fit: the nearer of two close openings, fresh");
            Check(PF::ChooseOpening(shaft, { a, b }, 1.0f, 0).opening == 0,
                  "fit: a shaft keeps the opening it took while another is not clearly nearer");
        }
        {
            // The corner at the entrance: straight on is full, a right angle folds the shaft and is left.
            const PF::Opening square{ PF::Kind::kVagina, { 0.0f, 10.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
            Check(Near(PF::Engagement(shaft, square, 1.0f), 0.0f), "fit: a way in at right angles to the approach is left");
            Check(Near(PF::Fade(1.0f, 2.0f, 3.0f), 1.0f) && Near(PF::Fade(3.0f, 2.0f, 3.0f), 0.0f) &&
                      Near(PF::Fade(2.5f, 2.0f, 3.0f), 0.5f) && Near(PF::Fade(std::numeric_limits<float>::quiet_NaN(), 2.0f, 3.0f), 0.0f),
                  "fit: a fade is full below, none past, smooth between");
        }
        {
            // An opening up and ahead: the fitted joints keep their spacing from the base and reach in along the way in.
            const PF::Opening up{ PF::Kind::kVagina, { 0.0f, 6.0f, 3.0f }, { 0.0f, 0.0f, 1.0f } };
            const auto        fit = PF::FitShaft(shaft, up, 1.0f);
            Check(Near(fit.front(), shaft.front()), "fit: the base stays where the clip put it");
            const float d = PF::Length(PF::Sub(up.entrance, shaft.front()));
            Check(Near(PF::Length(PF::Sub(fit[1], fit[0])), 2.0f), "fit: a joint keeps its distance along the shaft");
            Check(Near(fit.back(), PF::Add(up.entrance, PF::Scale(up.inward, 12.0f - d))),
                  "fit: past the entrance the shaft runs along the way in");
            Check(Near(PF::Depth(fit, up), 12.0f - d), "fit: the depth is the tip's distance in past the entrance");
            const auto half = PF::FitShaft(shaft, up, 0.5f);
            Check(Near(half.back(), PF::Lerp(shaft.back(), fit.back(), 0.5f)), "fit: a partial engagement moves part of the way");
            const auto none = PF::FitShaft(shaft, up, 0.0f);
            Check(Near(none.back(), shaft.back()), "fit: no engagement leaves the shaft as posed");
        }
        {
            Check(Near(PF::Spread(0.0f, 1.0f), 0.0f) && Near(PF::Spread(PF::kSpreadDepth, 1.0f), PF::kSpread) &&
                      Near(PF::Spread(100.0f, 2.0f), PF::kSpread * 2.0f) && Near(PF::Spread(nan, 1.0f), 0.0f),
                  "fit: the spread grows with depth to its full, at the shaft's scale");
            const PF::Opening o{ PF::Kind::kVagina, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } };
            Check(Near(PF::SpreadBone({ 1.0f, 0.0f, 2.0f }, o, 1.0f), PF::V3{ 2.0f, 0.0f, 2.0f }),
                  "fit: a rim bone moves out across the way in");
            Check(Near(PF::SpreadBone({ 0.0f, 0.0f, 2.0f }, o, 1.0f), PF::V3{ 0.0f, 0.0f, 2.0f }),
                  "fit: a bone on the centre line stays");
        }
        {
            PF::Opening o;
            Check(PF::OpeningOf(PF::Kind::kAnus, { { -1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } }, { 0.0f, 0.0f, 5.0f }, o) &&
                      Near(o.entrance, PF::V3{}) && Near(o.inward, PF::V3{ 0.0f, 0.0f, 1.0f }),
                  "fit: an opening sits at its rim's centre, its way in toward the bone behind it");
            Check(!PF::OpeningOf(PF::Kind::kAnus, {}, { 0.0f, 0.0f, 5.0f }, o), "fit: no rim, no opening");
            const auto m = PF::MouthOf({ 0.0f, 0.0f, 100.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, 1.0f);
            Check(Near(m.entrance, PF::V3{ 0.0f, PF::kMouthForward, 100.0f - PF::kMouthDown }) &&
                      Near(m.inward, PF::V3{ 0.0f, -1.0f, 0.0f }),
                  "fit: a mouth is ahead of and below the head bone, its way in back along the face");
        }
    }
}
