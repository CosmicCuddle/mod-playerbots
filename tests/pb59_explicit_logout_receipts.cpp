#include "AutoGearTimedRetirementMailbox.h"
#include <cassert>
#include <thread>
using M = PB59::TimedRetirementMailbox;
using S = M::Submit;
using R = M::ReceiptStatus;

int main()
{
    M m;
    M::Identity const original{1,2,3,4,5,6,7};
    M::Identity const replacement{1,20,30,40,5,60,70};
    auto submit = [&](M::Identity const& id) {
        return m.RequestExplicit(id.guid,id.player,id.session,id.masterGuid,id.masterPlayer,id.masterSession);
    };
    auto consume = [&](R status) {
        auto receipt = m.PeekReceipt();
        assert(receipt && receipt->status == status);
        auto copy = *receipt;
        assert(m.AcknowledgeReceipt(copy));
        assert(!m.AcknowledgeReceipt(copy));
        return copy;
    };
    assert(m.Register(original));
    assert(submit(original) == S::Accepted);
    assert(submit(original) == S::Coalesced);
    assert(!m.PeekReceipt());
    m.Forget(1,2,99,4); // wrong session cannot cancel
    assert(!m.PeekReceipt());
    m.ForgetHolder(4);
    auto lost = consume(R::OwnerLost);
    assert(lost.work.identity == original);
    assert(!m.Take() && !m.FinishExplicit(lost.work,R::Completed));

    assert(m.Register(original) && submit(original) == S::Accepted);
    auto claimed = m.Take();
    assert(claimed && !m.PeekReceipt());
    m.Forget(1,2,3,4); // successful native unlink while claimed
    assert(!m.PeekReceipt());
    assert(m.Register(replacement));
    assert(submit(replacement) == S::Accepted);
    assert(m.FinishExplicit(*claimed,R::Completed));
    assert(!m.FinishExplicit(*claimed,R::Refused));
    assert(consume(R::Completed).work == *claimed);
    auto next = m.Take();
    assert(next && next->identity == replacement);
    assert(!m.FinishExplicit(*next,R::OwnerLost));
    assert(m.FinishExplicit(*next,R::RecoveryRequired));
    assert(submit(replacement) == S::Blocked);
    assert(consume(R::RecoveryRequired).work == *next);
    m.ForgetHolder(40);

    // Capacity is reserved at acceptance, not on completion. Never evict
    // undelivered outcomes; acknowledgement allows a new request.
    assert(m.Register(original));
    for (std::size_t i=0;i<M::ReceiptCapacity;++i)
    {
        assert(submit(original) == S::Accepted);
        auto work=m.Take();
        assert(work && m.FinishExplicit(*work,R::Refused));
    }
    assert(submit(original) == S::Busy);
    auto first=m.PeekReceipt();
    assert(first);
    auto forged=*first;
    forged.status=R::Completed;
    assert(!m.AcknowledgeReceipt(forged));
    consume(R::Refused);
    assert(submit(original) == S::Accepted);
    m.Stop(); // pending intent is explicitly cancelled, not executed
    assert(!m.Take() && submit(original) == S::Stopped);
    for (std::size_t i=1;i<M::ReceiptCapacity;++i) consume(R::Refused);
    consume(R::Shutdown);
    assert(!m.PeekReceipt());

    // A stop/holder removal cannot turn an already claimed action into a
    // false cancellation. An independent exact completion is still accepted.
    M n;
    assert(n.Register(original));
    std::thread producer([&]{assert(n.RequestExplicit(1,2,3,5,6,7)==S::Accepted);});
    producer.join();
    auto running=n.Take();
    assert(running);
    n.Stop();
    n.ForgetHolder(4);
    assert(!n.PeekReceipt());
    assert(n.FinishExplicit(*running,R::RecoveryRequired));
    auto outcome=n.PeekReceipt();
    assert(outcome && outcome->work==*running && outcome->status==R::RecoveryRequired);
    assert(n.AcknowledgeReceipt(*outcome) && !n.PeekReceipt());
}
