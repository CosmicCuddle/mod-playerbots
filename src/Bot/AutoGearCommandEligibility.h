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

    // Every known form calls ComposeCap. Match does not require the sender
    // average. Leveling does. The caller says whether the sender is still
    // leveling; this function does not infer that from level 60.
    constexpr CapMode ModeForForm(CommandForm form, bool senderIsLeveling)
    {
        if (form == CommandForm::Match)
            return CapMode::Match;
        return senderIsLeveling ? CapMode::Leveling : CapMode::Endgame;
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
