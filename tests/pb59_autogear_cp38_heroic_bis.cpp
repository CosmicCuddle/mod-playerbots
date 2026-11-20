// CP38: master-Heroic check for `autogear bis`. Owner decision 18 Nov 2026
// 12:23 UK. BiS reaches a tier's Heroic item levels only when the master
// already wears Heroic gear of that tier or a later one; missing, unclear or
// unbound evidence falls to that tier's 25-man normal ceiling. The stage cap
// and era ceiling still apply.
// CP38b (owner delegated 19 Nov 2026 17:11 UK): known-normal pieces from a
// later tier at or above the Heroic level being unlocked also qualify.
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

MasterItemSource Piece(RaidPoint point, std::uint32_t itemLevel, RaidSize size, bool heroic, bool heroicKnown = true)
{
    MasterItemSource item;
    item.itemLevel = itemLevel;
    item.raidSourceKnown = true;
    item.point = point;
    item.size = size;
    item.heroic = heroic;
    item.heroicKnown = heroicKnown;
    return item;
}

MasterItemSource Other(std::uint32_t itemLevel)
{
    MasterItemSource item;
    item.itemLevel = itemLevel;
    item.raidSourceKnown = true;
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
        for (std::uint32_t level = 60; level <= 300; ++level)
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

// A level 80 commander whose equipped item levels are exactly the sources'.
void Prepare(Fixture& f, std::uint8_t stage, std::vector<MasterItemSource> sources, bool bis = true,
    std::uint8_t effective = 0)
{
    f.sources = std::move(sources);
    f.slots.clear();
    for (MasterItemSource const& source : f.sources)
        f.slots.push_back(source.itemLevel);
    f.facts = TierFacts{};
    f.facts.sourcesVerified = true;
    f.facts.sources = f.sources;
    f.facts.senderGuid = 42;
    f.facts.captureSession = 3;
    f.facts.equipmentRevision = 9;
    f.snapshot = SenderSnapshot{};
    f.snapshot.senderGuid = 42;
    f.snapshot.captureSession = 3;
    f.snapshot.equipmentRevision = 9;
    // CP40b: the captured identity sample, each source naming the same item.
    f.identities.assign(f.slots.size(), SlotIdentity{});
    for (std::size_t i = 0; i < f.slots.size(); ++i)
    {
        if (f.slots[i] == 0)
            continue;
        f.identities[i] = SlotIdentity{2000 + i, 50000 + static_cast<std::uint32_t>(i), f.slots[i]};
        f.sources[i].itemGuid = f.identities[i].itemGuid;
        f.sources[i].entry = f.identities[i].entry;
    }
    f.snapshot.slots = f.identities;
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
    r.effectiveStage = effective == 0 ? stage : effective;
    r.qualityLimit = 4;
    r.senderLevelKnown = true;
    r.senderLevel = 80;
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

bool Expect(std::string const& name, Fixture const& f, std::uint32_t cap, CapMode mode = CapMode::Endgame)
{
    RouteDecision const d = ResolveTieredAutoGearRoute(f.request, f.snapshot, f.facts, ScoreOf);
    bool const ok = d.cap == cap && d.mode == mode && d.action == GearAction::CommitSet &&
                    d.plan.selected[4].entryId == cap && !d.deletesGear;
    if (!ok)
        std::cerr << "  got cap=" << d.cap << " mode=" << static_cast<int>(d.mode)
                  << " action=" << static_cast<int>(d.action) << "\n";
    return Check(ok, name);
}

std::vector<MasterItemSource> Icc25H() { return Repeat(Piece(RaidPoint::WrathT10, 277, RaidSize::TwentyFive, true), 16); }
std::vector<MasterItemSource> Icc10H() { return Repeat(Piece(RaidPoint::WrathT10, 264, RaidSize::Ten, true), 16); }
std::vector<MasterItemSource> Icc25N() { return Repeat(Piece(RaidPoint::WrathT10, 264, RaidSize::TwentyFive, false), 16); }
std::vector<MasterItemSource> Toc25H() { return Repeat(Piece(RaidPoint::WrathT9, 258, RaidSize::TwentyFive, true), 16); }
std::vector<MasterItemSource> Toc25N() { return Repeat(Piece(RaidPoint::WrathT9, 245, RaidSize::TwentyFive, false), 16); }
std::vector<MasterItemSource> Rs25H() { return Repeat(Piece(RaidPoint::WrathRubySanctum, 284, RaidSize::TwentyFive, true), 16); }
std::vector<MasterItemSource> Rs25N() { return Repeat(Piece(RaidPoint::WrathRubySanctum, 271, RaidSize::TwentyFive, false), 16); }
}  // namespace

// Item-record splits (normal / Heroic, 10 / 25).
static_assert(RaidPointCeiling(RaidPoint::WrathT9, RaidSize::Ten) == 232 && RaidPointCeiling(RaidPoint::WrathT9, RaidSize::TwentyFive) == 245);
static_assert(RaidPointHeroicCeiling(RaidPoint::WrathT9, RaidSize::Ten) == 245 && RaidPointHeroicCeiling(RaidPoint::WrathT9, RaidSize::TwentyFive) == 258);
static_assert(RaidPointCeiling(RaidPoint::WrathT10, RaidSize::Ten) == 264 && RaidPointCeiling(RaidPoint::WrathT10, RaidSize::TwentyFive) == 271);
static_assert(RaidPointHeroicCeiling(RaidPoint::WrathT10, RaidSize::Ten) == 271 && RaidPointHeroicCeiling(RaidPoint::WrathT10, RaidSize::TwentyFive) == 284);
static_assert(RaidPointCeiling(RaidPoint::WrathRubySanctum, RaidSize::Ten) == 258 && RaidPointCeiling(RaidPoint::WrathRubySanctum, RaidSize::TwentyFive) == 271);
static_assert(RaidPointHeroicCeiling(RaidPoint::WrathRubySanctum, RaidSize::Ten) == 271 && RaidPointHeroicCeiling(RaidPoint::WrathRubySanctum, RaidSize::TwentyFive) == 284);
static_assert(RaidPointHeroicCeiling(RaidPoint::WrathT8, RaidSize::TwentyFive) == 0, "Ulduar has no Heroic tier");
static_assert(RaidPointHeroicCeiling(RaidPoint::WrathT7, RaidSize::TwentyFive) == 0 &&
              RaidPointHeroicCeiling(RaidPoint::TbcSunwell, RaidSize::Unknown) == 0 &&
              RaidPointHeroicCeiling(RaidPoint::Ambiguous, RaidSize::TwentyFive) == 0);
static_assert(RaidPointHeroicCeiling(RaidPoint::WrathT10, RaidSize::Unknown) == 271, "unknown size uses 10-man");

int main()
{
    bool ok = true;

    // --- stage splits ---
    for (std::uint8_t stage = 0; stage <= 14; ++stage)
        ok = Check(HeroicSplitForStage(stage).point == RaidPoint::None, "no Heroic split at stage " + std::to_string(stage)) && ok;
    ok = Check(HeroicSplitForStage(15).point == RaidPoint::WrathT9 && HeroicSplitForStage(15).normalCeiling == 245, "stage 15: ToC 245") && ok;
    ok = Check(HeroicSplitForStage(16).point == RaidPoint::WrathT9 && HeroicSplitForStage(16).heroicCeiling == 258, "stage 16: ToC Heroic 258") && ok;
    ok = Check(HeroicSplitForStage(17).point == RaidPoint::WrathT10 && HeroicSplitForStage(17).normalCeiling == 271 &&
                   HeroicSplitForStage(17).heroicCeiling == 284, "stage 17: ICC 271 / 284") && ok;
    ok = Check(HeroicSplitForStage(18).point == RaidPoint::WrathT10, "stage 18: ICC / Ruby Sanctum") && ok;
    ok = Check(HeroicSplitForStage(19).point == RaidPoint::None, "no split past stage 18") && ok;

    Fixture f;
    // --- Heroic of the same tier ---
    Prepare(f, 17, Icc25H());
    ok = Expect("stage 17, master in ICC 25 Heroic: 284", f, 284) && ok;
    Prepare(f, 17, Icc10H());
    ok = Expect("stage 17, master in ICC 10 Heroic: 284", f, 284) && ok;
    Prepare(f, 16, Toc25H());
    ok = Expect("stage 16, master in ToC 25 Heroic: 258", f, 258) && ok;
    Prepare(f, 18, Icc25H());
    ok = Expect("stage 18, master in ICC 25 Heroic: 284", f, 284) && ok;

    // --- Heroic of a higher tier ---
    Prepare(f, 17, Rs25H());
    ok = Expect("stage 17, master in Ruby Sanctum Heroic (later): 284", f, 284) && ok;
    Prepare(f, 16, Icc25H());
    ok = Expect("stage 16, master in ICC Heroic (later): ToC Heroic 258", f, 258) && ok;
    Prepare(f, 18, Rs25H());
    ok = Expect("stage 18, master in Ruby Sanctum Heroic: 284", f, 284) && ok;

    // --- Heroic of a lower tier ---
    Prepare(f, 17, Toc25H());
    ok = Expect("stage 17, master in ToC Heroic only (earlier): ICC normal 271", f, 271) && ok;
    Prepare(f, 18, Toc25H());
    ok = Expect("stage 18, master in ToC Heroic only: 271", f, 271) && ok;

    // --- normal only ---
    Prepare(f, 17, Icc25N());
    ok = Expect("stage 17, master in ICC 25 normal: 271", f, 271) && ok;
    Prepare(f, 16, Toc25N());
    ok = Expect("stage 16, master in ToC 25 normal: 245", f, 245) && ok;
    Prepare(f, 16, Icc25N());
    ok = Expect("stage 16, master in ICC 25 normal 264 (CP38b later-tier normal >= 258): ToC Heroic 258", f, 258) && ok;
    Prepare(f, 18, Rs25N());
    ok = Expect("stage 18, master in Ruby Sanctum normal: 271", f, 271) && ok;
    Prepare(f, 17, Repeat(Other(245), 16));
    ok = Expect("stage 17, master in non-raid gear: 271", f, 271) && ok;

    // --- unclear evidence: fail closed to normal ---
    Prepare(f, 17, Repeat(Piece(RaidPoint::WrathT10, 277, RaidSize::TwentyFive, true, false), 16));
    ok = Expect("Heroic flag with unknown difficulty: 271", f, 271) && ok;
    Prepare(f, 17, Join(Icc25H(), {Piece(RaidPoint::WrathT8, 239, RaidSize::TwentyFive, true)}));
    ok = Expect("a Heroic claim on Ulduar (no Heroic tier) makes it unclear: 271", f, 271) && ok;
    Prepare(f, 17, Join(Icc25H(), {Piece(RaidPoint::Ambiguous, 264, RaidSize::TwentyFive, true)}));
    ok = Expect("a Heroic claim on an ambiguous source: 271", f, 271) && ok;
    {
        std::vector<MasterItemSource> unknownRaid = Icc25H();
        unknownRaid[2].raidSourceKnown = false;
        Prepare(f, 17, unknownRaid);
        ok = Expect("a Heroic flag on an unknown raid source: 271", f, 271) && ok;
    }
    Prepare(f, 17, Repeat(Piece(RaidPoint::WrathT10, 284, RaidSize::Ten, true), 16));
    ok = Expect("ICC 10 Heroic claimed at 284 (above its 271): unclear, 271", f, 271) && ok;
    Prepare(f, 17, Join({Piece(RaidPoint::WrathT10, 277, RaidSize::TwentyFive, true)}, Icc25N()));
    ok = Expect("a single Heroic piece is not enough: 271", f, 271) && ok;
    Prepare(f, 17, Join(Repeat(Piece(RaidPoint::WrathT10, 277, RaidSize::TwentyFive, true), 5), Repeat(Piece(RaidPoint::WrathT10, 264, RaidSize::TwentyFive, false), 11)));
    ok = Expect("a Heroic minority (5 of 16) is not enough: 271", f, 271) && ok;
    Prepare(f, 17, Join(Repeat(Piece(RaidPoint::WrathT10, 277, RaidSize::TwentyFive, true), 8), Repeat(Piece(RaidPoint::WrathT10, 264, RaidSize::TwentyFive, false), 8)));
    ok = Expect("half Heroic (8 of 16) qualifies: 284", f, 284) && ok;
    Prepare(f, 17, Join(Repeat(Piece(RaidPoint::WrathT10, 277, RaidSize::TwentyFive, true), 4), Repeat(Other(232), 12)));
    ok = Expect("Heroic pieces among non-raid gear count against raid pieces only: 284", f, 284) && ok;

    // --- unbound evidence (G7 rules): fail closed to normal ---
    Prepare(f, 17, Icc25H());
    f.facts.sourcesVerified = false;
    ok = Expect("unverified source list: 271", f, 271) && ok;
    Prepare(f, 17, Icc25H());
    f.facts.equipmentRevision = 8;
    ok = Expect("stale equipment revision: 271", f, 271) && ok;
    Prepare(f, 17, Icc25H());
    f.facts.senderGuid = 43;
    ok = Expect("list from another sender: 271", f, 271) && ok;
    Prepare(f, 17, Icc25H());
    f.sources.pop_back();
    f.facts.sources = f.sources;
    ok = Expect("one entry missing: 271", f, 271) && ok;
    Prepare(f, 17, Icc25H());
    f.sources.push_back(Piece(RaidPoint::WrathT10, 277, RaidSize::TwentyFive, true));
    f.facts.sources = f.sources;
    ok = Expect("one extra entry: 271", f, 271) && ok;
    Prepare(f, 17, Icc25H());
    f.slots[3] = 264;
    f.request.commandingSenderSlots = f.slots;
    ok = Expect("an item level that differs from the equipped slot: 271", f, 271) && ok;
    Prepare(f, 17, Icc25H());
    f.request.commandingSenderVerified = false;
    ok = Expect("no verified master sample: 271", f, 271) && ok;
    Prepare(f, 17, Icc25H());
    {
        RouteDecision const noSnapshot = ResolveTieredAutoGearRoute(f.request, f.facts, ScoreOf);
        ok = Check(noSnapshot.cap == 271 && noSnapshot.mode == CapMode::Endgame, "the snapshot-less overload: 271") && ok;
    }
    Prepare(f, 17, Icc25H());
    ok = Check(!MasterWearsHeroic(RaidPoint::WrathT10, f.slots, SenderSnapshot{}, f.facts), "no snapshot: not Heroic") && ok;
    ok = Check(!MasterWearsHeroic(RaidPoint::WrathT8, f.slots, f.snapshot, f.facts), "Ulduar can never be the required Heroic tier") && ok;

    // --- CP38b: later-tier normal pieces at or above the Heroic level ---
    Prepare(f, 16, Repeat(Piece(RaidPoint::WrathT10, 258, RaidSize::TwentyFive, false), 16));
    ok = Expect("CP38b ICC normal at exactly 258 unlocks ToC Heroic 258", f, 258) && ok;
    Prepare(f, 16, Repeat(Piece(RaidPoint::WrathT10, 251, RaidSize::Ten, false), 16));
    ok = Expect("CP38b ICC 10 normal 251 (below 258) does not unlock: 245", f, 245) && ok;
    Prepare(f, 16, Repeat(Piece(RaidPoint::WrathRubySanctum, 258, RaidSize::Ten, false), 16));
    ok = Expect("CP38b Ruby Sanctum 10 normal 258 unlocks ToC Heroic 258", f, 258) && ok;
    Prepare(f, 16, Join(Repeat(Piece(RaidPoint::WrathT10, 264, RaidSize::TwentyFive, false), 1),
                        Repeat(Piece(RaidPoint::WrathT9, 245, RaidSize::TwentyFive, false), 15)));
    ok = Expect("CP38b one later-tier normal piece is not enough: 245", f, 245) && ok;
    Prepare(f, 16, Join(Repeat(Piece(RaidPoint::WrathT10, 264, RaidSize::TwentyFive, false), 5),
                        Repeat(Piece(RaidPoint::WrathT9, 245, RaidSize::TwentyFive, false), 11)));
    ok = Expect("CP38b later-tier normal minority (5 of 16): 245", f, 245) && ok;
    Prepare(f, 16, Join(Join(Repeat(Piece(RaidPoint::WrathT9, 258, RaidSize::TwentyFive, true), 4),
                             Repeat(Piece(RaidPoint::WrathT10, 264, RaidSize::TwentyFive, false), 4)),
                        Repeat(Piece(RaidPoint::WrathT9, 245, RaidSize::TwentyFive, false), 8)));
    ok = Expect("CP38b mix: 4 ToC Heroic + 4 ICC normal (8 of 16) unlocks 258", f, 258) && ok;
    Prepare(f, 16, Join(Join(Repeat(Piece(RaidPoint::WrathT9, 258, RaidSize::TwentyFive, true), 3),
                             Repeat(Piece(RaidPoint::WrathT10, 251, RaidSize::Ten, false), 4)),
                        Repeat(Piece(RaidPoint::WrathT9, 245, RaidSize::TwentyFive, false), 9)));
    ok = Expect("CP38b mix with below-threshold normal pieces (3 of 16 qualify): 245", f, 245) && ok;
    Prepare(f, 16, Repeat(Piece(RaidPoint::WrathT9, 245, RaidSize::TwentyFive, false), 16));
    ok = Expect("CP38b same-tier normal never unlocks its own Heroic: 245", f, 245) && ok;
    Prepare(f, 16, Repeat(Piece(RaidPoint::WrathT10, 264, RaidSize::TwentyFive, false, false), 16));
    ok = Expect("CP38b later-tier pieces with unknown difficulty do not count: 245", f, 245) && ok;
    Prepare(f, 16, Repeat(Piece(RaidPoint::WrathT10, 271, RaidSize::Ten, false), 16));
    ok = Expect("CP38b ICC 10 normal claimed at 271 (above its 264): unclear, 245", f, 245) && ok;
    Prepare(f, 17, Rs25N());
    ok = Expect("CP38b Ruby Sanctum normal 271 is below ICC Heroic 284: 271", f, 271) && ok;
    Prepare(f, 18, Repeat(Piece(RaidPoint::WrathRubySanctum, 271, RaidSize::TwentyFive, false), 16));
    ok = Expect("CP38b stage 18: no later tier can reach 284 at normal: 271", f, 271) && ok;
    Prepare(f, 16, Icc25N());
    f.facts.equipmentRevision = 8;
    ok = Expect("CP38b later-tier normal with a stale list: 245", f, 245) && ok;
    Prepare(f, 16, Icc25N());
    f.sources.pop_back();
    f.facts.sources = f.sources;
    ok = Expect("CP38b later-tier normal with a missing entry: 245", f, 245) && ok;
    Prepare(f, 16, Icc25N());
    f.slots[0] = 251;
    f.request.commandingSenderSlots = f.slots;
    ok = Expect("CP38b later-tier normal with a mismatched item level: 245", f, 245) && ok;
    Prepare(f, 16, Icc25N());
    f.request.scoreLimit = 250;
    ok = Expect("CP38b unlock never passes a lower configured limit: 250", f, 250) && ok;
    Prepare(f, 16, Icc25N(), true, 12);
    ok = Expect("CP38b raw 16 at TBC effective 12: era ceiling 159", f, 159) && ok;
    Prepare(f, 15, Icc25N());
    ok = Expect("CP38b stage 15: stage cap 245 still binds", f, 245) && ok;
    Prepare(f, 16, Rs25N());
    ok = Expect("CP38b stage 16 Ruby Sanctum normal 271: stage cap 258", f, 258) && ok;
    Prepare(f, 16, Icc25N());
    ok = Check(MasterWearsHeroic(RaidPoint::WrathT9, f.slots, f.snapshot, f.facts, 258), "CP38b helper: ICC normal 264 meets 258") && ok;
    ok = Check(!MasterWearsHeroic(RaidPoint::WrathT9, f.slots, f.snapshot, f.facts, 265), "CP38b helper: ICC normal 264 misses 265") && ok;
    ok = Check(!MasterWearsHeroic(RaidPoint::WrathT10, f.slots, f.snapshot, f.facts), "CP38b helper: same tier normal never meets its own Heroic") && ok;

    // --- stage cap and era ceiling still bind ---
    Prepare(f, 15, Toc25H());
    ok = Expect("stage 15, ToC Heroic master: stage cap 245 binds", f, 245) && ok;
    Prepare(f, 15, Toc25N());
    ok = Expect("stage 15, ToC normal master: 245", f, 245) && ok;
    Prepare(f, 16, Rs25H());
    ok = Expect("stage 16, Ruby Sanctum Heroic master: stage cap 258 binds", f, 258) && ok;
    Prepare(f, 17, Icc25H(), true, 12);
    ok = Expect("raw 17 at TBC effective stage 12, ICC Heroic master: TBC era ceiling 159", f, 159) && ok;
    Prepare(f, 17, Icc25N(), true, 12);
    ok = Expect("raw 17 at TBC effective stage 12, normal master: 159", f, 159) && ok;
    Prepare(f, 18, Rs25H());
    f.request.scoreLimit = 275;
    ok = Expect("a lower configured limit (275) still binds over Heroic 284", f, 275) && ok;
    Prepare(f, 17, Icc25N());
    f.request.scoreLimit = 290;
    ok = Expect("a higher configured limit (290) cannot lift the normal 271", f, 271) && ok;
    {
        Prepare(f, 18, Rs25H());
        RouteDecision const d = ResolveTieredAutoGearRoute(f.request, f.snapshot, f.facts, ScoreOf);
        ok = Check(d.cap <= ApprovedWrathEraCeiling, "never above the Wrath era ceiling 284") && ok;
    }

    // --- no Heroic split: unchanged ---
    Prepare(f, 14, Repeat(Piece(RaidPoint::WrathT8, 239, RaidSize::TwentyFive, false), 16));
    ok = Expect("stage 14 (Ulduar, no Heroic): stage cap 226", f, 226) && ok;
    Prepare(f, 13, Repeat(Piece(RaidPoint::WrathT7, 226, RaidSize::TwentyFive, false), 16));
    ok = Expect("stage 13: stage cap 226", f, 226) && ok;
    Prepare(f, 12, Repeat(Piece(RaidPoint::TbcT6, 151, RaidSize::Unknown, false), 16));
    f.request.senderLevel = 70;
    ok = Expect("TBC stage 12 BiS: stage cap 156", f, 156) && ok;

    // --- BiS numeric and other forms ---
    Prepare(f, 17, Icc25N());
    f.request.text = "280";
    ok = Expect("bis 280 with a normal master: 271", f, 271) && ok;
    Prepare(f, 17, Icc25H());
    f.request.text = "280";
    ok = Expect("bis 280 with an ICC Heroic master: 280", f, 280) && ok;
    Prepare(f, 17, Icc25N(), false);
    ok = Expect("normal autogear is not touched by the Heroic check (average 264)", f, 264, CapMode::Leveling) && ok;
    Prepare(f, 17, Icc25H(), false);
    ok = Expect("normal autogear, ICC Heroic master avg 277: worn tier 271", f, 271, CapMode::Leveling) && ok;
    Prepare(f, 17, Icc25N());
    f.request.senderLevel = 75;
    f.request.senderFixedKnown = true;
    f.request.senderFixedAverage = 264;
    ok = Expect("leveling master BiS still never above ICC normal: average 264", f, 264, CapMode::Leveling) && ok;
    Prepare(f, 17, Icc25H());
    f.request.tierSync = true;
    {
        RouteDecision const unwrapped = ResolveAutoGearRoute(f.request, ScoreOf);
        RouteDecision const wrapped = ResolveTieredAutoGearRoute(f.request, f.snapshot, f.facts, ScoreOf);
        ok = Check(wrapped.cap == unwrapped.cap && wrapped.action == unwrapped.action, "tier sync is untouched") && ok;
    }
    Prepare(f, 17, Icc25N());
    {
        RouteDecision const raw = ResolveAutoGearRoute(f.request, ScoreOf);
        ok = Check(raw.cap == 284, "the unwrapped route is unchanged (stage cap 284)") && ok;
    }

    std::cout << "checks=" << g_checks << "\n";
    return ok ? 0 : 1;
}
