#pragma once

#include "AutoGearEquipmentSample.h"
#include "EquipmentCaptureState.h"
#include <array>
#include <cstdint>
#include <optional>

namespace PlayerbotsAutoGearNativeAuthority
{
constexpr std::uint32_t CombatMask = ((1u << 19) - 1) & ~((1u << 3) | (1u << 18));

struct Sample
{
    PB59::EquipmentFacts facts;
    std::uint64_t policyRevision = 0;
    std::uint32_t countedMask = 0;
    std::uint64_t rawSum = 0;
    PlayerbotsAutoGearSample::Result average;
};

// Policy mask must be supplied by an approved class/level/capability resolver.
// Never derive it from occupied slots. This adapter makes no weighting decision.
inline std::optional<Sample> Capture(PB59::EquipmentFacts const& facts,
    std::uint64_t policyRevision, std::uint32_t countedMask, bool eligibilityKnown,
    std::uint32_t heirloomQuality)
{
    if (!facts.guid || !facts.stamp.session || !facts.stamp.revision || !facts.level ||
        !facts.playerClass || !facts.race || !policyRevision || !eligibilityKnown ||
        !countedMask || (countedMask & ~CombatMask))
        return std::nullopt;

    std::array<PlayerbotsAutoGearSample::Slot, 19> slots{};
    std::uint64_t rawSum = 0;
    for (std::size_t i = 0; i < slots.size(); ++i)
    {
        auto const& fact = facts.slots[i];
        bool const occupied = fact.itemGuid != 0;
        bool const counted = (countedMask & (1u << i)) != 0;
        if (counted && !occupied && fact != PB59::SlotFacts{})
            return std::nullopt;
        if (counted && occupied)
            rawSum += fact.itemLevel;
        slots[i] = {occupied, counted, occupied && fact.entry != 0,
            occupied && (fact.quality == heirloomQuality || fact.scalingDistribution || fact.scalingValue),
            fact.itemLevel};
    }
    auto average = PlayerbotsAutoGearSample::Average(slots, eligibilityKnown);
    if (!average.known)
        return std::nullopt;
    return Sample{facts, policyRevision, countedMask, rawSum, average};
}

// Compare the complete current raw facts, not only their numeric average.
// This is an owner-context check, not an IP authority or cross-thread lease.
inline bool Current(Sample const& sample, PB59::EquipmentFacts const& current,
    std::uint64_t policyRevision, std::uint32_t countedMask, bool eligibilityKnown,
    std::uint32_t heirloomQuality)
{
    auto fresh = Capture(current, policyRevision, countedMask, eligibilityKnown, heirloomQuality);
    return fresh && sample.facts == fresh->facts && sample.policyRevision == fresh->policyRevision &&
        sample.countedMask == fresh->countedMask && sample.rawSum == fresh->rawSum && sample.average.known &&
        sample.average.average == fresh->average.average &&
        sample.average.occupiedSlots == fresh->average.occupiedSlots &&
        sample.average.countedSlots == fresh->average.countedSlots;
}
}
