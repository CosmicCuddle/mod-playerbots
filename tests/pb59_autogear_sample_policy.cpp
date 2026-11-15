#include "AutoGearEquipmentSample.h"
#include <array>
#include <initializer_list>

using namespace PlayerbotsAutoGearSample;

// Test-only policy adapter: mainhand, offhand, ranged, optional armor slot.
// No production thresholds or class capability defaults are selected here.
struct Policy
{
    bool known;
    bool offhandEligible;
    bool rangedEligible;
    bool optionalEligible;
    unsigned twoHandWeight;
};

constexpr Result Sample(std::array<Slot, 4> slots, Policy policy, bool twoHand, bool titanGrip)
{
    if (!policy.known || (policy.twoHandWeight != 1 && policy.twoHandWeight != 2))
        return {};
    slots[0].countedByPolicy = true;
    slots[1].countedByPolicy = policy.offhandEligible;
    slots[2].countedByPolicy = policy.rangedEligible;
    slots[3].countedByPolicy = policy.optionalEligible;
    if (twoHand && !titanGrip && slots[0].occupied)
    {
        if (slots[1].occupied)
            return {}; // Invalid coupled set cannot authorize a sample.
        if (policy.offhandEligible && policy.twoHandWeight == 2)
            slots[1] = slots[0];
    }
    return Average(slots, true);
}

constexpr Policy bothHands{true, true, false, false, 2};
constexpr std::array<Slot, 4> twoHandOnly = {{
    {true, false, true, false, 78}, {}, {}, {}
}};
static_assert(Sample(twoHandOnly, bothHands, true, false).average == 78);
static_assert(Sample(twoHandOnly, {true, true, false, false, 1}, true, false).average == 39);
static_assert(Sample(twoHandOnly, bothHands, true, true).average == 39);
static_assert(Sample(twoHandOnly, {true, false, false, false, 2}, true, false).average == 78);
static_assert(!Sample(twoHandOnly, {false, true, false, false, 2}, true, false).known);
static_assert(!Sample(twoHandOnly, {true, true, false, false, 3}, true, false).known);

constexpr std::array<Slot, 4> oneAndOffhand = {{
    {true, false, true, false, 78}, {true, false, true, false, 40}, {}, {}
}};
// Shield/held-offhand arithmetic uses the same fixed eligible hand slots.
static_assert(Sample(oneAndOffhand, bothHands, false, false).average == 59);
static_assert(!Sample(oneAndOffhand, bothHands, true, false).known);
static_assert(Sample(oneAndOffhand, bothHands, true, true).average == 59);

constexpr bool RemovalCases()
{
    for (unsigned weight = 1; weight <= 2; ++weight)
        for (bool titanGrip : {false, true})
            for (bool ranged : {false, true})
                for (bool optional : {false, true})
                {
                    Policy policy{true, true, ranged, optional, weight};
                    auto loadout = twoHandOnly;
                    loadout[2] = {true, false, true, false, 30};
                    loadout[3] = {true, false, true, false, 20};
                    auto const before = Sample(loadout, policy, true, titanGrip);
                    for (unsigned removed = 0; removed < loadout.size(); ++removed)
                    {
                        auto changed = loadout;
                        changed[removed].occupied = false;
                        auto const after = Sample(changed, policy, true, titanGrip);
                        if (after.average > before.average || (after.known && !before.known))
                            return false;
                        if (after.known && after.countedSlots != before.countedSlots)
                            return false;
                    }
                }
    return true;
}
static_assert(RemovalCases());
constexpr auto offhandRemoved = []
{
    auto slots = oneAndOffhand;
    slots[1].occupied = false;
    return slots;
}();
static_assert(Sample(offhandRemoved, bothHands, false, false).average == 39);
static_assert(Sample(offhandRemoved, bothHands, false, false).countedSlots == 2);
static_assert(!Sample({}, bothHands, false, false).known);
static_assert(Sample({}, bothHands, false, false).average == 0);

int main() { return 0; }
