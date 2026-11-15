#pragma once

#include "PB59InventorySlotFacts.h"

namespace PlayerbotsAutoGearStorage
{
// Only validated owner-context slot observations may enter these predicates.
// A move changes position/container, not any copied item instance property.
inline bool StorageReservationValid(PB59::InventorySlotFacts const& original,
    std::uint16_t destination)
{
    return original.owner && original.position && original.position != destination &&
        destination && original.bag == 0 && original.item && original.item->guid &&
        original.item->owner == original.owner && original.item->count;
}
inline bool OriginalAndEmpty(PB59::InventorySlotFacts const& original,
    std::uint16_t destination, std::uint64_t destinationBag,
    PB59::InventorySlotFacts const& sourceNow, PB59::InventorySlotFacts const& destinationNow)
{
    return StorageReservationValid(original, destination) && sourceNow == original &&
        destinationNow.owner == original.owner && destinationNow.position == destination &&
        destinationNow.bag == destinationBag && !destinationNow.item;
}
inline bool Stored(PB59::InventorySlotFacts const& original,
    std::uint16_t destination, std::uint64_t destinationBag,
    PB59::InventorySlotFacts const& sourceNow, PB59::InventorySlotFacts const& destinationNow)
{
    return StorageReservationValid(original, destination) && sourceNow.owner == original.owner &&
        sourceNow.position == original.position && !sourceNow.bag && !sourceNow.item &&
        destinationNow.owner == original.owner && destinationNow.position == destination &&
        destinationNow.bag == destinationBag && PB59::SameInventoryInstance(original, destinationNow);
}
}
