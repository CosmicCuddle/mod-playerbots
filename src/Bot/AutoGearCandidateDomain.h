/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_CANDIDATE_DOMAIN_H
#define PLAYERBOTS_AUTOGEAR_CANDIDATE_DOMAIN_H

#include "AutoGearSetSolver.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace PlayerbotsAutoGearSelection
{
    struct AcceptedCandidate
    {
        std::uint8_t slot = 0;
        PlayerbotsAutoGearSet::Candidate candidate{};
    };

    struct DomainSet
    {
        enum class Status : std::uint8_t
        {
            Ok,
            Invalid,
            // More eligible items than the caller bounded. Not a partial set.
            Bounded
        };

        Status status = Status::Invalid;
        std::array<std::vector<PlayerbotsAutoGearSet::Candidate>, PlayerbotsAutoGearSet::Slots> slots{};
        std::array<bool, PlayerbotsAutoGearSet::Slots> complete{};
    };

    // Groups an already copied candidate list. No item lookup. The slot is
    // range-checked before any shift. A complete bit outside the eligible
    // combat mask is invalid. Overflow returns Bounded and keeps every slot
    // empty, so a short list is never reported as the whole universe.
    inline DomainSet CollectDomains(std::span<AcceptedCandidate const> accepted, std::uint32_t eligibleMask,
        std::uint32_t completeMask, std::size_t bound)
    {
        DomainSet domains;
        if ((eligibleMask & ~PlayerbotsAutoGearSet::CombatMask) != 0)
            return domains;
        if ((completeMask & ~eligibleMask) != 0)
            return domains;
        if (accepted.size() > bound)
        {
            domains.status = DomainSet::Status::Bounded;
            return domains;
        }
        for (auto const& acceptedFact : accepted)
        {
            if (acceptedFact.slot >= PlayerbotsAutoGearSet::Slots)
                return domains;
            std::uint32_t const bit = std::uint32_t{1} << acceptedFact.slot;
            bool const inMask = (eligibleMask & bit) != 0;
            bool const known = acceptedFact.candidate.metadataKnown && acceptedFact.candidate.eligible &&
                               acceptedFact.candidate.id != 0 && acceptedFact.candidate.itemLevel != 0;
            if (!inMask || !known)
                return domains;
            domains.slots[acceptedFact.slot].push_back(acceptedFact.candidate);
        }
        for (unsigned slot = 0; slot < PlayerbotsAutoGearSet::Slots; ++slot)
        {
            auto& list = domains.slots[slot];
            std::sort(list.begin(), list.end(), [](PlayerbotsAutoGearSet::Candidate const& a,
                                                    PlayerbotsAutoGearSet::Candidate const& b) { return a.id < b.id; });
            for (std::size_t i = 1; i < list.size(); ++i)
            {
                if (list[i].id <= list[i - 1].id)
                    return DomainSet{};
            }
            if ((eligibleMask & (std::uint32_t{1} << slot)) != 0)
                domains.complete[slot] = (completeMask & (std::uint32_t{1} << slot)) != 0;
        }
        domains.status = DomainSet::Status::Ok;
        return domains;
    }
}

#endif
