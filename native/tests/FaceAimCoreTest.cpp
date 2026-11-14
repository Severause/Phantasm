// Phantasm - native self-tests: FaceAimCore (the face camera of the `aim` hint).
//
// Author: Severause

#include "FaceAimCore.h"
#include "TestHarness.h"

#include <cmath>
#include <limits>

namespace Phantasm::Tests
{
    namespace
    {
        namespace FA = Phantasm::FaceAimCore;
        namespace MC = Phantasm::MirrorCameraCore;

        bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }
        bool Near(FA::V3 a, FA::V3 b, float eps = 1e-3f) { return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps); }

        FA::M3 Mul(const FA::M3& a, const FA::M3& b)
        {
            FA::M3 o;
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 3; ++c) o.m[r][c] = a.m[r][0] * b.m[0][c] + a.m[r][1] * b.m[1][c] + a.m[r][2] * b.m[2][c];
            }
            return o;
        }

        // An actor's root at heading h (radians, Skyrim's angle Z), row-vector: +Y goes to (sin h, cos h, 0).
        FA::M3 Root(float h)
        {
            FA::M3 m;
            m.m[0][0] = std::cos(h);
            m.m[0][1] = -std::sin(h);
            m.m[0][2] = 0;
            m.m[1][0] = std::sin(h);
            m.m[1][1] = std::cos(h);
            m.m[1][2] = 0;
            m.m[2][0] = 0;
            m.m[2][1] = 0;
            m.m[2][2] = 1;
            return m;
        }

        // A head bone whose own axes are not the root's (as a skeleton's are not): +X along the neck, +Y back, with a
        // scale, so the face is found from the rest pose, not from a fixed bone axis.
        FA::M3 OddRestHead()
        {
            FA::M3 m;
            const float s = 1.1f;
            m.m[0][0] = 0;
            m.m[0][1] = 0;
            m.m[0][2] = s;    // the bone's +X goes up the root
            m.m[1][0] = 0;
            m.m[1][1] = -s;   // its +Y goes back
            m.m[1][2] = 0;
            m.m[2][0] = s;    // its +Z goes to the root's right
            m.m[2][1] = 0;
            m.m[2][2] = 0;
            return m;
        }
    }

    void RunFaceAimCoreTests()
    {
        {
            FA::Frame f;
            Check(FA::MakeFrame({ 0, 2, 0.5f }, { 0, 0.3f, 1 }, f) && Near(MC::Length(f.forward), 1) &&
                      Near(MC::Length(f.up), 1) && Near(MC::Dot(f.forward, f.up), 0) && f.up.z > 0,
                  "face aim: a frame is made unit and orthogonal");
            const FA::Frame before = f;
            Check(!FA::MakeFrame({ 0, 1, 0 }, { 0, 2, 0 }, f) && Near(f.forward, before.forward),
                  "face aim: a parallel forward and up make no frame");
            Check(!FA::MakeFrame({ 0, 0, 0 }, { 0, 0, 1 }, f) &&
                      !FA::MakeFrame({ std::numeric_limits<float>::quiet_NaN(), 1, 0 }, { 0, 0, 1 }, f),
                  "face aim: a zero or NaN axis makes no frame");
        }
        {
            const FA::M3 rest = OddRestHead();
            const float  h    = 30.0f * FA::kDeg;
            FA::Frame    f;
            Check(FA::HeadFrame(rest, Mul(rest, Root(h)), f) && Near(f.forward, { std::sin(h), std::cos(h), 0 }) &&
                      Near(f.up, { 0, 0, 1 }),
                  "face aim: a head at rest looks along the actor's heading");
            Check(FA::HeadFrame(rest, Mul(Mul(rest, Root(180.0f * FA::kDeg)), Root(h)), f) &&
                      Near(f.forward, { -std::sin(h), -std::cos(h), 0 }),
                  "face aim: a skeleton the animation turned around looks back along the heading");
            FA::M3 onBack;   // about the root's X: +Y (the face) to +Z
            onBack.m[1][0] = 0;
            onBack.m[1][1] = 0;
            onBack.m[1][2] = 1;
            onBack.m[2][0] = 0;
            onBack.m[2][1] = -1;
            onBack.m[2][2] = 0;
            Check(FA::HeadFrame(rest, Mul(rest, onBack), f) && Near(f.forward, { 0, 0, 1 }) && Near(f.up, { 0, -1, 0 }),
                  "face aim: a head lying on its back looks up, its crown along the floor");
            FA::M3 flat;
            flat.m[1][1] = 0;
            Check(!FA::HeadFrame(rest, Mul(rest, flat), f), "face aim: a degenerate head makes no frame");
        }
        {
            const FA::Frame  up{};
            const FA::Camera c = FA::Orbit(up, 0, 0);
            Check(Near(c.dir, up.forward) && Near(c.up, up.up), "face aim: the camera looks the face in the eye");
            Check(Near(FA::Orbit(up, 90, 0).dir, { -1, 0, 0 }), "face aim: a positive yaw goes to the face's left");
            const FA::Camera p = FA::Orbit(up, 40, 30);
            Check(Near(MC::Length(p.dir), 1) && Near(MC::Length(p.up), 1) && Near(MC::Dot(p.dir, p.up), 0) &&
                      MC::Dot(p.up, up.up) > 0 && p.dir.z > 0,
                  "face aim: a turned and raised camera keeps an orthogonal up toward the crown");
            const FA::Camera lying = FA::Orbit({ { 0, 0, 1 }, { 0, -1, 0 } }, 0, 0);
            Check(Near(lying.dir, { 0, 0, 1 }) && Near(lying.up, { 0, -1, 0 }),
                  "face aim: a face looking up is seen from above, crown up in the picture");
            const float nan = std::numeric_limits<float>::quiet_NaN();
            Check(Near(FA::Orbit(up, nan, nan).dir, up.forward), "face aim: a NaN orbit is the front");
            Check(Near(FA::Orbit(up, 0, 200).dir, FA::Orbit(up, 0, 80).dir), "face aim: the pitch is clamped as the orbit's");
        }
        {
            // An upright face looking along the heading is the figure orbit (the live view's camera formula).
            bool same = true;
            for (const float hDeg : { 0.0f, 37.0f, -120.0f, 179.0f }) {
                const float     h     = hDeg * FA::kDeg;
                const float     front = MC::FacingYawDeg(h);
                const FA::Frame byYaw = FA::FigureFrame(front);
                FA::Frame       byHead;
                same = same && FA::HeadFrame(OddRestHead(), Mul(OddRestHead(), Root(h)), byHead) &&
                       Near(byYaw.forward, byHead.forward) && Near(byYaw.up, byHead.up);
                for (const float d : { 0.0f, 25.0f, -90.0f }) {
                    for (const float pDeg : { 0.0f, 20.0f, -35.0f }) {
                        const float y = (front + d) * FA::kDeg, p = pDeg * FA::kDeg;
                        const FA::V3 orbit{ std::sin(y) * std::cos(p), -std::cos(y) * std::cos(p), std::sin(p) };
                        same = same && Near(FA::Orbit(byHead, d, pDeg).dir, orbit);
                    }
                }
            }
            Check(same, "face aim: an upright face looking along the heading orbits as the figure does");
        }
        {
            Check(Near(FA::Target({ 0, 0, 120 }, {}, 1.0f, -2.0f), { 0, 0, 122 }),
                  "face aim: a shot's zoom moves the target along the face's up");
            Check(Near(FA::Target({ 0, 0, 120 }, {}, 1.0f), { 0, 0, 114 }) &&
                      Near(FA::Target({ 0, 0, 30 }, { { 0, 0, 1 }, { 0, -1, 0 } }, 2.0f), { 0, 12, 30 }),
                  "face aim: the camera looks a little below the head bone, along the face's up");
        }
        {
            FA::Follow      follow;
            const FA::Frame front{};
            Check(!FA::Step(follow, front, { 0, 0, 100 }, 0.016f, false) && follow.have &&
                      Near(follow.target, { 0, 0, 100 }),
                  "face aim: the first step takes the face at once");
            const float     a = 30.0f * FA::kDeg;
            const FA::Frame turned{ { std::sin(a), std::cos(a), 0 }, { 0, 0, 1 } };
            const bool      moving = FA::Step(follow, turned, { 10, 0, 100 }, 0.05f, false);
            const float     got    = FA::AngleDeg(follow.frame.forward, front.forward);
            Check(moving && got > 1.0f && got < 29.0f && follow.target.x > 0.5f && follow.target.x < 9.5f,
                  "face aim: a turn is followed part of the way in a step");
            int steps = 0;
            while (FA::Step(follow, turned, { 10, 0, 100 }, 0.05f, false) && steps < 1000) ++steps;
            Check(steps < 1000 && Near(follow.frame.forward, turned.forward, 1e-6f) && Near(follow.target, { 10, 0, 100 }, 1e-6f),
                  "face aim: the follow settles exactly, then stops");
            const FA::Frame held = follow.frame;
            Check(FA::Step(follow, front, { 0, 0, 100 }, std::numeric_limits<float>::quiet_NaN(), false) &&
                      Near(follow.frame.forward, held.forward),
                  "face aim: a NaN step holds and stays on its way");
            const float     b = 170.0f * FA::kDeg;
            const FA::Frame back{ { std::sin(b), std::cos(b), 0 }, { 0, 0, 1 } };
            Check(!FA::Step(follow, back, { 0, 0, 100 }, 0.016f, false) && Near(follow.frame.forward, back.forward),
                  "face aim: a turn past the jump angle is taken at once");
            Check(!FA::Step(follow, front, { 5, 5, 100 }, 0.016f, true) && Near(follow.frame.forward, front.forward) &&
                      Near(follow.target, { 5, 5, 100 }),
                  "face aim: a snap takes the face at once");
            Check(!FA::Step(follow, turned, { std::numeric_limits<float>::infinity(), 0, 0 }, 0.016f, false) &&
                      Near(follow.frame.forward, front.forward),
                  "face aim: a target that is not finite changes nothing");
        }
    }
}
