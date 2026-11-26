// AutoGear command/gear safety matrix (CS01 Part 41 Task 2, coordination #80 7198695947).
//
// Drives the real ResolveCommand -> PlanExecution -> Execute path with a scripted inventory, across
// command forms, commander stages, populations and inventory faults. Only approved policy is used:
// caps come from ApprovedVanillaCompletion, never from this file. Invariants checked on every run:
//   - a refused command changes nothing;
//   - a run never keeps a new item above the plan cap, and the plan cap never exceeds the approved
//     stage cap;
//   - nothing the bot owned before leaves its inventory unless the outcome is RecoveryRequired;
//   - after any recovery stop nothing reads or writes the inventory.
// Live-only properties (two-hand/off-hand displacement, ammo, mail) are listed as NOT EXECUTED here
// and covered by tests/pb59_test_autogear_adapter_contract.py source rules.
//
// Build: g++ -std=c++20 -Isrc/Bot -Isrc tests/pb70_autogear_safety_matrix.cpp

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "AutoGearCapCommand.h"
#include "AutoGearSlotPolicy.h"

namespace
{
using namespace PlayerbotsAutoGearExec;
using PlayerbotsAutoGearSelection::ApprovedVanillaCompletion;

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

constexpr PlayerbotsAutoGearSlots::Layout Layout = {0, 1, 3, 4, 6, 7, 10, 11, 12, 13, 15, 16, 17, 18};
auto const Searched = [](std::uint32_t level, std::uint8_t slot)
{ return PlayerbotsAutoGearSlots::SearchAtLevel(level, slot, Layout); };

enum class Fault
{
    None,
    StripRefused,    // Core refused the strip; nothing moved
    StripRestored,   // Core restored every moved item
    StripRecovery,   // a strip could not be proven
    FactoryStop,     // a factory replacement needed recovery
    LosesOwnedItem,  // an owned item leaves the inventory (e.g. implicit mail)
    BisStop,         // BiS needed recovery
};

// Scripted inventory honouring the Execute adapter contract.
struct Inventory
{
    std::array<ItemFact, EquipmentSlots> equipped{};
    std::vector<std::uint64_t> bags;
    std::uint32_t freeSlots = 16;
    std::uint32_t createLevel = 0;  // 0: create at the cap; otherwise this level
    Fault fault = Fault::None;
    std::uint64_t nextGuid = 9000;
    bool stopped = false;
    int accessAfterStop = 0;
    int strips = 0;

    void Touch() const
    {
        if (stopped)
            ++const_cast<Inventory*>(this)->accessAfterStop;
    }

    Snapshot Capture() const
    {
        Touch();
        Snapshot snapshot;
        snapshot.equipped = equipped;
        for (ItemFact const& item : equipped)
            if (item.guid)
                snapshot.carried.push_back(item.guid);
        snapshot.carried.insert(snapshot.carried.end(), bags.begin(), bags.end());
        snapshot.freeBagSlots = freeSlots;
        return snapshot;
    }

    StripResult StripToBags(std::uint32_t mask)
    {
        Touch();
        ++strips;
        switch (fault)
        {
            case Fault::StripRefused:
                return StripResult::Refused;
            case Fault::StripRestored:
                return StripResult::Restored;
            case Fault::StripRecovery:
                stopped = true;
                return StripResult::RecoveryRequired;
            default:
                break;
        }
        for (std::uint8_t slot = 0; slot < EquipmentSlots; ++slot)
            if ((mask & (1u << slot)) && equipped[slot].guid)
            {
                bags.push_back(equipped[slot].guid);
                equipped[slot] = {};
                --freeSlots;
            }
        return StripResult::Done;
    }

    bool RunFactory(std::uint32_t, std::uint32_t cap, bool)
    {
        Touch();
        for (std::uint8_t slot = 0; slot < EquipmentSlots; ++slot)
        {
            if (!IsCombatSlot(slot) || equipped[slot].guid)
                continue;
            if (fault == Fault::FactoryStop && slot > 2)
            {
                stopped = true;
                return false;
            }
            equipped[slot] = {nextGuid++, 700u + slot, createLevel ? createLevel : cap};
        }
        if (fault == Fault::LosesOwnedItem && !bags.empty())
            bags.pop_back();
        return true;
    }

    bool ApplyBisOverlay(std::uint32_t)
    {
        Touch();
        if (fault == Fault::BisStop)
        {
            stopped = true;
            return false;
        }
        return true;
    }

    void ApplyFinishers(std::uint32_t) { Touch(); }

    bool DestroyCreatedEquipped(std::uint8_t slot, std::uint64_t guid)
    {
        Touch();
        if (equipped[slot].guid != guid)
            return false;
        equipped[slot] = {};
        return true;
    }
};

// A geared random bot: three items, one over every Vanilla cap, plus two bag items.
Inventory GearedBot()
{
    Inventory inventory;
    inventory.equipped[0] = {101, 11, 60};
    inventory.equipped[4] = {102, 12, 200};
    inventory.equipped[15] = {103, 13, 50};
    inventory.bags = {201, 202};
    inventory.freeSlots = 14;
    return inventory;
}

CommandFacts Commander(std::uint8_t stage, std::string_view text = {})
{
    CommandFacts facts;
    facts.argument = text;
    facts.commandEnabled = true;
    facts.altCommandAllowed = true;  // historical setting deliberately on: must not admit alt bots
    facts.qualityLimit = 4;
    facts.randomBot = true;
    facts.randomAccount = true;
    facts.progressionKnown = true;
    facts.rawStage = stage;
    facts.effectiveStage = stage;
    facts.senderLevel = 60;
    facts.senderSlotsComplete = true;
    facts.senderIsMaster = true;
    facts.sourcesAvailable = true;
    for (std::uint8_t slot = 0; slot < EquipmentSlots; ++slot)
        if (IsCombatSlot(slot))
            facts.senderSlots[slot] = 300;
    return facts;
}

struct Run
{
    ResolvedCommand resolved;
    Outcome outcome;
    Snapshot before;
    Snapshot after;
};

Run Drive(CommandFacts const& facts, Inventory& inventory)
{
    Run run;
    run.before = inventory.Capture();
    run.resolved = ResolveCommand(facts, Searched);
    run.outcome = Execute(inventory, run.resolved.plan);
    inventory.stopped = false;  // the oracle may look after the run
    run.after = inventory.Capture();
    return run;
}

bool SameInventory(Snapshot const& a, Snapshot const& b)
{
    for (std::uint8_t slot = 0; slot < EquipmentSlots; ++slot)
        if (a.equipped[slot].guid != b.equipped[slot].guid)
            return false;
    return a.carried == b.carried;
}

// Invariants every run must keep.
void Invariants(Run const& run, std::uint8_t stage, std::string const& label)
{
    Outcome const& o = run.outcome;
    if (!o.ran)
        Check(SameInventory(run.before, run.after), label + ": refused command changed nothing");
    if (o.refusal == Refusal::None)
    {
        Check(run.resolved.plan.cap <= ApprovedVanillaCompletion[stage],
              label + ": plan cap within the approved stage cap");
        for (ItemFact const& item : run.after.equipped)
            if (item.guid && !Carried(run.before, item.guid))
                Check(item.itemLevel <= run.resolved.plan.cap, label + ": no new item above the cap");
    }
    if (o.refusal != Refusal::RecoveryRequired)
        for (std::uint64_t guid : run.before.carried)
            Check(Carried(run.after, guid), label + ": no owned item lost without a recovery");
    Check(o.Succeeded() == (o.ran && o.refusal == Refusal::None && o.capHeld), label + ": success is truthful");
}

void TestFormsAndStages()
{
    // Every form at every approved Vanilla stage: either refused without change or capped.
    char const* forms[] = {"", "reset", "match", "70", "300", "banana", "reset 70"};
    for (std::uint8_t stage = 0; stage < 8; ++stage)
        for (char const* form : forms)
        {
            Inventory inventory = GearedBot();
            Run const run = Drive(Commander(stage, form), inventory);
            Invariants(run, stage, "stage " + std::to_string(stage) + " form '" + form + "'");
        }
    {
        Inventory inventory = GearedBot();
        Run const run = Drive(Commander(0, "banana"), inventory);
        Check(!run.outcome.ran && run.outcome.refusal == Refusal::Rejected, "garbage argument is rejected");
    }
    {
        Inventory inventory = GearedBot();
        Run const run = Drive(Commander(1), inventory);
        Check(run.outcome.Succeeded() && run.resolved.plan.cap == 78, "stage 1 plain runs at cap 78");
        Check(!Carried(run.after, 0) && Carried(run.after, 102), "over-cap owned item kept (in bags)");
        Check(run.after.equipped[4].guid != 102, "over-cap item no longer equipped");
    }
}

void TestBisForms()
{
    CommandFacts facts = Commander(1);
    facts.bis = true;
    facts.commandEnabled = false;  // AutoGearBisCommand = 0
    Inventory off = GearedBot();
    Run const disabled = Drive(facts, off);
    Check(!disabled.outcome.ran && disabled.outcome.refusal == Refusal::Rejected, "BiS with the form off is rejected");
    Invariants(disabled, 1, "BiS off");

    facts.commandEnabled = true;
    facts.bisPvpSpec = true;
    Inventory pvp = GearedBot();
    Run const pvpRun = Drive(facts, pvp);
    Check(!pvpRun.outcome.ran, "BiS for a PvP spec is refused");
    Invariants(pvpRun, 1, "BiS PvP");

    facts.bisPvpSpec = false;
    Inventory on = GearedBot();
    Run const enabled = Drive(facts, on);
    Invariants(enabled, 1, "BiS on");
}

void TestCommanderVerification()
{
    CommandFacts facts = Commander(3);
    facts.progressionKnown = false;  // no IP answer, GM, bot or excluded account
    Inventory inventory = GearedBot();
    Run const run = Drive(facts, inventory);
    Check(!run.outcome.ran && run.outcome.refusal == Refusal::NotVerified, "missing IP progression is refused");
    Invariants(run, 3, "missing IP");

    CommandFacts noSources = Commander(1);
    noSources.sourcesAvailable = false;
    Inventory inventory2 = GearedBot();
    Run const run2 = Drive(noSources, inventory2);
    Check(!run2.outcome.ran && run2.outcome.refusal == Refusal::SourcesUnavailable,
          "no source index: refused before any change");
    Invariants(run2, 1, "no sources");
}

void TestPopulations()
{
    struct Case
    {
        bool randomBot, randomAccount;
        Refusal refusal;
        char const* label;
    } const cases[] = {
        {false, false, Refusal::Rejected, "alt bot"},
        // Finding (reported in #80): a mixed population is refused safely, but by the route's unknown-
        // population gate, which leaves the cap unverified. The plan then reports NotVerified, so the bot
        // tells the commander their progression is unconfirmed. Pinned as observed; UnknownPopulation is
        // never reached from these facts.
        {true, false, Refusal::NotVerified, "random bot on a non-random account"},
        {false, true, Refusal::NotVerified, "random account, untracked bot (AddClass)"},
    };
    for (Case const& c : cases)
        for (char const* form : {"", "reset", "match"})
        {
            CommandFacts facts = Commander(1, form);
            facts.randomBot = c.randomBot;
            facts.randomAccount = c.randomAccount;
            Inventory inventory = GearedBot();
            Run const run = Drive(facts, inventory);
            std::string const label = std::string(c.label) + " '" + form + "'";
            Check(!run.outcome.ran && run.outcome.refusal == c.refusal, label + ": refused with the expected reason");
            Check(inventory.strips == 0, label + ": original gear never stripped");
            Invariants(run, 1, label);
        }
}

void TestInventoryFaults()
{
    struct Case
    {
        Fault fault;
        char const* form;
        Refusal refusal;
        bool ran;
        char const* label;
    } const cases[] = {
        {Fault::StripRefused, "", Refusal::StripRefused, false, "strip refused"},
        {Fault::StripRestored, "reset", Refusal::StripRestored, false, "strip restored"},
        {Fault::StripRecovery, "reset", Refusal::RecoveryRequired, false, "strip unproven"},
        {Fault::FactoryStop, "reset", Refusal::RecoveryRequired, true, "factory stop"},
        {Fault::LosesOwnedItem, "reset", Refusal::RecoveryRequired, true, "owned item lost (implicit mail)"},
    };
    for (Case const& c : cases)
    {
        Inventory inventory = GearedBot();
        inventory.fault = c.fault;
        Run const run = Drive(Commander(1, c.form), inventory);
        Check(run.outcome.refusal == c.refusal && run.outcome.ran == c.ran,
              std::string(c.label) + ": expected outcome");
        Check(inventory.accessAfterStop == 0, std::string(c.label) + ": no inventory access after a stop");
        Check(!run.outcome.Succeeded(), std::string(c.label) + ": never reported as success");
        Invariants(run, 1, c.label);
    }
    {
        Inventory inventory = GearedBot();
        inventory.fault = Fault::LosesOwnedItem;
        Run const run = Drive(Commander(1, "reset"), inventory);
        Check(run.outcome.ownedMissing == 1, "a lost owned item is counted, never silent");
    }
    {
        CommandFacts facts = Commander(1);
        facts.bis = true;
        Inventory inventory = GearedBot();
        inventory.fault = Fault::BisStop;
        Run const run = Drive(facts, inventory);
        if (run.resolved.plan.Runs())
        {
            Check(run.outcome.refusal == Refusal::RecoveryRequired && run.outcome.bisRestoreFailed,
                  "BiS stop is a recovery");
            Check(inventory.accessAfterStop == 0, "BiS stop: no inventory access after the stop");
        }
    }
    {
        // Full bags: the planned strip does not fit, nothing moves.
        Inventory inventory = GearedBot();
        inventory.freeSlots = 0;
        Run const run = Drive(Commander(1, "reset"), inventory);
        Check(!run.outcome.ran && run.outcome.refusal == Refusal::BagsFull, "full bags refuse a reset");
        Invariants(run, 1, "bags full");
    }
}

void TestCapEnforcement()
{
    // The factory tries to keep items above the cap: they are removed, and the run still reports truthfully.
    Inventory inventory = GearedBot();
    inventory.createLevel = 250;
    Run const run = Drive(Commander(1), inventory);
    Check(run.outcome.ran && run.outcome.revoked > 0 && run.outcome.capHeld, "over-cap created items are revoked");
    Invariants(run, 1, "over-cap factory");
    for (std::uint64_t guid : run.before.carried)
        Check(Carried(run.after, guid), "revocation never touches owned items");
}

void NotExecuted()
{
    // Properties that need a live Player and PlayerbotFactory. Reported, never faked.
    char const* const blocked[] = {
        "two-hand/off-hand displacement (PlayerbotFactory::DisplacesOffhand on a live Player)",
        "ammo preservation in capped finishers (PlayerbotFactory::InitAmmo with SetKeepOwnedItems)",
        "no implicit off-hand mail from Core during equip (Player::AutoUnequipOffhandIfNeed)",
        "native strip/restore effects and quarantine (Core executor)",
    };
    for (char const* item : blocked)
        std::cout << "NOT EXECUTED / BLOCKED (needs native fixture): " << item << "\n";
}
}  // namespace

int main()
{
    TestFormsAndStages();
    TestBisForms();
    TestCommanderVerification();
    TestPopulations();
    TestInventoryFaults();
    TestCapEnforcement();
    NotExecuted();
    std::cout << "checks=" << g_checks << " failures=" << g_failures << "\n";
    return g_failures == 0 ? 0 : 1;
}
