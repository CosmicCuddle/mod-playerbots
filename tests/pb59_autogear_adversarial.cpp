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
    fact.setEvidenceKnown = true;
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

    std::array<std::uint8_t, 16> worldBytes{};
    std::array<std::uint8_t, 16> spellBytes{};
    FileChunks worldFile;
    FileChunks spellFile;
    std::array<DeclaredChunk, 2> worldParts{};
    std::array<DeclaredChunk, 2> spellParts{};
    auto fill = [](char const* text, std::array<std::uint8_t, 16>& storage, std::array<DeclaredChunk, 2>& parts,
                    FileChunks& chunks)
    {
        std::size_t length = 0;
        while (text[length] != 0 && length < storage.size())
        {
            storage[length] = static_cast<std::uint8_t>(text[length]);
            ++length;
        }
        std::size_t const mid = length / 2;
        parts[0].bytes = std::span<std::uint8_t const>(storage.data(), mid);
        parts[0].declaredBytes = mid;
        parts[1].bytes = std::span<std::uint8_t const>(storage.data() + mid, length - mid);
        parts[1].declaredBytes = length - mid;
        chunks.chunks = parts;
        chunks.declaredCount = 2;
    };
    fill("world", worldBytes, worldParts, worldFile);
    fill("spell", spellBytes, spellParts, spellFile);
    std::array<std::int32_t, 2> spellIds{{100, 200}};
    std::array<bool, 2> known{{true, true}};
    std::array<bool, 2> noProc{{true, false}};
    SpellManifest spells;
    spells.headerIsId = true;
    spells.versionKnown = true;
    spells.digestKnown = true;
    spells.expectedDigest = FileDigest(spellFile);
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
    world.expectedDigest = FileDigest(worldFile);
    world.itemTemplateRows = 1;
    world.expectedItemRows = 1;
    world.itemSpellsRequired = true;
    world.validationBound = 8;
    ok = Check(CheckExport(world, worldFile, spells, spellFile) == ExportVerdict::Invalid,
            "a required item-spell list cannot be empty") &&
         ok;
    std::array<std::int32_t, 1> negative{{-5}};
    world.itemSpellIds = negative;
    ExportVerdict const negativeRef = CheckExport(world, worldFile, spells, spellFile);
    ok = Check(negativeRef == ExportVerdict::Unknown, "a negative item reference is not given a spell meaning") && ok;
    std::array<std::int32_t, 1> zeroSpell{{0}};
    spells.spellIds = zeroSpell;
    spells.spellRows = 1;
    spells.expectedSpellRows = 1;
    spells.overrideKnown = std::span<bool const>(known.data(), 1);
    spells.procKnown = spells.overrideKnown;
    spells.sourceKnown = spells.overrideKnown;
    ok = Check(CheckExport(world, worldFile, spells, spellFile) == ExportVerdict::Invalid,
            "a nonpositive spell record id is invalid") &&
         ok;
    spells.spellIds = spellIds;
    spells.spellRows = 2;
    spells.expectedSpellRows = 2;
    spells.overrideKnown = known;
    spells.procKnown = known;
    spells.sourceKnown = known;
    std::array<std::int32_t, 1> missing{{300}};
    world.itemSpellIds = missing;
    ExportVerdict const missingSpell = CheckExport(world, worldFile, spells, spellFile);
    ok = Check(missingSpell == ExportVerdict::Unknown, "a spell missing from the manifest stays unknown") && ok;
    std::array<std::int32_t, 1> linked{{200}};
    world.itemSpellIds = linked;
    spells.procKnown = noProc;
    ok = Check(CheckExport(world, worldFile, spells, spellFile) == ExportVerdict::Unknown,
            "missing proc evidence stays unknown") &&
         ok;
    spells.procKnown = known;
    spells.validationBound = 1;
    ok = Check(CheckExport(world, worldFile, spells, spellFile) == ExportVerdict::Bounded,
            "an index past the bound is not scanned") &&
         ok;
    std::array<std::int32_t, 2> unsorted{{200, 100}};
    spells.spellIds = unsorted;
    spells.validationBound = 8;
    ok = Check(CheckExport(world, worldFile, spells, spellFile) == ExportVerdict::Invalid,
            "an unsorted spell index is invalid") &&
         ok;
    ok = Check(!ExportIsProductionReady(ExportVerdict::MetadataChecked) &&
               !ExportIsProductionReady(ExportVerdict::Unknown),
            "no export verdict is production ready") &&
         ok;

    int scores = 0;
    auto counting = [&](std::uint32_t, std::int32_t, std::uint32_t) -> std::uint32_t
    {
        ++scores;
        return 30;
    };
    std::vector<ItemFact> many;
    std::uint32_t mask = 0;
    unsigned const slots[] = {0, 1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
    for (unsigned slot : slots)
    {
        ItemFact fact = Chest(77, static_cast<std::int32_t>(slot) - 9, static_cast<std::uint8_t>(slot));
        many.push_back(fact);
        mask |= 1u << slot;
    }
    PlanInput spread = Base();
    spread.senderEpoch = 1;
    spread.senderIpRevision = 2;
    spread.senderEquipmentRevision = 3;
    spread.botEpoch = 4;
    spread.botEquipmentRevision = 5;
    spread.sessionIncarnation = 6;
    spread.botIncarnation = 7;
    spread.policyRevision = 8;
    spread.facts = many;
    spread.eligibleMask = mask;
    spread.requiredMask = mask;
    spread.completeMask = mask;
    Plan const handed = SelectFromFacts(spread, counting);
    bool handoff = handed.outcome.disposition == Disposition::Commit && handed.storage.ready && scores == 14;
    for (unsigned slot : slots)
    {
        std::int32_t const property = static_cast<std::int32_t>(slot) - 9;
        handoff = handoff && handed.selected[slot].entryId == 77 && handed.selected[slot].randomProperty == property &&
                  handed.storage.pieces[slot].entryId == 77 && handed.storage.pieces[slot].randomProperty == property &&
                  handed.solved.items[slot] == 77;
    }
    ok = Check(handoff, "the same entry keeps its signed property in the plan and the storage request") && ok;
    ok = Check(scores == 14, "each accepted fact is scored once") && ok;

    ItemFact unknownScore = Chest(4, 0, 4);
    unknownScore.scoreKnown = false;
    std::vector<ItemFact> unknownFacts{unknownScore};
    PlanInput unknownPlan = Base();
    unknownPlan.facts = unknownFacts;
    unknownPlan.eligibleMask = 1u << 4;
    unknownPlan.requiredMask = 1u << 4;
    unknownPlan.completeMask = 1u << 4;
    Plan const unknown = SelectFromFacts(unknownPlan, Score);
    ok = Check(unknown.outcome.disposition == Disposition::Preserve && !unknown.storage.ready &&
               unknown.outcome.action != GearAction::StripToBags,
            "an unknown score cannot authorize a commit or a strip") &&
         ok;

    ProvenanceRow same = Row(1, RowState::KnownPresent);
    std::array<ProvenanceRow, 3> crowded{{same, same, same}};
    ok = Check(AssessSlotList(crowded, 7, true, 2).verdict == ListVerdict::Incomplete,
            "proof rows past the bound are not scanned") &&
         ok;
    ok = Check(AssessSlotList(crowded, 7, true, 8).verdict == ListVerdict::Invalid,
            "the same rows are a duplicate once the bound allows the scan") &&
         ok;

    if (!ok)
        return 1;
    std::cout << "checks=" << g_checks << "\n";
    return 0;
}
