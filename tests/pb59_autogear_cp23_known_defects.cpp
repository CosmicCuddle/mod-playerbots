#include <iostream>
#include <string>
#include <vector>

#include "AutoGearCandidateCollector.h"
#include "AutoGearSlotEnumeration.h"

// PB59_CP23_KNOWN_DEFECT is 0. The desired Preserve and Incomplete results are
// the contract. Pass -DPB59_CP23_KNOWN_DEFECT=1 only to replay the old certificates.
#ifndef PB59_CP23_KNOWN_DEFECT
#define PB59_CP23_KNOWN_DEFECT 0
#endif

using namespace PlayerbotsAutoGearSelection;

namespace
{
constexpr bool KnownDefect = PB59_CP23_KNOWN_DEFECT != 0;
constexpr std::uint32_t kFinger = (1u << 10) | (1u << 11);
constexpr std::uint32_t kTrinket = (1u << 12) | (1u << 13);
constexpr std::uint32_t kChest = 1u << 4;
constexpr std::uint64_t kRevision = 7;

int g_checks = 0;
std::vector<VerifiedItemIdentity> g_names{};

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

std::string Label(char const* name, char const* part)
{
    std::string text = KnownDefect ? "known defect " : "desired ";
    text += name;
    text += " ";
    text += part;
    return text;
}

std::uint32_t ScoreOf(std::uint32_t, std::int32_t, std::uint32_t) { return 10; }

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
    bot.specTab = 0;
    bot.roleKnown = true;
    bot.role = Role::Melee;
    bot.armorKnown = true;
    bot.armorMask = 1u << 4;
    bot.weaponsKnown = true;
    return bot;
}

CandidateTemplate Item(std::uint32_t id, std::uint8_t slot)
{
    CandidateTemplate row;
    row.loaded = true;
    row.metadataKnown = true;
    row.entry = id;
    row.itemLevel = 10;
    row.requiredLevel = 1;
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

CandidateTemplate EquipTrinket(std::uint32_t id, std::uint8_t slot)
{
    CandidateTemplate row = Item(id, slot);
    row.limitCategory = 9;
    row.limitKnown = true;
    row.categoryLimit = 1;
    return row;
}

RetainedCount Count(std::uint32_t entry, std::uint32_t category, std::uint32_t equipped, std::uint32_t gems,
                    bool gemsKnown)
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

CollectorRequest Domain(BotView const& bot, std::uint32_t eligible, std::uint32_t required)
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

CollectorResult Collect(CollectorRequest request, std::vector<CandidateTemplate> const& rows,
                        std::vector<RetainedCount> const& retained, std::vector<ApprovedOriginal> const& originals)
{
    request.rows = rows;
    request.retained = retained;
    request.approvedOriginals = originals;
    return CollectCandidateFacts(request, ScoreOf);
}

ListVerdict Verdict(CollectorResult const& result, unsigned slot)
{
    return AssessSlotList(result.provenance[slot], kRevision, true).verdict;
}

bool ExpectPolicyFloor(char const* name, CollectorResult const& result, unsigned left, unsigned right)
{
    if (KnownDefect)
    {
        bool const stripped = result.action == GearAction::StripToBags && result.claimedEmpty &&
                              result.plan.sourceComplete && result.facts.empty() && !result.deletesGear;
        bool ok = Check(stripped, Label(name, "strips an empty domain").c_str());
        ok = Check(Verdict(result, left) == ListVerdict::ProvenEmpty, Label(name, "left proven empty").c_str()) && ok;
        ok = Check(Verdict(result, right) == ListVerdict::ProvenEmpty, Label(name, "right proven empty").c_str()) && ok;
        return ok;
    }
    bool const held = result.action == GearAction::Preserve && !result.claimedEmpty && !result.plan.sourceComplete &&
                      !result.deletesGear;
    bool ok = Check(held, Label(name, "preserves").c_str());
    ok = Check(Verdict(result, left) == ListVerdict::Incomplete, Label(name, "left incomplete").c_str()) && ok;
    ok = Check(Verdict(result, right) == ListVerdict::Incomplete, Label(name, "right incomplete").c_str()) && ok;
    return ok;
}

bool ExpectMixed(char const* name, CollectorResult const& result)
{
    if (KnownDefect)
    {
        bool const committed = result.action == GearAction::CommitSet && !result.claimedEmpty &&
                               result.plan.sourceComplete && !result.deletesGear &&
                               result.plan.selected[4].entryId == 200;
        bool ok = Check(committed, Label(name, "commits the chest").c_str());
        ok = Check(Verdict(result, 10) == ListVerdict::ProvenEmpty, Label(name, "finger 1 proven empty").c_str()) && ok;
        ok = Check(Verdict(result, 11) == ListVerdict::ProvenEmpty, Label(name, "finger 2 proven empty").c_str()) && ok;
        return ok;
    }
    bool const held = result.action == GearAction::Preserve && !result.claimedEmpty && !result.plan.sourceComplete &&
                      !result.deletesGear;
    bool ok = Check(held, Label(name, "preserves").c_str());
    ok = Check(Verdict(result, 10) == ListVerdict::Incomplete, Label(name, "finger 1 incomplete").c_str()) && ok;
    ok = Check(Verdict(result, 11) == ListVerdict::Incomplete, Label(name, "finger 2 incomplete").c_str()) && ok;
    return ok;
}

bool ExpectCommit(char const* name, CollectorResult const& result, unsigned left, std::uint32_t leftId, unsigned right,
                  std::uint32_t rightId)
{
    bool const committed = result.action == GearAction::CommitSet && !result.claimedEmpty &&
                           result.plan.sourceComplete && !result.deletesGear &&
                           result.plan.selected[left].entryId == leftId &&
                           result.plan.selected[right].entryId == rightId;
    bool ok = Check(committed, Label(name, "commits both").c_str());
    ok = Check(Verdict(result, left) == ListVerdict::ProvenCandidates, Label(name, "left present").c_str()) && ok;
    ok = Check(Verdict(result, right) == ListVerdict::ProvenCandidates, Label(name, "right present").c_str()) && ok;
    return ok;
}

bool ExpectEquipBlock(char const* name, CollectorResult const& result)
{
    if (KnownDefect)
    {
        bool const committed = result.action == GearAction::CommitSet && result.plan.sourceComplete &&
                               !result.claimedEmpty && !result.deletesGear && result.plan.selected[12].entryId == 500;
        return Check(committed, Label(name, "commits over the category").c_str());
    }
    bool const held = result.action == GearAction::Preserve && !result.plan.sourceComplete && !result.claimedEmpty &&
                      !result.deletesGear && result.plan.selected[12].entryId == 0;
    return Check(held, Label(name, "preserves").c_str());
}
}  // namespace

int main()
{
    g_names = {Known(100), Known(101), Known(200), Known(300), Known(301), Known(500), Known(501)};
    bool ok = true;
    std::vector<CandidateTemplate> const rings{Item(100, 10), Item(101, 11)};
    std::vector<CandidateTemplate> const trinkets{Item(300, 12), Item(301, 13)};
    std::vector<CandidateTemplate> const chestAndRings{Item(100, 10), Item(101, 11), Item(200, 4)};
    std::vector<RetainedCount> const none{};
    std::vector<ApprovedOriginal> const noOriginals{};

    ok = ExpectPolicyFloor("rings level 15", Collect(Domain(Warrior(15), kFinger, kFinger), rings, none, noOriginals),
                           10, 11) &&
         ok;
    ok = ExpectPolicyFloor("trinkets level 49",
                           Collect(Domain(Warrior(49), kTrinket, kTrinket), trinkets, none, noOriginals), 12, 13) &&
         ok;
    ok = ExpectMixed("chest plus rings level 15",
                     Collect(Domain(Warrior(15), kChest | kFinger, kChest), chestAndRings, none, noOriginals)) &&
         ok;
    ok = ExpectCommit("rings level 20", Collect(Domain(Warrior(20), kFinger, kFinger), rings, none, noOriginals), 10,
                      100, 11, 101) &&
         ok;
    ok =
        ExpectCommit("trinkets level 50", Collect(Domain(Warrior(50), kTrinket, kTrinket), trinkets, none, noOriginals),
                     12, 300, 13, 301) &&
        ok;

    std::vector<CandidateTemplate> const oneTrinket{EquipTrinket(500, 12)};
    std::vector<RetainedCount> const gems{Count(0, 9, 0, 1, true)};
    std::vector<RetainedCount> const wornOutside{Count(0, 9, 1, 0, true)};
    std::vector<RetainedCount> const unknownGems{Count(0, 9, 0, 0, false)};
    CollectorRequest const trinketSlot = Domain(Warrior(80), 1u << 12, 1u << 12);
    ok = ExpectEquipBlock("socketed gem", Collect(trinketSlot, oneTrinket, gems, noOriginals)) && ok;
    ok = ExpectEquipBlock("equipped copy outside cleared slots",
                          Collect(trinketSlot, oneTrinket, wornOutside, noOriginals)) &&
         ok;
    ok = ExpectEquipBlock("unknown gem data", Collect(trinketSlot, oneTrinket, unknownGems, noOriginals)) && ok;

    CollectorRequest replacement = Domain(Warrior(80), 1u << 12, 1u << 12);
    replacement.population = CountPopulation::RandomBot;
    ApprovedOriginal original;
    original.entryId = 500;
    original.propertyKnown = true;
    original.slot = 12;
    original.approved = true;
    original.limitCategory = 9;
    original.guid = 42;
    original.guidKnown = true;
    std::vector<RetainedCount> const sameSlot{Count(500, 0, 1, 0, true), Count(0, 9, 1, 0, true)};
    std::vector<ApprovedOriginal> const originals{original};
    CollectorResult const replaced = Collect(replacement, oneTrinket, sameSlot, originals);
    bool const sameSlotLegal = replaced.action == GearAction::CommitSet && replaced.plan.sourceComplete &&
                               !replaced.claimedEmpty && !replaced.deletesGear &&
                               replaced.plan.selected[12].entryId == 500;
    ok = Check(sameSlotLegal, "same-slot occupant replacement may commit") && ok;

    std::vector<CandidateTemplate> const both{EquipTrinket(500, 12), EquipTrinket(501, 13)};
    CollectorResult const crowded = Collect(Domain(Warrior(80), kTrinket, kTrinket), both, none, noOriginals);
    bool const oneCategory = crowded.action == GearAction::Preserve && !crowded.claimedEmpty &&
                             !crowded.plan.sourceComplete && crowded.plan.selected[12].entryId == 0 &&
                             crowded.plan.selected[13].entryId == 0;
    ok = Check(oneCategory, "two required trinkets in one equip category stay preserve") && ok;

    std::vector<RetainedCount> const severalGems{Count(0, 9, 0, 3, true)};
    CollectorResult const manyGems = Collect(trinketSlot, oneTrinket, severalGems, noOriginals);
    bool const gemsFill = manyGems.action == GearAction::Preserve && !manyGems.claimedEmpty &&
                          !manyGems.plan.sourceComplete && manyGems.plan.selected[12].entryId == 0;
    ok = Check(gemsFill, "several socketed gems fill an equip limit") && ok;

    std::vector<CandidateTemplate> roomy{EquipTrinket(500, 12)};
    roomy[0].categoryLimit = 3;
    std::vector<RetainedCount> const twoGems{Count(0, 9, 0, 2, true)};
    CollectorResult const gemsLeaveRoom = Collect(trinketSlot, roomy, twoGems, noOriginals);
    bool const roomLeft = gemsLeaveRoom.action == GearAction::CommitSet && !gemsLeaveRoom.claimedEmpty &&
                          gemsLeaveRoom.plan.sourceComplete && gemsLeaveRoom.plan.selected[12].entryId == 500;
    ok = Check(roomLeft, "socketed gems below the equip limit still leave one copy") && ok;

    std::vector<RetainedCount> const gemAndWorn{Count(0, 9, 1, 1, true)};
    CollectorResult const bothCounts = Collect(trinketSlot, oneTrinket, gemAndWorn, noOriginals);
    bool const summed = bothCounts.action == GearAction::Preserve && !bothCounts.claimedEmpty &&
                        !bothCounts.plan.sourceComplete && bothCounts.plan.selected[12].entryId == 0;
    ok = Check(summed, "a gem plus an outside equipped copy fill an equip limit") && ok;

    CandidateTemplate haveTrinket = Item(500, 12);
    haveTrinket.limitCategory = 9;
    haveTrinket.limitKnown = true;
    haveTrinket.categoryHaveLimit = 1;
    std::vector<CandidateTemplate> const haveRows{haveTrinket};
    std::vector<RetainedCount> const haveEmpty{Count(0, 9, 0, 0, true)};
    CollectorResult const haveOpen = Collect(trinketSlot, haveRows, haveEmpty, noOriginals);
    bool const haveCommits = haveOpen.action == GearAction::CommitSet && !haveOpen.claimedEmpty &&
                             haveOpen.plan.sourceComplete && haveOpen.plan.selected[12].entryId == 500;
    ok = Check(haveCommits, "a have-mode category does not use the equip gem seed") && ok;
    std::vector<RetainedCount> haveHeld{Count(0, 9, 0, 0, true)};
    haveHeld[0].bags = 1;
    CollectorResult const haveClosed = Collect(trinketSlot, haveRows, haveHeld, noOriginals);
    bool const haveBlocks = haveClosed.action == GearAction::Preserve && !haveClosed.claimedEmpty &&
                            haveClosed.plan.selected[12].entryId == 0;
    ok = Check(haveBlocks, "a have-mode category still blocks a possessed copy") && ok;

    CollectorRequest optionalFinger = Domain(Warrior(15), kChest | (1u << 10), kChest);
    std::vector<CandidateTemplate> const chestAndOneRing{Item(200, 4), Item(100, 10)};
    CollectorResult const optional = Collect(optionalFinger, chestAndOneRing, none, noOriginals);
    bool const optionalHeld = optional.action == GearAction::Preserve && !optional.claimedEmpty &&
                              optional.action != GearAction::StripToBags && !optional.plan.sourceComplete &&
                              Verdict(optional, 10) == ListVerdict::Incomplete;
    ok = Check(optionalHeld, "an unrequired floor slot stays incomplete") && ok;

    std::vector<CandidateTemplate> const noItems{};
    CollectorResult const noRows = Collect(Domain(Warrior(15), kFinger, kFinger), noItems, none, noOriginals);
    bool const unmarked = noRows.action == GearAction::Preserve && !noRows.claimedEmpty &&
                          noRows.action != GearAction::StripToBags && Verdict(noRows, 10) == ListVerdict::Incomplete &&
                          Verdict(noRows, 11) == ListVerdict::Incomplete;
    ok = Check(unmarked, "an unsearched required slot with no rows is incomplete") && ok;

    std::cout << "PB59_CP23_KNOWN_DEFECT=" << (KnownDefect ? 1 : 0) << "\n";
    std::cout << "checks=" << g_checks << "\n";
    return ok ? 0 : 1;
}
