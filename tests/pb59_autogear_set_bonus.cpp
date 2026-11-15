#include "AutoGearSetBonusCompare.h"
#include <iostream>

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

    if (!ok)
        return 1;
    std::cout << "checks=" << g_checks << "\n";
    return 0;
}
