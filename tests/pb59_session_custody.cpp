#include "AutoGearSessionCustody.h"
#include <cassert>
#include <memory>
#include <stdexcept>
#include <thread>

struct Guid { std::uint64_t value; std::uint64_t GetRawValue() const noexcept { return value; } };
struct Player
{
    Guid guid{1};
    std::uint64_t owner = 2;
    Guid GetGUID() const noexcept { return guid; }
    std::uint64_t GetPB59OwnerIncarnation() const noexcept { return owner; }
};
struct Session
{
    static inline int destroyed = 0;
    bool headless = true;
    std::uint64_t incarnation = 5;
    std::uint64_t GetPB59SessionIncarnation() const noexcept { return incarnation; }
    Player original;
    Player* player = &original;
    bool IsHeadless() const noexcept { return headless; }
    Player* GetPlayer() const noexcept { return player; }
    ~Session() { ++destroyed; }
};
using C = PB59::SessionCustody<Session>;
constexpr C::Identity id{1, 2, 3, 4, 5};
int main()
{
    auto source = std::make_unique<Session>();
    assert(!C::Reserve(id, source.get())); // no implicit thread binding
    assert(C::BindOwnerThread());
    assert(!C::Reserve({}, source.get()));
    assert(!C::Reserve({1,2,3,4,99}, source.get()));
    source->headless = false;
    assert(!C::Reserve(id, source.get()));
    source->headless = true;
    source->player = 0;
    assert(!C::Reserve(id, source.get()));
    source->player = &source->original;
    {
        auto ticket = C::Reserve(id, source.get());
        assert(ticket && C::Inspect(id) == C::Phase::Reserved);
        assert(!C::Reserve(id, source.get()));
        assert(!C::Reserve({2,3,4,5,6}, source.get()));
        assert(!ticket.DeleteDetached(id));
        assert(!ticket.WithSession(id, [](Session&) { assert(false); }));
    }
    assert(C::Inspect(id) == C::Phase::Missing && source && Session::destroyed == 0);
    auto ticket = C::Reserve(id, source.get());
    auto wrong = std::make_unique<Session>();
    assert(!ticket.Adopt(id, wrong) && wrong);
    assert(!ticket.Adopt({1,2,3,99,5}, source) && source);
    auto* exact = source.get();
    std::thread foreign([&] {
        assert(!C::BindOwnerThread());
        assert(!C::Reserve(id, exact));
        assert(!ticket.Adopt(id, source));
        assert(C::Inspect(id) == C::Phase::Missing);
    });
    foreign.join();
    Player impostor;
    source->player = &impostor;
    assert(!ticket.Adopt(id, source) && source);
    source->player = &source->original;
    source->original.owner = 99;
    assert(!ticket.Adopt(id, source) && source);
    source->original.owner = 2;
    source->incarnation = 99;
    assert(!ticket.Adopt(id, source) && source);
    source->incarnation = 5;
    assert(ticket.Adopt(id, source) && !source);
    assert(C::Inspect(id) == C::Phase::Adopted);
    assert(!ticket.DeleteDetached(id));
    wrong->original.owner = 9;
    assert(!C::Reserve({1,9,3,4,5}, wrong.get())); // session incarnation duplicate
    wrong->original.owner = 2;
    wrong->incarnation = 9;
    assert(!C::Reserve({1,2,3,8,9}, wrong.get())); // exact player duplicate
    // A native logout veto returns without detaching the Player. No replay.
    assert(ticket.WithSession(id, [&](Session& live) { assert(&live == exact); }));
    assert(!ticket.DeleteDetached(id));
    assert(C::Inspect(id) == C::Phase::RecoveryRequired);
    assert(!ticket.WithSession(id, [](Session&) { assert(false); }));
    exact->player = nullptr; // later unrelated detachment is not proof of this call
    assert(!ticket.DeleteDetached(id));
    assert(C::Inspect(id) == C::Phase::RecoveryRequired);
    // A throw after detachment is still not a successful native return.
    constexpr C::Identity failed{2, 3, 4, 5, 6};
    auto failing = std::make_unique<Session>();
    failing->original = {{2},3};
    failing->incarnation = 6;
    auto fault = C::Reserve(failed, failing.get());
    assert(fault.Adopt(failed, failing));
    try { fault.WithSession(failed, [](Session& live) { live.player = nullptr; throw std::runtime_error("fault"); }); assert(false); }
    catch (std::runtime_error const&) { }
    assert(C::Inspect(failed) == C::Phase::RecoveryRequired && Session::destroyed == 0);
    assert(!fault.DeleteDetached(failed));
    assert(!fault.WithSession(failed, [](Session&) { assert(false); }));
    constexpr C::Identity success{3, 4, 5, 6, 7};
    auto successful = std::make_unique<Session>();
    successful->original = {{3},4};
    successful->incarnation = 7;
    auto completed = C::Reserve(success, successful.get());
    assert(completed.Adopt(success, successful));
    auto moved = std::move(completed);
    assert(!completed && moved);
    assert(moved.WithSession(success, [&](Session& live) {
        assert(C::Inspect(success) == C::Phase::Busy);
        live.player = nullptr;
        assert(!moved.DeleteDetached(success)); // native stack still active
        assert(!moved.WithSession(success, [](Session&) { assert(false); }));
    }));
    assert(C::Inspect(success) == C::Phase::Detached);
    assert(!moved.DeleteDetached({3,4,5,6,99}));
    assert(moved.DeleteDetached(success) && Session::destroyed == 1);
    assert(C::Inspect(success) == C::Phase::Missing && !moved);
    // Dropping a ticket after adoption preserves the independent live owner.
    constexpr C::Identity old{10,11,12,13,14};
    wrong->original = {{10},11};
    wrong->incarnation = 14;
    {
        auto abandoned = C::Reserve(old, wrong.get());
        assert(abandoned.Adopt(old, wrong) && !wrong);
    }
    assert(C::Inspect(old) == C::Phase::Adopted && Session::destroyed == 1);
    // Same GUID but distinct Player/session incarnation cannot erase old custody.
    auto replacement = std::make_unique<Session>();
    constexpr C::Identity next{10,21,22,23,24};
    replacement->original = {{10},21};
    replacement->incarnation = 24;
    {
        auto fresh = C::Reserve(next, replacement.get());
        assert(fresh && fresh.Adopt(next, replacement));
        assert(!fresh.DeleteDetached(old));
        assert(fresh.WithSession(next, [](Session& live) { live.player = 0; }));
        assert(fresh.DeleteDetached(next));
    }
    assert(C::Inspect(old) == C::Phase::Adopted && Session::destroyed == 2);
}
