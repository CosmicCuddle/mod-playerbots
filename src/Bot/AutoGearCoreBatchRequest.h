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

// What a Core result means for the strip.
// - Refused is trusted only if Playerbots' own inventory fingerprint is unchanged too (agreed point 2):
//   otherwise something changed during Core's checks, and recovery is needed.
// - Restored stays Restored even when no move had taken effect (Grok F5): both Refused and Restored mean
//   nothing changed, and Playerbots does not second-guess which one Core proved.
// - Any value outside the four is treated as recovery.
// There is no fallback: no result leads to Playerbots moving items itself.
constexpr PlayerbotsAutoGearExec::StripResult ToStripResult(CoreBatchOutcome outcome, bool fingerprintUnchanged)
{
    using PlayerbotsAutoGearExec::StripResult;
    switch (outcome)
    {
        case CoreBatchOutcome::Refused:
            return fingerprintUnchanged ? StripResult::Refused : StripResult::RecoveryRequired;
        case CoreBatchOutcome::Applied:
            return StripResult::Done;
        case CoreBatchOutcome::Restored:
            return StripResult::Restored;
        case CoreBatchOutcome::RecoveryRequired:
            return StripResult::RecoveryRequired;
    }
    return StripResult::RecoveryRequired;
}

// Without Core's batch executor the capped command does not strip at all (coordination #52 7130978738,
// Layer 2: no legacy public-API fallback for the capped path). A missing executor is a plain refusal.
enum class StripPath : std::uint8_t
{
    CoreBatch,
    Unavailable
};

constexpr StripPath SelectStripPath(bool coreBatchExecutor)
{
    return coreBatchExecutor ? StripPath::CoreBatch : StripPath::Unavailable;
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
inline std::string Diagnostic(CoreBatchOutcome outcome, bool fingerprintUnchanged,
                              std::span<std::uint64_t const> missing)
{
    std::string line = std::string("core batch outcome=") + OutcomeName(outcome) +
                       " fingerprintUnchanged=" + (fingerprintUnchanged ? "1" : "0") + " strip=";
    switch (ToStripResult(outcome, fingerprintUnchanged))
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
