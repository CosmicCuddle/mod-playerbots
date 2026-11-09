// Actual new pure PB14 quota header: no Worldserver, login or DB.
#include "../src/Bot/F14VanillaTbcRandomBotPopulationCount.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <unordered_set>
namespace Q = CosmicF14VanillaTbcRandomBotPopulationCount;
int main()
{
    constexpr std::size_t cases = 1024;
    std::size_t failures = 0;
    for (std::size_t mask = 0; mask < cases; ++mask)
    {
        std::unordered_set<std::uint32_t> scheduled;
        std::unordered_set<std::uint32_t> held;
        for (std::uint32_t i = 0; i < 10; ++i)
        {
            scheduled.insert(101 + i);
            if ((mask >> i) & 1)
                held.insert(101 + i);
        }

        std::size_t calls = 0;
        auto predicate = [&](std::uint32_t botGuid) {
            ++calls;
            return held.contains(botGuid);
        };

        // Manual OFF: exactly historical scheduled count, no cache calls.
        auto const off = Q::CountForQuota(false, scheduled, predicate);
        if (off != 10 || calls != 0)
            ++failures;

        // Manual ON: exclude only the positively held subset and NEVER
        // modify scheduled GUIDs or the caller's selected record.
        auto const on = Q::CountForQuota(true, scheduled, predicate);
        if (on != 10 - held.size() || calls != scheduled.size() ||
            scheduled.size() != 10)
            ++failures;

        // Unknown / ineligible identities are not included in held.
        auto const none = Q::CountForQuota(true, scheduled, [](auto) { return false; });
        if (none != 10)
            ++failures;
        auto const all = Q::CountForQuota(true, scheduled, [](auto) { return true; });
        if (all != 0)
            ++failures;

        // A result must never exceed size or underflow when all are held.
        if (on > scheduled.size() || on + held.size() != scheduled.size())
            ++failures;
    }

    std::unordered_set<std::uint32_t> empty;
    std::size_t never = 0;
    auto const emptyOn = Q::CountForQuota(
        true, empty, [&](std::uint32_t) { ++never; return true; });
    auto const emptyOff = Q::CountForQuota(
        false, empty, [&](std::uint32_t) { ++never; return true; });
    if (emptyOn != 0 || emptyOff != 0 || never != 0)
        ++failures;

    if (!failures)
        std::cout << "PASS PB14 actual pure quota 1024 bitsets, manual OFF unchanged, "
                  << "no erase, safe positive holds only\n";
    return failures ? 1 : 0;
}
