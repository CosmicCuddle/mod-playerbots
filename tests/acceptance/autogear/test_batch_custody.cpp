#include "AutoGearBatchCustody.h"
#include <iostream>
#include <vector>
using namespace PB59;
using namespace PlayerbotsAutoGearCoreBatch;
unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << "FAIL " << __LINE__ << '\n'; return 1; } } while (false)
InventorySlotFacts slot(unsigned pos, unsigned guid = 0, unsigned bag = 0)
{
    InventorySlotFacts s{1, bag, static_cast<std::uint16_t>(pos), {}};
    if (guid) { s.item.emplace(); s.item->guid = guid; s.item->owner = 1; s.item->entry = 100; s.item->count = 1; }
    return s;
}
int main()
{
    std::vector<InventorySlotFacts> before{slot(1, 11), slot(2), slot(3, 33), slot(4), slot(5, 55, 9), slot(6, 0, 9)};
    std::vector<InventoryBatchMove> moves{{before[0], before[1]}, {before[4], before[5]}};
    auto after = before;
    after[1].item = after[0].item; after[0].item.reset();
    after[5].item = after[4].item; after[4].item.reset();
    auto check = [&](auto const& a) { return AppliedCustodyPreserved(1, before, a, moves); };
    CHECK(check(after));
    auto a = after; ++a[2].item->durability; ++a[2].item->flags; a[2].item->text = "metadata"; CHECK(check(a));
    a = after; std::swap(a[2], a[4]); CHECK(check(a));
    a = after; std::swap(a[2].item, a[3].item); CHECK(!check(a));
    a = after; a[2].item->owner = 99; CHECK(!check(a));
    a = after; a[2].item.reset(); CHECK(!check(a));
    a = after; a[3].item = slot(4, 88).item; CHECK(!check(a));
    a = after; a[2].bag = 5000; CHECK(!check(a));
    a = after; ++a[2].item->count; CHECK(!check(a));
    a = after; ++a[2].item->entry; CHECK(!check(a));
    a = after; a[2].item->guid = 11; CHECK(!check(a));
    a = after; a[2].position = a[3].position; CHECK(!check(a));
    a = after; a.pop_back(); CHECK(!check(a));
    a = after; a[3].position = 99; CHECK(!check(a));
    a = after; a[1].item.reset(); CHECK(!check(a));
    a = after; a[0].item = before[0].item; CHECK(!check(a));
    a = after; a[1].item->guid = 99; CHECK(!check(a));
    a = after; a[5].bag = 10; CHECK(!check(a));
    a = after; a[3].owner = 99; CHECK(!check(a));
    a = before; a[2].item->guid = 11; CHECK(!AppliedCustodyPreserved(1, a, after, moves));
    auto m = moves; m[0].source.item->count = 2; CHECK(!AppliedCustodyPreserved(1, before, after, m));
    m = moves; m[0].destination.bag = 8; CHECK(!AppliedCustodyPreserved(1, before, after, m));
    m = moves; m[0].source.position = 99; CHECK(!AppliedCustodyPreserved(1, before, after, m));
    CHECK(!AppliedCustodyPreserved(1, before, after, {}));
    CHECK(!AppliedCustodyPreserved(0, before, after, moves));
    std::cout << checks << " custody checks PASS\n";
}
