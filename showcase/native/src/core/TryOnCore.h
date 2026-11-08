#pragma once

// Phantasm Showcase - TryOnCore: who the wardrobe may dress, when it lends a copy, and how many copies it takes back.
// Engine-free.

#include <algorithm>
#include <cstdint>
#include <map>
#include <string_view>

namespace Showcase::TryOnCore
{
    struct Who
    {
        bool player = false;
        bool child = false;
        bool dead = false;
        bool loaded = false;
    };

    /// "" when the wardrobe may dress them, else the refusal's word. The player is refused: rebuilding the player's 3D
    /// while the game is paused is not safe. A child is never dressed.
    constexpr std::string_view Refusal(const Who& w)
    {
        if (w.player) return "player";
        if (w.child) return "child";
        if (w.dead) return "dead";
        if (!w.loaded) return "notLoaded";
        return "";
    }

    /// A piece the actor holds no copy of at all is lent: one plain copy, recorded. Any copy they hold is theirs to
    /// wear, so nothing is lent then.
    constexpr bool Lends(std::int32_t held) { return held <= 0; }

    /// The lent copies of each item, by FormID, for one try-on session.
    class Ledger
    {
    public:
        void Lent(std::uint32_t item) { ++m_lent[item]; }
        std::int32_t Of(std::uint32_t item) const
        {
            const auto it = m_lent.find(item);
            return it == m_lent.end() ? 0 : it->second;
        }
        const std::map<std::uint32_t, std::int32_t>& All() const { return m_lent; }
        bool Empty() const { return m_lent.empty(); }
        void Clear() { m_lent.clear(); }

    private:
        std::map<std::uint32_t, std::int32_t> m_lent;
    };

    /// How many copies a revert takes back: what was lent, never more plain copies than the actor holds (a copy the
    /// player worked on is not plain, so it is never counted).
    constexpr std::int32_t TakeBack(std::int32_t lent, std::int32_t plainHeld)
    {
        return std::clamp(lent, 0, std::max(0, plainHeld));
    }
}
