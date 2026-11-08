// Phantasm Showcase - Views: see Views.h.

#include "PCH.h"

#include "Views.h"

#include "Bridge.h"
#include "ImGuiPanel.h"
#include "Ui.h"
#include "Underlay.h"
#include "Phantasm_API.h"
#include "ViewCore.h"

#include <cstddef>

namespace Showcase::Views
{
    namespace
    {
        constexpr const char* kClient = "showcase";

        const PHANTASM_API::Interface1* g_api = nullptr;
        PHANTASM_API::ClientId          g_client = 0;
        std::string_view                g_unavailable = "absent";

        struct Slot
        {
            PHANTASM_API::ViewId view = 0;
            bool                 item = false;
            std::uint32_t        form = 0;
            std::uint32_t        w = 0, h = 0;
            Hints                applied;   // what this view was last told
            std::optional<nlohmann::json> last;   // the last shown or waiting push, sent again to a page that asks again
        };

        // Under g_mutex. Phantasm sends no event from inside a call, so calling it under the lock cannot reach
        // OnEvent's lock on the same thread.
        std::mutex                           g_mutex;
        std::map<std::string, Slot, std::less<>> g_slots;

        const char* Str(const char* s) { return s ? s : ""; }

        std::string Hex(std::uint32_t id) { return std::format("{:08X}", id); }

        /// The slot holding view `id`, or end.
        auto FindView(PHANTASM_API::ViewId id)
        {
            for (auto it = g_slots.begin(); it != g_slots.end(); ++it) {
                if (it->second.view == id) return it;
            }
            return g_slots.end();
        }

        bool Has(std::size_t field) { return g_api && g_api->size >= field + sizeof(void*); }

        void OnEvent(const PHANTASM_API::Event* e, void*)
        {
            // Every field read below lies before `key`, read only for a Reply after the size check.
            if (!e || e->size < offsetof(PHANTASM_API::Event, key)) return;
            using PHANTASM_API::EventKind;
            nlohmann::json push;
            {
                std::lock_guard lock(g_mutex);
                const auto it = FindView(e->view);
                if (it == g_slots.end()) return;   // an earlier view's late event
                auto& s = it->second;
                // A re-target keeps the view id, so an event queued for the previous target still names it.
                if (e->kind != EventKind::Ended && e->actorFormId != s.form) return;
                if (it->first == ViewCore::kPanelSlot) {
                    if (e->kind == EventKind::Ended) g_slots.erase(it);
                    ImGuiPanel::OnViewEvent(e->kind, e->view, e->actorFormId,
                                            e->kind == EventKind::Shown ? e->srv : nullptr, Str(e->reason));
                    return;
                }
                const auto form = Hex(e->actorFormId);
                switch (e->kind) {
                case EventKind::Shown:
                    push = { { "event", "view" }, { "slot", it->first },
                             { "view", { { "state", "shown" }, { "formId", form }, { "imageUrl", Str(e->imageUrl) },
                                         { "lastSeen", e->lastSeen } } } };
                    s.last = push;
                    // On PrismaUI the picture is drawn under the page from the SRV (Underlay.h).
                    if (Ui::IsPrisma() && e->srv) Underlay::SetImage(it->first, e->srv);
                    break;
                case EventKind::Waiting:
                    push = { { "event", "view" }, { "slot", it->first },
                             { "view", { { "state", "waiting" }, { "formId", form }, { "reason", Str(e->reason) } } } };
                    s.last = push;
                    Underlay::ClearImage(it->first);   // the SRV lives until the view's next event only
                    break;
                case EventKind::Ended: {
                    const std::string reason = Str(e->reason);
                    SKSE::log::info("views: {} ({}) ended ({})", it->first, e->view, reason);
                    const std::string slot = it->first;
                    g_slots.erase(it);
                    Underlay::ClearImage(slot);
                    if (reason == "closed") return;   // our own close: the page asked for it
                    push = { { "event", "view" }, { "slot", slot },
                             { "view", { { "state", "ended" }, { "formId", form }, { "reason", reason } } } };
                    break;
                }
                case EventKind::Reply: {
                    if (e->size < offsetof(PHANTASM_API::Event, json) + sizeof(const char*)) return;
                    nlohmann::json answer = nlohmann::json::parse(Str(e->json), nullptr, false);
                    if (answer.is_discarded()) answer = nlohmann::json::object();
                    push = { { "event", "reply" }, { "slot", it->first }, { "key", Str(e->key) },
                             { "formId", form }, { "json", std::move(answer) } };
                    break;
                }
                default:
                    return;
                }
            }
            Bridge::Push(push);
        }

        /// Brings the view's hints to `want`. Under g_mutex.
        void ApplyHints(Slot& s, const Hints& want)
        {
            for (const auto& [key, value] : want) {
                const auto it = s.applied.find(key);
                if (it != s.applied.end() && it->second == value) continue;
                if (g_api->SetHint(s.view, key.c_str(), value.c_str()) == PHANTASM_API::Result::Ok) s.applied[key] = value;
            }
        }

        /// The page's hints plus, on PrismaUI and for the ImGui panel, `hostless=on`: Phantasm draws a view with no
        /// Magelight only for a client that says it draws the SRV itself (the underlay, the panel), and answers
        /// Ended("no-image") otherwise. Set in the same call as OpenView, before the view's first frame.
        Hints WithHost(std::string_view slot, const Hints& hints)
        {
            if (!Ui::IsPrisma() && slot != ViewCore::kPanelSlot) return hints;
            Hints out = hints;
            out["hostless"] = "on";
            return out;
        }

        struct StillBox
        {
            StillDone done;
        };

        void OnStill(const PHANTASM_API::Still* st, void* user)
        {
            std::unique_ptr<StillBox> box(static_cast<StillBox*>(user));
            if (!box || !box->done) return;
            if (!st) {
                box->done(false, "read-failed", {}, 0, 0);
                return;
            }
            if (!st->ok || !st->rgba || !st->width || !st->height) {
                box->done(false, st->ok ? "read-failed" : Str(st->reason), {}, 0, 0);
                return;
            }
            // Alive for the call only.
            std::vector<std::uint8_t> rgba(st->rgba, st->rgba + std::size_t(st->width) * st->height * 4);
            box->done(true, "", std::move(rgba), st->width, st->height);
        }
    }

    void Initialize()
    {
        const auto module = ::GetModuleHandleW(L"Phantasm.dll");
        if (!module) {
            SKSE::log::info("views: Phantasm is not installed - nothing can draw");
            return;
        }
        const auto get = reinterpret_cast<PHANTASM_API::RequestPluginAPIFn>(::GetProcAddress(module, "RequestPluginAPI"));
        const auto* api = get ? static_cast<const PHANTASM_API::Interface1*>(get(PHANTASM_API::kInterface1)) : nullptr;
        // Item views and hinted stills: Phantasm 0.2.0 and later.
        constexpr std::size_t kNeed = offsetof(PHANTASM_API::Interface1, RequestStillHinted) + sizeof(void*);
        if (!api || api->size < kNeed) {
            g_unavailable = "old";
            SKSE::log::warn("views: this Phantasm is too old (no item views or hinted stills)");
            return;
        }
        PHANTASM_API::ClientId client = 0;
        const auto r = api->RegisterClient(kClient, &OnEvent, nullptr, &client);
        if (r != PHANTASM_API::Result::Ok) {
            g_unavailable = "refused";
            SKSE::log::error("views: Phantasm refused the client '{}' ({})", kClient, static_cast<int>(r));
            return;
        }
        g_api = api;
        g_client = client;
        g_unavailable = "";
        SKSE::log::info("views: Phantasm {} ready, {} view(s) draw at once", api->pluginVersion,
                        ViewCore::MaxDrawing(api->pluginVersion, REL::Module::IsVR()));
    }

    std::string_view Unavailable() { return g_unavailable; }

    std::uint32_t PluginVersion() { return g_api ? g_api->pluginVersion : 0; }

    namespace
    {
        /// Open's and OpenPanel's work, in device pixels. `opened` gets the slot's view id.
        bool OpenSlot(std::string_view slot, bool item, std::uint32_t formId, std::uint32_t w, std::uint32_t h,
                      const Hints& hints, std::string& why, PHANTASM_API::ViewId& opened)
        {
            std::optional<nlohmann::json> resend;
            {
                std::lock_guard lock(g_mutex);
                auto it = g_slots.find(slot);
                if (it != g_slots.end() && it->second.view && it->second.item == item && it->second.form == formId) {
                    auto& s = it->second;
                    if ((w != s.w || h != s.h) && g_api->ResizeView(s.view, w, h) == PHANTASM_API::Result::Ok) s.w = w, s.h = h;
                    ApplyHints(s, WithHost(slot, hints));
                    // A page that asks again (remounted) gets the picture back; the panel's view is not the page's.
                    if (slot != ViewCore::kPanelSlot) resend = s.last;
                    opened = s.view;
                } else {
                    const std::string name(slot);
                    PHANTASM_API::ViewId view = 0;
                    const auto r = item ? g_api->OpenItemView(g_client, name.c_str(), formId, w, h, &view)
                                        : g_api->OpenView(g_client, name.c_str(), formId, w, h,
                                                          PHANTASM_API::Framing::Figure, &view);
                    if (r != PHANTASM_API::Result::Ok) {
                        SKSE::log::warn("views: Phantasm refused {} {:08X} in {} at {}x{} ({})", item ? "item" : "actor",
                                        formId, name, w, h, static_cast<int>(r));
                        if (it != g_slots.end()) {
                            if (it->second.view) g_api->CloseView(it->second.view);
                            g_slots.erase(it);
                        }
                        // Its Ended finds no slot, so the picture is let go here.
                        Underlay::ClearImage(name);
                        why = r == PHANTASM_API::Result::TooMany ? "tooMany" : "refused";
                        return false;
                    }
                    auto& s = g_slots[name];
                    // A held slot keeps its view id; a re-target draws a new subject, so every hint is told again.
                    s.view = view, s.item = item, s.form = formId, s.w = w, s.h = h;
                    s.applied.clear();
                    s.last.reset();
                    ApplyHints(s, WithHost(slot, hints));
                    opened = view;
                }
            }
            if (resend) Bridge::Push(*resend);
            return true;
        }
    }

    bool Open(std::string_view slot, bool item, std::uint32_t formId, double cssW, double cssH, double scale,
              const Hints& hints, std::string& why)
    {
        if (!g_api) {
            why = std::string(g_unavailable);
            return false;
        }
        if (!ViewCore::IsSlot(slot)) {
            why = "badSlot";
            return false;
        }
        const auto w = ViewCore::Pixels(cssW, scale), h = ViewCore::Pixels(cssH, scale);
        if (!w || !h || !formId) {
            why = "badSize";
            return false;
        }
        PHANTASM_API::ViewId opened = 0;
        return OpenSlot(slot, item, formId, w, h, hints, why, opened);
    }

    bool OpenPanel(std::uint32_t formId, std::uint32_t width, std::uint32_t height, const Hints& hints, std::string& why,
                   std::uint32_t& view)
    {
        if (!g_api) {
            why = std::string(g_unavailable);
            return false;
        }
        const auto w = ViewCore::Pixels(width, 1.0), h = ViewCore::Pixels(height, 1.0);
        if (!w || !h || !formId) {
            why = "badSize";
            return false;
        }
        return OpenSlot(ViewCore::kPanelSlot, false, formId, w, h, hints, why, view);
    }

    bool Hint(std::string_view slot, const std::string& key, const std::string& value)
    {
        std::lock_guard lock(g_mutex);
        const auto it = g_slots.find(slot);
        if (!g_api || it == g_slots.end() || !it->second.view) return false;
        ApplyHints(it->second, { { key, value } });
        return true;
    }

    bool Query(std::string_view slot, const std::string& key)
    {
        std::lock_guard lock(g_mutex);
        const auto it = g_slots.find(slot);
        return g_api && it != g_slots.end() && it->second.view &&
               g_api->QueryHint(it->second.view, key.c_str()) == PHANTASM_API::Result::Ok;
    }

    bool Orbit(std::string_view slot, float dx, float dy)
    {
        if (!std::isfinite(dx) || !std::isfinite(dy)) return false;
        std::lock_guard lock(g_mutex);
        const auto it = g_slots.find(slot);
        return g_api && it != g_slots.end() && it->second.view &&
               g_api->Orbit(it->second.view, dx, dy) == PHANTASM_API::Result::Ok;
    }

    bool Zoom(std::string_view slot, float dz, float u, float v)
    {
        if (!std::isfinite(dz) || !std::isfinite(u) || !std::isfinite(v)) return false;
        std::lock_guard lock(g_mutex);
        const auto it = g_slots.find(slot);
        return g_api && it != g_slots.end() && it->second.view &&
               g_api->Zoom(it->second.view, dz, std::clamp(u, 0.0f, 1.0f), std::clamp(v, 0.0f, 1.0f)) ==
                   PHANTASM_API::Result::Ok;
    }

    bool Refresh(std::string_view slot)
    {
        std::lock_guard lock(g_mutex);
        const auto it = g_slots.find(slot);
        return g_api && it != g_slots.end() && it->second.view && !it->second.item &&
               g_api->Refresh(it->second.view) == PHANTASM_API::Result::Ok;
    }

    void Close(std::string_view slot)
    {
        std::lock_guard lock(g_mutex);
        const auto it = g_slots.find(slot);
        if (!g_api || it == g_slots.end()) return;
        if (it->second.view) g_api->CloseView(it->second.view);
        // The view's Ended will find no slot: its picture is let go now, or the stage keeps showing it.
        Underlay::ClearImage(it->first);
        g_slots.erase(it);
    }

    void CloseAll()
    {
        std::lock_guard lock(g_mutex);
        if (!g_api) return;
        for (auto it = g_slots.begin(); it != g_slots.end();) {
            if (it->first == ViewCore::kPanelSlot) {
                ++it;
                continue;
            }
            if (it->second.view) g_api->CloseView(it->second.view);
            Underlay::ClearImage(it->first);
            it = g_slots.erase(it);
        }
    }

    bool Still(std::uint32_t actorId, std::uint32_t width, std::uint32_t height, bool cutout, const std::string& hints,
               StillDone done, std::uint32_t* requestId)
    {
        if (!g_api || !actorId || !width || !height || !done) return false;
        auto* box = new StillBox{ std::move(done) };
        std::uint32_t id = 0;
        const auto framing = cutout ? PHANTASM_API::Framing::Cutout : PHANTASM_API::Framing::Figure;
        const auto r = g_api->RequestStillHinted(g_client, actorId, width, height, framing,
                                                 hints.empty() ? nullptr : hints.c_str(), &OnStill, box, &id);
        if (r != PHANTASM_API::Result::Ok) {
            delete box;
            SKSE::log::warn("views: Phantasm refused a still of {:08X} ({})", actorId, static_cast<int>(r));
            return false;
        }
        if (requestId) *requestId = id;
        return true;
    }

    void CancelStill(std::uint32_t requestId)
    {
        if (Has(offsetof(PHANTASM_API::Interface1, CancelStill)) && requestId) g_api->CancelStill(g_client, requestId);
    }

    bool RebuildActor3D(std::uint32_t actorId)
    {
        if (!g_api) return false;
        const auto r = g_api->RebuildActor3D(actorId, "showcase");
        if (r != PHANTASM_API::Result::Ok) SKSE::log::warn("views: RebuildActor3D {:08X} refused ({})", actorId, static_cast<int>(r));
        return r == PHANTASM_API::Result::Ok;
    }
}
