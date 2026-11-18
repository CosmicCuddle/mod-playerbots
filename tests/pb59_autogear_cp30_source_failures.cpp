// CP30 G2/G4: native-source failure regressions. Every unknown, stale, forged,
// truncated or unsearched source must Preserve. None may strip to bags or
// claim an empty domain. Controls show a proven input can still commit.
#include <iostream>
#include <string>
#include <vector>

#include "AutoGearCandidateCollector.h"
#include "AutoGearNativeCandidateCollector.h"

using namespace PlayerbotsAutoGearSelection;

namespace
{
constexpr std::uint32_t kChest = 1u << 4;
constexpr std::uint32_t kFinger = (1u << 10) | (1u << 11);
constexpr std::uint32_t kTrinket = (1u << 12) | (1u << 13);
constexpr std::uint64_t kRevision = 11;

int g_checks = 0;
std::vector<VerifiedItemIdentity> g_names{};

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

std::uint32_t ScoreOf(std::uint32_t id, std::int32_t, std::uint32_t) { return 10 + id % 7; }

VerifiedItemIdentity Known(std::uint32_t entry)
{
    VerifiedItemIdentity identity;
    identity.entry = entry;
    identity.verified = true;
    return identity;
}

BotView Warrior(std::uint32_t level)
{
    BotView bot;
    bot.levelKnown = true;
    bot.level = level;
    bot.classKnown = true;
    bot.classId = 1;
    bot.specKnown = true;
    bot.roleKnown = true;
    bot.role = Role::Melee;
    bot.armorKnown = true;
    bot.armorMask = 1u << 4;
    bot.weaponsKnown = true;
    return bot;
}

CandidateTemplate Item(std::uint32_t id, std::uint8_t slot, std::uint32_t requiredLevel = 1)
{
    CandidateTemplate row;
    row.loaded = true;
    row.metadataKnown = true;
    row.entry = id;
    row.itemLevel = 10;
    row.requiredLevel = requiredLevel;
    row.qualityKnown = true;
    row.quality = 2;
    row.slotKnown = true;
    row.slot = slot;
    row.groupsKnown = true;
    row.equipKnown = true;
    row.equipAllowed = true;
    row.scoreKnown = true;
    row.limitKnown = true;
    row.setEvidenceKnown = true;
    return row;
}

RetainedCount Count(std::uint32_t entry, std::uint32_t category, std::uint32_t equipped, std::uint32_t gems,
    bool gemsKnown = true)
{
    RetainedCount row;
    row.entryId = entry;
    row.limitCategory = category;
    row.bagsKnown = true;
    row.bankKnown = true;
    row.equippedKnown = true;
    row.gemsKnown = gemsKnown;
    row.equipped = equipped;
    row.gems = gems;
    return row;
}

// Best case a caller could claim: exhaustive, complete, authority valid.
CollectorRequest Claimed(BotView const& bot, std::uint32_t eligible, std::uint32_t required)
{
    CollectorRequest request;
    request.capSupplied = true;
    request.resolvedCap.preserve = false;
    request.resolvedCap.cap = 200;
    request.qualityLimitKnown = true;
    request.qualityLimit = 4;
    request.bot = bot;
    request.identities = g_names;
    request.eligibleMask = eligible;
    request.requiredMask = required;
    request.completeMask = eligible;
    request.sourceExhausted = true;
    request.exhaustiveLegalDomain = true;
    request.sourceRevision = kRevision;
    request.bags = BagSpace::Enough;
    request.authorityValid = true;
    request.populationVerified = true;
    request.population = CountPopulation::AltBot;
    return request;
}

CollectorResult Run(CollectorRequest request, std::vector<CandidateTemplate> const& rows,
    std::vector<RetainedCount> const& retained = {})
{
    request.rows = rows;
    request.retained = retained;
    return CollectCandidateFacts(request, ScoreOf);
}

bool Held(std::string const& name, CollectorResult const& result)
{
    bool const held = result.action == GearAction::Preserve && result.preserve && !result.claimedEmpty &&
                      !result.deletesGear && !result.plan.sourceComplete &&
                      result.plan.outcome.action != GearAction::StripToBags;
    bool noneSelected = true;
    for (SelectedPiece const& piece : result.plan.selected)
    {
        if (piece.entryId != 0 && result.action != GearAction::Preserve)
            noneSelected = false;
    }
    return Check(held && noneSelected, name + " preserves without an empty claim");
}

bool HeldPlan(std::string const& name, Plan const& plan)
{
    bool ok = plan.outcome.action == GearAction::Preserve && !plan.sourceComplete;
    for (SelectedPiece const& piece : plan.selected)
        ok = ok && piece.entryId == 0;
    return Check(ok, name + " direct plan preserves");
}

PlanInput Direct(BotView const& bot, std::vector<ItemFact> const& facts, std::uint32_t eligible,
    std::uint32_t required)
{
    PlanInput in;
    in.cap.preserve = false;
    in.cap.cap = 200;
    in.bot = bot;
    in.facts = facts;
    in.eligibleMask = eligible;
    in.requiredMask = required;
    in.completeMask = eligible;
    in.authorityValid = true;
    in.bags = BagSpace::Enough;
    return in;
}

// Native cache fakes (field names follow ItemTemplate at core 28df5c5f).
struct Proto
{
    std::uint32_t ItemId = 0;
    std::uint32_t Class = 4;
    std::uint32_t SubClass = 0;
    std::uint32_t Quality = 2;
    std::uint32_t Flags = 0;
    std::uint32_t InventoryType = 11;
    std::uint32_t AllowableClass = 0xFFFFFFFFu;
    std::uint32_t ItemLevel = 10;
    std::uint32_t RequiredLevel = 10;
    std::int32_t MaxCount = 0;
    std::int32_t RandomProperty = 0;
    std::int32_t RandomSuffix = 0;
    std::uint32_t ItemSet = 0;
    std::uint32_t ItemLimitCategory = 0;
};

enum class CacheInventory : std::uint32_t
{
};

struct LimitEntry
{
    std::uint32_t ID = 0;
    std::uint32_t maxCount = 0;
    std::uint32_t mode = 0;
};

struct SetEntry
{
    std::uint32_t spells[8]{};
    std::uint32_t items_to_triggerspell[8]{};
};

struct Cache
{
    std::vector<std::uint32_t> rings{};
    std::vector<std::uint32_t> empty{};

    std::vector<std::uint32_t> const& GetEquipment(std::uint32_t, std::uint8_t, std::uint8_t slot,
        std::uint32_t) const
    {
        return slot == 10 || slot == 11 ? rings : empty;
    }

    std::vector<std::uint32_t> const& GetEquipmentNew(std::uint32_t, CacheInventory) const { return empty; }
};
}  // namespace

int main()
{
    bool ok = true;
    g_names = {Known(100), Known(101), Known(200), Known(201), Known(300), Known(301), Known(400)};
    std::vector<CandidateTemplate> const chest{Item(200, 4)};

    // Control: a fully proven chest list commits. Failures below are not a
    // side effect of a fixture that can never commit.
    CollectorResult const control = Run(Claimed(Warrior(60), kChest, kChest), chest);
    ok = Check(control.action == GearAction::CommitSet && control.plan.selected[4].entryId == 200 &&
                   !control.deletesGear,
             "control: a proven chest list commits") &&
         ok;

    // --- G2.1 missing / stale / fabricated / truncated provenance ---
    {
        std::vector<CandidateTemplate> missing{Item(200, 4)};
        missing[0].loaded = false;
        ok = Held("missing template", Run(Claimed(Warrior(60), kChest, kChest), missing)) && ok;
        missing[0] = Item(200, 4);
        missing[0].metadataKnown = false;
        ok = Held("missing metadata", Run(Claimed(Warrior(60), kChest, kChest), missing)) && ok;
        missing[0] = Item(200, 4);
        missing[0].itemLevel = 0;
        ok = Held("zero item level", Run(Claimed(Warrior(60), kChest, kChest), missing)) && ok;
        missing[0] = Item(200, 4);
        missing[0].slotKnown = false;
        ok = Held("unknown slot", Run(Claimed(Warrior(60), kChest, kChest), missing)) && ok;

        std::vector<CandidateTemplate> unlisted{Item(999, 4)};
        ok = Held("identity not verified", Run(Claimed(Warrior(60), kChest, kChest), unlisted)) && ok;
        std::vector<VerifiedItemIdentity> twice{Known(200), Known(200)};
        CollectorRequest dup = Claimed(Warrior(60), kChest, kChest);
        dup.identities = twice;
        ok = Held("duplicate identity record", Run(dup, chest)) && ok;
        std::vector<VerifiedItemIdentity> unverified{Known(200)};
        unverified[0].verified = false;
        CollectorRequest uv = Claimed(Warrior(60), kChest, kChest);
        uv.identities = unverified;
        ok = Held("unverified identity record", Run(uv, chest)) && ok;

        std::vector<CandidateTemplate> forged{Item(200, 4), Item(200, 4)};
        forged[1].itemLevel = 11;
        CollectorResult const clash = Run(Claimed(Warrior(60), kChest, kChest), forged);
        ok = Held("fabricated duplicate template", clash) && ok;
        ok = Check(clash.conflict, "fabricated duplicate template is flagged as a conflict") && ok;

        CollectorRequest noRev = Claimed(Warrior(60), kChest, kChest);
        noRev.sourceRevision = 0;
        CollectorResult const unrevised = Run(noRev, chest);
        ok = Check(!unrevised.nativeSourceUsed && unrevised.action != GearAction::StripToBags && !unrevised.claimedEmpty,
                 "revision 0 never uses the native source or claims empty") &&
             ok;

        CollectorRequest cut = Claimed(Warrior(60), kChest, kChest);
        cut.sourceBudget = 1;
        std::vector<CandidateTemplate> two{Item(200, 4), Item(201, 4)};
        CollectorResult const overSource = Run(cut, two);
        ok = Held("source budget exceeded", overSource) && ok;
        ok = Check(overSource.truncated, "source budget exceeded is truncated") && ok;
        CollectorRequest rowCut = Claimed(Warrior(60), kChest, kChest);
        rowCut.rowBudget = 1;
        ok = Held("row budget exceeded", Run(rowCut, two)) && ok;
        CollectorRequest expandCut = Claimed(Warrior(60), kChest, kChest);
        expandCut.expansionBudget = 1;
        ok = Held("expansion budget exceeded", Run(expandCut, two)) && ok;
        CollectorRequest notDone = Claimed(Warrior(60), kChest, kChest);
        notDone.sourceExhausted = false;
        ok = Held("source not exhausted, empty list", Run(notDone, {})) && ok;
        ok = Held("source not exhausted, one row", Run(notDone, chest)) && ok;
    }

    // --- G2.2 completeMask must not mask an unsearched or unproven slot ---
    {
        CollectorRequest noDomain = Claimed(Warrior(60), kChest, kChest);
        noDomain.exhaustiveLegalDomain = false;
        ok = Held("complete mask without an exhaustive domain, empty list", Run(noDomain, {})) && ok;
        ok = Held("complete mask without an exhaustive domain, one row", Run(noDomain, chest)) && ok;
        CollectorRequest noAuthority = Claimed(Warrior(60), kChest, kChest);
        noAuthority.authorityValid = false;
        ok = Held("complete mask without authority", Run(noAuthority, {})) && ok;
        CollectorRequest noCap = Claimed(Warrior(60), kChest, kChest);
        noCap.capSupplied = false;
        ok = Held("complete mask without a cap", Run(noCap, {})) && ok;
        CollectorRequest noQuality = Claimed(Warrior(60), kChest, kChest);
        noQuality.qualityLimitKnown = false;
        ok = Held("complete mask without a quality limit", Run(noQuality, {})) && ok;
        BotView unknownLevel = Warrior(60);
        unknownLevel.levelKnown = false;
        ok = Held("complete mask with an unknown bot level", Run(Claimed(unknownLevel, kFinger, kFinger), {})) && ok;
    }

    // --- G2.3 low-level rings and trinkets (TBC Jewelcrafting ~10, Engineering) ---
    {
        std::vector<CandidateTemplate> const jcRings{Item(100, 10, 10), Item(101, 11, 10)};
        std::vector<CandidateTemplate> const engTrinkets{Item(300, 12, 25), Item(301, 13, 35)};
        for (std::uint32_t level : {1u, 5u, 10u, 15u, 19u})
        {
            std::string tag = "level " + std::to_string(level);
            ok = Held(tag + " rings, empty list", Run(Claimed(Warrior(level), kFinger, kFinger), {})) && ok;
            ok = Held(tag + " rings, JC rings listed", Run(Claimed(Warrior(level), kFinger, kFinger), jcRings)) && ok;
            CollectorResult const one = Run(Claimed(Warrior(level), kFinger, kFinger), {Item(100, 10, 1)});
            ok = Held(tag + " rings, one ring listed", one) && ok;
            ok = Check(AssessSlotList(one.provenance[11], kRevision, true).verdict != ListVerdict::ProvenEmpty,
                     tag + " the unlisted ring slot is never proven empty") &&
                 ok;
            std::vector<ItemFact> none{};
            ok = HeldPlan(tag + " rings, no facts", SelectFromFacts(Direct(Warrior(level), none, kFinger, kFinger), ScoreOf)) &&
                 ok;
        }
        for (std::uint32_t level : {20u, 30u, 35u, 49u})
        {
            std::string tag = "level " + std::to_string(level);
            ok = Held(tag + " trinkets, empty list", Run(Claimed(Warrior(level), kTrinket, kTrinket), {})) && ok;
            ok = Held(tag + " trinkets, engineering listed",
                     Run(Claimed(Warrior(level), kTrinket, kTrinket), engTrinkets)) &&
                 ok;
            std::vector<ItemFact> none{};
            ok = HeldPlan(tag + " trinkets, no facts",
                     SelectFromFacts(Direct(Warrior(level), none, kTrinket, kTrinket), ScoreOf)) &&
                 ok;
        }
        // Mixed: chest searched, rings below the floor. Rings keep the whole plan held.
        std::vector<CandidateTemplate> mixed{Item(200, 4), Item(100, 10, 10)};
        ok = Held("level 12 chest plus rings", Run(Claimed(Warrior(12), kChest | kFinger, kChest | kFinger), mixed)) && ok;
        // Controls: at the search floor the same items commit.
        CollectorResult const rings20 = Run(Claimed(Warrior(20), kFinger, kFinger), jcRings);
        ok = Check(rings20.action == GearAction::CommitSet && rings20.plan.selected[10].entryId != 0,
                 "control: level 20 rings are searched and commit") &&
             ok;
        CollectorResult const trinkets50 = Run(Claimed(Warrior(50), kTrinket, kTrinket), engTrinkets);
        ok = Check(trinkets50.action == GearAction::CommitSet && trinkets50.plan.selected[12].entryId != 0,
                 "control: level 50 trinkets are searched and commit") &&
             ok;

        // Native cache walk at level 10 with a JC ring present.
        Cache cache;
        cache.rings = {100};
        Proto ring;
        ring.ItemId = 100;
        NativeCollectQuery query;
        query.request = Claimed(Warrior(10), kFinger, kFinger);
        query.level = 10;
        query.classId = 1;
        auto noLimit = [](std::uint32_t) -> LimitEntry const* { return nullptr; };
        auto noSet = [](std::uint32_t) -> SetEntry const* { return nullptr; };
        CollectorResult const native = CollectNativeCandidateFacts<CacheInventory>(
            cache, [&ring](std::uint32_t) { return &ring; }, noLimit, noSet, query, ScoreOf);
        ok = Held("native level 10 JC ring walk", native) && ok;
        cache.rings.clear();
        CollectorResult const nativeEmpty = CollectNativeCandidateFacts<CacheInventory>(
            cache, [&ring](std::uint32_t) { return &ring; }, noLimit, noSet, query, ScoreOf);
        ok = Held("native level 10 empty ring cache", nativeEmpty) && ok;
        query.request.sourceBudget = 0;
        cache.rings = {100};
        CollectorResult const nativeCut = CollectNativeCandidateFacts<CacheInventory>(
            cache, [&ring](std::uint32_t) { return &ring; }, noLimit, noSet, query, ScoreOf);
        ok = Held("native walk over its source budget", nativeCut) && ok;
        ok = Check(nativeCut.truncated, "native walk over budget is truncated") && ok;
    }

    // --- G2.4 category proof missing / zero / unknown and gem counts ---
    {
        CandidateTemplate gem = Item(400, 12, 50);
        gem.limitCategory = 9;
        gem.categoryLimit = 1;
        std::vector<CandidateTemplate> equipLimited{gem};
        BotView bot = Warrior(60);
        ok = Held("EQUIP limit with no retained table", Run(Claimed(bot, 1u << 12, 1u << 12), equipLimited)) && ok;
        ok = Held("EQUIP limit with an unrelated category row",
                 Run(Claimed(bot, 1u << 12, 1u << 12), equipLimited, {Count(0, 8, 0, 0)})) &&
             ok;
        ok = Held("EQUIP limit with unknown gem count",
                 Run(Claimed(bot, 1u << 12, 1u << 12), equipLimited, {Count(0, 9, 0, 0, false)})) &&
             ok;
        ok = Held("EQUIP limit already filled by gems",
                 Run(Claimed(bot, 1u << 12, 1u << 12), equipLimited, {Count(0, 9, 0, 1)})) &&
             ok;
        RetainedCount unknownEquipped = Count(0, 9, 0, 0);
        unknownEquipped.equippedKnown = false;
        ok = Held("EQUIP limit with unknown equipped count",
                 Run(Claimed(bot, 1u << 12, 1u << 12), equipLimited, {unknownEquipped})) &&
             ok;
        CandidateTemplate unknownLimit = gem;
        unknownLimit.limitKnown = false;
        ok = Held("category entry not loaded", Run(Claimed(bot, 1u << 12, 1u << 12), {unknownLimit}, {Count(0, 9, 0, 0)})) &&
             ok;
        CandidateTemplate have = Item(401, 12, 50);
        have.limitCategory = 10;
        have.categoryHaveLimit = 1;
        g_names.push_back(Known(401));
        CollectorRequest haveReq = Claimed(bot, 1u << 12, 1u << 12);
        haveReq.identities = g_names;
        ok = Held("HAVE limit with no retained table", Run(haveReq, {have})) && ok;
        CandidateTemplate maxed = Item(400, 12, 50);
        maxed.maxCount = 1;
        CollectorRequest maxReq = Claimed(bot, 1u << 12, 1u << 12);
        maxReq.identities = g_names;
        ok = Held("max count with no retained entry", Run(maxReq, {maxed})) && ok;
        // ApplyLimitEntry: missing, wrong id, zero max and unknown mode stay unknown.
        LimitEntry const wrong{8, 1, 1};
        LimitEntry const zero{9, 0, 1};
        LimitEntry const odd{9, 1, 7};
        for (LimitEntry const* entry : {static_cast<LimitEntry const*>(nullptr), &wrong, &zero, &odd})
        {
            CandidateTemplate row = Item(400, 12, 50);
            row.limitCategory = 9;
            ApplyLimitEntry(row, entry);
            ok = Check(!row.limitKnown && !IdentityComplete(row), "an unproven limit entry leaves the row unknown") && ok;
        }
        // Control: an exact zero proof still commits once.
        CollectorResult const proven =
            Run(Claimed(bot, 1u << 12, 1u << 12), equipLimited, {Count(0, 9, 0, 0)});
        ok = Check(proven.action == GearAction::CommitSet && proven.plan.selected[12].entryId == 400,
                 "control: a proven zero EQUIP seed commits") &&
             ok;
    }

    // --- G2.5 conflicting property / suffix variants ---
    {
        CandidateTemplate grouped = Item(200, 4);
        grouped.propertyGroup = 5;
        ok = Held("random-property group with no pool", Run(Claimed(Warrior(60), kChest, kChest), {grouped})) && ok;
        CandidateTemplate suffix = Item(200, 4);
        suffix.suffixGroup = 6;
        ok = Held("random-suffix group with no pool", Run(Claimed(Warrior(60), kChest, kChest), {suffix})) && ok;
        CandidateTemplate both = Item(200, 4);
        both.propertyGroup = 5;
        both.suffixGroup = 6;
        ok = Held("both property and suffix groups", Run(Claimed(Warrior(60), kChest, kChest), {both})) && ok;
        CandidateTemplate a = Item(200, 4);
        a.groupsKnown = false;
        a.randomPropertyKnown = true;
        a.randomProperty = 3;
        CandidateTemplate b = a;
        b.randomProperty = -3;
        b.itemLevel = 12;
        CollectorResult const split = Run(Claimed(Warrior(60), kChest, kChest), {a, b});
        ok = Check(!split.nativeSourceUsed && !split.claimedEmpty && split.action != GearAction::StripToBags,
                 "a split property/suffix pair never uses native proof or strips") &&
             ok;
        CandidateTemplate unknownProperty = Item(200, 4);
        unknownProperty.groupsKnown = false;
        unknownProperty.randomPropertyKnown = false;
        ok = Held("unknown rolled property", Run(Claimed(Warrior(60), kChest, kChest), {unknownProperty})) && ok;
    }

    // --- G2.6 Darkmoon 19287-19290 with forged flags ---
    for (std::uint32_t card = 19287; card <= 19290; ++card)
    {
        std::string tag = "Darkmoon " + std::to_string(card);
        std::vector<VerifiedItemIdentity> forgedNames = g_names;
        VerifiedItemIdentity lie = Known(card);
        lie.darkmoonCard = false;
        lie.availabilityKnown = true;
        lie.procKnown = true;
        forgedNames.push_back(lie);
        CandidateTemplate row = Item(card, 12, 1);
        row.darkmoonCard = false;
        row.darkmoonIdentityKnown = true;
        row.darkmoonAvailabilityKnown = true;
        row.darkmoonProcKnown = true;
        CollectorRequest request = Claimed(Warrior(60), 1u << 12, 1u << 12);
        request.identities = forgedNames;
        CollectorResult const result = Run(request, {row});
        ok = Held(tag + " forged identity", result) && ok;
        ok = Check(result.facts.empty(), tag + " never reaches the solver") && ok;

        ItemFact fact;
        fact.id = card;
        fact.itemLevel = 10;
        fact.requiredLevel = 1;
        fact.slot = 12;
        fact.sourceKnown = true;
        fact.metadataKnown = true;
        fact.equipKnown = true;
        fact.equipAllowed = true;
        fact.scoreKnown = true;
        fact.setEvidenceKnown = true;
        fact.darkmoonCard = false;
        fact.darkmoonAvailabilityKnown = true;
        fact.darkmoonProcKnown = true;
        fact.requestForceBis = true;
        std::vector<ItemFact> facts{fact};
        ok = Check(Classify(fact, Warrior(60), 200) == FactDecision::Unknown, tag + " classifies Unknown") && ok;
        ok = HeldPlan(tag + " forged direct fact", SelectFromFacts(Direct(Warrior(60), facts, 1u << 12, 1u << 12), ScoreOf)) &&
             ok;
    }

    // --- G4 same unknown input: collector and direct SelectFromFacts agree ---
    {
        BotView bot = Warrior(60);
        // (a) required chest, no rows, completeness not justified.
        CollectorRequest collector = Claimed(bot, kChest, kChest);
        collector.exhaustiveLegalDomain = false;
        CollectorResult const viaCollector = Run(collector, {});
        PlanInput direct = Direct(bot, {}, kChest, kChest);
        direct.completeMask = 0;
        Plan const viaDirect = SelectFromFacts(direct, ScoreOf);
        ok = Check(viaCollector.action == GearAction::Preserve && viaDirect.outcome.action == GearAction::Preserve &&
                       !viaCollector.plan.sourceComplete && !viaDirect.sourceComplete,
                 "unjustified empty: both paths preserve") &&
             ok;
        // (b) one unknown-identity row.
        std::vector<CandidateTemplate> unknownRow{Item(999, 4)};
        CollectorResult const c2 = Run(Claimed(bot, kChest, kChest), unknownRow);
        ItemFact unknownFact = FactFromTemplate(unknownRow[0]);
        unknownFact.metadataKnown = false;
        std::vector<ItemFact> f2{unknownFact};
        Plan const d2 = SelectFromFacts(Direct(bot, f2, kChest, kChest), ScoreOf);
        ok = Check(c2.action == GearAction::Preserve && d2.outcome.action == GearAction::Preserve,
                 "unknown row: both paths preserve") &&
             ok;
        // (c) low-level ring floor: both paths Preserve.
        CollectorResult const c3 = Run(Claimed(Warrior(10), kFinger, kFinger), {});
        Plan const d3 = SelectFromFacts(Direct(Warrior(10), {}, kFinger, kFinger), ScoreOf);
        ok = Check(c3.action == GearAction::Preserve && d3.outcome.action == GearAction::Preserve,
                 "ring floor: both paths preserve") &&
             ok;
        // (d) EQUIP seed counted exactly once: one gem in a limit-2 category
        // leaves room for one more, not zero (double count) and not two.
        CandidateTemplate gemA = Item(400, 12, 50);
        gemA.limitCategory = 9;
        gemA.categoryLimit = 2;
        CollectorResult const once = Run(Claimed(bot, 1u << 12, 1u << 12), {gemA}, {Count(0, 9, 0, 1)});
        ok = Check(once.action == GearAction::CommitSet && once.plan.selected[12].entryId == 400,
                 "one gem in a limit-2 category leaves exactly one slot") &&
             ok;
        CollectorResult const full = Run(Claimed(bot, 1u << 12, 1u << 12), {gemA}, {Count(0, 9, 0, 2)});
        ok = Held("two gems fill a limit-2 category", full) && ok;
        // (e) RNDBOT original retirement is counted once: approved original
        // removed from the projection exactly once and only for a verified RNDBOT.
        CollectorRequest rnd = Claimed(bot, kChest, kChest);
        rnd.population = CountPopulation::RandomBot;
        ApprovedOriginal original;
        original.entryId = 200;
        original.slot = 4;
        original.approved = true;
        original.propertyKnown = true;
        original.guidKnown = true;
        original.guid = 77;
        std::vector<ApprovedOriginal> originals{original};
        rnd.approvedOriginals = originals;
        CollectorResult const projected = Run(rnd, chest, {Count(200, 0, 1, 0)});
        ok = Check(projected.projectionKnown && projected.projectedCounts.size() == 1 &&
                       projected.projectedCounts[0].equipped == 0 && projected.physicalCounts[0].equipped == 1,
                 "a verified RNDBOT original is subtracted exactly once") &&
             ok;
        std::vector<ApprovedOriginal> twiceSame{original, original};
        twiceSame[1].guid = 78;
        rnd.approvedOriginals = twiceSame;
        CollectorResult const doubled = Run(rnd, chest, {Count(200, 0, 2, 0)});
        ok = Held("two approvals for one slot", doubled) && ok;
        for (CountPopulation population : {CountPopulation::AltBot, CountPopulation::Unknown})
        {
            CollectorRequest other = rnd;
            other.population = population;
            other.approvedOriginals = originals;
            CollectorResult const kept = Run(other, chest, {Count(200, 0, 1, 0)});
            ok = Check(kept.projectedCounts.size() == 1 && kept.projectedCounts[0].equipped == 1,
                     "a non-RNDBOT keeps its physical count") &&
                 ok;
        }
        CollectorRequest unverified = rnd;
        unverified.populationVerified = false;
        unverified.approvedOriginals = originals;
        CollectorResult const unv = Run(unverified, chest, {Count(200, 0, 1, 0)});
        ok = Check(unv.projectedCounts.size() == 1 && unv.projectedCounts[0].equipped == 1,
                 "an unverified RNDBOT claim keeps its physical count") &&
             ok;
    }

    std::cout << "checks=" << g_checks << "\n";
    return ok ? 0 : 1;
}
