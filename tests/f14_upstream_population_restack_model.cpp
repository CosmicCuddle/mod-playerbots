// Playerbots upstream-restack F14 population accounting model.
// Pure scheduler arithmetic only: no Worldserver, database, login or logout.
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <unordered_set>
#include <vector>

struct OnlineBot
{
    std::uint32_t guid;
    bool alliance;
    bool removable;
};

static std::uint32_t EffectiveCount(
    std::unordered_set<std::uint32_t> const& selected,
    std::unordered_set<std::uint32_t> const& held)
{
    std::uint32_t count = static_cast<std::uint32_t>(selected.size());
    for (std::uint32_t guid : held)
        if (selected.contains(guid))
            --count;
    return count;
}

static std::uint32_t EffectiveOnlineCount(
    std::vector<OnlineBot> const& online,
    std::unordered_set<std::uint32_t> const& held)
{
    std::uint32_t count = static_cast<std::uint32_t>(online.size());
    for (OnlineBot const& bot : online)
        if (held.contains(bot.guid))
            --count;
    return count;
}

struct FactionDownsize
{
    std::uint32_t onlineAlliance = 0;
    std::uint32_t onlineHorde = 0;
    std::uint32_t removableAlliance = 0;
    std::uint32_t removableHorde = 0;
};

static FactionDownsize BuildFactionDownsize(
    std::vector<OnlineBot> const& online,
    std::unordered_set<std::uint32_t> const& held)
{
    FactionDownsize out;
    for (OnlineBot const& bot : online)
    {
        // F14-held bots are outside the effective population entirely.
        if (held.contains(bot.guid))
            continue;

        if (bot.alliance)
            ++out.onlineAlliance;
        else
            ++out.onlineHorde;

        // Ordinary upstream non-removable bots still occupy their faction share.
        if (!bot.removable)
            continue;

        if (bot.alliance)
            ++out.removableAlliance;
        else
            ++out.removableHorde;
    }
    return out;
}

int main()
{
    std::size_t failures = 0;

    // Every held subset of ten selected bots: OFF == upstream raw count;
    // ON excludes exactly the held subset and never underflows.
    for (std::uint32_t mask = 0; mask < 1024; ++mask)
    {
        std::unordered_set<std::uint32_t> selected;
        std::unordered_set<std::uint32_t> held;
        std::vector<OnlineBot> online;
        for (std::uint32_t i = 0; i < 10; ++i)
        {
            std::uint32_t guid = 100 + i;
            selected.insert(guid);
            online.push_back({guid, (i % 2) == 0, true});
            if ((mask >> i) & 1U)
                held.insert(guid);
        }

        std::uint32_t const heldCount = static_cast<std::uint32_t>(held.size());
        if (EffectiveCount(selected, {}) != 10)
            ++failures;
        if (EffectiveCount(selected, held) != 10 - heldCount)
            ++failures;
        if (EffectiveOnlineCount(online, {}) != 10)
            ++failures;
        if (EffectiveOnlineCount(online, held) != 10 - heldCount)
            ++failures;

        // Upstream count modes choose the target, not the F14 accounting.
        // With hold OFF, all three modes therefore see the exact raw count.
        for (std::uint32_t mode = 0; mode <= 2; ++mode)
        {
            (void)mode;
            if (EffectiveCount(selected, {}) != selected.size())
                ++failures;
        }
    }

    // Held Alliance bot must not inflate Alliance occupancy. A normal
    // non-removable Alliance bot still counts toward the share.
    std::vector<OnlineBot> online = {
        {1, true, true},   // F14-held: excluded entirely
        {2, true, true},
        {3, true, true},
        {4, true, false},  // ordinary upstream non-removable: still occupies share
        {5, false, true},
        {6, false, true},
    };
    std::unordered_set<std::uint32_t> held = {1};
    FactionDownsize const f = BuildFactionDownsize(online, held);
    if (f.onlineAlliance != 3 || f.onlineHorde != 2 ||
        f.removableAlliance != 2 || f.removableHorde != 2)
        ++failures;

    // Effective scheduled 5 at target 5 means no downsize even though raw is 6.
    std::unordered_set<std::uint32_t> selected = {1,2,3,4,5,6};
    if (EffectiveCount(selected, held) != 5)
        ++failures;
    std::uint32_t const target = 5;
    std::uint32_t const toRemove =
        EffectiveCount(selected, held) > target ? EffectiveCount(selected, held) - target : 0;
    if (toRemove != 0)
        ++failures;

    // Raw online remains available for operational upstream behavior while
    // effective online capacity excludes the held actor.
    if (online.size() != 6 || EffectiveOnlineCount(online, held) != 5)
        ++failures;

    if (!failures)
        std::cout << "PASS PB upstream restack: effective scheduled/online/faction accounting and OFF identity\n";
    return failures ? 1 : 0;
}
