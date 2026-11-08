#pragma once

// Phantasm Showcase - PeopleCore: who the actor pickers list and in what order. Engine-free.

#include <string>
#include <string_view>

namespace Showcase::PeopleCore
{
    /// NPCs farther than this from the player are not listed (followers are, at any distance).
    inline constexpr float kNearbyRadius = 4096.0f;

    enum class Group : int
    {
        kPlayer = 0,
        kFollower = 1,
        kNearby = 2,
    };

    struct Person
    {
        Group       group = Group::kNearby;
        float       distance = 0.0f;
        std::string name;
    };

    constexpr std::string_view Word(Group g)
    {
        return g == Group::kPlayer ? "player" : g == Group::kFollower ? "follower" : "nearby";
    }

    /// Listed: the player, a follower anywhere loaded, anyone else within the radius who is not hostile.
    constexpr bool Listed(Group g, float distance, bool hostile)
    {
        if (g != Group::kNearby) return true;
        return !hostile && distance <= kNearbyRadius;
    }

    /// The player first, then followers by name, then the nearest others.
    inline bool Before(const Person& a, const Person& b)
    {
        if (a.group != b.group) return static_cast<int>(a.group) < static_cast<int>(b.group);
        if (a.group == Group::kNearby && a.distance != b.distance) return a.distance < b.distance;
        return a.name < b.name;
    }
}
