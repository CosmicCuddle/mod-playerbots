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
        bool combatEquipment;
        bool templateKnown;
        bool scalingUnresolved;
        std::uint32_t rawItemLevel;
    };

    struct Result
    {
        bool known = false;
        std::uint32_t average = 0;
        std::uint32_t occupiedSlots = 0;
    };

    // Caller captures slots on the sender-owning thread. This value alone has
    // no session/revision/freshness proof and cannot authorize a command.
    constexpr Result Average(std::span<Slot const> slots)
    {
        std::uint64_t sum = 0;
        std::uint32_t count = 0;
        // Core has 19 equipment positions, of which 17 are combat slots.
        if (slots.size() > 19)
            return {};
        for (Slot const& slot : slots)
        {
            if (!slot.occupied || !slot.combatEquipment)
                continue;
            if (!slot.templateKnown || slot.scalingUnresolved)
                return {};
            sum += slot.rawItemLevel;
            ++count;
        }
        if (!count || sum / count == 0)
            return {};
        return {true, static_cast<std::uint32_t>(sum / count), count};
    }
}

#endif
