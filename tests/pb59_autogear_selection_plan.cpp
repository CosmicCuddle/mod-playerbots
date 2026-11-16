#include "AutoGearCandidateDomain.h"
#include "AutoGearConstraintFill.h"
#include "AutoGearSelectionOutcome.h"
#include "AutoGearSelectionPlan.h"
#include "AutoGearScoreFill.h"
#include <iostream>
#include <vector>

using namespace PlayerbotsAutoGearSelection;
using PlayerbotsAutoGearWeapons::Bit;
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

std::uint32_t ScoreOf(std::uint32_t id, std::int32_t prop, std::uint32_t slot)
{
    (void)prop;
    (void)slot;
    switch (id)
    {
        case 65:
            return 10;
        case 66:
            return 20;
        case 67:
            return 90;
        case 77:
            return 10;
        case 78:
            return 20;
        case 79:
            return 90;
        case 40:
            return 10;
        case 50:
            return 90;
        case 701:
            return 100;
        case 702:
            return 10;
        case 801:
            return 100;
        case 802:
            return 5;
        case 810:
            return 100;
        case 811:
            return 5;
        case 820:
            return 100;
        case 821:
            return 5;
        case 830:
            return 100;
        case 831:
            return 5;
        case 840:
            return 100;
        case 841:
            return 5;
        case 30:
            return 50;
        case 31:
            return 40;
        case 32:
            return 50;
        case 33:
            return 40;
        // SYNTHETIC Darkmoon scores. Not the owner's item database.
        case 910001:
            return 80;
        case 910002:
            return 10;
        case 910003:
            return 80;
        default:
            return 1;
    }
}

ItemFact Fact(std::uint32_t id, std::uint32_t itemLevel, std::uint8_t slot)
{
    ItemFact fact;
    fact.id = id;
    fact.itemLevel = itemLevel;
    fact.requiredLevel = 1;
    fact.slot = slot;
    fact.sourceKnown = true;
    fact.metadataKnown = true;
    fact.equipKnown = true;
    fact.equipAllowed = true;
    fact.randomProperty = 4;
    if (slot == 4)
        fact.armorSubtype = 1;
    return fact;
}

BotView Mage(std::uint32_t level)
{
    BotView bot;
    bot.levelKnown = true;
    bot.level = level;
    bot.classKnown = true;
    bot.classId = 8;
    bot.specKnown = true;
    bot.specTab = 1;
    bot.roleKnown = true;
    bot.role = Role::Caster;
    bot.armorKnown = true;
    bot.armorMask = std::uint32_t{1} << 1;
    bot.weaponsKnown = true;
    bot.weapons.mainhand = Bit(Kind::OneHand) | Bit(Kind::TwoHand);
    bot.weapons.offhand = Bit(Kind::OneHand) | Bit(Kind::Shield) | Bit(Kind::Holdable);
    return bot;
}

CapResult Usable(std::uint32_t cap)
{
    return CapResult{false, cap};
}

PlanInput Chest(std::uint32_t cap, std::vector<ItemFact> const& facts, std::uint32_t level)
{
    PlanInput in;
    in.cap = Usable(cap);
    in.bot = Mage(level);
    in.facts = facts;
    in.eligibleMask = 1u << 4;
    in.requiredMask = 1u << 4;
    in.completeMask = 1u << 4;
    in.authorityValid = true;
    in.bags = BagSpace::Enough;
    in.candidateBudget = 1000;
    return in;
}
}

int main()
{
    bool ok = true;
    auto passthrough = [](std::uint32_t id, std::int32_t prop, std::uint32_t slot)
    { return id + static_cast<std::uint32_t>(prop) + slot; };
    ok = Check(ExistingScore(10, 4, 4, passthrough) == 18, "score wrapper is unchanged") && ok;
    ok = Check(ExistingScore(0, 0, 0, passthrough) == 0, "zero score stays zero") && ok;

    std::vector<ItemFact> tier0{Fact(65, 65, 4), Fact(66, 66, 4), Fact(67, 67, 4)};
    Plan const at66 = SelectFromFacts(Chest(66, tier0, 60), ScoreOf);
    ok = Check(at66.outcome.disposition == Disposition::Commit && at66.solved.items[4] == 66,
            "stage boundary 66") &&
         ok;
    ok = Check(at66.solved.items[4] != 67, "67 unavailable at cap 66") && ok;

    std::vector<ItemFact> tier1{Fact(77, 77, 4), Fact(78, 78, 4), Fact(79, 79, 4)};
    Plan const at78 = SelectFromFacts(Chest(78, tier1, 60), ScoreOf);
    ok = Check(at78.outcome.disposition == Disposition::Commit && at78.solved.items[4] == 78,
            "stage boundary 78") &&
         ok;
    ok = Check(at78.solved.items[4] != 79, "79 unavailable at cap 78") && ok;

    std::vector<ItemFact> levelling{Fact(40, 40, 4), Fact(50, 50, 4)};
    Plan const levelCap = SelectFromFacts(Chest(40, levelling, 60), ScoreOf);
    ok = Check(levelCap.outcome.disposition == Disposition::Commit && levelCap.solved.items[4] == 40,
            "levelling target beats a higher item") &&
         ok;

    std::vector<ItemFact> tooHard{Fact(40, 20, 4)};
    tooHard[0].requiredLevel = 61;
    Plan const needLevel = SelectFromFacts(Chest(66, tooHard, 60), ScoreOf);
    ok = Check(needLevel.outcome.disposition == Disposition::GenuineNoSet &&
               needLevel.outcome.action == GearAction::StripToBags,
            "required level above the bot is no set") &&
         ok;

    ItemFact earlyRing = Fact(11, 10, 10);
    std::vector<ItemFact> early{earlyRing};
    PlanInput earlyIn = Chest(66, early, 10);
    earlyIn.facts = early;
    earlyIn.eligibleMask = 1u << 10;
    earlyIn.requiredMask = 1u << 10;
    earlyIn.completeMask = 1u << 10;
    Plan const noRing = SelectFromFacts(earlyIn, ScoreOf);
    ok = Check(noRing.outcome.disposition == Disposition::GenuineNoSet, "ring skipped below 20") && ok;

    std::vector<ItemFact> retained{Fact(701, 90, 4), Fact(702, 60, 4)};
    Plan const keepLegal = SelectFromFacts(Chest(66, retained, 60), ScoreOf);
    ok = Check(keepLegal.outcome.disposition == Disposition::Commit && keepLegal.solved.items[4] == 702,
            "over-cap retained item is not a fallback") &&
         ok;

    ItemFact gemmed = Fact(801, 60, 4);
    gemmed.hasGem = true;
    gemmed.gemItemLevel = 80;
    ItemFact plain = Fact(802, 60, 4);
    std::vector<ItemFact> gems{gemmed, plain};
    Plan const gemCap = SelectFromFacts(Chest(66, gems, 60), ScoreOf);
    ok = Check(gemCap.solved.items[4] == 802, "gem above the cap is unavailable") && ok;
    ok = Check(gemCap.solved.score == ScoreOf(802, 4, 4), "chosen score is the wrapped score") && ok;

    ItemFact badGem = Fact(802, 60, 4);
    badGem.hasGem = true;
    badGem.gemItemLevel = 0;
    std::vector<ItemFact> zeroGem{badGem};
    Plan const gemZero = SelectFromFacts(Chest(66, zeroGem, 60), ScoreOf);
    ok = Check(gemZero.outcome.disposition == Disposition::Preserve, "gem ilvl 0 is invalid, not unlimited") && ok;

    ItemFact enchanted = Fact(810, 60, 4);
    enchanted.hasEnchant = true;
    enchanted.enchantItemLevel = 90;
    std::vector<ItemFact> enchants{enchanted, Fact(811, 60, 4)};
    ok = Check(SelectFromFacts(Chest(66, enchants, 60), ScoreOf).solved.items[4] == 811, "enchant above cap skipped") &&
         ok;

    ItemFact forbiddenSet = Fact(820, 60, 4);
    forbiddenSet.setId = 9;
    forbiddenSet.setPermitted = false;
    std::vector<ItemFact> sets{forbiddenSet, Fact(821, 60, 4)};
    Plan const setPlan = SelectFromFacts(Chest(66, sets, 60), ScoreOf);
    ok = Check(setPlan.solved.items[4] == 821 && setPlan.solved.score == ScoreOf(821, 4, 4),
            "disallowed set does not change the other score") &&
         ok;

    ItemFact plate = Fact(830, 60, 4);
    plate.armorSubtype = 4;
    std::vector<ItemFact> armor{plate, Fact(831, 60, 4)};
    ok = Check(SelectFromFacts(Chest(66, armor, 60), ScoreOf).solved.items[4] == 831, "wrong armor skipped") && ok;

    ItemFact warriorOnly = Fact(840, 60, 4);
    warriorOnly.classMask = ClassBit(1);
    std::vector<ItemFact> classes{warriorOnly, Fact(841, 60, 4)};
    ok = Check(SelectFromFacts(Chest(66, classes, 60), ScoreOf).solved.items[4] == 841, "wrong class skipped") && ok;

    ItemFact tankItem = Fact(840, 60, 4);
    tankItem.role = Role::Tank;
    std::vector<ItemFact> roles{tankItem, Fact(841, 60, 4)};
    ok = Check(SelectFromFacts(Chest(66, roles, 60), ScoreOf).solved.items[4] == 841, "wrong role skipped") && ok;

    ItemFact otherSpec = Fact(840, 60, 4);
    otherSpec.specRestricted = true;
    otherSpec.specTab = 2;
    std::vector<ItemFact> specs{otherSpec, Fact(841, 60, 4)};
    ok = Check(SelectFromFacts(Chest(66, specs, 60), ScoreOf).solved.items[4] == 841, "wrong spec skipped") && ok;

    ItemFact left{Fact(5, 60, 10)};
    left.uniqueEquipped = true;
    ItemFact right{Fact(5, 60, 11)};
    right.uniqueEquipped = true;
    std::vector<ItemFact> unique{left, right};
    PlanInput uniqueIn = Chest(66, unique, 60);
    uniqueIn.facts = unique;
    uniqueIn.eligibleMask = (1u << 10) | (1u << 11);
    uniqueIn.requiredMask = uniqueIn.eligibleMask;
    uniqueIn.completeMask = uniqueIn.eligibleMask;
    Plan const uniquePlan = SelectFromFacts(uniqueIn, ScoreOf);
    ok = Check(uniquePlan.outcome.disposition == Disposition::GenuineNoSet &&
               uniquePlan.outcome.action == GearAction::StripToBags,
            "unique pair is a genuine no-set") &&
         ok;
    uniqueIn.bags = BagSpace::Full;
    Plan const fullBags = SelectFromFacts(uniqueIn, ScoreOf);
    ok = Check(fullBags.outcome.disposition == Disposition::TechnicalFailure &&
               fullBags.outcome.action == GearAction::Preserve &&
               fullBags.outcome.recovery.reason == RecoveryReason::None,
            "full bags are a technical preserve, not a strip") &&
         ok;

    ItemFact exclusive = Fact(30, 60, 15);
    exclusive.kind = Kind::OneHand;
    exclusive.exclusiveMainhand = true;
    exclusive.armorSubtype = 0;
    ItemFact shield = Fact(31, 60, 16);
    shield.kind = Kind::Shield;
    shield.armorSubtype = 0;
    std::vector<ItemFact> weapons{exclusive, shield};
    PlanInput weaponIn = Chest(66, weapons, 60);
    weaponIn.facts = weapons;
    weaponIn.eligibleMask = (1u << 15) | (1u << 16);
    weaponIn.requiredMask = 1u << 15;
    weaponIn.completeMask = weaponIn.eligibleMask;
    Plan const blocked = SelectFromFacts(weaponIn, ScoreOf);
    ok = Check(blocked.outcome.disposition == Disposition::Commit && blocked.solved.items[15] == 30 &&
               blocked.solved.items[16] == 0,
            "exclusive main hand leaves the offhand empty") &&
         ok;

    ItemFact paired = Fact(32, 60, 15);
    paired.kind = Kind::OneHand;
    paired.armorSubtype = 0;
    ItemFact pairedShield = Fact(33, 60, 16);
    pairedShield.kind = Kind::Shield;
    pairedShield.armorSubtype = 0;
    std::vector<ItemFact> legalWeapons{paired, pairedShield};
    weaponIn.facts = legalWeapons;
    Plan const pairedPlan = SelectFromFacts(weaponIn, ScoreOf);
    ok = Check(pairedPlan.solved.items[15] == 32 && pairedPlan.solved.items[16] == 33, "legal weapon pair") && ok;

    // Darkmoon availability and procs stay unknown. Base stats below are the
    // tooltip fixture from coordination #64 comment 6900198011. They are not a
    // score and requestForceBis does not accept the card.
    struct TooltipBase
    {
        std::uint32_t id;
        std::uint32_t stamina;
        std::uint32_t allResist;
    };
    TooltipBase const cards[] = {{19287, 15, 0}, {19288, 0, 0}, {19289, 0, 0}, {19290, 15, 10}};
    for (TooltipBase const& tooltip : cards)
    {
        ItemFact card = Fact(tooltip.id, 66, 12);
        card.armorSubtype = 0;
        card.requiredLevel = 60;
        card.uniqueEquipped = true;
        card.darkmoonCard = true;
        card.requestForceBis = true;
        ok = Check(card.itemLevel == 66 && card.requiredLevel == 60 && card.uniqueEquipped && tooltip.stamina <= 15 &&
                   Classify(card, Mage(60), 66) == FactDecision::Unknown,
                "darkmoon base stats do not make availability or a proc known") &&
             ok;
        std::vector<ItemFact> oneCard{card};
        PlanInput trinket = Chest(66, oneCard, 60);
        trinket.facts = oneCard;
        trinket.eligibleMask = 1u << 12;
        trinket.requiredMask = 1u << 12;
        trinket.completeMask = 1u << 12;
        Plan const held = SelectFromFacts(trinket, ScoreOf);
        ok = Check(held.outcome.disposition == Disposition::Preserve &&
                   held.outcome.action != GearAction::StripToBags && held.solved.items[12] == 0,
                "an incomplete darkmoon card is not forced and does not strip") &&
             ok;
    }

    PlanInput incomplete = Chest(66, tier0, 60);
    incomplete.completeMask = 0;
    Plan const partial = SelectFromFacts(incomplete, ScoreOf);
    ok = Check(partial.outcome.disposition == Disposition::Preserve && partial.outcome.action == GearAction::Preserve,
            "incomplete search does not strip") &&
         ok;

    PlanInput missing = Chest(66, tier0, 60);
    missing.cap.preserve = true;
    missing.cap.cap = 0;
    ok = Check(SelectFromFacts(missing, ScoreOf).outcome.disposition == Disposition::Preserve,
            "missing cap preserves") &&
         ok;
    missing.cap = Usable(66);
    missing.authorityValid = false;
    ok = Check(SelectFromFacts(missing, ScoreOf).outcome.disposition == Disposition::Preserve,
            "missing authority preserves") &&
         ok;

    Plan const moved = SelectFromFacts(Chest(66, tier0, 60), ScoreOf);
    ok = Check(!moved.outcome.recovery.needed, "pure selection does not authorize recovery") && ok;
    RecoveryRequest const needed = NoteRecoveryNeeded(RecoveryReason::MoveFailed);
    ok = Check(needed.needed && needed.reason == RecoveryReason::MoveFailed, "recovery needed is diagnostic only") &&
         ok;
    int throws = 0;
    auto throwing = [&](std::uint32_t, std::int32_t, std::uint32_t) -> std::uint32_t
    {
        ++throws;
        throw 1;
    };
    Plan const threw = SelectFromFacts(Chest(66, tier0, 60), throwing);
    ok = Check(threw.outcome.disposition == Disposition::Preserve && !threw.sourceComplete && throws > 0,
            "a throwing scorer preserves") &&
         ok;
    throws = 0;
    ItemFact malformed = Fact(66, 60, 4);
    malformed.kind = Kind::OneHand;
    std::vector<ItemFact> badFacts{malformed};
    PlanInput badIn = Chest(66, badFacts, 60);
    Plan const badPlan = SelectFromFacts(badIn, throwing);
    ok = Check(badPlan.outcome.disposition == Disposition::Preserve && throws == 0, "malformed facts are not scored") &&
         ok;

    PlanInput illegal = Chest(66, tier0, 60);
    illegal.bot.weapons.mainhand = Bit(Kind::OneHand) | Bit(Kind::Shield);
    illegal.bot.weaponsKnown = true;
    ItemFact hand = Fact(66, 60, 15);
    hand.kind = Kind::OneHand;
    hand.armorSubtype = 0;
    std::vector<ItemFact> hands{hand};
    illegal.facts = hands;
    illegal.eligibleMask = 1u << 15;
    illegal.requiredMask = 1u << 15;
    illegal.completeMask = 1u << 15;
    Plan const invalid = SelectFromFacts(illegal, ScoreOf);
    ok = Check(invalid.solved.status == PlayerbotsAutoGearSet::Status::Invalid && !invalid.sourceComplete &&
               invalid.outcome.disposition == Disposition::Preserve,
            "an invalid solver result is not a complete source") &&
         ok;

    PlanInput budget = Chest(66, tier0, 60);
    budget.nodeBudget = 0;
    ok = Check(SelectFromFacts(budget, ScoreOf).outcome.disposition == Disposition::Preserve,
            "budget stop preserves") &&
         ok;

    std::vector<ItemFact> none;
    Plan const empty = SelectFromFacts(Chest(66, none, 60), ScoreOf);
    ok = Check(empty.outcome.disposition == Disposition::GenuineNoSet &&
               empty.outcome.action == GearAction::StripToBags,
            "complete empty domain strips to bags") &&
         ok;

    PlanInput unknownBags = Chest(66, none, 60);
    unknownBags.bags = BagSpace::Unknown;
    Plan const unknownStrip = SelectFromFacts(unknownBags, ScoreOf);
    ok = Check(unknownStrip.outcome.disposition == Disposition::TechnicalFailure &&
               unknownStrip.outcome.action == GearAction::Preserve,
            "unknown bags do not strip") &&
         ok;
    ok = Check(PlanInput{}.bags == BagSpace::Unknown, "bags default to unknown") && ok;

    AcceptedCandidate badSlot;
    badSlot.candidate.id = 1;
    badSlot.candidate.itemLevel = 1;
    badSlot.candidate.metadataKnown = true;
    badSlot.candidate.eligible = true;
    std::uint8_t const badSlots[] = {19, 31, 32, 255};
    for (std::uint8_t slot : badSlots)
    {
        badSlot.slot = slot;
        DomainSet const rejected = CollectDomains(std::span<AcceptedCandidate const>(&badSlot, 1), 1u << 4, 1u << 4, 8);
        ok = Check(rejected.status == DomainSet::Status::Invalid, "out of range slot is invalid") && ok;
    }
    AcceptedCandidate goodSlot = badSlot;
    goodSlot.slot = 4;
    DomainSet const badComplete =
        CollectDomains(std::span<AcceptedCandidate const>(&goodSlot, 1), 1u << 4, 1u << 31, 8);
    ok = Check(badComplete.status == DomainSet::Status::Invalid, "complete mask bit 31 is invalid") && ok;
    std::uint32_t const bodyMask = (1u << 4) | (1u << 3);
    DomainSet const bodyBit = CollectDomains(std::span<AcceptedCandidate const>(&goodSlot, 1), bodyMask, 1u << 4, 8);
    ok = Check(bodyBit.status == DomainSet::Status::Invalid, "body slot cannot enter the domain") && ok;

    std::vector<ItemFact> four{Fact(65, 60, 4), Fact(66, 60, 4), Fact(40, 60, 4), Fact(50, 60, 4)};
    PlanInput covered = Chest(66, four, 60);
    covered.candidateBudget = 4;
    int scores = 0;
    auto counting = [&](std::uint32_t id, std::int32_t prop, std::uint32_t slot)
    {
        ++scores;
        return ScoreOf(id, prop, slot);
    };
    Plan const allFour = SelectFromFacts(covered, counting);
    ok = Check(allFour.sourceComplete && allFour.factsAccepted == 4 && allFour.solved.items[4] == 50,
            "every eligible item is offered to the solver") &&
         ok;
    scores = 0;
    covered.candidateBudget = 2;
    Plan const overflow = SelectFromFacts(covered, counting);
    ok = Check(overflow.outcome.disposition == Disposition::Preserve && scores == 0 && !overflow.sourceComplete,
            "overflow preserves before scoring") &&
         ok;

    BotView unknownSpec = Mage(60);
    unknownSpec.specKnown = false;
    std::vector<ItemFact> one{Fact(66, 60, 4)};
    PlanInput specIn = Chest(66, one, 60);
    specIn.bot = unknownSpec;
    specIn.facts = one;
    Plan const specUnknown = SelectFromFacts(specIn, ScoreOf);
    ok = Check(specUnknown.outcome.disposition == Disposition::Preserve &&
               specUnknown.outcome.disposition != Disposition::GenuineNoSet,
            "unknown spec preserves") &&
         ok;
    ok = Check(Classify(one[0], unknownSpec, 66) == FactDecision::Unknown, "unknown spec is not unavailable") && ok;
    BotView known = Mage(60);
    ItemFact otherClass = Fact(66, 60, 4);
    otherClass.classMask = ClassBit(1);
    ok = Check(Classify(otherClass, known, 66) == FactDecision::Unavailable,
            "known class mismatch is unavailable") &&
         ok;
    ItemFact noSource = Fact(66, 60, 4);
    noSource.sourceKnown = false;
    ok = Check(Classify(noSource, known, 66) == FactDecision::Unknown, "missing item source is unknown") && ok;

    AcceptedCandidate first = goodSlot;
    AcceptedCandidate second = goodSlot;
    second.candidate.id = 2;
    AcceptedCandidate extra[2] = {first, second};
    DomainSet const bounded = CollectDomains(std::span<AcceptedCandidate const>(extra, 2), 1u << 4, 1u << 4, 1);
    ok = Check(bounded.status == DomainSet::Status::Bounded && bounded.slots[4].empty(),
            "domain overflow keeps no partial list") &&
         ok;

    int shifts = 0;
    auto shiftScorer = [&](std::uint32_t, std::int32_t, std::uint32_t) -> std::uint32_t
    {
        ++shifts;
        return 1;
    };
    BotView class255 = Mage(60);
    class255.classId = 255;
    PlanInput classIn = Chest(66, one, 60);
    classIn.bot = class255;
    Plan const classPlan = SelectFromFacts(classIn, shiftScorer);
    ok = Check(classPlan.outcome.disposition == Disposition::Preserve && shifts == 0 &&
               Classify(one[0], class255, 66) == FactDecision::Invalid && ClassBit(255) == 0,
            "class 255 is invalid before any shift") &&
         ok;
    BotView class10 = Mage(60);
    class10.classId = 10;
    shifts = 0;
    PlanInput class10In = Chest(66, one, 60);
    class10In.bot = class10;
    Plan const class10Plan = SelectFromFacts(class10In, shiftScorer);
    ok = Check(class10Plan.outcome.disposition == Disposition::Preserve && shifts == 0 &&
               Classify(one[0], class10, 66) == FactDecision::Invalid && ClassBit(10) == 0,
            "class 10 is not a playable class and does not shift") &&
         ok;
    BotView spec255 = Mage(60);
    spec255.specTab = 255;
    ok = Check(Classify(one[0], spec255, 66) == FactDecision::Invalid, "spec 255 is invalid") && ok;
    BotView role255 = Mage(60);
    role255.role = static_cast<Role>(255);
    ok = Check(Classify(one[0], role255, 66) == FactDecision::Invalid, "role 255 is invalid") && ok;
    ItemFact kind255 = one[0];
    kind255.kind = static_cast<Kind>(255);
    ok = Check(Classify(kind255, Mage(60), 66) == FactDecision::Invalid, "weapon kind 255 is invalid before a shift") &&
         ok;

    PlanInput emptyRequired = Chest(66, one, 60);
    emptyRequired.requiredMask = 0;
    shifts = 0;
    Plan const noRequired = SelectFromFacts(emptyRequired, shiftScorer);
    ok = Check(noRequired.outcome.disposition == Disposition::Preserve && shifts == 0,
            "an empty required mask preserves before scoring") &&
         ok;
    PlanInput outside = Chest(66, one, 60);
    outside.requiredMask = (1u << 4) | (1u << 5);
    shifts = 0;
    Plan const outsidePlan = SelectFromFacts(outside, shiftScorer);
    ok = Check(outsidePlan.outcome.disposition == Disposition::Preserve && shifts == 0,
            "a required bit outside the eligible mask preserves") &&
         ok;

    PlanInput alloc = Chest(66, one, 60);
    alloc.allocationFault = PlanInput::AllocationPoint::Domain;
    Plan const domainAlloc = SelectFromFacts(alloc, ScoreOf);
    ok = Check(domainAlloc.outcome.disposition == Disposition::Preserve && !domainAlloc.sourceComplete,
            "a domain allocation failure preserves") &&
         ok;
    alloc.allocationFault = PlanInput::AllocationPoint::Solve;
    Plan const solveAlloc = SelectFromFacts(alloc, ScoreOf);
    ok = Check(solveAlloc.outcome.disposition == Disposition::Preserve && !solveAlloc.sourceComplete &&
               solveAlloc.outcome.action != GearAction::StripToBags,
            "a solver allocation failure preserves") &&
         ok;
    alloc.allocationFault = PlanInput::AllocationPoint::Scoring;
    shifts = 0;
    Plan const scoreAlloc = SelectFromFacts(alloc, shiftScorer);
    ok = Check(scoreAlloc.outcome.disposition == Disposition::Preserve && shifts == 0,
            "a scoring allocation failure preserves") &&
         ok;

    ItemFact counted = Fact(66, 60, 4);
    counted.maxCount = 1;
    std::vector<ItemFact> countedFacts{counted};
    PlanInput countedIn = Chest(66, countedFacts, 60);
    shifts = 0;
    Plan const unknownCounts = SelectFromFacts(countedIn, shiftScorer);
    ok = Check(unknownCounts.outcome.disposition == Disposition::Preserve && shifts == 0,
            "unknown retained counts preserve before scoring") &&
         ok;
    RetainedCount held;
    held.entryId = 66;
    held.bagsKnown = true;
    held.bankKnown = true;
    held.equippedKnown = true;
    countedIn.retained = std::span<RetainedCount const>(&held, 1);
    shifts = 0;
    Plan const unknownGems = SelectFromFacts(countedIn, shiftScorer);
    ok = Check(unknownGems.outcome.disposition == Disposition::Preserve && shifts == 0,
            "an unknown gem count preserves") &&
         ok;
    held.gemsKnown = true;
    Plan const room = SelectFromFacts(countedIn, ScoreOf);
    ok = Check(room.outcome.disposition == Disposition::Commit && room.solved.items[4] == 66,
            "a known empty possession still allows one copy") &&
         ok;
    held.bags = 1;
    Plan const alreadyHeld = SelectFromFacts(countedIn, ScoreOf);
    ok = Check(alreadyHeld.outcome.disposition == Disposition::GenuineNoSet && alreadyHeld.solved.items[4] == 0,
            "a known full MaxCount omits the item") &&
         ok;
    ItemFact haveItem = Fact(66, 60, 4);
    haveItem.limitCategory = 9;
    haveItem.categoryLimit = 2;
    haveItem.categoryHaveLimit = 1;
    std::vector<ItemFact> haveFacts{haveItem};
    PlanInput haveIn = Chest(66, haveFacts, 60);
    RetainedCount categoryHeld;
    categoryHeld.limitCategory = 9;
    categoryHeld.bagsKnown = true;
    categoryHeld.bankKnown = true;
    categoryHeld.equippedKnown = true;
    categoryHeld.gemsKnown = true;
    categoryHeld.bags = 1;
    haveIn.retained = std::span<RetainedCount const>(&categoryHeld, 1);
    Plan const haveBlocks = SelectFromFacts(haveIn, ScoreOf);
    ok = Check(haveBlocks.outcome.disposition == Disposition::GenuineNoSet && haveBlocks.solved.items[4] == 0,
            "HAVE blocks a copy that EQUIP would still allow") &&
         ok;
    categoryHeld.bags = 0;
    Plan const haveAllows = SelectFromFacts(haveIn, ScoreOf);
    ok = Check(haveAllows.outcome.disposition == Disposition::Commit && haveAllows.solved.items[4] == 66,
            "HAVE and EQUIP are separate limits") &&
         ok;

    if (!ok)
        return 1;
    std::cout << "checks=" << g_checks << "\n";
    return 0;
}
