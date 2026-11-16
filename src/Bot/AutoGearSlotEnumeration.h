/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_SLOT_ENUMERATION_H
#define PLAYERBOTS_AUTOGEAR_SLOT_ENUMERATION_H

#include <cstdint>
#include <span>

namespace PlayerbotsAutoGearSelection
{
    // One copied row for one slot. Unknown is not known-unavailable.
    enum class RowState : std::uint8_t
    {
        Unknown,
        KnownUnavailable,
        KnownPresent,
        Stale,
        Mismatched,
        Duplicate
    };

    struct ProvenanceRow
    {
        std::uint32_t entryId = 0;
        std::int32_t randomProperty = 0;
        std::uint32_t itemLevel = 0;
        std::uint32_t setId = 0;
        bool setEvidenceKnown = false;
        bool setBonusPresent = false;
        RowState state = RowState::Unknown;
        std::uint64_t revision = 0;
    };

    enum class ListVerdict : std::uint8_t
    {
        Invalid,
        Incomplete,
        ProvenEmpty,
        ProvenCandidates
    };

    struct ListAssessment
    {
        ListVerdict verdict = ListVerdict::Incomplete;
        // True only when every set on this list is known not to grant a bonus.
        bool setBonusProvenAbsent = false;
        std::size_t present = 0;
    };

    inline bool SameIdentity(ProvenanceRow const& left, ProvenanceRow const& right)
    {
        return left.entryId == right.entryId && left.randomProperty == right.randomProperty;
    }

    // authoritative false, revision 0, or any unknown/stale/mismatched row is
    // Incomplete. A duplicate identity is Invalid. Known-unavailable rows are
    // omitted. An empty remainder can be ProvenEmpty. A set bonus is proven
    // absent only when every set id on the list has known evidence and no bonus.
    inline ListAssessment AssessSlotList(std::span<ProvenanceRow const> rows, std::uint64_t expectedRevision,
        bool authoritative, std::size_t rowBound = 64)
    {
        ListAssessment assessment;
        if (rows.size() > rowBound)
            return assessment;
        if (!authoritative || expectedRevision == 0)
            return assessment;
        bool setUnknown = false;
        bool setPresent = false;
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            ProvenanceRow const& row = rows[i];
            if (row.state == RowState::Duplicate)
            {
                assessment.verdict = ListVerdict::Invalid;
                return assessment;
            }
            if (row.state == RowState::Unknown || row.state == RowState::Stale || row.state == RowState::Mismatched ||
                row.revision != expectedRevision)
                return assessment;
            if (row.state != RowState::KnownUnavailable && row.state != RowState::KnownPresent)
                return assessment;
            if (row.entryId == 0 || row.itemLevel == 0)
                return assessment;
            if (row.setId != 0)
            {
                if (!row.setEvidenceKnown)
                    setUnknown = true;
                else if (row.setBonusPresent)
                    setPresent = true;
            }
            for (std::size_t earlier = 0; earlier < i; ++earlier)
            {
                ProvenanceRow const& previous = rows[earlier];
                bool const tracked = previous.state == RowState::KnownPresent ||
                                     previous.state == RowState::KnownUnavailable;
                if (tracked && SameIdentity(previous, row))
                {
                    assessment.verdict = ListVerdict::Invalid;
                    return assessment;
                }
            }
            if (row.state == RowState::KnownUnavailable)
                continue;
            ++assessment.present;
        }
        if (setUnknown)
            return assessment;
        assessment.setBonusProvenAbsent = !setPresent;
        assessment.verdict = assessment.present == 0 ? ListVerdict::ProvenEmpty : ListVerdict::ProvenCandidates;
        return assessment;
    }
}

#endif
