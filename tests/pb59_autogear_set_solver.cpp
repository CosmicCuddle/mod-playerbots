#include "AutoGearSetSolver.h"
#include <cassert>

using namespace PlayerbotsAutoGearSet;
using PlayerbotsAutoGearWeapons::Bit;

int main()
{
    std::array<Candidate, 2> rings1{{{1, 50, 100, 0, 0, Kind::Count, true, true, true},
        {2, 50, 90, 0, 0, Kind::Count, false, true, true}}};
    std::array<Candidate, 2> rings2{{{1, 50, 100, 0, 0, Kind::Count, true, true, true},
        {3, 50, 80, 0, 0, Kind::Count, false, true, true}}};
    std::array<Candidate, 2> main{{{10, 50, 70, 0, 0, Kind::OneHand, false, true, true},
        {11, 50, 100, 0, 0, Kind::TwoHand, false, true, true}}};
    std::array<Candidate, 1> off{{{12, 50, 60, 0, 0, Kind::Shield, false, true, true}}};
    std::array<Domain, Slots> domains{};
    domains[10] = {rings1, true};
    domains[11] = {rings2, true};
    domains[Main] = {main, true};
    domains[Off] = {off, true};
    Policy policy{60, (1u << 10) | (1u << 11) | (1u << Main) | (1u << Off),
        (1u << 10) | (1u << 11), {Bit(Kind::OneHand) | Bit(Kind::TwoHand), Bit(Kind::Shield), 0,
            false, false, true, false, false}, true};
    auto result = Solve(domains, policy, 100, 1000);
    assert(result.status == Status::SuitableSet && result.score == 320);
    assert(result.items[10] == 2 && result.items[11] == 1);
    assert(result.items[Main] == 10 && result.items[Off] == 12);
    assert(Solve(domains, policy, 1, 1000).status == Status::Incomplete);
    assert(Solve(domains, policy, 100, 1).status == Status::Incomplete);
    domains[10].complete = false;
    assert(Solve(domains, policy, 100, 1000).status == Status::Incomplete);
    domains[10].complete = true;
    policy.cap = 49;
    assert(Solve(domains, policy, 100, 1000).status == Status::NoSet);
    policy.cap = 60;
    rings1[0].metadataKnown = false;
    assert(Solve(domains, policy, 100, 1000).status == Status::Invalid);
    rings1[0].metadataKnown = true;
    policy.weapons.titanGrip = true;
    policy.weapons.dualWield = true;
    off[0].kind = Kind::TwoHand;
    policy.weapons.offhand = Bit(Kind::TwoHand);
    result = Solve(domains, policy, 100, 1000);
    assert(result.status == Status::SuitableSet && result.items[Main] == 11 && result.items[Off] == 12);
    main[1].exclusiveMainhand = true;
    result = Solve(domains, policy, 100, 1000);
    assert(result.status == Status::SuitableSet && result.items[Main] == 10 && result.items[Off] == 12);
    main[1].exclusiveMainhand = false;
    off[0].exclusiveMainhand = true;
    result = Solve(domains, policy, 100, 1000);
    assert(result.status == Status::SuitableSet && result.items[Main] == 11 && result.items[Off] == 0);
    off[0].exclusiveMainhand = false;
    rings1[0].limitCategory = rings1[1].limitCategory = rings2[0].limitCategory = rings2[1].limitCategory = 7;
    rings1[0].categoryLimit = rings1[1].categoryLimit = rings2[0].categoryLimit = rings2[1].categoryLimit = 1;
    assert(Solve(domains, policy, 100, 1000).status == Status::NoSet);
    rings2[0].categoryLimit = 2;
    assert(Solve(domains, policy, 100, 1000).status == Status::Invalid);
    rings2[0].categoryLimit = 1;
    policy.weapons.mainhand |= Bit(Kind::Shield);
    assert(Solve(domains, policy, 100, 1000).status == Status::Invalid);
}
