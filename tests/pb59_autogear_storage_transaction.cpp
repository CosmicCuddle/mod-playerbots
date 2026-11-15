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
    bool botContext = true;
    unsigned senderRevision = 1, inspections = 0;
    unsigned attempts = 0;
    auto reset = [&] {
        slots = {{100,{1,0,3}},{101,{2,0,1}},{102,{3,0,2}},
            {200,{0,10,0}},{201,{0,10,0}},{202,{0,11,0}}};
        botContext = true; attempts = 0; senderRevision = 1; inspections = 0;
    };
    auto inspect = [&](std::uint16_t pos) -> std::optional<ExactSlot>
    { assert(botContext); ++inspections; auto it = slots.find(pos); if (it == slots.end()) return std::nullopt; return it->second; };
    auto context = [&] { return botContext; };
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
    reset();
    assert(Execute(moves, false, inspect, context, move) == TransactionStatus::Rejected && attempts == 0 && inspections == 0);
    botContext = false;
    assert(Execute(moves, true, inspect, context, move) == TransactionStatus::Rejected && attempts == 0 && inspections == 0);
    reset();
    assert(Execute(moves, true, inspect, context, move) == TransactionStatus::Committed && attempts == 3);
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
            assert(Execute(moves, true, inspect, context, fault) == TransactionStatus::RolledBack);
            assert(slots == before);
        }
    reset();
    // A later sender change is not a loss of the already admitted B ticket.
    auto laterSender = [&](ExactMove const& m, bool restore) {
        bool ok = move(m,restore); if (!restore) ++senderRevision; return ok;
    };
    assert(Execute(moves, true, inspect, context, laterSender) == TransactionStatus::Committed &&
        senderRevision == 4 && attempts == 3);
    reset(); unsigned readsAtLoss = 0;
    auto lostBot = [&](ExactMove const& m, bool restore) {
        bool ok = move(m,restore);
        if (!restore) { botContext = false; readsAtLoss = inspections; }
        return ok;
    };
    assert(Execute(moves, true, inspect, context, lostBot) == TransactionStatus::RecoveryRequired &&
        attempts == 1 && inspections == readsAtLoss);
    assert((slots[200] == ExactSlot{1,10,3}));
    reset();
    auto lastLostBot = [&](ExactMove const& m, bool restore) {
        bool ok = move(m,restore);
        if (!restore && m.position.itemGuid == 3) { botContext = false; readsAtLoss = inspections; }
        return ok;
    };
    assert(Execute(moves, true, inspect, context, lastLostBot) == TransactionStatus::RecoveryRequired &&
        attempts == 3 && inspections == readsAtLoss);
    reset();
    // Context loss before the first mutation must not call the backend.
    auto losesBeforeMove = [&] { return inspections < 6; };
    assert(Execute(moves, true, inspect, losesBeforeMove, move) == TransactionStatus::Rejected && attempts == 0);
    reset();
    auto restoreLosesBot = [&](ExactMove const& m, bool restore) {
        if (!restore && m.position.itemGuid == 2) return false;
        bool ok = move(m,restore);
        if (restore) { botContext = false; readsAtLoss = inspections; }
        return ok;
    };
    assert(Execute(moves, true, inspect, context, restoreLosesBot) == TransactionStatus::RecoveryRequired &&
        inspections == readsAtLoss);
    reset(); slots[200].bagGuid = 99;
    assert(Execute(moves, true, inspect, context, move) == TransactionStatus::Rejected && attempts == 0);
    reset(); slots.erase(201);
    assert(Execute(moves, true, inspect, context, move) == TransactionStatus::Rejected && attempts == 0);
    reset(); slots[100].count = 2;
    assert(Execute(moves, true, inspect, context, move) == TransactionStatus::Rejected && attempts == 0);
    reset(); slots[201] = {77,10,1};
    assert(Execute(moves, true, inspect, context, move) == TransactionStatus::Rejected && attempts == 0);
    reset();
    auto conflict = [&](ExactMove const& m, bool restore) {
        if (!restore && m.position.itemGuid == 2) { slots[100] = {99,0,1}; return false; }
        return move(m,restore);
    };
    assert(Execute(moves, true, inspect, context, conflict) == TransactionStatus::RecoveryRequired);
    assert((slots[100] == ExactSlot{99,0,1} && slots[200] == ExactSlot{1,10,3}));
    reset();
    auto changedBag = [&](ExactMove const& m, bool restore) {
        if (!restore && m.position.itemGuid == 2) { slots[200].bagGuid = 88; return false; }
        return move(m,restore);
    };
    assert(Execute(moves, true, inspect, context, changedBag) == TransactionStatus::RecoveryRequired && attempts == 1);
    assert((slots[200] == ExactSlot{1,88,3}));
    reset();
    auto changedCount = [&](ExactMove const& m, bool restore) {
        if (!restore && m.position.itemGuid == 2) { slots[200].count = 4; return false; }
        return move(m,restore);
    };
    assert(Execute(moves, true, inspect, context, changedCount) == TransactionStatus::RecoveryRequired && attempts == 1);
    assert((slots[200] == ExactSlot{1,10,4}));
    reset();
    auto failedRestore = [&](ExactMove const& m, bool restore) {
        if (restore || m.position.itemGuid == 2) return false;
        return move(m,restore);
    };
    assert(Execute(moves, true, inspect, context, failedRestore) == TransactionStatus::RecoveryRequired);
    assert((slots[200] == ExactSlot{1,10,3}));
    reset(); auto unknown = [&](std::uint16_t pos) -> std::optional<ExactSlot> {
        if (pos == 201) throw std::runtime_error("read");
        return inspect(pos);
    };
    assert(Execute(moves, true, unknown, context, move) == TransactionStatus::Rejected && attempts == 0);
    auto bad = moves; bad[1].position.destination = bad[0].position.destination;
    assert(Execute(bad, true, inspect, context, move) == TransactionStatus::Rejected && attempts == 0);
    bad = moves; bad[0].count = 0;
    assert(Execute(bad, true, inspect, context, move) == TransactionStatus::Rejected && attempts == 0);
    auto throwsContext = []() -> bool { throw std::runtime_error("context"); };
    assert(Execute(moves, true, inspect, throwsContext, move) == TransactionStatus::Rejected && attempts == 0);
    // A backend may find a changed count during a successful forward move.
    reset(); auto badForward = [&](ExactMove const& m, bool restore) {
        bool ok = move(m,restore); if (!restore) ++slots[m.position.destination].count; return ok;
    };
    assert(Execute(moves, true, inspect, context, badForward) == TransactionStatus::RecoveryRequired && attempts == 1);
}
