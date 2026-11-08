#pragma once

// Phantasm - LiveTextureCacheCore: the live views' path-texture cache rule, engine-free (native/build.ps1
// -Tests). LoadTextureSRVByPath (MannequinRenderer.cpp) keeps each texture it loads by path in a WorkingSetCache.
//
// The cache keeps the WORKING SET of the latest collect, not every texture ever loaded: each lookup stamps its entry
// with the current collect, and after a complete collect commits, PruneToCurrent drops what that collect did not use.
// Nothing drawn relies on the cache to stay alive (the lifetime contract is s_texPathCache's); what the cache adds is
// warmth, so a recollect of the same NPC or item finds its textures without loading them again.
//
// Author: Severause

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>

namespace Phantasm::LiveTextureCacheCore
{
    template <class Value>
    class WorkingSetCache
    {
    public:
        /// Starts a collect: lookups and inserts from here on belong to it.
        void BeginCollect() noexcept { ++m_gen; }

        /// The value cached for key, stamped as used by the current collect; null on a miss.
        Value* Find(const std::string& key)
        {
            const auto it = m_map.find(key);
            if (it == m_map.end()) return nullptr;
            it->second.gen = m_gen;
            return &it->second.value;
        }

        /// Caches value for key as used by the current collect, replacing any earlier value.
        void Insert(std::string key, Value value) { m_map.insert_or_assign(std::move(key), Entry{ std::move(value), m_gen }); }

        /// Drops every entry the current collect neither found nor inserted; returns how many. Call it only after a
        /// collect that ran to the end: one cut short skipped lookups, and its prune would drop textures still in use.
        std::size_t PruneToCurrent()
        {
            std::size_t dropped = 0;
            for (auto it = m_map.begin(); it != m_map.end();) {
                if (it->second.gen != m_gen) {
                    it = m_map.erase(it);
                    ++dropped;
                } else {
                    ++it;
                }
            }
            return dropped;
        }

        /// Drops everything; returns how many entries there were.
        std::size_t Clear() noexcept
        {
            const std::size_t n = m_map.size();
            m_map.clear();
            return n;
        }

        std::size_t Size() const noexcept { return m_map.size(); }

    private:
        struct Entry
        {
            Value         value;
            std::uint64_t gen = 0;
        };
        std::unordered_map<std::string, Entry> m_map;
        std::uint64_t                          m_gen = 0;
    };
}
