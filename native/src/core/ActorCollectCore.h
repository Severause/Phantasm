#pragma once

// Phantasm - ActorCollectCore: what the actor view leaves out of an actor's 3D when it collects it (CollectActorGeometryStage
// in MannequinRenderer.cpp), engine-free (native/build.ps1 -Tests): the engine's runtime attachments, which are not the
// actor's look, and the engine-hidden parts.
//
// Author: Severause

#include "AsciiText.h"

#include <cstdint>
#include <string_view>

namespace Phantasm::ActorCollectCore
{
    namespace detail
    {
        // An empty part is never contained (AsciiText::ContainsI finds it everywhere).
        inline bool ContainsNoCase(std::string_view s, std::string_view part) noexcept
        {
            return !part.empty() && AsciiText::ContainsI(s, part);
        }

        inline bool StartsWithNoCase(std::string_view s, std::string_view prefix) noexcept
        {
            return AsciiText::StartsWithI(s, prefix);
        }
    }

    /// A node of an actor's 3D that the engine hangs runtime attachments under, by its name.
    enum class Attachment
    {
        kNone,
        kMagicNode,          // "NPC L/R MagicNode [LMag]", "NPC Head MagicNode [Hmag]": a readied spell's hand art
        kMagicEffectsNode,   // "MagicEffectsNode": spell, cloak and hit art
        kAnimObject,         // "AnimObjectA/B/L/R": an idle's prop (a tankard, a broom)
        kBloodShell,         // a blood decal shell ("DaggerBloodFX", "BloodLighting", a weapon's "EdgeBlood03")
    };

    /// What a node is by its name (any case). Worn armour and weapons hang under other nodes (the bones, the weapon
    /// attach nodes WEAPON, SHIELD, WeaponStaff and their kin, beside the magic nodes rather than under them), so a
    /// glow authored in a worn piece's own model is never under one of these. A blood shell's name is
    /// parent-qualified, so it is matched anywhere in the name.
    inline Attachment AttachmentOf(std::string_view name) noexcept
    {
        if (name.empty()) return Attachment::kNone;
        if (detail::ContainsNoCase(name, "bloodfx") || detail::ContainsNoCase(name, "bloodlighting") ||
            detail::ContainsNoCase(name, "edgeblood")) {
            return Attachment::kBloodShell;
        }
        if (detail::StartsWithNoCase(name, "MagicEffectsNode")) return Attachment::kMagicEffectsNode;
        if (detail::ContainsNoCase(name, "MagicNode")) return Attachment::kMagicNode;
        if (detail::StartsWithNoCase(name, "AnimObject")) return Attachment::kAnimObject;
        return Attachment::kNone;
    }

    inline const char* AttachmentName(Attachment a) noexcept
    {
        switch (a) {
        case Attachment::kMagicNode:        return "magic node";
        case Attachment::kMagicEffectsNode: return "magic effects node";
        case Attachment::kAnimObject:       return "anim object";
        case Attachment::kBloodShell:       return "blood shell";
        default:                            return "none";
        }
    }

    /// Whether a collect honours kHidden below the actor's root (never on the root itself). A visible root: always.
    /// A hidden root: when it reads as the engine hiding the root alone, as the player's third-person root while in
    /// first person, where the shapes below keep flags of their own (a hidden blood shell, an edge variant, a hidden
    /// duplicate body, a helmet a toggle hid): more shapes shown than hidden. Otherwise the flags are taken to be a
    /// fade's spread below the root and none is honoured, so the actor still draws. flagged / unflagged: the shapes
    /// below the root counted as the walk reads them, hidden when the shape or a node between it and the root
    /// carries kHidden. Which nodes a distance fade flags is not verified here; the collect logs both counts.
    inline bool HonoursHiddenBelow(bool rootHidden, std::uint32_t flagged, std::uint32_t unflagged) noexcept
    {
        if (!rootHidden) return true;
        return unflagged > flagged;
    }

    /// What one skin partition gives a collect (TryCollectSkinned, actor or item).
    enum class PartitionUse
    {
        kSkip,        // no buffer data at all: left out, uncounted
        kEmpty,       // no triangles: nothing to draw and nothing owed
        kNoBuffers,   // triangles whose vertex or index buffer is not there yet: the look stays incomplete
        kDraw,
    };

    /// The engine gives a partition with no triangles no index buffer, ever, so it is empty before the buffer test.
    /// Counted as missing buffers, it would leave every collect of that actor incomplete: the view would never freeze
    /// or keep the look as complete, and each burst would spend its one recollect on it.
    inline PartitionUse PartitionUseOf(bool buffData, std::uint32_t triangles, bool vertexBuffer,
                                       bool indexBuffer) noexcept
    {
        if (!buffData) return PartitionUse::kSkip;
        if (triangles == 0) return PartitionUse::kEmpty;
        if (!vertexBuffer || !indexBuffer) return PartitionUse::kNoBuffers;
        return PartitionUse::kDraw;
    }

    /// Game units. A skinned dyn shape (a FaceGen piece, hair) whose vertex centroid is below this height is in
    /// head-bone-local vertex space (eyes, brows, mouth, lashes); above it the vertices carry the head-height offset
    /// (actor-root-local: standard hair, the head). A geom whose own world is more than this far above the actor
    /// already sits at head height (parented to a head-attached node).
    inline constexpr float kHeadHeight = 50.0f;

    /// centroidKnown false: the centroid could not be read (no vertex copy).
    inline bool IsHeadBoneLocal(bool centroidKnown, float centroidZ) noexcept
    {
        return centroidKnown && centroidZ < kHeadHeight;
    }

    inline bool AtHeadHeight(float geomZ, float actorZ) noexcept
    {
        return geomZ > actorZ + kHeadHeight;
    }

    /// What decides where a skinned dyn shape goes (TryCollectSkinned; the engine work stays there).
    struct DynShapeFacts
    {
        bool actor             = false;   // an actor collect (false: the item view)
        bool capturedPose      = false;   // the view draws the pose as captured
        bool palette           = false;   // the skin's captured palette was built (it skins the piece)
        bool dropHeadBoneLocal = false;   // classic: no bind-pose head, and no live head bone / face-mesh anchor
                                          // pair agreeing on Z
        bool liveHeadBone      = false;   // the collect found the live head bone
        bool headBoneAnchor    = false;   // the collect has a head-bone anchor (its world)
        bool headBoneLocal     = false;   // IsHeadBoneLocal
        bool atHeadHeight      = false;   // AtHeadHeight
    };

    enum class DynShapeDrop
    {
        kKeep,
        kNoBindPoseHead,   // classic, dropHeadBoneLocal: a head-bone-local piece has no head to go on (the head
                           // shape itself, centroid ~+120, stays)
        kNoAnchor,         // captured, not skinned, at the actor root with no anchor at all: its own world is the
                           // actor's feet
    };

    /// Whether the collect drops the piece (it still counts as skinned).
    inline DynShapeDrop DropOf(const DynShapeFacts& f) noexcept
    {
        if (!f.palette && !f.capturedPose && f.dropHeadBoneLocal && f.headBoneLocal) return DynShapeDrop::kNoBindPoseHead;
        if (f.actor && f.capturedPose && !f.palette && !f.liveHeadBone && !f.headBoneAnchor && f.headBoneLocal &&
            !f.atHeadHeight) {
            return DynShapeDrop::kNoAnchor;
        }
        return DynShapeDrop::kKeep;
    }

    /// Whether a captured collect that drew `unbound` skins the engine has not bound to its skeleton yet is drawn again
    /// in the classic pose, so that nothing stays at its bind placement apart from what the bones moved. Always on VR
    /// outside a portrait, where only the paused player rebuild leaves drawn armour unbound. On SE and AE one unbound
    /// skin is usually one the engine never binds (a body under armour), so a view takes it only with no skin bound
    /// at all (an NPC the game has not drawn yet), and a portrait also when a head part is unbound: its hair and face
    /// would part from the head. A hidden head part's false alarm costs a portrait only its idle.
    inline bool ClassicForUnbound(bool vr, bool portrait, int unbound, int unboundHeadParts, int bound) noexcept
    {
        if (unbound <= 0) return false;
        if (vr && !portrait) return true;
        return bound <= 0 || (portrait && unboundHeadParts > 0);
    }

    /// SelfBindsUnbound's verdict.
    enum class SelfBind
    {
        kBinds,
        kNoBones,      // the skin data names no bone
        kTooMany,      // more bones than the palette holds
        kNoBoneList,
        kNoScene,      // the skin's root parent is not in a scene
        kNoBoneData,
        kNotLinked,    // a bone is null, unreadable, off the root parent's scene or not finite
    };

    /// Whether a skin the engine has not bound yet (no palette) is skinned on its own bone list instead
    /// (FillUnboundPalette). The engine links that list when the skin is attached, one entry per skin-data bone, but
    /// fills the palette only when it draws the shape. count: the skin data's bones; linked: how many of them, from
    /// the first, passed every test.
    inline SelfBind SelfBindsUnbound(std::uint32_t count, std::uint32_t maxBones, bool boneList, bool inScene,
                                     bool boneData, std::uint32_t linked) noexcept
    {
        if (count == 0) return SelfBind::kNoBones;
        if (count > maxBones) return SelfBind::kTooMany;
        if (!boneList) return SelfBind::kNoBoneList;
        if (!inScene) return SelfBind::kNoScene;
        if (!boneData) return SelfBind::kNoBoneData;
        return linked == count ? SelfBind::kBinds : SelfBind::kNotLinked;
    }

    enum class DynShapePlace
    {
        kLiveHeadBone,     // captured, not skinned, head-bone-local at the actor root: on the live head bone, with the
                           // posed head, not on the bind-pose anchor
        kHeadBoneAnchor,   // head-bone-local at the actor root (the BSFaceGenNiNode: brows, eyes, mouth): on the
                           // head-bone anchor
        kOwnWorld,         // everything else: its own world (an actor-root-local centroid carries the head height
                           // already, and a head-bone anchor would add it twice; a piece the palette skins takes it
                           // for its sort centre and any unskinned partition; an unreadable centroid)
    };

    /// Where a piece DropOf keeps goes.
    inline DynShapePlace PlaceOf(const DynShapeFacts& f) noexcept
    {
        if (f.capturedPose && !f.palette && f.actor && f.headBoneLocal && !f.atHeadHeight && f.liveHeadBone) {
            return DynShapePlace::kLiveHeadBone;
        }
        if (f.headBoneLocal && !f.atHeadHeight && f.headBoneAnchor) return DynShapePlace::kHeadBoneAnchor;
        return DynShapePlace::kOwnWorld;
    }

    /// What a captured live view does with a skin the engine has not bound to its skeleton (IsUnboundSkin).
    enum class UnboundPlace
    {
        kHidden,     // the game does not draw it either: not drawn, not owed, never watched
        kRideHead,   // drawn rigid on the live head, placed from the head bind of the skins it was authored with
        kLeaveOut,   // left out (LeavesOutUnbound) and watched until the engine binds it
    };

    struct UnboundFacts
    {
        bool hasShader    = true;    // no shader property: never drawn (an SMP collision shape such as a wig's head)
        bool hairHidden   = false;   // the hair-hide drops it (a head part a worn wig or helmet covers)
        bool skinMaterial = false;   // a body-skin material (FaceGen tint)
        bool dynShape     = false;   // a FaceGen part
        bool hairMaterial = false;   // a hair-shader material
        bool headBind     = false;   // the collect armed a head re-parent (a bind-pose head for the authored frame)
        bool engineDrew   = true;    // the engine has bound some skin of the actor (it has drawn it at least once)
    };

    /// A skin the engine never binds is never seen in game: a reference body an armour carries under its own pieces
    /// stays unbound for the whole session and shows nowhere. So a piece the game would not draw anyway is kHidden: no
    /// shader, dropped by the hair-hide, or body skin on an armour piece (not a FaceGen part, which a rebuild leaves
    /// unbound for a moment). That reading needs an actor the engine has drawn: on one it never drew (placed hidden,
    /// or while the game is paused) every skin is unbound and an armour's arms are as unbound as its hidden body, so
    /// body skin is not hidden there. An armour's hair-shader piece (a wig's hairline) is kRideHead: its rest
    /// placement is in the bind frame, so moved by inverse(bind head) x live head it sits on the head and follows it.
    /// Anything else is kLeaveOut: drawn rigid at its rest it would float when the body moves (a hair right after a
    /// rebuild).
    inline UnboundPlace UnboundPlaceOf(const UnboundFacts& f) noexcept
    {
        if (!f.hasShader || f.hairHidden || (f.skinMaterial && !f.dynShape && f.engineDrew)) {
            return UnboundPlace::kHidden;
        }
        if (!f.dynShape && f.hairMaterial && f.headBind) return UnboundPlace::kRideHead;
        return UnboundPlace::kLeaveOut;
    }

    /// Whether a captured live view leaves out a skin the engine has not bound to its skeleton yet (IsUnboundSkin)
    /// rather than draw it rigid at its own node: no bone moves such a piece, so a pose or a clip leaves it at its
    /// bind placement (a hair beside the head, or left in the air when the body lies down). A head-local FaceGen piece
    /// still goes on the live head bone (kLiveHeadBone), which a clip moves; a portrait keeps every piece, and
    /// ClassicForUnbound decides its pose. The view looks at the skin again (UnboundRecheckMs) and collects only once
    /// the engine has bound it, so a recheck that finds nothing new changes nothing on screen.
    inline bool LeavesOutUnbound(bool captured, bool portrait, bool unbound, bool dynShape, DynShapePlace place) noexcept
    {
        if (!captured || portrait || !unbound) return false;
        return !dynShape || place == DynShapePlace::kOwnWorld;
    }

    /// Rechecks of a view owing unbound skins that come at the short interval before the long one, and how many in a
    /// row a view that left skins out makes in all. A recheck reads the watched skins in the 3D; only one that finds a
    /// skin bound collects again.
    inline constexpr int kUnboundFastTries = 10;
    inline constexpr int kUnboundMaxTries  = 20;

    /// How long a view owing unbound skins waits before its next recheck, in milliseconds, after `tries` rechecks
    /// in a row; 0 = no more. VR: always 1 s (the paused player rebuild binds when the game runs). Flat: a view that
    /// left skins out asks every second for the first kUnboundFastTries (an NPC's are bound within a few frames of a
    /// rebuild), then every 5 s, and stops at kUnboundMaxTries (a skin the game never binds, a body under armour, stays
    /// out; an equip collects again anyway); a view held in the classic pose for an NPC the game has not drawn asks
    /// every 5 s for as long as it stays open.
    inline int UnboundRecheckMs(bool vr, bool leftOut, int tries) noexcept
    {
        if (vr) return 1000;
        if (!leftOut) return 5000;
        if (tries >= kUnboundMaxTries) return 0;
        return tries < kUnboundFastTries ? 1000 : 5000;
    }

    /// Rechecks (one a second) during which a look with skins left out still counts as incomplete: it is not kept as
    /// the actor's last look and does not freeze. An NPC's skins are bound within a few frames of a rebuild; one still
    /// unbound after this is likely never bound (a body under armour, hidden anyway), so the look counts as complete.
    inline constexpr int kUnboundSettleTries = 3;

    /// Whether a collect that left `leftOut` skins out, after `tries` rechecks in a row, is still incomplete.
    inline bool LeftOutIncomplete(int leftOut, int tries) noexcept
    {
        return leftOut > 0 && tries <= kUnboundSettleTries;
    }

    /// The recheck count after a collect owing `owed` skins: 0 when none are owed, 1 when the number owed changed (a
    /// new episode: a rebuild left others unbound), else unchanged. A collect never counts (a texture fill-in or an
    /// equip's collect is not a recheck); each recheck adds one itself.
    inline int NextUnboundTries(int owed, int owedBefore, int triesBefore) noexcept
    {
        if (owed == 0) return 0;
        if (owed != owedBefore) return 1;
        return triesBefore;
    }
}
