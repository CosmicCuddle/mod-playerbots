// Tests the TEST-ONLY snapshot interface; no AzerothCore/Playerbots source linkage.
// An actual F14 provider still requires approved world-thread creation,
// map-owner/user rights, old-hide restoration and 3.3.5a gameplay testing.
#include "f14_immutable_verified_randombot_snapshot.h"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

using namespace CosmicF14Prototype;

static void Check(bool ok, char const* message)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

static Candidate Good(std::uint32_t guid, std::uint64_t incarnation, std::uint8_t race)
{
    Candidate c;
    c.Guid = guid;
    c.Incarnation = incarnation;
    c.Account = guid + 5000;
    c.Race = race;
    c.Headless = true;
    c.PlayerbotAI = true;
    c.SelfBot = false;
    c.InRandomAccountList = true;
    c.InActiveRandomBotList = true;
    return c;
}

int main()
{
    ImmutableVerifiedRandomBotSnapshot s;
    Check(s.Size() == 0, "safe empty default");
    Candidate c = Good(11, 42, 10);
    Check(!s.IsVerified(11, 42, c.Account, 10), "no identity before producer");
    for (int bit = 0; bit < 10; ++bit)
    {
        Candidate bad = c;
        switch (bit)
        {
        case 0: bad.Guid = 0; break;
        case 1: bad.Incarnation = 0; break;
        case 2: bad.Account = 0; break;
        case 3: bad.Race = 1; break;
        case 4: bad.Headless = false; break;
        case 5: bad.PlayerbotAI = false; break;
        case 6: bad.SelfBot = true; break;
        case 7: bad.InRandomAccountList = false; break;
        case 8: bad.InActiveRandomBotList = false; break;
        case 9: bad.Race = 12; break;
        }
        Check(!s.Publish(bad), "reject all unverified/normal/Altbot identity");
        Check(s.Size() == 0, "rejected producer never changes view");
    }
    Check(s.Publish(c), "verified headless race10 producer");
    Check(s.Size() == 1, "one published actor");
    Check(s.IsVerified(11, 42, c.Account, 10), "exact current actor");
    Check(!s.IsVerified(11, 41, c.Account, 10), "stale incarnation veto");
    Check(!s.IsVerified(11, 42, c.Account + 1, 10), "foreign account veto");
    Check(!s.IsVerified(11, 42, c.Account, 11), "wrong race veto");
    Check(!s.IsVerified(12, 42, c.Account, 10), "wrong guid veto");
    Check(!s.Publish(c), "no repeat publication");
    Check(!s.Publish(Good(11, 41, 10)), "no older reincarnation overwrite");
    Check(s.Publish(Good(11, 43, 11)), "newer different race incarnation");
    Check(!s.Retire(11, 42), "late old logout cannot evict new identity");
    Check(s.IsVerified(11, 43, c.Account, 11), "newer actor survives late retirement");
    Check(s.Retire(11, 43), "exact retirement");
    Check(!s.IsVerified(11, 43, c.Account, 11), "hidden actor cannot persist after committed retire");
    Check(s.Size() == 0, "exact last retirement empties view");
    Check(!s.Publish(Good(11, 43, 11)), "same retired incarnation cannot reappear on delayed login callback");
    Check(!s.Publish(Good(11, 42, 10)), "older retired incarnation cannot reappear");
    Check(!s.Retire(11, 43), "duplicate retirement cannot resurrect an actor");
    Check(s.Publish(Good(11, 44, 10)), "genuinely newer incarnation can be admitted");
    Check(s.IsVerified(11, 44, c.Account, 10), "new incarnation exact stamp is visible");
    Check(s.Retire(11, 44), "new incarnation can retire");
    Check(!s.Publish(Good(11, 44, 10)), "second retired incarnation stays fenced");
    Check(s.Size() == 0, "retired replay fence is not an active actor");
    Check(s.Publish(Good(23, 100, 11)), "verified headless race11 producer");
    Check(s.IsVerified(23, 100, 5023, 11), "race11 positive exact match");

    // Eight concurrent readers + one producer. Readers use ONLY primitive
    // identifying facts and immutable views. No Player/manager access.
    ImmutableVerifiedRandomBotSnapshot concurrent;
    std::atomic<std::uint64_t> checked{0};
    std::vector<std::thread> workers;
    for (std::uint32_t t = 0; t < 8; ++t)
    {
        workers.emplace_back([&concurrent, &checked, t]() {
            std::uint64_t local = 0;
            for (std::uint32_t i = 0; i < 15000; ++i)
            {
                std::uint32_t id = (i * 13 + t * 37) % 64 + 1;
                // These reads may overlap publication/retirement. A positive
                // value is internally coherent, NOT proof that a live object
                // still exists on its owning map.
                local += concurrent.IsVerified(id, i % 700 + 1, id + 5000, 10) ? 1u : 0u;
                local += concurrent.Size() > 64 ? 1u : 0u;
            }
            checked.fetch_add(local + 15000, std::memory_order_relaxed);
        });
    }
    std::thread producer([&concurrent]() {
        for (std::uint64_t epoch = 1; epoch <= 700; ++epoch)
        {
            auto id = std::uint32_t(epoch % 64 + 1);
            bool ok = concurrent.Publish(Good(id, epoch, 10));
            Check(ok, "higher incarnation always publishes");
            if (epoch % 3 == 0)
            {
                Check(concurrent.Retire(id, epoch), "matching retirement");
                Check(!concurrent.Publish(Good(id, epoch, 10)),
                      "old producer event cannot republish just-retired actor");
            }
            if (epoch > 100 && epoch % 7 == 0)
                Check(!concurrent.Retire(id, epoch - 64), "old logout cannot retire newer actor");
        }
    });
    for (std::thread& worker : workers)
        worker.join();
    producer.join();
    Check(concurrent.Size() <= 64, "bounded published membership");
    Check(checked.load() >= 8 * 15000, "all read iterations executed");
    std::printf("PASS: F14 test-only immutable producer/retire/8-worker snapshot, no runtime policy\n");
    return EXIT_SUCCESS;
}
