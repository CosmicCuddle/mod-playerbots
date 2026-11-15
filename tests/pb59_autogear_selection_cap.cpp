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

constexpr std::array<std::uint32_t, 19> kDefault{{66, 78}};

constexpr CapInput Base(std::uint8_t stage, std::uint32_t sample)
{
    CapInput in;
    in.playerContext = true;
    in.progressionContext = true;
    in.rawStage = stage;
    in.effectiveStage = stage;
    in.configuredCompletion = kDefault;
    in.senderSampleKnown = true;
    in.senderAverage = sample;
    return in;
}

constexpr std::array<CommandForm, 7> kForms{{CommandForm::Plain, CommandForm::Reset, CommandForm::Match,
    CommandForm::Quality, CommandForm::Numeric, CommandForm::Bis, CommandForm::BisNumeric}};
}

static_assert(ComposeCap(Base(0, 100)).cap == ApprovedStage0Cap);
static_assert(!ComposeCap(Base(0, 100)).preserve);
static_assert(ComposeCap(Base(1, 100)).cap == ApprovedStage1Cap);
static_assert(ComposeCap(Base(0, 65)).cap == 65);
static_assert(ComposeCap(Base(1, 77)).cap == 77);
static_assert(ComposeCap(Base(2, 100)).preserve);
static_assert(ComposeCap(Base(7, 100)).preserve);

int main()
{
    bool ok = true;
    ok = Check(ComposeCap(Base(0, 100)).cap == 66, "stage 0") && ok;
    ok = Check(ComposeCap(Base(1, 200)).cap == 78, "stage 1") && ok;
    for (std::uint8_t stage = 2; stage <= 7; ++stage)
        ok = Check(ComposeCap(Base(stage, 300)).preserve, "unresolved stage preserves") && ok;

    std::array<std::uint32_t, 19> configured = kDefault;
    configured[3] = 83;
    configured[7] = 92;
    CapInput stage3 = Base(3, 200);
    stage3.configuredCompletion = configured;
    ok = Check(ComposeCap(stage3).cap == 83, "configured stage 3") && ok;
    stage3.scoreLimit = 80;
    ok = Check(ComposeCap(stage3).cap == 80, "score limit tighter than stage") && ok;
    stage3.typedItemLevelPresent = true;
    stage3.typedItemLevel = 70;
    ok = Check(ComposeCap(stage3).cap == 70, "typed tighter than score limit") && ok;
    stage3.senderAverage = 60;
    ok = Check(ComposeCap(stage3).cap == 60, "sample tighter than typed") && ok;

    CapInput raised = Base(0, 200);
    std::array<std::uint32_t, 19> higher = kDefault;
    higher[0] = 90;
    raised.configuredCompletion = higher;
    ok = Check(ComposeCap(raised).cap == 66, "config cannot raise stage 0") && ok;
    higher[0] = 50;
    raised.configuredCompletion = higher;
    ok = Check(ComposeCap(raised).cap == 50, "lower config wins") && ok;

    CapInput unresolved = Base(2, 100);
    unresolved.scoreLimit = 50;
    ok = Check(ComposeCap(unresolved).preserve, "score limit does not replace an unresolved stage") && ok;

    CapInput missingPlayer = Base(0, 100);
    missingPlayer.playerContext = false;
    ok = Check(ComposeCap(missingPlayer).preserve && ComposeCap(missingPlayer).cap == 0, "missing player") && ok;
    CapInput missingProgress = Base(0, 100);
    missingProgress.progressionContext = false;
    ok = Check(ComposeCap(missingProgress).preserve, "missing progression") && ok;
    CapInput noSample = Base(0, 100);
    noSample.senderSampleKnown = false;
    ok = Check(ComposeCap(noSample).preserve, "missing sample") && ok;
    CapInput zeroSample = Base(0, 0);
    ok = Check(ComposeCap(zeroSample).preserve, "zero sample is not unlimited") && ok;
    CapInput zeroTyped = Base(0, 100);
    zeroTyped.typedItemLevelPresent = true;
    zeroTyped.typedItemLevel = 0;
    ok = Check(ComposeCap(zeroTyped).preserve, "typed zero is not unlimited") && ok;
    CapInput later = Base(0, 100);
    later.rawStage = 8;
    later.effectiveStage = 8;
    std::array<std::uint32_t, 19> tbc = kDefault;
    tbc[8] = 200;
    later.configuredCompletion = tbc;
    ok = Check(ComposeCap(later).preserve, "stage 8 stays unresolved") && ok;
    CapInput mismatched = Base(1, 100);
    mismatched.effectiveStage = 2;
    ok = Check(ComposeCap(mismatched).preserve, "effective above raw") && ok;

    CapInput shared = Base(0, 100);
    CapResult const plain = ResolveCommandEligibility(CommandForm::Plain, shared);
    for (CommandForm form : kForms)
    {
        CapResult const got = ResolveCommandEligibility(form, shared);
        ok = Check(KnownCommandForm(form) && got.preserve == plain.preserve && got.cap == plain.cap,
                "forms share eligibility") &&
             ok;
    }
    CapInput numeric = Base(1, 100);
    numeric.typedItemLevelPresent = true;
    numeric.typedItemLevel = 50;
    CapResult const numericCap = ResolveCommandEligibility(CommandForm::Numeric, numeric);
    CapResult const bisNumeric = ResolveCommandEligibility(CommandForm::BisNumeric, numeric);
    ok = Check(numericCap.cap == 50 && bisNumeric.cap == numericCap.cap, "numeric and bis numeric match") && ok;
    ok = Check(ResolveCommandEligibility(CommandForm::Plain, Base(1, 100)).cap == 78, "plain has no typed cap") && ok;
    ok = Check(ResolveCommandEligibility(CommandForm::Reset, Base(1, 100)).cap ==
                   ResolveCommandEligibility(CommandForm::Match, Base(1, 100)).cap &&
               ResolveCommandEligibility(CommandForm::Quality, Base(0, 90)).cap ==
                   ResolveCommandEligibility(CommandForm::Bis, Base(0, 90)).cap,
            "reset match quality and bis stay on the composer") &&
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
