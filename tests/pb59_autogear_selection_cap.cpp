#include "AutoGearCommandEligibility.h"
#include "AutoGearRndbotTierSync.h"
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

constexpr auto kStage01 = std::to_array(ApprovedVanillaCompletion);
constexpr std::array<std::uint32_t, 3> kFloors{{0, 0, 0}};
constexpr std::array<std::uint32_t, 3> kCeiling78{{78, 0, 0}};
constexpr std::array<std::uint32_t, 3> kCeiling92{{92, 0, 0}};

constexpr PlayerbotsAutoGearCap::Policy PolicyFor(std::span<std::uint32_t const> caps, std::uint32_t vanillaCeiling)
{
    PlayerbotsAutoGearCap::Policy policy;
    policy.completionCaps = caps;
    policy.eraEntryFloors = kFloors;
    policy.eraCeilings = vanillaCeiling == 92 ? kCeiling92 : kCeiling78;
    return policy;
}

constexpr CapInput Base(std::uint8_t stage, std::uint32_t sample)
{
    CapInput in;
    in.playerContext = true;
    in.progressionContext = true;
    in.mode = CapMode::Leveling;
    in.rawStage = stage;
    in.effectiveStage = stage;
    in.policy = PolicyFor(kStage01, 92);
    in.senderSampleKnown = true;
    in.senderAverage = sample;
    return in;
}

constexpr std::array<CommandForm, 7> kForms{{CommandForm::Plain, CommandForm::Reset, CommandForm::Match,
    CommandForm::Quality, CommandForm::Numeric, CommandForm::Bis, CommandForm::BisNumeric}};
}

static_assert(ApprovedVanillaCompletion[0] == 66);
static_assert(ApprovedVanillaCompletion[1] == 78);
static_assert(ApprovedVanillaCompletion[2] == 78);
static_assert(ApprovedVanillaCompletion[3] == 83);
static_assert(ApprovedVanillaCompletion[6] == 88);
static_assert(ApprovedVanillaCompletion[7] == 92);
static_assert(ApprovedVanillaCompletion[8] == 125);
static_assert(ApprovedVanillaCompletion[11] == 0);
static_assert(ApprovedVanillaEraCeiling() == 92);
static_assert(ComposeCap(Base(0, 100)).cap == 66);
static_assert(!ComposeCap(Base(0, 100)).preserve);
static_assert(ComposeCap(Base(1, 100)).cap == 78);
static_assert(ComposeCap(Base(0, 65)).cap == 65);
static_assert(ComposeCap(Base(1, 77)).cap == 77);
static_assert(ComposeCap(Base(2, 100)).cap == 78);
static_assert(ComposeCap(Base(7, 100)).cap == 92);

int main()
{
    bool ok = true;
    ok = Check(ComposeCap(Base(0, 100)).cap == 66, "stage 0") && ok;
    ok = Check(ComposeCap(Base(1, 200)).cap == 78, "stage 1") && ok;
    std::uint32_t const approvedCaps[] = {78, 83, 83, 83, 88, 92};
    for (std::uint8_t stage = 2; stage <= 7; ++stage)
        ok = Check(ComposeCap(Base(stage, 300)).cap == approvedCaps[stage - 2], "approved stage cap") && ok;

    std::array<std::uint32_t, 19> proposed{};
    for (std::size_t i = 0; i < 19; ++i)
        proposed[i] = ApprovedVanillaCompletion[i];
    CapInput stage7 = Base(7, 200);
    stage7.policy = PolicyFor(proposed, 92);
    stage7.mode = CapMode::Endgame;
    stage7.senderSampleKnown = false;
    ok = Check(ComposeCap(stage7).cap == 92, "approved stage 7 is 92") && ok;
    stage7.policy = PolicyFor(proposed, 78);
    ok = Check(ComposeCap(stage7).cap == 78, "era ceiling still clamps the proposal") && ok;

    CapInput stage3 = Base(3, 200);
    stage3.policy = PolicyFor(proposed, 92);
    stage3.mode = CapMode::Endgame;
    stage3.senderSampleKnown = false;
    ok = Check(ComposeCap(stage3).cap == 83, "proposed stage 3") && ok;
    stage3.scoreLimit = 80;
    ok = Check(ComposeCap(stage3).cap == 80, "score limit tighter than stage") && ok;
    stage3.typedItemLevelPresent = true;
    stage3.typedItemLevel = 70;
    ok = Check(ComposeCap(stage3).cap == 70, "typed tighter than score limit") && ok;

    CapInput leveling = Base(1, 40);
    ok = Check(ComposeCap(leveling).cap == 40, "leveling uses the sender average") && ok;
    leveling.senderSampleKnown = false;
    ok = Check(ComposeCap(leveling).preserve, "leveling without a sample preserves") && ok;
    CapInput endgame = Base(1, 40);
    endgame.mode = CapMode::Endgame;
    endgame.senderSampleKnown = false;
    ok = Check(ComposeCap(endgame).cap == 78, "endgame does not require the average") && ok;
    CapInput match = Base(1, 40);
    match.mode = CapMode::Match;
    match.senderSampleKnown = false;
    ok = Check(ComposeCap(match).preserve, "match without its own sample preserves") && ok;
    match.matchSampleKnown = true;
    match.matchAverage = 40;
    ok = Check(ComposeCap(match).cap == 40, "explicit match keeps its average") && ok;

    CapInput unresolved = Base(8, 100);
    unresolved.mode = CapMode::Endgame;
    unresolved.scoreLimit = 50;
    ok = Check(ComposeCap(unresolved).preserve, "score limit does not replace an unset stage") && ok;

    CapInput missingPlayer = Base(0, 100);
    missingPlayer.playerContext = false;
    ok = Check(ComposeCap(missingPlayer).preserve && ComposeCap(missingPlayer).cap == 0, "missing player") && ok;
    CapInput missingProgress = Base(0, 100);
    missingProgress.progressionContext = false;
    ok = Check(ComposeCap(missingProgress).preserve, "missing progression") && ok;
    CapInput zeroSample = Base(0, 0);
    ok = Check(ComposeCap(zeroSample).preserve, "zero sample is not unlimited") && ok;
    CapInput zeroTyped = Base(0, 100);
    zeroTyped.typedItemLevelPresent = true;
    zeroTyped.typedItemLevel = 0;
    ok = Check(ComposeCap(zeroTyped).preserve, "typed zero is not unlimited") && ok;
    CapInput later = Base(0, 100);
    later.rawStage = 8;
    later.effectiveStage = 8;
    later.mode = CapMode::Endgame;
    later.policy = PolicyFor(proposed, 92);
    ok = Check(ComposeCap(later).preserve, "stage 8 stays unresolved") && ok;
    CapInput mismatched = Base(1, 100);
    mismatched.effectiveStage = 2;
    ok = Check(ComposeCap(mismatched).preserve, "effective above raw") && ok;

    CapInput shared = Base(0, 100);
    CapResult const plain = ResolveCommandEligibility(CommandForm::Plain, shared);
    for (CommandForm form : kForms)
    {
        CapResult const got = ResolveCommandEligibility(form, shared);
        if (form == CommandForm::Match)
            ok = Check(got.preserve, "match form rejects a leveling mode") && ok;
        else
            ok = Check(got.preserve == plain.preserve && got.cap == plain.cap, "non-match forms share the mode") && ok;
    }
    CapInput badMode = shared;
    badMode.mode = static_cast<CapMode>(9);
    ok = Check(ResolveCommandEligibility(CommandForm::Plain, badMode).preserve, "unknown mode preserves") && ok;
    CapInput numeric = Base(1, 100);
    numeric.typedItemLevelPresent = true;
    numeric.typedItemLevel = 50;
    CapResult const numericCap = ResolveCommandEligibility(CommandForm::Numeric, numeric);
    CapResult const bisNumeric = ResolveCommandEligibility(CommandForm::BisNumeric, numeric);
    ok = Check(numericCap.cap == 50 && bisNumeric.cap == numericCap.cap, "numeric and bis numeric match") && ok;
    ok = Check(ResolveCommandEligibility(CommandForm::Plain, Base(1, 100)).cap == 78, "plain has no typed cap") && ok;
    ok = Check(ModeForForm(CommandForm::Match, true) == CapMode::Match, "match mode ignores leveling") && ok;
    ok = Check(ModeForForm(CommandForm::Plain, true) == CapMode::Leveling, "plain leveling") && ok;
    ok = Check(ModeForForm(CommandForm::Bis, false) == CapMode::Endgame, "bis endgame") && ok;
    ok = Check(ModeForForm(CommandForm::Reset, false) == CapMode::Endgame, "reset endgame") && ok;
    ok = Check(ModeForForm(CommandForm::Plain, false, true) == CapMode::Leveling, "plain keeps the average") && ok;
    ok = Check(ModeForForm(CommandForm::Reset, false, true) == CapMode::Leveling, "reset keeps the average") && ok;
    ok = Check(ModeForForm(CommandForm::Quality, false, true) == CapMode::Leveling, "quality keeps the average") && ok;
    ok = Check(ModeForForm(CommandForm::Numeric, false, true) == CapMode::Leveling, "numeric keeps the average") && ok;
    ok = Check(ModeForForm(CommandForm::Bis, false, true) == CapMode::Endgame, "bis uses the stage cap") && ok;
    CapMode const bisNumber = ModeForForm(CommandForm::BisNumeric, false, true);
    ok = Check(bisNumber == CapMode::Endgame, "bis numeric uses the stage cap") && ok;
    CapMode const matchAfter = ModeForForm(CommandForm::Match, false, true);
    ok = Check(matchAfter == CapMode::Match, "match is unchanged after leveling") && ok;
    CapInput postLevel = Base(1, 50);
    postLevel.mode = ModeForForm(CommandForm::Plain, false, true);
    CapInput postBis = postLevel;
    postBis.mode = ModeForForm(CommandForm::Bis, false, true);
    postBis.senderSampleKnown = false;
    ok = Check(ComposeCap(postLevel).cap == 50 && ComposeCap(postBis).cap == 78,
            "after leveling, normal matches the average and bis uses the stage") &&
         ok;
    ok = Check(!KnownCommandForm(static_cast<CommandForm>(255)) &&
               ResolveCommandEligibility(static_cast<CommandForm>(255), shared).preserve,
            "unknown form preserves") &&
         ok;

    ok = Check(AllowsPopulationTierSync(Population::RandomBot), "rndbot allowed") && ok;
    ok = Check(!AllowsPopulationTierSync(Population::AltBot), "altbot denied") && ok;
    ok = Check(!AllowsPopulationTierSync(Population::Unknown), "unknown denied") && ok;
    ok = Check(!AllowsPopulationTierSync(Population::AddClass), "addclass denied") && ok;

    if (!ok)
        return 1;
    std::cout << "checks=" << g_checks << "\n";
    return 0;
}
