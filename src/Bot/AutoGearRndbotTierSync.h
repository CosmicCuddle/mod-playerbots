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

    // The owner excludes alt bots from every AutoGear command, even when the
    // historical AutoGearCommandAltBots setting is enabled.
    constexpr bool AllowsPopulationAutoGearCommand(Population population)
    {
        return population == Population::RandomBot;
    }

    constexpr bool IsConfirmedRandomBot(bool randomBot, bool randomAccount)
    {
        return randomBot && randomAccount;
    }

    // Population regear only. This predicate never writes a progression stage.
    constexpr bool AllowsPopulationTierSync(Population population)
    {
        return population == Population::RandomBot;
    }
}

#endif
