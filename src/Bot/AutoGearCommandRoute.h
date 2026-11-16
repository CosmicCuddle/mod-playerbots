/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_COMMAND_ROUTE_H
#define PLAYERBOTS_AUTOGEAR_COMMAND_ROUTE_H

#include "AutoGearCommandParse.h"
#include "AutoGearRndbotTierSync.h"
#include "AutoGearSelectionPlan.h"

namespace PlayerbotsAutoGearSelection
{
    // What AutoGearAction::Execute and BisGearAction::Execute should call.
    // The flags stay off, so this does not run from the live command yet.
    // TrainerAction.cpp is coordinate-first and is not edited here.
    struct RouteRequest
    {
        bool commandEnabled = false;
        bool ipCapEnabled = false;
        bool altCommandAllowed = true;
        bool bis = false;
        bool tierSync = false;
        std::string_view text{};
        Population population = Population::Unknown;
        bool senderIsLeveling = false;
        bool firstStageAfterLeveling = false;
        std::uint8_t rawStage = 0;
        std::uint8_t effectiveStage = 0;
        // Playerbots.AutoGearScoreLimit. Zero does not invent a ceiling.
        std::uint32_t scoreLimit = 0;
        std::uint32_t qualityLimit = 3;
        std::span<std::uint32_t const> occupiedSlots{};
        bool senderFixedKnown = false;
        std::uint32_t senderFixedAverage = 0;
        PlanInput selection{};
    };

    struct RouteDecision
    {
        bool ranNewPath = false;
        bool legacyUnchanged = false;
        bool rejected = false;
        bool preserve = true;
        bool selectionRan = false;
        bool writesProgression = false;
        bool deletesGear = false;
        GearAction action = GearAction::Preserve;
        std::uint32_t cap = 0;
        CapMode mode = CapMode::Endgame;
        CommandForm form = CommandForm::Plain;
        Plan plan{};
    };

    inline void FillApprovedProgression(RouteRequest const& request, CapInput& cap)
    {
        cap.playerContext = true;
        cap.progressionContext = true;
        cap.rawStage = request.rawStage;
        cap.effectiveStage = request.effectiveStage;
        cap.policy = ApprovedCapPolicy();
        cap.scoreLimit = request.scoreLimit;
    }

    // Occupied combat slots. A zero entry is an empty slot and is not counted.
    inline std::uint32_t OccupiedMasterAverage(RouteRequest const& request)
    {
        return OccupiedAverage(request.occupiedSlots);
    }

    template <class Scorer>
    RouteDecision ResolveAutoGearRoute(RouteRequest const& request, Scorer const& scorer)
    {
        RouteDecision decision;
        decision.writesProgression = false;
        decision.deletesGear = false;
        if (request.tierSync)
        {
            if (!AllowsPopulationTierSync(request.population))
            {
                decision.rejected = true;
                return decision;
            }
            CapInput cap;
            FillApprovedProgression(request, cap);
            cap.mode = CapMode::Endgame;
            CapResult const resolved = ComposeCap(cap);
            decision.ranNewPath = true;
            decision.preserve = resolved.preserve;
            decision.cap = resolved.cap;
            decision.mode = CapMode::Endgame;
            decision.action = resolved.preserve ? GearAction::Preserve : GearAction::CommitSet;
            return decision;
        }
        if (!request.commandEnabled)
        {
            decision.rejected = true;
            return decision;
        }
        if (request.population == Population::AltBot && !request.altCommandAllowed)
        {
            decision.rejected = true;
            return decision;
        }
        if (!request.ipCapEnabled)
        {
            decision.legacyUnchanged = true;
            decision.preserve = false;
            decision.cap = request.scoreLimit;
            return decision;
        }

        bool const postLevel = request.firstStageAfterLeveling && !request.senderIsLeveling;
        std::uint32_t const occupied = OccupiedMasterAverage(request);
        ParseRequest parsed;
        parsed.featureOn = true;
        parsed.bis = request.bis;
        parsed.text = request.text;
        parsed.qualityLimit = request.qualityLimit;
        parsed.scoreLimit = request.scoreLimit;
        parsed.progression.playerContext = true;
        parsed.progression.progressionContext = true;
        FillApprovedProgression(request, parsed.progression);
        parsed.evidence.masterPresent = true;
        parsed.evidence.masterOccupied = request.occupiedSlots;
        CommandForm const preview = FormFor(false, ArgKind::None, request.bis);
        parsed.requestedMode = ModeForForm(preview, request.senderIsLeveling, request.firstStageAfterLeveling);
        if (postLevel && !request.bis)
        {
            parsed.progression.senderSampleKnown = occupied != 0;
            parsed.progression.senderAverage = occupied;
        }
        else if (request.senderIsLeveling)
        {
            parsed.progression.senderSampleKnown = request.senderFixedKnown;
            parsed.progression.senderAverage = request.senderFixedAverage;
        }
        CommandDecision const command = ParseCommand(parsed);
        decision.form = FormFor(command.reset, command.argument, request.bis);
        decision.mode = ModeForForm(decision.form, request.senderIsLeveling, request.firstStageAfterLeveling);
        if (postLevel && decision.form != CommandForm::Match && decision.form != CommandForm::Bis &&
            decision.form != CommandForm::BisNumeric)
        {
            decision.mode = CapMode::Leveling;
            parsed.progression.senderSampleKnown = occupied != 0;
            parsed.progression.senderAverage = occupied;
        }
        parsed.requestedMode = decision.mode;
        CommandDecision const resolved = ParseCommand(parsed);
        decision.ranNewPath = true;
        decision.rejected = resolved.rejected;
        if (resolved.rejected)
        {
            decision.preserve = true;
            return decision;
        }
        decision.preserve = resolved.preserve;
        decision.cap = resolved.cap.cap;
        decision.mode = parsed.requestedMode;
        if (resolved.preserve || request.selection.facts.empty())
        {
            decision.action = GearAction::Preserve;
            return decision;
        }
        PlanInput selection = request.selection;
        selection.cap = resolved.cap;
        decision.plan = SelectFromFacts(selection, scorer);
        decision.selectionRan = true;
        decision.action = decision.plan.outcome.action;
        decision.preserve = decision.action == GearAction::Preserve;
        decision.deletesGear = decision.plan.storage.deletesRetainedGear;
        return decision;
    }
}

#endif
