#pragma once

#include "AutoGearNativeDiscardPreparation.h"
#include "Item.h"
#include <array>
#include <memory>
#include <utility>

namespace PlayerbotsAutoGearStorage
{
struct RequestedReplacement
{
    std::uint32_t entry = 0;
    std::int32_t randomProperty = 0;
};

// Copied selection, not an authority token or proof of final equip legality.
struct NativeReplacementSelection
{
    std::uint32_t cap = 0;
    std::uint32_t quality = 0;
    std::uint32_t eligibleMask = 0;
    std::uint32_t requiredMask = 0;
    std::array<RequestedReplacement, EQUIPMENT_SLOT_END> pieces{};
};

// RAII owns only newly created, detached Items. Never insert these objects
// into native inventory or queues without an explicit ownership handoff.
struct NativeReplacementBatch
{
    std::array<std::unique_ptr<Item>, EQUIPMENT_SLOT_END> items{};
    std::uint32_t selectedMask = 0;
};

inline bool NativeReplacementItemCurrent(Item& item, RequestedReplacement piece,
    std::uint32_t cap, std::uint32_t quality)
{
    auto const* proto = item.GetTemplate();
    return proto && item.GetGUID() && piece.entry &&
        item.GetEntry() == piece.entry && item.GetCount() == 1 &&
        item.GetItemRandomPropertyId() == piece.randomProperty &&
        proto->ItemLevel && proto->ItemLevel <= cap && proto->Quality <= quality &&
        !item.IsBag() && item.GetOwnerGUID().IsEmpty() &&
        item.GetGuidValue(ITEM_FIELD_CONTAINED).IsEmpty() &&
        item.GetSlot() == NULL_SLOT && !item.GetContainer() &&
        !item.IsInWorld() && !item.IsInUpdateQueue() && item.GetState() == ITEM_NEW;
}

template<class VerifiedRandom>
std::optional<NativeReplacementBatch> PrepareNativeRandomReplacement(Player& bot,
    NativeDiscardPlan const& original, NativeReplacementSelection const& selection,
    VerifiedRandom const& verifiedRandom)
{
    static_assert(EQUIPMENT_SLOT_START == 0 && EQUIPMENT_SLOT_END == 19);
    constexpr std::uint32_t combatMask = ((1u << EQUIPMENT_SLOT_END) - 1) &
        ~((1u << EQUIPMENT_SLOT_BODY) | (1u << EQUIPMENT_SLOT_TABARD));
    if (!selection.cap || !selection.requiredMask ||
        (selection.eligibleMask & ~combatMask) ||
        (selection.requiredMask & ~selection.eligibleMask))
        return std::nullopt;
    std::uint32_t selectedMask = 0;
    for (std::uint8_t slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        auto const piece = selection.pieces[slot];
        if (!piece.entry)
        {
            if (piece.randomProperty)
                return std::nullopt;
            continue;
        }
        if (!(selection.eligibleMask & (1u << slot)))
            return std::nullopt;
        selectedMask |= 1u << slot;
    }
    if ((selectedMask & selection.requiredMask) != selection.requiredMask)
        return std::nullopt;

    auto scope = bot.AcquirePB59CallbackScope();
    if (!scope.Admitted())
        return std::nullopt;
    try
    {
        NativeReplacementBatch batch;
        batch.selectedMask = selectedMask;
        auto current = [&]()
        {
            if (!scope.Admitted() || !NativeDiscardCurrent(bot, original, verifiedRandom))
                return false;
            for (std::uint8_t slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
            {
                if (!batch.items[slot])
                    continue;
                if (!NativeReplacementItemCurrent(*batch.items[slot], selection.pieces[slot],
                        selection.cap, selection.quality))
                    return false;
                for (std::uint8_t other = 0; other < slot; ++other)
                    if (batch.items[other] && batch.items[other]->GetGUID() == batch.items[slot]->GetGUID())
                        return false;
            }
            return true;
        };
        if (!current())
            return std::nullopt;
        for (std::uint8_t slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        {
            auto const piece = selection.pieces[slot];
            if (!piece.entry)
                continue;
            batch.items[slot] = Item::CreatePB59Detached(piece.entry, piece.randomProperty);
            if (!batch.items[slot] || !current())
                return std::nullopt;
            // Real class/race/skill/reputation/template policy. This is NOT
            // CanEquipItem or whole-set legality; old gear is still equipped.
            auto const use = bot.CanUseItem(batch.items[slot].get(), true);
            if (!current() || use != EQUIP_ERR_OK)
                return std::nullopt;
        }
        if (!current())
            return std::nullopt;
        return batch;
    }
    catch (...)
    {
        return std::nullopt;
    }
}
}
