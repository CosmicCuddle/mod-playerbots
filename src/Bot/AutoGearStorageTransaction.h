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

// Bag identity is zero for the base inventory (equipment or backpack).
// The backend must return unknown for missing/replaced/unreadable containers,
// not an empty slot. Count is zero if and only if the slot is empty.
struct ExactSlot
{
    std::uint64_t itemGuid, bagGuid;
    std::uint32_t count;
    bool operator==(ExactSlot const&) const = default;
};
struct ExactMove
{
    Move position;
    std::uint32_t count;
    std::uint64_t destinationBagGuid;
};

// Final-B admission is an immutable, already validated command ticket, not
// a renewable sender lease. Later sender changes are diagnostic only.
// ContextValid checks only the exact live BOT owner/lifetime/serialization;
// it must be non-yielding, read-only, and remain false after losing that lease.
// Both inspection and restoration require that independent bot context.
// MoveExact receives the full reservation in BOTH directions and must never
// destroy/merge/overwrite, even if it detects a stale reservation internally.
template<class Inspect, class ContextValid, class MoveExact>
TransactionStatus Execute(std::span<ExactMove const> moves, bool admittedFinalB,
    Inspect inspect, ContextValid contextValid, MoveExact moveExact)
{
    if (!admittedFinalB || moves.size() > 17)
        return TransactionStatus::Rejected;
    Move positions[17]{};
    for (std::size_t i = 0; i < moves.size(); ++i)
    {
        if (!moves[i].count)
            return TransactionStatus::Rejected;
        positions[i] = moves[i].position;
    }
    if (!ValidMoves(std::span<Move const>(positions, moves.size())))
        return TransactionStatus::Rejected;
    auto context = [&]()
    {
        try { return contextValid(); }
        catch (...) { return false; }
    };
    if (!context())
        return TransactionStatus::Rejected;
    auto at = [&](std::uint16_t position, ExactSlot expected)
    {
        if (!context())
            return false;
        try { return inspect(position) == std::optional<ExactSlot>(expected); }
        catch (...) { return false; }
    };
    auto original = [&](ExactMove const& m)
    {
        return at(m.position.source, {m.position.itemGuid, 0, m.count}) &&
            at(m.position.destination, {0, m.destinationBagGuid, 0});
    };
    auto moved = [&](ExactMove const& m)
    {
        return at(m.position.source, {0, 0, 0}) &&
            at(m.position.destination, {m.position.itemGuid, m.destinationBagGuid, m.count});
    };
    for (auto const& m : moves)
        if (!original(m))
            return TransactionStatus::Rejected;

    bool attempted = false;
    auto attempt = [&](ExactMove const& m, bool restore)
    {
        if (!context())
            return false;
        attempted = true; // a throwing backend may already have moved the item
        try { return moveExact(m, restore); }
        catch (...) { return false; }
    };
    bool success = true;
    for (auto const& m : moves)
    {
        if (!context() || !original(m) || !attempt(m, false) || !moved(m))
        {
            success = false;
            break;
        }
    }
    if (success && context())
    {
        for (auto const& m : moves)
            if (!moved(m))
            {
                success = false;
                break;
            }
        if (success && context())
            return TransactionStatus::Committed;
    }

    if (!attempted)
        return TransactionStatus::Rejected;
    // Observe actual state after failures, including exceptions. Never move an
    // item from a replacement bag or a stack whose count changed. If exact
    // restoration cannot be proven, return RecoveryRequired, not RolledBack.
    for (auto it = moves.rbegin(); it != moves.rend(); ++it)
    {
        if (!context())
            return TransactionStatus::RecoveryRequired;
        if (moved(*it))
            attempt(*it, true);
    }
    for (auto const& m : moves)
        if (!original(m))
            return TransactionStatus::RecoveryRequired;
    return context() ? TransactionStatus::RolledBack : TransactionStatus::RecoveryRequired;
}
}
