#include "AutoGearCandidateScan.h"
#include <array>

using namespace PlayerbotsAutoGearCandidates;

constexpr std::array<Candidate, 4> candidates = {{
    {1, 66, true, true}, {2, 78, true, true}, {3, 30, true, false}, {4, 65, true, true}
}};
static_assert(Scan(candidates, 66, true, 4).status == Status::CompleteWithCandidates);
static_assert(Scan(candidates, 66, true, 4).visited == 4);
static_assert(Scan(candidates, 66, true, 4).matches == 2);
static_assert(Scan(candidates, 66, true, 4).firstItemId == 1);
static_assert(Scan(candidates, 66, true, 3).status == Status::Incomplete);
static_assert(Scan(candidates, 66, true, 3).matches == 0);
static_assert(Scan(candidates, 66, false, 4).status == Status::Incomplete);
static_assert(Scan(candidates, 1, true, 4).status == Status::CompleteEmpty);
static_assert(Scan(candidates, 0, true, 4).status == Status::InvalidInput);
static_assert(Scan({}, 66, false, 0).status == Status::Incomplete);
static_assert(Scan({}, 66, true, 0).status == Status::CompleteEmpty);
constexpr std::array<Candidate, 2> duplicate = {{{1, 66, true, true}, {1, 65, true, true}}};
constexpr std::array<Candidate, 2> unordered = {{{2, 66, true, true}, {1, 65, true, true}}};
constexpr std::array<Candidate, 1> unknown = {{{1, 66, false, false}}};
static_assert(Scan(duplicate, 66, true, 2).status == Status::InvalidInput);
static_assert(Scan(unordered, 66, true, 2).status == Status::InvalidInput);
static_assert(Scan(unknown, 66, true, 1).status == Status::InvalidInput);

int main() { return 0; }
