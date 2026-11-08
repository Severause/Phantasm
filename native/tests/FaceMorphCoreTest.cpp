// Phantasm - native self-tests: FaceMorphCore (the actor view's own face: .tri morphs, taking the
// game's face off, chosen expressions and the blink).
//
// Author: Severause

#include "FaceMorphCore.h"
#include "TestHarness.h"

#include <cmath>
#include <cstring>

namespace Phantasm::Tests
{
    namespace
    {
        namespace FM = Phantasm::FaceMorphCore;

        void PutU32(std::vector<std::uint8_t>& b, std::uint32_t v) { b.insert(b.end(), reinterpret_cast<std::uint8_t*>(&v), reinterpret_cast<std::uint8_t*>(&v) + 4); }
        void PutF32(std::vector<std::uint8_t>& b, float v) { b.insert(b.end(), reinterpret_cast<std::uint8_t*>(&v), reinterpret_cast<std::uint8_t*>(&v) + 4); }
        void PutI16(std::vector<std::uint8_t>& b, std::int16_t v) { b.insert(b.end(), reinterpret_cast<std::uint8_t*>(&v), reinterpret_cast<std::uint8_t*>(&v) + 2); }

        // Two vertices, one triangle, two UVs, and two morphs: BlinkLeft moves vertex 0 up 1u, MoodHappy moves
        // vertex 1 right 2u.
        std::vector<std::uint8_t> SmallTri()
        {
            std::vector<std::uint8_t> b(8);
            std::memcpy(b.data(), "FRTRI003", 8);
            const std::uint32_t h[14] = { 2, 1, 0, 0, 0, 2, 1, 2, 0, 0, 0, 0, 0, 0 };
            for (const auto v : h) PutU32(b, v);
            for (int i = 0; i < 6; ++i) PutF32(b, 1.0f);           // vertices
            for (int i = 0; i < 3; ++i) PutU32(b, 0);              // triangle
            for (int i = 0; i < 4; ++i) PutF32(b, 0.5f);           // UVs
            for (int i = 0; i < 3; ++i) PutU32(b, 0);              // UV triangle
            const auto morph = [&](const char* name, float scale, std::array<std::int16_t, 6> d) {
                PutU32(b, static_cast<std::uint32_t>(std::strlen(name) + 1));
                b.insert(b.end(), name, name + std::strlen(name) + 1);
                PutF32(b, scale);
                for (const auto v : d) PutI16(b, v);
            };
            morph("BlinkLeft", 0.5f, { 0, 0, 2, 0, 0, 0 });
            morph("MoodHappy", 0.25f, { 0, 0, 0, 8, 0, 0 });
            return b;
        }

        bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }
    }

    void RunFaceMorphCoreTests()
    {
        const auto bytes = SmallTri();
        FM::Tri    tri;
        std::string why;
        Check(FM::ParseTri(bytes.data(), bytes.size(), tri, &why) && tri.vertexCount == 2 && tri.morphs.size() == 2 &&
                  tri.morphs[0].name == "BlinkLeft" && tri.Find("moodhappy") == &tri.morphs[1],
              "face: an FRTRI003 file's morphs are read, found in any case");
        Check(!FM::ParseTri(bytes.data(), bytes.size() - 3, tri, &why) && tri.morphs.empty(),
              "face: a short file is refused");
        const std::uint8_t junk[80] = {};
        Check(!FM::ParseTri(junk, sizeof(junk), tri, &why), "face: a file without the magic is refused");

        FM::ParseTri(bytes.data(), bytes.size(), tri, &why);
        const std::vector<FM::P4> base{ { 1, 1, 1, 1 }, { 1, 1, 1, 1 } };
        std::vector<FM::P4>       out;
        FM::Apply(base, { { tri.Find("BlinkLeft"), 1.0f }, { tri.Find("MoodHappy"), 0.5f } }, out);
        Check(Near(out[0].z, 2.0f) && Near(out[1].x, 2.0f) && Near(out[0].x, 1.0f) && Near(out[1].w, 1.0f),
              "face: morphs add their scaled deltas at their weights");

        FM::GameWeights game;
        game.modifiers   = { 0.5f, 0.0f };
        game.expressions = std::vector<float>(17, 0.0f);
        game.expressions[10] = 1.0f;   // MoodHappy
        std::vector<FM::P4> live;
        FM::Apply(base, { { tri.Find("BlinkLeft"), 0.5f }, { tri.Find("MoodHappy"), 1.0f } }, live);
        std::vector<FM::P4> neutral, blinkless;
        FM::Apply(live, FM::TakeOff(tri, game, true), neutral);
        FM::Apply(live, FM::TakeOff(tri, game, false), blinkless);
        Check(Near(neutral[0].z, 1.0f) && Near(neutral[1].x, 1.0f), "face: taking the game's face off leaves the face at rest");
        Check(Near(blinkless[0].z, 1.0f) && Near(blinkless[1].x, 3.0f), "face: taking only the blink off keeps the game's mood");

        Check(FM::ParseExpression("Happy") == FM::Expression::kHappy && FM::ParseExpression("") == FM::Expression::kAsInGame &&
                  FM::ParseExpression("smug") == FM::Expression::kAsInGame &&
                  FM::ParseExpression(FM::ExpressionId(FM::Expression::kDisgusted)) == FM::Expression::kDisgusted,
              "face: expressions read in any case, anything else is as in game");
        Check(FM::MoodMorph(FM::Expression::kAngry) == "MoodAnger" && FM::MoodMorph(FM::Expression::kNeutral).empty() &&
                  FM::MoodMorph(FM::Expression::kAsInGame).empty(),
              "face: each mood names the game's mood morph; neutral and as in game name none");

        FM::Blinker blink(12345);
        float       peak = 0.0f;
        int         blinks = 0;
        bool        shut = false;
        double      lastStart = -1.0, minGap = 1e9;
        for (double t = 0.0; t < 60.0; t += 0.005) {
            const float w = blink.Weight(t);
            peak = std::max(peak, w);
            if (w > 0.5f && !shut) {
                shut = true;
                ++blinks;
                if (lastStart >= 0.0) minGap = std::min(minGap, t - lastStart);
                lastStart = t;
            }
            if (w < 0.1f) shut = false;
        }
        Check(Near(peak, 1.0f) && blinks >= 9 && blinks <= 30 && minGap > 0.2,
              "face: the blink shuts fully, every few seconds, never twice at once");
        FM::Blinker a(1), b(2);
        bool differ = false;
        for (double t = 0.0; t < 20.0 && !differ; t += 0.01) differ = std::fabs(a.Weight(t) - b.Weight(t)) > 0.5f;
        Check(differ, "face: two seeds blink on rhythms of their own");
    }
}
