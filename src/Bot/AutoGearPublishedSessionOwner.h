#pragma once

#include "AutoGearPendingSessionOwner.h"
#include "AutoGearRecoveryRegistry.h"
#include "AutoGearSessionCustody.h"
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <utility>

namespace PB59
{
// Staged adapter: actual unique_ptr ownership from pending to published to
// independent recovery custody. NOT wired to native manager/retirement yet.
// Native callers must prove serialized owner context and exact map publication.
// The registry protects records, not arbitrary foreign-thread Player access.
template<class Session>
class PublishedSessionOwner final
{
    using Journal = RecoveryRegistry;
    using Custody = SessionCustody<Session>;
    using Pending = PendingSessionOwner<Session>;
    using HolderTag = typename Journal::HolderLifetime::Tag;
public:
    using Id = std::uint64_t;
    struct Identity
    {
        Id guid, player, holder, session;
        bool operator==(Identity const&) const = default;
        bool Valid() const noexcept { return guid && player && holder && session; }
    };
    enum class Phase { Missing, Reserved, Published, Orphaned, InCustody, RecoveryRequired };
private:
    struct Entry
    {
        Entry(Identity id, std::shared_ptr<HolderTag const> holder, Session* expected)
            : identity(id), holder(std::move(holder)), expected(expected) { }
        Identity const identity;
        std::shared_ptr<HolderTag const> const holder;
        Session* const expected;
        std::unique_ptr<Session> owned;
        Phase phase = Phase::Reserved;
        Id operation = 0;
        std::size_t callbacks = 0;
        bool retirementReserved = false;
    };
    struct State
    {
        std::mutex lock;
        // A GUID with unresolved custody blocks republication, even if the
        // Player/session incarnation changes. Never evict a surviving owner.
        std::map<Id, std::shared_ptr<Entry>> entries;
        bool stopping = false;
    };
    static State& Store() { static auto* state = new State; return *state; }
    static bool Matches(Identity id, Session* session) noexcept
    {
        if (!id.Valid() || !session || !session->IsHeadless() ||
            session->GetPB59SessionIncarnation() != id.session) return false;
        auto* p = session->GetPlayer();
        return p && p->GetGUID().GetRawValue() == id.guid && p->GetPB59OwnerIncarnation() == id.player;
    }
    static void EraseExact(State& state, std::shared_ptr<Entry> const& entry) noexcept
    {
        auto it = state.entries.find(entry->identity.guid);
        if (it != state.entries.end() && it->second == entry) state.entries.erase(it);
    }
public:
    class Publication final
    {
    public:
        Publication() = default;
        ~Publication() { Cancel(); }
        Publication(Publication&&) noexcept = default;
        Publication& operator=(Publication&&) = delete;
        Publication(Publication const&) = delete;
        Publication& operator=(Publication const&) = delete;
        explicit operator bool() const noexcept { return bool(_entry); }
        // Caller has inserted the exact holder-map entry; no callbacks between
        // insertion and this transfer. Failed publication leaves pending owner.
        bool Publish(Pending& pending) noexcept
        {
            if (!_entry || !Custody::IsOwnerThread()) return false;
            auto& state = Store();
            std::lock_guard guard(state.lock);
            if (state.stopping || _entry->phase != Phase::Reserved || !_entry->holder->Alive() ||
                !Matches(_entry->identity, pending.Get()) || pending.Get() != _entry->expected ||
                !pending.TransferPublished(_entry->expected, _entry->owned)) return false;
            _entry->phase = Phase::Published;
            return true;
        }
    private:
        friend class PublishedSessionOwner;
        explicit Publication(std::shared_ptr<Entry> e) : _entry(std::move(e)) { }
        void Cancel() noexcept
        {
            if (!_entry) return;
            auto& state = Store();
            std::lock_guard guard(state.lock);
            if (_entry->phase == Phase::Reserved) EraseExact(state, _entry);
            _entry.reset();
        }
        std::shared_ptr<Entry> _entry;
    };
    static Publication Reserve(Identity id, std::shared_ptr<HolderTag const> holder, Pending const& pending) noexcept
    {
        if (!Custody::IsOwnerThread() || !holder || !holder->Alive() || holder->identity != id.holder ||
            !pending.CanPublish(pending.Get()) || !Matches(id, pending.Get())) return {};
        try
        {
            auto& state = Store();
            std::lock_guard guard(state.lock);
            if (state.stopping || state.entries.contains(id.guid)) return {};
            for (auto const& [guid, e] : state.entries)
                if (e->identity.session == id.session || e->expected == pending.Get()) return {};
            auto e = std::make_shared<Entry>(id, std::move(holder), pending.Get());
            state.entries.emplace(id.guid, e);
            return Publication(std::move(e));
        }
        catch (...) { return {}; }
    }
    // Borrow only a genuinely owned published session on the bound owner
    // thread. The registry lock is never held across native callbacks.
    // This prevents this registry's handoff from deleting the session on the
    // callback stack; native destruction routes still have to honor ownership.
    template<class Call>
    static bool WithPublished(Identity id, Call&& call)
    {
        if (!Custody::IsOwnerThread() || !id.Valid())
            return false;
        std::shared_ptr<Entry> entry;
        {
            auto& state = Store();
            std::lock_guard guard(state.lock);
            auto it = state.entries.find(id.guid);
            if (it == state.entries.end())
                return false;
            entry = it->second;
            if (state.stopping || entry->identity != id || entry->phase != Phase::Published ||
                !entry->holder->Alive() || entry->retirementReserved || !Matches(id, entry->owned.get()) ||
                entry->callbacks == std::numeric_limits<std::size_t>::max())
                return false;
            ++entry->callbacks;
        }
        struct Release
        {
            std::shared_ptr<Entry> entry;
            ~Release()
            {
                auto& state = Store();
                std::lock_guard guard(state.lock);
                --entry->callbacks;
            }
        } release{entry};
        std::forward<Call>(call)(*entry->owned);
        // Native callbacks may change the attached Player or retire the holder.
        // Do not report an exact continuation merely because the session survived.
        return entry->holder->Alive() && Matches(id, entry->owned.get());
    }

    // Reserve before the native producer marks mutation or invokes callbacks.
    // This is exclusive against WithPublished and other retirement producers.
    // It is not a Player lifetime pin or authorization to run off-thread.
    class RetirementAdmission final
    {
    public:
        RetirementAdmission() = default;
        ~RetirementAdmission()
        {
            if (_entry)
            {
                auto& state = Store();
                std::lock_guard guard(state.lock);
                _entry->retirementReserved = false;
            }
        }
        RetirementAdmission(RetirementAdmission&&) noexcept = default;
        RetirementAdmission& operator=(RetirementAdmission&&) = delete;
        RetirementAdmission(RetirementAdmission const&) = delete;
        RetirementAdmission& operator=(RetirementAdmission const&) = delete;
        explicit operator bool() const noexcept { return bool(_entry); }
    private:
        friend class PublishedSessionOwner;
        explicit RetirementAdmission(std::shared_ptr<Entry> entry) noexcept : _entry(std::move(entry)) { }
        std::shared_ptr<Entry> _entry;
    };
    static RetirementAdmission TryReserveRetirement(Identity id) noexcept
    {
        if (!Custody::IsOwnerThread() || !id.Valid())
            return {};
        try
        {
            auto& state = Store();
            std::lock_guard guard(state.lock);
            auto it = state.entries.find(id.guid);
            if (it == state.entries.end())
                return {};
            auto entry = it->second;
            if (entry->identity != id || entry->phase != Phase::Published ||
                entry->callbacks || entry->retirementReserved || !Matches(id, entry->owned.get()))
                return {};
            // Orphaned published sessions may be reserved for controlled
            // recovery; the native caller must prove unlink/quiescence.
            entry->retirementReserved = true;
            return RetirementAdmission(std::move(entry));
        }
        catch (...) { return {}; }
    }

    class Handoff final
    {
    public:
        Handoff() = default;
        ~Handoff() { Abandon(); }
        Handoff(Handoff&&) noexcept = default;
        Handoff& operator=(Handoff&&) = delete;
        Handoff(Handoff const&) = delete;
        Handoff& operator=(Handoff const&) = delete;
        explicit operator bool() const noexcept { return bool(_entry); }
        // The native adapter must remove the exact holder entry and quiesce
        // all native users before returning true. This callback is a boundary,
        // not proof that arbitrary native callers obey it. No retry on failure.
        template<class Unlinker>
        bool Unlink(Unlinker&& unlink)
        {
            if (!Active() || _unlinkAttempted) return false;
            _unlinkAttempted = true;
            _record.MarkStep(Journal::Step::BeforeHolderErase);
            _unlinked = std::forward<Unlinker>(unlink)(_entry->identity);
            if (_unlinked) _record.MarkStep(Journal::Step::HolderErased);
            return _unlinked;
        }
        template<class Call>
        bool WithSession(Call&& call)
        {
            if (!Active() || !_unlinked || _logoutAttempted) return false;
            _logoutAttempted = true;
            _record.MarkStep(Journal::Step::BeforeNativeLogout);
            _returned = _custody.WithSession(_id, std::forward<Call>(call));
            if (_returned) _record.MarkStep(Journal::Step::NativeLogoutReturned);
            return _returned;
        }
        // Actual successful custody deletion AND the active journal's native
        // producer completion remove the fence. Neither is DB durability.
        bool DeleteDetached() noexcept
        {
            if (!Active() || !_unlinked || !_returned || Custody::Inspect(_id) != Custody::Phase::Detached)
                return false;
            _record.MarkStep(Journal::Step::BeforeSessionDelete);
            if (!_custody.DeleteDetached(_id)) return false;
            _record.MarkStep(Journal::Step::SessionDeleteReturned);
            bool const completed = _record.Complete();
            auto& state = Store();
            std::lock_guard guard(state.lock);
            if (completed) EraseExact(state, _entry);
            else _entry->phase = Phase::RecoveryRequired;
            _entry.reset();
            return completed;
        }
    private:
        friend class PublishedSessionOwner;
        Handoff(std::shared_ptr<Entry> e, typename Custody::Identity id, typename Custody::Reservation custody,
            Journal::Reservation record)
            : _entry(std::move(e)), _id(id), _custody(std::move(custody)), _record(std::move(record)) { }
        bool Active() const noexcept
        {
            return Custody::IsOwnerThread() && _entry && _record.OwnsInProgress(
                {_id.guid, _id.player, _id.holder, _id.operation}, Journal::Kind::Logout);
        }
        void Abandon() noexcept
        {
            if (!_entry) return;
            auto& state = Store();
            std::lock_guard guard(state.lock);
            _entry->phase = Phase::RecoveryRequired;
            _entry.reset();
        }
        std::shared_ptr<Entry> _entry;
        typename Custody::Identity _id{};
        typename Custody::Reservation _custody;
        Journal::Reservation _record;
        bool _unlinkAttempted = false;
        bool _unlinked = false;
        bool _logoutAttempted = false;
        bool _returned = false;
    };
    // Must run in the independently bound world owner context. The journal
    // identity is checked, but native retirement admission/manager quiescence
    // still belong to the caller. Do not use this as cross-thread pointer proof.
    static Handoff TakeForRecovery(Identity id, RetirementAdmission const& admission, Journal::Reservation& record) noexcept
    {
        if (!Custody::IsOwnerThread() || !id.Valid() || !admission._entry) return {};
        auto operation = record.Operation();
        if (!operation || !record.OwnsInProgress({id.guid, id.player, id.holder, operation}, Journal::Kind::Logout)) return {};
        try
        {
            auto& state = Store();
            std::lock_guard guard(state.lock);
            auto it = state.entries.find(id.guid);
            if (it == state.entries.end()) return {};
            auto e = it->second;
            if (e != admission._entry || !e->retirementReserved || e->identity != id ||
                e->phase != Phase::Published || e->callbacks || !Matches(id, e->owned.get())) return {};
            typename Custody::Identity custodyId{id.guid, id.player, id.holder, operation, id.session};
            auto ticket = Custody::Reserve(custodyId, e->owned.get());
            if (!ticket || !ticket.Adopt(custodyId, e->owned)) return {};
            e->operation = operation;
            e->phase = Phase::InCustody;
            return Handoff(std::move(e), custodyId, std::move(ticket), std::move(record));
        }
        catch (...) { return {}; }
    }
    // Pointer-free diagnostics. An orphan is still owned, not safe to replay.
    static Phase Inspect(Identity id) noexcept
    {
        auto& state = Store();
        std::lock_guard guard(state.lock);
        auto it = state.entries.find(id.guid);
        if (it == state.entries.end() || it->second->identity != id) return Phase::Missing;
        auto const& e = *it->second;
        if (e.phase == Phase::Published && !e.holder->Alive()) return Phase::Orphaned;
        return e.phase;
    }
    static void StopPublications() noexcept
    {
        if (!Custody::IsOwnerThread()) return;
        auto& state = Store();
        std::lock_guard guard(state.lock);
        state.stopping = true;
    }
    static std::size_t Outstanding() noexcept
    {
        auto& state = Store();
        std::lock_guard guard(state.lock);
        return state.entries.size();
    }
};
}
