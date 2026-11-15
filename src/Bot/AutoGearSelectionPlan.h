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
        BagSpace bags = BagSpace::Enough;
        RecoveryReason technical = RecoveryReason::None;
    };

    struct Plan
    {
        Outcome outcome{};
        PlayerbotsAutoGearSet::Result solved{};
        std::uint32_t cap = 0;
    };

    template <class Scorer>
    Plan SelectFromFacts(PlanInput const& in, Scorer const& scorer)
    {
        using PlayerbotsAutoGearCommand::PreparationResult;
        Plan plan;
        if (in.technical != RecoveryReason::None)
        {
            plan.outcome = MapOutcome(false, PreparationResult::NotPrepared, false, in.bags, in.technical);
            return plan;
        }
        if (!in.authorityValid || in.cap.preserve || in.cap.cap == 0)
        {
            plan.outcome = MapOutcome(false, PreparationResult::InvalidPolicy, false, in.bags, RecoveryReason::None);
            return plan;
        }

        std::vector<AcceptedCandidate> accepted;
        accepted.reserve(in.facts.size());
        for (ItemFact const& fact : in.facts)
        {
            PreparedFact const prepared = PrepareFact(fact, in.bot, in.cap.cap, scorer);
            if (prepared.decision == FactDecision::Invalid)
            {
                plan.outcome = MapOutcome(true, PreparationResult::InvalidPolicy, false, in.bags, RecoveryReason::None);
                return plan;
            }
            if (prepared.decision == FactDecision::Accept)
                accepted.push_back(AcceptedCandidate{fact.slot, prepared.candidate});
        }

        DomainSet const domains = CollectDomains(accepted, in.eligibleMask, in.completeMask);
        if (domains.status != DomainSet::Status::Ok)
        {
            plan.outcome = MapOutcome(true, PreparationResult::InvalidPolicy, false, in.bags, RecoveryReason::None);
            return plan;
        }

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
        plan.solved = PlayerbotsAutoGearSet::Solve(views, policy, in.candidateBudget, in.nodeBudget);
        if (plan.solved.status == PlayerbotsAutoGearSet::Status::Incomplete)
            searchComplete = false;

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
        plan.outcome = MapOutcome(true, prepared, searchComplete, in.bags, RecoveryReason::None);
        return plan;
    }
}

#endif
