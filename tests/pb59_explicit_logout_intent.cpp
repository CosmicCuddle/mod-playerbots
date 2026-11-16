#include "AutoGearTimedRetirementMailbox.h"
#include <cassert>
#include <thread>
using M=PB59::TimedRetirementMailbox;
using S=M::Submit;
int main(){
 M m; M::Identity p{1,2,3,4,5,6,7}; assert(m.Register(p));assert(m.Register({8,9,10,11,0,0,0}));
 auto request=[&]{return m.RequestExplicit(1,2,3,5,6,7);};
 assert(m.RequestExplicit(8,9,10,5,6,7)==S::Missing);
 assert(m.RequestExplicit(1,2,3,5,6,99)==S::Missing);
 assert(m.RequestExplicit(1,99,3,5,6,7)==S::Missing);
 assert(m.RequestExplicit(1,2,99,5,6,7)==S::Missing);
 assert(m.RequestExplicit(1,2,3,0,0,0)==S::Missing);
 assert(m.Request(1,2,3)==S::Accepted);assert(request()==S::Busy);
 auto timed=m.Take();assert(timed&&timed->mode==M::Mode::Timed);assert(request()==S::Busy);assert(m.Finish(*timed));
 std::thread producer([&]{assert(request()==S::Accepted);}); producer.join();assert(request()==S::Coalesced);assert(m.Request(1,2,3)==S::Coalesced);
 auto work=m.Take();assert(work&&work->mode==M::Mode::Explicit);assert(request()==S::Coalesced);
 auto wrong=*work;wrong.mode=M::Mode::Timed;assert(!m.Finish(wrong));assert(!m.Finish(*timed));assert(m.FinishExplicit(*work,M::ReceiptStatus::Refused));
 assert(request()==S::Accepted);auto newer=m.Take();assert(newer&&newer->ticket!=work->ticket);assert(!m.Finish(*work));assert(m.FinishExplicit(*newer,M::ReceiptStatus::RecoveryRequired));assert(request()==S::Blocked);
 m.Forget(1,2,3,4);assert(m.Register({1,20,30,4,5,60,70}));assert(request()==S::Missing);assert(!m.Finish(*newer));assert(m.RequestExplicit(1,20,30,5,60,70)==S::Accepted);
 m.Stop();assert(m.RequestExplicit(1,20,30,5,60,70)==S::Stopped);
}
