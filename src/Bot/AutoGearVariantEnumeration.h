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
            bool const inProperty = ListedId(request.propertyDbc, enchant);
            bool const inSuffix = ListedId(request.suffixDbc, enchant);
            bool const wrongStore = suffix ? !inSuffix || inProperty : !inProperty || inSuffix;
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
}

#endif
