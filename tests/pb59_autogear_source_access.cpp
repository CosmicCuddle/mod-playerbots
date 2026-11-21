// C1-B source selection: an IP-capped command never offers an item whose only proven source is a
// raid the commander cannot enter yet. Required stages are Individual Progression's own entry gates.
// Builds a small SourceIndex from world rows the same way the server does.
#include <array>
#include <iostream>
#include <string>
#include <vector>

#include "AutoGearSourceAccess.h"

using namespace PlayerbotsAutoGearMasterSource;
using namespace PlayerbotsAutoGearSourceAccess;

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
constexpr std::uint32_t DungeonItem = 13340;    // Blackrock Depths: not a raid
constexpr std::uint32_t VendorBwlItem = 16950;  // drops in BWL but is also sold: Unknown
constexpr std::uint32_t TwoRaidItem = 17000;    // drops in MC and BWL: Unknown
constexpr std::uint32_t KarazhanItem = 28500;
constexpr std::uint32_t UlduarItem = 45000;
constexpr std::uint32_t NotInIndex = 99999;

struct World
{
    std::vector<LootRow> creatureLoot;
    std::vector<CreatureTemplateRow> creatures;
    std::vector<SpawnRow> creatureSpawns;
    std::vector<std::uint32_t> vendor;

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
    Check(index.Lookup(TwoRaidItem).verdict == Verdict::Unknown, "two raids make an item Unknown");
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

    // Unknown sources are judged by the item-level cap only.
    Check(Offered(index, DungeonItem, 0), "dungeon loot at stage 0");
    Check(Offered(index, VendorBwlItem, 0), "an item also sold by a vendor at stage 0");
    Check(Offered(index, TwoRaidItem, 0), "an item from two raids at stage 0");
    Check(Offered(index, NotInIndex, 0), "an item the index never saw");
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

    // A raid-proven entry on a map the table does not know is refused, never waved through.
    SourceEntry stray;
    stray.verdict = Verdict::VanillaRaid;
    stray.map = 1;
    Check(!Allows(stray, 18, Gates{}), "raid proof on an ungated map is refused");
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
    std::cout << "checks=" << g_checks << " failures=" << g_failures << "\n";
    return g_failures == 0 ? 0 : 1;
}
