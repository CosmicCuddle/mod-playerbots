#include "AutoGearSelectionPlan.h"
#include "AutoGearVariantSolver.h"
#include <array>
#include <iostream>
#include <vector>

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

std::uint32_t Next(std::uint32_t& seed)
{
    seed = seed * 1664525u + 1013904223u;
    return seed;
}

PlayerbotsAutoGearSet::Candidate Shared(std::uint32_t id, std::uint32_t score)
{
    PlayerbotsAutoGearSet::Candidate row;
    row.id = id;
    row.itemLevel = 60;
    row.score = score;
    row.kind = Kind::Count;
    row.metadataKnown = true;
    row.eligible = true;
    return row;
}

PlayerbotsAutoGearVariant::Candidate VariantOf(PlayerbotsAutoGearSet::Candidate const& shared)
{
    PlayerbotsAutoGearVariant::Candidate row;
    row.entryId = shared.id;
    row.itemLevel = shared.itemLevel;
    row.score = shared.score;
    row.kind = shared.kind;
    row.metadataKnown = true;
    row.eligible = true;
    return row;
}

bool Agree(PlayerbotsAutoGearSet::Result const& shared, PlayerbotsAutoGearVariant::Result const& variant)
{
    if (static_cast<int>(shared.status) != static_cast<int>(variant.status) || shared.score != variant.score)
        return false;
    for (unsigned slot = 0; slot < PlayerbotsAutoGearSet::Slots; ++slot)
    {
        if (shared.items[slot] != variant.entries[slot] || variant.properties[slot] != 0)
            return false;
    }
    return true;
}
}

int main()
{
    bool ok = true;
    std::uint32_t const fingers = (1u << 10) | (1u << 11);
    std::uint32_t seed = 0xC0FFEEu;
    for (int sample = 0; sample < 8; ++sample)
    {
        std::array<PlayerbotsAutoGearSet::Candidate, 2> low{{Shared(1, (Next(seed) % 40) + 1),
            Shared(2, (Next(seed) % 40) + 1)}};
        std::array<PlayerbotsAutoGearSet::Candidate, 2> high{{Shared(3, (Next(seed) % 40) + 1),
            Shared(4, (Next(seed) % 40) + 1)}};
        std::array<PlayerbotsAutoGearVariant::Candidate, 2> lowV{{VariantOf(low[0]), VariantOf(low[1])}};
        std::array<PlayerbotsAutoGearVariant::Candidate, 2> highV{{VariantOf(high[0]), VariantOf(high[1])}};
        std::array<PlayerbotsAutoGearSet::Domain, PlayerbotsAutoGearSet::Slots> sharedDomains{};
        std::array<PlayerbotsAutoGearVariant::Domain, PlayerbotsAutoGearSet::Slots> variantDomains{};
        sharedDomains[10] = {low, true};
        sharedDomains[11] = {high, true};
        variantDomains[10] = {lowV, true};
        variantDomains[11] = {highV, true};
        PlayerbotsAutoGearSet::Policy sharedPolicy;
        sharedPolicy.cap = 66;
        sharedPolicy.eligibleMask = fingers;
        sharedPolicy.requiredMask = fingers;
        sharedPolicy.known = true;
        PlayerbotsAutoGearVariant::Policy variantPolicy;
        variantPolicy.cap = 66;
        variantPolicy.eligibleMask = fingers;
        variantPolicy.requiredMask = fingers;
        variantPolicy.known = true;
        PlayerbotsAutoGearSet::Result const sharedResult =
            PlayerbotsAutoGearSet::Solve(sharedDomains, sharedPolicy, 1000, 100000);
        PlayerbotsAutoGearVariant::Result const variantResult =
            PlayerbotsAutoGearVariant::Solve(variantDomains, variantPolicy, 1000, 100000);
        std::uint64_t best = 0;
        std::uint32_t bestLow = 0;
        std::uint32_t bestHigh = 0;
        for (PlayerbotsAutoGearSet::Candidate const& left : low)
        {
            for (PlayerbotsAutoGearSet::Candidate const& right : high)
            {
                std::uint64_t const score = std::uint64_t{left.score} + right.score;
                bool const better = score > best || (score == best && (bestLow == 0 || left.id < bestLow ||
                                                                          (left.id == bestLow && right.id < bestHigh)));
                if (better)
                {
                    best = score;
                    bestLow = left.id;
                    bestHigh = right.id;
                }
            }
        }
        bool const same = Agree(sharedResult, variantResult);
        bool const chosen = sharedResult.status == PlayerbotsAutoGearSet::Status::SuitableSet &&
                            sharedResult.items[10] == bestLow && sharedResult.items[11] == bestHigh &&
                            sharedResult.score == best;
        ok = Check(same && chosen, "fixed-seed pair matches the exhaustive oracle and the shared solver") && ok;
    }

    std::array<PlayerbotsAutoGearVariant::Candidate, 2> malformed{{VariantOf(Shared(2, 1)), VariantOf(Shared(1, 9))}};
    std::array<PlayerbotsAutoGearVariant::Domain, PlayerbotsAutoGearSet::Slots> badDomains{};
    badDomains[10] = {malformed, true};
    PlayerbotsAutoGearVariant::Policy badPolicy;
    badPolicy.cap = 66;
    badPolicy.eligibleMask = 1u << 10;
    badPolicy.requiredMask = 1u << 10;
    badPolicy.known = true;
    ok = Check(PlayerbotsAutoGearVariant::Solve(badDomains, badPolicy, 1000, 100000).status ==
               PlayerbotsAutoGearVariant::Status::Invalid,
            "unsorted identities are invalid") &&
         ok;
    malformed[0].entryId = 0;
    ok = Check(PlayerbotsAutoGearVariant::Solve(badDomains, badPolicy, 1000, 100000).status ==
               PlayerbotsAutoGearVariant::Status::Invalid,
            "a zero entry is invalid") &&
         ok;

    std::array<PlayerbotsAutoGearVariant::Candidate, 3> overflow{{VariantOf(Shared(1, 1)), VariantOf(Shared(2, 1)),
        VariantOf(Shared(3, 1))}};
    badDomains[10] = {overflow, true};
    ok = Check(PlayerbotsAutoGearVariant::Solve(badDomains, badPolicy, 1, 100000).status ==
               PlayerbotsAutoGearVariant::Status::Incomplete,
            "a candidate overflow is incomplete") &&
         ok;
    ok = Check(PlayerbotsAutoGearVariant::Solve(badDomains, badPolicy, 1000, 0).status ==
               PlayerbotsAutoGearVariant::Status::Incomplete,
            "a node budget of zero is incomplete") &&
         ok;
    badPolicy.allocationFault = PlayerbotsAutoGearVariant::Policy::AllocationFault::Allocate;
    ok = Check(PlayerbotsAutoGearVariant::Solve(badDomains, badPolicy, 1000, 100000).status ==
               PlayerbotsAutoGearVariant::Status::Incomplete,
            "an injected allocation failure is incomplete") &&
         ok;

    PlayerbotsAutoGearSelection::PlanInput exhausted;
    exhausted.cap = PlayerbotsAutoGearSelection::CapResult{false, 66};
    exhausted.bot.levelKnown = true;
    exhausted.bot.level = 60;
    exhausted.bot.classKnown = true;
    exhausted.bot.classId = 8;
    exhausted.bot.specKnown = true;
    exhausted.bot.specTab = 1;
    exhausted.bot.roleKnown = true;
    exhausted.bot.role = PlayerbotsAutoGearSelection::Role::Caster;
    exhausted.bot.armorKnown = true;
    exhausted.bot.armorMask = 1u << 1;
    exhausted.bot.weaponsKnown = true;
    exhausted.eligibleMask = 1u << 4;
    exhausted.requiredMask = 1u << 4;
    exhausted.completeMask = 1u << 4;
    exhausted.authorityValid = true;
    exhausted.bags = PlayerbotsAutoGearSelection::BagSpace::Enough;
    exhausted.nodeBudget = 0;
    PlayerbotsAutoGearSelection::ItemFact fact;
    fact.id = 4;
    fact.itemLevel = 60;
    fact.requiredLevel = 1;
    fact.slot = 4;
    fact.armorSubtype = 1;
    fact.sourceKnown = true;
    fact.metadataKnown = true;
    fact.equipKnown = true;
    fact.equipAllowed = true;
    std::array<PlayerbotsAutoGearSelection::ItemFact, 1> facts{{fact}};
    exhausted.facts = facts;
    PlayerbotsAutoGearSelection::Plan const preserved = PlayerbotsAutoGearSelection::SelectFromFacts(
        exhausted, [](std::uint32_t, std::int32_t, std::uint32_t) { return 1u; });
    ok = Check(preserved.outcome.disposition == PlayerbotsAutoGearSelection::Disposition::Preserve &&
               preserved.outcome.action != PlayerbotsAutoGearSelection::GearAction::StripToBags,
            "search exhaustion preserves") &&
         ok;

    if (!ok)
        return 1;
    std::cout << "checks=" << g_checks << "\n";
    return 0;
}
