#pragma once

// Phantasm Showcase - Items: every playable, named weapon and armour with a model, indexed once at kDataLoaded for the
// Armory's and the wardrobe's pickers (ItemCore's kinds). Read-only after Build, so any thread may search it.

#include "ItemCore.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Showcase::Items
{
    struct Entry
    {
        std::uint32_t  id = 0;
        std::string    name;
        std::string    folded;
        std::string    plugin;
        ItemCore::Kind kind = ItemCore::Kind::kOther;
        bool           enchanted = false;
        std::int32_t   value = 0;
    };

    /// kDataLoaded, game thread.
    void Build();

    const Entry* Get(std::uint32_t id);

    /// A row for the page: { formId, name, plugin, kind, enchanted, value }.
    nlohmann::json Card(const Entry& e);

    /// The entries whose name has every word of `text` and whose kind is in `filter` (ItemCore::InFilter), by name,
    /// `limit` from `offset`. { total, items }.
    nlohmann::json Search(std::string_view text, std::string_view filter, std::size_t offset, std::size_t limit);

    /// The indexed items the actor holds, in `filter`, by name: { total, items } with each card's `count` and `worn`.
    /// Game thread.
    nlohmann::json Held(RE::Actor* actor, std::string_view text, std::string_view filter);
}
