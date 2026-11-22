// G7 world rows load completely or not at all (CS01 #59 7069065777, 7070450862). A fake database
// injects failures per table: a failed count, a failed query after a count, a short result, a
// malformed row, and legitimately empty tables. Nothing partial may be published.
#include <array>
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
    {"quest_template", "quests", "uuuuuuuuuuuuuuuuuussssuuuu"},
    {"quest_template_addon", "quest_chains", "ushu"},
    {"trainer_spell", "trainer_spells", "u"},
    {"item_template spells", "item_spells", "usbsbsbsbsb"},
    {"quest_template reward spells", "quest_spells", "usu"},
    {"npc_vendor rows", "vendor_rows", "usu"},
    {"item_template reputation", "item_reputations", "uh"},
    {"creature_onkill_reputation", "kill_reputations", "un"},
    {"quest_template reputation", "quest_reputations", "uhsshsshsshsshss"},
};

// One quest_template row: ID, 10 rewards, 6 hand-ins, start item, 4 kill targets, 4 hand-in sources.
Row QuestRow(std::int64_t id, std::int64_t reward, std::int64_t handIn = 0, std::int64_t kill = 0)
{
    Row row(26, 0);
    row[0] = id;
    row[1] = reward;
    row[11] = handIn;
    row[18] = kill;
    return row;
}

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
        tables["quests"] = {QuestRow(500, BwlQuestItem)};
        tables["quest_chains"] = {};
        tables["trainer_spells"] = {};
        tables["item_spells"] = {};
        tables["quest_spells"] = {{9001, 0, 0}};
        tables["vendor_rows"] = {{2, BwlVendorItem, 0}};
        tables["item_reputations"] = {};
        tables["kill_reputations"] = {};
        tables["quest_reputations"] = {};
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
    Check(rows->quests.size() == 1 && rows->quests[0].id == 500 && rows->quests[0].rewards[0] == BwlQuestItem,
          "quest rows keep their id and rewards");
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
        {"trainer_spells", true, false, LoadFailure::CountFailed, "trainer_spell"},
        {"item_spells", true, false, LoadFailure::CountFailed, "item_template spells"},
        {"quest_spells", false, true, LoadFailure::QueryFailed, "quest_template reward spells"},
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
    hugeItem.tables["quests"] = {QuestRow(501, std::int64_t{1} << 33)};
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
void TestQuestRows()
{
    LoadReport report;
    FakeDatabase chain;
    chain.tables["quests"] = {QuestRow(600, 0, 19003), QuestRow(601, 19383, 0, 11583)};
    chain.tables["quest_chains"] = {{601, 600, 0, 0}, {602, 0, 0, 0}, {603, -600, 0, 0}};
    auto const rows = Load(chain, report);
    Check(rows.has_value(), "quest rows with hand-ins, kill targets and chains load");
    if (rows)
    {
        Check(rows->quests[0].items[0] == 19003 && rows->quests[1].npcOrGo[0] == 11583,
              "hand-in and kill target columns land in place");
        Check(rows->questChains.size() == 2 && rows->questChains[0].prev == 600 && rows->questChains[1].prev == -600,
              "chain rows without a previous quest are skipped; negative PrevQuestID is kept");
    }
    FakeDatabase badChain;
    badChain.tables["quest_chains"] = {{601, std::numeric_limits<std::int32_t>::min(), 0, 0}};
    Check(!Load(badChain, report).has_value() && report.failure == LoadFailure::MalformedRow,
          "an INT32_MIN PrevQuestID is malformed");
    FakeDatabase chainFails;
    chainFails.countFails.insert("quest_chains");
    Check(!Load(chainFails, report).has_value() && std::string(report.table) == "quest_template_addon",
          "a failed quest chain table publishes nothing");
    FakeDatabase badKill;
    badKill.tables["quests"] = {QuestRow(700, 1, 0, std::int64_t{1} << 31)};
    Check(!Load(badKill, report).has_value() && report.failure == LoadFailure::MalformedRow,
          "a kill target outside int32 is malformed");
}
// Trainer, item and quest spell rows load, and resolve into how players get each spell.
void TestSpellRows()
{
    LoadReport report;
    FakeDatabase db;
    db.tables["trainer_spells"] = {{3914}};
    // A recipe (Learning on use, the recipe spell on learn), and an item whose use creates another.
    db.tables["item_spells"] = {{2408, 483, 0, 3915, 6, 0, 0, 0, 0, 0, 0}, {9372, 0, 0, 21160, 0, 0, 0, 0, 0, 0, 0}};
    db.tables["quest_spells"] = {{8100, -1, 0}, {8101, 24250, 0}, {8102, 0, 7421}};
    auto rows = Load(db, report);
    Check(rows.has_value(), "spell rows load");
    if (!rows)
        return;
    Check(rows->trainerSpells == std::vector<std::uint32_t>{3914}, "trainer spells land in place");
    Check(rows->itemSpells.size() == 2 && rows->itemSpells[0].spells[1] == 3915 &&
              rows->itemSpells[0].triggers[1] == ItemSpellTriggerLearnSpell,
          "item spell slots keep their spell and trigger");
    Check(rows->questSpells.size() == 3 && rows->questSpells[0].rewardSpell == -1 &&
              rows->questSpells[2].displaySpell == 7421,
          "quest reward spells keep a negative RewardSpell as read");

    std::multimap<std::uint32_t, std::uint32_t> const learns{{483, 0}, {3914, 3916}};
    std::vector<std::uint32_t> const skill{2018};
    auto const sources = ResolveSpellSources(rows->trainerSpells, skill, rows->itemSpells, rows->questSpells, learns);
    auto has = [&](std::uint32_t spell, SpellSource kind, std::uint32_t id)
    {
        for (SpellSourceRow const& s : sources)
            if (s.spell == spell && s.kind == kind && s.id == id)
                return true;
        return false;
    };
    Check(has(3914, SpellSource::Taught, 0) && has(3916, SpellSource::Taught, 0),
          "a trainer spell and the spell it teaches are taught");
    Check(has(2018, SpellSource::Taught, 0), "a skill-learned spell is taught");
    Check(has(3915, SpellSource::Item, 2408), "a recipe item teaches its spell");
    Check(has(21160, SpellSource::Item, 9372), "an item's use spell follows that item");
    Check(has(24250, SpellSource::Quest, 8101) && has(7421, SpellSource::Quest, 8102),
          "quest reward and display spells follow the quest");
    bool zeroOrNegative = false;
    for (SpellSourceRow const& s : sources)
        zeroOrNegative = zeroOrNegative || s.spell == 0;
    Check(!zeroOrNegative, "zero, negative and empty learn targets add no source");

    FakeDatabase badTrigger;
    badTrigger.tables["item_spells"] = {{1, 2, 300, 0, 0, 0, 0, 0, 0, 0, 0}};
    Check(!Load(badTrigger, report).has_value() && report.failure == LoadFailure::MalformedRow &&
              std::string(report.table) == "item_template spells",
          "a spell trigger above 255 is malformed");
    FakeDatabase badQuestSpell;
    badQuestSpell.tables["quest_spells"] = {{1, std::int64_t{1} << 31, 0}};
    Check(!Load(badQuestSpell, report).has_value() && report.failure == LoadFailure::MalformedRow,
          "a RewardSpell outside int32 is malformed");
}
// Vendor rows, reputation rows and the quest addon columns load, and resolve.
void TestVendorAndReputationRows()
{
    LoadReport report;
    FakeDatabase db;
    // Vendor 10 sells 500 for ExtendedCost 7 and includes list 20; list 20 includes 30, which includes 20.
    db.tables["vendor_rows"] = {{10, 500, 7}, {10, -20, 0}, {20, 600, 99}, {20, -30, 0}, {30, 700, 0}, {30, -20, 0}};
    db.tables["item_reputations"] = {{19764, 270}};
    db.tables["kill_reputations"] = {{11380, 270}, {11381, -5}};
    // Quest 8041: +Zandalar by value index 5; quest 8042: an override of -1000 is a loss despite value 5.
    Row gain(16, 0), loss(16, 0);
    gain[0] = 8041;
    gain[1] = 270;
    gain[2] = 5;
    loss[0] = 8042;
    loss[1] = 270;
    loss[2] = 5;
    loss[3] = -1000;
    loss[4] = 529;
    loss[5] = 0;
    loss[6] = 250;  // a positive override with value 0 is a gain
    db.tables["quest_reputations"] = {gain, loss};
    db.tables["quest_chains"] = {{8041, 0, 270, 0}, {8240, 0, 0, 2}, {8241, 0, 0, 1}, {8242, 0, 0, 0}};
    auto rows = Load(db, report);
    Check(rows.has_value(), "vendor, reputation and quest addon rows load");
    if (!rows)
        return;
    Check(rows->vendorRows.size() == 6 && rows->vendorRows[1].item == -20, "vendor rows keep negative references");
    Check(rows->itemReputations.size() == 1 && rows->itemReputations[0].faction == 270, "item reputation lands");
    Check(rows->questChains.size() == 2 && rows->questChains[0].requiredFaction == 270 &&
              !rows->questChains[0].eventOnly && rows->questChains[1].id == 8240 && rows->questChains[1].eventOnly,
          "addon rows keep the required reputation and the event flag; other flags and empty rows are skipped");

    std::map<std::uint32_t, std::array<std::uint32_t, 5>> const costs{{7, {20888, 0, 0, 0, 0}}};
    auto const offers = ResolveVendorOffers(rows->vendorRows, costs);
    auto sells = [&](std::uint32_t vendor, std::uint32_t item, std::uint32_t cost)
    {
        for (VendorOfferRow const& o : offers)
            if (o.vendor == vendor && o.item == item && o.costItems[0] == cost)
                return true;
        return false;
    };
    Check(sells(10, 500, 20888), "an ExtendedCost lists the items it asks for");
    Check(sells(10, 600, 0) && sells(10, 700, 0), "a referenced list is sold by the including vendor, nested too");
    Check(sells(20, 600, 0) && sells(20, 700, 0) && sells(30, 700, 0) && sells(30, 600, 0),
          "a reference loop terminates and keeps every list");
    Check(offers.size() == 7, "no duplicates from the loop, no rows for references themselves");

    auto const sources = ResolveReputationSources(rows->killReputations, rows->questReputations);
    auto has = [&](std::uint32_t faction, bool quest, std::uint32_t id)
    {
        for (ReputationSourceRow const& r : sources)
            if (r.faction == faction && r.quest == quest && r.id == id)
                return true;
        return false;
    };
    Check(has(270, false, 11380) && !has(0, false, 11381) && sources.size() == 3,
          "kills raise their faction; a negative faction id adds nothing");
    Check(has(270, true, 8041) && !has(270, true, 8042), "a value index gains, a negative override loses");
    Check(has(529, true, 8042), "a positive override gains");

    FakeDatabase badReference;
    badReference.tables["vendor_rows"] = {{10, std::numeric_limits<std::int32_t>::min(), 0}};
    Check(!Load(badReference, report).has_value() && report.failure == LoadFailure::MalformedRow &&
              std::string(report.table) == "npc_vendor rows",
          "an INT32_MIN vendor reference is malformed");
    FakeDatabase badFaction;
    badFaction.tables["kill_reputations"] = {{1, 40000}};
    Check(!Load(badFaction, report).has_value() && report.failure == LoadFailure::MalformedRow,
          "a kill faction outside SMALLINT is malformed");
    FakeDatabase repFails;
    repFails.tables["quest_reputations"] = {gain};
    repFails.queryFails.insert("quest_reputations");
    Check(!Load(repFails, report).has_value() && report.failure == LoadFailure::QueryFailed &&
              std::string(report.table) == "quest_template reputation",
          "a failed reputation query publishes nothing");
    FakeDatabase dropBad;
    Row drop = QuestRow(900, 1);
    drop[22] = -1;
    dropBad.tables["quests"] = {drop};
    Check(!Load(dropBad, report).has_value() && report.failure == LoadFailure::MalformedRow,
          "a negative ItemDrop is malformed");
    FakeDatabase dropOk;
    Row drop2 = QuestRow(901, 1);
    drop2[22] = 19881;
    dropOk.tables["quests"] = {drop2};
    auto const dropRows = Load(dropOk, report);
    Check(dropRows.has_value() && dropRows->quests[0].items[7] == 19881, "ItemDrop1 lands after the start item");
}
}  // namespace

int main()
{
    TestCompleteLoad();
    TestFailuresPublishNothing();
    TestSignedReference();
    TestMalformedRows();
    TestQuestRows();
    TestSpellRows();
    TestVendorAndReputationRows();
    std::cout << "checks=" << g_checks << " failures=" << g_failures << "\n";
    return g_failures == 0 ? 0 : 1;
}
