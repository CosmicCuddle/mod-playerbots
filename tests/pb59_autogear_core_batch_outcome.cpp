// The Core batch executor's four agreed results, mapped to the strip result and run through the real
// executor (PlayerbotsAutoGearExec::Execute) with a fake adapter whose strip returns a scripted Core result.
// Needs no Core headers: the native executor type is not published yet (coordination #52 7132070192).
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "AutoGearCoreBatchRequest.h"

namespace
{
using namespace PlayerbotsAutoGearExec;
using PlayerbotsAutoGearCoreBatch::CoreBatchOutcome;
using PlayerbotsAutoGearCoreBatch::Diagnostic;
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

// A random bot wearing three items above the cap, under a reset command: the executor strips all three.
// The strip goes through the Core path only: it returns the scripted Core result and moves items only when
// Core reports Applied.
struct CoreStripInventory
{
    std::array<ItemFact, EquipmentSlots> equipped{};
    std::vector<std::uint64_t> bags;
    bool coreExecutor = true;
    CoreBatchOutcome outcome = CoreBatchOutcome::Applied;
    bool fingerprintUnchanged = true;
    int strips = 0;
    int factoryRuns = 0;
    int legacyMoves = 0;  // moves made outside Core: must stay zero

    CoreStripInventory()
    {
        equipped[0] = {101, 1, 90};
        equipped[4] = {102, 2, 90};
        equipped[16] = {103, 3, 90};
    }

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
        ++strips;
        if (SelectStripPath(coreExecutor) == StripPath::Unavailable)
            return StripResult::Refused;
        StripResult const result = ToStripResult(outcome, fingerprintUnchanged);
        if (outcome == CoreBatchOutcome::Applied)
            for (std::uint8_t slot = 0; slot < EquipmentSlots; ++slot)
                if ((slotMask & (1u << slot)) && equipped[slot].guid)
                {
                    bags.push_back(equipped[slot].guid);
                    equipped[slot] = {};
                }
        return result;
    }

    void RunFactory(std::uint32_t, std::uint32_t, bool) { ++factoryRuns; }
    bool ApplyBisOverlay(std::uint32_t) { return true; }
    void ApplyFinishers(std::uint32_t) {}
    bool DestroyCreatedEquipped(std::uint8_t, std::uint64_t) { return false; }
};

Plan ResetPlan()
{
    PlanInput input;
    input.capVerified = true;
    input.cap = 78;
    input.reset = true;
    input.populationKnown = true;
    input.population = Population::RandomBot;
    input.sourcesAvailable = true;
    return PlanExecution(input);
}

void TestMapping()
{
    for (bool unchanged : {true, false})
    {
        std::string const tag = unchanged ? " (fingerprint unchanged)" : " (fingerprint changed)";
        Check(ToStripResult(CoreBatchOutcome::Applied, unchanged) == StripResult::Done, "Applied -> Done" + tag);
        Check(ToStripResult(CoreBatchOutcome::Restored, unchanged) == StripResult::Restored,
              "Restored -> Restored" + tag);
        Check(ToStripResult(CoreBatchOutcome::RecoveryRequired, unchanged) == StripResult::RecoveryRequired,
              "RecoveryRequired -> RecoveryRequired" + tag);
    }
    Check(ToStripResult(CoreBatchOutcome::Refused, true) == StripResult::Refused,
          "Refused with nothing changed -> Refused");
    Check(ToStripResult(CoreBatchOutcome::Refused, false) == StripResult::RecoveryRequired,
          "Refused while something changed -> RecoveryRequired");
    Check(ToStripResult(static_cast<CoreBatchOutcome>(7), true) == StripResult::RecoveryRequired,
          "an unknown result fails closed to recovery");
    for (CoreBatchOutcome outcome : AllOutcomes)
        Check(ToStripResult(outcome, true) != StripResult::NoSpace,
              std::string(PlayerbotsAutoGearCoreBatch::OutcomeName(outcome)) + " never claims no space");
}

// Through the real executor: what the command reports for each Core result.
void TestThroughExecutor()
{
    struct Case
    {
        CoreBatchOutcome outcome;
        bool unchanged;
        bool ran;
        Refusal refusal;
        char const* name;
    };
    Case const cases[] = {
        {CoreBatchOutcome::Applied, true, true, Refusal::None, "Applied: gearing runs"},
        {CoreBatchOutcome::Refused, true, false, Refusal::StripRefused, "Refused: nothing changed"},
        {CoreBatchOutcome::Refused, false, false, Refusal::RecoveryRequired, "Refused after a change: recovery"},
        // Grok F5: Core proved everything back even if no move had taken effect; still "put back".
        {CoreBatchOutcome::Restored, true, false, Refusal::StripRestored, "Restored: put back (F5 kept)"},
        {CoreBatchOutcome::RecoveryRequired, true, false, Refusal::RecoveryRequired, "RecoveryRequired: recovery"},
    };
    for (Case const& c : cases)
    {
        CoreStripInventory inventory;
        inventory.outcome = c.outcome;
        inventory.fingerprintUnchanged = c.unchanged;
        Outcome const outcome = Execute(inventory, ResetPlan());
        Check(outcome.ran == c.ran && outcome.refusal == c.refusal, c.name);
        Check(inventory.strips == 1 && inventory.legacyMoves == 0, std::string(c.name) + ": one Core strip only");
        Check(inventory.factoryRuns == (c.ran ? 1 : 0), std::string(c.name) + ": gearing only after Applied");
        Check(outcome.Succeeded() == (c.outcome == CoreBatchOutcome::Applied),
              std::string(c.name) + ": success only after Applied");
        if (!c.ran)
            Check(inventory.equipped[0].guid == 101 && inventory.equipped[16].guid == 103 && inventory.bags.empty(),
                  std::string(c.name) + ": gear left in place");
    }
}

// No fallback: without Core's executor the capped strip refuses and nothing moves.
void TestNoFallback()
{
    Check(SelectStripPath(true) == StripPath::CoreBatch, "with the executor the Core path is used");
    Check(SelectStripPath(false) == StripPath::Unavailable, "without it there is no strip path");
    CoreStripInventory inventory;
    inventory.coreExecutor = false;
    Outcome const outcome = Execute(inventory, ResetPlan());
    Check(!outcome.ran && outcome.refusal == Refusal::StripRefused && inventory.factoryRuns == 0 &&
              inventory.legacyMoves == 0 && inventory.bags.empty(),
          "missing executor: refused, nothing moved, no gearing");
}

void TestDiagnostic()
{
    std::uint64_t const missing[] = {101, 103};
    std::string const line = Diagnostic(CoreBatchOutcome::RecoveryRequired, true, missing);
    Check(line == "core batch outcome=RecoveryRequired fingerprintUnchanged=1 strip=RecoveryRequired missing=101,103",
          "recovery diagnostic names the missing items: " + line);
    Check(Diagnostic(CoreBatchOutcome::Refused, false, {}) ==
              "core batch outcome=Refused fingerprintUnchanged=0 strip=RecoveryRequired",
          "refusal after a change is logged as recovery");
    Check(Diagnostic(CoreBatchOutcome::Restored, true, {}).find("strip=Restored") != std::string::npos,
          "restored is logged as restored");
}
}  // namespace

int main()
{
    TestMapping();
    TestThroughExecutor();
    TestNoFallback();
    TestDiagnostic();
    std::cout << "checks=" << g_checks << " failures=" << g_failures << "\n";
    return g_failures == 0 ? 0 : 1;
}
