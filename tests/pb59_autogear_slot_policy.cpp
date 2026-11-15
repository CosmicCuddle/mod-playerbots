#include "AutoGearSlotPolicy.h"

using namespace PlayerbotsAutoGearSlots;

// Synthetic slot IDs follow Core's equipment layout; production binds its enums.
constexpr Layout slots{0, 1, 3, 4, 6, 7, 10, 11, 12, 13, 15, 16, 17, 18};

constexpr unsigned SearchableCount(unsigned level)
{
    unsigned count = 0;
    for (unsigned slot = 0; slot < 19; ++slot)
        if (SearchAtLevel(level, slot, slots))
            ++count;
    return count;
}

static_assert(SearchableCount(1) == 6);
static_assert(SearchableCount(4) == 6);
static_assert(SearchableCount(5) == 11);
static_assert(SearchableCount(19) == 11);
static_assert(SearchableCount(20) == 13);
static_assert(SearchableCount(29) == 13);
static_assert(SearchableCount(30) == 15);
static_assert(SearchableCount(49) == 15);
static_assert(SearchableCount(50) == 17);
static_assert(SearchableCount(60) == 17);
static_assert(SearchableCount(61) == 17);
static_assert(SearchableCount(70) == 17);
static_assert(SearchableCount(71) == 17);
static_assert(SearchableCount(80) == 17);
static_assert(!SearchAtLevel(80, slots.body, slots));
static_assert(!SearchAtLevel(80, slots.tabard, slots));
static_assert(SearchAtLevel(1, slots.mainhand, slots));
static_assert(!SearchAtLevel(29, slots.head, slots));
static_assert(SearchAtLevel(30, slots.head, slots));

int main() { return 0; }
