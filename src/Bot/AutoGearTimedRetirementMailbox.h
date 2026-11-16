#pragma once

#include <cstdint>
#include <limits>
#include <map>
#include <mutex>
#include <optional>

namespace PB59
{
// Value-only, process-local mailbox. Register/forget in the serialized native
// owner context; producers submit only their own exact bot/session identity.
// No allocation at request/claim time, no callback under the mailbox lock.
class TimedRetirementMailbox final
{
public:
    using Id = std::uint64_t;
    struct Identity
    {
        Id guid, player, session, holder, masterGuid, masterPlayer, masterSession;
        bool operator==(Identity const&) const = default;
        bool Valid() const noexcept
        {
            return guid && player && session && holder &&
                ((!masterGuid && !masterPlayer && !masterSession) || (masterGuid && masterPlayer && masterSession));
        }
    };
    enum class Mode { Timed, Explicit };
    struct Work { Identity identity; Id ticket; Mode mode = Mode::Timed; };
    enum class Submit { Accepted, Coalesced, Missing, Stopped, Exhausted, Blocked, Busy };
private:
    enum class Phase { Idle, Pending, Claimed, Blocked };
    struct Entry { Identity identity; Phase phase = Phase::Idle; Id ticket = 0; Mode mode = Mode::Timed; };
    std::mutex _lock;
    std::map<Id, Entry> _entries;
    Id _cursor = 0, _lastTicket = 0;
    bool _stopping = false;
public:
    static TimedRetirementMailbox& Instance()
    {
        static auto* mailbox = new TimedRetirementMailbox;
        return *mailbox;
    }
    bool Register(Identity id) noexcept
    {
        if (!id.Valid()) return false;
        try
        {
            std::lock_guard guard(_lock);
            if (_stopping || _entries.contains(id.guid)) return false;
            return _entries.emplace(id.guid, Entry{id}).second;
        }
        catch (...) { return false; }
    }
    void Forget(Id guid, Id player, Id session, Id holder) noexcept
    {
        std::lock_guard guard(_lock);
        auto it = _entries.find(guid);
        if (it != _entries.end())
        {
            auto const& id = it->second.identity;
            if (id.player == player && id.session == session && id.holder == holder) _entries.erase(it);
        }
    }
    void ForgetHolder(Id holder) noexcept
    {
        std::lock_guard guard(_lock);
        for (auto it = _entries.begin(); it != _entries.end();)
            if (it->second.identity.holder == holder) it = _entries.erase(it); else ++it;
    }
    // Value snapshot only; no native object lifetime or retirement authority.
    std::optional<Identity> Lookup(Id guid, Id player, Id session) noexcept
    {
        std::lock_guard guard(_lock);
        auto it = _entries.find(guid);
        if (it == _entries.end() || it->second.identity.player != player ||
            it->second.identity.session != session || it->second.phase == Phase::Blocked)
            return std::nullopt;
        return it->second.identity;
    }
    Submit Request(Id guid, Id player, Id session) noexcept
    {
        std::lock_guard guard(_lock);
        if (_stopping) return Submit::Stopped;
        auto it = _entries.find(guid);
        if (it == _entries.end() || it->second.identity.player != player || it->second.identity.session != session)
            return Submit::Missing;
        auto& e = it->second;
        if (e.phase == Phase::Blocked) return Submit::Blocked;
        if (e.phase != Phase::Idle) return Submit::Coalesced;
        if (_lastTicket == std::numeric_limits<Id>::max()) return Submit::Exhausted;
        e.ticket = ++_lastTicket;
        e.mode = Mode::Timed;
        e.phase = Phase::Pending;
        return Submit::Accepted;
    }
    // Explicit logout authorization is the original personal master, not
    // merely a matching account or the current AI master. No pointers cross.
    Submit RequestExplicit(Id guid, Id player, Id session, Id masterGuid, Id masterPlayer, Id masterSession) noexcept
    {
        std::lock_guard guard(_lock);
        if (_stopping) return Submit::Stopped;
        auto it = _entries.find(guid);
        if (it == _entries.end()) return Submit::Missing;
        auto& e = it->second;
        auto const& id = e.identity;
        if (!masterGuid || !masterPlayer || !masterSession || id.player != player || id.session != session ||
            id.masterGuid != masterGuid || id.masterPlayer != masterPlayer || id.masterSession != masterSession)
            return Submit::Missing;
        if (e.phase == Phase::Blocked) return Submit::Blocked;
        if (e.phase != Phase::Idle)
            return e.mode == Mode::Explicit ? Submit::Coalesced : Submit::Busy;
        if (_lastTicket == std::numeric_limits<Id>::max()) return Submit::Exhausted;
        e.ticket = ++_lastTicket;
        e.mode = Mode::Explicit;
        e.phase = Phase::Pending;
        return Submit::Accepted;
    }
    // Round-robin scan: a repeatedly requesting low GUID cannot starve others.
    // Claimed work is a value copy; the caller must re-resolve all native facts.
    std::optional<Work> Take() noexcept
    {
        std::lock_guard guard(_lock);
        if (_entries.empty()) return std::nullopt;
        auto it = _entries.upper_bound(_cursor);
        if (it == _entries.end()) it = _entries.begin();
        auto start = it;
        do
        {
            auto& e = it->second;
            if (e.phase == Phase::Pending)
            {
                e.phase = Phase::Claimed;
                _cursor = it->first;
                return Work{e.identity, e.ticket, e.mode};
            }
            if (++it == _entries.end()) it = _entries.begin();
        } while (it != start);
        return std::nullopt;
    }
    // Old completion cannot clear a later request or replacement incarnation.
    bool Finish(Work const& work, bool recoveryRequired = false) noexcept
    {
        std::lock_guard guard(_lock);
        auto it = _entries.find(work.identity.guid);
        if (it == _entries.end() || it->second.identity != work.identity ||
            it->second.ticket != work.ticket || it->second.mode != work.mode || it->second.phase != Phase::Claimed) return false;
        it->second.phase = recoveryRequired ? Phase::Blocked : Phase::Idle;
        return true;
    }
    void Stop() noexcept { std::lock_guard guard(_lock); _stopping = true; }
    std::size_t Outstanding() noexcept
    {
        std::lock_guard guard(_lock);
        std::size_t n = 0;
        for (auto const& [guid, e] : _entries) if (e.phase != Phase::Idle) ++n;
        return n;
    }
};
}
