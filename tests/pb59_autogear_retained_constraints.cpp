#include "AutoGearSetBonusCompare.h"
#include "AutoGearVariantSolver.h"
#include <array>
#include <iostream>
#include <vector>

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

PlayerbotsAutoGearVariant::Candidate Item(std::uint32_t entry, std::int32_t property, std::uint32_t score, Kind kind)
{
    PlayerbotsAutoGearVariant::Candidate row;
    row.entryId = entry;
    row.randomProperty = property;
    row.itemLevel = 60;
    row.score = score;
    row.kind = kind;
    row.metadataKnown = true;
    row.eligible = true;
    return row;
}

PlayerbotsAutoGearSelection::RetainedCount KnownZero(std::uint32_t entry)
{
    PlayerbotsAutoGearSelection::RetainedCount row;
    row.entryId = entry;
    row.bagsKnown = true;
    row.bankKnown = true;
    row.equippedKnown = true;
    row.gemsKnown = true;
    return row;
}

PlayerbotsAutoGearVariant::Result SolveSlots(std::vector<PlayerbotsAutoGearVariant::Candidate> const& slotA,
    unsigned slotAIndex, std::vector<PlayerbotsAutoGearVariant::Candidate> const& slotB, unsigned slotBIndex,
    std::uint32_t required, PlayerbotsAutoGearWeapons::Capabilities const& weapons,
    std::span<PlayerbotsAutoGearSelection::RetainedCount const> held)
{
    std::array<PlayerbotsAutoGearVariant::Domain, PlayerbotsAutoGearSet::Slots> domains{};
    domains[slotAIndex] = {slotA, true};
    if (slotBIndex < PlayerbotsAutoGearSet::Slots)
        domains[slotBIndex] = {slotB, true};
    PlayerbotsAutoGearVariant::Policy policy;
    policy.cap = 66;
    policy.eligibleMask = (1u << slotAIndex);
    if (slotBIndex < PlayerbotsAutoGearSet::Slots)
        policy.eligibleMask |= 1u << slotBIndex;
    policy.requiredMask = required;
    policy.weapons = weapons;
    policy.known = true;
    policy.entryHeld = held;
    return PlayerbotsAutoGearVariant::Solve(domains, policy, 1000, 100000);
}
}

int main()
{
    bool ok = true;
    using PlayerbotsAutoGearVariant::Status;
    PlayerbotsAutoGearSelection::RetainedCount held = KnownZero(9);
    std::array<PlayerbotsAutoGearSelection::RetainedCount, 1> heldRows{{held}};
    std::vector<PlayerbotsAutoGearVariant::Candidate> propLow{Item(9, 1, 10, Kind::Count)};
    std::vector<PlayerbotsAutoGearVariant::Candidate> propHigh{Item(9, 2, 40, Kind::Count)};
    propLow[0].maxCount = 1;
    propHigh[0].maxCount = 1;
    PlayerbotsAutoGearVariant::Result const variants =
        SolveSlots(propLow, 10, propHigh, 11, (1u << 10) | (1u << 11), {}, heldRows);
    ok = Check(variants.status == Status::NoSet, "two required variants of one MaxCount cannot both be worn") && ok;

    std::vector<PlayerbotsAutoGearVariant::Candidate> withAlt{Item(8, 0, 30, Kind::Count), Item(9, 1, 10, Kind::Count)};
    withAlt[1].maxCount = 1;
    PlayerbotsAutoGearVariant::Result const alt =
        SolveSlots(withAlt, 10, propHigh, 11, (1u << 10) | (1u << 11), {}, heldRows);
    ok = Check(alt.status == Status::SuitableSet && alt.entries[10] == 8 && alt.entries[11] == 9 &&
               alt.properties[11] == 2,
            "one variant of a shared template can pair with another entry") &&
         ok;

    PlayerbotsAutoGearWeapons::Capabilities weapons;
    weapons.mainhand = Bit(Kind::OneHand) | Bit(Kind::TwoHand);
    weapons.offhand = Bit(Kind::Shield) | Bit(Kind::Holdable);
    weapons.mainhandRequired = true;
    std::vector<PlayerbotsAutoGearVariant::Candidate> twoHand{Item(4, 0, 50, Kind::TwoHand)};
    std::vector<PlayerbotsAutoGearVariant::Candidate> shield{Item(5, 0, 20, Kind::Shield)};
    PlayerbotsAutoGearVariant::Result const blocked =
        SolveSlots(twoHand, 15, shield, 16, 0, weapons, {});
    ok = Check(blocked.status == Status::SuitableSet && blocked.entries[15] == 4 && blocked.entries[16] == 0,
            "a two-hand main hand leaves the off hand empty") &&
         ok;

    std::vector<PlayerbotsAutoGearVariant::Candidate> ringA{Item(3, 0, 15, Kind::Count)};
    std::vector<PlayerbotsAutoGearVariant::Candidate> ringB{Item(3, 0, 15, Kind::Count)};
    ringA[0].uniqueEquipped = true;
    ringB[0].uniqueEquipped = true;
    PlayerbotsAutoGearVariant::Result const rings =
        SolveSlots(ringA, 10, ringB, 11, (1u << 10) | (1u << 11), {}, {});
    ok = Check(rings.status == Status::NoSet, "unique rings in both slots are no set") && ok;

    std::vector<PlayerbotsAutoGearVariant::Candidate> trinketA{Item(6, 0, 12, Kind::Count)};
    std::vector<PlayerbotsAutoGearVariant::Candidate> trinketB{Item(6, 0, 12, Kind::Count)};
    trinketA[0].uniqueEquipped = true;
    trinketB[0].uniqueEquipped = true;
    PlayerbotsAutoGearVariant::Result const trinkets =
        SolveSlots(trinketA, 12, trinketB, 13, (1u << 12) | (1u << 13), {}, {});
    ok = Check(trinkets.status == Status::NoSet, "unique trinkets in both slots are no set") && ok;

    std::vector<PlayerbotsAutoGearVariant::Candidate> tie{Item(1, 0, 10, Kind::Count), Item(2, -1, 10, Kind::Count),
        Item(2, 4, 10, Kind::Count)};
    PlayerbotsAutoGearVariant::Result const tied = SolveSlots(tie, 10, {}, 19, 1u << 10, {}, {});
    ok = Check(tied.status == Status::SuitableSet && tied.entries[10] == 1 && tied.properties[10] == 0,
            "an equal score keeps the lower entry") &&
         ok;
    std::vector<PlayerbotsAutoGearVariant::Candidate> propTie{Item(2, -1, 10, Kind::Count),
        Item(2, 4, 10, Kind::Count)};
    PlayerbotsAutoGearVariant::Result const propTied = SolveSlots(propTie, 10, {}, 19, 1u << 10, {}, {});
    ok = Check(propTied.status == Status::SuitableSet && propTied.entries[10] == 2 && propTied.properties[10] == -1,
            "an equal score on one entry keeps the lower property") &&
         ok;

    PlayerbotsAutoGearSelection::BonusPiece only{1, 0, 40, 7, 60};
    PlayerbotsAutoGearSelection::BonusRule needTwo[] = {{7, 2, 100}};
    PlayerbotsAutoGearSelection::BonusCompare const incomplete =
        PlayerbotsAutoGearSelection::CompareSetBonus(std::span<PlayerbotsAutoGearSelection::BonusPiece const>(&only, 1),
            needTwo, 66);
    ok = Check(!incomplete.bounded && incomplete.jointScore == 40 && incomplete.greedyScore == 40,
            "a one-piece set does not invent the two-piece bonus") &&
         ok;

    std::vector<PlayerbotsAutoGearVariant::Candidate> alone{Item(3, 0, 15, Kind::Count)};
    alone[0].uniqueEquipped = true;
    PlayerbotsAutoGearVariant::Result const noSet =
        SolveSlots(alone, 10, ringB, 11, (1u << 10) | (1u << 11), {}, {});
    ok = Check(noSet.status == Status::NoSet, "no feasible unique pair stays no set") && ok;

    if (!ok)
        return 1;
    std::cout << "checks=" << g_checks << "\n";
    return 0;
}
