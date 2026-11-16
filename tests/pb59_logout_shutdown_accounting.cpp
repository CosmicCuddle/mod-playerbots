#include "AutoGearTimedRetirementMailbox.h"
#include <cassert>

using M = PB59::TimedRetirementMailbox;
using S = M::Submit;
using R = M::ReceiptStatus;

int main()
{
    M m;
    M::Identity const a{1,2,3,4,5,6,7};
    M::Identity const b{8,9,10,11,12,13,14};
    M::Identity const c{15,16,17,18,0,0,0};
    assert(m.Register(a) && m.Register(b) && m.Register(c));
    assert(m.RequestExplicit(1,2,3,5,6,7) == S::Accepted);
    auto work = m.Take();
    assert(work && work->identity == a);
    m.ForgetHolder(4);
    assert(m.Outstanding() == 0); // legacy count misses detached receipt
    auto counts = m.InspectShutdown();
    assert(counts.explicitClaimed == 1 && counts.explicitPending == 0);
    assert(m.RequestExplicit(8,9,10,12,13,14) == S::Accepted);
    assert(m.Request(15,16,17) == S::Accepted);
    counts = m.InspectShutdown();
    assert(counts.explicitPending == 1 && counts.explicitClaimed == 1 && counts.timedPending == 1);
    m.Stop();
    counts = m.InspectShutdown();
    assert(counts.explicitPending == 0 && counts.explicitClaimed == 1 &&
        counts.explicitTerminal == 1 && counts.timedPending == 1);
    auto cancelled = m.PeekReceipt();
    assert(cancelled && cancelled->status == R::Shutdown && m.AcknowledgeReceipt(*cancelled));
    assert(m.FinishExplicit(*work, R::RecoveryRequired));
    counts = m.InspectShutdown();
    assert(counts.explicitClaimed == 0 && counts.explicitTerminal == 1 && counts.blockedEntries == 0);
    auto receipt = m.PeekReceipt();
    assert(receipt && receipt->work == *work && m.AcknowledgeReceipt(*receipt));
    assert(m.InspectShutdown().explicitTerminal == 0);

    M n;
    assert(n.Register(a));
    assert(n.RequestExplicit(1,2,3,5,6,7) == S::Accepted);
    auto claimed = n.Take();
    assert(claimed && n.FinishExplicit(*claimed, R::RecoveryRequired));
    counts = n.InspectShutdown();
    assert(counts.blockedEntries == 1 && counts.explicitTerminal == 1 && counts.explicitClaimed == 0);
    auto terminal = n.PeekReceipt();
    assert(terminal && n.AcknowledgeReceipt(*terminal));
    assert(n.InspectShutdown().blockedEntries == 1);
}
