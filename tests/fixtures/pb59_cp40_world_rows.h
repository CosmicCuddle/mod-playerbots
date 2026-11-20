// CP40 fixture: real world-database rows, copied as data (no migration).
// Core rows: Grimfeather/azerothcore-wotlk d7fb43f47,
//   data/sql/base/db_world/{creature_template,creature,gameobject,
//   gameobject_template,creature_loot_template,gameobject_loot_template,
//   reference_loot_template,npc_vendor,quest_template}.sql
// IP rows: Grimfeather/mod-individual-progression 78db1ac8,
//   data/sql/world/base/naxx40_creatures.sql (@CENTRY = 351000):
//   creature_template line 40, creature_loot_template line 1379,
//   reference_loot_template line 2444, creature spawn line 2646.
// Only the rows on the loot path of each sampled item are kept; the
// verdicts below were also checked against the full core tables.
#pragma once

#include "AutoGearMasterSourceIndex.h"
#include <array>
#include <cstdint>

namespace Cp40Fixture
{
    using namespace PlayerbotsAutoGearMasterSource;

    // Items (name, item level, expected verdict)
    constexpr std::uint32_t Naxx25Amulet = 40064;   // Thunderstorm Amulet 213: Patchwerk 25 (29324)
    constexpr std::uint32_t Naxx10Girdle = 39261;   // Tainted Girdle of Mending 200: Patchwerk 10
    constexpr std::uint32_t Icc25HBand = 50604;     // Band of the Bone Colossus 277: Marrowgar 25H (37959)
    constexpr std::uint32_t Icc25HHelm = 50605;     // Snowserpent Mail Helm 277: Marrowgar 25H
    constexpr std::uint32_t Icc10NSliver = 50339;   // Sliver of Pure Ice 251: Marrowgar 10N
    constexpr std::uint32_t Icc10HPendant = 51894;  // Soulcleave Pendant 264: Deathbringer's Cache 10H (chest)
    constexpr std::uint32_t VoaRobe = 39492;        // Heroes' Frostfire Robe 200: Archavon + vendor 28995
    constexpr std::uint32_t UlduarSphere = 37254;   // Super Simian Sphere 200: Ulduar trash 10 and 25
    constexpr std::uint32_t TocBlade = 48708;       // Spellstorm Blade 245: ToC Tribute Chest (script-summoned)
    constexpr std::uint32_t TokenHelm = 51212;      // Sanctified Ymirjar Lord's Helmet 264: token vendor 35498
    constexpr std::uint32_t KaraRing = 28757;       // Ring of a Thousand Marks 125: Prince Malchezaar
    constexpr std::uint32_t KaraBadge = 29434;      // Badge of Justice: Prince loot + quest 9583 reward
    constexpr std::uint32_t Naxx40Severance = 22815; // Severance 81: IP Patchwerk 40 (351028), 10H spawn

    inline constexpr std::array<CreatureTemplateRow, 12> Creatures{{
        {16028, {29324, 0, 0}, 16028},          // Patchwerk
        {29324, {0, 0, 0}, 29324},              // Patchwerk (1) 25
        {36612, {37957, 37958, 37959}, 36612},  // Lord Marrowgar
        {37957, {0, 0, 0}, 37957},
        {37958, {0, 0, 0}, 37958},
        {37959, {0, 0, 0}, 37959},
        {31125, {31722, 0, 0}, 31125},          // Archavon the Stone Watcher
        {31722, {0, 0, 0}, 31722},
        {34085, {34186, 0, 0}, 34183},          // Ulduar trash (lootid 34183)
        {34186, {0, 0, 0}, 33700},              // its 25-man clone (lootid 33700)
        {15690, {0, 0, 0}, 15690},              // Prince Malchezaar
        {351028, {0, 0, 0}, 351028},            // IP Patchwerk 40
    }};

    inline constexpr std::array<SpawnRow, 6> CreatureSpawns{{
        {16028, 533, 3},    // Naxxramas 10N + 25N
        {36612, 631, 15},   // ICC all four
        {31125, 624, 3},    // Vault of Archavon
        {34085, 603, 3},    // Ulduar 10N + 25N
        {15690, 532, 1},    // Karazhan
        {351028, 533, 4},   // IP: Naxxramas 10H = Naxx 40
    }};

    inline constexpr std::array<ChestTemplateRow, 2> Chests{{
        {202238, 28058},    // Deathbringer's Cache (10H)
        {195665, 195665},   // Argent Crusade Tribute Chest: summoned by script, no spawn row
    }};

    inline constexpr std::array<SpawnRow, 1> ChestSpawns{{
        {202238, 631, 4},
    }};

    inline constexpr std::array<LootRow, 10> CreatureLoot{{
        {29324, 1, 34140},
        {16028, 1, 34100},
        {37959, 1, 34266},
        {36612, 1, 34230},
        {31125, 1, 34209},
        {34183, 1, 34112},
        {33700, 1, 34156},
        {15690, 28757, 0},
        {15690, 29434, 0},
        {351028, 304770, 304770},
    }};

    inline constexpr std::array<LootRow, 2> GameobjectLoot{{
        {28058, 1, 34264},
        {195665, 1, 34285},
    }};

    inline constexpr std::array<LootRow, 13> ReferenceLoot{{
        {34140, 40064, 0},
        {34100, 39261, 0},
        {34266, 50604, 0},
        {34266, 50605, 0},
        {34230, 50339, 0},
        {34209, 39492, 0},
        {34112, 7, 34111},
        {34111, 37254, 0},
        {34156, 7, 34155},
        {34155, 37254, 0},
        {304770, 22815, 0},
        {34264, 51894, 0},
        {34285, 48708, 0},
    }};

    inline constexpr std::array<std::uint32_t, 2> VendorItems{{
        39492,  // npc_vendor 28995, ExtendedCost 2485
        51212,  // npc_vendor 35498, ExtendedCost 2877 (token / emblem)
    }};

    inline constexpr std::array<std::uint32_t, 1> QuestRewardItems{{
        29434,  // quest_template 9583 RewardItem1
    }};

    inline WorldRows Rows()
    {
        WorldRows rows;
        rows.creatureLoot = CreatureLoot;
        rows.gameobjectLoot = GameobjectLoot;
        rows.referenceLoot = ReferenceLoot;
        rows.creatures = Creatures;
        rows.creatureSpawns = CreatureSpawns;
        rows.chests = Chests;
        rows.chestSpawns = ChestSpawns;
        rows.vendorItems = VendorItems;
        rows.questRewardItems = QuestRewardItems;
        return rows;
    }
}
