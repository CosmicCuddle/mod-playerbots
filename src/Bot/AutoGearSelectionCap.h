/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_SELECTION_CAP_H
#define PLAYERBOTS_AUTOGEAR_SELECTION_CAP_H

#include "AutoGearProgressionCap.h"
#include <cstdint>

namespace PlayerbotsAutoGearSelection
{
    // Owner-approved 16 Nov 2026. Stages 0 and 1 were already 66 and 78.
    // Stages 2 to 7 are 78, 83, 83, 83, 88, 92.
    // Owner-approved 18 Nov 2026 from item-database research: stages 8 to 18
    // are 125, 125, 141, 0, 156, 226, 226, 245, 258, 284, 284. Stage 11 is
    // disabled in Individual Progression and stays 0, so it refuses. Wrath
    // Naxx 25 tops out at 226, not 224, and nothing lootable reaches 290; the
    // best drops are 284. Each cap is the 25-man normal level of the content
    // that stage allows; Heroic splits are a later, separate check.
    // The name is kept because other branches include this header.
    inline constexpr std::uint32_t ApprovedVanillaCompletion[19] = {
        66, 78, 78, 83, 83, 83, 88, 92, 125, 125, 141, 0, 156, 226, 226, 245, 258, 284, 284};

    // The highest approved Vanilla stage (0 to 7). Later stages do not raise
    // it. A zero score-limit config does not replace this with a ceiling of
    // 78. A nonzero score limit only lowers the cap.
    constexpr std::uint32_t ApprovedVanillaEraCeiling()
    {
        std::uint32_t ceiling = 0;
        for (std::uint32_t stage = 0; stage < PlayerbotsAutoGearCap::TbcEntryStage; ++stage)
        {
            if (ApprovedVanillaCompletion[stage] > ceiling)
                ceiling = ApprovedVanillaCompletion[stage];
        }
        return ceiling;
    }

    // Owner-approved 18 Nov 2026: Wrath 284 expansion ceiling (12:23 UK) and
    // TBC 159, the highest actual Sunwell drop (16:49 UK; was 164).
    inline constexpr std::uint32_t ApprovedTbcEraCeiling = 159;
    inline constexpr std::uint32_t ApprovedWrathEraCeiling = 284;

    inline constexpr std::array<std::uint32_t, 3> ApprovedEraFloors{{0, 0, 0}};
    inline constexpr std::array<std::uint32_t, 3> ApprovedEraCeilings{
        {ApprovedVanillaEraCeiling(), ApprovedTbcEraCeiling, ApprovedWrathEraCeiling}};

    constexpr PlayerbotsAutoGearCap::Policy ApprovedCapPolicy()
    {
        PlayerbotsAutoGearCap::Policy policy;
        policy.completionCaps = ApprovedVanillaCompletion;
        policy.eraEntryFloors = ApprovedEraFloors;
        policy.eraCeilings = ApprovedEraCeilings;
        return policy;
    }

    enum class CapMode : std::uint8_t
    {
        // Requires the sender's fixed-denominator equipped average.
        Leveling,
        // Tier cap only. The average is not required.
        Endgame,
        // Master's average over occupied combat slots. Empty slots are ignored.
        Match
    };

    struct CapInput
    {
        bool playerContext = false;
        bool progressionContext = false;
        CapMode mode = CapMode::Endgame;
        std::uint8_t rawStage = 0;
        std::uint8_t effectiveStage = 0;
        PlayerbotsAutoGearCap::Policy policy{};
        std::uint32_t scoreLimit = 0;
        bool typedItemLevelPresent = false;
        std::uint32_t typedItemLevel = 0;
        bool senderSampleKnown = false;
        std::uint32_t senderAverage = 0;
        // Explicit match target. Default callers leave this unset.
        // Legacy match fills it from the master's occupied-slot average.
        bool matchSampleKnown = false;
        std::uint32_t matchAverage = 0;
    };

    struct CapResult
    {
        bool preserve = true;
        std::uint32_t cap = 0;
    };

    // Stage and era arithmetic is PlayerbotsAutoGearCap::Resolve. This
    // function adds the leveling average and rejects a present typed 0.
    // A zero result is preserve, never unlimited.
    constexpr CapResult ComposeCap(CapInput const& in)
    {
        if (in.mode != CapMode::Leveling && in.mode != CapMode::Endgame && in.mode != CapMode::Match)
            return {};
        if (!in.playerContext || !in.progressionContext)
            return {};
        if (in.typedItemLevelPresent && in.typedItemLevel == 0)
            return {};

        std::uint32_t const typed = in.typedItemLevelPresent ? in.typedItemLevel : 0;
        PlayerbotsAutoGearCap::Decision const stage =
            PlayerbotsAutoGearCap::Resolve(in.rawStage, in.effectiveStage, in.policy, in.scoreLimit, typed);
        if (!stage.valid || stage.itemLevelCap == 0)
            return {};

        std::uint32_t cap = stage.itemLevelCap;
        if (in.mode == CapMode::Leveling)
        {
            if (!in.senderSampleKnown || in.senderAverage == 0)
                return {};
            cap = cap < in.senderAverage ? cap : in.senderAverage;
        }
        else if (in.mode == CapMode::Match)
        {
            // Do not replace an explicit match with the tier cap alone.
            if (!in.matchSampleKnown || in.matchAverage == 0)
                return {};
            cap = cap < in.matchAverage ? cap : in.matchAverage;
        }
        if (cap == 0)
            return {};
        return {false, cap};
    }
}

#endif
