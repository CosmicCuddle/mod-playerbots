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

    // Diagnostic only. Nothing here can permit a restore.
    struct RecoveryRequest
    {
        bool needed = false;
        RecoveryReason reason = RecoveryReason::None;
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
        Restore,
        StripDiscard
    };

    struct Outcome
    {
        Disposition disposition = Disposition::Preserve;
        GearAction action = GearAction::Preserve;
        RecoveryRequest recovery{};
    };

    // Planning only. Discard is an explicit RNDBOT-specific intent, never
    // deletion authority. Body and tabard stay; no item is mutated here.
    constexpr Outcome MapOutcome(bool authorityValid, PlayerbotsAutoGearCommand::PreparationResult prepared,
        bool searchComplete, BagSpace bags, bool randomDiscardPrepared = false)
    {
        using PlayerbotsAutoGearCommand::ResolveNextStep;
        auto const step = ResolveNextStep(authorityValid, prepared, searchComplete);
        if (step == PlayerbotsAutoGearCommand::NextStep::CommitPreparedSet)
        {
            if (randomDiscardPrepared ? !searchComplete : bags != BagSpace::Enough)
                return {Disposition::TechnicalFailure, GearAction::Preserve, {}};
            return {Disposition::Commit, GearAction::CommitSet, {}};
        }
        if (step == PlayerbotsAutoGearCommand::NextStep::RequestStrip)
        {
            if (randomDiscardPrepared)
                return {Disposition::GenuineNoSet, GearAction::StripDiscard, {}};
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
        return {true, reason};
    }
}

#endif
