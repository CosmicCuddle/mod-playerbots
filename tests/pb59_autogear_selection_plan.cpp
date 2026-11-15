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
    fact.metadataKnown = true;
    fact.randomProperty = 4;
    if (slot == 4)
        fact.armorSubtype = 1;
    return fact;
}

BotView Mage(std::uint32_t level)
{
    BotView bot;
    bot.classId = 8;
    bot.level = level;
    bot.role = Role::Caster;
    bot.specKnown = true;
    bot.specTab = 1;
    bot.armorMask = std::uint32_t{1} << 1;
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
    ok = Check(fullBags.outcome.disposition == Disposition::GenuineNoSet &&
               fullBags.outcome.action == GearAction::Preserve,
            "full bags do not destroy") &&
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

    // SYNTHETIC Darkmoon card: entry 910001, item level 60, dummy +30 attack power.
    // These numbers are not read from Michael's database.
    ItemFact card = Fact(910001, 60, 12);
    card.armorSubtype = 0;
    card.darkmoonCard = true;
    card.requestForceBis = true;
    ItemFact otherCard = Fact(910002, 60, 12);
    otherCard.armorSubtype = 0;
    std::vector<ItemFact> cards{card, otherCard};
    PlanInput trinket = Chest(66, cards, 60);
    trinket.facts = cards;
    trinket.eligibleMask = 1u << 12;
    trinket.requiredMask = 1u << 12;
    trinket.completeMask = 1u << 12;
    Plan const cardWins = SelectFromFacts(trinket, ScoreOf);
    ok = Check(cardWins.outcome.disposition == Disposition::Commit && cardWins.solved.items[12] == 910001,
            "synthetic card wins on its supplied score") &&
         ok;

    ItemFact weakCard = Fact(910001, 60, 12);
    weakCard.armorSubtype = 0;
    weakCard.darkmoonCard = true;
    weakCard.requestForceBis = true;
    ItemFact stronger = Fact(910003, 60, 12);
    stronger.armorSubtype = 0;
    // Reuse 910003 as the non-card with the higher synthetic score.
    std::vector<ItemFact> notForced{weakCard, stronger};
    // Both currently score 80. Lower the card by giving the non-card the id that scores 80
    // and the card id 910002 (score 10) while still requesting force.
    notForced[0].id = 910002;
    notForced[1].id = 910003;
    trinket.facts = notForced;
    Plan const cardLoses = SelectFromFacts(trinket, ScoreOf);
    ok = Check(cardLoses.solved.items[12] == 910003, "force flag does not beat a higher score") && ok;

    ItemFact overCard = Fact(910001, 67, 12);
    overCard.armorSubtype = 0;
    overCard.darkmoonCard = true;
    overCard.requestForceBis = true;
    std::vector<ItemFact> over{overCard};
    trinket.facts = over;
    Plan const cardOver = SelectFromFacts(trinket, ScoreOf);
    ok = Check(cardOver.outcome.disposition == Disposition::GenuineNoSet && cardOver.solved.items[12] == 0,
            "over-cap synthetic card is not forced") &&
         ok;

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

    PlanInput failed = Chest(66, tier0, 60);
    failed.technical = RecoveryReason::MoveFailed;
    Plan const moved = SelectFromFacts(failed, ScoreOf);
    ok = Check(moved.outcome.disposition == Disposition::TechnicalFailure &&
               moved.outcome.action == GearAction::Restore &&
               moved.outcome.recovery.reason == RecoveryReason::MoveFailed,
            "move failure requests restore") &&
         ok;
    failed.technical = RecoveryReason::ContextLost;
    ok = Check(SelectFromFacts(failed, ScoreOf).outcome.action == GearAction::Restore, "context loss restores") && ok;
    failed.technical = RecoveryReason::InspectUnknown;
    Plan const unknown = SelectFromFacts(failed, ScoreOf);
    ok = Check(unknown.outcome.disposition == Disposition::TechnicalFailure &&
               unknown.outcome.action == GearAction::Preserve,
            "unknown inspect preserves") &&
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

    if (!ok)
        return 1;
    std::cout << "checks=" << g_checks << "\n";
    return 0;
}
