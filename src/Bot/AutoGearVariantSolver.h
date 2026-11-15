/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_VARIANT_SOLVER_H
#define PLAYERBOTS_AUTOGEAR_VARIANT_SOLVER_H

#include "AutoGearSetSolver.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace PlayerbotsAutoGearVariant
{
    using PlayerbotsAutoGearWeapons::Kind;
    using PlayerbotsAutoGearSet::CombatMask;
    using PlayerbotsAutoGearSet::Main;
    using PlayerbotsAutoGearSet::Off;
    using PlayerbotsAutoGearSet::Ranged;
    using PlayerbotsAutoGearSet::Slots;
    using PlayerbotsAutoGearSet::WeaponMask;

    // Equipped identity is the variant. Unique, MaxCount, and category use the template entry.
    struct Candidate
    {
        std::uint32_t entryId = 0;
        std::int32_t randomProperty = 0;
        std::uint32_t itemLevel = 0;
        std::uint32_t score = 0;
        std::uint32_t limitCategory = 0;
        std::uint32_t categoryLimit = 0;
        std::uint32_t maxCount = 0;
        Kind kind = Kind::Count;
        bool uniqueEquipped = false;
        bool metadataKnown = false;
        bool eligible = false;
        bool exclusiveMainhand = false;
    };

    struct Domain
    {
        std::span<Candidate const> candidates;
        bool complete = false;
    };

    struct Policy
    {
        std::uint32_t cap = 0;
        std::uint32_t eligibleMask = 0;
        std::uint32_t requiredMask = 0;
        PlayerbotsAutoGearWeapons::Capabilities weapons{};
        bool known = false;
    };

    enum class Status
    {
        Invalid,
        Incomplete,
        NoSet,
        SuitableSet
    };

    struct Result
    {
        Status status = Status::Invalid;
        std::array<std::uint32_t, Slots> entries{};
        std::array<std::int32_t, Slots> properties{};
        std::uint64_t score = 0;
        std::size_t visited = 0;
    };

    inline bool IdentityBefore(Candidate const& left, Candidate const& right)
    {
        return left.entryId < right.entryId ||
               (left.entryId == right.entryId && left.randomProperty < right.randomProperty);
    }

    // Prototype. Bounds match the shared solver. It does not replace that solver.
    inline Result Solve(std::array<Domain, Slots> const& domains, Policy const& policy, std::size_t candidateBudget,
        std::size_t nodeBudget)
    {
        using namespace PlayerbotsAutoGearWeapons;
        constexpr auto mainKinds = Bit(Kind::OneHand) | Bit(Kind::TwoHand);
        constexpr auto offKinds = mainKinds | Bit(Kind::Shield) | Bit(Kind::Holdable);
        constexpr auto rangedKinds = Bit(Kind::Bow) | Bit(Kind::Gun) | Bit(Kind::Crossbow) | Bit(Kind::Wand) |
                                     Bit(Kind::Thrown) | Bit(Kind::Relic);
        if (!policy.known || !policy.cap || !policy.eligibleMask || (policy.weapons.mainhand & ~mainKinds) ||
            (policy.weapons.offhand & ~offKinds) || (policy.weapons.ranged & ~rangedKinds) ||
            (!policy.requiredMask && !policy.weapons.mainhandRequired && !policy.weapons.offhandRequired &&
                !policy.weapons.rangedRequired) ||
            (policy.eligibleMask & ~CombatMask) || (policy.requiredMask & ~policy.eligibleMask) ||
            (policy.requiredMask & WeaponMask) ||
            (policy.weapons.mainhandRequired && !(policy.eligibleMask & (1u << Main))) ||
            (policy.weapons.offhandRequired && !(policy.eligibleMask & (1u << Off))) ||
            (policy.weapons.rangedRequired && !(policy.eligibleMask & (1u << Ranged))))
            return {};

        std::array<std::vector<Candidate>, Slots> lists;
        std::size_t seen = 0;
        std::unordered_map<std::uint32_t, Candidate> templates;
        std::unordered_map<std::uint32_t, std::uint32_t> categoryLimits;
        for (unsigned slot = 0; slot < Slots; ++slot)
        {
            if (!(policy.eligibleMask & (1u << slot)))
                continue;
            if (!domains[slot].complete)
                return {Status::Incomplete};
            bool havePrevious = false;
            Candidate previous{};
            for (Candidate const& candidate : domains[slot].candidates)
            {
                if (seen == candidateBudget)
                    return {Status::Incomplete};
                ++seen;
                bool const weaponSlot = slot == Main || slot == Off || slot == Ranged;
                if (!candidate.metadataKnown || candidate.entryId == 0 ||
                    (havePrevious && !IdentityBefore(previous, candidate)) || candidate.itemLevel == 0 ||
                    (candidate.limitCategory == 0) != (candidate.categoryLimit == 0) ||
                    (weaponSlot && candidate.kind >= Kind::Count) || (!weaponSlot && candidate.kind != Kind::Count) ||
                    (candidate.exclusiveMainhand && candidate.kind != Kind::OneHand &&
                        candidate.kind != Kind::TwoHand))
                    return {};
                havePrevious = true;
                previous = candidate;
                auto [meta, inserted] = templates.emplace(candidate.entryId, candidate);
                Candidate const& old = meta->second;
                if (!inserted &&
                    (old.itemLevel != candidate.itemLevel || old.uniqueEquipped != candidate.uniqueEquipped ||
                        old.maxCount != candidate.maxCount || old.limitCategory != candidate.limitCategory ||
                        old.categoryLimit != candidate.categoryLimit || old.kind != candidate.kind ||
                        old.exclusiveMainhand != candidate.exclusiveMainhand))
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
            std::sort(lists[slot].begin(), lists[slot].end(), [](Candidate const& left, Candidate const& right)
                {
                    if (left.score != right.score)
                        return left.score > right.score;
                    return IdentityBefore(left, right);
                });
        }

        std::array<unsigned, Slots> order{Main, Off, Ranged, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 18};
        std::array<std::uint64_t, Slots + 1> upper{};
        for (unsigned depth = Slots; depth > 0; --depth)
        {
            std::vector<Candidate> const& list = lists[order[depth - 1]];
            upper[depth - 1] = upper[depth] + (list.empty() ? 0 : list.front().score);
        }

        std::array<Candidate const*, Slots> picked{};
        Result result{Status::NoSet};
        bool exhausted = false;
        auto entryLimitOf = [](Candidate const& candidate) -> std::uint32_t
        {
            if (candidate.uniqueEquipped)
                return candidate.maxCount == 0 ? 1u : std::min(candidate.maxCount, 1u);
            return candidate.maxCount;
        };
        auto compatible = [&](Candidate const& candidate)
        {
            unsigned entryCount = 1;
            unsigned categoryCount = 1;
            std::uint32_t entryLimit = entryLimitOf(candidate);
            std::uint32_t limit = candidate.categoryLimit;
            for (Candidate const* other : picked)
            {
                if (!other)
                    continue;
                if (candidate.entryId == other->entryId)
                {
                    ++entryCount;
                    std::uint32_t const otherLimit = entryLimitOf(*other);
                    if (otherLimit != 0)
                        entryLimit = entryLimit == 0 ? otherLimit : std::min(entryLimit, otherLimit);
                }
                if (candidate.limitCategory && candidate.limitCategory == other->limitCategory)
                {
                    ++categoryCount;
                    limit = std::min(limit, other->categoryLimit);
                }
            }
            if (entryLimit != 0 && entryCount > entryLimit)
                return false;
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
                {
                    result.entries[slot] = picked[slot] ? picked[slot]->entryId : 0;
                    result.properties[slot] = picked[slot] ? picked[slot]->randomProperty : 0;
                }
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
                    Kind const planned = picked[Main] ? picked[Main]->kind : Kind::Count;
                    auto capabilities = policy.weapons;
                    if (picked[Main] && picked[Main]->exclusiveMainhand)
                    {
                        capabilities.offhand = 0;
                        capabilities.offhandRequired = false;
                    }
                    auto const weaponDomains = Build(capabilities, planned);
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
                if (candidate &&
                    ((weaponSlot && !weapons.Accepts(true, candidate->kind)) ||
                        (slot == Off && candidate->exclusiveMainhand) || !compatible(*candidate)))
                    return;
                picked[slot] = candidate;
                self(self, depth + 1, score + (candidate ? candidate->score : 0));
                picked[slot] = nullptr;
            };
            for (Candidate const& candidate : lists[slot])
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
            return {Status::Incomplete, {}, {}, 0, result.visited};
        return result;
    }
}

#endif
