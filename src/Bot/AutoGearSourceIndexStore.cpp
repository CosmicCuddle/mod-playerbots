/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "AutoGearSourceIndexStore.h"

#include <atomic>
#include <cstdlib>
#include <memory>
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

void LoadLoot(char const* sql, std::vector<LootRow>& rows)
{
    QueryResult result = WorldDatabase.Query(sql);
    if (!result)
        return;  // an empty table is valid
    do
    {
        Field* fields = result->Fetch();
        // Reference is a signed column; Core's LootMgr reads it as int32 and follows std::abs(reference).
        rows.push_back(
            {fields[0].Get<uint32>(), fields[1].Get<uint32>(), static_cast<uint32>(std::abs(fields[2].Get<int32>()))});
    } while (result->NextRow());
}

void LoadSpawns(char const* sql, std::vector<SpawnRow>& rows)
{
    QueryResult result = WorldDatabase.Query(sql);
    if (!result)
        return;
    do
    {
        Field* fields = result->Fetch();
        rows.push_back({fields[0].Get<uint32>(), fields[1].Get<uint32>(), fields[2].Get<uint8>()});
    } while (result->NextRow());
}
}  // namespace

void SourceIndexStore::LoadFromWorldDatabase()
{
    uint32 const started = getMSTime();
    std::vector<LootRow> creatureLoot, gameobjectLoot, referenceLoot, itemLoot;
    std::vector<CreatureTemplateRow> creatures;
    std::vector<SpawnRow> creatureSpawns, chestSpawns;
    std::vector<ChestTemplateRow> chests;
    std::vector<std::uint32_t> vendorItems, questRewardItems;

    LoadLoot(SourceQueries::CreatureLoot, creatureLoot);
    LoadLoot(SourceQueries::GameobjectLoot, gameobjectLoot);
    LoadLoot(SourceQueries::ReferenceLoot, referenceLoot);
    LoadLoot(SourceQueries::ItemLoot, itemLoot);
    LoadSpawns(SourceQueries::CreatureSpawns, creatureSpawns);
    LoadSpawns(SourceQueries::ChestSpawns, chestSpawns);
    if (QueryResult result = WorldDatabase.Query(SourceQueries::Creatures))
        do
        {
            Field* fields = result->Fetch();
            creatures.push_back({fields[0].Get<uint32>(),
                                 {fields[1].Get<uint32>(), fields[2].Get<uint32>(), fields[3].Get<uint32>()},
                                 fields[4].Get<uint32>()});
        } while (result->NextRow());
    if (QueryResult result = WorldDatabase.Query(SourceQueries::Chests))
        do
        {
            Field* fields = result->Fetch();
            if (int32 const lootId = fields[1].Get<int32>(); lootId > 0)  // Data1 is a signed column
                chests.push_back({fields[0].Get<uint32>(), static_cast<uint32>(lootId)});
        } while (result->NextRow());
    if (QueryResult result = WorldDatabase.Query(SourceQueries::VendorItems))
        do
        {
            // A negative item names another vendor list (Core's LoadReferenceVendor); that list's own
            // rows are already in this result.
            if (int32 const item = result->Fetch()[0].Get<int32>(); item > 0)
                vendorItems.push_back(static_cast<uint32>(item));
        } while (result->NextRow());
    if (QueryResult result = WorldDatabase.Query(SourceQueries::QuestRewards))
        do
        {
            Field* fields = result->Fetch();
            for (uint32 i = 0; i < 10; ++i)
                if (uint32 const item = fields[i].Get<uint32>())
                    questRewardItems.push_back(item);
        } while (result->NextRow());

    if (creatures.empty() || creatureSpawns.empty() || creatureLoot.empty())
    {
        LOG_ERROR("server.loading",
                  ">> AutoGear source index not built: creature rows missing. Raid tiers stay at the era floor.");
        return;
    }

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
    storedIndex = std::make_unique<SourceIndex const>(SourceIndex::Build(rows));
    publishedIndex.store(storedIndex.get(), std::memory_order_release);
    LOG_INFO("server.loading",
             ">> AutoGear source index: {} raid-sourced items from {} creature, {} chest, {} reference loot rows "
             "in {} ms",
             storedIndex->RaidEntries(), creatureLoot.size(), gameobjectLoot.size(), referenceLoot.size(),
             GetMSTimeDiffToNow(started));
}

SourceIndex const* SourceIndexStore::Get() { return publishedIndex.load(std::memory_order_acquire); }
}  // namespace PlayerbotsAutoGearMasterSource
