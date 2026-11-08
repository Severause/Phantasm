// Phantasm - native self-tests: MannequinSpringCore (the actor view's secondary motion: how the spring
// steps, rests and resets, its cone, and the camera-spin turn).
//
// Author: Severause

#include "MannequinSpringCore.h"
#include "TestHarness.h"

#include <cmath>

namespace Phantasm::Tests
{
    void RunMannequinSpringCoreTests()
    {
        namespace MS = Phantasm::MannequinSpringCore;
        using MS::Vec3;
        const auto near = [](float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; };

        // 1. A first step rests; a still target stays at rest
        const Vec3             origin{ 0, 0, 100 };
        const Vec3             target{ 0, 0, 90 };   // a 10u bone hanging straight down
        MS::SpringState        s;
        const MS::SpringParams hair{ 70.0f, 7.0f, 0.30f };
        Check(!MS::Step(s, origin, target, hair, 1.0f / 60.0f) && s.primed && near(s.tip.z, 90.0f),
              "spring: the first step puts the tip at rest");
        Check(!MS::Step(s, origin, target, hair, 1.0f / 60.0f) && near(s.tip.z, 90.0f), "spring: a still target stays at rest");

        // 2. A moving target swings it, then it settles; the bone keeps its length and the cone holds
        const Vec3 moved{ 0, 10, 100 };   // the pose turns the bone 90 degrees in one frame
        const bool moving = MS::Step(s, origin, moved, hair, 1.0f / 60.0f);
        const float len   = MS::Length(s.tip - origin);
        const float cosA  = MS::Dot(s.tip - origin, moved - origin) / (len * 10.0f);
        Check(moving && near(len, 10.0f, 1e-2f), "spring: a jump of the target swings the tip and keeps the bone's length");
        Check(std::acos(std::fmin(1.0f, cosA)) <= hair.maxAngle + 1e-3f, "spring: the tip never leaves the cone");
        for (int i = 0; i < 600; ++i) MS::Step(s, origin, moved, hair, 1.0f / 60.0f);
        Check(!MS::Step(s, origin, moved, hair, 1.0f / 60.0f) && MS::Length(s.tip - moved) < 0.05f,
              "spring: it settles on the new rest");

        // 3. A long gap resets instead of catching up; a bad step is safe
        MS::Step(s, origin, target, hair, 1.0f / 60.0f);
        Check(!MS::Step(s, origin, moved, hair, 2.0f) && near(s.tip.y, 10.0f), "spring: a long gap puts the tip at rest");
        Check(!MS::Step(s, origin, target, hair, 0.0f) && near(s.tip.z, 90.0f), "spring: a zero step rests");
        MS::SpringState none;
        Check(!MS::Step(none, origin, target, MS::SpringParams{}, 1.0f / 60.0f), "spring: no params never move");

        // 4. The cone and the camera turn
        const Vec3 c = MS::ClampToCone({ 10, 0, 0 }, { 0, 0, -10 }, 0.5f);
        Check(near(MS::Length(c), 10.0f) && near(std::acos(MS::Dot(c, { 0, 0, -1 }) / 10.0f), 0.5f),
              "spring: the cone turns a vector to its edge at the same length");
        const Vec3 r = MS::RotateAboutZ({ 10, 0, 5 }, { 0, 0, 0 }, 3.14159265f / 2.0f);
        Check(near(r.x, 0.0f) && near(r.y, 10.0f) && near(r.z, 5.0f), "spring: a turn about Z is counter-clockwise seen from above");
        const Vec3 rp = MS::RotateAboutZ({ 11, 2, 0 }, { 1, 2, 0 }, 3.14159265f);
        Check(near(rp.x, -9.0f) && near(rp.y, 2.0f), "spring: the turn is about the pivot");
    }
}
