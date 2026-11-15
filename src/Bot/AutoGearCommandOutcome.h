/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_COMMAND_OUTCOME_H
#define PLAYERBOTS_AUTOGEAR_COMMAND_OUTCOME_H

namespace PlayerbotsAutoGearCommand
{
    enum class PreparationResult
    {
        NotPrepared,
        SuitableSet,
        NoSuitableSet,
        InvalidPolicy,
        InfrastructureFailure
    };

    enum class NextStep
    {
        PreserveGear,
        CommitPreparedSet,
        RequestStrip
    };

    // Planning only. Final matching Known B authorizes one bounded synchronous
    // bot-owner-thread command; later C is diagnostic, not a lease or correction.
    // RequestStrip never authorizes deletion itself.
    constexpr NextStep ResolveNextStep(bool authorityValid, PreparationResult result, bool searchComplete = false)
    {
        if (!authorityValid)
            return NextStep::PreserveGear;
        switch (result)
        {
            case PreparationResult::SuitableSet:
                return NextStep::CommitPreparedSet;
            case PreparationResult::NoSuitableSet:
                return searchComplete ? NextStep::RequestStrip : NextStep::PreserveGear;
            default:
                return NextStep::PreserveGear;
        }
    }
}

#endif
