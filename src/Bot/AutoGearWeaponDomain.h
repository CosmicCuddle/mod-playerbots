/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_WEAPON_DOMAIN_H
#define PLAYERBOTS_AUTOGEAR_WEAPON_DOMAIN_H

#include <cstdint>

namespace PlayerbotsAutoGearWeapons
{
    enum class Kind : std::uint8_t
    {
        OneHand, TwoHand, Shield, Holdable, Bow, Gun, Crossbow, Wand, Thrown, Relic, Count
    };

    constexpr std::uint32_t Bit(Kind kind)
    {
        return kind < Kind::Count ? std::uint32_t{1} << static_cast<unsigned>(kind) : 0;
    }

    struct Domain
    {
        std::uint32_t allowed = 0;
        bool required = false;

        constexpr bool Accepts(bool occupied, Kind kind) const
        {
            return occupied ? (allowed & Bit(kind)) != 0 : !required;
        }
    };

    struct Capabilities
    {
        // These masks must come from class/skill/item eligibility, not class names
        // guessed by the planner. Core CanUseItem/CanEquipItem remain authoritative.
        std::uint32_t mainhand = 0;
        std::uint32_t offhand = 0;
        std::uint32_t ranged = 0;
        bool dualWield = false;
        bool titanGrip = false;
        bool mainhandRequired = false;
        bool offhandRequired = false;
        bool rangedRequired = false;
    };

    struct Domains
    {
        Domain mainhand;
        Domain offhand;
        Domain ranged;
        bool valid = false;
    };

    constexpr Domains Build(Capabilities const& capabilities, Kind plannedMainhand)
    {
        constexpr auto weapons = Bit(Kind::OneHand) | Bit(Kind::TwoHand);
        constexpr auto offhandItems = weapons | Bit(Kind::Shield) | Bit(Kind::Holdable);
        constexpr auto rangedItems = Bit(Kind::Bow) | Bit(Kind::Gun) | Bit(Kind::Crossbow) |
            Bit(Kind::Wand) | Bit(Kind::Thrown) | Bit(Kind::Relic);
        Domains domains{{capabilities.mainhand & weapons, capabilities.mainhandRequired},
            {capabilities.offhand & offhandItems, capabilities.offhandRequired},
            {capabilities.ranged & rangedItems, capabilities.rangedRequired}, false};

        if (!domains.mainhand.Accepts(plannedMainhand != Kind::Count, plannedMainhand))
            return domains;
        bool const blocked = plannedMainhand == Kind::TwoHand && !capabilities.titanGrip;
        if (blocked)
            domains.offhand = {0, false};
        else
        {
            if (!capabilities.dualWield || plannedMainhand == Kind::Count)
                domains.offhand.allowed &= ~weapons;
            if (!capabilities.titanGrip)
                domains.offhand.allowed &= ~Bit(Kind::TwoHand);
        }
        domains.valid = (!domains.mainhand.required || domains.mainhand.allowed) &&
            (!domains.offhand.required || domains.offhand.allowed) &&
            (!domains.ranged.required || domains.ranged.allowed);
        return domains;
    }
}

#endif
