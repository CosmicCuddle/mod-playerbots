#pragma once

#include <atomic>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

namespace PB59
{
// Process-lifetime retirement journal. Owns records, not Player/session/AI
// pointers. This is NOT a live custody transfer, durable journal or authority
// to replay. Native work still requires a verified serialized owner context.
class RecoveryRegistry final
{
public:
    using Id = std::uint64_t;
    enum class Kind { Logout, Disable };
    enum class Phase { Reserved, InProgress, RecoveryRequired, Completed, Cancelled };
    enum class Step
    {
        Reserved, BeforeBookkeeping, BeforeIdentityRetire, BeforeGroupQueue,
        BeforeTaxiChange, BeforeSave, BeforeFarewell, BeforeMotion,
        BeforeRepositorySave, BeforeHolderErase, HolderErased,
        BeforeNativeLogout, NativeLogoutReturned, BeforeSessionDelete,
        SessionDeleteReturned, BeforeAIDelete, AIDeleteReturned
    };
    struct Identity
    {
        Id guid, owner, holder, operation;
        bool operator==(Identity const&) const = default;
    };
    struct Snapshot
    {
        Identity identity;
        Kind kind;
        Phase phase;
        Step step;
    };
private:
    using Key = std::pair<Id, Id>; // exact GUID + Core ledger incarnation
    struct Entry
    {
        Entry(Identity identity, Kind kind) : identity(identity), kind(kind) { }
        Identity const identity;
        Kind const kind;
        std::atomic<Phase> phase{Phase::Reserved};
        std::atomic<Step> step{Step::Reserved};
        std::atomic<bool> released{false};
    };
    struct State
    {
        std::mutex lock;
        std::map<Key, std::shared_ptr<Entry>> records;
    };
    static std::shared_ptr<State> SharedState()
    {
        static auto state = std::make_shared<State>();
        return state;
    }
    static bool Terminal(Phase phase) noexcept
    {
        return phase == Phase::Completed || phase == Phase::Cancelled;
    }
public:
    // Holder and operation IDs use one non-recycling process counter. Zero is
    // never valid; exhaustion fails closed, independently of the Core IDs.
    static Id NextIdentity() noexcept
    {
        static std::atomic<Id> last{0};
        auto value = last.load(std::memory_order_relaxed);
        while (value != std::numeric_limits<Id>::max())
            if (last.compare_exchange_weak(value, value + 1, std::memory_order_relaxed))
                return value + 1;
        return 0;
    }
    // A holder observer survives the C++ holder only as an invalidation tag.
    // It never pins the actual manager or authorizes foreign-thread access.
    class HolderLifetime final
    {
    public:
        struct Tag
        {
            explicit Tag(Id identity) : identity(identity) { }
            Id const identity;
            std::atomic<bool> attached{true};
            [[nodiscard]] bool Alive() const noexcept
            { return identity && attached.load(std::memory_order_acquire); }
        };
        HolderLifetime() : _tag(std::make_shared<Tag>(NextIdentity())) { }
        ~HolderLifetime() { Detach(); }
        HolderLifetime(HolderLifetime const&) = delete;
        HolderLifetime& operator=(HolderLifetime const&) = delete;
        [[nodiscard]] std::shared_ptr<Tag const> Observe() const noexcept { return _tag; }
        void Detach() noexcept { _tag->attached.store(false, std::memory_order_release); }
    private:
        std::shared_ptr<Tag> _tag;
    };
    class Reservation final
    {
    public:
        Reservation() = default;
        ~Reservation() { Release(); }
        Reservation(Reservation&&) noexcept = default;
        Reservation& operator=(Reservation&& other) noexcept
        {
            if (this != &other)
            {
                Release();
                _state = std::move(other._state);
                _entry = std::move(other._entry);
            }
            return *this;
        }
        Reservation(Reservation const&) = delete;
        Reservation& operator=(Reservation const&) = delete;
        explicit operator bool() const noexcept { return bool(_entry); }
        [[nodiscard]] Id Operation() const noexcept { return _entry ? _entry->identity.operation : 0; }
        [[nodiscard]] bool MarkMutation() noexcept
        {
            auto expected = Phase::Reserved;
            return _entry && _entry->phase.compare_exchange_strong(expected, Phase::InProgress,
                std::memory_order_acq_rel);
        }
        // A Before step records an attempted side effect, NOT proof it ran.
        void MarkStep(Step step) noexcept
        {
            if (_entry && _entry->phase.load(std::memory_order_acquire) == Phase::InProgress)
                _entry->step.store(step, std::memory_order_release);
        }
        // Producer completion only, never a durable DB acknowledgement.
        [[nodiscard]] bool Complete() noexcept
        {
            if (!_entry)
                return false;
            auto final = _entry->kind == Kind::Logout ? Step::SessionDeleteReturned : Step::AIDeleteReturned;
            if (_entry->step.load(std::memory_order_acquire) != final)
                return false;
            auto expected = Phase::InProgress;
            return _entry->phase.compare_exchange_strong(expected, Phase::Completed,
                std::memory_order_acq_rel);
        }
    private:
        friend class RecoveryRegistry;
        Reservation(std::shared_ptr<State> state, std::shared_ptr<Entry> entry) noexcept
            : _state(std::move(state)), _entry(std::move(entry)) { }
        void Release() noexcept
        {
            if (!_entry)
                return;
            auto phase = _entry->phase.load(std::memory_order_acquire);
            if (phase == Phase::InProgress)
                _entry->phase.store(Phase::RecoveryRequired, std::memory_order_release);
            else if (phase == Phase::Reserved)
                _entry->phase.store(Phase::Cancelled, std::memory_order_release);
            _entry->released.store(true, std::memory_order_release);
            if (Terminal(_entry->phase.load(std::memory_order_acquire)))
            {
                // Exact compare-and-erase; an old reservation cannot remove
                // a newer record at the same key. No allocation on unwind.
                try
                {
                    std::lock_guard guard(_state->lock);
                    auto it = _state->records.find({_entry->identity.guid, _entry->identity.owner});
                    if (it != _state->records.end() && it->second == _entry)
                        _state->records.erase(it);
                }
                catch (...) { /* terminal entry can be collected on next exact reservation */ }
            }
            _entry.reset();
            _state.reset();
        }
        std::shared_ptr<State> _state;
        std::shared_ptr<Entry> _entry;
    };
    // Reserve the independent record before any producer effects. Allocation
    // failure has no native effects; unresolved exact owners cannot be retried.
    [[nodiscard]] static Reservation Reserve(Id guid, Id owner, Id holder, Kind kind) noexcept
    {
        if (!guid || !owner || !holder)
            return {};
        try
        {
            auto state = SharedState();
            std::lock_guard guard(state->lock);
            Key key{guid, owner};
            auto it = state->records.find(key);
            if (it != state->records.end())
            {
                if (!it->second->released.load(std::memory_order_acquire) ||
                    !Terminal(it->second->phase.load(std::memory_order_acquire)))
                    return {};
                state->records.erase(it);
            }
            auto operation = NextIdentity();
            if (!operation)
                return {};
            auto entry = std::make_shared<Entry>(Identity{guid, owner, holder, operation}, kind);
            state->records.emplace(key, entry);
            return Reservation(std::move(state), std::move(entry));
        }
        catch (...) { return {}; }
    }
    // Read-only diagnostic; cannot authorize recovery, mutation or deletion.
    [[nodiscard]] static std::optional<Snapshot> Inspect(Id guid, Id owner) noexcept
    {
        try
        {
            auto state = SharedState();
            std::lock_guard guard(state->lock);
            auto it = state->records.find({guid, owner});
            if (it == state->records.end())
                return std::nullopt;
            auto const& e = *it->second;
            auto phase = e.phase.load(std::memory_order_acquire);
            return Snapshot{e.identity, e.kind, phase, e.step.load(std::memory_order_acquire)};
        }
        catch (...) { return std::nullopt; }
    }
};
}
