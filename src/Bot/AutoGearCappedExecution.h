/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_CAPPED_EXECUTION_H
#define PLAYERBOTS_AUTOGEAR_CAPPED_EXECUTION_H

#include "AutoGearCommandEligibility.h"
#include "AutoGearRndbotTierSync.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

// Executes a verified, progression-capped AutoGear command. The orchestration is a template over an
// inventory adapter so the live Player adapter and the test fakes run the same steps.
namespace PlayerbotsAutoGearExec
{
    using PlayerbotsAutoGearSelection::CommandForm;
    using PlayerbotsAutoGearSelection::Population;

    constexpr std::uint8_t EquipmentSlots = 19;
    constexpr std::uint8_t SlotBody = 3;
    constexpr std::uint8_t SlotTabard = 18;

    enum class Refusal : std::uint8_t
    {
        None,
        Rejected,           // disabled form, alt bot refused, bad argument
        NotVerified,        // no verified sender, progression stage or cap
        UnknownPopulation,  // not a proven random bot or alt bot
        ResetNotAllowed,    // reset only strips random bots
        BagsFull,           // not enough free bag slots for the planned strip; nothing changed
        StripFailed         // a move to bags failed part way; moved items stay in bags, nothing else ran
    };

    struct PlanInput
    {
        bool routeRejected = false;
        bool capVerified = false;
        std::uint32_t cap = 0;
        CommandForm form = CommandForm::Plain;
        bool reset = false;
        std::uint32_t quality = 0;
        bool populationKnown = false;
        Population population = Population::Unknown;
    };

    struct Plan
    {
        Refusal refusal = Refusal::NotVerified;
        bool strip = false;          // reset: every combat item goes to bags first
        bool stripOverCap = false;   // random bots: equipped items above the cap go to bags first
        bool incremental = true;
        bool bisOverlay = false;
        std::uint32_t quality = 0;
        std::uint32_t cap = 0;

        constexpr bool Runs() const { return refusal == Refusal::None; }
    };

    constexpr bool IsBisForm(CommandForm form)
    {
        return form == CommandForm::Bis || form == CommandForm::BisNumeric;
    }

    // A zero cap is never "unlimited" here. Nothing in this path deletes a bot's own gear: strips move
    // equipped items into empty bag slots (random bots only) and replacements go through the bags.
    constexpr Plan PlanExecution(PlanInput const& in)
    {
        Plan plan;
        if (in.routeRejected)
        {
            plan.refusal = Refusal::Rejected;
            return plan;
        }
        if (!in.capVerified || in.cap == 0)
            return plan;
        if (!in.populationKnown ||
            (in.population != Population::RandomBot && in.population != Population::AltBot))
        {
            plan.refusal = Refusal::UnknownPopulation;
            return plan;
        }
        if (in.reset && in.population != Population::RandomBot)
        {
            plan.refusal = Refusal::ResetNotAllowed;
            return plan;
        }
        plan.refusal = Refusal::None;
        plan.strip = in.reset;
        plan.stripOverCap = in.population == Population::RandomBot;
        plan.incremental = !in.reset;
        plan.bisOverlay = IsBisForm(in.form);
        plan.quality = in.quality;
        plan.cap = in.cap;
        return plan;
    }

    struct ItemFact
    {
        std::uint64_t guid = 0;
        std::uint32_t entry = 0;
        std::uint32_t itemLevel = 0;
    };

    struct Snapshot
    {
        std::array<ItemFact, EquipmentSlots> equipped{};
        std::vector<std::uint64_t> carried;  // every item guid in equipment, backpack and bags
        std::uint32_t freeBagSlots = 0;
    };

    struct Outcome
    {
        Refusal refusal = Refusal::NotVerified;
        bool ran = false;
        std::uint32_t stripped = 0;
        std::uint32_t retainedOverCap = 0;  // alt bot gear above the cap, left as it was
        std::uint32_t newItems = 0;
        std::uint32_t revoked = 0;
        bool capHeld = false;
    };

    constexpr bool IsCombatSlot(std::uint8_t slot)
    {
        return slot < EquipmentSlots && slot != SlotBody && slot != SlotTabard;
    }

    inline bool Carried(Snapshot const& snapshot, std::uint64_t guid)
    {
        return std::find(snapshot.carried.begin(), snapshot.carried.end(), guid) != snapshot.carried.end();
    }

    // Inventory adapter contract:
    //   Snapshot Capture();
    //   bool MoveEquippedToBags(uint8 slot);              true only if the slot is now empty
    //   void RunFactory(uint32 quality, uint32 cap, bool incremental);
    //   void ApplyBisOverlay(uint32 cap);
    //   void ApplyFinishers(uint32 cap);
    //   bool DestroyCreatedEquipped(uint8 slot, uint64 guid);
    template <class Inventory>
    Outcome Execute(Inventory& inventory, Plan const& plan)
    {
        Outcome outcome;
        outcome.refusal = plan.refusal;
        if (!plan.Runs())
            return outcome;

        Snapshot const before = inventory.Capture();
        std::array<bool, EquipmentSlots> moveOut{};
        std::uint32_t planned = 0;
        for (std::uint8_t slot = 0; slot < EquipmentSlots; ++slot)
        {
            ItemFact const& item = before.equipped[slot];
            if (!IsCombatSlot(slot) || !item.guid)
                continue;
            bool const overCap = item.itemLevel > plan.cap;
            moveOut[slot] = plan.strip || (plan.stripOverCap && overCap);
            planned += moveOut[slot] ? 1 : 0;
            if (overCap && !moveOut[slot])
                ++outcome.retainedOverCap;
        }
        if (before.freeBagSlots < planned)
        {
            outcome.refusal = Refusal::BagsFull;
            return outcome;
        }
        for (std::uint8_t slot = 0; slot < EquipmentSlots; ++slot)
        {
            if (!moveOut[slot])
                continue;
            if (!inventory.MoveEquippedToBags(slot))
            {
                outcome.refusal = Refusal::StripFailed;
                return outcome;
            }
            ++outcome.stripped;
        }

        inventory.RunFactory(plan.quality, plan.cap, plan.incremental);
        if (plan.bisOverlay)
            inventory.ApplyBisOverlay(plan.cap);
        inventory.ApplyFinishers(plan.cap);

        // Only items this command created are checked; gear the bot already owned is left alone.
        Snapshot const after = inventory.Capture();
        outcome.capHeld = true;
        for (std::uint8_t slot = 0; slot < EquipmentSlots; ++slot)
        {
            ItemFact const& item = after.equipped[slot];
            if (!item.guid || Carried(before, item.guid))
                continue;
            ++outcome.newItems;
            if (item.itemLevel <= plan.cap)
                continue;
            if (inventory.DestroyCreatedEquipped(slot, item.guid))
                ++outcome.revoked;
            else
                outcome.capHeld = false;
        }
        outcome.refusal = Refusal::None;
        outcome.ran = true;
        return outcome;
    }

    // Counted slots for a commander still leveling: what the gear schedule searches at that level.
    // Empty counted slots count as zero without shrinking the denominator, so a sparse sample can
    // only lower the cap.
    template <class SearchAtLevel>
    std::uint32_t FixedSenderAverage(std::array<std::uint32_t, EquipmentSlots> const& itemLevels,
        std::uint32_t senderLevel, SearchAtLevel const& searched)
    {
        std::uint64_t sum = 0;
        std::uint32_t counted = 0;
        for (std::uint8_t slot = 0; slot < EquipmentSlots; ++slot)
        {
            if (!IsCombatSlot(slot) || !searched(senderLevel, slot))
                continue;
            ++counted;
            sum += itemLevels[slot];
        }
        return counted ? static_cast<std::uint32_t>(sum / counted) : 0;
    }
}

#endif
