// Engine-free tests for NeuralStillCore (a still laid over NR's ground and taken off it again).

#include "TestHarness.h"

#include "NeuralStillCore.h"

#include <cstdlib>
#include <vector>

namespace Phantasm::Tests
{
    void RunNeuralStillCoreTests()
    {
        namespace NS = Phantasm::NeuralStillCore;

        // One pixel each: transparent, opaque, and half covered (premultiplied in linear light, sRGB-encoded).
        const std::uint8_t half = NS::EncodeByte(NS::SrgbToLinear(200 / 255.0f) * 0.5f);
        const std::vector<std::uint8_t> shot{
            0, 0, 0, 0,
            180, 120, 90, 255,
            half, half, half, 128,
        };
        std::vector<std::uint8_t> picture;
        std::vector<float>        depth;
        NS::ToInput(shot, 3, 1, picture, depth);

        Check(picture.size() == 12 && depth.size() == 3, "neural: input sized to the shot");
        Check(picture[3] == 255 && picture[7] == 255 && picture[11] == 255, "neural: the input is opaque");
        Check(picture[0] == NS::EncodeByte(NS::kGroundLinear), "neural: an uncovered pixel shows the ground");
        Check(picture[4] == 180 && picture[5] == 120 && picture[6] == 90, "neural: a covered pixel keeps its colour");
        Check(depth[0] == NS::kGroundDepth && depth[1] == NS::kFigureDepth && depth[2] == NS::kFigureDepth,
              "neural: depth near on the figure, far on the ground");

        {
            // An answer that changes nothing gives the shot back.
            std::vector<std::uint8_t> back = shot;
            NS::FromOutput(back, 3, 1, picture);
            bool same = true;
            for (std::size_t i = 0; i < shot.size(); ++i) same &= std::abs(int(back[i]) - int(shot[i])) <= 1;
            Check(same, "neural: an unchanged answer round-trips the shot");
        }
        {
            // A changed answer: the opaque pixel takes it, the transparent one stays transparent black, and the half
            // covered one never exceeds its coverage.
            std::vector<std::uint8_t> answer(12, 255);
            answer[4] = 10, answer[5] = 20, answer[6] = 30;
            std::vector<std::uint8_t> back = shot;
            NS::FromOutput(back, 3, 1, answer);
            Check(back[0] == 0 && back[1] == 0 && back[2] == 0 && back[3] == 0, "neural: transparent stays transparent");
            Check(back[4] == 10 && back[5] == 20 && back[6] == 30 && back[7] == 255, "neural: opaque takes NR's colour");
            const std::uint8_t cap = NS::EncodeByte(128 / 255.0f);
            Check(back[8] <= cap && back[9] <= cap && back[10] <= cap && back[11] == 128,
                  "neural: a half covered pixel stays within its coverage, its alpha kept");
        }

        Check(NS::ClampModel(-3) == 0 && NS::ClampModel(7) == 2, "neural: the model is 0..2");
        Check(NS::ClampStrength(2.0f) == 1.0f && NS::ClampStrength(-1.0f) == 0.0f, "neural: strengths are 0..1");
        Check(NS::ClampPasses(0) == 1 && NS::ClampPasses(99) == 8, "neural: passes are 1..8");
    }
}
