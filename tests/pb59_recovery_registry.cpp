#include "AutoGearRecoveryRegistry.h"
#include "PB59TransactionAdmission.h"
#include <cassert>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>
using R=PB59::RecoveryRegistry;
using A=PB59::TransactionAdmission;
int main()
{
    auto first=std::make_unique<A>();auto holder=std::make_unique<R::HolderLifetime>();
    auto obs=holder->Observe();auto oldOwner=first->OwnerIncarnation(),oldHolder=obs->identity;
    assert(oldOwner&&oldHolder&&obs->Alive());
    assert(!R::Reserve(0,oldOwner,oldHolder,R::Kind::Logout));
    assert(!R::Reserve(1,0,oldHolder,R::Kind::Logout));
    assert(!R::Reserve(1,oldOwner,0,R::Kind::Logout));
    {auto r=R::Reserve(1,oldOwner,oldHolder,R::Kind::Logout);assert(r);
     assert(!R::Reserve(1,oldOwner,oldHolder,R::Kind::Logout));assert(!r.Complete());}
    assert(!R::Inspect(1,oldOwner));
    auto r=R::Reserve(1,oldOwner,oldHolder,R::Kind::Logout);assert(r&&r.MarkMutation());
    auto oldOp=r.Operation();r.MarkStep(R::Step::BeforeNativeLogout);
    assert(!r.Complete());assert(!r.MarkMutation());
    holder.reset();first.reset();assert(!obs->Alive());
    {auto moved=std::move(r);assert(moved&&!r);}
    auto old=R::Inspect(1,oldOwner);assert(old&&old->phase==R::Phase::RecoveryRequired&&old->step==R::Step::BeforeNativeLogout);
    assert(old->identity.owner==oldOwner&&old->identity.holder==oldHolder&&old->identity.operation==oldOp);
    assert(!R::Reserve(1,oldOwner,oldHolder,R::Kind::Logout));
    // Same GUID, new Core ledger + holder: independent record, old one untouched.
    A replacement;R::HolderLifetime newHolder;auto newObs=newHolder.Observe();
    assert(replacement.OwnerIncarnation()!=oldOwner&&newObs->identity!=oldHolder);
    {auto next=R::Reserve(1,replacement.OwnerIncarnation(),newObs->identity,R::Kind::Logout);
     assert(next&&next.Operation()!=oldOp&&next.MarkMutation());next.MarkStep(R::Step::SessionDeleteReturned);assert(next.Complete());
     assert(!R::Reserve(1,replacement.OwnerIncarnation(),newObs->identity,R::Kind::Logout));}
    assert(!R::Inspect(1,replacement.OwnerIncarnation()));assert(R::Inspect(1,oldOwner)->identity==old->identity);
    // Throw at every recorded native boundary; no pointer or owner needed later.
    for(auto step:{R::Step::BeforeBookkeeping,R::Step::BeforeIdentityRetire,R::Step::BeforeGroupQueue,R::Step::BeforeTaxiChange,R::Step::BeforeSave,R::Step::BeforeFarewell,R::Step::BeforeMotion,R::Step::BeforeRepositorySave,R::Step::BeforeHolderErase,R::Step::HolderErased,R::Step::BeforeNativeLogout,R::Step::NativeLogoutReturned,R::Step::BeforeSessionDelete,R::Step::BeforeAIDelete})
    { A a;auto id=a.OwnerIncarnation();try{auto x=R::Reserve(2,id,newObs->identity,R::Kind::Logout);assert(x&&x.MarkMutation());x.MarkStep(step);throw std::runtime_error("fault");}catch(std::runtime_error const&){}
      auto s=R::Inspect(2,id);assert(s&&s->phase==R::Phase::RecoveryRequired&&s->step==step);assert(!R::Reserve(2,id,newObs->identity,R::Kind::Logout)); }
    // Disable completion requires AI deletion, not a logout marker.
    {A a;auto x=R::Reserve(3,a.OwnerIncarnation(),newObs->identity,R::Kind::Disable);assert(x&&x.MarkMutation());x.MarkStep(R::Step::SessionDeleteReturned);assert(!x.Complete());x.MarkStep(R::Step::AIDeleteReturned);assert(x.Complete());}
    // Same owner cannot be reserved by two competing producers. Hold both
    // reservations until both calls finish; no native Player access here.
    for(unsigned i=0;i<100;++i){A a;auto id=a.OwnerIncarnation();R::Reservation x,y;std::thread t([&]{x=R::Reserve(4,id,newObs->identity,R::Kind::Logout);});std::thread u([&]{y=R::Reserve(4,id,newObs->identity,R::Kind::Logout);});t.join();u.join();assert(bool(x)!=bool(y));}
    newHolder.Detach();assert(!newObs->Alive());
}
