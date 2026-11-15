#include "AutoGearEquipmentSample.h"
#include <array>
#include <limits>

using namespace PlayerbotsAutoGearSample;

constexpr std::array<Slot, 4> mixed = {{
    {true, true, true, false, 65}, {true, true, true, false, 78},
    {false, true, false, false, 999}, {true, false, false, true, 999}
}};
static_assert(Average(mixed, true).known);
static_assert(Average(mixed, true).average == 47);
static_assert(Average(mixed, true).occupiedSlots == 2);
static_assert(!Average(std::span<Slot const>{}, true).known);
constexpr std::array<Slot, 1> zero = {{{true, true, true, false, 0}}};
constexpr std::array<Slot, 1> missing = {{{true, true, false, false, 78}}};
constexpr std::array<Slot, 1> heirloom = {{{true, true, true, true, 1}}};
static_assert(!Average(zero, true).known);
constexpr std::array<Slot, 2> mixedZero = {{
    {true, true, true, false, 0}, {true, true, true, false, 70}
}};
static_assert(!Average(mixedZero, true).known);
static_assert(Average(mixedZero, true).average == 0);
constexpr std::array<Slot, 2> excludedZero = {{
    {true, false, true, false, 0}, {true, true, true, false, 70}
}};
static_assert(Average(excludedZero, true).known && Average(excludedZero, true).average == 70);
static_assert(!Average(missing, true).known);
static_assert(!Average(heirloom, true).known);
constexpr std::uint32_t maximum = std::numeric_limits<std::uint32_t>::max();
constexpr std::array<Slot, 2> large = {{
    {true, true, true, false, maximum}, {true, true, true, false, maximum}
}};
static_assert(Average(large, true).average == maximum);
constexpr std::array<Slot, 20> oversized{};
static_assert(!Average(oversized, true).known);
static_assert(Average(mixed, true).countedSlots == 3);
static_assert(!Average(mixed, false).known);

constexpr bool RemovingGearNeverRaisesKnownSample()
{
    // Every occupancy pattern of four eligible slots; the denominator is fixed.
    for (unsigned mask = 0; mask < 16; ++mask)
    {
        std::array<Slot, 4> loadout{};
        for (unsigned slot = 0; slot < loadout.size(); ++slot)
            loadout[slot] = {(mask & (1u << slot)) != 0, true, true, false, 40 + slot * 13};
        auto const before = Average(loadout, true);
        for (unsigned removed = 0; removed < loadout.size(); ++removed)
        {
            auto afterRemoval = loadout;
            afterRemoval[removed].occupied = false;
            auto const after = Average(afterRemoval, true);
            if (after.average > before.average || (after.known && !before.known))
                return false;
            if (after.known && after.countedSlots != 4)
                return false;
        }
    }
    return true;
}
static_assert(RemovingGearNeverRaisesKnownSample());

constexpr auto LowGearRemovalExample()
{
    std::array<Slot, 17> loadout{};
    for (unsigned slot = 0; slot < loadout.size(); ++slot)
        loadout[slot] = {true, true, true, false, slot < 7 ? 76u : 40u};
    return loadout;
}
constexpr auto beforeRemoval = LowGearRemovalExample();
static_assert(Average(beforeRemoval, true).average == 54);
constexpr auto afterRemoval = []
{
    auto loadout = beforeRemoval;
    for (unsigned slot = 7; slot < loadout.size(); ++slot)
        loadout[slot].occupied = false;
    return loadout;
}();
static_assert(Average(afterRemoval, true).average == 31);

int main() { return 0; }
