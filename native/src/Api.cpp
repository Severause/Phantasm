// Phantasm - Api: the exported RequestPluginAPI and the interface tables it hands out (Phantasm_API.h). Each entry
// forwards to Host; nothing here touches the game.

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include "Host.h"

namespace
{
    using namespace PHANTASM_API;

    constexpr Interface1 kInterface1Table{
        .size             = sizeof(Interface1),
        .interfaceVersion = kInterface1,
        .pluginVersion    = PLUGIN_VERSION_MAJOR * 10000 + PLUGIN_VERSION_MINOR * 100 + PLUGIN_VERSION_PATCH,
        .RegisterClient   = &Phantasm::Host::RegisterClient,
        .ReleaseClient    = &Phantasm::Host::ReleaseClient,
        .OpenView         = &Phantasm::Host::OpenView,
        .CloseView        = &Phantasm::Host::CloseView,
        .ResizeView       = &Phantasm::Host::ResizeView,
        .Orbit            = &Phantasm::Host::Orbit,
        .Zoom             = &Phantasm::Host::Zoom,
        .Refresh          = &Phantasm::Host::Refresh,
        .SetHint          = &Phantasm::Host::SetHint,
        .QueryHint        = &Phantasm::Host::QueryHint,
        .RequestStill     = &Phantasm::Host::RequestStill,
        .RebuildActor3D   = &Phantasm::Host::RebuildActor3D,
        .OpenItemView     = &Phantasm::Host::OpenItemView,
        .CancelStill      = &Phantasm::Host::CancelStill,
        .RequestStillHinted = &Phantasm::Host::RequestStillHinted,
        .RequestItemStill   = &Phantasm::Host::RequestItemStill,
    };
}

extern "C" __declspec(dllexport) void* RequestPluginAPI(std::uint32_t interfaceVersion)
{
    if (interfaceVersion == kInterface1) return const_cast<Interface1*>(&kInterface1Table);
    SKSE::log::warn("Phantasm: a client asked for interface {}, which this build ({}) does not serve", interfaceVersion,
                    PLUGIN_VERSION);
    return nullptr;
}
