/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_SELECTION_OUTCOME_H
#define PLAYERBOTS_AUTOGEAR_SELECTION_OUTCOME_H

#include "AutoGearCommandOutcome.h"
#include <cstdint>
#include <type_traits>

namespace PlayerbotsAutoGearSelection
{
    enum class BagSpace : std::uint8_t
    {
        Enough,
        Full,
        Unknown
    };

    // A value for a later Maintenance call. This is not a function pointer
    // and it does not name a Core private insertion routine.
    enum class RecoveryReason : std::uint8_t
    {
        None,
        MoveFailed,
        ContextLost,
        InspectUnknown
    };

    struct RecoveryRequest
    {
        // The plan noticed a failure after a mutation. Not permission to undo it.
        bool needed = false;
        RecoveryReason reason = RecoveryReason::None;
        // Only the native transaction owner may set this. Selection never does.
        bool restorePermitted = false;
    };

    static_assert(std::is_trivially_copyable_v<RecoveryRequest>);
    static_assert(std::is_standard_layout_v<RecoveryRequest>);

    enum class Disposition : std::uint8_t
    {
        Commit,
        GenuineNoSet,
        TechnicalFailure,
        Preserve
    };

    enum class GearAction : std::uint8_t
    {
        CommitSet,
        StripToBags,
        Preserve,
        Restore
    };

    struct Outcome
    {
        Disposition disposition = Disposition::Preserve;
        GearAction action = GearAction::Preserve;
        RecoveryRequest recovery{};
    };

    // Planning only. restorePermitted stays false. StripToBags moves combat
    // gear into empty storage. Body and tabard stay. Nothing is deleted.
    constexpr Outcome MapOutcome(bool authorityValid, PlayerbotsAutoGearCommand::PreparationResult prepared,
        bool searchComplete, BagSpace bags)
    {
        using PlayerbotsAutoGearCommand::ResolveNextStep;
        auto const step = ResolveNextStep(authorityValid, prepared, searchComplete);
        if (step == PlayerbotsAutoGearCommand::NextStep::CommitPreparedSet)
        {
            if (bags != BagSpace::Enough)
                return {Disposition::TechnicalFailure, GearAction::Preserve, {}};
            return {Disposition::Commit, GearAction::CommitSet, {}};
        }
        if (step == PlayerbotsAutoGearCommand::NextStep::RequestStrip)
        {
            if (bags == BagSpace::Enough)
                return {Disposition::GenuineNoSet, GearAction::StripToBags, {}};
            return {Disposition::TechnicalFailure, GearAction::Preserve, {}};
        }
        return {};
    }

    // Records that recovery is needed. It does not permit restore.
    constexpr RecoveryRequest NoteRecoveryNeeded(RecoveryReason reason)
    {
        if (reason == RecoveryReason::None)
            return {};
        return {true, reason, false};
    }

    // The native transaction owner is the only caller that may permit restore.
    constexpr RecoveryRequest PermitRestore(RecoveryRequest const& needed)
    {
        if (!needed.needed || needed.reason == RecoveryReason::None)
            return needed;
        RecoveryRequest granted = needed;
        granted.restorePermitted = true;
        return granted;
    }
}

#endif
