#pragma once

#include "PB59InventorySlotFacts.h"
#include "Player.h"
#include "WorldSession.h"
#include <array>
#include <optional>

namespace PlayerbotsAutoGearStorage
{
// Exact no-bag preparation for verified RNDBOTs. Owns values only. The
// population callback must re-resolve current random-account and bot identity
// on the serialized world thread. It may not authorize an Altbot or human.
struct NativeDiscardPlan
{
    std::uint64_t ownerIncarnation = 0;
    std::uint64_t sessionIncarnation = 0;
    std::array<PB59::InventorySlotFacts, 17> equipment{};
};

inline constexpr bool DiscardCombatSlot(std::uint8_t slot)
{
    return slot < EQUIPMENT_SLOT_END && slot != EQUIPMENT_SLOT_BODY &&
        slot != EQUIPMENT_SLOT_TABARD;
}

inline std::uint16_t DiscardPosition(std::uint8_t slot)
{
    return (std::uint16_t(INVENTORY_SLOT_BAG_0) << 8) | slot;
}

template<class VerifiedRandom>
bool NativeDiscardCurrent(Player const& bot, NativeDiscardPlan const& plan,
    VerifiedRandom const& verifiedRandom)
{
    auto* session = bot.GetSession();
    if (!plan.ownerIncarnation || !plan.sessionIncarnation || !session ||
        bot.GetPB59OwnerIncarnation() != plan.ownerIncarnation ||
        session->GetPB59SessionIncarnation() != plan.sessionIncarnation ||
        !verifiedRandom(bot))
        return false;
    std::size_t index = 0;
    for (std::uint8_t slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        if (!DiscardCombatSlot(slot))
            continue;
        auto const& expected = plan.equipment[index++];
        if (expected.position != DiscardPosition(slot) || expected.bag || !expected.owner)
            return false;
        auto current = bot.CapturePB59InventorySlot(expected.position);
        if (!current || *current != expected)
            return false;
    }
    return index == plan.equipment.size();
}

// This is a preparation, not an admission ticket or permission to delete.
// No storage-space query, destruction, item creation, or stat mutation.
template<class VerifiedRandom>
std::optional<NativeDiscardPlan> PrepareNativeRandomDiscard(Player& bot,
    VerifiedRandom const& verifiedRandom)
{
    static_assert(EQUIPMENT_SLOT_START == 0 && EQUIPMENT_SLOT_END == 19);
    auto scope = bot.AcquirePB59CallbackScope();
    if (!scope.Admitted())
        return std::nullopt;
    try
    {
        if (!verifiedRandom(bot))
            return std::nullopt;
        auto* session = bot.GetSession();
        if (!session)
            return std::nullopt;
        NativeDiscardPlan plan;
        plan.ownerIncarnation = bot.GetPB59OwnerIncarnation();
        plan.sessionIncarnation = session->GetPB59SessionIncarnation();
        std::size_t index = 0;
        for (std::uint8_t slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        {
            if (!DiscardCombatSlot(slot))
                continue;
            auto facts = bot.CapturePB59InventorySlot(DiscardPosition(slot));
            if (!facts || facts->bag || !facts->owner)
                return std::nullopt;
            plan.equipment[index++] = std::move(*facts);
        }
        auto current = [&]()
        {
            return scope.Admitted() && NativeDiscardCurrent(bot, plan, verifiedRandom);
        };
        if (!current())
            return std::nullopt;
        for (auto const& source : plan.equipment)
        {
            if (!source.item)
                continue;
            auto const result = bot.CanUnequipItem(source.position, false);
            if (!current() || result != EQUIP_ERR_OK)
                return std::nullopt;
        }
        if (!current())
            return std::nullopt;
        return plan;
    }
    catch (...)
    {
        return std::nullopt;
    }
}
}
