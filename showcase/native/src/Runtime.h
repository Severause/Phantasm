#pragma once

// Phantasm Showcase - Runtime: the order things start and load in.

namespace Showcase::Runtime
{
    /// kDataLoaded: settings, the item index, the Phantasm client, the interface, the ImGui panel.
    void OnDataLoaded();

    /// kPreLoadGame: a load is starting; work posted for the ending session does nothing.
    void OnPreLoad();

    /// kPostLoadGame and kNewGame (SKSE VR sends no kNewGame, which costs nothing here).
    void OnPostLoad();
}
