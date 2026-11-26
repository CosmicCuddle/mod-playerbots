// Finding A1 (coordination #80 7203489253, CS01 7204771857): the capped AutoGear command on a quarantined bot.
//
// Part 1 reproduces today's behaviour through the real PlayerbotsAutoGearExec::Execute. The bot is quarantined
// (Core withholds CapturePB59InventoryStamp, and its mutation guards block item creation), and nothing needs
// stripping. The factory creates nothing, the finishers still run, and the outcome is reported as a success.
// Those checks pin the defect; they are expected to flip when the fix lands.
//
// Part 2 states the regression expectations for the proposed fix (tests/PB70_A1_QUARANTINE_FIX_PROPOSAL.md)
// with a local model of the gate. It does not change src/. The model must keep an ordinary no-op success
// (a ready bot that needs nothing) apart from a blocked one.
//
// Build: g++ -std=c++20 -Isrc/Bot -Isrc tests/pb70_a1_quarantine_reproduction.cpp

#include <cstdint>
#include <iostream>
#include <string>

#include "AutoGearCappedExecution.h"

namespace
{
using namespace PlayerbotsAutoGearExec;

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, std::string const& what)
{
    ++g_checks;
    if (!ok)
    {
        ++g_failures;
        std::cerr << "FAIL: " << what << "\n";
    }
}

// A bot whose combat slots are all filled with items within the cap, as a fully geared bot would be.
struct Bot
{
    bool quarantined = false;  // Core: InventoryQuarantined(); CapturePB59InventoryStamp() returns nothing
    bool missingSlot = false;  // one empty combat slot the factory would fill
    std::array<ItemFact, EquipmentSlots> equipped{};
    int captures = 0, strips = 0, factoryCreates = 0, finisherRuns = 0, unguardedWrites = 0, destroys = 0;

    Bot()
    {
        for (std::uint8_t slot = 0; slot < EquipmentSlots; ++slot)
            if (IsCombatSlot(slot))
                equipped[slot] = {100u + slot, 500u + slot, 60};
    }

    void EmptyOneSlot()
    {
        missingSlot = true;
        equipped[1] = {};
    }

    // Execute adapter contract.
    Snapshot Capture()
    {
        ++captures;
        Snapshot snapshot;
        snapshot.equipped = equipped;
        for (ItemFact const& item : equipped)
            if (item.guid)
                snapshot.carried.push_back(item.guid);
        snapshot.freeBagSlots = 16;
        return snapshot;
    }
    StripResult StripToBags(std::uint32_t)
    {
        ++strips;
        return quarantined ? StripResult::Refused : StripResult::Done;  // Core: no stamp, so the strip refuses
    }
    bool RunFactory(std::uint32_t, std::uint32_t cap, bool)
    {
        if (!missingSlot)
            return true;
        // EquipNewItem is guarded by InventoryMutationAllowed(): quarantine makes the create a silent no-op.
        if (!quarantined)
        {
            equipped[1] = {9001, 700, cap};
            ++factoryCreates;
        }
        return true;
    }
    bool ApplyBisOverlay(std::uint32_t) { return true; }
    void ApplyFinishers(std::uint32_t)
    {
        ++finisherRuns;
        // DurabilityRepairAll writes ITEM_FIELD_DURABILITY before its guarded SetState (Core88 Player.cpp 5062).
        if (quarantined)
            ++unguardedWrites;
    }
    bool DestroyCreatedEquipped(std::uint8_t, std::uint64_t)
    {
        ++destroys;
        return false;
    }
    // Proposed: Core's own readiness signal (the stamp), read before anything else.
    bool Ready() const { return !quarantined; }
};

Plan RunningPlan()
{
    PlanInput input;
    input.capVerified = true;
    input.cap = 78;
    input.populationKnown = true;
    input.population = PlayerbotsAutoGearSelection::Population::RandomBot;
    input.sourcesAvailable = true;
    return PlanExecution(input);
}

// Model of the proposed gate: before any adapter call, a bot without a stamp is refused. NotReady is modelled
// as RecoveryRequired's neighbour, a refusal that ran nothing; the real fix adds Refusal::NotReady.
struct Gated
{
    Outcome outcome;
    bool notReady = false;
};

template <class Inventory>
Gated ExecuteWithReadiness(Inventory& inventory, Plan const& plan)
{
    Gated gated;
    if (plan.Runs() && !inventory.Ready())
    {
        gated.notReady = true;
        gated.outcome.refusal = Refusal::NotVerified;  // placeholder for the proposed Refusal::NotReady
        return gated;
    }
    gated.outcome = Execute(inventory, plan);
    return gated;
}

void ReproduceToday()
{
    Bot bot;
    bot.quarantined = true;
    bot.missingSlot = true;
    Outcome const outcome = Execute(bot, RunningPlan());
    // The defect: blocked mutation reported as success, and finishers ran on a quarantined bot.
    Check(outcome.Succeeded(), "A1 today: a quarantined bot's blocked command is reported as success");
    Check(outcome.newItems == 0 && bot.factoryCreates == 0, "A1 today: nothing was created");
    Check(bot.finisherRuns == 1 && bot.unguardedWrites == 1, "A1 today: finishers ran, including the unguarded repair");

    Bot geared;  // ordinary no-op on a ready bot: also success with no new items
    Outcome const noop = Execute(geared, RunningPlan());
    Check(noop.Succeeded() && noop.newItems == 0, "today: a ready, fully geared bot is a no-op success");
    Check(outcome.Succeeded() == noop.Succeeded() && outcome.newItems == noop.newItems,
          "A1 today: Execute cannot tell the blocked run from the no-op");
}

void ExpectAfterFix()
{
    {
        Bot bot;
        bot.quarantined = true;
        bot.EmptyOneSlot();
        Gated const gated = ExecuteWithReadiness(bot, RunningPlan());
        Check(gated.notReady && !gated.outcome.ran && !gated.outcome.Succeeded(), "fix: a quarantined bot is refused");
        Check(bot.captures + bot.strips + bot.factoryCreates + bot.finisherRuns + bot.destroys == 0,
              "fix: no adapter call at all on a quarantined bot");
    }
    {
        Bot bot;  // ready and fully geared: the ordinary no-op stays a success
        Gated const gated = ExecuteWithReadiness(bot, RunningPlan());
        Check(!gated.notReady && gated.outcome.Succeeded() && gated.outcome.newItems == 0,
              "fix: an ordinary no-op on a ready bot is still a success");
    }
    {
        Bot bot;  // ready with an empty slot: unchanged behaviour
        bot.EmptyOneSlot();
        Gated const gated = ExecuteWithReadiness(bot, RunningPlan());
        Check(gated.outcome.Succeeded() && gated.outcome.newItems == 1, "fix: a ready bot is geared as before");
    }
    {
        Bot bot;  // a refused plan never asks for readiness and changes nothing
        bot.quarantined = true;
        PlanInput input;
        Gated const gated = ExecuteWithReadiness(bot, PlanExecution(input));
        Check(!gated.notReady && gated.outcome.refusal == Refusal::NotVerified && bot.captures == 0,
              "fix: an already refused plan keeps its own refusal");
    }
}
}  // namespace

int main()
{
    ReproduceToday();
    ExpectAfterFix();
    std::cout << "checks=" << g_checks << " failures=" << g_failures << "\n";
    return g_failures == 0 ? 0 : 1;
}
