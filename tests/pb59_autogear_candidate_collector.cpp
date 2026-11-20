#include "AutoGearNativeCandidateCollector.h"
#include <iostream>
#include <vector>

using namespace PlayerbotsAutoGearSelection;
using PlayerbotsAutoGearWeapons::Kind;

namespace
{
int g_checks = 0;

bool Check(bool condition, char const* text)
{
    ++g_checks;
    if (!condition)
    {
        std::cerr << "FAIL " << text << "\n";
        return false;
    }
    return true;
}

std::uint32_t ScoreOf(std::uint32_t id, std::int32_t property, std::uint32_t)
{
    if (id == 66 && property == 5)
        return 40;
    if (id == 66 && property == -3)
        return 10;
    if (id == 67)
        return 30;
    if (id == 910001)
        return 1;
    return 5;
}

BotView Mage()
{
    BotView bot;
    bot.levelKnown = true;
    bot.level = 60;
    bot.classKnown = true;
    bot.classId = 8;
    bot.specKnown = true;
    bot.specTab = 1;
    bot.roleKnown = true;
    bot.role = Role::Caster;
    bot.armorKnown = true;
    bot.armorMask = std::uint32_t{1} << 1;
    bot.weaponsKnown = true;
    return bot;
}

CandidateTemplate Legal(std::uint32_t id, std::uint32_t itemLevel)
{
    CandidateTemplate row;
    row.loaded = true;
    row.metadataKnown = true;
    row.darkmoonIdentityKnown = true;
    row.entry = id;
    row.itemLevel = itemLevel;
    row.requiredLevel = 1;
    row.qualityKnown = true;
    row.quality = 3;
    row.slotKnown = true;
    row.slot = 4;
    row.randomPropertyKnown = true;
    row.equipKnown = true;
    row.equipAllowed = true;
    row.scoreKnown = true;
    row.limitKnown = true;
    row.setEvidenceKnown = true;
    return row;
}

VerifiedItemIdentity Known(std::uint32_t entry)
{
    VerifiedItemIdentity identity;
    identity.entry = entry;
    identity.verified = true;
    return identity;
}

std::vector<VerifiedItemIdentity> g_names{};

CollectorRequest Ready(std::uint32_t cap)
{
    CollectorRequest request;
    request.capSupplied = true;
    request.resolvedCap.preserve = false;
    request.resolvedCap.cap = cap;
    request.resolvedMode = CapMode::Endgame;
    request.qualityLimitKnown = true;
    request.qualityLimit = 4;
    request.bot = Mage();
    request.identities = g_names;
    request.sourceExhausted = true;
    request.exhaustiveLegalDomain = true;
    request.sourceRevision = 1;
    request.eligibleMask = 1u << 4;
    request.requiredMask = 1u << 4;
    request.completeMask = 1u << 4;
    request.bags = BagSpace::Enough;
    request.authorityValid = true;
    return request;
}

struct Proto
{
    std::uint32_t ItemId = 66;
    std::uint32_t Class = 4;
    std::uint32_t SubClass = 1;
    std::uint32_t Quality = 3;
    std::uint32_t Flags = 0;
    std::uint32_t InventoryType = 5;
    std::uint32_t AllowableClass = 0xFFFFFFFFu;
    std::uint32_t ItemLevel = 60;
    std::uint32_t RequiredLevel = 1;
    std::int32_t MaxCount = 0;
    std::int32_t RandomProperty = 0;
    std::int32_t RandomSuffix = 0;
    std::uint32_t ItemSet = 0;
    std::uint32_t ItemLimitCategory = 0;
};

enum class CacheInventory : std::uint32_t
{
    Chest = 5,
    Robe = 20
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

struct Source
{
    std::vector<std::uint32_t> chest{};
    std::vector<std::uint32_t> newer{};
    std::vector<std::uint32_t> empty{};

    std::vector<std::uint32_t> const& GetEquipment(std::uint32_t, std::uint8_t, std::uint8_t slot,
        std::uint32_t quality) const
    {
        if (slot == 4 && quality == 4)
            return chest;
        return empty;
    }

    std::vector<std::uint32_t> const& GetEquipmentNew(std::uint32_t, CacheInventory type) const
    {
        if (type == CacheInventory::Chest)
            return newer;
        return empty;
    }
};
}

int main()
{
    bool ok = true;
    g_names = {Known(66), Known(67), Known(70), Known(910001)};
    std::uint32_t const caps[] = {66, 78, 78, 83, 83, 83, 88, 92};
    for (std::uint8_t stage = 0; stage <= 7; ++stage)
    {
        CollectorRequest request = Ready(caps[stage]);
        request.exhaustiveLegalDomain = false;
        request.completeMask = 0;
        CollectorResult const result = CollectCandidateFacts(request, ScoreOf);
        ok = Check(result.cap == caps[stage] && !result.claimedEmpty && !result.deletesGear,
                "a supplied stage cap is kept and an unjustified list is not empty") &&
             ok;
    }

    std::vector<CandidateTemplate> pair{Legal(66, 60), Legal(70, 70)};
    CollectorRequest capped = Ready(66);
    capped.rows = pair;
    CollectorResult const kept = CollectCandidateFacts(capped, ScoreOf);
    ok = Check(kept.cap == 66 && kept.action == GearAction::CommitSet && kept.plan.selected[4].entryId == 66 &&
               !kept.rows[1].withinCap && !kept.deletesGear,
            "an item above the stage cap is filtered and the legal row is kept") &&
         ok;
    CollectorRequest matched = Ready(40);
    matched.resolvedMode = CapMode::Match;
    matched.rows = pair;
    CollectorResult const matchCap = CollectCandidateFacts(matched, ScoreOf);
    ok = Check(matchCap.cap == 40 && matchCap.mode == CapMode::Match && matchCap.claimedEmpty,
            "match uses the supplied cap and does not recompute the stage") &&
         ok;
    CollectorRequest leveling = Ready(40);
    leveling.resolvedMode = CapMode::Leveling;
    leveling.rows = pair;
    ok = Check(CollectCandidateFacts(leveling, ScoreOf).cap == 40, "leveling uses the supplied cap") && ok;
    CollectorRequest numeric = Ready(50);
    numeric.rows = pair;
    CollectorResult const lowered = CollectCandidateFacts(numeric, ScoreOf);
    ok = Check(lowered.cap == 50 && lowered.claimedEmpty && lowered.action == GearAction::StripToBags &&
               !lowered.deletesGear,
            "a supplied numeric cap can justify an empty domain") &&
         ok;
    numeric.exhaustiveLegalDomain = false;
    CollectorResult const unjustified = CollectCandidateFacts(numeric, ScoreOf);
    ok = Check(unjustified.action == GearAction::Preserve && !unjustified.claimedEmpty,
            "the same filtered rows do not justify an empty domain") &&
         ok;

    std::vector<CandidateTemplate> qualities{Legal(66, 60)};
    qualities[0].quality = 5;
    CollectorRequest quality = Ready(66);
    quality.rows = qualities;
    CollectorResult const tooFine = CollectCandidateFacts(quality, ScoreOf);
    ok = Check(!tooFine.rows[0].withinQuality && tooFine.claimedEmpty, "quality above the limit is not a candidate") &&
         ok;
    qualities[0].quality = 2;
    quality.rows = qualities;
    ok = Check(CollectCandidateFacts(quality, ScoreOf).action == GearAction::CommitSet,
            "quality inside the limit stays eligible") &&
         ok;

    std::vector<CandidateTemplate> many{Legal(66, 60), Legal(67, 60)};
    CollectorRequest bound = Ready(66);
    bound.rows = many;
    bound.rowBudget = 1;
    CollectorResult const cut = CollectCandidateFacts(bound, ScoreOf);
    ok = Check(cut.truncated && cut.action == GearAction::Preserve && !cut.claimedEmpty && cut.rows.size() == 1,
            "a bounded scan preserves and does not call the unread part empty") &&
         ok;
    bound.rowBudget = 64;
    bound.sourceExhausted = false;
    ok = Check(CollectCandidateFacts(bound, ScoreOf).truncated &&
               !CollectCandidateFacts(bound, ScoreOf).claimedEmpty,
            "a source that did not finish is not an empty domain") &&
         ok;

    std::vector<CandidateTemplate> variants{Legal(66, 60), Legal(66, 60)};
    variants[0].randomProperty = 5;
    variants[1].randomProperty = -3;
    CollectorRequest variant = Ready(66);
    variant.rows = variants;
    CollectorResult const chosen = CollectCandidateFacts(variant, ScoreOf);
    ok = Check(chosen.action == GearAction::CommitSet && chosen.plan.selected[4].entryId == 66 &&
               chosen.plan.selected[4].randomProperty == 5 && chosen.rows.size() == 2,
            "the same entry keeps both properties and the higher score wins") &&
         ok;

    std::vector<CandidateTemplate> retainedRows{Legal(66, 60), Legal(67, 60)};
    retainedRows[0].maxCount = 1;
    retainedRows[0].uniqueEquipped = true;
    RetainedCount held;
    held.entryId = 66;
    held.bagsKnown = true;
    held.bankKnown = true;
    held.equippedKnown = true;
    held.gemsKnown = true;
    held.equipped = 1;
    std::vector<RetainedCount> retained{held};
    CollectorRequest owned = Ready(66);
    owned.rows = retainedRows;
    owned.retained = retained;
    CollectorResult const skipped = CollectCandidateFacts(owned, ScoreOf);
    ok = Check(skipped.action == GearAction::CommitSet && skipped.plan.selected[4].entryId == 67 &&
               skipped.rows[0].retainedKnown && skipped.rows[0].retainedOwned == 1,
            "a retained unique copy is not selected again") &&
         ok;

    std::vector<CandidateTemplate> missing{Legal(66, 60)};
    missing[0].loaded = false;
    CollectorRequest unknown = Ready(66);
    unknown.rows = missing;
    CollectorResult const lost = CollectCandidateFacts(unknown, ScoreOf);
    ok = Check(lost.action == GearAction::Preserve && lost.preserve && !lost.claimedEmpty,
            "a missing template preserves") &&
         ok;
    missing[0] = Legal(66, 60);
    missing[0].equipKnown = false;
    unknown.rows = missing;
    ok = Check(CollectCandidateFacts(unknown, ScoreOf).action == GearAction::Preserve,
            "an unchecked equip result preserves") &&
         ok;

    std::vector<CandidateTemplate> cards{Legal(910001, 60), Legal(66, 60)};
    std::vector<VerifiedItemIdentity> cardNames{Known(66), Known(910001)};
    cardNames[1].darkmoonCard = true;
    CollectorRequest darkmoon = Ready(66);
    darkmoon.identities = cardNames;
    darkmoon.rows = cards;
    CollectorResult const blocked = CollectCandidateFacts(darkmoon, ScoreOf);
    ok = Check(blocked.action == GearAction::Preserve && !blocked.claimedEmpty,
            "unknown Darkmoon availability preserves") &&
         ok;
    cardNames[1].availabilityKnown = true;
    cardNames[1].procKnown = true;
    darkmoon.identities = cardNames;
    darkmoon.rows = cards;
    CollectorResult const scored = CollectCandidateFacts(darkmoon, ScoreOf);
    bool forced = false;
    for (ItemFact const& fact : scored.facts)
    {
        if (fact.requestForceBis)
            forced = true;
    }
    ok = Check(scored.action == GearAction::CommitSet && scored.plan.selected[4].entryId == 66 && !forced,
            "a known Darkmoon card is scored and is not forced") &&
         ok;

    Proto proto;
    proto.Flags = 0x00080000u;
    proto.RandomProperty = 12;
    CandidateTemplate const read = ReadItemTemplate(&proto, 4);
    ok = Check(read.entry == 66 && read.itemLevel == 60 && read.uniqueEquipped && !read.randomPropertyKnown &&
               !read.equipKnown && read.armorSubtype == 1,
            "a template row copies the entry and does not invent a property or an equip result") &&
         ok;
    CandidateTemplate const missingProto = ReadItemTemplate(static_cast<Proto const*>(nullptr), 4);
    ok = Check(!missingProto.loaded, "a null template is not a source row") && ok;

    std::vector<CandidateTemplate> classic{Legal(19287, 66)};
    std::vector<VerifiedItemIdentity> forcedCard{Known(19287)};
    forcedCard[0].darkmoonCard = false;
    forcedCard[0].availabilityKnown = true;
    forcedCard[0].procKnown = true;
    CollectorRequest classicRequest = Ready(66);
    classicRequest.rows = classic;
    classicRequest.identities = forcedCard;
    CollectorResult const classicCard = CollectCandidateFacts(classicRequest, ScoreOf);
    ok = Check(classicCard.action == GearAction::Preserve && !classicCard.claimedEmpty && classicCard.facts.empty(),
            "cards 19287-19290 stay unknown and are not forced") &&
         ok;

    CandidateTemplate limited = Legal(66, 60);
    limited.limitCategory = 2;
    LimitEntry limit;
    limit.ID = 2;
    limit.maxCount = 1;
    limit.mode = 1;
    ApplyLimitEntry(limited, &limit);
    SetEntry setBonus;
    setBonus.spells[0] = 9;
    setBonus.items_to_triggerspell[0] = 2;
    limited.setId = 4;
    ApplySetEntry(limited, &setBonus);
    ok = Check(limited.limitKnown && limited.categoryLimit == 1 && limited.categoryHaveLimit == 0 &&
               limited.setEvidenceKnown && limited.setBonusPresent,
            "limit category mode and set thresholds are read from the metadata") &&
         ok;

    RetainedCount physical;
    physical.entryId = 66;
    physical.bagsKnown = true;
    physical.bankKnown = true;
    physical.equippedKnown = true;
    physical.gemsKnown = true;
    physical.equipped = 1;
    physical.bags = 1;
    ApprovedOriginal original;
    original.entryId = 66;
    original.randomProperty = 0;
    original.propertyKnown = true;
    original.slot = 4;
    original.approved = true;
    original.guid = 10;
    original.guidKnown = true;
    StagingPiece staged;
    staged.entryId = 66;
    staged.count = 1;
    std::vector<RetainedCount> physicalRows{physical};
    std::vector<ApprovedOriginal> originals{original};
    std::vector<StagingPiece> stagedRows{staged};
    std::vector<CandidateTemplate> projectedRows{Legal(66, 60), Legal(67, 60)};
    projectedRows[0].maxCount = 1;
    CollectorRequest randomBot = Ready(66);
    randomBot.rows = projectedRows;
    randomBot.retained = physicalRows;
    randomBot.approvedOriginals = originals;
    randomBot.staging = stagedRows;
    randomBot.population = CountPopulation::RandomBot;
    randomBot.populationVerified = true;
    CollectorResult const projected = CollectCandidateFacts(randomBot, ScoreOf);
    ok = Check(projected.projectionKnown && projected.projectedCounts[0].equipped == 0 &&
               projected.projectedCounts[0].bags == 1 && projected.physicalCounts[0].equipped == 1 &&
               projected.plan.selected[4].entryId == 67 && !projected.deletesGear,
            "a random bot drops only the approved original and keeps staging") &&
         ok;
    randomBot.population = CountPopulation::AltBot;
    CollectorResult const altBot = CollectCandidateFacts(randomBot, ScoreOf);
    ok = Check(altBot.projectedCounts[0].equipped == 1 && altBot.projectedCounts[0].bags == 1,
            "an alt bot keeps the physical count") &&
         ok;
    randomBot.population = CountPopulation::RandomBot;
    originals[0].approved = false;
    CollectorResult const unapproved = CollectCandidateFacts(randomBot, ScoreOf);
    ok = Check(unapproved.projectedCounts[0].equipped == 1, "an unapproved original is not subtracted") && ok;
    originals[0].approved = true;
    originals[0].propertyKnown = false;
    CollectorResult const inexact = CollectCandidateFacts(randomBot, ScoreOf);
    ok = Check(!inexact.projectionKnown || inexact.projectedCounts[0].equipped == 1,
            "an original without a known property is not subtracted") &&
         ok;

    CollectorRequest bis = Ready(78);
    bis.bis = true;
    bis.qualityLimit = 4;
    bis.resolvedMode = CapMode::Endgame;
    std::vector<CandidateTemplate> bisRows{Legal(66, 60), Legal(67, 90)};
    bisRows[0].quality = 4;
    bisRows[1].quality = 5;
    bis.rows = bisRows;
    CollectorResult const bisPlan = CollectCandidateFacts(bis, ScoreOf);
    ok = Check(bisPlan.bis && bisPlan.cap == 78 && bisPlan.plan.selected[4].entryId == 66 && !bisPlan.deletesGear,
            "bis keeps the supplied cap and the supplied quality") &&
         ok;

    Source source;
    source.chest = {66, 67};
    auto noLimit = [](std::uint32_t) -> LimitEntry const* { return nullptr; };
    auto noSet = [](std::uint32_t) -> SetEntry const* { return nullptr; };
    NativeCollectQuery native;
    native.request = Ready(66);
    native.request.qualityLimit = 4;
    native.request.exhaustiveLegalDomain = true;
    native.level = 0;
    native.classId = 8;
    native.request.rowBudget = 1;
    CollectorResult const nativeCut = CollectNativeCandidateFacts<CacheInventory>(
        source, [&proto](std::uint32_t) { return &proto; }, noLimit, noSet, native, ScoreOf);
    ok = Check(nativeCut.truncated && nativeCut.action == GearAction::Preserve && !nativeCut.claimedEmpty &&
               !nativeCut.nativeEquipProven,
            "the native walk stops at the deduped budget and does not claim an empty domain") &&
         ok;
    source.chest = {66};
    source.newer = {66};
    native.request.rowBudget = 1;
    native.level = 0;
    Proto plain = proto;
    plain.RandomProperty = 0;
    CollectorResult const deduped = CollectNativeCandidateFacts<CacheInventory>(
        source, [&plain](std::uint32_t) { return &plain; }, noLimit, noSet, native, ScoreOf);
    ok = Check(!deduped.truncated && deduped.rows.size() == 1 && deduped.action == GearAction::Preserve &&
               !deduped.claimedEmpty && !deduped.nativeEquipProven,
            "the same entry and slot from both caches counts once and stays unproven") &&
         ok;
    Proto other = plain;
    other.ItemLevel = 50;
    int lookups = 0;
    CollectorResult const clash = CollectNativeCandidateFacts<CacheInventory>(
        source,
        [&](std::uint32_t)
        {
            ++lookups;
            return lookups == 1 ? &plain : &other;
        },
        noLimit, noSet, native, ScoreOf);
    ok = Check(clash.conflict && clash.action == GearAction::Preserve && !clash.claimedEmpty,
            "conflicting cache rows are rejected") &&
         ok;
    source.chest.clear();
    source.newer.clear();
    native.request.rowBudget = 64;
    CollectorResult const emptyCache = CollectNativeCandidateFacts<CacheInventory>(
        source, [&plain](std::uint32_t) { return &plain; }, noLimit, noSet, native, ScoreOf);
    ok = Check(emptyCache.action == GearAction::Preserve && !emptyCache.claimedEmpty,
            "a finished equipment cache is not a proven empty domain") &&
         ok;

    if (!ok)
        return 1;
    std::cout << "checks=" << g_checks << "\n";
    return 0;
}
