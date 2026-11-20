#include "AutoGearNativeCandidateCollector.h"
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

std::uint32_t ScoreOf(std::uint32_t, std::int32_t property, std::uint32_t)
{
    return property > 0 ? static_cast<std::uint32_t>(property) : 1u;
}

struct MockVariants
{
    std::vector<CoreGroupRow> core{};
    std::vector<std::uint32_t> mgr{};
    bool present = true;
    std::vector<std::uint32_t> properties{};
    std::vector<std::uint32_t> suffixes{};
    bool dbc = true;

    auto View(std::int32_t) const
    {
        struct Viewed
        {
            std::span<CoreGroupRow const> coreRows{};
            std::span<std::uint32_t const> mgrPool{};
            bool mgrPoolPresent = false;
            std::span<std::uint32_t const> propertyDbc{};
            std::span<std::uint32_t const> suffixDbc{};
            bool dbcKnown = false;
        };
        return Viewed{core, mgr, present, properties, suffixes, dbc};
    }

    bool operator()(std::int32_t group, std::uint32_t enchant) const
    {
        if (group <= 0 || enchant == 0)
            return false;
        for (CoreGroupRow const& row : core)
        {
            if (row.enchant == enchant && AuthoritativeWeight(row.weight))
                return true;
        }
        return false;
    }
};
}

int main()
{
    bool ok = true;
    std::vector<CoreGroupRow> core{{101, 50.f}, {102, 50.f}};
    std::vector<std::uint32_t> mgr{101, 102};
    std::vector<std::uint32_t> properties{101, 102};
    std::vector<std::uint32_t> suffixes{101};
    auto member = [](std::int32_t group, std::uint32_t enchant)
    {
        return group == 10 && (enchant == 101 || enchant == 102);
    };
    OwnedVariantSource const adapted =
        AdaptLoadedGroup(core, mgr, true, 10, member, properties, suffixes, true);
    ok = Check(adapted.complete && !adapted.partial && adapted.pool.size() == 2,
            "a matching positive-weight pool is complete") &&
         ok;

    VariantRequest request;
    request.randomProperty = 10;
    request.groupsKnown = true;
    request.pool = adapted.pool;
    request.poolPresent = adapted.poolPresent;
    request.membership = adapted.membership;
    request.membershipKnown = adapted.membershipKnown;
    request.propertyDbc = adapted.propertyDbc;
    request.suffixDbc = adapted.suffixDbc;
    request.dbcKnown = adapted.dbcKnown;
    VariantResult const listed = EnumerateVariants(request);
    ok = Check(listed.complete && listed.variants[0].signedId == 101 && listed.variants[1].signedId == 102,
            "the same numeric id in the suffix store does not reject a property") &&
         ok;

    std::vector<std::uint32_t> partialMgr{101};
    OwnedVariantSource const partial = AdaptLoadedGroup(core, partialMgr, true, 10, member, properties, suffixes, true);
    ok = Check(partial.partial && !partial.complete, "a mgr pool missing a Core row is not complete") && ok;

    OwnedVariantSource const missing = AdaptLoadedGroup(core, mgr, false, 10, member, properties, suffixes, true);
    ok = Check(!missing.complete && missing.partial, "a missing mgr pool is not complete") && ok;

    std::vector<CoreGroupRow> duplicated{{101, 10.f}, {101, 10.f}};
    OwnedVariantSource const duplicate =
        AdaptLoadedGroup(duplicated, mgr, true, 10, member, properties, suffixes, true);
    ok = Check(duplicate.conflict && !duplicate.complete, "a duplicate Core row conflicts") && ok;

    std::vector<CoreGroupRow> weightless{{101, 0.f}};
    std::vector<std::uint32_t> weightlessMgr{101};
    OwnedVariantSource const noWeight =
        AdaptLoadedGroup(weightless, weightlessMgr, true, 10, member, properties, suffixes, true);
    ok = Check(!noWeight.complete && noWeight.partial, "a non-positive weight is not an authoritative member") && ok;

    RetainedCount entry;
    entry.entryId = 66;
    entry.bagsKnown = true;
    entry.bankKnown = true;
    entry.equippedKnown = true;
    entry.gemsKnown = true;
    entry.equipped = 1;
    RetainedCount category;
    category.limitCategory = 3;
    category.bagsKnown = true;
    category.bankKnown = true;
    category.equippedKnown = true;
    category.gemsKnown = true;
    category.equipped = 1;
    std::vector<RetainedCount> retained{entry, category};
    ApprovedOriginal original;
    original.entryId = 66;
    original.propertyKnown = true;
    original.slot = 4;
    original.approved = true;
    original.guid = 5;
    original.guidKnown = true;
    original.limitCategory = 3;
    std::vector<ApprovedOriginal> originals{original};
    CollectorRequest counts;
    counts.population = CountPopulation::RandomBot;
    counts.populationVerified = true;
    counts.retained = retained;
    counts.approvedOriginals = originals;
    CollectorResult projected;
    ok = Check(ProjectFinalCounts(counts, projected) && projected.projectedCounts[0].equipped == 0 &&
               projected.projectedCounts[1].equipped == 0,
            "the entry count and the HAVE category both drop") &&
         ok;
    originals.push_back(original);
    counts.approvedOriginals = originals;
    CollectorResult duplicatedOriginal;
    ok = Check(!ProjectFinalCounts(counts, duplicatedOriginal), "a duplicate slot or guid fails closed") && ok;

    CandidateTemplate left;
    left.entry = 1;
    left.slot = 4;
    left.loaded = true;
    left.propertyGroup = 10;
    CandidateTemplate right = left;
    right.suffixGroup = 20;
    ok = Check(!SameTemplateIdentity(left, right) && ConflictingTemplates(left, right),
            "different property and suffix groups conflict") &&
         ok;

    struct Meta
    {
        std::uint32_t ID = 0;
        std::uint32_t maxCount = 0;
        std::uint32_t mode = 0;
        std::uint32_t spells[8]{};
        std::uint32_t items_to_triggerspell[8]{};
    };
    struct Proto
    {
        std::uint32_t ItemId = 66;
        std::uint32_t Class = 4;
        std::uint32_t SubClass = 1;
        std::uint32_t Quality = 3;
        std::uint32_t Flags = 0;
        std::uint32_t InventoryType = 5;
        std::uint32_t AllowableClass = 0xFFFFFFFFu;
        std::uint32_t ItemLevel = 60;
        std::uint32_t RequiredLevel = 1;
        std::int32_t MaxCount = 0;
        std::int32_t RandomProperty = 10;
        std::int32_t RandomSuffix = 0;
        std::uint32_t ItemSet = 0;
        std::uint32_t ItemLimitCategory = 0;
    };
    enum class CacheInventory : std::uint32_t
    {
        Chest = 5
    };
    struct Source
    {
        std::vector<std::uint32_t> chest{66};
        std::vector<std::uint32_t> empty{};
        std::vector<std::uint32_t> const& GetEquipment(std::uint32_t, std::uint8_t, std::uint8_t, std::uint32_t quality)
            const
        {
            return quality == 4 ? chest : empty;
        }
        std::vector<std::uint32_t> const& GetEquipmentNew(std::uint32_t, CacheInventory) const { return empty; }
    };
    Proto proto;
    MockVariants mock;
    mock.core = core;
    mock.mgr = mgr;
    mock.properties = properties;
    mock.suffixes = suffixes;
    Source source;
    NativeCollectQuery query;
    query.request.capSupplied = true;
    query.request.resolvedCap.preserve = false;
    query.request.resolvedCap.cap = 66;
    query.request.qualityLimitKnown = true;
    query.request.qualityLimit = 4;
    query.request.eligibleMask = 1u << 4;
    query.level = 0;
    query.classId = 8;
    query.request.sourceBudget = 4;
    query.request.rowBudget = 4;
    query.request.expansionBudget = 1;
    CollectorResult const bounded = CollectNativeCandidateFacts<CacheInventory>(
        source, [&proto](std::uint32_t) { return &proto; }, [](std::uint32_t) -> Meta const* { return nullptr; },
        [](std::uint32_t) -> Meta const* { return nullptr; }, mock, query, ScoreOf);
    ok = Check(bounded.truncated && bounded.preserve && !bounded.claimedEmpty,
            "an expansion budget stops before a partial variant list is certified") &&
         ok;
    query.request.expansionBudget = 8;
    CollectorResult const wired = CollectNativeCandidateFacts<CacheInventory>(
        source, [&proto](std::uint32_t) { return &proto; }, [](std::uint32_t) -> Meta const* { return nullptr; },
        [](std::uint32_t) -> Meta const* { return nullptr; }, mock, query, ScoreOf);
    ok = Check(wired.preserve && !wired.nativeEquipProven && wired.rows.size() == 2 &&
               wired.rows[0].randomProperty == 101 && wired.rows[1].randomProperty == 102,
            "the native adapter expands a grouped template into signed property ids") &&
         ok;

    if (!ok)
        return 1;
    std::cout << "checks=" << g_checks << "\n";
    return 0;
}
