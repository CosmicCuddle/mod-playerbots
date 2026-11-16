/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_COMMAND_PARSE_H
#define PLAYERBOTS_AUTOGEAR_COMMAND_PARSE_H

#include "AutoGearCommandEligibility.h"
#include <cstdint>
#include <string_view>

namespace PlayerbotsAutoGearSelection
{
    // Current explicit match: GetWornItemLevelAverage on the master.
    // Occupied combat slots only. Body and tabard are already omitted by
    // the caller. Switching source or denominator is opt-in.
    enum class MatchSource : std::uint8_t
    {
        LegacyMaster,
        CommandingSender
    };

    enum class MatchDenominator : std::uint8_t
    {
        OccupiedCombatSlots,
        FixedCountedSlots
    };

    struct MatchPolicy
    {
        MatchSource source = MatchSource::LegacyMaster;
        MatchDenominator denominator = MatchDenominator::OccupiedCombatSlots;
    };

    struct MatchEvidence
    {
        bool masterPresent = false;
        std::span<std::uint32_t const> masterOccupied{};
        bool senderPresent = false;
        std::span<std::uint32_t const> senderOccupied{};
        std::span<std::uint32_t const> senderFixed{};
        std::uint32_t senderFixedDenominator = 0;
    };

    enum class ArgKind : std::uint8_t
    {
        None,
        Quality,
        Match,
        Numeric,
        Garbage
    };

    struct CommandDecision
    {
        bool featureOn = false;
        bool legacyUnchanged = false;
        bool preserve = true;
        bool rejected = false;
        bool reset = false;
        bool bis = false;
        ArgKind argument = ArgKind::None;
        std::uint32_t quality = 0;
        std::uint32_t legacyIlvl = 0;
        CapResult cap{};
    };

    constexpr std::uint32_t QualityLegendary = 5;
    constexpr std::uint32_t QualityEpic = 4;

    constexpr std::string_view Trim(std::string_view text)
    {
        while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
            text.remove_prefix(1);
        while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
            text.remove_suffix(1);
        return text;
    }

    constexpr bool Equals(std::string_view text, char const* word)
    {
        std::size_t n = 0;
        while (word[n] != '\0')
            ++n;
        return text.size() == n && text.compare(0, n, word) == 0;
    }

    // Same words as ChatHelper::itemQualities. 0xFFFFFFFF means not a quality.
    constexpr std::uint32_t ParseQualityWord(std::string_view text)
    {
        if (Equals(text, "poor") || Equals(text, "gray") || Equals(text, "grey"))
            return 0;
        if (Equals(text, "normal") || Equals(text, "common") || Equals(text, "white"))
            return 1;
        if (Equals(text, "uncommon") || Equals(text, "green"))
            return 2;
        if (Equals(text, "rare") || Equals(text, "blue"))
            return 3;
        if (Equals(text, "epic") || Equals(text, "violet") || Equals(text, "purple"))
            return 4;
        if (Equals(text, "legendary") || Equals(text, "yellow") || Equals(text, "orange"))
            return 5;
        if (Equals(text, "artifact"))
            return 6;
        if (Equals(text, "heirloom"))
            return 7;
        return 0xFFFFFFFFu;
    }

    constexpr std::uint32_t OccupiedAverage(std::span<std::uint32_t const> levels)
    {
        std::uint64_t sum = 0;
        std::uint32_t count = 0;
        for (std::uint32_t level : levels)
        {
            if (level == 0)
                continue;
            sum += level;
            ++count;
        }
        return count == 0 ? 0 : static_cast<std::uint32_t>(sum / count);
    }

    constexpr std::uint32_t FixedAverage(std::span<std::uint32_t const> levels, std::uint32_t denominator)
    {
        if (denominator == 0 || levels.size() != denominator)
            return 0;
        std::uint64_t sum = 0;
        for (std::uint32_t level : levels)
            sum += level;
        std::uint32_t const average = static_cast<std::uint32_t>(sum / denominator);
        return average == 0 ? 0 : average;
    }

    constexpr std::uint32_t MatchTarget(MatchPolicy const& policy, MatchEvidence const& evidence)
    {
        if (policy.denominator == MatchDenominator::OccupiedCombatSlots)
        {
            if (policy.source == MatchSource::LegacyMaster)
                return evidence.masterPresent ? OccupiedAverage(evidence.masterOccupied) : 0;
            return evidence.senderPresent ? OccupiedAverage(evidence.senderOccupied) : 0;
        }
        if (policy.source != MatchSource::CommandingSender || !evidence.senderPresent)
            return 0;
        return FixedAverage(evidence.senderFixed, evidence.senderFixedDenominator);
    }

    struct ArgParse
    {
        ArgKind kind = ArgKind::Garbage;
        std::uint32_t quality = 0;
        std::uint32_t ilvl = 0;
    };

    constexpr ArgParse ParseArgument(std::string_view text, bool bis)
    {
        ArgParse parsed;
        if (text.empty())
        {
            parsed.kind = ArgKind::None;
            return parsed;
        }
        if (!bis && Equals(text, "match"))
        {
            parsed.kind = ArgKind::Match;
            return parsed;
        }
        std::uint32_t const quality = ParseQualityWord(text);
        if (!bis && quality != 0xFFFFFFFFu)
        {
            parsed.kind = ArgKind::Quality;
            parsed.quality = quality;
            return parsed;
        }
        if (text.size() > (bis ? 5u : 5u) || text.find_first_not_of("0123456789") != std::string_view::npos)
            return parsed;
        std::uint32_t value = 0;
        for (char ch : text)
        {
            std::uint32_t const digit = static_cast<std::uint32_t>(ch - '0');
            if (value > (0xFFFFFFFFu - digit) / 10u)
                return parsed;
            value = value * 10u + digit;
        }
        if (value == 0)
            return parsed;
        if (!bis && value <= QualityLegendary)
            return parsed;
        if (bis && value > 0xFFFFu)
            return parsed;
        parsed.kind = ArgKind::Numeric;
        parsed.ilvl = value;
        return parsed;
    }

    struct ParseRequest
    {
        bool featureOn = false;
        bool bis = false;
        std::string_view text{};
        std::uint32_t qualityLimit = 3;
        std::uint32_t scoreLimit = 0;
        CapMode requestedMode = CapMode::Endgame;
        MatchPolicy match{};
        MatchEvidence evidence{};
        CapInput progression{};
    };

    constexpr CommandForm FormFor(bool reset, ArgKind argument, bool bis)
    {
        if (bis)
            return argument == ArgKind::Numeric ? CommandForm::BisNumeric : CommandForm::Bis;
        if (argument == ArgKind::Match)
            return CommandForm::Match;
        if (reset)
            return CommandForm::Reset;
        if (argument == ArgKind::Quality)
            return CommandForm::Quality;
        if (argument == ArgKind::Numeric)
            return CommandForm::Numeric;
        return CommandForm::Plain;
    }

    // Feature off follows the current command: occupied master average, clamped
    // only by a non-zero score limit. Feature on also runs form/mode eligibility.
    constexpr CommandDecision ParseCommand(ParseRequest const& request)
    {
        CommandDecision decision;
        decision.featureOn = request.featureOn;
        decision.bis = request.bis;
        std::string_view text = Trim(request.text);
        bool reset = false;
        if (!request.bis && (Equals(text, "reset") || (text.size() > 6 && text.substr(0, 6) == "reset ")))
        {
            reset = true;
            text = Equals(text, "reset") ? std::string_view{} : Trim(text.substr(6));
        }
        decision.reset = reset;
        ArgParse const argument = ParseArgument(text, request.bis);
        decision.argument = argument.kind;
        if (argument.kind == ArgKind::Garbage)
        {
            decision.rejected = true;
            return decision;
        }
        if (request.bis && request.qualityLimit < QualityEpic)
        {
            decision.rejected = true;
            return decision;
        }
        decision.quality = request.qualityLimit;
        if (argument.kind == ArgKind::Quality)
            decision.quality = argument.quality < request.qualityLimit ? argument.quality : request.qualityLimit;

        std::uint32_t legacy = request.scoreLimit;
        if (argument.kind == ArgKind::Numeric)
        {
            legacy = argument.ilvl;
            if (request.scoreLimit != 0 && request.scoreLimit < legacy)
                legacy = request.scoreLimit;
        }
        if (argument.kind == ArgKind::Match)
        {
            std::uint32_t const target = MatchTarget(request.match, request.evidence);
            if (target == 0)
            {
                decision.preserve = true;
                decision.legacyUnchanged = !request.featureOn;
                return decision;
            }
            legacy = request.scoreLimit == 0 ? target : (target < request.scoreLimit ? target : request.scoreLimit);
        }
        decision.legacyIlvl = legacy;

        if (!request.featureOn)
        {
            decision.legacyUnchanged = true;
            decision.preserve = false;
            return decision;
        }

        CapInput cap = request.progression;
        cap.mode = request.requestedMode;
        if (argument.kind == ArgKind::Numeric)
        {
            cap.typedItemLevelPresent = true;
            cap.typedItemLevel = argument.ilvl;
        }
        if (argument.kind == ArgKind::Match)
        {
            cap.matchSampleKnown = true;
            cap.matchAverage = MatchTarget(request.match, request.evidence);
        }
        CommandForm const form = FormFor(reset, argument.kind, request.bis);
        decision.cap = ResolveCommandEligibility(form, cap);
        decision.preserve = decision.cap.preserve;
        decision.legacyUnchanged = false;
        return decision;
    }
}

#endif
