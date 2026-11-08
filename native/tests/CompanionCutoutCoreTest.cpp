// Engine-free tests for CompanionCutoutCore (the party cutouts' signature and chest-up crop).

#include "TestHarness.h"

#include "CompanionCutoutCore.h"

#include <cstdint>
#include <vector>

namespace Phantasm::Tests
{
    void RunCompanionCutoutCoreTests()
    {
        namespace CC = Phantasm::CompanionCutoutCore;

        // ── Signature ──
        Check(CC::Signature({ 1, 2, 3 }) == CC::Signature({ 3, 1, 2 }), "cutout: the parts' order does not matter");
        Check(CC::Signature({ 1, 2, 3 }) != CC::Signature({ 1, 2, 4 }), "cutout: a changed part changes the signature");
        Check(CC::Signature({ 1, 2 }) != CC::Signature({ 1, 2, 0 }), "cutout: an added part changes the signature");
        Check(CC::SignatureText(0x1234abcdull) == "000000001234abcd", "cutout: the signature text is 16 hex digits");
        const auto sig7 = CC::SignatureText(CC::Signature({ 7 }));
        Check(CC::UpToDate(true, sig7, CC::kOutW, CC::Signature({ 7 }), true),
              "cutout: the same look with its file is up to date");
        Check(!CC::UpToDate(false, sig7, CC::kOutW, CC::Signature({ 7 }), true), "cutout: a missing file is taken again");
        Check(!CC::UpToDate(true, "", CC::kOutW, CC::Signature({ 7 }), false), "cutout: no stored signature is taken again");
        Check(CC::UpToDate(true, sig7, CC::kSmallW, CC::Signature({ 7 }), false),
              "cutout: a tracked NPC's small cutout is up to date for them");
        Check(!CC::UpToDate(true, sig7, CC::kSmallW, CC::Signature({ 7 }), true),
              "cutout: a small cutout is taken again once they join the party");

        // ── The cap on tracked cutouts ──
        Check(CC::OldestBeyond({ 5, 1, 9 }, 3).empty(), "cutout: under the cap nothing goes");
        const auto gone = CC::OldestBeyond({ 5, 1, 9, 3 }, 2);
        Check(gone.size() == 2 && gone[0] == 1 && gone[1] == 3, "cutout: past the cap the oldest go first");

        // ── The chest-up crop ──
        // A 400 x 800 image: a head (x 180-220, y 100-150) over a body (x 140-260, y 150-750).
        const std::uint32_t W = 400, H = 800;
        std::vector<std::uint8_t> a(static_cast<std::size_t>(W) * H, 0);
        const auto fill = [&](std::uint32_t x0, std::uint32_t y0, std::uint32_t x1, std::uint32_t y1) {
            for (std::uint32_t y = y0; y < y1; ++y)
                for (std::uint32_t x = x0; x < x1; ++x) a[static_cast<std::size_t>(y) * W + x] = 255;
        };
        fill(180, 100, 220, 150);
        fill(140, 150, 260, 750);
        const auto at = [&](std::uint32_t x, std::uint32_t y) { return a[static_cast<std::size_t>(y) * W + x]; };
        const auto r = CC::ChestUpCrop(W, H, 600.0f, at);
        // Standing 650 px tall (the figure outgrows the 600 guess): 39% of it high, 4:5, from 5% over the top.
        Check(r.h == 254 && r.w == 203, "cutout: the crop is 39% of the standing height, 4:5");
        Check(r.y == 68, "cutout: the crop starts the headroom over the head");
        Check(r.x + r.w / 2 >= 195 && r.x + r.w / 2 <= 205, "cutout: the crop is centred on the head");

        // Shoulders that sit off to one side (a turned pose, a cloak) and a ponytail beside the neck: the crop stays
        // on the head.
        std::fill(a.begin(), a.end(), 0);
        fill(180, 100, 220, 150);
        fill(220, 125, 270, 150);
        fill(150, 150, 340, 750);
        const auto t = CC::ChestUpCrop(W, H, 600.0f, at);
        Check(t.x + t.w / 2 >= 195 && t.x + t.w / 2 <= 205, "cutout: one-sided shoulders and hair do not pull the crop off the head");
        Check(CC::HeadCentreColumn(W, 0, 10, at) < 0.0f, "cutout: a band with no figure has no head column");

        // Seated: the figure is shorter than standing, so the standing guess sizes the crop.
        std::fill(a.begin(), a.end(), 0);
        fill(180, 400, 220, 450);
        fill(140, 450, 260, 650);
        const auto s = CC::ChestUpCrop(W, H, 600.0f, at);
        Check(s.h == 234 && s.y == 370, "cutout: a seated figure is cropped at their standing size");

        // Off to one side: the crop moves, and stays inside the image.
        std::fill(a.begin(), a.end(), 0);
        fill(10, 100, 40, 150);
        fill(0, 150, 60, 750);
        const auto e = CC::ChestUpCrop(W, H, 600.0f, at);
        Check(e.x == 0 && e.w > 0, "cutout: a crop at the edge stays inside the image");

        // A lone speck is not a head; an empty image has no crop.
        std::fill(a.begin(), a.end(), 0);
        a[static_cast<std::size_t>(5) * W + 5] = 255;
        Check(CC::ChestUpCrop(W, H, 600.0f, at).Empty(), "cutout: one stray pixel is no figure");
        Check(CC::ChestUpCrop(0, 0, 600.0f, at).Empty(), "cutout: an empty image has no crop");

        // A figure taller than the image fits: the crop shrinks to it.
        std::fill(a.begin(), a.end(), 255);
        const auto big = CC::ChestUpCrop(100, 100, 5000.0f, [&](std::uint32_t, std::uint32_t) { return std::uint8_t(255); });
        Check(big.w <= 100 && big.h <= 100 && !big.Empty(), "cutout: a crop larger than the image is shrunk to fit");

        // ── Pixels per unit ──
        const float ppu = CC::PixelsPerUnit(2048, 280.0f, 35.0f);
        Check(ppu > 11.5f && ppu < 11.7f, "cutout: 2048 rows over 280u at 35 degrees is about 11.6 px a unit");
        Check(CC::PixelsPerUnit(2048, 0.0f, 35.0f) == 0.0f, "cutout: no distance, no scale");

        // ── Unpremultiply, in linear light ──
        std::uint8_t px[12] = { 64, 32, 0, 128, 10, 10, 10, 0, 200, 100, 50, 255 };
        CC::UnpremultiplySrgb(px, 3);
        Check(px[0] == 90 && px[1] == 47 && px[2] == 0 && px[3] == 128,
              "cutout: half alpha doubles the colour in linear light, not the encoded value");
        Check(px[4] == 0 && px[7] == 0, "cutout: a clear pixel is all zero");
        Check(px[8] == 200 && px[11] == 255, "cutout: an opaque pixel is unchanged");

        // ── The shot budget ──
        CC::ShotBudget budget;
        int taken = 0;
        while (budget.Take(0.0)) ++taken;
        Check(taken == static_cast<int>(CC::kBudgetMax), "cutout budget: a full budget gives kBudgetMax shots at once");
        Check(budget.WaitSec(0.0) > 0.0, "cutout budget: an empty budget waits");
        Check(!budget.Take(CC::kBudgetRefillSec * 0.5), "cutout budget: half a refill is not a shot");
        Check(budget.Take(CC::kBudgetRefillSec * 1.01), "cutout budget: one refill period gives one shot back");
        Check(!budget.Take(CC::kBudgetRefillSec * 1.02), "cutout budget: and only one");
        budget.Refill(10000.0);
        Check(budget.tokens == CC::kBudgetMax, "cutout budget: a long idle never fills past kBudgetMax");
        CC::ShotBudget late;
        late.Take(5.0);
        Check(late.Take(4.0), "cutout budget: a clock that goes back spends from what is there, nothing refilled");
    }
}
