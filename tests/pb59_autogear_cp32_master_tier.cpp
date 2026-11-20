// CP32: at TBC and Wrath max level, normal autogear is
// min(commander average, worn raid tier ceiling, stage cap). Owner decision
// 18 Nov 2026 14:49 UK. BiS, leveling and match are unchanged.
// CP34 (owner, 18 Nov 16:08 UK): Sunwell ceiling 159; at Vanilla stages 1-7
// with a level 60 commander, normal autogear is min(average, stage cap).
// CP35 G7: a raid-source list lifts the tier only when it is bound to the
// exact averaged sample (length, per-slot item level, sender, revision).
// CP36: Ruby Sanctum is its own point (10-man 258, 25-man 271), apart from
// ICC (264/271); Zul'Aman stays T5 141.
// CP38: BiS at stages with a Heroic split needs master Heroic evidence.
#include "AutoGearMasterTier.h"
#include <iostream>
#include <string>
#include <vector>

using namespace PlayerbotsAutoGearSelection;
using PlayerbotsAutoGearWeapons::Bit;
using PlayerbotsAutoGearWeapons::Kind;

namespace
{
int g_checks = 0;

bool Check(bool condition, std::string const& text)
{
    ++g_checks;
    if (!condition)
    {
        std::cerr << "FAIL " << text << "\n";
        return false;
    }
    return true;
}

std::uint32_t ScoreOf(std::uint32_t id, std::int32_t, std::uint32_t) { return id; }

MasterItemSource Raid(RaidPoint point, std::uint32_t itemLevel, RaidSize size = RaidSize::Unknown,
    bool heroic = false)
{
    MasterItemSource item;
    item.itemLevel = itemLevel;
    item.raidSourceKnown = true;
    item.point = point;
    item.size = size;
    item.heroic = heroic;
    return item;
}

MasterItemSource Other(std::uint32_t itemLevel)
{
    MasterItemSource item;
    item.itemLevel = itemLevel;
    item.raidSourceKnown = true;
    item.point = RaidPoint::None;
    return item;
}

std::vector<MasterItemSource> Repeat(MasterItemSource item, std::size_t count)
{
    return std::vector<MasterItemSource>(count, item);
}

std::vector<MasterItemSource> Join(std::vector<MasterItemSource> left, std::vector<MasterItemSource> const& right)
{
    left.insert(left.end(), right.begin(), right.end());
    return left;
}

ItemFact Chest(std::uint32_t itemLevel)
{
    ItemFact fact;
    fact.id = itemLevel;
    fact.itemLevel = itemLevel;
    fact.requiredLevel = 1;
    fact.slot = 4;
    fact.armorSubtype = 1;
    fact.sourceKnown = true;
    fact.metadataKnown = true;
    fact.equipKnown = true;
    fact.equipAllowed = true;
    fact.scoreKnown = true;
    fact.setEvidenceKnown = true;
    return fact;
}

std::vector<ItemFact> const& Pool()
{
    static std::vector<ItemFact> const pool = [] {
        std::vector<ItemFact> facts;
        for (std::uint32_t level = 60; level <= 284; ++level)
            facts.push_back(Chest(level));
        return facts;
    }();
    return pool;
}

struct Fixture
{
    std::vector<std::uint32_t> slots;
    std::vector<MasterItemSource> sources;
    RouteRequest request;
    TierFacts facts;
    SenderSnapshot snapshot;
    std::vector<SlotIdentity> identities;
};

// CP40b: the captured 19-slot identity sample behind f.slots, with each
// aligned source naming the same item instance.
void BindIdentities(Fixture& f)
{
    f.identities.assign(f.slots.size(), SlotIdentity{});
    for (std::size_t i = 0; i < f.slots.size(); ++i)
    {
        if (f.slots[i] != 0)
            f.identities[i] = SlotIdentity{1000 + i, 60000 + static_cast<std::uint32_t>(i), f.slots[i]};
        if (i < f.sources.size() && f.sources[i].itemLevel != 0)
        {
            f.sources[i].itemGuid = f.identities[i].itemGuid;
            f.sources[i].entry = f.identities[i].entry;
        }
    }
    f.snapshot.slots = f.identities;
}

// A commander at the given stage and level with a flat average.
void Prepare(Fixture& f, std::uint8_t stage, std::uint32_t level, std::uint32_t average, bool bis,
    std::vector<MasterItemSource> sources, bool sourcesVerified = true)
{
    f.slots.assign(16, average);
    f.sources = std::move(sources);
    // A flat sample: every equipped slot reads the average, so an aligned
    // source list carries the same item level per slot.
    if (f.sources.size() == f.slots.size())
    {
        for (MasterItemSource& source : f.sources)
            source.itemLevel = average;
    }
    f.facts = TierFacts{};
    f.facts.sourcesVerified = sourcesVerified;
    f.facts.sources = f.sources;
    f.facts.senderGuid = 42;
    f.facts.captureSession = 3;
    f.facts.equipmentRevision = 7;
    f.snapshot = SenderSnapshot{};
    f.snapshot.senderGuid = 42;
    f.snapshot.captureSession = 3;
    f.snapshot.equipmentRevision = 7;
    BindIdentities(f);
    RouteRequest& r = f.request;
    r = RouteRequest{};
    r.commandEnabled = true;
    r.ipCapEnabled = true;
    r.bis = bis;
    r.populationKnown = true;
    r.population = Population::RandomBot;
    r.playerContextVerified = true;
    r.progressionContextVerified = true;
    r.rawStage = stage;
    r.effectiveStage = stage;
    r.qualityLimit = 4;
    r.senderLevelKnown = true;
    r.senderLevel = level;
    r.firstStageAfterLeveling = level == 60 && stage == 0;
    r.senderFixedKnown = true;
    r.senderFixedAverage = average;
    r.commandingSenderVerified = true;
    r.commandingSenderSlots = f.slots;
    BotView bot;
    bot.levelKnown = true;
    bot.level = 80;
    bot.classKnown = true;
    bot.classId = 8;
    bot.specKnown = true;
    bot.roleKnown = true;
    bot.role = Role::Caster;
    bot.armorKnown = true;
    bot.armorMask = std::uint32_t{1} << 1;
    bot.weaponsKnown = true;
    bot.weapons.mainhand = Bit(Kind::OneHand);
    r.selection.bot = bot;
    r.selection.facts = Pool();
    r.selection.eligibleMask = 1u << 4;
    r.selection.requiredMask = 1u << 4;
    r.selection.completeMask = 1u << 4;
    r.selection.authorityValid = true;
    r.selection.bags = BagSpace::Enough;
}

bool Expect(std::string const& name, Fixture const& f, std::uint32_t cap, CapMode mode)
{
    RouteDecision const d = ResolveTieredAutoGearRoute(f.request, f.snapshot, f.facts, ScoreOf);
    bool const ok = d.cap == cap && d.mode == mode && d.action == GearAction::CommitSet &&
                    d.plan.selected[4].entryId == cap && !d.deletesGear;
    if (!ok)
        std::cerr << "  got cap=" << d.cap << " mode=" << static_cast<int>(d.mode)
                  << " action=" << static_cast<int>(d.action) << "\n";
    return Check(ok, name);
}
}  // namespace

static_assert(EraForStage(7) == TierEra::None && EraForStage(8) == TierEra::Tbc && EraForStage(13) == TierEra::Wrath);
static_assert(RaidPointCeiling(RaidPoint::WrathT7, RaidSize::Unknown) == 213);
static_assert(EraFloorCeiling(TierEra::Tbc) == 125 && EraFloorCeiling(TierEra::Wrath) == 213);

int main()
{
    bool ok = true;
    auto tier = [](TierEra era, std::vector<MasterItemSource> const& items) { return ResolveMasterTier(era, items); };
    std::vector<MasterItemSource> const quest = Repeat(Other(110), 8);

    // --- tier resolution ---
    MasterTier const t4 = tier(TierEra::Tbc, Join(Repeat(Raid(RaidPoint::TbcT4, 120), 8), quest));
    ok = Check(t4.known && t4.ceiling == 125 && t4.raidPieces == 8, "T4 master resolves to 125") && ok;
    MasterTier const t5 = tier(TierEra::Tbc, Repeat(Raid(RaidPoint::TbcT5, 133), 16));
    ok = Check(t5.known && t5.ceiling == 141, "full T5 master resolves to 141") && ok;
    MasterTier const stray = tier(TierEra::Tbc, Join(Repeat(Raid(RaidPoint::TbcT4, 120), 5), {Raid(RaidPoint::TbcSunwell, 159)}));
    ok = Check(stray.ceiling == 125, "one Sunwell piece among T4 stays T4") && ok;
    MasterTier const minority = tier(TierEra::Tbc, Join(Repeat(Raid(RaidPoint::TbcT6, 151), 3), Repeat(Raid(RaidPoint::TbcT4, 120), 5)));
    ok = Check(minority.ceiling == 125, "a T6 minority falls to the lower tier") && ok;
    MasterTier const half = tier(TierEra::Tbc, Join(Repeat(Raid(RaidPoint::TbcT6, 151), 4), Repeat(Raid(RaidPoint::TbcT4, 120), 4)));
    ok = Check(half.known && half.ceiling == 156, "half T6 reaches T6") && ok;
    MasterTier const single = tier(TierEra::Tbc, Join({Raid(RaidPoint::TbcT6, 151)}, quest));
    ok = Check(!single.known && single.ceiling == 125, "a single raid piece is unclear: era floor") && ok;
    MasterTier const none = tier(TierEra::Tbc, quest);
    ok = Check(!none.known && none.ceiling == 125, "no raid source: TBC floor 125") && ok;
    MasterTier const empty = tier(TierEra::Wrath, {});
    ok = Check(!empty.known && empty.ceiling == 213, "no data: Wrath floor 213") && ok;
    MasterTier const voa = tier(TierEra::Wrath, Repeat(Raid(RaidPoint::Ambiguous, 245), 8));
    ok = Check(!voa.known && voa.ceiling == 213, "cross-phase Vault of Archavon gives no tier") && ok;
    MasterTier const wrongEra = tier(TierEra::Wrath, Repeat(Raid(RaidPoint::TbcSunwell, 159), 8));
    ok = Check(!wrongEra.known && wrongEra.ceiling == 213, "TBC raid gear is no Wrath tier evidence") && ok;
    std::vector<MasterItemSource> unknownSource = Repeat(Raid(RaidPoint::TbcT6, 151), 8);
    for (MasterItemSource& item : unknownSource)
        item.raidSourceKnown = false;
    ok = Check(tier(TierEra::Tbc, unknownSource).ceiling == 125, "unknown sources fall to the floor") && ok;
    std::vector<MasterItemSource> broken = Repeat(Raid(RaidPoint::TbcT6, 151), 8);
    broken[3].itemLevel = 0;
    ok = Check(!tier(TierEra::Tbc, broken).known && tier(TierEra::Tbc, broken).ceiling == 125,
             "a broken slot record falls to the floor") &&
         ok;
    ok = Check(tier(TierEra::None, Repeat(Raid(RaidPoint::TbcT6, 151), 8)).ceiling == 0, "Vanilla has no tier ceiling") &&
         ok;

    // --- Wrath 10/25 overlap ---
    MasterTier const naxx25 = tier(TierEra::Wrath, Repeat(Raid(RaidPoint::WrathT7, 226, RaidSize::TwentyFive), 10));
    ok = Check(naxx25.ceiling == 226, "Naxx 25 master: 226") && ok;
    MasterTier const naxx10 = tier(TierEra::Wrath, Repeat(Raid(RaidPoint::WrathT7, 213, RaidSize::Ten), 10));
    ok = Check(naxx10.ceiling == 213, "Naxx 10 master: 213") && ok;
    MasterTier const naxxUnknown = tier(TierEra::Wrath, Repeat(Raid(RaidPoint::WrathT7, 226), 10));
    ok = Check(naxxUnknown.ceiling == 213, "Naxx of unknown size uses the 10-man ceiling") && ok;
    MasterTier const uld10 = tier(TierEra::Wrath, Repeat(Raid(RaidPoint::WrathT8, 219, RaidSize::Ten), 10));
    ok = Check(uld10.ceiling == 232, "Ulduar 10 master: 232") && ok;
    MasterTier const uld25 = tier(TierEra::Wrath, Repeat(Raid(RaidPoint::WrathT8, 232, RaidSize::TwentyFive), 10));
    ok = Check(uld25.ceiling == 239, "Ulduar 25 master: 239") && ok;
    MasterTier const toc25h = tier(TierEra::Wrath, Repeat(Raid(RaidPoint::WrathT9, 258, RaidSize::TwentyFive, true), 10));
    ok = Check(toc25h.ceiling == 245, "ToC 25 Heroic counts as ToC 25 normal 245 until the Heroic check") && ok;
    MasterTier const icc = tier(TierEra::Wrath, Repeat(Raid(RaidPoint::WrathT10, 264, RaidSize::TwentyFive), 10));
    ok = Check(icc.ceiling == 271, "ICC 25 master: 271") && ok;
    MasterTier const split = tier(TierEra::Wrath,
        Join(Repeat(Raid(RaidPoint::WrathT7, 226, RaidSize::TwentyFive), 6), Repeat(Raid(RaidPoint::WrathT8, 219, RaidSize::Ten), 2)));
    ok = Check(split.ceiling == 226, "mostly Naxx 25 with two Ulduar 10 pieces stays 226") && ok;

    // --- route: TBC/Wrath max level, normal autogear ---
    Fixture f;
    Prepare(f, 9, 70, 120, false, Join(Repeat(Raid(RaidPoint::TbcT4, 120), 8), quest));
    ok = Expect("T4 master avg 120 at stage 9: average binds", f, 120, CapMode::Leveling) && ok;
    Prepare(f, 10, 70, 133, false, Join(Repeat(Raid(RaidPoint::TbcT4, 125), 8), quest));
    ok = Expect("T4 master avg 133 (badge gear) at stage 10: T4 tier 125 binds", f, 125, CapMode::Leveling) && ok;
    Prepare(f, 10, 70, 138, false, Repeat(Raid(RaidPoint::TbcT5, 138), 16));
    ok = Expect("T5 master avg 138 at stage 10: average binds", f, 138, CapMode::Leveling) && ok;
    Prepare(f, 12, 70, 150, false, Join(Repeat(Raid(RaidPoint::TbcT6, 151), 6), Repeat(Raid(RaidPoint::TbcT4, 120), 10)));
    ok = Expect("mixed master falls to the lower tier", f, 125, CapMode::Leveling) && ok;
    Prepare(f, 12, 70, 150, false, Repeat(Raid(RaidPoint::TbcT6, 151), 16), false);
    ok = Expect("unverified sources fall to the TBC floor", f, 125, CapMode::Leveling) && ok;
    Prepare(f, 8, 70, 110, false, Repeat(Other(110), 16));
    ok = Expect("fresh 70 in quest gear: average binds", f, 110, CapMode::Leveling) && ok;
    Prepare(f, 9, 70, 160, false, Repeat(Raid(RaidPoint::TbcSunwell, 159), 16));
    ok = Expect("stage cap still binds above the worn tier", f, 125, CapMode::Leveling) && ok;
    Prepare(f, 15, 80, 230, false, Repeat(Raid(RaidPoint::WrathT7, 226, RaidSize::TwentyFive), 16));
    ok = Expect("Naxx 25 master avg 230 at stage 15: 226", f, 226, CapMode::Leveling) && ok;
    Prepare(f, 16, 80, 225, false, Repeat(Raid(RaidPoint::WrathT8, 219, RaidSize::Ten), 16));
    ok = Expect("Ulduar 10 master avg 225 at stage 16: average binds", f, 225, CapMode::Leveling) && ok;
    Prepare(f, 18, 80, 280, false, Repeat(Raid(RaidPoint::WrathT10, 277, RaidSize::TwentyFive, true), 16));
    ok = Expect("ICC 25 Heroic master at stage 18: normal 271", f, 271, CapMode::Leveling) && ok;
    Prepare(f, 9, 70, 120, false, quest);
    f.request.commandingSenderVerified = false;
    RouteDecision const noSample = ResolveTieredAutoGearRoute(f.request, f.snapshot, f.facts, ScoreOf);
    ok = Check(noSample.preserve && noSample.cap == 0, "no commander average preserves") && ok;
    Prepare(f, 11, 70, 120, false, quest);
    RouteDecision const s11 = ResolveTieredAutoGearRoute(f.request, f.snapshot, f.facts, ScoreOf);
    ok = Check(s11.preserve && s11.cap == 0, "stage 11 still refuses") && ok;

    // --- unchanged paths ---
    Prepare(f, 10, 70, 120, true, Repeat(Raid(RaidPoint::TbcT4, 120), 16));
    ok = Expect("BiS keeps the stage cap regardless of worn tier", f, 141, CapMode::Endgame) && ok;
    Prepare(f, 14, 80, 220, true, {});
    ok = Expect("Wrath BiS keeps the stage cap where there is no Heroic split", f, 226, CapMode::Endgame) && ok;
    Prepare(f, 18, 80, 220, true, {});
    ok = Expect("Wrath stage 18 BiS without Heroic evidence: ICC normal 271 (CP38)", f, 271, CapMode::Endgame) && ok;
    Prepare(f, 8, 65, 100, false, Repeat(Raid(RaidPoint::TbcSunwell, 159), 16));
    ok = Expect("leveling at stage 8 is unchanged", f, 100, CapMode::Leveling) && ok;
    Prepare(f, 13, 75, 180, false, {});
    ok = Expect("leveling at stage 13 is unchanged", f, 180, CapMode::Leveling) && ok;
    Prepare(f, 7, 60, 70, false, Repeat(Raid(RaidPoint::TbcT4, 120), 16));
    ok = Expect("Vanilla stage 7 max level: master average 70 binds", f, 70, CapMode::Leveling) && ok;
    Prepare(f, 0, 60, 62, false, {});
    ok = Expect("Vanilla first stage after leveling is unchanged (average)", f, 62, CapMode::Leveling) && ok;
    Prepare(f, 0, 60, 62, true, {});
    ok = Expect("Vanilla first-stage BiS is unchanged (stage cap)", f, 66, CapMode::Endgame) && ok;
    std::vector<std::uint32_t> master(16, 110);
    Prepare(f, 10, 70, 138, false, Repeat(Raid(RaidPoint::TbcT5, 138), 16));
    f.request.text = "match";
    f.request.masterSampleVerified = true;
    f.request.masterSlots = master;
    ok = Expect("match keeps the master's own average", f, 110, CapMode::Match) && ok;

    // --- CP34: Sunwell ceiling and Vanilla stages 1-7 ---
    ok = Check(RaidPointCeiling(RaidPoint::TbcSunwell, RaidSize::Unknown) == 159, "Sunwell ceiling is 159") && ok;
    ok = Check(RaidPointCeiling(RaidPoint::WrathT8, RaidSize::TwentyFive) == 239, "Ulduar 25 ceiling stays 239") && ok;
    MasterTier const sunwell = tier(TierEra::Tbc, Repeat(Raid(RaidPoint::TbcSunwell, 159), 16));
    ok = Check(sunwell.known && sunwell.ceiling == 159, "full Sunwell master resolves to 159") && ok;
    Prepare(f, 12, 70, 158, false, Repeat(Raid(RaidPoint::TbcSunwell, 159), 16));
    ok = Expect("Sunwell master avg 158 at stage 12: stage cap 156 binds", f, 156, CapMode::Leveling) && ok;
    Prepare(f, 12, 70, 152, false, Repeat(Raid(RaidPoint::TbcSunwell, 159), 16));
    ok = Expect("Sunwell master avg 152 at stage 12: average binds", f, 152, CapMode::Leveling) && ok;
    struct VanillaCase
    {
        std::uint8_t stage;
        std::uint32_t average;
        std::uint32_t stageCap;
    };
    VanillaCase const vanilla[] = {{1, 70, 78}, {2, 75, 78}, {3, 80, 83}, {4, 66, 83}, {5, 83, 83}, {6, 86, 88}, {7, 90, 92}};
    for (VanillaCase const& v : vanilla)
    {
        std::string const tag = "Vanilla stage " + std::to_string(v.stage);
        Prepare(f, v.stage, 60, v.average, false, {});
        ok = Expect(tag + " level 60: average binds", f, v.average, CapMode::Leveling) && ok;
        Prepare(f, v.stage, 60, v.stageCap + 20, false, {});
        ok = Expect(tag + " level 60: stage cap binds above it", f, v.stageCap, CapMode::Leveling) && ok;
        Prepare(f, v.stage, 60, v.average, true, {});
        ok = Expect(tag + " BiS keeps the stage cap", f, v.stageCap, CapMode::Endgame) && ok;
        Prepare(f, v.stage, 55, v.average, false, {});
        ok = Expect(tag + " leveling commander is unchanged", f, v.average, CapMode::Leveling) && ok;
    }
    Prepare(f, 7, 60, 70, false, {});
    f.request.senderLevelKnown = false;
    f.request.senderIsLeveling = false;
    ok = Expect("Vanilla stage 7 legacy max-level flag: average binds", f, 70, CapMode::Leveling) && ok;
    Prepare(f, 7, 60, 70, false, {});
    f.request.commandingSenderVerified = false;
    RouteDecision const vanillaNoSample = ResolveTieredAutoGearRoute(f.request, f.snapshot, f.facts, ScoreOf);
    ok = Check(vanillaNoSample.preserve && vanillaNoSample.cap == 0, "Vanilla stage 7 with no commander average preserves") && ok;
    Prepare(f, 7, 60, 70, false, {});
    f.request.text = "match";
    f.request.masterSampleVerified = true;
    std::vector<std::uint32_t> const vanillaMaster(16, 64);
    f.request.masterSlots = vanillaMaster;
    ok = Expect("Vanilla stage 7 match keeps the master's own average", f, 64, CapMode::Match) && ok;
    Prepare(f, 7, 60, 70, false, {});
    RouteDecision const rawVanilla = ResolveAutoGearRoute(f.request, ScoreOf);
    ok = Check(rawVanilla.cap == 92 && rawVanilla.mode == CapMode::Endgame, "the unwrapped route at stage 7 is unchanged") && ok;

    // --- G7: the source list must describe exactly the averaged sample ---
    {
        // CS01's example: 14 Naxx 10 + 2 Ulduar 25 pieces, average 230, stage 15 cap 245.
        auto naxxUld = [] {
            return Join(Repeat(Raid(RaidPoint::WrathT7, 230, RaidSize::Ten), 14),
                Repeat(Raid(RaidPoint::WrathT8, 230, RaidSize::TwentyFive), 2));
        };
        Prepare(f, 15, 80, 230, false, naxxUld());
        ok = Expect("G7 full 14 Naxx 10 + 2 Ulduar 25 list caps at 213", f, 213, CapMode::Leveling) && ok;
        std::vector<MasterItemSource> const onlyUld = Repeat(Raid(RaidPoint::WrathT8, 230, RaidSize::TwentyFive), 2);
        Prepare(f, 15, 80, 230, false, onlyUld);
        ok = Expect("G7 list of only the 2 Ulduar pieces gives no lift (213, not 230)", f, 213, CapMode::Leveling) && ok;
        RouteDecision const truncated = ResolveTieredAutoGearRoute(f.request, f.snapshot, f.facts, ScoreOf);
        ok = Check(truncated.cap <= 213, "G7 truncated list never goes above 213") && ok;
        ok = Check(!ResolveBoundMasterTier(TierEra::Wrath, f.slots, f.snapshot, f.facts).known,
                 "G7 truncated list is not a known tier") && ok;

        // Same answer through the pure helpers for every failure mode.
        auto liftOf = [](Fixture const& fx) { return ResolveBoundMasterTier(TierEra::Wrath, fx.slots, fx.snapshot, fx.facts); };
        std::vector<MasterItemSource> const fullUld = Repeat(Raid(RaidPoint::WrathT8, 230, RaidSize::TwentyFive), 16);
        Prepare(f, 15, 80, 230, false, fullUld);
        MasterTier const good = liftOf(f);
        ok = Check(good.known && good.ceiling == 239, "G7 bound full Ulduar 25 list lifts to 239 (control)") && ok;
        ok = Expect("G7 bound full Ulduar 25 list: average 230 binds", f, 230, CapMode::Leveling) && ok;

        Prepare(f, 15, 80, 230, false, Repeat(Raid(RaidPoint::WrathT8, 230, RaidSize::TwentyFive), 15));
        ok = Check(!liftOf(f).known && liftOf(f).ceiling == 213, "G7 missing one entry: floor 213") && ok;
        ok = Expect("G7 missing one entry: route 213", f, 213, CapMode::Leveling) && ok;

        Prepare(f, 15, 80, 230, false, fullUld);
        f.sources.push_back(Raid(RaidPoint::WrathT8, 230, RaidSize::TwentyFive));
        f.facts.sources = f.sources;
        ok = Check(!liftOf(f).known && liftOf(f).ceiling == 213, "G7 extra entry: floor 213") && ok;
        ok = Expect("G7 extra entry: route 213", f, 213, CapMode::Leveling) && ok;

        Prepare(f, 15, 80, 230, false, fullUld);
        f.sources[5].itemLevel = 232;
        ok = Check(!liftOf(f).known, "G7 one slot item level differs from the equipped sample: no lift") && ok;
        ok = Expect("G7 mismatched slot: route 213", f, 213, CapMode::Leveling) && ok;

        Prepare(f, 15, 80, 230, false, fullUld);
        f.slots[3] = 0;  // that slot is now empty on the commander, the list still claims a raid piece
        f.request.commandingSenderSlots = f.slots;
        ok = Check(!liftOf(f).known, "G7 list claims a piece in an empty slot: no lift") && ok;

        Prepare(f, 15, 80, 230, false, fullUld);
        f.slots[3] = 0;
        f.sources[3] = MasterItemSource{};
        f.identities[3] = SlotIdentity{};
        f.request.commandingSenderSlots = f.slots;
        f.facts.sources = f.sources;
        MasterTier const withEmpty = liftOf(f);
        ok = Check(withEmpty.known && withEmpty.ceiling == 239, "G7 an aligned empty slot is skipped (control)") && ok;

        Prepare(f, 15, 80, 230, false, fullUld);
        f.facts.equipmentRevision = 6;
        ok = Check(!liftOf(f).known, "G7 stale equipment revision: no lift") && ok;
        ok = Expect("G7 stale revision: route 213", f, 213, CapMode::Leveling) && ok;

        Prepare(f, 15, 80, 230, false, fullUld);
        f.facts.senderGuid = 43;
        ok = Check(!liftOf(f).known, "G7 list from another sender: no lift") && ok;
        ok = Expect("G7 other sender: route 213", f, 213, CapMode::Leveling) && ok;

        Prepare(f, 15, 80, 230, false, fullUld);
        f.snapshot.equipmentRevision = 8;  // the commander re-equipped; the old verified list is reused
        ok = Check(!liftOf(f).known, "G7 reused list after re-equip: no lift") && ok;

        // CP40b trust boundary: session, 64-bit revision and item identity.
        Prepare(f, 15, 80, 230, false, fullUld);
        f.facts.captureSession = 4;
        ok = Check(!liftOf(f).known, "CP40b list from another capture session: no lift") && ok;

        Prepare(f, 15, 80, 230, false, fullUld);
        f.facts.equipmentRevision = 7 + (std::uint64_t(1) << 32);
        ok = Check(!liftOf(f).known, "CP40b revision equal only in the low 32 bits: no lift (no truncation)") && ok;

        Prepare(f, 15, 80, 230, false, fullUld);
        f.identities[4].itemGuid = 9999;  // same item level, another item instance in that slot
        ok = Check(!liftOf(f).known, "CP40b same item level but another item GUID: no lift") && ok;

        Prepare(f, 15, 80, 230, false, fullUld);
        f.sources[4].entry = 61234;
        ok = Check(!liftOf(f).known, "CP40b source names another item entry: no lift") && ok;

        Prepare(f, 15, 80, 230, false, fullUld);
        f.snapshot.slots = {};
        ok = Check(!liftOf(f).known, "CP40b snapshot without its 19-slot identity sample: no lift") && ok;

        Prepare(f, 15, 80, 230, false, fullUld);
        f.facts.senderGuid = 0;
        f.facts.equipmentRevision = 0;
        f.snapshot = SenderSnapshot{};
        ok = Check(!liftOf(f).known, "G7 unbound list (no identity or revision): no lift") && ok;

        Prepare(f, 15, 80, 230, false, fullUld);
        RouteDecision const noSnapshot = ResolveTieredAutoGearRoute(f.request, f.facts, ScoreOf);
        ok = Check(noSnapshot.cap == 213, "G7 the snapshot-less overload gives no lift") && ok;

        Prepare(f, 15, 80, 230, false, fullUld, false);
        ok = Check(!liftOf(f).known, "G7 unverified list: no lift") && ok;

        Prepare(f, 10, 70, 138, false, Repeat(Raid(RaidPoint::TbcT5, 138), 2));
        ok = Expect("G7 TBC truncated T5 list: TBC floor 125", f, 125, CapMode::Leveling) && ok;
    }

    // --- CP36: Ruby Sanctum is its own point; Zul'Aman stays T5 ---
    ok = Check(RaidPointCeiling(RaidPoint::WrathRubySanctum, RaidSize::Ten) == 258, "Ruby Sanctum 10 ceiling 258") && ok;
    ok = Check(RaidPointCeiling(RaidPoint::WrathRubySanctum, RaidSize::TwentyFive) == 271, "Ruby Sanctum 25 ceiling 271") && ok;
    ok = Check(RaidPointCeiling(RaidPoint::WrathRubySanctum, RaidSize::Unknown) == 258, "Ruby Sanctum unknown size uses 10-man 258") && ok;
    ok = Check(RaidPointCeiling(RaidPoint::WrathT10, RaidSize::Ten) == 264 && RaidPointCeiling(RaidPoint::WrathT10, RaidSize::TwentyFive) == 271,
             "ICC stays 264 / 271") && ok;
    ok = Check(EraForPoint(RaidPoint::WrathRubySanctum) == TierEra::Wrath, "Ruby Sanctum is Wrath tier evidence") && ok;
    ok = Check(RaidPointCeiling(RaidPoint::TbcT5, RaidSize::Unknown) == 141, "Zul'Aman (T5) stays 141") && ok;
    MasterTier const rs10 = tier(TierEra::Wrath, Repeat(Raid(RaidPoint::WrathRubySanctum, 258, RaidSize::Ten), 10));
    ok = Check(rs10.known && rs10.ceiling == 258, "Ruby Sanctum 10 master: 258, not ICC 264") && ok;
    MasterTier const rs25 = tier(TierEra::Wrath, Repeat(Raid(RaidPoint::WrathRubySanctum, 271, RaidSize::TwentyFive), 10));
    ok = Check(rs25.known && rs25.ceiling == 271, "Ruby Sanctum 25 master: 271") && ok;
    MasterTier const rsIcc = tier(TierEra::Wrath,
        Join(Repeat(Raid(RaidPoint::WrathRubySanctum, 258, RaidSize::Ten), 4), Repeat(Raid(RaidPoint::WrathT10, 264, RaidSize::Ten), 4)));
    ok = Check(rsIcc.known && rsIcc.ceiling == 264, "half ICC 10 + half RS 10 reaches ICC 264") && ok;
    MasterTier const rsMostly = tier(TierEra::Wrath,
        Join(Repeat(Raid(RaidPoint::WrathRubySanctum, 258, RaidSize::Ten), 10), Repeat(Raid(RaidPoint::WrathT10, 264, RaidSize::Ten), 2)));
    ok = Check(rsMostly.ceiling == 258, "mostly RS 10 with two ICC 10 pieces stays 258") && ok;
    MasterTier const rsTbc = tier(TierEra::Tbc, Repeat(Raid(RaidPoint::WrathRubySanctum, 258, RaidSize::Ten), 10));
    ok = Check(!rsTbc.known && rsTbc.ceiling == 125, "Ruby Sanctum gear is no TBC tier evidence") && ok;
    Prepare(f, 18, 80, 265, false, Repeat(Raid(RaidPoint::WrathRubySanctum, 258, RaidSize::Ten), 16));
    ok = Expect("RS 10 master avg 265 at stage 18: 258", f, 258, CapMode::Leveling) && ok;
    Prepare(f, 18, 80, 275, false, Repeat(Raid(RaidPoint::WrathRubySanctum, 271, RaidSize::TwentyFive, true), 16));
    ok = Expect("RS 25 Heroic master avg 275 at stage 18: normal 271", f, 271, CapMode::Leveling) && ok;
    Prepare(f, 16, 80, 262, false, Repeat(Raid(RaidPoint::WrathRubySanctum, 258, RaidSize::Ten), 16));
    ok = Expect("RS 10 master at stage 16: 258 (stage cap 258 too)", f, 258, CapMode::Leveling) && ok;
    Prepare(f, 18, 80, 265, true, Repeat(Raid(RaidPoint::WrathRubySanctum, 258, RaidSize::Ten), 16));
    ok = Expect("RS 10 normal master BiS at stage 18: normal 271 (CP38 Heroic check)", f, 271, CapMode::Endgame) && ok;

    // The route itself is not edited (PR #42 also changes it): unwrapped, it
    // still gives the stage cap; only the wrapper applies the worn tier.
    Prepare(f, 10, 70, 133, false, Repeat(Raid(RaidPoint::TbcT4, 125), 16));
    RouteDecision const raw = ResolveAutoGearRoute(f.request, ScoreOf);
    ok = Check(raw.cap == 141 && raw.mode == CapMode::Endgame, "the unwrapped route is unchanged") && ok;

    std::cout << "checks=" << g_checks << "\n";
    return ok ? 0 : 1;
}
