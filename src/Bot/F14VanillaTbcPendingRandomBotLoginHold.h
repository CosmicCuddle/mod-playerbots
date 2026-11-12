#ifndef COSMIC_PB_F14_VANILLA_PENDING_RANDOMBOT_LOGIN_HOLD_H
#define COSMIC_PB_F14_VANILLA_PENDING_RANDOMBOT_LOGIN_HOLD_H

// No character, event, session or database mutation. This pure policy is
// consulted only while a previously queued headless login callback runs.
// Unknown / insufficiently proven identities retain the historical path.
#include "F14VanillaTbcRandomBotPopulationHold.h"
#include <cstdint>

namespace CosmicF14VanillaPendingRandomBotLogin
{
constexpr bool ShouldHold(bool manualVanillaHold, bool zeroMasterRequest,
                          bool selectedGuid, bool cacheEntryPresent,
                          std::uint32_t requestedAccountId,
                          std::uint32_t currentCachedAccountId,
                          bool assignedType1RandomBot, std::uint8_t cachedRace)
{
    return zeroMasterRequest && selectedGuid && cacheEntryPresent &&
           requestedAccountId != 0 && requestedAccountId == currentCachedAccountId &&
           CosmicF14VanillaTbcPopulationHold::Decide(
               manualVanillaHold, assignedType1RandomBot, cachedRace) ==
               CosmicF14VanillaTbcPopulationHold::Result::HoldBeforeAddEvent;
}
} // namespace CosmicF14VanillaPendingRandomBotLogin

#endif
