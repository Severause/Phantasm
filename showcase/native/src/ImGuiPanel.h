#pragma once

// Phantasm Showcase - ImGuiPanel: a live view in SKSE Menu Framework's Mod Control Panel (an ImGui menu), to show that
// a Phantasm view draws in an ImGui window as well as in a Magelight or PrismaUI page (docs/DESIGN.md section 10).
// A section "Phantasm Showcase > Live view" and a window it pops out, sharing one view (ViewCore::kPanelSlot). Nothing
// is registered without SKSE Menu Framework, or with one whose exports or ImGui build the panel was not made for.
//
// Threads: the framework calls the panel on its render thread, inside the frame's Present; the panel touches no RE::
// state there (the people list is built in an SKSE task) and calls Phantasm, which any thread may. OnViewEvent comes
// from Views on the game thread, under Views' lock, so the panel never calls Views while holding its own data lock.

#include <cstdint>
#include <string_view>

#include "Phantasm_API.h"

namespace Showcase::ImGuiPanel
{
    /// Registers the section and the window when a usable SKSE Menu Framework is loaded. Game thread, at kDataLoaded,
    /// after Views::Initialize.
    void Initialize();

    /// A game load is starting: the panel's view is closed (the load would end it anyway).
    void OnPreLoad();

    /// The panel's view `view` drew (Shown, with its SRV), waits or ended. A Shown's srv is AddRef'd and kept until
    /// the view's next Shown, Waiting or Ended; one handed to ImGui is kept until ImGui has rendered it (section 10).
    void OnViewEvent(PHANTASM_API::EventKind kind, PHANTASM_API::ViewId view, std::uint32_t formId,
                     ID3D11ShaderResourceView* srv, std::string_view reason);
}
