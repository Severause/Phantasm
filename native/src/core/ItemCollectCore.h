#pragma once

// Phantasm - ItemCollectCore: which objects of an item model the item view skips, as one rule for the item collector
// (MannequinRenderer.cpp's CollectItemGeometry) and for CountRealItemGeometry, whose zero sends the model to the
// private parse, so the two cannot disagree on "empty". Engine-free; tested by ItemCollectCoreTest.cpp.
//
// Author: Severause

#include "RendererMaterialCore.h"

#include <cstdint>

namespace Phantasm::ItemCollectCore
{
    /// A collect tier. honorHidden: an object the engine hides (kHidden), and everything under it, is skipped.
    /// permissive: decor quads and effect-shader meshes stay, and an engine-invisible piece draws at full alpha (the
    /// last tier, for a model that is nothing else). keepAnimatedEffects: effect-shader meshes stay because the view
    /// runs their effects (RendererFx); unanimated they are static placeholder plates.
    struct Tier
    {
        bool honorHidden         = true;
        bool permissive          = false;
        bool keepAnimatedEffects = false;
    };

    /// The collector's tiers, each tried only when the one before it collected nothing: hides honoured, hides
    /// ignored (some models load with everything under a hidden node), then permissive.
    inline constexpr Tier kTiers[3] = { { true, false, false }, { false, false, false }, { false, true, false } };

    /// CountRealItemGeometry's tier: the second (hides ignored, as the second tier draws hidden geometry), and no
    /// effect mesh counts, whatever the view would animate: a model of nothing but effect shells is the stripped
    /// model the private parse is for.
    inline constexpr Tier kCountTier{ false, false, false };

    /// What is known of any object of the tree, node or geometry.
    struct ObjectFacts
    {
        bool hidden       = false;   // kHidden
        bool editorMarker = false;   // named EditorMarker*
        bool bloodShell   = false;   // a blood / gore decal shell (ActorCollectCore::Attachment::kBloodShell)
    };

    /// What is known of a geometry from its own properties, without loading a texture.
    struct GeomFacts
    {
        bool          nonVisual    = false;   // collision and other shapes the engine never draws
        bool          refraction   = false;   // the shader property's Refraction flag: a distortion pass
        bool          glowMap      = false;   // a lighting glow-map material (kGlowMap)
        bool          hasDiffuse   = true;    // it names an authored diffuse (DiffuseIdentity)
        bool          effectShader = false;   // a BSEffectShaderProperty
        bool          blended      = false;   // its NiAlphaProperty blends
        bool          triShape     = false;   // a BSTriShape: `triangles` is its static count
        std::uint32_t triangles    = 0;       // 0 = unknown
        float         materialAlpha = 1.0f;   // the alpha the engine composites it with
    };

    enum class Skip
    {
        kNone,
        kHidden,
        kEditorMarker,
        kBloodShell,     // in every tier, the permissive one included: a blood shell is worse than the bare card
        kNonVisual,
        kRefraction,     // drawn as colour it paints a gradient disc; skipped in every tier
        kGlowOnly,       // a pure glow / aura mesh (RendererMaterialCore::GlowMapSkips); every tier
        kDecorQuad,
        kEffect,
        kInvisible,      // engine-invisible material alpha (hidden glass, an SMP collision body)
    };

    inline Skip ObjectSkip(const ObjectFacts& o, const Tier& t) noexcept
    {
        if (t.honorHidden && o.hidden) return Skip::kHidden;
        if (o.editorMarker) return Skip::kEditorMarker;
        if (o.bloodShell) return Skip::kBloodShell;
        return Skip::kNone;
    }

    /// A flat blended quad (at most two triangles: a shadow blob) draws as a black sheet. An unknown count is not one.
    inline bool IsDecorQuad(bool blended, bool triShape, std::uint32_t triangles) noexcept
    {
        return blended && triShape && triangles != 0 && triangles <= 2;
    }

    /// Whether a geometry that passed ObjectSkip is skipped in this tier.
    inline Skip GeomSkip(const GeomFacts& g, const Tier& t) noexcept
    {
        if (g.nonVisual) return Skip::kNonVisual;
        if (g.refraction) return Skip::kRefraction;
        if (g.glowMap && RendererMaterialCore::GlowMapSkips(g.hasDiffuse)) return Skip::kGlowOnly;
        const bool keepEffect = t.permissive || (t.keepAnimatedEffects && g.effectShader);
        if (!keepEffect && IsDecorQuad(g.blended, g.triShape, g.triangles)) return Skip::kDecorQuad;
        if (!keepEffect && g.effectShader) return Skip::kEffect;
        if (!t.permissive && RendererMaterialCore::IsEngineInvisible(g.materialAlpha)) return Skip::kInvisible;
        return Skip::kNone;
    }
}
