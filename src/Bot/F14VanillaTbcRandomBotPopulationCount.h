#ifndef COSMIC_PB_F14_VANILLA_TBC_RANDOMBOT_POPULATION_COUNT_H
#define COSMIC_PB_F14_VANILLA_TBC_RANDOMBOT_POPULATION_COUNT_H

// Read-only scheduled population accounting for a MANUAL Vanilla F14 hold.
// This is a pure helper; caller must positively prove each character belongs
// to Playerbots' assigned type1 RandomBot pool and is race 10/11. Unknowns
// remain counted. It never deletes events, GUIDs or any character data.
#include <cstddef>

namespace CosmicF14VanillaTbcRandomBotPopulationCount
{
template <typename GuidRange, typename IsPositivelyHeld>
std::size_t CountForQuota(bool manualVanillaHold,
                          GuidRange const& scheduledGuids,
                          IsPositivelyHeld const& isPositivelyHeld)
{
    // Preserve the exact historical count and avoid cache/DB work when OFF.
    if (!manualVanillaHold)
        return scheduledGuids.size();

    std::size_t eligible = 0;
    for (auto const& guid : scheduledGuids)
        if (!isPositivelyHeld(guid))
            ++eligible;

    return eligible;
}
}
#endif
