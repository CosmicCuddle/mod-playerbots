#pragma once

#include <cstdint>
#include <memory>
#include <utility>

namespace PB59
{
// World-owner-context only. Pending and queued login have the same exclusive
// session owner. The queue contains a value identity and a lifetime marker,
// never the session. Native publication explicitly releases to the legacy holder.
template<class Session>
class PendingSessionOwner final
{
public:
    enum class Phase { Loading, Queued, Failed, RecoveryRequired, Released, Closed };
    using Marker = std::shared_ptr<int>;
    explicit PendingSessionOwner(std::unique_ptr<Session> session) noexcept
        : _session(std::move(session)), _identity(_session ? _session->GetPB59SessionIncarnation() : 0) { }
    PendingSessionOwner(PendingSessionOwner const&) = delete;
    PendingSessionOwner& operator=(PendingSessionOwner const&) = delete;
    PendingSessionOwner(PendingSessionOwner&&) = delete;
    PendingSessionOwner& operator=(PendingSessionOwner&&) = delete;
    [[nodiscard]] Session* Get() const noexcept { return _session.get(); }
    [[nodiscard]] std::uint64_t Identity() const noexcept { return _identity; }
    [[nodiscard]] Phase State() const noexcept { return _phase; }
    [[nodiscard]] bool Busy() const noexcept { return _busy; }
    [[nodiscard]] bool Matches(std::uint64_t id) const noexcept { return id && id == _identity && _session && _session->GetPB59SessionIncarnation() == id; }
    [[nodiscard]] bool Begin(Phase expected) noexcept
    {
        if (_busy || _phase != expected || !Matches(_identity)) return false;
        _busy = true;
        return true;
    }
    void End() noexcept { _busy = false; }
    [[nodiscard]] bool MarkQueued(Marker const& marker) noexcept
    {
        if (!_busy || _phase != Phase::Loading || !marker) return false;
        _queue = marker;
        _phase = Phase::Queued;
        return true;
    }
    [[nodiscard]] bool QueueDropped() const noexcept { return _phase == Phase::Queued && _queue.expired(); }
    void Fail() noexcept
    {
        if (_phase == Phase::Loading || _phase == Phase::Queued) _phase = Phase::Failed;
    }
    [[nodiscard]] bool CanPublish(Session const* expected) const noexcept
    {
        return _busy && _phase == Phase::Queued && expected && expected == Get() && Matches(_identity);
    }
    // Called only after exact native holder insertion succeeds, before callbacks.
    [[nodiscard]] bool ReleasePublished(Session const* expected) noexcept
    {
        if (!CanPublish(expected)) return false;
        (void)_session.release();
        _phase = Phase::Released;
        return true;
    }
    // No blind retry after an exception or a logout veto. The pending owner
    // retains the session and marks recovery required until a future reconciler.
    template<class Logout>
    [[nodiscard]] bool CloseFailed(Logout&& logout) noexcept
    {
        if (!Begin(Phase::Failed)) return false;
        _phase = Phase::RecoveryRequired;
        try
        {
            std::forward<Logout>(logout)(*_session);
            if (!_session->GetPlayer())
            {
                _session.reset();
                _phase = Phase::Closed;
            }
        }
        catch (...) { }
        End();
        return _phase == Phase::Closed;
    }
private:
    std::unique_ptr<Session> _session;
    std::uint64_t const _identity;
    std::weak_ptr<int> _queue;
    Phase _phase = Phase::Loading;
    bool _busy = false;
};
}
