#include "AutoGearSelectionCap.h"
#include <array>
#include <iostream>

using namespace PlayerbotsAutoGearSelection;

namespace
{
int g_checks = 0;

bool Check(bool condition, char const* text)
{
    ++g_checks;
    if (!condition)
    {
        std::cerr << "FAIL " << text << "\n";
        return false;
    }
    return true;
}

// Stages 2 to 7 stay unset. These zeros are placeholders, not proposed caps.
constexpr std::array<std::uint32_t, 19> kApproved{{66, 78, 0, 0, 0, 0, 0, 0}};
constexpr std::array<std::uint32_t, 3> kFloors{{0, 0, 0}};
constexpr std::array<std::uint32_t, 3> kCeiling{{78, 0, 0}};

constexpr PlayerbotsAutoGearCap::Policy ApprovedPolicy()
{
    PlayerbotsAutoGearCap::Policy policy;
    policy.completionCaps = kApproved;
    policy.eraEntryFloors = kFloors;
    policy.eraCeilings = kCeiling;
    return policy;
}

constexpr CapInput Input(std::uint8_t stage, CapMode mode)
{
    CapInput in;
    in.playerContext = true;
    in.progressionContext = true;
    in.mode = mode;
    in.rawStage = stage;
    in.effectiveStage = stage;
    in.policy = ApprovedPolicy();
    return in;
}
}

int main()
{
    bool ok = true;
    CapInput endgame = Input(0, CapMode::Endgame);
    ok = Check(ComposeCap(endgame).cap == 66 && !ComposeCap(endgame).preserve, "approved stage 0 is 66") && ok;
    endgame.rawStage = 1;
    endgame.effectiveStage = 1;
    ok = Check(ComposeCap(endgame).cap == 78, "approved stage 1 is 78") && ok;
    for (std::uint8_t stage = 2; stage <= 7; ++stage)
    {
        CapInput unset = Input(stage, CapMode::Endgame);
        ok = Check(ComposeCap(unset).preserve && kApproved[stage] == 0, "stages 2 to 7 stay unset placeholders") && ok;
    }

    CapInput leveling = Input(0, CapMode::Leveling);
    leveling.senderSampleKnown = true;
    leveling.senderAverage = 40;
    ok = Check(ComposeCap(leveling).cap == 40, "leveling uses the supplied sender average") && ok;
    leveling.senderSampleKnown = false;
    ok = Check(ComposeCap(leveling).preserve, "leveling without a sample preserves") && ok;
    CapInput levelIgnored = Input(0, CapMode::Endgame);
    levelIgnored.senderSampleKnown = true;
    levelIgnored.senderAverage = 40;
    ok = Check(ComposeCap(levelIgnored).cap == 66, "endgame does not switch on from level or the average") && ok;

    CapInput match = Input(0, CapMode::Match);
    ok = Check(ComposeCap(match).preserve, "match without an explicit sample preserves") && ok;
    match.matchSampleKnown = true;
    match.matchAverage = 50;
    ok = Check(ComposeCap(match).cap == 50, "match uses only its own supplied target") && ok;

    // The recorded proposal is not consulted by this matrix.
    ok = Check(kApproved[2] == 0 && ProposedVanillaCompletionAwaitingOwnerReview[2] != 0,
            "the proposal is not the placeholder table") &&
         ok;

    if (!ok)
        return 1;
    std::cout << "checks=" << g_checks << "\n";
    return 0;
}
