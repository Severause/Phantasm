// Phantasm - native self-tests: RendererFxCore (the live views' particles, billboards and effects clock).
//
// Author: Severause

#include "MirrorCameraCore.h"
#include "RendererFxCore.h"
#include "TestHarness.h"

#include <cmath>

namespace Phantasm::Tests
{
    namespace
    {
        namespace FX = Phantasm::RendererFxCore;
        namespace MC = Phantasm::MirrorCameraCore;

        bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }
        bool Near(FX::V3 a, FX::V3 b) { return Near(a.x, b.x) && Near(a.y, b.y) && Near(a.z, b.z); }
    }

    void RunRendererFxCoreTests()
    {
        // The torch's 1x8 strip and the hearth's 4x2 atlas, as the meshes store them.
        const float strip[8] = { 0.0f, 1.0f, 0.0f, 0.125f, 0.0f, 1.0f, 0.125f, 0.125f };
        const auto  s1       = FX::SubRect(strip, 2, 1);
        Check(Near(s1.u0, 0.0f) && Near(s1.u1, 1.0f) && Near(s1.v0, 0.125f) && Near(s1.v1, 0.25f),
              "fx: a strip frame spans the width and one eighth of the height");
        const float atlas[8] = { 0.0f, 0.25f, 0.0f, 0.5f, 0.75f, 0.25f, 0.5f, 0.5f };
        const auto  a1       = FX::SubRect(atlas, 2, 1);
        Check(Near(a1.u0, 0.75f) && Near(a1.u1, 1.0f) && Near(a1.v0, 0.5f) && Near(a1.v1, 1.0f),
              "fx: an atlas frame is its own cell");
        const auto whole = FX::SubRect(atlas, 2, 5);
        Check(whole.u0 == 0.0f && whole.u1 == 1.0f && whole.v0 == 0.0f && whole.v1 == 1.0f &&
                  FX::SubRect(nullptr, 0, 0).u1 == 1.0f,
              "fx: no frame or one past the frames is the whole texture");

        Check(!FX::IsAdditive(0x12EC), "fx: alpha-test only is not blended");
        Check(FX::IsAdditive(0x100D), "fx: SrcAlpha onto One is additive");
        Check(!FX::IsAdditive(0x00ED), "fx: SrcAlpha onto InvSrcAlpha is alpha, not additive");
        Check(!FX::IsAdditive(0x12ED), "fx: blended and tested onto InvSrcAlpha (a Restoration staff's body) is alpha");

        // A flame turning about its up axis faces a camera off to the side and stays upright.
        FX::V3 x, y, z;
        const bool upright = FX::BillboardAxes(1, { 0, 0, 0 }, { 0, 0, 1 }, { 100, 0, 50 }, { 0, 0, 1 }, x, y, z);
        Check(upright && Near(z, { 0, 0, 1 }) && Near(y, { -1, 0, 0 }) && Near(FX::Dot(x, y), 0.0f) &&
                  Near(FX::Dot(FX::Cross(x, y), z), 1.0f),
              "fx: an upright billboard turns about its up axis to face the camera");
        // Fully facing: y points straight away from the camera, even from above.
        const bool full = FX::BillboardAxes(3, { 0, 0, 0 }, { 0, 0, 1 }, { 0, -100, 100 }, { 0, 0, 1 }, x, y, z);
        const float s   = 1.0f / std::sqrt(2.0f);
        Check(full && Near(y, { 0, s, -s }) && Near(FX::Dot(z, y), 0.0f) && z.z > 0.0f,
              "fx: a camera-facing billboard looks straight at the camera");
        // Gamebryo's rigid facing: z at the camera, y the camera's up, so content along y rises up the screen.
        const bool rigid = FX::BillboardAxes(2, { 0, 0, 0 }, { 0, 0, 1 }, { 0, -100, 0 }, { 0, 0, 1 }, x, y, z);
        Check(rigid && Near(z, { 0, -1, 0 }) && Near(y, { 0, 0, 1 }) && Near(FX::Dot(FX::Cross(x, y), z), 1.0f),
              "fx: a rigid camera-facing billboard keeps the camera's up as its y");
        Check(!FX::BillboardAxes(1, { 0, 0, 0 }, { 0, 0, 1 }, { 0, 0, 80 }, { 0, 1, 0 }, x, y, z),
              "fx: a camera straight above an upright billboard keeps its own axes");
        // A node lying along world Y (a torch on its side) turns about its own Z, not about world up.
        const bool lying = FX::BillboardAxes(5, { 0, 0, 0 }, { 0, 1, 0 }, { 100, 50, 0 }, { 0, 0, 1 }, x, y, z);
        Check(lying && Near(z, { 0, 1, 0 }) && Near(y, { -1, 0, 0 }) && Near(FX::Dot(FX::Cross(x, y), z), 1.0f),
              "fx: a rotate-about-up billboard keeps its node's own up");

        Check(Near(FX::ParticleTurnAxis(5, { 0, 1, 0 }), { 0, 1, 0 }) &&
                  Near(FX::ParticleTurnAxis(1, { 0, 1, 0 }), { 0, 0, 1 }),
              "fx: particle frames turn mode 5 about the node's own up and keep world up for mode 1");

        // The live views draw unmirrored: the camera's right is forward x up (MirrorCameraCore::OrbitBasis). Every
        // mode puts the content's x on the picture's right, so a card reads as in game.
        for (const float yaw : { 0.0f, 37.0f, 140.0f, -95.0f }) {
            const auto     b = MC::OrbitBasis(yaw, 0.0f);
            const FX::V3   cam{ -b.forward.x * 200.0f, -b.forward.y * 200.0f, -b.forward.z * 200.0f };
            const FX::V3   right{ b.right.x, b.right.y, b.right.z };
            const FX::V3   camUp{ b.up.x, b.up.y, b.up.z };
            bool           all = true;
            for (const int mode : { 0, 1, 2, 3, 4, 5 }) {
                all = all && FX::BillboardAxes(mode, { 0, 0, 0 }, { 0, 0, 1 }, cam, camUp, x, y, z) && Near(x, right);
            }
            Check(all, "fx: every billboard mode puts its x on the unmirrored picture's right");
        }

        // A billboard node's frame from its world (DirectXMath's row-vector layout).
        const float world[16] = { 0, 2, 0, 0, -2, 0, 0, 0, 0, 0, 2, 0, 10, 20, 30, 1 };
        FX::BillboardFrame f;
        Check(FX::BillboardFrameOf(world, f) && Near(f.pos, { 10, 20, 30 }) && Near(f.up, { 0, 0, 1 }) && Near(f.scale, 2.0f),
              "fx: a billboard frame is the node's position, own unit Z and scale");
        float flat[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        Check(!FX::BillboardFrameOf(flat, f), "fx: a degenerate billboard world has no frame");
        float nanWorld[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        nanWorld[13]       = std::nanf("");
        Check(!FX::BillboardFrameOf(nanWorld, f) && !FX::BillboardFrameOf(nullptr, f),
              "fx: a non-finite world has no frame");

        // Particle corners: the quad's two triangles, turned, with their UVs.
        float r = 0, u = 0, uu = 0, vv = 0;
        FX::ParticleCorner(0, 2.0f, 0.0f, { 0.0f, 0.5f, 1.0f, 1.0f }, r, u, uu, vv);
        Check(Near(r, -2.0f) && Near(u, 2.0f) && Near(uu, 0.0f) && Near(vv, 0.5f), "fx: corner 0 is the top left");
        FX::ParticleCorner(5, 2.0f, 0.0f, { 0.0f, 0.5f, 1.0f, 1.0f }, r, u, uu, vv);
        Check(Near(r, 2.0f) && Near(u, -2.0f) && Near(uu, 1.0f) && Near(vv, 1.0f), "fx: corner 5 is the bottom right");
        FX::ParticleCorner(1, 1.0f, 3.14159265f * 0.5f, {}, r, u, uu, vv);
        Check(Near(r, -1.0f) && Near(u, 1.0f), "fx: a quarter turn moves the top right corner to the top left");

        // Effect piece lanes: the palette colour replaces the base colour, which keeps only its red as the row.
        const auto plain = FX::EffectPieceLookOf(1.0f, 0.5f, 0.25f, 0.8f, 2.0f, 0.5f, false);
        Check(Near(plain.r, 0.9f) && Near(plain.g, 0.45f) && Near(plain.b, 0.225f), "fx look: base colour x scale x the dim");
        Check(Near(plain.alpha, 0.4f) && Near(plain.paletteRow, 1.0f), "fx look: base alpha x property alpha; the row is base red");
        const auto pal = FX::EffectPieceLookOf(0.3f, 0.9f, 0.1f, 1.0f, 2.0f, 1.0f, true);
        Check(Near(pal.r, 0.9f) && Near(pal.g, 0.9f) && Near(pal.b, 0.9f) && Near(pal.paletteRow, 0.3f),
              "fx look: a palette colour is the scale alone, read at the base red");
        const auto hot = FX::EffectPieceLookOf(2.0f, 1.0f, 1.0f, 1.0f, 9.0f, 1.0f, false);
        Check(Near(hot.r, 2.0f) && Near(hot.g, FX::kEffectScaleCap * FX::kEffectDim) && Near(hot.paletteRow, 1.0f),
              "fx look: the scale and each channel are capped, the row clamped to the palette");
        const float qnan = std::nanf("");
        const auto  bad  = FX::EffectPieceLookOf(qnan, 1.0f, 1.0f, 1.0f, qnan, 1.0f, false);
        Check(bad.r == 0.0f && bad.g == 0.0f && bad.paletteRow == 0.0f, "fx look: NaN colour and scale read as 0");

        // The item view's clock: which property controllers it runs. Flags as LoreRim's meshes carry them: every
        // controller of the bound bow (boundbow.nif) is 0x6C, driven by its NiControllerManager; Wisp Wrappings'
        // scrolling lantern box and a torch's heat haze are 0x48, free running.
        using PC = FX::PropertyController;
        Check(FX::PropertyControllerOf("BSEffectShaderPropertyFloatController") == PC::kEffect &&
                  FX::PropertyControllerOf("BSEffectShaderPropertyColorController") == PC::kEffect,
              "fx clock: effect shader controllers");
        Check(FX::PropertyControllerOf("BSLightingShaderPropertyFloatController") == PC::kLighting &&
                  FX::PropertyControllerOf("BSLightingShaderPropertyColorController") == PC::kLighting &&
                  FX::PropertyControllerOf("BSLightingShaderPropertyUShortController") == PC::kLighting,
              "fx clock: lighting shader controllers");
        Check(FX::PropertyControllerOf("BSNiAlphaPropertyTestRefController") == PC::kAlphaTestRef &&
                  FX::PropertyControllerOf("NiAlphaController") == PC::kNone &&
                  FX::PropertyControllerOf("NiTransformController") == PC::kNone &&
                  FX::PropertyControllerOf("NiPSysEmitterCtlr") == PC::kNone &&
                  FX::PropertyControllerOf("NiControllerManager") == PC::kNone && FX::PropertyControllerOf(nullptr) == PC::kNone,
              "fx clock: the alpha test controller; node, particle and manager controllers are not the clock's");
        Check(!FX::RunsOnViewClock(PC::kEffect, 0x6C) && !FX::RunsOnViewClock(PC::kLighting, 0x6C),
              "fx clock: a manager-controlled controller (the bound bow's flames) is never run");
        Check(FX::RunsOnViewClock(PC::kLighting, 0x48) && FX::RunsOnViewClock(PC::kEffect, 0x48) &&
                  FX::RunsOnViewClock(PC::kAlphaTestRef, 0x08),
              "fx clock: free-running property controllers are");
        Check(!FX::RunsOnViewClock(PC::kNone, 0x48), "fx clock: any other controller is not");

        // The actor view's paused clock.
        const float never = -3.4028235e38f;
        Check(FX::PausedFxBase(1234.5f) == 1234.5f && FX::PausedFxBase(-2.0f) == -2.0f,
              "fx paused clock: a controller the engine ran starts from its last time");
        Check(FX::PausedFxBase(never) == 0.0f && FX::PausedFxBase(qnan) == 0.0f && FX::PausedFxBase(INFINITY) == 0.0f,
              "fx paused clock: one never run, or with no time, starts from 0");
        Check(FX::PausedFxStep(0.016f) == 0.016f && FX::PausedFxStep(3.0f) == FX::kPausedFxMaxStep,
              "fx paused clock: a step is the real time, capped");
        Check(FX::PausedFxStep(-1.0f) == 0.0f && FX::PausedFxStep(qnan) == 0.0f,
              "fx paused clock: a backward or NaN step adds nothing");

        // The read-back's alpha: a piece the permissive tier forced to full alpha (material alpha 0 in the file)
        // keeps it whatever moves it; any other piece takes its material's.
        Check(FX::ReadBackAlpha(true, 1.0f, 0.0f) == 1.0f,
              "fx read-back: a forced piece with a UV scroll does not vanish");
        Check(FX::ReadBackAlpha(false, 1.0f, 0.25f) == 0.25f && FX::ReadBackAlpha(false, 0.0f, 0.5f) == 0.5f,
              "fx read-back: an unforced piece follows its material's alpha both ways");
    }
}
