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

CollectorRequest Ready(std::uint8_t stage)
{
    CollectorRequest request;
    request.playerContext = true;
    request.progressionContext = true;
    request.rawStage = stage;
    request.effectiveStage = stage;
    request.qualityLimitKnown = true;
    request.qualityLimit = 4;
    request.bot = Mage();
    request.sourceExhausted = true;
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

struct Source
{
    std::vector<std::uint32_t> chest{};
    std::vector<std::uint32_t> empty{};

    std::vector<std::uint32_t> const& GetEquipment(std::uint32_t, std::uint8_t, std::uint8_t slot,
        std::uint32_t quality) const
    {
        if (slot == 4 && quality == 4)
            return chest;
        return empty;
    }

    std::vector<std::uint32_t> const& GetEquipmentNew(std::uint32_t, std::uint32_t) const
    {
        return empty;
    }
};
}

int main()
{
    bool ok = true;
    std::uint32_t const caps[] = {66, 78, 78, 83, 83, 83, 88, 92};
    for (std::uint8_t stage = 0; stage <= 7; ++stage)
    {
        CollectorRequest request = Ready(stage);
        request.completeMask = 0;
        CollectorResult const result = CollectCandidateFacts(request, ScoreOf);
        ok = Check(result.cap == caps[stage] && !result.claimedEmpty && !result.deletesGear,
                "a zero score limit uses the stage cap and does not invent an empty set") &&
             ok;
    }

    std::vector<CandidateTemplate> pair{Legal(66, 60), Legal(70, 70)};
    CollectorRequest capped = Ready(0);
    capped.rows = pair;
    CollectorResult const kept = CollectCandidateFacts(capped, ScoreOf);
    ok = Check(kept.cap == 66 && kept.action == GearAction::CommitSet && kept.plan.selected[4].entryId == 66 &&
               !kept.rows[1].withinCap && !kept.deletesGear,
            "an item above the stage cap is filtered and the legal row is kept") &&
         ok;
    capped.scoreLimit = 100;
    ok = Check(CollectCandidateFacts(capped, ScoreOf).cap == 66, "a score limit cannot raise the stage cap") && ok;
    capped.scoreLimit = 50;
    CollectorResult const lowered = CollectCandidateFacts(capped, ScoreOf);
    ok = Check(lowered.cap == 50 && lowered.claimedEmpty && lowered.action == GearAction::StripToBags &&
               !lowered.deletesGear,
            "a lower score limit removes both rows and the finished domain is empty") &&
         ok;

    std::vector<CandidateTemplate> qualities{Legal(66, 60)};
    qualities[0].quality = 5;
    CollectorRequest quality = Ready(0);
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
    CollectorRequest bound = Ready(0);
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
    CollectorRequest variant = Ready(0);
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
    CollectorRequest owned = Ready(0);
    owned.rows = retainedRows;
    owned.retained = retained;
    CollectorResult const skipped = CollectCandidateFacts(owned, ScoreOf);
    ok = Check(skipped.action == GearAction::CommitSet && skipped.plan.selected[4].entryId == 67 &&
               skipped.rows[0].retainedKnown && skipped.rows[0].retainedOwned == 1,
            "a retained unique copy is not selected again") &&
         ok;

    std::vector<CandidateTemplate> missing{Legal(66, 60)};
    missing[0].loaded = false;
    CollectorRequest unknown = Ready(0);
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
    cards[0].darkmoonCard = true;
    CollectorRequest darkmoon = Ready(0);
    darkmoon.rows = cards;
    CollectorResult const blocked = CollectCandidateFacts(darkmoon, ScoreOf);
    ok = Check(blocked.action == GearAction::Preserve && !blocked.claimedEmpty,
            "unknown Darkmoon availability preserves") &&
         ok;
    cards[0].darkmoonAvailabilityKnown = true;
    cards[0].darkmoonProcKnown = true;
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

    Source source;
    source.chest = {66, 67};
    NativeCollectQuery native;
    native.request = Ready(0);
    native.request.qualityLimit = 4;
    native.level = 60;
    native.classId = 8;
    native.cacheComplete = true;
    native.request.rowBudget = 1;
    CollectorResult const nativeCut =
        CollectNativeCandidateFacts(source, [&proto](std::uint32_t) { return &proto; }, native, ScoreOf);
    ok = Check(nativeCut.truncated && nativeCut.action == GearAction::Preserve && !nativeCut.claimedEmpty,
            "the native walk stops at the budget and does not claim an empty domain") &&
         ok;
    native.request.rowBudget = 64;
    native.cacheComplete = false;
    CollectorResult const nativeOpen =
        CollectNativeCandidateFacts(source, [&proto](std::uint32_t) { return &proto; }, native, ScoreOf);
    ok = Check(nativeOpen.action == GearAction::Preserve && !nativeOpen.claimedEmpty && !nativeOpen.rows.empty(),
            "an unfinished equipment cache preserves the rows it could read") &&
         ok;

    if (!ok)
        return 1;
    std::cout << "checks=" << g_checks << "\n";
    return 0;
}
