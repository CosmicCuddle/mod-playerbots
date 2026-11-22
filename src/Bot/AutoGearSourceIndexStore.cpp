/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "AutoGearSourceIndexStore.h"

#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "DatabaseEnv.h"
#include "Field.h"
#include "Log.h"
#include "QueryResult.h"
#include "Timer.h"

namespace PlayerbotsAutoGearMasterSource
{
namespace
{
std::unique_ptr<SourceIndex const> storedIndex;
std::atomic<SourceIndex const*> publishedIndex{nullptr};

char const* FailureName(LoadFailure failure)
{
    switch (failure)
    {
        case LoadFailure::None:
            return "none";
        case LoadFailure::CountFailed:
            return "row count query failed";
        case LoadFailure::QueryFailed:
            return "query failed";
        case LoadFailure::RowCountMismatch:
            return "row count changed";
        case LoadFailure::MalformedRow:
            return "malformed row";
        case LoadFailure::NoCreatureRows:
            return "no creature rows";
    }
    return "unknown";
}

// The world database as RowLoader sees it. Each column is read with its own type (TableQuery::kinds)
// and widened to int64, so a signed value is never read as unsigned.
class WorldDatabaseSource
{
public:
    std::optional<std::uint64_t> Count(char const* sql)
    {
        QueryResult result = WorldDatabase.Query("SELECT COUNT(*) FROM ({}) AS g7_rows", sql);
        if (!result)
            return std::nullopt;
        return result->Fetch()[0].Get<uint64>();
    }

    template <class Visit>
    bool Rows(TableQuery const& query, Visit&& visit)
    {
        QueryResult result = WorldDatabase.Query(query.sql);
        if (!result)
            return false;
        std::size_t const columns = query.Columns();
        if (result->GetFieldCount() != columns)
        {
            visit(std::span<std::int64_t const>());  // a width mismatch is a malformed row
            return true;
        }
        std::array<std::int64_t, 32> row{};
        do
        {
            Field* fields = result->Fetch();
            for (std::size_t i = 0; i < columns && i < row.size(); ++i)
            {
                switch (query.kinds[i])
                {
                    case 's':
                        row[i] = fields[i].Get<int32>();
                        break;
                    case 'h':
                        row[i] = fields[i].Get<uint16>();
                        break;
                    case 'b':
                        row[i] = fields[i].Get<uint8>();
                        break;
                    default:
                        row[i] = fields[i].Get<uint32>();
                        break;
                }
            }
            if (!visit(std::span<std::int64_t const>(row.data(), std::min(columns, row.size()))))
                break;
        } while (result->NextRow());
        return true;
    }
};
}  // namespace

void SourceIndexStore::LoadFromWorldDatabase()
{
    uint32 const started = getMSTime();
    WorldDatabaseSource source;
    LoadReport report;
    std::optional<OwnedWorldRows> rows = LoadWorldRows(source, SourceTables, report);
    if (!rows)
    {
        LOG_ERROR("server.loading",
                  ">> AutoGear source index not built: {} on {} ({} of {} rows). IP-capped autogear commands keep "
                  "the bot's current gear until the next restart.",
                  FailureName(report.failure), report.table ? report.table : "?", report.loaded, report.expected);
        return;
    }

    storedIndex = std::make_unique<SourceIndex const>(SourceIndex::Build(rows->View()));
    publishedIndex.store(storedIndex.get(), std::memory_order_release);
    LOG_INFO("server.loading",
             ">> AutoGear source index: {} raid-sourced items from {} creature, {} chest, {} reference loot rows "
             "in {} ms",
             storedIndex->RaidEntries(), rows->creatureLoot.size(), rows->gameobjectLoot.size(),
             rows->referenceLoot.size(), GetMSTimeDiffToNow(started));
}

SourceIndex const* SourceIndexStore::Get() { return publishedIndex.load(std::memory_order_acquire); }
}  // namespace PlayerbotsAutoGearMasterSource
