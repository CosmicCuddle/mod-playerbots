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
        bool populationKnown = false;
        bool playerContextVerified = false;
        bool progressionContextVerified = false;
        bool senderIsLeveling = false;
        bool firstStageAfterLeveling = false;
        // When set, leveling comes from SenderPhaseForStage(senderLevel,
        // rawStage) and senderIsLeveling is ignored. Unknown preserves.
        bool senderLevelKnown = false;
        std::uint32_t senderLevel = 0;
        std::uint8_t rawStage = 0;
        std::uint8_t effectiveStage = 0;
        // Playerbots.AutoGearScoreLimit. Zero does not invent a ceiling.
        std::uint32_t scoreLimit = 0;
        std::uint32_t qualityLimit = 3;
        // Commanding player's occupied combat slots. Normal forms use this.
        bool commandingSenderVerified = false;
        std::span<std::uint32_t const> commandingSenderSlots{};
        // The bot's own master. Match uses this sample, not the sender sample.
        bool masterSampleVerified = false;
        std::span<std::uint32_t const> masterSlots{};
        bool senderFixedKnown = false;
        std::uint32_t senderFixedAverage = 0;
        // Facts were fully enumerated and the required domain is empty.
        bool domainProvenEmpty = false;
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
        bool reset = false;
        std::uint32_t quality = 0;
        GearAction action = GearAction::Preserve;
        std::uint32_t cap = 0;
        CapMode mode = CapMode::Endgame;
        CommandForm form = CommandForm::Plain;
        Plan plan{};
    };

    inline void FillApprovedProgression(RouteRequest const& request, CapInput& cap)
    {
        cap.playerContext = request.playerContextVerified;
        cap.progressionContext = request.progressionContextVerified;
        cap.rawStage = request.rawStage;
        cap.effectiveStage = request.effectiveStage;
        cap.policy = ApprovedCapPolicy();
        cap.scoreLimit = request.scoreLimit;
    }

    inline bool HasSelectedEntry(Plan const& plan)
    {
        for (SelectedPiece const& piece : plan.selected)
        {
            if (piece.entryId != 0)
                return true;
        }
        return false;
    }

    inline void PreserveDecision(RouteDecision& decision)
    {
        decision.action = GearAction::Preserve;
        decision.preserve = true;
        decision.deletesGear = false;
    }

    template <class Scorer>
    RouteDecision ResolveAutoGearRoute(RouteRequest const& request, Scorer const& scorer)
    {
        RouteDecision decision;
        decision.writesProgression = false;
        decision.deletesGear = false;
        if (request.tierSync)
        {
            if (!request.ipCapEnabled)
            {
                PreserveDecision(decision);
                return decision;
            }
            if (!request.populationKnown || !AllowsPopulationTierSync(request.population))
            {
                decision.rejected = true;
                PreserveDecision(decision);
                return decision;
            }
            if (!request.playerContextVerified || !request.progressionContextVerified)
            {
                PreserveDecision(decision);
                return decision;
            }
            CapInput cap;
            FillApprovedProgression(request, cap);
            cap.mode = CapMode::Endgame;
            CapResult const resolved = ComposeCap(cap);
            decision.ranNewPath = true;
            decision.cap = resolved.cap;
            decision.mode = CapMode::Endgame;
            PreserveDecision(decision);
            return decision;
        }
        if (!request.commandEnabled)
        {
            decision.rejected = true;
            PreserveDecision(decision);
            return decision;
        }
        if (!request.populationKnown || request.population == Population::Unknown)
        {
            PreserveDecision(decision);
            return decision;
        }
        if (request.population == Population::AltBot && !request.altCommandAllowed)
        {
            decision.rejected = true;
            PreserveDecision(decision);
            return decision;
        }
        if (!request.ipCapEnabled)
        {
            decision.legacyUnchanged = true;
            decision.preserve = false;
            decision.cap = request.scoreLimit;
            return decision;
        }
        if (!request.playerContextVerified || !request.progressionContextVerified)
        {
            decision.ranNewPath = true;
            PreserveDecision(decision);
            return decision;
        }

        bool senderIsLeveling = request.senderIsLeveling;
        if (request.senderLevelKnown)
        {
            SenderPhase const phase =
                SenderPhaseForStage(true, request.senderLevel, request.progressionContextVerified, request.rawStage);
            if (phase == SenderPhase::Unknown)
            {
                decision.ranNewPath = true;
                PreserveDecision(decision);
                return decision;
            }
            senderIsLeveling = phase == SenderPhase::Leveling;
        }
        bool const postLevel = request.firstStageAfterLeveling && !senderIsLeveling;
        std::uint32_t const senderAverage = request.commandingSenderVerified
            ? OccupiedAverage(request.commandingSenderSlots) : 0;
        ParseRequest parsed;
        parsed.featureOn = true;
        parsed.bis = request.bis;
        parsed.text = request.text;
        parsed.qualityLimit = request.qualityLimit;
        parsed.scoreLimit = request.scoreLimit;
        FillApprovedProgression(request, parsed.progression);
        if (request.masterSampleVerified)
        {
            parsed.evidence.masterPresent = true;
            parsed.evidence.masterOccupied = request.masterSlots;
        }
        CommandForm const preview = FormFor(false, ArgKind::None, request.bis);
        parsed.requestedMode = ModeForForm(preview, senderIsLeveling, request.firstStageAfterLeveling);
        if (senderIsLeveling)
        {
            parsed.progression.senderSampleKnown = request.senderFixedKnown;
            parsed.progression.senderAverage = request.senderFixedAverage;
        }
        CommandDecision const command = ParseCommand(parsed);
        decision.form = FormFor(command.reset, command.argument, request.bis);
        decision.reset = command.reset;
        decision.quality = command.quality;
        decision.mode = ModeForForm(decision.form, senderIsLeveling, request.firstStageAfterLeveling);
        if (postLevel && decision.form != CommandForm::Match && decision.form != CommandForm::Bis &&
            decision.form != CommandForm::BisNumeric)
        {
            decision.mode = CapMode::Leveling;
            parsed.progression.senderSampleKnown = request.commandingSenderVerified && senderAverage != 0;
            parsed.progression.senderAverage = senderAverage;
        }
        parsed.requestedMode = decision.mode;
        CommandDecision const resolved = ParseCommand(parsed);
        decision.ranNewPath = true;
        decision.form = FormFor(resolved.reset, resolved.argument, request.bis);
        decision.reset = resolved.reset;
        decision.quality = resolved.quality;
        decision.rejected = resolved.rejected;
        if (resolved.rejected)
        {
            PreserveDecision(decision);
            return decision;
        }
        decision.cap = resolved.cap.cap;
        decision.mode = parsed.requestedMode;
        if (resolved.preserve || !request.selection.authorityValid)
        {
            PreserveDecision(decision);
            return decision;
        }
        // Moving equipped gear to bags is RNDBOT-only. Alt bots, self bots,
        // AddClass and unknown owners keep their gear on any empty result.
        bool const stripAllowed = request.populationKnown && request.population == Population::RandomBot;
        if (request.selection.facts.empty())
        {
            if (request.domainProvenEmpty && request.selection.requiredMask != 0 && stripAllowed)
            {
                decision.action = GearAction::StripToBags;
                decision.preserve = false;
                decision.deletesGear = false;
                return decision;
            }
            PreserveDecision(decision);
            return decision;
        }
        PlanInput selection = request.selection;
        selection.cap = resolved.cap;
        decision.plan = SelectFromFacts(selection, scorer);
        decision.selectionRan = true;
        decision.action = decision.plan.outcome.action;
        if (decision.action == GearAction::CommitSet && !HasSelectedEntry(decision.plan))
            decision.action = GearAction::Preserve;
        if (decision.action == GearAction::StripToBags && !stripAllowed)
            decision.action = GearAction::Preserve;
        decision.preserve = decision.action == GearAction::Preserve;
        decision.deletesGear = false;
        return decision;
    }
}

#endif
