#include "AutoGearCommandOutcome.h"
#include <array>

using namespace PlayerbotsAutoGearCommand;

constexpr std::array<PreparationResult, 5> results = {
    PreparationResult::NotPrepared, PreparationResult::SuitableSet, PreparationResult::NoSuitableSet,
    PreparationResult::InvalidPolicy, PreparationResult::InfrastructureFailure
};

constexpr bool InvalidAuthorityPreservesGear()
{
    for (auto result : results)
        if (ResolveNextStep(false, result) != NextStep::PreserveGear ||
            ResolveNextStep(false, result, true) != NextStep::PreserveGear)
            return false;
    return true;
}

static_assert(InvalidAuthorityPreservesGear());
static_assert(ResolveNextStep(true, PreparationResult::SuitableSet) == NextStep::CommitPreparedSet);
static_assert(ResolveNextStep(true, PreparationResult::NoSuitableSet) == NextStep::PreserveGear);
static_assert(ResolveNextStep(true, PreparationResult::NoSuitableSet, true) == NextStep::RequestStrip);
static_assert(ResolveNextStep(true, PreparationResult::NotPrepared) == NextStep::PreserveGear);
static_assert(ResolveNextStep(true, PreparationResult::InvalidPolicy) == NextStep::PreserveGear);
static_assert(ResolveNextStep(true, PreparationResult::InfrastructureFailure) == NextStep::PreserveGear);
static_assert(ResolveNextStep(true, PreparationResult::InfrastructureFailure, true) == NextStep::PreserveGear);
static_assert(ResolveNextStep(true, PreparationResult::InvalidPolicy, true) == NextStep::PreserveGear);
static_assert(ResolveNextStep(true, static_cast<PreparationResult>(255)) == NextStep::PreserveGear);

int main() { return 0; }
