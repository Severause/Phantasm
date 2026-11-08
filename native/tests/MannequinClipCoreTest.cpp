// Phantasm - native self-tests: MannequinClipCore (the actor view's clip player: idle families, a take's
// window and clock, the idle cycle, the follow of the engine's clip and the shared idle pool).
//
// Author: Severause

#include "MannequinClipCore.h"
#include "TestHarness.h"

#include <cmath>

namespace Phantasm::Tests
{
    void RunMannequinClipCoreTests()
    {
        namespace MC = Phantasm::MannequinClipCore;
        using MC::ClipMode;
        using MC::Follow;
        const auto near = [](float a, float b) { return std::fabs(a - b) < 1e-4f; };

        // 1. Modes: the engine's byte, and what loops
        Check(MC::FromEngineMode(0) == ClipMode::kToEnd && MC::FromEngineMode(2) == ClipMode::kHeld,
              "clip: engine modes map in order");
        Check(MC::FromEngineMode(4) == ClipMode::kLoop && MC::FromEngineMode(255) == ClipMode::kLoop,
              "clip: an out-of-range engine mode loops");
        Check(MC::IsLooping(ClipMode::kLoop) && MC::IsLooping(ClipMode::kPingPong) && !MC::IsLooping(ClipMode::kToEnd) &&
                  !MC::IsLooping(ClipMode::kHeld),
              "clip: loop and ping-pong are base idles");

        // 2. Transitions
        Check(MC::IsTransitionClip("Actors\\Character\\Animations\\ChairIdleStart.hkx"), "clip: ...Start.hkx is a transition");
        Check(MC::IsTransitionClip("AnimObjectChairEatingIdleExit2.HKX"), "clip: ...Exit2 is a transition, any case");
        Check(MC::IsTransitionClip("chair_enter"), "clip: ...enter is a transition");
        Check(!MC::IsTransitionClip("mt_idle.hkx") && !MC::IsTransitionClip("IdleStretch.hkx"),
              "clip: an idle is not a transition");
        Check(!MC::IsTransitionClip(""), "clip: an empty name is not a transition");
        Check(MC::IsTransitionClip("Animations\\MT_Idle_A_to_B_Trans.hkx"), "clip: the human A-to-B idle change is a transition");
        Check(MC::IsTransitionClip("Animations\\MT_IdleToAttackCombatState.hkx"),
              "clip: the Vampire Lord's idle-to-attack is a transition");
        Check(MC::IsTransitionClip("Animations\\idle_graze_stop.hkx") && MC::IsTransitionClip("dog_spice_sit_stop.hkx"),
              "clip: a creature's ...Stop is a transition");
        Check(!MC::IsTransitionClip("Animations\\idle_graze_loop.hkx") && !MC::IsTransitionClip("ww_idlebase.hkx"),
              "clip: a creature's idle is not a transition");

        // 3. Idle families
        Check(MC::IdleFamily("meshes\\actors\\character\\animations\\MT_Idle.hkx") == "mt_idle", "family: MT_Idle");
        Check(MC::IdleFamily("MT_IdleA_Var2.hkx") == "mt_idle", "family: an MT_Idle variation is the standing family");
        Check(MC::IdleFamily("OAR\\_variants_mt_idle\\anything.hkx") == "mt_idle", "family: a replacer's variants folder");
        Check(MC::IdleFamily("x\\_variants_chair_idlebase\\a.hkx") == "chair_idle", "family: a clip's variants folder");
        Check(MC::IdleFamily("Chair_IdleBaseVar1.hkx") == "chair_idle", "family: Var and Base and the number go");
        Check(MC::IdleFamily("AnimObjectChairEatingIdleStart.hkx") == "animobjectchaireatingidle",
              "family: a transition's family is the idle it leads into");
        Check(MC::IdleFamily("npc_applaud2.hkx") == "npc_applaud", "family: a trailing number goes");
        Check(MC::IdleFamily("Idle1.hkx").empty(), "family: under six characters names no family");
        Check(MC::IdleFamily("Animations\\idle.hkx", true) == "idle" && MC::IdleFamily("Animations\\idle.hkx").empty(),
              "family: the horse's Idle names a family in a creature's project only");
        Check(MC::IsCharacterProject("DefaultFemale") && MC::IsCharacterProject("defaultmale") &&
                  !MC::IsCharacterProject("HorseProject") && !MC::IsCharacterProject("WerewolfBeastProject"),
              "family: the humanoid projects are the character's");
        Check(MC::IdleFamily("Animations\\ww_idlebase.hkx", true) == "ww_idle", "family: the werewolf's standing idle");
        Check(MC::IdleFamily("Animations\\idle_graze_stop.hkx", true) == "idle_graze",
              "family: a ...Stop clip's family is the idle it leaves");
        Check(MC::IsLocomotionClip("Animations\\WalkForward.hkx") && MC::IsLocomotionClip("runforwardl.hkx") &&
                  MC::IsLocomotionClip("trotforward_wolf.hkx") && MC::IsLocomotionClip("TurnCannedR90.hkx") &&
                  MC::IsLocomotionClip("sprintforward.hkx") && MC::IsLocomotionClip("swimf.hkx") &&
                  MC::IsLocomotionClip("jumpstart_sprint.hkx"),
              "family: the creatures' walk, run, trot, turn, sprint, swim and jump clips move them");
        Check(!MC::IsLocomotionClip("idle_rearup.hkx") && !MC::IsLocomotionClip("idle_headshake.hkx") &&
                  !MC::IsLocomotionClip("mt_idle.hkx") && !MC::IsLocomotionClip("dog_spice_sit_idle.hkx"),
              "family: an idle never moves them");
        Check(MC::IdleFamily("Animations\\swimf.hkx", true).empty() && MC::IdleFamily("Animations\\walkforward.hkx", true).empty(),
              "family: a creature's locomotion names no family");
        Check(MC::IdleFamily("Animations\\walkforward.hkx") == "walkforward", "family: a character's names keep theirs");
        Check(MC::IsInteractionIdle("Animations\\idle_pet.hkx") && !MC::IsInteractionIdle("idle_pawing.hkx") &&
                  !MC::IsInteractionIdle("idle_head1.hkx"),
              "family: petting is an interaction, pawing is not");
        Check(MC::LeadInOf("Animations\\Idle_Graze_Loop.hkx") == "idle_graze_start.hkx" && MC::LeadInOf("idle_head1.hkx").empty(),
              "family: a looping middle part's lead-in");

        // 3b. Replacer sets
        const char* sheoA   = "Meshes\\Actors\\Character\\Animations\\OpenAnimationReplacer\\NPCAR\\1000Sheo\\male\\mt_idlelookinga.hkx";
        const char* sheoSet = "meshes\\actors\\character\\animations\\openanimationreplacer\\npcar\\1000sheo\\";
        const char* lca     = "meshes/actors/character/animations/OpenAnimationReplacer/LCA/00playidlH/female/_variants_mt_idle/idlelaugh.hkx";
        const char* lcaSet  = "meshes\\actors\\character\\animations\\openanimationreplacer\\lca\\00playidlh\\";
        Check(MC::ReplacerSetOf(sheoA) == sheoSet, "replacer: an OAR file's set is its mod and submod, lower case");
        Check(MC::ReplacerSetOf(lca) == lcaSet, "replacer: a _variants_ file under a sex folder is its submod's set");
        Check(MC::ReplacerSetOf("Animations\\DynamicAnimationReplacer\\_CustomConditions\\5000\\male\\mt_idle.hkx") ==
                  "animations\\dynamicanimationreplacer\\_customconditions\\5000\\",
              "replacer: a DAR custom-conditions set is its id");
        Check(MC::ReplacerSetOf("Animations\\DynamicAnimationReplacer\\Skyrim.esm\\00013BBF\\mt_idle.hkx") ==
                  "animations\\dynamicanimationreplacer\\skyrim.esm\\00013bbf\\",
              "replacer: a DAR form set is its plugin and form");
        Check(MC::ReplacerSetOf("Animations\\male\\MT_Idle.hkx").empty() && MC::ReplacerSetOf("").empty(),
              "replacer: a file of no replacer has no set");
        Check(MC::ReplacerSetOf("x\\OpenAnimationReplacer\\Mod\\a.hkx") == "x\\openanimationreplacer\\mod\\",
              "replacer: a file too shallow for a submod is its folder's set");
        Check(MC::ReplacerSetOf("x\\NotOpenAnimationReplacer\\Mod\\Sub\\a.hkx").empty(),
              "replacer: the root must be a whole folder name");
        Check(MC::ReplacerSetName(sheoSet) == "1000sheo" && MC::ReplacerSetName("").empty() &&
                  MC::ReplacerSetName(MC::kSetUnknown).empty(),
              "replacer: a set's name is its last folder");

        // 3c. Who joins the playing clip's idle cycle
        {
            using MC::Member;
            const auto vanilla = [](const char* f, bool female = false) {
                return MC::FamilyMember("mt_idle", "mt_idle", "", f, female);
            };
            Check(vanilla("Animations\\MT_Idle_A_Left_Long.hkx") == Member::kShot, "member: a vanilla MT_Idle one-shot");
            Check(vanilla(sheoA) == Member::kNone, "member: NPCAR's conditional idle never joins a vanilla idle");
            Check(vanilla(lca, true) == Member::kNone, "member: a child's play idle never joins an adult's");
            Check(MC::FamilyMember("mt_idle", "mt_idle", sheoSet, sheoA, false) == Member::kShot,
                  "member: an idle of the set that plays joins it");
            Check(MC::FamilyMember("mt_idle", "mt_idle", lcaSet, lca, true) == Member::kLoop,
                  "member: the playing set's _variants_mt_idle are base loops");
            Check(MC::FamilyMember("mt_idle", "mt_idle", lcaSet, lca, false) == Member::kNone,
                  "member: a replacer's folder for the other sex never joins");
            Check(MC::FamilyMember("mt_idle", "mt_idle", sheoSet,
                                   "meshes\\actors\\character\\animations\\openanimationreplacer\\npcar\\1004fakeint\\male\\mt_idlelookinga.hkx",
                                   false) == Member::kNone,
                  "member: another set of the same mod never joins");
            Check(vanilla("Animations\\MT_Idle_A_to_B_Trans.hkx") == Member::kNone, "member: a transition never joins");
            Check(MC::FamilyMember("ww_idle", "ww_idlebase", "", "Animations\\ww_idlebasecombat.hkx", false) == Member::kNone &&
                      MC::FamilyMember("ww_idle", "ww_idlebase", "", "Animations\\WW_IdleBaseCombatExcitedLeft.hkx", false) ==
                          Member::kNone,
                  "member: the werewolf's combat stances leave its standing cycle");
            Check(MC::FamilyMember("ww_idlebasecombat", "ww_idlebasecombat", "", "Animations\\ww_idlebasecombatexcitedleft.hkx",
                                   false) == Member::kLoop,
                  "member: a combat family keeps its combat stances");
            Check(MC::FamilyMember("mt_idle", "mt_idle", "", "Animations\\MT_IdleToAttackCombatState.hkx", false) == Member::kNone,
                  "member: the Vampire Lord's idle-to-attack never joins");
            // The horse plays Idle.hkx (HorseProject: a creature).
            const auto horse = [](const char* f, bool leadIn = false) {
                return MC::FamilyMember("idle", "idle", "", f, false, true, leadIn);
            };
            Check(horse("Animations\\idle_head1.hkx") == Member::kShot && horse("Animations\\idle_tail6.hkx") == Member::kShot &&
                      horse("Animations\\idle_pawing.hkx") == Member::kShot && horse("Animations\\idle_rearup.hkx") == Member::kShot,
                  "member: a creature's other idles join as one-shots, never base loops");
            Check(horse("Animations\\idle_graze_start.hkx") == Member::kNone && horse("Animations\\idle_graze_stop.hkx") == Member::kNone,
                  "member: the horse's graze transitions never join");
            Check(horse("Animations\\idle_graze_loop.hkx", true) == Member::kNone,
                  "member: a looping middle part whose lead-in the set holds never joins");
            Check(horse("Animations\\idle_graze_loop.hkx", false) == Member::kShot,
                  "member: a loop with no lead-in in the set joins as a one-shot");
            Check(horse("Animations\\idle_pet.hkx") == Member::kNone, "member: the horse's petting idle never joins");
            Check(MC::FamilyMember("mt_idle", "mt_idle", "", "Animations\\mt_idle_wolf.hkx", false, true) == Member::kShot &&
                      MC::FamilyMember("mt_idle", "mt_idle", "", "Animations\\mt_idle.hkx", false, true) == Member::kShot,
                  "member: a dog's standing idles join as one-shots");
            Check(MC::FamilyMember("idle", "idle", "", "Animations\\idle_head1.hkx", false) == Member::kLoop,
                  "member: a character's family still joins as base loops");
            Check(MC::FamilyMember("", "x", "", "Animations\\idle_head1.hkx", false) == Member::kNone,
                  "member: no family, no members");
        }

        // 4. Windows
        {
            const auto w = MC::ChooseWindow(ClipMode::kLoop, 4.0f, 1.5f);
            Check(w.mode == ClipMode::kLoop && w.baseLoop && near(w.windowStart, 0) && near(w.windowSec, 4) &&
                      near(w.startSec, 1.5f),
                  "window: a short base loop is baked whole, its clock where the clip is");
        }
        {
            const auto w = MC::ChooseWindow(ClipMode::kLoop, 90.0f, 70.0f);
            Check(w.mode == ClipMode::kToEnd && w.baseLoop && near(w.windowStart, 70) && near(w.windowSec, 60) &&
                      near(w.startSec, 0),
                  "window: a long base loop bakes kBakeMaxSec from where it is");
            Check(near(MC::FrameTime(w.windowStart, w.windowSec, MC::FrameCount(w.windowSec) - 1, MC::FrameCount(w.windowSec), 90.0f), 40.0f),
                  "window: a long loop's window wraps past the clip's end once");
        }
        {
            const auto w = MC::ChooseWindow(ClipMode::kToEnd, 90.0f, 50.0f);
            Check(w.mode == ClipMode::kToEnd && !w.baseLoop && near(w.windowStart, 30) && near(w.startSec, 20),
                  "window: a long one-shot near its end bakes the last kBakeMaxSec");
        }
        {
            const auto w = MC::ChooseWindow(ClipMode::kHeld, 3.0f, 1.25f);
            Check(w.mode == ClipMode::kHeld && near(w.windowStart, 1.25f) && near(w.windowSec, 0) && !w.baseLoop,
                  "window: a held clip is its current frame");
            Check(MC::FrameCount(w.windowSec) == 2, "window: a held window bakes two frames");
        }
        Check(MC::FrameCount(1.0f) == 21 && MC::FrameCount(60.0f) == 1201, "window: frames at kBakeRate, both ends");

        // 5. The clock
        Check(near(MC::WindowTime(ClipMode::kLoop, 2.0f, 0.0f, 5.0f), 1.0f), "clock: a loop wraps");
        Check(near(MC::WindowTime(ClipMode::kLoop, 2.0f, 0.0f, -0.5f), 1.5f), "clock: a loop played backwards wraps");
        Check(near(MC::WindowTime(ClipMode::kPingPong, 2.0f, 0.0f, 3.0f), 1.0f), "clock: ping-pong comes back");
        Check(near(MC::WindowTime(ClipMode::kToEnd, 2.0f, 0.0f, 9.0f), 2.0f), "clock: a one-shot holds its end");
        Check(near(MC::WindowTime(ClipMode::kHeld, 0.0f, 0.0f, 9.0f), 0.0f), "clock: a held take never moves");
        Check(!std::isnan(MC::WindowTime(ClipMode::kLoop, 0.0f, 0.0f, 3.0f)), "clock: an empty window divides by nothing");
        Check(MC::TakeEnded(ClipMode::kToEnd, 2.0f, 2.0f, 1.0f) && !MC::TakeEnded(ClipMode::kToEnd, 2.0f, 1.9f, 1.0f),
              "clock: a one-shot ends at its window's end");
        Check(MC::TakeEnded(ClipMode::kToEnd, 2.0f, 0.0f, -1.0f), "clock: a one-shot played backwards ends at its start");
        Check(!MC::TakeEnded(ClipMode::kLoop, 2.0f, 50.0f, 1.0f), "clock: a loop never ends");
        Check(near(MC::AnimTime(0.5f, 4.0f, 3.0f, 2.0f), 1.5f), "clock: the animation time wraps the cropped range");
        Check(near(MC::WindowTimeAt(0.5f, 4.0f, 3.0f, 3.0f, 1.5f), 2.0f) &&
                  near(MC::WindowTimeAt(0.5f, 4.0f, 0.0f, 4.0f, 2.5f), 2.0f),
              "clock: an animation time back into its window, across the wrap");
        Check(near(MC::WindowTimeAt(0.0f, 9.5f, 0.0f, 2.0f, 5.0f), 2.0f), "clock: a time past the window holds its end");
        {
            // A 90 s base loop baked 60 s from 70 s: the window runs 70..90 then 0..40.
            const auto w = MC::ChooseWindow(ClipMode::kLoop, 90.0f, 70.0f);
            Check(near(MC::WindowOffset(0.0f, 90.0f, w.windowStart, w.baseLoop, 80.0f), 10.0f),
                  "offset: a long loop before its wrap");
            Check(near(MC::WindowOffset(0.0f, 90.0f, w.windowStart, w.baseLoop, 5.0f), 25.0f),
                  "offset: the engine wrapped the long loop: still inside its window");
            const float out = MC::WindowOffset(0.0f, 90.0f, w.windowStart, w.baseLoop, 50.0f);
            Check(out > w.windowSec, "offset: past the wrapped window is outside it");
            Check(MC::WindowOffset(0.5f, 90.0f, 30.0f, false, 10.5f) < 0.0f,
                  "offset: a one-shot's time before its window is outside it, never wrapped");
            Check(near(MC::WindowOffset(0.5f, 4.0f, 0.0f, true, 2.5f), 2.0f), "offset: a short loop is its time less the crop");
        }
        {
            const auto p = MC::FrameAt(1.0f, 2.0f, 41);
            Check(p.f0 == 20 && p.f1 == 21 && near(p.u, 0.0f), "clock: the frame a window time falls on");
            const auto e = MC::FrameAt(2.0f, 2.0f, 41);
            Check(e.f0 == 40 && e.f1 == 40, "clock: the last frame never reads past the end");
        }
        Check(near(MC::BlendWeight(0.0f), 0.0f) && near(MC::BlendWeight(MC::kBlendSec), 1.0f) &&
                  near(MC::BlendWeight(10.0f), 1.0f),
              "clock: the crossfade runs 0 to 1 over kBlendSec");
        Check(MC::PlausibleSpeed(-1.0f) && !MC::PlausibleSpeed(0.0f) && !MC::PlausibleSpeed(50.0f) &&
                  !MC::PlausibleSpeed(std::nanf("")),
              "clock: implausible speeds are refused");

        // 6. The idle cycle's breaks
        Check(MC::CanBreakInto(false, ClipMode::kToEnd, "IdleStretch.hkx"), "cycle: a one-shot idle is a break");
        Check(!MC::CanBreakInto(false, ClipMode::kToEnd, "ChairIdleExit.hkx"), "cycle: a transition is never a break");
        Check(!MC::CanBreakInto(true, ClipMode::kToEnd, "mt_idle.hkx"), "cycle: a base loop is not a break");
        Check(!MC::CanBreakInto(false, ClipMode::kHeld, "IdleStretch.hkx"), "cycle: a held take is not a break");
        Check(MC::ReplayAtEnd(false, false, true), "cycle: while the game runs its base loop plays again, never a random variant");
        Check(!MC::ReplayAtEnd(false, true, true), "cycle: paused, a base loop gives way to another variant");
        Check(!MC::ReplayAtEnd(false, false, false), "cycle: a one-shot that ends goes back to a base loop");
        Check(MC::ReplayAtEnd(true, true, false), "cycle: a pinned idle always plays again");

        // 7. The follow of the engine's clip
        Check(MC::DecideFollow(true, false, false, ClipMode::kToEnd, true, false) == Follow::kKeep,
              "follow: a frozen graph does not pull the cycle back to its clip");
        Check(MC::DecideFollow(true, true, false, ClipMode::kLoop, true, false) == Follow::kKeep,
              "follow: the current take with the engine still keeps its clock");
        Check(MC::DecideFollow(true, true, true, ClipMode::kLoop, true, false) == Follow::kResync,
              "follow: the current take follows the engine's clock");
        Check(MC::DecideFollow(true, false, true, ClipMode::kLoop, true, false) == Follow::kSwitch,
              "follow: another take the engine moves to is switched to");
        Check(MC::DecideFollow(true, true, true, ClipMode::kHeld, false, false) == Follow::kResample,
              "follow: a held take the engine moves is sampled again, not rebaked");
        Check(MC::DecideFollow(false, false, true, ClipMode::kLoop, false, false) == Follow::kRebake,
              "follow: a clip the mirror lacks is baked");
        Check(MC::DecideFollow(false, false, true, ClipMode::kLoop, false, true) == Follow::kKeep,
              "follow: a clip refused this session is not baked again");
        Check(MC::DecideFollow(true, true, true, ClipMode::kToEnd, false, false) == Follow::kRebake,
              "follow: the engine past a take's window rebakes");

        // 8. The pool: what it keeps, its key, its limits, the harvest
        Check(MC::PoolAccepts(ClipMode::kLoop, "mt_idle.hkx"), "pool: a base idle is kept");
        Check(!MC::PoolAccepts(ClipMode::kHeld, "mt_idle.hkx"), "pool: a held clip is never kept");
        Check(!MC::PoolAccepts(ClipMode::kToEnd, "ChairIdleStart.hkx"), "pool: a transition is never kept");
        Check(!MC::PoolAccepts(ClipMode::kLoop, "Idle1.hkx"), "pool: a clip of no family is never kept");
        Check(MC::PoolAccepts(ClipMode::kLoop, "Animations\\idle.hkx", true), "pool: a creature's short family is kept");
        Check(MC::PoolKey("DefaultFemale", true, 1.0f, "", "mt_idle") == "defaultfemale|f|1.00||mt_idle",
              "pool: the key names the project, sex, scale, replacer set and family");
        Check(MC::PoolKey("DraugrProject", false, 1.0f, "", "mt_idle") != MC::PoolKey("DefaultMale", false, 1.0f, "", "mt_idle"),
              "pool: another skeleton's idles never share a key");
        Check(MC::PoolKey("DefaultMale", false, 1.0f, sheoSet, "mt_idle") != MC::PoolKey("DefaultMale", false, 1.0f, "", "mt_idle"),
              "pool: a replacer set's idles never share the vanilla key");
        Check(MC::PoolKey("DefaultMale", false, 1.0f, MC::kSetUnknown, "mt_idle").empty(), "pool: an unknown set has no key");
        {
            const auto vanillaKey = MC::PoolKey("DefaultFemale", true, 1.0f, "", "mt_idle");
            const auto lcaKey     = MC::PoolKey("DefaultFemale", true, 1.0f, lcaSet, "mt_idle");
            Check(MC::PoolKeyFits(vanillaKey, "DefaultFemale", true, 1.0f, "") &&
                      MC::PoolKeyFits(vanillaKey, "DefaultFemale", true, 1.0f, sheoSet),
                  "pool: the vanilla idles fit every NPC of the skeleton");
            Check(!MC::PoolKeyFits(lcaKey, "DefaultFemale", true, 1.0f, "") &&
                      !MC::PoolKeyFits(lcaKey, "DefaultFemale", true, 1.0f, sheoSet),
                  "pool: a child's play idles fit no NPC playing from another set");
            Check(MC::PoolKeyFits(lcaKey, "DefaultFemale", true, 1.0f, lcaSet), "pool: a set's idles fit an NPC playing from it");
            Check(!MC::PoolKeyFits(vanillaKey, "DefaultFemale", false, 1.0f, "") &&
                      !MC::PoolKeyFits(vanillaKey, "DefaultFemale", true, 1.2f, ""),
                  "pool: another sex or scale never fits");
            Check(MC::PoolKeyFits(vanillaKey, "DefaultFemale", true, 1.0f, MC::kSetUnknown), "pool: an unknown set reads the vanilla idles");
        }
        {
            std::vector<MC::PoolTake> t(MC::kPoolShotsPerKey + 1, MC::PoolTake{ false, 10 });
            t.insert(t.begin(), MC::PoolTake{ true, 10 });   // one loop, oldest
            const auto d = MC::EvictWithinKey(t, MC::kPoolBytes);
            Check(d.size() == 1 && d[0] == 1, "pool: past the one-shot cap the oldest one-shot goes, not the loop");
        }
        {
            std::vector<MC::PoolTake> t{ { true, 30 }, { false, 30 }, { false, 30 } };
            const auto                d = MC::EvictWithinKey(t, 50);
            Check(d.size() == 2 && d[0] == 0 && d[1] == 1, "pool: one key over budget drops its oldest, never the newest");
            const auto big = MC::EvictWithinKey({ { false, 30 }, { false, 90 } }, 50);
            Check(big.size() == 1 && big[0] == 0, "pool: a newest take over budget alone stays");
        }
        Check(MC::EvictWithinKey({ { true, 5 } }, 1).empty(), "pool: a lone take is never dropped");
        Check(MC::HarvestWants(ClipMode::kLoop, "mt_idle.hkx", false, false, true), "harvest: an idle the pool lacks");
        Check(!MC::HarvestWants(ClipMode::kHeld, "mt_idle.hkx", false, false, true),
              "harvest: a clip the pool refuses is never harvested");
        Check(!MC::HarvestWants(ClipMode::kLoop, "mt_idle.hkx", false, true, true),
              "harvest: a clip tried this session is not tried again");
        Check(!MC::HarvestWants(ClipMode::kLoop, "mt_idle.hkx", true, false, true), "harvest: a pooled clip is not harvested");
        Check(!MC::HarvestWants(ClipMode::kLoop, "mt_idle.hkx", false, false, false),
              "harvest: a full family is left alone");
        Check(MC::PoolHasRoom(MC::kPoolLoopsPerKey - 1, true) && !MC::PoolHasRoom(MC::kPoolLoopsPerKey, true),
              "pool: base loops fill at kPoolLoopsPerKey");
        Check(MC::PoolHasRoom(MC::kPoolLoopsPerKey, false) && !MC::PoolHasRoom(MC::kPoolShotsPerKey, false),
              "pool: one-shots fill at kPoolShotsPerKey, apart from the loops");

        // 9. The idle picker
        Check(MC::IdleLabel("Animations\\male\\MT_IdleLookingC.hkx") == "MT IdleLookingC", "picker: a clip's label is its file name");
        Check(MC::IdleLabel("x\\_variants_mt_idle\\i_lk_arnd_light.HKX") == "i lk arnd light",
              "picker: a replacer variant is labelled by its own file");
        Check(MC::PickerOffers(ClipMode::kLoop, "mt_idle.hkx") && MC::PickerOffers(ClipMode::kToEnd, "IdleStretch.hkx"),
              "picker: idles and one-shots are offered");
        Check(!MC::PickerOffers(ClipMode::kToEnd, "ChairIdleEnter.hkx") && !MC::PickerOffers(ClipMode::kHeld, "a.hkx") &&
                  !MC::PickerOffers(ClipMode::kLoop, ".hkx"),
              "picker: transitions, held clips and nameless files are not");
        Check(!MC::EndsWithI(MC::kIdleStill, ".hkx"), "picker: the hold-still choice can never name a clip file");
        Check(MC::IdleFilePath("file:actors\\character\\animations\\Female\\MT_Idle.hkx") ==
                  "meshes\\actors\\character\\animations\\Female\\MT_Idle.hkx",
              "idle file: a path under meshes gets the prefix");
        Check(MC::IdleFilePath("FILE: Meshes/Actors/x.HKX ") == "Meshes\\Actors\\x.HKX",
              "idle file: the prefix and extension in any case, forward slashes turned, spaces trimmed, meshes kept");
        Check(MC::IdleFilePath("actors\\x.hkx").empty() && MC::IdleFilePath("").empty() &&
                  MC::IdleFilePath(MC::kIdleStill).empty(),
              "idle file: a clip name, nothing and still are not files");
        Check(MC::IdleFilePath("file:").empty() && MC::IdleFilePath("file:actors\\x.nif").empty(),
              "idle file: no path, or one that is not an .hkx, is refused");
        Check(MC::IdleFilePath("file:..\\x.hkx").empty() && MC::IdleFilePath("file:actors\\..\\..\\x.hkx").empty() &&
                  MC::IdleFilePath("file:C:\\x.hkx").empty() && MC::IdleFilePath("file:\\x.hkx").empty(),
              "idle file: a path that could leave Data is refused");
        Check(MC::IdleFilePath("file:actors\\..x.hkx") == "meshes\\actors\\..x.hkx",
              "idle file: two dots inside a name are not a parent");

        // 9b. The still test's bone on a skeleton with no NPC Head
        Check(MC::SignatureNodeRank("Canine_Head") == 4 && MC::SignatureNodeRank("Sabrecat_Head [Head]") == 4,
              "signature: a dog's and a sabre cat's head");
        Check(MC::SignatureNodeRank("HorseScull") == 3 && MC::SignatureNodeRank("HorseNeck1") == 2 &&
                  MC::SignatureNodeRank("HorseSpine1") == 1,
              "signature: the horse's skull, then neck, then spine");
        Check(MC::SignatureNodeRank("HorseTail1") == 0 && MC::SignatureNodeRank("") == 0, "signature: other bones are none");

        // 10. Names
        Check(MC::BaseName("a/b\\c.hkx") == "c.hkx" && MC::DirOf("a/b\\c.hkx") == "a/b" && MC::DirOf("c").empty(),
              "names: base name and folder over either slash");
        Check(MC::EqualsI("MT_Idle", "mt_idle") && MC::StartsWithI("MT_IdleA", "mt_idle") && MC::EndsWithI("x.HKX", ".hkx"),
              "names: case-insensitive compares");

        // 11. What moves once a node rides: node 1 rides; 2 is its child; 3 is attached to the later 4, a child of 2;
        // 5 hangs under 3; 6 is a child of the still root.
        {
            const std::int32_t        composesOn[7] = { -1, 0, 1, 4, 2, 3, 0 };
            std::vector<std::uint8_t> moves(7, 0);
            moves[1] = 1;
            MC::PropagateMoves(moves, [&](std::size_t j) { return composesOn[j]; });
            Check(moves[2] && moves[3] && moves[4] && moves[5] && !moves[6] && !moves[0],
                  "rig: every node composing on a moving one moves, one attached to a later node too");
            std::vector<std::uint8_t> once(7, 0);
            once[1] = 1;
            MC::PropagateMoves(once, [&](std::size_t j) { return composesOn[j]; }, 1);
            Check(once[2] && once[4] && !once[3], "rig: one sweep misses a node attached to a later one");
        }

        // 12. Layered loops: a spell's idle in each hand, each driving one arm
        Check(MC::IsLayer(ClipMode::kLoop, ClipMode::kLoop, "Animations\\MLh_Idle.hkx") &&
                  MC::IsLayer(ClipMode::kLoop, ClipMode::kPingPong, "Animations\\MLh_Idle.hkx"),
              "layers: a loop beside the base loop is a layer (the left hand's magic idle)");
        Check(!MC::IsLayer(ClipMode::kLoop, ClipMode::kToEnd, "a.hkx") && !MC::IsLayer(ClipMode::kLoop, ClipMode::kHeld, "a.hkx") &&
                  !MC::IsLayer(ClipMode::kHeld, ClipMode::kLoop, "a.hkx"),
              "layers: a one-shot or held clip is never one, nor is anything beside a held clip");
        Check(MC::IsLayer(ClipMode::kToEnd, ClipMode::kLoop, "Animations\\Shd_BlockIdle.hkx") &&
                  !MC::IsLayer(ClipMode::kToEnd, ClipMode::kLoop, "Animations\\MT_WalkForward.hkx"),
              "layers: the stance loop under a one-shot is one (the shield arm of a look-around)");
        Check(!MC::IsLayer(ClipMode::kLoop, ClipMode::kLoop, "Animations\\MT_WalkForward.hkx") &&
                  !MC::IsLayer(ClipMode::kLoop, ClipMode::kLoop, "WalkLeft_Wolf.hkx") &&
                  !MC::IsLayer(ClipMode::kLoop, ClipMode::kLoop, "mt_turnleft.hkx"),
              "layers: a walk, run or turn of a locomotion blend is never one");
        Check(near(MC::PhaseOf(1.0f, 2.0f, 2.0f), 0.5f) && near(MC::PhaseOf(0.0f, 2.0f, 5.0f), 0.5f) &&
                  near(MC::PhaseOf(0.0f, 2.0f, -0.5f), 0.75f) && MC::PhaseOf(0.0f, 0.0f, 1.0f) == 0.0f,
              "layers: a phase is the fraction of the cropped range, wrapped");
        Check(MC::PhaseLocked(0.3f, 0.305f) && MC::PhaseLocked(0.998f, 0.003f) && !MC::PhaseLocked(0.3f, 0.35f),
              "layers: a loop at the base loop's phase is the graph's sync, across the wrap too");
        Check(!MC::PhaseDrifted(0.3f, 0.33f) && !MC::PhaseDrifted(0.99f, 0.02f) && MC::PhaseDrifted(0.3f, 0.4f) &&
                  MC::PhaseDrifted(0.3f, std::numeric_limits<float>::quiet_NaN()),
              "layers: a locked loop is let go only once its phase drifts well past the lock's, across the wrap too");
        Check(MC::ToleranceUnits({ 0.025f, 0.0f, 0.5f, true }) < 1.0f && MC::ToleranceUnits({ 0.05f, 0.0f, 0.0f, true }) >= 1.0f &&
                  near(MC::ToleranceUnits({ 0.01f, 0.0f, 3.0f, true }), 3.0f),
              "layers: a gap in tolerances is the worse of rotation and move");
        Check(std::isinf(MC::ToleranceUnits({ NAN, 0.0f, 0.0f, true })), "layers: a gap that is not a number is infinite");
        {
            using E = MC::NodeError;
            const E none{};
            // Nodes: 0 the root (none), 1 the shared spine (alike), 2 the right arm (the base's), 3 the left arm (the
            // layer's), 4 a bone the base alone drives, 5 one only the layer drives, 6 a near tie.
            const std::vector<std::vector<E>> err{
                { none, { 0.01f, 0, 0.1f, true }, { 0.01f, 0, 0.1f, true }, { 0.8f, 0, 5.0f, true }, { 0.3f, 0, 0, true }, none,
                  { 0.08f, 0, 0, true } },
                { none, { 0.01f, 0, 0.1f, true }, { 0.7f, 0, 4.0f, true }, { 0.01f, 0, 0.2f, true }, none,
                  { 0.9f, 0, 9.0f, true }, { 0.05f, 0, 0, true } },
            };
            const auto a = MC::AssignLayers(err);
            Check(a.size() == 7 && a[0] == -1, "layers: a node no take drives has none");
            Check(a[1] == 0, "layers: the body both clips pose alike stays with the base loop");
            Check(a[2] == 0 && a[3] == 1, "layers: each arm goes to the clip that poses it");
            Check(a[4] == 0 && a[5] == 1, "layers: a node one take alone drives is that take's, however far");
            Check(a[6] == 0, "layers: a layer closer by less than the margin leaves the node to the base loop");
            const auto single = MC::AssignLayers({ err[0] });
            Check(single[1] == 0 && single[3] == 0 && single[5] == -1, "layers: with no layer the base loop keeps every node it drives");

            const std::vector<std::vector<E>> three{ { { 1.0f, 0, 9.0f, true } }, { { 0.1f, 0, 0, true } }, { { 0.1f, 0, 0, true } } };
            Check(MC::AssignLayers(three)[0] == 1, "layers: of equally close layers the first takes the node");

            const auto s = MC::SummarizeAssigned(err, a);
            Check(s.compared == 6 && s.close == 3, "layers: the self-check counts each node by the take that drives it");
            Check(near(s.worstRot, 0.9f) && near(s.worstMove, 9.0f), "layers: and keeps the worst gaps of those");
            const auto base = MC::SummarizeAssigned(err, single);
            Check(base.compared == 5 && base.close == 2 && near(base.worstMove, 5.0f), "layers: the base loop alone leaves the other arm off");

            // NaN and ragged rows
            const E ok{ 0.01f, 0, 0.1f, true }, nanE{ NAN, 0, NAN, true };
            Check(MC::AssignLayers({ { none, nanE }, { none, ok } })[1] == 1, "layers: a layer takes a node whose base gap is not a number");
            Check(MC::AssignLayers({ { none, nanE }, { none, nanE }, { none, nanE } })[1] == 0,
                  "layers: when no take's gap is a number the base loop keeps the node");
            Check(MC::AssignLayers({ { none, ok, nanE }, { none, nanE, nanE } })[1] == 0, "layers: a layer whose gap is not a number never wins");
            const auto ragged = MC::AssignLayers({ { none, ok }, { none, ok, ok, ok } });
            Check(ragged.size() == 4 && ragged[1] == 0 && ragged[2] == 1 && ragged[3] == 1,
                  "layers: past a short base row a layer drives the nodes it has");
            const auto shortLayer = MC::AssignLayers({ { none, ok, ok }, { none } });
            Check(shortLayer.size() == 3 && shortLayer[1] == 0 && shortLayer[2] == 0, "layers: a short layer row drives none past its end");
            Check(MC::AssignLayers({}).empty(), "layers: no takes, no nodes");

            // Thin layers: a stray bone or two goes back to the base loop
            std::vector<std::vector<E>> thin(2, std::vector<E>(12, none));
            for (int i = 1; i < 12; ++i) {
                thin[0][static_cast<std::size_t>(i)] = i == 11 ? none : E{ 0.9f, 0, 9.0f, true };
                thin[1][static_cast<std::size_t>(i)] = ok;
            }
            auto thinAssign = MC::AssignLayers(thin);
            Check(thinAssign[1] == 1 && thinAssign[11] == 1, "layers: before the drop every node goes to the closer layer");
            auto kept = MC::DropThinLayers(thinAssign, thin);
            Check(kept[1] == 11 && thinAssign[1] == 1, "layers: a layer driving an arm's worth of nodes is kept");
            auto stray = MC::AssignLayers(thin);
            for (int i = 1; i < 9; ++i) stray[static_cast<std::size_t>(i)] = 0;   // the layer keeps nodes 9-11 only
            const auto strayKept = MC::DropThinLayers(stray, thin);
            Check(strayKept[1] == 0 && stray[9] == 0 && stray[10] == 0, "layers: a thin layer's nodes go back to the base loop");
            Check(stray[11] == -1 && strayKept[0] == 10, "layers: and a node the base loop does not drive to none");
        }
    }
}
