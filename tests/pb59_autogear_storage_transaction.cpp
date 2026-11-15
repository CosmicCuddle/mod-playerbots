#include "AutoGearStorageTransaction.h"
#include <array>
#include <cassert>
#include <map>
#include <stdexcept>
using namespace PlayerbotsAutoGearStorage;

int main()
{
    std::array<Move, 3> moves{{{1, 100, 200}, {2, 101, 201}, {3, 102, 202}}};
    std::map<unsigned, std::uint64_t> items;
    auto reset = [&] { items = {{100,1},{101,2},{102,3}}; };
    auto inspect = [&](std::uint16_t pos) -> std::optional<std::uint64_t>
    { auto it = items.find(pos); return it == items.end() ? 0 : it->second; };
    auto move = [&](std::uint16_t from, std::uint16_t to, std::uint64_t guid)
    {
        if (inspect(from) != guid || inspect(to) != 0) return false;
        items.erase(from); items[to] = guid; return true;
    };
    reset();
    assert(Execute(moves, false, inspect, move) == TransactionStatus::Rejected);
    assert(Execute(moves, true, inspect, move) == TransactionStatus::Committed);
    assert(inspect(200) == 1 && inspect(201) == 2 && inspect(202) == 3);
    for (unsigned fail = 0; fail < 3; ++fail)
        for (unsigned mode = 0; mode < 3; ++mode)
        {
            reset();
            auto before = items;
            unsigned n = 0;
            auto fault = [&](std::uint16_t from, std::uint16_t to, std::uint64_t guid)
            {
                if (from < 200 && n++ == fail)
                {
                    if (mode == 0) return false;
                    if (mode == 1) { move(from, to, guid); return false; }
                    move(from, to, guid); throw std::runtime_error("after move");
                }
                return move(from, to, guid);
            };
            assert(Execute(moves, true, inspect, fault) == TransactionStatus::RolledBack);
            assert(items == before);
        }
    reset();
    auto conflict = [&](std::uint16_t from, std::uint16_t to, std::uint64_t guid)
    {
        if (from == 101) { items[100] = 99; return false; }
        return move(from, to, guid);
    };
    assert(Execute(moves, true, inspect, conflict) == TransactionStatus::RecoveryRequired);
    assert(inspect(100) == 99 && inspect(200) == 1); // preserve both, never overwrite
    reset();
    auto failedRollback = [&](std::uint16_t from, std::uint16_t to, std::uint64_t guid)
    {
        if (from == 101 || from == 200) return false;
        return move(from, to, guid);
    };
    assert(Execute(moves, true, inspect, failedRollback) == TransactionStatus::RecoveryRequired);
    assert(inspect(200) == 1);
    reset();
    items[201] = 77;
    assert(Execute(moves, true, inspect, move) == TransactionStatus::Rejected);
    assert(inspect(100) == 1 && inspect(201) == 77);
    reset();
    auto unknown = [&](std::uint16_t pos) -> std::optional<std::uint64_t>
    { if (pos == 201) throw std::runtime_error("read"); return inspect(pos); };
    assert(Execute(moves, true, unknown, move) == TransactionStatus::Rejected);
    auto bad = moves; bad[1].destination = bad[0].destination;
    assert(Execute(bad, true, inspect, move) == TransactionStatus::Rejected);
}
