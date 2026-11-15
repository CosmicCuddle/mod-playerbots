#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace PB59
{
// Independent headless-session custody primitive. NOT wired to native logout.
// The world-thread owner must bind explicitly, prove exclusive native ownership,
// and use the Core non-recycled session incarnation. No restart durability.
template<class Session>
class SessionCustody final
{
public:
    using Id = std::uint64_t;
    struct Identity
    {
        Id guid, player, holder, operation, session;
        bool operator==(Identity const&) const = default;
        bool Valid() const noexcept { return guid && player && holder && operation && session; }
    };
    enum class Phase { Missing, Reserved, Adopted, Busy, RecoveryRequired, Detached };
private:
    using Key = std::pair<Id, Id>;
    using PlayerPointer = decltype(std::declval<Session&>().GetPlayer());
    static bool MatchesPlayer(Identity id, PlayerPointer player) noexcept
    {
        return player && player->GetGUID().GetRawValue() == id.guid &&
            player->GetPB59OwnerIncarnation() == id.player;
    }
    struct Entry
    {
        Entry(Identity identity, Session* expected) : identity(identity), expected(expected), player(expected->GetPlayer()) { }
        Identity const identity;
        Session* const expected;
        PlayerPointer const player;
        std::unique_ptr<Session> owned;
        bool busy = false;
        bool attempted = false;
        bool detachedOnReturn = false;
    };
    struct State
    {
        std::mutex bindLock;
        std::thread::id owner;
        std::map<Key, std::shared_ptr<Entry>> entries;
    };
    // Deliberately process-lifetime: static destruction cannot implicitly delete
    // an attached session. Native shutdown must explicitly reconcile/drain.
    static State& Store() { static State* state = new State; return *state; }
    static bool OnOwner() noexcept
    {
        try
        {
            auto& state = Store();
            std::lock_guard lock(state.bindLock);
            return state.owner == std::this_thread::get_id();
        }
        catch (...) { return false; }
    }
    static Key KeyOf(Identity id) noexcept { return {id.guid, id.player}; }
    static void EraseExact(std::shared_ptr<Entry> const& entry) noexcept
    {
        auto& entries = Store().entries;
        auto it = entries.find(KeyOf(entry->identity));
        if (it != entries.end() && it->second == entry)
            entries.erase(it);
    }
public:
    static bool BindOwnerThread() noexcept
    {
        try
        {
            auto& state = Store();
            std::lock_guard lock(state.bindLock);
            if (state.owner == std::thread::id{})
                state.owner = std::this_thread::get_id();
            return state.owner == std::this_thread::get_id();
        }
        catch (...) { return false; }
    }
    [[nodiscard]] static bool IsOwnerThread() noexcept { return OnOwner(); }
    class Reservation final
    {
    public:
        Reservation() = default;
        ~Reservation() { Cancel(); }
        Reservation(Reservation&&) noexcept = default;
        Reservation& operator=(Reservation&& other) noexcept
        {
            if (this != &other) { Cancel(); _entry = std::move(other._entry); }
            return *this;
        }
        Reservation(Reservation const&) = delete;
        Reservation& operator=(Reservation const&) = delete;
        explicit operator bool() const noexcept { return bool(_entry); }
        // A record/reservation is not custody. Transfer a real exclusive owner,
        // without allocating, only at the verified native handoff boundary.
        bool Adopt(Identity id, std::unique_ptr<Session>& source) noexcept
        {
            if (!OnOwner() || !_entry || _entry->identity != id || _entry->owned ||
                !source || source.get() != _entry->expected || !source->IsHeadless() ||
                source->GetPB59SessionIncarnation() != id.session ||
                source->GetPlayer() != _entry->player || !MatchesPlayer(id, source->GetPlayer()))
                return false;
            _entry->owned = std::move(source);
            return true;
        }
        // Only the existing operation may borrow during a serialized native
        // call. One attempt only: a throw or veto retains custody, not replay
        // authority. Completion cannot delete on this stack.
        template<class Call>
        bool WithSession(Identity id, Call&& call)
        {
            auto entry = _entry;
            if (!OnOwner() || !entry || entry->identity != id || !entry->owned || entry->busy || entry->attempted ||
                entry->owned->GetPB59SessionIncarnation() != id.session ||
                entry->owned->GetPlayer() != entry->player || !MatchesPlayer(id, entry->owned->GetPlayer()))
                return false;
            entry->attempted = true;
            entry->busy = true;
            struct Reset { Entry& entry; ~Reset() { entry.busy = false; } } reset{*entry};
            std::forward<Call>(call)(*entry->owned);
            entry->detachedOnReturn = !entry->owned->GetPlayer();
            return true;
        }
        // Only after the native call returned and detached its Player. A veto,
        // interrupted logout or attached session must remain independently owned.
        bool DeleteDetached(Identity id) noexcept
        {
            auto entry = _entry;
            if (!OnOwner() || !entry || entry->identity != id || !entry->owned ||
                entry->busy || !entry->detachedOnReturn || entry->owned->GetPB59SessionIncarnation() != id.session ||
                entry->owned->GetPlayer())
                return false;
            auto session = std::move(entry->owned);
            EraseExact(entry);
            _entry.reset();
            session.reset();
            return true;
        }
    private:
        friend class SessionCustody;
        explicit Reservation(std::shared_ptr<Entry> entry) : _entry(std::move(entry)) { }
        void Cancel() noexcept
        {
            // Dropping an adopted ticket never destroys its live session. A
            // foreign-thread drop cannot mutate the owner-thread registry.
            if (_entry && OnOwner() && !_entry->owned)
                EraseExact(_entry);
            _entry.reset();
        }
        std::shared_ptr<Entry> _entry;
    };
    static Reservation Reserve(Identity id, Session* expected) noexcept
    {
        if (!OnOwner() || !id.Valid() || !expected || !expected->IsHeadless() ||
            expected->GetPB59SessionIncarnation() != id.session || !MatchesPlayer(id, expected->GetPlayer()))
            return {};
        try
        {
            auto& entries = Store().entries;
            for (auto const& [key, entry] : entries)
                if (key == KeyOf(id) || entry->identity.session == id.session || entry->expected == expected)
                    return {};
            auto entry = std::make_shared<Entry>(id, expected);
            entries.emplace(KeyOf(id), entry);
            return Reservation(std::move(entry));
        }
        catch (...) { return {}; }
    }
    static Phase Inspect(Identity id) noexcept
    {
        if (!OnOwner() || !id.Valid()) return Phase::Missing;
        auto const& entries = Store().entries;
        auto it = entries.find(KeyOf(id));
        if (it == entries.end() || it->second->identity != id) return Phase::Missing;
        if (it->second->busy) return Phase::Busy;
        auto const& entry = *it->second;
        if (!entry.owned) return Phase::Reserved;
        if (!entry.attempted) return Phase::Adopted;
        return entry.detachedOnReturn && !entry.owned->GetPlayer() ? Phase::Detached : Phase::RecoveryRequired;
    }
};
}
