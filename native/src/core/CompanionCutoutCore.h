#pragma once

// The party cutouts (CompanionCutouts): a character drawn from the chest up on a transparent ground, the way the
// Dashboard's party row shows them. The rules here are engine-free; tested by CompanionCutoutCoreTest.cpp.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Phantasm::CompanionCutoutCore
{
    /// Bumped when the shot or the crop changes, so every stored cutout is taken again.
    inline constexpr std::uint32_t kVersion = 7;

    /// The stored cutout's size (4:5): a party member's (the Dashboard's party row draws it large), and a tracked
    /// NPC's (only ever a face in a list).
    inline constexpr std::uint32_t kOutW   = 384;
    inline constexpr std::uint32_t kOutH   = 480;
    inline constexpr std::uint32_t kSmallW = 256;
    inline constexpr std::uint32_t kSmallH = 320;

    /// Tracked NPCs (not the party) shot per menu open, nearest first, and the most cutouts of NPCs outside the party
    /// kept on disk, tracked and shown alike (the oldest go first; a party member's and the player's are never removed).
    inline constexpr std::size_t kTrackedPerOpen = 8;
    inline constexpr std::size_t kTrackedKept    = 400;
    /// The other NPCs a page showed this session (CompanionCutouts::NoteShown) queued per scan (a menu open's, or a
    /// page's showing someone new), nearest first.
    inline constexpr std::size_t kShownPerScan = 8;

    /// The budget every shot outside the party draws from: kBudgetMax at most, one back every kBudgetRefillSec
    /// (about 15 a minute), so walking through a busy place fills faces in over time rather than in one burst.
    inline constexpr double kBudgetMax       = 12.0;
    inline constexpr double kBudgetRefillSec = 4.0;

    struct ShotBudget
    {
        double tokens = kBudgetMax;
        double lastSec = -1.0;   // when tokens was last brought up to date (-1 = never)

        void Refill(double nowSec)
        {
            if (lastSec >= 0.0 && nowSec > lastSec) {
                tokens = std::min(kBudgetMax, tokens + (nowSec - lastSec) / kBudgetRefillSec);
            }
            lastSec = nowSec;
        }
        /// Spends one shot; false (nothing spent) when none is left.
        bool Take(double nowSec)
        {
            Refill(nowSec);
            if (tokens < 1.0) return false;
            tokens -= 1.0;
            return true;
        }
        /// Seconds until a shot is available again (0 when one is).
        double WaitSec(double nowSec)
        {
            Refill(nowSec);
            return tokens >= 1.0 ? 0.0 : (1.0 - tokens) * kBudgetRefillSec;
        }
    };

    /// The figure shot's view, in view pixels: the offscreen supersamples it to 1280 x 2048, which leaves about 550
    /// rows for a human's chest-up crop.
    inline constexpr float kShotRectW = 640.0f;
    inline constexpr float kShotRectH = 1024.0f;

    /// A pixel at or under this alpha is ground, not figure.
    inline constexpr std::uint8_t kAlphaCut = 24;

    /// The crop, as fractions of the character's standing height: headroom over the top of the figure, and the
    /// drop from that top to the bottom edge (the middle of the chest).
    inline constexpr float kHeadroom = 0.05f;
    inline constexpr float kDrop     = 0.34f;
    /// The band under the top the crop centres on: the head, about an eighth of a standing human. Centring on the
    /// shoulders too pulled a turned pose, a cloak or long hair off to one side.
    inline constexpr float kHeadBand = 0.12f;
    /// A row of the band wider than this times the head's own width is shoulders, hair or an arm, not the head.
    inline constexpr float kHeadWidthLimit = 1.5f;

    /// The head's centre column: the median midpoint (between the outermost figure pixels) of the rows from top to
    /// bandEnd, leaving out rows wider than kHeadWidthLimit times the median width of the band's upper half.
    /// Negative when no row holds figure.
    template <class AlphaAt>
    inline float HeadCentreColumn(std::uint32_t w, std::uint32_t top, std::uint32_t bandEnd, AlphaAt alpha)
    {
        struct Span { float mid; std::uint32_t width; };
        std::vector<Span> rows;
        for (std::uint32_t y = top; y < bandEnd; ++y) {
            std::uint32_t l = w, r = 0;
            for (std::uint32_t x = 0; x < w; ++x) {
                if (alpha(x, y) <= kAlphaCut) continue;
                if (l == w) l = x;
                r = x;
            }
            if (l < w) rows.push_back({ (static_cast<float>(l) + static_cast<float>(r)) * 0.5f, r - l + 1 });
        }
        if (rows.empty()) return -1.0f;
        std::vector<std::uint32_t> upper;
        for (std::size_t i = 0; i < (rows.size() + 1) / 2; ++i) upper.push_back(rows[i].width);
        std::nth_element(upper.begin(), upper.begin() + upper.size() / 2, upper.end());
        const float limit = static_cast<float>(upper[upper.size() / 2]) * kHeadWidthLimit;
        std::vector<float> mids;
        for (const auto& s : rows)
            if (static_cast<float>(s.width) <= limit) mids.push_back(s.mid);
        if (mids.empty()) return rows.front().mid;
        std::nth_element(mids.begin(), mids.begin() + mids.size() / 2, mids.end());
        return mids[mids.size() / 2];
    }

    /// A humanoid's standing height in game units at race height and scale 1.
    inline constexpr float kHumanHeight = 122.0f;

    /// The look a cutout was taken of: FNV-1a over the parts the caller lists (CompanionCutouts' LookSignature),
    /// sorted so their order does not matter, and kVersion.
    inline std::uint64_t Signature(std::vector<std::uint32_t> parts)
    {
        std::sort(parts.begin(), parts.end());
        std::uint64_t h = 1469598103934665603ull;
        const auto mix = [&h](std::uint32_t v) {
            for (int i = 0; i < 4; ++i) {
                h ^= (v >> (i * 8)) & 0xFFu;
                h *= 1099511628211ull;
            }
        };
        mix(kVersion);
        mix(static_cast<std::uint32_t>(parts.size()));
        for (const auto p : parts) mix(p);
        return h;
    }

    /// A signature as 16 hex digits (the sidecar's text).
    inline std::string SignatureText(std::uint64_t sig)
    {
        static constexpr char kHex[] = "0123456789abcdef";
        std::string s(16, '0');
        for (int i = 15; i >= 0; --i) {
            s[static_cast<std::size_t>(i)] = kHex[sig & 0xF];
            sig >>= 4;
        }
        return s;
    }

    struct Rect
    {
        std::uint32_t x = 0, y = 0, w = 0, h = 0;
        bool          Empty() const noexcept { return w == 0 || h == 0; }
    };

    /// The pixels per game unit at the subject, for a vertical field of view fovDeg seen from distance units away
    /// on an image imageH pixels tall.
    inline float PixelsPerUnit(std::uint32_t imageH, float distance, float fovDeg) noexcept
    {
        const float half = std::tan(fovDeg * 0.5f * 3.14159265f / 180.0f);
        if (!(distance > 0.0f) || !(half > 0.0f)) return 0.0f;
        return static_cast<float>(imageH) / (2.0f * distance * half);
    }

    /// The chest-up crop of a figure on a transparent ground. alpha(x, y) reads a pixel's alpha. standingPx is the
    /// character's standing height in pixels; a figure drawn taller (they stand, and the guess ran short) is taken
    /// as their height instead. The crop starts kHeadroom over the figure's top, reaches kDrop below it, is centred
    /// on the head (HeadCentreColumn over the kHeadBand under its top), keeps the kOutW:kOutH aspect and stays inside the image
    /// (shrunk when the image is too small). Empty when the image holds no figure.
    template <class AlphaAt>
    inline Rect ChestUpCrop(std::uint32_t w, std::uint32_t h, float standingPx, AlphaAt alpha)
    {
        if (w == 0 || h == 0) return {};
        // A row counts once it holds two figure pixels: a lone speck (a stray spark) is not the head.
        const auto rowHas = [&](std::uint32_t y) {
            int n = 0;
            for (std::uint32_t x = 0; x < w; ++x) {
                if (alpha(x, y) > kAlphaCut && ++n >= 2) return true;
            }
            return false;
        };
        std::uint32_t top = 0;
        while (top < h && !rowHas(top)) ++top;
        if (top >= h) return {};
        std::uint32_t bottom = h - 1;
        while (bottom > top && !rowHas(bottom)) --bottom;
        const float figurePx = static_cast<float>(bottom - top + 1);
        const float height   = std::max(standingPx > 0.0f ? standingPx : 0.0f, figurePx);

        const std::uint32_t bandEnd = std::min<std::uint32_t>(h, top + static_cast<std::uint32_t>(height * kHeadBand) + 1);
        const float head = HeadCentreColumn(w, top, bandEnd, alpha);
        const float cx = head >= 0.0f ? head : static_cast<float>(w) * 0.5f;

        float cropH = height * (kHeadroom + kDrop);
        float cropW = cropH * static_cast<float>(kOutW) / static_cast<float>(kOutH);
        const float fit = std::min({ 1.0f, static_cast<float>(w) / cropW, static_cast<float>(h) / cropH });
        cropW *= fit;
        cropH *= fit;
        const auto rw = std::max<std::uint32_t>(1, std::min<std::uint32_t>(w, static_cast<std::uint32_t>(std::lround(cropW))));
        const auto rh = std::max<std::uint32_t>(1, std::min<std::uint32_t>(h, static_cast<std::uint32_t>(std::lround(cropH))));
        const float x0 = std::clamp(cx - static_cast<float>(rw) * 0.5f, 0.0f, static_cast<float>(w - rw));
        const float y0 = std::clamp(static_cast<float>(top) - height * kHeadroom * fit, 0.0f, static_cast<float>(h - rh));
        return { static_cast<std::uint32_t>(std::lround(x0)), static_cast<std::uint32_t>(std::lround(y0)), rw, rh };
    }

    inline float SrgbToLinear(float s) noexcept
    {
        return s <= 0.04045f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
    }
    inline float LinearToSrgb(float l) noexcept
    {
        l = std::clamp(l, 0.0f, 1.0f);
        return l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
    }

    /// Premultiplied RGBA8 back to straight alpha in place (PNG stores straight alpha). The colour was premultiplied
    /// in linear light and then sRGB-encoded (an _SRGB render target), so it is decoded, divided by the alpha and
    /// encoded again; dividing the encoded value would brighten every soft edge. A clear pixel stays all zero.
    inline void UnpremultiplySrgb(std::uint8_t* rgba, std::size_t pixels) noexcept
    {
        for (std::size_t i = 0; i < pixels; ++i) {
            std::uint8_t* p = rgba + i * 4;
            const unsigned a = p[3];
            if (a == 0) {
                p[0] = p[1] = p[2] = 0;
                continue;
            }
            if (a == 255) continue;
            const float alpha = static_cast<float>(a) / 255.0f;
            for (int c = 0; c < 3; ++c) {
                const float lin = SrgbToLinear(static_cast<float>(p[c]) / 255.0f) / alpha;
                p[c] = static_cast<std::uint8_t>(std::lround(LinearToSrgb(lin) * 255.0f));
            }
        }
    }

    /// Whether the cutout stored under storedSig, storedW wide, shows the look now: the file is there, the signature
    /// is the same, and a party member's is full size (a tracked NPC who joins the party is taken again, large).
    inline bool UpToDate(bool fileExists, const std::string& storedSig, std::uint32_t storedW, std::uint64_t currentSig,
                         bool party)
    {
        return fileExists && storedSig == SignatureText(currentSig) && (!party || storedW >= kOutW);
    }

    /// Which tracked cutouts to delete to keep at most cap: given each one's last-write time, the indexes of the
    /// oldest past the cap.
    inline std::vector<std::size_t> OldestBeyond(const std::vector<std::int64_t>& stamps, std::size_t cap)
    {
        std::vector<std::size_t> order(stamps.size());
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
        if (stamps.size() <= cap) return {};
        std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            return stamps[a] != stamps[b] ? stamps[a] < stamps[b] : a < b;
        });
        order.resize(stamps.size() - cap);
        return order;
    }
}
