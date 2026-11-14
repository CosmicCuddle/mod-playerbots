/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_DUNGEON_LEVEL_BRACKET_H
#define PLAYERBOTS_DUNGEON_LEVEL_BRACKET_H

#include <cstdint>
#include <span>

namespace PlayerbotsDungeonBracket
{
    struct Row
    {
        std::uint32_t id;
        std::uint32_t mapId;
        std::uint32_t difficulty;
        std::uint32_t minLevel;
        std::uint32_t maxLevel;
        bool physicalDungeon;
        bool seasonal;
        bool seasonActive;
    };

    enum class Result
    {
        Allowed,
        BelowMinimum,
        AboveMaximum,
        SeasonalException,
        SeasonalInactive,
        MissingBracket,
        AmbiguousBracket
    };

    struct Decision
    {
        Result result;
        std::uint32_t minLevel = 0;
        std::uint32_t maxLevel = 0;
    };

    constexpr Decision Evaluate(Row const& row, std::uint32_t level, std::uint32_t overrideMin,
        std::uint32_t overrideMax)
    {
        std::uint32_t const minimum = overrideMin ? overrideMin : row.minLevel;
        std::uint32_t const maximum = overrideMax ? overrideMax : row.maxLevel;
        if (!minimum || !maximum || minimum > maximum)
            return {Result::MissingBracket};
        if (level < minimum)
            return {Result::BelowMinimum, minimum, maximum};
        if (level > maximum)
            return {Result::AboveMaximum, minimum, maximum};
        return {Result::Allowed, minimum, maximum};
    }

    constexpr Decision Resolve(std::span<Row const> rows, std::uint32_t mapId, std::uint32_t difficulty,
        std::uint32_t selectedDungeonId, std::uint32_t level, std::uint32_t overrideMin = 0,
        std::uint32_t overrideMax = 0)
    {
        // An exact selected physical dungeon is authoritative, including a live seasonal run.
        for (Row const& row : rows)
        {
            if (!row.physicalDungeon || row.mapId != mapId || row.difficulty != difficulty ||
                row.id != selectedDungeonId || !selectedDungeonId)
                continue;

            if (row.seasonal)
            {
                if (row.seasonActive)
                    return {Result::SeasonalException};
                return {Result::SeasonalInactive};
            }

            return Evaluate(row, level, overrideMin, overrideMax);
        }

        // Without a physical identity, all nonseasonal rows must agree on one bracket.
        std::uint32_t minimum = 0;
        std::uint32_t maximum = 0;
        bool found = false;
        for (Row const& row : rows)
        {
            if (!row.physicalDungeon || row.seasonal || row.mapId != mapId || row.difficulty != difficulty)
                continue;

            std::uint32_t const candidateMin = overrideMin ? overrideMin : row.minLevel;
            std::uint32_t const candidateMax = overrideMax ? overrideMax : row.maxLevel;
            if (!candidateMin || !candidateMax || candidateMin > candidateMax)
                return {Result::MissingBracket};
            if (found && (candidateMin != minimum || candidateMax != maximum))
                return {Result::AmbiguousBracket};
            minimum = candidateMin;
            maximum = candidateMax;
            found = true;
        }

        if (!found)
        {
            if (!overrideMin || !overrideMax || overrideMin > overrideMax)
                return {Result::MissingBracket};
            minimum = overrideMin;
            maximum = overrideMax;
        }
        if (level < minimum)
            return {Result::BelowMinimum, minimum, maximum};
        if (level > maximum)
            return {Result::AboveMaximum, minimum, maximum};
        return {Result::Allowed, minimum, maximum};
    }
}

#endif
