#ifndef COSMIC_PB_F14_VANILLA_TBC_RANDOMBOT_POPULATION_HOLD_H
#define COSMIC_PB_F14_VANILLA_TBC_RANDOMBOT_POPULATION_HOLD_H

// CosmicCuddle: source-only F14 preactivation policy. The actual PB caller
// is the verified RandomBot type-1 candidate selector, NOT an arbitrary
// human/Altbot login. A deliberate manual hold switch is OFF by default.
// This function neither grants an IP stage nor hides/alters any character.
#include <cstdint>

namespace CosmicF14VanillaTbcPopulationHold
{
enum class Result : std::uint8_t
{
    StockAdmissionPath,     // Leave all existing selection logic untouched.
    HoldBeforeAddEvent      // Refuse candidate selection before any admission side effect.
};

constexpr Result Decide(bool manuallyEnabledForVanilla,
                        bool fromAssignedRandomBotPool,
                        std::uint8_t race)
{
    return manuallyEnabledForVanilla && fromAssignedRandomBotPool &&
           (race == 10 || race == 11)
        ? Result::HoldBeforeAddEvent
        : Result::StockAdmissionPath;
}
} // namespace CosmicF14VanillaTbcPopulationHold
#endif
