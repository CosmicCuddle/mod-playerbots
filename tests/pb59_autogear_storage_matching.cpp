#include "AutoGearStorageMatching.h"
#include <array>
#include <cassert>
#include <set>
using namespace PlayerbotsAutoGearStorage;

int main()
{
    std::vector<Domain> d{{1, 100, {200, 201}}, {2, 101, {200}}};
    auto result = Match(d, true, 100);
    assert(result.status == Status::Ready && result.moves.size() == 2);
    assert(result.moves[0].destination == 201 && result.moves[1].destination == 200);
    assert(Match(d, false, 100).status == Status::Incomplete);
    assert(Match(d, true, 2).status == Status::Incomplete);
    d[0].destinations = {200};
    assert(Match(d, true, 100).status == Status::NoSpace);
    d[0].destinations = {101};
    assert(Match(d, true, 100).status == Status::Invalid);
    d[0].destinations = {201};
    d[0].itemGuid = 2;
    assert(Match(d, true, 100).status == Status::Invalid);
    assert(Match({}, true, 0).status == Status::Ready);
    // Exhaustive 3 x 3 bipartite graphs, independent brute force oracle.
    for (unsigned mask = 0; mask < 512; ++mask)
    {
        d.clear();
        for (unsigned i = 0; i < 3; ++i)
        {
            Domain row{1+i, static_cast<std::uint16_t>(100+i), {}};
            for (unsigned j = 0; j < 3; ++j)
                if (mask & (1u << (i*3+j)))
                    row.destinations.push_back(static_cast<std::uint16_t>(200+j));
            d.push_back(row);
        }
        std::array<unsigned, 3> p{0,1,2};
        bool possible = false;
        do { possible |= (mask & (1u << p[0])) && (mask & (1u << (3+p[1]))) && (mask & (1u << (6+p[2]))); }
        while (std::next_permutation(p.begin(), p.end()));
        auto r = Match(d, true, 9);
        assert((r.status == Status::Ready) == possible);
        if (possible)
        {
            std::set<unsigned> used;
            for (unsigned i = 0; i < 3; ++i)
            {
                assert(r.moves[i].itemGuid == d[i].itemGuid && r.moves[i].source == d[i].source);
                assert(std::find(d[i].destinations.begin(), d[i].destinations.end(), r.moves[i].destination) != d[i].destinations.end());
                assert(used.insert(r.moves[i].destination).second);
            }
        }
        else assert(r.status == Status::NoSpace && r.moves.empty());
    }
}
