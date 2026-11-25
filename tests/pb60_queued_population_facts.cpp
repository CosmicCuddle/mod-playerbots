// Queued AutoGear population facts: the exact identity lines of RunPB59AutoGearWorldPlan
// (extracted by run_pb60_queued_population_facts.py into facts.inc) feed the real
// ResolveCommand -> ResolveAutoGearRoute -> PlanExecution. Mocked identity calls only;
// this does not prove Worldserver, RandomPlayerbotMgr or account-list behaviour.
#include <array>
#include <cstdint>
#include <iostream>
#include <set>
#include <string>
#include <string_view>

#include "AutoGearCapCommand.h"
#include "AutoGearSlotPolicy.h"

using uint8 = std::uint8_t;
using uint32 = std::uint32_t;
using int32 = std::int32_t;

struct Session { uint32 account = 0; uint32 GetAccountId() const { return account; } };
struct Guid { uint32 counter = 0; uint32 GetCounter() const { return counter; } };
struct Player
{
    Guid guid; Session* session = nullptr; uint8 cls = 1;
    Guid GetGUID() const { return guid; }
    Session* GetSession() const { return session; }
    uint8 getClass() const { return cls; }
};
struct Config
{
    int32 AutoGearCommand = 1, AutoGearCommandAltBots = 1, AutoGearQualityLimit = 4, AutoGearScoreLimit = 0;
    int32 AutoGearBisCommand = 1;
    std::array<uint32, 19> AutoGearIPCompletionCaps{};
    std::array<uint32, 3> AutoGearIPEraCeilings{};
    std::set<uint32> randomAccounts;
    uint32 lastAccountQueried = 0;
    bool IsInRandomAccountList(uint32 id) { lastAccountQueried = id; return randomAccounts.count(id) != 0; }
} sPlayerbotAIConfig;
struct Mgr
{
    std::set<uint32> randomBots;
    int isRandomBotCalls = 0;
    bool IsRandomBot(Player* bot) { ++isRandomBotCalls; return bot && randomBots.count(bot->GetGUID().GetCounter()) != 0; }
    bool IsSpecPvp(uint32, uint8) { return false; }
} sRandomPlayerbotMgr;

PlayerbotsAutoGearExec::CommandFacts ReadFacts(Player* bot, bool bis, std::string_view argument)
{
#include "facts.inc"
    return facts;
}

namespace
{
int g_checks = 0, g_failures = 0;
void Check(bool ok, std::string const& what)
{
    ++g_checks;
    if (!ok) { ++g_failures; std::cerr << "FAIL: " << what << "\n"; }
}
constexpr PlayerbotsAutoGearSlots::Layout Layout = {0, 1, 3, 4, 6, 7, 10, 11, 12, 13, 15, 16, 17, 18};
auto const Searched = [](std::uint32_t level, std::uint8_t slot) { return PlayerbotsAutoGearSlots::SearchAtLevel(level, slot, Layout); };
}  // namespace

int main()
{
    using namespace PlayerbotsAutoGearExec;
    sPlayerbotAIConfig.AutoGearIPCompletionCaps.fill(0xffffffffu);  // config never lowers the approved caps here
    sPlayerbotAIConfig.AutoGearIPEraCeilings.fill(0xffffffffu);
    for (bool randomBot : {false, true})
    for (bool randomAccount : {false, true})
    for (bool bis : {false, true})
    for (char const* argument : {"", "reset", "70"})
    {
        Session session{randomAccount ? 501u : 9001u};
        Player bot{Guid{77}, &session};
        sPlayerbotAIConfig.randomAccounts = {501u};
        sRandomPlayerbotMgr.randomBots = randomBot ? std::set<uint32>{77u} : std::set<uint32>{};
        sRandomPlayerbotMgr.isRandomBotCalls = 0;
        CommandFacts facts = ReadFacts(&bot, bis, argument);
        std::string const name = std::string(randomBot ? "T" : "F") + "/" + (randomAccount ? "T" : "F") +
                                 (bis ? " bis '" : " '") + argument + "'";
        Check(facts.randomBot == randomBot && facts.randomAccount == randomAccount &&
                  sRandomPlayerbotMgr.isRandomBotCalls == 1 && sPlayerbotAIConfig.lastAccountQueried == session.account,
              name + ": excerpt reads IsRandomBot and the session account");
        // Verified max-level commander at stage 1, every other fact favourable.
        facts.progressionKnown = true;
        facts.rawStage = facts.effectiveStage = 1;
        facts.senderLevel = 60;
        facts.senderSlotsComplete = true;
        facts.senderIsMaster = true;
        facts.sourcesAvailable = true;
        for (std::uint8_t slot = 0; slot < EquipmentSlots; ++slot)
            if (IsCombatSlot(slot))
                facts.senderSlots[slot] = 90;
        ResolvedCommand const resolved = ResolveCommand(facts, Searched);
        if (randomBot && randomAccount)
        {
            if (!bis && std::string_view(argument).empty())
                Check(resolved.plan.Runs(), name + ": confirmed RNDBOT plain command runs");
        }
        else
        {
            Check(!resolved.plan.Runs() && resolved.plan.refusal != Refusal::None && !resolved.plan.strip &&
                      !resolved.plan.stripOverCap && !resolved.plan.bisOverlay,
                  name + ": non-RNDBOT shape never runs");
            if (randomBot == randomAccount)  // alt: rejected by the route itself, not only by the plan
                Check(resolved.decision.rejected && resolved.plan.refusal == Refusal::Rejected,
                      name + ": alt bot rejected at the route");
            else  // either mismatch: population is not nameable, never an alt or RNDBOT run
                Check(!resolved.request.populationKnown &&
                          (resolved.plan.refusal == Refusal::NotVerified ||
                           resolved.plan.refusal == Refusal::UnknownPopulation),
                      name + ": mismatched identity is an unknown population");
        }
    }
    std::cout << "checks=" << g_checks << " failures=" << g_failures << "\n";
    return g_failures ? 1 : 0;
}
