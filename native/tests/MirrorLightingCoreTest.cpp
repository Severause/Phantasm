// Phantasm - native self-tests: MirrorLightingCore (the actor view's lighting presets, the "Here"
// reading of the game's light and the floor).
//
// Author: Severause

#include "MirrorLightingCore.h"
#include "TestHarness.h"

#include <cmath>

namespace Phantasm::Tests
{
    void RunMirrorLightingCoreTests()
    {
        namespace ML = Phantasm::MirrorLightingCore;
        using P      = ML::Preset;

        Check(ML::ParsePreset("torchlight") == P::kTorchlight && ML::ParsePreset("Moonlight") == P::kMoonlight &&
                  ML::ParsePreset("HERE") == P::kHere,
              "lighting: a preset is read in any case");
        Check(ML::kDefaultPreset == P::kHearth && ML::ParsePreset("") == P::kHearth &&
                  ML::ParsePreset("candles") == P::kHearth && ML::ParsePreset("studiox") == P::kHearth,
              "lighting: anything else is the default, the hearth");
        Check(ML::ParsePreset(ML::PresetId(P::kStudio)) == P::kStudio && ML::PresetId(P::kCount) == "hearth",
              "lighting: ids round-trip");

        const auto studio = ML::PresetLook(P::kStudio);
        Check(studio.key.frame == ML::Frame::kCamera && studio.fill.frame == ML::Frame::kCamera &&
                  std::fabs(studio.key.intensity - 0.85f) < 1e-6f && std::fabs(studio.key.yawDeg - 20.0f) < 1e-6f,
              "lighting: the studio follows the camera");
        bool fixedToFigure = true;
        bool keyRight      = studio.key.yawDeg > 0.0f && studio.fill.yawDeg < 0.0f;
        for (const P p : { P::kDaylight, P::kHearth, P::kTorchlight, P::kMoonlight }) {
            const auto l = ML::PresetLook(p);
            fixedToFigure = fixedToFigure && l.key.frame == ML::Frame::kFigure && l.fill.frame == ML::Frame::kFigure &&
                            l.rim.frame == ML::Frame::kCamera;
            keyRight = keyRight && l.key.yawDeg > 0.0f && l.fill.yawDeg < 0.0f;
        }
        Check(fixedToFigure, "lighting: the dramatic presets fix key and fill to the figure, the rim to the viewer");
        Check(keyRight, "lighting: every key starts on the picture's right, its fill on the left");

        const auto noon = ML::WeatherTimeWeights(12.0f);
        const auto dusk = ML::WeatherTimeWeights(19.0f);
        const auto late = ML::WeatherTimeWeights(23.5f);
        Check(noon[1] == 1.0f && late[3] == 1.0f && std::fabs(dusk[2] - 0.5f) < 1e-5f &&
                  std::fabs(dusk[3] - 0.5f) < 1e-5f,
              "here: the weather's colour times by the hour");
        float sum = 0;
        for (const float w : ML::WeatherTimeWeights(5.3f)) sum += w;
        Check(std::fabs(sum - 1.0f) < 1e-5f && ML::WeatherTimeWeights(-1.0f)[3] == 1.0f,
              "here: the weights sum to one, any hour");

        float yaw = 0, up = 0;
        ML::SkyLightDirection(6.5f, yaw, up);
        const bool east = yaw > 45.0f && yaw < 135.0f;
        ML::SkyLightDirection(12.0f, yaw, up);
        const float noonUp = up;
        ML::SkyLightDirection(17.5f, yaw, up);
        Check(east && yaw < -45.0f && yaw > -135.0f && noonUp > 1.0f && up >= 0.12f,
              "here: the sun rises east, stands high at noon, sets west, never on the horizon");

        ML::GameLight dark;
        dark.valid       = true;
        dark.directional = { 0.02f, 0.02f, 0.03f };
        dark.ambientUp   = { 0.01f, 0.01f, 0.01f };
        dark.ambientDown = { 0.00f, 0.00f, 0.00f };
        const auto here = ML::HereLook(dark);
        Check(here.key.frame == ML::Frame::kWorld && here.key.intensity >= 0.35f * 0.9f - 1e-5f &&
                  ML::Luma(here.ambientSky) > 0.2f && ML::Luma(here.ambientGround) > 0.0f,
              "here: a dim cell is lit enough to read, never black");
        ML::GameLight bright = dark;
        bright.directional   = { 1.0f, 0.5f, 0.2f };
        Check(ML::HereLook(bright).key.intensity <= 0.9f + 1e-5f && ML::HereLook(bright).key.color.r > ML::HereLook(bright).key.color.b,
              "here: the game's hue, never past the studio's key");
        Check(ML::HereLook(ML::GameLight{}).key.frame == ML::Frame::kCamera, "here: with nothing read, the studio");
        ML::GameLight blue = dark;
        blue.directional   = { 0.04f, 0.08f, 0.35f };
        blue.ambientUp     = { 0.0f, 0.0f, 0.30f };
        const auto deep    = ML::HereLook(blue);
        Check(deep.key.color.b <= 1.0f + 1e-5f && deep.ambientSky.b <= 1.0f + 1e-5f && deep.fill.color.b <= 1.0f + 1e-5f,
              "here: a saturated light never pushes a channel past one");

        Check(ML::FloorDraws(true, 10.0f, 0.0f) && !ML::FloorDraws(true, 1.0f, 0.0f) && !ML::FloorDraws(false, 50.0f, 0.0f),
              "floor: drawn with the row on, never from below");
        Check(ML::FloorFade(0.0f) == 1.0f && ML::FloorFade(0.3f) == 1.0f && ML::FloorFade(1.0f) == 0.0f &&
                  ML::FloorFade(0.7f) > 0.0f && ML::FloorFade(0.7f) < 1.0f && ML::FloorFade(std::nanf("")) == 0.0f,
              "floor: full at the feet, gone at the edge");
    }
}
