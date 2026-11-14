// PhantasmProbe - a development client of Phantasm, through the public header only (never shipped).
//
// F7 toggles a HUD panel (a Magelight Hud view, click-through, drawn during play) that shows a live Phantasm view of
// the actor under the crosshair, or the player when there is none. Ctrl+F7 cycles the pose hint; Alt+F7 cycles the
// panel size. While it is open the numpad drives the camera: + and - zoom
// toward the aim point, 8/2/4/6 move that point, 7/9 turn, 1/3 tilt, 5 resets. The panel takes no input, so these are
// keys rather than the mouse; without a numpad the arrows turn (left / right) and tilt (up / down), Page Up and Page
// Down zoom and Home resets. The page is Data/Magelight/PhantasmProbe/views/probe/index.html.
//
// Shift+F7 plays the next stage on the view's actor (the "idle" hint's file: form, with the captured pose and motion
// on), Ctrl+Shift+F7 the one before; Ctrl+F7 back to the classic pose also goes back to the idle cycle. Ctrl+Alt+F7
// brings a partner into the picture (the "scene" hint: the actor under the crosshair, else the nearest other one) or
// sends them away. A stage is a line of Data/SKSE/Plugins/PhantasmProbe/clips.txt, "<first position> | <second>"
// (paths under meshes, '#' starts a comment; the second may be left off), else five stages of vanilla idles.
// The first position goes to the woman of a pair with one, the view's actor otherwise. Alt+Shift+F7 toggles the "aim"
// hint between face and figure (Phantasm 1.1.0+): with a stage playing, the camera keeps the face in view however
// the stage turns or lays the actor down; the zoom then closes on the face, so 8/2/4/6 do nothing.
//
// Smaller panels at the right of the display, each a view in a slot of its own, so up to four views draw at once
// (Phantasm 0.4.0+; an older one shows them waiting): F8 toggles one of the actor under the crosshair or the player,
// Ctrl+F8 one of an item (the item under the crosshair, else the right-hand weapon of the actor under it or of the
// player, else their body armour), Alt+F8 another actor panel. The keys above drive the first panel only.
// Shift+F8 asks for a Cutout still of the actor under the crosshair or the player, Ctrl+Shift+F8 for an item still of
// the item panel's item (or the item Ctrl+F8 would pick), and each logs the answer (since 0.4.0 a shot draws beside
// the open views; before, it answered view-drawing while one drew). Alt+Shift+F8 toggles the "freeze" hint on the
// first panel (Phantasm 1.0.0+) and logs QueryHint("freeze"): freeze it, open F8 on the same actor, re-dress them.

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include "MagelightUI_Mod.h"
#include "Phantasm_API.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <format>
#include <string>
#include <vector>

SKSEPluginInfo(
    .Version = { 0, 1, 0, 0 },
    .Name = "PhantasmProbe",
    .Author = "Severause",
    .SupportEmail = "",
    .StructCompatibility = SKSE::StructCompatibility::Independent,
    .RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary,
    .MinimumSKSEVersion = { 2, 0, 0, 0 }
)

namespace
{
    constexpr std::uint32_t kKey  = 0x41;   // F7
    constexpr std::uint32_t kKey2 = 0x42;   // F8
    // DirectInput scan codes of the numpad.
    constexpr std::uint32_t kNum7 = 0x47, kNum8 = 0x48, kNum9 = 0x49, kNumMinus = 0x4A, kNum4 = 0x4B, kNum5 = 0x4C,
                            kNum6 = 0x4D, kNumPlus = 0x4E, kNum1 = 0x4F, kNum2 = 0x50, kNum3 = 0x51;
    // The arrows and Home / Page Up / Page Down, for a keyboard with no numpad (the numpad's codes with 0x80 set).
    constexpr std::uint32_t kHome = 0xC7, kUp = 0xC8, kPageUp = 0xC9, kLeft = 0xCB, kRight = 0xCD, kDown = 0xD0,
                            kPageDown = 0xD1;
    constexpr int kPanelX = 40, kPanelY = 60;
    // The card around the picture: margin 6 + border 1 + padding 8 a side, and the two text lines below it.
    constexpr int kChromeW = 30, kChromeH = 30 + 44;
    // The picture's height as a share of the display's; 0 is the old fixed 360x480.
    constexpr float kSizes[] = { 0.80f, 0.55f, 0.0f };

    const PHANTASM_API::Interface1* g_api    = nullptr;
    PHANTASM_API::ClientId          g_client = 0;
    PHANTASM_API::ViewId            g_view   = 0;   // the Phantasm view, 0 while the panel is closed
    std::uint32_t                   g_actor  = 0;   // who g_view shows
    MAGELIGHT_API::MagelightMod     g_mod;
    MAGELIGHT_API::ViewId           g_page = 0;
    bool                            g_captured = false;
    bool                            g_frozen   = false;   // the first panel's "freeze" hint
    bool                            g_faceAim  = false;   // the first panel's "aim" hint: face, else figure
    std::size_t                     g_size  = 0;   // index into kSizes
    std::uint32_t                   g_picW = 360, g_picH = 480;
    float                           g_aimU = 0.5f, g_aimV = 0.5f;
    struct Stage
    {
        std::string first, second;   // the files of the two positions; second may be empty
    };
    std::vector<Stage>              g_clips;          // the stages Shift+F7 steps through
    int                             g_clip    = -1;   // the one playing, -1 for the idle cycle
    std::uint32_t                   g_partner = 0;    // the scene's other actor, 0 for none
    // The smaller panels (the F8 keys), placed from the top right: column 0 is the rightmost, row 0 the top.
    struct Panel
    {
        const char*           slot;       // the Phantasm slot, and the page's name with "probe" before it
        bool                  item;       // an item view, else an actor view
        std::uint32_t         w, h;       // the picture's size
        int                   col, row;
        PHANTASM_API::ViewId  view     = 0;
        PHANTASM_API::ViewId  lastView = 0;   // its last view, so the Ended after a close still goes there
        MAGELIGHT_API::ViewId page     = 0;
        std::uint32_t         target   = 0;   // the actor or item it shows
    };
    Panel g_panels[] = { { "side", false, 240, 320, 0, 0 }, { "item", true, 240, 240, 0, 1 },
                         { "third", false, 240, 320, 1, 1 } };
    constexpr std::size_t kSidePanel = 0, kItemPanel = 1, kThirdPanel = 2;

    std::string JsString(std::string_view s) { return nlohmann::json(std::string(s)).dump(); }

    void PageCallTo(MAGELIGHT_API::ViewId page, const std::string& script)
    {
        if (page && g_mod.v4()) g_mod.v4()->InvokeJS(page, script.c_str());
    }

    void PageCall(const std::string& script) { PageCallTo(g_page, script); }

    /// Sizes the picture (3:4, as Framing::Figure draws) from the display and lays the panel and page out to it.
    void ApplySize()
    {
        std::int32_t dw = 0, dh = 0;
        g_mod.v4()->GetDisplaySize(&dw, &dh);
        const float share = kSizes[g_size];
        std::uint32_t h = 480;
        if (share > 0.0f) {
            h = static_cast<std::uint32_t>((dh > 0 ? dh : 1200) * share);
            if (dh > 0) h = std::min<std::uint32_t>(h, static_cast<std::uint32_t>(dh - kPanelY - kChromeH - 10));
        }
        g_picH = std::max<std::uint32_t>(h, 120);
        g_picW = g_picH * 3 / 4;
        g_mod.v4()->SetViewBounds(g_page, kPanelX, kPanelY, static_cast<int>(g_picW) + kChromeW,
                                  static_cast<int>(g_picH) + kChromeH);
        PageCall(std::format("window.probeLayout({}, {})", g_picW, g_picH));
    }

    void Aim(float du, float dv)
    {
        g_aimU = std::clamp(g_aimU + du, 0.0f, 1.0f);
        g_aimV = std::clamp(g_aimV + dv, 0.0f, 1.0f);
        PageCall(std::format("window.probeAim({}, {})", g_aimU, g_aimV));
    }

    bool OpenOn(std::uint32_t actor)
    {
        const auto r = g_api->OpenView(g_client, "main", actor, g_picW, g_picH, PHANTASM_API::Framing::Figure, &g_view);
        SKSE::log::info("probe: OpenView({:08X}, {}x{}) = {}, view {}", actor, g_picW, g_picH, static_cast<int>(r),
                        g_view);
        if (r != PHANTASM_API::Result::Ok) return false;
        g_actor = actor;
        PageCall(std::format("window.probeStatus({})", JsString(std::format("opening {:08X}", actor))));
        return true;
    }

    void OnPhantasmEvent(const PHANTASM_API::Event* e, void*)
    {
        using PHANTASM_API::EventKind;
        Panel* panel = nullptr;
        for (auto& p : g_panels) {
            if (e->view && e->view == p.lastView) panel = &p;
        }
        const auto page = panel ? panel->page : g_page;
        switch (e->kind) {
        case EventKind::Shown:
            SKSE::log::info("probe: view {} shown: {} ({}x{}) url {} srv {}{}", e->view, e->imageName, e->width,
                            e->height, e->imageUrl, static_cast<const void*>(e->srv), e->lastSeen ? " (last seen)" : "");
            PageCallTo(page, std::format("window.probeShow({}, {}, {}, {})", JsString(e->imageUrl),
                                         JsString(e->imageName), e->width, e->height));
            break;
        case EventKind::Waiting:
            SKSE::log::info("probe: view {} waiting ({})", e->view, e->reason);
            PageCallTo(page, std::format("window.probeStatus({})", JsString(std::string("waiting: ") + e->reason)));
            break;
        case EventKind::Ended:
            SKSE::log::info("probe: view {} ended ({})", e->view, e->reason);
            if (e->view == g_view) {
                g_view    = 0;
                g_clip    = -1;
                g_partner = 0;
            }
            if (panel && e->view == panel->view) panel->view = 0;
            PageCallTo(page, std::format("window.probeStatus({})", JsString(std::string("ended: ") + e->reason)));
            break;
        case EventKind::Reply:
            SKSE::log::info("probe: reply {} = {}", e->key, e->json);
            break;
        }
    }

    std::string Trimmed(std::string v)
    {
        while (!v.empty() && std::isspace(static_cast<unsigned char>(v.back()))) v.pop_back();
        while (!v.empty() && std::isspace(static_cast<unsigned char>(v.front()))) v.erase(v.begin());
        return v;
    }

    /// clips.txt's stages, else five stages of vanilla idles (Skyrim - Animations.bsa), one for each position.
    void LoadClipList()
    {
        wchar_t buf[MAX_PATH]{};
        ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
        const auto list = std::filesystem::path(buf).parent_path() / L"Data" / L"SKSE" / L"Plugins" / L"PhantasmProbe" /
                          L"clips.txt";
        if (std::ifstream in(list); in) {
            for (std::string line; std::getline(in, line);) {
                if (const auto hash = line.find('#'); hash != std::string::npos) line.erase(hash);
                const auto bar = line.find('|');
                Stage      st{ Trimmed(line.substr(0, bar)), bar == std::string::npos ? std::string{} : Trimmed(line.substr(bar + 1)) };
                if (!st.first.empty()) g_clips.push_back(std::move(st));
            }
        }
        if (g_clips.empty()) {
            static constexpr const char* kVanillaStages[][2] = {
                { "female\\mt_idle.hkx", "male\\mt_idle.hkx" },
                { "idlewarmhands.hkx", "idlewarmarms.hkx" },
                { "idlestudy.hkx", "idlewipebrow.hkx" },
                { "1hm_idle.hkx", "2hm_idle.hkx" },
                { "shd_blockidle.hkx", "sneakmtidle.hkx" },
            };
            for (const auto& stage : kVanillaStages) {
                g_clips.push_back({ std::format("actors\\character\\animations\\{}", stage[0]),
                                    std::format("actors\\character\\animations\\{}", stage[1]) });
            }
        }
        SKSE::log::info("probe: {} stages for Shift+F7", g_clips.size());
    }

    bool IsFemale(std::uint32_t fid)
    {
        auto* a    = RE::TESForm::LookupByID<RE::Actor>(fid);
        auto* base = a ? a->GetActorBase() : nullptr;
        return base && base->GetSex() == RE::SEX::kFemale;
    }

    std::string BaseNameOf(const std::string& path)
    {
        const auto slash = path.find_last_of("\\/");
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }

    /// Plays stage i (-1: back to the idle cycle): the view's actor one position, the partner (when there is one
    /// and the stage has a second) the other, in the scene hint. A file plays only with the captured pose and motion
    /// on, so those are set with it.
    void PlayClip(int i)
    {
        if (!g_view || g_clips.empty()) return;
        g_clip = i;
        if (i < 0) {
            g_api->SetHint(g_view, "scene", "");
            g_api->SetHint(g_view, "idle", "");
            PageCall(std::format("window.probeStatus({})", JsString("idle cycle")));
            return;
        }
        g_captured = true;
        g_api->SetHint(g_view, "pose", "captured");
        g_api->SetHint(g_view, "motion", "on");
        const auto& st   = g_clips[static_cast<std::size_t>(i)];
        const bool  pair = g_partner && !st.second.empty();
        // The first position to the woman of a pair with one, else to the view's actor.
        const bool  mineFirst = !pair || IsFemale(g_actor) || !IsFemale(g_partner);
        const auto& mine      = mineFirst ? st.first : st.second;
        const auto& theirs    = mineFirst ? st.second : st.first;
        g_api->SetHint(g_view, "scene", pair ? std::format("{:08X}|file:{}", g_partner, theirs).c_str() : "");
        g_api->SetHint(g_view, "idle", ("file:" + mine).c_str());
        PageCall(std::format("window.probeStatus({})",
                             JsString(std::format("stage {}/{}: {}{}", i + 1, g_clips.size(), BaseNameOf(mine),
                                                  pair ? std::format(" + {:08X} {}", g_partner, BaseNameOf(theirs))
                                                       : std::string{}))));
        SKSE::log::info("probe: stage {}/{}: {:08X} plays {}{}", i + 1, g_clips.size(), g_actor, mine,
                        pair ? std::format(", {:08X} plays {}", g_partner, theirs) : std::string{});
    }

    /// The scene's other actor: the one under the crosshair, else the nearest loaded actor within 3000 units; 0 for none.
    std::uint32_t PickPartner()
    {
        if (auto* pick = RE::CrosshairPickData::GetSingleton()) {
            const auto ref = pick->GetActiveTarget().get();
            if (ref && ref->As<RE::Actor>() && ref->GetFormID() != g_actor) return ref->GetFormID();
        }
        auto* self  = RE::TESForm::LookupByID<RE::Actor>(g_actor);
        auto* lists = RE::ProcessLists::GetSingleton();
        if (!self || !lists) return 0;
        std::uint32_t best = 0;
        float         bestD = 3000.0f;
        for (auto& h : lists->highActorHandles) {
            const auto a = h.get();
            if (!a || a->GetFormID() == g_actor || a->IsDead() || !a->Get3D()) continue;
            const float d = a->GetPosition().GetDistance(self->GetPosition());
            if (d < bestD) {
                bestD = d;
                best  = a->GetFormID();
            }
        }
        if (g_actor != 0x14) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (player && player->GetPosition().GetDistance(self->GetPosition()) < bestD) best = 0x14;
        }
        return best;
    }

    void TogglePartner()
    {
        g_partner = g_partner ? 0 : PickPartner();
        SKSE::log::info("probe: partner {:08X}", g_partner);
        if (g_clip >= 0) {
            PlayClip(g_clip);
        } else {
            PageCall(std::format("window.probeStatus({})",
                                 JsString(g_partner ? std::format("partner {:08X}: Shift+F7 plays a stage", g_partner)
                                                    : std::string("no partner"))));
        }
    }

    void StepClip(int dir)
    {
        const int n = static_cast<int>(g_clips.size());
        if (n == 0) return;
        PlayClip(g_clip < 0 ? (dir > 0 ? 0 : n - 1) : (g_clip + dir + n) % n);
    }

    std::uint32_t Target()
    {
        if (auto* pick = RE::CrosshairPickData::GetSingleton()) {
            const auto ref = pick->GetActiveTarget().get();
            if (ref && ref->As<RE::Actor>()) return ref->GetFormID();
        }
        return 0x14;
    }

    void Toggle()
    {
        if (!g_api || !g_page) return;
        if (g_view) {
            // The clip and the scene end with the view (Phantasm puts them back to the idle cycle).
            g_clip    = -1;
            g_partner = 0;
            g_api->CloseView(g_view);
            g_view = 0;
            g_mod.v4()->ShowView(g_page, false);
            return;
        }
        ApplySize();
        g_aimU = g_aimV = 0.5f;
        if (!OpenOn(Target())) return;
        g_mod.v4()->ShowView(g_page, true);
    }

    /// An item for the item panel and the item still: the base form of the item under the crosshair; else the
    /// right-hand weapon of the actor under it, or of the player; else their body armour. 0 for none.
    std::uint32_t PickItem()
    {
        RE::NiPointer<RE::TESObjectREFR> ref;
        if (auto* pick = RE::CrosshairPickData::GetSingleton()) ref = pick->GetActiveTarget().get();
        if (ref && !ref->As<RE::Actor>()) {
            if (auto* base = ref->GetBaseObject()) {
                using FT = RE::FormType;
                switch (base->GetFormType()) {
                case FT::Weapon: case FT::Armor: case FT::Misc: case FT::Book: case FT::Ammo: case FT::Ingredient:
                case FT::AlchemyItem: case FT::KeyMaster: case FT::SoulGem: case FT::Scroll: case FT::Light:
                    return base->GetFormID();
                default:
                    break;
                }
            }
        }
        RE::Actor* actor = ref ? ref->As<RE::Actor>() : nullptr;
        if (!actor) actor = RE::PlayerCharacter::GetSingleton();
        if (!actor) return 0;
        if (auto* held = actor->GetEquippedObject(false); held && held->Is(RE::FormType::Weapon)) return held->GetFormID();
        if (auto* body = actor->GetWornArmor(RE::BGSBipedObjectForm::BipedObjectSlot::kBody)) return body->GetFormID();
        return 0;
    }

    /// A still's answer: logged, and shown on the status line of the panel it was asked for (user), else the first.
    void OnStill(const PHANTASM_API::Still* s, void* user)
    {
        if (!s) return;
        SKSE::log::info("probe: still {} {} ({}x{}, standing {:.0f} px){}", s->requestId, s->ok ? "ok" : "refused",
                        s->width, s->height, s->standingPx, s->ok ? std::string{} : std::string(" - ") + s->reason);
        const auto* panel = static_cast<const Panel*>(user);
        PageCallTo(panel && panel->view ? panel->page : g_page,
                   std::format("window.probeStatus({})", JsString(s->ok ? std::format("still {}x{}", s->width, s->height)
                                                                        : std::string("still refused: ") + s->reason)));
    }

    void RequestCutout()
    {
        if (!g_api) return;
        std::uint32_t id    = 0;
        const auto    actor = Target();
        const auto    r = g_api->RequestStill(g_client, actor, 256, 320, PHANTASM_API::Framing::Cutout, &OnStill,
                                              &g_panels[kSidePanel], &id);
        SKSE::log::info("probe: RequestStill(Cutout, {:08X}) = {}, request {}", actor, static_cast<int>(r), id);
    }

    void RequestItemShot()
    {
        if (!g_api || !g_api->RequestItemStill) return;
        auto&               panel = g_panels[kItemPanel];
        const std::uint32_t item  = panel.view && panel.target ? panel.target : PickItem();
        if (!item) {
            SKSE::log::info("probe: no item for an item still");
            return;
        }
        std::uint32_t id = 0;
        const auto    r  = g_api->RequestItemStill(g_client, item, 256, 256, "turn=30", &OnStill, &panel, &id);
        SKSE::log::info("probe: RequestItemStill({:08X}) = {}, request {}", item, static_cast<int>(r), id);
    }

    /// Opens or closes one of the smaller panels, at its place from the top right of the display.
    void TogglePanel(Panel& p)
    {
        if (!g_api || !p.page) return;
        if (p.view) {
            g_api->CloseView(p.view);
            p.view = 0;
            g_mod.v4()->ShowView(p.page, false);
            return;
        }
        std::int32_t dw = 0, dh = 0;
        g_mod.v4()->GetDisplaySize(&dw, &dh);
        const int w = static_cast<int>(p.w) + kChromeW, h = static_cast<int>(p.h) + kChromeH;
        // Each column as wide, and each row as tall, as the widest and tallest panel: 240x320 plus the card.
        const int cellW = 240 + kChromeW + 10, cellH = 320 + kChromeH + 10;
        const int x     = std::max(0, (dw > 0 ? dw : 1920) - kPanelX - w - p.col * cellW);
        g_mod.v4()->SetViewBounds(p.page, x, kPanelY + p.row * cellH, w, h);
        PageCallTo(p.page, std::format("window.probeLayout({}, {})", p.w, p.h));
        p.target = p.item ? PickItem() : Target();
        if (!p.target) {
            SKSE::log::info("probe: no item for the item panel");
            return;
        }
        const auto r = p.item ? (g_api->OpenItemView ? g_api->OpenItemView(g_client, p.slot, p.target, p.w, p.h, &p.view)
                                                     : PHANTASM_API::Result::Unsupported)
                              : g_api->OpenView(g_client, p.slot, p.target, p.w, p.h, PHANTASM_API::Framing::Figure, &p.view);
        SKSE::log::info("probe: panel '{}' Open{}View({:08X}) = {}, view {}", p.slot, p.item ? "Item" : "", p.target,
                        static_cast<int>(r), p.view);
        if (r != PHANTASM_API::Result::Ok) return;
        p.lastView = p.view;
        PageCallTo(p.page, std::format("window.probeStatus({})", JsString(std::format("opening {:08X}", p.target))));
        g_mod.v4()->ShowView(p.page, true);
    }

    void CycleSize()
    {
        g_size = (g_size + 1) % std::size(kSizes);
        ApplySize();
        if (g_view) g_api->ResizeView(g_view, g_picW, g_picH);
    }

    /// A fresh view of the same actor: the camera starts over (OpenView on an open slot keeps its camera).
    void ResetCamera()
    {
        g_api->CloseView(g_view);
        g_view = 0;
        g_aimU = g_aimV = 0.5f;
        Aim(0.0f, 0.0f);
        if (!OpenOn(g_actor)) return;
        // A new view starts from no hints: the pose, the freeze, the aim and the stage come back with it.
        if (g_frozen) g_api->SetHint(g_view, "freeze", "on");
        if (g_faceAim) g_api->SetHint(g_view, "aim", "face");
        if (g_captured) {
            g_api->SetHint(g_view, "pose", "captured");
            g_api->SetHint(g_view, "motion", "on");
        }
        if (g_clip >= 0) PlayClip(g_clip);
    }

    /// The numpad camera keys, while the panel is open. False for any other key.
    bool CameraKey(std::uint32_t code)
    {
        constexpr float kAimStep = 0.05f;
        switch (code) {
        case kNumPlus:  g_api->Zoom(g_view, -200.0f, g_aimU, g_aimV); return true;   // a wheel's deltaY: up zooms in
        case kNumMinus: g_api->Zoom(g_view, 200.0f, g_aimU, g_aimV); return true;
        case kNum8:     Aim(0.0f, -kAimStep); return true;
        case kNum2:     Aim(0.0f, kAimStep); return true;
        case kNum4:     Aim(-kAimStep, 0.0f); return true;
        case kNum6:     Aim(kAimStep, 0.0f); return true;
        case kNum7:     g_api->Orbit(g_view, -15.0f, 0.0f); return true;
        case kNum9:     g_api->Orbit(g_view, 15.0f, 0.0f); return true;
        case kNum1:     g_api->Orbit(g_view, 0.0f, -20.0f); return true;   // dy is drag pixels: half a degree each
        case kNum3:     g_api->Orbit(g_view, 0.0f, 20.0f); return true;
        case kNum5:     ResetCamera(); return true;
        case kPageUp:   g_api->Zoom(g_view, -200.0f, g_aimU, g_aimV); return true;
        case kPageDown: g_api->Zoom(g_view, 200.0f, g_aimU, g_aimV); return true;
        case kLeft:     g_api->Orbit(g_view, -15.0f, 0.0f); return true;
        case kRight:    g_api->Orbit(g_view, 15.0f, 0.0f); return true;
        case kUp:       g_api->Orbit(g_view, 0.0f, 20.0f); return true;
        case kDown:     g_api->Orbit(g_view, 0.0f, -20.0f); return true;
        case kHome:     ResetCamera(); return true;
        default:        return false;
        }
    }

    class KeySink final : public RE::BSTEventSink<RE::InputEvent*>
    {
    public:
        RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* events, RE::BSTEventSource<RE::InputEvent*>*) override
        {
            for (auto* e = events ? *events : nullptr; e; e = e->next) {
                auto* b = e->AsButtonEvent();
                if (!b || b->GetDevice() != RE::INPUT_DEVICE::kKeyboard || !b->IsDown()) continue;
                if (RE::UI::GetSingleton()->GameIsPaused()) continue;
                const bool shift = (::GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                const bool ctrl  = (::GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
                if (b->GetIDCode() == kKey2) {
                    const bool alt2 = (::GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
                    if (shift && ctrl) RequestItemShot();
                    else if (shift && alt2) {
                        if (g_view) {
                            g_frozen = !g_frozen;
                            g_api->SetHint(g_view, "freeze", g_frozen ? "on" : "off");
                            g_api->QueryHint(g_view, "freeze");
                            SKSE::log::info("probe: freeze {} on the first panel", g_frozen ? "on" : "off");
                        }
                    }
                    else if (shift) RequestCutout();
                    else if (ctrl) TogglePanel(g_panels[kItemPanel]);
                    else if (alt2) TogglePanel(g_panels[kThirdPanel]);
                    else TogglePanel(g_panels[kSidePanel]);
                    continue;
                }
                if (b->GetIDCode() != kKey) {
                    if (g_view) CameraKey(b->GetIDCode());
                    continue;
                }
                const bool alt   = (::GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
                if (alt && ctrl && g_view) {
                    TogglePartner();
                } else if (alt && shift && g_view) {
                    g_faceAim = !g_faceAim;
                    g_api->SetHint(g_view, "aim", g_faceAim ? "face" : "figure");
                    SKSE::log::info("probe: the first panel aims at the {}", g_faceAim ? "face" : "figure");
                } else if (alt && g_page) {
                    CycleSize();
                } else if (shift && g_view) {
                    StepClip(ctrl ? -1 : 1);
                } else if (ctrl && g_view) {
                    g_captured = !g_captured;
                    if (!g_captured && g_clip >= 0) PlayClip(-1);   // a clip plays only in the captured pose
                    g_api->SetHint(g_view, "pose", g_captured ? "captured" : "classic");
                    g_api->SetHint(g_view, "motion", g_captured ? "on" : "off");
                    g_api->QueryHint(g_view, "idles");
                } else {
                    Toggle();
                }
            }
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    void OnDataLoaded()
    {
        const auto get = reinterpret_cast<PHANTASM_API::RequestPluginAPIFn>(
            ::GetProcAddress(::GetModuleHandleW(L"Phantasm.dll"), "RequestPluginAPI"));
        g_api = get ? static_cast<const PHANTASM_API::Interface1*>(get(PHANTASM_API::kInterface1)) : nullptr;
        if (!g_api) {
            SKSE::log::error("probe: Phantasm is not loaded or serves no interface 1");
            return;
        }
        const auto rc = g_api->RegisterClient("probe", &OnPhantasmEvent, nullptr, &g_client);
        SKSE::log::info("probe: Phantasm {} - RegisterClient = {}, client {}", g_api->pluginVersion, static_cast<int>(rc),
                        g_client);

        if (!g_mod.acquire("PhantasmProbe", "Phantasm Probe", 1, 3, 0)) {
            SKSE::log::error("probe: Magelight 1.3.0+ is not available");
            return;
        }
        wchar_t buf[MAX_PATH]{};
        ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
        const auto page = std::filesystem::path(buf).parent_path() / L"Data" / L"Magelight" / L"PhantasmProbe" /
                          L"views" / L"probe" / L"index.html";
        const auto pageText = page.string();
        // Top left: a right anchor is resolved now, before Magelight knows the display size, and lands off-screen.
        // The size here is a placeholder: ApplySize sets it from the display at each open.
        g_page = g_mod.createView("probe", pageText.c_str(), MAGELIGHT_API::Layer::Hud,
                                  { .clickThrough = true, .startVisible = false, .anchor = MAGELIGHT_API::Anchor::TopLeft,
                                    .x = kPanelX, .y = kPanelY, .w = 360 + kChromeW, .h = 480 + kChromeH });
        if (!g_page) {
            SKSE::log::error("probe: Magelight refused the page: {}", g_mod.v4()->GetLastErrorMessage(g_mod.id()));
            return;
        }
        for (auto& p : g_panels) {
            const std::string name = std::string("probe") + p.slot;
            p.page = g_mod.createView(name.c_str(), pageText.c_str(), MAGELIGHT_API::Layer::Hud,
                                      { .clickThrough = true, .startVisible = false,
                                        .anchor = MAGELIGHT_API::Anchor::TopLeft, .x = kPanelX, .y = kPanelY,
                                        .w = static_cast<int>(p.w) + kChromeW, .h = static_cast<int>(p.h) + kChromeH });
            if (!p.page) SKSE::log::warn("probe: Magelight refused the '{}' panel's page - its key does nothing", p.slot);
        }
        static KeySink s_sink;
        RE::BSInputDeviceManager::GetSingleton()->AddEventSink(&s_sink);
        LoadClipList();
        SKSE::log::info("probe: ready - F7 toggles the panel, Ctrl+F7 cycles the pose, Alt+F7 the size, Shift+F7 "
                        "plays the next stage and Ctrl+Shift+F7 the one before, Ctrl+Alt+F7 brings a partner in or "
                        "out, Alt+Shift+F7 toggles the face aim; the numpad zooms (+/-), aims (8/2/4/6), turns (7/9), tilts (1/3) and resets (5), as do the "
                        "arrows (turn, tilt), Page Up / Down (zoom) and Home (reset); F8 / Ctrl+F8 / Alt+F8 toggle an actor, an item and another "
                        "actor panel, Shift+F8 asks for a Cutout and Ctrl+Shift+F8 for an item still, Alt+Shift+F8 freezes the "
                        "first panel");
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse);
    SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* m) {
        if (m->type == SKSE::MessagingInterface::kDataLoaded) OnDataLoaded();
        if (m->type == SKSE::MessagingInterface::kPostLoadGame || m->type == SKSE::MessagingInterface::kNewGame) {
            // A load ended every view (Ended "game-load"); the panel closes with it.
            if (g_page) g_mod.v4()->ShowView(g_page, false);
            for (auto& p : g_panels) {
                p.view = 0;
                if (p.page) g_mod.v4()->ShowView(p.page, false);
            }
        }
    });
    return true;
}
