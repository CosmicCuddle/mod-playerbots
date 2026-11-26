// Cross-component interface audit: Playerbots PB68 against Core88's real executor header
// (CS01 Part 41 Task 3, coordination #80 7198695947; R1 source tuple #52 7190890604).
//
// Compile with Core88 `src/server/game/Entities/Player` and `src/common` on the include path:
//   g++ -std=c++20 -Isrc/Bot -Isrc -I<core>/src/server/game/Entities/Player -I<core>/src/common
//     tests/pb70_core88_interface_audit.cpp   (one command line)
// Source and compile evidence only: no Player, executor call or gameplay is involved.

#include <cstdint>
#include <iostream>
#include <string>
#include <type_traits>

#include "AutoGearCoreBatchRequest.h"
#include "PB59InventoryBatchPlan.h"

namespace
{
using namespace PlayerbotsAutoGearCoreBatch;
using PlayerbotsAutoGearExec::StripResult;
using Native = PB59::InventoryBatchOutcome;
using NativeFailure = PB59::InventoryBatchFailure;

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, std::string const& what)
{
    ++g_checks;
    if (!ok)
    {
        ++g_failures;
        std::cerr << "FAIL: " << what << "\n";
    }
}

// Shape of Core's result: one recovery GUID per equipment slot, a small count, an enum outcome.
static_assert(std::tuple_size_v<decltype(PB59::InventoryBatchResult{}.recoveryGuids)> ==
                  PlayerbotsAutoGearExec::EquipmentSlots,
              "Core reports at most one recovery GUID per equipment slot");
static_assert(std::is_same_v<decltype(PB59::InventoryBatchResult{}.recoveryCount), std::uint8_t>);
static_assert(std::is_enum_v<Native> && std::is_enum_v<NativeFailure>);

// Each native outcome maps by name to the same Playerbots outcome.
static_assert(OutcomeFrom(Native::Refused) == CoreBatchOutcome::Refused);
static_assert(OutcomeFrom(Native::Applied) == CoreBatchOutcome::Applied);
static_assert(OutcomeFrom(Native::Restored) == CoreBatchOutcome::Restored);
static_assert(OutcomeFrom(Native::RecoveryRequired) == CoreBatchOutcome::RecoveryRequired);

// What a result is allowed to mean, written independently of ToStripResult.
StripResult Expected(CoreBatchOutcome outcome, CoreBatchObservation const& o)
{
    if (o.missing)
        return StripResult::RecoveryRequired;
    if (outcome == CoreBatchOutcome::Refused && o.unchangedSinceStart)
        return StripResult::Refused;
    if (outcome == CoreBatchOutcome::Restored && o.unchangedSinceStart)
        return StripResult::Restored;
    if (outcome == CoreBatchOutcome::Applied && o.everyMoveAtDestination && !o.failureReported)
        return StripResult::Done;
    return StripResult::RecoveryRequired;
}

void TestBeliefMatrix()
{
    Native const natives[] = {Native::Refused, Native::Applied, Native::Restored, Native::RecoveryRequired};
    for (Native native : natives)
        for (int bits = 0; bits < 16; ++bits)
        {
            CoreBatchObservation o;
            o.unchangedSinceStart = bits & 1;
            o.everyMoveAtDestination = bits & 2;
            o.failureReported = bits & 4;
            o.missing = (bits & 8) ? 1 : 0;
            CoreBatchOutcome const outcome = OutcomeFrom(native);
            Check(ToStripResult(outcome, o) == Expected(outcome, o),
                  "belief matrix native=" + std::to_string(int(native)) + " bits=" + std::to_string(bits));
        }
}

void TestUnknownAndEmptyRecovery()
{
    // A native value outside the four agreed names is a recovery, whatever the observation says.
    auto const unknown = OutcomeFrom(static_cast<Native>(0x7F));
    CoreBatchObservation perfect;
    perfect.unchangedSinceStart = true;
    perfect.everyMoveAtDestination = true;
    Check(ToStripResult(unknown, perfect) == StripResult::RecoveryRequired, "unknown native outcome is a recovery");

    // Core documents effect-only recovery with an empty GUID list: an empty list is not proof.
    PB59::InventoryBatchResult result;
    result.outcome = Native::RecoveryRequired;
    result.failure = NativeFailure::Effects;
    result.recoveryCount = 0;
    CoreBatchObservation observed = perfect;
    observed.missing = result.recoveryCount;
    observed.failureReported = result.failure != NativeFailure::None;
    Check(ToStripResult(OutcomeFrom(result.outcome), observed) == StripResult::RecoveryRequired,
          "native RecoveryRequired with an empty GUID list stays a recovery");

    // Applied with any failure reason is not believed.
    NativeFailure const failures[] = {
        NativeFailure::Context, NativeFailure::Plan,  NativeFailure::UnsupportedState, NativeFailure::Policy,
        NativeFailure::Effects, NativeFailure::Links, NativeFailure::Restoration};
    for (NativeFailure failure : failures)
    {
        CoreBatchObservation o = perfect;
        o.failureReported = failure != NativeFailure::None;
        Check(ToStripResult(CoreBatchOutcome::Applied, o) == StripResult::RecoveryRequired,
              "Applied with failure " + std::to_string(int(failure)) + " is a recovery");
    }
}

void TestDefaultResult()
{
    // A default-constructed result reads as Refused with no failure; Playerbots still needs an unchanged
    // fingerprint to believe it, so a result Core never filled cannot pass as success.
    PB59::InventoryBatchResult result;
    Check(result.outcome == Native::Refused && result.failure == NativeFailure::None && result.recoveryCount == 0,
          "default result is Refused/None/empty");
    CoreBatchObservation changed;
    Check(ToStripResult(OutcomeFrom(result.outcome), changed) == StripResult::RecoveryRequired,
          "default result with a changed inventory is a recovery");
}
}  // namespace

int main()
{
    TestBeliefMatrix();
    TestUnknownAndEmptyRecovery();
    TestDefaultResult();
    std::cout << "checks=" << g_checks << " failures=" << g_failures << "\n";
    return g_failures == 0 ? 0 : 1;
}
