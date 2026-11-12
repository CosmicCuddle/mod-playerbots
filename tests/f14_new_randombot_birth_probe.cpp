// Standalone native exercise of the ACTUAL Playerbots F14 observer header.
// No AzerothCore runtime/DB/map/Player pointer is present in this fixture.
#include "F14NewRandomBotBirthProbe.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
using CosmicF14::NewRandomBotBirthProbe;
static void Check(bool ok, char const* what)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); }
}
int main()
{
    auto& r = NewRandomBotBirthProbe::Instance();
    Check(!r.RecordCandidate(0, 11, 10), "zero GUID excluded");
    Check(!r.RecordCandidate(1, 0, 10), "zero account excluded");
    Check(!r.RecordCandidate(1, 11, 1), "humans excluded");
    Check(!r.RecordCandidate(1, 11, 6), "other race excluded");
    Check(!r.ObserveAdmission(1, 11, 10, true, true, true, true), "no invented birth");
    Check(r.RecordCandidate(1, 11, 10), "new BE candidate");
    Check(!r.RecordCandidate(1, 11, 10), "no duplicate");
    Check(!r.RecordCandidate(1, 44, 11), "no account/race replacement");
    Check(!r.ObserveAdmission(1, 99, 10, true, true, true, true), "wrong account");
    Check(!r.ObserveAdmission(1, 11, 11, true, true, true, true), "wrong race");
    for (int n = 0; n < 4; ++n)
        Check(!r.ObserveAdmission(1, 11, 10, n != 0, n != 1, n != 2, n != 3),
              "all four positive login proofs required");
    Check(r.ObserveAdmission(1, 11, 10, true, true, true, true), "verified handoff");
    Check(r.HasUnretiredAdmission(1, 11, 10), "handoff saved");
    Check(!r.ObserveAdmission(1, 11, 10, true, true, true, true), "duplicate login denied");
    r.Retire(1);
    Check(!r.HasUnretiredAdmission(1, 11, 10), "no actor after logout");
    Check(!r.ObserveAdmission(1, 11, 10, true, true, true, true), "retired replay denied");
    Check(!r.RecordCandidate(1, 11, 10), "retired candidate cannot be overwritten");
    r.Retire(1);
    Check(r.RecordCandidate(2, 12, 11), "new Draenei candidate");
    r.Retire(2);
    Check(!r.ObserveAdmission(2, 12, 11, true, true, true, true), "early logout denied");

    std::atomic<int> successes{0};
    std::vector<std::thread> threads;
    for (unsigned t = 0; t < 8; ++t)
        threads.emplace_back([&r, &successes, t]() {
            for (unsigned i = 0; i < 900; ++i)
            {
                std::uint32_t guid = 100u + 900u * t + i;
                std::uint32_t acct = 1000u + guid;
                std::uint8_t race = (i % 2) ? 10 : 11;
                if (r.RecordCandidate(guid, acct, race) &&
                    r.ObserveAdmission(guid, acct, race, true, true, true, true))
                {
                    ++successes;
                    r.Retire(guid);
                }
            }
        });
    for (auto& t : threads) t.join();
    Check(successes.load() == 7200, "eight worker admits exactly once");
    Check(r.CandidateCount() == 7202, "bounded tombstones retained");
    std::puts("PASS actual F14 PB creation-to-login probe: 8 workers; OFF by default");
    return 0;
}
