#include "AutoGearSetSolver.h"
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

struct Spec
{
    std::uint32_t entry = 0;
    std::int32_t property = 0;
    std::uint32_t score = 0;
    std::uint32_t itemLevel = 60;
    std::uint8_t slot = 0;
    bool unique = false;
    std::uint32_t maxCount = 0;
    std::uint32_t category = 0;
    std::uint32_t categoryLimit = 0;
    Kind kind = Kind::Count;
};

struct Expectation
{
    PlayerbotsAutoGearSet::Status status = PlayerbotsAutoGearSet::Status::Invalid;
    std::uint64_t score = 0;
    std::array<std::uint32_t, PlayerbotsAutoGearSet::Slots> entries{};
    std::array<std::int32_t, PlayerbotsAutoGearSet::Slots> properties{};
};

bool SolveBoth(std::vector<Spec> const& specs, std::uint32_t cap, std::uint32_t eligible, std::uint32_t required,
    PlayerbotsAutoGearWeapons::Capabilities const& weapons, std::size_t budget, Expectation& oldOut,
    PlayerbotsAutoGearVariant::Result& variant)
{
    std::array<std::vector<PlayerbotsAutoGearSet::Candidate>, PlayerbotsAutoGearSet::Slots> oldItems;
    std::array<std::vector<PlayerbotsAutoGearVariant::Candidate>, PlayerbotsAutoGearSet::Slots> newItems;
    for (Spec const& spec : specs)
    {
        PlayerbotsAutoGearSet::Candidate old;
        old.id = spec.entry;
        old.itemLevel = spec.itemLevel;
        old.score = spec.score;
        old.limitCategory = spec.category;
        old.categoryLimit = spec.categoryLimit;
        old.kind = spec.kind;
        old.uniqueEquipped = spec.unique;
        old.metadataKnown = true;
        old.eligible = true;
        oldItems[spec.slot].push_back(old);

        PlayerbotsAutoGearVariant::Candidate row;
        row.entryId = spec.entry;
        row.randomProperty = spec.property;
        row.itemLevel = spec.itemLevel;
        row.score = spec.score;
        row.limitCategory = spec.category;
        row.categoryLimit = spec.categoryLimit;
        row.maxCount = spec.maxCount;
        row.kind = spec.kind;
        row.uniqueEquipped = spec.unique;
        row.metadataKnown = true;
        row.eligible = true;
        newItems[spec.slot].push_back(row);
    }
    std::array<PlayerbotsAutoGearSet::Domain, PlayerbotsAutoGearSet::Slots> oldDomains{};
    std::array<PlayerbotsAutoGearVariant::Domain, PlayerbotsAutoGearSet::Slots> newDomains{};
    for (unsigned slot = 0; slot < PlayerbotsAutoGearSet::Slots; ++slot)
    {
        if ((eligible & (1u << slot)) == 0)
            continue;
        oldDomains[slot] = {oldItems[slot], true};
        newDomains[slot] = {newItems[slot], true};
    }
    PlayerbotsAutoGearSet::Policy oldPolicy;
    oldPolicy.cap = cap;
    oldPolicy.eligibleMask = eligible;
    oldPolicy.requiredMask = required;
    oldPolicy.weapons = weapons;
    oldPolicy.known = true;
    PlayerbotsAutoGearVariant::Policy newPolicy;
    newPolicy.cap = cap;
    newPolicy.eligibleMask = eligible;
    newPolicy.requiredMask = required;
    newPolicy.weapons = weapons;
    newPolicy.known = true;
    PlayerbotsAutoGearSet::Result const oldResult =
        PlayerbotsAutoGearSet::Solve(oldDomains, oldPolicy, budget, 100000);
    variant = PlayerbotsAutoGearVariant::Solve(newDomains, newPolicy, budget, 100000);
    oldOut.status = oldResult.status;
    oldOut.score = oldResult.score;
    oldOut.entries = oldResult.items;
    return true;
}

bool SameAsShared(Expectation const& oldResult, PlayerbotsAutoGearVariant::Result const& variant)
{
    if (static_cast<int>(oldResult.status) != static_cast<int>(variant.status) || oldResult.score != variant.score)
        return false;
    for (unsigned slot = 0; slot < PlayerbotsAutoGearSet::Slots; ++slot)
    {
        if (oldResult.entries[slot] != variant.entries[slot] || variant.properties[slot] != 0)
            return false;
    }
    return true;
}
}

int main()
{
    bool ok = true;
    std::uint32_t const fingers = (1u << 10) | (1u << 11);
    Expectation oldResult;
    PlayerbotsAutoGearVariant::Result variant;
    std::vector<Spec> uniqueConflict{
        {1, 0, 100, 60, 10, true},
        {2, 0, 90, 60, 10, false},
        {1, 0, 100, 60, 11, true},
        {3, 0, 80, 60, 11, false},
    };
    SolveBoth(uniqueConflict, 60, fingers, fingers, {}, 1000, oldResult, variant);
    ok = Check(SameAsShared(oldResult, variant) && variant.status == PlayerbotsAutoGearVariant::Status::SuitableSet &&
               variant.entries[10] == 2 && variant.entries[11] == 1 && variant.score == 190,
            "non-variant unique choice matches the shared solver") &&
         ok;

    std::vector<Spec> uniqueOnly{{1, 0, 100, 60, 10, true}, {1, 0, 100, 60, 11, true}};
    SolveBoth(uniqueOnly, 60, fingers, fingers, {}, 1000, oldResult, variant);
    ok = Check(SameAsShared(oldResult, variant) && variant.status == PlayerbotsAutoGearVariant::Status::NoSet,
            "the same unique entry in two slots is no set on both solvers") &&
         ok;

    std::vector<Spec> category{
        {1, 0, 100, 60, 10, false, 0, 9, 1},
        {2, 0, 50, 60, 10, false, 0, 0, 0},
        {3, 0, 100, 60, 11, false, 0, 9, 1},
        {4, 0, 40, 60, 11, false, 0, 0, 0},
    };
    SolveBoth(category, 60, fingers, fingers, {}, 1000, oldResult, variant);
    ok = Check(SameAsShared(oldResult, variant) && variant.entries[10] == 2 && variant.entries[11] == 3 &&
               variant.score == 150,
            "category limits match the shared solver") &&
         ok;

    std::vector<Spec> tie{{4, 0, 10, 60, 10, false}, {5, 0, 10, 60, 10, false}};
    SolveBoth(tie, 60, 1u << 10, 1u << 10, {}, 1000, oldResult, variant);
    ok = Check(SameAsShared(oldResult, variant) && variant.entries[10] == 4,
            "an equal score keeps the lower entry") &&
         ok;

    std::vector<Spec> over{{8, 0, 100, 90, 10, false}};
    SolveBoth(over, 60, 1u << 10, 1u << 10, {}, 1000, oldResult, variant);
    ok = Check(SameAsShared(oldResult, variant) && variant.status == PlayerbotsAutoGearVariant::Status::NoSet,
            "an over-cap domain is no set on both solvers") &&
         ok;

    PlayerbotsAutoGearWeapons::Capabilities weapons;
    weapons.mainhand = Bit(Kind::OneHand);
    weapons.mainhandRequired = true;
    std::vector<Spec> hand{{4, 0, 15, 60, 15, false, 0, 0, 0, Kind::OneHand}};
    SolveBoth(hand, 60, 1u << 15, 0, weapons, 1000, oldResult, variant);
    ok = Check(SameAsShared(oldResult, variant) && variant.entries[15] == 4 &&
               variant.status == PlayerbotsAutoGearVariant::Status::SuitableSet,
            "a non-variant weapon matches the shared solver") &&
         ok;

    std::vector<Spec> properties{{5, 1, 10, 60, 10, false}, {5, 4, 40, 60, 10, false}};
    SolveBoth(properties, 60, 1u << 10, 1u << 10, {}, 1000, oldResult, variant);
    ok = Check(oldResult.status == PlayerbotsAutoGearSet::Status::Invalid &&
               variant.status == PlayerbotsAutoGearVariant::Status::SuitableSet && variant.entries[10] == 5 &&
               variant.properties[10] == 4,
            "distinct properties stay one template and the shared solver rejects that duplicate id") &&
         ok;

    std::vector<Spec> propertyTie{{5, -1, 10, 60, 10, false}, {5, 3, 10, 60, 10, false}};
    PlayerbotsAutoGearVariant::Result const lowerProperty = [&]()
    {
        Expectation ignored;
        PlayerbotsAutoGearVariant::Result solved;
        SolveBoth(propertyTie, 60, 1u << 10, 1u << 10, {}, 1000, ignored, solved);
        return solved;
    }();
    ok = Check(lowerProperty.status == PlayerbotsAutoGearVariant::Status::SuitableSet &&
               lowerProperty.properties[10] == -1,
            "an equal score keeps the lower random property") &&
         ok;

    std::vector<Spec> counted{
        {7, 1, 50, 60, 10, false, 1},
        {8, 0, 40, 60, 10, false, 0},
        {7, 2, 50, 60, 11, false, 1},
    };
    PlayerbotsAutoGearVariant::Result const maxCount = [&]()
    {
        Expectation ignored;
        PlayerbotsAutoGearVariant::Result solved;
        SolveBoth(counted, 60, fingers, fingers, {}, 1000, ignored, solved);
        return solved;
    }();
    ok = Check(maxCount.status == PlayerbotsAutoGearVariant::Status::SuitableSet && maxCount.entries[10] == 8 &&
               maxCount.entries[11] == 7 && maxCount.properties[11] == 2 && maxCount.score == 90,
            "max count uses the template entry, not the random property") &&
         ok;

    std::vector<Spec> splitUnique{{7, 1, 50, 60, 10, true}, {7, 2, 50, 60, 11, true}};
    PlayerbotsAutoGearVariant::Result const split = [&]()
    {
        Expectation ignored;
        PlayerbotsAutoGearVariant::Result solved;
        SolveBoth(splitUnique, 60, fingers, fingers, {}, 1000, ignored, solved);
        return solved;
    }();
    ok = Check(split.status == PlayerbotsAutoGearVariant::Status::NoSet,
            "two properties of one unique entry are still one template") &&
         ok;

    std::vector<Spec> duplicate{{5, 1, 10, 60, 10, false}, {5, 1, 40, 60, 10, false}};
    PlayerbotsAutoGearVariant::Result const duplicated = [&]()
    {
        Expectation ignored;
        PlayerbotsAutoGearVariant::Result solved;
        SolveBoth(duplicate, 60, 1u << 10, 1u << 10, {}, 1000, ignored, solved);
        return solved;
    }();
    ok = Check(duplicated.status == PlayerbotsAutoGearVariant::Status::Invalid,
            "a duplicate variant is invalid, not no set") &&
         ok;

    std::vector<Spec> unsorted{{2, 0, 10, 60, 10, false}, {1, 0, 90, 60, 10, false}};
    SolveBoth(unsorted, 60, 1u << 10, 1u << 10, {}, 1000, oldResult, variant);
    ok = Check(SameAsShared(oldResult, variant) && variant.status == PlayerbotsAutoGearVariant::Status::Invalid,
            "unsorted identities are invalid on both solvers") &&
         ok;

    SolveBoth(uniqueConflict, 60, fingers, fingers, {}, 1, oldResult, variant);
    ok = Check(SameAsShared(oldResult, variant) && variant.status == PlayerbotsAutoGearVariant::Status::Incomplete,
            "a candidate budget stops as incomplete, not no set") &&
         ok;

    std::vector<Spec> badKind{{9, 0, 10, 60, 10, false, 0, 0, 0, static_cast<Kind>(255)}};
    PlayerbotsAutoGearVariant::Result const bad = [&]()
    {
        Expectation ignored;
        PlayerbotsAutoGearVariant::Result solved;
        SolveBoth(badKind, 60, 1u << 10, 1u << 10, {}, 1000, ignored, solved);
        return solved;
    }();
    ok = Check(bad.status == PlayerbotsAutoGearVariant::Status::Invalid, "kind 255 is invalid before a shift") && ok;

    if (!ok)
        return 1;
    std::cout << "checks=" << g_checks << "\n";
    return 0;
}
