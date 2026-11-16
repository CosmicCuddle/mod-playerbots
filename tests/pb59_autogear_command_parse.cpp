#include "AutoGearCommandParse.h"
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

constexpr std::array<std::uint32_t, 19> kStage01{{66, 78}};
constexpr std::array<std::uint32_t, 3> kFloors{};
constexpr std::array<std::uint32_t, 3> kCeil{{78, 0, 0}};

CapInput Stage0()
{
    CapInput in;
    in.playerContext = true;
    in.progressionContext = true;
    in.mode = CapMode::Endgame;
    in.rawStage = 0;
    in.effectiveStage = 0;
    in.policy.completionCaps = kStage01;
    in.policy.eraEntryFloors = kFloors;
    in.policy.eraCeilings = kCeil;
    in.senderSampleKnown = true;
    in.senderAverage = 100;
    return in;
}

ParseRequest Base(bool featureOn, bool bis, char const* text)
{
    ParseRequest request;
    request.featureOn = featureOn;
    request.bis = bis;
    request.text = text;
    request.qualityLimit = 3;
    request.scoreLimit = 0;
    request.requestedMode = CapMode::Endgame;
    request.progression = Stage0();
    request.evidence.masterPresent = true;
    return request;
}
}

int main()
{
    bool ok = true;
    std::array<std::uint32_t, 2> occupied{{60, 80}};
    ParseRequest match = Base(false, false, "match");
    match.evidence.masterOccupied = occupied;
    CommandDecision const legacyMatch = ParseCommand(match);
    ok = Check(legacyMatch.legacyUnchanged && !legacyMatch.preserve && legacyMatch.legacyIlvl == 70,
            "feature off match uses the occupied master average") &&
         ok;
    match.scoreLimit = 65;
    ok = Check(ParseCommand(match).legacyIlvl == 65, "feature off match is clamped by config") && ok;

    ParseRequest offNumeric = Base(false, false, "200");
    ok = Check(ParseCommand(offNumeric).legacyUnchanged && ParseCommand(offNumeric).legacyIlvl == 200,
            "feature off numeric is not stage capped") &&
         ok;
    ParseRequest onNumeric = offNumeric;
    onNumeric.featureOn = true;
    ok = Check(!ParseCommand(onNumeric).legacyUnchanged && ParseCommand(onNumeric).cap.cap == 66,
            "feature on numeric uses the stage cap") &&
         ok;

    ParseRequest resetQuality = Base(false, false, "reset green");
    CommandDecision const green = ParseCommand(resetQuality);
    ok = Check(green.reset && green.argument == ArgKind::Quality && green.quality == 2 && green.legacyUnchanged,
            "reset plus quality") &&
         ok;
    ParseRequest resetNumber = Base(false, false, "reset 200");
    ok = Check(ParseCommand(resetNumber).reset && ParseCommand(resetNumber).argument == ArgKind::Numeric &&
               ParseCommand(resetNumber).legacyIlvl == 200,
            "reset plus numeric") &&
         ok;
    ParseRequest resetMatch = Base(false, false, "reset match");
    resetMatch.evidence.masterOccupied = occupied;
    CommandDecision const resetMatched = ParseCommand(resetMatch);
    ok = Check(resetMatched.reset && resetMatched.argument == ArgKind::Match && resetMatched.legacyIlvl == 70,
            "reset plus match") &&
         ok;

    ok = Check(ParseCommand(Base(false, false, "0")).rejected, "zero is garbage") && ok;
    ok = Check(ParseCommand(Base(false, false, "3")).rejected, "quality-sized number is garbage") && ok;
    ok = Check(ParseCommand(Base(false, false, "nope")).rejected, "garbage text is rejected") && ok;
    ok = Check(ParseCommand(Base(false, true, "")).rejected, "bis below epic quality is rejected") && ok;
    ParseRequest bis = Base(false, true, "");
    bis.qualityLimit = 4;
    ok = Check(!ParseCommand(bis).rejected && ParseCommand(bis).bis && ParseCommand(bis).argument == ArgKind::None,
            "plain bis") &&
         ok;
    ParseRequest bisNumber = Base(false, true, "55");
    bisNumber.qualityLimit = 4;
    ok = Check(ParseCommand(bisNumber).argument == ArgKind::Numeric && ParseCommand(bisNumber).legacyIlvl == 55,
            "bis numeric") &&
         ok;
    ok = Check(ParseCommand(Base(false, true, "0")).rejected, "bis zero is rejected") && ok;
    ok = Check(ParseCommand(Base(false, true, "green")).rejected, "bis quality word is rejected") && ok;

    ParseRequest fixed = Base(false, false, "match");
    fixed.match.source = MatchSource::CommandingSender;
    fixed.match.denominator = MatchDenominator::FixedCountedSlots;
    fixed.evidence.masterOccupied = occupied;
    fixed.evidence.senderPresent = true;
    std::array<std::uint32_t, 3> fixedLevels{{30, 0, 30}};
    fixed.evidence.senderFixed = fixedLevels;
    fixed.evidence.senderFixedDenominator = 3;
    ok = Check(ParseCommand(fixed).legacyIlvl == 20, "configured fixed denominator can be selected") && ok;
    ParseRequest defaults = Base(false, false, "match");
    defaults.evidence.masterOccupied = occupied;
    defaults.evidence.senderPresent = true;
    defaults.evidence.senderFixed = fixedLevels;
    defaults.evidence.senderFixedDenominator = 3;
    ok = Check(ParseCommand(defaults).legacyIlvl == 70, "default match stays the occupied master average") && ok;
    std::array<std::uint32_t, 3> withEmpty{{60, 0, 80}};
    ParseRequest emptySlot = Base(false, false, "match");
    emptySlot.evidence.masterPresent = true;
    emptySlot.evidence.masterOccupied = withEmpty;
    ok = Check(ParseCommand(emptySlot).legacyIlvl == 70, "match ignores an empty occupied slot") && ok;

    ParseRequest onMatch = defaults;
    onMatch.featureOn = true;
    onMatch.requestedMode = CapMode::Match;
    ok = Check(ParseCommand(onMatch).cap.cap == 66, "feature on match is also stage capped") && ok;
    onMatch.requestedMode = CapMode::Leveling;
    ok = Check(ParseCommand(onMatch).preserve, "match text with leveling mode preserves") && ok;
    ParseRequest plain = Base(true, false, "");
    plain.requestedMode = CapMode::Match;
    ok = Check(ParseCommand(plain).preserve, "plain text with match mode preserves") && ok;
    plain.requestedMode = static_cast<CapMode>(9);
    ok = Check(ParseCommand(plain).preserve, "unknown mode preserves") && ok;

    ParseRequest emptyMaster = Base(false, false, "match");
    ok = Check(ParseCommand(emptyMaster).preserve && ParseCommand(emptyMaster).legacyUnchanged,
            "a zero match average changes no gear") &&
         ok;

    if (!ok)
        return 1;
    std::cout << "checks=" << g_checks << "\n";
    return 0;
}
