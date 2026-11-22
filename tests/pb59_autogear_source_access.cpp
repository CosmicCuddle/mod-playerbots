// C1-B source selection: an IP-capped command never offers an item whose only proven source is a
// raid the commander cannot enter yet. Required stages are Individual Progression's own entry gates.
// Builds a small SourceIndex from world rows the same way the server does.
#include <algorithm>
#include <array>
#include <iostream>
#include <string>
#include <vector>

#include "AutoGearSourceAccess.h"

using namespace PlayerbotsAutoGearMasterSource;
using namespace PlayerbotsAutoGearSourceAccess;
using PlayerbotsAutoGearMasterSource::QuestChainRow;
using PlayerbotsAutoGearMasterSource::QuestRow;

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

// Item entries of the fixture.
constexpr std::uint32_t MoltenCoreItem = 16800;
constexpr std::uint32_t BlackwingItem = 16900;
constexpr std::uint32_t ZulGurubItem = 19800;
constexpr std::uint32_t Aq20Item = 21400;
constexpr std::uint32_t Aq40Item = 21600;
constexpr std::uint32_t Naxx40Item = 22800;
constexpr std::uint32_t Onyxia40Item = 17060;
constexpr std::uint32_t DungeonItem = 13340;       // Blackrock Depths: not a raid
constexpr std::uint32_t VendorBwlItem = 16950;     // drops in BWL but is also sold: Unknown
constexpr std::uint32_t TwoRaidItem = 17000;       // drops in MC and BWL: one raid open at stage 0
constexpr std::uint32_t LockedPairItem = 17100;    // drops in BWL and AQ20: both locked at stage 1
constexpr std::uint32_t DungeonBwlItem = 17200;    // drops in Blackrock Depths and BWL
constexpr std::uint32_t UnspawnedBwlItem = 17300;  // BWL drop whose loot also sits on an unspawned template
constexpr std::uint32_t ContainerBwlItem = 17400;  // BWL drop also found inside a container item
constexpr std::uint32_t ContainerOnlyItem = 17500;
constexpr std::uint32_t KarazhanItem = 28500;
constexpr std::uint32_t UlduarItem = 45000;
constexpr std::uint32_t NotInIndex = 99999;

struct World
{
    std::vector<LootRow> creatureLoot;
    std::vector<CreatureTemplateRow> creatures;
    std::vector<SpawnRow> creatureSpawns;
    std::vector<std::uint32_t> vendor;
    std::vector<LootRow> itemLoot;

    // One boss per (map, spawn mask) dropping one item.
    void Boss(std::uint32_t entry, std::uint32_t map, std::uint8_t mask, std::uint32_t item)
    {
        creatures.push_back({entry, {}, entry});
        creatureSpawns.push_back({entry, map, mask});
        creatureLoot.push_back({entry, item, 0});
    }

    WorldRows View() const
    {
        WorldRows rows;
        rows.creatureLoot = creatureLoot;
        rows.creatures = creatures;
        rows.creatureSpawns = creatureSpawns;
        rows.vendorItems = vendor;
        rows.itemLoot = itemLoot;
        return rows;
    }
};

SourceIndex BuildFixture()
{
    World world;
    world.Boss(1, 409, 1, MoltenCoreItem);
    world.Boss(2, 469, 1, BlackwingItem);
    world.Boss(3, 309, 1, ZulGurubItem);
    world.Boss(4, 509, 1, Aq20Item);
    world.Boss(5, 531, 1, Aq40Item);
    world.Boss(6, 533, 4, Naxx40Item);    // 10H: the IP 40-man version
    world.Boss(7, 249, 4, Onyxia40Item);  // 10H: the IP 40-man version
    world.Boss(8, 230, 1, DungeonItem);
    world.Boss(9, 469, 1, VendorBwlItem);
    world.vendor.push_back(VendorBwlItem);
    world.Boss(10, 409, 1, TwoRaidItem);
    world.Boss(11, 469, 1, TwoRaidItem);
    world.Boss(12, 532, 1, KarazhanItem);
    world.Boss(13, 603, 1, UlduarItem);
    world.Boss(14, 469, 1, LockedPairItem);
    world.Boss(15, 509, 1, LockedPairItem);
    world.Boss(16, 230, 1, DungeonBwlItem);
    world.Boss(17, 469, 1, DungeonBwlItem);
    world.Boss(18, 469, 1, UnspawnedBwlItem);
    world.creatures.push_back({19, {}, 19});  // never spawned: script-summoned use is unknown
    world.creatureLoot.push_back({19, UnspawnedBwlItem, 0});
    world.Boss(20, 469, 1, ContainerBwlItem);
    world.itemLoot.push_back({9000, ContainerBwlItem, 0});
    world.itemLoot.push_back({9001, ContainerOnlyItem, 0});
    return SourceIndex::Build(world.View());
}

void TestIndexKeepsSourceMap(SourceIndex const& index)
{
    SourceEntry const bwl = index.Lookup(BlackwingItem);
    Check(bwl.verdict == Verdict::VanillaRaid && bwl.map == 469 && bwl.difficulty == 0, "BWL item keeps map 469");
    SourceEntry const naxx = index.Lookup(Naxx40Item);
    Check(naxx.verdict == Verdict::VanillaRaid && naxx.map == 533 && naxx.difficulty == 2,
          "Naxx 40 item keeps map 533, 10H");
    Check(index.Lookup(DungeonItem).verdict == Verdict::Unknown && index.Lookup(DungeonItem).map == 0,
          "dungeon item is Unknown with no map");
    Check(index.Lookup(VendorBwlItem).verdict == Verdict::Unknown, "a vendor source makes a BWL drop Unknown");
    Check(index.Lookup(TwoRaidItem).verdict == Verdict::Unknown, "two raids make an item Unknown for the tier lift");
    AccessEntry const pair = index.Access(LockedPairItem);
    Check(pair.known && pair.spawned.size() == 2 && !pair.vendor, "access keeps both raid sources");
    Check(index.Access(VendorBwlItem).vendor, "access records the vendor source");
    Check(index.Access(UnspawnedBwlItem).unattributed, "access records loot on an unspawned template");
    Check(index.Access(ContainerBwlItem).container, "access records the container source");
    Check(!index.Access(NotInIndex).known, "an unnamed item has no access entry");
}

bool Offered(SourceIndex const& index, std::uint32_t item, std::uint8_t stage, Gates gates = {})
{
    Filter const filter{&index, stage, gates};
    return filter.Allows(item);
}

void TestVanillaStages(SourceIndex const& index)
{
    // Stage 0: Ragnaros not completed. MC and Onyxia are open in IP.
    Check(Offered(index, MoltenCoreItem, 0), "stage 0: Molten Core loot");
    Check(Offered(index, Onyxia40Item, 0), "stage 0: Onyxia 40 loot");
    Check(!Offered(index, BlackwingItem, 0), "stage 0: no BWL loot");
    Check(!Offered(index, ZulGurubItem, 0), "stage 0: no ZG loot (IP default gate 3)");
    Check(!Offered(index, Aq20Item, 0), "stage 0: no AQ20 loot");
    Check(!Offered(index, Naxx40Item, 0), "stage 0: no Naxx 40 loot");

    // Stage 1 (cap 78): BWL items fit the item-level cap but BWL needs Onyxia completed.
    Check(!Offered(index, BlackwingItem, 1), "stage 1: no BWL loot");
    Check(Offered(index, BlackwingItem, 2), "stage 2 (PROGRESSION_ONYXIA, BWL available): BWL loot");
    Check(!Offered(index, ZulGurubItem, 2), "stage 2: still no ZG loot");
    Check(Offered(index, ZulGurubItem, 3), "stage 3: ZG loot");
    Check(!Offered(index, Aq40Item, 3), "stage 3: no AQ40 loot");
    Check(Offered(index, Aq20Item, 4) && Offered(index, Aq40Item, 4), "stage 4 (PRE_AQ): AQ20 and AQ40 loot");
    Check(!Offered(index, Naxx40Item, 5), "stage 5: no Naxx 40 loot");
    Check(Offered(index, Naxx40Item, 6), "stage 6 (PROGRESSION_AQ): Naxx 40 loot");
    Check(Offered(index, MoltenCoreItem, 18), "Vanilla raids never re-lock");

    // Items with an ordinary source are judged by the item-level cap only.
    Check(Offered(index, DungeonItem, 0), "dungeon loot at stage 0");
    Check(Offered(index, DungeonBwlItem, 0), "BRD + BWL: the dungeon source is reachable at stage 0");

    // CS01 7065120451 fixtures.
    // (a) two raids, one unlocked: MC is open at stage 0.
    Check(Offered(index, TwoRaidItem, 0), "(a) MC + BWL at stage 0: MC is open");
    // Two raids, both locked: no reachable source.
    Check(!Offered(index, LockedPairItem, 1), "BWL + AQ20 at stage 1: both locked");
    Check(Offered(index, LockedPairItem, 2), "BWL + AQ20 at stage 2: BWL opens");
    // (b) vendor plus a locked raid: the vendor row is a verified ordinary source (owner decision pending).
    Check(Offered(index, VendorBwlItem, 0), "(b) BWL + vendor at stage 0: the vendor source counts");
    // (c) an item no row names has no known source: refused at every stage (owner, #74 7079669889).
    Check(!Offered(index, NotInIndex, 0) && !Offered(index, NotInIndex, 18), "(c) an item no row names is refused");
    // A locked raid plus loot nobody can place is not a verified alternative.
    Check(!Offered(index, UnspawnedBwlItem, 1), "BWL + unspawned-template loot at stage 1: refused");
    Check(!Offered(index, ContainerBwlItem, 1), "BWL + container contents at stage 1: refused");
    Check(Offered(index, ContainerOnlyItem, 0), "container-only item: no raid evidence");
    // (d) attunement and first-entry conditions are not readable here: the stage is necessary, never
    // sufficient. At stage 6 Naxx 40 loot is offered whether or not the commander is attuned.
    Check(Offered(index, Naxx40Item, 6), "(d) stage-only: Naxx 40 at stage 6 without an attunement input");
}

void TestLaterEras(SourceIndex const& index)
{
    Check(!Offered(index, KarazhanItem, 7), "stage 7: no Karazhan loot");
    Check(Offered(index, KarazhanItem, 8), "stage 8 (PRE_TBC): Karazhan loot");
    Check(!Offered(index, UlduarItem, 13), "stage 13: no Ulduar loot");
    Check(Offered(index, UlduarItem, 14), "stage 14 (WOTLK_TIER_1): Ulduar loot");
}

void TestConfiguredGates(SourceIndex const& index)
{
    Gates early;
    early.zulGurub = 1;
    Check(Offered(index, ZulGurubItem, 1, early), "RequiredZulGurubProgression 1: ZG loot at stage 1");
    Check(!Offered(index, ZulGurubItem, 0, early), "RequiredZulGurubProgression 1: no ZG loot at stage 0");
    Gates late;
    late.zulGurub = 5;
    Check(!Offered(index, ZulGurubItem, 4, late), "RequiredZulGurubProgression 5: no ZG loot at stage 4");
}

void TestTable()
{
    // Every map the index can classify as a raid has a gate, at every difficulty it classifies.
    for (std::uint32_t map = 0; map < 1000; ++map)
        for (std::uint8_t difficulty = 0; difficulty < DifficultyCount; ++difficulty)
            if (ClassifyBucket(map, difficulty).verdict != Verdict::Unknown)
                Check(RequiredStage(map, difficulty, Gates{}) != NotGated,
                      "gate for raid map " + std::to_string(map) + " difficulty " + std::to_string(difficulty));
    Check(RequiredStage(249, 0, Gates{}) == 18 && RequiredStage(533, 0, Gates{}) == 13,
          "level-80 Onyxia and WotLK Naxxramas use their WotLK gates");
    Check(RequiredStage(568, 0, Gates{}) == 12, "Zul'Aman uses IP's default 12");

    Check(RequiredStage(624, 0, Gates{}) == 13, "Vault of Archavon is gated like the other Northrend raids");
    // Every 3.3.5a raid map is gated, so "not gated" really means a dungeon or the open world.
    for (std::uint32_t map : {309u, 409u, 469u, 509u, 531u, 533u, 249u, 532u, 565u, 544u, 548u, 550u,
                              534u, 564u, 568u, 580u, 615u, 616u, 603u, 649u, 631u, 724u, 624u})
        Check(RequiredStage(map, 0, Gates{}) != NotGated, "raid map " + std::to_string(map) + " is gated");
    AccessEntry world;
    world.known = true;
    world.spawned.push_back({1, 0});  // Kalimdor
    world.spawned.push_back({469, 0});
    Check(Allows(world, 0, Gates{}), "a world drop clears a locked raid source");
    AccessEntry empty;
    Check(Allows(empty, 0, Gates{}), "an entry without sources has no raid evidence");
}

// Owner instruction 22 Nov 2026 (coordination #74, comment 7079237461): a quest reward is locked while
// completing its quest is locked.
void TestQuestRewards()
{
    World world;
    std::vector<QuestRow> quests;
    std::vector<QuestChainRow> chains;
    auto quest = [&](std::uint32_t id, std::uint32_t reward, std::uint32_t handIn = 0, std::int32_t kill = 0,
                     std::uint32_t startItem = 0)
    {
        QuestRow row;
        row.id = id;
        row.rewards[0] = reward;
        row.items[0] = handIn;
        row.items[6] = startItem;
        row.npcOrGo[0] = kill;
        quests.push_back(row);
    };
    constexpr std::uint32_t Armsplint = 19717;       // Primal Hakkari Armsplint: Zul'Gurub only
    constexpr std::uint32_t AugurBracers = 19830;    // Paragons of Power reward
    constexpr std::uint32_t HeadOfNefarian = 19003;  // Blackwing Lair only
    constexpr std::uint32_t DragonslayerMedal = 19383;
    world.Boss(40, 309, 1, Armsplint);
    world.Boss(41, 469, 1, HeadOfNefarian);
    world.Boss(42, 230, 1, 6000);  // a dungeon hand-in
    quest(8056, AugurBracers, Armsplint);
    quest(7781, 0, HeadOfNefarian, 0, HeadOfNefarian);
    quest(7782, DragonslayerMedal);
    chains.push_back({7782, 7781});
    quest(7790, 5100);
    chains.push_back({7790, -7781});  // must be active: still needs the head
    quest(100, 5000, 6000);           // ordinary quest with a dungeon hand-in
    world.creatures.push_back({30, {}, 0});
    world.creatureSpawns.push_back({30, 469, 1});  // kill target only in BWL
    quest(101, 5001, 0, 30);
    world.creatures.push_back({31, {}, 0});
    world.creatureSpawns.push_back({31, 469, 1});
    world.creatureSpawns.push_back({31, 0, 1});  // also in the open world
    quest(102, 5002, 0, 31);
    quest(103, 5003, 0, 32);  // kill target with no spawn: unknown
    quest(104, 5004);         // no requirements
    quest(105, 5005, Armsplint);
    world.vendor.push_back(5005);  // locked quest, but also sold
    quest(106, 5006, Armsplint);
    world.Boss(43, 230, 1, 5006);  // locked quest, but also a dungeon drop
    quest(107, 5007, Armsplint);
    world.Boss(44, 469, 1, 5007);  // locked quest and a locked raid drop
    quest(900, 9000, 9000);        // a quest that needs its own reward: must terminate
    quest(901, 9001, 0, -500);     // game object target: not placed by the index
    std::vector<std::uint32_t> legacy{5009};

    WorldRows rows = world.View();
    rows.quests = quests;
    rows.questChains = chains;
    rows.questRewardItems = legacy;
    SourceIndex const index = SourceIndex::Build(rows);

    Check(index.Access(AugurBracers).quests.size() == 1 && index.Quest(7782) && index.Quest(7782)->prev == 7781,
          "index keeps rewarding quests and the chain");
    Check(!Offered(index, AugurBracers, 0), "Zandalar reward at stage 0: its hand-in only drops in Zul'Gurub");
    Check(!Offered(index, AugurBracers, 2), "Zandalar reward at stage 2: Zul'Gurub still locked");
    Check(Offered(index, AugurBracers, 3), "Zandalar reward at stage 3: Zul'Gurub open");
    Gates early;
    early.zulGurub = 1;
    Check(Offered(index, AugurBracers, 1, early), "Zandalar reward follows RequiredZulGurubProgression");

    Check(!Offered(index, DragonslayerMedal, 1), "Head of Nefarian follow-up at stage 1: the first quest is locked");
    Check(Offered(index, DragonslayerMedal, 2), "Head of Nefarian follow-up at stage 2: BWL open");
    Check(!Offered(index, 5100, 1), "a negative PrevQuestID is followed too");

    Check(Offered(index, 5000, 0), "ordinary quest with a dungeon hand-in");
    Check(!Offered(index, 5001, 1), "kill target spawned only in a locked raid");
    Check(Offered(index, 5001, 2), "kill target's raid open");
    Check(Offered(index, 5002, 0), "kill target also spawned in the open world");
    Check(Offered(index, 5003, 0), "kill target without a spawn: no evidence");
    Check(Offered(index, 5004, 0), "quest without requirements");
    Check(Offered(index, 5005, 0), "locked quest, but a vendor sells the reward");
    Check(Offered(index, 5006, 0), "locked quest, but the reward also drops in a dungeon");
    Check(!Offered(index, 5007, 1), "locked quest and a locked raid drop");
    Check(Offered(index, 9000, 0), "a self-referencing quest terminates and stays allowed");
    Check(Offered(index, 9001, 0), "game object targets are not judged");
    Check(Offered(index, 5009, 0), "a reward row without requirements (older callers) stays allowed");

    // Hand-ins that are themselves quest rewards are followed one more step.
    World deep;
    deep.Boss(50, 469, 1, 7000);
    std::vector<QuestRow> deepQuests;
    QuestRow first;
    first.id = 200;
    first.rewards[0] = 7100;
    first.items[0] = 7000;  // BWL hand-in
    QuestRow second;
    second.id = 201;
    second.rewards[0] = 7200;
    second.items[0] = 7100;  // needs the first reward
    deepQuests = {first, second};
    WorldRows deepRows = deep.View();
    deepRows.quests = deepQuests;
    SourceIndex const deepIndex = SourceIndex::Build(deepRows);
    Check(!Offered(deepIndex, 7200, 1), "a reward whose hand-in is a locked quest reward is locked");
    Check(Offered(deepIndex, 7200, 2), "and opens with the raid");
}

// Crafting is a source that follows how the recipe is obtained; items without any source are refused
// (owner instruction of 22 Nov 2026, coordination #74 comment 7079669889).
void TestCraftsAndNoSource()
{
    World world;
    constexpr std::uint32_t TrainerCraft = 14139;  // Mooncloth Shoulders style: trainer-taught recipe
    constexpr std::uint32_t RecipeCraft = 15092;   // a world-drop recipe item teaches it
    constexpr std::uint32_t WorldRecipe = 15700;
    constexpr std::uint32_t ZgRecipeCraft = 19900;  // the recipe drops only in Zul'Gurub
    constexpr std::uint32_t ZgRecipe = 19901;
    constexpr std::uint32_t Charm = 19956;  // made by using a Zul'Gurub drop
    constexpr std::uint32_t CharmSource = 19957;
    constexpr std::uint32_t QuestCraft = 16000;            // the recipe is a quest reward spell
    constexpr std::uint32_t RemovedRecipeCraft = 16100;    // nobody can learn the recipe
    constexpr std::uint32_t UnsourcedRecipeCraft = 16200;  // the recipe item itself has no source
    constexpr std::uint32_t UnsourcedRecipe = 16201;
    constexpr std::uint32_t BwlAndTrainer = 16300;   // a BWL drop that is also trainer-crafted
    constexpr std::uint32_t BwlAndZgRecipe = 16400;  // a BWL drop also crafted from a Zul'Gurub recipe
    constexpr std::uint32_t ScriptLootAndUnsourcedRecipe = 16500;
    constexpr std::uint32_t LoopA = 16600;  // A is made with B, B is made with A
    constexpr std::uint32_t LoopB = 16601;
    world.Boss(60, 0, 1, WorldRecipe);
    world.Boss(61, 309, 1, ZgRecipe);
    world.Boss(62, 309, 1, CharmSource);
    world.Boss(63, 469, 1, BwlAndTrainer);
    world.Boss(64, 469, 1, BwlAndZgRecipe);
    world.creatureLoot.push_back({65, ScriptLootAndUnsourcedRecipe, 0});
    world.creatures.push_back({65, {}, 65});  // a script-summoned boss: loot without a spawn
    std::vector<CraftRow> crafts{{1, TrainerCraft},
                                 {2, RecipeCraft},
                                 {3, ZgRecipeCraft},
                                 {4, Charm},
                                 {5, QuestCraft},
                                 {6, RemovedRecipeCraft},
                                 {7, UnsourcedRecipeCraft},
                                 {8, BwlAndTrainer},
                                 {9, BwlAndZgRecipe},
                                 {10, ScriptLootAndUnsourcedRecipe},
                                 {11, LoopA},
                                 {12, LoopB}};
    std::vector<SpellSourceRow> sources{{1, SpellSource::Taught, 0},
                                        {2, SpellSource::Item, WorldRecipe},
                                        {3, SpellSource::Item, ZgRecipe},
                                        {4, SpellSource::Item, CharmSource},
                                        {5, SpellSource::Quest, 300},
                                        {7, SpellSource::Item, UnsourcedRecipe},
                                        {8, SpellSource::Taught, 0},
                                        {9, SpellSource::Item, ZgRecipe},
                                        {10, SpellSource::Item, UnsourcedRecipe},
                                        {11, SpellSource::Item, LoopB},
                                        {12, SpellSource::Item, LoopA},
                                        {99, SpellSource::Taught, 0}};  // a taught spell that makes nothing
    std::vector<QuestRow> quests;
    QuestRow handIn;
    handIn.id = 300;
    handIn.items[0] = ZgRecipe;  // the quest teaching spell 5 needs a Zul'Gurub item
    quests.push_back(handIn);
    WorldRows rows = world.View();
    rows.crafts = crafts;
    rows.spellSources = sources;
    rows.quests = quests;
    SourceIndex const index = SourceIndex::Build(rows);

    Check(index.Access(TrainerCraft).taught &&
              index.Access(RecipeCraft).fromItems == std::vector<std::uint32_t>{WorldRecipe} &&
              index.Access(QuestCraft).quests == std::vector<std::uint32_t>{300},
          "the index keeps how each craft is learned");
    Check(!index.FindAccess(RemovedRecipeCraft), "a craft nobody can learn adds no source");
    Check(Offered(index, TrainerCraft, 0), "a trainer-taught craft is offered");
    Check(Offered(index, RecipeCraft, 0), "a craft from a world-drop recipe is offered");
    Check(!Offered(index, ZgRecipeCraft, 2) && Offered(index, ZgRecipeCraft, 3),
          "a craft from a Zul'Gurub recipe waits for Zul'Gurub");
    Check(!Offered(index, Charm, 2) && Offered(index, Charm, 3), "an item made by using a Zul'Gurub drop waits too");
    Check(!Offered(index, QuestCraft, 2) && Offered(index, QuestCraft, 3), "a quest-taught craft follows its quest");
    Check(!Offered(index, RemovedRecipeCraft, 18), "a craft nobody can learn is refused");
    Check(!Offered(index, UnsourcedRecipeCraft, 18), "a craft whose recipe item has no source is refused");
    Check(Offered(index, BwlAndTrainer, 0), "a trainer craft clears a BWL drop");
    Gates earlyZg;
    earlyZg.zulGurub = 1;
    Check(!Offered(index, BwlAndZgRecipe, 1) && Offered(index, BwlAndZgRecipe, 2) &&
              Offered(index, BwlAndZgRecipe, 1, earlyZg),
          "a BWL drop also crafted from a Zul'Gurub recipe opens with whichever raid opens first");
    Check(Offered(index, ScriptLootAndUnsourcedRecipe, 0), "script-summoned loot is still a source");
    Check(!Offered(index, LoopA, 18) && !Offered(index, LoopB, 18), "a creation loop with no way in is refused");
    Check(!Offered(index, 77777, 0), "an item no row names is refused");
    Check(index.Lookup(BwlAndTrainer).verdict != Verdict::Raid, "a taught craft is not raid-only for the tier lift");

    auto const blocked = BlockedSets(index, Gates{});
    bool same = true;
    for (std::uint8_t stage = 0; stage < StageCount; ++stage)
    {
        Filter const live{&index, stage, Gates{}, nullptr};
        Filter const fast{&index, stage, Gates{}, &blocked[stage]};
        for (std::uint32_t item : {TrainerCraft, RecipeCraft, ZgRecipeCraft, Charm, QuestCraft, RemovedRecipeCraft,
                                   UnsourcedRecipeCraft, BwlAndZgRecipe, LoopA, 77777u})
            same = same && live.Allows(item) == fast.Allows(item);
    }
    Check(same, "precomputed sets agree with the live rule for crafts and unsourced items");
}

// Random-bot gearing reads precomputed sets; they must say exactly what the live rule says.
void TestBlockedSetsAndBotStage()
{
    SourceIndex const index = BuildFixture();
    for (Gates gates : {Gates{}, Gates{1, 12}, Gates{5, 12}})
    {
        auto const blocked = BlockedSets(index, gates);
        bool same = true;
        std::size_t compared = 0;
        for (std::uint8_t stage = 0; stage < StageCount; ++stage)
        {
            Filter const live{&index, stage, gates, nullptr};
            Filter const fast{&index, stage, gates, &blocked[stage]};
            index.ForEachAccessItem(
                [&](std::uint32_t item)
                {
                    ++compared;
                    same = same && live.Allows(item) == fast.Allows(item);
                });
            same = same && !fast.Allows(NotInIndex) && !live.Allows(NotInIndex);
        }
        Check(same && compared > 0,
              "precomputed sets match the live rule (ZG gate " + std::to_string(gates.zulGurub) + ")");
    }
    auto const blocked = BlockedSets(index, Gates{});
    Check(blocked[0].count(BlackwingItem) && blocked[0].count(ZulGurubItem) && !blocked[0].count(MoltenCoreItem),
          "stage 0 set refuses BWL and ZG loot, not MC");
    Check(!blocked[2].count(BlackwingItem) && blocked[2].count(ZulGurubItem), "stage 2 set opens BWL only");
    Check(blocked[18].empty(), "nothing is refused at stage 18");
    Filter const noIndex{nullptr, 0, {}, &blocked[0]};
    Check(!noIndex.Allows(MoltenCoreItem), "a filter without an index still offers nothing");

    // IP's stage: the highest rewarded marker quest 66000 + stage.
    auto stageOf = [](std::initializer_list<std::uint32_t> rewarded)
    {
        return StageFromMarkerQuests([&](std::uint32_t quest)
                                     { return std::find(rewarded.begin(), rewarded.end(), quest) != rewarded.end(); });
    };
    Check(stageOf({}) == 0, "no marker quest: stage 0");
    Check(stageOf({66001}) == 1, "Molten Core marker: stage 1");
    Check(stageOf({66001, 66002, 66003}) == 3, "highest marker wins");
    Check(stageOf({66003}) == 3, "a gap does not lower the stage (as IP)");
    Check(stageOf({66018}) == 18, "WotLK tier 5 marker: stage 18");
    Check(stageOf({66000, 66019, 12345}) == 0, "only 66001-66018 count");
}

void TestNoIndex()
{
    Filter const filter{nullptr, 18, Gates{}};
    Check(!filter.Allows(DungeonItem), "without an index nothing is offered");
}
}  // namespace

int main()
{
    SourceIndex const index = BuildFixture();
    Check(index.Built(), "index built");
    TestIndexKeepsSourceMap(index);
    TestVanillaStages(index);
    TestLaterEras(index);
    TestConfiguredGates(index);
    TestTable();
    TestNoIndex();
    TestQuestRewards();
    TestCraftsAndNoSource();
    TestBlockedSetsAndBotStage();
    std::cout << "checks=" << g_checks << " failures=" << g_failures << "\n";
    return g_failures == 0 ? 0 : 1;
}
