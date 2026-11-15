/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_CANDIDATE_SCAN_H
#define PLAYERBOTS_AUTOGEAR_CANDIDATE_SCAN_H

#include <cstddef>
#include <cstdint>
#include <span>

namespace PlayerbotsAutoGearCandidates
{
    struct Candidate
    {
        std::uint32_t itemId;
        std::uint32_t rawItemLevel;
        bool metadataKnown;
        bool eligible;
    };

    enum class Status { Incomplete, InvalidInput, CompleteEmpty, CompleteWithCandidates };

    struct Result
    {
        Status status = Status::Incomplete;
        std::size_t visited = 0;
        std::size_t matches = 0;
        std::uint32_t firstItemId = 0;
    };

    // Input is one slot's immutable, strictly item-ID-ordered universe after
    // owner-thread eligibility capture. No random sampling or early success exit.
    // CompleteEmpty proves only this domain empty, never an entire set impossible.
    constexpr Result Scan(std::span<Candidate const> candidates, std::uint32_t hardCap,
        bool universeComplete, std::size_t budget)
    {
        Result result;
        if (!hardCap)
            return {Status::InvalidInput};
        std::uint32_t previous = 0;
        for (Candidate const& candidate : candidates)
        {
            if (result.visited == budget)
                return {Status::Incomplete, result.visited};
            ++result.visited;
            if (!candidate.metadataKnown || !candidate.itemId || candidate.itemId <= previous ||
                !candidate.rawItemLevel)
                return {Status::InvalidInput, result.visited};
            previous = candidate.itemId;
            if (!candidate.eligible || candidate.rawItemLevel > hardCap)
                continue;
            if (!result.matches)
                result.firstItemId = candidate.itemId;
            ++result.matches;
        }
        if (!universeComplete)
            return {Status::Incomplete, result.visited};
        result.status = result.matches ? Status::CompleteWithCandidates : Status::CompleteEmpty;
        return result;
    }
}

#endif
