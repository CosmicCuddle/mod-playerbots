#include "AutoGearExportCheck.h"
#include "AutoGearSelectionPlan.h"
#include "AutoGearSlotEnumeration.h"
#include "AutoGearVariantSolver.h"
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

ProvenanceRow Row(std::uint32_t entry, RowState state)
{
    ProvenanceRow row;
    row.entryId = entry;
    row.randomProperty = 1;
    row.itemLevel = 60;
    row.state = state;
    row.revision = 7;
    row.setEvidenceKnown = true;
    return row;
}

ItemFact Chest(std::uint32_t id, std::int32_t property, std::uint8_t slot)
{
    ItemFact fact;
    fact.id = id;
    fact.randomProperty = property;
    fact.itemLevel = 60;
    fact.requiredLevel = 1;
    fact.slot = slot;
    fact.armorSubtype = slot == 4 ? 1 : 0;
    fact.sourceKnown = true;
    fact.metadataKnown = true;
    fact.equipKnown = true;
    fact.equipAllowed = true;
    return fact;
}

PlanInput Base()
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
    in.authorityValid = true;
    in.bags = BagSpace::Enough;
    in.candidateBudget = 1000;
    return in;
}

std::uint32_t Score(std::uint32_t id, std::int32_t, std::uint32_t)
{
    return id == 8 ? 10u : 100u;
}

Sha256Digest DigestOf(char const* text)
{
    std::array<std::uint8_t, 8> bytes{};
    std::size_t length = 0;
    while (text[length] != 0 && length < bytes.size())
    {
        bytes[length] = static_cast<std::uint8_t>(text[length]);
        ++length;
    }
    return HashSha256(std::span<std::uint8_t const>(bytes.data(), length));
}
}

int main()
{
    bool ok = true;
    ProvenanceRow present = Row(10, RowState::KnownPresent);
    ProvenanceRow unavailable = Row(10, RowState::KnownUnavailable);
    std::array<ProvenanceRow, 2> presentFirst{{present, unavailable}};
    std::array<ProvenanceRow, 2> unavailableFirst{{unavailable, present}};
    std::array<ProvenanceRow, 2> bothUnavailable{{unavailable, unavailable}};
    ok = Check(AssessSlotList(presentFirst, 7, true).verdict == ListVerdict::Invalid,
            "present then unavailable is a duplicate") &&
         ok;
    ok = Check(AssessSlotList(unavailableFirst, 7, true).verdict == ListVerdict::Invalid,
            "unavailable then present is a duplicate") &&
         ok;
    ok = Check(AssessSlotList(bothUnavailable, 7, true).verdict == ListVerdict::Invalid,
            "two unavailable copies are a duplicate") &&
         ok;
    ProvenanceRow otherLevel = present;
    otherLevel.itemLevel = 61;
    std::array<ProvenanceRow, 2> levels{{present, otherLevel}};
    std::array<ProvenanceRow, 2> levelsReversed{{otherLevel, present}};
    ok = Check(AssessSlotList(levels, 7, true).verdict == ListVerdict::Invalid &&
               AssessSlotList(levelsReversed, 7, true).verdict == ListVerdict::Invalid,
            "contradictory item levels are invalid in either order") &&
         ok;

    ItemFact first = Chest(9, 1, 10);
    first.maxCount = 1;
    ItemFact second = Chest(9, 2, 11);
    second.maxCount = 1;
    std::vector<ItemFact> variants{first, second};
    PlanInput limited = Base();
    limited.facts = variants;
    limited.eligibleMask = (1u << 10) | (1u << 11);
    limited.requiredMask = limited.eligibleMask;
    limited.completeMask = limited.eligibleMask;
    RetainedCount held;
    held.entryId = 9;
    held.bagsKnown = true;
    held.bankKnown = true;
    held.equippedKnown = true;
    held.gemsKnown = true;
    std::array<RetainedCount, 1> heldRows{{held}};
    limited.retained = heldRows;
    Plan const over = SelectFromFacts(limited, Score);
    ok = Check(over.solved.status == PlayerbotsAutoGearSet::Status::NoSet && over.solved.items[10] == 0 &&
               over.solved.items[11] == 0,
            "MaxCount counts both variants and the retained total") &&
         ok;

    std::array<PlayerbotsAutoGearSet::Candidate, 1> sharedLow{{}};
    sharedLow[0].id = 9;
    sharedLow[0].itemLevel = 60;
    sharedLow[0].score = 100;
    sharedLow[0].metadataKnown = true;
    sharedLow[0].eligible = true;
    sharedLow[0].kind = Kind::Count;
    std::array<PlayerbotsAutoGearSet::Domain, PlayerbotsAutoGearSet::Slots> sharedDomains{};
    sharedDomains[10] = {sharedLow, true};
    sharedDomains[11] = {sharedLow, true};
    PlayerbotsAutoGearSet::Policy sharedPolicy;
    sharedPolicy.cap = 66;
    sharedPolicy.eligibleMask = limited.eligibleMask;
    sharedPolicy.requiredMask = limited.requiredMask;
    sharedPolicy.known = true;
    PlayerbotsAutoGearSet::Result const shared =
        PlayerbotsAutoGearSet::Solve(sharedDomains, sharedPolicy, 1000, 100000);
    ok = Check(shared.status == PlayerbotsAutoGearSet::Status::SuitableSet && shared.items[10] == 9 &&
               shared.items[11] == 9 && over.solved.status != shared.status,
            "the shared solver is not used for a cross-variant MaxCount") &&
         ok;

    heldRows[0].bags = 1;
    Plan const retainedFull = SelectFromFacts(limited, Score);
    ok = Check(retainedFull.solved.items[10] == 0 && retainedFull.solved.items[11] == 0,
            "a retained copy consumes MaxCount for every variant") &&
         ok;

    SourceRow sourceA;
    sourceA.entryId = 4;
    sourceA.itemLevel = 60;
    sourceA.slot = 4;
    sourceA.templateLoaded = true;
    sourceA.eligibility = SourceEligibility::KnownEligible;
    sourceA.staticCapabilityKnown = true;
    sourceA.staticCapable = true;
    std::array<SourceRow, 1> onlyChest{{sourceA}};
    ItemFact chest = Chest(4, 0, 4);
    std::array<ItemFact, 1> chestFacts{{chest}};
    ProvenanceRow chestProof = Row(4, RowState::KnownPresent);
    chestProof.randomProperty = 0;
    std::array<ProvenanceRow, 1> chestRows{{chestProof}};
    PlanInput native = Base();
    native.hasNativeSource = true;
    native.sourceBudget = 4;
    native.sourceRevision = 7;
    native.nativeRows = onlyChest;
    native.facts = chestFacts;
    native.eligibleMask = (1u << 4) | (1u << 10);
    native.requiredMask = 1u << 4;
    native.completeMask = native.eligibleMask;
    native.nativeProofs[4].authoritative = true;
    native.nativeProofs[4].rows = chestRows;
    native.nativeProofs[10].authoritative = true;
    Plan const optionalEmpty = SelectFromFacts(native, Score);
    ok = Check(optionalEmpty.outcome.disposition == Disposition::Commit && optionalEmpty.solved.items[4] == 4 &&
               optionalEmpty.solved.items[10] == 0,
            "a proven-empty optional slot does not block the required slot") &&
         ok;

    std::array<ProvenanceRow, 2> contradictory{{chestProof, chestProof}};
    native.nativeProofs[4].rows = contradictory;
    Plan const badProof = SelectFromFacts(native, Score);
    ok = Check(badProof.outcome.disposition == Disposition::Preserve &&
               badProof.outcome.action != GearAction::StripToBags,
            "a contradictory proof does not strip") &&
         ok;

    ItemFact left = Chest(9, 1, 10);
    left.uniqueEquipped = true;
    left.maxCount = 0;
    ItemFact right = Chest(9, 0, 11);
    right.uniqueEquipped = true;
    std::vector<ItemFact> uniqueFacts{left, right};
    SourceRow uniqueLeft = sourceA;
    uniqueLeft.entryId = 9;
    uniqueLeft.randomProperty = 1;
    uniqueLeft.slot = 10;
    SourceRow uniqueRight = uniqueLeft;
    uniqueRight.randomProperty = 0;
    uniqueRight.slot = 11;
    std::array<SourceRow, 2> uniqueRows{{uniqueLeft, uniqueRight}};
    ProvenanceRow proofLeft = Row(9, RowState::KnownPresent);
    proofLeft.randomProperty = 1;
    ProvenanceRow proofRight = Row(9, RowState::KnownPresent);
    proofRight.randomProperty = 0;
    std::array<ProvenanceRow, 1> leftProof{{proofLeft}};
    std::array<ProvenanceRow, 1> rightProof{{proofRight}};
    PlanInput infeasible = Base();
    infeasible.hasNativeSource = true;
    infeasible.sourceBudget = 4;
    infeasible.sourceRevision = 7;
    infeasible.nativeRows = uniqueRows;
    infeasible.facts = uniqueFacts;
    infeasible.eligibleMask = (1u << 10) | (1u << 11);
    infeasible.requiredMask = infeasible.eligibleMask;
    infeasible.completeMask = infeasible.eligibleMask;
    infeasible.nativeProofs[10].authoritative = true;
    infeasible.nativeProofs[10].rows = leftProof;
    infeasible.nativeProofs[11].authoritative = true;
    infeasible.nativeProofs[11].rows = rightProof;
    Plan const noneFit = SelectFromFacts(infeasible, Score);
    bool const heldBack = noneFit.outcome.disposition == Disposition::Preserve &&
                          noneFit.outcome.action != GearAction::StripToBags;
    ok = Check(heldBack, "known options that cannot be worn together are not an empty strip") && ok;

    std::array<std::int32_t, 2> spellIds{{100, 200}};
    std::array<bool, 2> known{{true, true}};
    std::array<bool, 2> noProc{{true, false}};
    SpellManifest spells;
    spells.headerIsId = true;
    spells.versionKnown = true;
    spells.digestKnown = true;
    spells.digest = DigestOf("spell");
    spells.expectedDigest = spells.digest;
    spells.spellRows = 2;
    spells.expectedSpellRows = 2;
    spells.spellIds = spellIds;
    spells.overrideKnown = known;
    spells.procKnown = known;
    spells.sourceKnown = known;
    spells.validationBound = 8;
    WorldManifest world;
    world.schemaNamed = true;
    world.versionKnown = true;
    world.digestKnown = true;
    world.digest = DigestOf("world");
    world.expectedDigest = world.digest;
    world.itemTemplateRows = 1;
    world.expectedItemRows = 1;
    world.itemSpellsRequired = true;
    world.validationBound = 8;
    ok = Check(CheckExport(world, spells) == ExportVerdict::Invalid, "a required item-spell list cannot be empty") &&
         ok;
    std::array<std::int32_t, 1> negative{{-5}};
    world.itemSpellIds = negative;
    ok = Check(CheckExport(world, spells) == ExportVerdict::Invalid, "a negative spell reference is invalid") && ok;
    std::array<std::int32_t, 1> missing{{300}};
    world.itemSpellIds = missing;
    ExportVerdict const missingSpell = CheckExport(world, spells);
    ok = Check(missingSpell == ExportVerdict::Unknown, "a spell missing from the manifest stays unknown") && ok;
    std::array<std::int32_t, 1> linked{{200}};
    world.itemSpellIds = linked;
    spells.procKnown = noProc;
    ok = Check(CheckExport(world, spells) == ExportVerdict::Unknown, "missing proc evidence stays unknown") && ok;
    spells.procKnown = known;
    spells.validationBound = 1;
    ok = Check(CheckExport(world, spells) == ExportVerdict::Bounded, "an index past the bound is not scanned") && ok;
    std::array<std::int32_t, 2> unsorted{{200, 100}};
    spells.spellIds = unsorted;
    spells.validationBound = 8;
    ok = Check(CheckExport(world, spells) == ExportVerdict::Invalid, "an unsorted spell index is invalid") && ok;
    ok = Check(!ExportIsProductionReady(ExportVerdict::MetadataChecked) &&
               !ExportIsProductionReady(ExportVerdict::Unknown),
            "no export verdict is production ready") &&
         ok;

    if (!ok)
        return 1;
    std::cout << "checks=" << g_checks << "\n";
    return 0;
}
