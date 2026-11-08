#pragma once

// Phantasm Showcase - TryOn: the wardrobe's try-on (docs/DESIGN.md section 5). Opening an NPC records what they wear;
// each piece tried on is equipped at once, lent as one plain copy when they hold none (TryOnCore); Revert puts the
// recorded pieces back on and takes back exactly the copies it lent, never a copy with the player's work. One NPC at a
// time; the menu's close reverts.
//
// Every change runs on the lane (Lane::Run) and rebuilds the NPC's 3D through Phantasm's RebuildActor3D there.

#include <cstdint>
#include <functional>

namespace Showcase::TryOn
{
    /// The answer of a lane job: ok with data, or an error word.
    using Done = std::function<void(bool ok, nlohmann::json data)>;

    /// Starts a session for the NPC (a session for another NPC is reverted first): records what they wear. Answers
    /// { refusal } (TryOnCore::Refusal) or { worn: [cards] }. Game thread; the answer comes from the lane.
    void Begin(std::uint32_t actorId, Done done);

    /// Puts an armour piece on the session's NPC: { lent, worn }.
    void Wear(std::uint32_t actorId, std::uint32_t itemId, Done done);

    /// Puts the recorded pieces back on and takes back the lent copies; ends the session: { taken, worn }.
    void Revert(std::uint32_t actorId, Done done);

    /// Reverts whatever session is open (the menu closed). Any thread.
    void RevertAll();

    /// A game load is starting: the session belongs to the ending one.
    void Forget();
}
