// Test the ACTUAL new pure hold predicate under GCC/Clang C++17/C++20.
// No server startup, DB, player data, accounts or visibility packets.
#include "../src/Bot/F14VanillaTbcPendingRandomBotLoginHold.h"
#include <cstdint>
#include <iostream>

namespace P = CosmicF14VanillaPendingRandomBotLogin;
int main()
{
    std::uint32_t failures = 0;
    std::uint32_t cases = 0;
    for (std::uint32_t mask = 0; mask < 128; ++mask)
    {
        bool const manual = (mask & 1u) != 0;
        bool const zeroMaster = (mask & 2u) != 0;
        bool const selected = (mask & 4u) != 0;
        bool const cached = (mask & 8u) != 0;
        bool const type1 = (mask & 16u) != 0;
        bool const match = (mask & 32u) != 0;
        bool const accountPresent = (mask & 64u) != 0;
        std::uint32_t const requestedAccount = accountPresent ? 4321 : 0;
        std::uint32_t const cachedAccount = match ? requestedAccount : 9876;
        for (std::uint8_t race : {std::uint8_t(1), std::uint8_t(10),
                                  std::uint8_t(11), std::uint8_t(12)})
        {
            bool const expected =
                manual && zeroMaster && selected && cached && type1 &&
                match && accountPresent && (race == 10 || race == 11);
            bool const actual = P::ShouldHold(
                manual, zeroMaster, selected, cached, requestedAccount,
                cachedAccount, type1, race);
            if (actual != expected)
                ++failures;
            ++cases;
        }
    }

    // Explicit stale-identity case: a different current cached account
    // must not hold an unrelated/reassigned character based on old data.
    if (P::ShouldHold(true, true, true, true, 123, 456, true, 10))
        ++failures;
    // Manual OFF always preserves historical behaviour.
    if (P::ShouldHold(false, true, true, true, 123, 123, true, 11))
        ++failures;

    if (!failures)
        std::cout << "PASS PB F14 pending login: " << cases
                  << " positive/negative combinations + stale-account/OFF cases\n";
    return failures ? 1 : 0;
}
