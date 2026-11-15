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

    // Every chat form uses ComposeCap. The form does not change the math.
    // Callers set a typed item level only for Numeric and BisNumeric, and
    // they pass the fixed-denominator sender average for every form,
    // including Match. Quality stays outside this cap.
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

    constexpr CapResult ResolveCommandEligibility(CommandForm form, CapInput const& in)
    {
        if (!KnownCommandForm(form))
            return {};
        return ComposeCap(in);
    }
}

#endif
