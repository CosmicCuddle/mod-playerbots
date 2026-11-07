// TEST-ONLY negative contract: an observed PB #3 RandomBot is never proof
// that Player::SaveToDB's transaction committed or old data was absent.
// Include the ACTUAL owner production diagnostic header; NO DB or server.
#include "F14NewRandomBotBirthProbe.h"
#include "f14_synthetic_db_commit_receipt_model.h"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

using CosmicF14::NewRandomBotBirthProbe;
using CosmicF14TestOnly::PriorData;
using CosmicF14TestOnly::SyntheticCommitProofLedger;

static void Check(bool result, char const* message)
{
    if (!result)
    {
        std::fprintf(stderr, "FAIL F14 synthetic transaction guard: %s\n", message);
        std::exit(1);
    }
}

static bool Start(SyntheticCommitProofLedger& ledger, std::uint32_t guid,
                  std::uint32_t account, std::uint8_t race, std::uint64_t nonce)
{
    return ledger.Begin(guid, account, race, nonce,
                        PriorData::Absent, PriorData::Absent, PriorData::Absent);
}

int main()
{
    SyntheticCommitProofLedger ledger;
    NewRandomBotBirthProbe& pb3 = NewRandomBotBirthProbe::Instance();

    // Unknown/old character/quests/paid receipt must always fail CLOSED.
    for (PriorData value : {PriorData::Unknown, PriorData::Present})
    {
        Check(!ledger.Begin(90, 77, 10, 901, value,
                            PriorData::Absent, PriorData::Absent),
              "unknown or existing character denies preflight");
        Check(!ledger.Begin(90, 77, 10, 901, PriorData::Absent,
                            value, PriorData::Absent),
              "unknown or existing quest history denies preflight");
        Check(!ledger.Begin(90, 77, 10, 901, PriorData::Absent,
                            PriorData::Absent, value),
              "unknown or existing paid history denies preflight");
    }
    Check(!Start(ledger, 0, 77, 10, 901), "zero guid");
    Check(!Start(ledger, 90, 0, 10, 901), "zero account");
    Check(!Start(ledger, 90, 77, 10, 0), "zero transaction nonce");
    Check(!Start(ledger, 90, 77, 1, 901), "normal human race");
    Check(!Start(ledger, 90, 77, 6, 901), "other race");

    // Candidate, real headless login, and globally drained queue by
    // themselves must NEVER produce evidence-complete.
    Check(Start(ledger, 100, 77, 10, 901), "valid synthetic preflight");
    Check(pb3.RecordCandidate(100, 77, 10), "actual PB3 creation candidate");
    Check(pb3.ObserveAdmission(100, 77, 10, true, true, true, true),
          "actual PB3 admitted event");
    Check(!ledger.EvidenceCompleteForReview(100, 77, 10, 901),
          "PB3 full admission alone never proves DB commit");
    Check(!ledger.ObserveAdmission(100, 77, 10, 901,
                                  pb3.HasUnretiredAdmission(100, 77, 10),
                                  true, true, true, true),
          "login before commit+readback is not sufficient");
    Check(!ledger.ConfirmReadback(100, 77, 10, 901, true, true, true),
          "post-save SELECT before commit result is not sufficient");
    Check(!ledger.ConfirmCommit(100, 999, true), "wrong nonce cannot commit");
    Check(!ledger.ConfirmCommit(101, 901, true), "wrong GUID cannot commit");

    // A FAILED async transaction result permanently retires this attempt.
    Check(!ledger.ConfirmCommit(100, 901, false), "failed DB commit denied");
    Check(!ledger.ConfirmCommit(100, 901, true),
          "late success cannot revive a failed commit");
    Check(!ledger.ConfirmReadback(100, 77, 10, 901, true, true, true),
          "readback cannot rescue failed commit");
    Check(!ledger.EvidenceCompleteForReview(100, 77, 10, 901),
          "PB3 admission does not override failed commit");
    pb3.Retire(100);
    Check(!Start(ledger, 100, 77, 10, 902), "GUID tombstone not reissued");
    Check(!Start(ledger, 101, 77, 11, 901), "transaction nonce not reused");

    // True synthetic success requires three separate observed facts:
    // matched preflight, callback SUCCESS, post-commit row/history checks.
    Check(Start(ledger, 102, 78, 11, 902), "separate Draenei preflight");
    Check(pb3.RecordCandidate(102, 78, 11), "separate real PB3 candidate");
    Check(!ledger.ConfirmReadback(102, 78, 11, 902, true, true, true),
          "out-of-order readback rejected");
    Check(ledger.ConfirmCommit(102, 902, true), "synthetic commit success");
    Check(!ledger.EvidenceCompleteForReview(102, 78, 11, 902),
          "transaction success alone not enough");
    Check(!ledger.ObserveAdmission(102, 78, 11, 902,
                                  false, true, true, true, true),
          "cannot fabricate PB admission");
    Check(ledger.ConfirmReadback(102, 78, 11, 902, true, true, true),
          "matched readback and no old data");
    Check(!ledger.EvidenceCompleteForReview(102, 78, 11, 902),
          "readback alone does not prove genuine random bot");
    Check(pb3.ObserveAdmission(102, 78, 11, true, true, true, true),
          "PB3 observed actual matching login");
    Check(!ledger.ObserveAdmission(102, 78, 11, 902,
                                  pb3.HasUnretiredAdmission(102, 78, 11),
                                  false, true, true, true), "human socket rejected");
    Check(!ledger.ObserveAdmission(102, 78, 11, 902,
                                  pb3.HasUnretiredAdmission(102, 78, 11),
                                  true, false, true, true), "missing AI rejected");
    Check(!ledger.ObserveAdmission(102, 78, 11, 902,
                                  pb3.HasUnretiredAdmission(102, 78, 11),
                                  true, true, false, true), "not in world rejected");
    Check(!ledger.ObserveAdmission(102, 78, 11, 902,
                                  pb3.HasUnretiredAdmission(102, 78, 11),
                                  true, true, true, false), "not RandomBot rejected");
    Check(!ledger.ObserveAdmission(102, 78, 10, 902,
                                  true, true, true, true, true), "race swap rejected");
    Check(!ledger.ObserveAdmission(102, 999, 11, 902,
                                  true, true, true, true, true), "account swap rejected");
    Check(ledger.ObserveAdmission(102, 78, 11, 902,
                                 pb3.HasUnretiredAdmission(102, 78, 11),
                                 true, true, true, true),
          "all independent synthetic proof stages observed");
    Check(ledger.EvidenceCompleteForReview(102, 78, 11, 902),
          "synthetic evidence complete FOR REVIEW ONLY, no grant");
    Check(!ledger.ObserveAdmission(102, 78, 11, 902,
                                  true, true, true, true, true), "relogin does not reconsume");
    pb3.Retire(102);
    ledger.Retire(102);
    Check(!ledger.EvidenceCompleteForReview(102, 78, 11, 902),
          "logout removes evidence and retains tombstone");
    Check(!ledger.ConfirmCommit(102, 902, true), "old callback cannot revive");

    // Readback error or preexisting history poisons the attempt.
    for (int error = 0; error < 3; ++error)
    {
        std::uint32_t const guid = 200 + error;
        std::uint64_t const nonce = 1200 + error;
        Check(Start(ledger, guid, 80, 10, nonce), "negative readback candidate");
        Check(ledger.ConfirmCommit(guid, nonce, true), "negative case commit");
        Check(!ledger.ConfirmReadback(guid, 80, 10, nonce,
                                     error != 0, error != 1, error != 2),
              "missing row/old quest/paid prevents full evidence");
        Check(!ledger.EvidenceCompleteForReview(guid, 80, 10, nonce),
              "poisoned readback not ready");
        Check(!ledger.ConfirmReadback(guid, 80, 10, nonce, true, true, true),
              "later clean readback cannot unpoison");
    }

    // Optional concurrency stress: same GUID/nonce cannot be consumed twice.
    constexpr int workers = 8, perWorker = 160;
    std::atomic<int> completed{0};
    std::vector<std::thread> threads;
    for (int worker = 0; worker < workers; ++worker)
    {
        threads.emplace_back([&ledger, &pb3, &completed, worker]() {
            for (int i = 0; i < perWorker; ++i)
            {
                std::uint32_t guid = 1000u + worker * perWorker + i;
                std::uint32_t acct = 5000u + guid;
                std::uint64_t nonce = 100000u + guid;
                std::uint8_t race = (i % 2) ? 10 : 11;
                bool ok = Start(ledger, guid, acct, race, nonce);
                ok = ok && pb3.RecordCandidate(guid, acct, race);
                ok = ok && ledger.ConfirmCommit(guid, nonce, true);
                ok = ok && ledger.ConfirmReadback(guid, acct, race, nonce,
                                                  true, true, true);
                ok = ok && pb3.ObserveAdmission(guid, acct, race,
                                                true, true, true, true);
                ok = ok && ledger.ObserveAdmission(
                    guid, acct, race, nonce,
                    pb3.HasUnretiredAdmission(guid, acct, race),
                    true, true, true, true);
                if (ok && ledger.EvidenceCompleteForReview(guid, acct, race, nonce))
                    ++completed;
                ledger.Retire(guid);
                pb3.Retire(guid);
            }
        });
    }
    for (auto& t : threads)
        t.join();
    Check(completed.load() == workers * perWorker,
          "eight workers complete synthetic stages once");
    Check(!ledger.EvidenceCompleteForReview(1000, 6000, 11, 101000),
          "retired concurrent actor has no ready evidence");

    std::puts("PASS: actual PB3 observation + synthetic callback/readback protocol; "
              "no SQL, IP stage, paid entitlement, client or live server");
    return 0;
}
