#include "AutoGearExactStorageObservation.h"
#include <cassert>
using namespace PlayerbotsAutoGearStorage;
using namespace PB59;
int main()
{
    InventoryInstanceFacts item;
    item.guid = 17; item.owner = 42; item.entry = 500; item.count = 2;
    item.enchants[2] = {7,0,0}; item.durability = 20; item.randomProperty = -8;
    InventorySlotFacts original{42,0,0xff0f,item};
    InventorySlotFacts empty{42,99,0x1300,std::nullopt};
    InventorySlotFacts sourceEmpty{42,0,0xff0f,std::nullopt};
    auto stored = empty; stored.item = item;
    assert(OriginalAndEmpty(original,empty.position,99,original,empty));
    assert(Stored(original,empty.position,99,sourceEmpty,stored));
    assert(!Stored(original,empty.position,99,original,stored));
    assert(!OriginalAndEmpty(original,empty.position,99,original,stored));
    auto check = [&](auto change) {
        auto altered = stored; change(altered);
        assert(!Stored(original,empty.position,99,sourceEmpty,altered));
    };
    check([](auto& x){++x.bag;});
    check([](auto& x){++x.owner;});
    check([](auto& x){++x.position;});
    check([](auto& x){++x.item->count;});
    check([](auto& x){++x.item->guid;});
    check([](auto& x){++x.item->enchants[2].id;});
    check([](auto& x){++x.item->durability;});
    check([](auto& x){++x.item->randomProperty;});
    check([](auto& x){x.item.reset();});
    auto bad = original; ++bad.item->enchants[2].id;
    assert(!OriginalAndEmpty(original,empty.position,99,bad,empty));
    bad = empty; ++bad.bag;
    assert(!OriginalAndEmpty(original,empty.position,99,original,bad));
    bad = sourceEmpty; bad.bag = 100;
    assert(!Stored(original,empty.position,99,bad,stored));
    bad = original; bad.item.reset();
    assert(!StorageReservationValid(bad,empty.position));
    assert(!StorageReservationValid(original,original.position));
    assert(!StorageReservationValid(original,0));
    bad = original; bad.bag = 5;
    assert(!StorageReservationValid(bad,empty.position));
    bad = original; bad.item->owner = 43;
    assert(!StorageReservationValid(bad,empty.position));
    bad = original; bad.item->count = 0;
    assert(!StorageReservationValid(bad,empty.position));
    InventorySlotFacts backpack{42,0,0xff17,std::nullopt};
    assert(OriginalAndEmpty(original,backpack.position,0,original,backpack));
    backpack.item = item;
    assert(Stored(original,backpack.position,0,sourceEmpty,backpack));
}
