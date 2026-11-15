#pragma once

#include "AutoGearWeaponDomain.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace PlayerbotsAutoGearSet
{
using PlayerbotsAutoGearWeapons::Kind;
using PlayerbotsAutoGearWeapons::Capabilities;
inline constexpr unsigned Slots = 19, Main = 15, Off = 16, Ranged = 17;
inline constexpr std::uint32_t CombatMask = ((1u << Slots) - 1) & ~((1u << 3) | (1u << 18));
inline constexpr std::uint32_t WeaponMask = (1u << Main) | (1u << Off) | (1u << Ranged);

struct Candidate
{
    std::uint32_t id = 0, itemLevel = 0, score = 0;
    std::uint32_t limitCategory = 0, categoryLimit = 0;
    Kind kind = Kind::Count;
    bool uniqueEquipped = false, metadataKnown = false, eligible = false;
};
struct Domain
{
    std::span<Candidate const> candidates;
    bool complete = false;
};
struct Policy
{
    std::uint32_t cap = 0, eligibleMask = 0, requiredMask = 0;
    Capabilities weapons;
    bool known = false;
};
enum class Status { Invalid, Incomplete, NoSet, SuitableSet };
struct Result
{
    Status status = Status::Invalid;
    std::array<std::uint32_t, Slots> items{};
    std::uint64_t score = 0;
    std::size_t visited = 0;
};

// Search only copied, complete, independently validated domains. The caller
// defines the eligible universe and item score; the solver never mutates items.
// A partial search cannot claim BiS or a no-set result.
inline Result Solve(std::array<Domain, Slots> const& domains, Policy const& policy,
    std::size_t candidateBudget, std::size_t nodeBudget)
{
    using namespace PlayerbotsAutoGearWeapons;
    constexpr auto mainKinds = Bit(Kind::OneHand) | Bit(Kind::TwoHand);
    constexpr auto offKinds = mainKinds | Bit(Kind::Shield) | Bit(Kind::Holdable);
    constexpr auto rangedKinds = Bit(Kind::Bow) | Bit(Kind::Gun) | Bit(Kind::Crossbow) |
        Bit(Kind::Wand) | Bit(Kind::Thrown) | Bit(Kind::Relic);
    if (!policy.known || !policy.cap || !policy.eligibleMask ||
        (policy.weapons.mainhand & ~mainKinds) || (policy.weapons.offhand & ~offKinds) ||
        (policy.weapons.ranged & ~rangedKinds) ||
        (!policy.requiredMask && !policy.weapons.mainhandRequired &&
            !policy.weapons.offhandRequired && !policy.weapons.rangedRequired) ||
        (policy.eligibleMask & ~CombatMask) || (policy.requiredMask & ~policy.eligibleMask) ||
        (policy.requiredMask & WeaponMask) ||
        (policy.weapons.mainhandRequired && !(policy.eligibleMask & (1u << Main))) ||
        (policy.weapons.offhandRequired && !(policy.eligibleMask & (1u << Off))) ||
        (policy.weapons.rangedRequired && !(policy.eligibleMask & (1u << Ranged))))
        return {};
    std::array<std::vector<Candidate>, Slots> lists;
    std::size_t seen = 0;
    std::unordered_map<std::uint32_t, Candidate> metadata;
    std::unordered_map<std::uint32_t, std::uint32_t> categoryLimits;
    for (unsigned slot = 0; slot < Slots; ++slot)
    {
        if (!(policy.eligibleMask & (1u << slot)))
            continue;
        if (!domains[slot].complete)
            return {Status::Incomplete};
        std::uint32_t previous = 0;
        for (auto const& candidate : domains[slot].candidates)
        {
            if (seen == candidateBudget)
                return {Status::Incomplete};
            ++seen;
            if (!candidate.metadataKnown || !candidate.id || candidate.id <= previous ||
                !candidate.itemLevel || (candidate.limitCategory == 0) != (candidate.categoryLimit == 0) ||
                ((slot == Main || slot == Off || slot == Ranged) && candidate.kind >= Kind::Count) ||
                ((slot != Main && slot != Off && slot != Ranged) && candidate.kind != Kind::Count))
                return {};
            previous = candidate.id;
            auto [meta, inserted] = metadata.emplace(candidate.id, candidate);
            auto const& old = meta->second;
            if (!inserted && (old.itemLevel != candidate.itemLevel || old.uniqueEquipped != candidate.uniqueEquipped ||
                old.limitCategory != candidate.limitCategory || old.categoryLimit != candidate.categoryLimit ||
                old.kind != candidate.kind))
                return {};
            if (candidate.limitCategory)
            {
                auto [category, fresh] = categoryLimits.emplace(candidate.limitCategory, candidate.categoryLimit);
                if (!fresh && category->second != candidate.categoryLimit)
                    return {};
            }
            if (candidate.eligible && candidate.itemLevel <= policy.cap)
                lists[slot].push_back(candidate);
        }
        std::sort(lists[slot].begin(), lists[slot].end(), [](Candidate const& a, Candidate const& b)
        {
            return a.score != b.score ? a.score > b.score : a.id < b.id;
        });
    }
    std::array<unsigned, Slots> order{Main, Off, Ranged, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 18};
    std::array<std::uint64_t, Slots + 1> upper{};
    for (unsigned depth = Slots; depth > 0; --depth)
    {
        auto const& list = lists[order[depth - 1]];
        upper[depth - 1] = upper[depth] + (list.empty() ? 0 : list.front().score);
    }
    std::array<Candidate const*, Slots> picked{};
    Result result{Status::NoSet};
    bool exhausted = false;
    auto compatible = [&](Candidate const& candidate)
    {
        unsigned categoryCount = 1;
        std::uint32_t limit = candidate.categoryLimit;
        for (auto other : picked)
        {
            if (!other)
                continue;
            if (candidate.id == other->id && (candidate.uniqueEquipped || other->uniqueEquipped))
                return false;
            if (candidate.limitCategory && candidate.limitCategory == other->limitCategory)
            {
                ++categoryCount;
                limit = std::min(limit, other->categoryLimit);
            }
        }
        return !candidate.limitCategory || categoryCount <= limit;
    };
    auto search = [&](auto&& self, unsigned depth, std::uint64_t score) -> void
    {
        if (exhausted || (result.status == Status::SuitableSet && score + upper[depth] <= result.score))
            return;
        if (depth == Slots)
        {
            result.status = Status::SuitableSet;
            result.score = score;
            for (unsigned slot = 0; slot < Slots; ++slot)
                result.items[slot] = picked[slot] ? picked[slot]->id : 0;
            return;
        }
        unsigned const slot = order[depth];
        if (!(policy.eligibleMask & (1u << slot)))
        {
            self(self, depth + 1, score);
            return;
        }
        bool required = (policy.requiredMask & (1u << slot)) != 0;
        PlayerbotsAutoGearWeapons::Domain weapons;
        bool const weaponSlot = (WeaponMask & (1u << slot)) != 0;
        if (weaponSlot)
        {
            if (slot == Main)
                weapons = {policy.weapons.mainhand, policy.weapons.mainhandRequired};
            else
            {
                auto const planned = picked[Main] ? picked[Main]->kind : Kind::Count;
                auto const weaponDomains = Build(policy.weapons, planned);
                if (!weaponDomains.valid)
                    return;
                weapons = slot == Off ? weaponDomains.offhand : weaponDomains.ranged;
            }
            required = weapons.required;
        }
        auto visit = [&](Candidate const* candidate)
        {
            if (result.visited == nodeBudget)
            {
                exhausted = true;
                return;
            }
            ++result.visited;
            if (candidate && ((weaponSlot && !weapons.Accepts(true, candidate->kind)) || !compatible(*candidate)))
                return;
            picked[slot] = candidate;
            self(self, depth + 1, score + (candidate ? candidate->score : 0));
            picked[slot] = nullptr;
        };
        for (auto const& candidate : lists[slot])
        {
            visit(&candidate);
            if (exhausted)
                return;
        }
        if (!required)
            visit(nullptr);
    };
    search(search, 0, 0);
    if (exhausted)
        return {Status::Incomplete, {}, 0, result.visited};
    return result;
}
}
