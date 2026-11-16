#include "AutoGearSelectionOutcome.h"
#include <cassert>
#include <iostream>
using namespace PlayerbotsAutoGearSelection;
using PlayerbotsAutoGearCommand::PreparationResult;
int main()
{
    unsigned cases = 0;
    for (bool authority : {false, true})
        for (bool complete : {false, true})
            for (bool discard : {false, true})
                for (auto bags : {BagSpace::Enough, BagSpace::Full, BagSpace::Unknown})
                    for (auto prepared : {PreparationResult::NotPrepared, PreparationResult::SuitableSet,
                            PreparationResult::NoSuitableSet, PreparationResult::InvalidPolicy,
                            PreparationResult::InfrastructureFailure})
                    {
                        auto result = MapOutcome(authority, prepared, complete, bags, discard);
                        GearAction expected = GearAction::Preserve;
                        if (authority && prepared == PreparationResult::SuitableSet &&
                            (discard ? complete : bags == BagSpace::Enough))
                            expected = GearAction::CommitSet;
                        if (authority && prepared == PreparationResult::NoSuitableSet && complete)
                        {
                            if (discard)
                                expected = GearAction::StripDiscard;
                            else if (bags == BagSpace::Enough)
                                expected = GearAction::StripToBags;
                        }
                        assert(result.action == expected && !result.recovery.needed);
                        ++cases;
                    }
    std::cout << cases << " outcome cases PASS\n";
}
