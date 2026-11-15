#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace PlayerbotsAutoGearStorage
{
// A complete set of exact, empty storage destinations for every original item.
// Matching is read-only; never merge stacks or reserve a destination twice.
struct Domain
{
    std::uint64_t itemGuid;
    std::uint16_t source;
    std::vector<std::uint16_t> destinations;
};
struct Move
{
    std::uint64_t itemGuid;
    std::uint16_t source, destination;
    bool operator==(Move const&) const = default;
};
enum class Status { Invalid, Incomplete, NoSpace, Ready };
struct Result
{
    Status status = Status::Invalid;
    std::vector<Move> moves;
};

inline Result Match(std::vector<Domain> const& domains, bool complete,
    std::size_t edgeBudget)
{
    if (!complete)
        return {Status::Incomplete, {}};
    if (domains.size() > 17)
        return {};
    std::vector<std::uint16_t> positions;
    std::size_t edges = 0;
    for (std::size_t i = 0; i < domains.size(); ++i)
    {
        auto const& d = domains[i];
        if (!d.itemGuid || !d.source)
            return {};
        for (std::size_t j = 0; j < i; ++j)
            if (d.itemGuid == domains[j].itemGuid || d.source == domains[j].source)
                return {};
        if (d.destinations.size() > edgeBudget - edges)
            return {Status::Incomplete, {}};
        edges += d.destinations.size();
        for (auto dest : d.destinations)
        {
            if (!dest || std::any_of(domains.begin(), domains.end(), [dest](Domain const& other)
                { return other.source == dest; }))
                return {};
            positions.push_back(dest);
        }
    }
    std::sort(positions.begin(), positions.end());
    positions.erase(std::unique(positions.begin(), positions.end()), positions.end());
    // Augmenting paths can move a flexible item to a specialized bag so a
    // less flexible item can use the only ordinary backpack slot.
    std::vector<int> owner(positions.size(), -1);
    std::vector<bool> visited(positions.size());
    auto augment = [&](auto&& self, std::size_t item) -> bool
    {
        for (auto dest : domains[item].destinations)
        {
            auto index = static_cast<std::size_t>(std::lower_bound(positions.begin(), positions.end(), dest) - positions.begin());
            if (visited[index])
                continue;
            visited[index] = true;
            if (owner[index] == -1 || self(self, static_cast<std::size_t>(owner[index])))
            {
                owner[index] = static_cast<int>(item);
                return true;
            }
        }
        return false;
    };
    for (std::size_t item = 0; item < domains.size(); ++item)
    {
        std::fill(visited.begin(), visited.end(), false);
        if (!augment(augment, item))
            return {Status::NoSpace, {}};
    }
    Result result{Status::Ready, std::vector<Move>(domains.size())};
    for (std::size_t i = 0; i < positions.size(); ++i)
        if (owner[i] >= 0)
        {
            auto item = static_cast<std::size_t>(owner[i]);
            result.moves[item] = {domains[item].itemGuid, domains[item].source, positions[i]};
        }
    return result;
}
}
