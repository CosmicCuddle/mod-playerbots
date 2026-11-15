/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_PROGRESSION_CAP_H
#define PLAYERBOTS_AUTOGEAR_PROGRESSION_CAP_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace PlayerbotsAutoGearCap
{
    constexpr std::uint8_t TbcEntryStage = 8;
    constexpr std::uint8_t WrathEntryStage = 13;
    constexpr std::uint8_t LastSupportedStage = 18;

    struct Policy
    {
        std::span<std::uint32_t const> completionCaps;
        std::array<std::uint32_t, 3> eraEntryFloors;
        std::array<std::uint32_t, 3> eraCeilings;
    };

    struct Decision
    {
        bool valid;
        std::uint32_t itemLevelCap;
    };

    constexpr Decision Resolve(std::uint8_t rawStage, std::uint8_t effectiveStage, Policy const& policy,
        std::uint32_t configuredCap, std::uint32_t requestedCap = 0)
    {
        std::size_t const era = effectiveStage < TbcEntryStage ? 0 : effectiveStage < WrathEntryStage ? 1 : 2;
        if (rawStage > LastSupportedStage || effectiveStage > LastSupportedStage ||
            rawStage >= policy.completionCaps.size() || !policy.completionCaps[rawStage] ||
            !policy.eraCeilings[era] || policy.eraEntryFloors[era] > policy.eraCeilings[era])
            return {false, 0};

        std::uint32_t cap = std::min(std::max(policy.completionCaps[rawStage], policy.eraEntryFloors[era]),
            policy.eraCeilings[era]);
        if (configuredCap)
            cap = std::min(cap, configuredCap);
        if (requestedCap)
            cap = std::min(cap, requestedCap);
        return {true, cap};
    }

    constexpr bool AllowsRawItemLevel(std::uint32_t itemLevel, std::uint32_t hardCap)
    {
        return !hardCap || itemLevel <= hardCap;
    }
}

#endif
