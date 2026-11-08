#pragma once

// Phantasm - CastingArtCore: a readied spell's casting art in the actor view (MannequinRenderer.cpp's FindCastingArts,
// CastingSignatureOf and the casting-art particles), engine-free (native/build.ps1 -Tests): whether a hand's art is
// drawn, and the signature whose change while a view is open recollects it.
//
// Author: Severause

#include <cstddef>
#include <cstdint>

namespace Phantasm::CastingArtCore
{
    /// What is read of one hand's caster (ActorMagicCaster). The pointers are identities, compared and never followed.
    struct HandFacts
    {
        bool           caster      = false;   // the actor has this hand's caster
        bool           attached    = false;   // its kCastingArtAttached flag
        std::uint32_t  artFormId   = 0;       // its casting art: the caster's castingArt, else the held spell's (0: none)
        std::uintptr_t artRoot     = 0;       // castingArtData.attachedArt: the art's 3D root (0: none)
        std::uintptr_t magicNode   = 0;       // the caster's magic node
    };

    enum class Verdict
    {
        kDraw,
        kNoCaster,
        kNotReadied,     // no art attached: the hand holds no readied spell, or its art is still loading
        kWeaponsOff,     // the view draws no weapons (the hint, or a portrait): a spell is drawn with them
        kNotOnDrawn3D,   // the art or the magic node is not on the 3D the view draws (the player's first-person hands)
        kNoRoot,         // attached, but the art's root was not found under the magic node
    };

    inline const char* VerdictText(Verdict v) noexcept
    {
        switch (v) {
        case Verdict::kDraw:         return "drawn";
        case Verdict::kNoCaster:     return "no caster";
        case Verdict::kNotReadied:   return "no art attached";
        case Verdict::kWeaponsOff:   return "weapons not drawn";
        case Verdict::kNotOnDrawn3D: return "not on the drawn 3D";
        case Verdict::kNoRoot:       return "its root was not found under the magic node";
        default:                     return "?";
        }
    }

    /// Whether a hand's art is drawn. magicNodeOnDrawn: the caster's magic node is on the drawn 3D. rootFound: the
    /// art's root was found under that magic node (by attachedArt, or by the model's root name).
    inline Verdict HandVerdict(const HandFacts& h, bool drawWeapons, bool magicNodeOnDrawn, bool rootFound) noexcept
    {
        if (!h.caster) return Verdict::kNoCaster;
        if (!h.attached || h.artFormId == 0) return Verdict::kNotReadied;
        if (!drawWeapons) return Verdict::kWeaponsOff;
        if (h.magicNode == 0 || !magicNodeOnDrawn) return Verdict::kNotOnDrawn3D;
        if (!rootFound) return Verdict::kNoRoot;
        return Verdict::kDraw;
    }

    /// The hands' casting art as a signature (FNV-1a over each hand's art, flag, root and magic node), compared every
    /// frame against the one the collect recorded: a change (a spell readied, swapped, put away, or re-attached as a
    /// new clone) recollects. 0 while the view draws no weapons or no hand has art attached, so a hand with no spell
    /// never recollects anything.
    inline std::uint64_t Signature(const HandFacts* hands, std::size_t count, bool drawWeapons) noexcept
    {
        if (!drawWeapons || !hands) return 0;
        bool any = false;
        for (std::size_t i = 0; i < count; ++i) any = any || (hands[i].attached && hands[i].artFormId != 0);
        if (!any) return 0;
        std::uint64_t h   = 1469598103934665603ull;
        const auto    mix = [&h](std::uint64_t v) {
            for (int b = 0; b < 8; ++b) {
                h ^= (v >> (b * 8)) & 0xFFu;
                h *= 1099511628211ull;
            }
        };
        for (std::size_t i = 0; i < count; ++i) {
            const auto& f = hands[i];
            const bool  on = f.caster && f.attached && f.artFormId != 0;
            mix(on ? 1u : 0u);
            if (!on) continue;
            mix(f.artFormId);
            mix(f.artRoot);
            mix(f.magicNode);
        }
        return h ? h : 1;   // 0 is "nothing drawn"
    }
}
