#include "AutoGearConstraintFill.h"
#include "AutoGearSelectionPlan.h"
#include "AutoGearVariantSource.h"
#include <array>
#include <iostream>
#include <vector>

using namespace PlayerbotsAutoGearSelection;
using PlayerbotsAutoGearWeapons::Kind;

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

ItemFact KnownChest()
{
    ItemFact fact;
    fact.id = 10;
    fact.itemLevel = 60;
    fact.requiredLevel = 1;
    fact.slot = 4;
    fact.armorSubtype = 1;
    fact.sourceKnown = true;
    fact.metadataKnown = true;
    fact.equipKnown = true;
    fact.equipAllowed = true;
    return fact;
}

BotView KnownMage()
{
    BotView bot;
    bot.levelKnown = true;
    bot.level = 60;
    bot.classKnown = true;
    bot.classId = 8;
    bot.specKnown = true;
    bot.specTab = 1;
    bot.roleKnown = true;
    bot.role = Role::Caster;
    bot.armorKnown = true;
    bot.armorMask = 1u << 1;
    bot.weaponsKnown = true;
    return bot;
}
}

int main()
{
    bool ok = true;
    SourceRow loaded;
    loaded.entryId = 100;
    loaded.itemLevel = 60;
    loaded.templateLoaded = true;
    loaded.eligibility = SourceEligibility::KnownUnavailable;
    loaded.staticCapabilityKnown = true;
    loaded.staticCapable = true;
    std::array<SourceRow, 1> onlyLoaded{{loaded}};
    CopiedSource const notEligible = CopyBoundedSource(onlyLoaded, 4);
    ok = Check(notEligible.rows.empty() && notEligible.sourceComplete && notEligible.status == CopyStatus::Copied,
            "known-unavailable is omitted and the source can still be complete") &&
         ok;

    SourceRow unknown = loaded;
    unknown.eligibility = SourceEligibility::Unknown;
    std::array<SourceRow, 1> unknownRows{{unknown}};
    CopiedSource const unknownCopy = CopyBoundedSource(unknownRows, 4);
    ok = Check(unknownCopy.rows.empty() && !unknownCopy.sourceComplete && unknownCopy.status == CopyStatus::Incomplete,
            "unknown eligibility is not an empty complete source") &&
         ok;
    SourceRow unloaded = loaded;
    unloaded.eligibility = SourceEligibility::KnownEligible;
    unloaded.templateLoaded = false;
    std::array<SourceRow, 1> unloadedRows{{unloaded}};
    ok = Check(!CopyBoundedSource(unloadedRows, 4).sourceComplete, "an unloaded template is incomplete") && ok;
    SourceRow unknownCapability = unloaded;
    unknownCapability.templateLoaded = true;
    unknownCapability.staticCapabilityKnown = false;
    std::array<SourceRow, 1> capabilityRows{{unknownCapability}};
    ok = Check(!CopyBoundedSource(capabilityRows, 4).sourceComplete, "unknown capability is incomplete") && ok;
    SourceRow zeroEntry = unloaded;
    zeroEntry.templateLoaded = true;
    zeroEntry.entryId = 0;
    std::array<SourceRow, 1> zeroEntryRows{{zeroEntry}};
    ok = Check(!CopyBoundedSource(zeroEntryRows, 4).sourceComplete, "a zero entry is incomplete") && ok;
    SourceRow zeroLevel = unloaded;
    zeroLevel.templateLoaded = true;
    zeroLevel.itemLevel = 0;
    std::array<SourceRow, 1> zeroLevelRows{{zeroLevel}};
    ok = Check(!CopyBoundedSource(zeroLevelRows, 4).sourceComplete, "a zero item level is incomplete") && ok;
    SourceRow incapable = unloaded;
    incapable.templateLoaded = true;
    incapable.staticCapable = false;
    std::array<SourceRow, 1> incapableRows{{incapable}};
    CopiedSource const incapableCopy = CopyBoundedSource(incapableRows, 4);
    ok = Check(incapableCopy.rows.empty() && !incapableCopy.sourceComplete &&
               incapableCopy.status == CopyStatus::IncompleteVariants,
            "eligible but incapable row cannot certify an exhaustive legal domain") &&
         ok;
    SourceRow invalidEligibility = incapable;
    invalidEligibility.staticCapable = true;
    invalidEligibility.eligibility = static_cast<SourceEligibility>(255);
    std::array<SourceRow, 1> invalidRows{{invalidEligibility}};
    CopiedSource const invalidCopy = CopyBoundedSource(invalidRows, 4);
    ok = Check(invalidCopy.status == CopyStatus::Invalid && invalidCopy.rows.empty() && !invalidCopy.sourceComplete,
            "eligibility 255 is invalid, not eligible") &&
         ok;

    SourceRow propA = loaded;
    propA.eligibility = SourceEligibility::KnownEligible;
    propA.randomProperty = 1;
    SourceRow propB = propA;
    propB.randomProperty = 2;
    std::array<SourceRow, 2> variants{{propA, propB}};
    CopiedSource const both = CopyBoundedSource(variants, 4);
    ok = Check(both.rows.empty() && !both.sourceComplete && both.status == CopyStatus::IncompleteVariants,
            "legacy mixed-property source clears all rows and remains incomplete") &&
         ok;
    CopiedSource const variantAwareBoth = CopyBoundedSource(variants, 4, true);
    ok = Check(variantAwareBoth.rows.size() == 2 && variantAwareBoth.sourceComplete &&
               variantAwareBoth.status == CopyStatus::Copied,
            "explicit signed-variant caller retains both verified property rows") &&
         ok;
    std::array<SourceRow, 1> one{{propA}};
    ok = Check(CopyBoundedSource(one, 4).sourceComplete && CopyBoundedSource(one, 4).rows.size() == 1,
            "one variant can be complete") &&
         ok;

    std::array<SourceRow, 5> many{};
    for (std::size_t i = 0; i < many.size(); ++i)
    {
        many[i] = propA;
        many[i].entryId = static_cast<std::uint32_t>(200 + i);
    }
    CopiedSource const bounded = CopyBoundedSource(many, 2);
    ok = Check(bounded.status == CopyStatus::Bounded && bounded.rows.empty() && !bounded.sourceComplete,
            "native rows are bounded before they are copied") &&
         ok;

    ItemFact wornUnique = KnownChest();
    wornUnique.equipAllowed = false;
    wornUnique.blockedOnlyByCurrentGear = true;
    ok = Check(Classify(wornUnique, KnownMage(), 66) == FactDecision::Accept,
            "a current-gear block is not a static miss") &&
         ok;
    wornUnique.blockedOnlyByCurrentGear = false;
    ok = Check(Classify(wornUnique, KnownMage(), 66) == FactDecision::Unavailable,
            "a known equip miss stays unavailable") &&
         ok;

    OwnerThreadCopy published;
    published.senderEpoch = 11;
    published.botEpoch = 22;
    published.senderIpRevision = 33;
    published.senderEquipmentRevision = 44;
    published.botEquipmentRevision = 55;
    published.sessionIncarnation = 66;
    published.botIncarnation = 77;
    published.policyRevision = 88;
    published.immutable = true;
    OwnerThreadCopy same = published;
    OwnerThreadCopy otherBot = published;
    otherBot.botEpoch = 99;
    OwnerThreadCopy otherSenderGear = published;
    otherSenderGear.senderEquipmentRevision = 1;
    OwnerThreadCopy otherBotGear = published;
    otherBotGear.botEquipmentRevision = 1;
    OwnerThreadCopy otherSession = published;
    otherSession.sessionIncarnation = 1;
    OwnerThreadCopy otherPolicy = published;
    otherPolicy.policyRevision = 1;
    OwnerThreadCopy zeroPolicy = published;
    zeroPolicy.policyRevision = 0;
    OwnerThreadCopy mutableCopy = published;
    mutableCopy.immutable = false;
    ok = Check(SameFinalCopy(published, same) && !SameFinalCopy(published, otherBot),
            "final B compares sender and bot epochs on the copy") &&
         ok;
    ok = Check(!SameFinalCopy(published, otherSenderGear) && !SameFinalCopy(published, otherBotGear) &&
               !SameFinalCopy(published, otherSession) && !SameFinalCopy(published, otherPolicy),
            "final B compares each revision on its own") &&
         ok;
    ok = Check(!SameFinalCopy(published, zeroPolicy) && !SameFinalCopy(published, mutableCopy),
            "a zero or mutable revision is not the same copy") &&
         ok;

    auto counting = [](std::uint32_t, std::int32_t, std::uint32_t) -> std::uint32_t
    {
        return 1;
    };
    PlanInput unknownPlan;
    unknownPlan.cap = CapResult{false, 66};
    unknownPlan.bot = KnownMage();
    unknownPlan.eligibleMask = 1u << 4;
    unknownPlan.requiredMask = 1u << 4;
    unknownPlan.completeMask = 1u << 4;
    unknownPlan.authorityValid = true;
    unknownPlan.bags = BagSpace::Enough;
    unknownPlan.hasNativeSource = true;
    unknownPlan.nativeRows = unknownRows;
    unknownPlan.sourceBudget = 4;
    Plan const unknownResult = SelectFromFacts(unknownPlan, counting);
    ok = Check(unknownResult.outcome.disposition == Disposition::Preserve &&
               unknownResult.outcome.action != GearAction::StripToBags && !unknownResult.sourceComplete,
            "an unknown native row cannot authorize a strip") &&
         ok;

    PlanInput knownEmpty = unknownPlan;
    knownEmpty.nativeRows = onlyLoaded;
    Plan const unprovenEmpty = SelectFromFacts(knownEmpty, counting);
    ok = Check(unprovenEmpty.outcome.disposition == Disposition::Preserve &&
               unprovenEmpty.outcome.action != GearAction::StripToBags && !unprovenEmpty.sourceComplete,
            "a complete flag without a per-slot list does not strip") &&
         ok;
    knownEmpty.slotLists[4] = PlanInput::SlotListState::Complete;
    Plan const flagOnly = SelectFromFacts(knownEmpty, counting);
    ok = Check(flagOnly.outcome.disposition == Disposition::Preserve && !flagOnly.sourceComplete,
            "a complete flag is not a native slot proof") &&
         ok;
    knownEmpty.sourceRevision = 7;
    knownEmpty.nativeProofs[4].authoritative = true;
    // The old fixture has only a slot-0 source. An empty slot-4 proof,
    // even with a declared zero-row count, is not a native universe witness.
    PlanInput bareEmpty = knownEmpty;
    bareEmpty.nativeProofs[4].rowsDeclared = true;
    bareEmpty.nativeProofs[4].declaredRows = 0;
    Plan const bareEmptyResult = SelectFromFacts(bareEmpty, counting);
    ok = Check(bareEmptyResult.outcome.disposition == Disposition::Preserve &&
               bareEmptyResult.outcome.action != GearAction::StripToBags &&
               !bareEmptyResult.sourceComplete,
            "declared zero-row same-slot source cannot authorize stripping") && ok;

    // A positive synthetic case must match a real raw unavailable row in
    // the required slot to the corresponding declared provenance record.
    SourceRow unavailableChest = loaded;
    unavailableChest.slot = 4;
    std::array<SourceRow, 1> chestRows{{unavailableChest}};
    ProvenanceRow unavailableProof;
    unavailableProof.entryId = unavailableChest.entryId;
    unavailableProof.randomProperty = unavailableChest.randomProperty;
    unavailableProof.itemLevel = unavailableChest.itemLevel;
    unavailableProof.state = RowState::KnownUnavailable;
    unavailableProof.revision = 7;
    std::array<ProvenanceRow, 1> unavailableProofRows{{unavailableProof}};
    knownEmpty.nativeRows = chestRows;
    knownEmpty.nativeProofs[4].rowsDeclared = true;
    knownEmpty.nativeProofs[4].declaredRows = unavailableProofRows.size();
    knownEmpty.nativeProofs[4].rows = unavailableProofRows;
    Plan const knownEmptyResult = SelectFromFacts(knownEmpty, counting);
    ok = Check(knownEmptyResult.outcome.disposition == Disposition::GenuineNoSet &&
               knownEmptyResult.outcome.action == GearAction::StripToBags && knownEmptyResult.sourceComplete,
            "a proven empty slot list can still be a no-set") &&
         ok;

    SourceRow levelRow = propA;
    levelRow.slot = 4;
    levelRow.itemLevel = 60;
    std::array<SourceRow, 1> levelRows{{levelRow}};
    ItemFact levelFact = KnownChest();
    levelFact.id = 100;
    levelFact.randomProperty = 1;
    levelFact.itemLevel = 61;
    std::array<ItemFact, 1> levelFacts{{levelFact}};
    PlanInput levelPlan = knownEmpty;
    levelPlan.nativeRows = levelRows;
    levelPlan.facts = levelFacts;
    Plan const levelMismatch = SelectFromFacts(levelPlan, counting);
    ok = Check(levelMismatch.outcome.disposition == Disposition::Preserve && !levelMismatch.sourceComplete,
            "entry and variant are not enough when the item level differs") &&
         ok;
    levelFacts[0].itemLevel = 60;
    levelFacts[0].setEvidenceKnown = true;
    levelPlan.facts = levelFacts;
    ProvenanceRow matched;
    matched.entryId = 100;
    matched.randomProperty = 1;
    matched.itemLevel = 60;
    matched.state = RowState::KnownPresent;
    matched.revision = 7;
    matched.setEvidenceKnown = true;
    std::array<ProvenanceRow, 1> matchedRows{{matched}};
    levelPlan.sourceRevision = 7;
    levelPlan.nativeProofs[4].authoritative = true;
    levelPlan.nativeProofs[4].rows = matchedRows;
    Plan const levelMatch = SelectFromFacts(levelPlan, counting);
    ok = Check(levelMatch.outcome.disposition == Disposition::Commit && levelMatch.solved.items[4] == 100 &&
               levelMatch.sourceComplete,
            "a per-slot list matches entry, variant, and item level") &&
         ok;

    std::array<ItemFact, 2> tooMany{{levelFact, levelFact}};
    tooMany[0].itemLevel = 60;
    tooMany[1].id = 101;
    tooMany[1].itemLevel = 60;
    PlanInput boundedFacts = levelPlan;
    boundedFacts.facts = tooMany;
    boundedFacts.candidateBudget = 1;
    int boundedScores = 0;
    auto boundedScorer = [&](std::uint32_t, std::int32_t, std::uint32_t) -> std::uint32_t
    {
        ++boundedScores;
        return 1;
    };
    Plan const boundedPlan = SelectFromFacts(boundedFacts, boundedScorer);
    ok = Check(boundedPlan.outcome.disposition == Disposition::Preserve && boundedScores == 0,
            "facts are bounded before they are matched to native rows") &&
         ok;

    ItemFact extra = KnownChest();
    extra.id = 100;
    extra.randomProperty = 1;
    std::array<ItemFact, 1> extraFacts{{extra}};
    PlanInput extraPlan = unknownPlan;
    extraPlan.facts = extraFacts;
    Plan const extraResult = SelectFromFacts(extraPlan, counting);
    ok = Check(extraResult.outcome.disposition == Disposition::Preserve && !extraResult.sourceComplete,
            "facts cannot outrun an unknown native source") &&
         ok;

    if (!ok)
        return 1;
    std::cout << "checks=" << g_checks << "\n";
    return 0;
}
