#include "AutoGearNativeAuthority.h"
#include <cassert>

using namespace PlayerbotsAutoGearNativeAuthority;

int main()
{
    PB59::EquipmentFacts facts;
    facts.stamp = {1, 1};
    facts.guid = 42;
    facts.level = 35;
    facts.playerClass = 1;
    facts.race = 1;
    facts.slots[0] = {101, 1001, 80, 3, 1, 4, 4, 0, 0};
    facts.slots[1] = {102, 1002, 20, 3, 2, 4, 0, 0, 0};
    auto sample = Capture(facts, 1, 7, true, 7);
    assert(sample && sample->average.average == 33 && sample->average.countedSlots == 3);
    assert(Current(*sample, facts, 1, 7, true, 7));
    assert(!Current(*sample, facts, 2, 7, true, 7));
    assert(!Current(*sample, facts, 1, 3, true, 7));
    assert(!Current(*sample, facts, 1, 7, false, 7));
    auto changed = facts;
    changed.slots[1] = {};
    auto removed = Capture(changed, 1, 7, true, 7);
    assert(removed && removed->average.average == 26);
    assert(!Current(*sample, changed, 1, 7, true, 7));
    changed = facts;
    changed.slots[0].itemGuid++;
    assert(!Current(*sample, changed, 1, 7, true, 7));
    changed = facts;
    changed.stamp.session++;
    assert(!Current(*sample, changed, 1, 7, true, 7));
    changed = facts;
    changed.dualWield = true;
    assert(!Current(*sample, changed, 1, 7, true, 7));
    changed = facts;
    changed.slots[0].scalingDistribution = 1;
    assert(!Capture(changed, 1, 7, true, 7));
    changed = facts;
    changed.slots[0].quality = 7;
    assert(!Capture(changed, 1, 7, true, 7));
    changed = facts;
    changed.slots[0].entry = 0;
    assert(!Capture(changed, 1, 7, true, 7));
    changed = facts;
    changed.slots[0].itemGuid = 0;
    assert(!Capture(changed, 1, 7, true, 7));
    assert(!Capture(facts, 0, 7, true, 7));
    assert(!Capture(facts, 1, 1u << 3, true, 7));
    assert(!Capture(facts, 1, 1u << 18, true, 7));
    assert(!Capture(facts, 1, 0, true, 7));
    assert(!Capture(facts, 1, 1u << 2, true, 7));
}
