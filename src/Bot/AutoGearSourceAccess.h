/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_SOURCE_ACCESS_H
#define PLAYERBOTS_AUTOGEAR_SOURCE_ACCESS_H

#include <cstdint>

#include "AutoGearMasterSourceIndex.h"

// C1-B source selection: an IP-capped autogear command never gives a bot an item whose only proven
// source is a raid the commanding human cannot enter yet. The item-level cap alone allows that: a
// stage 1 commander (cap 78) could otherwise receive Blackwing Lair loot, and a stage 0 commander
// (cap 66) Zul'Gurub or AQ20 loot.
//
// The stage each raid needs is not a new rule: it is Individual Progression's own entry gate
// (IndividualProgressionPlayer.cpp, OnPlayerBeforeTeleport and the instance parent checks) and its
// ProgressionState comments, compared like hasPassedProgression (effective stage >= required).
// IP adds further per-player conditions for some raids (Drakefire Amulet, Naxxramas attunement);
// those are not readable here, so the stage is a necessary condition, never a grant on its own.
//
// An item is refused when the world rows show raid loot for it and every source is a raid the
// commander cannot enter yet: two locked raids, or a locked raid plus loot nobody can place (unspawned
// or script-summoned templates, container contents). A source the commander can reach clears it: a
// spawned dungeon or world drop, a raid they can enter, or a vendor or quest row. An item no row
// names at all has no raid evidence and keeps the item-level cap only.
namespace PlayerbotsAutoGearSourceAccess
{
using PlayerbotsAutoGearMasterSource::AccessEntry;
using PlayerbotsAutoGearMasterSource::QuestFacts;
using PlayerbotsAutoGearMasterSource::SourceIndex;
using PlayerbotsAutoGearMasterSource::Verdict;

// IP config values that move a raid's gate (IndividualProgression.RequiredZulGurubProgression and
// IndividualProgression.RequiredZulAmanProgression; IP's own defaults).
struct Gates
{
    std::uint8_t zulGurub = 3;
    std::uint8_t zulAman = 12;
};

inline constexpr std::int32_t NotGated = -1;  // a raid map this table does not know

// Stage the commander must have reached before IP lets them into the raid that drops the item.
constexpr std::int32_t RequiredStage(std::uint32_t map, std::uint8_t difficulty, Gates gates)
{
    switch (map)
    {
        case 409:  // Molten Core
            return 0;
        case 249:  // Onyxia: the 40-man Vanilla version is 10H on this map; the level-80 one needs WotLK tier 5
            return difficulty == 2 ? 0 : 18;
        case 469:  // Blackwing Lair: PROGRESSION_ONYXIA "BWL available"
            return 2;
        case 309:  // Zul'Gurub
            return gates.zulGurub;
        case 509:  // Ruins of Ahn'Qiraj
        case 531:  // Temple of Ahn'Qiraj: PROGRESSION_PRE_AQ
            return 4;
        case 533:  // Naxxramas: 10H is the IP 40-man version (PROGRESSION_AQ); otherwise WotLK Naxx (TBC_TIER_5)
            return difficulty == 2 ? 6 : 13;
        case 532:  // Karazhan
        case 565:  // Gruul's Lair
        case 544:  // Magtheridon's Lair: PROGRESSION_PRE_TBC
            return 8;
        case 548:  // Serpentshrine Cavern
        case 550:  // Tempest Keep: PROGRESSION_TBC_TIER_1
            return 9;
        case 534:  // Hyjal Summit
        case 564:  // Black Temple: PROGRESSION_TBC_TIER_2
            return 10;
        case 568:  // Zul'Aman
            return gates.zulAman;
        case 580:  // Sunwell Plateau: PROGRESSION_TBC_TIER_4
            return 12;
        case 615:  // Obsidian Sanctum
        case 616:  // Eye of Eternity: PROGRESSION_TBC_TIER_5
            return 13;
        case 603:  // Ulduar
            return 14;
        case 649:  // Trial of the Crusader
            return 15;
        case 631:  // Icecrown Citadel
            return 16;
        case 624:  // Vault of Archavon: a Northrend raid (instance parent check, PROGRESSION_TBC_TIER_5)
            return 13;
        case 724:  // Ruby Sanctum
            return 17;
        default:
            return NotGated;
    }
}

enum class Reach : std::uint8_t
{
    Reachable,  // at least one source the commander can use now
    Locked,     // raid evidence, and every source is out of reach
    Unknown     // no evidence either way (no rows, unplaceable loot, containers)
};

inline Reach SpawnReach(std::uint32_t map, std::uint8_t difficulty, std::uint8_t effectiveStage, Gates gates)
{
    std::int32_t const required = RequiredStage(map, difficulty, gates);
    if (required == NotGated)
        return Reach::Reachable;  // spawned outside any gated raid: a dungeon or the open world
    return static_cast<std::int32_t>(effectiveStage) >= required ? Reach::Reachable : Reach::Locked;
}

// One item's own sources, without following quests (tests and simple callers).
inline bool Allows(AccessEntry const& entry, std::uint8_t effectiveStage, Gates gates)
{
    bool locked = false;
    for (auto const& [map, difficulty] : entry.spawned)
    {
        if (SpawnReach(map, difficulty, effectiveStage, gates) == Reach::Reachable)
            return true;
        locked = true;
    }
    if (entry.vendor || entry.questUnstructured || !entry.quests.empty())
        return true;
    return !locked;
}

// What the factory and the BiS overlay consult for one command. Built on the world thread from the
// published (immutable) index; it lives for that one command only.
//
// A quest reward is only as reachable as the quest: the owner's decision of 22 Nov 2026 is that a
// reward is locked while completing its quest is locked. A quest is locked when a hand-in or start
// item is itself locked (for example Primal Hakkari items, which only drop in Zul'Gurub, for the
// Zandalar "Paragons of Power" rewards), when a creature it requires is spawned only in locked
// raids, or when the quest before it in the chain is locked (Head of Nefarian -> its follow-up).
// Vendors stay reachable. Anything without evidence stays allowed and keeps the item-level cap.
struct Filter
{
    SourceIndex const* index = nullptr;
    std::uint8_t effectiveStage = 0;
    Gates gates{};

    bool Allows(std::uint32_t item) const { return index && ItemReach(item, 0) != Reach::Locked; }

    Reach ItemReach(std::uint32_t item, int depth) const
    {
        AccessEntry const* entry = index->FindAccess(item);
        if (!entry)
            return Reach::Unknown;
        bool locked = false;
        for (auto const& [map, difficulty] : entry->spawned)
        {
            if (SpawnReach(map, difficulty, effectiveStage, gates) == Reach::Reachable)
                return Reach::Reachable;
            locked = true;
        }
        if (entry->vendor || entry->questUnstructured)
            return Reach::Reachable;
        for (std::uint32_t quest : entry->quests)
        {
            Reach const reach = QuestReach(quest, depth + 1);
            if (reach == Reach::Reachable)
                return Reach::Reachable;
            locked = locked || reach == Reach::Locked;
        }
        // Quests without evidence either way leave the item Unknown, which is allowed.
        return locked ? Reach::Locked : Reach::Unknown;
    }

    // Chains and hand-ins rarely go deeper than three steps; past this the answer is Unknown.
    static constexpr int MaxDepth = 8;

    Reach QuestReach(std::uint32_t quest, int depth) const
    {
        if (depth > MaxDepth)
            return Reach::Unknown;
        QuestFacts const* facts = index->Quest(quest);
        if (!facts)
            return Reach::Unknown;
        for (std::uint32_t item : facts->items)
            if (item && ItemReach(item, depth) == Reach::Locked)
                return Reach::Locked;
        for (std::int32_t target : facts->npcOrGo)
        {
            if (target <= 0)
                continue;  // game objects are not placed by this index
            auto const* spawns = index->CreatureSpawns(static_cast<std::uint32_t>(target));
            if (!spawns || spawns->empty())
                continue;
            bool reachable = false;
            for (auto const& [map, difficulty] : *spawns)
                reachable = reachable || SpawnReach(map, difficulty, effectiveStage, gates) == Reach::Reachable;
            if (!reachable)
                return Reach::Locked;
        }
        if (facts->prev != 0)
        {
            std::int64_t const prev = facts->prev < 0 ? -static_cast<std::int64_t>(facts->prev) : facts->prev;
            if (prev != quest && QuestReach(static_cast<std::uint32_t>(prev), depth + 1) == Reach::Locked)
                return Reach::Locked;
        }
        return Reach::Reachable;
    }
};
}  // namespace PlayerbotsAutoGearSourceAccess

#endif
