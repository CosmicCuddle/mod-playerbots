// G7 world rows load completely or not at all (CS01 #59 7069065777, 7070450862). A fake database
// injects failures per table: a failed count, a failed query after a count, a short result, a
// malformed row, and legitimately empty tables. Nothing partial may be published.
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "AutoGearSourceAccess.h"
#include "AutoGearSourceRowLoad.h"

using namespace PlayerbotsAutoGearMasterSource;

namespace
{
int g_checks = 0;
int g_failures = 0;

void Check(bool condition, std::string const& text)
{
    ++g_checks;
    if (!condition)
    {
        ++g_failures;
        std::cerr << "FAIL " << text << "\n";
    }
}

using Row = std::vector<std::int64_t>;

constexpr SourceTableSet Tables{
    {"creature_loot_template", "creature_loot", "uus"},
    {"gameobject_loot_template", "gameobject_loot", "uus"},
    {"reference_loot_template", "reference_loot", "uus"},
    {"item_loot_template", "item_loot", "uus"},
    {"creature_template", "creatures", "uuuuu"},
    {"creature", "creature_spawns", "uhb"},
    {"gameobject_template", "chests", "us"},
    {"gameobject", "chest_spawns", "uhb"},
    {"npc_vendor", "vendor", "s"},
    {"quest_template", "quests", "uuuuuuuuuu"},
};

constexpr std::uint32_t BwlItem = 16900;
constexpr std::uint32_t BwlVendorItem = 16950;
constexpr std::uint32_t BwlQuestItem = 16960;

struct FakeDatabase
{
    std::map<std::string, std::vector<Row>> tables;
    std::set<std::string> countFails, queryFails;
    std::map<std::string, std::uint64_t> countOverride;

    FakeDatabase()
    {
        // A BWL boss drops three items; one is also sold, one is also a quest reward.
        tables["creature_loot"] = {{1, BwlItem, 0}, {1, BwlVendorItem, 0}, {1, BwlQuestItem, 0}};
        tables["gameobject_loot"] = {};
        tables["reference_loot"] = {};
        tables["item_loot"] = {};
        tables["creatures"] = {{1, 0, 0, 0, 1}};
        tables["creature_spawns"] = {{1, 469, 1}};
        tables["chests"] = {};
        tables["chest_spawns"] = {};
        tables["vendor"] = {{BwlVendorItem}, {-200001}};
        tables["quests"] = {{BwlQuestItem, 0, 0, 0, 0, 0, 0, 0, 0, 0}};
    }

    std::optional<std::uint64_t> Count(char const* sql)
    {
        if (countFails.count(sql))
            return std::nullopt;
        if (auto const o = countOverride.find(sql); o != countOverride.end())
            return o->second;
        return tables[sql].size();
    }

    template <class Visit>
    bool Rows(TableQuery const& query, Visit&& visit)
    {
        if (queryFails.count(query.sql))
            return false;
        for (Row const& row : tables[query.sql])
            if (!visit(std::span<std::int64_t const>(row)))
                break;
        return true;
    }
};

std::optional<OwnedWorldRows> Load(FakeDatabase& db, LoadReport& report) { return LoadWorldRows(db, Tables, report); }

bool Offered(OwnedWorldRows const& rows, std::uint32_t item, std::uint8_t stage)
{
    SourceIndex const index = SourceIndex::Build(rows.View());
    PlayerbotsAutoGearSourceAccess::Filter const filter{&index, stage, {}};
    return filter.Allows(item);
}

void TestCompleteLoad()
{
    FakeDatabase db;
    LoadReport report;
    auto const rows = Load(db, report);
    Check(rows.has_value() && report.failure == LoadFailure::None, "complete rows load");
    if (!rows)
        return;
    Check(rows->vendorItems.size() == 1 && rows->vendorItems[0] == BwlVendorItem,
          "a negative vendor item (vendor-list reference) is skipped");
    Check(rows->questRewardItems.size() == 1, "quest reward zeros are skipped");
    Check(!Offered(*rows, BwlItem, 1), "BWL-only item is refused at stage 1");
    Check(Offered(*rows, BwlVendorItem, 1), "BWL item also sold is offered");
    Check(Offered(*rows, BwlQuestItem, 1), "BWL item also a quest reward is offered");
}

void TestFailuresPublishNothing()
{
    struct Case
    {
        char const* sql;
        bool countFails;
        bool queryFails;
        LoadFailure expected;
        char const* table;
    };
    Case const cases[] = {
        {"vendor", true, false, LoadFailure::CountFailed, "npc_vendor"},
        {"vendor", false, true, LoadFailure::QueryFailed, "npc_vendor"},
        {"quests", true, false, LoadFailure::CountFailed, "quest_template"},
        {"quests", false, true, LoadFailure::QueryFailed, "quest_template"},
        {"reference_loot", true, false, LoadFailure::CountFailed, "reference_loot_template"},
        {"gameobject_loot", true, false, LoadFailure::CountFailed, "gameobject_loot_template"},
        {"item_loot", true, false, LoadFailure::CountFailed, "item_loot_template"},
        {"chests", true, false, LoadFailure::CountFailed, "gameobject_template"},
        {"creature_loot", false, true, LoadFailure::QueryFailed, "creature_loot_template"},
    };
    for (Case const& c : cases)
    {
        FakeDatabase db;
        if (c.countFails)
            db.countFails.insert(c.sql);
        if (c.queryFails)
            db.queryFails.insert(c.sql);
        LoadReport report;
        auto const rows = Load(db, report);
        std::string const what = std::string(c.table) + (c.countFails ? " count" : " query") + " failure";
        Check(!rows.has_value(), what + ": nothing published");
        Check(report.failure == c.expected && report.table && std::string(report.table) == c.table,
              what + ": reported on the right table");
    }

    // An empty table that answered an empty query is not a failure, but a query that answered
    // nothing after rows were counted is.
    FakeDatabase emptyVendor;
    emptyVendor.tables["vendor"] = {};
    LoadReport report;
    Check(Load(emptyVendor, report).has_value(), "a legitimately empty vendor table loads");
    FakeDatabase emptyAll;
    emptyAll.tables["vendor"] = {};
    emptyAll.tables["quests"] = {};
    emptyAll.tables["item_loot"] = {};
    Check(Load(emptyAll, report).has_value(), "legitimately empty vendor, quest and container tables load");

    // Fewer rows than counted (a result cut short).
    FakeDatabase shortVendor;
    shortVendor.countOverride["vendor"] = 5;
    Check(!Load(shortVendor, report).has_value() && report.failure == LoadFailure::RowCountMismatch &&
              report.expected == 5 && report.loaded == 2,
          "a short vendor result publishes nothing");

    // No creature rows: nothing can be proven.
    FakeDatabase noCreatures;
    noCreatures.tables["creatures"] = {};
    Check(!Load(noCreatures, report).has_value() && report.failure == LoadFailure::NoCreatureRows,
          "no creature templates publishes nothing");
    FakeDatabase noLoot;
    noLoot.tables["creature_loot"] = {};
    Check(!Load(noLoot, report).has_value() && report.failure == LoadFailure::NoCreatureRows,
          "no creature loot publishes nothing");

    // A failure stops the load at that table: later tables are not even queried.
    FakeDatabase early;
    early.countFails.insert("creature_loot");
    early.queryFails.insert("quests");
    Check(!Load(early, report).has_value() && std::string(report.table) == "creature_loot_template",
          "the first failing table is the one reported");

    // Without an index the command-level filter offers nothing (the command keeps the gear).
    PlayerbotsAutoGearSourceAccess::Filter const none{nullptr, 18, {}};
    Check(!none.Allows(BwlVendorItem), "no published index: nothing is offered");
}

void TestSignedReference()
{
    using RowValues::Reference;
    std::int64_t const minimum = std::numeric_limits<std::int32_t>::min();
    Check(!Reference(minimum).has_value(), "INT32_MIN reference is malformed");
    Check(Reference(-1) == 1u, "-1 reference follows reference 1");
    Check(Reference(0) == 0u, "0 is no reference");
    Check(Reference(34377) == 34377u, "positive reference");
    Check(Reference(-34377) == 34377u, "ordinary negative reference follows its magnitude");
    Check(Reference(std::numeric_limits<std::int32_t>::max()) == 2147483647u, "INT32_MAX reference");
    Check(!Reference(std::int64_t{1} << 32).has_value(), "a value outside int32 is malformed");

    FakeDatabase db;
    db.tables["creature_loot"].push_back({1, 1234, minimum});
    LoadReport report;
    Check(!Load(db, report).has_value() && report.failure == LoadFailure::MalformedRow &&
              std::string(report.table) == "creature_loot_template",
          "an INT32_MIN loot reference publishes nothing");

    FakeDatabase negative;
    negative.tables["reference_loot"] = {{34377, 5555, 0}};
    negative.tables["creature_loot"].push_back({1, 0, -34377});
    auto const rows = Load(negative, report);
    Check(rows.has_value() && !Offered(*rows, 5555, 1),
          "a negative reference reaches its table: the referenced BWL item is refused at stage 1");
}

void TestMalformedRows()
{
    LoadReport report;
    FakeDatabase wide;
    wide.tables["vendor"] = {{BwlVendorItem, 7}};
    Check(!Load(wide, report).has_value() && report.failure == LoadFailure::MalformedRow, "a too-wide row");
    FakeDatabase negativeEntry;
    negativeEntry.tables["creature_loot"].push_back({-5, 1, 0});
    Check(!Load(negativeEntry, report).has_value() && report.failure == LoadFailure::MalformedRow,
          "a negative loot entry");
    FakeDatabase hugeItem;
    hugeItem.tables["quests"] = {{std::int64_t{1} << 33, 0, 0, 0, 0, 0, 0, 0, 0, 0}};
    Check(!Load(hugeItem, report).has_value() && report.failure == LoadFailure::MalformedRow,
          "a quest item above uint32");
    FakeDatabase badMask;
    badMask.tables["creature_spawns"] = {{1, 469, 300}};
    Check(!Load(badMask, report).has_value() && report.failure == LoadFailure::MalformedRow, "a spawn mask above 255");

    // Signed columns: a non-positive chest loot id is no loot, not a malformed row.
    FakeDatabase chests;
    chests.tables["chests"] = {{10, 0}, {11, -3}, {12, 77}};
    auto const rows = Load(chests, report);
    Check(rows.has_value() && rows->chests.size() == 1 && rows->chests[0].lootId == 77,
          "chests without a positive loot id are skipped");
}
}  // namespace

int main()
{
    TestCompleteLoad();
    TestFailuresPublishNothing();
    TestSignedReference();
    TestMalformedRows();
    std::cout << "checks=" << g_checks << " failures=" << g_failures << "\n";
    return g_failures == 0 ? 0 : 1;
}
