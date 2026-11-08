#pragma once

// Phantasm - MirrorLightingCore: the actor view's lighting presets (the lighting hint), the
// floor under the figure (the floor hint) and the "Here" preset's reading of the game's own light, engine-free
// (native/build.ps1 -Tests). MannequinRenderer.cpp turns a Look into its light rig and constant buffer.
//
// Author: Severause

#include "AsciiText.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <string_view>

namespace Phantasm::MirrorLightingCore
{
    enum class Preset : int
    {
        kStudio = 0,
        kDaylight,
        kHearth,
        kTorchlight,
        kMoonlight,
        kHere,
        kCount
    };

    /// The lighting hint's value for each preset, in cycle order.
    inline constexpr std::array<std::string_view, static_cast<std::size_t>(Preset::kCount)> kPresetIds{
        "studio", "daylight", "hearth", "torchlight", "moonlight", "here"
    };

    /// The preset the renderer starts with, and the one an unknown lighting hint value maps to.
    inline constexpr Preset kDefaultPreset = Preset::kHearth;

    inline std::string_view PresetId(Preset p) noexcept
    {
        const auto i = static_cast<std::size_t>(p);
        return i < kPresetIds.size() ? kPresetIds[i] : kPresetIds[static_cast<std::size_t>(kDefaultPreset)];
    }

    /// The preset a lighting hint value names, any case; anything else is the default (Hearth).
    inline Preset ParsePreset(std::string_view text) noexcept
    {
        for (std::size_t i = 0; i < kPresetIds.size(); ++i) {
            if (AsciiText::EqualsI(text, kPresetIds[i])) return static_cast<Preset>(i);
        }
        return kDefaultPreset;
    }

    struct Rgb
    {
        float r = 0, g = 0, b = 0;
    };

    /// What a light's direction is measured from: the camera (turns with the trackball), the figure's front (the
    /// light stays on one side of them as the camera turns) or the world (the game's own sun or directional light).
    enum class Frame
    {
        kCamera,
        kFigure,
        kWorld
    };

    /// A light: yawDeg around Z from its frame's forward (for kWorld, from +Y, the game's north, clockwise toward
    /// +X, east; for kCamera and kFigure counterclockwise seen from above, so a positive yaw is on the picture's right
    /// while that forward faces the viewer), up the height added to that horizontal unit vector before it is
    /// normalised (0 level, 1 = 45 deg).
    struct Light
    {
        Frame frame     = Frame::kCamera;
        float yawDeg    = 0.0f;
        float up        = 0.0f;
        Rgb   color     = {};
        float intensity = 0.0f;
    };

    /// A preset's whole look. Colours are linear (the offscreen is _SRGB). The ambient lobes multiply the piece's
    /// ambient floor, so they average about 1 where the light is as bright as the studio's. floorShadow is how dark
    /// the floor gets under the key's full shadow (0 none, 1 black); contact the dark patch under the feet.
    struct Look
    {
        Light key, fill, rim;
        Rgb   ambientSky, ambientGround;
        Rgb   background;
        float floorShadow = 0.5f;
        float contact     = 0.45f;
    };

    /// The studio rig (lights follow the camera), and the four other looks the lighting hint picks. The dramatic
    /// ones fix their key and fill to the figure, so turning the camera shows the lit and the dark side; every rim
    /// stays behind the figure from the viewer's side, for the edge it separates. Each key starts on the picture's
    /// right (the figure's left hand) and its fill on the left.
    inline Look PresetLook(Preset p) noexcept
    {
        using F = Frame;
        switch (p) {
        case Preset::kDaylight:
            return { { F::kFigure, 35.0f, 1.40f, { 1.00f, 0.97f, 0.92f }, 1.00f },
                     { F::kFigure, -60.0f, 0.40f, { 0.55f, 0.68f, 0.95f }, 0.30f },
                     { F::kCamera, 180.0f, 0.60f, { 0.90f, 0.95f, 1.00f }, 0.10f },
                     { 0.85f, 0.95f, 1.15f }, { 1.00f, 0.92f, 0.78f },
                     { 0.0170f, 0.0160f, 0.0140f }, 0.55f, 0.50f };
        case Preset::kHearth:
            return { { F::kFigure, 55.0f, 0.15f, { 1.00f, 0.62f, 0.30f }, 1.00f },
                     { F::kFigure, -70.0f, 0.10f, { 0.45f, 0.30f, 0.22f }, 0.12f },
                     { F::kCamera, 180.0f, 0.30f, { 1.00f, 0.55f, 0.25f }, 0.12f },
                     { 0.55f, 0.45f, 0.40f }, { 0.85f, 0.55f, 0.35f },
                     { 0.0100f, 0.0045f, 0.0018f }, 0.65f, 0.60f };
        case Preset::kTorchlight:
            return { { F::kFigure, 80.0f, 0.25f, { 1.00f, 0.55f, 0.22f }, 1.15f },
                     { F::kFigure, -90.0f, 0.10f, { 0.30f, 0.32f, 0.40f }, 0.05f },
                     { F::kCamera, 180.0f, 0.40f, { 1.00f, 0.60f, 0.30f }, 0.08f },
                     { 0.30f, 0.28f, 0.30f }, { 0.45f, 0.32f, 0.22f },
                     { 0.0040f, 0.0025f, 0.0015f }, 0.75f, 0.70f };
        case Preset::kMoonlight:
            return { { F::kFigure, 30.0f, 0.90f, { 0.55f, 0.68f, 1.00f }, 0.55f },
                     { F::kFigure, -60.0f, 0.20f, { 0.30f, 0.35f, 0.55f }, 0.15f },
                     { F::kCamera, 180.0f, 0.50f, { 0.75f, 0.85f, 1.00f }, 0.18f },
                     { 0.40f, 0.48f, 0.70f }, { 0.30f, 0.30f, 0.38f },
                     { 0.0018f, 0.0024f, 0.0045f }, 0.60f, 0.60f };
        case Preset::kStudio:
        case Preset::kHere:   // until a reading of the game's light replaces it (HereLook)
        default:
            return { { F::kCamera, 20.0f, 0.30f, { 1.00f, 0.91f, 0.77f }, 0.85f },
                     { F::kCamera, -35.0f, 0.10f, { 0.55f, 0.65f, 0.80f }, 0.28f },
                     { F::kCamera, 180.0f, 0.50f, { 0.65f, 0.85f, 0.95f }, 0.06f },
                     { 0.95f, 1.00f, 1.08f }, { 1.16f, 0.90f, 0.64f },
                     { 0.00744f, 0.00598f, 0.00382f }, 0.50f, 0.45f };
        }
    }

    // ── "Here": the game's own light ──

    /// How much each of the weather's four colour times (sunrise, day, sunset, night) counts at hour h (0-24): a
    /// time holds its colours, and the two around a change cross over in two hours. The weights sum to 1.
    inline std::array<float, 4> WeatherTimeWeights(float hour) noexcept
    {
        float h = std::fmod(hour, 24.0f);
        if (!(h >= 0.0f)) h = h < 0.0f ? h + 24.0f : 12.0f;   // NaN reads as noon
        std::array<float, 4> w{ 0, 0, 0, 0 };
        constexpr int kSunrise = 0, kDay = 1, kSunset = 2, kNight = 3;
        const auto blend = [&](int from, int to, float t) {
            w[from] = 1.0f - t;
            w[to]   = t;
        };
        if (h < 4.0f) w[kNight] = 1.0f;
        else if (h < 6.0f) blend(kNight, kSunrise, (h - 4.0f) / 2.0f);
        else if (h < 8.0f) blend(kSunrise, kDay, (h - 6.0f) / 2.0f);
        else if (h < 16.0f) w[kDay] = 1.0f;
        else if (h < 18.0f) blend(kDay, kSunset, (h - 16.0f) / 2.0f);
        else if (h < 20.0f) blend(kSunset, kNight, (h - 18.0f) / 2.0f);
        else w[kNight] = 1.0f;
        return w;
    }

    /// The direction toward the sun at hour h (6 rises in the east, +X; 18 sets in the west; at noon high and a
    /// little south, -Y), or toward the moon at night, lower. Returns a world-frame light (yaw from +Y toward +X).
    inline void SkyLightDirection(float hour, float& yawDeg, float& up) noexcept
    {
        constexpr float kPi = 3.14159265358979f;
        float h = std::fmod(hour, 24.0f);
        if (!(h >= 0.0f)) h = h < 0.0f ? h + 24.0f : 12.0f;
        const bool  day = h >= 6.0f && h < 18.0f;
        const float t   = day ? (h - 6.0f) / 12.0f * kPi : std::fmod(h + 6.0f, 24.0f) / 12.0f * kPi;
        const float x   = std::cos(t);            // +1 east at rise, -1 west at set
        const float y   = -0.35f;                 // a little south
        const float s   = std::sin(t);
        yawDeg = std::atan2(x, y) * (180.0f / kPi);
        // Never quite on the horizon: a light at 0 casts an endless shadow and lights nothing from above.
        up = std::max(0.12f, (day ? 1.6f : 0.9f) * s);
    }

    /// The game's light where the NPC stands: the cell's or the weather's colours as 0-1, the key's world direction.
    struct GameLight
    {
        bool  valid = false;
        Rgb   directional;           // interior directional, or the weather's sunlight
        Rgb   ambientUp, ambientDown;// the directional ambient's +Z and -Z lobes (light from above / below)
        Rgb   fog;                   // the interior's near fog, or the weather's lower sky
        float keyYawDeg = 0.0f;      // world frame
        float keyUp     = 0.6f;
    };

    inline float Luma(const Rgb& c) noexcept { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }

    /// c's hue, its brightest channel at 1 (black stays black): a saturated colour scaled by luma would push one
    /// channel far past 1 (pure blue to ~14).
    inline Rgb Hue(const Rgb& c) noexcept
    {
        const float m = std::max({ c.r, c.g, c.b });
        if (!(m > 1e-4f)) return { 0, 0, 0 };
        return { c.r / m, c.g / m, c.b / m };
    }

    inline Rgb Scaled(const Rgb& c, float s) noexcept { return { c.r * s, c.g * s, c.b * s }; }

    /// c scaled to luma `target` (a black colour stays black).
    inline Rgb WithLuma(const Rgb& c, float target) noexcept
    {
        const float l = Luma(c);
        if (!(l > 1e-4f)) return { 0, 0, 0 };
        const float s = target / l;
        return { c.r * s, c.g * s, c.b * s };
    }

    /// The Look for the game's light: its hues, with brightness kept in a range the mirror reads (an interior's
    /// directional light is dim, since its point lights do most of the work, and the mirror does not draw those).
    /// Bounded both ways: never black, never past the studio's key, and no channel past 1.
    inline Look HereLook(const GameLight& g) noexcept
    {
        Look look = PresetLook(Preset::kStudio);
        if (!g.valid) return look;
        const float keyLuma = std::clamp(Luma(g.directional) * 1.6f, 0.35f, 1.0f);
        const Rgb   keyHue  = Hue(g.directional);
        look.key = { Frame::kWorld, g.keyYawDeg, g.keyUp, Luma(keyHue) > 0.0f ? keyHue : Rgb{ 1, 1, 1 }, keyLuma * 0.9f };
        const Rgb ambAvg{ (g.ambientUp.r + g.ambientDown.r) * 0.5f, (g.ambientUp.g + g.ambientDown.g) * 0.5f,
                          (g.ambientUp.b + g.ambientDown.b) * 0.5f };
        const float ambLuma = std::clamp(Luma(ambAvg) * 2.5f, 0.35f, 1.0f);
        const Rgb   up      = Scaled(Hue(g.ambientUp), ambLuma);
        const Rgb   down    = Scaled(Hue(g.ambientDown), ambLuma);
        look.ambientSky    = Luma(up) > 0.0f ? up : Rgb{ ambLuma, ambLuma, ambLuma };
        look.ambientGround = Luma(down) > 0.0f ? down : Rgb{ ambLuma, ambLuma, ambLuma };
        look.fill          = { Frame::kWorld, g.keyYawDeg + 150.0f, 0.20f, Hue(ambAvg), 0.18f };
        if (!(Luma(look.fill.color) > 0.0f)) look.fill.color = { 1, 1, 1 };
        // The place's fog as a dark backdrop: kept near the studio's darkness so what a client draws around the
        // image still reads.
        const Rgb bg = WithLuma(g.fog, 0.006f);
        look.background = Luma(bg) > 0.0f ? bg : look.background;
        return look;
    }

    // ── The floor ──

    /// The floor disc's radius, in game units around the NPC's feet.
    inline constexpr float kFloorRadius = 140.0f;

    /// Whether the floor draws: off with the floor hint, and never seen from below (it would hide the figure).
    inline bool FloorDraws(bool floorOn, float cameraZ, float floorZ) noexcept
    {
        return floorOn && cameraZ > floorZ + 2.0f;
    }

    /// How strongly the floor shows at r (0 at the feet, 1 at the edge of the disc): full near the feet, fading
    /// smoothly to nothing at the edge, so it never has an edge or a horizon line.
    inline float FloorFade(float r) noexcept
    {
        if (!(r >= 0.0f)) return 0.0f;
        const float t = std::clamp((r - 0.35f) / 0.65f, 0.0f, 1.0f);
        return 1.0f - t * t * (3.0f - 2.0f * t);
    }
}
