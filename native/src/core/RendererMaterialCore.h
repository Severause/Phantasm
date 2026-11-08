#pragma once

// Phantasm - RendererMaterialCore: the live views' per-piece material rules that need no engine, shared by the
// actor view and the item view (MannequinRenderer.cpp) and tested by native/build.ps1 -Tests. The lane
// encodings here are read by kShaderHLSL in MannequinRenderer.cpp: a change on one side is a change on both.
//
// Author: Severause

#include "AsciiText.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace Phantasm::RendererMaterialCore
{
    // ── Alpha test (colorMode.z) ──

    /// A hair / brow / lash strand: a kHairTint material whose NiAlphaProperty tests or blends (HairAlphaTests), which
    /// alpha-tests with no cutout threshold (its own test is strandTestRef). Strands draw in the premultiplied pass;
    /// only one that writes depth casts a shadow (StrandCastsShadow).
    inline bool IsHairStrand(bool useAlphaTest, float alphaTestRef) noexcept
    {
        return useAlphaTest && !(alphaTestRef > 0.0f);
    }

    /// Whether a hair-shader (kHairTint) piece alpha-tests, so draws as a strand: only when its NiAlphaProperty tests
    /// or blends (both false without one). Otherwise the game draws it opaque (Redguard braids, Argonian crests,
    /// headbands): depth write and a shadow, still in the hair colour (materialParams.z).
    inline bool HairAlphaTests(bool alphaTested, bool alphaBlended) noexcept
    {
        return alphaTested || alphaBlended;
    }

    /// A NiAlphaProperty's test threshold (0-255) as the shader's cutoff (alphaTestRef), kept within [0.01, 0.99]:
    /// above zero, which IsHairStrand reads as a strand's missing threshold.
    inline float AlphaTestRefOf(std::uint8_t threshold) noexcept
    {
        return std::clamp(static_cast<float>(threshold) / 255.0f, 0.01f, 0.99f);
    }

    /// A generic NiAlphaProperty cutout (mail, netting, leaves): drawn opaque, clipped at its own threshold, and its
    /// shadow is clipped the same way.
    inline bool IsCutout(bool useAlphaTest, float alphaTestRef) noexcept
    {
        return useAlphaTest && alphaTestRef > 0.0f;
    }

    /// The colorMode.z lane: 0 no test, 1 a blended strand (clip at 0.05, the texture alpha as coverage), 1 + ref a
    /// strand the game alpha-tests at ref without blending (coverage that test with a soft one-pixel edge), 2 + ref a
    /// split strand's core pass (kStrandCoreCode: solid at and above ref, a hard edge), 3 + ref a strand the game
    /// both tests at ref and blends (clipped at ref, the texture alpha as coverage above it); a strand's colour is
    /// premultiplied by its coverage and the coverage emitted;
    /// -alphaTestRef a cutout (clip at the material threshold, opaque where kept).
    inline float AlphaTestCode(bool useAlphaTest, float alphaTestRef, float strandTestRef = 0.0f,
                               bool strandBlended = false) noexcept
    {
        if (!useAlphaTest) return 0.0f;
        if (IsCutout(true, alphaTestRef)) return -alphaTestRef;
        if (!(strandTestRef > 0.0f)) return 1.0f;
        return (strandBlended ? 3.0f : 1.0f) + std::clamp(strandTestRef, 0.01f, 0.99f);
    }

    /// A blended eye's core: the threshold its depth-writing draw clips at (see EyeDrawsBlended).
    inline constexpr float kEyeCoreRef = 0.5f;

    /// The threshold a shadow caster's depth pass clips at: a blended eye's core (eyeBlend), a cutout's own, a
    /// strand's that writes depth (each casts the shadow its drawn depth would), 0 for a piece drawn whole.
    inline float ShadowClipRef(bool useAlphaTest, float alphaTestRef, float strandTestRef, bool strandDepthWrite,
                               bool eyeBlend = false) noexcept
    {
        if (eyeBlend) return kEyeCoreRef;
        if (IsCutout(useAlphaTest, alphaTestRef)) return alphaTestRef;
        if (IsHairStrand(useAlphaTest, alphaTestRef) && strandDepthWrite && strandTestRef > 0.0f) return strandTestRef;
        return 0.0f;
    }

    /// Whether a piece of the strand pass casts a shadow: only a strand that writes depth.
    inline bool StrandCastsShadow(bool useAlphaTest, float alphaTestRef, bool strandDepthWrite) noexcept
    {
        return IsHairStrand(useAlphaTest, alphaTestRef) && strandDepthWrite;
    }

    /// A hair strand's highlight, from its own material as the game's: none unless its shader's Specular flag is
    /// on; strength from specularColorScale x the specular colour's luminance (0.22 = the live views' calibration of
    /// a scale of 1, as for an item), width from specularPower (a power of 4 or less is untuned: 32).
    inline void HairSpecular(bool specularFlag, float scale, float power, float colorLuminance, float& outIntensity,
                             float& outPower) noexcept
    {
        outIntensity = specularFlag ? std::clamp(0.22f * scale * colorLuminance, 0.0f, 0.6f) : 0.0f;
        outPower     = power > 4.0f ? std::clamp(power, 8.0f, 160.0f) : 32.0f;
    }

    /// Whether a strand writes depth, as the game draws it: only one its material alpha-tests (strandTestRef > 0) and
    /// whose shader asks for depth writes. A tested strand that writes depth hides the strands behind it the way the
    /// game's does; without it, a mesh's inner layers composite over its outer ones in index order.
    inline bool StrandWritesDepth(float strandTestRef, bool shaderZWrite) noexcept
    {
        return strandTestRef > 0.0f && shaderZWrite;
    }

    /// Whether a blend-pass piece writes depth, as the game draws it: one that also alpha-tests and whose shader asks
    /// for depth writes (a Restoration staff body, a rune glow plane: blend plus a test at 128). Without it, a closed
    /// two-sided mesh drawn in triangle order shows its back faces through its front ones.
    inline bool BlendWritesDepth(bool alphaTested, bool shaderZWrite) noexcept
    {
        return alphaTested && shaderZWrite;
    }

    /// A depth-writing strand the game tests below kStrandCoreRef and blends draws in two passes: its solid core
    /// (kStrandCoreCode) with depth, then the whole strand blended without it. In one pass its faint fringe writes
    /// depth and hides the hair behind it; the game's TAA blurs that away, the live views show it as speckle. A
    /// strand tested at kStrandCoreRef or above has no faint fringe and keeps the game's one pass.
    inline constexpr float kStrandCoreRef = 0.5f;

    /// The colorMode.z lane of a split strand's core pass (2 + ref, see AlphaTestCode).
    inline constexpr float kStrandCoreCode = 2.0f + kStrandCoreRef;

    inline bool StrandSplitsCore(float strandTestRef, bool strandBlended, bool strandDepthWrite) noexcept
    {
        return strandDepthWrite && strandBlended && strandTestRef > 0.0f && strandTestRef < kStrandCoreRef;
    }

    /// The key and fill shadow lookups' depth bias, in light-space NDC z (the actor view's light spans 750u: about 1.1u).
    /// A strand takes about 6u: hair layers sit closer than that, and the shadow map's ~0.15u texels turned each strand's
    /// shadow on the layer under it into dark dashes. A head or a helmet is farther away and still shadows the hair.
    inline constexpr float kShadowBias       = 0.0015f;
    inline constexpr float kStrandShadowBias = 0.008f;

    inline float ShadowBias(bool strand) noexcept { return strand ? kStrandShadowBias : kShadowBias; }

    // ── Face culling ──

    /// Whether a piece of a live view's colour passes draws with its back faces culled: as the game draws it, unless
    /// its shader property is kTwoSided, and never for a hair strand (thin cards whose back halves, or a whole card
    /// wound the other way, are part of the hair as seen; the strand pass draws both sides whatever the flag says).
    /// Eyes, overlays, skin decals, effects and blend pieces follow the flag like any other piece. The shadow depth
    /// pass, the floor and the particles never cull.
    inline bool CullsBackFaces(bool twoSided, bool hairStrand) noexcept
    {
        return !twoSided && !hairStrand;
    }

    // ── Emitted alpha (pieceFlags.w, materialParams.x) ──

    /// pieceFlags.w: how a transparent piece emits its alpha (an overlay, a skin decal, a blended eye's blend draw:
    /// kEmitOverlay; a blend piece: kEmitBlend). The PS emits the sampled alpha for both; only kEmitBlend lifts it under a specular
    /// glint (glass).
    inline constexpr float kEmitNone    = 0.0f;
    inline constexpr float kEmitOverlay = 1.0f;
    inline constexpr float kEmitBlend   = 2.0f;

    inline float EmitAlphaMode(bool isOverlay, bool isAlphaBlend) noexcept
    {
        return isOverlay ? kEmitOverlay : isAlphaBlend ? kEmitBlend : kEmitNone;
    }

    /// A material whose alpha is at or below this draws nothing in game, blended or opaque; the collectors drop it.
    inline constexpr float kInvisibleMaterialAlpha = 0.01f;

    /// NaN counts as invisible.
    inline bool IsEngineInvisible(float materialAlpha) noexcept
    {
        return !(materialAlpha > kInvisibleMaterialAlpha);
    }

    /// materialParams.x: the multiplier on an overlay's, skin decal's, blend piece's or blended eye's emitted alpha, in
    /// [0, 1]. NaN is 1, the neutral value (an unreadable material alpha shows the piece as authored).
    inline float EmitAlphaMultiplier(float alpha) noexcept
    {
        if (std::isnan(alpha)) return 1.0f;
        return std::clamp(alpha, 0.0f, 1.0f);
    }

    // ── Emissive (miscParams.x, emissiveAndFlags) ──

    /// miscParams.x: where a piece's emissive goes. A lighting-shader piece adds it to the light it receives, so the
    /// albedo (vertex colour included) multiplies it, unshadowed, as the game's lighting shader does. A TruePBR piece
    /// adds its glow map's emissive after the lighting and has none without that map, as Community Shaders' does (less
    /// its vertex-colour tint). An effect piece's whole colour is its emissive (an unlit black albedo).
    inline constexpr float kEmissiveLit    = 0.0f;
    inline constexpr float kEmissivePBR    = 1.0f;
    inline constexpr float kEmissiveEffect = 2.0f;

    inline float EmissiveRoute(bool isEffect, bool isPBR) noexcept
    {
        return isEffect ? kEmissiveEffect : isPBR ? kEmissivePBR : kEmissiveLit;
    }

    /// A lighting piece's emissiveColor x emissiveMult, per channel, is held to [0, kEmissiveCap]: an aura or a
    /// Spriggan's runs to 3, which the live views have no HDR to show (it would turn the piece white). NaN is 0.
    inline constexpr float kEmissiveCap = 1.5f;

    inline float EmissiveChannel(float v) noexcept
    {
        if (std::isnan(v)) return 0.0f;
        return std::clamp(v, 0.0f, kEmissiveCap);
    }

    // ── Highlight tint (specColorTint.a) ──

    /// specColorTint.a: what the material's specularColor (rgb; only a colour away from white is carried) tints. Every
    /// code but kSpecTintNone tints the metal reflection. The highlights take it too, as the game multiplies each
    /// light's specular by it: whole on a lighting piece (kSpecTintFull), as its hue alone on a hair-shader piece
    /// (kSpecTintHue: HairSpecular already scales the strength by the colour's luminance), not at all on a TruePBR
    /// piece (kSpecTintMetal), whose highlight comes from its RMAOS map.
    inline constexpr float kSpecTintNone  = 0.0f;
    inline constexpr float kSpecTintFull  = 1.0f;
    inline constexpr float kSpecTintHue   = 2.0f;
    inline constexpr float kSpecTintMetal = 3.0f;

    inline float SpecTintCode(bool hasSpecTint, bool isPBR, bool isHairMaterial) noexcept
    {
        if (!hasSpecTint) return kSpecTintNone;
        if (isPBR) return kSpecTintMetal;
        return isHairMaterial ? kSpecTintHue : kSpecTintFull;
    }

    // ── Texture addressing ──

    /// BSGraphics::TextureAddressMode: 0 clamp S / clamp T, 1 clamp S / wrap T, 2 wrap S / clamp T, 3 wrap both (the
    /// default). S is U and T is V. The value is also the index of the live views' sampler for that mode; an
    /// out-of-range mode samples as the default.
    inline constexpr std::uint8_t kSamplerCount       = 4;
    inline constexpr std::uint8_t kDefaultAddressMode = 3;

    inline std::uint8_t SamplerIndex(int textureClampMode) noexcept
    {
        return (textureClampMode >= 0 && textureClampMode < kSamplerCount)
                   ? static_cast<std::uint8_t>(textureClampMode)
                   : kDefaultAddressMode;
    }
    inline bool SamplerWrapsU(std::uint8_t index) noexcept { return (index & 2u) != 0; }
    inline bool SamplerWrapsV(std::uint8_t index) noexcept { return (index & 1u) != 0; }

    // ── Transparent pass order ──

    /// depth = dot(piece centre, camera forward), which grows away from the camera. Non-finite depth sorts as 0 so the
    /// comparator stays a strict weak order.
    inline float SortDepth(float depth) noexcept
    {
        return std::isfinite(depth) ? depth : 0.0f;
    }

    /// Back-to-front: the farther piece draws first, so a nearer one blends over it.
    inline bool DrawsBefore(float depthA, float depthB) noexcept
    {
        return SortDepth(depthA) > SortDepth(depthB);
    }

    // ── Head / body skin brightness (the collect's log only; nothing is corrected) ──

    inline constexpr float kHeadLumaScaleMin = 0.55f;
    inline constexpr float kHeadLumaScaleMax = 1.8f;

    /// The engine's skin tint overlay (FACEGEN_RGB_TINT) on one gamma value: b^2 + 2 t b (1 - b), clamped to [0, 1];
    /// t = 0.5 leaves b as it is. Applied to an average luma with the tint's luma, it is the tinted body's brightness the
    /// head is compared with.
    inline float SkinTintOverlay(float base, float tint) noexcept
    {
        if (!std::isfinite(base) || !std::isfinite(tint)) return base;
        return std::clamp(base * base + 2.0f * tint * base * (1.0f - base), 0.0f, 1.0f);
    }

    /// The head's skin brightness as drawn: the head texture's luma under its FaceTint's overlay (the FaceTint carries
    /// the skin tone, which brightens the head as the body tint does the body); the bare texture's when the FaceTint's
    /// luma is unknown (< 0 or NaN) or the head's is.
    inline float HeadParityLuma(float headLuma, float faceTintLuma) noexcept
    {
        if (!(headLuma > 0.0f) || !(faceTintLuma > 0.0f)) return headLuma;
        return SkinTintOverlay(headLuma, faceTintLuma);
    }

    /// The ratio of the body's average skin luminance to the head's, clamped against outlier bakes; 1 when either side
    /// is unknown (< 0 or NaN).
    inline float HeadLumaScale(float bodyLuma, float headLuma) noexcept
    {
        if (!(bodyLuma > 0.0f) || !(headLuma > 0.0f)) return 1.0f;
        return std::clamp(bodyLuma / headLuma, kHeadLumaScaleMin, kHeadLumaScaleMax);
    }

    // ── RaceMenu overlays (SKEE's "<part> [OvlN]" and spell "<part> [SOvlN]" shapes) ──

    /// A RaceMenu overlay shape, by its node name: SKEE names every overlay copy of a skin shape this way.
    inline bool IsRaceMenuOverlayName(std::string_view name) noexcept
    {
        return name.find("[Ovl") != std::string_view::npos || name.find("[SOvl") != std::string_view::npos;
    }

    /// SKEE's empty overlay slot: its diffuse is the shipped transparent `actors\character\overlays\default.dds`, under
    /// any of the spellings a path arrives in (case, either slash, a `textures\` or `data\textures\` prefix).
    inline bool IsEmptyOverlayDiffuse(std::string_view path) noexcept
    {
        constexpr std::string_view kTail = "actors\\character\\overlays\\default.dds";
        if (path.size() < kTail.size()) return false;
        const std::size_t start = path.size() - kTail.size();
        for (std::size_t i = 0; i < kTail.size(); ++i) {
            char c = AsciiText::Lower(path[start + i]);
            if (c == '/') c = '\\';
            if (c != kTail[i]) return false;
        }
        return start == 0 || path[start - 1] == '\\' || path[start - 1] == '/';
    }

    /// An overlay's skin tint and how it applies.
    struct OverlayTint
    {
        float r = 1.0f, g = 1.0f, b = 1.0f;
        bool  engineBlend = false;   // skinTint.a = 2 (the SkinTintOverlay blend on the gamma diffuse), else 1
    };

    /// A FaceGenRGBTint overlay takes its own material's tintColor (SKEE writes the player's colour there) through the
    /// engine's skin tint blend; black is a colour (the engine draws b^2), never dropped. Any other overlay (kFaceGen)
    /// is untinted: white, multiplied. A non-finite channel reads as the template's 1.
    inline OverlayTint OverlaySkinTint(bool rgbTintMaterial, float r, float g, float b) noexcept
    {
        if (!rgbTintMaterial) return {};
        const auto ch = [](float v) { return std::isfinite(v) ? v : 1.0f; };
        return { ch(r), ch(g), ch(b), true };
    }

    // ── Skin decals ──

    /// How a skin-shader piece (kFaceGen, kFaceGenRGBTint) that is not a RaceMenu overlay draws: as skin (a cutout
    /// when it tests), in the decal pass, or not at all.
    enum class SkinLayer
    {
        kSkin,
        kDecal,
        kSkip
    };

    /// A skin piece whose NiAlphaProperty blends is a decal lying on the skin (a face scar, the DF followers' eye
    /// shadow, a whole-face mask, a body fluid layer), whatever head part brings it: an actor's kFaceGenRGBTint one
    /// draws in the decal pass (straight alpha, no depth write, no shadow); any other is skipped (a kFaceGen one would
    /// take the head's FaceTint and detail map onto a separate mesh, and the item view has no decal pass). A Decal or
    /// Dynamic Decal shader flag without a blend is skipped too unless the piece alpha-tests on the skin tint shader
    /// (HPH ENB Brow Fix's brows): untested, its background would paint over the skin; tested, it draws as a cutout at
    /// its threshold. A tested decal on the head's own (kFaceGen) shader is skipped like a blended one.
    inline SkinLayer SkinLayerOf(bool rgbTintMaterial, bool actorPiece, bool alphaBlended, bool alphaTested,
                                 bool decalFlagged) noexcept
    {
        if (alphaBlended) return rgbTintMaterial && actorPiece ? SkinLayer::kDecal : SkinLayer::kSkip;
        if (decalFlagged && (!alphaTested || !rgbTintMaterial)) return SkinLayer::kSkip;
        return SkinLayer::kSkin;
    }

    /// How far a skin decal is drawn toward the camera, in game units of view depth (materialParams.w; VS_Main moves
    /// the vertex along its own view ray, so it stays where it is on screen). A decal is a separate mesh laid on the
    /// skin, and part of it sits a little inside the skin it covers: the DF eye shadow and the vanilla gashes need up
    /// to about 0.1u along the view ray from the front to three-quarter views. Too large and a decal shows through
    /// skin standing less than this in front of it (an eyelid's rim, a nose or brow ridge seen from the side).
    inline constexpr float kSkinDecalDepthPull = 0.15f;

    /// The depth pull of a skin piece: a decal's, and a decal-flagged cutout's, which lies on the skin the same way; 0
    /// for anything else.
    inline float SkinDepthPull(SkinLayer layer, bool decalFlagged) noexcept
    {
        return layer == SkinLayer::kDecal || (layer == SkinLayer::kSkin && decalFlagged) ? kSkinDecalDepthPull : 0.0f;
    }

    // ── Hair under a helmet ──

    /// A head part's type (BGSHeadPart PNAM, the same values), or kNotHeadPart for a piece no head part of the actor
    /// names.
    enum class HeadPartKind : int
    {
        kMisc = 0,
        kFace,
        kEyes,
        kHair,
        kFacialHair,
        kScar,
        kEyebrows,
        kNotHeadPart
    };

    /// A head part type's value as HeadPartKind; an unknown value is kMisc (kept, as the game keeps it).
    inline HeadPartKind HeadPartKindOf(std::uint32_t pnam) noexcept
    {
        return pnam <= static_cast<std::uint32_t>(HeadPartKind::kEyebrows) ? static_cast<HeadPartKind>(pnam)
                                                                            : HeadPartKind::kMisc;
    }

    /// Whether a lowercase piece name reads as a face feature (brows, lashes, beards, mustaches): the rule for a
    /// hair-shader piece that is not one of the actor's head parts.
    inline bool NameIsFaceFeature(std::string_view lower) noexcept
    {
        for (const std::string_view k : { "brow", "lash", "beard", "mustache", "stache" }) {
            if (lower.find(k) != std::string_view::npos) return true;
        }
        return false;
    }

    /// Whether a hair-shader piece is dropped while a drawn armor covers the hair slots. A head part goes by its
    /// type, as the game hides them: only Hair (and the extra parts it brings, which take its type) goes; brows,
    /// beards, eyes and the rest stay, whatever they are called. Anything else goes by NameIsFaceFeature.
    inline bool HidesUnderHelmet(HeadPartKind kind, std::string_view lowerName) noexcept
    {
        if (kind == HeadPartKind::kNotHeadPart) return !NameIsFaceFeature(lowerName);
        return kind == HeadPartKind::kHair;
    }

    // ── Eyes ──

    /// Whether a lighting-shader piece is an eye (EyeSpecular, no metal reflection, the eye pass when it blends). The
    /// Eye shader always is; a hair or skin shader never is (some hair-shader lashes carry eye vertex data, and an
    /// eye-type shell on the skin shader is a decal). Any other shader on an actor's face shape (a BSDynamicTriShape)
    /// is one when its vertices carry eye data (VF_EYEDATA), or when its name is a head part whose OWN type is Eyes and
    /// that is not an extra part: never the type an extra part takes from its parent for the hair-hide (a lash the
    /// eyes bring is not an eye), and the Eyes-type extra parts that ship (DF eye-shadow shells) are blended layers.
    inline bool IsEyePiece(bool eyeShader, bool hairOrSkinShader, bool actorFaceShape, bool vertexEyeData,
                           HeadPartKind ownKind, bool ownIsExtraPart) noexcept
    {
        if (eyeShader) return true;
        if (hairOrSkinShader || !actorFaceShape) return false;
        return vertexEyeData || (ownKind == HeadPartKind::kEyes && !ownIsExtraPart);
    }

    /// A blended eye (its NiAlphaProperty blends, whatever it tests) draws in the eye pass, between the opaque pass
    /// and the strand pass, in two draws as a split strand does: its core (EyeDraw::kCore: clipped at kEyeCoreRef,
    /// opaque, writing depth), then the whole piece (EyeDraw::kBlend: straight alpha, no depth write, its own alpha
    /// test, the texture alpha x its material alpha emitted as an overlay's, so no glass lift). The vanilla eye mesh
    /// carries its lash cards as near-black, near-transparent texels: drawn opaque they are slabs, and blended with
    /// depth they would cut holes in the lash and brow strands drawn after them. The RaceMenu overlays draw before
    /// it, so its soft edges blend over the paint as in game; the skin decals after it (SkinLayer::kDecal), so an eye
    /// shadow shell darkens the eyeball it lies in front of. Its shadow is its core's (ShadowClipRef).
    inline bool EyeDrawsBlended(bool isEye, bool alphaBlended) noexcept
    {
        return isEye && alphaBlended;
    }

    enum class EyeDraw
    {
        kCore,
        kBlend
    };

    /// colorMode.z of an eye-pass draw: the core is a cutout at kEyeCoreRef (AlphaTestCode's -ref lane), the blend
    /// keeps the piece's own lane (ownCode, AlphaTestCode of its material: 0 or a cutout's -ref, never a strand's).
    inline float EyeDrawAlphaCode(EyeDraw draw, float ownCode) noexcept
    {
        return draw == EyeDraw::kCore ? -kEyeCoreRef : ownCode;
    }

    /// pieceFlags.w of an eye-pass draw.
    inline float EyeDrawEmit(EyeDraw draw) noexcept
    {
        return draw == EyeDraw::kCore ? kEmitNone : kEmitOverlay;
    }

    /// An eye's highlight: a small high-power catchlight (the armour default blows a bright or vampire iris out into
    /// a glint), none when its shader's Specular flag is off, as the game's.
    inline constexpr float kEyeSpecIntensity = 0.05f;
    inline constexpr float kEyeSpecPower     = 80.0f;

    inline void EyeSpecular(bool specularFlag, float& outIntensity, float& outPower) noexcept
    {
        outIntensity = specularFlag ? kEyeSpecIntensity : 0.0f;
        outPower     = kEyeSpecPower;
    }

    // ── Bind-pose re-parent ──

    /// A piece hanging off a live bone is moved onto that bone's bind pose (R = inverse(live) x bind) only when the
    /// live bone inverts, R is finite, and the two bones are closer than this: a farther bind entry is judged
    /// implausible (an outfit skin authored against another skeleton) and the piece stays at the live bone.
    inline constexpr float kMaxBindReparentDistance = 40.0f;
    /// The same for a weapon, shield or quiver on the skeleton's attach nodes: its bone is a hand, forearm, pelvis
    /// or spine bone of the body's own skeleton, which a drawn weapon or a seated NPC moves far from its bind pose.
    inline constexpr float kMaxWeaponBindReparentDistance = 200.0f;

    inline bool AcceptsBindReparent(float liveDeterminant, bool reparentFinite, float liveToBindDistance,
                                    float maxDistance = kMaxBindReparentDistance) noexcept
    {
        return std::fabs(liveDeterminant) > 1e-12f && reparentFinite && liveToBindDistance < maxDistance;
    }

    namespace detail
    {
        inline bool EndsWith(std::string_view s, std::string_view suffix) noexcept
        {
            return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
        }
        inline std::string_view Basename(std::string_view s) noexcept
        {
            const auto slash = s.find_last_of("\\/");
            return slash == std::string_view::npos ? s : s.substr(slash + 1);
        }
    }

    // ── Texture sets and material maps ──

    /// A TXST (BGSTextureSet) lists its paths TX00-TX07, a NIF's BSShaderTextureSet by shader slot; the two orders
    /// differ in slots 2-5: TX02 is the env mask / subsurface tint (NIF slot 5), TX03 the glow / skin `_sk` (slot 2),
    /// TX04 the height / FaceGen detail (slot 3), TX05 the env cube (slot 4). The TX index that fills a NIF slot (0-7).
    inline constexpr std::uint8_t kTxstIndexOfNifSlot[8] = { 0, 1, 3, 4, 5, 2, 6, 7 };

    inline std::uint8_t TxstIndexOfNifSlot(std::uint32_t nifSlot) noexcept
    {
        return nifSlot < 8 ? kTxstIndexOfNifSlot[nifSlot] : static_cast<std::uint8_t>(nifSlot);
    }

    /// The diffuse a material was authored with: its loaded texture's name, else its texture set's slot 0 (worn
    /// gear's loaded diffuse can be unnamed). Never the loaded view: an empty slot binds the engine's default, and a
    /// texture not streamed in has none. Null when neither names one.
    inline const char* DiffuseIdentity(const char* loadedName, const char* texSetSlot0) noexcept
    {
        if (loadedName && loadedName[0]) return loadedName;
        if (texSetSlot0 && texSetSlot0[0]) return texSetSlot0;
        return nullptr;
    }

    /// Whether a kGlowMap piece is a pure glow / aura mesh, skipped: one with no authored diffuse. A blended one
    /// with a diffuse is a real surface (a Restoration staff's body, a rune weapon's glow layer) and draws in the
    /// blend pass, or the eye pass for a vampire's eye (IsGlowEye).
    inline bool GlowMapSkips(bool hasDiffuseIdentity) noexcept
    {
        return !hasDiffuseIdentity;
    }

    /// A blended FaceGen head part (a dynamic shape) on the glow shader with a diffuse: a vampire's eye.
    inline bool IsGlowEye(bool alphaBlended, bool hasDiffuseIdentity, bool dynamicShape) noexcept
    {
        return alphaBlended && hasDiffuseIdentity && dynamicShape;
    }

    namespace detail
    {
        inline char FoldPathChar(char c) noexcept
        {
            if (c == '/') return '\\';
            return AsciiText::Lower(c);
        }
        /// The path without a leading `data\` and then `textures\` (either slash, any case): a TXST's paths omit
        /// `textures\`, a NIF's and a loaded texture's name carry it.
        inline std::string_view TextureRelative(std::string_view p) noexcept
        {
            const auto strip = [&p](std::string_view prefix) {
                if (p.size() < prefix.size()) return;
                for (std::size_t i = 0; i < prefix.size(); ++i) {
                    if (FoldPathChar(p[i]) != prefix[i]) return;
                }
                p.remove_prefix(prefix.size());
            };
            strip("data\\");
            strip("textures\\");
            return p;
        }
    }

    /// Whether two texture paths name the same file: case and slashes aside, and with or without `data\` and
    /// `textures\` in front.
    inline bool SameTexturePath(std::string_view a, std::string_view b) noexcept
    {
        a = detail::TextureRelative(a);
        b = detail::TextureRelative(b);
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (detail::FoldPathChar(a[i]) != detail::FoldPathChar(b[i])) return false;
        }
        return true;
    }

    /// A texture this small (32 px a side or less) is an engine placeholder: the 16x16 default of a file missing on
    /// disk, or one not streamed in yet. 0 x 0 is no texture at all, not a placeholder.
    inline bool IsPlaceholderSized(std::uint32_t w, std::uint32_t h) noexcept
    {
        constexpr std::uint32_t kPlaceholderMaxDim = 32;
        return w > 0 && w <= kPlaceholderMaxDim && h > 0 && h <= kPlaceholderMaxDim;
    }

    /// Whether a path load drops its view as a placeholder (the PS then takes its fallback): a 2D view under 4 px a
    /// side. Never a cube: a 1x1 cube is real (the Community Shaders `dynamic1pxcubemap_black` sentinel, 1 px `_e`
    /// cubes), and the game samples it.
    inline bool PathLoadDropsAsPlaceholder(bool cubeView, std::uint32_t w, std::uint32_t h) noexcept
    {
        return !cubeView && (w < 4 || h < 4);
    }

    /// Whether a material map the engine already loaded is drawn in place of loading its path again: a view at
    /// least 4 px a side and not placeholder-sized, of the kind the slot samples (a cube for a cube slot, never a
    /// cube for a 2D one), whose name is empty or names the same file as the path. Not for a hair diffuse: the hair
    /// material's diffuse can be a placeholder that passes the size test yet samples zero.
    inline bool PrefersMaterialMap(bool haveView, std::uint32_t w, std::uint32_t h, bool cubeView, bool wantCube,
                                   std::string_view materialName, std::string_view path) noexcept
    {
        if (!haveView || w < 4 || h < 4 || IsPlaceholderSized(w, h) || cubeView != wantCube) return false;
        return materialName.empty() || SameTexturePath(materialName, path);
    }

    /// An engine default texture the shader binds to an empty slot (`BSShader_Def...`, any case): no authored map.
    inline bool IsEngineDefaultTextureName(std::string_view name) noexcept
    {
        constexpr std::string_view kPrefix = "bsshader_def";
        if (name.size() < kPrefix.size()) return false;
        for (std::size_t i = 0; i < kPrefix.size(); ++i) {
            if (detail::FoldPathChar(name[i]) != kPrefix[i]) return false;
        }
        return true;
    }

    /// miscParams.w: an env-mapped non-skin piece with a cube and no mask path (none on the material, none in slot
    /// 5) takes the engine's gloss as its mask: the normal map's alpha, or the slot-7 specular map on a model-space
    /// piece. Decided by the PATH, never a null view: a load the texture budget deferred is still coming.
    inline float EnvGlossMaskCode(bool nonSkin, bool haveCube, bool noMaskPath) noexcept
    {
        return nonSkin && haveCube && noMaskPath ? 1.0f : 0.0f;
    }

    /// A hair strand whose diffuse load the live view's texture budget deferred is left out of that pass: drawn, it
    /// would be an opaque card in the hair colour (the PS draws a strand without a diffuse whole). A diffuse missing
    /// for a real reason keeps the flat hair tint.
    inline bool SkipsDeferredStrand(bool strand, bool diffuseDeferred) noexcept
    {
        return strand && diffuseDeferred;
    }

    // ── Normal-map names (textureSet[1]) ──

    /// A model-space normal map (`_msn`, the FaceGen skin convention). The path is lower-cased.
    inline bool IsModelSpaceNormalMapName(std::string_view lowerPath) noexcept
    {
        return lowerPath.find("_msn") != std::string_view::npos;
    }

    /// A tangent-space normal map by its filename TAIL (`_n`, `_nm`, `_nrm`, `_norm`, `_normal`, ` norm` or ` normal`
    /// as HDT-SMP Hairpack names them, or a file named `normal`), never a substring: some NIFs put the diffuse in slot
    /// 1, and sampled as normals it renders the piece near-black. The path is lower-cased.
    inline bool IsTangentSpaceNormalMapName(std::string_view lowerPath) noexcept
    {
        if (IsModelSpaceNormalMapName(lowerPath)) return false;
        using detail::EndsWith;
        const auto base = detail::Basename(lowerPath);
        return EndsWith(lowerPath, "_n.dds") || EndsWith(lowerPath, "_n.bmp") ||
               EndsWith(lowerPath, "_nm.dds") || EndsWith(lowerPath, "_nrm.dds") ||
               EndsWith(lowerPath, "_norm.dds") || EndsWith(lowerPath, "_norm.bmp") ||
               EndsWith(lowerPath, "_normal.dds") || EndsWith(lowerPath, "_normal.bmp") ||
               EndsWith(lowerPath, " norm.dds") || EndsWith(lowerPath, " normal.dds") ||
               base == "normal.dds" || base == "normal.bmp";
    }
}
