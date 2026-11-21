// Flight-path watchdog policy (coordination #57 / C2). Pure model: steps a clock in the same
// 5-second samples the PlayerbotAI hook uses. It does not prove Core taxi behaviour in game.
#include <array>
#include <deque>
#include <iostream>
#include <string>

#include "TaxiWatchdog.h"

namespace
{
using namespace PlayerbotsTaxiWatchdog;

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, std::string const& what)
{
    ++g_checks;
    if (!ok)
    {
        ++g_failures;
        std::cerr << "FAIL: " << what << "\n";
    }
}

constexpr std::uint64_t Step = 5000;

Config Enabled()
{
    Config config;
    config.enabled = true;
    return config;
}

Sample Flying(std::uint64_t now, float x, std::uint32_t node = 3)
{
    Sample sample;
    sample.nowMs = now;
    sample.eligible = true;
    sample.taxiState = true;
    sample.flightGenerator = true;
    sample.splineMoving = true;
    sample.mapId = 0;
    sample.node = node;
    sample.x = x;
    return sample;
}

// Bot keeps taxi flags but the spline is gone: the upstream #2863 shape.
Sample Stuck(std::uint64_t now, float x = 100.0f)
{
    Sample sample = Flying(now, x);
    sample.flightGenerator = false;
    sample.splineMoving = false;
    return sample;
}

// Runs samples from start to end (inclusive) and returns the first time a Recover appears, or 0.
template <class Make>
std::uint64_t FirstRecover(Watchdog& dog, Config const& config, std::uint64_t start, std::uint64_t end, Make make)
{
    for (std::uint64_t now = start; now <= end; now += Step)
        if (dog.Observe(make(now), config) == Action::Recover)
            return now;
    return 0;
}

void TestLongFlightNeverTriggers()
{
    Watchdog dog;
    Config const config = Enabled();
    // A 40-minute flight moving 160 yards per sample.
    std::uint64_t const hit =
        FirstRecover(dog, config, 0, 40 * 60 * 1000, [](std::uint64_t now) { return Flying(now, now / 31.0f); });
    Check(hit == 0, "a progressing 40-minute flight never recovers");
    // Node advancing alone also counts, even if position samples coincide.
    Watchdog nodes;
    std::uint64_t const nodeHit = FirstRecover(nodes, config, 0, 30 * 60 * 1000, [](std::uint64_t now)
                                               { return Flying(now, 0.0f, static_cast<std::uint32_t>(now / 60000)); });
    Check(nodeHit == 0, "route node advancing every minute is progress");
}

void TestExactBoundary()
{
    Watchdog dog;
    Config const config = Enabled();
    Check(dog.Observe(Stuck(0), config) == Action::None, "first stuck sample starts tracking");
    Check(dog.Observe(Stuck(config.stuckMs - Step), config) == Action::None, "895 s without progress: no action");
    Check(dog.Observe(Stuck(config.stuckMs - 1), config) == Action::None, "899.999 s: no action");
    Check(dog.Observe(Stuck(config.stuckMs), config) == Action::Recover, "exactly 900 s: recover");
}

void TestProgressResetsTimer()
{
    Watchdog dog;
    Config const config = Enabled();
    dog.Observe(Flying(0, 0.0f), config);
    // Spline stalls in place for 800 s, then moves once, then stalls again.
    std::uint64_t now = 0;
    for (; now <= 800000; now += Step)
        Check(dog.Observe(Flying(now, 0.0f), config) == Action::None, "stalled < 900 s");
    Check(dog.Observe(Flying(now, 50.0f), config) == Action::None, "movement resets the timer");
    std::uint64_t const progressAt = now;
    std::uint64_t const hit = FirstRecover(dog, config, progressAt + Step, progressAt + 2 * config.stuckMs,
                                           [](std::uint64_t t) { return Flying(t, 50.0f); });
    Check(hit == progressAt + config.stuckMs, "timer counts from the last progress, not the flight start");
    Check(dog.LastProgressMs() == progressAt || dog.Recoveries() == 0, "last progress recorded");
}

void TestGroundWalkWithTaxiFlagsIsNotProgress()
{
    Watchdog dog;
    Config const config = Enabled();
    // Bot walks around (position changes) while taxi flags stay set and no flight spline runs.
    std::uint64_t const hit =
        FirstRecover(dog, config, 0, config.stuckMs, [](std::uint64_t now) { return Stuck(now, now / 100.0f); });
    Check(hit == config.stuckMs, "walking with stale taxi flags is not flight progress");
}

void TestRouteCompletionClears()
{
    Watchdog dog;
    Config const config = Enabled();
    dog.Observe(Stuck(0), config);
    Sample landed = Stuck(400000);
    landed.taxiState = false;
    Check(dog.Observe(landed, config) == Action::None && !dog.Tracking(), "landing clears tracking");
    Check(dog.Observe(Stuck(400000 + Step), config) == Action::None, "next flight starts a fresh timer");
    Check(dog.Observe(Stuck(900000), config) == Action::None, "old flight time is not carried over");
}

void TestTeleportAndMapChange()
{
    Config const config = Enabled();
    {
        Watchdog dog;
        dog.Observe(Stuck(0), config);
        Sample teleport = Stuck(850000);
        teleport.transitional = true;
        Check(dog.Observe(teleport, config) == Action::None, "teleport in progress is never recovered");
        Check(dog.Observe(Stuck(config.stuckMs), config) == Action::None,
              "900 s after the flight start but 50 s after a teleport: no action");
        Check(dog.Observe(Stuck(850000 + config.stuckMs), config) == Action::Recover,
              "timer runs again from the teleport");
    }
    {
        Watchdog dog;
        dog.Observe(Stuck(0), config);
        Sample otherMap = Stuck(850000);
        otherMap.mapId = 1;
        Check(dog.Observe(otherMap, config) == Action::None, "cross-map hop is not recovered");
        otherMap.nowMs = config.stuckMs;
        Check(dog.Observe(otherMap, config) == Action::None, "cross-map hop restarts the timer");
        otherMap.nowMs = 850000 + config.stuckMs;
        Check(dog.Observe(otherMap, config) == Action::Recover, "stuck on the new map is still caught");
    }
}

void TestNotEligible()
{
    Config const config = Enabled();
    for (int variant = 0; variant < 3; ++variant)
    {
        Watchdog dog;
        std::uint64_t const hit = FirstRecover(dog, config, 0, 2 * config.stuckMs,
                                               [variant](std::uint64_t now)
                                               {
                                                   Sample sample = Stuck(now);
                                                   if (variant == 0)
                                                       sample.eligible = false;  // human, self-bot, or dead bot
                                                   if (variant == 1)
                                                       sample.taxiState = false;  // not on a taxi at all
                                                   return sample;
                                               });
        Config disabled = config;
        disabled.enabled = variant != 2;
        Watchdog off;
        std::uint64_t const offHit =
            FirstRecover(off, disabled, 0, 2 * config.stuckMs, [](std::uint64_t now) { return Stuck(now); });
        if (variant < 2)
            Check(hit == 0, "ineligible or non-taxi bots are never recovered");
        else
            Check(offHit == 0, "watchdog disabled never recovers");
    }
}

void TestBoundedRetries()
{
    Watchdog dog;
    Config const config = Enabled();
    std::uint64_t now = 0;
    int recoveries = 0;
    int giveUps = 0;
    std::uint64_t lastRecovery = 0;
    bool spacingOk = true;
    // Recovery never clears the stuck state in this run: worst case for loops.
    for (; now <= 3 * 3600000ull; now += Step)
    {
        Action const action = dog.Observe(Stuck(now), config);
        if (action == Action::Recover)
        {
            if (recoveries && now - lastRecovery < config.stuckMs)
                spacingOk = false;
            ++recoveries;
            lastRecovery = now;
            dog.Recovered(now);
        }
        giveUps += action == Action::GiveUp;
    }
    Check(recoveries == static_cast<int>(config.maxRecoveries), "at most 3 recoveries for one stuck flight");
    Check(giveUps == 1, "gives up exactly once, then stays quiet");
    Check(spacingOk, "recoveries are at least a full stuck period apart");

    // A new flight after landing gets a fresh give-up flag but the hourly budget still applies.
    Sample landed = Stuck(now);
    landed.taxiState = false;
    dog.Observe(landed, config);
    Check(!dog.Tracking(), "landing resets tracking after give-up");
}

void TestCooldown()
{
    Watchdog dog;
    Config config = Enabled();
    config.stuckMs = 60000;  // minimum allowed timeout
    dog.Observe(Stuck(0), config);
    Check(dog.Observe(Stuck(60000), config) == Action::Recover, "first recovery at the timeout");
    dog.Recovered(60000);
    Check(FirstRecover(dog, config, 60000 + Step, 60000 + config.cooldownMs - Step,
                       [](std::uint64_t now) { return Stuck(now); }) == 0,
          "no second recovery inside the 10-minute cooldown");
    Check(dog.Observe(Stuck(60000 + config.cooldownMs), config) == Action::Recover,
          "second recovery after the cooldown");
}
}  // namespace

// Owner decision (21 Nov): a recovered bot lands at the flight master it was heading to, i.e. the end
// of its route, not the next hop. Mirrors the PlayerbotAI loop: the first usable node wins.
std::uint32_t FirstUsable(std::array<std::uint32_t, 3> const& nodes, std::uint32_t unusable = 0)
{
    for (std::uint32_t node : nodes)
        if (node && node != unusable)
            return node;
    return 0;  // homebind
}

void TestRecoveryNodes()
{
    using Nodes = std::array<std::uint32_t, 3>;
    // Multi-hop Stormwind(2) -> Lakeshire(5) -> Darkshire(12) -> Sentinel Hill(4).
    std::deque<std::uint32_t> route{2, 5, 12, 4};
    Check(RecoveryNodes(route) == Nodes{4, 5, 2}, "multi-hop: final, next hop, source");
    Check(FirstUsable(RecoveryNodes(route)) == 4, "multi-hop lands at the final flight master");
    Check(FirstUsable(RecoveryNodes(route), 4) == 5, "unusable final falls back to the next hop");

    // Core pops the front as each leg completes; the final destination never changes.
    route.pop_front();
    Check(RecoveryNodes(route) == Nodes{4, 12, 5}, "after one leg the final is unchanged");
    route.pop_front();
    Check(RecoveryNodes(route) == Nodes{4, 12, 0}, "last leg: final equals next hop, no duplicate");

    Check(RecoveryNodes({7, 9}) == Nodes{9, 7, 0}, "direct flight: destination then source");
    Check(RecoveryNodes({7}) == Nodes{7, 0, 0}, "source only");
    Check(RecoveryNodes({}) == Nodes{0, 0, 0}, "no route: homebind");
    Check(FirstUsable(RecoveryNodes({})) == 0, "no route falls through to homebind");
    Check(RecoveryNodes({0, 0, 6}) == Nodes{6, 0, 0}, "zero nodes are skipped");
    Check(RecoveryNodes({3, 8, 3}) == Nodes{3, 8, 0}, "round trip back to the source is still the final");
}

int main()
{
    TestLongFlightNeverTriggers();
    TestExactBoundary();
    TestProgressResetsTimer();
    TestGroundWalkWithTaxiFlagsIsNotProgress();
    TestRouteCompletionClears();
    TestTeleportAndMapChange();
    TestNotEligible();
    TestBoundedRetries();
    TestCooldown();
    TestRecoveryNodes();
    std::cout << "checks=" << g_checks << " failures=" << g_failures << "\n";
    return g_failures == 0 ? 0 : 1;
}
