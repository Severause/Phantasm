// Phantasm - native self-tests: MannequinPhysicsCore (the actor view reading CBPC's configs and SMP's
// physics XMLs, and CBPC's jiggle step).
//
// Author: Severause

#include "MannequinPhysicsCore.h"
#include "TestHarness.h"

#include <cmath>

namespace Phantasm::Tests
{
    void RunMannequinPhysicsCoreTests()
    {
        namespace MP = Phantasm::MannequinPhysicsCore;
        using MP::Vec3;
        const auto near = [](float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; };

        // 1. Conditions: AND before OR, NOT, unknown functions false
        {
            const auto c = MP::ParseCbpcCondition("IsFemale() AND NOT IsUnique() OR IsPlayerTeammate()");
            Check(c.allOf.size() == 2 && c.allOf[1].size() == 2 && c.allOf[1][0].negate && c.allOf[1][0].fn == "isunique",
                  "cbpc: a condition splits on AND, then OR, and reads NOT");
            const auto facts = [](bool female, bool unique) {
                return [=](const MP::CbpcAtom& a) {
                    if (a.fn == "isfemale") return female;
                    if (a.fn == "isunique") return unique;
                    return false;
                };
            };
            Check(MP::EvalCbpcCondition(c, facts(true, false)) && !MP::EvalCbpcCondition(c, facts(false, false)) &&
                      !MP::EvalCbpcCondition(c, facts(true, true)),
                  "cbpc: a condition holds only when every AND part holds");
            Check(MP::EvalCbpcCondition(MP::ParseCbpcCondition(""), facts(false, false)), "cbpc: no condition always holds");
            const auto f = MP::ParseCbpcCondition("IsRaceFormId(Skyrim.esm|013746)");
            Check(f.allOf.size() == 1 && f.allOf[0][0].arg == "Skyrim.esm|013746", "cbpc: a function keeps its argument");
        }

        // 2. Master config: the map, comments, MalePhysics, a later line replacing an earlier one
        {
            MP::CbpcMaster m;
            MP::ParseCbpcMaster("[Settings]\nMalePhysics=1   #comment\n[ConfigMap]   #more\nNPC L Breast=Breast=IsFemale()\n"
                                "<\nL Breast01=ExtraBreast1L\n>\nNPC Pelvis [Pelv]\n",
                                m);
            MP::ParseCbpcMaster("[ConfigMap]\nNPC L Breast=LBreast=IsFemale()\n", m);
            Check(m.malePhysics && m.bones.size() == 2 && m.bones[0].alias == "LBreast" && m.bones[1].bone == "L Breast01" &&
                      m.bones[1].condition.allOf.empty(),
                  "cbpc: the master map reads bones, aliases and conditions, a later file wins");
            Check(MP::IsFemaleBodyBone("NPC L RearThigh") && !MP::IsFemaleBodyBone("GenitalsLag01"), "cbpc: female body bones");
        }

        // 3. Bounce config: weight pairs and CBPC's fallbacks
        {
            const auto f = MP::ParseCbpcBounce("Conditions = IsFemale()\nPriority = 3\n# c\nBreast.stiffness 0.04\n"
                                               "Breast.damping 0.2 0.1\nBreast.maxoffset 3\nBreast.linearX 1\n"
                                               "Breast.rotationalX 0.1\nBreast.timetick 4\n");
            Check(f.conditioned && f.priority == 3 && f.aliases.count("breast") == 1, "cbpc: a bounce file reads conditions and aliases");
            const auto p100 = MP::ResolveCbpc(f.aliases.at("breast"), 100.0f);
            const auto p0   = MP::ResolveCbpc(f.aliases.at("breast"), 0.0f);
            const auto p50  = MP::ResolveCbpc(f.aliases.at("breast"), 50.0f);
            Check(near(p100.damping.y, 0.2f) && near(p0.damping.y, 0.1f) && near(p50.damping.y, 0.15f),
                  "cbpc: the first value is weight 100, the second weight 0, in between by weight");
            Check(near(p100.stiffness.z, 0.04f) && near(p100.stiffnessRot.x, 0.04f) && near(p100.dampingRot.x, 0.2f),
                  "cbpc: a generic stiffness and damping fill every axis and the rotation");
            Check(near(p100.maxOff.x, 3.0f) && near(p100.minOff.z, -3.0f) && near(p100.maxOffRot.y, 3.0f),
                  "cbpc: maxoffset bounds every axis");
            Check(near(p100.timeTickRot, 4.0f) && near(p100.timeStepRot, 1.0f) && near(p100.linRot[0][1], 1.0f) &&
                      near(p100.linRot[0][0], 0.0f) && near(p100.amplitude, 1.0f),
                  "cbpc: the rotation's tick and step follow the linear ones, defaults hold");
            Check(MP::CbpcMoves(p100) && !MP::CbpcMoves(MP::ResolveCbpc(MP::CbpcEntry{}, 100.0f)),
                  "cbpc: an alias that shows none of its lag never moves the bone");
            const auto open = MP::ResolveCbpc(MP::ParseCbpcBounce("O.linearX 1\n").aliases.at("o"), 100.0f);
            Check(MP::CbpcMoves(open) && near(open.maxOff.y, 5.0f) && near(open.minOff.x, -5.0f),
                  "cbpc: unset linear bounds are CBPC's +-5");
            const auto lowNot = MP::ParseCbpcCondition("not IsFemale()");
            Check(lowNot.allOf.size() == 1 && lowNot.allOf[0][0].negate && lowNot.allOf[0][0].fn == "isfemale",
                  "cbpc: not is read in any case");
            const auto lin = MP::ParseCbpcBounce("B.Xmaxoffset 2\nB.Xminoffset -1\nB.Zmaxoffset 5\n");
            const auto pl  = MP::ResolveCbpc(lin.aliases.at("b"), 100.0f);
            Check(near(pl.maxOffRot.y, 2.0f) && near(pl.minOffRot.y, -1.0f) && near(pl.maxOffRot.x, 5.0f),
                  "cbpc: the rotation's bounds follow linear Z/X/Y when not given");

            MP::CbpcBounceFile merged;
            MP::MergeCbpcBounce(merged, MP::ParseCbpcBounce("A.stiffness 0.1\nA.damping 0.2\n"));
            MP::MergeCbpcBounce(merged, MP::ParseCbpcBounce("A.damping 0.3\nB.stiffness 0.4\n"));
            Check(near(merged.aliases.at("a").at100.at("stiffness"), 0.1f) && near(merged.aliases.at("a").at100.at("damping"), 0.3f) &&
                      merged.aliases.count("b") == 1,
                  "cbpc: unconditioned files merge key by key, a later one winning");
            std::vector<MP::CbpcBounceFile> conds{
                MP::ParseCbpcBounce("Conditions = IsFemale()\nPriority = 1\nA.stiffness 1\n"),
                MP::ParseCbpcBounce("Conditions = IsFemale() AND IsUnique()\nPriority = 1\nA.stiffness 2\n"),
                MP::ParseCbpcBounce("Conditions = IsUnique()\nPriority = 0\nA.stiffness 3\n"),
            };
            const auto holdsAll = [](const MP::CbpcCondition&) { return true; };
            const auto femaleOnly = [](const MP::CbpcCondition& c) {
                return MP::EvalCbpcCondition(c, [](const MP::CbpcAtom& a) { return a.fn == "isfemale"; });
            };
            Check(MP::ChooseCbpcBounce(conds, holdsAll) == &conds[1] && MP::ChooseCbpcBounce(conds, femaleOnly) == &conds[0] &&
                      MP::ChooseCbpcBounce(conds, [](const MP::CbpcCondition&) { return false; }) == nullptr,
                  "cbpc: the conditioned file is the highest priority, then the most AND parts, that holds");
            Check(MP::CbpcEntryFor("a", &conds[0], merged) == &conds[0].aliases.at("a") &&
                      MP::CbpcEntryFor("b", nullptr, merged) == &merged.aliases.at("b") &&
                      MP::CbpcEntryFor("zz", nullptr, merged) == nullptr,
                  "cbpc: an alias comes from the chosen file, or the merged ones when none is chosen");
            Check(MP::CbpcEntryFor("b", &conds[0], merged) == nullptr,
                  "cbpc: an alias the chosen file does not name takes CBPC's defaults there, which move nothing");
            const auto ampOnly = MP::ParseCbpcBounce("Conditions = IsFemale()\nB.amplitude 0\n");
            const auto ampCol  = MP::ParseCbpcBounce("Conditions = IsFemale()\nB.amplitude 0\nB.collisionXmaxoffset 50\n");
            Check(MP::CbpcEntryFor("b", &ampOnly, merged) == &merged.aliases.at("b") &&
                      MP::CbpcEntryFor("b", &ampCol, merged) == &ampCol.aliases.at("b"),
                  "cbpc: only a chosen entry that sets nothing but a zero amplitude falls back to the merged ones");
            Check(!MP::CbpcEntryAllocated(ampOnly.aliases.at("b")) && MP::CbpcEntryAllocated(conds[0].aliases.at("a")) &&
                      MP::CbpcEntryAllocated(MP::ParseCbpcBounce("C.damping 0.1\nC.amplitude 0\n").aliases.at("c")),
                  "cbpc: an entry counts with a stiffness or damping key, a moved collision bound or an amplitude");
            Check(MP::CbpcConfigFileKind(L"CBPCMasterConfig_3BA.txt") == MP::CbpcConfigFile::kMaster &&
                      MP::CbpcConfigFileKind(L"cbpconfig_3b.INI") == MP::CbpcConfigFile::kBounce &&
                      MP::CbpcConfigFileKind(L"CBPConfig.txt.bak") == MP::CbpcConfigFile::kNone &&
                      MP::CbpcConfigFileKind(L"CBPC-Collision.txt") == MP::CbpcConfigFile::kNone,
                  "cbpc: master and bounce configs are found as .txt or .ini, in any case");
        }

        // 3b. Breast bones: the armour's amplitude, gravity from Spine2
        {
            const auto f = MP::ParseCbpcBounce("B.breastClothedAmplitude 0.9 0.5\nB.breastLightArmoredAmplitude 0.8\n"
                                               "B.breastHeavyArmoredAmplitude 0.7\nB.maxoffset 2\nB.linearZ 1\n"
                                               "B.gravityCorrection -0.5\nB.gravityInvertedCorrection 0.5\n"
                                               "B.gravityInvertedCorrectionStart 0\nB.gravityInvertedCorrectionEnd 1\n");
            auto p = MP::ResolveCbpc(f.aliases.at("b"), 100.0f);
            Check(near(p.clothedAmplitude, 0.9f) && near(p.lightAmplitude, 0.8f) && near(p.heavyAmplitude, 0.7f) &&
                      near(MP::ResolveCbpc(f.aliases.at("b"), 0.0f).clothedAmplitude, 0.5f) &&
                      near(MP::ResolveCbpc(MP::CbpcEntry{}, 100.0f).heavyAmplitude, 1.0f),
                  "cbpc: the armour amplitudes read by weight, 1 when unset");
            using MP::CbpcArmor;
            MP::CbpcArmorFacts heavyKw;
            heavyKw.heavy = heavyKw.clothing = true;
            MP::CbpcArmorFacts asLight = heavyKw;
            asLight.asLight            = true;
            MP::CbpcArmorFacts asNaked = asLight;
            asNaked.asNaked            = true;
            Check(MP::CbpcArmorFor(MP::CbpcArmorFacts{}) == CbpcArmor::kNone && MP::CbpcArmorFor(heavyKw) == CbpcArmor::kHeavy &&
                      MP::CbpcArmorFor(asLight) == CbpcArmor::kLight && MP::CbpcArmorFor(asNaked) == CbpcArmor::kNone,
                  "cbpc: AsNaked wins, an As keyword replaces the game's, heavy before light before clothing");
            Check(near(MP::CbpcArmorAmplitude(p, CbpcArmor::kHeavy), 0.7f) && near(MP::CbpcArmorAmplitude(p, CbpcArmor::kNone), 1.0f),
                  "cbpc: the class picks its amplitude");
            Check(MP::CbpcIsBreastBone("L Breast01") && MP::CbpcIsBreastBone("NPC R Breast") && !MP::CbpcIsBreastBone("NPC L Butt") &&
                      MP::CbpcBreastSide("NPC L Breast") == 'L' && MP::CbpcBreastSide("r breast02") == 'R' &&
                      MP::CbpcBreastSide("ExtraBreast1") == 0,
                  "cbpc: breast bones and their side, by name");
            const Vec3 ax{ 1, 0, 0 }, ay{ 0, 1, 0 }, az{ 0, 0, 1 }, down{ 0, 0, -1 };
            Check(near(MP::CbpcRest(p, ax, ay, az).offset.z, -0.5f) && near(MP::CbpcRest(p, ax, ay, az, &down).offset.z, 0.5f),
                  "cbpc: the gravity reference's own Z decides the correction, not the parent's");
            p.armorAmplitude = 0.5f;
            Check(near(MP::CbpcRest(p, ax, ay, az).offset.z, -0.25f), "cbpc: the armour amplitude scales the move");
            Check(MP::CbpcMoves(p), "cbpc: a scaled bone still moves");
            p.armorAmplitude = 0.0f;
            const auto still = MP::ResolveCbpc(MP::ParseCbpcBounce("S.maxoffset 2\nS.linearZ 1\nS.amplitude 0\n").aliases.at("s"), 100.0f);
            Check(!MP::CbpcMoves(p) && !MP::CbpcMoves(still), "cbpc: an amplitude of 0, its own or the armour's, never moves the bone");
        }

        // 4. The jiggle: rests, lags a moving parent inside its bounds, settles
        {
            const auto f = MP::ParseCbpcBounce("B.stiffness 0.05\nB.stiffness2 0.01\nB.damping 0.15\nB.maxoffset 2\n"
                                               "B.linearX 1\nB.linearY 1\nB.linearZ 1\nB.rotationalY 0.1\nB.timetick 4\n");
            const auto p = MP::ResolveCbpc(f.aliases.at("b"), 100.0f);
            const Vec3 ax{ 1, 0, 0 }, ay{ 0, 1, 0 }, az{ 0, 0, 1 };
            MP::CbpcState s;
            MP::CbpcPose  pose;
            Check(!MP::CbpcStep(s, { 0, 0, 100 }, ax, ay, az, p, 1.0f / 60.0f, pose) && s.primed && near(pose.offset.x, 0.0f),
                  "cbpc: the first step rests the bone");
            const bool moving = MP::CbpcStep(s, { 10, 0, 100 }, ax, ay, az, p, 1.0f / 60.0f, pose);
            Check(moving && pose.offset.x < -0.1f && pose.offset.x >= -2.0f - 1e-4f,
                  "cbpc: a parent that moves leaves the bone lagging, inside its bounds");
            Check(std::fabs(pose.attitude) + std::fabs(pose.heading) + std::fabs(pose.bank) > 0.0f, "cbpc: the lag turns the bone");
            for (int i = 0; i < 2000; ++i) MP::CbpcStep(s, { 10, 0, 100 }, ax, ay, az, p, 1.0f / 60.0f, pose);
            Check(!MP::CbpcStep(s, { 10, 0, 100 }, ax, ay, az, p, 1.0f / 60.0f, pose) && std::fabs(pose.offset.x) < 0.05f,
                  "cbpc: it settles at rest again");
            MP::CbpcStep(s, { 50, 0, 100 }, ax, ay, az, p, 1.0f, pose);
            Check(near(pose.offset.x, 0.0f) && near(s.pos.x, 50.0f), "cbpc: a long gap rests instead of catching up");
            const auto g = MP::ResolveCbpc(MP::ParseCbpcBounce("G.maxoffset 2\nG.linearZ 1\nG.gravityCorrection -0.5\n").aliases.at("g"), 100.0f);
            MP::CbpcState sg;
            MP::CbpcStep(sg, { 0, 0, 0 }, ax, ay, az, g, 1.0f / 60.0f, pose);
            Check(near(pose.offset.z, -0.5f), "cbpc: gravityCorrection lowers the rest");
            const auto rest = MP::CbpcRest(g, ax, ay, az);
            Check(near(rest.offset.z, pose.offset.z) && near(rest.heading, pose.heading), "cbpc: the rest pose is the first step's");
            float m[3][3];
            MP::EulerMatrix(0.0f, 0.0f, 0.0f, m);
            Check(near(m[0][0], 1.0f) && near(m[1][1], 1.0f) && near(m[2][2], 1.0f) && near(m[0][1], 0.0f), "cbpc: zero angles are no turn");
            MP::EulerMatrix(0.0f, 0.0f, 1.5707963f, m);
            Check(near(m[2][1], 1.0f) && near(m[1][2], -1.0f), "cbpc: bank turns about X");
        }

        // 5. SMP XML: kinematic and simulated bones, defaults, templates, constraints
        {
            const char* xml = R"(<?xml version="1.0"?>
<system>
  <bone name="NPC Head [Head]"/>
  <!-- a comment -->
  <bone-default><mass>0.1</mass><angularDamping>0.7</angularDamping></bone-default>
  <bone name="Hair 02"/>
  <bone-default name="heavy" extends=""><mass>2</mass></bone-default>
  <bone name="Hair 03" template="heavy"><angularDamping>0.2</angularDamping></bone>
  <bone name="Cape 01"/>
  <generic-constraint-default>
    <angularLowerLimit x="-0.2" y="-0.1" z="-0.4"/>
    <angularUpperLimit x="0.2" y="0.1" z="0.4"/>
  </generic-constraint-default>
  <constraint-group>
    <generic-constraint bodyA="Hair 02" bodyB="NPC Head [Head]"/>
    <generic-constraint bodyA="Hair 03" bodyB="Hair 02">
      <angularLowerLimit x="-0.05" y="-0.05" z="-0.05"/>
      <angularUpperLimit x="0.05" y="0.05" z="0.05"/>
    </generic-constraint>
  </constraint-group>
  <conetwist-constraint bodyA="Cape 01" bodyB="NPC Head [Head]"><swingSpan1>0.5</swingSpan1><swingSpan2>0.3</swingSpan2></conetwist-constraint>
</system>)";
            MP::SmpPhysics smp;
            Check(MP::ParseSmp(xml, smp) && smp.bones.size() == 4, "smp: a physics XML reads its bones");
            Check(!smp.bones.at("npc head [head]").dynamic && smp.bones.at("hair 02").dynamic, "smp: mass 0 follows the animation, above 0 is simulated");
            Check(near(smp.bones.at("hair 02").angularDamping, 0.7f) && near(smp.bones.at("hair 03").angularDamping, 0.2f) &&
                      smp.bones.at("hair 03").dynamic,
                  "smp: a bone takes the defaults above it, or its template, then its own settings");
            Check(near(smp.bones.at("hair 02").cone, 0.3f) && near(smp.bones.at("hair 03").cone, 0.05f) &&
                      near(smp.bones.at("cape 01").cone, 0.4f),
                  "smp: a constraint's limits set how far its simulated body swings");
            MP::SmpBone into{ false, 0.0f, -1.0f };
            MP::MergeSmpBone(into, MP::SmpBone{ true, 0.5f, 0.4f });
            MP::MergeSmpBone(into, MP::SmpBone{ true, 0.1f, 0.2f });
            MP::MergeSmpBone(into, MP::SmpBone{ false, 0.0f, -1.0f });
            Check(into.dynamic && near(into.cone, 0.2f) && near(into.angularDamping, 0.5f),
                  "smp: a bone several files name is simulated if any says so, as far as the tightest lets it");
            const auto sp = MP::SmpSpringParams(smp.bones.at("hair 02"));
            Check(near(sp.maxAngle, 0.3f) && sp.damping > 4.0f && sp.stiffness > 0.0f, "smp: a simulated bone's spring");
            std::unordered_map<std::string, std::string> bbps;
            Check(MP::ParseSmpDefaultBbps("<?xml version=\"1.0\"?><default-bbps><map shape=\"ErinTail\" "
                                          "file=\"SKSE\\Plugins\\tail.xml\"/><map shape=\"\" file=\"x.xml\"/>"
                                          "<other/></default-bbps>",
                                          bbps) &&
                      bbps.size() == 1 && bbps.at("erintail") == "SKSE\\Plugins\\tail.xml",
                  "smp: the shape mapping reads shape -> file, skipping an empty shape");
            Check(!MP::ParseSmpDefaultBbps("<system/>", bbps), "smp: a file that is not a shape mapping is refused");
            MP::SmpPhysics bad;
            Check(!MP::ParseSmp("<system><bone name='x'></system>", bad) && !MP::ParseSmp("<notsmp/>", bad),
                  "smp: a malformed file, or one that is not SMP, is refused");
        }

        // 6. SMP files as shipped: junk after the root, FSMP's renamed bones
        {
            const std::string body = "<system><bone-default><mass>1</mass></bone-default><bone name=\"HDT_BS01 2\"/>";
            MP::SmpPhysics    a, b;
            Check(MP::ParseSmp(body + "</system>undefined</xml>", a) && a.bones.count("hdt_bs01 2") == 1 &&
                      a.bones.at("hdt_bs01 2").dynamic,
                  "smp: a stray close tag after the root is skipped, and the file's bones are read");
            Check(MP::ParseSmp(body + "</system>\r\n</xml>\r\n", b) && b.bones.count("hdt_bs01 2") == 1,
                  "smp: a stray close tag on its own line is skipped too");
            Check(MP::SmpBoneKey("hdtSSEPhysics_AutoRename_Armor_0000001A HDT_BS01 2") == "hdt_bs01 2" &&
                      MP::SmpBoneKey("HDTSSEPHYSICS_AUTORENAME_HEAD_00000002 Sky188 Ponytail 1") == "sky188 ponytail 1",
                  "smp: FSMP's armour and head rename prefixes come off, the name's own spaces stay");
            Check(MP::SmpBoneKey("hdtSSEPhysics_AutoRename_Head_2 hdtSSEPhysics_AutoRename_Armor_1234567890 Bone01") == "bone01",
                  "smp: a node renamed twice loses both prefixes, whatever the id's length");
            Check(MP::SmpBoneKey("hdtSSEPhysics_AutoRename_Armor_0000001A") == "hdtssephysics_autorename_armor_0000001a" &&
                      MP::SmpBoneKey("hdtSSEPhysics_AutoRename_Armor_xyz Bone01") == "hdtssephysics_autorename_armor_xyz bone01" &&
                      MP::SmpBoneKey("hdtSSEPhysics_AutoRename_Body_01 Bone01") == "hdtssephysics_autorename_body_01 bone01" &&
                      MP::SmpBoneKey("NPC L Breast") == "npc l breast",
                  "smp: a malformed prefix, or none, leaves the name as it is (lower-cased)");
            const auto split = MP::SplitSmpNodeName("hdtSSEPhysics_AutoRename_Head_2 HDTSSEPhysics_AutoRename_Armor_1A Bone01");
            Check(split.key == "bone01" && split.pieces.size() == 2 && split.pieces[0] == "head_2" && split.pieces[1] == "armor_1a" &&
                      MP::SplitSmpNodeName("NPC Spine [Spn0]").pieces.empty(),
                  "smp: a renamed node gives the rename ids of its pieces, outermost first; a skeleton bone none");
        }

        // 6b. Two pieces' files that name a bone alike stay apart
        {
            const auto file = [](bool dynamic, float cone) {
                auto f = std::make_shared<MP::SmpPhysics>();
                f->bones["bone01"] = MP::SmpBone{ dynamic, 0.5f, cone };
                return f;
            };
            const auto a = file(true, 0.3f), b = file(false, -1.0f);
            const auto c = std::make_shared<MP::SmpPhysics>();
            c->bones["npc spine"] = MP::SmpBone{ false, 0.0f, -1.0f };
            c->bones["cloak 1"]   = MP::SmpBone{ true, 0.1f, 0.2f };
            MP::SmpBoneIndex idx;
            const std::size_t fa = idx.AddFile(a), fb = idx.AddFile(b), fc = idx.AddFile(c);
            Check(idx.AddFile(a) == fa && idx.Files() == 3 && idx.Bones() == 3, "smp: a file is indexed once");
            idx.NotePieceFile("armor_a", fa);
            idx.NotePieceFile("armor_b", fb);
            using F = MP::SmpBoneIndex::Found;
            MP::SmpBone out;
            Check(idx.Find("hdtSSEPhysics_AutoRename_Armor_A Bone01", out) == F::kBone && out.dynamic &&
                      idx.Find("hdtSSEPhysics_AutoRename_Armor_B Bone01", out) == F::kBone && !out.dynamic,
                  "smp: a renamed node takes its bone from its own piece's file, not another piece's");
            Check(idx.Find("hdtSSEPhysics_AutoRename_Armor_B Cloak 1", out) == F::kNone,
                  "smp: a bone its own piece's files do not name stays still");
            Check(idx.Find("hdtSSEPhysics_AutoRename_Armor_C Bone01", out) == F::kAmbiguous &&
                      idx.Find("hdtSSEPhysics_AutoRename_Armor_C Cloak 1", out) == F::kBone && out.dynamic,
                  "smp: a node of an unknown piece takes only a name one file alone gives");
            Check(idx.Find("Bone01", out) == F::kBone && out.dynamic && idx.Find("NPC Spine", out) == F::kBone && !out.dynamic,
                  "smp: a skeleton bone takes every file's, simulated if any simulates it");
            (void)fc;
        }

        // 7. SMP's rules for bones and constraints
        {
            // Kinematic anchors first, then a simulated default, then constraints on bodies no <bone> declared.
            const char* xml = R"(<system>
  <bone name="a 1"/>
  <bone name="NPC Spine"/>
  <bone-default><mass>0.5</mass></bone-default>
  <bone name="NPC Spine"/>
  <generic-constraint bodyA="a 2" bodyB="a 1">
    <angularLowerLimit x="-0.1" y="-0.1" z="-0.1"/><angularUpperLimit x="0.1" y="0.1" z="0.1"/>
  </generic-constraint>
  <bone name="a 2"><mass>0</mass></bone>
  <stiffspring-constraint bodyA="a 3" bodyB="a 2"/>
</system>)";
            MP::SmpPhysics smp;
            Check(MP::ParseSmp(xml, smp) && smp.bones.at("a 2").dynamic && smp.bones.at("a 3").dynamic &&
                      !smp.bones.at("a 1").dynamic && near(smp.bones.at("a 2").cone, 0.1f) && smp.fromConstraints == 2,
                  "smp: a constraint body no <bone> declared comes from the bone-default in force there");
            Check(!smp.bones.at("npc spine").dynamic && smp.repeated == 2,
                  "smp: a bone's first declaration wins, a later one is ignored");

            const auto coneOf = [](const char* constraint) {
                MP::SmpPhysics s;
                const std::string x = std::string("<system><bone-default><mass>1</mass></bone-default><bone name=\"b\"/>") +
                                      constraint + "</system>";
                return MP::ParseSmp(x, s) ? s.bones.at("b").cone : -2.0f;
            };
            Check(near(coneOf(R"(<generic-constraint bodyA="b" bodyB="x"><angularLowerLimit x="0.2" y="-0.1" z="-0.1"/>)"
                              R"(<angularUpperLimit x="0" y="0.1" z="0.1"/></generic-constraint>)"),
                       0.35f),
                  "smp: an axis whose lower limit is above its upper one is free");
            Check(near(coneOf(R"(<generic-constraint bodyA="b" bodyB="x"><angularUpperLimit x="0.1" y="0.1" z="0.1"/></generic-constraint>)"),
                       MP::kSmpUnconstrainedCone),
                  "smp: an unset limit leaves its axes free");
            Check(near(coneOf(R"(<conetwist-constraint bodyA="b" bodyB="x"><coneLimit>0.4</coneLimit><planeLimit>0.2</planeLimit></conetwist-constraint>)"),
                       0.3f) &&
                      near(coneOf(R"(<conetwist-constraint bodyA="b" bodyB="x"><limitZ>0.6</limitZ><limitY>0.2</limitY></conetwist-constraint>)"),
                           0.4f) &&
                      near(coneOf(R"(<conetwist-constraint bodyA="b" bodyB="x"><swingSpan1>0.8</swingSpan1><coneLimit>0.4</coneLimit>)"
                                  R"(<swingSpan2>-1</swingSpan2></conetwist-constraint>)"),
                           0.2f),
                  "smp: coneLimit / limitZ are swingSpan1 and planeLimit / limitY swingSpan2, the last one written wins, never below 0");
            MP::SmpPhysics comma;
            Check(MP::ParseSmp("<system><bone name=\"c\"><mass>0,5</mass><angularDamping>0,25</angularDamping></bone></system>", comma) &&
                      comma.bones.at("c").dynamic && near(comma.bones.at("c").angularDamping, 0.25f),
                  "smp: a comma is a decimal point in an SMP file");
            float cbpcValue = -1.0f;
            Check(MP::ParseFloat("0,5", cbpcValue) && near(cbpcValue, 0.0f), "cbpc: a comma is not a decimal point in a CBPC config");
        }
    }
}
