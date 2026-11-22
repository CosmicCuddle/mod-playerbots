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

// True when the item may be offered.
inline bool Allows(AccessEntry const& entry, std::uint8_t effectiveStage, Gates gates)
{
    bool lockedRaid = false;
    for (auto const& [map, difficulty] : entry.spawned)
    {
        std::int32_t const required = RequiredStage(map, difficulty, gates);
        if (required == NotGated)
            return true;  // spawned outside any gated raid: a dungeon or world drop
        if (static_cast<std::int32_t>(effectiveStage) >= required)
            return true;  // a raid the commander can enter
        lockedRaid = true;
    }
    if (entry.vendorOrQuest)
        return true;
    return !lockedRaid;
}

// What the factory and the BiS overlay consult for one command. Built on the world thread from the
// published (immutable) index; it lives for that one command only.
struct Filter
{
    SourceIndex const* index = nullptr;
    std::uint8_t effectiveStage = 0;
    Gates gates{};

    bool Allows(std::uint32_t item) const
    {
        return index && PlayerbotsAutoGearSourceAccess::Allows(index->Access(item), effectiveStage, gates);
    }
};
}  // namespace PlayerbotsAutoGearSourceAccess

#endif
