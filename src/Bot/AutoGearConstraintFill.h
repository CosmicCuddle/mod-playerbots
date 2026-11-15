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

    constexpr bool WeaponSlot(unsigned slot)
    {
        return slot == PlayerbotsAutoGearSet::Main || slot == PlayerbotsAutoGearSet::Off ||
               slot == PlayerbotsAutoGearSet::Ranged;
    }

    constexpr bool KindAllowed(std::uint32_t mask, PlayerbotsAutoGearWeapons::Kind kind)
    {
        return (mask & PlayerbotsAutoGearWeapons::Bit(kind)) != 0;
    }

    // Class, spec, role, weapon, armor, slot, unique, category, gem, enchant
    // and set permission. requestForceBis does not accept an item. The score
    // copied here is the wrapped existing score, unchanged.
    template <class Scorer>
    PreparedFact PrepareFact(ItemFact const& fact, BotView const& bot, std::uint32_t cap, Scorer const& scorer)
    {
        using PlayerbotsAutoGearWeapons::Kind;
        PreparedFact prepared;
        // Read and discard. A force request must not change this result.
        bool const ignoredForce = fact.requestForceBis;
        (void)ignoredForce;
        (void)fact.darkmoonCard;

        if (!fact.metadataKnown || fact.id == 0 || fact.itemLevel == 0 || fact.slot >= PlayerbotsAutoGearSet::Slots ||
            bot.classId == 0 || bot.level == 0 || cap == 0)
        {
            prepared.decision = FactDecision::Invalid;
            return prepared;
        }
        if ((fact.limitCategory == 0) != (fact.categoryLimit == 0))
        {
            prepared.decision = FactDecision::Invalid;
            return prepared;
        }
        if (fact.exclusiveMainhand && fact.kind != Kind::OneHand && fact.kind != Kind::TwoHand)
        {
            prepared.decision = FactDecision::Invalid;
            return prepared;
        }
        if (WeaponSlot(fact.slot) == (fact.kind == Kind::Count))
        {
            prepared.decision = FactDecision::Invalid;
            return prepared;
        }
        if ((fact.hasGem && fact.gemItemLevel == 0) || (fact.hasEnchant && fact.enchantItemLevel == 0))
        {
            prepared.decision = FactDecision::Invalid;
            return prepared;
        }
        if (fact.armorSubtype > 31)
        {
            prepared.decision = FactDecision::Invalid;
            return prepared;
        }

        auto skip = [&]()
        {
            prepared.decision = FactDecision::Skip;
            return prepared;
        };

        if (fact.itemLevel > cap || fact.requiredLevel > bot.level)
            return skip();
        if (!PlayerbotsAutoGearSlots::SearchAtLevel(bot.level, fact.slot, SelectionSlots))
            return skip();
        if (fact.classMask != 0 && (fact.classMask & ClassBit(bot.classId)) == 0)
            return skip();
        if (fact.specRestricted && (!bot.specKnown || fact.specTab != bot.specTab))
            return skip();
        if (fact.role != Role::Any && fact.role != bot.role)
            return skip();
        if (fact.armorSubtype != 0 && (bot.armorMask & (std::uint32_t{1} << fact.armorSubtype)) == 0)
            return skip();
        if (fact.hasGem && fact.gemItemLevel > cap)
            return skip();
        if (fact.hasEnchant && fact.enchantItemLevel > cap)
            return skip();
        if (fact.setId != 0 && !fact.setPermitted)
            return skip();

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
            return skip();
        if (fact.slot == PlayerbotsAutoGearSet::Off &&
            (fact.exclusiveMainhand || !KindAllowed(bot.weapons.offhand & offKinds, fact.kind)))
            return skip();
        if (fact.slot == PlayerbotsAutoGearSet::Ranged && !KindAllowed(bot.weapons.ranged & rangedKinds, fact.kind))
            return skip();

        prepared.decision = FactDecision::Accept;
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
