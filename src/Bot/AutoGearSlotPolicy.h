/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_SLOT_POLICY_H
#define PLAYERBOTS_AUTOGEAR_SLOT_POLICY_H

namespace PlayerbotsAutoGearSlots
{
    struct Layout
    {
        unsigned head, neck, body, chest, legs, feet;
        unsigned finger1, finger2, trinket1, trinket2;
        unsigned mainhand, offhand, ranged, tabard;
    };

    // False means the schedule did not search the slot. It is not proof that
    // the slot is empty. A low-level bot may already be wearing the item.
    constexpr bool SearchAtLevel(unsigned level, unsigned slot, Layout const& layout)
    {
        if (slot == layout.tabard || slot == layout.body)
            return false;
        if (level < 50 && (slot == layout.trinket1 || slot == layout.trinket2))
            return false;
        if (level < 30 && (slot == layout.neck || slot == layout.head))
            return false;
        if (level < 20 && (slot == layout.finger1 || slot == layout.finger2))
            return false;
        return level >= 5 || slot == layout.mainhand || slot == layout.offhand || slot == layout.feet ||
            slot == layout.legs || slot == layout.chest || slot == layout.ranged;
    }
}

#endif
