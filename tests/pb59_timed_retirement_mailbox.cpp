#include "AutoGearTimedRetirementMailbox.h"
#include <cassert>
#include <thread>
#include <vector>

using M = PB59::TimedRetirementMailbox;
using S = M::Submit;
int main()
{
    M m;
    constexpr M::Identity a{1,2,3,4,0,0,0}, b{2,3,4,5,10,11,12}, replacement{1,20,30,40,0,0,0};
    assert(!m.Register({1,2,3,4,10,0,12}));
    assert(m.Register(a) && m.Register(b) && !m.Register(a));
    assert(m.Request(1,2,99)==S::Missing && m.Request(99,2,3)==S::Missing);
    std::vector<std::thread> threads;
    for (int i=0;i<16;++i) threads.emplace_back([&]{for(int n=0;n<1000;++n){auto r=m.Request(1,2,3);assert(r==S::Accepted||r==S::Coalesced);}});
    for(auto& t:threads)t.join();
    assert(m.Outstanding()==1 && m.Request(2,3,4)==S::Accepted);
    auto first=m.Take();assert(first&&first->identity==a);
    assert(m.Request(1,2,3)==S::Coalesced && m.Finish(*first));
    assert(m.Request(1,2,3)==S::Accepted);
    auto second=m.Take();assert(second&&second->identity==b); // fair despite repeated low GUID
    assert(m.Finish(*second));
    auto third=m.Take();assert(third&&third->identity==a&&third->ticket!=first->ticket);
    assert(!m.Finish(*first) && m.Outstanding()==1);
    m.Forget(1,2,99,4);assert(m.Outstanding()==1);
    m.Forget(1,2,3,4);assert(m.Outstanding()==0);
    assert(m.Register(replacement)&&m.Request(1,20,30)==S::Accepted);
    assert(!m.Finish(*third));
    auto fresh=m.Take();assert(fresh&&fresh->identity==replacement);
    assert(!m.Finish({a,fresh->ticket}));
    assert(m.Finish(*fresh)&&!m.Take());
    assert(m.Request(2,3,4)==S::Accepted);
    m.ForgetHolder(5);assert(m.Outstanding()==0&&m.Request(2,3,4)==S::Missing);
    assert(m.Request(1,20,30)==S::Accepted);
    m.Stop();assert(m.Request(1,20,30)==S::Stopped&&!m.Register(b));
    auto last=m.Take();assert(last&&last->identity==replacement&&m.Finish(*last));
    assert(!m.Take()&&m.Outstanding()==0);
}
