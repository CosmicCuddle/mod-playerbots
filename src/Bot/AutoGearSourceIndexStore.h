/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_SOURCE_INDEX_STORE_H
#define PLAYERBOTS_AUTOGEAR_SOURCE_INDEX_STORE_H

#include "AutoGearMasterSourceIndex.h"
#include "AutoGearSourceRowLoad.h"

namespace PlayerbotsAutoGearMasterSource
{
// The world rows the G7 source index is built from. Kept here, exactly as sent to the world database,
// so the offline coverage harness runs the same SQL.
namespace SourceQueries
{
inline constexpr char const* CreatureLoot = "SELECT Entry, Item, Reference FROM creature_loot_template";
inline constexpr char const* GameobjectLoot = "SELECT Entry, Item, Reference FROM gameobject_loot_template";
inline constexpr char const* ReferenceLoot = "SELECT Entry, Item, Reference FROM reference_loot_template";
inline constexpr char const* ItemLoot = "SELECT Entry, Item, Reference FROM item_loot_template";
inline constexpr char const* Creatures =
    "SELECT entry, difficulty_entry_1, difficulty_entry_2, difficulty_entry_3, lootid FROM creature_template";
inline constexpr char const* CreatureSpawns = "SELECT id, map, spawnMask FROM creature";
inline constexpr char const* Chests = "SELECT entry, Data1 FROM gameobject_template WHERE type = 3";
inline constexpr char const* ChestSpawns = "SELECT id, map, spawnMask FROM gameobject";
inline constexpr char const* VendorItems = "SELECT DISTINCT item FROM npc_vendor";
inline constexpr char const* QuestRewards =
    "SELECT ID, RewardItem1, RewardItem2, RewardItem3, RewardItem4, RewardChoiceItemID1, RewardChoiceItemID2, "
    "RewardChoiceItemID3, RewardChoiceItemID4, RewardChoiceItemID5, RewardChoiceItemID6, RequiredItemId1, "
    "RequiredItemId2, RequiredItemId3, RequiredItemId4, RequiredItemId5, RequiredItemId6, StartItem, "
    "RequiredNpcOrGo1, RequiredNpcOrGo2, RequiredNpcOrGo3, RequiredNpcOrGo4 FROM quest_template";
inline constexpr char const* QuestChains = "SELECT ID, PrevQuestID FROM quest_template_addon";
}  // namespace SourceQueries

// The same queries with the exact column types (see TableQuery). Read completely or not at all.
inline constexpr SourceTableSet SourceTables{
    {"creature_loot_template", SourceQueries::CreatureLoot, "uus"},
    {"gameobject_loot_template", SourceQueries::GameobjectLoot, "uus"},
    {"reference_loot_template", SourceQueries::ReferenceLoot, "uus"},
    {"item_loot_template", SourceQueries::ItemLoot, "uus"},
    {"creature_template", SourceQueries::Creatures, "uuuuu"},
    {"creature", SourceQueries::CreatureSpawns, "uhb"},
    {"gameobject_template", SourceQueries::Chests, "us"},
    {"gameobject", SourceQueries::ChestSpawns, "uhb"},
    {"npc_vendor", SourceQueries::VendorItems, "s"},
    {"quest_template", SourceQueries::QuestRewards, "uuuuuuuuuuuuuuuuuussss"},
    {"quest_template_addon", SourceQueries::QuestChains, "us"},
};

// Built once on the world thread during startup, before map updates run, and immutable afterwards.
// Get() returns nullptr when the index was not built (feature off, or any table failed to load
// completely); callers then get no tier lift, and IP-capped commands keep the bot's gear.
class SourceIndexStore
{
public:
    static void LoadFromWorldDatabase();
    static SourceIndex const* Get();
};
}  // namespace PlayerbotsAutoGearMasterSource

#endif
