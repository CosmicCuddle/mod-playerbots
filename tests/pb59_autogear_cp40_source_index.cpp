// CP40 (owner approval 20 Nov 2026 11:50 UK, CP39 item 1): fail-closed
// loot-table provenance index and the capture adapter, against a fixture of
// real world-database rows (tests/fixtures/pb59_cp40_world_rows.h).
#include "AutoGearMasterSourceIndex.h"
#include "fixtures/pb59_cp40_world_rows.h"
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
    SourceIndex const index = SourceIndex::Build(Rows());
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

    // IP Vanilla: Naxx 40 runs in 10H on map 533. Vanilla, never Wrath Heroic.
    Check(Is(index.Lookup(Naxx40Severance), Verdict::VanillaRaid, RaidPoint::None, RaidSize::Unknown, false),
        "IP Naxx 40 (map 533 10H, no difficulty_entry_2) is Vanilla");

    // Unknown cases.
    Check(IsUnknown(index.Lookup(VoaRobe)), "VoA drop that a vendor also sells is unknown");
    Check(IsUnknown(index.Lookup(UlduarSphere)), "Ulduar entry in both 10 and 25 loot is unknown");
    Check(IsUnknown(index.Lookup(TocBlade)), "ToC Tribute Chest loot (no spawn row) is unknown");
    Check(IsUnknown(index.Lookup(TokenHelm)), "token/emblem vendor tier piece is unknown");
    Check(IsUnknown(index.Lookup(KaraBadge)), "Kara drop that is also a quest reward is unknown");
    Check(IsUnknown(index.Lookup(0)), "entry 0 is unknown");
    Check(IsUnknown(index.Lookup(49623)), "entry with no row at all is unknown");
    Check(index.RaidEntries() == 7, "exactly seven raid-proof entries in the fixture");

    // Removing the vendor row does not make VoA proof: the map is not mapped.
    WorldRows noVendor = Rows();
    noVendor.vendorItems = {};
    SourceIndex const withoutVendor = SourceIndex::Build(noVendor);
    Check(IsUnknown(withoutVendor.Lookup(VoaRobe)), "VoA stays unknown without the vendor row");
    Check(IsUnknown(withoutVendor.Lookup(TokenHelm)), "token piece without loot stays unknown");

    // Removing the quest row restores the Kara badge's single raid bucket:
    // the quest row alone was what poisoned it.
    WorldRows noQuest = Rows();
    noQuest.questRewardItems = {};
    Check(Is(SourceIndex::Build(noQuest).Lookup(KaraBadge), Verdict::Raid, RaidPoint::TbcT4, RaidSize::Ten, false),
        "quest row is the only thing that made the badge unknown");

    // A chest only proves something once a spawn row reaches it.
    WorldRows noChestSpawn = Rows();
    noChestSpawn.chestSpawns = {};
    Check(IsUnknown(SourceIndex::Build(noChestSpawn).Lookup(Icc10HPendant)),
        "chest loot without a spawn row is unknown");

    // Default index is not built and proves nothing.
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

void TestAdapter()
{
    SourceIndex const index = SourceIndex::Build(Rows());

    // Two ICC 25H pieces plus unknown and body/tabard slots.
    CapturedEquipment capture = Capture();
    capture.slots[0] = Slot(1001, Icc25HHelm, 277);
    capture.slots[11] = Slot(1002, Icc25HBand, 277);
    capture.slots[2] = Slot(1003, TokenHelm, 264);   // unknown source (token)
    capture.slots[3] = Slot(1004, 6833, 1, 1);       // shirt: not counted
    capture.slots[18] = Slot(1005, 46874, 1, 1);     // tabard: not counted
    BoundMasterSources const bound = BindMasterSources(index, capture);
    Check(bound.Bound(), "complete capture binds");
    Check(bound.slots[0] == 277 && bound.slots[11] == 277 && bound.slots[2] == 264, "item levels carried");
    Check(bound.slots[3] == 0 && bound.slots[18] == 0, "body and tabard are not counted");
    Check(bound.sources[0].raidSourceKnown && bound.sources[0].heroic && bound.sources[0].heroicKnown,
        "ICC 25H slot carries a known Heroic source");
    Check(!bound.sources[2].raidSourceKnown && bound.sources[2].point == RaidPoint::None,
        "token slot carries no raid source");
    Check(bound.snapshot.senderGuid == 42 && bound.snapshot.equipmentRevision == 9, "snapshot from the capture");

    TierFacts const facts = bound.Facts();
    MasterTier const tier = ResolveBoundMasterTier(TierEra::Wrath, bound.slots, bound.snapshot, facts);
    Check(tier.known && tier.ceiling == 271 && tier.raidPieces == 2, "two ICC 25 pieces give the T10 25N ceiling");
    Check(HeroicBisCeiling(17, bound.slots, bound.snapshot, facts) == 284, "ICC 25H pieces unlock Heroic BiS at stage 17");

    SenderSnapshot stale = bound.snapshot;
    stale.equipmentRevision = 10;
    Check(!ResolveBoundMasterTier(TierEra::Wrath, bound.slots, stale, facts).known,
        "a later equipment revision does not reuse the sources");

    // Unknown never raises: token pieces alone give the era floor.
    CapturedEquipment tokens = Capture();
    tokens.slots[0] = Slot(2001, TokenHelm, 264);
    tokens.slots[2] = Slot(2002, TokenHelm, 264);
    tokens.slots[4] = Slot(2003, TocBlade, 245);
    BoundMasterSources const tokenBound = BindMasterSources(index, tokens);
    MasterTier const tokenTier =
        ResolveBoundMasterTier(TierEra::Wrath, tokenBound.slots, tokenBound.snapshot, tokenBound.Facts());
    Check(tokenBound.Bound() && !tokenTier.known && tokenTier.ceiling == 213, "unknown pieces stay at the floor");

    // IP Vanilla pieces carry no Wrath evidence.
    CapturedEquipment vanilla = Capture();
    vanilla.slots[15] = Slot(3001, Naxx40Severance, 81);
    vanilla.slots[16] = Slot(3002, Naxx40Severance, 81);
    BoundMasterSources const vanillaBound = BindMasterSources(index, vanilla);
    Check(vanillaBound.Bound() && !vanillaBound.sources[15].raidSourceKnown && !vanillaBound.sources[15].heroic,
        "IP Naxx 40 piece is not a Wrath Heroic source");
    Check(!ResolveBoundMasterTier(TierEra::Wrath, vanillaBound.slots, vanillaBound.snapshot, vanillaBound.Facts()).known,
        "IP Naxx 40 pieces do not lift the Wrath tier");

    // Revision field: owner 11:50 UK, reject what PR37's 32-bit field cannot hold.
    CapturedEquipment wide = capture;
    wide.revision = std::uint64_t(std::numeric_limits<std::uint32_t>::max()) + 1;
    BoundMasterSources const wideBound = BindMasterSources(index, wide);
    Check(wideBound.result == BindResult::RevisionTooWide && !wideBound.Facts().sourcesVerified,
        "revision above 2^32-1 is rejected, not truncated");
    Check(!ResolveBoundMasterTier(TierEra::Wrath, wideBound.slots, SenderSnapshot{42, 0}, wideBound.Facts()).known,
        "rejected revision gives no lift");
    CapturedEquipment edge = capture;
    edge.revision = std::numeric_limits<std::uint32_t>::max();
    Check(BindMasterSources(index, edge).Bound(), "revision 2^32-1 still fits");

    CapturedEquipment noSession = capture;
    noSession.session = 0;
    Check(BindMasterSources(index, noSession).result == BindResult::MissingIdentity, "session 0 is rejected");
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
    TestAdapter();
    std::cout << "checks=" << g_checks << " failures=" << g_failures << "\n";
    return g_failures == 0 ? 0 : 1;
}
