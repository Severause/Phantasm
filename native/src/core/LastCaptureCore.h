#pragma once

// Phantasm - LastCaptureCore: the actor view keeps each NPC's last complete look for the session and
// shows it when the NPC cannot be captured live (MannequinRenderer.cpp), engine-free (native/build.ps1 -Tests).
//
// Author: Severause

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Phantasm::LastCaptureCore
{
    /// How many looks are kept: each pins its pieces' buffers and textures.
    inline constexpr std::size_t kCapacity = 6;

    /// A kept look is the NPC's as one pose and weapons choice drew them: views of the same NPC drawn otherwise keep
    /// looks of their own, so a view's hint change never drops another view's look.
    struct LookKey
    {
        std::uint32_t fid      = 0;
        bool          captured = false;   // the captured pose, else the classic doll
        bool          weapons  = false;   // drawn with what they carry

        friend bool operator==(const LookKey&, const LookKey&) = default;
    };

    struct LookKeyHash
    {
        std::size_t operator()(const LookKey& k) const noexcept
        {
            return std::hash<std::uint64_t>{}(static_cast<std::uint64_t>(k.fid) | (std::uint64_t{ k.captured } << 32) |
                                              (std::uint64_t{ k.weapons } << 33));
        }
    };
    /// And how much memory they may pin together: textures and buffers, each counted once however many looks share it.
    inline constexpr std::uint64_t kBudgetBytes = 512ull << 20;
    /// How often a view showing a kept look checks whether the NPC can be captured live again, and how long it
    /// waits after a live capture of them failed.
    inline constexpr std::chrono::milliseconds kUpgradeCheck{ 1000 };
    inline constexpr std::chrono::milliseconds kUpgradeBackoff{ 5000 };

    /// Whether a collect is kept as the NPC's look: a complete one (no texture still owed, no piece skipped for
    /// buffers the engine had not uploaded yet), and not a try-on (the keepLook hint off), whose clothes are not how
    /// the NPC looks.
    inline bool KeepsCollect(bool texturesPending, bool piecesSkipped, bool tryOnOpen) noexcept
    {
        return !texturesPending && !piecesSkipped && !tryOnOpen;
    }

    /// Whether a view whose NPC left keeps what it last drew as their look: never a try-on, and only a complete
    /// collect (KeepsCollect's first two terms, recorded when it was made) unless no look of them is kept yet, so an
    /// incomplete one never replaces a complete one.
    inline bool KeepsAtLeave(bool collectComplete, bool haveKept, bool tryOnOpen) noexcept
    {
        return !tryOnOpen && (collectComplete || !haveKept);
    }

    /// Whether a view shows a kept look in place of a live capture: there is one, and the NPC still exists and
    /// lives (a dead NPC's old look would mislead).
    inline bool ServesKept(bool haveKept, bool actorExists, bool dead) noexcept
    {
        return haveKept && actorExists && !dead;
    }

    /// Bytes of a 2D texture: each of its mip levels at max(1, w >> i) x max(1, h >> i), rounded up to 4x4 blocks
    /// when blockCompressed, at bitsPerPixel, times arraySize (6 for a cube).
    inline std::uint64_t TextureBytes(std::uint32_t w, std::uint32_t h, std::uint32_t mips, std::uint32_t arraySize,
                                      std::uint32_t bitsPerPixel, bool blockCompressed) noexcept
    {
        std::uint64_t total = 0;
        const std::uint32_t levels = (std::max)(mips, 1u);
        for (std::uint32_t i = 0; i < levels; ++i) {
            std::uint64_t lw = (std::max)(w >> i, 1u), lh = (std::max)(h >> i, 1u);
            if (blockCompressed) {
                lw = (lw + 3) / 4 * 4;
                lh = (lh + 3) / 4 * 4;
            }
            total += lw * lh * bitsPerPixel / 8;
        }
        return total * (std::max)(arraySize, 1u);
    }

    /// The resources a kept look pins, each by an id and its bytes.
    template <class Id>
    using Resources = std::vector<std::pair<Id, std::uint64_t>>;

    /// How many of the looks (newest first) are kept: the most recent ones whose resources together, each counted
    /// once, stay within budget, and never fewer than one, so the newest is kept whatever it costs.
    template <class Id>
    std::size_t KeepWithin(const std::vector<Resources<Id>>& newestFirst, std::uint64_t budget = kBudgetBytes)
    {
        std::unordered_set<Id> seen;
        std::uint64_t total = 0;
        for (std::size_t n = 0; n < newestFirst.size(); ++n) {
            std::uint64_t added = 0;
            std::vector<Id> fresh;
            for (const auto& [id, bytes] : newestFirst[n]) {
                if (seen.count(id) || std::find(fresh.begin(), fresh.end(), id) != fresh.end()) continue;
                fresh.push_back(id);
                added += bytes;
            }
            if (n > 0 && total + added > budget) return n;
            total += added;
            seen.insert(fresh.begin(), fresh.end());
        }
        return newestFirst.size();
    }

    /// The bytes those looks pin together, each resource counted once.
    template <class Id>
    std::uint64_t TotalBytes(const std::vector<Resources<Id>>& looks)
    {
        std::unordered_set<Id> seen;
        std::uint64_t total = 0;
        for (const auto& look : looks) {
            for (const auto& [id, bytes] : look) {
                if (seen.insert(id).second) total += bytes;
            }
        }
        return total;
    }

    /// The kept NPCs, most recently kept first.
    template <class Key>
    class Recency
    {
    public:
        /// Puts key first; returns the keys pushed past `capacity`, which the caller drops.
        std::vector<Key> Touch(const Key& key, std::size_t capacity = kCapacity)
        {
            Erase(key);
            m_order.insert(m_order.begin(), key);
            std::vector<Key> dropped;
            while (m_order.size() > capacity) {
                dropped.push_back(m_order.back());
                m_order.pop_back();
            }
            return dropped;
        }

        void Erase(const Key& key)
        {
            m_order.erase(std::remove(m_order.begin(), m_order.end(), key), m_order.end());
        }

        void Clear() { m_order.clear(); }

        const std::vector<Key>& Order() const { return m_order; }

    private:
        std::vector<Key> m_order;
    };
}
