/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_SOURCE_INDEX_STORE_H
#define PLAYERBOTS_AUTOGEAR_SOURCE_INDEX_STORE_H

#include "AutoGearMasterSourceIndex.h"
#include "AutoGearSourceAccess.h"
#include "AutoGearSourceRowLoad.h"

namespace PlayerbotsAutoGearMasterSource
{
// The world rows the G7 source index is built from. Kept here, exactly as sent to the world database,
// so the offline coverage harness runs the same SQL. Spell data (what spells create and teach) comes
// from the DBC stores Core loaded before the index (AutoGearSourceIndexStore.cpp).
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
    "RequiredNpcOrGo1, RequiredNpcOrGo2, RequiredNpcOrGo3, RequiredNpcOrGo4, ItemDrop1, ItemDrop2, ItemDrop3, "
    "ItemDrop4 FROM quest_template";
inline constexpr char const* QuestChains =
    "SELECT ID, PrevQuestID, RequiredMinRepFaction, SpecialFlags FROM quest_template_addon";
inline constexpr char const* TrainerSpells = "SELECT DISTINCT SpellId FROM trainer_spell";
inline constexpr char const* ItemSpells =
    "SELECT entry, spellid_1, spelltrigger_1, spellid_2, spelltrigger_2, spellid_3, spelltrigger_3, spellid_4, "
    "spelltrigger_4, spellid_5, spelltrigger_5 FROM item_template WHERE spellid_1 > 0 OR spellid_2 > 0 OR "
    "spellid_3 > 0 OR spellid_4 > 0 OR spellid_5 > 0";
inline constexpr char const* QuestSpells =
    "SELECT ID, RewardSpell, RewardDisplaySpell FROM quest_template WHERE RewardSpell <> 0 OR RewardDisplaySpell <> 0";
inline constexpr char const* VendorRows = "SELECT entry, item, ExtendedCost FROM npc_vendor";
inline constexpr char const* ItemReputations =
    "SELECT entry, RequiredReputationFaction FROM item_template WHERE RequiredReputationFaction > 0";
inline constexpr char const* KillReputations =
    "SELECT creature_id, RewOnKillRepFaction1 FROM creature_onkill_reputation WHERE RewOnKillRepFaction1 > 0 AND "
    "RewOnKillRepValue1 > 0 UNION ALL SELECT creature_id, RewOnKillRepFaction2 FROM creature_onkill_reputation WHERE "
    "RewOnKillRepFaction2 > 0 AND RewOnKillRepValue2 > 0";
inline constexpr char const* QuestReputations =
    "SELECT ID, RewardFactionID1, RewardFactionValue1, RewardFactionOverride1, RewardFactionID2, RewardFactionValue2, "
    "RewardFactionOverride2, RewardFactionID3, RewardFactionValue3, RewardFactionOverride3, RewardFactionID4, "
    "RewardFactionValue4, RewardFactionOverride4, RewardFactionID5, RewardFactionValue5, RewardFactionOverride5 FROM "
    "quest_template WHERE RewardFactionID1 > 0 OR RewardFactionID2 > 0 OR RewardFactionID3 > 0 OR RewardFactionID4 > 0 "
    "OR RewardFactionID5 > 0";
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
    {"quest_template", SourceQueries::QuestRewards, "uuuuuuuuuuuuuuuuuussssuuuu"},
    {"quest_template_addon", SourceQueries::QuestChains, "ushu"},
    {"trainer_spell", SourceQueries::TrainerSpells, "u"},
    {"item_template spells", SourceQueries::ItemSpells, "usbsbsbsbsb"},
    {"quest_template reward spells", SourceQueries::QuestSpells, "usu"},
    {"npc_vendor rows", SourceQueries::VendorRows, "usu"},
    {"item_template reputation", SourceQueries::ItemReputations, "uh"},
    {"creature_onkill_reputation", SourceQueries::KillReputations, "un"},
    {"quest_template reputation", SourceQueries::QuestReputations, "uhsshsshsshsshss"},
};

// Built once on the world thread during startup, before map updates run, and immutable afterwards.
//
// Freshness (CS01 #52 Part 28): the index is the world database as it was at startup. Core's runtime
// ".reload" commands for these tables (loot templates, npc_vendor, quest_template, creature_template)
// and GM-added spawns give modules no hook, so they reach AutoGear only after a restart. Staleness is
// not one-sided: a vendor added later still looks raid-only (refused), but a vendor removed later still
// counts as a source (offered). Restart after changing these tables. Each table is counted and read
// with separate queries on the pool's connections, so there is no shared snapshot: a different row
// count publishes nothing, but an edit that keeps the count during the few seconds of startup is not
// detected. The world is not open yet then; only an external tool could write.
// Get() returns nullptr when the index was not built (feature off, or any table failed to load
// completely); callers then get no tier lift, and IP-capped commands keep the bot's gear.
class SourceIndexStore
{
public:
    static void LoadFromWorldDatabase();
    static SourceIndex const* Get();
    // Items refused at an IP stage, precomputed at load with the IP raid gates read then (LoadGates()).
    // nullptr when the index was not built or the stage is out of range.
    static std::unordered_set<std::uint32_t> const* BlockedAtStage(std::uint8_t stage);
    static PlayerbotsAutoGearSourceAccess::Gates LoadGates();
};
}  // namespace PlayerbotsAutoGearMasterSource

#endif
