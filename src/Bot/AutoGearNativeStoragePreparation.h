#pragma once

#include "AutoGearExactStorageObservation.h"
#include "AutoGearStorageTransaction.h"
#include "Bag.h"
#include "Player.h"
#include <array>
#include <utility>

namespace PlayerbotsAutoGearStorage
{
// World/serialized bot owner only. No item creation, movement or save.
// A preparation is an owning snapshot, NOT a command admission or a lease.
struct NativeStoragePlan
{
    std::uint64_t ownerIncarnation = 0;
    PB59::Stamp equipmentStamp{};
    std::vector<PB59::InventorySlotFacts> equipment;
    std::vector<PB59::InventorySlotFacts> destinations;
    std::vector<ExactMove> moves;
};

enum class NativePreparationStatus { Rejected, NoSpace, Ready };
struct NativePreparation
{
    NativePreparationStatus status = NativePreparationStatus::Rejected;
    NativeStoragePlan plan;
};

inline std::uint16_t StoragePosition(std::uint8_t bag, std::uint8_t slot)
{
    return (std::uint16_t(bag) << 8) | slot;
}

inline bool NativeStorageCurrent(Player const& bot, NativeStoragePlan const& plan)
{
    if (!plan.ownerIncarnation || bot.GetPB59OwnerIncarnation() != plan.ownerIncarnation ||
        !bot.IsPB59EquipmentCurrent(plan.equipmentStamp))
        return false;
    for (auto const& expected : plan.equipment)
    {
        auto now = bot.CapturePB59InventorySlot(expected.position);
        if (!now || *now != expected)
            return false;
    }
    for (auto const& expected : plan.destinations)
    {
        auto now = bot.CapturePB59InventorySlot(expected.position);
        if (!now || *now != expected)
            return false;
    }
    return true;
}

inline NativePreparation PrepareNativeStorage(Player& bot)
{
    // Core policy checks may call scripts. Prevent PB59 retirement/transaction
    // entry while preparing, and recheck copied facts after each policy call.
    auto scope = bot.AcquirePB59CallbackScope();
    if (!scope.Admitted())
        return {};
    try
    {
        auto snapshot = bot.CapturePB59Equipment();
        if (!snapshot || !bot.IsPB59EquipmentCurrent(snapshot->stamp))
            return {};
        NativeStoragePlan plan;
        plan.ownerIncarnation = bot.GetPB59OwnerIncarnation();
        plan.equipmentStamp = snapshot->stamp;
        if (!plan.ownerIncarnation)
            return {};
        for (std::uint8_t slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        {
            if (slot == EQUIPMENT_SLOT_BODY || slot == EQUIPMENT_SLOT_TABARD)
                continue;
            auto facts = bot.CapturePB59InventorySlot(StoragePosition(INVENTORY_SLOT_BAG_0, slot));
            if (!facts)
                return {};
            plan.equipment.push_back(std::move(*facts));
        }

        // Enumerate only normal backpack/equipped-bag positions. Existing
        // occupied slots never qualify, even if a stack could be merged.
        std::vector<PB59::InventorySlotFacts> empty;
        auto collect = [&](std::uint8_t bag, std::uint8_t slot)
        {
            auto pos = StoragePosition(bag, slot);
            if (bot.GetItemByPos(pos))
                return true;
            auto facts = bot.CapturePB59InventorySlot(pos);
            if (!facts || facts->item)
                return false;
            empty.push_back(std::move(*facts));
            return true;
        };
        for (std::uint8_t slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            if (!collect(INVENTORY_SLOT_BAG_0, slot))
                return {};
        for (std::uint8_t bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
        {
            auto* equipped = bot.GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
            if (!equipped)
                continue;
            auto* container = bot.GetBagByPos(bag);
            if (!container || container->GetBagSize() > MAX_BAG_SIZE)
                return {};
            for (std::uint32_t slot = 0; slot < container->GetBagSize(); ++slot)
                if (!collect(bag, static_cast<std::uint8_t>(slot)))
                    return {};
        }
        // During preparation validate all candidate empty positions, not only
        // the chosen matching. A callback can occupy a previously free slot.
        plan.destinations = empty;
        auto current = [&]()
        {
            return scope.Admitted() && NativeStorageCurrent(bot, plan);
        };
        if (!current())
            return {};
        std::vector<Domain> domains;
        for (auto const& source : plan.equipment)
        {
            if (!source.item)
                continue;
            auto unequip = bot.CanUnequipItem(source.position, false);
            if (!current() || unequip != EQUIP_ERR_OK)
                return {};
            Domain domain{source.item->guid, source.position, {}};
            for (auto const& destination : empty)
            {
                Item* item = bot.GetItemByPos(source.position);
                if (!item || item->GetGUID().GetRawValue() != source.item->guid)
                    return {};
                ItemPosCountVec places;
                auto result = bot.CanStoreItem(destination.position >> 8,
                    destination.position & 255, places, item, false);
                if (!current())
                    return {};
                if (result == EQUIP_ERR_OK && places.size() == 1 &&
                    places.front().pos == destination.position &&
                    places.front().count == source.item->count)
                    domain.destinations.push_back(destination.position);
            }
            domains.push_back(std::move(domain));
        }
        auto matching = Match(domains, true, 17u * (16u + 4u * MAX_BAG_SIZE));
        if (matching.status == Status::NoSpace)
            return {NativePreparationStatus::NoSpace, {}};
        if (matching.status != Status::Ready || !current())
            return {};
        std::vector<PB59::InventorySlotFacts> chosen;
        for (auto const& move : matching.moves)
        {
            auto source = std::find_if(plan.equipment.begin(), plan.equipment.end(),
                [&](auto const& f) { return f.position == move.source; });
            auto destination = std::find_if(empty.begin(), empty.end(),
                [&](auto const& f) { return f.position == move.destination; });
            if (source == plan.equipment.end() || destination == empty.end() ||
                !source->item || !OriginalAndEmpty(*source, destination->position,
                    destination->bag, *source, *destination))
                return {};
            plan.moves.push_back({move, source->item->count, destination->bag});
            chosen.push_back(*destination);
        }
        plan.destinations = std::move(chosen);
        if (!current())
            return {};
        return {NativePreparationStatus::Ready, std::move(plan)};
    }
    catch (...)
    {
        return {};
    }
}
}
