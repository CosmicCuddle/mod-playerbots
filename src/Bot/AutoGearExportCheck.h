/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_EXPORT_CHECK_H
#define PLAYERBOTS_AUTOGEAR_EXPORT_CHECK_H

#include <cstdint>
#include <span>

namespace PlayerbotsAutoGearSelection
{
    // Offline check for a future owner export. It does not open a database
    // and it does not invent a missing spell, proc, or source.
    enum class ExportVerdict : std::uint8_t
    {
        Unknown,
        Invalid,
        Accepted
    };

    struct ExportSnapshot
    {
        bool schemaNamed = false;
        bool checksumKnown = false;
        bool versionKnown = false;
        std::uint64_t checksum = 0;
        std::uint64_t expectedChecksum = 0;
        std::uint32_t itemTemplateRows = 0;
        std::uint32_t expectedItemRows = 0;
        std::uint32_t spellRows = 0;
        std::uint32_t expectedSpellRows = 0;
        bool spellHeaderIsId = false;
        std::span<std::uint32_t const> spellIds{};
        std::span<std::uint32_t const> itemSpellIds{};
    };

    inline ExportVerdict CheckExport(ExportSnapshot const& snapshot)
    {
        if (!snapshot.schemaNamed || !snapshot.checksumKnown || !snapshot.versionKnown ||
            snapshot.expectedItemRows == 0 || snapshot.expectedSpellRows == 0 || !snapshot.spellHeaderIsId)
            return ExportVerdict::Unknown;
        if (snapshot.checksum != snapshot.expectedChecksum || snapshot.itemTemplateRows != snapshot.expectedItemRows ||
            snapshot.spellRows != snapshot.expectedSpellRows || snapshot.spellIds.size() != snapshot.spellRows)
            return ExportVerdict::Invalid;
        for (std::size_t i = 0; i < snapshot.spellIds.size(); ++i)
        {
            if (snapshot.spellIds[i] == 0)
                return ExportVerdict::Invalid;
            for (std::size_t earlier = 0; earlier < i; ++earlier)
            {
                if (snapshot.spellIds[earlier] == snapshot.spellIds[i])
                    return ExportVerdict::Invalid;
            }
        }
        for (std::uint32_t referenced : snapshot.itemSpellIds)
        {
            if (referenced == 0)
                continue;
            bool found = false;
            for (std::uint32_t spellId : snapshot.spellIds)
            {
                if (spellId == referenced)
                    found = true;
            }
            if (!found)
                return ExportVerdict::Unknown;
        }
        return ExportVerdict::Accepted;
    }
}

#endif
