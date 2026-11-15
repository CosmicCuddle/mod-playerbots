#include "AutoGearStorageTransaction.h"
#include <array>
#include <cassert>
#include <map>
#include <stdexcept>
using namespace PlayerbotsAutoGearStorage;

int main()
{
    std::array<ExactMove, 3> moves{{{{1,100,200},3,10},{{2,101,201},1,10},{{3,102,202},2,11}}};
    std::map<unsigned, ExactSlot> slots;
    bool authority = true;
    unsigned attempts = 0;
    auto reset = [&] {
        slots = {{100,{1,0,3}},{101,{2,0,1}},{102,{3,0,2}},
            {200,{0,10,0}},{201,{0,10,0}},{202,{0,11,0}}};
        authority = true; attempts = 0;
    };
    auto inspect = [&](std::uint16_t pos) -> std::optional<ExactSlot>
    { auto it = slots.find(pos); if (it == slots.end()) return std::nullopt; return it->second; };
    auto auth = [&] { return authority; };
    auto move = [&](ExactMove const& m, bool restore)
    {
        ++attempts;
        auto from = restore ? m.position.destination : m.position.source;
        auto to = restore ? m.position.source : m.position.destination;
        auto fromBag = restore ? m.destinationBagGuid : 0;
        auto toBag = restore ? 0 : m.destinationBagGuid;
        if (inspect(from) != ExactSlot{m.position.itemGuid,fromBag,m.count} ||
            inspect(to) != ExactSlot{0,toBag,0}) return false;
        slots[from] = {0,fromBag,0}; slots[to] = {m.position.itemGuid,toBag,m.count}; return true;
    };
    reset(); authority = false;
    assert(Execute(moves, inspect, auth, move) == TransactionStatus::Rejected && attempts == 0);
    reset();
    assert(Execute(moves, inspect, auth, move) == TransactionStatus::Committed && attempts == 3);
    assert((slots[200] == ExactSlot{1,10,3} && slots[202] == ExactSlot{3,11,2}));
    for (unsigned fail = 0; fail < 3; ++fail)
        for (unsigned mode = 0; mode < 3; ++mode)
        {
            reset(); auto before = slots; unsigned n = 0;
            auto fault = [&](ExactMove const& m, bool restore) {
                if (!restore && n++ == fail) {
                    if (mode == 0) return false;
                    if (mode == 1) { move(m,restore); return false; }
                    move(m,restore); throw std::runtime_error("after move");
                }
                return move(m,restore);
            };
            assert(Execute(moves, inspect, auth, fault) == TransactionStatus::RolledBack);
            assert(slots == before);
        }
    reset(); auto before = slots;
    auto stale = [&](ExactMove const& m, bool restore) {
        bool ok = move(m,restore); if (!restore) authority = false; return ok;
    };
    assert(Execute(moves, inspect, auth, stale) == TransactionStatus::RolledBack && slots == before && attempts == 2);
    reset(); before = slots;
    auto lastStale = [&](ExactMove const& m, bool restore) {
        bool ok = move(m,restore); if (!restore && m.position.itemGuid == 3) authority = false; return ok;
    };
    assert(Execute(moves, inspect, auth, lastStale) == TransactionStatus::RolledBack && slots == before && attempts == 6);
    reset(); slots[200].bagGuid = 99;
    assert(Execute(moves, inspect, auth, move) == TransactionStatus::Rejected && attempts == 0);
    reset(); slots.erase(201);
    assert(Execute(moves, inspect, auth, move) == TransactionStatus::Rejected && attempts == 0);
    reset(); slots[100].count = 2;
    assert(Execute(moves, inspect, auth, move) == TransactionStatus::Rejected && attempts == 0);
    reset(); slots[201] = {77,10,1};
    assert(Execute(moves, inspect, auth, move) == TransactionStatus::Rejected && attempts == 0);
    reset();
    auto conflict = [&](ExactMove const& m, bool restore) {
        if (!restore && m.position.itemGuid == 2) { slots[100] = {99,0,1}; return false; }
        return move(m,restore);
    };
    assert(Execute(moves, inspect, auth, conflict) == TransactionStatus::RecoveryRequired);
    assert((slots[100] == ExactSlot{99,0,1} && slots[200] == ExactSlot{1,10,3}));
    reset();
    auto changedBag = [&](ExactMove const& m, bool restore) {
        if (!restore && m.position.itemGuid == 2) { slots[200].bagGuid = 88; return false; }
        return move(m,restore);
    };
    assert(Execute(moves, inspect, auth, changedBag) == TransactionStatus::RecoveryRequired && attempts == 1);
    assert((slots[200] == ExactSlot{1,88,3}));
    reset();
    auto changedCount = [&](ExactMove const& m, bool restore) {
        if (!restore && m.position.itemGuid == 2) { slots[200].count = 4; return false; }
        return move(m,restore);
    };
    assert(Execute(moves, inspect, auth, changedCount) == TransactionStatus::RecoveryRequired && attempts == 1);
    assert((slots[200] == ExactSlot{1,10,4}));
    reset();
    auto failedRestore = [&](ExactMove const& m, bool restore) {
        if (restore || m.position.itemGuid == 2) return false;
        return move(m,restore);
    };
    assert(Execute(moves, inspect, auth, failedRestore) == TransactionStatus::RecoveryRequired);
    assert((slots[200] == ExactSlot{1,10,3}));
    reset(); auto unknown = [&](std::uint16_t pos) -> std::optional<ExactSlot> {
        if (pos == 201) throw std::runtime_error("read");
        return inspect(pos);
    };
    assert(Execute(moves, unknown, auth, move) == TransactionStatus::Rejected && attempts == 0);
    auto bad = moves; bad[1].position.destination = bad[0].position.destination;
    assert(Execute(bad, inspect, auth, move) == TransactionStatus::Rejected && attempts == 0);
    bad = moves; bad[0].count = 0;
    assert(Execute(bad, inspect, auth, move) == TransactionStatus::Rejected && attempts == 0);
    auto throwsAuth = []() -> bool { throw std::runtime_error("authority"); };
    assert(Execute(moves, inspect, throwsAuth, move) == TransactionStatus::Rejected && attempts == 0);
    // A backend may find a changed count during a successful forward move.
    reset(); auto badForward = [&](ExactMove const& m, bool restore) {
        bool ok = move(m,restore); if (!restore) ++slots[m.position.destination].count; return ok;
    };
    assert(Execute(moves, inspect, auth, badForward) == TransactionStatus::RecoveryRequired && attempts == 1);
}
