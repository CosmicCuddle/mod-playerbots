/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS
 * for license details.
 */
#pragma once

#include "AutoGearSetSolver.h"

namespace PlayerbotsAutoGearFactory
{
    // PlayerbotFactory::PlanEquipmentSet has no retained equipped-item/gem
    // snapshot, source provenance or approved-original projection. Its plain
    // solver only counts items chosen in this new set, not equipped items or
    // socketed gems kept outside the selected slots.
    //
    // Do not certify an EQUIP item-limit category from this insufficient
    // evidence. This only gates the factory's optional planning helper; the
    // existing InitEquipment routine and verified AutoGear collector are
    // separate code paths, and neither is changed by this function.
    inline PlayerbotsAutoGearSet::Result PlanWithUnprovenEquipLimits(
        std::array<PlayerbotsAutoGearSet::Domain, PlayerbotsAutoGearSet::Slots> const& domains,
        PlayerbotsAutoGearSet::Policy const& policy,
        std::size_t candidateBudget, std::size_t nodeBudget)
    {
        std::size_t scanned = 0;
        for (unsigned slot = 0; slot < PlayerbotsAutoGearSet::Slots; ++slot)
        {
            if ((policy.eligibleMask & (std::uint32_t{1} << slot)) == 0)
                continue;
            for (PlayerbotsAutoGearSet::Candidate const& candidate : domains[slot].candidates)
            {
                if (scanned == candidateBudget)
                    return {PlayerbotsAutoGearSet::Status::Incomplete};
                ++scanned;
                // A non-zero category ID, even with malformed/unknown limit,
                // must not be quietly treated as an unlimited item.
                if (candidate.limitCategory != 0 || candidate.categoryLimit != 0)
                    return {PlayerbotsAutoGearSet::Status::Incomplete};
            }
        }
        return PlayerbotsAutoGearSet::Solve(domains, policy, candidateBudget, nodeBudget);
    }
}
