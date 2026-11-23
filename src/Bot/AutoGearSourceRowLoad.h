/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_SOURCE_ROW_LOAD_H
#define PLAYERBOTS_AUTOGEAR_SOURCE_ROW_LOAD_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "AutoGearMasterSourceIndex.h"

// Loads the G7 world rows completely or not at all. Vendor and quest rows remove a raid-only
// verdict, so a vendor or quest query that silently failed would turn an item with an ordinary
// source into "raid proven". Every table is therefore counted with the same SQL first: a failed
// count, a missing result where rows were counted, a different number of rows, or a malformed row
// means no index is published (capped commands then keep the bot's gear). A table that counts zero
// rows is a valid empty table.
//
// Pure: the caller supplies the database through Source (see SourceIndexStore for the real one and
// the tests for fakes). Values arrive widened to int64 so the checks below see the stored number.
namespace PlayerbotsAutoGearMasterSource
{
enum class LoadFailure : std::uint8_t
{
    None,
    CountFailed,       // COUNT(*) over the table's SQL returned no result
    QueryFailed,       // rows were counted but the query returned no result
    RowCountMismatch,  // a different number of rows than counted
    MalformedRow,      // wrong width or a value outside its column's domain
    NoCreatureRows     // creature templates, spawns or loot are empty: nothing can be proven
};

// kinds: one letter per column, the exact column type the database adapter reads it as:
// 'u' uint32 (INT UNSIGNED), 's' int32 (INT), 'h' uint16 (SMALLINT UNSIGNED), 'n' int16 (SMALLINT),
// 'b' uint8 (TINYINT UNSIGNED).
struct TableQuery
{
    char const* name;
    char const* sql;
    char const* kinds;

    constexpr std::size_t Columns() const { return std::string_view(kinds).size(); }
};

struct LoadReport
{
    LoadFailure failure = LoadFailure::None;
    char const* table = nullptr;  // the table that failed
    std::uint64_t expected = 0;
    std::uint64_t loaded = 0;
};

// item_template spellid_N / spelltrigger_N, one row per item with its five spell slots.
struct ItemSpellRow
{
    std::uint32_t item = 0;
    std::array<std::int32_t, 5> spells{};
    std::array<std::uint8_t, 5> triggers{};
};

// quest_template RewardSpell (cast on completion) and RewardDisplaySpell (learned on completion).
struct QuestSpellRow
{
    std::uint32_t quest = 0;
    std::int32_t rewardSpell = 0;
    std::uint32_t displaySpell = 0;
};

inline constexpr std::int64_t QuestSpecialFlagExplorationOrEvent = 0x2;  // QUEST_SPECIAL_FLAGS_EXPLORATION_OR_EVENT
inline constexpr std::uint8_t ItemSpellTriggerOnUse = 0;                 // ITEM_SPELLTRIGGER_ON_USE
inline constexpr std::uint8_t ItemSpellTriggerLearnSpell = 6;            // ITEM_SPELLTRIGGER_LEARN_SPELL_ID

// How players get each spell, from the trainer lists, the skill-learned spells (SkillLineAbility
// AcquireMethod 1 or 2), item spells and quest reward spells. A spell that teaches others
// (SPELL_EFFECT_LEARN_SPELL, `learns`: teacher -> taught) passes its source on to them, one level, as a
// trainer, recipe or quest cast does in Core. An item's use spell counts too: using it may create the
// item (Sul'thraze) or teach a recipe.
inline std::vector<SpellSourceRow> ResolveSpellSources(std::span<std::uint32_t const> trainerSpells,
                                                       std::span<std::uint32_t const> skillSpells,
                                                       std::span<ItemSpellRow const> itemSpells,
                                                       std::span<QuestSpellRow const> questSpells,
                                                       std::multimap<std::uint32_t, std::uint32_t> const& learns)
{
    std::vector<SpellSourceRow> out;
    auto add = [&](std::int64_t spell, SpellSource kind, std::uint32_t id)
    {
        if (spell <= 0 || spell > std::numeric_limits<std::uint32_t>::max())
            return;
        std::uint32_t const id32 = static_cast<std::uint32_t>(spell);
        out.push_back({id32, kind, id});
        auto const [first, last] = learns.equal_range(id32);
        for (auto it = first; it != last; ++it)
            if (it->second)
                out.push_back({it->second, kind, id});
    };
    for (std::uint32_t spell : trainerSpells)
        add(spell, SpellSource::Taught, 0);
    for (std::uint32_t spell : skillSpells)
        add(spell, SpellSource::Taught, 0);
    for (ItemSpellRow const& row : itemSpells)
        for (std::size_t i = 0; i < row.spells.size(); ++i)
            if (row.triggers[i] == ItemSpellTriggerOnUse || row.triggers[i] == ItemSpellTriggerLearnSpell)
                add(row.spells[i], SpellSource::Item, row.item);
    for (QuestSpellRow const& row : questSpells)
    {
        add(row.rewardSpell, SpellSource::Quest, row.quest);
        add(row.displaySpell, SpellSource::Quest, row.quest);
    }
    return out;
}

// npc_vendor as stored: a negative item includes another vendor's list (Core's LoadReferenceVendor).
struct VendorRow
{
    std::uint32_t vendor = 0;
    std::int32_t item = 0;
    std::uint32_t extendedCost = 0;
};

// Every item each vendor sells, with the items its ExtendedCost asks for (`costs`: ItemExtendedCost id ->
// reqitem, from the DBC). Referenced lists are expanded for the including vendor, nested ones too.
template <class Costs>
std::vector<VendorOfferRow> ResolveVendorOffers(std::span<VendorRow const> rows, Costs const& costs)
{
    std::multimap<std::uint32_t, VendorRow> byVendor;
    for (VendorRow const& row : rows)
        byVendor.emplace(row.vendor, row);
    std::vector<VendorOfferRow> out;
    for (auto it = byVendor.begin(); it != byVendor.end(); it = byVendor.upper_bound(it->first))
    {
        std::uint32_t const seller = it->first;
        std::vector<std::uint32_t> lists{seller};
        std::vector<std::uint32_t> seen{seller};
        while (!lists.empty())
        {
            std::uint32_t const list = lists.back();
            lists.pop_back();
            auto const [first, last] = byVendor.equal_range(list);
            for (auto row = first; row != last; ++row)
            {
                VendorRow const& r = row->second;
                if (r.item < 0)
                {
                    std::uint32_t const ref = static_cast<std::uint32_t>(-static_cast<std::int64_t>(r.item));
                    if (std::find(seen.begin(), seen.end(), ref) == seen.end())
                    {
                        seen.push_back(ref);
                        lists.push_back(ref);
                    }
                    continue;
                }
                if (r.item == 0)
                    continue;
                VendorOfferRow offer{seller, static_cast<std::uint32_t>(r.item), {}};
                if (r.extendedCost)
                    if (auto const cost = costs.find(r.extendedCost); cost != costs.end())
                        offer.costItems = cost->second;
                out.push_back(offer);
            }
        }
    }
    return out;
}

// creature_onkill_reputation, one row per (creature, faction) with a positive value.
struct KillReputationRow
{
    std::uint32_t creature = 0;
    std::int32_t faction = 0;
};

// quest_template RewardFactionID1-5 with RewardFactionValue1-5 and RewardFactionOverride1-5.
struct QuestReputationRow
{
    std::uint32_t quest = 0;
    std::array<std::uint32_t, 5> factions{};
    std::array<std::int32_t, 5> values{};
    std::array<std::int32_t, 5> overrides{};
};

// Ways to gain reputation. A quest grants reputation when its override is positive, or without an
// override when its value (an index into QuestFactionReward.dbc, negative for losses) is positive.
inline std::vector<ReputationSourceRow> ResolveReputationSources(std::span<KillReputationRow const> kills,
                                                                 std::span<QuestReputationRow const> quests)
{
    std::vector<ReputationSourceRow> out;
    for (KillReputationRow const& kill : kills)
        if (kill.faction > 0 && kill.creature)
            out.push_back({static_cast<std::uint32_t>(kill.faction), false, kill.creature});
    for (QuestReputationRow const& quest : quests)
        for (std::size_t i = 0; i < quest.factions.size(); ++i)
        {
            bool const gains = quest.overrides[i] != 0 ? quest.overrides[i] > 0 : quest.values[i] > 0;
            if (quest.factions[i] && gains)
                out.push_back({quest.factions[i], true, quest.quest});
        }
    return out;
}

// instance_encounters with creditType 0: the creature whose kill completes a DungeonEncounter.dbc entry.
struct EncounterCreditRow
{
    std::uint32_t encounter = 0;
    std::uint32_t creature = 0;
};

// Where an encounter happens (DungeonEncounter.dbc mapId and difficulty).
struct EncounterPlace
{
    std::uint32_t map = 0;
    std::uint32_t difficulty = 0;
};

// Raid bosses that a script summons (Nefarian, Ragnaros, Ouro, the Edge of Madness bosses) have no row in
// `creature`, so their loot cannot be placed and stays Unknown. Their encounter says where they are
// fought: one spawn row per unspawned credit creature, at the encounter's map and difficulty, in the
// same form `creature` rows have (spawnMask bit = difficulty). Creatures with real spawns are left alone.
template <class Places>
std::vector<SpawnRow> EncounterSpawns(std::span<EncounterCreditRow const> credits, Places const& places,
                                      std::span<SpawnRow const> creatureSpawns)
{
    std::vector<std::uint32_t> spawned;
    spawned.reserve(creatureSpawns.size());
    for (SpawnRow const& spawn : creatureSpawns)
        spawned.push_back(spawn.id);
    std::sort(spawned.begin(), spawned.end());
    std::vector<SpawnRow> out;
    for (EncounterCreditRow const& credit : credits)
    {
        if (!credit.creature || std::binary_search(spawned.begin(), spawned.end(), credit.creature))
            continue;
        auto const place = places.find(credit.encounter);
        if (place == places.end() || place->second.difficulty >= 8)
            continue;  // an encounter the DBC does not know, or a difficulty no spawn mask can hold
        out.push_back({credit.creature, place->second.map, static_cast<std::uint8_t>(1u << place->second.difficulty)});
    }
    return out;
}

// Owns what WorldRows points at.
struct OwnedWorldRows
{
    std::vector<LootRow> creatureLoot, gameobjectLoot, referenceLoot, itemLoot;
    std::vector<CreatureTemplateRow> creatures;
    std::vector<SpawnRow> creatureSpawns, chestSpawns;
    std::vector<ChestTemplateRow> chests;
    std::vector<std::uint32_t> vendorItems, questRewardItems;
    std::vector<QuestRow> quests;
    std::vector<QuestChainRow> questChains;
    std::vector<std::uint32_t> trainerSpells;
    std::vector<ItemSpellRow> itemSpells;
    std::vector<QuestSpellRow> questSpells;
    // Filled by the caller from the spell data (DBC), not the world database: what each spell creates,
    // and how players get each spell (ResolveSpellSources).
    std::vector<CraftRow> crafts;
    std::vector<SpellSourceRow> spellSources;
    std::vector<VendorRow> vendorRows;
    std::vector<ItemReputationRow> itemReputations;
    std::vector<KillReputationRow> killReputations;
    std::vector<QuestReputationRow> questReputations;
    std::vector<EncounterCreditRow> encounterCredits;
    // Filled by the caller: vendorRows resolved with the ExtendedCost data (DBC), and reputation sources.
    std::vector<VendorOfferRow> vendorOffers;
    std::vector<ReputationSourceRow> reputationSources;

    WorldRows View() const
    {
        WorldRows rows;
        rows.creatureLoot = creatureLoot;
        rows.gameobjectLoot = gameobjectLoot;
        rows.referenceLoot = referenceLoot;
        rows.itemLoot = itemLoot;
        rows.creatures = creatures;
        rows.creatureSpawns = creatureSpawns;
        rows.chests = chests;
        rows.chestSpawns = chestSpawns;
        rows.vendorItems = vendorItems;
        rows.questRewardItems = questRewardItems;
        rows.quests = quests;
        rows.questChains = questChains;
        rows.crafts = crafts;
        rows.spellSources = spellSources;
        rows.vendorOffers = vendorOffers;
        rows.itemReputations = itemReputations;
        rows.reputationSources = reputationSources;
        return rows;
    }
};

namespace RowValues
{
inline constexpr std::int64_t MaxUnsigned32 = std::numeric_limits<std::uint32_t>::max();

constexpr bool Unsigned32(std::int64_t value) { return value >= 0 && value <= MaxUnsigned32; }

// Loot Reference is a signed column; Core's LootMgr follows std::abs(reference). INT32_MIN has no
// int32 magnitude (std::abs would overflow, in Core as well), so that row is malformed.
constexpr std::optional<std::uint32_t> Reference(std::int64_t value)
{
    if (value <= std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::int32_t>::max())
        return std::nullopt;
    return static_cast<std::uint32_t>(value < 0 ? -value : value);
}
}  // namespace RowValues

// Source:
//   std::optional<std::uint64_t> Count(char const* sql)  - COUNT(*) over sql; nullopt when no result
//   bool Rows(TableQuery const& query, Visit visit)
//        - false when the query returned no result; otherwise calls visit(std::span<int64 const>) per
//          row and stops early when visit returns false (the caller sees the shortfall)
template <class Source>
class RowLoader
{
public:
    explicit RowLoader(Source& source) : _source(source) {}

    // Loads one table. OnRow(std::span<int64 const>) returns false for a malformed row.
    template <class OnRow>
    bool Table(TableQuery const& query, OnRow&& onRow)
    {
        if (_report.failure != LoadFailure::None)
            return false;
        std::optional<std::uint64_t> const expected = _source.Count(query.sql);
        if (!expected)
            return Fail(query, LoadFailure::CountFailed, 0, 0);
        if (*expected == 0)
            return true;
        std::uint64_t loaded = 0;
        bool malformed = false;
        bool const answered = _source.Rows(query,
                                           [&](std::span<std::int64_t const> row)
                                           {
                                               if (row.size() != query.Columns() || !onRow(row))
                                               {
                                                   malformed = true;
                                                   return false;
                                               }
                                               ++loaded;
                                               return true;
                                           });
        if (!answered)
            return Fail(query, LoadFailure::QueryFailed, *expected, 0);
        if (malformed)
            return Fail(query, LoadFailure::MalformedRow, *expected, loaded);
        if (loaded != *expected)
            return Fail(query, LoadFailure::RowCountMismatch, *expected, loaded);
        return true;
    }

    LoadReport const& Report() const { return _report; }

private:
    bool Fail(TableQuery const& query, LoadFailure failure, std::uint64_t expected, std::uint64_t loaded)
    {
        _report = LoadReport{failure, query.name, expected, loaded};
        return false;
    }

    Source& _source;
    LoadReport _report{};
};

// The G7 queries, in SourceQueries order (AutoGearSourceIndexStore.h).
struct SourceTableSet
{
    TableQuery creatureLoot, gameobjectLoot, referenceLoot, itemLoot, creatures, creatureSpawns, chests, chestSpawns,
        vendorItems, questRewards, questChains, trainerSpells, itemSpells, questSpells, vendorRows, itemReputations,
        killReputations, questReputations, encounterCredits;
};

template <class Source>
std::optional<OwnedWorldRows> LoadWorldRows(Source& source, SourceTableSet const& tables, LoadReport& report)
{
    using namespace RowValues;
    OwnedWorldRows rows;
    RowLoader<Source> loader(source);
    auto loot = [](std::vector<LootRow>& out)
    {
        return [&out](std::span<std::int64_t const> r)
        {
            std::optional<std::uint32_t> const reference = Reference(r[2]);
            if (!Unsigned32(r[0]) || !Unsigned32(r[1]) || !reference)
                return false;
            out.push_back({static_cast<std::uint32_t>(r[0]), static_cast<std::uint32_t>(r[1]), *reference});
            return true;
        };
    };
    auto spawns = [](std::vector<SpawnRow>& out)
    {
        return [&out](std::span<std::int64_t const> r)
        {
            if (!Unsigned32(r[0]) || !Unsigned32(r[1]) || r[2] < 0 || r[2] > 0xFF)
                return false;
            out.push_back(
                {static_cast<std::uint32_t>(r[0]), static_cast<std::uint32_t>(r[1]), static_cast<std::uint8_t>(r[2])});
            return true;
        };
    };

    loader.Table(tables.creatureLoot, loot(rows.creatureLoot));
    loader.Table(tables.gameobjectLoot, loot(rows.gameobjectLoot));
    loader.Table(tables.referenceLoot, loot(rows.referenceLoot));
    loader.Table(tables.itemLoot, loot(rows.itemLoot));
    loader.Table(tables.creatures,
                 [&](std::span<std::int64_t const> r)
                 {
                     for (std::int64_t value : r)
                         if (!Unsigned32(value))
                             return false;
                     rows.creatures.push_back({static_cast<std::uint32_t>(r[0]),
                                               {static_cast<std::uint32_t>(r[1]), static_cast<std::uint32_t>(r[2]),
                                                static_cast<std::uint32_t>(r[3])},
                                               static_cast<std::uint32_t>(r[4])});
                     return true;
                 });
    loader.Table(tables.creatureSpawns, spawns(rows.creatureSpawns));
    loader.Table(tables.chests,
                 [&](std::span<std::int64_t const> r)
                 {
                     // Data1 is a signed column: a chest without a positive loot id has no loot.
                     if (!Unsigned32(r[0]) || r[1] < std::numeric_limits<std::int32_t>::min() || r[1] > MaxUnsigned32)
                         return false;
                     if (r[1] > 0)
                         rows.chests.push_back({static_cast<std::uint32_t>(r[0]), static_cast<std::uint32_t>(r[1])});
                     return true;
                 });
    loader.Table(tables.chestSpawns, spawns(rows.chestSpawns));
    loader.Table(tables.vendorItems,
                 [&](std::span<std::int64_t const> r)
                 {
                     // A negative item names another vendor list (Core's LoadReferenceVendor), whose own rows
                     // are already in this result.
                     if (r[0] < std::numeric_limits<std::int32_t>::min() || r[0] > MaxUnsigned32)
                         return false;
                     if (r[0] > 0)
                         rows.vendorItems.push_back(static_cast<std::uint32_t>(r[0]));
                     return true;
                 });
    // ID, RewardItem1-4, RewardChoiceItemID1-6, RequiredItemId1-6, StartItem, RequiredNpcOrGo1-4, ItemDrop1-4
    // (the items a hand-in is made from, e.g. Channeler's Heads for the Gurubashi Head Collection).
    loader.Table(
        tables.questRewards,
        [&](std::span<std::int64_t const> r)
        {
            for (std::size_t i = 0; i < 18; ++i)
                if (!Unsigned32(r[i]))
                    return false;
            for (std::size_t i = 22; i < 26; ++i)
                if (!Unsigned32(r[i]))
                    return false;
            for (std::size_t i = 18; i < 22; ++i)
                if (r[i] < std::numeric_limits<std::int32_t>::min() || r[i] > std::numeric_limits<std::int32_t>::max())
                    return false;
            QuestRow quest;
            quest.id = static_cast<std::uint32_t>(r[0]);
            for (std::size_t i = 0; i < 10; ++i)
            {
                quest.rewards[i] = static_cast<std::uint32_t>(r[1 + i]);
                if (quest.rewards[i])
                    rows.questRewardItems.push_back(quest.rewards[i]);
            }
            for (std::size_t i = 0; i < 7; ++i)
                quest.items[i] = static_cast<std::uint32_t>(r[11 + i]);
            for (std::size_t i = 0; i < 4; ++i)
                quest.items[7 + i] = static_cast<std::uint32_t>(r[22 + i]);
            for (std::size_t i = 0; i < 4; ++i)
                quest.npcOrGo[i] = static_cast<std::int32_t>(r[18 + i]);
            rows.quests.push_back(quest);
            return true;
        });
    loader.Table(tables.questChains,
                 [&](std::span<std::int64_t const> r)
                 {
                     // ID, PrevQuestID, RequiredMinRepFaction, SpecialFlags.
                     if (!Unsigned32(r[0]) || r[1] <= std::numeric_limits<std::int32_t>::min() ||
                         r[1] > std::numeric_limits<std::int32_t>::max() || r[2] < 0 || r[2] > 0xFFFF ||
                         !Unsigned32(r[3]))
                         return false;
                     bool const eventOnly = (r[3] & QuestSpecialFlagExplorationOrEvent) != 0;
                     if (r[1] != 0 || r[2] != 0 || eventOnly)
                         rows.questChains.push_back({static_cast<std::uint32_t>(r[0]), static_cast<std::int32_t>(r[1]),
                                                     static_cast<std::uint32_t>(r[2]), eventOnly});
                     return true;
                 });
    loader.Table(tables.trainerSpells,
                 [&](std::span<std::int64_t const> r)
                 {
                     if (!Unsigned32(r[0]))
                         return false;
                     rows.trainerSpells.push_back(static_cast<std::uint32_t>(r[0]));
                     return true;
                 });
    // entry, then spellid_N (signed) and spelltrigger_N (TINYINT UNSIGNED) for N = 1..5.
    loader.Table(tables.itemSpells,
                 [&](std::span<std::int64_t const> r)
                 {
                     if (!Unsigned32(r[0]))
                         return false;
                     ItemSpellRow row;
                     row.item = static_cast<std::uint32_t>(r[0]);
                     for (std::size_t i = 0; i < 5; ++i)
                     {
                         std::int64_t const spell = r[1 + 2 * i];
                         std::int64_t const trigger = r[2 + 2 * i];
                         if (spell < std::numeric_limits<std::int32_t>::min() ||
                             spell > std::numeric_limits<std::int32_t>::max() || trigger < 0 || trigger > 0xFF)
                             return false;
                         row.spells[i] = static_cast<std::int32_t>(spell);
                         row.triggers[i] = static_cast<std::uint8_t>(trigger);
                     }
                     rows.itemSpells.push_back(row);
                     return true;
                 });
    // ID, RewardSpell (signed), RewardDisplaySpell.
    loader.Table(tables.questSpells,
                 [&](std::span<std::int64_t const> r)
                 {
                     if (!Unsigned32(r[0]) || r[1] < std::numeric_limits<std::int32_t>::min() ||
                         r[1] > std::numeric_limits<std::int32_t>::max() || !Unsigned32(r[2]))
                         return false;
                     rows.questSpells.push_back({static_cast<std::uint32_t>(r[0]), static_cast<std::int32_t>(r[1]),
                                                 static_cast<std::uint32_t>(r[2])});
                     return true;
                 });
    auto int32 = [](std::int64_t v)
    { return v >= std::numeric_limits<std::int32_t>::min() && v <= std::numeric_limits<std::int32_t>::max(); };
    // entry, item (signed: a negative item references another list), ExtendedCost.
    loader.Table(tables.vendorRows,
                 [&](std::span<std::int64_t const> r)
                 {
                     if (!Unsigned32(r[0]) || !int32(r[1]) || r[1] == std::numeric_limits<std::int32_t>::min() ||
                         !Unsigned32(r[2]))
                         return false;
                     rows.vendorRows.push_back({static_cast<std::uint32_t>(r[0]), static_cast<std::int32_t>(r[1]),
                                                static_cast<std::uint32_t>(r[2])});
                     return true;
                 });
    loader.Table(
        tables.itemReputations,
        [&](std::span<std::int64_t const> r)
        {
            if (!Unsigned32(r[0]) || r[1] < 0 || r[1] > 0xFFFF)
                return false;
            rows.itemReputations.push_back({static_cast<std::uint32_t>(r[0]), static_cast<std::uint32_t>(r[1])});
            return true;
        });
    loader.Table(
        tables.killReputations,
        [&](std::span<std::int64_t const> r)
        {
            if (!Unsigned32(r[0]) || r[1] < std::numeric_limits<std::int16_t>::min() ||
                r[1] > std::numeric_limits<std::int16_t>::max())
                return false;
            rows.killReputations.push_back({static_cast<std::uint32_t>(r[0]), static_cast<std::int32_t>(r[1])});
            return true;
        });
    loader.Table(
        tables.encounterCredits,
        [&](std::span<std::int64_t const> r)
        {
            if (!Unsigned32(r[0]) || !Unsigned32(r[1]))
                return false;
            rows.encounterCredits.push_back({static_cast<std::uint32_t>(r[0]), static_cast<std::uint32_t>(r[1])});
            return true;
        });
    // ID, then RewardFactionID, RewardFactionValue, RewardFactionOverride for 1..5.
    loader.Table(tables.questReputations,
                 [&](std::span<std::int64_t const> r)
                 {
                     if (!Unsigned32(r[0]))
                         return false;
                     QuestReputationRow row;
                     row.quest = static_cast<std::uint32_t>(r[0]);
                     for (std::size_t i = 0; i < 5; ++i)
                     {
                         std::int64_t const faction = r[1 + 3 * i];
                         if (faction < 0 || faction > 0xFFFF || !int32(r[2 + 3 * i]) || !int32(r[3 + 3 * i]))
                             return false;
                         row.factions[i] = static_cast<std::uint32_t>(faction);
                         row.values[i] = static_cast<std::int32_t>(r[2 + 3 * i]);
                         row.overrides[i] = static_cast<std::int32_t>(r[3 + 3 * i]);
                     }
                     rows.questReputations.push_back(row);
                     return true;
                 });

    report = loader.Report();
    if (report.failure != LoadFailure::None)
        return std::nullopt;
    if (rows.creatures.empty() || rows.creatureSpawns.empty() || rows.creatureLoot.empty())
    {
        report = LoadReport{LoadFailure::NoCreatureRows, tables.creatures.name, 0, 0};
        return std::nullopt;
    }
    return rows;
}
}  // namespace PlayerbotsAutoGearMasterSource

#endif
