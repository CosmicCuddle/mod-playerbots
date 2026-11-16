/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_SELECTION_TYPES_H
#define PLAYERBOTS_AUTOGEAR_SELECTION_TYPES_H

#include "AutoGearWeaponDomain.h"
#include <cstdint>
#include <span>

// Pure selection facts. Callers copy them from item templates and from the
// existing scorer. Nothing here reads a database, a config file, or a Player.
namespace PlayerbotsAutoGearSelection
{
    enum class Role : std::uint8_t
    {
        Any,
        Tank,
        Healer,
        Melee,
        Caster
    };

    enum class FactDecision : std::uint8_t
    {
        Accept,
        // The fact is known and the item does not fit. It may be omitted.
        Unavailable,
        // Spec, role, capability, level, class, source, or equip result is
        // missing. This must not be treated as Unavailable.
        Unknown,
        Invalid
    };

    struct BotView
    {
        bool levelKnown = false;
        std::uint32_t level = 0;
        bool classKnown = false;
        std::uint8_t classId = 0;
        bool specKnown = false;
        std::uint8_t specTab = 0;
        bool roleKnown = false;
        Role role = Role::Any;
        bool armorKnown = false;
        // Bit (1u << armor subclass). Meaningful only when armorKnown.
        std::uint32_t armorMask = 0;
        bool weaponsKnown = false;
        PlayerbotsAutoGearWeapons::Capabilities weapons{};
    };

    // One item the caller already loaded. itemLevel 0 is missing data, not
    // "no cap". requestForceBis is ignored: a Darkmoon card competes only
    // through the same fields as any other item.
    struct ItemFact
    {
        std::uint32_t id = 0;
        std::uint32_t itemLevel = 0;
        std::uint32_t requiredLevel = 0;
        std::uint8_t slot = 0;
        std::int32_t randomProperty = 0;
        std::uint32_t classMask = 0;
        bool specRestricted = false;
        std::uint8_t specTab = 0;
        Role role = Role::Any;
        PlayerbotsAutoGearWeapons::Kind kind = PlayerbotsAutoGearWeapons::Kind::Count;
        bool exclusiveMainhand = false;
        std::uint8_t armorSubtype = 0;
        bool uniqueEquipped = false;
        // MaxCount is how many of this entry may be possessed.
        std::uint32_t maxCount = 0;
        std::uint32_t limitCategory = 0;
        // EQUIP limit. This is not the possession limit.
        std::uint32_t categoryLimit = 0;
        // HAVE limit. Bags, bank, equipped copies, and gems all count.
        std::uint32_t categoryHaveLimit = 0;
        bool hasGem = false;
        std::uint32_t gemItemLevel = 0;
        bool hasEnchant = false;
        std::uint32_t enchantItemLevel = 0;
        std::uint32_t setId = 0;
        bool setPermitted = true;
        // Set evidence belongs to this fact. A proof row may not disagree.
        bool setEvidenceKnown = false;
        bool setBonusPresent = false;
        // False means the score was not established. It cannot authorize a strip.
        bool scoreKnown = true;
        bool darkmoonCard = false;
        // Tooltip base stats are not availability or a proc. Both stay unknown
        // until installed evidence exists. requestForceBis does not fill them.
        bool darkmoonAvailabilityKnown = false;
        bool darkmoonProcKnown = false;
        bool requestForceBis = false;
        // Template was actually loaded. False is Unknown, not "no such item".
        bool sourceKnown = false;
        bool metadataKnown = false;
        // Result of the bot's real equip check. False equipKnown is Unknown.
        bool equipKnown = false;
        bool equipAllowed = false;
        // CanEquip failed only because of gear that a strip would remove.
        // That is a final-set question, not a static miss.
        bool blockedOnlyByCurrentGear = false;
    };

    // Playable WotLK classes are 1..9 and 11. Class 10 is not in
    // CLASSMASK_ALL_PLAYABLE and must not shift.
    inline constexpr bool ValidWotlkClass(std::uint8_t classId)
    {
        return (classId >= 1 && classId <= 9) || classId == 11;
    }

    inline constexpr std::uint32_t ClassBit(std::uint8_t classId)
    {
        if (!ValidWotlkClass(classId))
            return 0;
        return std::uint32_t{1} << (classId - 1);
    }

    inline constexpr bool ValidSpecTab(std::uint8_t specTab)
    {
        return specTab <= 2 || specTab == 10;
    }

    inline constexpr bool ValidRole(Role role)
    {
        switch (role)
        {
            case Role::Any:
            case Role::Tank:
            case Role::Healer:
            case Role::Melee:
            case Role::Caster:
                return true;
        }
        return false;
    }

    inline constexpr bool ValidWeaponKind(PlayerbotsAutoGearWeapons::Kind kind)
    {
        switch (kind)
        {
            case PlayerbotsAutoGearWeapons::Kind::OneHand:
            case PlayerbotsAutoGearWeapons::Kind::TwoHand:
            case PlayerbotsAutoGearWeapons::Kind::Shield:
            case PlayerbotsAutoGearWeapons::Kind::Holdable:
            case PlayerbotsAutoGearWeapons::Kind::Bow:
            case PlayerbotsAutoGearWeapons::Kind::Gun:
            case PlayerbotsAutoGearWeapons::Kind::Crossbow:
            case PlayerbotsAutoGearWeapons::Kind::Wand:
            case PlayerbotsAutoGearWeapons::Kind::Thrown:
            case PlayerbotsAutoGearWeapons::Kind::Relic:
            case PlayerbotsAutoGearWeapons::Kind::Count:
                return true;
        }
        return false;
    }

    // Counts already possessed outside the set being searched.
    struct RetainedCount
    {
        std::uint32_t entryId = 0;
        std::uint32_t limitCategory = 0;
        bool bagsKnown = false;
        bool bankKnown = false;
        bool equippedKnown = false;
        bool gemsKnown = false;
        std::uint32_t bags = 0;
        std::uint32_t bank = 0;
        std::uint32_t equipped = 0;
        std::uint32_t gems = 0;
    };

    inline constexpr bool CountsKnown(RetainedCount const& row)
    {
        return row.bagsKnown && row.bankKnown && row.equippedKnown && row.gemsKnown;
    }

    inline constexpr bool CountTotal(RetainedCount const& row, std::uint32_t& total)
    {
        total = 0;
        std::uint32_t const parts[] = {row.bags, row.bank, row.equipped, row.gems};
        for (std::uint32_t part : parts)
        {
            if (total > 0xFFFFFFFFu - part)
                return false;
            total += part;
        }
        return true;
    }

    inline RetainedCount const* FindEntryCount(std::span<RetainedCount const> retained, std::uint32_t entryId)
    {
        for (RetainedCount const& row : retained)
        {
            if (row.entryId == entryId && row.limitCategory == 0)
                return &row;
        }
        return nullptr;
    }

    inline RetainedCount const* FindCategoryCount(std::span<RetainedCount const> retained, std::uint32_t category)
    {
        for (RetainedCount const& row : retained)
        {
            if (row.entryId == 0 && row.limitCategory == category)
                return &row;
        }
        return nullptr;
    }
}

#endif
