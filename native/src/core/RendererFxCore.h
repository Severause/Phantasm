#pragma once

// The live views' effects (fire, glows, embers): the rules the renderer's particle and billboard passes follow, kept
// engine-free. Tested by RendererFxCoreTest.cpp.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace Phantasm::RendererFxCore
{
    struct V3
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
    };
    inline V3    Sub(V3 a, V3 b) noexcept { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
    inline V3    Scale(V3 a, float s) noexcept { return { a.x * s, a.y * s, a.z * s }; }
    inline float Dot(V3 a, V3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
    inline V3    Cross(V3 a, V3 b) noexcept { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
    inline bool  Normalize(V3& a) noexcept
    {
        const float l = std::sqrt(Dot(a, a));
        if (!(l > 1.0e-6f)) return false;
        a = Scale(a, 1.0f / l);
        return true;
    }

    /// A rectangle of a texture: u0 v0 its top left, u1 v1 its bottom right.
    struct UvRect
    {
        float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
    };

    /// A particle's flipbook frame. NiParticlesData's subtextureOffsets hold one NiColorA per frame laid out as
    /// (u offset, u size, v offset, v size): vanilla torch fire is a 1x8 strip (0, 1, k/8, 1/8), the hearth's flames
    /// 4x2 and 4x4 atlases. offsets is count x 4 floats; no frames or an index past them is the whole texture.
    inline UvRect SubRect(const float* offsets, std::uint32_t count, std::uint32_t index) noexcept
    {
        if (!offsets || count == 0 || index >= count) return {};
        const float* o = offsets + std::size_t(index) * 4;
        if (!(o[1] > 0.0f) || !(o[3] > 0.0f)) return {};
        return { o[0], o[2], o[0] + o[1], o[2] + o[3] };
    }

    /// True when a NiAlphaProperty's flags blend additively (anything onto One: fire, glows): bit 0 blending on,
    /// bits 5-8 the destination function (0 One, 7 InvSrcAlpha).
    inline bool IsAdditive(std::uint16_t alphaFlags) noexcept
    {
        return (alphaFlags & 0x1) && ((alphaFlags >> 5) & 0xF) == 0;
    }

    /// What a property controller drives, from its class (NiRTTI) name: an effect shader's values (the
    /// BSEffectShaderProperty Float / Color controllers), a lighting shader's (BSLightingShaderProperty Float / Color
    /// / UShort: UV offset and scale, emissive, alpha), or a NiAlphaProperty's test threshold. Skyrim has no
    /// NiMaterialProperty, so Gamebryo's NiAlphaController never occurs.
    enum class PropertyController
    {
        kNone,
        kEffect,
        kLighting,
        kAlphaTestRef,
    };

    inline PropertyController PropertyControllerOf(const char* rttiName) noexcept
    {
        if (!rttiName) return PropertyController::kNone;
        const auto startsWith = [rttiName](const char* prefix) {
            return std::strncmp(rttiName, prefix, std::strlen(prefix)) == 0;
        };
        if (startsWith("BSEffectShaderProperty")) return PropertyController::kEffect;
        if (startsWith("BSLightingShaderProperty")) return PropertyController::kLighting;
        if (std::strcmp(rttiName, "BSNiAlphaPropertyTestRefController") == 0) return PropertyController::kAlphaTestRef;
        return PropertyController::kNone;
    }

    /// The material alpha a piece the clock moves draws with after a tick: what its material holds now, unless the
    /// collect forced the piece to full alpha (`forced`: the item view's permissive tier draws a piece the engine
    /// leaves invisible). Which lane a controller drives (its controlled variable) is not read, its runtime offset
    /// being unverified, so a forced piece keeps `drawn` whatever moves it: a UV scroll or an emissive pulse must not
    /// make it vanish, at the cost of holding a controller that animates its alpha.
    inline float ReadBackAlpha(bool forced, float drawn, float material) noexcept
    {
        return forced ? drawn : material;
    }

    /// NiTimeController::Flag::kManagerControlled: the model's NiControllerManager drives the controller through a
    /// blend interpolator, so only a playing sequence moves it (MannequinRenderer static_asserts the value).
    inline constexpr std::uint16_t kManagerControlledFlag = 0x20;

    /// Whether the item view's clock runs a controller and reads its property back: a property controller that runs
    /// on its own. A manager-controlled one is left alone: no sequence plays in the view, so it moves nothing, and
    /// reading its property back each frame would only undo the collect's look (the bound bow's flames, at material
    /// alpha 0 in the file, lost the full alpha the permissive tier draws them with).
    inline bool RunsOnViewClock(PropertyController kind, std::uint16_t controllerFlags) noexcept
    {
        return kind != PropertyController::kNone && !(controllerFlags & kManagerControlledFlag);
    }

    /// The actor view's effect clock while the game is paused (the engine then advances no controller): the seconds
    /// it has run since the pause are added to each controller's last update (ControllerKeysCore::ScaledTime). The
    /// app time of that update is the controller's lastTime, or 0 for one the engine has not run yet (lastTime
    /// -FLT_MAX after Start, e.g. a piece equipped under the pause).
    inline constexpr float kPausedFxMaxStep  = 0.5f;           // a hitch or a long frame advances the clock this much at most
    inline constexpr float kPausedFxInterval = 1.0f / 60.0f;   // the clock steps at most this often
    inline float PausedFxBase(float lastTime) noexcept
    {
        return std::isfinite(lastTime) && std::fabs(lastTime) < 1.0e30f ? lastTime : 0.0f;
    }
    /// The seconds one step adds to the paused clock: the real time since the last step, never negative and at most
    /// kPausedFxMaxStep. NaN adds nothing.
    inline float PausedFxStep(float realSeconds) noexcept
    {
        return realSeconds > 0.0f ? (std::min)(realSeconds, kPausedFxMaxStep) : 0.0f;
    }

    /// The effect-shader material (BSEffectShaderMaterial) of a piece, as its lanes. With its greyscale palette for
    /// colour the game replaces the colour with baseColorScale x the palette's texel at (texture green, baseColor red
    /// x vertex red), so `r` `g` `b` are the scale alone and `paletteRow` the base red (the PS multiplies the vertex red
    /// in); otherwise the sampled colour takes baseColor x the scale (`r` `g` `b`, at most 2 a channel). Both are dimmed
    /// by kEffectDim. `alpha` is baseColor alpha x the property's, the alpha the palette's alpha row is read at (with
    /// the vertex alpha) or that multiplies the texture's. NaN scale or colour reads as 0.
    // Without the game's soft lighting and falloff, full base colour reads as flat neon.
    inline constexpr float kEffectDim      = 0.45f;
    inline constexpr float kEffectScaleCap = 4.0f;

    struct EffectPieceLook
    {
        float r = 0.0f, g = 0.0f, b = 0.0f;
        float paletteRow = 0.0f;
        float alpha      = 1.0f;
    };

    inline EffectPieceLook EffectPieceLookOf(float baseR, float baseG, float baseB, float baseA, float baseColorScale,
                                             float propertyAlpha, bool paletteColour) noexcept
    {
        const auto finite = [](float v) { return std::isnan(v) ? 0.0f : v; };
        const float s = std::clamp(finite(baseColorScale), 0.0f, kEffectScaleCap) * kEffectDim;
        EffectPieceLook l;
        if (paletteColour) {
            l.r = l.g = l.b = s;
        } else {
            l.r = std::clamp(finite(baseR) * s, 0.0f, 2.0f);
            l.g = std::clamp(finite(baseG) * s, 0.0f, 2.0f);
            l.b = std::clamp(finite(baseB) * s, 0.0f, 2.0f);
        }
        l.paletteRow = std::clamp(finite(baseR), 0.0f, 1.0f);
        l.alpha      = baseA * propertyAlpha;
        return l;
    }

    /// The axes a NiBillboardNode turns to (NiBillboardNode::FaceMode), as unit right-handed world axes x y z.
    /// 1 and 5 turn only about nodeUp (cards that stay upright, a torch's fire); 3 and 4 face the camera fully. Those
    /// four keep the content's Z up with Y pointing away from the camera. 0 and 2 face the camera Gamebryo's way:
    /// Y is the camera's up and Z points at the camera (an Embers XD torch's fire rises up the screen through it).
    /// False when the camera is on the node (keep its own axes).
    inline bool BillboardAxes(int mode, V3 nodePos, V3 nodeUp, V3 cameraPos, V3 cameraUp, V3& x, V3& y, V3& z) noexcept
    {
        V3 toCam = Sub(cameraPos, nodePos);
        if (mode == 0 || mode == 2) {
            if (!Normalize(toCam)) return false;
            V3 up = Sub(cameraUp, Scale(toCam, Dot(cameraUp, toCam)));
            if (!Normalize(up)) return false;
            z = toCam;
            y = up;
            x = Cross(y, z);
            return true;
        }
        if (mode == 1 || mode == 5) {
            V3 up = nodeUp;
            if (!Normalize(up)) return false;
            toCam = Sub(toCam, Scale(up, Dot(toCam, up)));   // flattened onto the plane the node turns in
            if (!Normalize(toCam)) return false;
            z = up;
        } else {
            if (!Normalize(toCam)) return false;
            V3 up = Sub(cameraUp, Scale(toCam, Dot(cameraUp, toCam)));
            if (!Normalize(up)) return false;
            z = up;
        }
        y = Scale(toCam, -1.0f);
        x = Cross(y, z);
        return true;
    }

    /// The axis a billboard turns about in modes 1 and 5, for the item view's particle frames. nif.xml has 5
    /// (BSRotateAboutUp) turning about the node's own Z (nodeUp); 1 keeps world up there until the game shows
    /// otherwise. Effect pieces pass the node's own Z for both (MannequinRenderer's TurnItemBillboard and
    /// TurnActorBillboard), which for 1 is assumed, not verified.
    inline V3 ParticleTurnAxis(int mode, V3 nodeUp) noexcept
    {
        return mode == 5 ? nodeUp : V3{ 0.0f, 0.0f, 1.0f };
    }

    /// A billboard node's frame read from its world: position, own Z axis (unit) and scale. m is the world as a
    /// row-vector 4x4 in row-major order (DirectXMath's layout: rows 0-2 the node's x y z axes times its scale, row 3
    /// its position). False when the matrix is not finite or its axes are degenerate.
    struct BillboardFrame
    {
        V3    pos;
        V3    up{ 0.0f, 0.0f, 1.0f };
        float scale = 1.0f;
    };

    inline bool BillboardFrameOf(const float* m, BillboardFrame& out) noexcept
    {
        if (!m) return false;
        for (int i = 0; i < 16; ++i) {
            if (!std::isfinite(m[i])) return false;
        }
        const V3    x{ m[0], m[1], m[2] };
        V3          z{ m[8], m[9], m[10] };
        const float scale = std::sqrt(Dot(x, x));
        if (!(scale > 1.0e-6f) || !Normalize(z)) return false;
        out.pos   = { m[12], m[13], m[14] };
        out.up    = z;
        out.scale = scale;
        return true;
    }

    /// The corners of a camera-facing particle quad, in the order its two triangles draw them (0 1 2, 2 1 3 of the
    /// four corners): offsets from its centre along the camera's right and up for a half size h turned by angle
    /// (radians, anticlockwise as seen), and their UVs within rect (v down). c is 0-5.
    inline void ParticleCorner(int c, float h, float angle, const UvRect& rect, float& right, float& up, float& u,
                               float& v) noexcept
    {
        static constexpr int   kOrder[6] = { 0, 1, 2, 2, 1, 3 };
        static constexpr float kX[4]     = { -1.0f, 1.0f, -1.0f, 1.0f };
        static constexpr float kY[4]     = { 1.0f, 1.0f, -1.0f, -1.0f };
        const int   k  = kOrder[std::clamp(c, 0, 5)];
        const float cs = std::cos(angle), sn = std::sin(angle);
        right = (kX[k] * cs - kY[k] * sn) * h;
        up    = (kX[k] * sn + kY[k] * cs) * h;
        u     = kX[k] < 0.0f ? rect.u0 : rect.u1;
        v     = kY[k] > 0.0f ? rect.v0 : rect.v1;
    }

    // The effects clock: the view runs its particle simulations and the game's effect-property controllers
    // (scrolling flame textures, fading glows) on a clock of its own, kWarmup seconds ahead in kWarmupStep steps when
    // the view opens so a fire is already burning, then in steps of at most kMaxStep so a stalled frame does not burst
    // a fire.
    inline constexpr float kWarmup  = 1.5f;
    inline constexpr float kMaxStep = 1.0f / 15.0f;
    inline constexpr float kWarmupStep = 1.0f / 30.0f;

    /// The most particles one frame draws, every system together (the vertex buffer's room: six vertices each).
    inline constexpr std::uint32_t kMaxParticles = 4096;
}
