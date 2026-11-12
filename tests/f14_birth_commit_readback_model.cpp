// Native test of the actual new PB7-child F14 in-process evidence registry.
// No character DB or Worldserver is available to this test, so no committed
// transaction, new saved birth, IP stage grant or client hide is proven.
#include "F14NewRandomBotBirthProbe.h"
#include <cstdio>
#include <cstdlib>

using CosmicF14::NewRandomBotBirthProbe;
static void Require(bool good, char const* description)
{
    if (!good)
    {
        std::fprintf(stderr, "FAIL: %s\n", description);
        std::exit(1);
    }
}

int main()
{
    auto& r = NewRandomBotBirthProbe::Instance();
    Require(!r.MarkCommittedRowReadback(90001, 100, 10), "no invented DB receipt");
    Require(!r.HasAuditedRowAndUnretiredAdmission(90001, 100, 10), "no invented admission");
    Require(r.RecordCandidate(90001, 100, 10), "first Blood Elf candidate");
    Require(!r.HasAuditedRowAndUnretiredAdmission(90001, 100, 10), "candidate alone insufficient");
    Require(!r.MarkCommittedRowReadback(90001, 999, 10), "wrong account rejected");
    Require(!r.MarkCommittedRowReadback(90001, 100, 11), "wrong race rejected");
    Require(!r.HasAuditedRowAndUnretiredAdmission(90001, 100, 10), "wrong identity no receipt");
    Require(r.MarkCommittedRowReadback(90001, 100, 10), "separate DB observer bit");
    Require(!r.MarkCommittedRowReadback(90001, 100, 10), "duplicate receipt rejected");
    Require(!r.HasAuditedRowAndUnretiredAdmission(90001, 100, 10), "without login still denied");
    Require(!r.ObserveAdmission(90001, 100, 10, false, true, true, true), "unverified bot denied");
    Require(!r.HasAuditedRowAndUnretiredAdmission(90001, 100, 10), "no forged login admission");
    Require(r.ObserveAdmission(90001, 100, 10, true, true, true, true), "positive headless login observation");
    Require(r.HasAuditedRowAndUnretiredAdmission(90001, 100, 10), "audit and login both observed");
    Require(!r.HasAuditedRowAndUnretiredAdmission(90001, 100, 11), "race cannot change");
    Require(!r.HasAuditedRowAndUnretiredAdmission(90001, 999, 10), "account cannot change");
    r.Retire(90001);
    Require(!r.HasAuditedRowAndUnretiredAdmission(90001, 100, 10), "retired record denied");
    Require(!r.MarkCommittedRowReadback(90001, 100, 10), "late callback after retirement denied");
    Require(!r.RecordCandidate(90001, 100, 10), "no reused GUID admission");

    Require(r.RecordCandidate(90002, 101, 11), "Draenei candidate");
    Require(r.ObserveAdmission(90002, 101, 11, true, true, true, true), "Draenei login only");
    Require(!r.HasAuditedRowAndUnretiredAdmission(90002, 101, 11), "login alone never DB proof");
    r.Retire(90002);
    Require(!r.MarkCommittedRowReadback(90002, 101, 11), "stale DB callback after retire denied");
    Require(!r.RecordCandidate(90003, 110, 1), "no human candidate");
    Require(!r.MarkCommittedRowReadback(90003, 110, 1), "no human receipt");
    std::puts("PASS: F14 PB committed-row plus login gating model, all fail closed");
    return 0;
}
