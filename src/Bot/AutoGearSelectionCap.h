/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_SELECTION_CAP_H
#define PLAYERBOTS_AUTOGEAR_SELECTION_CAP_H

#include <algorithm>
#include <cstdint>
#include <span>

namespace PlayerbotsAutoGearSelection
{
    // Approved Vanilla completion caps. Stages 2..7 have none.
    inline constexpr std::uint32_t ApprovedStage0Cap = 66;
    inline constexpr std::uint32_t ApprovedStage1Cap = 78;
    inline constexpr std::uint8_t VanillaStageCount = 8;

    struct CapInput
    {
        bool playerContext = false;
        bool progressionContext = false;
        std::uint8_t rawStage = 0;
        std::uint8_t effectiveStage = 0;
        // Index is the raw stage. Zero means "not configured", never "unlimited".
        std::span<std::uint32_t const> configuredCompletion{};
        // Zero score limit and a missing typed item level do not raise the cap.
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

    // Levelling target is the sender's fixed-denominator average, then the
    // stricter of that average, the stage cap, a non-zero score limit and a
    // typed item level. A missing player, a missing progression context, an
    // unresolved stage, or a zero item level preserves. Cap 0 is never a
    // usable "unlimited" result.
    constexpr CapResult ComposeCap(CapInput const& in)
    {
        if (!in.playerContext || !in.progressionContext || in.effectiveStage > in.rawStage ||
            in.rawStage >= VanillaStageCount)
            return {};

        bool haveStage = false;
        std::uint32_t stageCap = 0;
        if (in.rawStage == 0)
        {
            stageCap = ApprovedStage0Cap;
            haveStage = true;
        }
        else if (in.rawStage == 1)
        {
            stageCap = ApprovedStage1Cap;
            haveStage = true;
        }

        if (in.rawStage < in.configuredCompletion.size() && in.configuredCompletion[in.rawStage] != 0)
        {
            std::uint32_t const configured = in.configuredCompletion[in.rawStage];
            if (!haveStage)
            {
                stageCap = configured;
                haveStage = true;
            }
            else
                stageCap = std::min(stageCap, configured);
        }
        if (!haveStage || stageCap == 0)
            return {};

        std::uint32_t cap = stageCap;
        if (in.scoreLimit != 0)
            cap = std::min(cap, in.scoreLimit);
        if (in.typedItemLevelPresent)
        {
            if (in.typedItemLevel == 0)
                return {};
            cap = std::min(cap, in.typedItemLevel);
        }
        // The sender average is the levelling target. Unknown or zero does
        // not mean "no ceiling".
        if (!in.senderSampleKnown || in.senderAverage == 0)
            return {};
        cap = std::min(cap, in.senderAverage);
        if (cap == 0)
            return {};
        return {false, cap};
    }
}

#endif
