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
        Restore
    };

    struct Outcome
    {
        Disposition disposition = Disposition::Preserve;
        GearAction action = GearAction::Preserve;
        RecoveryRequest recovery{};
    };

    // StripToBags moves combat gear into empty storage. Body and tabard stay
    // equipped. There is no deletion action. Full or unknown bags are a
    // technical failure and preserve. A recovery reason is recorded only
    // after a mutation has already started. Pure selection passes false.
    constexpr Outcome MapOutcome(bool authorityValid, PlayerbotsAutoGearCommand::PreparationResult prepared,
        bool searchComplete, BagSpace bags, RecoveryReason technical, bool mutationStarted)
    {
        using PlayerbotsAutoGearCommand::ResolveNextStep;
        if (technical != RecoveryReason::None)
        {
            bool const moved = technical == RecoveryReason::MoveFailed || technical == RecoveryReason::ContextLost;
            bool const mayRestore = mutationStarted && moved;
            if (!mayRestore)
                return {Disposition::TechnicalFailure, GearAction::Preserve, {}};
            return {Disposition::TechnicalFailure, GearAction::Restore, RecoveryRequest{technical}};
        }

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
}

#endif
