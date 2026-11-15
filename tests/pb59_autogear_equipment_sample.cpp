#include "AutoGearEquipmentSample.h"
#include <array>
#include <limits>

using namespace PlayerbotsAutoGearSample;

constexpr std::array<Slot, 4> mixed = {{
    {true, true, true, false, 65}, {true, true, true, false, 78},
    {false, true, false, false, 999}, {true, false, false, true, 999}
}};
static_assert(Average(mixed).known);
static_assert(Average(mixed).average == 71);
static_assert(Average(mixed).occupiedSlots == 2);
static_assert(!Average(std::span<Slot const>{}).known);
constexpr std::array<Slot, 1> zero = {{{true, true, true, false, 0}}};
constexpr std::array<Slot, 1> missing = {{{true, true, false, false, 78}}};
constexpr std::array<Slot, 1> heirloom = {{{true, true, true, true, 1}}};
static_assert(!Average(zero).known);
static_assert(!Average(missing).known);
static_assert(!Average(heirloom).known);
constexpr std::uint32_t maximum = std::numeric_limits<std::uint32_t>::max();
constexpr std::array<Slot, 2> large = {{
    {true, true, true, false, maximum}, {true, true, true, false, maximum}
}};
static_assert(Average(large).average == maximum);
constexpr std::array<Slot, 20> oversized{};
static_assert(!Average(oversized).known);

int main() { return 0; }
