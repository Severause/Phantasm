#pragma once

// Phantasm Showcase - Views: every live view and still the pages ask Phantasm for (docs/DESIGN.md section 4), and the
// ImGui panel's view (section 10). Phantasm is required for anything to draw, but the plugin loads without it: the
// page then says why. One client, "showcase", holds at most one view per slot: the page's (ViewCore::kSlots) and the
// panel's (ViewCore::kPanelSlot).
//
// Threads: every function may be called from any thread; none touches RE:: state but RebuildActor3D, which runs on
// the lane. Phantasm's events arrive on the game thread: a page slot's are forwarded to the page as pushes, the
// panel's go to ImGuiPanel::OnViewEvent, called under Views' lock.

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Showcase::Views
{
    void Initialize();

    /// "" when views can open; else "absent", "old" (no item views) or "refused".
    std::string_view Unavailable();

    /// Interface1::pluginVersion (MAJOR*10000 + MINOR*100 + PATCH), 0 without Phantasm.
    std::uint32_t PluginVersion();

    using Hints = std::map<std::string, std::string>;

    /// Shows an actor (item false) or an item in `slot`, in a box of cssW x cssH CSS pixels at devicePixelRatio
    /// `scale`: opens the slot's view, re-targets it or resizes it, and brings its hints to `hints` (a key missing
    /// from `hints` is left as it is; an empty value removes it). False with `why` when nothing opened.
    bool Open(std::string_view slot, bool item, std::uint32_t formId, double cssW, double cssH, double scale,
              const Hints& hints, std::string& why);

    /// The ImGui panel's view (ViewCore::kPanelSlot, ImGuiPanel.h) of an actor at width x height pixels: opened,
    /// re-targeted or resized as Open does, always with `hostless=on` (the panel draws the SRV itself). Its events go to
    /// ImGuiPanel::OnViewEvent, never to the page. `view` is the view's id (a re-target keeps it). False with `why`
    /// when nothing opened.
    bool OpenPanel(std::uint32_t formId, std::uint32_t width, std::uint32_t height, const Hints& hints, std::string& why,
                   std::uint32_t& view);

    bool Hint(std::string_view slot, const std::string& key, const std::string& value);

    /// Asks the slot's view for a query key; the answer comes as a `reply` push.
    bool Query(std::string_view slot, const std::string& key);

    bool Orbit(std::string_view slot, float dx, float dy);
    bool Zoom(std::string_view slot, float dz, float u, float v);
    bool Refresh(std::string_view slot);
    void Close(std::string_view slot);

    /// Closes every page view (the page or its menu went away); the ImGui panel's stays, it closes its own.
    void CloseAll();

    /// A still's answer: ok, the reason token when not, the pixels (RGBA8, sRGB; a Cutout premultiplied in linear
    /// light) and their size. Called once, on the game thread.
    using StillDone = std::function<void(bool ok, const std::string& reason, std::vector<std::uint8_t> rgba,
                                         std::uint32_t width, std::uint32_t height)>;

    /// One still of an actor: cutout false is a Figure (what the drawing view of that actor shows), true a Cutout on
    /// a transparent ground. hints: Phantasm's still hints ("turn=30;pitch=10;lighting=studio"). False when Phantasm
    /// refused it (done is not called then).
    bool Still(std::uint32_t actorId, std::uint32_t width, std::uint32_t height, bool cutout, const std::string& hints,
               StillDone done, std::uint32_t* requestId);
    void CancelStill(std::uint32_t requestId);

    /// Phantasm's RebuildActor3D, so no view of the actor shows a torn frame. Only on the lane (SKSE's UI-task
    /// queue), never inside SKSE's AddTask drain or a Phantasm event. False without Phantasm or when it refused.
    bool RebuildActor3D(std::uint32_t actorId);
}
