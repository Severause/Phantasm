// Phantasm - native self-tests: RestPoseCore (the classic pose's skeleton at rest) and the actor view's figure scale
// (MirrorCameraCore::FigureScale).
//
// Author: Severause

#include "MirrorCameraCore.h"
#include "RestPoseCore.h"
#include "TestHarness.h"

#include <cmath>
#include <limits>

namespace Phantasm::Tests
{
    namespace
    {
        namespace RP = Phantasm::RestPoseCore;
        namespace MC = Phantasm::MirrorCameraCore;

        bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }
        bool Near(RP::P3 a, RP::P3 b, float eps = 1e-3f) { return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps); }

        RP::Xf At(float x, float y, float z, float s = 1.0f)
        {
            RP::Xf t;
            t.t[0] = x;
            t.t[1] = y;
            t.t[2] = z;
            t.s    = s;
            return t;
        }

        // A turn of `deg` about Z.
        RP::Xf TurnZ(float deg, float x, float y, float z)
        {
            const float r = deg * 3.14159265f / 180.0f;
            RP::Xf      t = At(x, y, z);
            t.rot[0][0] = std::cos(r);
            t.rot[0][1] = -std::sin(r);
            t.rot[1][0] = std::sin(r);
            t.rot[1][1] = std::cos(r);
            return t;
        }
    }

    void RunRestPoseCoreTests()
    {
        using LS = RP::LocalSource;

        // The local sources.
        Check(RP::ChooseLocalSource(true, false) == LS::kRestWithLiveScale && RP::ChooseLocalSource(true, true) == LS::kLive &&
                  RP::ChooseLocalSource(false, true) == LS::kLive && RP::ChooseLocalSource(false, false) == LS::kNone,
              "rest pose: a NIF node takes the NIF's rest, a helper its live local, any other none");
        Check(RP::IsUndrivenHelperNode("CME Neck [Neck]:481") && RP::IsUndrivenHelperNode("cme spine") &&
                  RP::IsUndrivenHelperNode("NPC") && RP::IsUndrivenHelperNode("npc"),
              "rest pose: the CME nodes and NPC are helpers, any case");
        Check(!RP::IsUndrivenHelperNode("NPC Root [Root]") && !RP::IsUndrivenHelperNode("NPC Head [Head]") &&
                  !RP::IsUndrivenHelperNode("CMEx") && !RP::IsUndrivenHelperNode("NP"),
              "rest pose: the driven bones are not helpers");
        Check(RP::IsUndrivenHelperNode("") && RP::ChooseLocalSource(false, RP::IsUndrivenHelperNode("")) == LS::kLive,
              "rest pose: an unnamed node takes its live local, its subtree goes on");
        Check(RP::IsHeadBoneKey("npc head [head]") && RP::IsHeadBoneKey("NPC Head [Head]") &&
                  RP::IsHeadBoneKey("NPC Head [HEad]") && RP::IsHeadBoneKey("NPC Head") && !RP::IsHeadBoneKey("npc head2") &&
                  !RP::IsHeadBoneKey("NPC Head MagicNode [Hmag]") && !RP::IsHeadBoneKey(""),
              "rest pose: both head spellings, any case");

        const float nan = std::numeric_limits<float>::quiet_NaN();
        Check(RP::UsableScale(nan) == 1.0f && RP::UsableScale(-1.0f) == 1.0f && RP::UsableScale(0.0f) == 0.0f &&
                  RP::UsableScale(0.8517f) == 0.8517f,
              "rest pose: a hidden node keeps scale 0, an unusable scale reads 1");

        // A female hand: the skeleton's hand node is scaled 0.8517 and the mesh binds it at 1.0, so a finger vertex
        // 5u past the wrist lands 0.8517 x 5u past it.
        {
            const RP::Xf forearm   = At(20.0f, 0.0f, 100.0f);
            const RP::Xf handNif   = At(10.0f, 0.0f, 0.0f);
            const RP::Xf handLive  = TurnZ(30.0f, 10.5f, 0.0f, 0.0f);   // posed by an animation
            RP::Xf       handLive2 = handLive;
            handLive2.s            = 0.8517f;
            const RP::Xf hand      = RP::Compose(forearm, RP::RestLocal(handNif, handLive2, LS::kRestWithLiveScale));
            const RP::Xf skinToBone = RP::Inverse(At(30.0f, 0.0f, 100.0f));   // the mesh's bind of the hand
            const RP::P3 v          = RP::Apply(RP::Compose(hand, skinToBone), { 35.0f, 0.0f, 100.0f });
            Check(Near(v, { 30.0f + 0.8517f * 5.0f, 0.0f, 100.0f }),
                  "rest pose: a 0.8517 hand under a 1.0 skinToBone shrinks about the wrist");
            const RP::P3 w = RP::Apply(RP::Compose(At(30.0f, 0.0f, 100.0f), skinToBone), { 35.0f, 0.0f, 100.0f });
            Check(Near(w, { 35.0f, 0.0f, 100.0f }), "rest pose: a bone at its bind places the vertex as authored");
        }

        // A seated NPC: the COM's live translation is the animation's, and the rest pose ignores it.
        {
            const RP::Xf root     = At(1000.0f, 2000.0f, 50.0f);
            const RP::Xf comNif   = At(0.0f, 0.0f, 68.0f);
            const RP::Xf standing = At(0.0f, 0.0f, 68.0f);
            const RP::Xf seated   = TurnZ(10.0f, 0.0f, 12.0f, 20.0f);
            const RP::Xf pelvis   = At(0.0f, 0.0f, 0.0f);
            const RP::Xf a = RP::Compose(RP::Compose(root, RP::RestLocal(comNif, standing, LS::kRestWithLiveScale)), pelvis);
            const RP::Xf b = RP::Compose(RP::Compose(root, RP::RestLocal(comNif, seated, LS::kRestWithLiveScale)), pelvis);
            Check(Near(RP::Apply(a, {}), RP::Apply(b, {})) && Near(RP::Apply(b, {}).z, 118.0f),
                  "rest pose: a seated COM leaves the rest pose standing");
        }

        // A race skeleton's helper (CME UBody t=(0,0,2), s=0.98) carries its offset and scale down the chain.
        {
            const RP::Xf spine  = At(0.0f, 0.0f, 80.0f);
            const RP::Xf ubody  = RP::Compose(spine, RP::RestLocal({}, At(0.0f, 0.0f, 2.0f, 0.98f), LS::kLive));
            const RP::Xf head   = RP::Compose(ubody, RP::RestLocal(At(0.0f, 0.0f, 40.0f), At(3.0f, 0.0f, 40.0f), LS::kRestWithLiveScale));
            Check(Near(RP::Apply(head, {}).z, 80.0f + 2.0f + 0.98f * 40.0f) && Near(head.s, 0.98f) &&
                      Near(RP::Apply(head, {}).x, 0.0f),
                  "rest pose: a helper's offset and scale propagate, the driven bone keeps the NIF's translation");
        }

        // Compose and Inverse agree.
        {
            RP::Xf x = TurnZ(47.0f, 3.0f, -4.0f, 9.0f);
            x.s      = 1.3f;
            const RP::P3 p{ 2.0f, 5.0f, -1.0f };
            Check(Near(RP::Apply(RP::Inverse(x), RP::Apply(x, p)), p) && RP::IsFinite(x) && !RP::IsFinite(At(nan, 0, 0)),
                  "rest pose: a transform and its inverse round-trip");
        }

        // A flattened bone tree, placed by its own parent links. The NIF: Root, COM (68 up), an unnamed node the
        // engine does not list (2 up, scale 0.98: a race's proportions), Pelvis, Head (50 up).
        {
            RP::NifSkeleton nif;
            nif.local  = { At(0, 0, 0), At(0, 0, 68.0f), At(0, 0, 2.0f, 0.98f), At(0, 0, 0), At(0, 0, 50.0f) };
            nif.parent = { -1, 0, 1, 2, 3 };
            const RP::Xf seatedCom = TurnZ(20.0f, 0.0f, 15.0f, 20.0f);   // the animation's COM, low and turned
            RP::Xf       headLive  = At(0, 0, 40.0f);
            headLive.s             = 0.9f;
            // Listed out of order (Head before its parent), a helper of no NIF node under the pelvis, a bone of no
            // rest local with a child, a parent index out of range, and a two-entry cycle.
            std::vector<RP::TreeEntry> e(10);
            e[0] = { -1, {}, 0, false, true };              // the tree node itself (Root)
            e[1] = { 4, headLive, 4, false, false };        // Head, under Pelvis (entry 4)
            e[2] = { 0, seatedCom, 1, false, false };       // COM
            e[3] = { 4, At(0, 0, 1.0f), -1, true, false };  // a CME helper the NIF lacks
            e[4] = { 2, At(0, 0, 0), 3, false, false };     // Pelvis, under COM: the unnamed node folded in
            e[5] = { 4, At(0, 0, 9.0f), -1, false, false }; // an SMP bone: no rest local
            e[6] = { 5, At(0, 0, 1.0f), -1, true, false };  // under it
            e[7] = { 40, At(0, 0, 1.0f), 3, false, false }; // parent out of range
            e[8] = { 9, At(0, 0, 1.0f), 3, false, false };  // cycle
            e[9] = { 8, At(0, 0, 1.0f), 3, false, false };
            std::vector<RP::Xf>      worlds;
            std::vector<signed char> placed;
            RP::PlaceTreeEntries(nif, e, At(100.0f, 0, 10.0f), 0, worlds, placed);
            Check(placed[0] == 1 && placed[1] == 1 && placed[2] == 1 && placed[3] == 1 && placed[4] == 1,
                  "flattened tree: every listed bone is placed, in any order");
            Check(Near(RP::Apply(worlds[2], {}), { 100.0f, 0.0f, 78.0f }) && Near(RP::Apply(worlds[4], {}).z, 80.0f) &&
                      Near(worlds[4].s, 0.98f),
                  "flattened tree: the seated COM stands, the unnamed node's offset and scale fold into the pelvis");
            Check(Near(RP::Apply(worlds[1], {}).z, 80.0f + 0.98f * 50.0f) && Near(worlds[1].s, 0.98f * 0.9f),
                  "flattened tree: the head takes the NIF's translation and its own live scale");
            Check(Near(RP::Apply(worlds[3], {}).z, 80.0f + 0.98f) && placed[5] == 0 && placed[6] == 0 &&
                      placed[7] == 0 && placed[8] == 0 && placed[9] == 0,
                  "flattened tree: a helper rides live; no rest local, a bad parent or a cycle stays unplaced");
        }

        // How far a rest world may sit from its bind.
        Check(RP::RestAgreesWithBind(30.0f, 120.0f) && RP::RestAgreesWithBind(99.0f, 120.0f) &&
                  !RP::RestAgreesWithBind(101.0f, 120.0f) && !RP::RestAgreesWithBind(45.0f, 0.0f) &&
                  !RP::RestAgreesWithBind(nan, 120.0f) && !RP::RestAgreesWithBind(10.0f, nan),
              "rest pose: a rest world near its bind is trusted, a far or unreadable one is not");

        // The figure scale.
        Check(Near(MC::FigureScale(120.0f, 1.0f), 1.0f) && Near(MC::FigureScale(96.0f, 0.8f), 0.8f) &&
                  Near(MC::FigureScale(249.0f, 1.0f), 249.0f / 120.0f),
              "figure scale: a humanoid is 1, a child smaller, a giant larger");
        Check(Near(MC::FigureScale(144.0f, 1.0f), 1.2f), "figure scale: RaceMenu's height under the root counts");
        Check(Near(MC::FigureScale(50.0f, 1.0f), 1.0f) && Near(MC::FigureScale(30.0f, 1.0f), 1.0f) &&
                  Near(MC::FigureScale(85.0f, 1.0f), 1.0f),
              "figure scale: a quadruped's or a lying or seated actor's low head never frames below the root scale");
        Check(MC::FigureScale(20.0f, 0.2f) == MC::kFigureScaleMin && MC::FigureScale(600.0f, 1.0f) == MC::kFigureScaleMax,
              "figure scale: clamped");
        Check(Near(MC::FigureScale(nan, 1.2f), 1.2f) && Near(MC::FigureScale(-5.0f, 0.9f), 0.9f) &&
                  Near(MC::FigureScale(60.0f, nan), 0.5f) && MC::FigureScale(nan, nan) == 1.0f &&
                  MC::FigureScale(0.0f, 0.0f) == 1.0f && MC::FigureScale(nan, 9.0f) == MC::kFigureScaleMax,
              "figure scale: without a head the root scale, without either 1");
        Check(MC::SettledFigureScale(1.0f, 1.03f) == 1.0f && MC::SettledFigureScale(1.0f, 0.8f) == 0.8f &&
                  MC::SettledFigureScale(0.0f, 1.03f) == 1.03f && MC::SettledFigureScale(nan, 1.2f) == 1.2f,
              "figure scale: a recollect keeps a scale within 5%, takes a real change");
    }
}
