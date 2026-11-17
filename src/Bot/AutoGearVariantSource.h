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
    // False is not "unavailable". Unknown stays unknown.
    enum class SourceEligibility : std::uint8_t
    {
        Unknown,
        KnownUnavailable,
        KnownEligible
    };

    struct SourceRow
    {
        std::uint32_t entryId = 0;
        std::int32_t randomProperty = 0;
        std::uint32_t itemLevel = 0;
        std::uint8_t slot = 0;
        bool templateLoaded = false;
        SourceEligibility eligibility = SourceEligibility::Unknown;
        bool staticCapabilityKnown = false;
        bool staticCapable = false;
    };

    enum class CopyStatus : std::uint8_t
    {
        Copied,
        Bounded,
        Incomplete,
        IncompleteVariants,
        Invalid
    };

    inline constexpr bool ValidSourceEligibility(SourceEligibility eligibility)
    {
        switch (eligibility)
        {
            case SourceEligibility::Unknown:
            case SourceEligibility::KnownUnavailable:
            case SourceEligibility::KnownEligible:
                return true;
        }
        return false;
    }

    struct CopiedSource
    {
        CopyStatus status = CopyStatus::Incomplete;
        std::vector<SourceRow> rows{};
        bool sourceComplete = false;
    };

    // This is only a bounded structural copy; per-slot source authority, native
    // item/equip facts and source revision require separate validation by the
    // calling planner. Known-unavailable rows remain outside candidate lists.
    // By default, mixed signed variants retain the historical Preserve gate.
    // Variant-aware callers may opt in only when they also use the signed-key
    // solver and the independent exact per-slot provenance checks.
    inline CopiedSource CopyBoundedSource(std::span<SourceRow const> nativeRows, std::size_t copyBudget,
        bool variantAware = false)
    {
        CopiedSource copied;
        if (nativeRows.size() > copyBudget)
        {
            copied.status = CopyStatus::Bounded;
            return copied;
        }
        bool splitVariant = false;
        for (std::size_t i = 0; i < nativeRows.size(); ++i)
        {
            SourceRow const& row = nativeRows[i];
            if (!ValidSourceEligibility(row.eligibility))
            {
                copied.rows.clear();
                copied.status = CopyStatus::Invalid;
                copied.sourceComplete = false;
                return copied;
            }
            bool const malformed = !row.templateLoaded || !row.staticCapabilityKnown || row.entryId == 0 ||
                                   row.itemLevel == 0 || row.eligibility == SourceEligibility::Unknown;
            if (malformed)
            {
                copied.rows.clear();
                copied.status = CopyStatus::Incomplete;
                copied.sourceComplete = false;
                return copied;
            }
            for (std::size_t j = 0; j < i; ++j)
            {
                SourceRow const& prior = nativeRows[j];
                if (row.entryId != prior.entryId)
                    continue;
                // ItemLevel belongs to the item template, not the random roll.
                // An exact (entry, signed variant, equipment slot) identity
                // must appear only once, even if sources disagree on eligibility.
                if (row.itemLevel != prior.itemLevel ||
                    (row.randomProperty == prior.randomProperty && row.slot == prior.slot))
                {
                    copied.rows.clear();
                    copied.status = CopyStatus::Invalid;
                    copied.sourceComplete = false;
                    return copied;
                }
                // A single template cannot simultaneously claim ungrouped,
                // property and suffix namespaces without independent native
                // group authority. Preserve rather than certify mixed signs.
                if ((row.randomProperty == 0) != (prior.randomProperty == 0) ||
                    (row.randomProperty < 0) != (prior.randomProperty < 0))
                {
                    copied.rows.clear();
                    copied.status = CopyStatus::IncompleteVariants;
                    copied.sourceComplete = false;
                    return copied;
                }
                if (row.randomProperty != prior.randomProperty &&
                    row.eligibility == SourceEligibility::KnownEligible &&
                    prior.eligibility == SourceEligibility::KnownEligible &&
                    row.staticCapable && prior.staticCapable)
                    splitVariant = true;
            }
            if (row.eligibility == SourceEligibility::KnownEligible && !row.staticCapable)
            {
                // A dropped eligible variant makes the copied legal set
                // incomplete; do not certify its capable siblings.
                copied.rows.clear();
                copied.status = CopyStatus::IncompleteVariants;
                copied.sourceComplete = false;
                return copied;
            }
            if (row.eligibility == SourceEligibility::KnownUnavailable)
                continue;
            copied.rows.push_back(row);
        }
        if (splitVariant && !variantAware)
        {
            copied.status = CopyStatus::IncompleteVariants;
            copied.sourceComplete = false;
            return copied;
        }
        copied.status = CopyStatus::Copied;
        copied.sourceComplete = true;
        return copied;
    }

    // Separately validated revisions. This compares copies only.
    struct OwnerThreadCopy
    {
        std::uint64_t senderEpoch = 0;
        std::uint64_t botEpoch = 0;
        std::uint64_t senderIpRevision = 0;
        std::uint64_t senderEquipmentRevision = 0;
        std::uint64_t botEquipmentRevision = 0;
        std::uint64_t sessionIncarnation = 0;
        std::uint64_t botIncarnation = 0;
        std::uint64_t policyRevision = 0;
        bool immutable = false;
    };

    constexpr bool SameFinalCopy(OwnerThreadCopy const& published, OwnerThreadCopy const& finalB)
    {
        if (!published.immutable || !finalB.immutable)
            return false;
        if (published.senderEpoch == 0 || published.botEpoch == 0 || published.senderIpRevision == 0 ||
            published.senderEquipmentRevision == 0 || published.botEquipmentRevision == 0 ||
            published.sessionIncarnation == 0 || published.botIncarnation == 0 || published.policyRevision == 0)
            return false;
        return published.senderEpoch == finalB.senderEpoch && published.botEpoch == finalB.botEpoch &&
               published.senderIpRevision == finalB.senderIpRevision &&
               published.senderEquipmentRevision == finalB.senderEquipmentRevision &&
               published.botEquipmentRevision == finalB.botEquipmentRevision &&
               published.sessionIncarnation == finalB.sessionIncarnation &&
               published.botIncarnation == finalB.botIncarnation && published.policyRevision == finalB.policyRevision;
    }
}

#endif
