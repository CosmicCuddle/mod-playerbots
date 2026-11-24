// Playerbots' exact strip plan against Core's batch-move contract (Core PR74, PB59InventoryBatchPlan.h).
// Compile with the Core Entities/Player directory on the include path. Uses Core's real structural
// check; no native Player, inventory or executor is involved.
#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "AutoGearCoreBatchRequest.h"

namespace
{
using namespace PlayerbotsAutoGearCoreBatch;
using PB59::InventorySlotFacts;

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

constexpr std::uint64_t Owner = 42;
constexpr std::uint16_t Equip(std::uint8_t slot) { return static_cast<std::uint16_t>(0xFF00u | slot); }
constexpr std::uint16_t Backpack(std::uint8_t index) { return static_cast<std::uint16_t>(0xFF00u | (23u + index)); }
constexpr std::uint16_t InBag(std::uint8_t bag, std::uint8_t index)
{
    return static_cast<std::uint16_t>((bag << 8) | index);
}

struct Case
{
    std::vector<PlannedMove> plan;
    std::vector<InventorySlotFacts> sources;
    std::vector<InventorySlotFacts> destinations;
};

// What CapturePB59InventorySlot reports for a planned move right before the batch.
void Add(Case& c, std::uint8_t slot, std::uint16_t destination, std::uint64_t bag = 0, std::uint64_t guid = 0)
{
    PB59::InventoryInstanceFacts item;
    item.guid = guid ? guid : 1000u + slot;
    item.owner = Owner;
    item.entry = 5000u + slot;
    item.count = 1;
    c.plan.push_back({item.guid, Equip(slot), destination, 1, bag});
    c.sources.push_back({Owner, 0, Equip(slot), item});
    c.destinations.push_back({Owner, bag, destination, std::nullopt});
}

Built Build(Case const& c) { return BuildMoves(Owner, c.plan, c.sources, c.destinations); }

void TestPlansPassCore()
{
    // A one-slot BiS strip.
    {
        Case c;
        Add(c, 0, Backpack(0));
        Built const built = Build(c);
        Check(built.error == BuildError::None && built.moves.size() == 1, "one-slot strip passes Core's check");
    }
    // A reset: every combat slot, off-hand first, into backpack and bag slots, as PrepareEquipmentStorage plans.
    {
        Case c;
        Add(c, 16, Backpack(0));
        std::uint8_t next = 1;
        for (std::uint8_t slot = 0; slot < 19; ++slot)
        {
            if (slot == 3 || slot == 18 || slot == 16)
                continue;
            if (next < 16)
                Add(c, slot, Backpack(next++));
            else
                Add(c, slot, InBag(19, static_cast<std::uint8_t>(next++ - 16)), 777);
        }
        Built const built = Build(c);
        Check(built.error == BuildError::None && built.moves.size() == 17, "17-item reset passes Core's check");
        Check(!built.moves.empty() && (built.moves.front().source.position & 255) == 16,
              "the batch keeps the off-hand first");
        bool sameOrder = built.moves.size() == c.plan.size();
        for (std::size_t i = 0; sameOrder && i < c.plan.size(); ++i)
            sameOrder = built.moves[i].source.position == c.plan[i].source &&
                        built.moves[i].destination.position == c.plan[i].destination;
        Check(sameOrder, "the batch keeps the plan order");
    }
}

void TestObservationMismatch()
{
    auto base = []()
    {
        Case c;
        Add(c, 16, Backpack(0));
        Add(c, 15, Backpack(1));
        return c;
    };
    struct Mutation
    {
        char const* name;
        void (*apply)(Case&);
    };
    Mutation const mutations[] = {
        {"source moved", [](Case& c) { c.sources[1].position = Equip(14); }},
        {"different item in the slot", [](Case& c) { c.sources[1].item->guid = 999; }},
        {"source slot empty", [](Case& c) { c.sources[1].item.reset(); }},
        {"count changed", [](Case& c) { c.sources[1].item->count = 2; }},
        {"destination taken", [](Case& c) { c.destinations[1].item = c.sources[0].item; }},
        {"destination position differs", [](Case& c) { c.destinations[1].position = Backpack(5); }},
        {"destination bag replaced", [](Case& c) { c.destinations[1].bag = 778; }},
    };
    for (Mutation const& m : mutations)
    {
        Case c = base();
        m.apply(c);
        Built const built = Build(c);
        Check(built.error == BuildError::Observation && built.moves.empty(), std::string(m.name) + ": refused");
    }
    Case c = base();
    c.destinations.pop_back();
    Check(Build(c).error == BuildError::Shape, "missing destination observation: refused");
    c = base();
    c.sources.pop_back();
    Check(Build(c).error == BuildError::Shape, "missing source observation: refused");
    Check(BuildMoves(Owner, {}, {}, {}).error == BuildError::Shape, "empty plan: refused");
}

void TestCoreRefusals()
{
    // Plans Playerbots never makes, still refused by Core's own check rather than passed through.
    {
        Case c;
        Add(c, 16, Backpack(0));
        Add(c, 15, Backpack(0));
        Built const built = Build(c);
        Check(built.error == BuildError::Core && built.core == PB59::InventoryBatchPlanError::Overlap &&
                  built.moves.empty(),
              "two moves to one slot: Core overlap");
    }
    {
        Case c;
        Add(c, 16, Backpack(0), 0, 77);
        Add(c, 15, Backpack(1), 0, 77);
        Check(Build(c).core == PB59::InventoryBatchPlanError::DuplicateItem, "one item twice: Core duplicate");
    }
    {
        Case c;
        Add(c, 16, Backpack(0));
        c.sources[0].item->owner = Owner + 1;
        Check(Build(c).core == PB59::InventoryBatchPlanError::Source, "item owned by someone else: Core source");
    }
    {
        Case c;
        Add(c, 16, Backpack(0));
        c.destinations[0].owner = Owner + 1;
        Check(Build(c).core == PB59::InventoryBatchPlanError::Owner, "foreign destination: Core owner");
    }
    {
        Case c;
        Add(c, 16, Equip(16));
        c.destinations[0].position = Equip(16);
        Check(Build(c).core == PB59::InventoryBatchPlanError::Overlap, "move onto itself: Core overlap");
    }
    {
        Case c;
        Add(c, 16, Backpack(0));
        Check(BuildMoves(0, c.plan, c.sources, c.destinations).core == PB59::InventoryBatchPlanError::Owner,
              "no owner: Core owner");
    }
}
}  // namespace

int main()
{
    TestPlansPassCore();
    TestObservationMismatch();
    TestCoreRefusals();
    std::cout << "checks=" << g_checks << " failures=" << g_failures << "\n";
    return g_failures == 0 ? 0 : 1;
}
