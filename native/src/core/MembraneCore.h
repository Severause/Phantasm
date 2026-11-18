#pragma once

// Phantasm - MembraneCore: a ghost's effect shader membrane on its own pieces, engine-free (native/build.ps1 -Tests).
// A ghost's ability keeps its effect shader (FXPersist: GhostFXShader), which draws over every piece it wears; the view
// draws its look at the shader's persistent alpha ratios, the state the game holds it in once its fade-in has played
// (MannequinRenderer's ReadActorMembrane reads the record, PS_Membrane draws it). Only a ghost's: the gains below are
// matched to one, and another actor's lasting shader is not drawn until it is matched too.
//
// The game's membrane, per pixel: a rim of the edge colour, (1 - N.V) to the edge falloff, plus the fill colour and
// the fill texture times the fill alpha, added over what is drawn. With projected UVs the fill texture is laid on by
// height over the figure (v: 0 at the feet, 1 at the head) and by angle about the figure (u).
//
// Author: Severause

#include <algorithm>
#include <cmath>

namespace Phantasm::MembraneCore
{
    struct Rgb
    {
        float r = 0.0f, g = 0.0f, b = 0.0f;
    };

    /// What the view reads of an effect shader record (TESEffectShader's DATA), colours 0-1 as the record stores them.
    struct ShaderFacts
    {
        bool  membrane = true;   // the record's membrane is on (no kDisableTextureShader)
        Rgb   edge;
        float edgeFalloff    = 1.0f;
        float edgePersistent = 0.0f;   // edgeEffectPersistentAlphaRatio
        Rgb   fillKeys[3];
        float fillKeyScale[3] = { 1.0f, 1.0f, 1.0f };
        float fillKeyTime[3]  = { 0.0f, 0.0f, 0.0f };
        float fillPersistent  = 0.0f;   // fillTextureEffectPersistentAlphaRatio
        bool  hasFillTexture  = false;
        bool  projectedUV     = false;   // kFillTexProjectedUV
        bool  skinOnly        = false;   // kSkinOnly
        bool  ignoreWeapons   = false;   // kIgnoreWeapons
        bool  edgeSubtractive = false;   // kEdgeColorSubtractive
        bool  greyscale       = false;   // kGreyscaleToColor or kGreyscaleToAlpha: the colour or alpha from a palette
    };

    /// How strongly the view draws a membrane's rim and fill against the record's alpha ratios. The game's are its HDR
    /// and bloom, which a view does not have: matched to an in-game shot of Holgeir (GhostFXShader) beside the view's.
    inline constexpr float kRimGain  = 4.5f;
    inline constexpr float kFillGain = 10.0f;
    /// A ghost's own pieces under its membrane: faint, so the scene shows through as in game (matched to the same shot).
    inline constexpr float kGhostBaseAlpha = 0.2f;
    /// The height the projected fill is laid over, in game units at figure scale 1 (a humanoid from feet to crown).
    inline constexpr float kFigureHeight = 130.0f;
    /// The diffuse alpha below which a blended or strand piece takes no membrane (its own cutout ref otherwise).
    inline constexpr float kSoftClip = 0.05f;
    /// The share of the rim a hair piece takes: its cards face every way, so a full rim lights most of them as edges and
    /// the hair comes out in bright flecks over a body the fill colours evenly (matched to the same shot).
    inline constexpr float kHairRimShare = 0.3f;

    /// The membrane the view draws over an actor.
    struct Look
    {
        bool  on = false;
        Rgb   rim;
        float rimStrength = 0.0f;
        float falloff     = 1.0f;
        Rgb   fill;
        float fillStrength = 0.0f;
        bool  projectedUV  = false;
        bool  skinOnly     = false;
        bool  ignoreWeapons = false;
        /// The actor's own pieces' alpha under it: kGhostBaseAlpha while on, 1 (drawn as they are) while off.
        float baseAlpha = 1.0f;
    };

    /// The fill colour once its keys have played: the key with the latest time (the first on a tie), times its scale.
    inline Rgb PersistentFill(const ShaderFacts& f) noexcept
    {
        int last = 0;
        for (int i = 1; i < 3; ++i) {
            if (f.fillKeyTime[i] > f.fillKeyTime[last]) last = i;
        }
        const float s = std::isfinite(f.fillKeyScale[last]) ? std::max(f.fillKeyScale[last], 0.0f) : 1.0f;
        return { f.fillKeys[last].r * s, f.fillKeys[last].g * s, f.fillKeys[last].b * s };
    }

    inline float Finite01(float v) noexcept { return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f; }

    /// The membrane at the shader's persistent alpha ratios, for a ghost (its record's IsGhost flag or ActorTypeGhost),
    /// whose own pieces are drawn faint under it. Off for anyone else, when the record's membrane is off, for a shader
    /// the view does not draw (a subtractive edge, a palette), and when neither its rim nor its fill would show.
    inline Look Persistent(const ShaderFacts& f, bool ghost) noexcept
    {
        Look l;
        if (!ghost || !f.membrane || f.edgeSubtractive || f.greyscale) return l;
        l.rim          = { Finite01(f.edge.r), Finite01(f.edge.g), Finite01(f.edge.b) };
        l.rimStrength  = Finite01(f.edgePersistent) * kRimGain;
        l.falloff      = std::isfinite(f.edgeFalloff) && f.edgeFalloff > 0.0f ? f.edgeFalloff : 1.0f;
        l.fill         = PersistentFill(f);
        l.fillStrength = f.hasFillTexture ? Finite01(f.fillPersistent) * kFillGain : 0.0f;
        l.projectedUV  = f.projectedUV;
        l.skinOnly     = f.skinOnly;
        l.ignoreWeapons = f.ignoreWeapons;
        const bool rimShows  = l.rimStrength > 0.0f && (l.rim.r + l.rim.g + l.rim.b) > 0.0f;
        const bool fillShows = l.fillStrength > 0.0f;
        l.on        = rimShows || fillShows;
        l.baseAlpha = l.on ? kGhostBaseAlpha : 1.0f;
        return l;
    }

    /// The projected v of a point at height z: 0 at the feet, 1 at feetZ + height, clamped. 0 for a height that is not
    /// positive.
    inline float ProjectedV(float z, float feetZ, float height) noexcept
    {
        if (!(height > 0.0f) || !std::isfinite(z) || !std::isfinite(feetZ)) return 0.0f;
        return std::clamp((z - feetZ) / height, 0.0f, 1.0f);
    }

    /// The projected u of a point (x, y) from the figure's centre: its angle about the up axis, 0 to 1 either way
    /// round (the game folds both halves onto one).
    inline float ProjectedU(float x, float y) noexcept
    {
        if (!std::isfinite(x) || !std::isfinite(y) || (x == 0.0f && y == 0.0f)) return 0.0f;
        return std::fabs(std::atan2(y, x)) / 3.14159265f;
    }
}
