#pragma once

// Phantasm - ViewRegistryCore: the API's clients and views as plain data, engine-free (native/build.ps1 -Tests).
// Host.cpp owns one Registry under its mutex (any thread) and turns its answers into renderer calls and client events.
//
// The drawing rule: the newest open views draw (those whose OpenView or OpenItemView came last), up to kMaxDrawing of
// them, kMaxDrawingVR on VR; when one closes, the newest view not drawing takes its place. A view that is not a drawer
// is Waiting.

#include "Phantasm_API.h"

#include "AsciiText.h"
#include "LiveViewSizingCore.h"
#include "MannequinClipCore.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Phantasm::ViewRegistryCore
{
    using PHANTASM_API::ClientId;
    using PHANTASM_API::Framing;
    using PHANTASM_API::Result;
    using PHANTASM_API::ViewId;

    inline constexpr std::size_t kMaxNameLength     = 24;
    inline constexpr std::size_t kMaxViewsPerClient = 16;
    inline constexpr std::size_t kMaxHintLength     = 2048;   // room for a props hint of a scene's objects
    /// How many views draw at once: each is a draw of its own in the game's frame (LiveViewBudgetCore).
    inline constexpr std::size_t kMaxDrawing   = 4;
    inline constexpr std::size_t kMaxDrawingVR = 2;

    /// A client or slot name: 1-24 of a-z, 0-9, '-' and '_'. Lower case only, so an image name means one thing on a
    /// case-folding file system and in Magelight's image table.
    inline bool ValidName(std::string_view name)
    {
        if (name.empty() || name.size() > kMaxNameLength) return false;
        return std::all_of(name.begin(), name.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        });
    }

    inline std::string ImageName(std::string_view client, std::string_view slot)
    {
        std::string out = "phantasm.";
        out.append(client).append(".").append(slot);
        return out;
    }

    struct View
    {
        ViewId        id      = 0;
        ClientId      client  = 0;
        std::string   slot;
        std::string   image;           // ImageName(client, slot)
        std::uint32_t actor   = 0;      // the actor, or an item view's base form
        bool          item    = false;  // an item view (OpenItemView)
        std::uint32_t width   = 0, height = 0;
        Framing       framing = Framing::Figure;
        std::uint64_t openedSeq = 0;   // the OpenView that last targeted it; the highest draws
        std::map<std::string, std::string> hints;
    };

    struct Client
    {
        ClientId    id = 0;
        std::string name;
    };

    class Registry
    {
    public:
        Result RegisterClient(std::string_view name, ClientId& out)
        {
            out = 0;
            if (!ValidName(name)) return Result::BadArgument;
            for (const auto& c : m_clients) {
                if (c.name == name) return Result::NameTaken;
            }
            m_clients.push_back({ ++m_lastClient, std::string(name) });
            out = m_clients.back().id;
            return Result::Ok;
        }

        /// Frees the name; the client's views are removed and listed in `ended`.
        Result ReleaseClient(ClientId client, std::vector<View>& ended)
        {
            const auto it = std::find_if(m_clients.begin(), m_clients.end(), [&](const Client& c) { return c.id == client; });
            if (it == m_clients.end()) return Result::UnknownClient;
            for (auto v = m_views.begin(); v != m_views.end();) {
                if (v->client == client) {
                    ended.push_back(std::move(*v));
                    v = m_views.erase(v);
                } else {
                    ++v;
                }
            }
            m_clients.erase(it);
            return Result::Ok;
        }

        const Client* FindClient(ClientId id) const
        {
            for (const auto& c : m_clients) {
                if (c.id == id) return &c;
            }
            return nullptr;
        }

        /// Opens a view in the client's slot, or re-targets the one there (same id, `created` false), of an actor or
        /// (item) an item's base form. Either way it becomes the newest, so it draws.
        Result Open(ClientId client, std::string_view slot, std::uint32_t actor, std::uint32_t width,
                    std::uint32_t height, Framing framing, ViewId& out, bool& created, bool item = false)
        {
            out     = 0;
            created = false;
            const Client* c = FindClient(client);
            if (!c) return Result::UnknownClient;
            if (!ValidName(slot) || actor == 0 || width == 0 || height == 0) return Result::BadArgument;
            if (framing != Framing::Figure) {
                return framing == Framing::Portrait ? Result::Unsupported : Result::BadArgument;
            }
            View* v = FindSlot(client, slot);
            if (!v) {
                const auto held = std::count_if(m_views.begin(), m_views.end(), [&](const View& x) { return x.client == client; });
                if (static_cast<std::size_t>(held) >= kMaxViewsPerClient) return Result::TooMany;
                View nv;
                nv.id     = ++m_lastView;
                nv.client = client;
                nv.slot   = std::string(slot);
                nv.image  = ImageName(c->name, slot);
                m_views.push_back(std::move(nv));
                v       = &m_views.back();
                created = true;
            }
            v->actor     = actor;
            v->item      = item;
            v->width     = width;
            v->height    = height;
            v->framing   = framing;
            v->openedSeq = ++m_seq;
            out = v->id;
            return Result::Ok;
        }

        /// Removes the view; `closed` receives it.
        Result Close(ViewId id, View& closed)
        {
            const auto it = std::find_if(m_views.begin(), m_views.end(), [&](const View& v) { return v.id == id; });
            if (it == m_views.end()) return Result::UnknownView;
            closed = std::move(*it);
            m_views.erase(it);
            return Result::Ok;
        }

        /// Valid until the next Open, Close, ReleaseClient or TakeAll.
        View* Find(ViewId id)
        {
            for (auto& v : m_views) {
                if (v.id == id) return &v;
            }
            return nullptr;
        }

        /// The views that draw now, newest first: the `cap` newest open ones.
        std::vector<ViewId> Drawers(std::size_t cap) const
        {
            std::vector<const View*> byAge;
            byAge.reserve(m_views.size());
            for (const auto& v : m_views) byAge.push_back(&v);
            std::sort(byAge.begin(), byAge.end(), [](const View* a, const View* b) { return a->openedSeq > b->openedSeq; });
            std::vector<ViewId> out;
            for (std::size_t i = 0; i < byAge.size() && i < cap; ++i) out.push_back(byAge[i]->id);
            return out;
        }

        /// The newest open view, or 0.
        ViewId Drawer() const
        {
            const auto d = Drawers(1);
            return d.empty() ? 0 : d.front();
        }

        /// Every open view, for a game load that ends them all.
        std::vector<View> TakeAll()
        {
            std::vector<View> out = std::move(m_views);
            m_views.clear();
            return out;
        }

        const std::vector<View>& Views() const { return m_views; }

    private:
        View* FindSlot(ClientId client, std::string_view slot)
        {
            for (auto& v : m_views) {
                if (v.client == client && v.slot == slot) return &v;
            }
            return nullptr;
        }

        std::vector<Client> m_clients;
        std::vector<View>   m_views;
        ClientId            m_lastClient = 0;
        ViewId              m_lastView   = 0;
        std::uint64_t       m_seq        = 0;
    };

    /// A hint or query key as the registry keeps it: ASCII lower case, so keys are case-insensitive.
    inline std::string LowerKey(std::string_view key) { return AsciiText::Lowered(key); }

    /// A hint as the registry keeps it: the key lower-cased, the value as given up to kMaxHintLength bytes; an empty
    /// value removes the hint (the renderer's default applies).
    inline void SetHint(View& v, std::string_view key, std::string_view value)
    {
        const std::string k = LowerKey(key);
        if (k.empty()) return;
        if (value.empty()) {
            v.hints.erase(k);
            return;
        }
        v.hints[k] = std::string(value.substr(0, kMaxHintLength));
    }

    inline std::string_view Hint(const View& v, std::string_view key)
    {
        const auto it = v.hints.find(std::string(key));
        return it == v.hints.end() ? std::string_view{} : std::string_view(it->second);
    }

    /// The "idle" hint's value for the renderer: the "still" sentinel in any case becomes the renderer's exact
    /// kIdleStill (it compares that one exactly); a clip name passes as given (clip names match in any case there).
    inline std::string IdleHintValue(std::string_view value)
    {
        if (MannequinClipCore::EqualsI(value, MannequinClipCore::kIdleStill)) return std::string(MannequinClipCore::kIdleStill);
        return std::string(value);
    }

    /// One other actor of a view's scene (the "scene" hint): who, the idle they play there (the idle hint's forms),
    /// and where, from the view's own actor: x along their facing, y to their right, z up (game units), r degrees
    /// turned clockwise seen from above.
    struct SceneCastEntry
    {
        std::uint32_t actor = 0;
        std::string   idle;
        float         x = 0.0f, y = 0.0f, z = 0.0f, r = 0.0f;
    };
    inline constexpr std::size_t kMaxSceneCast = 3;

    namespace HintFields
    {
        inline std::vector<std::string_view> Split(std::string_view v, char sep)
        {
            std::vector<std::string_view> parts;
            for (std::size_t at = 0;;) {
                const std::size_t end = v.find(sep, at);
                parts.push_back(v.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at));
                if (end == std::string_view::npos) break;
                at = end + 1;
            }
            return parts;
        }

        /// A FormID in hex (a 0x before it allowed); 0 for none, a word or one longer than eight digits.
        inline std::uint32_t FormId(std::string_view field)
        {
            std::string_view id = AsciiText::Trim(field);
            if (id.size() > 2 && id[0] == '0' && (id[1] == 'x' || id[1] == 'X')) id.remove_prefix(2);
            if (id.empty() || id.size() > 8 ||
                !std::all_of(id.begin(), id.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; })) {
                return 0;
            }
            return static_cast<std::uint32_t>(std::strtoul(std::string(id).c_str(), nullptr, 16));
        }

        /// "x,y,z,r" as four finite numbers; false (and v untouched) for anything else.
        inline bool Offset(std::string_view field, float (&v)[4])
        {
            const auto nums = Split(AsciiText::Trim(field), ',');
            if (nums.size() != 4) return false;
            float got[4]{};
            for (std::size_t i = 0; i < 4; ++i) {
                const std::string s(AsciiText::Trim(nums[i]));
                char*             end = nullptr;
                got[i]                = std::strtof(s.c_str(), &end);
                if (s.empty() || end != s.c_str() + s.size() || !std::isfinite(got[i])) return false;
            }
            std::copy(std::begin(got), std::end(got), std::begin(v));
            return true;
        }
    }

    /// The "scene" hint's entries: "<FormID in hex>|<idle>|<x,y,z,r>", separated by ';' (the offset may be left off;
    /// spaces around a field are dropped). An entry with no FormID, the view's own actor (self), a repeat, no clip
    /// to play (no idle, or still: an extra is drawn only in its clip's pose) or one past kMaxSceneCast is left out;
    /// an offset that is not four finite numbers leaves the entry at the origin.
    inline std::vector<SceneCastEntry> SceneCastHint(std::string_view value, std::uint32_t self)
    {
        std::vector<SceneCastEntry> out;
        for (const auto entry : HintFields::Split(value, ';')) {
            const auto fields = HintFields::Split(entry, '|');
            SceneCastEntry e;
            e.actor = HintFields::FormId(fields[0]);
            if (e.actor == 0 || e.actor == self || out.size() >= kMaxSceneCast ||
                std::any_of(out.begin(), out.end(), [&](const SceneCastEntry& o) { return o.actor == e.actor; })) {
                continue;
            }
            if (fields.size() > 1) e.idle = IdleHintValue(AsciiText::Trim(fields[1]));
            if (e.idle.empty() || e.idle == MannequinClipCore::kIdleStill) continue;
            float v[4]{};
            if (fields.size() > 2 && HintFields::Offset(fields[2], v)) {
                e.x = v[0];
                e.y = v[1];
                e.z = v[2];
                e.r = v[3];
            }
            out.push_back(std::move(e));
        }
        return out;
    }

    /// A prop's model as BSModelDB takes it: under meshes, without the "meshes\" (the model DB adds it), backslashes,
    /// a .nif. "" for a path that could leave Data (a root, a drive, a "..") or names no .nif.
    inline std::string PropModelPath(std::string_view text)
    {
        std::string p(AsciiText::Trim(text));
        std::replace(p.begin(), p.end(), '/', '\\');
        if (p.empty() || p.front() == '\\' || p.find(':') != std::string::npos || !AsciiText::EndsWithI(p, ".nif")) {
            return {};
        }
        for (std::size_t at = 0; at <= p.size();) {
            const std::size_t end = (std::min)(p.find('\\', at), p.size());
            if (end - at == 2 && p.compare(at, 2, "..") == 0) return {};
            at = end + 1;
        }
        if (AsciiText::StartsWithI(p, "meshes\\")) p.erase(0, 7);
        return p.empty() || p.front() == '\\' ? std::string{} : p;
    }

    /// One prop of a view's scene (the "props" hint): a model drawn on one of the scene's actors, or standing in it.
    struct ScenePropEntry
    {
        std::uint32_t actor = 0;   // the actor it is on; 0 for one standing at a place
        std::string   model;       // PropModelPath's form
        std::string   node;        // the node it hangs from; "" for the one its model names (its Prn string)
        float         x = 0.0f, y = 0.0f, z = 0.0f, r = 0.0f;   // a standing prop's place, as a scene entry's

        bool operator==(const ScenePropEntry&) const = default;
    };
    inline constexpr std::size_t kMaxSceneProps = 16;

    /// The "props" hint's entries, separated by ';': "<FormID in hex>|<model>" on that actor at the node its model
    /// names, "<FormID in hex>|<model>|<node>" at that node, and "place|<model>|<x,y,z,r>" standing at the view's
    /// actor's place moved and turned as a scene entry (the offset may be left off). An entry with no actor, no
    /// model, a repeat or one past kMaxSceneProps is left out.
    inline std::vector<ScenePropEntry> ScenePropsHint(std::string_view value)
    {
        std::vector<ScenePropEntry> out;
        for (const auto entry : HintFields::Split(value, ';')) {
            const auto fields = HintFields::Split(entry, '|');
            if (fields.size() < 2 || out.size() >= kMaxSceneProps) continue;
            ScenePropEntry e;
            const bool     placed = AsciiText::EqualsI(AsciiText::Trim(fields[0]), "place");
            e.actor               = placed ? 0u : HintFields::FormId(fields[0]);
            e.model               = PropModelPath(fields[1]);
            if ((!placed && e.actor == 0) || e.model.empty()) continue;
            if (fields.size() > 2) {
                float v[4]{};
                if (!placed) {
                    e.node = std::string(AsciiText::Trim(fields[2]));
                } else if (HintFields::Offset(fields[2], v)) {
                    e.x = v[0];
                    e.y = v[1];
                    e.z = v[2];
                    e.r = v[3];
                }
            }
            if (std::find(out.begin(), out.end(), e) != out.end()) continue;
            out.push_back(std::move(e));
        }
        return out;
    }

    /// Where a scene's entry stands: x y z in game units, angle the engine's heading in radians.
    struct ScenePlacement
    {
        float x = 0.0f, y = 0.0f, z = 0.0f, angle = 0.0f;
    };

    /// The place x along the facing, y to the right, z up and turned r degrees from the view's actor at (ox, oy, oz)
    /// facing `heading` (radians). Skyrim's heading turns clockwise seen from above: forward is (sin a, cos a), right
    /// (cos a, -sin a).
    inline ScenePlacement ScenePlace(float ox, float oy, float oz, float heading, float x, float y, float z, float r)
    {
        const float s = std::sin(heading), c = std::cos(heading);
        constexpr float kDegToRad = 3.14159265358979f / 180.0f;
        return { ox + x * s + y * c, oy + x * c - y * s, oz + z, heading + r * kDegToRad };
    }

    inline ScenePlacement ScenePlace(float ox, float oy, float oz, float heading, const SceneCastEntry& e)
    {
        return ScenePlace(ox, oy, oz, heading, e.x, e.y, e.z, e.r);
    }

    /// Orbit and Zoom take finite numbers only: a NaN or an infinity would stay in the view's camera until the view
    /// starts again. False = BadArgument.
    inline bool CameraInputOk(std::initializer_list<float> values)
    {
        return std::all_of(values.begin(), values.end(), [](float f) { return std::isfinite(f); });
    }

    /// The smallest side, in device pixels, the renderer draws. A view opened or resized smaller is accepted (the API
    /// refuses only zero) and drawn at this side, so it still gets Shown; the page scales the picture down.
    inline constexpr std::uint32_t kMinDrawSide = 4;
    static_assert(static_cast<float>(kMinDrawSide) == LiveViewSizingCore::kMinDrawPx,
                  "Host's smallest drawn side is the renderer's");

    inline std::uint32_t DrawSide(std::uint32_t side) { return std::max(side, kMinDrawSide); }

    /// Host's side of the renderer's GPU refusal window (LiveViewLifecycleCore::kGpuRetryDelay). While it lasts the
    /// renderer refuses every start with "gpu-unavailable", so Host starts nothing: the views behind the one that was
    /// refused stay Waiting instead of each being started, refused and ended in turn, and one Reconcile tries again
    /// when the window has passed. Host notes the refusal when its event reaches it, after the renderer started the
    /// window, so Host's window ends no earlier than the renderer's.
    class GpuHold
    {
    public:
        using Clock = std::chrono::steady_clock;

        void Note(Clock::time_point now, std::chrono::milliseconds window)
        {
            m_until = std::max(m_until, now + window);
        }

        /// The time left before a start may be tried; zero = start.
        std::chrono::milliseconds Left(Clock::time_point now) const
        {
            if (now >= m_until) return std::chrono::milliseconds(0);
            // Rounded up, so a wait of Left() always reaches the end of the window.
            return std::chrono::ceil<std::chrono::milliseconds>(m_until - now);
        }

    private:
        Clock::time_point m_until{};
    };
}
