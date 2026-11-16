/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_VARIANT_ENUMERATION_H
#define PLAYERBOTS_AUTOGEAR_VARIANT_ENUMERATION_H

#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace PlayerbotsAutoGearSelection
{
    // One installed enchantment-template member. Positive ids are random
    // properties. Negative ids are random suffixes. Zero is not a variant.
    struct VariantId
    {
        std::int32_t signedId = 0;
        std::uint32_t enchant = 0;
        bool suffix = false;
    };

    struct VariantRequest
    {
        std::int32_t randomProperty = 0;
        std::int32_t randomSuffix = 0;
        bool groupsKnown = false;
        std::span<std::uint32_t const> pool{};
        bool poolPresent = false;
        // Parallel to pool. Nonzero means HasItemEnchantMod(group, pool[i]).
        std::span<std::uint8_t const> membership{};
        bool membershipKnown = false;
        std::span<std::uint32_t const> propertyDbc{};
        std::span<std::uint32_t const> suffixDbc{};
        bool dbcKnown = false;
        std::size_t budget = 32;
    };

    struct VariantResult
    {
        bool preserve = true;
        bool complete = false;
        bool truncated = false;
        bool conflict = false;
        std::vector<VariantId> variants{};
    };

    inline bool ListedId(std::span<std::uint32_t const> ids, std::uint32_t id)
    {
        for (std::uint32_t row : ids)
        {
            if (row == id)
                return true;
        }
        return false;
    }

    // Read-only. Does not roll, create an item, or query SQL.
    inline VariantResult EnumerateVariants(VariantRequest const& request)
    {
        VariantResult result;
        bool const both = request.randomProperty > 0 && request.randomSuffix > 0;
        bool const negative = request.randomProperty < 0 || request.randomSuffix < 0;
        if (!request.groupsKnown || both || negative)
        {
            result.conflict = both || negative;
            return result;
        }
        if (request.randomProperty == 0 && request.randomSuffix == 0)
        {
            if (request.budget == 0)
            {
                result.truncated = true;
                return result;
            }
            result.variants.push_back({});
            result.complete = true;
            result.preserve = false;
            return result;
        }
        if (!request.poolPresent || !request.membershipKnown || !request.dbcKnown ||
            request.membership.size() != request.pool.size())
        {
            result.conflict = request.membershipKnown && request.membership.size() != request.pool.size();
            return result;
        }
        bool const suffix = request.randomSuffix > 0;
        for (std::size_t index = 0; index < request.pool.size(); ++index)
        {
            std::uint32_t const enchant = request.pool[index];
            bool duplicate = false;
            for (VariantId const& prior : result.variants)
            {
                if (prior.enchant == enchant)
                    duplicate = true;
            }
            bool const tooWide = enchant > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
            // The two DBC stores are separate. The same number may exist in both.
            bool const inSelected =
                suffix ? ListedId(request.suffixDbc, enchant) : ListedId(request.propertyDbc, enchant);
            bool const wrongStore = !inSelected;
            if (enchant == 0 || request.membership[index] == 0 || duplicate || tooWide || wrongStore)
            {
                result.conflict = true;
                result.variants.clear();
                result.complete = false;
                result.preserve = true;
                return result;
            }
            if (result.variants.size() >= request.budget)
            {
                result.truncated = true;
                result.complete = false;
                result.preserve = true;
                return result;
            }
            VariantId id;
            id.enchant = enchant;
            id.suffix = suffix;
            id.signedId = suffix ? -static_cast<std::int32_t>(enchant) : static_cast<std::int32_t>(enchant);
            result.variants.push_back(id);
        }
        if (result.variants.empty())
            return result;
        result.complete = true;
        result.preserve = false;
        return result;
    }

    // Pool comes from RandomItemMgr::GetEnchantmentPool. Membership is
    // HasItemEnchantMod(group, enchant) at core 8d6ac43. A bounded list is
    // not complete.
    template <class Membership>
    VariantResult EnumerateInstalledPool(std::span<std::uint32_t const> pool, bool poolPresent, std::int32_t property,
        std::int32_t suffix, Membership const& member, std::span<std::uint32_t const> propertyDbc,
        std::span<std::uint32_t const> suffixDbc, bool dbcKnown, std::size_t budget)
    {
        std::int32_t const group = property > 0 ? property : suffix;
        std::vector<std::uint8_t> flags(pool.size());
        for (std::size_t index = 0; index < pool.size(); ++index)
            flags[index] = member(group, pool[index]) ? 1 : 0;
        VariantRequest request;
        request.randomProperty = property;
        request.randomSuffix = suffix;
        request.groupsKnown = true;
        request.pool = pool;
        request.poolPresent = poolPresent;
        request.membership = flags;
        request.membershipKnown = true;
        request.propertyDbc = propertyDbc;
        request.suffixDbc = suffixDbc;
        request.dbcKnown = dbcKnown;
        request.budget = budget;
        return EnumerateVariants(request);
    }

    // One loaded Core item_enchantment_template row. Only a positive weight,
    // in the same range Core keeps, is an authoritative member.
    struct CoreGroupRow
    {
        std::uint32_t enchant = 0;
        float weight = 0.f;
    };

    inline bool AuthoritativeWeight(float weight)
    {
        return weight > 0.000001f && weight <= 100.f;
    }

    // Owned copies. Spans into these arrays stay valid until the owner dies.
    struct OwnedVariantSource
    {
        std::vector<std::uint32_t> pool{};
        std::vector<std::uint8_t> membership{};
        std::vector<std::uint32_t> propertyDbc{};
        std::vector<std::uint32_t> suffixDbc{};
        bool poolPresent = false;
        bool membershipKnown = false;
        bool dbcKnown = false;
        bool complete = false;
        bool partial = false;
        bool conflict = false;
    };

    // Core positive-weight rows are the authoritative group. The RandomItemMgr
    // pool is complete only when it contains exactly those ids. HasItemEnchantMod
    // confirms a member. It does not prove that the pool is exhaustive.
    template <class Membership>
    OwnedVariantSource AdaptLoadedGroup(std::span<CoreGroupRow const> coreRows, std::span<std::uint32_t const> mgrPool,
        bool mgrPoolPresent, std::int32_t group, Membership const& member, std::span<std::uint32_t const> propertyDbc,
        std::span<std::uint32_t const> suffixDbc, bool dbcKnown)
    {
        OwnedVariantSource owned;
        owned.propertyDbc.assign(propertyDbc.begin(), propertyDbc.end());
        owned.suffixDbc.assign(suffixDbc.begin(), suffixDbc.end());
        owned.dbcKnown = dbcKnown;
        if (group <= 0 || !mgrPoolPresent)
        {
            owned.partial = group > 0;
            return owned;
        }
        for (CoreGroupRow const& row : coreRows)
        {
            if (row.enchant == 0)
            {
                owned.conflict = true;
                return owned;
            }
            if (!AuthoritativeWeight(row.weight))
                continue;
            for (std::uint32_t prior : owned.pool)
            {
                if (prior == row.enchant)
                {
                    owned.conflict = true;
                    owned.pool.clear();
                    return owned;
                }
            }
            owned.pool.push_back(row.enchant);
        }
        if (owned.pool.empty())
        {
            owned.partial = true;
            return owned;
        }
        if (mgrPool.size() != owned.pool.size())
        {
            owned.partial = true;
            owned.pool.clear();
            return owned;
        }
        for (std::uint32_t id : owned.pool)
        {
            bool inMgr = false;
            for (std::uint32_t mgr : mgrPool)
            {
                if (mgr == id)
                    inMgr = true;
            }
            if (!inMgr)
            {
                owned.partial = true;
                owned.pool.clear();
                return owned;
            }
        }
        owned.membership.assign(owned.pool.size(), 0);
        owned.membershipKnown = true;
        for (std::size_t index = 0; index < owned.pool.size(); ++index)
        {
            bool const listed = member(group, owned.pool[index]);
            owned.membership[index] = listed ? 1 : 0;
            if (!listed)
            {
                owned.conflict = true;
                return owned;
            }
        }
        owned.poolPresent = true;
        owned.complete = true;
        return owned;
    }
}

#endif
