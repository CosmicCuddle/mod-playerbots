/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_RNDBOT_TIER_SYNC_H
#define PLAYERBOTS_AUTOGEAR_RNDBOT_TIER_SYNC_H

#include <cstdint>

namespace PlayerbotsAutoGearSelection
{
    enum class Population : std::uint8_t
    {
        RandomBot,
        AltBot,
        AddClass,
        Unknown
    };

    // Population regear only. A commanded alt bot can still be geared under
    // the sender's cap later. This predicate never writes a progression stage.
    constexpr bool AllowsPopulationTierSync(Population population)
    {
        return population == Population::RandomBot;
    }
}

#endif
