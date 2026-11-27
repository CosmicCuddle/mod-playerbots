// Adversarial inventory request cases (CS01 Part 41 Task 4, coordination #80 7198695947).
//
// Feeds BuildMoves forged or stale slot facts against Core88's real PB59InventoryBatchPlan.h, and checks
// what Playerbots believes when a competing actor changes the inventory around a Core result. Every
// forged request must be refused before Core would act; every changed inventory must end as a refusal
// or a recovery, never as Done. Persistence, shutdown and durable-receipt cases have no interface yet
// and are printed as NOT EXECUTED / BLOCKED.
//
// Build (one line): g++ -std=c++20 -Isrc/Bot -Isrc -I<core>/src/server/game/Entities/Player
//                   -I<core>/src/common tests/pb70_autogear_adversarial_request.cpp

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "AutoGearCoreBatchRequest.h"

namespace
{
using namespace PlayerbotsAutoGearCoreBatch;
using PlayerbotsAutoGearExec::StripResult;

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

constexpr std::uint64_t Owner = 0x2A;
constexpr std::uint16_t Equip(std::uint8_t slot) { return static_cast<std::uint16_t>(0xFF00u | slot); }
constexpr std::uint16_t Backpack(std::uint8_t slot) { return static_cast<std::uint16_t>(0xFF00u | (23 + slot)); }

PB59::InventorySlotFacts Holding(std::uint16_t position, std::uint64_t guid, std::uint64_t owner = Owner,
                                 std::uint32_t count = 1)
{
    PB59::InventorySlotFacts facts;
    facts.owner = Owner;
    facts.position = position;
    PB59::InventoryInstanceFacts item;
    item.guid = guid;
    item.owner = owner;
    item.entry = 1000;
    item.count = count;
    facts.item = item;
    return facts;
}

PB59::InventorySlotFacts Empty(std::uint16_t position, std::uint64_t slotOwner = Owner)
{
    PB59::InventorySlotFacts facts;
    facts.owner = slotOwner;
    facts.position = position;
    return facts;
}

struct Request
{
    std::vector<PlannedMove> plan;
    std::vector<PB59::InventorySlotFacts> sources;
    std::vector<PB59::InventorySlotFacts> destinations;
};

// An honest two-move strip: main hand and chest to the first two backpack slots.
Request Honest()
{
    Request r;
    r.plan = {{101, Equip(15), Backpack(0), 1, 0}, {102, Equip(4), Backpack(1), 1, 0}};
    r.sources = {Holding(Equip(15), 101), Holding(Equip(4), 102)};
    r.destinations = {Empty(Backpack(0)), Empty(Backpack(1))};
    return r;
}

Built Build(Request const& r, std::uint64_t owner = Owner)
{
    return BuildMoves(owner, r.plan, r.sources, r.destinations);
}

void TestForgedRequests()
{
    Check(Build(Honest()).error == BuildError::None, "honest request builds");

    struct Case
    {
        char const* label;
        void (*forge)(Request&);
    } const cases[] = {
        {"wrong item GUID in the source slot", [](Request& r) { r.sources[0].item->guid = 999; }},
        {"source slot moved since planning", [](Request& r) { r.sources[0].position = Equip(16); }},
        {"stack count changed", [](Request& r) { r.sources[1].item->count = 2; }},
        {"destination now occupied", [](Request& r) { r.destinations[0] = Holding(Backpack(0), 555); }},
        {"destination in another bag", [](Request& r) { r.destinations[1].bag = 77; }},
        {"source slot emptied", [](Request& r) { r.sources[0].item.reset(); }},
        {"observation lists shorter than the plan", [](Request& r) { r.sources.pop_back(); }},
        {"item owned by another character", [](Request& r) { r.sources[0].item->owner = 0x99; }},
        {"slot of another character", [](Request& r) { r.sources[1].owner = 0x99; }},
        {"duplicate item GUID",
         [](Request& r)
         {
             r.plan[1].itemGuid = 101;
             r.sources[1].item->guid = 101;
         }},
        {"two moves into one destination",
         [](Request& r)
         {
             r.plan[1].destination = Backpack(0);
             r.destinations[1].position = Backpack(0);
         }},
        {"source equals destination",
         [](Request& r)
         {
             r.plan[0].destination = Equip(15);
             r.destinations[0] = Empty(Equip(15));
         }},
        {"empty plan",
         [](Request& r)
         {
             r.plan.clear();
             r.sources.clear();
             r.destinations.clear();
         }},
    };
    for (Case const& c : cases)
    {
        Request r = Honest();
        c.forge(r);
        Built const built = Build(r);
        Check(built.error != BuildError::None && built.moves.empty(), std::string("refused: ") + c.label);
    }
    Check(Build(Honest(), 0).error != BuildError::None, "refused: no owner");
    Check(Build(Honest(), 0x99).error != BuildError::None, "refused: request for another owner");

    // Twenty moves exceed Core's batch limit of 19.
    Request big;
    for (std::uint8_t i = 0; i < 20; ++i)
    {
        big.plan.push_back({500u + i, Equip(i % 19), Backpack(i), 1, 0});
        big.sources.push_back(Holding(Equip(i % 19), 500u + i));
        big.destinations.push_back(Empty(Backpack(i)));
    }
    Check(Build(big).error != BuildError::None, "refused: more moves than Core allows");
}

void TestCompetingActor()
{
    // Another actor changes the inventory while Core runs; Playerbots' observation disagrees with Core.
    CoreBatchObservation changed;  // fingerprint differs, items not at destination
    Check(ToStripResult(CoreBatchOutcome::Refused, changed) == StripResult::RecoveryRequired,
          "Core refused but the inventory changed: recovery");
    Check(ToStripResult(CoreBatchOutcome::Restored, changed) == StripResult::RecoveryRequired,
          "Core restored but the inventory changed: recovery");
    Check(ToStripResult(CoreBatchOutcome::Applied, changed) == StripResult::RecoveryRequired,
          "Core applied but an item is not at its destination: recovery");

    // Replayed or duplicated completion: a second Applied for the same plan finds the items already moved,
    // so the sources are empty and the fingerprint has changed; nothing is believed twice.
    CoreBatchObservation replay;
    replay.everyMoveAtDestination = true;
    replay.missing = 1;
    Check(ToStripResult(CoreBatchOutcome::Applied, replay) == StripResult::RecoveryRequired,
          "duplicate completion with a reported missing item: recovery");
    Request again = Honest();
    again.sources[0].item.reset();  // the first run already moved it
    again.sources[1].item.reset();
    Check(Build(again).error != BuildError::None, "replaying an applied plan is refused before Core");
}

void NotExecuted()
{
    char const* const blocked[] = {
        "unexpected worldserver shutdown during a batch (native fixture + Core recovery persistence)",
        "database readback failure after a batch (Core114+ readback holder, owned by CS01)",
        "durable per-command receipt and nonce (no interface in R1 + PB68)",
        "duplicate account/character at fixture setup (Python fixture tests cover the refusal)",
    };
    for (char const* item : blocked)
        std::cout << "NOT EXECUTED / BLOCKED: " << item << "\n";
}
}  // namespace

int main()
{
    TestForgedRequests();
    TestCompetingActor();
    NotExecuted();
    std::cout << "checks=" << g_checks << " failures=" << g_failures << "\n";
    return g_failures == 0 ? 0 : 1;
}
