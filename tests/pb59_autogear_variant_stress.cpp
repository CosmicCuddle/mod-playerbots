#include "AutoGearCandidateCollector.h"
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

std::uint32_t ScoreOf(std::uint32_t, std::int32_t, std::uint32_t)
{
    return 1;
}

bool Member(std::int32_t group, std::uint32_t enchant)
{
    return group == 10 && (enchant == 101 || enchant == 102);
}

OwnedVariantSource Adapt(std::vector<CoreGroupRow> const& core, std::vector<std::uint32_t> const& mgr, bool present,
    std::vector<std::uint32_t> const& properties, std::vector<std::uint32_t> const& suffixes)
{
    return AdaptLoadedGroup(core, mgr, present, 10, Member, properties, suffixes, true);
}
}

int main()
{
    bool ok = true;
    std::vector<std::uint32_t> properties{101, 102};
    std::vector<std::uint32_t> suffixes{101};
    std::vector<std::uint32_t> mgr{101, 102};
    std::vector<CoreGroupRow> selected{{101, 0.0000011f}, {102, 100.f}, {103, 0.f}, {104, 100.0001f}};
    OwnedVariantSource const weights = Adapt(selected, mgr, true, properties, suffixes);
    ok = Check(weights.complete && weights.pool.size() == 2 && weights.pool[0] == 101 && weights.pool[1] == 102,
            "only Core weights inside the loader window are members") &&
         ok;

    VariantRequest listed;
    listed.randomProperty = 10;
    listed.groupsKnown = true;
    listed.pool = weights.pool;
    listed.poolPresent = weights.poolPresent;
    listed.membership = weights.membership;
    listed.membershipKnown = weights.membershipKnown;
    listed.propertyDbc = weights.propertyDbc;
    listed.suffixDbc = weights.suffixDbc;
    listed.dbcKnown = true;
    VariantResult const collision = EnumerateVariants(listed);
    ok = Check(collision.complete && !collision.preserve && collision.variants[0].signedId == 101,
            "a property id that also exists as a suffix stays positive") &&
         ok;
    std::vector<std::uint32_t> suffixPool{101};
    listed.randomProperty = 0;
    listed.randomSuffix = 10;
    listed.pool = suffixPool;
    std::vector<std::uint8_t> suffixMember{1};
    listed.membership = suffixMember;
    listed.poolPresent = true;
    listed.membershipKnown = true;
    VariantResult const suffixCollision = EnumerateVariants(listed);
    ok = Check(suffixCollision.complete && suffixCollision.variants.size() == 1 &&
               suffixCollision.variants[0].signedId == -101,
            "a suffix id that also exists as a property stays negative") &&
         ok;

    std::vector<CoreGroupRow> duplicated{{101, 50.f}, {101, 50.f}};
    OwnedVariantSource const duplicate = Adapt(duplicated, mgr, true, properties, suffixes);
    ok = Check(duplicate.conflict && !duplicate.complete && duplicate.pool.empty(),
            "a duplicate Core row fails closed") &&
         ok;
    std::vector<std::uint32_t> ambiguous{101, 101};
    std::vector<CoreGroupRow> one{{101, 50.f}};
    OwnedVariantSource const mgrTwice = Adapt(one, ambiguous, true, properties, suffixes);
    ok = Check(mgrTwice.partial && !mgrTwice.complete, "a repeated manager id is not the Core set") && ok;
    std::vector<std::uint32_t> extra{101, 999};
    OwnedVariantSource const mgrExtra = Adapt(one, extra, true, properties, suffixes);
    ok = Check(mgrExtra.partial && !mgrExtra.complete, "a manager id outside Core is not complete") && ok;
    auto deny = [](std::int32_t, std::uint32_t) { return false; };
    OwnedVariantSource const denied = AdaptLoadedGroup(one, std::vector<std::uint32_t>{101}, true, 10, deny,
        properties, suffixes, true);
    ok = Check(denied.conflict && !denied.complete, "membership that rejects a Core row fails closed") && ok;

    VariantRequest emptyBudget;
    emptyBudget.groupsKnown = true;
    emptyBudget.budget = 0;
    VariantResult const noRoom = EnumerateVariants(emptyBudget);
    ok = Check(noRoom.truncated && noRoom.preserve && !noRoom.complete, "a zero variant budget is not complete") && ok;
    listed.randomProperty = 10;
    listed.randomSuffix = 0;
    listed.pool = weights.pool;
    listed.membership = weights.membership;
    listed.budget = 1;
    VariantResult const cut = EnumerateVariants(listed);
    ok = Check(cut.truncated && cut.preserve && !cut.complete, "a short variant budget is not a complete pool") && ok;

    CandidateTemplate row;
    row.loaded = true;
    row.metadataKnown = true;
    row.entry = 66;
    row.itemLevel = 60;
    row.slotKnown = true;
    row.slot = 4;
    row.qualityKnown = true;
    row.quality = 2;
    row.randomPropertyKnown = true;
    std::vector<CandidateTemplate> rows{row, row};
    CollectorRequest request;
    request.rows = rows;
    request.capSupplied = true;
    request.resolvedCap.preserve = false;
    request.resolvedCap.cap = 66;
    request.qualityLimitKnown = true;
    request.qualityLimit = 4;
    request.sourceBudget = 0;
    request.authorityValid = true;
    request.exhaustiveLegalDomain = true;
    request.sourceExhausted = true;
    CollectorResult const sourceCut = CollectCandidateFacts(request, ScoreOf);
    ok = Check(sourceCut.truncated && sourceCut.action == GearAction::Preserve && !sourceCut.claimedEmpty &&
               sourceCut.facts.empty(),
            "a zero source budget preserves before scoring") &&
         ok;
    request.sourceBudget = 1;
    CollectorResult const oneRow = CollectCandidateFacts(request, ScoreOf);
    ok = Check(oneRow.truncated && oneRow.action == GearAction::Preserve && !oneRow.claimedEmpty,
            "a source budget below the row count preserves") &&
         ok;

    RetainedCount entry;
    entry.entryId = 66;
    entry.bagsKnown = true;
    entry.bankKnown = true;
    entry.equippedKnown = true;
    entry.gemsKnown = true;
    entry.equipped = 1;
    entry.bags = 4;
    RetainedCount have;
    have.limitCategory = 7;
    have.bagsKnown = true;
    have.bankKnown = true;
    have.equippedKnown = true;
    have.gemsKnown = true;
    have.equipped = 1;
    have.bags = 3;
    std::vector<RetainedCount> retained{entry, have};
    ApprovedOriginal original;
    original.entryId = 66;
    original.propertyKnown = true;
    original.approved = true;
    original.guidKnown = true;
    original.guid = 1;
    original.slot = 4;
    original.limitCategory = 7;
    std::vector<ApprovedOriginal> originals{original};
    CollectorRequest counts;
    counts.population = CountPopulation::RandomBot;
    counts.populationVerified = true;
    counts.retained = retained;
    counts.approvedOriginals = originals;
    CollectorResult projected;
    ok = Check(ProjectFinalCounts(counts, projected) && projected.projectedCounts[0].equipped == 0 &&
               projected.projectedCounts[0].bags == 4 && projected.projectedCounts[1].equipped == 0 &&
               projected.projectedCounts[1].bags == 3,
            "HAVE bags stay and the equipped category count drops") &&
         ok;
    counts.population = CountPopulation::AltBot;
    CollectorResult alt;
    ok = Check(ProjectFinalCounts(counts, alt) && alt.projectedCounts[0].equipped == 1 &&
               alt.projectedCounts[1].equipped == 1,
            "an alt bot projection does not drop HAVE or EQUIPPED counts") &&
         ok;
    counts.population = CountPopulation::RandomBot;
    have.equipped = 0;
    retained[1] = have;
    counts.retained = retained;
    CollectorResult bagsOnly;
    ok = Check(!ProjectFinalCounts(counts, bagsOnly), "a HAVE row with no equipped count fails closed") && ok;

    ApprovedOriginal other = original;
    other.guid = 2;
    other.slot = 5;
    originals = {original, other};
    entry.equipped = 1;
    have.equipped = 1;
    retained = {entry, have};
    counts.retained = retained;
    counts.approvedOriginals = originals;
    CollectorResult overlap;
    ok = Check(!ProjectFinalCounts(counts, overlap), "a second original that would go negative fails closed") && ok;
    other.slot = original.slot;
    originals = {original, other};
    counts.approvedOriginals = originals;
    CollectorResult sameSlot;
    ok = Check(!ProjectFinalCounts(counts, sameSlot), "two originals in one slot fail closed") && ok;
    other.slot = 5;
    other.guid = original.guid;
    originals = {original, other};
    counts.approvedOriginals = originals;
    CollectorResult sameGuid;
    ok = Check(!ProjectFinalCounts(counts, sameGuid), "two originals with one guid fail closed") && ok;

    if (!ok)
        return 1;
    std::cout << "checks=" << g_checks << "\n";
    return 0;
}
