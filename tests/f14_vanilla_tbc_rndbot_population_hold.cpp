// Source-only / portable F14 new RandomBot preactivation hold policy tests.
// Does not compile AzerothCore, create a bot, write DB, or grant stage8.
#include "../src/Bot/F14VanillaTbcRandomBotPopulationHold.h"
#include <cstdint>
#include <iostream>

namespace F14 = CosmicF14VanillaTbcPopulationHold;

constexpr bool Held(bool flag, bool positivelySelectedRndAccount, std::uint8_t race)
{
    return F14::Decide(flag, positivelySelectedRndAccount, race) ==
           F14::Result::HoldBeforeAddEvent;
}

static_assert(!Held(false, true, 10), "default-OFF must keep Blood Elf original");
static_assert(!Held(false, true, 11), "default-OFF must keep Draenei original");
static_assert(Held(true, true, 10), "only ON + assigned RNDbot Blood Elf held");
static_assert(Held(true, true, 11), "only ON + assigned RNDbot Draenei held");
static_assert(!Held(true, false, 10), "unknown/unassigned account not classified");
static_assert(!Held(true, false, 11), "unknown/unassigned account not classified");
static_assert(!Held(true, true, 1), "normal human races untouched");
static_assert(!Held(true, true, 5), "normal undead race untouched");

int main()
{
    std::uint32_t checked = 0;
    std::uint32_t failed = 0;
    for (int pilot = 0; pilot < 2; ++pilot)
        for (int selected = 0; selected < 2; ++selected)
            for (int race = 0; race <= 255; ++race)
            {
                bool const expected =
                    pilot && selected && (race == 10 || race == 11);
                bool const actual = Held(pilot != 0, selected != 0,
                                         static_cast<std::uint8_t>(race));
                ++checked;
                if (actual != expected)
                {
                    ++failed;
                    std::cerr << "FAIL pilot=" << pilot
                              << " selected=" << selected
                              << " race=" << race << "\n";
                }
            }
    if (!failed)
        std::cout << "PASS F14 manual population hold "
                  << checked << "/" << checked
                  << " combinations; NO LOGIN OR STAGE GRANT\n";
    return failed ? 1 : 0;
}
