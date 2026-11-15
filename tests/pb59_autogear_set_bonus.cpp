#include "AutoGearSetBonusCompare.h"
#include <iostream>
#include <span>

using namespace PlayerbotsAutoGearSelection;

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
    BonusPiece highA{1, 0, 50, 0, 60};
    BonusPiece lowA{2, 0, 30, 7, 60};
    BonusPiece highB{3, 1, 50, 0, 60};
    BonusPiece lowB{4, 1, 30, 7, 60};
    BonusPiece over{9, 0, 1000, 7, 90};
    BonusPiece pieces[] = {highA, lowA, highB, lowB, over};
    BonusRule rules[] = {{7, 2, 100}};
    BonusCompare const compared = CompareSetBonus(pieces, rules, 66);
    ok = Check(!compared.bounded && compared.greedyScore == 100 && compared.jointScore == 160,
            "joint set bonus beats greedy") &&
         ok;
    ok = Check(compared.greedyIds[0] == 1 && compared.greedyIds[1] == 3, "greedy keeps the higher base scores") && ok;
    ok = Check(compared.jointIds[0] == 2 && compared.jointIds[1] == 4, "joint keeps the set") && ok;
    ok = Check(compared.greedyIds[0] != 9 && compared.jointIds[0] != 9, "over-cap pieces stay out of both") && ok;

    BonusPiece overflow[5];
    for (std::uint32_t i = 0; i < 5; ++i)
        overflow[i] = BonusPiece{i + 1, 0, 10, 0, 60};
    ok = Check(CompareSetBonus(overflow, rules, 66).bounded, "more than four pieces in one slot is bounded") && ok;

    BonusRule tooMany[] = {{1, 2, 1}, {2, 2, 1}, {3, 2, 1}, {4, 2, 1}, {5, 2, 1}};
    BonusCompare const tooManyResult = CompareSetBonus(pieces, tooMany, 66);
    ok = Check(tooManyResult.bounded && tooManyResult.jointScore == 0 && tooManyResult.greedyScore == 0,
            "more than four rules is bounded") &&
         ok;
    BonusRule zeroSet[] = {{0, 2, 10}};
    ok = Check(CompareSetBonus(pieces, zeroSet, 66).bounded, "a zero set id is rejected") && ok;
    BonusRule zeroPieces[] = {{7, 0, 10}};
    ok = Check(CompareSetBonus(pieces, zeroPieces, 66).bounded, "a zero piece count is rejected") && ok;
    BonusRule duplicate[] = {{7, 2, 100}, {7, 2, 1}};
    ok = Check(CompareSetBonus(pieces, duplicate, 66).bounded, "a duplicate set id is rejected") && ok;
    BonusRule overflowing[] = {{7, 2, 0xFFFFFFFFu}};
    BonusCompare const overflowed = CompareSetBonus(pieces, overflowing, 66);
    ok = Check(overflowed.bounded && overflowed.jointScore == 0 && overflowed.greedyScore == 0,
            "an overflowing bonus is rejected") &&
         ok;
    BonusCompare const noRule =
        CompareSetBonus(std::span<BonusPiece const>(pieces, 4), std::span<BonusRule const>{}, 66);
    ok = Check(!noRule.bounded && noRule.jointScore == 100 && noRule.greedyScore == 100, "no rule invents no bonus") &&
         ok;
    BonusPiece dupA{1, 0, 10, 0, 60};
    BonusPiece dupB{1, 0, 20, 0, 60};
    BonusPiece dups[] = {dupA, dupB};
    ok = Check(CompareSetBonus(dups, rules, 66).bounded, "a duplicate piece id is rejected") && ok;

    if (!ok)
        return 1;
    std::cout << "checks=" << g_checks << "\n";
    return 0;
}
