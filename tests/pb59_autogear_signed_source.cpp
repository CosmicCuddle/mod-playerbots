#include "AutoGearVariantSource.h"
#include <cstdint>
#include <iostream>
#include <vector>

using namespace PlayerbotsAutoGearSelection;

namespace
{
int checks = 0;
bool Check(bool condition, char const* description)
{
    ++checks;
    if (!condition)
        std::cerr << "FAIL " << description << '\n';
    return condition;
}
SourceRow Row(std::uint32_t id, std::int32_t variant, std::uint8_t slot = 4)
{
    return SourceRow{id, variant, 60, slot, true, SourceEligibility::KnownEligible, true, true};
}
}

int main()
{
    bool ok = true;
    std::vector<SourceRow> plain{Row(100, 0)};
    auto const plainCopy = CopyBoundedSource(plain, 8);
    ok = Check(plainCopy.status == CopyStatus::Copied && plainCopy.sourceComplete &&
            plainCopy.rows.size() == 1, "plain source remains supported") && ok;

    std::vector<SourceRow> variants{Row(100, 101), Row(100, 102)};
    auto const legacy = CopyBoundedSource(variants, 8);
    ok = Check(legacy.status == CopyStatus::IncompleteVariants && !legacy.sourceComplete,
            "default legacy route retains split-variant preserve") && ok;
    auto const signedCopy = CopyBoundedSource(variants, 8, true);
    ok = Check(signedCopy.status == CopyStatus::Copied && signedCopy.sourceComplete &&
            signedCopy.rows.size() == 2 && signedCopy.rows[0].randomProperty == 101 &&
            signedCopy.rows[1].randomProperty == 102,
            "variant-aware copy keeps exact positive identities") && ok;

    std::vector<SourceRow> mixed{Row(100, 101), Row(100, -101), Row(100, 0)};
    auto const signCopy = CopyBoundedSource(mixed, 8, true);
    ok = Check(signCopy.status == CopyStatus::IncompleteVariants && !signCopy.sourceComplete &&
            signCopy.rows.empty(),
            "mixed positive, negative and zero namespaces preserve") && ok;
    std::vector<SourceRow> mixedSign{Row(66, 5), Row(66, -5)};
    auto const mixedSignCopy = CopyBoundedSource(mixedSign, 8, true);
    ok = Check(mixedSignCopy.status == CopyStatus::IncompleteVariants &&
            !mixedSignCopy.sourceComplete && mixedSignCopy.rows.empty(),
            "A1: property and suffix on one template cannot be certified") && ok;
    std::vector<SourceRow> mixedZero{Row(66, 0), Row(66, 5)};
    auto const mixedZeroCopy = CopyBoundedSource(mixedZero, 8, true);
    ok = Check(mixedZeroCopy.status == CopyStatus::IncompleteVariants &&
            !mixedZeroCopy.sourceComplete && mixedZeroCopy.rows.empty(),
            "A2: ungrouped and rolled template cannot be certified") && ok;
    std::vector<SourceRow> partialVariant{Row(66, 5), Row(66, 7)};
    partialVariant[1].staticCapable = false;
    auto const partialCopy = CopyBoundedSource(partialVariant, 8, true);
    ok = Check(partialCopy.status == CopyStatus::IncompleteVariants &&
            !partialCopy.sourceComplete && partialCopy.rows.empty(),
            "A3: dropping an eligible incapable sibling cannot certify source") && ok;

    std::vector<SourceRow> ring{Row(100, 101, 10), Row(100, 101, 11)};
    auto const ringCopy = CopyBoundedSource(ring, 8, true);
    ok = Check(ringCopy.status == CopyStatus::Copied && ringCopy.rows.size() == 2,
            "same variant can be considered in different equipment slots") && ok;

    std::vector<SourceRow> duplicate{Row(100, 101), Row(100, 101)};
    auto const doubled = CopyBoundedSource(duplicate, 8, true);
    ok = Check(doubled.status == CopyStatus::Invalid && !doubled.sourceComplete &&
            doubled.rows.empty(), "duplicate exact signed-key and slot fails closed") && ok;

    std::vector<SourceRow> contradictory{Row(100, 101), Row(100, 101)};
    contradictory[1].eligibility = SourceEligibility::KnownUnavailable;
    auto const unavailableConflict = CopyBoundedSource(contradictory, 8, true);
    ok = Check(unavailableConflict.status == CopyStatus::Invalid && unavailableConflict.rows.empty(),
            "eligible-unavailable duplicate source identity fails closed") && ok;

    std::vector<SourceRow> wrongLevel{Row(100, 101), Row(100, 102)};
    wrongLevel[1].itemLevel = 66;
    auto const conflictingTemplate = CopyBoundedSource(wrongLevel, 8, true);
    ok = Check(conflictingTemplate.status == CopyStatus::Invalid && conflictingTemplate.rows.empty(),
            "same item template cannot have two item levels") && ok;

    std::vector<SourceRow> unknown{Row(100, 101)};
    unknown[0].eligibility = SourceEligibility::Unknown;
    auto const unknownCopy = CopyBoundedSource(unknown, 8, true);
    ok = Check(unknownCopy.status == CopyStatus::Incomplete && !unknownCopy.sourceComplete,
            "unknown eligibility is incomplete") && ok;

    std::vector<SourceRow> invalid{Row(100, 101)};
    invalid[0].eligibility = static_cast<SourceEligibility>(255);
    auto const invalidCopy = CopyBoundedSource(invalid, 8, true);
    ok = Check(invalidCopy.status == CopyStatus::Invalid && invalidCopy.rows.empty(),
            "invalid enum is rejected") && ok;

    auto const bounded = CopyBoundedSource(variants, 1, true);
    ok = Check(bounded.status == CopyStatus::Bounded && !bounded.sourceComplete &&
            bounded.rows.empty(), "partial source is not copied") && ok;

    std::vector<SourceRow> unavailable{Row(100, 101)};
    unavailable[0].eligibility = SourceEligibility::KnownUnavailable;
    auto const missing = CopyBoundedSource(unavailable, 8, true);
    ok = Check(missing.status == CopyStatus::Copied && missing.sourceComplete &&
            missing.rows.empty(), "known-unavailable row is not a candidate") && ok;
    unavailable.push_back(Row(100, 102));
    auto const mixedEligibility = CopyBoundedSource(unavailable, 8);
    ok = Check(mixedEligibility.status == CopyStatus::Copied && mixedEligibility.sourceComplete &&
            mixedEligibility.rows.size() == 1,
            "a known-unavailable sibling is not a split-variant candidate") && ok;

    std::vector<SourceRow> staticDenied{Row(100, 101)};
    staticDenied[0].staticCapable = false;
    auto const staticCopy = CopyBoundedSource(staticDenied, 8, true);
    ok = Check(staticCopy.status == CopyStatus::IncompleteVariants &&
            !staticCopy.sourceComplete && staticCopy.rows.empty(),
            "eligible but statically incapable source cannot certify completeness") && ok;

    ok = Check(CopyBoundedSource(plain, 0, true).status == CopyStatus::Bounded,
            "zero budget cannot copy a present row") && ok;
    std::vector<SourceRow> empty;
    auto const emptyCopy = CopyBoundedSource(empty, 0, true);
    ok = Check(emptyCopy.status == CopyStatus::Copied && emptyCopy.rows.empty(),
            "empty copied list still needs independent slot authority") && ok;

    if (!ok)
        return 1;
    std::cout << "checks=" << checks << '\n';
    return 0;
}
