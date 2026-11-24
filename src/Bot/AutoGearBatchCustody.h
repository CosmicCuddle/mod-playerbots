#pragma once

#include "PB59InventoryBatchPlan.h"
#include <algorithm>
#include <span>

namespace PlayerbotsAutoGearCoreBatch
{
// Only custody in the observed carried inventory. Effects and mutable item
// metadata are deliberately not compared; bank/mail and native lifetime are
// outside this value-only check. The caller must establish owner context first.
inline bool SameCustody(PB59::InventoryInstanceFacts const& a,
                        PB59::InventoryInstanceFacts const& b)
{
    return a.guid == b.guid && a.owner == b.owner && a.entry == b.entry && a.count == b.count;
}

inline bool AppliedCustodyPreserved(std::uint64_t owner,
    std::span<PB59::InventorySlotFacts const> before,
    std::span<PB59::InventorySlotFacts const> after,
    std::span<PB59::InventoryBatchMove const> moves)
{
    if (before.empty() || before.size() != after.size() ||
        PB59::ValidateInventoryBatchPlan(owner, moves) != PB59::InventoryBatchPlanError::None)
        return false;
    auto valid = [owner](auto slots)
    {
        for (std::size_t i = 0; i < slots.size(); ++i)
        {
            auto const& s = slots[i];
            if (s.owner != owner || !s.position ||
                (s.item && (!s.item->guid || s.item->owner != owner || !s.item->entry || !s.item->count)))
                return false;
            for (std::size_t j = 0; j < i; ++j)
                if (s.position == slots[j].position ||
                    (s.item && slots[j].item && s.item->guid == slots[j].item->guid))
                    return false;
        }
        return true;
    };
    if (!valid(before) || !valid(after))
        return false;
    auto find = [](auto slots, std::uint16_t pos) -> PB59::InventorySlotFacts const*
    {
        auto it = std::find_if(slots.begin(), slots.end(), [pos](auto const& s) { return s.position == pos; });
        return it == slots.end() ? nullptr : &*it;
    };
    for (auto const& move : moves)
    {
        auto const* source = find(before, move.source.position);
        auto const* dest = find(before, move.destination.position);
        if (!source || !dest || !source->item || dest->item || source->bag != move.source.bag ||
            dest->bag != move.destination.bag || !SameCustody(*source->item, *move.source.item))
            return false;
    }
    for (auto const& old : before)
    {
        auto const* now = find(after, old.position);
        if (!now || now->bag != old.bag)
            return false;
        auto const* expected = &old;
        for (auto const& move : moves)
        {
            if (old.position == move.source.position)
                expected = &move.destination;
            else if (old.position == move.destination.position)
                expected = &move.source;
        }
        if (expected->item.has_value() != now->item.has_value() ||
            (expected->item && !SameCustody(*expected->item, *now->item)))
            return false;
    }
    return true;
}
}
