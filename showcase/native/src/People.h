#pragma once

// Phantasm Showcase - People: the actors the pickers offer (PeopleCore's rules). Game thread.

namespace Showcase::People
{
    /// [{ formId, name, group: player|follower|nearby, female, child, distance }], in PeopleCore's order: the player,
    /// the teammates, then the living, loaded, named actors in the high process within PeopleCore::kNearbyRadius who
    /// are not hostile.
    nlohmann::json List();

    /// The actor when it is one the pages may show now (alive, loaded, the player included), else null.
    RE::Actor* Showable(std::uint32_t id);

    /// The actor under the crosshair, when it is a living NPC, else 0.
    std::uint32_t CrosshairActor();

    std::string Hex(std::uint32_t id);
}
