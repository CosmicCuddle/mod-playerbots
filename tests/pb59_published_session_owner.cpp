#include "AutoGearPublishedSessionOwner.h"
#include <cassert>
#include <stdexcept>
#include <thread>

struct Guid { std::uint64_t value; std::uint64_t GetRawValue() const noexcept { return value; } };
struct Player
{
    Guid guid;
    std::uint64_t owner;
    Guid GetGUID() const noexcept { return guid; }
    std::uint64_t GetPB59OwnerIncarnation() const noexcept { return owner; }
};
struct Session
{
    static inline unsigned destroyed = 0;
    Player original;
    Player* player = &original;
    std::uint64_t session;
    bool headless = true;
    Session(std::uint64_t guid, std::uint64_t owner, std::uint64_t id) : original{{guid},owner}, session(id) { }
    bool IsHeadless() const noexcept { return headless; }
    Player* GetPlayer() const noexcept { return player; }
    std::uint64_t GetPB59SessionIncarnation() const noexcept { return session; }
    ~Session() { assert(!player); ++destroyed; }
};
using P = PB59::PendingSessionOwner<Session>;
using R = PB59::PublishedSessionOwner<Session>;
using C = PB59::SessionCustody<Session>;
using J = PB59::RecoveryRegistry;
using Phase = R::Phase;

static R::Identity Id(J::HolderLifetime const& holder, std::uint64_t guid, std::uint64_t owner, std::uint64_t session)
{ return {guid, owner, holder.Observe()->identity, session}; }
static void Queue(P& pending, P::Marker const& marker)
{ assert(pending.Begin(P::Phase::Loading)); assert(pending.MarkQueued(marker)); pending.End(); assert(pending.Begin(P::Phase::Queued)); }
static auto Start(R::Identity id, J::Kind kind = J::Kind::Logout)
{ auto record = J::Reserve(id.guid,id.player,id.holder,kind); assert(record && record.MarkMutation()); return record; }
static void Publish(R::Identity id, J::HolderLifetime const& holder, P& pending)
{ auto slot=R::Reserve(id,holder.Observe(),pending); assert(slot && slot.Publish(pending)); pending.End(); }

int main()
{
    J::HolderLifetime holder;
    auto id=Id(holder,1,2,3);
    P pending(std::make_unique<Session>(1,2,3)); auto marker=std::make_shared<int>(0); Queue(pending,marker);
    auto* exact=pending.Get();
    assert(!R::Reserve(id,holder.Observe(),pending)); // no implicit binding
    assert(C::BindOwnerThread());
    assert(!R::Reserve({1,2,id.holder,99},holder.Observe(),pending));
    {
        auto slot=R::Reserve(id,holder.Observe(),pending); assert(slot && R::Inspect(id)==Phase::Reserved);
        assert(!R::Reserve(id,holder.Observe(),pending));
        // Reservation is not ownership; dropping it leaves pending intact.
    }
    assert(R::Inspect(id)==Phase::Missing && pending.Get()==exact);
    auto slot=R::Reserve(id,holder.Observe(),pending); assert(slot);
    J::HolderLifetime replacementHolder;
    P wrong(std::make_unique<Session>(1,22,33)); Queue(wrong,marker);
    assert(!slot.Publish(wrong) && wrong.Get());
    std::thread foreign([&]{assert(!slot.Publish(pending));assert(!R::Reserve(Id(replacementHolder,1,22,33),replacementHolder.Observe(),wrong));}); foreign.join();
    assert(slot.Publish(pending) && !pending.Get()); pending.End();
    assert(R::Inspect(id)==Phase::Published && Session::destroyed==0);
    // Nested owner callbacks retain actual session custody, and foreign or
    // mismatched identities cannot borrow. Exceptions release the counter.
    assert(!R::WithPublished({1,2,id.holder,99},[](Session&){assert(false);}));
    std::thread borrower([&]{assert(!R::WithPublished(id,[](Session&){assert(false);}));}); borrower.join();
    assert(R::WithPublished(id,[&](Session& outer){
        assert(&outer==exact);
        assert(R::WithPublished(id,[&](Session& inner){assert(&inner==exact);}));
    }));
    try { R::WithPublished(id,[](Session&){throw std::runtime_error("callback");});assert(false); }
    catch(std::runtime_error const&) {}

    assert(!R::Reserve(Id(replacementHolder,1,22,33),replacementHolder.Observe(),wrong));
    // A callback refuses retirement BEFORE journal mutation; an admitted
    // retirement excludes new callbacks and competing retirement producers.
    assert(R::WithPublished(id,[&](Session& live){
        assert(&live==exact);assert(!R::TryReserveRetirement(id));
    }));
    { auto permit=R::TryReserveRetirement(id);assert(permit);
      assert(!R::TryReserveRetirement(id));
      assert(!R::WithPublished(id,[](Session&){assert(false);}));
      auto record=J::Reserve(id.guid,id.player,id.holder,J::Kind::Disable);assert(record && record.MarkMutation());
      assert(!R::TakeForRecovery(id,permit,record));record.MarkStep(J::Step::AIDeleteReturned);assert(record.Complete()); }
    assert(R::WithPublished(id,[](Session&){}));
    { auto permit=R::TryReserveRetirement(id);assert(permit);
      auto record=J::Reserve(id.guid,id.player,id.holder,J::Kind::Logout);assert(record);
      assert(!R::TakeForRecovery(id,permit,record));assert(record.MarkMutation());
      assert(!R::TakeForRecovery({1,2,id.holder,99},permit,record));
      std::thread t([&]{assert(!R::TryReserveRetirement(id));assert(!R::TakeForRecovery(id,permit,record));});t.join();
      auto handoff=R::TakeForRecovery(id,permit,record);assert(handoff && !record && R::Inspect(id)==Phase::InCustody);
      assert(!handoff.WithSession([](Session&){assert(false);}));
      assert(!handoff.DeleteDetached());
      // Manager disappears after ownership is accepted, never deletes session.
      holder.Detach();bool linked=true;
      assert(handoff.Unlink([&](R::Identity old){assert(old==id);linked=false;return true;}));
      assert(!handoff.Unlink([](R::Identity){assert(false);return false;}));
      assert(handoff.WithSession([&](Session& live){assert(!linked && &live==exact);live.player=nullptr;assert(!handoff.DeleteDetached());assert(!handoff.WithSession([](Session&){assert(false);}));}));
      assert(handoff.DeleteDetached() && Session::destroyed==1 && R::Inspect(id)==Phase::Missing);
      auto report=J::Inspect(id.guid,id.player);assert(report&&report->phase==J::Phase::Completed);
    }
    assert(!J::Inspect(id.guid,id.player));
    // Only completed exact deletion reopens the GUID for another incarnation.
    auto next=Id(replacementHolder,1,22,33);Publish(next,replacementHolder,wrong);assert(R::Inspect(id)==Phase::Missing);
    { auto permit=R::TryReserveRetirement(next);assert(permit);auto record=Start(next);auto handoff=R::TakeForRecovery(next,permit,record);assert(handoff);assert(handoff.Unlink([](R::Identity){return true;}));assert(handoff.WithSession([](Session& s){s.player=nullptr;}));assert(handoff.DeleteDetached()); }
    // Manager loss prior to handoff leaves a real independent owner. No implicit logout.
    J::HolderLifetime lost;auto orphan=Id(lost,4,5,6);P p3(std::make_unique<Session>(4,5,6));Queue(p3,marker);Publish(orphan,lost,p3);lost.Detach();assert(R::Inspect(orphan)==Phase::Orphaned&&Session::destroyed==2);
    { auto permit=R::TryReserveRetirement(orphan);assert(permit);auto record=Start(orphan);auto handoff=R::TakeForRecovery(orphan,permit,record);assert(handoff);assert(handoff.Unlink([](R::Identity){return true;}));assert(handoff.WithSession([](Session& s){s.player=nullptr;}));assert(handoff.DeleteDetached()); }
    // Faults at unlink/after adoption/native veto/throw keep ownership and fence.
    for(unsigned n=0;n<5;++n)
    {
        J::HolderLifetime h;auto old=Id(h,10+n,20+n,30+n);P p(std::make_unique<Session>(old.guid,old.player,old.session));Queue(p,marker);Publish(old,h,p);
        { auto permit=R::TryReserveRetirement(old);assert(permit);auto rec=Start(old);auto handoff=R::TakeForRecovery(old,permit,rec);assert(handoff && !rec);
          if(n==0) { /* lost ticket before unlink */ }
          if(n==1) { assert(!handoff.Unlink([](R::Identity){return false;}));assert(!handoff.WithSession([](Session&){assert(false);})); }
          if(n==2) { try{handoff.Unlink([](R::Identity)->bool{throw std::runtime_error("unlink");});assert(false);}catch(std::runtime_error const&){} assert(!handoff.WithSession([](Session&){assert(false);})); }
          if(n>=3) { assert(handoff.Unlink([](R::Identity){return true;}));if(n==3)assert(handoff.WithSession([](Session&){}));else{try{handoff.WithSession([](Session& s){s.player=nullptr;throw std::runtime_error("after detach");});assert(false);}catch(std::runtime_error const&){} }assert(!handoff.DeleteDetached());assert(!handoff.WithSession([](Session&){assert(false);})); }
        }
        assert(R::Inspect(old)==Phase::RecoveryRequired);auto record=J::Inspect(old.guid,old.player);assert(record&&record->phase==J::Phase::RecoveryRequired);
        P relog(std::make_unique<Session>(old.guid,100+n,200+n));Queue(relog,marker);assert(!R::Reserve(Id(h,old.guid,100+n,200+n),h.Observe(),relog));relog.Get()->player=nullptr;
    }
    J::HolderLifetime h;auto last=Id(h,99,100,101);P p(std::make_unique<Session>(99,100,101));Queue(p,marker);auto reserved=R::Reserve(last,h.Observe(),p);assert(reserved);R::StopPublications();assert(!reserved.Publish(p)&&p.Get());assert(!R::Reserve(last,h.Observe(),p));p.Get()->player=nullptr;
    assert(R::Outstanding()==6 && Session::destroyed==8); // five unresolved + reserved; five synthetic relog destructions
}
