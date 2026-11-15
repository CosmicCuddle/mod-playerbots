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
    // Recorded proposal from coordination #59 (comment 6885246726).
    // Stages 0 and 1 are owner-confirmed (66 and 78). Stages 2 to 7 are
    // PROPOSED and await owner review. They are not a production default:
    // pass them in a Policy only when that review accepts them. Entries
    // after stage 7 stay 0 so Resolve fails closed.
    inline constexpr std::uint32_t ProposedVanillaCompletionAwaitingOwnerReview[19] = {
        66, 78, 78, 83, 83, 83, 88, 92, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    enum class CapMode : std::uint8_t
    {
        // Requires the sender's fixed-denominator equipped average.
        Leveling,
        // Tier cap only. The average is not required.
        Endgame,
        // Average is not required. Whether Match should also target it
        // awaits owner confirmation (#59 comment 6893387731).
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
        if (cap == 0)
            return {};
        return {false, cap};
    }
}

#endif
