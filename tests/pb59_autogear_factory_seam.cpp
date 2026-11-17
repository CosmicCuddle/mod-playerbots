// Native factory planning helper regression; includes the real plain solver.
// This is NOT InitEquipment, a live Player, an AutoGear command or a DB test.
#include "AutoGearFactoryPlanningGuard.h"

#include <array>
#include <cstdint>
#include <iostream>

using namespace PlayerbotsAutoGearSet;
using PlayerbotsAutoGearFactory::PlanWithUnprovenEquipLimits;

namespace
{
int checks = 0;
bool Check(bool valid, char const* name)
{
    ++checks;
    if (!valid)
        std::cerr << "FAIL " << checks << " " << name << '\n';
    return valid;
}
Candidate Item(std::uint32_t id, std::uint32_t category = 0, std::uint32_t limit = 0)
{
    Candidate row{};
    row.id = id;
    row.itemLevel = 20;
    row.score = 25;
    row.kind = Kind::Count;
    row.metadataKnown = true;
    row.eligible = true;
    row.limitCategory = category;
    row.categoryLimit = limit;
    return row;
}
}

int main()
{
    bool ok = true;
    auto test = [&](bool condition, char const* description)
    {
        ok = Check(condition, description) && ok;
    };

    constexpr std::uint32_t chest = 1u << 4;
    constexpr std::uint32_t trinket = 1u << 12;

    Policy policy{};
    policy.known = true;
    policy.cap = 66;
    policy.eligibleMask = chest;
    policy.requiredMask = chest;

    std::array<Candidate, 1> chestRows{Item(100)};
    std::array<Candidate, 1> categoryRows{Item(500, 9, 1)};
    std::array<Domain, Slots> domains{};
    domains[4] = {chestRows, true};

    auto result = PlanWithUnprovenEquipLimits(domains, policy, 20, 100);
    test(result.status == Status::SuitableSet, "ordinary uncategorized set preserved");
    test(result.items[4] == 100, "normal chest entry is unchanged");
    test(result.score == 25, "normal scorer result unchanged");

    domains[4] = {categoryRows, true};
    result = PlanWithUnprovenEquipLimits(domains, policy, 20, 100);
    test(result.status == Status::Incomplete, "required category lacks retained equipped and gem proof");
    test(result.items[4] == 0, "no category item certified");
    test(result.visited == 0, "unsafe category never reaches solver traversal");

    // No set may be certified if the category only appears in an OPTIONAL
    // but eligible slot, including an unchosen source candidate.
    domains[4] = {chestRows, true};
    domains[12] = {categoryRows, true};
    policy.eligibleMask = chest | trinket;
    result = PlanWithUnprovenEquipLimits(domains, policy, 20, 100);
    test(result.status == Status::Incomplete, "optional category blocks full-set certificate");
    test(result.items[4] == 0, "optional category cannot leak a partial set");

    // A malformed category must fail closed instead of becoming unlimited.
    categoryRows[0].categoryLimit = 0;
    result = PlanWithUnprovenEquipLimits(domains, policy, 20, 100);
    test(result.status == Status::Incomplete, "category ID without cap is unknown");
    categoryRows[0].limitCategory = 0;
    categoryRows[0].categoryLimit = 1;
    result = PlanWithUnprovenEquipLimits(domains, policy, 20, 100);
    test(result.status == Status::Incomplete, "cap without category identity is unknown");
    categoryRows[0].limitCategory = 9;
    categoryRows[0].categoryLimit = 1;
    categoryRows[0].eligible = false;
    result = PlanWithUnprovenEquipLimits(domains, policy, 20, 100);
    test(result.status == Status::Incomplete, "ineligible-looking category cannot prove absence");
    categoryRows[0].eligible = true;

    // Noneligible domains remain outside this planner's declared universe.
    policy.eligibleMask = chest;
    result = PlanWithUnprovenEquipLimits(domains, policy, 20, 100);
    test(result.status == Status::SuitableSet, "unselected domain does not change authorized chest result");
    test(result.items[4] == 100, "noneligible category leaves chest untouched");

    // Incomplete domain metadata remains incomplete even with no category.
    policy.eligibleMask = chest | trinket;
    domains[12] = {{}, false};
    result = PlanWithUnprovenEquipLimits(domains, policy, 20, 100);
    test(result.status == Status::Incomplete, "incomplete eligible slot still not proven empty");

    // The added inspection cannot scan unbounded source candidates.
    std::array<Candidate, 2> twoRows{Item(100), Item(101)};
    policy.eligibleMask = chest;
    domains[4] = {twoRows, true};
    result = PlanWithUnprovenEquipLimits(domains, policy, 1, 100);
    test(result.status == Status::Incomplete, "source budget is enforced before solver");
    result = PlanWithUnprovenEquipLimits(domains, policy, 2, 100);
    test(result.status == Status::SuitableSet, "bounded ordinary domain still solved");
    test(result.items[4] == 100, "tie-break remains deterministic");

    // Bad native policy must not accidentally become a positive certificate.
    policy.known = false;
    result = PlanWithUnprovenEquipLimits(domains, policy, 2, 100);
    test(result.status != Status::SuitableSet, "unverified policy is not a safe set");

    std::cout << "checks=" << checks << " status=" << (ok ? "PASS" : "FAIL") << '\n';
    return ok ? 0 : 1;
}
