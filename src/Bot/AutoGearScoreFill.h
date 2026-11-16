/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_SCORE_FILL_H
#define PLAYERBOTS_AUTOGEAR_SCORE_FILL_H

#include <cstdint>

namespace PlayerbotsAutoGearSelection
{
    // Scorer matches StatsWeightCalculator::CalculateItem(itemId, randomProperty, slot).
    // The returned value is that score. This wrapper has no weight table and
    // does not scale, prefer, or add a set-bonus delta of its own.
    template <class Scorer>
    constexpr std::uint32_t ExistingScore(std::uint32_t itemId, std::int32_t randomProperty, std::uint32_t slot,
        Scorer const& scorer)
    {
        return scorer(itemId, randomProperty, slot);
    }
}

#endif
