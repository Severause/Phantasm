// Phantasm - Host: the API's clients and views over the renderer (contract in Host.h).

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include "Host.h"

#include "ItemRenderer.h"
#include "LiveViewLifecycleCore.h"
#include "MannequinRenderer.h"
#include "NeuralStills.h"
#include "PresentHandoff.h"
#include "StillShotCore.h"
#include "ViewRegistryCore.h"

#include <d3d11.h>
#include <cstddef>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace Phantasm::Host
{
    namespace
    {
        namespace VRC = ViewRegistryCore;

        struct Callback
        {
            EventFn fn   = nullptr;
            void*   user = nullptr;
        };

        // The registry and the callbacks: any thread, under g_mx. Never held while a client's callback runs or while
        // the renderer is called.
        std::mutex                             g_mx;
        VRC::Registry                          g_reg;
        std::unordered_map<ClientId, Callback> g_callbacks;

        // From here down: Host's posted tasks only. SKSE may run them on a job thread, but one at a time; the renderer's
        // events (OnRendererEvent) read none of this.

        // The views the renderer draws now, by id, each as it was when it started (with the hints applied to it), and
        // whether its Shown has gone out for that start.
        struct Drawing
        {
            VRC::View view;
            bool      shown = false;
        };
        std::unordered_map<ViewId, Drawing> g_drawing;
        // The views told Waiting, with the OpenView (openedSeq) they were told for: one Waiting per wait.
        std::unordered_map<ViewId, std::uint64_t> g_toldWaiting;
        // The actors the renderer marks as wearing a try-on: those of every open view with keepLook off.
        std::set<std::uint32_t> g_transientActors;

        // QueryHint calls waiting for the renderer's answer (mirrorIdles / mirrorExpression).
        struct PendingQuery
        {
            ViewId        view   = 0;
            ClientId      client = 0;
            std::uint32_t actor  = 0;
            std::string   key;
        };
        std::vector<PendingQuery> g_queries;

        // The shot still (Cutout, Portrait or an item's) the renderer is shooting (RequestStill, RequestStillHinted,
        // RequestItemStill; CancelStill ends it): its request and client, 0 when none.
        std::uint32_t g_cutoutRequest = 0;
        ClientId      g_cutoutClient  = 0;

        // A shot asked for while another runs, waiting its turn (oldest first). Game thread only (Post tasks).
        struct QueuedShot
        {
            std::uint32_t        id     = 0;
            ClientId             client = 0;
            bool                 item   = false;
            std::uint32_t        formId = 0;
            std::uint32_t        width = 0, height = 0;
            Framing              framing = Framing::Cutout;
            StillShotCore::Hints hints;
            std::function<void(bool, MannequinRenderer::PortraitShot, std::string)> answer;
        };
        std::deque<QueuedShot> g_shotQueue;
        // More waiting than this answers "busy".
        constexpr std::size_t kMaxQueuedShots = 32;
        // The subject of the last shot started and how many of its shots ran in a row (StillShotCore::NextShotIndex).
        StillShotCore::ShotKey g_lastShotKey;
        int                    g_lastShotRun = 0;

        StillShotCore::ShotKey ShotKeyOf(const QueuedShot& q)
        {
            StillShotCore::ShotKey k;
            k.item   = q.item;
            k.formId = q.formId;
            if (q.item) k.worn = q.hints.worn;
            else k.weapons = q.hints.hasWeapons && q.hints.weapons;
            return k;
        }

        // The renderer's GPU refusal window as Host sees it (ViewRegistryCore::GpuHold): no start inside it.
        VRC::GpuHold g_gpuHold;

        std::atomic<bool> g_reconcilePosted{ false };
        // One timer thread at a time waits out the GPU hold and then posts one Reconcile (ArmGpuRetry).
        std::atomic<bool> g_gpuRetryArmed{ false };
        std::uint32_t     g_mainThread = 0;
        // Set while a client's handler runs: the handler runs inside SKSE's AddTask drain (see RebuildActor3D).
        thread_local bool t_delivering = false;
        // RebuildActor3D logs the thread of its first call on the main thread and of its first call off it.
        std::atomic<bool> g_rebuildSeenMain{ false };
        std::atomic<bool> g_rebuildSeenOther{ false };

        // Every renderer call happens in these tasks. The pump hands them to SKSE from its own thread, so a caller
        // inside Magelight's Present never posts to SKSE itself; they run in the order they were posted.
        void Post(std::function<void()> fn) { PresentHandoff::PumpTask(std::move(fn)); }


        // An item view's `worn` hint: an armor drawn as worn by a man or a woman (StillShotCore::ParseWorn), else its
        // ground model.
        StillShotCore::Worn WornOf(const VRC::View& v)
        {
            StillShotCore::Worn w = StillShotCore::Worn::kGround;
            StillShotCore::ParseWorn(VRC::Hint(v, "worn"), w);
            return w;
        }

        // Ends a drawing view in the renderer that draws it (an actor or an item view) and forgets it.
        void EndDrawing(ViewId id)
        {
            const auto it = g_drawing.find(id);
            if (it == g_drawing.end()) return;
            if (it->second.view.item) ItemRenderer::GetSingleton()->LiveEnd(id);
            else MannequinRenderer::GetSingleton()->LiveEnd(id);
            g_drawing.erase(it);
        }

        bool IsOpen(ViewId view, std::uint64_t seq = 0)
        {
            std::lock_guard lock(g_mx);
            const auto*     v = g_reg.Find(view);
            return v && (seq == 0 || v->openedSeq == seq);
        }

        struct EventData
        {
            EventKind                                        kind = EventKind::Ended;
            ViewId                                           view = 0;
            std::uint32_t                                    actor = 0;
            std::string                                      reason, image, url, key, json;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;   // a ref held for the call
            std::uint32_t                                    width = 0, height = 0;
            bool                                             lastSeen = false;
        };

        // Calls the client's callback.
        void Deliver(ClientId client, const EventData& d)
        {
            Callback cb;
            {
                std::lock_guard lock(g_mx);
                const auto it = g_callbacks.find(client);
                if (it == g_callbacks.end()) return;   // released
                cb = it->second;
            }
            if (!cb.fn) return;
            Event e{};
            e.size        = sizeof(Event);
            e.kind        = d.kind;
            e.view        = d.view;
            e.actorFormId = d.actor;
            e.reason      = d.reason.c_str();
            e.imageName   = d.image.c_str();
            e.imageUrl    = d.url.c_str();
            e.srv         = d.srv.Get();
            e.width       = d.width;
            e.height      = d.height;
            e.lastSeen    = d.lastSeen;
            e.key         = d.key.c_str();
            e.json        = d.json.c_str();
            const bool outer = t_delivering;
            t_delivering     = true;
            try {
                cb.fn(&e, cb.user);
            } catch (...) {
                SKSE::log::error("Phantasm: client {}'s event handler threw (event {} for view {})", client,
                                 static_cast<std::uint32_t>(d.kind), d.view);
            }
            t_delivering = outer;
        }

        // A scene and its props are the view's own, and go with it; a file's clip is the view's too (docs/DESIGN.md),
        // but kept per actor: once no open view shows v's actor, the file v set goes (unless another choice has
        // replaced it since, such as a scene casting them). A named idle stays for the session.
        void ReleaseViewChoices(const VRC::View& v)
        {
            if (!v.actor || v.item) return;
            auto* r = MannequinRenderer::GetSingleton();
            if (!VRC::Hint(v, "scene").empty()) r->EndSceneOf(v.id, v.actor);
            if (!VRC::Hint(v, "props").empty()) r->EndPropsOf(v.id, v.actor);
            {
                std::lock_guard lock(g_mx);
                for (const auto& o : g_reg.Views()) {
                    if (!o.item && o.actor == v.actor) return;
                }
            }
            const auto idle = VRC::Hint(v, "idle");
            if (!MannequinClipCore::IdleFilePath(idle).empty()) r->EndMirrorIdleIf(v.actor, VRC::IdleHintValue(idle));
        }

        // The view is out of the registry; this tells its client and drops what Host kept for it.
        void EndView(const VRC::View& v, const char* reason)
        {
            std::erase_if(g_queries, [&](const PendingQuery& q) { return q.view == v.id; });
            g_toldWaiting.erase(v.id);
            ReleaseViewChoices(v);
            EventData d;
            d.kind   = EventKind::Ended;
            d.view   = v.id;
            d.actor  = v.actor;
            d.reason = reason;
            SKSE::log::info("Phantasm: view {} ({}, actor {:08X}) ended ({})", v.id, v.image, v.actor, reason);
            Deliver(v.client, d);
        }

        // The API reason for a shot the renderer gave up on (FinishPortrait's or FinishItemShot's text).
        std::string CutoutReason(const std::string& why)
        {
            if (why == "cancelled" || why == "gpu-unavailable" || why == "faulted" || why == "no-model" ||
                why == "read-failed" || why == "timed-out") {
                return why;
            }
            if (why == "not loaded") return "not-loaded";
            if (why == "the read-back failed") return "read-failed";
            return "not-drawn";   // its textures never finished loading, or the figure never drew
        }

        // An on/off hint, any case: on, true, yes or 1 is on; anything else set is off.
        bool IsOn(std::string_view value, bool fallback)
        {
            if (value.empty()) return fallback;
            const std::string v = VRC::LowerKey(value);
            return v == "on" || v == "true" || v == "1" || v == "yes";
        }

        // The view's `hostless` hint: its client draws Shown's SRV itself, so with no Magelight the view draws with no
        // image instead of ending "no-image". Read when the view starts.
        bool HostlessOf(const VRC::View& v) { return IsOn(VRC::Hint(v, "hostless"), false); }

        // Pushes the starting view's hints into the renderer: every display hint of the view (its default where the
        // view sets none), and the per-actor ones the view sets or has just removed (removed = the default again).
        void ApplyHints(const VRC::View& v, const VRC::View& before, bool restart)
        {
            auto*      r       = MannequinRenderer::GetSingleton();
            const auto changed = [&](const char* key) {
                if (restart) return true;
                return VRC::Hint(before, key) != VRC::Hint(v, key) || before.hints.contains(key) != v.hints.contains(key);
            };
            if (changed("pose")) r->SetCapturedPose(v.id, VRC::LowerKey(VRC::Hint(v, "pose")) == "captured");
            if (changed("motion")) r->SetMotion(v.id, IsOn(VRC::Hint(v, "motion"), false));
            if (changed("weapons")) r->SetWeapons(v.id, IsOn(VRC::Hint(v, "weapons"), true));
            if (changed("floor")) r->SetFloor(v.id, IsOn(VRC::Hint(v, "floor"), true));
            if (changed("lighting")) r->SetLighting(v.id, VRC::Hint(v, "lighting"));
            if (changed("aim")) r->SetAim(v.id, VRC::Hint(v, "aim"));
            if (changed("scale")) r->SetFigureScale(v.id, VRC::FigureScaleHint(VRC::Hint(v, "scale")));
            if (changed("freeze")) r->SetFreeze(v.id, IsOn(VRC::Hint(v, "freeze"), false));
            // Per actor, kept by the renderer for the session: a start applies only what the view sets (another
            // client's choice for the same actor stands otherwise); a change while drawing applies the new value, and
            // a removed hint the default.
            const auto perActor = [&](const char* key) { return restart ? v.hints.contains(key) : changed(key); };
            if (perActor("idle")) r->SetMirrorIdle(v.actor, VRC::IdleHintValue(VRC::Hint(v, "idle")));
            if (perActor("expression")) r->SetMirrorExpression(v.actor, VRC::Hint(v, "expression"));
            // The scene is the drawing view's own: a start sets it (no other actors when the view names none).
            if (restart || changed("scene")) {
                r->SetSceneCast(v.id, v.actor, VRC::SceneCastHint(VRC::Hint(v, "scene"), v.actor));
            }
            if (restart || changed("props")) r->SetSceneProps(v.id, v.actor, VRC::ScenePropsHint(VRC::Hint(v, "props")));
        }

        // The renderer's try-on marks follow the open views: an actor is marked while any open view of them has
        // keepLook off, whoever's view draws.
        void SyncTransientActors()
        {
            std::set<std::uint32_t> want;
            {
                std::lock_guard lock(g_mx);
                for (const auto& v : g_reg.Views()) {
                    if (!v.item && VRC::Hint(v, "keeplook").size() && !IsOn(VRC::Hint(v, "keeplook"), true)) {
                        want.insert(v.actor);
                    }
                }
            }
            auto* r = MannequinRenderer::GetSingleton();
            for (const auto a : g_transientActors) {
                if (!want.contains(a)) r->SetLookTransient(a, false);
            }
            for (const auto a : want) {
                if (!g_transientActors.contains(a)) r->SetLookTransient(a, true);
            }
            g_transientActors = std::move(want);
        }

        void PostReconcile();

        // Posts one Reconcile once `wait` has passed, from a thread of its own: the hold is waited out by a sleeping
        // thread, never by a task that posts itself again. If the hold was extended meanwhile, that Reconcile arms
        // the next wait.
        void ArmGpuRetry(std::chrono::milliseconds wait)
        {
            if (g_gpuRetryArmed.exchange(true)) return;
            try {
                std::thread([wait]() {
                    std::this_thread::sleep_for(wait);
                    g_gpuRetryArmed.store(false);
                    PostReconcile();
                }).detach();
            } catch (const std::exception& e) {
                g_gpuRetryArmed.store(false);
                SKSE::log::error("Phantasm: no thread to retry the waiting views after the GPU refusal ({}); they draw "
                                 "at the next OpenView, CloseView, ResizeView or SetHint", e.what());
            }
        }

        // Makes the renderer draw one of the registry's drawers: starts it, re-targets it, or passes it what changed.
        void DrawView(const VRC::View& d)
        {
            g_toldWaiting.erase(d.id);
            auto* r = MannequinRenderer::GetSingleton();
            // The registry keeps the size as asked; the renderer gets at least the smallest side it draws.
            const float w   = static_cast<float>(VRC::DrawSide(d.width));
            const float h   = static_cast<float>(VRC::DrawSide(d.height));
            auto        cur = g_drawing.find(d.id);
            if (cur == g_drawing.end() || cur->second.view.openedSeq != d.openedSeq) {
                if (cur != g_drawing.end()) {
                    const VRC::View was = cur->second.view;
                    if (was.actor != d.actor || was.item != d.item) ReleaseViewChoices(was);   // re-targeted
                    if (was.item != d.item) EndDrawing(d.id);   // the other renderer drew it
                }
                SKSE::log::info("Phantasm: view {} ({}) draws {} {:08X} at {}x{}", d.id, d.image, d.item ? "item" : "actor",
                                d.actor, d.width, d.height);
                if (d.item) {
                    g_drawing[d.id] = { d, false };
                    ItemRenderer::GetSingleton()->LiveStart(d.actor, w, h, d.image, d.id, d.openedSeq, WornOf(d),
                                                            HostlessOf(d));
                    return;
                }
                ApplyHints(d, {}, true);
                g_drawing[d.id] = { d, false };
                r->LiveStart(d.actor, w, h, d.image, d.id, d.openedSeq, HostlessOf(d));
                return;
            }
            Drawing& drawing = cur->second;
            if (d.item && WornOf(d) != WornOf(drawing.view)) {
                // Another model of the item: it loads again (a worn armor's or its ground model).
                drawing = { d, false };
                ItemRenderer::GetSingleton()->LiveStart(d.actor, w, h, d.image, d.id, d.openedSeq, WornOf(d),
                                                        HostlessOf(d));
                return;
            }
            if (d.width != drawing.view.width || d.height != drawing.view.height) {
                if (d.item) ItemRenderer::GetSingleton()->LiveSetRect(d.id, w, h);
                else r->LiveSetRect(d.id, w, h);
            }
            if (!d.item && d.hints != drawing.view.hints) ApplyHints(d, drawing.view, false);
            drawing.view = d;
        }

        // Makes the renderer draw the registry's drawers (the newest open views, up to the cap) and tells every other
        // open view it waits; a view that no longer draws (closed, or pushed past the cap) ends in the renderer.
        // Coalesced (one pending at a time). Inside the GPU hold it starts nothing: a view that would start waits
        // with the others, and the views already drawing go on.
        void Reconcile()
        {
            g_reconcilePosted.store(false);
            SyncTransientActors();
            const std::size_t      cap = REL::Module::IsVR() ? VRC::kMaxDrawingVR : VRC::kMaxDrawing;
            std::vector<VRC::View> drawers;
            std::vector<VRC::View> others;
            {
                std::lock_guard lock(g_mx);
                const auto      ids = g_reg.Drawers(cap);
                for (const ViewId id : ids) drawers.push_back(*g_reg.Find(id));
                for (const auto& v : g_reg.Views()) {
                    if (std::find(ids.begin(), ids.end(), v.id) == ids.end()) others.push_back(v);
                }
            }
            std::vector<ViewId> gone;
            for (const auto& [id, drawing] : g_drawing) {
                if (std::none_of(drawers.begin(), drawers.end(), [&](const VRC::View& d) { return d.id == id; })) {
                    gone.push_back(id);
                }
            }
            for (const ViewId id : gone) EndDrawing(id);
            if (const auto left = g_gpuHold.Left(std::chrono::steady_clock::now()); left.count() > 0) {
                std::vector<VRC::View> held;
                std::erase_if(drawers, [&](const VRC::View& d) {
                    const auto it = g_drawing.find(d.id);
                    if (it != g_drawing.end() && it->second.view.openedSeq == d.openedSeq) return false;
                    held.push_back(d);
                    return true;
                });
                for (const auto& d : held) {
                    EndDrawing(d.id);
                    others.push_back(d);
                }
                if (!held.empty()) ArmGpuRetry(left);
            }
            for (const auto& o : others) {
                const auto told = g_toldWaiting.find(o.id);
                if (told != g_toldWaiting.end() && told->second == o.openedSeq) continue;
                g_toldWaiting[o.id] = o.openedSeq;
                EventData w;
                w.kind   = EventKind::Waiting;
                w.view   = o.id;
                w.actor  = o.actor;
                w.reason = "busy";
                Deliver(o.client, w);
            }
            // Oldest first, so when two views of one actor start together the newest one's per-actor hints (idle,
            // expression) are applied last and stand.
            for (auto it = drawers.rbegin(); it != drawers.rend(); ++it) DrawView(*it);
        }

        void PostReconcile()
        {
            if (g_reconcilePosted.exchange(true)) return;
            Post(Reconcile);
        }

        // Posts fn for a drawing view only, told whether it is an item view: a camera move or refresh of a view that
        // is not drawing is dropped (its camera starts fresh when it draws again).
        Result ForDrawing(ViewId view, std::function<void(bool item)> fn)
        {
            if (!IsOpen(view)) return Result::UnknownView;
            Post([view, fn = std::move(fn)]() {
                if (const auto it = g_drawing.find(view); it != g_drawing.end()) fn(it->second.view.item);
            });
            return Result::Ok;
        }

        // ── Renderer events ─────────────────────────────────────────────
        // Raised under the renderer's LiveLock, by a Host task or inside the carrier's render phase on the main thread,
        // so it reads nothing of Host's: an event belongs to the start the renderer stamped on it (view and openedSeq,
        // given to LiveStart: OpenView re-targets a view without changing its id); everything else happens in a posted
        // task (PresentHandoff: pumped under the lock), so a client never runs inside the renderer and the renderer
        // finishes its own end first. A task whose start is no longer current drops the event: the Reconcile behind
        // it has restarted the view.
        void OnRendererEvent(const char* channel, const std::string& payload)
        {
            const std::string ch = channel ? channel : "";
            const nlohmann::json j = nlohmann::json::parse(payload, nullptr, false);
            if (j.is_discarded()) return;
            const std::uint32_t actor = j.value("formId", 0u);

            if (ch == "mannequinLiveStatus" || ch == "actorImageChanged" || ch == "itemLiveStatus" ||
                ch == "itemImageChanged") {
                // The start the renderer was drawing when it raised the event, stamped by the renderer itself.
                const ViewId        view = j.value("view", 0u);
                const std::uint64_t seq  = j.value("seq", std::uint64_t{ 0 });
                if (!view) return;
                const bool        rebuilt  = ch == "actorImageChanged" || ch == "itemImageChanged";
                const bool        item     = ch == "itemLiveStatus" || ch == "itemImageChanged";
                const bool        ok       = rebuilt || j.value("ok", false);
                const bool        lastSeen = j.value("lastSeen", false);
                const std::string reason   = j.value("reason", std::string("unavailable"));
                PresentHandoff::AddTask([view, seq, actor, ok, rebuilt, item, lastSeen, reason]() {
                    // Noted even for a start that is no longer current: the window is the device's, not the view's.
                    // Never for shaders that failed to compile: that is final, so holding the waiting views would
                    // only end them one per window instead of at once.
                    if (!ok && reason == "gpu-unavailable" &&
                        !MannequinRenderer::GetSingleton()->IsLiveGpuFailedForGood()) {
                        g_gpuHold.Note(std::chrono::steady_clock::now(), LiveViewLifecycleCore::kGpuRetryDelay);
                    }
                    if (!IsOpen(view, seq)) return;
                    if (ok) {
                        const auto it = g_drawing.find(view);
                        if (it == g_drawing.end()) return;
                        Drawing& drawing = it->second;
                        if (drawing.view.openedSeq != seq || drawing.view.item != item) return;
                        if (rebuilt && !drawing.shown) return;   // the first Shown is still to come
                        auto*      r    = MannequinRenderer::GetSingleton();
                        const auto info = item ? r->ItemImage(view) : r->ActorImage(view);
                        if (!info.srv) return;
                        EventData d;
                        d.kind     = EventKind::Shown;
                        d.view     = view;
                        d.actor    = actor;
                        // A hostless view has no image: its Shown carries the SRV alone, imageName empty as imageUrl.
                        d.image    = info.url.empty() ? std::string{} : drawing.view.image;
                        d.srv      = info.srv;
                        d.width    = info.width;
                        d.height   = info.height;
                        d.url      = info.url;
                        d.lastSeen = lastSeen;
                        drawing.shown = true;
                        Deliver(drawing.view.client, d);
                        return;
                    }
                    // The renderer refused or ended this start: the view is over for the client. After a device
                    // refusal (not a shader failure) the Reconcile behind it starts nothing until the hold has passed.
                    VRC::View closed;
                    {
                        std::lock_guard lock(g_mx);
                        if (g_reg.Close(view, closed) != Result::Ok) return;
                    }
                    EndDrawing(view);   // the renderer has ended it: this forgets its state there
                    EndView(closed, reason.c_str());
                    PostReconcile();
                });
                return;
            }

            if (ch == "mirrorIdles" || ch == "mirrorExpression") {
                const std::string key = ch == "mirrorIdles" ? "idles" : "expression";
                PresentHandoff::AddTask([key, actor, payload]() {
                    std::vector<PendingQuery> answered;
                    std::erase_if(g_queries, [&](const PendingQuery& q) {
                        if (q.key != key || q.actor != actor) return false;
                        answered.push_back(q);
                        return true;
                    });
                    for (const auto& q : answered) {
                        if (!IsOpen(q.view)) continue;
                        EventData d;
                        d.kind  = EventKind::Reply;
                        d.view  = q.view;
                        d.actor = q.actor;
                        d.key   = q.key;
                        d.json  = payload;
                        Deliver(q.client, d);
                    }
                });
            }
        }
    }

    Result RegisterClient(const char* name, EventFn onEvent, void* user, ClientId* out)
    {
        if (!out) return Result::BadArgument;
        *out = 0;
        if (!name || !onEvent) return Result::BadArgument;
        std::lock_guard lock(g_mx);
        const Result r = g_reg.RegisterClient(name, *out);
        if (r == Result::Ok) {
            g_callbacks[*out] = { onEvent, user };
            SKSE::log::info("Phantasm: client '{}' registered as {}", name, *out);
        }
        return r;
    }

    Result ReleaseClient(ClientId client)
    {
        std::vector<VRC::View> ended;
        {
            std::lock_guard lock(g_mx);
            const Result r = g_reg.ReleaseClient(client, ended);
            if (r != Result::Ok) return r;
            g_callbacks.erase(client);
        }
        SKSE::log::info("Phantasm: client {} released ({} views)", client, ended.size());
        Post([ended = std::move(ended), client]() {
            // Its waiting shots go too, unanswered (it hears nothing more), so they hold no place in the queue.
            std::erase_if(g_shotQueue, [client](const QueuedShot& q) { return q.client == client; });
            for (const auto& v : ended) {
                std::erase_if(g_queries, [&](const PendingQuery& q) { return q.view == v.id; });
                g_toldWaiting.erase(v.id);
            }
        });
        PostReconcile();
        return Result::Ok;
    }

    Result OpenView(ClientId client, const char* slot, std::uint32_t actorFormId, std::uint32_t width,
                    std::uint32_t height, Framing framing, ViewId* out)
    {
        if (!out) return Result::BadArgument;
        *out = 0;
        if (!slot) return Result::BadArgument;
        bool   created = false;
        Result r;
        {
            std::lock_guard lock(g_mx);
            r = g_reg.Open(client, slot, actorFormId, width, height, framing, *out, created);
        }
        if (r != Result::Ok) return r;
        PostReconcile();
        return Result::Ok;
    }

    Result OpenItemView(ClientId client, const char* slot, std::uint32_t itemFormId, std::uint32_t width,
                        std::uint32_t height, ViewId* out)
    {
        if (!out) return Result::BadArgument;
        *out = 0;
        if (!slot) return Result::BadArgument;
        bool   created = false;
        Result r;
        {
            std::lock_guard lock(g_mx);
            r = g_reg.Open(client, slot, itemFormId, width, height, Framing::Figure, *out, created, true);
        }
        if (r != Result::Ok) return r;
        PostReconcile();
        return Result::Ok;
    }

    Result CloseView(ViewId view)
    {
        VRC::View closed;
        {
            std::lock_guard lock(g_mx);
            const Result r = g_reg.Close(view, closed);
            if (r != Result::Ok) return r;
        }
        Post([closed = std::move(closed)]() { EndView(closed, "closed"); });
        PostReconcile();
        return Result::Ok;
    }

    Result ResizeView(ViewId view, std::uint32_t width, std::uint32_t height)
    {
        if (width == 0 || height == 0) return Result::BadArgument;
        {
            std::lock_guard lock(g_mx);
            auto*           v = g_reg.Find(view);
            if (!v) return Result::UnknownView;
            v->width  = width;
            v->height = height;
        }
        PostReconcile();
        return Result::Ok;
    }

    Result Orbit(ViewId view, float dx, float dy)
    {
        if (!VRC::CameraInputOk({ dx, dy })) return Result::BadArgument;
        return ForDrawing(view, [view, dx, dy](bool item) {
            if (item) ItemRenderer::GetSingleton()->LiveDrag(view, dx, dy);
            else MannequinRenderer::GetSingleton()->LiveDrag(view, dx, dy);
        });
    }

    Result Zoom(ViewId view, float dz, float u, float v)
    {
        if (!VRC::CameraInputOk({ dz, u, v })) return Result::BadArgument;
        return ForDrawing(view, [view, dz, u, v](bool item) {
            if (item) ItemRenderer::GetSingleton()->LiveZoom(view, dz, u, v);
            else MannequinRenderer::GetSingleton()->LiveZoom(view, dz, u, v);
        });
    }

    namespace
    {
        // 0.3.0's Still ended at standingPx (sizeof 48 on x64): a field added since must lie past it, or a client's
        // size test reads an older build's padding as the field.
        static_assert(offsetof(PHANTASM_API::Still, classicPose) >= 48, "Still::classicPose inside 0.3.0's struct");

        // The answer to a still request, handed to done once on the game thread, never after the client is released.
        std::function<void(bool, MannequinRenderer::PortraitShot, std::string)> StillAnswer(ClientId client, StillFn done,
                                                                                           void* user, std::uint32_t id)
        {
            return [client, done, user, id](bool ok, MannequinRenderer::PortraitShot shot, std::string why) {
                {
                    std::lock_guard lock(g_mx);
                    if (!g_callbacks.contains(client)) return;
                }
                Still s{};
                s.size      = sizeof(Still);
                s.requestId = id;
                s.ok        = ok && !shot.rgba.empty();
                s.reason    = s.ok ? "" : (why.empty() ? "read-failed" : why.c_str());
                s.width     = s.ok ? shot.width : 0;
                s.height    = s.ok ? shot.height : 0;
                s.rgba      = s.ok ? shot.rgba.data() : nullptr;
                s.standingPx = s.ok ? shot.standingPx : 0.0f;
                s.reserved0   = 0;
                s.classicPose = s.ok && shot.classicPose;
                const bool outer = t_delivering;
                t_delivering     = true;
                try {
                    done(&s, user);
                } catch (...) {
                    SKSE::log::error("Phantasm: client {}'s still handler threw (request {})", client, id);
                }
                t_delivering = outer;
            };
        }

        void StartNextShot();

        // The shot's outcome wrapper: frees the shot slot, notes a GPU refusal, fits the picture, answers, then starts
        // the next queued shot (a following shot of the same actor reuses the view the renderer kept up). An actor's
        // shot (`actor`) goes through NeuralStills first when it is on, at the size it was drawn, and is answered
        // from a later task; the next shot does not wait for it.
        std::function<void(bool, MannequinRenderer::PortraitShot, std::string)> ShotAnswer(
            std::function<void(bool, MannequinRenderer::PortraitShot, std::string)> answer, std::uint32_t width,
            std::uint32_t height, std::uint32_t id, bool actor)
        {
            return [answer = std::move(answer), width, height, id, actor](bool ok, MannequinRenderer::PortraitShot s, std::string w) {
                if (g_cutoutRequest == id) g_cutoutRequest = 0, g_cutoutClient = 0;
                if (!ok && w == "gpu-unavailable" && !MannequinRenderer::GetSingleton()->IsLiveGpuFailedForGood()) {
                    g_gpuHold.Note(std::chrono::steady_clock::now(), LiveViewLifecycleCore::kGpuRetryDelay);
                }
                if (ok && actor && NeuralStills::Enabled()) {
                    NeuralStills::Submit(std::move(s), [answer, width, height](MannequinRenderer::PortraitShot done) {
                        MannequinRenderer::FitShot(done, width, height);   // CPU only: safe on the NR worker
                        Post([answer, done = std::move(done)]() mutable { answer(true, std::move(done), {}); });
                    });
                    StartNextShot();
                    return;
                }
                if (ok) MannequinRenderer::FitShot(s, width, height);
                answer(ok, std::move(s), ok ? std::string{} : CutoutReason(w));
                StartNextShot();
            };
        }

        bool StillSizeOk(std::uint32_t width, std::uint32_t height)
        {
            return width > 0 && height > 0 && width <= 4096 && height <= 4096;
        }

        std::uint32_t NextStillId()
        {
            static std::atomic<std::uint32_t> s_seq{ 0 };
            return ++s_seq;
        }

        bool KnownClient(ClientId client)
        {
            std::lock_guard lock(g_mx);
            return g_callbacks.contains(client);
        }

        // A shot ends inside its own frames (taken, or given up after its timeout), so one whose frames stop coming
        // would hold the shot slot for good: this posts its end as timed out well after either job's own timeout
        // (kPortraitTimeout, StillShotCore::kItemTimeoutMs), if it is still the one running. Game thread.
        void ArmShotBackstop(std::uint32_t id)
        {
            g_cutoutRequest = id;
            std::thread([id]() {
                std::this_thread::sleep_for(std::chrono::seconds(10));
                Post([id]() {
                    if (g_cutoutRequest == id) MannequinRenderer::GetSingleton()->PortraitExpire();
                });
            }).detach();
        }

        // Starts a shot, or answers why it cannot; true when it started. A shot draws beside the views. Game thread.
        bool StartShot(QueuedShot& q)
        {
            if (!KnownClient(q.client)) return false;   // released since: nobody hears the answer
            auto*       r       = MannequinRenderer::GetSingleton();
            const auto  answer  = ShotAnswer(q.answer, q.width, q.height, q.id, !q.item);
            const char* why     = nullptr;
            bool        started = false;
            if (q.item) {
                why     = r->ItemShotRefusal(q.formId);
                started = !why && r->ItemShotStart(q.formId, q.hints.hasTurn ? q.hints.turnDeg : 0.0f,
                                                   q.hints.hasPitch ? q.hints.pitchDeg : 15.0f, q.hints.worn, answer);
                if (!started && !why) why = "missing";
            } else {
                MannequinRenderer::ShotOptions options;
                options.head = q.framing == Framing::Portrait;
                if (q.hints.hasTurn) options.turnDeg = q.hints.turnDeg;
                if (q.hints.hasPitch) options.pitchDeg = q.hints.pitchDeg;
                if (q.hints.hasLighting) options.lighting = q.hints.lighting;
                if (q.hints.hasWeapons) options.weapons = q.hints.weapons;
                options.aim = q.hints.aim;
                if (q.hints.hasZoom) options.zoom = q.hints.zoom;
                why     = r->PortraitRefusal(q.formId);
                started = !why && r->PortraitStart(q.formId, options, answer);
                if (!started && !why) why = "not-loaded";
            }
            if (!started) {
                q.answer(false, {}, why);
                return false;
            }
            g_cutoutClient = q.client;
            const auto key = ShotKeyOf(q);
            g_lastShotRun  = key == g_lastShotKey ? g_lastShotRun + 1 : 1;
            g_lastShotKey  = key;
            ArmShotBackstop(q.id);
            return true;
        }

        // The next waiting shot of the subject just shot goes first, so it reuses the view that shot left up.
        void StartNextShot()
        {
            while (!g_cutoutRequest && !g_shotQueue.empty()) {
                std::vector<StillShotCore::ShotKey> keys;
                keys.reserve(g_shotQueue.size());
                for (const auto& w : g_shotQueue) keys.push_back(ShotKeyOf(w));
                const auto at = static_cast<std::ptrdiff_t>(StillShotCore::NextShotIndex(keys, g_lastShotKey, g_lastShotRun));
                QueuedShot q  = std::move(g_shotQueue[static_cast<std::size_t>(at)]);
                g_shotQueue.erase(g_shotQueue.begin() + at);
                StartShot(q);
            }
        }

        // A new shot starts now, or waits behind the one running. Game thread.
        void QueueShot(QueuedShot q)
        {
            if (!g_cutoutRequest) {
                StartShot(q);
                return;
            }
            if (g_shotQueue.size() >= kMaxQueuedShots) {
                q.answer(false, {}, "busy");
                return;
            }
            g_shotQueue.push_back(std::move(q));
        }
    }

    Result RequestStill(ClientId client, std::uint32_t actorFormId, std::uint32_t width, std::uint32_t height,
                        Framing framing, StillFn done, void* user, std::uint32_t* requestId)
    {
        return RequestStillHinted(client, actorFormId, width, height, framing, nullptr, done, user, requestId);
    }

    Result RequestStillHinted(ClientId client, std::uint32_t actorFormId, std::uint32_t width, std::uint32_t height,
                              Framing framing, const char* hints, StillFn done, void* user, std::uint32_t* requestId)
    {
        if (requestId) *requestId = 0;
        if (!done || !actorFormId || !StillSizeOk(width, height)) return Result::BadArgument;
        if (framing != Framing::Figure && framing != Framing::Cutout && framing != Framing::Portrait) {
            return Result::BadArgument;
        }
        if (!KnownClient(client)) return Result::UnknownClient;
        const std::uint32_t id = NextStillId();
        if (requestId) *requestId = id;
        const StillShotCore::Hints h = StillShotCore::Parse(hints ? std::string_view(hints) : std::string_view{});
        Post([client, actorFormId, width, height, framing, h, done, user, id]() {
            const auto answer = StillAnswer(client, done, user, id);
            auto*      r      = MannequinRenderer::GetSingleton();
            if (framing == Framing::Cutout || framing == Framing::Portrait) {
                // The renderer answers through PresentHandoff, outside its render phase.
                QueueShot({ .id = id, .client = client, .item = false, .formId = actorFormId, .width = width,
                            .height = height, .framing = framing, .hints = h, .answer = answer });
                return;
            }
            if (!r->StillOfView(actorFormId, width, height, answer)) {
                answer(false, {}, "not-drawing");
                return;
            }
            // The backstop for a view that stops drawing frames before the still is due.
            std::thread([]() {
                std::this_thread::sleep_for(std::chrono::seconds(9));
                Post([]() { MannequinRenderer::GetSingleton()->ExpireStills(); });
            }).detach();
        });
        return Result::Ok;
    }

    Result RequestItemStill(ClientId client, std::uint32_t itemFormId, std::uint32_t width, std::uint32_t height,
                            const char* hints, StillFn done, void* user, std::uint32_t* requestId)
    {
        if (requestId) *requestId = 0;
        if (!done || !itemFormId || !StillSizeOk(width, height)) return Result::BadArgument;
        if (!KnownClient(client)) return Result::UnknownClient;
        const std::uint32_t id = NextStillId();
        if (requestId) *requestId = id;
        const StillShotCore::Hints h = StillShotCore::Parse(hints ? std::string_view(hints) : std::string_view{});
        Post([client, itemFormId, width, height, h, done, user, id]() {
            QueueShot({ .id = id, .client = client, .item = true, .formId = itemFormId, .width = width, .height = height,
                        .framing = Framing::Figure, .hints = h, .answer = StillAnswer(client, done, user, id) });
        });
        return Result::Ok;
    }

    Result CancelStill(ClientId client, std::uint32_t requestId)
    {
        {
            std::lock_guard lock(g_mx);
            if (!g_callbacks.contains(client)) return Result::UnknownClient;
        }
        Post([client, requestId]() {
            const auto queued = std::find_if(g_shotQueue.begin(), g_shotQueue.end(), [&](const QueuedShot& q) {
                return q.id == requestId && q.client == client;
            });
            if (queued != g_shotQueue.end()) {
                auto answer = std::move(queued->answer);
                g_shotQueue.erase(queued);
                answer(false, {}, "cancelled");
                return;
            }
            if (g_cutoutRequest != requestId || g_cutoutClient != client) return;   // answered already, or not theirs
            MannequinRenderer::GetSingleton()->PortraitCancel();
        });
        return Result::Ok;
    }

    Result Refresh(ViewId view)
    {
        return ForDrawing(view, [view](bool item) {
            if (!item) MannequinRenderer::GetSingleton()->LiveRefresh(view);
        });
    }

    Result SetHint(ViewId view, const char* key, const char* value)
    {
        if (!key) return Result::BadArgument;
        {
            std::lock_guard lock(g_mx);
            auto*           v = g_reg.Find(view);
            if (!v) return Result::UnknownView;
            VRC::SetHint(*v, key, value ? value : "");
        }
        PostReconcile();
        return Result::Ok;
    }

    Result QueryHint(ViewId view, const char* key)
    {
        if (!key) return Result::BadArgument;
        PendingQuery q;
        {
            std::lock_guard lock(g_mx);
            const auto*     v = g_reg.Find(view);
            if (!v) return Result::UnknownView;
            q.view   = view;
            q.client = v->client;
            q.actor  = v->actor;
        }
        q.key = VRC::LowerKey(key);
        Post([q = std::move(q)]() mutable {
            if (!IsOpen(q.view)) return;   // ended meanwhile: nothing follows an Ended
            auto* r = MannequinRenderer::GetSingleton();
            bool  item = false;
            {
                std::lock_guard lock(g_mx);
                const auto*     v = g_reg.Find(q.view);
                item              = v && v->item;
            }
            if (!item && q.key == "freeze") {
                // freeze: the hint as set; held: the view draws a frozen look now (it freezes at a draw after the
                // hint, once its look is complete, and only while it draws).
                bool want = false;
                {
                    std::lock_guard lock(g_mx);
                    if (const auto* v = g_reg.Find(q.view)) want = IsOn(VRC::Hint(*v, "freeze"), false);
                }
                EventData d;
                d.kind  = EventKind::Reply;
                d.view  = q.view;
                d.actor = q.actor;
                d.key   = q.key;
                d.json  = nlohmann::json{ { "known", true }, { "freeze", want }, { "held", r->IsFrozen(q.view) } }.dump();
                Deliver(q.client, d);
                return;
            }
            if (q.key == "stats") {
                // The view's frame rates over the last second (ViewStatsCore): fps = drawn + skipped-unchanged.
                const auto s = r->GetViewStats(q.view);
                EventData  d;
                d.kind  = EventKind::Reply;
                d.view  = q.view;
                d.actor = q.actor;
                d.key   = q.key;
                auto r1 = [](double x) { return std::round(x * 10.0) / 10.0; };
                d.json  = nlohmann::json{ { "known", true },        { "drawing", s.drawing },
                                         { "valid", s.valid },       { "fps", r1(s.fps) },
                                         { "drawnFps", r1(s.drawnFps) }, { "skippedFps", r1(s.skippedFps) },
                                         { "waitedFps", r1(s.waitedFps) }, { "stalledFps", r1(s.stalledFps) },
                                         { "cpuMs", std::round(s.cpuMs * 100.0) / 100.0 },
                                         { "budgetMode", s.budget }, { "budgetApplied", s.budgetApplied },
                                         { "budgetMs", r1(s.budgetMs) } }
                              .dump();
                Deliver(q.client, d);
                return;
            }
            if (!item && (q.key == "idles" || q.key == "expression")) {
                const auto actor = q.actor;
                const bool idles = q.key == "idles";
                g_queries.push_back(std::move(q));
                if (idles) r->RequestMirrorIdles(actor);
                else r->RequestMirrorExpression(actor);
                return;
            }
            EventData d;
            d.kind  = EventKind::Reply;
            d.view  = q.view;
            d.actor = q.actor;
            d.key   = q.key;
            d.json  = R"({"known":false})";
            Deliver(q.client, d);
        });
        return Result::Ok;
    }

    Result RebuildActor3D(std::uint32_t actorFormId, const char* who)
    {
        const auto thread = REX::W32::GetCurrentThreadId();
        const bool onMain = thread == g_mainThread;
        // Shows where clients' UI tasks really drain (the main thread during play, or another thread) without a
        // line per call.
        if (!(onMain ? g_rebuildSeenMain : g_rebuildSeenOther).exchange(true)) {
            SKSE::log::info("Phantasm: first RebuildActor3D {} the main thread (thread {}, main {}, {}{}{})",
                            onMain ? "on" : "off", thread, g_mainThread, REL::Module::IsVR() ? "VR, " : "",
                            PresentHandoff::Inside() ? "inside a render phase, " : "",
                            who && who[0] ? who : "a client");
        }
        // Inside a render phase the 3D would change under the frame being drawn. Off the main thread the renderer's
        // state would change under the carrier's draw; inside a handler the call is inside SKSE's AddTask drain,
        // where RaceMenu's armor hook deadlocks Update3DModel. On VR SKSE runs its UI-task queue on a job thread, so
        // the thread is not checked there; the draw skips its frames while the rebuild runs (LiveRebuildActor3D).
        if (PresentHandoff::Inside()) return Result::BadThread;
        if (!REL::Module::IsVR() && !onMain) return Result::BadThread;
        if (t_delivering) return Result::BadThread;
        auto* actor = actorFormId ? RE::TESForm::LookupByID<RE::Actor>(actorFormId) : nullptr;
        if (!actor) return Result::BadArgument;
        MannequinRenderer::GetSingleton()->LiveRebuildActor3D(actor, who && who[0] ? who : "a client");
        return Result::Ok;
    }

    void Initialize()
    {
        g_mainThread = REX::W32::GetCurrentThreadId();
        MannequinRenderer::GetSingleton()->SetSendEventCallback(OnRendererEvent);
    }

    void OnRevert()
    {
        std::vector<VRC::View> all;
        {
            std::lock_guard lock(g_mx);
            all = g_reg.TakeAll();
        }
        // Through the pump, behind the API work posted before the load. The views hold NiPointers and geometry that
        // must not cross saves; the renderer forgets this save's looks and try-on marks. A Reconcile posted before
        // the load can run ahead of this task and start a view a client opened since (its own Reconcile coalesced
        // into that one), which this task then ends in the renderer, so it reconciles once more at its end: the
        // views opened after the load start again with their hints and try-on marks.
        Post([all = std::move(all)]() {
            // Shots still waiting were asked of the game being left: answered, never started in the next one.
            auto waiting = std::move(g_shotQueue);
            g_shotQueue.clear();
            g_lastShotKey = {};
            g_lastShotRun = 0;
            for (auto& q : waiting) q.answer(false, {}, "cancelled");
            ItemRenderer::GetSingleton()->ResetTransientState();
            MannequinRenderer::GetSingleton()->LiveEnd();
            MannequinRenderer::GetSingleton()->ResetTransientState();
            g_drawing.clear();
            g_queries.clear();
            g_transientActors.clear();
            for (const auto& v : all) EndView(v, "game-load");
            Reconcile();
        });
    }
}
