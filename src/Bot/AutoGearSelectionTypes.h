/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_SELECTION_TYPES_H
#define PLAYERBOTS_AUTOGEAR_SELECTION_TYPES_H

#include "AutoGearWeaponDomain.h"
#include <cstdint>

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
        std::uint32_t limitCategory = 0;
        std::uint32_t categoryLimit = 0;
        bool hasGem = false;
        std::uint32_t gemItemLevel = 0;
        bool hasEnchant = false;
        std::uint32_t enchantItemLevel = 0;
        std::uint32_t setId = 0;
        bool setPermitted = true;
        bool darkmoonCard = false;
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

    inline constexpr std::uint32_t ClassBit(std::uint8_t classId)
    {
        return classId >= 1 && classId <= 31 ? std::uint32_t{1} << (classId - 1) : 0;
    }
}

#endif
