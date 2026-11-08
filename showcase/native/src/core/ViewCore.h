#pragma once

// Phantasm Showcase - ViewCore: the slots the page draws into and how many views draw at once. Engine-free.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace Showcase::ViewCore
{
    /// Every slot the page may name. Phantasm keeps a slot's image registered for the life of the process, so the set
    /// is small and fixed: one per place on the page, never one per actor or item.
    inline constexpr std::array<std::string_view, 12> kSlots = {
        "lineup0", "lineup1", "lineup2", "lineup3", "armory0", "armory1", "armory2", "armory3",
        "anim",    "studio",  "wardrobe", "before",
    };

    inline bool IsSlot(std::string_view s)
    {
        return std::find(kSlots.begin(), kSlots.end(), s) != kSlots.end();
    }

    /// The ImGui panel's one view (ImGuiPanel.h). Not a page slot: the page can neither name it nor close it.
    inline constexpr std::string_view kPanelSlot = "imgui";

    /// The first Phantasm that draws several views at once (0.4.0), as Interface1::pluginVersion counts.
    inline constexpr std::uint32_t kMultiViewVersion = 400;

    /// How many views draw at once: Phantasm 0.4.0 draws the newest 4 (2 on VR); an older one only the newest.
    constexpr std::uint32_t MaxDrawing(std::uint32_t pluginVersion, bool vr)
    {
        if (pluginVersion < kMultiViewVersion) return 1;
        return vr ? 2u : 4u;
    }

    /// A box of `css` CSS pixels at devicePixelRatio `scale` in device pixels, as OpenView takes it: 0 for a box that
    /// is not one (not finite, not positive), else clamped to 64..4096.
    inline std::uint32_t Pixels(double css, double scale)
    {
        if (!std::isfinite(css) || !std::isfinite(scale) || css <= 0.0 || scale <= 0.0) return 0;
        return static_cast<std::uint32_t>(std::clamp(std::lround(css * scale), 64L, 4096L));
    }
}
