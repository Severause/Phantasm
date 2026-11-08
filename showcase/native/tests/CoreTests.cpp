// Phantasm Showcase - self-tests of the engine-free rules in src/core.

#include "TestHarness.h"

#include "AnimPathCore.h"
#include "HostCore.h"
#include "ImageCore.h"
#include "ItemCore.h"
#include "PanelCore.h"
#include "PeopleCore.h"
#include "Text.h"
#include "TryOnCore.h"
#include "TurnCore.h"
#include "ViewCore.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace Showcase::Tests
{
    void RunTextTests()
    {
        Check(Text::Fold("AbC-Ü") == "abc-Ü", "text: folds ASCII only");
        const auto w = Text::Words("  Iron   SWORD ");
        Check(w.size() == 2 && w[0] == "iron" && w[1] == "sword", "text: words split and folded");
        Check(Text::MatchesAll("ancient nord sword", w) == false, "text: every word must occur");
        Check(Text::MatchesAll("iron sword", w), "text: all words occur");
        Check(Text::MatchesAll("anything", {}), "text: no words match everything");
    }

    void RunViewCoreTests()
    {
        Check(ViewCore::IsSlot("lineup3") && ViewCore::IsSlot("before") && !ViewCore::IsSlot("lineup4"),
              "views: only the fixed slots");
        Check(!ViewCore::IsSlot(ViewCore::kPanelSlot), "views: the page cannot name the ImGui panel's slot");
        Check(ViewCore::MaxDrawing(400, false) == 4 && ViewCore::MaxDrawing(400, true) == 2,
              "views: 0.4.0 draws four, two on VR");
        Check(ViewCore::MaxDrawing(300, false) == 1, "views: before 0.4.0 one draws");
        Check(ViewCore::Pixels(300.0, 1.5) == 450, "views: CSS pixels times the scale");
        Check(ViewCore::Pixels(10.0, 1.0) == 64 && ViewCore::Pixels(9000.0, 1.0) == 4096, "views: clamped");
        Check(ViewCore::Pixels(0.0, 1.0) == 0 && ViewCore::Pixels(std::numeric_limits<double>::infinity(), 1.0) == 0, "views: not a box is 0");
    }

    void RunAnimPathTests()
    {
        using namespace AnimPathCore;
        Check(Normalize("/actors//character/animations/x.hkx/") == "actors\\character\\animations\\x.hkx",
              "anim: normalized");
        Check(UnderMeshes("C:\\Modlists\\x\\mods\\Pack\\meshes\\actors\\character\\animations\\a.hkx") ==
                  "actors\\character\\animations\\a.hkx",
              "anim: a mod folder's file is read from its meshes folder (MO2)");
        Check(UnderMeshes("C:/Game/Data/Meshes/actors/a.hkx") == "actors\\a.hkx", "anim: meshes in any case");
        Check(UnderMeshes("C:\\meshes\\Pack\\meshes\\actors\\a.hkx") == "actors\\a.hkx", "anim: the last meshes folder");
        Check(UnderMeshes("C:\\Data\\textures\\a.hkx").empty(), "anim: no meshes folder is none");
        Check(IsAnimation("actors\\character\\animations\\mt_idle.hkx"), "anim: an animation");
        Check(!IsAnimation("actors\\character\\behaviors\\0_master.hkx"), "anim: a behaviour graph is not");
        Check(!IsAnimation("actors\\character\\character assets\\skeleton.hkx"), "anim: a skeleton is not");
        Check(!IsAnimation("actors\\character\\animations\\readme.txt"), "anim: only .hkx");
        Check(!IsAnimation("actors\\character\\x.hkx"), "anim: outside animations is not");
        Check(IsAnimation("actors\\character\\animations\\openanimationreplacer\\pack\\a\\mt_walk.hkx"),
              "anim: a replacer's clip deep down is");

        const std::vector<std::string> hidden = { "secret", "actors\\character\\animations\\wip" };
        Check(Hidden("actors\\character\\animations\\secret\\a.hkx", hidden), "anim: a folder name hides anywhere");
        Check(!Hidden("actors\\character\\animations\\secretive\\a.hkx", hidden), "anim: never part of a name");
        Check(Hidden("actors\\character\\animations\\wip\\b\\a.hkx", hidden), "anim: a path hides from the top");
        Check(!Hidden("actors\\character\\animations\\wipx\\a.hkx", hidden), "anim: a path hides whole folders");
        Check(!Hidden("actors\\character\\animations\\secret.hkx", hidden), "anim: a file of that name is not a folder");

        std::vector<std::string> paths = {
            "actors\\character\\animations\\a.hkx",
            "actors\\character\\animations\\dance\\one.hkx",
            "actors\\character\\animations\\dance\\two.hkx",
            "actors\\character\\animations\\idle\\x.hkx",
            "actors\\dragon\\animations\\fly.hkx",
        };
        std::sort(paths.begin(), paths.end());
        const auto top = List(paths, "");
        Check(top.folders.size() == 1 && top.folders[0].name == "actors" && top.folders[0].count == 5, "anim: the top");
        const auto anims = List(paths, "actors\\character\\animations");
        Check(anims.folders.size() == 2 && anims.folders[0].name == "dance" && anims.folders[0].count == 2 &&
                  anims.files.size() == 1 && paths[anims.files[0]] == "actors\\character\\animations\\a.hkx",
              "anim: a folder's folders and files");
        Check(List(paths, "actors\\char").folders.empty(), "anim: a folder prefix is not a folder");
        const auto hits = Search(paths, Text::Words("DANCE two"), 10);
        Check(hits.size() == 1 && paths[hits[0]].ends_with("two.hkx"), "anim: search by words");
        Check(Search(paths, Text::Words("hkx"), 2).size() == 2, "anim: search stops at the limit");
        Check(Stem("a\\b\\Dance_01.HKX") == "Dance_01", "anim: stem");
        Check(!SafeToPlay(std::string_view("x.dll\0.hkx", 10)), "anim path: an embedded NUL is refused");
    Check(SafeToPlay("actors\\character\\animations\\a.hkx") && !SafeToPlay("..\\x.hkx") &&
                  !SafeToPlay("C:\\x.hkx") && !SafeToPlay("\\x.hkx") && !SafeToPlay("a\\x.nif"),
              "anim: only a relative .hkx plays");
    }

    void RunImageTests()
    {
        std::uint8_t px[12] = { 10, 20, 30, 0, 255, 255, 255, 255, 64, 64, 64, 128 };
        ImageCore::UnpremultiplySrgb(px, 3);
        Check(px[0] == 0 && px[1] == 0 && px[2] == 0, "image: a clear pixel is all zero");
        Check(px[4] == 255 && px[7] == 255, "image: an opaque pixel is untouched");
        Check(px[8] > 64 && px[11] == 128, "image: a half pixel is brightened, alpha kept");
        const std::uint8_t b[] = { 'M', 'a', 'n' };
        Check(ImageCore::Base64(b, 3) == "TWFu" && ImageCore::Base64(b, 2) == "TWE=" && ImageCore::Base64(b, 1) == "TQ==",
              "image: base64 with padding");
        std::uint8_t s[4] = { 1, 2, 3, 4 };
        ImageCore::SwapRedBlue(s, 1);
        Check(s[0] == 3 && s[2] == 1 && s[3] == 4, "image: red and blue swapped");
    }

    void RunTurnTests()
    {
        Check(TurnCore::TurnFor(0, 24) == 0.0f && TurnCore::TurnFor(6, 24) == 90.0f && TurnCore::TurnFor(12, 24) == 180.0f &&
                  TurnCore::TurnFor(18, 24) == -90.0f,
              "turn: angles round the figure");
        Check(TurnCore::FrameAt(0, 25.0, 10.0, 24) == 2 && TurnCore::FrameAt(0, -5.0, 10.0, 24) == 23,
              "turn: a drag steps frames both ways");
        Check(TurnCore::FrameAt(3, std::numeric_limits<double>::infinity(), 10.0, 24) == 3, "turn: a bad drag keeps the frame");
        Check(TurnCore::ClampFrames(100) == 32 && TurnCore::ClampFrames(1) == 4, "turn: frame count clamped");
    }

    void RunItemTests()
    {
        using namespace ItemCore;
        Check(ArmorKind(kSlotBody | kSlotHead) == Kind::kBody, "item: a hooded robe is body wear");
        Check(ArmorKind(kSlotCirclet) == Kind::kHead && ArmorKind(kSlotShield) == Kind::kShield &&
                  ArmorKind(kSlotRing) == Kind::kJewelry && ArmorKind(0) == Kind::kOther,
              "item: armour kinds");
        Check(WeaponKind(9) == Kind::kBow && WeaponKind(8) == Kind::kStaff && WeaponKind(1) == Kind::kWeapon,
              "item: weapon kinds");
        Check(InFilter(Kind::kBody, "outfit") && !InFilter(Kind::kShield, "outfit") && !InFilter(Kind::kBow, "outfit"),
          "item filter: outfit is every wearable but a shield");
    Check(InFilter(Kind::kBow, "weapons") && !InFilter(Kind::kBow, "armor") && InFilter(Kind::kHead, "head") &&
                  !InFilter(Kind::kBody, "head") && InFilter(Kind::kBody, "all"),
              "item: filters");
        Check(Plain({}) && !Plain({ .tempered = true }) && !Plain({ .enchanted = true }) && !Plain({ .named = true }),
              "item: only untouched copies are plain");
        Check(!IsName(true, false, false, true) && IsName(true, true, false, true) && IsName(true, false, false, false),
              "item: the engine's tempered cache is not a name");
    }

    void RunTryOnTests()
    {
        using namespace TryOnCore;
        Check(Refusal({ .player = true, .loaded = true }) == "player", "tryon: the player is refused");
        Check(Refusal({ .child = true, .loaded = true }) == "child", "tryon: a child is refused");
        Check(Refusal({ .loaded = false }) == "notLoaded" && Refusal({ .loaded = true }).empty(), "tryon: loaded adults");
        Check(Lends(0) && !Lends(1), "tryon: lends only what they hold none of");
        Ledger l;
        l.Lent(0x1234);
        l.Lent(0x1234);
        Check(l.Of(0x1234) == 2 && l.Of(0x99) == 0, "tryon: the ledger counts");
        Check(TakeBack(2, 5) == 2 && TakeBack(2, 1) == 1 && TakeBack(2, 0) == 0, "tryon: never more than plain held");
    }

    void RunPeopleTests()
    {
        using namespace PeopleCore;
        Check(Listed(Group::kFollower, 99999.0f, true) && !Listed(Group::kNearby, 10.0f, true) &&
                  !Listed(Group::kNearby, 5000.0f, false) && Listed(Group::kNearby, 100.0f, false),
              "people: who is listed");
        const Person p{ Group::kPlayer, 0.0f, "Z" }, f{ Group::kFollower, 9.0f, "B" }, n1{ Group::kNearby, 50.0f, "A" },
            n2{ Group::kNearby, 20.0f, "C" };
        Check(Before(p, f) && Before(f, n1) && Before(n2, n1), "people: player, followers, nearest first");
    }

    void RunHostTests()
    {
        namespace H = HostCore;
        using P = H::Preference;
        using K = H::Kind;
        Check(H::ParsePreference("prisma") == P::Prisma && H::ParsePreference("magelight") == P::Magelight &&
                  H::ParsePreference("") == P::Auto && H::ParsePreference("Prisma") == P::Auto,
              "host: the setting's three words, anything else auto");
        Check(H::Choose(P::Auto, true, true, false) == K::Magelight, "host: auto takes Magelight first");
        Check(H::Choose(P::Auto, false, true, false) == K::Prisma, "host: auto falls back to PrismaUI");
        Check(H::Choose(P::Prisma, true, true, false) == K::Prisma, "host: prisma forces PrismaUI when both exist");
        Check(H::Choose(P::Prisma, true, false, false) == K::Magelight, "host: prisma without PrismaUI takes Magelight");
        Check(H::Choose(P::Prisma, true, true, true) == K::Magelight && H::Choose(P::Auto, false, true, true) == K::None,
              "host: PrismaUI is never used on VR");
        Check(H::Choose(P::Magelight, false, false, false) == K::None, "host: none without either");
    Check(H::Choose(P::Magelight, false, true, false) == K::None && H::Choose(P::Magelight, true, true, false) == K::Magelight,
          "host: magelight never falls back to PrismaUI");

        H::Box in{ { 100, 50, 400, 300 }, { 100, 50, 400, 300 }, 16 };
        H::Box out;
        Check(H::ToTarget(in, 1920, 1080, 1920, 1080, out) && out.rect.x == 100 && out.clip.w == 400 && out.radius == 16,
              "host: same size maps one to one");
        Check(H::ToTarget(in, 1920, 1080, 3840, 2160, out) && out.rect.x == 200 && out.rect.w == 800 && out.radius == 32,
              "host: a target twice the page doubles the box and the radius");
        H::Box clipped{ { -100, 0, 400, 300 }, { 0, 0, 1920, 100 }, 0 };
        Check(H::ToTarget(clipped, 1920, 1080, 1920, 1080, out) && out.rect.x == -100 && out.clip.x == 0 &&
                  out.clip.w == 300 && out.clip.h == 100,
              "host: the rect stays whole (the picture fills it), the clip is cut to rect, page clip and target");
        H::Box gone{ { 2000, 0, 100, 100 }, { 2000, 0, 100, 100 }, 0 };
        Check(!H::ToTarget(gone, 1920, 1080, 1920, 1080, out), "host: a box off the target draws nothing");
        H::Box bad{ { 0, 0, 100, 100 }, { 0, 0, 100, 100 }, 0 };
        Check(!H::ToTarget(bad, 0, 1080, 1920, 1080, out) && !H::ToTarget(bad, 1920, 1080, NAN, 1080, out),
              "host: a zero or NaN size draws nothing");
        H::Box round{ { 0, 0, 100, 40 }, { 0, 0, 100, 40 }, 500 };
        Check(H::ToTarget(round, 100, 100, 100, 100, out) && out.radius == 20, "host: the radius is at most half the short side");
    }

    void RunPanelTests()
    {
        namespace P = PanelCore;
        const auto tall = P::Fit(600, 600);
        Check(tall.h == 600 && tall.w == 450, "panel: a square fits a figure by its height");
        const auto wide = P::Fit(300, 900);
        Check(wide.w == 300 && wide.h == 400, "panel: a narrow box fits a figure by its width");
        Check(P::Fit(100, 100).h == 0 && P::Fit(NAN, 500).h == 0 && P::Fit(-1, 500).h == 0,
              "panel: too small or not a size is no picture");

        P::ResizeGate gate;
        Check(gate.Update({ 400, 600 }, 0) == P::Size{ 400, 600 }, "panel: the first size is taken at once");
        Check(gate.Update({ 410, 615 }, 10) == P::Size{ 400, 600 }, "panel: a change within the slack is not followed");
        Check(gate.Update({ 600, 900 }, 20) == P::Size{ 400, 600 }, "panel: a new size waits");
        Check(gate.Update({ 600, 900 }, 200) == P::Size{ 400, 600 }, "panel: still waiting before it settles");
        Check(gate.Update({ 600, 900 }, 300) == P::Size{ 600, 900 }, "panel: a size that held still is taken");
        Check(gate.Update({ 800, 1200 }, 310) == P::Size{ 600, 900 } &&
                  gate.Update({ 900, 1350 }, 400) == P::Size{ 600, 900 } &&
                  gate.Update({ 900, 1350 }, 640) == P::Size{ 600, 900 } &&
                  gate.Update({ 900, 1350 }, 660) == P::Size{ 900, 1350 },
              "panel: a size still changing restarts the wait");
        Check(gate.Update({ 0, 0 }, 700) == P::Size{ 900, 1350 }, "panel: no box keeps the size");
        gate.Reset();
        Check(gate.Update({ 300, 450 }, 710) == P::Size{ 300, 450 }, "panel: after a reset the next size is taken at once");

        const auto o = P::Orbit(10, -4);
        Check(o[0] == 5.0f && o[1] == -4.0f, "panel: a drag turns as the pages do");
        Check(P::Zoom(1.0f) == -100.0f && P::Zoom(-2.0f) == 200.0f, "panel: the wheel up zooms in, as deltaY < 0");

        Check(P::DefaultPick({ 0x14, 0x1A2B, 0x3C4D }, 0x3C4D) == 0x3C4D, "panel: the crosshair actor when listed");
        Check(P::DefaultPick({ 0x14, 0x1A2B }, 0x9999) == 0x14, "panel: else the first listed, the player");
        Check(P::DefaultPick({}, 0x9999) == 0, "panel: nobody listed is nobody");
        Check(P::kLights[0].value == "studio" && P::kExpressions[0].value.empty(),
              "panel: the defaults are the pages' (studio light, the game's expression)");
        Check(P::Note("dead") == "They are dead." && P::Note("tooMany") == "Too many views are open.",
              "panel: a known reason in the page's words");
        Check(P::Note("weird") == "The picture isn't available (weird).", "panel: an unknown reason is named");
    }
}
