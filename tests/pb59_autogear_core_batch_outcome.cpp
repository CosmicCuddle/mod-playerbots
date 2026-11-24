// The Core batch executor's four agreed results, mapped to the strip result and run through the real
// executor (PlayerbotsAutoGearExec::Execute) on every route that can move an item the bot owns.
// Needs no Core headers: the native executor type is not published yet (coordination #52 7132070192,
// 7133329461). The fake adapter below is the wiring contract the live adapter must meet once wired.
#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "AutoGearCoreBatchRequest.h"

namespace
{
using namespace PlayerbotsAutoGearExec;
using PlayerbotsAutoGearCoreBatch::CoreBatchObservation;
using PlayerbotsAutoGearCoreBatch::CoreBatchOutcome;
using PlayerbotsAutoGearCoreBatch::Diagnostic;
using PlayerbotsAutoGearCoreBatch::MayMoveOwnedItems;
using PlayerbotsAutoGearCoreBatch::SelectStripPath;
using PlayerbotsAutoGearCoreBatch::StripPath;
using PlayerbotsAutoGearCoreBatch::ToStripResult;

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

constexpr CoreBatchOutcome AllOutcomes[] = {CoreBatchOutcome::Refused, CoreBatchOutcome::Applied,
                                            CoreBatchOutcome::Restored, CoreBatchOutcome::RecoveryRequired};

CoreBatchObservation Observed(bool unchanged, bool atDestination, std::size_t missing = 0)
{
    CoreBatchObservation observed;
    observed.unchangedSinceStart = unchanged;
    observed.everyMoveAtDestination = atDestination;
    observed.missing = missing;
    return observed;
}

void TestMapping()
{
    // The consistent cases.
    Check(ToStripResult(CoreBatchOutcome::Applied, Observed(false, true)) == StripResult::Done,
          "Applied with every item at its destination -> Done");
    Check(ToStripResult(CoreBatchOutcome::Refused, Observed(true, false)) == StripResult::Refused,
          "Refused with nothing changed -> Refused");
    Check(ToStripResult(CoreBatchOutcome::Restored, Observed(true, false)) == StripResult::Restored,
          "Restored with nothing changed -> Restored (F5 kept)");
    Check(ToStripResult(CoreBatchOutcome::RecoveryRequired, Observed(true, false)) == StripResult::RecoveryRequired,
          "RecoveryRequired -> RecoveryRequired");
    // A result Playerbots' own observation contradicts is never believed.
    Check(ToStripResult(CoreBatchOutcome::Refused, Observed(false, false)) == StripResult::RecoveryRequired,
          "Refused while something changed -> recovery");
    Check(ToStripResult(CoreBatchOutcome::Restored, Observed(false, false)) == StripResult::RecoveryRequired,
          "Restored while something changed -> recovery (a changed inventory never passes as restored)");
    Check(ToStripResult(CoreBatchOutcome::Applied, Observed(false, false)) == StripResult::RecoveryRequired,
          "Applied but an item is not at its destination -> recovery");
    Check(ToStripResult(CoreBatchOutcome::Applied, Observed(true, false)) == StripResult::RecoveryRequired,
          "Applied but nothing moved -> recovery");
    for (CoreBatchOutcome outcome : AllOutcomes)
        for (bool unchanged : {true, false})
            for (bool atDestination : {true, false})
                Check(
                    ToStripResult(outcome, Observed(unchanged, atDestination, 1)) == StripResult::RecoveryRequired,
                    std::string(PlayerbotsAutoGearCoreBatch::OutcomeName(outcome)) + " with missing items -> recovery");
    Check(ToStripResult(static_cast<CoreBatchOutcome>(7), Observed(true, true)) == StripResult::RecoveryRequired,
          "an unknown result fails closed to recovery");
    for (CoreBatchOutcome outcome : AllOutcomes)
        for (bool unchanged : {true, false})
            for (bool atDestination : {true, false})
                Check(ToStripResult(outcome, Observed(unchanged, atDestination)) != StripResult::NoSpace,
                      "no Core result claims no space");
}

// The capped command's routes, with or without Core's executor. Every move of an item the bot owns goes
// through the Core strip and only there; creating an item into an empty slot moves nothing and is allowed.
struct RouteInventory
{
    std::array<ItemFact, EquipmentSlots> equipped{};
    std::vector<std::uint64_t> bags;
    std::map<std::uint8_t, ItemFact> bisTable;  // BiS rows
    std::uint32_t factoryLevel = 60;            // what the factory would equip in every searched slot
    bool coreExecutor = false;
    CoreBatchOutcome outcome = CoreBatchOutcome::Applied;
    std::uint64_t nextGuid = 900;
    int coreStrips = 0;
    int ownedMoves = 0;   // owned items moved; only ever by an Applied Core strip
    int created = 0;      // items created into empty slots
    int gatedRoutes = 0;  // moves refused because the executor is missing

    Snapshot Capture() const
    {
        Snapshot snapshot;
        snapshot.equipped = equipped;
        for (ItemFact const& item : equipped)
            if (item.guid)
                snapshot.carried.push_back(item.guid);
        snapshot.carried.insert(snapshot.carried.end(), bags.begin(), bags.end());
        snapshot.freeBagSlots = 16 - static_cast<std::uint32_t>(bags.size());
        return snapshot;
    }

    StripResult StripToBags(std::uint32_t slotMask)
    {
        if (!MayMoveOwnedItems(coreExecutor))
        {
            ++gatedRoutes;
            return StripResult::Refused;
        }
        ++coreStrips;
        bool const applied = outcome == CoreBatchOutcome::Applied;
        if (applied)
            for (std::uint8_t slot = 0; slot < EquipmentSlots; ++slot)
                if ((slotMask & (1u << slot)) && equipped[slot].guid)
                {
                    bags.push_back(equipped[slot].guid);
                    equipped[slot] = {};
                    ++ownedMoves;
                }
        return ToStripResult(outcome, Observed(!applied, applied));
    }

    void Create(std::uint8_t slot, std::uint32_t entry, std::uint32_t itemLevel)
    {
        equipped[slot] = {nextGuid++, entry, itemLevel};
        ++created;
    }

    // An equipped slot is replaced only after an exact Core strip of that slot; an empty slot is filled.
    void RunFactory(std::uint32_t, std::uint32_t cap, bool incremental)
    {
        std::uint32_t const level = std::min(factoryLevel, cap);
        if (!level)
            return;  // no candidate at all
        for (std::uint8_t slot = 0; slot < 6; ++slot)
        {
            if (!IsCombatSlot(slot))
                continue;
            if (equipped[slot].guid)
            {
                if (!incremental || level <= equipped[slot].itemLevel || StripToBags(1u << slot) != StripResult::Done)
                    continue;
            }
            Create(slot, 700u + slot, level);
        }
    }

    bool ApplyBisOverlay(std::uint32_t cap)
    {
        for (auto const& [slot, row] : bisTable)
        {
            if (row.itemLevel > cap || equipped[slot].entry == row.entry)
                continue;
            if (equipped[slot].guid)
            {
                StripResult const strip = StripToBags(1u << slot);
                if (strip == StripResult::RecoveryRequired)
                    return false;
                if (strip != StripResult::Done)
                    continue;
            }
            Create(slot, row.entry, row.itemLevel);
        }
        return true;
    }

    void ApplyFinishers(std::uint32_t) {}
    bool DestroyCreatedEquipped(std::uint8_t, std::uint64_t) { return false; }
};

Plan MakePlan(bool reset, Population population, CommandForm form = CommandForm::Plain)
{
    PlanInput input;
    input.capVerified = true;
    input.cap = 78;
    input.reset = reset;
    input.form = form;
    input.populationKnown = true;
    input.population = population;
    input.sourcesAvailable = true;
    return PlanExecution(input);
}

void TestStripRouteResults()
{
    struct Case
    {
        CoreBatchOutcome outcome;
        bool ran;
        Refusal refusal;
        char const* name;
    };
    Case const cases[] = {
        {CoreBatchOutcome::Applied, true, Refusal::None, "reset, Applied: gearing runs"},
        {CoreBatchOutcome::Refused, false, Refusal::StripRefused, "reset, Refused: nothing changed"},
        {CoreBatchOutcome::Restored, false, Refusal::StripRestored, "reset, Restored: put back (F5 kept)"},
        {CoreBatchOutcome::RecoveryRequired, false, Refusal::RecoveryRequired, "reset, RecoveryRequired"},
    };
    for (Case const& c : cases)
    {
        RouteInventory inventory;
        inventory.coreExecutor = true;
        inventory.outcome = c.outcome;
        inventory.equipped[0] = {101, 1, 90};
        inventory.equipped[4] = {102, 2, 90};
        Outcome const outcome = Execute(inventory, MakePlan(true, Population::RandomBot));
        Check(outcome.ran == c.ran && outcome.refusal == c.refusal, c.name);
        Check(outcome.Succeeded() == (c.outcome == CoreBatchOutcome::Applied),
              std::string(c.name) + ": success only after Applied");
        Check(c.ran || (inventory.equipped[0].guid == 101 && inventory.created == 0 && inventory.ownedMoves == 0),
              std::string(c.name) + ": nothing moved or created unless Applied");
    }
}

// CS01 7133329461: each route that would move an owned item refuses that move without the executor;
// routes that move nothing keep working.
void TestRoutesWithoutExecutor()
{
    // Strip route: a random bot wearing gear above the cap.
    {
        RouteInventory inventory;
        inventory.equipped[0] = {101, 1, 90};
        Outcome const outcome = Execute(inventory, MakePlan(false, Population::RandomBot));
        Check(!outcome.ran && outcome.refusal == Refusal::StripRefused && inventory.gatedRoutes == 1 &&
                  inventory.ownedMoves == 0 && inventory.created == 0,
              "strip route without executor: refused, nothing moved or created");
    }
    // Zero-strip incremental: nothing above the cap, one slot worth upgrading, one empty slot.
    {
        RouteInventory inventory;
        inventory.equipped[0] = {101, 1, 50};
        Outcome const outcome = Execute(inventory, MakePlan(false, Population::RandomBot));
        Check(outcome.ran && outcome.refusal == Refusal::None, "zero-strip incremental runs without executor");
        Check(inventory.equipped[0].guid == 101 && inventory.ownedMoves == 0 && inventory.gatedRoutes == 1,
              "zero-strip incremental: the equipped item is not replaced (its move is gated)");
        Check(inventory.equipped[1].guid && inventory.created == 4,
              "zero-strip incremental: empty slots are still filled (no move)");
    }
    // Alt bot, plain: never stripped; the same replacement gate applies.
    {
        RouteInventory inventory;
        inventory.equipped[0] = {101, 1, 90};
        inventory.equipped[1] = {102, 2, 50};
        Outcome const outcome = Execute(inventory, MakePlan(false, Population::AltBot));
        Check(outcome.ran && inventory.equipped[0].guid == 101 && inventory.equipped[1].guid == 102 &&
                  inventory.ownedMoves == 0,
              "alt bot without executor: its gear stays, nothing moved");
    }
    // Empty reset: a random bot wearing nothing in combat slots; nothing to strip.
    {
        RouteInventory inventory;
        Outcome const outcome = Execute(inventory, MakePlan(true, Population::RandomBot));
        Check(outcome.ran && outcome.Succeeded() && inventory.gatedRoutes == 0 && inventory.created == 5 &&
                  inventory.ownedMoves == 0,
              "empty reset without executor: no strip needed, empty slots filled");
    }
    // One-slot BiS replacement: the equipped slot is gated and keeps its item; the empty slot gets its row.
    {
        RouteInventory inventory;
        inventory.factoryLevel = 0;  // the factory finds nothing; only BiS acts
        inventory.equipped[0] = {101, 1, 50};
        inventory.bisTable[0] = {0, 777, 70};
        inventory.bisTable[1] = {0, 778, 70};
        Outcome const outcome = Execute(inventory, MakePlan(false, Population::RandomBot, CommandForm::Bis));
        Check(outcome.ran && inventory.equipped[0].guid == 101 && inventory.equipped[1].entry == 778,
              "BiS without executor: equipped slot kept, empty slot filled");
        Check(inventory.ownedMoves == 0 && inventory.gatedRoutes == 1, "BiS without executor: the swap is gated");
    }
}

// With the executor the same routes move owned items, and only through the Core strip.
void TestRoutesWithExecutor()
{
    {
        RouteInventory inventory;
        inventory.coreExecutor = true;
        inventory.equipped[0] = {101, 1, 50};
        Outcome const outcome = Execute(inventory, MakePlan(false, Population::RandomBot));
        Check(outcome.ran && inventory.equipped[0].itemLevel == 60 && inventory.coreStrips == 1 &&
                  inventory.ownedMoves == 1,
              "zero-strip incremental with executor: the upgrade goes through one Core strip");
    }
    {
        RouteInventory inventory;
        inventory.coreExecutor = true;
        inventory.factoryLevel = 0;
        inventory.equipped[0] = {101, 1, 50};
        inventory.bisTable[0] = {0, 777, 70};
        Outcome const outcome = Execute(inventory, MakePlan(false, Population::RandomBot, CommandForm::Bis));
        Check(outcome.ran && inventory.equipped[0].entry == 777 && inventory.coreStrips == 1,
              "BiS with executor: the swap goes through one Core strip");
    }
    {
        RouteInventory inventory;
        inventory.coreExecutor = true;
        inventory.outcome = CoreBatchOutcome::RecoveryRequired;
        inventory.factoryLevel = 0;
        inventory.equipped[0] = {101, 1, 50};
        inventory.bisTable[0] = {0, 777, 70};
        Outcome const outcome = Execute(inventory, MakePlan(false, Population::RandomBot, CommandForm::Bis));
        Check(outcome.ran && outcome.refusal == Refusal::RecoveryRequired && !outcome.Succeeded(),
              "BiS with executor, Core recovery: the run stops as a recovery");
    }
}

void TestPathSelection()
{
    Check(SelectStripPath(true) == StripPath::CoreBatch && MayMoveOwnedItems(true), "with the executor: Core path");
    Check(SelectStripPath(false) == StripPath::Unavailable && !MayMoveOwnedItems(false),
          "without it: no path that moves owned items");
}

void TestDiagnostic()
{
    std::uint64_t const missing[] = {101, 103};
    std::string const line = Diagnostic(CoreBatchOutcome::Applied, Observed(false, true), missing);
    Check(line == "core batch outcome=Applied unchanged=0 atDestination=1 strip=RecoveryRequired missing=101,103",
          "Applied with missing items is logged as recovery, with the items: " + line);
    Check(Diagnostic(CoreBatchOutcome::Restored, Observed(false, false), {}) ==
              "core batch outcome=Restored unchanged=0 atDestination=0 strip=RecoveryRequired",
          "Restored over a changed inventory is logged as recovery");
    Check(Diagnostic(CoreBatchOutcome::Refused, Observed(true, false), {}).find("strip=Refused") != std::string::npos,
          "a clean refusal is logged as refused");
}
}  // namespace

int main()
{
    TestMapping();
    TestStripRouteResults();
    TestRoutesWithoutExecutor();
    TestRoutesWithExecutor();
    TestPathSelection();
    TestDiagnostic();
    std::cout << "checks=" << g_checks << " failures=" << g_failures << "\n";
    return g_failures == 0 ? 0 : 1;
}
