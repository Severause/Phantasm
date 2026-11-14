#pragma once
// MannequinRenderer — the live actor view, which Host.cpp drives for the API's views.
//
// LiveStart collects the actor's geometry ONCE (anchors, hair coverage, the
// classic pose's rest palettes and head re-parent, dyn-shape snapshot); the actor carrier menu
// (Carriers.h) draws those frozen entries every frame into an offscreen a
// client page shows as a Magelight texture image, or, for a hostless view with no Magelight, that the client
// draws itself from its SRV. Free trackball camera;
// TESEquipEvent drives debounced recollects.

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include "MirrorLightingCore.h"
#include "StillShotCore.h"

struct ID3D11ShaderResourceView;

namespace Phantasm
{
    namespace ViewRegistryCore
    {
        struct SceneCastEntry;
        struct ScenePropEntry;
    }
    // SEH net for the collect and the per-frame draws: they read raw BSGeometry /
    // skin-bone pointers off a live actor, and a 3D rebuild mid-walk leaves them
    // dangling (a wild non-null pointer can't be validated in C++). Returns false
    // on any caught SEH exception so the caller degrades instead of crashing.
    // Out-of-line: a function with __try can't own objects needing unwinding.
    bool RunGuarded(const std::function<void()>& fn) noexcept;

    class MannequinRenderer
    {
    public:
        // (channel, jsonPayload): the renderer's status and answers, wired to
        // Host::OnRendererEvent at startup. Called under LiveLock, from a Host
        // task or inside the carrier's render phase; a live status carries the
        // view and openedSeq LiveStart was given.
        using SendEventFn = std::function<void(const char*, const std::string&)>;

        static MannequinRenderer* GetSingleton()
        {
            static MannequinRenderer instance;
            return &instance;
        }

        void SetSendEventCallback(SendEventFn cb) { m_sendEvent = std::move(cb); }

        // The live status/image events go out here; ItemRenderer (bottom of
        // MannequinRenderer.cpp) uses it for its events too.
        void SendUiEvent(const char* channel, const std::string& payload)
        {
            if (m_sendEvent) {
                m_sendEvent(channel, payload);
            } else {
                SKSE::log::warn(
                    "MannequinRenderer::SendUiEvent: no send-event callback "
                    "registered — '{}' event dropped", channel);
            }
        }

        // Save-revert hook (Host.cpp, after LiveEnd): forgets the session - the
        // idle pool the clip player keeps (keyed by skeleton, sex, scale and idle
        // family), the per-NPC idle and expression hints, the kept looks, the
        // keepLook marks and the `here` light. Game thread.
        void ResetTransientState();

        // Registers the two carrier menus (Carriers.h) with the engine's UI. Once, at kDataLoaded (main.cpp);
        // a second call does nothing. Until it has run every live start is refused ("unrenderable").
        static void RegisterCarriers();

        // ── Live actor viewport ─────────────────────────────────────────
        // Each API view has a state of its own in the renderer, named by Host's view id (0 is a portrait shot's): its
        // actor, camera, image, display hints, scene and props. Several draw at once, each into its own offscreen, a
        // shot's view (0) beside the clients'.
        // Called from Host's posted tasks, which SKSE may run on a job thread
        // while a carrier's PostDisplay draws on the main thread: every entry
        // point holds the live state lock (LiveLock, MannequinRenderer.cpp).
        // w / h are the view's size in device pixels (CSS px x
        // devicePixelRatio), which size the offscreen; a side under 4 is drawn at 4
        // (LiveViewSizingCore::ViewPx). imageName is the Magelight
        // texture image the view publishes to: registered on first use and kept
        // for the process (Magelight's contract); the image a previous start
        // used is pointed at a transparent texture. With no Magelight nothing is
        // registered: hostless (the view's `hostless` hint) lets the view draw for
        // its client's own D3D11 draw, else the start is refused "no-image".
        // view / openedSeq are Host's start, stamped on every live status this start raises.
        void LiveStart(RE::FormID actorFormId, float w, float h, std::string_view imageName, std::uint32_t view,
                       std::uint64_t openedSeq, bool hostless = false);
        // Start with retries while the actor's 3D is briefly gone (a client's
        // try-on rebuild can tear it down for a moment). attempt 0 = a fresh
        // LiveStart, >0 = the 120 ms retry chain. Public for the retry's AddTask.
        void LiveStartDeferred(std::uint32_t view, RE::FormID actorFormId, float w, float h, int attempt);
        // The camera and size calls do nothing for a view the renderer does not hold.
        void LiveSetRect(std::uint32_t view, float w, float h);
        // A drag or zoom step that is not finite is ignored (MirrorCameraCore).
        void LiveDrag(std::uint32_t view, float dx, float dy);
        // u / v: the pointer's place in the view (0..1 from the top left), which the zoom moves toward.
        void LiveZoom(std::uint32_t view, float dz, float u = 0.5f, float v = 0.5f);
        // Recollect on the next drawn frame (the client says the look changed);
        // camera untouched, no-op unless live. Level-triggered, so
        // bursts coalesce.
        void LiveRefresh(std::uint32_t view);
        // Ends the view and forgets its state; LiveEnd() ends every actor view.
        void LiveEnd(std::uint32_t view);
        void LiveEnd();
        // The display hints, each the view's own (a view not started yet keeps them for its start). Game thread.
        // The view's pose (the "pose" hint): classic (false) is the doll at
        // rest (skinned onto the skeleton's rest pose, its bone scales kept);
        // captured (true) GPU-skins every skinned piece onto the actor's
        // current bones. A change recollects a live view.
        void SetCapturedPose(std::uint32_t view, bool captured);
        // The "motion" hint: the captured view follows the NPC, their
        // live bones while the game animates them and the clip they were
        // playing while it does not (paused, out of view). No effect on the
        // classic doll. A change recollects a live view.
        void SetMotion(std::uint32_t view, bool on);
        // The "weapons" hint: the view draws the weapons, shield and quiver where the game has them, drawn or
        // sheathed. A change recollects a live view.
        void SetWeapons(std::uint32_t view, bool on);
        // The "floor" hint: a floor under the NPC that catches their shadow. The "lighting" hint: the view's
        // lighting preset (MirrorLightingCore; any other text is the default, Hearth). A change
        // redraws.
        void SetFloor(std::uint32_t view, bool on);
        void SetLighting(std::uint32_t view, std::string_view preset);
        // The "aim" hint: face points the camera at the drawn head from where the face looks, following the head as
        // it moves; figure (or any other text) the chest-anchored orbit from the actor's heading. A change redraws.
        void SetAim(std::uint32_t view, std::string_view aim);
        // The "scale" hint: the view's actor drawn at this multiple of the size the game gives them, about their
        // place, while the idle the view chose (the "idle" hint) plays them. A change redraws.
        void SetFigureScale(std::uint32_t view, float scale);
        // The "freeze" hint: on, the view draws a still copy of the look it holds (once complete) and collects
        // nothing, through equips and rebuilds of the actor's 3D; off, it goes back to live. Game thread.
        void SetFreeze(std::uint32_t view, bool on);
        // Whether the view draws a frozen look now (QueryHint("freeze")'s held). Game thread.
        bool IsFrozen(std::uint32_t view) const;
        // A view's frame rates over the last second (QueryHint("stats"), ViewStatsCore). Game thread.
        struct ViewStats
        {
            bool        drawing    = false;   // an actor or item view of that id is active (not ended, not waiting)
            bool        valid      = false;   // a full second has been counted
            double      fps        = 0.0;     // drawn + skipped-unchanged frames a second
            double      drawnFps   = 0.0;
            double      skippedFps = 0.0;
            double      waitedFps  = 0.0;
            double      stalledFps = 0.0;     // turns that could not draw (a rebuild, a start loading, the GPU bundle)
            double      cpuMs      = 0.0;     // average CPU ms of a drawn frame
            const char* budget        = "";   // the budget mode set (LiveViewBudgetCore::ModeName)
            const char* budgetApplied = "";   // the mode in force now (smooth runs balanced while the game runs)
            double      budgetMs      = 0.0;  // the CPU ms a frame in force now
        };
        ViewStats GetViewStats(std::uint32_t view) const;
        // The live views' budget (Phantasm.ini [LiveViews]): smooth, balanced or light, and an explicit ms that
        // replaces the mode's budget when above 0. Any thread; logs the result.
        void SetLiveBudget(std::string_view mode, double overrideMs);
        // True once the live shaders failed to compile: "gpu-unavailable" is then final for the process, not a
        // device refusal that clears after LiveViewLifecycleCore::kGpuRetryDelay. Any thread.
        bool IsLiveGpuFailedForGood() const;
        // The idle the view shows for an NPC (the "idle" hint), kept per actor
        // for the session: "" runs the idle cycle, "still" (MannequinClipCore::kIdleStill)
        // holds the pose while the game does not animate them, a clip name loops
        // that idle whatever the game does, on the skeleton it first played on (a
        // werewolf or Vampire Lord runs the cycle until they are back). It plays
        // only while the view is captured and follows the NPC (the "pose" and
        // "motion" hints); otherwise the choice waits. Game thread.
        void SetMirrorIdle(RE::FormID actorFormId, std::string idle);
        // The other actors the view draws with its actor `origin` (the "scene" hint): each plays its idle at origin's
        // place, turned and moved by its offset. An empty cast, or a view of another actor, draws origin alone. Game
        // thread.
        void SetSceneCast(std::uint32_t view, RE::FormID origin, const std::vector<ViewRegistryCore::SceneCastEntry>& cast);
        // Drops the view's scene when it is origin's (its other actors back to the idle cycle, unless another view's
        // scene names them). Game thread.
        void EndSceneOf(std::uint32_t view, RE::FormID origin);
        // The models the view of `origin` draws in its scene (the "props" hint): each on an actor's node, or standing
        // at origin's place. Loads the models it has not loaded. Game thread.
        void SetSceneProps(std::uint32_t view, RE::FormID origin, const std::vector<ViewRegistryCore::ScenePropEntry>& props);
        // Drops the view's props when they are origin's. Game thread.
        void EndPropsOf(std::uint32_t view, RE::FormID origin);
        // Back to the idle cycle when the actor's chosen idle is still `idle` (any case); another choice stays. Game
        // thread.
        void EndMirrorIdleIf(RE::FormID actorFormId, std::string_view idle);
        // The expression the view shows for an NPC this session (the "expression" hint; FaceMorphCore ids, "" or
        // anything else is as in game), answered as mirrorExpression {formId, expression}, as RequestMirrorExpression
        // answers. Game thread.
        void SetMirrorExpression(RE::FormID actorFormId, std::string_view expression);
        void RequestMirrorExpression(RE::FormID actorFormId);
        // The idles the view can play for the NPC it shows, answered on the
        // next drawn frame as the mirrorIdles event. Game thread.
        void RequestMirrorIdles(RE::FormID actorFormId);
        // The "keepLook" hint: whether what the actor wears in this view is their look, which the view keeps as
        // their last look this session (LastCaptureCore). Off for a client that dresses them in a try-on. Game thread.
        void SetLookTransient(RE::FormID actorFormId, bool transient);
        // A live view's offscreen as published: its SRV, size and the image's URL ("" before the first frame, for
        // a view the renderer does not hold, and for a hostless view, which has no image). The SRV is the renderer's: AddRef it to keep it past the current task.
        // Game thread.
        struct ImageInfo
        {
            ID3D11ShaderResourceView* srv = nullptr;
            std::uint32_t             width = 0, height = 0;
            std::string               url;
        };
        ImageInfo ActorImage(std::uint32_t view) const;
        ImageInfo ItemImage(std::uint32_t view) const;
        // ── Portrait shots (the API's Cutout and Portrait stills) ───────
        // One frame of an actor drawn on a view of its own (API view 0), beside any client views: their standing idle
        // (their live pose where the clip player cannot run), a neutral face without blinks, no weapons or floor, on
        // a transparent ground, in the light and from the side ShotOptions name. The shot is the whole figure, or
        // their head and shoulders.
        struct PortraitShot
        {
            // RGBA8 rows of width * 4 bytes: the colour premultiplied by alpha in linear light, then sRGB-encoded
            // (the offscreen's _SRGB target), on transparent black.
            std::vector<std::uint8_t> rgba;
            std::uint32_t             width = 0, height = 0;
            float                     standingPx = 0.0f;   // the character's standing height in the shot's pixels
            bool                      classicPose = false; // drawn in the classic pose: the actor's skins were unbound
        };
        // Called once, in a task of its own (outside any render phase and the renderer's lock), with the shot, or
        // why there is none ("cancelled" whenever the shot's view ends first: PortraitCancel, or LiveEnd for any
        // reason, a dead actor or a fault included).
        using PortraitDone = std::function<void(bool ok, PortraitShot shot, std::string why)>;
        // A shot's choices. turnDeg: the camera's turn from the figure's front (degrees; positive toward the
        // Hearth key's side, the figure's left).
        struct ShotOptions
        {
            bool                       head     = false;
            float                      turnDeg  = 14.0f;
            float                      pitchDeg = 4.0f;
            MirrorLightingCore::Preset lighting = MirrorLightingCore::Preset::kHearth;
            bool                       weapons  = false;   // draw what they carry (a sheathed sword, a bow)
            // A head shot faces the drawn head, turnDeg and pitchDeg measured from where the face looks (FaceAimCore).
            StillShotCore::Aim         aim      = StillShotCore::Aim::kFigure;
            // A head shot's zoom (StillShotCore::HeadBelowBone): 1 the head and shoulders, about 2 the face alone.
            float                      zoom     = 1.0f;
        };
        // Starts a shot of a loaded actor. False, and done never called, when one cannot start now: another shot is
        // up, or the actor's 3D is not loaded. A shot needs no UI host (it publishes no image). After
        // a shot the view lingers StillShotCore::kShotLingerMs, and a shot of the same actor with the same weapons
        // choice reuses it (no new collect). Game thread.
        bool PortraitStart(RE::FormID actorFormId, PortraitDone done) { return PortraitStart(actorFormId, {}, done); }
        bool PortraitStart(RE::FormID actorFormId, const ShotOptions& options, PortraitDone done);
        // A picture of what the drawing view of an actor shows (RequestStill): its next frame drawn with the figure
        // and its textures settled (or the frame after kStillTimeout), read back and scaled to fit maxWidth x
        // maxHeight, on the view's backdrop. done is called once, in a task of its own, with the picture or a reason
        // token ("view-ended", "read-failed"). False, and done never called, when no view of that actor is drawing.
        // Game thread.
        bool StillOfView(RE::FormID actorFormId, std::uint32_t maxWidth, std::uint32_t maxHeight, PortraitDone done);
        // Answers "timed-out" to every still asked kStillGiveUp ago or more: a view that stopped drawing frames never
        // reaches TendStills. Game thread.
        void ExpireStills();
        // Ends a running shot, an actor's or an item's, as cancelled (no-op otherwise). Game thread.
        void PortraitCancel();
        // The same as timed out: Host's backstop for a shot whose frames stopped coming (a carrier that does not
        // draw), which would otherwise hold the shot slot, and for an item the save-refusing carrier, for good.
        void PortraitExpire();
        // A shot, an actor's or an item's, is running. Game thread.
        bool PortraitBusy() const;
        // Why a shot of this actor cannot start now (an API reason token), or null: "busy" (a shot runs), a live
        // start's refusal ("no-image", "unrenderable", "gpu-unavailable"), "missing", "dead" or "not-loaded". Game
        // thread.
        const char* PortraitRefusal(RE::FormID actorFormId) const;

        // ── Item shots (the API's item stills) ──────────────────────────
        // One frame of an item's model drawn on an item view of its own (API view 0), beside any views, on a transparent
        // ground (the shot's pixels as a PortraitShot's, standingPx 0), turned turnDeg from the item view's opening side (kItemYawDeg), after its
        // model, effects and particles have run StillShotCore::kItemSettleMs. worn: an armor drawn as worn by a man or
        // a woman (its addon's model, turned from the wearer's front), else its ground model. On success the view stays
        // up for StillShotCore::kItemShotLingerMs, so a next shot of the same item and model reuses its load. done as
        // PortraitStart's: "cancelled" when its view ends first, "no-model" when the item drew nothing. False, and
        // done never called, when one cannot start now (ItemShotRefusal). Game thread.
        bool ItemShotStart(RE::FormID itemFormId, float turnDeg, float pitchDeg, StillShotCore::Worn worn,
                           PortraitDone done);
        // Why an item shot cannot start now, or null: "busy" (a shot runs), a live start's refusal, "missing" (no
        // such form). Game thread.
        const char* ItemShotRefusal(RE::FormID itemFormId) const;
        // The shot scaled to fit maxW x maxH (never up), its standing height with it. Any thread.
        static void FitShot(PortraitShot& shot, std::uint32_t maxW, std::uint32_t maxH);

        // THE way to rebuild an actor's 3D (Update3DModel) wherever a view
        // might show them (the API's RebuildActor3D). Call from outside SKSE's
        // task drain (MutationLane.h has the deadlock). When the view is live on this actor it releases its
        // holds, rebuilds, and recollects IMMEDIATELY (not via the 250 ms equip
        // debounce; for the player after settling a world the paused rebuild
        // left stale), so no blank frame is presented; a failed recollect, or a
        // 3D not back yet, falls back to the debounced retry, which waits up to
        // LiveViewLifecycleCore::kAbsentBudget for the 3D. A view showing the
        // actor's kept look keeps it unless a collect after the rebuild
        // succeeds. Otherwise it only rebuilds. Logs entry and exit around the engine call (a hang shows
        // as a missing exit line); `who` names the caller in the log. Game
        // thread.
        void LiveRebuildActor3D(RE::Actor* actor, const char* who);

    private:
        MannequinRenderer() = default;
        MannequinRenderer(const MannequinRenderer&)            = delete;
        MannequinRenderer& operator=(const MannequinRenderer&) = delete;

        SendEventFn m_sendEvent;
    };

}  // namespace Phantasm
