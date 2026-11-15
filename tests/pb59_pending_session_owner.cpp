#include "AutoGearPendingSessionOwner.h"
#include <cassert>
#include <stdexcept>

struct Session
{
    static inline int destroyed = 0;
    std::uint64_t id;
    bool attached = false;
    std::uint64_t GetPB59SessionIncarnation() const noexcept { return id; }
    void* GetPlayer() const noexcept { return attached ? (void*)this : nullptr; }
    ~Session() { ++destroyed; }
};
using Owner = PB59::PendingSessionOwner<Session>;
using Phase = Owner::Phase;
int main()
{
    // Original allocation remains owned during callback and queued attach.
    {
        Owner pending(std::make_unique<Session>(Session{1}));
        int const before = Session::destroyed;
        auto* exact = pending.Get();
        assert(pending.Matches(1) && !pending.Matches(2));
        assert(!pending.ReleasePublished(exact));
        assert(pending.Begin(Phase::Loading));
        assert(!pending.Begin(Phase::Loading));
        auto marker = std::make_shared<int>(0);
        assert(pending.MarkQueued(marker) && !pending.QueueDropped());
        assert(!pending.CloseFailed([](Session&){ assert(false); }));
        pending.End();
        assert(!pending.CanPublish(exact));
        assert(pending.Begin(Phase::Queued));
        Session other{2};
        assert(!pending.ReleasePublished(&other));
        assert(pending.ReleasePublished(exact));
        assert(pending.State() == Phase::Released && !pending.Get());
        assert(Session::destroyed == before);
        pending.End();
        std::unique_ptr<Session> legacy(exact);
    }
    // Queue rejection/drop leaves owner in place and is cleaned once detached.
    {
        Owner pending(std::make_unique<Session>(Session{3}));
        auto marker = std::make_shared<int>(0);
        assert(pending.Begin(Phase::Loading) && pending.MarkQueued(marker));
        pending.End();
        marker.reset();
        assert(pending.QueueDropped());
        pending.Fail();
        int const before = Session::destroyed;
        assert(pending.CloseFailed([&](Session& live){
            assert(!pending.CloseFailed([](Session&){ assert(false); }));
            live.attached = false;
        }));
        assert(pending.State() == Phase::Closed && Session::destroyed == before + 1);
        assert(!pending.CloseFailed([](Session&){ assert(false); }));
    }
    // Native veto/throw retains exclusive ownership with sticky failure.
    for (int failure=0; failure<2; ++failure)
    {
        Owner pending(std::make_unique<Session>(Session{std::uint64_t(4+failure),true}));
        auto* exact = pending.Get();
        int const before = Session::destroyed;
        pending.Fail();
        assert(!pending.CloseFailed([&](Session& live){
            if (failure) { live.attached=false; throw std::runtime_error("logout fault"); }
        }));
        assert(pending.State()==Phase::RecoveryRequired && pending.Get()==exact && Session::destroyed==before);
        assert(!pending.CloseFailed([](Session&){ assert(false); }));
        // Synthetic test teardown only; production unresolved entries stay owned.
        exact->attached=false;
    }
    // Shutdown or callback failure cannot publish a failed or stale owner.
    {
        Owner pending(std::make_unique<Session>(Session{7}));
        assert(pending.Begin(Phase::Loading));
        pending.Fail();
        assert(!pending.MarkQueued(std::make_shared<int>(0)));
        assert(!pending.CloseFailed([](Session&){ assert(false); }));
        pending.End();
        assert(!pending.Begin(Phase::Queued));
        assert(pending.CloseFailed([](Session&){}));
    }
}
