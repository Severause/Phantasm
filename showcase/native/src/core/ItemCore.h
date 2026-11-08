#pragma once

// Phantasm Showcase - ItemCore: the kinds the item pickers sort weapons and armour into, and which copies of an item
// are the player's own work. Engine-free.

#include <array>
#include <cstdint>
#include <string_view>

namespace Showcase::ItemCore
{
    enum class Kind : std::uint8_t
    {
        kWeapon,
        kBow,
        kStaff,
        kHead,
        kBody,
        kHands,
        kFeet,
        kShield,
        kJewelry,
        kOther,
    };

    inline constexpr std::array<std::string_view, 10> kKindWords = { "weapon", "bow",    "staff",   "head",  "body",
                                                                     "hands",  "feet",   "shield",  "jewelry", "other" };

    constexpr std::string_view Word(Kind k) { return kKindWords[static_cast<std::size_t>(k)]; }

    /// Wearable: drawn as worn by the Armory's Male / Female switch and offered by the wardrobe.
    constexpr bool Wearable(Kind k) { return k >= Kind::kHead && k <= Kind::kOther; }

    // Biped slot bits (BGSBipedObjectForm): 30 head, 31 hair, 32 body, 33 hands, 35 amulet, 36 ring, 37 feet, 39
    // shield, 42 circlet.
    inline constexpr std::uint32_t kSlotHead = 1u << 0, kSlotHair = 1u << 1, kSlotBody = 1u << 2,
                                   kSlotHands = 1u << 3, kSlotAmulet = 1u << 5, kSlotRing = 1u << 6,
                                   kSlotFeet = 1u << 7, kSlotShield = 1u << 9, kSlotCirclet = 1u << 12;

    /// An armour's kind by its slots: the body wins (a robe with a hood is body wear), then shield, head, hands, feet,
    /// then jewellery.
    constexpr Kind ArmorKind(std::uint32_t slots)
    {
        if (slots & kSlotBody) return Kind::kBody;
        if (slots & kSlotShield) return Kind::kShield;
        if (slots & (kSlotHead | kSlotHair | kSlotCirclet)) return Kind::kHead;
        if (slots & kSlotHands) return Kind::kHands;
        if (slots & kSlotFeet) return Kind::kFeet;
        if (slots & (kSlotAmulet | kSlotRing)) return Kind::kJewelry;
        return Kind::kOther;
    }

    /// A weapon's kind by the engine's weapon type (0 hand to hand ... 7 bow, 8 staff, 9 crossbow).
    constexpr Kind WeaponKind(std::uint32_t type)
    {
        if (type == 7 || type == 9) return Kind::kBow;
        if (type == 8) return Kind::kStaff;
        return Kind::kWeapon;
    }

    /// What a picker's filter word takes in: "weapons" (weapon, bow, staff), "armor" (every wearable), "outfit" (every
    /// wearable but a shield: the wardrobe's), a kind word, or anything else for everything.
    inline bool InFilter(Kind k, std::string_view filter)
    {
        if (filter == "weapons") return !Wearable(k);
        if (filter == "armor") return Wearable(k);
        if (filter == "outfit") return Wearable(k) && k != Kind::kShield;
        for (std::size_t i = 0; i < kKindWords.size(); ++i) {
            if (filter == kKindWords[i]) return static_cast<std::size_t>(k) == i;
        }
        return true;
    }

    /// One stack of an item as the wardrobe sees it.
    struct Stack
    {
        bool named = false;      // a name the player (or a quest) gave it
        bool soul = false;
        bool tempered = false;
        bool enchanted = false;  // an enchantment of its own, not the base form's
    };

    /// A name of the player's (or a quest's or message's), unless it is only the engine's cache: the game writes
    /// "Glass Sword (Legendary)" onto a tempered stack the first time anything asks for its name, with no owner.
    constexpr bool IsName(bool hasText, bool playerSet, bool questOrMessage, bool tempered)
    {
        return hasText && (playerSet || questOrMessage || !tempered);
    }

    /// A plain copy: none of the player's work, so the wardrobe may take it back as the lent copy it gave.
    constexpr bool Plain(const Stack& s) { return !s.named && !s.soul && !s.tempered && !s.enchanted; }
}
