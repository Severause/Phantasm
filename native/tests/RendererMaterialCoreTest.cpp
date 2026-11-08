// Phantasm - native self-tests: RendererMaterialCore (the live views' per-piece material rules).
//
// Author: Severause

#include "RendererMaterialCore.h"
#include "TestHarness.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <string_view>
#include <vector>

namespace Phantasm::Tests
{
    namespace
    {
        namespace RMC = Phantasm::RendererMaterialCore;

        bool Near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }
    }

    void RunRendererMaterialCoreTests()
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();

        // 1. Alpha test: a strand (no threshold) and a cutout (NiAlphaProperty threshold) never overlap
        {
            Check(RMC::IsHairStrand(true, 0.0f) && !RMC::IsCutout(true, 0.0f), "alpha test: a threshold-less test is a strand");
            Check(RMC::IsCutout(true, 0.5f) && !RMC::IsHairStrand(true, 0.5f), "alpha test: a thresholded test is a cutout");
            Check(!RMC::IsHairStrand(false, 0.0f) && !RMC::IsCutout(false, 0.5f), "alpha test: no test is neither");
        }

        // 1b. Face culling: back faces go unless the material is two-sided or the piece is a hair strand
        {
            Check(RMC::CullsBackFaces(false, RMC::IsHairStrand(false, 0.0f)) &&
                      RMC::CullsBackFaces(false, RMC::IsHairStrand(true, 0.5f)),
                  "culling: a single-sided piece, a cutout included, culls its back faces");
            Check(!RMC::CullsBackFaces(true, false) && !RMC::CullsBackFaces(true, RMC::IsHairStrand(true, 0.5f)),
                  "culling: a two-sided material draws both faces");
            Check(!RMC::CullsBackFaces(false, RMC::IsHairStrand(true, 0.0f)),
                  "culling: a hair strand draws both faces whatever its flag says");
        }

        // 2. The colorMode.z lane: 0 / 1 / -threshold
        {
            Check(RMC::AlphaTestCode(false, 0.5f) == 0.0f, "lane: no test sends 0");
            Check(RMC::AlphaTestCode(true, 0.0f) == 1.0f, "lane: a blended strand sends 1 (its alpha is its coverage)");
            Check(RMC::AlphaTestCode(true, 0.0f, 0.5f) == 1.5f, "lane: a tested strand sends 1 + its threshold");
            Check(RMC::AlphaTestCode(true, 0.0f, 0.001f) > 1.001f, "lane: the smallest strand threshold still reads as tested");
            Check(RMC::AlphaTestCode(true, 0.0f, 0.5f, true) == 3.5f && RMC::AlphaTestCode(true, 0.0f, 0.99f, false) < 2.0f &&
                      RMC::AlphaTestCode(true, 0.0f, 0.001f, true) > 3.0f,
                  "lane: a tested and blended strand sends 3 + its threshold, apart from a tested one and a core pass");
            Check(RMC::AlphaTestCode(true, 0.0f, 0.0f, true) == 1.0f, "lane: a blended strand with no test stays 1");
            Check(RMC::StrandWritesDepth(0.5f, true) && !RMC::StrandWritesDepth(0.5f, false) && !RMC::StrandWritesDepth(0.0f, true),
                  "lane: a strand writes depth only when it is tested and its shader writes depth");
            Check(RMC::BlendWritesDepth(true, true) && !RMC::BlendWritesDepth(true, false) &&
                      !RMC::BlendWritesDepth(false, true),
                  "lane: a blend piece writes depth only when it also tests and its shader writes depth");
            Check(RMC::ShadowClipRef(true, 0.4f, 0.0f, false) == 0.4f && RMC::ShadowClipRef(true, 0.0f, 0.3f, true) == 0.3f &&
                      RMC::ShadowClipRef(true, 0.0f, 0.3f, false) == 0.0f && RMC::ShadowClipRef(false, 0.0f, 0.0f, false) == 0.0f,
                  "shadow: a cutout clips at its threshold, a depth-writing strand at its own, anything else not at all");
            Check(RMC::StrandCastsShadow(true, 0.0f, true) && !RMC::StrandCastsShadow(true, 0.0f, false) &&
                      !RMC::StrandCastsShadow(true, 0.5f, true),
                  "shadow: only a strand that writes depth casts one");
            Check(RMC::StrandSplitsCore(0.16f, true, true) && !RMC::StrandSplitsCore(0.16f, false, true) &&
                      !RMC::StrandSplitsCore(0.16f, true, false) && !RMC::StrandSplitsCore(0.0f, true, true),
                  "split: only a tested, blended, depth-writing strand draws a core pass");
            Check(!RMC::StrandSplitsCore(0.5f, true, true) && !RMC::StrandSplitsCore(0.78f, true, true),
                  "split: a strand tested at the core threshold or above keeps the game's one pass");
            Check(RMC::kStrandCoreCode > 2.0f && RMC::kStrandCoreCode < 3.0f && RMC::kStrandCoreCode - 2.0f == RMC::kStrandCoreRef,
                  "split: the core lane is 2 + the core threshold, between the tested and the blended lanes");
            Check(RMC::ShadowBias(false) == RMC::kShadowBias && RMC::ShadowBias(true) == RMC::kStrandShadowBias &&
                      RMC::kStrandShadowBias * 750.0f > 5.0f && RMC::kShadowBias * 750.0f < 1.5f,
                  "shadow bias: a strand takes about 6u over the mirror's 750u light, everything else about 1.1u");
            float hi = 0.0f, hp = 0.0f;
            RMC::HairSpecular(false, 1.0f, 50.0f, 1.0f, hi, hp);
            Check(hi == 0.0f && hp == 50.0f, "hair spec: no Specular flag, no highlight");
            RMC::HairSpecular(true, 1.0f, 2.0f, 0.5f, hi, hp);
            Check(std::fabs(hi - 0.11f) < 1e-5f && hp == 32.0f, "hair spec: strength from scale x colour, an untuned power is 32");
            RMC::HairSpecular(true, 10.0f, 500.0f, 1.0f, hi, hp);
            Check(hi == 0.6f && hp == 160.0f, "hair spec: clamped");
            Check(RMC::AlphaTestCode(true, 0.5f, 0.3f) == -0.5f, "lane: a cutout ignores a strand threshold");
            Check(RMC::AlphaTestCode(true, 0.5f) == -0.5f, "lane: a cutout sends minus its threshold");
            Check(RMC::AlphaTestCode(true, 0.01f) < -0.001f, "lane: the smallest threshold still reads as a cutout in the PS");

            // A hair-shader piece is a strand only when its NiAlphaProperty tests or blends.
            Check(!RMC::HairAlphaTests(false, false) && !RMC::IsHairStrand(RMC::HairAlphaTests(false, false), 0.0f) &&
                      RMC::AlphaTestCode(RMC::HairAlphaTests(false, false), 0.0f) == 0.0f,
                  "hair: no alpha property (a Redguard braid, an Argonian crest) draws opaque, not as a strand");
            Check(RMC::HairAlphaTests(true, false) && RMC::IsHairStrand(RMC::HairAlphaTests(true, false), 0.0f),
                  "hair: a tested one is a strand");
            Check(RMC::HairAlphaTests(false, true) &&
                      RMC::AlphaTestCode(RMC::HairAlphaTests(false, true), 0.0f, 0.0f, true) == 1.0f,
                  "hair: a blend-only one is a strand at lane 1");
            Check(RMC::HairAlphaTests(true, true), "hair: a tested and blended one is a strand");
        }

        // 3. Emitted alpha: overlay and blend are told apart, and neither is the opaque default
        {
            Check(RMC::EmitAlphaMode(false, false) == RMC::kEmitNone, "emit: an opaque piece emits no alpha");
            Check(RMC::EmitAlphaMode(true, false) == RMC::kEmitOverlay, "emit: an overlay is the overlay mode");
            Check(RMC::EmitAlphaMode(false, true) == RMC::kEmitBlend, "emit: a blend piece is the blend mode");
            Check(RMC::EmitAlphaMode(true, true) == RMC::kEmitOverlay, "emit: an overlay stays an overlay when also blended");
            Check(RMC::kEmitOverlay > 0.5f && RMC::kEmitBlend > 1.5f && RMC::kEmitOverlay < 1.5f,
                  "emit: the PS gates (> 0.5 emits, > 1.5 is a blend piece) separate the modes");
        }

        // 4. Material alpha: an engine-invisible piece is recognised, and the multiplier has no sentinel
        {
            Check(RMC::IsEngineInvisible(0.0f) && RMC::IsEngineInvisible(0.01f), "alpha: 0 and 0.01 are engine-invisible");
            Check(!RMC::IsEngineInvisible(0.02f) && !RMC::IsEngineInvisible(1.0f), "alpha: 0.02 and 1 draw");
            Check(RMC::IsEngineInvisible(nan), "alpha: a NaN material alpha is dropped");
            Check(RMC::EmitAlphaMultiplier(0.0f) == 0.0f, "multiplier: 0 stays 0 (no x1 sentinel)");
            Check(Near(RMC::EmitAlphaMultiplier(0.3f), 0.3f, 1e-6f), "multiplier: a 30% overlay emits 30%");
            Check(RMC::EmitAlphaMultiplier(1.7f) == 1.0f && RMC::EmitAlphaMultiplier(-0.2f) == 0.0f, "multiplier: clamped to [0, 1]");
            Check(RMC::EmitAlphaMultiplier(nan) == 1.0f, "multiplier: NaN is the neutral 1");
        }

        // 4b. Emissive: the route by shader, and the lighting piece's cap
        {
            Check(RMC::EmissiveRoute(false, false) == RMC::kEmissiveLit, "emissive: a lighting piece adds it to its light");
            Check(RMC::EmissiveRoute(false, true) == RMC::kEmissivePBR, "emissive: a TruePBR piece adds it after the lighting");
            Check(RMC::EmissiveRoute(true, false) == RMC::kEmissiveEffect && RMC::EmissiveRoute(true, true) == RMC::kEmissiveEffect,
                  "emissive: an effect piece is coloured by it, PBR or not");
            Check(RMC::kEmissivePBR > 0.5f && RMC::kEmissivePBR < 1.5f && RMC::kEmissiveEffect > 1.5f && RMC::kEmissiveLit < 0.5f,
                  "emissive: the PS gates (> 1.5 effect, > 0.5 PBR, else lit) separate the routes");
            Check(Near(RMC::EmissiveChannel(1.3f), 1.3f, 1e-6f) && RMC::EmissiveChannel(3.0f) == RMC::kEmissiveCap,
                  "emissive: a vampire eye's 1.3 passes, a Spriggan's 3 is capped");
            Check(RMC::EmissiveChannel(-1.0f) == 0.0f && RMC::EmissiveChannel(nan) == 0.0f, "emissive: negative and NaN are 0");
        }

        // 4c. Highlight tint: which highlights take the material's specularColor
        {
            Check(RMC::SpecTintCode(false, false, false) == RMC::kSpecTintNone &&
                      RMC::SpecTintCode(false, true, true) == RMC::kSpecTintNone,
                  "spec tint: a white specularColor tints nothing");
            Check(RMC::SpecTintCode(true, false, false) == RMC::kSpecTintFull, "spec tint: a lighting piece (male skin) takes it whole");
            Check(RMC::SpecTintCode(true, false, true) == RMC::kSpecTintHue, "spec tint: a hair piece takes its hue only");
            Check(RMC::SpecTintCode(true, true, false) == RMC::kSpecTintMetal && RMC::SpecTintCode(true, true, true) == RMC::kSpecTintMetal,
                  "spec tint: a TruePBR piece tints only its metal reflection");
            Check(RMC::kSpecTintFull > 0.5f && RMC::kSpecTintFull < 1.5f && RMC::kSpecTintHue > 1.5f && RMC::kSpecTintHue < 2.5f &&
                      RMC::kSpecTintMetal > 2.5f,
                  "spec tint: the PS gates (> 0.5 metal, < 1.5 full, < 2.5 hue) separate the codes");
        }

        // 5. Texture clamp mode: the four engine modes map onto their samplers, anything else is wrap/wrap
        {
            Check(RMC::SamplerIndex(0) == 0 && !RMC::SamplerWrapsU(0) && !RMC::SamplerWrapsV(0), "clamp: mode 0 clamps both");
            Check(RMC::SamplerIndex(1) == 1 && !RMC::SamplerWrapsU(1) && RMC::SamplerWrapsV(1), "clamp: mode 1 clamps U, wraps V");
            Check(RMC::SamplerIndex(2) == 2 && RMC::SamplerWrapsU(2) && !RMC::SamplerWrapsV(2), "clamp: mode 2 wraps U, clamps V");
            Check(RMC::SamplerIndex(3) == 3 && RMC::SamplerWrapsU(3) && RMC::SamplerWrapsV(3), "clamp: mode 3 wraps both");
            Check(RMC::SamplerIndex(-1) == RMC::kDefaultAddressMode && RMC::SamplerIndex(4) == RMC::kDefaultAddressMode,
                  "clamp: an out-of-range mode samples as wrap/wrap");
        }

        // 6. Transparent order: farther first, and a sort with non-finite keys stays well-formed
        {
            Check(RMC::DrawsBefore(120.0f, 80.0f) && !RMC::DrawsBefore(80.0f, 120.0f), "sort: the farther piece draws first");
            Check(!RMC::DrawsBefore(50.0f, 50.0f), "sort: a tie is not ordered");
            Check(!RMC::DrawsBefore(nan, 0.0f) && !RMC::DrawsBefore(0.0f, nan), "sort: NaN sorts as 0");
            Check(RMC::DrawsBefore(inf, 10.0f) == RMC::DrawsBefore(0.0f, 10.0f), "sort: infinity sorts as 0");
            std::vector<float> depths{ 5.0f, nan, 30.0f, -2.0f, inf, 12.0f };
            std::sort(depths.begin(), depths.end(), RMC::DrawsBefore);
            Check(depths.front() == 30.0f && depths.back() == -2.0f, "sort: a mixed list sorts back to front");
        }

        // 7. Head / body skin parity
        {
            Check(Near(RMC::HeadLumaScale(0.40f, 0.50f), 0.8f, 1e-6f), "parity: body / head");
            Check(RMC::HeadLumaScale(0.9f, 0.1f) == RMC::kHeadLumaScaleMax, "parity: a dark head bake is capped");
            Check(RMC::HeadLumaScale(0.05f, 0.9f) == RMC::kHeadLumaScaleMin, "parity: a bright head bake is floored");
            Check(RMC::HeadLumaScale(-1.0f, 0.5f) == 1.0f && RMC::HeadLumaScale(0.5f, -1.0f) == 1.0f,
                  "parity: an unknown side leaves the head alone");
            Check(RMC::HeadLumaScale(nan, 0.5f) == 1.0f && RMC::HeadLumaScale(0.5f, 0.0f) == 1.0f,
                  "parity: NaN or a zero head leaves the head alone");
            Check(Near(RMC::SkinTintOverlay(0.6f, 0.5f), 0.6f, 1e-6f), "overlay: a 0.5 tint leaves the skin as painted");
            Check(Near(RMC::HeadParityLuma(0.3f, 0.5f), 0.3f, 1e-6f), "head parity: a mid-grey FaceTint leaves the head");
            Check(Near(RMC::HeadParityLuma(0.3f, 0.7f), RMC::SkinTintOverlay(0.3f, 0.7f), 1e-6f),
                  "head parity: the head under its FaceTint's overlay");
            Check(RMC::HeadParityLuma(0.3f, -1.0f) == 0.3f && RMC::HeadParityLuma(0.3f, nan) == 0.3f,
                  "head parity: no FaceTint luma leaves the bare head");
            Check(RMC::HeadParityLuma(-1.0f, 0.7f) == -1.0f, "head parity: an unknown head stays unknown");
            Check(RMC::SkinTintOverlay(0.6f, 0.7f) > 0.6f, "overlay: a lighter tint brightens");
            Check(RMC::SkinTintOverlay(0.6f, 0.3f) < 0.6f, "overlay: a darker tint darkens");
            Check(Near(RMC::SkinTintOverlay(0.6f, 0.7f), 0.36f + 2.0f * 0.7f * 0.6f * 0.4f, 1e-6f),
                  "overlay: the engine's b^2 + 2tb(1 - b)");
            Check(RMC::SkinTintOverlay(nan, 0.5f) != RMC::SkinTintOverlay(nan, 0.5f), "overlay: NaN in, NaN out (unknown)");
        }

        // 8. Bind re-parent safety, shared by the FaceGen face and rigid bone-attached pieces
        {
            Check(RMC::AcceptsBindReparent(1.0f, true, 12.0f), "reparent: a nearby, finite, invertible bone is taken");
            Check(!RMC::AcceptsBindReparent(1.0f, true, 40.0f), "reparent: 40u apart is refused");
            Check(!RMC::AcceptsBindReparent(0.0f, true, 1.0f), "reparent: a singular live bone is refused");
            Check(!RMC::AcceptsBindReparent(1.0f, false, 1.0f), "reparent: a non-finite R is refused");
            Check(!RMC::AcceptsBindReparent(1.0f, true, nan), "reparent: a NaN distance is refused");
            Check(RMC::AcceptsBindReparent(1.0f, true, 120.0f, RMC::kMaxWeaponBindReparentDistance) &&
                      !RMC::AcceptsBindReparent(1.0f, true, 200.0f, RMC::kMaxWeaponBindReparentDistance) &&
                      !RMC::AcceptsBindReparent(1.0f, true, nan, RMC::kMaxWeaponBindReparentDistance),
                  "reparent: a held weapon's hand is taken farther, not without limit");
        }

        // 8b. Hair under a helmet: head parts by type, other pieces by name
        {
            using K = RMC::HeadPartKind;
            Check(RMC::HidesUnderHelmet(K::kHair, "hairfemalenord02") && RMC::HidesUnderHelmet(K::kHair, "hairbrownbraids"),
                  "helmet: a head part of type Hair hides, even one whose name holds 'brow'");
            Check(!RMC::HidesUnderHelmet(K::kFacialHair, "goatee01") && !RMC::HidesUnderHelmet(K::kEyebrows, "x") &&
                      !RMC::HidesUnderHelmet(K::kEyes, "eyesmale") && !RMC::HidesUnderHelmet(K::kMisc, "hairline"),
                  "helmet: facial hair, brows, eyes and misc parts stay, whatever their names");
            Check(RMC::HidesUnderHelmet(K::kNotHeadPart, "wighair") && !RMC::HidesUnderHelmet(K::kNotHeadPart, "eyelashes01") &&
                      !RMC::HidesUnderHelmet(K::kNotHeadPart, "beardnord"),
                  "helmet: a piece no head part names keeps the name rule");
            Check(RMC::HeadPartKindOf(3) == K::kHair && RMC::HeadPartKindOf(6) == K::kEyebrows &&
                      RMC::HeadPartKindOf(99) == K::kMisc,
                  "helmet: PNAM values map to their kinds, an unknown one is misc");
        }

        // 8c. Eyes: which pieces are eyes, the eye pass's two draws, their shadow and highlight
        {
            using K = RMC::HeadPartKind;
            Check(RMC::IsEyePiece(true, false, false, false, K::kNotHeadPart, false),
                  "eye: the Eye shader is an eye wherever it is");
            Check(RMC::IsEyePiece(false, false, true, true, K::kNotHeadPart, false),
                  "eye: an env-map face shape with eye vertex data (AO Clipping Fix, SUEMR) is an eye");
            Check(RMC::IsEyePiece(false, false, true, false, K::kEyes, false),
                  "eye: a face shape whose own head part type is Eyes (JenDFEyesob, JKJ) is an eye");
            Check(!RMC::IsEyePiece(false, true, true, true, K::kEyes, false),
                  "eye: a hair or skin shader never is, eye data or Eyes type (WeelBones lashes, eye-shadow decals)");
            Check(!RMC::IsEyePiece(false, false, true, false, K::kEyes, true),
                  "eye: an Eyes-type extra part (a DF eye-shadow shell) is not");
            Check(!RMC::IsEyePiece(false, false, true, false, K::kMisc, true) &&
                      !RMC::IsEyePiece(false, false, true, false, K::kNotHeadPart, false),
                  "eye: a lash the eyes bring (own type Misc) and an unnamed face shape are not");
            Check(!RMC::IsEyePiece(false, false, false, true, K::kEyes, false),
                  "eye: only an actor's face shape takes the vertex-data and head-part routes");

            Check(RMC::EyeDrawsBlended(true, true) && !RMC::EyeDrawsBlended(true, false) &&
                      !RMC::EyeDrawsBlended(false, true),
                  "eye pass: only a blended eye draws in it");
            Check(RMC::EyeDrawAlphaCode(RMC::EyeDraw::kCore, 0.0f) == -RMC::kEyeCoreRef &&
                      RMC::EyeDrawAlphaCode(RMC::EyeDraw::kCore, -0.01f) == -RMC::kEyeCoreRef,
                  "eye pass: the core is a cutout at the core threshold, over a 0.01 test too (AO Clipping Fix)");
            Check(RMC::EyeDrawAlphaCode(RMC::EyeDraw::kBlend, 0.0f) == 0.0f &&
                      RMC::EyeDrawAlphaCode(RMC::EyeDraw::kBlend, RMC::AlphaTestCode(true, 0.01f)) == -0.01f,
                  "eye pass: the blend draw keeps the material's own test");
            Check(RMC::EyeDrawAlphaCode(RMC::EyeDraw::kCore, 0.0f) < -0.001f &&
                      RMC::EyeDrawAlphaCode(RMC::EyeDraw::kCore, 0.0f) == RMC::AlphaTestCode(true, RMC::kEyeCoreRef),
                  "eye pass: the core lane is the PS's cutout lane, never a strand's");
            Check(RMC::EyeDrawEmit(RMC::EyeDraw::kCore) == RMC::kEmitNone &&
                      RMC::EyeDrawEmit(RMC::EyeDraw::kBlend) == RMC::kEmitOverlay,
                  "eye pass: the core is opaque, the blend emits the texture alpha with no glass lift");
            Check(RMC::ShadowClipRef(false, 0.0f, 0.0f, false, true) == RMC::kEyeCoreRef &&
                      RMC::ShadowClipRef(true, 0.01f, 0.0f, false, true) == RMC::kEyeCoreRef,
                  "eye pass: a blended eye's shadow is its core's");
            Check(RMC::kEyeCoreRef == RMC::kStrandCoreRef, "eye pass: the eye's core takes the strand core's threshold");

            float ei = 1.0f, ep = 0.0f;
            RMC::EyeSpecular(true, ei, ep);
            Check(ei == RMC::kEyeSpecIntensity && ep == RMC::kEyeSpecPower && ei < 0.1f,
                  "eye spec: a small catchlight with the Specular flag");
            RMC::EyeSpecular(false, ei, ep);
            Check(ei == 0.0f, "eye spec: none without it (JKJ eyes)");
        }

        // 9. Normal-map names
        {
            Check(RMC::IsTangentSpaceNormalMapName("textures\\armor\\iron\\cuirass_n.dds"), "normal: _n.dds");
            Check(RMC::IsTangentSpaceNormalMapName("textures\\mod\\robe_normal.dds"), "normal: _normal.dds");
            Check(RMC::IsTangentSpaceNormalMapName("textures\\mod\\robe_norm.dds") &&
                  RMC::IsTangentSpaceNormalMapName("textures\\mod\\robe_nm.dds"), "normal: _norm and _nm");
            Check(RMC::IsTangentSpaceNormalMapName("textures\\mod\\robe\\normal.dds") &&
                  RMC::IsTangentSpaceNormalMapName("normal.dds"), "normal: a file named normal.dds");
            Check(!RMC::IsTangentSpaceNormalMapName("textures\\mod\\abnormal.dds"), "normal: 'normal' inside a name is not a map");
            Check(!RMC::IsTangentSpaceNormalMapName("textures\\mod\\robe_d.dds") &&
                  !RMC::IsTangentSpaceNormalMapName("textures\\mod\\robe.dds"), "normal: a diffuse is not a normal map");
            Check(!RMC::IsTangentSpaceNormalMapName("textures\\actors\\character\\female\\femalebody_1_msn.dds") &&
                  RMC::IsModelSpaceNormalMapName("textures\\actors\\character\\female\\femalebody_1_msn.dds"),
                  "normal: _msn is model space, not tangent space");
            Check(RMC::IsTangentSpaceNormalMapName("textures\\fuse00\\hairpack01\\bellatrix\\bellatrix norm.dds") &&
                      RMC::IsTangentSpaceNormalMapName("textures\\fuse00\\hairpack02\\hair normal.dds"),
                  "normal: HDT-SMP Hairpack's ' norm' and ' normal' tails");
            Check(RMC::IsTangentSpaceNormalMapName("textures\\remnants\\nazar_hat_nrm.dds"), "normal: _nrm.dds");
            Check(!RMC::IsTangentSpaceNormalMapName("textures\\mod\\enorm.dds") &&
                      !RMC::IsTangentSpaceNormalMapName("textures\\mod\\hairnrm.dds") &&
                      !RMC::IsTangentSpaceNormalMapName("textures\\mod\\hair norm d.dds"),
                  "normal: the new tails need their separator and the end of the name");
        }

        // 10. RaceMenu overlays
        {
            Check(RMC::IsRaceMenuOverlayName("Body [Ovl0]") && RMC::IsRaceMenuOverlayName("Face [Ovl2]") &&
                      RMC::IsRaceMenuOverlayName("Hands [Ovl11]"),
                  "overlay: SKEE's [OvlN] shapes are overlays");
            Check(RMC::IsRaceMenuOverlayName("Body [SOvl0]") && RMC::IsRaceMenuOverlayName("Feet [SOvl3]"),
                  "overlay: SKEE's spell [SOvlN] shapes are overlays too");
            Check(!RMC::IsRaceMenuOverlayName("FemaleBody") && !RMC::IsRaceMenuOverlayName("HIMBO - Body") &&
                      !RMC::IsRaceMenuOverlayName("Overlay") && !RMC::IsRaceMenuOverlayName(""),
                  "overlay: a plain skin shape is not one");

            Check(RMC::IsEmptyOverlayDiffuse("textures\\actors\\character\\overlays\\default.dds") &&
                      RMC::IsEmptyOverlayDiffuse("Textures\\Actors\\Character\\Overlays\\default.dds") &&
                      RMC::IsEmptyOverlayDiffuse("Actors\\Character\\Overlays\\Default.dds") &&
                      RMC::IsEmptyOverlayDiffuse("data/textures/actors/character/overlays/DEFAULT.DDS"),
                  "overlay: SKEE's default.dds is the empty slot in every spelling");
            Check(!RMC::IsEmptyOverlayDiffuse("textures\\actors\\character\\overlays\\rmcommunity\\00 body.dds") &&
                      !RMC::IsEmptyOverlayDiffuse("textures\\actors\\character\\overlays\\mydefault.dds") &&
                      !RMC::IsEmptyOverlayDiffuse("textures\\actors\\character\\overlays\\default.dds.bak") &&
                      !RMC::IsEmptyOverlayDiffuse("default.dds") && !RMC::IsEmptyOverlayDiffuse(""),
                  "overlay: an applied texture is not the empty slot");

            const auto white = RMC::OverlaySkinTint(true, 1.0f, 1.0f, 1.0f);
            Check(white.engineBlend && white.r == 1.0f && white.g == 1.0f && white.b == 1.0f,
                  "overlay tint: an RGB-tint overlay takes its material's white through the engine blend");
            const auto black = RMC::OverlaySkinTint(true, 0.0f, 0.0f, 0.0f);
            Check(black.engineBlend && black.r == 0.0f && black.g == 0.0f && black.b == 0.0f,
                  "overlay tint: black stays black (the engine draws b^2), never dropped or defaulted");
            const auto red = RMC::OverlaySkinTint(true, 0.8f, 0.1f, 0.2f);
            Check(red.engineBlend && red.r == 0.8f && red.g == 0.1f && red.b == 0.2f,
                  "overlay tint: a colour passes through as it is");
            const auto faceGen = RMC::OverlaySkinTint(false, 0.2f, 0.3f, 0.4f);
            Check(!faceGen.engineBlend && faceGen.r == 1.0f && faceGen.g == 1.0f && faceGen.b == 1.0f,
                  "overlay tint: a kFaceGen overlay is untinted (white, multiplied)");
            const auto bad = RMC::OverlaySkinTint(true, nan, inf, 0.5f);
            Check(bad.engineBlend && bad.r == 1.0f && bad.g == 1.0f && bad.b == 0.5f,
                  "overlay tint: a non-finite channel reads as the template's 1");
            Check(Near(RMC::SkinTintOverlay(0.25f, 0.5f), 0.25f, 1e-6f) &&
                      Near(RMC::SkinTintOverlay(0.25f, 0.0f), 0.0625f, 1e-6f) &&
                      Near(RMC::SkinTintOverlay(0.25f, 1.0f), 0.4375f, 1e-6f),
                  "overlay tint: on a 0.25 grey base, grey is identity, black b^2, white 2b - b^2");
        }

        // 11. Skin decals: a blended skin piece is a decal, a tested decal-flagged one a cutout
        {
            using L = RMC::SkinLayer;
            // (rgbTint, actorPiece, blended, tested, decalFlagged)
            Check(RMC::SkinLayerOf(true, true, true, false, true) == L::kDecal &&
                      RMC::SkinLayerOf(true, true, true, false, false) == L::kDecal,
                  "decal: an actor's blended RGB-tint piece (a scar, an eye shadow) is a decal, decal flag or not");
            Check(RMC::SkinLayerOf(true, true, true, true, true) == L::kDecal,
                  "decal: blended and tested is still a decal (it keeps its own test)");
            Check(RMC::SkinLayerOf(false, true, true, false, true) == L::kSkip,
                  "decal: a blended kFaceGen piece is skipped (no FaceTint on a separate mesh)");
            Check(RMC::SkinLayerOf(true, false, true, false, true) == L::kSkip,
                  "decal: the item view has no decal pass");
            Check(RMC::SkinLayerOf(true, true, false, false, true) == L::kSkip &&
                      RMC::SkinLayerOf(false, true, false, false, true) == L::kSkip,
                  "decal: decal-flagged, neither blended nor tested, is skipped");
            Check(RMC::SkinLayerOf(true, true, false, true, true) == L::kSkin &&
                      RMC::SkinLayerOf(true, false, false, true, true) == L::kSkin,
                  "decal: decal-flagged and tested on the skin tint shader (HPH ENB Brow Fix brows) draws as a cutout");
            Check(RMC::SkinLayerOf(false, true, false, true, true) == L::kSkip,
                  "decal: a tested decal on the head's own shader is skipped (it would take the head's maps)");
            Check(RMC::SkinLayerOf(true, true, false, false, false) == L::kSkin &&
                      RMC::SkinLayerOf(true, false, false, false, false) == L::kSkin,
                  "decal: plain skin is skin");

            Check(RMC::SkinDepthPull(L::kDecal, false) == RMC::kSkinDecalDepthPull &&
                      RMC::SkinDepthPull(L::kDecal, true) == RMC::kSkinDecalDepthPull &&
                      RMC::SkinDepthPull(L::kSkin, true) == RMC::kSkinDecalDepthPull,
                  "decal pull: a decal and a decal-flagged cutout are pulled toward the camera");
            Check(RMC::SkinDepthPull(L::kSkin, false) == 0.0f && RMC::SkinDepthPull(L::kSkip, true) == 0.0f,
                  "decal pull: plain skin is not");
            Check(RMC::kSkinDecalDepthPull >= 0.1f && RMC::kSkinDecalDepthPull <= 0.2f,
                  "decal pull: past the ~0.1u a DF eye shadow or gash sits inside the skin, short of an eyelid rim");
        }

        // Texture sets and material maps
        {
            // Skyrim.esm's eye set: the env mask in TX02, the cube in TX05.
            Check(RMC::TxstIndexOfNifSlot(5) == 2 && RMC::TxstIndexOfNifSlot(4) == 5,
                  "txst: NIF slot 5 (env mask) is TX02, slot 4 (cube) is TX05");
            Check(RMC::TxstIndexOfNifSlot(2) == 3 && RMC::TxstIndexOfNifSlot(3) == 4,
                  "txst: NIF slot 2 (glow / _sk) is TX03, slot 3 (height / detail) is TX04");
            Check(RMC::TxstIndexOfNifSlot(0) == 0 && RMC::TxstIndexOfNifSlot(1) == 1 && RMC::TxstIndexOfNifSlot(6) == 6 &&
                      RMC::TxstIndexOfNifSlot(7) == 7,
                  "txst: diffuse, normal, multilayer and specular / back-light keep their index");
            bool seen[8]{};
            for (std::uint32_t s = 0; s < 8; ++s) seen[RMC::TxstIndexOfNifSlot(s)] = true;
            Check(std::all_of(std::begin(seen), std::end(seen), [](bool b) { return b; }),
                  "txst: every TX index fills exactly one NIF slot");

            Check(std::string_view(RMC::DiffuseIdentity("armor\\a.dds", "armor\\b.dds")) == "armor\\a.dds",
                  "diffuse identity: the loaded texture's name first");
            Check(std::string_view(RMC::DiffuseIdentity("", "armor\\b.dds")) == "armor\\b.dds" &&
                      std::string_view(RMC::DiffuseIdentity(nullptr, "armor\\b.dds")) == "armor\\b.dds",
                  "diffuse identity: an unnamed loaded diffuse falls back to texture-set slot 0");
            Check(RMC::DiffuseIdentity(nullptr, nullptr) == nullptr && RMC::DiffuseIdentity("", "") == nullptr,
                  "diffuse identity: none named is none");

            Check(RMC::GlowMapSkips(false) && !RMC::GlowMapSkips(true),
                  "glow: only a glow piece with no authored diffuse is a pure FX mesh");
            Check(RMC::IsGlowEye(true, true, true) && !RMC::IsGlowEye(true, true, false) &&
                      !RMC::IsGlowEye(false, true, true) && !RMC::IsGlowEye(true, false, true),
                  "glow: a blended dynamic glow shape with a diffuse is an eye; a blended static one is a surface");

            Check(RMC::SameTexturePath("textures\\armor\\iron\\cuirass_n.dds", "Armor/Iron/Cuirass_N.dds") &&
                      RMC::SameTexturePath("Data\\Textures\\a.dds", "a.dds") &&
                      RMC::SameTexturePath("data/textures/a.dds", "textures\\a.dds"),
                  "same path: case, slashes and the data / textures prefixes aside");
            Check(!RMC::SameTexturePath("armor\\a_n.dds", "armor\\a.dds") &&
                      !RMC::SameTexturePath("textures\\a.dds", "other\\textures\\a.dds"),
                  "same path: a different file, or textures\\ not at the front, differs");

            Check(RMC::IsPlaceholderSized(16, 16) && RMC::IsPlaceholderSized(32, 1) && !RMC::IsPlaceholderSized(64, 16) &&
                      !RMC::IsPlaceholderSized(0, 0),
                  "placeholder: 32 px a side or less, never 0 x 0");
            Check(RMC::PathLoadDropsAsPlaceholder(false, 1, 1) && RMC::PathLoadDropsAsPlaceholder(false, 512, 2) &&
                      !RMC::PathLoadDropsAsPlaceholder(false, 4, 4),
                  "path load: a 2D view under 4 px is dropped");
            Check(!RMC::PathLoadDropsAsPlaceholder(true, 1, 1),
                  "path load: a 1 px cube is real (the dynamic cubemap sentinel) and kept");

            Check(RMC::PrefersMaterialMap(true, 1024, 1024, false, false, "textures\\a_n.dds", "a_n.dds") &&
                      RMC::PrefersMaterialMap(true, 1024, 1024, false, false, "", "a_n.dds"),
                  "material map: a real 2D map named like the path, or unnamed, is used");
            Check(!RMC::PrefersMaterialMap(true, 16, 16, false, false, "", "a_n.dds") &&
                      !RMC::PrefersMaterialMap(true, 2, 2, false, false, "", "a_n.dds") &&
                      !RMC::PrefersMaterialMap(false, 1024, 1024, false, false, "", "a_n.dds"),
                  "material map: a placeholder-sized or missing view loads the path");
            Check(!RMC::PrefersMaterialMap(true, 1024, 1024, false, false, "textures\\b_n.dds", "a_n.dds"),
                  "material map: a material texture naming another file loads the path");
            Check(RMC::PrefersMaterialMap(true, 256, 256, true, true, "cubemaps\\ore_ebony_e.dds", "cubemaps\\ore_ebony_e.dds") &&
                      !RMC::PrefersMaterialMap(true, 256, 256, false, true, "", "cubemaps\\eyecubemap.dds") &&
                      !RMC::PrefersMaterialMap(true, 256, 256, true, false, "", "a_m.dds"),
                  "material map: a cube slot takes only a cube view, a 2D slot never one");

            Check(RMC::IsEngineDefaultTextureName("BSShader_DefNormalMap") &&
                      RMC::IsEngineDefaultTextureName("bsshader_defheightmap"),
                  "default texture: the shader's empty-slot defaults are recognised");
            Check(!RMC::IsEngineDefaultTextureName("textures\\armor\\a_m.dds") && !RMC::IsEngineDefaultTextureName("") &&
                      !RMC::IsEngineDefaultTextureName("bsshader"),
                  "default texture: an authored path is not one");

            Check(RMC::EnvGlossMaskCode(true, true, true) == 1.0f, "env gloss: a cube and no mask path masks by gloss");
            Check(RMC::EnvGlossMaskCode(true, true, false) == 0.0f && RMC::EnvGlossMaskCode(true, false, true) == 0.0f &&
                      RMC::EnvGlossMaskCode(false, true, true) == 0.0f,
                  "env gloss: not with a mask path, without a cube, or on skin");

            Check(RMC::SkipsDeferredStrand(true, true), "deferred strand: left out until its diffuse loads");
            Check(!RMC::SkipsDeferredStrand(true, false) && !RMC::SkipsDeferredStrand(false, true),
                  "deferred strand: a strand missing its diffuse for good keeps the flat tint; an opaque hair piece draws");
        }
    }
}
