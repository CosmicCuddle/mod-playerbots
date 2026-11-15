#include "AutoGearSelectionPlan.h"
#include "AutoGearSlotEnumeration.h"
#include <array>
#include <iostream>
#include <vector>

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

ProvenanceRow Present(std::uint32_t entry, std::int32_t property, std::uint32_t level)
{
    ProvenanceRow row;
    row.entryId = entry;
    row.randomProperty = property;
    row.itemLevel = level;
    row.state = RowState::KnownPresent;
    row.revision = 7;
    row.setEvidenceKnown = true;
    return row;
}

PlanInput EmptyChest()
{
    PlanInput in;
    in.cap = CapResult{false, 66};
    in.bot.levelKnown = true;
    in.bot.level = 60;
    in.bot.classKnown = true;
    in.bot.classId = 8;
    in.bot.specKnown = true;
    in.bot.specTab = 1;
    in.bot.roleKnown = true;
    in.bot.role = Role::Caster;
    in.bot.armorKnown = true;
    in.bot.armorMask = 1u << 1;
    in.bot.weaponsKnown = true;
    in.eligibleMask = 1u << 4;
    in.requiredMask = 1u << 4;
    in.completeMask = 1u << 4;
    in.authorityValid = true;
    in.bags = BagSpace::Enough;
    in.hasNativeSource = true;
    in.sourceBudget = 4;
    in.sourceRevision = 7;
    return in;
}

std::uint32_t ZeroScore(std::uint32_t, std::int32_t, std::uint32_t)
{
    return 1;
}
}

int main()
{
    bool ok = true;
    ListAssessment const missing = AssessSlotList({}, 7, false);
    ok = Check(missing.verdict == ListVerdict::Incomplete && !missing.setBonusProvenAbsent,
            "a missing enumeration is incomplete") &&
         ok;

    ListAssessment const empty = AssessSlotList({}, 7, true);
    ok = Check(empty.verdict == ListVerdict::ProvenEmpty && empty.setBonusProvenAbsent && empty.present == 0,
            "an authoritative empty slot is a proven empty domain") &&
         ok;

    ProvenanceRow unavailable = Present(10, 0, 60);
    unavailable.state = RowState::KnownUnavailable;
    std::array<ProvenanceRow, 1> unavailableRows{{unavailable}};
    ListAssessment const knownMiss = AssessSlotList(unavailableRows, 7, true);
    ok = Check(knownMiss.verdict == ListVerdict::ProvenEmpty && knownMiss.present == 0,
            "known-unavailable is not an unknown row") &&
         ok;

    ProvenanceRow unknown = Present(10, 0, 60);
    unknown.state = RowState::Unknown;
    std::array<ProvenanceRow, 1> unknownRows{{unknown}};
    ok = Check(AssessSlotList(unknownRows, 7, true).verdict == ListVerdict::Incomplete,
            "unknown provenance stays incomplete") &&
         ok;

    ProvenanceRow stale = Present(10, 0, 60);
    stale.revision = 6;
    std::array<ProvenanceRow, 1> staleRows{{stale}};
    ListAssessment const staleList = AssessSlotList(staleRows, 7, true);
    ok = Check(staleList.verdict == ListVerdict::Incomplete, "a stale revision is incomplete") && ok;

    ProvenanceRow mismatched = Present(10, 1, 60);
    mismatched.state = RowState::Mismatched;
    std::array<ProvenanceRow, 1> mismatchedRows{{mismatched}};
    ok = Check(AssessSlotList(mismatchedRows, 7, true).verdict == ListVerdict::Incomplete,
            "a mismatched row is incomplete") &&
         ok;

    std::array<ProvenanceRow, 2> duplicates{{Present(10, 1, 60), Present(10, 1, 60)}};
    ListAssessment const duplicateList = AssessSlotList(duplicates, 7, true);
    ok = Check(duplicateList.verdict == ListVerdict::Invalid, "a duplicate identity is invalid") && ok;

    ProvenanceRow bonus = Present(10, 0, 60);
    bonus.setId = 9;
    bonus.setEvidenceKnown = false;
    std::array<ProvenanceRow, 1> bonusRows{{bonus}};
    ListAssessment const unprovenBonus = AssessSlotList(bonusRows, 7, true);
    ok = Check(unprovenBonus.verdict == ListVerdict::Incomplete && !unprovenBonus.setBonusProvenAbsent,
            "an unproven set bonus is not absent") &&
         ok;
    bonus.setEvidenceKnown = true;
    bonus.setBonusPresent = false;
    std::array<ProvenanceRow, 1> absentRows{{bonus}};
    ListAssessment const absentBonus = AssessSlotList(absentRows, 7, true);
    ok = Check(absentBonus.verdict == ListVerdict::ProvenCandidates && absentBonus.setBonusProvenAbsent,
            "a set bonus strips only when proven absent") &&
         ok;

    PlanInput proven = EmptyChest();
    proven.nativeProofs[4].authoritative = true;
    Plan const stripped = SelectFromFacts(proven, ZeroScore);
    ok = Check(stripped.outcome.disposition == Disposition::GenuineNoSet && stripped.sourceComplete,
            "a proven empty slot may strip") &&
         ok;

    PlanInput unproven = EmptyChest();
    unproven.nativeProofs[4].authoritative = false;
    Plan const held = SelectFromFacts(unproven, ZeroScore);
    ok = Check(held.outcome.disposition == Disposition::Preserve && !held.sourceComplete,
            "incomplete slot proof preserves") &&
         ok;

    PlanInput bonusHold = EmptyChest();
    bonusHold.nativeProofs[4].authoritative = true;
    bonusHold.nativeProofs[4].rows = bonusRows;
    Plan const bonusPlan = SelectFromFacts(bonusHold, ZeroScore);
    ok = Check(bonusPlan.outcome.action != GearAction::StripToBags && !bonusPlan.sourceComplete,
            "a strip does not proceed while a set bonus is unproven") &&
         ok;

    if (!ok)
        return 1;
    std::cout << "checks=" << g_checks << "\n";
    return 0;
}
