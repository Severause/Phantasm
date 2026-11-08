// Phantasm - native self-tests: LiveViewSizingCore (the live views' offscreen size).
//
// Author: Severause

#include "LiveViewSizingCore.h"
#include "TestHarness.h"

#include <cmath>
#include <limits>

namespace Phantasm::Tests
{
    namespace
    {
        namespace LVS = Phantasm::LiveViewSizingCore;

        bool Near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

        float Aspect(const LVS::Plan& p) { return static_cast<float>(p.offW) / static_cast<float>(p.offH); }
    }

    void RunLiveViewSizingCoreTests()
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();

        // 1. Uncapped: twice the view on both sides
        {
            const auto p = LVS::PlanImage(300.0f, 570.0f);
            Check(p.ok && p.offW == 600 && p.offH == 1140, "image: a 300x570 view renders 600x1140");
        }

        // 2. The tall mirror hits the cap on ONE side: both sides shrink by the same factor
        {
            const auto p = LVS::PlanImage(900.0f, 1710.0f);
            Check(p.ok && p.offH == 2048 && p.offW == 1078, "image: a 900x1710 mirror renders 1078x2048, not 1800x2048");
            Check(Near(Aspect(p), 900.0f / 1710.0f, 0.001f), "image: a capped offscreen keeps the view's aspect");
        }

        // 3. A wide view capped on its width
        {
            const auto p = LVS::PlanImage(1600.0f, 900.0f);
            Check(p.ok && p.offW == 2048 && p.offH == 1152, "image: a 1600x900 view renders 2048x1152");
        }

        // 4. A VR item (243x181 view, square eye target): sized from the rect alone, so its 1.34 aspect survives
        {
            const auto p = LVS::PlanImage(243.0f, 181.0f);
            Check(p.ok && p.offW == 486 && p.offH == 362, "image: the 243x181 VR item renders 486x362");
        }

        // 5. The image path refuses a degenerate or non-finite rect
        {
            Check(!LVS::PlanImage(3.9f, 500.0f).ok && !LVS::PlanImage(500.0f, 3.9f).ok, "image: a side under 4 px draws nothing");
            Check(LVS::PlanImage(4.0f, 4.0f).ok && LVS::PlanImage(4.0f, 4.0f).offW == LVS::kMinSide,
                  "image: a 4x4 view draws, at the minimum side");
            Check(!LVS::PlanImage(nan, 500.0f).ok && !LVS::PlanImage(500.0f, nan).ok, "image: a NaN side draws nothing");
            Check(!LVS::PlanImage(inf, 500.0f).ok, "image: an infinite side draws nothing");
        }

        // 6. Every side stays within [kMinSide, kMaxSide]
        {
            const auto p = LVS::PlanImage(5.0f, 100000.0f);
            Check(p.ok && p.offW == LVS::kMinSide && p.offH == LVS::kMaxSide, "image: an extreme strip clamps to the limits");
        }

        // 7. A view the client sizes under 4 px is stored at 4, so it draws (and its first frame answers)
        {
            Check(LVS::ViewPx(1.0f) == LVS::kMinDrawPx && LVS::ViewPx(3.0f) == LVS::kMinDrawPx &&
                      LVS::ViewPx(0.0f) == LVS::kMinDrawPx && LVS::ViewPx(-5.0f) == LVS::kMinDrawPx,
                  "view px: a side under 4 px is raised to 4");
            Check(LVS::ViewPx(nan) == LVS::kMinDrawPx && LVS::ViewPx(inf) == LVS::kMinDrawPx,
                  "view px: a non-finite side is 4");
            Check(LVS::ViewPx(4.0f) == 4.0f && LVS::ViewPx(640.5f) == 640.5f, "view px: a drawable side is kept");
            Check(LVS::PlanImage(LVS::ViewPx(1.0f), LVS::ViewPx(2.0f)).ok, "view px: a raised 1x2 view draws");
        }
    }
}
