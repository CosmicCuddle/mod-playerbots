#include "AutoGearVariantEnumeration.h"
#include <iostream>
#include <vector>

using namespace PlayerbotsAutoGearSelection;

namespace
{
int g_checks = 0;

bool Check(bool condition, char const* text)
{
    ++g_checks;
    if (!condition)
    {
        std::cerr << "FAIL " << text << "\n";
        return false;
    }
    return true;
}

VariantRequest Group(std::int32_t property, std::int32_t suffix, std::span<std::uint32_t const> pool,
    std::span<std::uint8_t const> membership, std::span<std::uint32_t const> properties,
    std::span<std::uint32_t const> suffixes)
{
    VariantRequest request;
    request.randomProperty = property;
    request.randomSuffix = suffix;
    request.groupsKnown = true;
    request.pool = pool;
    request.poolPresent = true;
    request.membership = membership;
    request.membershipKnown = true;
    request.propertyDbc = properties;
    request.suffixDbc = suffixes;
    request.dbcKnown = true;
    request.budget = 8;
    return request;
}
}

int main()
{
    bool ok = true;
    std::vector<std::uint32_t> properties{101, 102, 103};
    std::vector<std::uint32_t> suffixes{201, 202};
    std::vector<std::uint32_t> propertyPool{101, 102};
    std::vector<std::uint8_t> propertyMembers{1, 1};
    VariantResult const propertiesListed =
        EnumerateVariants(Group(10, 0, propertyPool, propertyMembers, properties, suffixes));
    ok = Check(propertiesListed.complete && !propertiesListed.preserve && propertiesListed.variants.size() == 2 &&
               propertiesListed.variants[0].signedId == 101 && propertiesListed.variants[1].signedId == 102,
            "a random-property group becomes positive ids") &&
         ok;

    std::vector<std::uint32_t> suffixPool{201};
    std::vector<std::uint8_t> suffixMembers{1};
    VariantResult const suffixListed =
        EnumerateVariants(Group(0, 20, suffixPool, suffixMembers, properties, suffixes));
    bool const negativeSuffix = suffixListed.complete && suffixListed.variants.size() == 1 &&
                                suffixListed.variants[0].signedId == -201 && suffixListed.variants[0].suffix;
    ok = Check(negativeSuffix, "a random-suffix group becomes a negative id") && ok;

    VariantRequest partial = Group(10, 0, propertyPool, propertyMembers, properties, suffixes);
    partial.budget = 1;
    VariantResult const cut = EnumerateVariants(partial);
    ok = Check(cut.truncated && !cut.complete && cut.preserve, "a bounded variant list is not complete") && ok;
    partial.budget = 2;
    ok = Check(EnumerateVariants(partial).complete, "a budget that covers the pool can be complete") && ok;

    std::vector<std::uint32_t> duplicated{101, 101};
    std::vector<std::uint8_t> duplicateMembers{1, 1};
    VariantResult const duplicate =
        EnumerateVariants(Group(10, 0, duplicated, duplicateMembers, properties, suffixes));
    ok = Check(duplicate.conflict && duplicate.preserve && !duplicate.complete && duplicate.variants.empty(),
            "a duplicate enchant row preserves") &&
         ok;

    std::vector<std::uint8_t> denied{1, 0};
    VariantResult const deniedMember =
        EnumerateVariants(Group(10, 0, propertyPool, denied, properties, suffixes));
    ok = Check(deniedMember.conflict && deniedMember.preserve && !deniedMember.complete,
            "a pool row rejected by membership preserves") &&
         ok;

    std::vector<std::uint32_t> zeroPool{101, 0};
    std::vector<std::uint8_t> zeroMembers{1, 1};
    VariantResult const zero = EnumerateVariants(Group(10, 0, zeroPool, zeroMembers, properties, suffixes));
    ok = Check(zero.conflict && zero.preserve && !zero.complete, "a zero enchant preserves") && ok;

    VariantRequest missing = Group(10, 0, propertyPool, propertyMembers, properties, suffixes);
    missing.poolPresent = false;
    ok = Check(EnumerateVariants(missing).preserve && !EnumerateVariants(missing).complete,
            "a missing enchant group preserves") &&
         ok;

    VariantRequest both = Group(10, 20, propertyPool, propertyMembers, properties, suffixes);
    VariantResult const clash = EnumerateVariants(both);
    ok = Check(clash.conflict && clash.preserve && !clash.complete, "property and suffix together conflict") && ok;

    VariantRequest negative = Group(-1, 0, propertyPool, propertyMembers, properties, suffixes);
    ok = Check(EnumerateVariants(negative).preserve && !EnumerateVariants(negative).complete,
            "a negative template group preserves") &&
         ok;

    std::vector<std::uint32_t> onlySuffix{101};
    VariantResult const wrongStore =
        EnumerateVariants(Group(10, 0, propertyPool, propertyMembers, onlySuffix, suffixes));
    ok = Check(wrongStore.preserve && !wrongStore.complete, "a property missing from its DBC preserves") && ok;

    auto agrees = [](std::int32_t group, std::uint32_t enchant)
    {
        return group == 10 && (enchant == 101 || enchant == 102);
    };
    VariantResult const installed =
        EnumerateInstalledPool(propertyPool, true, 10, 0, agrees, properties, suffixes, true, 8);
    ok = Check(installed.complete && installed.variants[0].signedId == 101 && installed.variants[1].signedId > 0,
            "installed membership keeps property ids positive") &&
         ok;
    VariantResult const installedSuffix =
        EnumerateInstalledPool(suffixPool, true, 0, 20,
            [](std::int32_t, std::uint32_t enchant) { return enchant == 201; }, properties, suffixes, true, 8);
    ok = Check(installedSuffix.complete && installedSuffix.variants[0].signedId == -201,
            "installed membership keeps suffix ids negative") &&
         ok;

    if (!ok)
        return 1;
    std::cout << "checks=" << g_checks << "\n";
    return 0;
}
