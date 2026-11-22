/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_SOURCE_ROW_LOAD_H
#define PLAYERBOTS_AUTOGEAR_SOURCE_ROW_LOAD_H

#include <cstdint>
#include <limits>
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
// 'u' uint32 (INT UNSIGNED), 's' int32 (INT), 'h' uint16 (SMALLINT UNSIGNED), 'b' uint8 (TINYINT UNSIGNED).
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

// Owns what WorldRows points at.
struct OwnedWorldRows
{
    std::vector<LootRow> creatureLoot, gameobjectLoot, referenceLoot, itemLoot;
    std::vector<CreatureTemplateRow> creatures;
    std::vector<SpawnRow> creatureSpawns, chestSpawns;
    std::vector<ChestTemplateRow> chests;
    std::vector<std::uint32_t> vendorItems, questRewardItems;

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

// The ten G7 queries, in SourceQueries order (AutoGearSourceIndexStore.h).
struct SourceTableSet
{
    TableQuery creatureLoot, gameobjectLoot, referenceLoot, itemLoot, creatures, creatureSpawns, chests, chestSpawns,
        vendorItems, questRewards;
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
    loader.Table(tables.questRewards,
                 [&](std::span<std::int64_t const> r)
                 {
                     for (std::int64_t value : r)
                         if (!Unsigned32(value))
                             return false;
                     for (std::int64_t value : r)
                         if (value)
                             rows.questRewardItems.push_back(static_cast<std::uint32_t>(value));
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
