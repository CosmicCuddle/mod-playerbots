/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_COMMAND_ELIGIBILITY_H
#define PLAYERBOTS_AUTOGEAR_COMMAND_ELIGIBILITY_H

#include "AutoGearSelectionCap.h"

namespace PlayerbotsAutoGearSelection
{
    enum class CommandForm : std::uint8_t
    {
        Plain,
        Reset,
        Match,
        Quality,
        Numeric,
        Bis,
        BisNumeric
    };

    // Every known form calls ComposeCap. Match is the occupied-slot average.
    // While leveling, the other forms use that sender average. At the first
    // stage after leveling the caller sets the flag: normal forms keep the
    // average, and both BiS forms use the stage cap. This does not infer
    // level 60 by itself.
    constexpr CapMode ModeForForm(CommandForm form, bool senderIsLeveling, bool firstStageAfterLeveling = false)
    {
        if (form == CommandForm::Match)
            return CapMode::Match;
        bool const bis = form == CommandForm::Bis || form == CommandForm::BisNumeric;
        if (senderIsLeveling)
            return CapMode::Leveling;
        if (firstStageAfterLeveling)
            return bis ? CapMode::Endgame : CapMode::Leveling;
        return CapMode::Endgame;
    }

    // Owner decision 18 Nov 2026 13:06 UK. "Still leveling" follows the
    // commander's IP stage: stages 0-7 below 60, TBC stages 8-12 below 70,
    // Wrath stages 13-18 below 80. Stage 11 is disabled and stays unknown.
    // Unknown level or stage is Unknown, which callers must preserve on.
    enum class SenderPhase : std::uint8_t
    {
        Unknown,
        Leveling,
        MaxLevel
    };

    constexpr std::uint32_t StageMaxLevel(std::uint8_t rawStage)
    {
        if (rawStage < PlayerbotsAutoGearCap::TbcEntryStage)
            return 60;
        if (rawStage == 11)
            return 0;
        if (rawStage < PlayerbotsAutoGearCap::WrathEntryStage)
            return 70;
        if (rawStage <= PlayerbotsAutoGearCap::LastSupportedStage)
            return 80;
        return 0;
    }

    constexpr SenderPhase SenderPhaseForStage(bool levelKnown, std::uint32_t level, bool stageKnown,
        std::uint8_t rawStage)
    {
        if (!levelKnown || level == 0 || !stageKnown)
            return SenderPhase::Unknown;
        std::uint32_t const maxLevel = StageMaxLevel(rawStage);
        if (maxLevel == 0)
            return SenderPhase::Unknown;
        return level < maxLevel ? SenderPhase::Leveling : SenderPhase::MaxLevel;
    }

    constexpr bool KnownCommandForm(CommandForm form)
    {
        switch (form)
        {
            case CommandForm::Plain:
            case CommandForm::Reset:
            case CommandForm::Match:
            case CommandForm::Quality:
            case CommandForm::Numeric:
            case CommandForm::Bis:
            case CommandForm::BisNumeric:
                return true;
        }
        return false;
    }

    constexpr bool KnownCapMode(CapMode mode)
    {
        switch (mode)
        {
            case CapMode::Leveling:
            case CapMode::Endgame:
            case CapMode::Match:
                return true;
        }
        return false;
    }

    // Match, including reset+match, is only CapMode::Match. Every other
    // known form is Leveling or Endgame. An unknown mode preserves.
    constexpr bool FormAllowsMode(CommandForm form, CapMode mode)
    {
        if (!KnownCommandForm(form) || !KnownCapMode(mode))
            return false;
        if (form == CommandForm::Match)
            return mode == CapMode::Match;
        return mode == CapMode::Leveling || mode == CapMode::Endgame;
    }

    constexpr CapResult ResolveCommandEligibility(CommandForm form, CapInput const& in)
    {
        if (!FormAllowsMode(form, in.mode))
            return {};
        return ComposeCap(in);
    }
}

#endif
