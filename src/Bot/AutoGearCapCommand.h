/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_CAP_COMMAND_H
#define PLAYERBOTS_AUTOGEAR_CAP_COMMAND_H

#include <array>
#include <string_view>

#include "AutoGearCappedExecution.h"
#include "AutoGearCommandRoute.h"
#include "AutoGearMasterTier.h"

// Turns the facts the world thread read from the bot and the commanding player into one
// execution plan. Every command form goes through this single function.
namespace PlayerbotsAutoGearExec
{
struct CommandFacts
{
    bool bis = false;
    std::string_view argument{};
    bool commandEnabled = false;     // AutoGearCommand or AutoGearBisCommand
    bool bisPvpSpec = false;         // BiS is PvE only
    bool altCommandAllowed = false;  // AutoGearCommandAltBots
    std::uint32_t qualityLimit = 0;  // AutoGearQualityLimit
    std::uint32_t scoreLimit = 0;    // AutoGearScoreLimit, 0 = no extra limit
    bool randomBot = false;
    bool randomAccount = false;
    bool progressionKnown = false;  // exactly one IP provider answered for the commander
    std::uint8_t rawStage = 0;
    std::uint8_t effectiveStage = 0;
    std::uint32_t senderLevel = 0;
    bool senderSlotsComplete = false;
    std::array<std::uint32_t, EquipmentSlots> senderSlots{};
    bool senderIsMaster = false;
    // Playerbots.AutoGearIPCap.* tables. They can only lower the approved caps.
    bool useConfigCaps = false;
    std::array<std::uint32_t, 19> configCompletionCaps{};
    std::array<std::uint32_t, 3> configEraCeilings{};
};

// Lower of approved and configured; zero in either refuses that stage or era.
constexpr std::uint32_t Tighter(std::uint32_t approved, std::uint32_t configured)
{
    return (approved == 0 || configured == 0) ? 0 : (configured < approved ? configured : approved);
}

struct ResolvedCommand
{
    PlayerbotsAutoGearSelection::RouteRequest request;
    PlayerbotsAutoGearSelection::RouteDecision decision;
    Plan plan;
};

template <class SearchAtLevel>
ResolvedCommand ResolveCommand(CommandFacts const& facts, SearchAtLevel const& searched)
{
    using namespace PlayerbotsAutoGearSelection;
    ResolvedCommand resolved;
    RouteRequest& request = resolved.request;
    request.ipCapEnabled = true;
    request.bis = facts.bis;
    request.text = facts.argument;
    request.altCommandAllowed = facts.altCommandAllowed;
    request.qualityLimit = facts.qualityLimit;
    request.scoreLimit = facts.scoreLimit;
    request.commandEnabled = facts.commandEnabled && !(facts.bis && facts.bisPvpSpec);
    // A random bot on a non-random account, or the reverse, is not a population we can name.
    request.populationKnown = facts.randomBot == facts.randomAccount;
    request.population = facts.randomBot ? Population::RandomBot : Population::AltBot;
    request.playerContextVerified = true;
    request.progressionContextVerified = facts.progressionKnown;
    request.rawStage = facts.progressionKnown ? facts.rawStage : 0;
    request.effectiveStage = facts.progressionKnown ? facts.effectiveStage : 0;
    request.senderLevelKnown = facts.senderLevel != 0;
    request.senderLevel = facts.senderLevel;
    request.firstStageAfterLeveling = facts.senderLevel == 60 && facts.progressionKnown && facts.rawStage == 0;
    if (facts.senderSlotsComplete)
    {
        request.commandingSenderVerified = true;
        request.commandingSenderSlots = facts.senderSlots;
        request.senderFixedAverage = FixedSenderAverage(facts.senderSlots, facts.senderLevel, searched);
        request.senderFixedKnown = request.senderFixedAverage != 0;
        // Match reads the bot's own master, never an arbitrary authorized sender.
        if (facts.senderIsMaster)
        {
            request.masterSampleVerified = true;
            request.masterSlots = facts.senderSlots;
        }
    }
    std::array<std::uint32_t, 19> completion{};
    PlayerbotsAutoGearCap::Policy policy = ApprovedCapPolicy();
    if (facts.useConfigCaps)
    {
        for (std::size_t stage = 0; stage < completion.size(); ++stage)
            completion[stage] = Tighter(ApprovedVanillaCompletion[stage], facts.configCompletionCaps[stage]);
        policy.completionCaps = completion;
        for (std::size_t era = 0; era < policy.eraCeilings.size(); ++era)
            policy.eraCeilings[era] = Tighter(ApprovedEraCeilings[era], facts.configEraCeilings[era]);
    }
    request.policy = &policy;
    // The set selector is not fed; the route still resolves the verified stage cap. The tiered
    // wrapper applies the owner's average rules: Vanilla stages 1-7 with a max-level commander use
    // min(average, stage cap); TBC/Wrath also bound the average by the worn raid tier, which falls
    // to the era's lowest tier until a verified source list can lift it.
    resolved.decision =
        ResolveTieredAutoGearRoute(request, TierFacts{}, [](std::uint32_t, std::int32_t, std::uint32_t) { return 0u; });
    request.policy = nullptr;  // the local policy does not outlive this call

    PlanInput input;
    input.routeRejected = resolved.decision.rejected;
    input.capVerified = resolved.decision.capVerified;
    input.cap = resolved.decision.cap;
    input.form = resolved.decision.form;
    input.reset = resolved.decision.reset;
    input.quality = resolved.decision.quality;
    input.populationKnown = request.populationKnown;
    input.population = request.population;
    resolved.plan = PlanExecution(input);
    return resolved;
}
}  // namespace PlayerbotsAutoGearExec

#endif
