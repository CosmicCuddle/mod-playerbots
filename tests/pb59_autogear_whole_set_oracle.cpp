#include "AutoGearVariantSolver.h"
#include "AutoGearWholeSetScore.h"
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
}

int main()
{
    bool ok = true;
    using PlayerbotsAutoGearSelection::LegalPiece;
    using PlayerbotsAutoGearSelection::SearchLegalSet;
    using PlayerbotsAutoGearSelection::SetDelta;
    using PlayerbotsAutoGearSelection::WholeSetStatus;
    LegalPiece plain[] = {{1, 10, 10, 0, true}, {2, 10, 30, 0, true}, {3, 11, 10, 0, true}, {4, 11, 40, 0, true}};
    PlayerbotsAutoGearSelection::WholeSetOracle const additive = SearchLegalSet(plain, {}, 4, 4);
    std::vector<PlayerbotsAutoGearVariant::Candidate> low{[]
        {
            PlayerbotsAutoGearVariant::Candidate row;
            row.entryId = 1;
            row.itemLevel = 60;
            row.score = 10;
            row.kind = Kind::Count;
            row.metadataKnown = true;
            row.eligible = true;
            return row;
        }(),
        []
        {
            PlayerbotsAutoGearVariant::Candidate row;
            row.entryId = 2;
            row.itemLevel = 60;
            row.score = 30;
            row.kind = Kind::Count;
            row.metadataKnown = true;
            row.eligible = true;
            return row;
        }()};
    std::vector<PlayerbotsAutoGearVariant::Candidate> high{[]
        {
            PlayerbotsAutoGearVariant::Candidate row;
            row.entryId = 3;
            row.itemLevel = 60;
            row.score = 10;
            row.kind = Kind::Count;
            row.metadataKnown = true;
            row.eligible = true;
            return row;
        }(),
        []
        {
            PlayerbotsAutoGearVariant::Candidate row;
            row.entryId = 4;
            row.itemLevel = 60;
            row.score = 40;
            row.kind = Kind::Count;
            row.metadataKnown = true;
            row.eligible = true;
            return row;
        }()};
    std::array<PlayerbotsAutoGearVariant::Domain, PlayerbotsAutoGearSet::Slots> domains{};
    domains[10].candidates = low;
    domains[10].complete = true;
    domains[11].candidates = high;
    domains[11].complete = true;
    PlayerbotsAutoGearVariant::Policy policy;
    policy.cap = 66;
    policy.eligibleMask = (1u << 10) | (1u << 11);
    policy.requiredMask = policy.eligibleMask;
    policy.known = true;
    PlayerbotsAutoGearVariant::Result const variant =
        PlayerbotsAutoGearVariant::Solve(domains, policy, 1000, 100000);
    ok = Check(additive.status == WholeSetStatus::Scored && additive.score == variant.score && variant.score == 70 &&
               !additive.additiveSolverIsSetOptimal,
            "with no set bonus the oracle matches the additive solver") &&
         ok;

    LegalPiece setPieces[] = {{1, 10, 10, 7, true}, {2, 10, 50, 0, true}, {3, 11, 10, 7, true}, {4, 11, 50, 0, true}};
    SetDelta delta{7, 2, 100};
    std::span<SetDelta const> const rules(&delta, 1);
    PlayerbotsAutoGearSelection::WholeSetOracle const withDelta = SearchLegalSet(setPieces, rules, 4, 4);
    ok = Check(withDelta.status == WholeSetStatus::Scored && withDelta.score == 120 && withDelta.additiveScore == 20,
            "a two-piece threshold adds its delta") &&
         ok;

    setPieces[0].evidenceKnown = false;
    ok = Check(SearchLegalSet(setPieces, rules, 4, 4).status == WholeSetStatus::Unknown,
            "unknown set evidence does not score") &&
         ok;

    LegalPiece tooMany[] = {{1, 10, 1, 0, true}, {2, 10, 1, 0, true}, {3, 10, 1, 0, true}, {4, 10, 1, 0, true},
        {5, 10, 1, 0, true}};
    ok = Check(SearchLegalSet(tooMany, {}, 4, 4).status == WholeSetStatus::Unknown,
            "more than the item bound is unknown, not a partial score") &&
         ok;

    if (!ok)
        return 1;
    std::cout << "checks=" << g_checks << "\n";
    return 0;
}
