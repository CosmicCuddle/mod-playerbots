// CP40 (owner approval 20 Nov 2026 11:50 UK, CP39 item 1): fail-closed
// loot-table provenance index and the capture adapter, against a fixture of
// real world-database rows (tests/fixtures/pb59_cp40_world_rows.h).
// CP40b (owner 20 Nov 2026 12:16 UK): 64-bit session/revision trust boundary,
// capture freshness, and core-only vs IP-applied effective world rows.
#include "AutoGearMasterSourceIndex.h"
#include "fixtures/pb59_cp40_world_rows.h"
#include <array>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using namespace PlayerbotsAutoGearMasterSource;
using namespace PlayerbotsAutoGearSelection;
using namespace Cp40Fixture;

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

bool Is(SourceEntry const& e, Verdict v, RaidPoint p, RaidSize s, bool heroic)
{
    return e.verdict == v && e.point == p && e.size == s && e.heroic == heroic;
}

bool IsUnknown(SourceEntry const& e)
{
    return e.verdict == Verdict::Unknown && e.point == RaidPoint::None;
}

CapturedSlot Slot(std::uint64_t guid, std::uint32_t entry, std::uint32_t itemLevel, std::uint32_t quality = 4)
{
    return CapturedSlot{guid, entry, itemLevel, quality, 0, 0};
}

CapturedEquipment Capture(std::uint64_t revision = 9)
{
    CapturedEquipment capture;
    capture.guid = 42;
    capture.session = 7;
    capture.revision = revision;
    return capture;
}

void TestIndex()
{
    Effective const world = IpWorld();
    SourceIndex const index = SourceIndex::Build(world.View());
    Check(index.Built(), "index built");

    // Raid proof: one raid, size and difficulty from the loot record.
    Check(Is(index.Lookup(Naxx25Amulet), Verdict::Raid, RaidPoint::WrathT7, RaidSize::TwentyFive, false),
        "Naxx 25 drop via difficulty_entry_1 is T7 25N");
    Check(Is(index.Lookup(Naxx10Girdle), Verdict::Raid, RaidPoint::WrathT7, RaidSize::Ten, false),
        "Naxx 10 drop via base template is T7 10N");
    Check(Is(index.Lookup(Icc25HBand), Verdict::Raid, RaidPoint::WrathT10, RaidSize::TwentyFive, true),
        "ICC 25H drop via difficulty_entry_3 is T10 25H");
    Check(Is(index.Lookup(Icc10NSliver), Verdict::Raid, RaidPoint::WrathT10, RaidSize::Ten, false),
        "ICC 10N drop is T10 10N");
    Check(Is(index.Lookup(Icc10HPendant), Verdict::Raid, RaidPoint::WrathT10, RaidSize::Ten, true),
        "spawned 10H chest (spawnMask 4) is T10 10H");
    Check(Is(index.Lookup(KaraRing), Verdict::Raid, RaidPoint::TbcT4, RaidSize::Ten, false),
        "Karazhan drop is T4");

    // IP Vanilla: Naxx 40 and Onyxia 40 run in 10H on WotLK maps.
    Check(Is(index.Lookup(Naxx40Severance), Verdict::VanillaRaid, RaidPoint::None, RaidSize::Unknown, false),
        "IP Naxx 40 (map 533 10H, no difficulty_entry_2) is Vanilla");
    Check(Is(index.Lookup(Ony40Collar), Verdict::VanillaRaid, RaidPoint::None, RaidSize::Unknown, false),
        "IP Onyxia 40 (map 249 10H) is Vanilla, never Wrath Heroic");

    // Unknown cases.
    Check(IsUnknown(index.Lookup(VoaRobe)), "VoA drop that a vendor also sells is unknown");
    Check(IsUnknown(index.Lookup(UlduarSphere)), "Ulduar entry in both 10 and 25 loot is unknown");
    Check(IsUnknown(index.Lookup(TocBlade)), "ToC Tribute Chest loot (no spawn row) is unknown");
    Check(IsUnknown(index.Lookup(TokenHelm)), "token/emblem vendor tier piece is unknown");
    Check(IsUnknown(index.Lookup(KaraBadge)), "Kara drop that is also a quest reward is unknown");
    Check(IsUnknown(index.Lookup(0)), "entry 0 is unknown");
    Check(IsUnknown(index.Lookup(49623)), "entry with no row at all is unknown");
    Check(index.RaidEntries() == 7, "exactly seven raid-proof entries in the IP-applied fixture");

    // Core alone vs the effective IP-applied world: a unique core source is
    // not proof once custom SQL changes the tables.
    Effective const core = CoreWorld();
    SourceIndex const coreIndex = SourceIndex::Build(core.View());
    Check(Is(coreIndex.Lookup(MagSack), Verdict::Raid, RaidPoint::TbcT4, RaidSize::TwentyFive, false),
        "core alone: Magtheridon entry has one raid source");
    Check(IsUnknown(index.Lookup(MagSack)), "IP deletes that loot row: unknown in the deployed world");
    Check(IsUnknown(coreIndex.Lookup(Naxx40Severance)) && IsUnknown(coreIndex.Lookup(Ony40Collar)),
        "core alone has no IP 40-man loot: unknown");
    Check(coreIndex.RaidEntries() == 8, "core-only fixture has eight raid-proof entries");

    // Removing the vendor row does not make VoA proof: the map is not mapped.
    WorldRows noVendor = world.View();
    noVendor.vendorItems = {};
    SourceIndex const withoutVendor = SourceIndex::Build(noVendor);
    Check(IsUnknown(withoutVendor.Lookup(VoaRobe)), "VoA stays unknown without the vendor row");
    Check(IsUnknown(withoutVendor.Lookup(TokenHelm)), "token piece without loot stays unknown");

    // The quest row alone is what made the Kara badge unknown.
    WorldRows noQuest = world.View();
    noQuest.questRewardItems = {};
    Check(Is(SourceIndex::Build(noQuest).Lookup(KaraBadge), Verdict::Raid, RaidPoint::TbcT4, RaidSize::Ten, false),
        "quest row is the only thing that made the badge unknown");

    // A chest only proves something once a spawn row reaches it.
    WorldRows noChestSpawn = world.View();
    noChestSpawn.chestSpawns = {};
    Check(IsUnknown(SourceIndex::Build(noChestSpawn).Lookup(Icc10HPendant)),
        "chest loot without a spawn row is unknown");

    SourceIndex const empty;
    Check(!empty.Built() && IsUnknown(empty.Lookup(Icc25HBand)), "unbuilt index is unknown");
}

void TestBucketTable()
{
    Check(ClassifyBucket(533, 2).verdict == Verdict::VanillaRaid, "Naxx 10H is IP Vanilla");
    Check(ClassifyBucket(249, 2).verdict == Verdict::VanillaRaid, "Onyxia 10H is IP Vanilla");
    Check(ClassifyBucket(533, 3).verdict == Verdict::Unknown, "Naxx 25H does not exist: unknown");
    Check(ClassifyBucket(603, 2).verdict == Verdict::Unknown, "Ulduar 10H does not exist: unknown");
    Check(ClassifyBucket(532, 2).verdict == Verdict::Unknown, "TBC raid with a non-zero difficulty: unknown");
    Check(ClassifyBucket(624, 0).verdict == Verdict::Unknown, "Vault of Archavon: unknown");
    Check(ClassifyBucket(36, 0).verdict == Verdict::Unknown, "a dungeon or world map: unknown");
    RaidBucket const toc25h = ClassifyBucket(649, 3);
    Check(toc25h.verdict == Verdict::Raid && toc25h.point == RaidPoint::WrathT9 && toc25h.heroic &&
            toc25h.size == RaidSize::TwentyFive, "ToC 25H loot record is T9 25H");
    RaidBucket const rs10n = ClassifyBucket(724, 0);
    Check(rs10n.point == RaidPoint::WrathRubySanctum && !rs10n.heroic, "Ruby Sanctum 10N");
    Check(ClassifyBucket(409, 0).verdict == Verdict::VanillaRaid, "Molten Core is Vanilla");
}

// CP41: a Heroic map/spawn flag does not by itself prove the *loot
// template* was Heroic. Creature::InitEntry may fall back to 10N/25N/base.
void TestHeroicLootFallback()
{
    CreatureTemplateRow normal{};
    normal.entry = 90001;
    normal.lootId = 90010;
    LootRow normalLoot{90010, 90011, 0};
    SpawnRow spawn{90001, 631, 4}; // ICC 10H only
    WorldRows rows;
    rows.creatures = std::span<CreatureTemplateRow const>(&normal, 1);
    rows.creatureLoot = std::span<LootRow const>(&normalLoot, 1);
    rows.creatureSpawns = std::span<SpawnRow const>(&spawn, 1);

    Check(IsUnknown(SourceIndex::Build(rows).Lookup(90011)),
        "ICC 10H spawn using fallback 10N/base loot does not prove Heroic gear");

    // 25H with no 25H clone falls back to the actual 25N template.
    CreatureTemplateRow normal25{};
    normal25.entry = 90002;
    normal25.lootId = 90020;
    normal.difficultyEntry[0] = normal25.entry;
    std::array<CreatureTemplateRow, 2> templates{normal, normal25};
    LootRow twentyFiveLoot{90020, 90021, 0};
    spawn.spawnMask = 8; // ICC 25H only
    rows.creatures = templates;
    rows.creatureLoot = std::span<LootRow const>(&twentyFiveLoot, 1);
    Check(IsUnknown(SourceIndex::Build(rows).Lookup(90021)),
        "ICC 25H spawn using fallback 25N loot does not prove Heroic gear");

    // An actual Heroic difficulty template still carries positive evidence.
    CreatureTemplateRow heroic10{};
    heroic10.entry = 90003;
    heroic10.lootId = 90030;
    normal.difficultyEntry[1] = heroic10.entry;
    std::array<CreatureTemplateRow, 3> complete{normal, normal25, heroic10};
    LootRow heroicLoot{90030, 90031, 0};
    spawn.spawnMask = 4;
    rows.creatures = complete;
    rows.creatureLoot = std::span<LootRow const>(&heroicLoot, 1);
    Check(Is(SourceIndex::Build(rows).Lookup(90031), Verdict::Raid,
             RaidPoint::WrathT10, RaidSize::Ten, true),
        "ICC 10H with an actual 10H loot template remains Heroic proof");

    // IP 40-man Naxx uses 10H on map 533 as VANILLA, never Wrath Heroic.
    normal.difficultyEntry = {};
    rows.creatures = std::span<CreatureTemplateRow const>(&normal, 1);
    rows.creatureLoot = std::span<LootRow const>(&normalLoot, 1);
    spawn.map = 533;
    Check(Is(SourceIndex::Build(rows).Lookup(90011), Verdict::VanillaRaid,
             RaidPoint::None, RaidSize::Unknown, false),
        "IP Naxx40 10H fallback retains Vanilla-only classification");
}

// CP41: a known raid spawn must not hide a different, unspawned
// creature template that reuses the exact same loot table. Its possible
// script-summoned origin cannot be attributed to the mapped raid.
void TestSharedLootIdAlternateTemplate()
{
    std::array<CreatureTemplateRow, 2> templates{};
    templates[0].entry = 91001;
    templates[0].lootId = 91010;
    templates[1].entry = 91002;
    templates[1].lootId = 91010; // same loot root, no known spawn
    std::array<LootRow, 1> loot{{{91010, 91011, 0}}};
    std::array<SpawnRow, 2> spawns{{{91001, 631, 1}, {91002, 631, 1}}};
    WorldRows rows;
    rows.creatures = templates;
    rows.creatureLoot = loot;
    rows.creatureSpawns = std::span<SpawnRow const>(spawns.data(), 1);
    Check(IsUnknown(SourceIndex::Build(rows).Lookup(91011)),
        "shared raid loot id reused by an unspawned template is unknown");

    // Both templates explicitly spawn only in the same raid bucket.
    rows.creatureSpawns = spawns;
    Check(Is(SourceIndex::Build(rows).Lookup(91011), Verdict::Raid,
             RaidPoint::WrathT10, RaidSize::Ten, false),
        "both templates attributed to the same raid bucket remain valid");

    // A separate world-map source must still spoil the raid-only claim.
    spawns[1].map = 36;
    Check(IsUnknown(SourceIndex::Build(rows).Lookup(91011)),
        "a shared loot id reached by raid and nonraid templates is unknown");
}

void TestAdapter()
{
    Effective const world = IpWorld();
    SourceIndex const index = SourceIndex::Build(world.View());

    // Two ICC 25H pieces plus an unknown piece and body/tabard slots.
    CapturedEquipment capture = Capture();
    capture.slots[0] = Slot(1001, Icc25HHelm, 277);
    capture.slots[11] = Slot(1002, Icc25HBand, 277);
    capture.slots[2] = Slot(1003, TokenHelm, 264);   // unknown source (token)
    capture.slots[3] = Slot(1004, 6833, 1, 1);       // shirt: not counted
    capture.slots[18] = Slot(1005, 46874, 1, 1);     // tabard: not counted
    BoundMasterSources const bound = BindMasterSources(index, capture);
    CaptureSample const routeSample = SampleCapture(capture);  // what the route averaged
    SenderSnapshot const snapshot = routeSample.Snapshot();
    Check(bound.Bound() && routeSample.Usable(), "complete capture binds");
    Check(bound.Slots()[0] == 277 && bound.Slots()[11] == 277 && bound.Slots()[2] == 264, "item levels carried");
    Check(bound.Slots()[3] == 0 && bound.Slots()[18] == 0 && snapshot.slots[3] == SlotIdentity{},
        "body and tabard are not counted");
    Check(bound.sources[0].itemGuid == 1001 && bound.sources[0].entry == Icc25HHelm,
        "each source names its item GUID and entry");
    Check(bound.sources[0].raidSourceKnown && bound.sources[0].heroic && bound.sources[0].heroicKnown,
        "ICC 25H slot carries a known Heroic source");
    Check(!bound.sources[2].raidSourceKnown && bound.sources[2].point == RaidPoint::None,
        "token slot carries no raid source");

    TierFacts const facts = bound.FreshFacts(capture);
    MasterTier const tier = ResolveBoundMasterTier(TierEra::Wrath, bound.Slots(), snapshot, facts);
    Check(facts.sourcesVerified && facts.captureSession == 7 && facts.equipmentRevision == 9, "fresh facts carry the stamp");
    Check(tier.known && tier.ceiling == 271 && tier.raidPieces == 2, "two ICC 25 pieces give the T10 25N ceiling");
    Check(HeroicBisCeiling(17, bound.Slots(), snapshot, facts) == 284, "ICC 25H pieces unlock Heroic BiS at stage 17");

    // Freshness: a later revision, another session, or another item in a
    // slot (same entry and item level) means the bound facts are stale.
    CapturedEquipment later = capture;
    later.revision = 10;
    Check(!bound.FreshFacts(later).sourcesVerified, "later equipment revision: stale, no facts");
    CapturedEquipment relog = capture;
    relog.session = 8;
    Check(!bound.FreshFacts(relog).sourcesVerified, "another capture session: stale, no facts");
    CapturedEquipment swapped = capture;
    swapped.slots[11].itemGuid = 1099;
    Check(!bound.FreshFacts(swapped).sourcesVerified, "same entry and level, other item GUID: stale");
    CaptureSample const laterSample = SampleCapture(later);
    Check(!ResolveBoundMasterTier(TierEra::Wrath, bound.Slots(), laterSample.Snapshot(), bound.Facts()).known,
        "facts against a later route snapshot: no lift");
    CapturedEquipment other = capture;
    other.guid = 43;
    CaptureSample const otherSample = SampleCapture(other);
    Check(!ResolveBoundMasterTier(TierEra::Wrath, bound.Slots(), otherSample.Snapshot(), bound.Facts()).known,
        "facts against another sender's snapshot: no lift");

    // Truncation fix: 64-bit revisions are carried whole and compared whole.
    CapturedEquipment wide = capture;
    wide.revision = (std::uint64_t(1) << 32) + 9;
    BoundMasterSources const wideBound = BindMasterSources(index, wide);
    CaptureSample const wideSample = SampleCapture(wide);
    Check(wideBound.Bound() && wideBound.Facts().equipmentRevision == wide.revision, "revision above 2^32 kept whole");
    Check(ResolveBoundMasterTier(TierEra::Wrath, wideBound.Slots(), wideSample.Snapshot(), wideBound.FreshFacts(wide)).known,
        "wide revision binds against its own capture");
    Check(!ResolveBoundMasterTier(TierEra::Wrath, wideBound.Slots(), snapshot, wideBound.Facts()).known,
        "revision equal only in the low 32 bits does not bind");

    // Unknown never raises: token and script-chest pieces alone give the floor.
    CapturedEquipment tokens = Capture();
    tokens.slots[0] = Slot(2001, TokenHelm, 264);
    tokens.slots[2] = Slot(2002, TokenHelm, 264);
    tokens.slots[4] = Slot(2003, TocBlade, 245);
    BoundMasterSources const tokenBound = BindMasterSources(index, tokens);
    CaptureSample const tokenSample = SampleCapture(tokens);
    MasterTier const tokenTier =
        ResolveBoundMasterTier(TierEra::Wrath, tokenBound.Slots(), tokenSample.Snapshot(), tokenBound.FreshFacts(tokens));
    Check(tokenBound.Bound() && !tokenTier.known && tokenTier.ceiling == 213, "unknown pieces stay at the floor");

    // IP Vanilla pieces carry no Wrath evidence.
    CapturedEquipment vanilla = Capture();
    vanilla.slots[15] = Slot(3001, Naxx40Severance, 81);
    vanilla.slots[1] = Slot(3002, Ony40Collar, 66);
    BoundMasterSources const vanillaBound = BindMasterSources(index, vanilla);
    CaptureSample const vanillaSample = SampleCapture(vanilla);
    Check(vanillaBound.Bound() && !vanillaBound.sources[15].raidSourceKnown && !vanillaBound.sources[1].heroic,
        "IP 40-man pieces are not Wrath Heroic sources");
    Check(!ResolveBoundMasterTier(TierEra::Wrath, vanillaBound.Slots(), vanillaSample.Snapshot(),
              vanillaBound.FreshFacts(vanilla)).known,
        "IP 40-man pieces do not lift the Wrath tier");

    CapturedEquipment noSession = capture;
    noSession.session = 0;
    Check(BindMasterSources(index, noSession).result == BindResult::MissingIdentity &&
            !SampleCapture(noSession).Snapshot().captureSession, "session 0 is rejected, never invented");
    CapturedEquipment noGuid = capture;
    noGuid.guid = 0;
    Check(BindMasterSources(index, noGuid).result == BindResult::MissingIdentity, "guid 0 is rejected");

    CapturedEquipment heirloom = capture;
    heirloom.slots[5] = Slot(4001, 42949, 80, HeirloomQuality);
    Check(BindMasterSources(index, heirloom).result == BindResult::IncompleteSlot, "heirloom makes the capture unusable");
    CapturedEquipment scaling = capture;
    scaling.slots[5] = Slot(4002, 50339, 251);
    scaling.slots[5].scalingValue = 1;
    Check(BindMasterSources(index, scaling).result == BindResult::IncompleteSlot, "scaling item makes it unusable");
    CapturedEquipment noEntry = capture;
    noEntry.slots[5] = Slot(4003, 0, 251);
    Check(BindMasterSources(index, noEntry).result == BindResult::IncompleteSlot, "occupied slot without entry");
    CapturedEquipment junk = capture;
    junk.slots[6].entry = Icc25HBand;  // no item guid but stray data
    BoundMasterSources const junkBound = BindMasterSources(index, junk);
    Check(junkBound.result == BindResult::IncompleteSlot && !junkBound.Facts().sourcesVerified,
        "empty slot with stray data is rejected");

    SourceIndex const unbuilt;
    Check(BindMasterSources(unbuilt, capture).result == BindResult::IndexNotBuilt, "unbuilt index never binds");
}
}

int main()
{
    TestIndex();
    TestBucketTable();
    TestHeroicLootFallback();
    TestSharedLootIdAlternateTemplate();
    TestAdapter();
    std::cout << "checks=" << g_checks << " failures=" << g_failures << "\n";
    return g_failures == 0 ? 0 : 1;
}
