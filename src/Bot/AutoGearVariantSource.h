/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_VARIANT_SOURCE_H
#define PLAYERBOTS_AUTOGEAR_VARIANT_SOURCE_H

#include <cstdint>
#include <span>
#include <vector>

namespace PlayerbotsAutoGearSelection
{
    // A loaded template is not, by itself, an eligible source.
    struct SourceRow
    {
        std::uint32_t entryId = 0;
        std::int32_t randomProperty = 0;
        std::uint32_t itemLevel = 0;
        bool templateLoaded = false;
        bool sourceEligible = false;
        bool staticCapabilityKnown = false;
        bool staticCapable = false;
    };

    enum class CopyStatus : std::uint8_t
    {
        Copied,
        Bounded,
        IncompleteVariants
    };

    struct CopiedSource
    {
        CopyStatus status = CopyStatus::Bounded;
        std::vector<SourceRow> rows{};
        bool sourceComplete = false;
    };

    // Stops before copying when the native row count exceeds the budget.
    // Two random-property variants of one entry are both kept and the
    // source is not marked complete: the shared solver cannot represent
    // them as one id without dropping one.
    inline CopiedSource CopyBoundedSource(std::span<SourceRow const> nativeRows, std::size_t copyBudget)
    {
        CopiedSource copied;
        if (nativeRows.size() > copyBudget)
            return copied;
        bool splitVariant = false;
        for (std::size_t i = 0; i < nativeRows.size(); ++i)
        {
            SourceRow const& row = nativeRows[i];
            if (!row.templateLoaded || !row.sourceEligible || !row.staticCapabilityKnown || !row.staticCapable ||
                row.entryId == 0 || row.itemLevel == 0)
                continue;
            for (std::size_t j = 0; j < i; ++j)
            {
                SourceRow const& earlier = nativeRows[j];
                if (earlier.entryId == row.entryId && earlier.randomProperty != row.randomProperty &&
                    earlier.templateLoaded && earlier.sourceEligible)
                    splitVariant = true;
            }
            copied.rows.push_back(row);
        }
        copied.status = splitVariant ? CopyStatus::IncompleteVariants : CopyStatus::Copied;
        copied.sourceComplete = !splitVariant;
        return copied;
    }

    // Final B compares immutable copies. It does not call a live Player.
    struct OwnerThreadCopy
    {
        std::uint64_t senderEpoch = 0;
        std::uint64_t botEpoch = 0;
        std::uint64_t equipmentRevision = 0;
        bool immutable = false;
    };

    constexpr bool SameFinalCopy(OwnerThreadCopy const& published, OwnerThreadCopy const& finalB)
    {
        return published.immutable && finalB.immutable && published.senderEpoch != 0 && published.botEpoch != 0 &&
               published.senderEpoch == finalB.senderEpoch && published.botEpoch == finalB.botEpoch &&
               published.equipmentRevision != 0 && published.equipmentRevision == finalB.equipmentRevision;
    }
}

#endif
