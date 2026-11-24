/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef _PLAYERBOT_AUTOGEARCOREBATCHREQUEST_H
#define _PLAYERBOT_AUTOGEARCOREBATCHREQUEST_H

// Preparation for the Core batch inventory executor (coordination #52 7130978738, Layer 2; Core PR74).
// Pure and dormant: it maps the executor's four agreed results to the strip result, and turns Playerbots'
// exact strip plan into Core's batch moves and runs Core's own structural check on them. It moves nothing,
// authorizes nothing and has no caller yet. The native call that may move items is Codex's four-outcome
// executor, still to come; Core's read-only Player::CheckPB59InventoryBatchPlan is not permission to move.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "AutoGearCappedExecution.h"

namespace PlayerbotsAutoGearCoreBatch
{
// The four results agreed for Core's batch executor (coordination #51 7118628149 and 7130718831; Grok's
// harness, #52 7131874341). Core has not published the executor's type yet, so this is Playerbots' own
// copy of the agreed names; the mapping is checked against the native type once its SHA exists.
enum class CoreBatchOutcome : std::uint8_t
{
    Refused,          // nothing was changed
    Applied,          // every move done
    Restored,         // failed, and everything is proven back where it was
    RecoveryRequired  // not proven: the listed items are not where they should be
};

// What Playerbots itself observes around the Core call (its own inventory fingerprint and slot facts).
struct CoreBatchObservation
{
    bool unchangedSinceStart = false;     // the whole inventory and context match the fingerprint taken before
    bool everyMoveAtDestination = false;  // each planned item is exactly at its planned destination
    std::size_t missing = 0;              // GUIDs Core reported as not where they should be
};

// What a Core result means for the strip. A result is believed only when Playerbots' own observation agrees
// with it (coordination #52 7133329461); anything else is a recovery.
// - Refused and Restored both mean "nothing changed", so both need the fingerprint unchanged. Restored stays
//   Restored even when no move had taken effect (Grok F5): Playerbots does not second-guess which one Core
//   proved, but never lets a changed inventory pass as either.
// - Applied needs every planned item at its destination.
// - Any result that comes with missing GUIDs, and any value outside the four, is a recovery.
// There is no fallback: no result leads to Playerbots moving items itself.
constexpr PlayerbotsAutoGearExec::StripResult ToStripResult(CoreBatchOutcome outcome,
                                                            CoreBatchObservation const& observed)
{
    using PlayerbotsAutoGearExec::StripResult;
    if (observed.missing)
        return StripResult::RecoveryRequired;
    switch (outcome)
    {
        case CoreBatchOutcome::Refused:
            return observed.unchangedSinceStart ? StripResult::Refused : StripResult::RecoveryRequired;
        case CoreBatchOutcome::Applied:
            return observed.everyMoveAtDestination ? StripResult::Done : StripResult::RecoveryRequired;
        case CoreBatchOutcome::Restored:
            return observed.unchangedSinceStart ? StripResult::Restored : StripResult::RecoveryRequired;
        case CoreBatchOutcome::RecoveryRequired:
            return StripResult::RecoveryRequired;
    }
    return StripResult::RecoveryRequired;
}

// Without Core's batch executor the capped command moves no item the bot owns (coordination #52 7130978738,
// Layer 2: no legacy public-API fallback for the capped path; 7133329461). Every route that would move one is
// gated: the strip, the factory replacing an equipped item, and a BiS swap of an equipped item. Each refuses
// that move and changes nothing. Routes that move nothing stay allowed: creating an item into an empty slot
// (factory fill, BiS into an empty slot), and a command with nothing to strip.
enum class StripPath : std::uint8_t
{
    CoreBatch,
    Unavailable
};

constexpr StripPath SelectStripPath(bool coreBatchExecutor)
{
    return coreBatchExecutor ? StripPath::CoreBatch : StripPath::Unavailable;
}

// The gate for every route that moves an item the bot already owns.
constexpr bool MayMoveOwnedItems(bool coreBatchExecutor)
{
    return SelectStripPath(coreBatchExecutor) == StripPath::CoreBatch;
}

constexpr char const* OutcomeName(CoreBatchOutcome outcome)
{
    switch (outcome)
    {
        case CoreBatchOutcome::Refused:
            return "Refused";
        case CoreBatchOutcome::Applied:
            return "Applied";
        case CoreBatchOutcome::Restored:
            return "Restored";
        case CoreBatchOutcome::RecoveryRequired:
            return "RecoveryRequired";
    }
    return "Unknown";
}

// One log line for a Core result: the outcome, how Playerbots read it, and for a recovery the item GUIDs
// that are not where they should be.
inline std::string Diagnostic(CoreBatchOutcome outcome, CoreBatchObservation observed,
                              std::span<std::uint64_t const> missing)
{
    observed.missing = missing.size();
    std::string line = std::string("core batch outcome=") + OutcomeName(outcome) +
                       " unchanged=" + (observed.unchangedSinceStart ? "1" : "0") +
                       " atDestination=" + (observed.everyMoveAtDestination ? "1" : "0") + " strip=";
    switch (ToStripResult(outcome, observed))
    {
        case PlayerbotsAutoGearExec::StripResult::Done:
            line += "Done";
            break;
        case PlayerbotsAutoGearExec::StripResult::NoSpace:
            line += "NoSpace";
            break;
        case PlayerbotsAutoGearExec::StripResult::Refused:
            line += "Refused";
            break;
        case PlayerbotsAutoGearExec::StripResult::Restored:
            line += "Restored";
            break;
        case PlayerbotsAutoGearExec::StripResult::RecoveryRequired:
            line += "RecoveryRequired";
            break;
    }
    if (!missing.empty())
    {
        line += " missing=";
        for (std::size_t i = 0; i < missing.size(); ++i)
            line += (i ? "," : "") + std::to_string(missing[i]);
    }
    return line;
}
}  // namespace PlayerbotsAutoGearCoreBatch

#if __has_include("PB59InventoryBatchPlan.h")

#include <optional>

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
