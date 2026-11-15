/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_EQUIPMENT_SAMPLE_H
#define PLAYERBOTS_AUTOGEAR_EQUIPMENT_SAMPLE_H

#include <cstdint>
#include <span>

namespace PlayerbotsAutoGearSample
{
    struct Slot
    {
        bool occupied;
        bool countedByPolicy;
        bool templateKnown;
        bool scalingUnresolved;
        std::uint32_t rawItemLevel;
    };

    struct Result
    {
        bool known = false;
        std::uint32_t average = 0;
        std::uint32_t occupiedSlots = 0;
        std::uint32_t countedSlots = 0;
    };

    // Caller captures slots on the sender-owning thread. This value alone has
    // no session/revision/freshness proof and cannot authorize a command.
    // Counted slots depend on level/capability policy, never occupancy. Empty
    // counted slots contribute zero without shrinking the denominator.
    constexpr Result Average(std::span<Slot const> slots, bool eligibilityKnown)
    {
        std::uint64_t sum = 0;
        std::uint32_t count = 0;
        std::uint32_t occupied = 0;
        // Core has 19 equipment positions, of which 17 are combat slots.
        if (!eligibilityKnown || slots.size() > 19)
            return {};
        for (Slot const& slot : slots)
        {
            if (!slot.countedByPolicy)
                continue;
            ++count;
            if (!slot.occupied)
                continue;
            if (!slot.templateKnown || slot.scalingUnresolved || !slot.rawItemLevel)
                return {};
            sum += slot.rawItemLevel;
            ++occupied;
        }
        if (!count || sum / count == 0)
            return {};
        return {true, static_cast<std::uint32_t>(sum / count), occupied, count};
    }
}

#endif
