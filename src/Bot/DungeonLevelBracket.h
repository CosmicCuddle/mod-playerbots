/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_DUNGEON_LEVEL_BRACKET_H
#define PLAYERBOTS_DUNGEON_LEVEL_BRACKET_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

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
        std::uint32_t const minimum = std::max(row.minLevel, overrideMin);
        std::uint32_t const maximum = row.maxLevel && overrideMax ? std::min(row.maxLevel, overrideMax)
            : row.maxLevel ? row.maxLevel : overrideMax;
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
        // An exact selected physical dungeon is authoritative only when its rows agree.
        Row const* selected = nullptr;
        for (Row const& row : rows)
        {
            if (!row.physicalDungeon || row.mapId != mapId || row.difficulty != difficulty ||
                row.id != selectedDungeonId || !selectedDungeonId)
                continue;

            if (selected && (row.minLevel != selected->minLevel || row.maxLevel != selected->maxLevel ||
                row.seasonal != selected->seasonal || row.seasonActive != selected->seasonActive))
                return {Result::AmbiguousBracket};
            selected = &row;
        }
        if (selected)
        {
            if (selected->seasonal)
                return {selected->seasonActive ? Result::SeasonalException : Result::SeasonalInactive};
            return Evaluate(*selected, level, overrideMin, overrideMax);
        }

        // Without a physical identity, all nonseasonal rows must agree on one bracket.
        std::uint32_t minimum = 0;
        std::uint32_t maximum = 0;
        bool found = false;
        for (Row const& row : rows)
        {
            if (!row.physicalDungeon || row.seasonal || row.mapId != mapId || row.difficulty != difficulty)
                continue;

            Decision const candidate = Evaluate(row, level, overrideMin, overrideMax);
            if (candidate.result == Result::MissingBracket)
                return {Result::MissingBracket};
            if (found && (candidate.minLevel != minimum || candidate.maxLevel != maximum))
                return {Result::AmbiguousBracket};
            minimum = candidate.minLevel;
            maximum = candidate.maxLevel;
            found = true;
        }

        if (!found)
            return {Result::MissingBracket};
        if (level < minimum)
            return {Result::BelowMinimum, minimum, maximum};
        if (level > maximum)
            return {Result::AboveMaximum, minimum, maximum};
        return {Result::Allowed, minimum, maximum};
    }

    // Owner-reviewed bracket for a map/difficulty whose source rows cannot give one bracket
    // (several wings with different LFG ranges, or no rows). Never replaces a resolved bracket.
    struct OverrideRange
    {
        std::uint32_t mapId;
        std::uint32_t difficulty;
        std::uint32_t minLevel;
        std::uint32_t maxLevel;
    };

    constexpr Decision ApplyReviewedOverride(Decision decision, std::span<OverrideRange const> overrides,
        std::uint32_t mapId, std::uint32_t difficulty, std::uint32_t level)
    {
        if (decision.result != Result::AmbiguousBracket && decision.result != Result::MissingBracket)
            return decision;
        for (OverrideRange const& range : overrides)
        {
            if (range.mapId != mapId || range.difficulty != difficulty)
                continue;
            if (!range.minLevel || range.minLevel > range.maxLevel)
                return decision;
            if (level < range.minLevel)
                return {Result::BelowMinimum, range.minLevel, range.maxLevel};
            if (level > range.maxLevel)
                return {Result::AboveMaximum, range.minLevel, range.maxLevel};
            return {Result::Allowed, range.minLevel, range.maxLevel};
        }
        return decision;
    }

    // LFG entrance of one dungeon (lfg_dungeon_template), loaded at startup.
    struct Entrance
    {
        std::uint32_t dungeonId;
        std::uint32_t mapId;
        float x;
        float y;
        float z;
    };

    // Where the entering bot will appear, when the caller knows it: the teleport destination, or the
    // saved position on a login check.
    struct Destination
    {
        bool known = false;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    // Portals and LFG teleports land on the entrance itself; 10 yards absorbs float rounding only.
    inline constexpr float WingEntranceYards = 10.0f;

    // Owner decision (21 Nov 2026): a map whose wings carry different LFG brackets (Scarlet Monastery,
    // Blackrock Depths, Maraudon, Dire Maul) uses each wing's own original bracket. The wing is the one
    // whose LFG entrance the destination lands on. When the destination does not tell one wing (a summon
    // into the instance, two wings sharing an entrance, or a pre-check without a destination), the bot
    // must fit the original bracket of at least one candidate wing. Applies only to an ambiguous decision.
    constexpr Decision ResolveWing(Decision decision, std::span<Row const> rows, std::span<Entrance const> entrances,
                                   std::uint32_t mapId, std::uint32_t difficulty, Destination destination,
                                   std::uint32_t level, std::uint32_t overrideMin = 0, std::uint32_t overrideMax = 0)
    {
        if (decision.result != Result::AmbiguousBracket)
            return decision;

        auto const wingRow = [&](Row const& row)
        { return row.physicalDungeon && !row.seasonal && row.mapId == mapId && row.difficulty == difficulty; };
        auto const atEntrance = [&](Row const& row)
        {
            if (!destination.known)
                return false;
            for (Entrance const& entrance : entrances)
            {
                if (entrance.dungeonId != row.id || entrance.mapId != mapId)
                    continue;
                float const dx = entrance.x - destination.x;
                float const dy = entrance.y - destination.y;
                float const dz = entrance.z - destination.z;
                if (dx * dx + dy * dy + dz * dz <= WingEntranceYards * WingEntranceYards)
                    return true;
            }
            return false;
        };

        bool identified = false;
        for (Row const& row : rows)
            identified = identified || (wingRow(row) && atEntrance(row));

        std::uint32_t minimum = 0;
        std::uint32_t maximum = 0;
        std::uint32_t nextMinimum = 0;  // lowest wing minimum above the level, for a gap between wings
        bool found = false;
        bool fits = false;
        for (Row const& row : rows)
        {
            if (!wingRow(row) || (identified && !atEntrance(row)))
                continue;
            Decision const wing = Evaluate(row, level, overrideMin, overrideMax);
            if (wing.result == Result::MissingBracket)
                return {Result::MissingBracket};
            minimum = found ? std::min(minimum, wing.minLevel) : wing.minLevel;
            maximum = found ? std::max(maximum, wing.maxLevel) : wing.maxLevel;
            found = true;
            fits = fits || wing.result == Result::Allowed;
            if (wing.minLevel > level && (!nextMinimum || wing.minLevel < nextMinimum))
                nextMinimum = wing.minLevel;
        }

        if (!found)
            return decision;
        if (fits)
            return {Result::Allowed, minimum, maximum};
        if (level > maximum)
            return {Result::AboveMaximum, minimum, maximum};
        return {Result::BelowMinimum, nextMinimum ? nextMinimum : minimum, maximum};
    }

    // The startup index cannot know whether a holiday is running. Copies the rows and marks the exact
    // selected seasonal row active or not; with no selection, or too many rows to copy, nothing is active.
    template <std::size_t N>
    constexpr std::span<Row const> WithSeasonActivity(std::span<Row const> rows, std::uint32_t selectedDungeonId,
        bool selectedSeasonActive, std::array<Row, N>& scratch)
    {
        if (!selectedDungeonId || rows.size() > N)
            return rows;
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            scratch[i] = rows[i];
            scratch[i].seasonActive =
                scratch[i].seasonal && scratch[i].id == selectedDungeonId && selectedSeasonActive;
        }
        return std::span<Row const>(scratch.data(), rows.size());
    }

    // Normal cross-map teleports reach OnPlayerCanEnterMap through PlayerCannotEnter, which GM mode skips.
    // A same-map teleport to a dungeon map means the bot is already inside, so entry was already decided;
    // checking again would also lose the LFG selection that admitted it (e.g. a holiday boss).
    constexpr bool TeleportNeedsBracketCheck(bool sameMap, bool gmModeTeleport)
    {
        return !sameMap && gmModeTeleport;
    }

    // "map:difficulty:min:max" entries separated by commas. Any malformed or duplicate entry rejects the
    // whole list, so a typo never half-applies.
    inline bool ParseOverrides(std::string_view text, std::vector<OverrideRange>& out)
    {
        out.clear();
        auto number = [](std::string_view part, std::uint32_t& value)
        {
            if (part.empty() || part.size() > 6)
                return false;
            value = 0;
            for (char ch : part)
            {
                if (ch < '0' || ch > '9')
                    return false;
                value = value * 10 + static_cast<std::uint32_t>(ch - '0');
            }
            return true;
        };
        while (!text.empty())
        {
            std::size_t const comma = text.find(',');
            std::string_view entry = text.substr(0, comma);
            text = comma == std::string_view::npos ? std::string_view{} : text.substr(comma + 1);
            while (!entry.empty() && entry.front() == ' ')
                entry.remove_prefix(1);
            while (!entry.empty() && entry.back() == ' ')
                entry.remove_suffix(1);
            if (entry.empty())
                continue;
            std::uint32_t fields[4]{};
            for (std::uint32_t& field : fields)
            {
                std::size_t const colon = entry.find(':');
                if (!number(entry.substr(0, colon), field))
                {
                    out.clear();
                    return false;
                }
                entry = colon == std::string_view::npos ? std::string_view{} : entry.substr(colon + 1);
            }
            OverrideRange const range{fields[0], fields[1], fields[2], fields[3]};
            bool duplicate = false;
            for (OverrideRange const& existing : out)
                duplicate = duplicate || (existing.mapId == range.mapId && existing.difficulty == range.difficulty);
            if (!entry.empty() || !range.minLevel || range.minLevel > range.maxLevel || duplicate)
            {
                out.clear();
                return false;
            }
            out.push_back(range);
        }
        return true;
    }
}

#endif
