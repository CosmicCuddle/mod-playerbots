/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_CONSTRAINT_FILL_H
#define PLAYERBOTS_AUTOGEAR_CONSTRAINT_FILL_H

#include "AutoGearScoreFill.h"
#include "AutoGearSelectionTypes.h"
#include "AutoGearSetSolver.h"
#include "AutoGearSlotPolicy.h"

namespace PlayerbotsAutoGearSelection
{
    // Same slot numbers InitEquipment already searches. Body is 3, tabard is 18.
    inline constexpr PlayerbotsAutoGearSlots::Layout SelectionSlots{
        0, 1, 3, 4, 6, 7, 10, 11, 12, 13, 15, 16, 17, 18};

    struct PreparedFact
    {
        FactDecision decision = FactDecision::Invalid;
        PlayerbotsAutoGearSet::Candidate candidate{};
    };

    // True when an eligible slot is outside the search schedule. The caller
    // must preserve. It must not treat that slot as a proven-empty domain.
    inline bool UnsearchedEligibleSlot(BotView const& bot, std::uint32_t eligibleMask)
    {
        if (!bot.levelKnown)
            return true;
        for (unsigned slot = 0; slot < PlayerbotsAutoGearSet::Slots; ++slot)
        {
            if ((eligibleMask & (std::uint32_t{1} << slot)) == 0)
                continue;
            if (!PlayerbotsAutoGearSlots::SearchAtLevel(bot.level, slot, SelectionSlots))
                return true;
        }
        return false;
    }

    constexpr bool WeaponSlot(unsigned slot)
    {
        return slot == PlayerbotsAutoGearSet::Main || slot == PlayerbotsAutoGearSet::Off ||
               slot == PlayerbotsAutoGearSet::Ranged;
    }

    constexpr bool KindAllowed(std::uint32_t mask, PlayerbotsAutoGearWeapons::Kind kind)
    {
        return (mask & PlayerbotsAutoGearWeapons::Bit(kind)) != 0;
    }

    // Does not score and does not allocate. Unknown is not Unavailable.
    // requestForceBis does not accept an item. The slot is rejected before
    // any shift.
    inline FactDecision Classify(ItemFact const& fact, BotView const& bot, std::uint32_t cap)
    {
        using PlayerbotsAutoGearWeapons::Kind;
        (void)fact.requestForceBis;
        (void)fact.darkmoonCard;

        if (fact.slot >= PlayerbotsAutoGearSet::Slots || cap == 0)
            return FactDecision::Invalid;
        if (!ValidWeaponKind(fact.kind))
            return FactDecision::Invalid;
        if (!fact.sourceKnown || !fact.metadataKnown || fact.id == 0 || fact.itemLevel == 0)
            return FactDecision::Unknown;
        if (!bot.levelKnown || bot.level == 0 || !bot.classKnown || !bot.specKnown || !bot.roleKnown)
            return FactDecision::Unknown;
        if (!ValidWotlkClass(bot.classId) || !ValidSpecTab(bot.specTab) || !ValidRole(bot.role))
            return FactDecision::Invalid;
        if (fact.specRestricted && !ValidSpecTab(fact.specTab))
            return FactDecision::Invalid;
        if (!ValidRole(fact.role))
            return FactDecision::Invalid;
        if (!fact.equipKnown)
            return FactDecision::Unknown;
        bool const categoryLimited = fact.categoryLimit != 0 || fact.categoryHaveLimit != 0;
        if ((fact.limitCategory == 0) == categoryLimited || fact.armorSubtype > 31)
            return FactDecision::Invalid;
        if (fact.darkmoonCard && (!fact.darkmoonAvailabilityKnown || !fact.darkmoonProcKnown))
            return FactDecision::Unknown;
        if (fact.exclusiveMainhand && fact.kind != Kind::OneHand && fact.kind != Kind::TwoHand)
            return FactDecision::Invalid;
        if (WeaponSlot(fact.slot) == (fact.kind == Kind::Count))
            return FactDecision::Invalid;
        if ((fact.hasGem && fact.gemItemLevel == 0) || (fact.hasEnchant && fact.enchantItemLevel == 0))
            return FactDecision::Invalid;

        if (!fact.equipAllowed && !fact.blockedOnlyByCurrentGear)
            return FactDecision::Unavailable;
        if (fact.itemLevel > cap || fact.requiredLevel > bot.level)
            return FactDecision::Unavailable;
        // Not searched is missing proof, not a Core exclusion.
        if (!PlayerbotsAutoGearSlots::SearchAtLevel(bot.level, fact.slot, SelectionSlots))
            return FactDecision::Unknown;
        if (fact.classMask != 0 && (fact.classMask & ClassBit(bot.classId)) == 0)
            return FactDecision::Unavailable;
        if (fact.specRestricted && fact.specTab != bot.specTab)
            return FactDecision::Unavailable;
        if (fact.role != Role::Any && fact.role != bot.role)
            return FactDecision::Unavailable;
        if (fact.armorSubtype != 0)
        {
            if (!bot.armorKnown)
                return FactDecision::Unknown;
            if ((bot.armorMask & (std::uint32_t{1} << fact.armorSubtype)) == 0)
                return FactDecision::Unavailable;
        }
        if (fact.hasGem && fact.gemItemLevel > cap)
            return FactDecision::Unavailable;
        if (fact.hasEnchant && fact.enchantItemLevel > cap)
            return FactDecision::Unavailable;
        if (fact.setId != 0 && !fact.setPermitted)
            return FactDecision::Unavailable;
        if (WeaponSlot(fact.slot) && !bot.weaponsKnown)
            return FactDecision::Unknown;

        constexpr auto oneHand = PlayerbotsAutoGearWeapons::Bit(Kind::OneHand);
        constexpr auto twoHand = PlayerbotsAutoGearWeapons::Bit(Kind::TwoHand);
        constexpr auto mainKinds = oneHand | twoHand;
        constexpr auto offKinds = mainKinds | PlayerbotsAutoGearWeapons::Bit(Kind::Shield) |
                                  PlayerbotsAutoGearWeapons::Bit(Kind::Holdable);
        constexpr auto bow = PlayerbotsAutoGearWeapons::Bit(Kind::Bow);
        constexpr auto gun = PlayerbotsAutoGearWeapons::Bit(Kind::Gun);
        constexpr auto crossbow = PlayerbotsAutoGearWeapons::Bit(Kind::Crossbow);
        constexpr auto wand = PlayerbotsAutoGearWeapons::Bit(Kind::Wand);
        constexpr auto thrown = PlayerbotsAutoGearWeapons::Bit(Kind::Thrown);
        constexpr auto relic = PlayerbotsAutoGearWeapons::Bit(Kind::Relic);
        constexpr auto rangedKinds = bow | gun | crossbow | wand | thrown | relic;
        if (fact.slot == PlayerbotsAutoGearSet::Main && !KindAllowed(bot.weapons.mainhand & mainKinds, fact.kind))
            return FactDecision::Unavailable;
        if (fact.slot == PlayerbotsAutoGearSet::Off &&
            (fact.exclusiveMainhand || !KindAllowed(bot.weapons.offhand & offKinds, fact.kind)))
            return FactDecision::Unavailable;
        if (fact.slot == PlayerbotsAutoGearSet::Ranged && !KindAllowed(bot.weapons.ranged & rangedKinds, fact.kind))
            return FactDecision::Unavailable;
        return FactDecision::Accept;
    }

    // Scores only an accepted fact. Classify has already rejected unknowns.
    template <class Scorer>
    PreparedFact PrepareFact(ItemFact const& fact, BotView const& bot, std::uint32_t cap, Scorer const& scorer)
    {
        PreparedFact prepared;
        prepared.decision = Classify(fact, bot, cap);
        if (prepared.decision != FactDecision::Accept)
            return prepared;
        prepared.candidate.id = fact.id;
        prepared.candidate.itemLevel = fact.itemLevel;
        prepared.candidate.score = ExistingScore(fact.id, fact.randomProperty, fact.slot, scorer);
        prepared.candidate.limitCategory = fact.limitCategory;
        prepared.candidate.categoryLimit = fact.categoryLimit;
        prepared.candidate.kind = fact.kind;
        prepared.candidate.uniqueEquipped = fact.uniqueEquipped;
        prepared.candidate.metadataKnown = true;
        prepared.candidate.eligible = true;
        prepared.candidate.exclusiveMainhand = fact.exclusiveMainhand;
        return prepared;
    }
}

#endif
