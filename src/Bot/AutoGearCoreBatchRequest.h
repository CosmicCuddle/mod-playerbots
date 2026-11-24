/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef _PLAYERBOT_AUTOGEARCOREBATCHREQUEST_H
#define _PLAYERBOT_AUTOGEARCOREBATCHREQUEST_H

// Preparation for the Core batch inventory executor (coordination #52 7130978738, Layer 2; Core PR74).
// Pure and dormant: it turns Playerbots' exact strip plan into Core's batch moves and runs Core's own
// structural check on them. It moves nothing, authorizes nothing and has no caller yet. The native call
// that may move items is Codex's four-outcome executor, still to come; Core's read-only
// Player::CheckPB59InventoryBatchPlan is not permission to move.

#if __has_include("PB59InventoryBatchPlan.h")

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "PB59InventoryBatchPlan.h"

namespace PlayerbotsAutoGearCoreBatch
{
// One move of Playerbots' plan (PlayerbotFactory::EquipmentStorageMove, in plan order: off-hand first).
struct PlannedMove
{
    std::uint64_t itemGuid = 0;
    std::uint16_t source = 0;
    std::uint16_t destination = 0;
    std::uint32_t count = 0;
    std::uint64_t destinationBag = 0;  // 0 for the backpack
};

enum class BuildError
{
    None,
    Shape,        // the observation lists do not match the plan
    Observation,  // an observation is not the planned item or empty destination
    Core          // Core's structural check refused the batch
};

struct Built
{
    BuildError error = BuildError::None;
    PB59::InventoryBatchPlanError core = PB59::InventoryBatchPlanError::None;
    std::vector<PB59::InventoryBatchMove> moves;
};

// `sources` and `destinations` are the exact slot facts captured for each planned move, in plan order
// (Player::CapturePB59InventorySlot). The batch is built only if every source holds exactly the planned
// item and count, every destination is the planned empty slot in the planned bag, and Core's own
// ValidateInventoryBatchPlan accepts the whole batch. The order is kept, so restoration stays main hand
// before off-hand.
inline Built BuildMoves(std::uint64_t owner, std::span<PlannedMove const> plan,
                        std::span<PB59::InventorySlotFacts const> sources,
                        std::span<PB59::InventorySlotFacts const> destinations)
{
    Built built;
    if (plan.empty() || sources.size() != plan.size() || destinations.size() != plan.size())
    {
        built.error = BuildError::Shape;
        return built;
    }
    for (std::size_t i = 0; i < plan.size(); ++i)
    {
        PlannedMove const& move = plan[i];
        PB59::InventorySlotFacts const& source = sources[i];
        PB59::InventorySlotFacts const& destination = destinations[i];
        if (source.position != move.source || !source.item || source.item->guid != move.itemGuid ||
            source.item->count != move.count || destination.position != move.destination ||
            destination.bag != move.destinationBag || destination.item)
        {
            built.error = BuildError::Observation;
            built.moves.clear();
            return built;
        }
        built.moves.push_back({source, destination});
    }
    built.core = PB59::ValidateInventoryBatchPlan(owner, built.moves);
    if (built.core != PB59::InventoryBatchPlanError::None)
    {
        built.error = BuildError::Core;
        built.moves.clear();
    }
    return built;
}
}  // namespace PlayerbotsAutoGearCoreBatch

#endif

#endif
