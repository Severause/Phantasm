// Phantasm - native self-tests: ActorCollectCore (what the actor view leaves out of an actor's 3D).
//
// Author: Severause

#include "ActorCollectCore.h"
#include "TestHarness.h"

namespace Phantasm::Tests
{
    void RunActorCollectCoreTests()
    {
        namespace AC = Phantasm::ActorCollectCore;
        using A      = AC::Attachment;

        // The vanilla skeleton's attachment nodes.
        Check(AC::AttachmentOf("NPC R MagicNode [RMag]") == A::kMagicNode &&
                  AC::AttachmentOf("NPC L MagicNode [LMag]") == A::kMagicNode &&
                  AC::AttachmentOf("NPC Head MagicNode [Hmag]") == A::kMagicNode,
              "collect: the hand and head magic nodes hold runtime art");
        Check(AC::AttachmentOf("MagicEffectsNode") == A::kMagicEffectsNode, "collect: MagicEffectsNode holds runtime art");
        Check(AC::AttachmentOf("AnimObjectR") == A::kAnimObject && AC::AttachmentOf("animobjecta") == A::kAnimObject,
              "collect: AnimObject nodes hold an idle's prop, any case");
        Check(AC::AttachmentOf("DaggerBloodFX") == A::kBloodShell && AC::AttachmentOf("BloodLighting01") == A::kBloodShell,
              "collect: blood shells match anywhere in the name");
        Check(AC::AttachmentOf("EdgeBlood03") == A::kBloodShell && AC::AttachmentOf("edgeblood04") == A::kBloodShell,
              "collect: a weapon's edge blood shells, any case");

        // Worn gear and its attach nodes stay.
        Check(AC::AttachmentOf("WeaponStaff") == A::kNone && AC::AttachmentOf("WEAPON") == A::kNone &&
                  AC::AttachmentOf("SHIELD") == A::kNone && AC::AttachmentOf("NPC R Hand [RHnd]") == A::kNone,
              "collect: weapon attach nodes and the hand bones are not attachment nodes");
        Check(AC::AttachmentOf("StaffOrbGlow") == A::kNone && AC::AttachmentOf("MagicStaffRays") == A::kNone &&
                  AC::AttachmentOf("") == A::kNone,
              "collect: a staff's own glow pieces are not attachment nodes");

        // The hidden-root verdict.
        Check(AC::HonoursHiddenBelow(false, 40, 2), "collect: a visible root honours every flag below it");
        Check(AC::HonoursHiddenBelow(true, 3, 60),
              "collect: a root hidden alone (first person) still honours the flags below it");
        Check(!AC::HonoursHiddenBelow(true, 60, 0) && !AC::HonoursHiddenBelow(true, 58, 2),
              "collect: a distance fade's flags are not honoured");
        Check(!AC::HonoursHiddenBelow(true, 0, 0), "collect: a hidden root with no shapes honours nothing");

        // A skin partition's use.
        using P = AC::PartitionUse;
        Check(AC::PartitionUseOf(true, 0, true, false) == P::kEmpty &&
                  AC::PartitionUseOf(true, 0, false, false) == P::kEmpty,
              "partition: no triangles is empty whatever its buffers, never buffers owed");
        Check(AC::PartitionUseOf(true, 640, true, false) == P::kNoBuffers &&
                  AC::PartitionUseOf(true, 640, false, true) == P::kNoBuffers,
              "partition: triangles with a buffer missing are owed");
        Check(AC::PartitionUseOf(true, 640, true, true) == P::kDraw, "partition: triangles and both buffers draw");
        Check(AC::PartitionUseOf(false, 640, false, false) == P::kSkip &&
                  AC::PartitionUseOf(false, 0, false, false) == P::kSkip,
              "partition: no buffer data is left out uncounted");

        // A dyn shape's vertex space and height.
        Check(AC::IsHeadBoneLocal(true, 12.0f) && !AC::IsHeadBoneLocal(true, 120.0f) &&
                  !AC::IsHeadBoneLocal(true, 50.0f),
              "dyn shape: a centroid below 50 is head-bone-local, at or above it actor-root-local");
        Check(!AC::IsHeadBoneLocal(false, 12.0f), "dyn shape: an unreadable centroid is never head-bone-local");
        Check(AC::AtHeadHeight(1120.0f, 1000.0f) && !AC::AtHeadHeight(1050.0f, 1000.0f) &&
                  !AC::AtHeadHeight(1000.0f, 1000.0f),
              "dyn shape: a geom more than 50 above the actor sits at head height");

        using Drop  = AC::DynShapeDrop;
        using Place = AC::DynShapePlace;
        // An eye at the actor root (head-bone-local, not at head height).
        AC::DynShapeFacts eye;
        eye.actor         = true;
        eye.headBoneLocal = true;

        // Classic.
        auto f = eye;
        f.headBoneAnchor = true;
        Check(AC::DropOf(f) == Drop::kKeep && AC::PlaceOf(f) == Place::kHeadBoneAnchor,
              "dyn shape: classic, an eye at the root goes on the head-bone anchor");
        f = eye;
        f.dropHeadBoneLocal = true;
        Check(AC::DropOf(f) == Drop::kNoBindPoseHead, "dyn shape: classic with no bind-pose head drops an eye");
        f.headBoneLocal = false;
        Check(AC::DropOf(f) == Drop::kKeep && AC::PlaceOf(f) == Place::kOwnWorld,
              "dyn shape: classic with no bind-pose head keeps the head shape itself, on its own world");
        f = eye;
        f.dropHeadBoneLocal = true;
        f.palette           = true;
        Check(AC::DropOf(f) == Drop::kKeep, "dyn shape: a piece the palette skins is never dropped for a missing head");
        f = eye;
        f.atHeadHeight   = true;
        f.headBoneAnchor = true;
        Check(AC::PlaceOf(f) == Place::kOwnWorld,
              "dyn shape: a head-bone-local piece already at head height keeps its own world");
        f = eye;
        f.headBoneLocal  = false;
        f.headBoneAnchor = true;
        Check(AC::PlaceOf(f) == Place::kOwnWorld,
              "dyn shape: actor-root-local hair keeps its own world (an anchor would add the height twice)");
        f = eye;
        Check(AC::DropOf(f) == Drop::kKeep && AC::PlaceOf(f) == Place::kOwnWorld,
              "dyn shape: classic with no anchor and no drop flag keeps its own world");

        // As captured.
        f              = eye;
        f.capturedPose = true;
        f.liveHeadBone = true;
        f.headBoneAnchor = true;
        Check(AC::DropOf(f) == Drop::kKeep && AC::PlaceOf(f) == Place::kLiveHeadBone,
              "dyn shape: captured, an unskinned eye goes on the live head bone before the anchor");
        f.palette = true;
        Check(AC::PlaceOf(f) == Place::kHeadBoneAnchor,
              "dyn shape: captured and skinned, the live head bone is not taken");
        f              = eye;
        f.capturedPose = true;
        f.headBoneAnchor = true;
        Check(AC::PlaceOf(f) == Place::kHeadBoneAnchor,
              "dyn shape: captured with no live head bone, an unskinned eye takes the anchor");
        f              = eye;
        f.capturedPose = true;
        Check(AC::DropOf(f) == Drop::kNoAnchor, "dyn shape: captured, unskinned, with no anchor at all is dropped");
        f.dropHeadBoneLocal = true;
        Check(AC::DropOf(f) == Drop::kNoAnchor, "dyn shape: the classic drop flag does not apply as captured");
        f.atHeadHeight = true;
        Check(AC::DropOf(f) == Drop::kKeep, "dyn shape: captured, a piece already at head height is kept");
        f              = eye;
        f.capturedPose = true;
        f.actor        = false;
        Check(AC::DropOf(f) == Drop::kKeep && AC::PlaceOf(f) == Place::kOwnWorld,
              "dyn shape: outside an actor collect neither the captured drop nor the live head applies");

        Check(!AC::ClassicForUnbound(true, false, 0, 0, 5) && !AC::ClassicForUnbound(false, true, 0, 0, 0),
              "unbound: with no unbound skin the captured collect stands");
        Check(AC::ClassicForUnbound(true, false, 1, 0, 5), "unbound: a VR view takes the classic pose for any skin");
        Check(!AC::ClassicForUnbound(false, false, 1, 0, 5) && !AC::ClassicForUnbound(false, true, 1, 0, 5),
              "unbound: on flat a body under armour leaves the view and the portrait captured");
        Check(!AC::ClassicForUnbound(false, false, 2, 1, 5),
              "unbound: a live view keeps following with a head part unbound and other skins bound");

        using SB = AC::SelfBind;
        Check(AC::SelfBindsUnbound(60, 256, true, true, true, 60) == SB::kBinds,
              "self-bind: every bone linked on the root parent's scene binds the skin");
        Check(AC::SelfBindsUnbound(60, 256, true, true, true, 59) == SB::kNotLinked,
              "self-bind: one bone off the skeleton refuses the whole skin");
        Check(AC::SelfBindsUnbound(0, 256, true, true, true, 0) == SB::kNoBones,
              "self-bind: a skin naming no bone is not bound");
        Check(AC::SelfBindsUnbound(300, 256, true, true, true, 256) == SB::kTooMany,
              "self-bind: more bones than the palette holds are refused");
        Check(AC::SelfBindsUnbound(60, 256, false, true, true, 0) == SB::kNoBoneList &&
                  AC::SelfBindsUnbound(60, 256, true, false, true, 0) == SB::kNoScene &&
                  AC::SelfBindsUnbound(60, 256, true, true, false, 0) == SB::kNoBoneData,
              "self-bind: no bone list, no scene or no bone data refuses it");

        using Place = AC::DynShapePlace;
        Check(AC::LeavesOutUnbound(true, false, true, false, Place::kOwnWorld),
              "unbound: a captured live view leaves out an unbound armour skin (a wig's hairline)");
        Check(AC::LeavesOutUnbound(true, false, true, true, Place::kOwnWorld),
              "unbound: and an unbound FaceGen piece placed at its own world (a hair right after a rebuild)");
        Check(!AC::LeavesOutUnbound(true, false, true, true, Place::kLiveHeadBone) &&
                  !AC::LeavesOutUnbound(true, false, true, true, Place::kHeadBoneAnchor),
              "unbound: a head-local FaceGen piece keeps its head placement");
        Check(!AC::LeavesOutUnbound(true, false, false, false, Place::kOwnWorld),
              "unbound: a bound skin is drawn");
        Check(!AC::LeavesOutUnbound(false, false, true, false, Place::kOwnWorld) &&
                  !AC::LeavesOutUnbound(true, true, true, false, Place::kOwnWorld),
              "unbound: the classic pose and a portrait keep every piece");
        Check(AC::UnboundRecheckMs(true, false, 50) == 1000 && AC::UnboundRecheckMs(true, true, 50) == 1000,
              "unbound recheck: VR always 1 s");
        Check(AC::UnboundRecheckMs(false, true, 0) == 1000 &&
                  AC::UnboundRecheckMs(false, true, AC::kUnboundFastTries - 1) == 1000 &&
                  AC::UnboundRecheckMs(false, true, AC::kUnboundFastTries) == 5000,
              "unbound recheck: flat, a skin left out is asked for every second at first, then every 5 s");
        Check(AC::UnboundRecheckMs(false, true, AC::kUnboundMaxTries - 1) == 5000 &&
                  AC::UnboundRecheckMs(false, true, AC::kUnboundMaxTries) == 0,
              "unbound recheck: flat, a skin the game never binds is no longer asked for");
        Check(AC::UnboundRecheckMs(false, false, 0) == 5000,
              "unbound recheck: flat, a view held in the classic pose waits 5 s as before");
        Check(AC::NextUnboundTries(0, 2, 7) == 0 && AC::NextUnboundTries(2, 1, 7) == 1 &&
                  AC::NextUnboundTries(2, 0, 0) == 1,
              "unbound tries: none owed resets, a new number owed starts at 1");
        Check(AC::NextUnboundTries(2, 2, 3) == 3, "unbound tries: a collect does not count (each recheck adds one itself)");
        {
            using UP = AC::UnboundPlace;
            AC::UnboundFacts f;
            Check(AC::UnboundPlaceOf(f) == UP::kLeaveOut, "unbound place: an armour piece with nothing known is left out");
            f.hasShader = false;
            Check(AC::UnboundPlaceOf(f) == UP::kHidden, "unbound place: no shader is never drawn (a wig's SMP head)");
            f            = {};
            f.hairHidden = true;
            Check(AC::UnboundPlaceOf(f) == UP::kHidden, "unbound place: a head part the hair-hide drops is hidden");
            f              = {};
            f.skinMaterial = true;
            Check(AC::UnboundPlaceOf(f) == UP::kHidden, "unbound place: a reference body in an armour is hidden");
            f.engineDrew = false;
            Check(AC::UnboundPlaceOf(f) == UP::kLeaveOut,
                  "unbound place: on an actor the engine never drew, an armour's body skin (its arms) is not hidden");
            f.engineDrew = true;
            f.dynShape = true;
            Check(AC::UnboundPlaceOf(f) == UP::kLeaveOut, "unbound place: a FaceGen face right after a rebuild is left out");
            f              = {};
            f.hairMaterial = true;
            Check(AC::UnboundPlaceOf(f) == UP::kLeaveOut, "unbound place: a wig piece with no head bind is left out");
            f.headBind = true;
            Check(AC::UnboundPlaceOf(f) == UP::kRideHead, "unbound place: a wig's hairline rides the live head");
            f.dynShape = true;
            Check(AC::UnboundPlaceOf(f) == UP::kLeaveOut, "unbound place: a FaceGen hair waits for the engine instead");
        }
        Check(AC::LeftOutIncomplete(1, 1) && AC::LeftOutIncomplete(1, AC::kUnboundSettleTries) &&
                  !AC::LeftOutIncomplete(1, AC::kUnboundSettleTries + 1) && !AC::LeftOutIncomplete(0, 1),
              "unbound: a look with skins left out is incomplete for the first rechecks only");
        Check(AC::ClassicForUnbound(false, true, 2, 1, 5),
              "unbound: a portrait with a head part unbound takes the classic pose");
        Check(AC::ClassicForUnbound(true, true, 2, 1, 5) && !AC::ClassicForUnbound(true, true, 1, 0, 5),
              "unbound: a VR portrait follows the portrait rule");
        Check(AC::ClassicForUnbound(false, false, 30, 7, 0) && AC::ClassicForUnbound(false, true, 30, 0, 0),
              "unbound: with no skin bound a view and a portrait take the classic pose");
    }
}
