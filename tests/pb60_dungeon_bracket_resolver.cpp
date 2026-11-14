#include "DungeonLevelBracket.h"
#include <array>
#include <span>

using namespace PlayerbotsDungeonBracket;

constexpr std::array<Row, 5> rows = {{
    {1, 100, 0, 14, 18, true, false, false},
    {2, 100, 0, 14, 18, true, false, false},
    {3, 100, 1, 40, 50, true, false, false},
    {4, 100, 0, 1, 80, false, false, false}, // random queue category
    {5, 100, 0, 70, 80, true, true, true}
}};

constexpr std::span<Row const> physicalRows(rows);
static_assert(Resolve(physicalRows, 100, 0, 0, 14).result == Result::Allowed);
static_assert(Resolve(physicalRows, 100, 0, 0, 18).result == Result::Allowed);
static_assert(Resolve(physicalRows, 100, 0, 0, 13).result == Result::BelowMinimum);
static_assert(Resolve(physicalRows, 100, 0, 0, 19).result == Result::AboveMaximum);
static_assert(Resolve(physicalRows, 100, 1, 0, 45).result == Result::Allowed);
static_assert(Resolve(physicalRows, 100, 0, 5, 1).result == Result::SeasonalException);
static_assert(Resolve(physicalRows, 100, 0, 0, 1).result == Result::BelowMinimum);
static_assert(Resolve(physicalRows, 100, 0, 0, 20, 20, 25).result == Result::Allowed);
static_assert(Resolve(physicalRows, 999, 0, 0, 20).result == Result::MissingBracket);
static_assert(Resolve(physicalRows, 999, 0, 0, 20, 20, 25).result == Result::Allowed);

constexpr std::array<Row, 2> conflictingRows = {{
    {1, 100, 0, 14, 18, true, false, false},
    {2, 100, 0, 15, 20, true, false, false}
}};
static_assert(Resolve(std::span<Row const>(conflictingRows), 100, 0, 0, 16).result == Result::AmbiguousBracket);
static_assert(Resolve(std::span<Row const>(conflictingRows), 100, 0, 2, 19).result == Result::Allowed);

constexpr std::array<Row, 1> inactiveHoliday = {{{5, 100, 0, 70, 80, true, true, false}}};
static_assert(Resolve(std::span<Row const>(inactiveHoliday), 100, 0, 5, 75).result == Result::SeasonalInactive);
static_assert(Resolve(std::span<Row const>(inactiveHoliday), 100, 0, 0, 75).result == Result::MissingBracket);

int main() { return 0; }
