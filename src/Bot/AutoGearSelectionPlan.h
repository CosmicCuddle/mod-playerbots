/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_SELECTION_PLAN_H
#define PLAYERBOTS_AUTOGEAR_SELECTION_PLAN_H

#include "AutoGearCandidateDomain.h"
#include "AutoGearConstraintFill.h"
#include "AutoGearSelectionCap.h"
#include "AutoGearSelectionOutcome.h"
#include "AutoGearVariantSource.h"
#include <array>
#include <new>
#include <vector>

namespace PlayerbotsAutoGearSelection
{
    struct PlanInput
    {
        CapResult cap{};
        BotView bot{};
        std::span<ItemFact const> facts{};
        std::uint32_t eligibleMask = 0;
        std::uint32_t requiredMask = 0;
        std::uint32_t completeMask = 0;
        std::size_t candidateBudget = 1000;
        std::size_t nodeBudget = 100000;
        bool authorityValid = false;
        // Unknown until a real storage preflight says otherwise.
        BagSpace bags = BagSpace::Unknown;
        // When set, native rows are copied after facts are bounded and validated.
        bool hasNativeSource = false;
        std::span<SourceRow const> nativeRows{};
        std::size_t sourceBudget = 0;
        // Per-slot proof. Unknown is the default. completeMask is not this proof.
        enum class SlotListState : std::uint8_t
        {
            Unknown,
            Incomplete,
            Complete
        };
        std::array<SlotListState, PlayerbotsAutoGearSet::Slots> slotLists{};
        std::span<RetainedCount const> retained{};
        // Fixture hook. Production callers leave this at None.
        enum class AllocationPoint : std::uint8_t
        {
            None,
            Scoring,
            Domain,
            Solve
        };
        AllocationPoint allocationFault = AllocationPoint::None;
    };

    inline bool SameNativeFact(SourceRow const& row, ItemFact const& fact)
    {
        return row.entryId == fact.id && row.randomProperty == fact.randomProperty && row.itemLevel == fact.itemLevel &&
               row.slot == fact.slot;
    }

    // Unknown possession preserves the plan. A known HAVE or MaxCount limit
    // only tells the shared solver how many copies it is allowed to see.
    enum class PossessionGate : std::uint8_t
    {
        Ok,
        Unknown,
        Omit
    };

    inline PossessionGate GatePossession(ItemFact const& fact, std::span<RetainedCount const> retained,
        std::uint32_t& room)
    {
        room = 0xFFFFFFFFu;
        if (fact.maxCount == 0 && fact.categoryHaveLimit == 0)
            return PossessionGate::Ok;
        if (fact.maxCount != 0)
        {
            RetainedCount const* row = FindEntryCount(retained, fact.id);
            std::uint32_t owned = 0;
            if (!row || !CountsKnown(*row) || !CountTotal(*row, owned))
                return PossessionGate::Unknown;
            if (owned >= fact.maxCount)
                return PossessionGate::Omit;
            room = fact.maxCount - owned;
        }
        if (fact.categoryHaveLimit != 0)
        {
            RetainedCount const* row = FindCategoryCount(retained, fact.limitCategory);
            std::uint32_t owned = 0;
            if (!row || !CountsKnown(*row) || !CountTotal(*row, owned))
                return PossessionGate::Unknown;
            if (owned >= fact.categoryHaveLimit)
                return PossessionGate::Omit;
            std::uint32_t const haveRoom = fact.categoryHaveLimit - owned;
            if (haveRoom < room)
                room = haveRoom;
        }
        return PossessionGate::Ok;
    }

    struct Plan
    {
        Outcome outcome{};
        PlayerbotsAutoGearSet::Result solved{};
        std::uint32_t cap = 0;
        std::size_t factsSupplied = 0;
        std::size_t factsAccepted = 0;
        bool sourceComplete = false;
    };

    template <class Scorer>
    Plan SelectFromFacts(PlanInput const& in, Scorer const& scorer)
    {
        using PlayerbotsAutoGearCommand::PreparationResult;
        Plan plan;
        plan.factsSupplied = in.facts.size();
        auto finish = [&](bool authority, PreparationResult prepared, bool searchComplete)
        {
            plan.sourceComplete = false;
            plan.outcome = MapOutcome(authority, prepared, searchComplete, in.bags);
            return plan;
        };
        if (!in.authorityValid || in.cap.preserve || in.cap.cap == 0)
            return finish(false, PreparationResult::InvalidPolicy, false);
        if (in.requiredMask == 0 || (in.requiredMask & ~in.eligibleMask) != 0)
            return finish(true, PreparationResult::InvalidPolicy, false);
        if (!in.bot.levelKnown || !in.bot.classKnown || !in.bot.specKnown || !in.bot.roleKnown)
            return finish(true, PreparationResult::InvalidPolicy, false);
        if ((in.eligibleMask & PlayerbotsAutoGearSet::WeaponMask) != 0 && !in.bot.weaponsKnown)
            return finish(true, PreparationResult::InvalidPolicy, false);
        if ((in.eligibleMask & ~PlayerbotsAutoGearSet::CombatMask) != 0 || (in.completeMask & ~in.eligibleMask) != 0)
            return finish(true, PreparationResult::InvalidPolicy, false);
        // Bound and validate facts before they are matched to a native row.
        if (in.facts.size() > in.candidateBudget)
            return finish(true, PreparationResult::NotPrepared, false);
        for (ItemFact const& fact : in.facts)
        {
            FactDecision const decision = Classify(fact, in.bot, in.cap.cap);
            if (decision == FactDecision::Unknown || decision == FactDecision::Invalid)
                return finish(true, PreparationResult::InvalidPolicy, false);
        }
        std::array<std::uint32_t, PlayerbotsAutoGearSet::Slots> entrySlots{};
        std::array<std::uint32_t, PlayerbotsAutoGearSet::Slots> categorySlots{};
        for (ItemFact const& fact : in.facts)
        {
            if (fact.slot >= PlayerbotsAutoGearSet::Slots)
                return finish(true, PreparationResult::InvalidPolicy, false);
            if (fact.maxCount != 0)
                entrySlots[fact.slot] = fact.id;
            if (fact.categoryHaveLimit != 0)
                categorySlots[fact.slot] = fact.limitCategory;
            std::uint32_t room = 0;
            PossessionGate const gate = GatePossession(fact, in.retained, room);
            if (gate == PossessionGate::Unknown)
                return finish(true, PreparationResult::NotPrepared, false);
            if (gate == PossessionGate::Omit)
                continue;
            std::uint32_t slots = 0;
            if (fact.maxCount != 0 || fact.categoryHaveLimit != 0)
            {
                for (unsigned slot = 0; slot < PlayerbotsAutoGearSet::Slots; ++slot)
                {
                    bool const sameEntry = fact.maxCount != 0 && entrySlots[slot] == fact.id && entrySlots[slot] != 0;
                    bool const sameCategory = fact.categoryHaveLimit != 0 && categorySlots[slot] == fact.limitCategory;
                    if (sameEntry || sameCategory)
                        ++slots;
                }
                if (slots > room)
                    return finish(true, PreparationResult::NotPrepared, false);
            }
        }
        if (in.hasNativeSource)
        {
            try
            {
                CopiedSource const copied = CopyBoundedSource(in.nativeRows, in.sourceBudget);
                if (!copied.sourceComplete || copied.status != CopyStatus::Copied)
                    return finish(true, PreparationResult::NotPrepared, false);
                for (unsigned slot = 0; slot < PlayerbotsAutoGearSet::Slots; ++slot)
                {
                    if ((in.eligibleMask & (std::uint32_t{1} << slot)) == 0)
                        continue;
                    if (in.slotLists[slot] != PlanInput::SlotListState::Complete)
                        return finish(true, PreparationResult::NotPrepared, false);
                }
                for (ItemFact const& fact : in.facts)
                {
                    bool found = false;
                    for (SourceRow const& row : copied.rows)
                    {
                        if (SameNativeFact(row, fact))
                            found = true;
                    }
                    if (!found)
                        return finish(true, PreparationResult::NotPrepared, false);
                }
                for (SourceRow const& row : copied.rows)
                {
                    bool found = false;
                    for (ItemFact const& fact : in.facts)
                    {
                        if (SameNativeFact(row, fact))
                            found = true;
                    }
                    if (!found)
                        return finish(true, PreparationResult::NotPrepared, false);
                }
            }
            catch (...)
            {
                return finish(true, PreparationResult::NotPrepared, false);
            }
        }

        std::vector<AcceptedCandidate> accepted;
        DomainSet domains;
        try
        {
            if (in.allocationFault == PlanInput::AllocationPoint::Scoring)
                throw std::bad_alloc();
            accepted.reserve(in.facts.size());
            for (ItemFact const& fact : in.facts)
            {
                std::uint32_t room = 0;
                if (GatePossession(fact, in.retained, room) == PossessionGate::Omit)
                    continue;
                PreparedFact const prepared = PrepareFact(fact, in.bot, in.cap.cap, scorer);
                if (prepared.decision == FactDecision::Accept)
                    accepted.push_back(AcceptedCandidate{fact.slot, prepared.candidate});
            }
        }
        catch (...)
        {
            return finish(true, PreparationResult::NotPrepared, false);
        }
        plan.factsAccepted = accepted.size();

        try
        {
            if (in.allocationFault == PlanInput::AllocationPoint::Domain)
                throw std::bad_alloc();
            domains = CollectDomains(accepted, in.eligibleMask, in.completeMask, in.candidateBudget);
        }
        catch (...)
        {
            return finish(true, PreparationResult::NotPrepared, false);
        }
        if (domains.status == DomainSet::Status::Bounded)
            return finish(true, PreparationResult::NotPrepared, false);
        if (domains.status != DomainSet::Status::Ok)
            return finish(true, PreparationResult::InvalidPolicy, false);

        std::array<PlayerbotsAutoGearSet::Domain, PlayerbotsAutoGearSet::Slots> views{};
        bool searchComplete = true;
        for (unsigned slot = 0; slot < PlayerbotsAutoGearSet::Slots; ++slot)
        {
            if ((in.eligibleMask & (std::uint32_t{1} << slot)) == 0)
                continue;
            views[slot] = {domains.slots[slot], domains.complete[slot]};
            if (!domains.complete[slot])
                searchComplete = false;
        }

        PlayerbotsAutoGearSet::Policy policy;
        policy.cap = in.cap.cap;
        policy.eligibleMask = in.eligibleMask;
        policy.requiredMask = in.requiredMask & ~PlayerbotsAutoGearSet::WeaponMask;
        policy.weapons = in.bot.weapons;
        if (in.requiredMask & (std::uint32_t{1} << PlayerbotsAutoGearSet::Main))
            policy.weapons.mainhandRequired = true;
        if (in.requiredMask & (std::uint32_t{1} << PlayerbotsAutoGearSet::Off))
            policy.weapons.offhandRequired = true;
        if (in.requiredMask & (std::uint32_t{1} << PlayerbotsAutoGearSet::Ranged))
            policy.weapons.rangedRequired = true;
        policy.known = true;

        plan.cap = in.cap.cap;
        try
        {
            if (in.allocationFault == PlanInput::AllocationPoint::Solve)
                throw std::bad_alloc();
            plan.solved = PlayerbotsAutoGearSet::Solve(views, policy, in.candidateBudget, in.nodeBudget);
        }
        catch (...)
        {
            plan.sourceComplete = false;
            return finish(true, PreparationResult::NotPrepared, false);
        }
        if (plan.solved.status == PlayerbotsAutoGearSet::Status::Incomplete ||
            plan.solved.status == PlayerbotsAutoGearSet::Status::Invalid)
            searchComplete = false;
        bool const finished = plan.solved.status == PlayerbotsAutoGearSet::Status::SuitableSet ||
                              plan.solved.status == PlayerbotsAutoGearSet::Status::NoSet;
        plan.sourceComplete = searchComplete && finished;

        PreparationResult prepared = PreparationResult::InvalidPolicy;
        switch (plan.solved.status)
        {
            case PlayerbotsAutoGearSet::Status::SuitableSet:
                prepared = PreparationResult::SuitableSet;
                break;
            case PlayerbotsAutoGearSet::Status::NoSet:
                prepared = PreparationResult::NoSuitableSet;
                break;
            case PlayerbotsAutoGearSet::Status::Incomplete:
                prepared = PreparationResult::NotPrepared;
                break;
            case PlayerbotsAutoGearSet::Status::Invalid:
                prepared = PreparationResult::InvalidPolicy;
                break;
        }
        plan.outcome = MapOutcome(true, prepared, searchComplete, in.bags);
        return plan;
    }
}

#endif
