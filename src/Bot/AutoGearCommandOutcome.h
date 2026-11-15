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

    // Planning only. Mutation still needs owner-thread execution and authority
    // held valid through commit; RequestStrip never authorizes deletion itself.
    constexpr NextStep ResolveNextStep(bool authorityValid, PreparationResult result)
    {
        if (!authorityValid)
            return NextStep::PreserveGear;
        switch (result)
        {
            case PreparationResult::SuitableSet:
                return NextStep::CommitPreparedSet;
            case PreparationResult::NoSuitableSet:
                return NextStep::RequestStrip;
            default:
                return NextStep::PreserveGear;
        }
    }
}

#endif
