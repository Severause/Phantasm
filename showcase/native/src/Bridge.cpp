// Phantasm Showcase - Bridge: see Bridge.h.

#include "PCH.h"

#include "Bridge.h"

#include "AnimPathCore.h"
#include "Anims.h"
#include "Items.h"
#include "People.h"
#include "Settings.h"
#include "Shots.h"
#include "TryOn.h"
#include "TurnCore.h"
#include "Ui.h"
#include "Underlay.h"
#include "ViewCore.h"
#include "Views.h"

namespace Showcase::Bridge
{
    using json = nlohmann::json;

    namespace
    {
        // Two channels: a listener on a name makes the host define window.<name> in the page, and InteropCall on that
        // name calls it, so a reply on the request channel would come back to this DLL instead of reaching the page.
        constexpr const char* kChannelIn = "showcase";        // page -> DLL
        constexpr const char* kChannelOut = "showcaseEvent";  // DLL -> page

        // Written once by Attach / AttachSender on the game thread; read by Send from any thread (the animation walk,
        // the PNG encoder), so they are atomic and the view is published after the API.
        std::atomic<const MAGELIGHT_API::MagelightApi4*> g_v4{ nullptr };
        std::atomic<MAGELIGHT_API::ViewId>               g_view{ 0 };
        std::atomic<SendFn>                              g_sender{ nullptr };   // another host's InteropCall (PrismaUI)
        // Set by hello, cleared by the menu's close: a viewOpen that lands after the close must open nothing, or the
        // view would draw during play with nobody to close it. Game thread.
        bool g_menuOpen = false;

        // The wardrobe's turntable: the session a frame belongs to and the stills still running. Any thread.
        std::mutex                 g_turnMutex;
        std::uint32_t              g_turnSession = 0;
        std::vector<std::uint32_t> g_turnRequests;

        void Send(const json& j)
        {
            const auto text = j.dump(-1, ' ', false, json::error_handler_t::replace);
            if (const auto send = g_sender.load(std::memory_order_acquire)) {
                send(text.c_str());
                return;
            }
            const auto view = g_view.load(std::memory_order_acquire);
            const auto* v4 = g_v4.load(std::memory_order_acquire);
            if (v4 && view) v4->InteropCall(view, kChannelOut, text.c_str());
        }

        struct Fail
        {
            const char* code;
            std::string detail;
        };

        std::uint32_t ParseHex(const json& j)
        {
            if (!j.is_string()) return 0;
            const auto s = j.get<std::string>();
            char*      end = nullptr;
            const auto v = std::strtoul(s.c_str(), &end, 16);
            return (end && *end == '\0' && !s.empty()) ? static_cast<std::uint32_t>(v) : 0;
        }

        double Num(const json& req, const char* key, double fallback = 0.0)
        {
            const auto it = req.find(key);
            return (it != req.end() && it->is_number()) ? it->get<double>() : fallback;
        }

        std::string Str(const json& req, const char* key)
        {
            const auto it = req.find(key);
            return (it != req.end() && it->is_string()) ? it->get<std::string>() : std::string();
        }

        std::uint32_t FormArg(const json& req, const char* key = "formId")
        {
            const auto id = ParseHex(req.value(key, json()));
            if (!id) throw Fail{ "badRequest", std::string(key) + " must be a FormID" };
            return id;
        }

        std::string SlotArg(const json& req)
        {
            auto slot = Str(req, "slot");
            if (!ViewCore::IsSlot(slot)) throw Fail{ "badRequest", "unknown slot" };
            return slot;
        }

        /// A hint value from the page: a few known words or numbers only, at most 1024 bytes.
        std::string HintValue(const json& v)
        {
            if (v.is_string()) {
                auto s = v.get<std::string>();
                if (s.size() > 1024) throw Fail{ "badRequest", "hint too long" };
                // The value reaches Phantasm as a C string: an embedded NUL would cut it short past every check.
                if (s.find('\0') != std::string::npos) throw Fail{ "badRequest", "hint holds a NUL" };
                return s;
            }
            if (v.is_boolean()) return v.get<bool>() ? "on" : "off";
            if (v.is_number()) return std::format("{}", v.get<double>());
            return {};
        }

        json Hello()
        {
            const bool vr = REL::Module::IsVR();
            json j;
            j["version"] = PLUGIN_VERSION;
            j["runtime"] = vr ? "VR" : (REL::Module::IsAE() ? "AE" : "SE");
            j["menuKey"] = Settings::KeyName(Settings::Get().menuKey);
            j["uiScale"] = Settings::Get().uiScale;
            j["showFps"] = Settings::Get().showFps;
            j["freezeWorld"] = Settings::Get().freezeWorld;
            j["freezeAvailable"] = Ui::FreezeWorldAvailable();
            j["renderer"] = std::string(Views::Unavailable());
            j["host"] = Ui::HostName();
            j["phantasmVersion"] = Views::PluginVersion();
            j["maxDrawing"] = ViewCore::MaxDrawing(Views::PluginVersion(), vr);
            j["shotsFolder"] = Settings::Utf8(Shots::Folder());
            if (auto* player = RE::PlayerCharacter::GetSingleton(); player && player->GetActorBase())
                j["playerFemale"] = player->GetActorBase()->GetSex() == RE::SEX::kFemale;
            if (const auto target = People::CrosshairActor()) j["target"] = People::Hex(target);
            // PrismaUI loads the page hidden at the main menu and it says hello then: only an open menu counts.
            g_menuOpen = !Ui::IsPrisma() || Ui::IsOpen();
            j["open"] = g_menuOpen;
            return j;
        }

        Views::Hints HintsArg(const json& req)
        {
            Views::Hints out;
            const auto it = req.find("hints");
            if (it == req.end() || !it->is_object()) return out;
            for (const auto& [k, v] : it->items()) {
                if (k.empty() || k.size() > 32) continue;
                out[k] = HintValue(v);
            }
            return out;
        }

        /// The idle hint asks to play an animation file: only a relative .hkx (AnimPathCore::SafeToPlay).
        void CheckIdle(const std::string& key, const std::string& value)
        {
            if (key != "idle" || value.rfind("file:", 0) != 0) return;
            if (!AnimPathCore::SafeToPlay(value.substr(5))) throw Fail{ "badRequest", "not an animation file" };
        }

        /// The page's stages on PrismaUI (Underlay.h): {pageW, pageH, boxes: [{slot, x, y, w, h, cx, cy, cw, ch, r}]},
        /// in the page's device pixels.
        void StageRects(const json& req)
        {
            if (!Ui::IsPrisma()) return;
            std::vector<Underlay::PageBox> boxes;
            if (const auto it = req.find("boxes"); it != req.end() && it->is_array()) {
                for (const auto& b : *it) {
                    if (!b.is_object() || boxes.size() >= HostCore::kMaxBoxes) continue;
                    auto slot = Str(b, "slot");
                    if (!ViewCore::IsSlot(slot)) continue;
                    const auto f = [&](const char* k) { return static_cast<float>(Num(b, k)); };
                    boxes.push_back({ std::move(slot), { { f("x"), f("y"), f("w"), f("h") },
                                                         { f("cx"), f("cy"), f("cw"), f("ch") }, f("r") } });
                }
            }
            Underlay::SetBoxes(static_cast<float>(Num(req, "pageW")), static_cast<float>(Num(req, "pageH")),
                               std::move(boxes));
        }

        json ViewOpen(const json& req)
        {
            const auto slot = SlotArg(req);
            const bool item = Str(req, "kind") == "item";
            const auto form = FormArg(req);
            if (!g_menuOpen) return json{ { "state", "unavailable" }, { "reason", "closed" } };
            if (!item && !People::Showable(form)) return json{ { "state", "unavailable" }, { "reason", "notLoaded" } };
            if (item && !RE::TESForm::LookupByID(form)) throw Fail{ "notFound", "noItem" };
            const auto hints = HintsArg(req);
            for (const auto& [k, v] : hints) CheckIdle(k, v);
            std::string why;
            if (!Views::Open(slot, item, form, Num(req, "width"), Num(req, "height"), Num(req, "scale", 1.0), hints, why))
                return json{ { "state", "unavailable" }, { "reason", why } };
            return json{ { "state", "opening" } };
        }

        /// The Studio's Save picture: a Figure (what the view shows) or a transparent Cutout, written as a PNG.
        void Shot(const json& req, int id)
        {
            const auto form = FormArg(req);
            auto* actor = People::Showable(form);
            if (!actor) throw Fail{ "refused", "notLoaded" };
            const bool cutout = Str(req, "framing") == "cutout";
            const auto size = Settings::Get().shotSize;
            // Figure takes the view's own camera and light; a Cutout takes the still hints.
            std::string hints;
            if (cutout) {
                hints = std::format("turn={};pitch={}", std::clamp(Num(req, "turn", 15.0), -180.0, 180.0),
                                    std::clamp(Num(req, "pitch", 5.0), -80.0, 80.0));
                if (const auto l = Str(req, "lighting"); !l.empty() && l.size() < 24) hints += ";lighting=" + l;
                hints += Str(req, "weapons") == "off" ? ";weapons=off" : ";weapons=on";
            }
            const char* n = actor->GetDisplayFullName();
            std::string stem = std::string(n && *n ? n : "actor") + (cutout ? "_cutout" : "_figure");
            const bool ok = Views::Still(form, cutout ? size * 2 / 3 : size * 16 / 9, size, cutout, hints,
                [id, cutout, stem = std::move(stem)](bool good, const std::string& reason, std::vector<std::uint8_t> rgba,
                                                     std::uint32_t w, std::uint32_t h) {
                    if (!good) {
                        Send({ { "id", id }, { "ok", false }, { "error", "refused" }, { "detail", reason } });
                        return;
                    }
                    Shots::Save(std::move(rgba), w, h, cutout, stem, [id, w, h](bool saved, const std::string& pathOrError) {
                        if (saved) Send({ { "id", id }, { "ok", true }, { "data", { { "file", pathOrError }, { "width", w }, { "height", h } } } });
                        else Send({ { "id", id }, { "ok", false }, { "error", "refused" }, { "detail", pathOrError } });
                    });
                },
                nullptr);
            if (!ok) throw Fail{ "refused", "stillRefused" };
        }

        void TurntableCancel()
        {
            std::vector<std::uint32_t> running;
            {
                std::lock_guard lock(g_turnMutex);
                ++g_turnSession;
                running.swap(g_turnRequests);
            }
            for (const auto r : running) Views::CancelStill(r);
        }

        /// The wardrobe's turntable of stills: `frames` Cutouts round the NPC, each pushed as a data URL.
        json Turntable(const json& req)
        {
            const auto form = FormArg(req);
            if (!People::Showable(form)) throw Fail{ "refused", "notLoaded" };
            const auto frames = TurnCore::ClampFrames(static_cast<std::uint32_t>(std::clamp(Num(req, "frames", 24.0), 0.0, 64.0)));
            const auto h = static_cast<std::uint32_t>(std::clamp(Num(req, "height", 900.0), 256.0, 2048.0));
            const auto w = h * 2 / 3;
            const auto light = Str(req, "lighting");
            TurntableCancel();
            std::uint32_t session = 0;
            {
                std::lock_guard lock(g_turnMutex);
                session = g_turnSession;
            }
            for (std::uint32_t i = 0; i < frames; ++i) {
                auto hints = std::format("turn={};pitch=5;weapons=off", TurnCore::TurnFor(i, frames));
                if (!light.empty() && light.size() < 24) hints += ";lighting=" + light;
                std::uint32_t requestId = 0;
                const bool ok = Views::Still(form, w, h, true, hints,
                    [session, i](bool good, const std::string& reason, std::vector<std::uint8_t> rgba, std::uint32_t pw,
                                 std::uint32_t ph) {
                        {
                            std::lock_guard lock(g_turnMutex);
                            if (session != g_turnSession) return;
                        }
                        if (!good) {
                            Push({ { "event", "frame" }, { "session", session }, { "index", i }, { "ok", false }, { "reason", reason } });
                            return;
                        }
                        Shots::DataUrl(std::move(rgba), pw, ph, true, [session, i](bool encoded, std::string url) {
                            {
                                std::lock_guard lock(g_turnMutex);
                                if (session != g_turnSession) return;
                            }
                            Push({ { "event", "frame" }, { "session", session }, { "index", i }, { "ok", encoded },
                                   { "url", std::move(url) } });
                        });
                    },
                    &requestId);
                if (!ok) {
                    // The frames already asked for would come in under a session the page then waits on forever.
                    TurntableCancel();
                    throw Fail{ "refused", "stillRefused" };
                }
                std::lock_guard lock(g_turnMutex);
                g_turnRequests.push_back(requestId);
            }
            return { { "session", session }, { "frames", frames } };
        }

        /// A try-on answer from the lane.
        TryOn::Done LaneReply(int id)
        {
            return [id](bool ok, json data) {
                if (ok) Send({ { "id", id }, { "ok", true }, { "data", std::move(data) } });
                else Send({ { "id", id }, { "ok", false }, { "error", "refused" }, { "detail", data.is_string() ? data.get<std::string>() : "" } });
            };
        }

        /// The ops answered at once; shot and the try-on ops answer later (return nullopt).
        std::optional<json> Dispatch(const json& req, int id)
        {
            const auto op = req["op"].get<std::string>();
            if (op == "hello") return Hello();
            if (op == "setShowFps") {
                const auto on = req.find("on");
                Settings::SetShowFps(on == req.end() || !on->is_boolean() || on->get<bool>());
                return json::object();
            }
            if (op == "setFreezeWorld") {
                const auto on = req.find("on");
                Settings::SetFreezeWorld(on == req.end() || !on->is_boolean() || on->get<bool>());
                Ui::ApplyFreezeWorld();
                return json::object();
            }
            if (op == "people") return json{ { "people", People::List() } };
            if (op == "items") {
                const auto offset = static_cast<std::size_t>(std::clamp(Num(req, "offset"), 0.0, 1.0e7));
                const auto limit = static_cast<std::size_t>(std::clamp(Num(req, "limit", 80.0), 1.0, 200.0));
                if (Str(req, "source") == "held") {
                    auto* actor = People::Showable(FormArg(req, "actor"));
                    if (!actor) throw Fail{ "refused", "notLoaded" };
                    return Items::Held(actor, Str(req, "text"), Str(req, "filter"));
                }
                return Items::Search(Str(req, "text"), Str(req, "filter"), offset, limit);
            }
            if (op == "viewOpen") return ViewOpen(req);
            if (op == "viewHint") {
                const auto key = Str(req, "key");
                const auto value = HintValue(req.value("value", json()));
                CheckIdle(key, value);
                Views::Hint(SlotArg(req), key, value);
                return json::object();
            }
            if (op == "viewQuery") {
                Views::Query(SlotArg(req), Str(req, "key"));
                return json::object();
            }
            if (op == "viewOrbit") {
                const auto dx = static_cast<float>(Num(req, "dx")), dy = static_cast<float>(Num(req, "dy"));
                if (const auto it = req.find("slots"); it != req.end() && it->is_array()) {
                    for (const auto& s : *it) {
                        if (s.is_string() && ViewCore::IsSlot(s.get<std::string>())) Views::Orbit(s.get<std::string>(), dx, dy);
                    }
                } else {
                    Views::Orbit(SlotArg(req), dx, dy);
                }
                return json::object();
            }
            if (op == "viewZoom") {
                Views::Zoom(SlotArg(req), static_cast<float>(Num(req, "dz")), static_cast<float>(Num(req, "u", 0.5)),
                            static_cast<float>(Num(req, "v", 0.5)));
                return json::object();
            }
            if (op == "viewRefresh") {
                Views::Refresh(SlotArg(req));
                return json::object();
            }
            if (op == "viewClose") {
                Views::Close(SlotArg(req));
                return json::object();
            }
            if (op == "viewCloseAll") {
                Views::CloseAll();
                return json::object();
            }
            if (op == "close") {
                Ui::Close();
                return json::object();
            }
            if (op == "stageRects") {
                StageRects(req);
                return json::object();
            }
            if (op == "anims") return Anims::List(Str(req, "folder"));
            if (op == "animSearch") return Anims::Search(Str(req, "text"), static_cast<std::size_t>(std::clamp(Num(req, "limit", 300.0), 1.0, 1000.0)));
            if (op == "shot") {
                Shot(req, id);
                return std::nullopt;
            }
            if (op == "turntable") return Turntable(req);
            if (op == "turntableCancel") {
                TurntableCancel();
                return json::object();
            }
            // A fitting only while the menu is open: one asked for just before the close would run after the close's
            // revert and last through play.
            if ((op == "tryBegin" || op == "tryWear") && !g_menuOpen) throw Fail{ "refused", "closed" };
            if (op == "tryBegin") {
                TryOn::Begin(FormArg(req), LaneReply(id));
                return std::nullopt;
            }
            if (op == "tryWear") {
                TryOn::Wear(FormArg(req), FormArg(req, "item"), LaneReply(id));
                return std::nullopt;
            }
            if (op == "tryRevert") {
                TryOn::Revert(FormArg(req), LaneReply(id));
                return std::nullopt;
            }
            throw Fail{ "badRequest", "unknown op " + op };
        }

        /// Game thread (posted by OnRequest, or by another host's listener through HandleText).
        void Handle(const std::string& text)
        {
            json req = json::parse(text, nullptr, false);
            // Anything without an op is not a request (an echo of a reply would loop): it gets no answer.
            if (req.is_discarded() || !req.is_object() || !req.contains("op") || !req["op"].is_string()) return;
            int id = 0;
            try {
                id = req.value("id", 0);
                if (auto data = Dispatch(req, id)) Send({ { "id", id }, { "ok", true }, { "data", std::move(*data) } });
            } catch (const Fail& f) {
                Send({ { "id", id }, { "ok", false }, { "error", f.code }, { "detail", f.detail } });
            } catch (const std::exception& e) {
                SKSE::log::error("bridge: '{}' failed: {}", req["op"].get<std::string>(), e.what());
                Send({ { "id", id }, { "ok", false }, { "error", "badRequest" }, { "detail", e.what() } });
            }
        }

        /// Render thread, inside Present: copy, post, return.
        void OnRequest(MAGELIGHT_API::ViewId, const char* arg, void*)
        {
            auto* box = new std::string(arg ? arg : "");
            const auto r = g_v4.load(std::memory_order_acquire)->PostGameTask(
                [](void* user) {
                    std::unique_ptr<std::string> text(static_cast<std::string*>(user));
                    Handle(*text);
                },
                box);
            if (r != MAGELIGHT_API::Result::Ok) delete box;
        }
    }

    void Attach(const MAGELIGHT_API::MagelightApi4* v4, MAGELIGHT_API::ViewId view)
    {
        g_v4.store(v4, std::memory_order_release);
        g_view.store(view, std::memory_order_release);
        if (v4->RegisterJSListenerEx(view, kChannelIn, OnRequest, nullptr) != MAGELIGHT_API::Result::Ok)
            SKSE::log::error("bridge: could not listen on the page");
    }

    void AttachSender(SendFn send) { g_sender.store(send, std::memory_order_release); }

    void HandleText(const std::string& text) { Handle(text); }

    void Push(const json& message) { Send(message); }

    void OnMenuClosed()
    {
        // A view nobody closes keeps drawing during play.
        Views::CloseAll();
        TurntableCancel();
        TryOn::RevertAll();
        g_menuOpen = false;
    }
}
