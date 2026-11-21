// Runtime AutoGear under the commanding player's Individual Progression cap.
// Drives the same ResolveCommand -> ResolveAutoGearRoute -> PlanExecution -> Execute path the
// world-thread command uses, against a fake inventory with failure injection.
// This is a model of the live adapter: it does not prove Worldserver or client gameplay.
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "AutoGearCapCommand.h"
#include "AutoGearMasterSourceIndex.h"
#include "AutoGearSlotPolicy.h"

namespace
{
using namespace PlayerbotsAutoGearExec;
using PlayerbotsAutoGearSelection::CommandForm;

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

// Candidate the fake factory can create for every combat slot.
struct Candidate
{
    std::uint32_t entry;
    std::uint32_t itemLevel;
};

class FakeInventory
{
public:
    std::array<ItemFact, EquipmentSlots> equipped{};
    std::vector<ItemFact> bags;
    std::uint32_t bagCapacity = 16;
    std::vector<Candidate> catalog;              // factory candidates, any slot
    std::map<std::uint8_t, Candidate> bisTable;  // BiS rows
    std::uint64_t nextGuid = 1000;

    // Failure injection.
    int failMoveAfter = -1;  // the Nth MoveEquippedToBags fails (0-based)
    bool factoryIgnoresCap = false;
    bool failDestroy = false;
    bool failBisEquip = false;

    // Observations.
    int moves = 0;
    int factoryRuns = 0;
    bool lastIncremental = true;
    std::uint32_t lastFactoryCap = 0;
    int bisRuns = 0;
    int finisherRuns = 0;
    int destroyed = 0;
    int originalsDestroyed = 0;
    std::vector<std::uint64_t> originals;

    void Equip(std::uint8_t slot, std::uint32_t entry, std::uint32_t itemLevel)
    {
        equipped[slot] = {nextGuid++, entry, itemLevel};
        originals.push_back(equipped[slot].guid);
    }

    Snapshot Capture() const
    {
        Snapshot snapshot;
        snapshot.equipped = equipped;
        for (ItemFact const& item : equipped)
            if (item.guid)
                snapshot.carried.push_back(item.guid);
        for (ItemFact const& item : bags)
            snapshot.carried.push_back(item.guid);
        snapshot.freeBagSlots = bagCapacity - static_cast<std::uint32_t>(bags.size());
        return snapshot;
    }

    bool MoveEquippedToBags(std::uint8_t slot)
    {
        int const index = moves++;
        if (!equipped[slot].guid)
            return true;
        if (index == failMoveAfter || bags.size() >= bagCapacity)
            return false;
        bags.push_back(equipped[slot]);
        equipped[slot] = {};
        return true;
    }

    void RunFactory(std::uint32_t /*quality*/, std::uint32_t cap, bool incremental)
    {
        ++factoryRuns;
        lastIncremental = incremental;
        lastFactoryCap = cap;
        for (std::uint8_t slot = 0; slot < EquipmentSlots; ++slot)
        {
            if (!IsCombatSlot(slot))
                continue;
            Candidate const* best = nullptr;
            for (Candidate const& candidate : catalog)
                if ((factoryIgnoresCap || candidate.itemLevel <= cap) &&
                    (!best || candidate.itemLevel > best->itemLevel))
                    best = &candidate;
            if (!best)
                continue;
            if (equipped[slot].guid)
            {
                if (!incremental || best->itemLevel <= equipped[slot].itemLevel)
                    continue;
                if (!MoveEquippedToBags(slot))
                    continue;
            }
            equipped[slot] = {nextGuid++, best->entry, best->itemLevel};
        }
    }

    void ApplyBisOverlay(std::uint32_t cap)
    {
        ++bisRuns;
        for (auto const& [slot, candidate] : bisTable)
        {
            if (candidate.itemLevel > cap || equipped[slot].entry == candidate.entry)
                continue;
            ItemFact const previous = equipped[slot];
            if (previous.guid && !MoveEquippedToBags(slot))
                continue;
            if (failBisEquip)
            {
                // Restore the previous item from the bags.
                for (auto it = bags.begin(); it != bags.end(); ++it)
                    if (it->guid == previous.guid)
                    {
                        equipped[slot] = *it;
                        bags.erase(it);
                        break;
                    }
                continue;
            }
            equipped[slot] = {nextGuid++, candidate.entry, candidate.itemLevel};
        }
    }

    void ApplyFinishers(std::uint32_t /*cap*/) { ++finisherRuns; }

    bool DestroyCreatedEquipped(std::uint8_t slot, std::uint64_t guid)
    {
        if (failDestroy || equipped[slot].guid != guid)
            return false;
        for (std::uint64_t original : originals)
            if (original == guid)
                ++originalsDestroyed;
        equipped[slot] = {};
        ++destroyed;
        return true;
    }

    std::uint32_t MaxEquippedNewItemLevel() const
    {
        std::uint32_t maximum = 0;
        for (ItemFact const& item : equipped)
        {
            bool original = false;
            for (std::uint64_t guid : originals)
                original = original || guid == item.guid;
            if (item.guid && !original)
                maximum = std::max(maximum, item.itemLevel);
        }
        return maximum;
    }

    std::size_t OwnedOriginalsStillCarried() const
    {
        std::size_t count = 0;
        for (std::uint64_t guid : originals)
        {
            bool found = false;
            for (ItemFact const& item : equipped)
                found = found || item.guid == guid;
            for (ItemFact const& item : bags)
                found = found || item.guid == guid;
            count += found ? 1 : 0;
        }
        return count;
    }

    bool Untouched() const { return moves == 0 && factoryRuns == 0 && bisRuns == 0 && finisherRuns == 0; }
};

FakeInventory StandardInventory()
{
    FakeInventory inventory;
    inventory.catalog = {{100, 50}, {101, 66}, {102, 67}, {103, 78}, {104, 79}, {105, 92}, {106, 200}};
    return inventory;
}

CommandFacts MaxLevelSender(std::uint8_t stage, std::uint32_t slotLevel = 90)
{
    CommandFacts facts;
    facts.commandEnabled = true;
    facts.altCommandAllowed = true;
    facts.qualityLimit = 4;
    facts.randomBot = true;
    facts.randomAccount = true;
    facts.progressionKnown = true;
    facts.rawStage = stage;
    facts.effectiveStage = stage;
    facts.senderLevel = 60;
    facts.senderSlotsComplete = true;
    facts.senderIsMaster = true;
    for (std::uint8_t slot = 0; slot < EquipmentSlots; ++slot)
        if (IsCombatSlot(slot))
            facts.senderSlots[slot] = slotLevel;
    return facts;
}

struct Run
{
    ResolvedCommand resolved;
    Outcome outcome;
};

Run Execute(CommandFacts const& facts, FakeInventory& inventory)
{
    Run run;
    run.resolved = ResolveCommand(facts, Searched);
    run.outcome = PlayerbotsAutoGearExec::Execute(inventory, run.resolved.plan);
    return run;
}

void TestStageCaps()
{
    // Stage 0 sender at level 60 uses the first-stage-after-leveling average; a high sample keeps 66.
    {
        FakeInventory inventory = StandardInventory();
        CommandFacts facts = MaxLevelSender(0);
        Run const run = Execute(facts, inventory);
        Check(run.outcome.ran && run.resolved.plan.cap == 66, "stage 0 plain autogear runs at cap 66");
        Check(inventory.MaxEquippedNewItemLevel() == 66, "stage 0 equips 66, never 67");
        Check(run.outcome.capHeld && run.outcome.revoked == 0, "stage 0 cap held without revocation");
    }
    // Stage 1 after Ragnaros: 78, never 79.
    {
        FakeInventory inventory = StandardInventory();
        CommandFacts facts = MaxLevelSender(1);
        Run const run = Execute(facts, inventory);
        Check(run.outcome.ran && run.resolved.plan.cap == 78, "stage 1 autogear runs at cap 78");
        Check(inventory.MaxEquippedNewItemLevel() == 78, "stage 1 equips 78, never 79");
    }
}

void TestCommandForms()
{
    struct FormCase
    {
        bool bis;
        char const* argument;
        CommandForm form;
        bool runs;
        std::uint32_t cap;
        char const* name;
    };
    // Stage 1 (cap 78), sender average 90, random bot.
    FormCase const cases[] = {
        {false, "", CommandForm::Plain, true, 78, "autogear"},
        {false, "reset", CommandForm::Reset, true, 78, "autogear reset"},
        {false, "match", CommandForm::Match, true, 78, "autogear match (master avg 90 > cap)"},
        {false, "blue", CommandForm::Quality, true, 78, "autogear blue"},
        {false, "70", CommandForm::Numeric, true, 70, "autogear 70 (below cap)"},
        {false, "200", CommandForm::Numeric, true, 78, "autogear 200 clamps to cap"},
        {false, "reset 200", CommandForm::Reset, true, 78, "autogear reset 200 clamps to cap"},
        {true, "", CommandForm::Bis, true, 78, "autogear bis"},
        {true, "70", CommandForm::BisNumeric, true, 70, "autogear bis 70"},
        {true, "300", CommandForm::BisNumeric, true, 78, "autogear bis 300 clamps to cap"},
        {false, "3", CommandForm::Plain, false, 0, "autogear 3 is quality-like and rejected"},
        {false, "nonsense", CommandForm::Plain, false, 0, "garbage argument rejected"},
        {false, "-5", CommandForm::Plain, false, 0, "negative argument rejected"},
        {false, "0", CommandForm::Plain, false, 0, "typed zero rejected"},
    };
    for (FormCase const& c : cases)
    {
        FakeInventory inventory = StandardInventory();
        CommandFacts facts = MaxLevelSender(1);
        facts.bis = c.bis;
        facts.argument = c.argument;
        Run const run = Execute(facts, inventory);
        Check(run.outcome.ran == c.runs, std::string(c.name) + ": runs as expected");
        if (!c.runs)
        {
            Check(inventory.Untouched(), std::string(c.name) + ": inventory untouched");
            continue;
        }
        Check(run.resolved.decision.form == c.form, std::string(c.name) + ": form");
        Check(run.resolved.plan.cap == c.cap, std::string(c.name) + ": cap");
        Check(inventory.lastFactoryCap == c.cap, std::string(c.name) + ": factory search bounded by cap");
        Check(inventory.MaxEquippedNewItemLevel() <= c.cap, std::string(c.name) + ": no new item above cap");
        Check(run.resolved.plan.bisOverlay == c.bis, std::string(c.name) + ": BiS overlay only for BiS");
        Check(inventory.originalsDestroyed == 0, std::string(c.name) + ": no original destroyed");
    }
}

void TestConfigCapInteraction()
{
    struct ScoreCase
    {
        std::uint8_t stage;
        std::uint32_t scoreLimit;
        std::uint32_t expected;
    };
    ScoreCase const cases[] = {{0, 0, 66},  {0, 60, 60}, {0, 66, 66}, {0, 200, 66},
                               {1, 70, 70}, {1, 78, 78}, {1, 79, 78}};
    for (ScoreCase const& c : cases)
    {
        FakeInventory inventory = StandardInventory();
        CommandFacts facts = MaxLevelSender(c.stage);
        facts.scoreLimit = c.scoreLimit;
        Run const run = Execute(facts, inventory);
        Check(run.outcome.ran && run.resolved.plan.cap == c.expected,
              "stage " + std::to_string(c.stage) + " score limit " + std::to_string(c.scoreLimit) +
                  " gives the stricter cap " + std::to_string(c.expected));
    }
}

void TestFailClosed()
{
    // Unknown IP context (no provider, two providers, no IP module) never means unlimited.
    {
        FakeInventory inventory = StandardInventory();
        CommandFacts facts = MaxLevelSender(1);
        facts.progressionKnown = false;
        Run const run = Execute(facts, inventory);
        Check(!run.outcome.ran && run.outcome.refusal == Refusal::NotVerified && inventory.Untouched(),
              "missing IP progression preserves gear");
    }
    // Stage 11 is disabled in IP and has no cap.
    {
        FakeInventory inventory = StandardInventory();
        CommandFacts facts = MaxLevelSender(11);
        facts.senderLevel = 70;
        Run const run = Execute(facts, inventory);
        Check(!run.outcome.ran && inventory.Untouched(), "disabled stage 11 preserves gear");
    }
    // Stage above the supported table.
    {
        FakeInventory inventory = StandardInventory();
        CommandFacts facts = MaxLevelSender(19);
        Run const run = Execute(facts, inventory);
        Check(!run.outcome.ran && inventory.Untouched(), "unsupported stage 19 preserves gear");
    }
    // Effective stage above raw stage is inconsistent.
    {
        FakeInventory inventory = StandardInventory();
        CommandFacts facts = MaxLevelSender(1);
        facts.effectiveStage = 2;
        Run const run = Execute(facts, inventory);
        Check(!run.outcome.ran && inventory.Untouched(), "effective stage above raw preserves gear");
    }
    // Sender equipment sample incomplete: leveling needs it, so preserve.
    {
        FakeInventory inventory = StandardInventory();
        CommandFacts facts = MaxLevelSender(0);
        facts.senderLevel = 40;
        facts.senderSlotsComplete = false;
        Run const run = Execute(facts, inventory);
        Check(!run.outcome.ran && inventory.Untouched(), "leveling sender without a sample preserves gear");
    }
    // Command disabled in config.
    {
        FakeInventory inventory = StandardInventory();
        CommandFacts facts = MaxLevelSender(1);
        facts.commandEnabled = false;
        Run const run = Execute(facts, inventory);
        Check(!run.outcome.ran && run.outcome.refusal == Refusal::Rejected && inventory.Untouched(),
              "disabled command rejected");
    }
    // BiS needs epic quality limit and a PvE spec.
    {
        FakeInventory inventory = StandardInventory();
        CommandFacts facts = MaxLevelSender(1);
        facts.bis = true;
        facts.qualityLimit = 3;
        Check(!Execute(facts, inventory).outcome.ran && inventory.Untouched(), "BiS below epic quality rejected");
        facts.qualityLimit = 4;
        facts.bisPvpSpec = true;
        Check(!Execute(facts, inventory).outcome.ran && inventory.Untouched(), "BiS for a PvP spec rejected");
    }
}

void TestLevelingSender()
{
    // Level 40 commander at stage 0: cap is the fixed-denominator average of counted slots.
    FakeInventory inventory = StandardInventory();
    CommandFacts facts = MaxLevelSender(0, 0);
    facts.senderLevel = 40;
    std::uint32_t counted = 0;
    for (std::uint8_t slot = 0; slot < EquipmentSlots; ++slot)
        if (IsCombatSlot(slot) && Searched(40, slot))
            ++counted;
    // Fill only half of the counted slots at 60: empty slots count as zero.
    std::uint32_t filled = 0;
    for (std::uint8_t slot = 0; slot < EquipmentSlots && filled < counted / 2; ++slot)
        if (IsCombatSlot(slot) && Searched(40, slot))
        {
            facts.senderSlots[slot] = 60;
            ++filled;
        }
    std::uint32_t const expected = 60 * filled / counted;
    Run const run = Execute(facts, inventory);
    Check(run.outcome.ran && run.resolved.plan.cap == expected,
          "leveling cap is the fixed average " + std::to_string(expected) + ", empty slots lower it");
    Check(inventory.MaxEquippedNewItemLevel() <= expected, "leveling bot gets nothing above the fixed average");
}

void TestPopulation()
{
    // Alt bot: allowed when configured, but never stripped and reset is refused.
    {
        FakeInventory inventory = StandardInventory();
        inventory.Equip(0, 900, 92);  // alt's own over-cap helm
        CommandFacts facts = MaxLevelSender(1);
        facts.randomBot = false;
        facts.randomAccount = false;
        Run const run = Execute(facts, inventory);
        Check(run.outcome.ran && run.outcome.stripped == 0 && run.outcome.retainedOverCap == 1,
              "alt bot keeps its own over-cap item");
        Check(inventory.equipped[0].entry == 900, "alt bot helm untouched");
    }
    {
        FakeInventory inventory = StandardInventory();
        inventory.Equip(0, 900, 60);
        CommandFacts facts = MaxLevelSender(1);
        facts.randomBot = false;
        facts.randomAccount = false;
        facts.argument = "reset";
        Run const run = Execute(facts, inventory);
        Check(!run.outcome.ran && run.outcome.refusal == Refusal::ResetNotAllowed && inventory.Untouched(),
              "alt bot reset refused without touching gear");
    }
    {
        FakeInventory inventory = StandardInventory();
        CommandFacts facts = MaxLevelSender(1);
        facts.randomBot = false;
        facts.randomAccount = false;
        facts.altCommandAllowed = false;
        Run const run = Execute(facts, inventory);
        Check(!run.outcome.ran && run.outcome.refusal == Refusal::Rejected && inventory.Untouched(),
              "alt bot refused when AutoGearCommandAltBots is off");
    }
    // Random bot on a non-random account (or the reverse) is unknown.
    {
        FakeInventory inventory = StandardInventory();
        CommandFacts facts = MaxLevelSender(1);
        facts.randomAccount = false;
        Run const run = Execute(facts, inventory);
        Check(!run.outcome.ran && inventory.Untouched(), "mismatched random bot/account preserves gear");
    }
}

void TestRandomBotStrip()
{
    // Over-cap original goes to bags; factory fills the slot within the cap; nothing destroyed.
    {
        FakeInventory inventory = StandardInventory();
        inventory.Equip(0, 900, 92);
        inventory.Equip(4, 901, 60);
        Run const run = Execute(MaxLevelSender(0), inventory);
        Check(run.outcome.ran && run.outcome.stripped == 1, "random bot over-cap helm stripped");
        Check(inventory.equipped[0].itemLevel == 66, "helm slot refilled at cap 66");
        Check(inventory.OwnedOriginalsStillCarried() == 2 && inventory.originalsDestroyed == 0,
              "every original still owned");
    }
    // No legal candidate: the over-cap slot stays visibly empty.
    {
        FakeInventory inventory = StandardInventory();
        inventory.catalog = {{105, 92}};
        inventory.Equip(0, 900, 92);
        Run const run = Execute(MaxLevelSender(0), inventory);
        Check(run.outcome.ran && !inventory.equipped[0].guid, "no legal item leaves the random bot unequipped");
        Check(inventory.OwnedOriginalsStillCarried() == 1, "stripped helm is in the bags");
    }
    // Reset strips every combat item first, then gears non-incrementally.
    {
        FakeInventory inventory = StandardInventory();
        inventory.Equip(0, 900, 60);
        inventory.Equip(4, 901, 60);
        inventory.Equip(SlotBody, 902, 1);
        CommandFacts facts = MaxLevelSender(1);
        facts.argument = "reset";
        Run const run = Execute(facts, inventory);
        Check(run.outcome.ran && run.outcome.stripped == 2 && !inventory.lastIncremental,
              "reset strips combat slots and gears from scratch");
        Check(inventory.equipped[SlotBody].entry == 902, "shirt is never stripped");
        Check(inventory.OwnedOriginalsStillCarried() == 3, "reset destroys nothing");
    }
}

void TestFailureInjection()
{
    // Bags too full for the planned strip: nothing changes.
    {
        FakeInventory inventory = StandardInventory();
        inventory.Equip(0, 900, 60);
        inventory.Equip(4, 901, 60);
        inventory.bagCapacity = 1;
        CommandFacts facts = MaxLevelSender(1);
        facts.argument = "reset";
        Run const run = Execute(facts, inventory);
        Check(!run.outcome.ran && run.outcome.refusal == Refusal::BagsFull, "full bags refuse reset");
        Check(inventory.Untouched() && inventory.equipped[0].entry == 900, "full bags leave gear equipped");
    }
    // A move fails part way: moved items stay in bags, the factory never runs.
    {
        FakeInventory inventory = StandardInventory();
        inventory.Equip(0, 900, 60);
        inventory.Equip(4, 901, 60);
        inventory.Equip(6, 903, 60);
        inventory.failMoveAfter = 1;
        CommandFacts facts = MaxLevelSender(1);
        facts.argument = "reset";
        Run const run = Execute(facts, inventory);
        Check(!run.outcome.ran && run.outcome.refusal == Refusal::StripFailed && run.outcome.stripped == 1,
              "mid-strip failure is classified, not reported as rollback");
        Check(inventory.factoryRuns == 0, "no gearing after a failed strip");
        Check(inventory.OwnedOriginalsStillCarried() == 3, "mid-strip failure loses nothing");
    }
    // Factory creates an over-cap item: it is revoked, originals untouched.
    {
        FakeInventory inventory = StandardInventory();
        inventory.factoryIgnoresCap = true;
        inventory.Equip(4, 901, 60);
        Run const run = Execute(MaxLevelSender(0), inventory);
        Check(run.outcome.ran && run.outcome.revoked > 0 && run.outcome.capHeld,
              "over-cap item created by the command is revoked");
        Check(inventory.MaxEquippedNewItemLevel() <= 66 && inventory.originalsDestroyed == 0,
              "revocation only touches created items");
    }
    // Revocation itself fails: reported as a broken cap, never as success.
    {
        FakeInventory inventory = StandardInventory();
        inventory.factoryIgnoresCap = true;
        inventory.failDestroy = true;
        Run const run = Execute(MaxLevelSender(0), inventory);
        Check(run.outcome.ran && !run.outcome.capHeld, "failed revocation reports the cap as broken");
    }
    // BiS equip fails: the previous item goes back on.
    {
        FakeInventory inventory = StandardInventory();
        inventory.catalog = {{101, 50}};
        inventory.bisTable[0] = {777, 70};
        inventory.failBisEquip = true;
        CommandFacts facts = MaxLevelSender(1);
        facts.bis = true;
        Run const run = Execute(facts, inventory);
        Check(run.outcome.ran && inventory.equipped[0].entry == 101, "failed BiS equip restores the factory pick");
    }
    // BiS row above the cap is skipped.
    {
        FakeInventory inventory = StandardInventory();
        inventory.catalog = {{101, 50}};
        inventory.bisTable[0] = {777, 79};
        inventory.bisTable[4] = {778, 78};
        CommandFacts facts = MaxLevelSender(1);
        facts.bis = true;
        Run const run = Execute(facts, inventory);
        Check(run.outcome.ran && inventory.equipped[0].entry == 101 && inventory.equipped[4].entry == 778,
              "BiS rows above the cap never equip");
    }
}

void TestStageTransition()
{
    // Same random bot: stage 0 gear, then the commander kills Ragnaros.
    FakeInventory inventory = StandardInventory();
    inventory.bagCapacity = 64;
    Run const first = Execute(MaxLevelSender(0), inventory);
    Check(first.outcome.ran && inventory.MaxEquippedNewItemLevel() == 66, "stage 0 gearing");
    Run const second = Execute(MaxLevelSender(1), inventory);
    Check(second.outcome.ran && inventory.MaxEquippedNewItemLevel() == 78, "stage 1 upgrades to 78");
    // A lower commander stage strips the over-cap items from this random bot into its bags.
    Run const third = Execute(MaxLevelSender(0), inventory);
    Check(third.outcome.ran && third.outcome.stripped > 0, "lower commander stage strips over-cap random bot gear");
    for (std::uint8_t slot = 0; slot < EquipmentSlots; ++slot)
        Check(!IsCombatSlot(slot) || inventory.equipped[slot].itemLevel <= 66,
              "slot " + std::to_string(slot) + " is within the stage 0 cap");

    // The same downgrade with bags already full of the replaced gear preserves instead.
    FakeInventory full = StandardInventory();
    Execute(MaxLevelSender(0), full);
    Execute(MaxLevelSender(1), full);
    Run const blocked = Execute(MaxLevelSender(0), full);
    Check(!blocked.outcome.ran && blocked.outcome.refusal == Refusal::BagsFull,
          "downgrade strip with full bags is refused, gear stays");
}

// Owner rules for a max-level commander (18 Nov 2026): Vanilla stages 1-7 cap normal forms at
// min(equipped average, stage cap); TBC/Wrath also bound the average by the worn raid tier, which is
// the era's lowest tier without verified item sources. BiS keeps the stage cap.
void TestMaxLevelAverageRule()
{
    struct Case
    {
        std::uint8_t stage;
        std::uint32_t level;
        std::uint32_t average;
        bool bis;
        std::uint32_t expected;
        char const* name;
    };
    Case const cases[] = {
        {1, 60, 70, false, 70, "stage 1, average 70: capped at 70, not 78"},
        {1, 60, 90, false, 78, "stage 1, average 90: stage cap 78"},
        {3, 60, 80, false, 80, "stage 3, average 80 below cap 83"},
        {7, 60, 95, false, 92, "stage 7, average 95: stage cap 92"},
        {1, 60, 70, true, 78, "stage 1 BiS keeps the stage cap"},
        {10, 70, 150, false, 125, "TBC stage 10 without item sources: T4 floor 125, not 141"},
        {10, 70, 110, false, 110, "TBC stage 10, average 110 below the floor"},
        {13, 80, 240, false, 213, "Wrath stage 13 without item sources: T7 10-man floor 213, not 226"},
    };
    for (Case const& c : cases)
    {
        FakeInventory inventory = StandardInventory();
        CommandFacts facts = MaxLevelSender(c.stage, c.average);
        facts.senderLevel = c.level;
        facts.bis = c.bis;
        Run const run = Execute(facts, inventory);
        Check(run.outcome.ran && run.resolved.plan.cap == c.expected,
              std::string(c.name) + " (got " + std::to_string(run.resolved.plan.cap) + ")");
    }
    // The average needs a verified sample; without one the command preserves.
    FakeInventory inventory = StandardInventory();
    CommandFacts facts = MaxLevelSender(1, 70);
    facts.senderSlotsComplete = false;
    Run const run = Execute(facts, inventory);
    Check(!run.outcome.ran && inventory.Untouched(), "stage 1 max-level commander without a sample preserves");
}

// C1-B: a G7 source index built from world rows, bound to the commander's exact capture, lifts a
// TBC/Wrath max-level cap from the era floor to the raid tier the commander really wears.
void TestSourceIndexTierLift()
{
    namespace G7 = PlayerbotsAutoGearMasterSource;
    // Boss 9100 in Serpentshrine (map 548, normal) drops three items: Tier 5, 25-man.
    std::vector<G7::CreatureTemplateRow> const creatures = {{9100, {}, 9100}};
    std::vector<G7::SpawnRow> const spawns = {{9100, 548, 1}};
    std::vector<G7::LootRow> const loot = {{9100, 30001, 0}, {9100, 30002, 0}, {9100, 30003, 0}};
    G7::WorldRows rows;
    rows.creatures = creatures;
    rows.creatureSpawns = spawns;
    rows.creatureLoot = loot;
    G7::SourceIndex const index = G7::SourceIndex::Build(rows);
    Check(index.RaidEntries() == 3, "three Serpentshrine drops are raid-sourced");

    // Level-70 commander at stage 10 (cap 141): every combat slot is item level 140, three of them
    // the Serpentshrine pieces.
    G7::CapturedEquipment capture;
    capture.guid = 77;
    capture.session = 5;
    capture.revision = 9;
    std::uint32_t raidEntry = 30001;
    for (std::uint8_t slot = 0; slot < EquipmentSlots; ++slot)
    {
        if (!IsCombatSlot(slot))
            continue;
        std::uint32_t const entry = raidEntry <= 30003 ? raidEntry++ : 40000 + slot;
        capture.slots[slot] = {1000u + slot, entry, 140, 4, 0, 0};
    }
    auto resolve = [&](G7::BoundMasterSources const& bound, bool useFacts)
    {
        FakeInventory inventory = StandardInventory();
        CommandFacts facts = MaxLevelSender(10, 140);
        facts.senderLevel = 70;
        if (useFacts)
        {
            facts.tierSnapshot = bound.sample.Snapshot();
            facts.tierFacts = bound.Facts();
        }
        return Execute(facts, inventory);
    };
    G7::BoundMasterSources const bound = G7::BindMasterSources(index, capture);
    Check(bound.Bound(), "the capture binds to the index");
    Run const lifted = resolve(bound, true);
    Check(lifted.outcome.ran && lifted.resolved.plan.cap == 140,
          "bound Tier 5 pieces lift the cap to min(140 average, 141 tier) (got " +
              std::to_string(lifted.resolved.plan.cap) + ")");
    Check(resolve(bound, false).resolved.plan.cap == 125, "without facts the cap stays at the T4 floor 125");

    // A later capture (the commander changed gear) makes the old facts stale: no lift.
    G7::CapturedEquipment later = capture;
    later.revision = 10;
    G7::BoundMasterSources const stale = bound;
    FakeInventory inventory = StandardInventory();
    CommandFacts facts = MaxLevelSender(10, 140);
    facts.senderLevel = 70;
    facts.tierSnapshot = bound.sample.Snapshot();
    facts.tierFacts = stale.FreshFacts(later);
    Check(Execute(facts, inventory).resolved.plan.cap == 125, "stale capture facts give no lift");

    // Only one raid piece is not enough evidence of a worn tier.
    G7::CapturedEquipment single = capture;
    for (std::uint8_t slot = 0; slot < EquipmentSlots; ++slot)
        if (single.slots[slot].entry == 30002 || single.slots[slot].entry == 30003)
            single.slots[slot].entry = 41000 + slot;
    G7::BoundMasterSources const one = G7::BindMasterSources(index, single);
    Check(resolve(one, true).resolved.plan.cap == 125, "a single raid piece stays at the floor");

    // An index that was never built binds nothing.
    G7::SourceIndex const empty;
    Check(!G7::BindMasterSources(empty, capture).Bound(), "an unbuilt index never binds");
}

// The planner is the last boundary before mutation; check it without the route in front.
void TestPlannerDirect()
{
    PlanInput input;
    input.capVerified = true;
    input.cap = 78;
    input.populationKnown = true;
    input.population = Population::RandomBot;
    Check(PlanExecution(input).Runs(), "verified cap and random bot runs");
    input.cap = 0;
    Check(!PlanExecution(input).Runs(), "a verified zero cap is never unlimited");
    input.cap = 78;
    input.capVerified = false;
    Check(!PlanExecution(input).Runs(), "unverified cap preserves");
    input.capVerified = true;
    for (Population population : {Population::Unknown, Population::AddClass})
    {
        input.population = population;
        Check(PlanExecution(input).refusal == Refusal::UnknownPopulation, "unknown/AddClass population preserves");
    }
    input.population = Population::RandomBot;
    input.populationKnown = false;
    Check(PlanExecution(input).refusal == Refusal::UnknownPopulation, "unproven population preserves");
    input.populationKnown = true;
    input.routeRejected = true;
    Check(PlanExecution(input).refusal == Refusal::Rejected, "route rejection wins");
    FakeInventory inventory = StandardInventory();
    Check(!PlayerbotsAutoGearExec::Execute(inventory, PlanExecution(input)).ran && inventory.Untouched(),
          "a refused plan never touches the inventory");
}

// Playerbots.AutoGearIPCap.* config can only tighten the approved table.
void TestConfigTables()
{
    auto withConfig = [](std::uint8_t stage, std::array<std::uint32_t, 19> caps, std::array<std::uint32_t, 3> eras)
    {
        CommandFacts facts = MaxLevelSender(stage);
        facts.useConfigCaps = true;
        facts.configCompletionCaps = caps;
        facts.configEraCeilings = eras;
        return facts;
    };
    std::array<std::uint32_t, 19> const dist = {66,  78, 78,  83,  83,  83,  88,  92,  125, 125,
                                                141, 0,  156, 226, 226, 245, 258, 284, 284};
    std::array<std::uint32_t, 3> const distEras = {92, 164, 284};
    // conf.dist values match the approved table: stage 0 and 1 unchanged.
    {
        FakeInventory inventory = StandardInventory();
        Run const run = Execute(withConfig(1, dist, distEras), inventory);
        Check(run.outcome.ran && run.resolved.plan.cap == 78, "conf.dist table keeps stage 1 at 78");
        Check(run.resolved.request.policy == nullptr, "resolved request holds no pointer to the local policy");
    }
    // Lower config wins.
    {
        std::array<std::uint32_t, 19> lower = dist;
        lower[0] = 60;
        FakeInventory inventory = StandardInventory();
        Run const run = Execute(withConfig(0, lower, distEras), inventory);
        Check(run.outcome.ran && run.resolved.plan.cap == 60, "lower configured stage cap applies");
    }
    // Higher config never raises.
    {
        std::array<std::uint32_t, 19> higher = dist;
        higher[0] = 200;
        FakeInventory inventory = StandardInventory();
        Run const run = Execute(withConfig(0, higher, {200, 200, 300}), inventory);
        Check(run.outcome.ran && run.resolved.plan.cap == 66, "higher configured cap cannot raise 66");
    }
    // The C++ fallback when the owner's config lacks the key: only stages 0 and 1 are set.
    {
        std::array<std::uint32_t, 19> fallback{66, 78};
        FakeInventory inventory = StandardInventory();
        Check(Execute(withConfig(1, fallback, {78, 0, 0}), inventory).outcome.ran, "fallback table still runs stage 1");
        FakeInventory later = StandardInventory();
        Run const run = Execute(withConfig(2, fallback, {78, 0, 0}), later);
        Check(!run.outcome.ran && later.Untouched(), "fallback table refuses unconfirmed stage 2");
    }
    // A zero era ceiling refuses that era.
    {
        FakeInventory inventory = StandardInventory();
        CommandFacts facts = withConfig(9, dist, {92, 0, 284});
        facts.senderLevel = 70;
        Run const run = Execute(facts, inventory);
        Check(!run.outcome.ran && inventory.Untouched(), "zero TBC era ceiling refuses TBC stages");
    }
    Check(Tighter(66, 0) == 0 && Tighter(0, 66) == 0 && Tighter(66, 60) == 60 && Tighter(66, 70) == 66,
          "Tighter is min with zero refusing");
}

void TestBagAccounting()
{
    // Strip followed by incremental replacement never needs more bag slots than were checked.
    FakeInventory inventory = StandardInventory();
    for (std::uint8_t slot = 0; slot < EquipmentSlots; ++slot)
        if (IsCombatSlot(slot))
            inventory.Equip(slot, 800 + slot, 92);
    inventory.bagCapacity = 17;
    Run const run = Execute(MaxLevelSender(1), inventory);
    Check(run.outcome.ran && run.outcome.stripped == 17, "all 17 over-cap combat items stripped");
    Check(inventory.OwnedOriginalsStillCarried() == 17, "all 17 originals are in the bags");
}
}  // namespace

int main()
{
    TestStageCaps();
    TestCommandForms();
    TestConfigCapInteraction();
    TestFailClosed();
    TestLevelingSender();
    TestPopulation();
    TestRandomBotStrip();
    TestFailureInjection();
    TestStageTransition();
    TestBagAccounting();
    TestPlannerDirect();
    TestMaxLevelAverageRule();
    TestSourceIndexTierLift();
    TestConfigTables();
    std::cout << "checks=" << g_checks << " failures=" << g_failures << "\n";
    return g_failures == 0 ? 0 : 1;
}
