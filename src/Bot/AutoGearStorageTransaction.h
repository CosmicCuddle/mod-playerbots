#pragma once

#include "AutoGearStorageMatching.h"
#include <optional>
#include <span>

namespace PlayerbotsAutoGearStorage
{
// Owner-context orchestration only. The backend must implement a non-merging,
// identity-preserving exact move; arbitrary Core SwapItem is not that contract.
enum class TransactionStatus { Rejected, Committed, RolledBack, RecoveryRequired };

inline bool ValidMoves(std::span<Move const> moves)
{
    if (moves.size() > 17)
        return false;
    for (std::size_t i = 0; i < moves.size(); ++i)
    {
        auto const& m = moves[i];
        if (!m.itemGuid || !m.source || !m.destination || m.source == m.destination)
            return false;
        for (std::size_t j = 0; j < moves.size(); ++j)
        {
            auto const& other = moves[j];
            if (m.source == other.destination || (i != j &&
                (m.source == other.source || m.destination == other.destination || m.itemGuid == other.itemGuid)))
                return false;
        }
    }
    return true;
}

// Inspect returns nullopt on unknown, zero on empty, or exact item GUID.
// Verify all positions, not just the return value of a movement operation.
// No allocations or yields by this coordinator after the first mutation.
template<class Inspect, class MoveExact>
TransactionStatus Execute(std::span<Move const> moves, bool authorityValid,
    Inspect inspect, MoveExact moveExact)
{
    if (!authorityValid || !ValidMoves(moves))
        return TransactionStatus::Rejected;
    auto read = [&](std::uint16_t position) -> std::optional<std::uint64_t>
    {
        try { return inspect(position); }
        catch (...) { return std::nullopt; }
    };
    auto at = [&](std::uint16_t position, std::uint64_t guid)
    {
        auto value = read(position);
        return value && *value == guid;
    };
    auto original = [&](Move const& m) { return at(m.source, m.itemGuid) && at(m.destination, 0); };
    auto moved = [&](Move const& m) { return at(m.source, 0) && at(m.destination, m.itemGuid); };
    for (auto const& m : moves)
        if (!original(m))
            return TransactionStatus::Rejected;

    auto attempt = [&](std::uint16_t from, std::uint16_t to, std::uint64_t guid)
    {
        try { return moveExact(from, to, guid); }
        catch (...) { return false; }
    };
    bool success = true;
    for (auto const& m : moves)
    {
        if (!original(m) || !attempt(m.source, m.destination, m.itemGuid) || !moved(m))
        {
            success = false;
            break;
        }
    }
    if (success)
    {
        for (auto const& m : moves)
            if (!moved(m))
            {
                success = false;
                break;
            }
        if (success)
            return TransactionStatus::Committed;
    }

    // Roll back only the exact item in its reserved destination with its
    // original slot still empty. Never overwrite, delete, or guess ownership.
    // A backend can report failure after moving: observation is authoritative.
    for (auto it = moves.rbegin(); it != moves.rend(); ++it)
        if (moved(*it))
            attempt(it->destination, it->source, it->itemGuid);
    for (auto const& m : moves)
        if (!original(m))
            return TransactionStatus::RecoveryRequired;
    return TransactionStatus::RolledBack;
}
}
