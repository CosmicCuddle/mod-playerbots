/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_CANDIDATE_COLLECTOR_H
#define PLAYERBOTS_AUTOGEAR_CANDIDATE_COLLECTOR_H

#include "AutoGearSelectionPlan.h"
#include <vector>

namespace PlayerbotsAutoGearSelection
{
    // One template row the caller already copied. A false flag is missing
    // data. It is not proof that the item is absent.
    struct CandidateTemplate
    {
        bool loaded = false;
        bool metadataKnown = false;
        bool darkmoonIdentityKnown = false;
        std::uint32_t entry = 0;
        std::uint32_t itemLevel = 0;
        std::uint32_t requiredLevel = 0;
        bool qualityKnown = false;
        std::uint32_t quality = 0;
        bool slotKnown = false;
        std::uint8_t slot = 0;
        bool randomPropertyKnown = false;
        std::int32_t randomProperty = 0;
        std::uint32_t classMask = 0;
        bool specRestricted = false;
        std::uint8_t specTab = 0;
        Role role = Role::Any;
        PlayerbotsAutoGearWeapons::Kind kind = PlayerbotsAutoGearWeapons::Kind::Count;
        bool exclusiveMainhand = false;
        std::uint8_t armorSubtype = 0;
        bool uniqueEquipped = false;
        std::uint32_t maxCount = 0;
        std::uint32_t limitCategory = 0;
        bool limitKnown = false;
        std::uint32_t categoryLimit = 0;
        std::uint32_t categoryHaveLimit = 0;
        std::uint32_t setId = 0;
        bool setEvidenceKnown = false;
        bool setBonusPresent = false;
        bool darkmoonCard = false;
        bool darkmoonAvailabilityKnown = false;
        bool darkmoonProcKnown = false;
        bool equipKnown = false;
        bool equipAllowed = false;
        bool blockedOnlyByCurrentGear = false;
        bool scoreKnown = true;
    };

    // Owning copy of one source row after the cap and quality filters.
    struct ProvenSourceRow
    {
        std::uint32_t entry = 0;
        std::int32_t randomProperty = 0;
        std::uint8_t slot = 0;
        FactDecision eligibility = FactDecision::Unknown;
        std::uint32_t classMask = 0;
        bool specRestricted = false;
        std::uint8_t specTab = 0;
        PlayerbotsAutoGearWeapons::Kind weapon = PlayerbotsAutoGearWeapons::Kind::Count;
        bool uniqueEquipped = false;
        std::uint32_t maxCount = 0;
        std::uint32_t limitCategory = 0;
        std::uint32_t categoryLimit = 0;
        std::uint32_t categoryHaveLimit = 0;
        std::uint32_t retainedOwned = 0;
        bool retainedKnown = false;
        std::uint32_t setId = 0;
        bool setEvidenceKnown = false;
        bool setBonusPresent = false;
        std::uint32_t itemLevel = 0;
        std::uint32_t quality = 0;
        bool withinCap = false;
        bool withinQuality = false;
        bool included = false;
    };

    struct CollectorRequest
    {
        bool playerContext = false;
        bool progressionContext = false;
        std::uint8_t rawStage = 0;
        std::uint8_t effectiveStage = 0;
        // Zero means the approved stage cap governs. A nonzero value only lowers it.
        std::uint32_t scoreLimit = 0;
        bool qualityLimitKnown = false;
        std::uint32_t qualityLimit = 0;
        BotView bot{};
        std::span<CandidateTemplate const> rows{};
        std::span<RetainedCount const> retained{};
        std::uint32_t eligibleMask = 0;
        std::uint32_t requiredMask = 0;
        std::uint32_t completeMask = 0;
        // False when the caller stopped before the source ended.
        bool sourceExhausted = false;
        std::size_t rowBudget = 64;
        BagSpace bags = BagSpace::Unknown;
        bool authorityValid = false;
    };

    struct CollectorResult
    {
        bool preserve = true;
        bool truncated = false;
        // True only for a finished scan whose required domain has no legal row.
        bool claimedEmpty = false;
        bool deletesGear = false;
        std::uint32_t cap = 0;
        GearAction action = GearAction::Preserve;
        Plan plan{};
        std::vector<ItemFact> facts{};
        std::vector<ProvenSourceRow> rows{};
    };

    inline bool IdentityComplete(CandidateTemplate const& row)
    {
        if (!row.loaded || !row.metadataKnown || row.entry == 0 || row.itemLevel == 0)
            return false;
        if (!row.slotKnown || row.slot >= PlayerbotsAutoGearSet::Slots)
            return false;
        if (!row.qualityKnown || !row.randomPropertyKnown || !row.darkmoonIdentityKnown)
            return false;
        if (row.limitCategory != 0 && !row.limitKnown)
            return false;
        if (row.setId != 0 && !row.setEvidenceKnown)
            return false;
        return true;
    }

    inline ItemFact FactFromTemplate(CandidateTemplate const& row)
    {
        ItemFact fact;
        fact.id = row.entry;
        fact.itemLevel = row.itemLevel;
        fact.requiredLevel = row.requiredLevel;
        fact.slot = row.slot;
        fact.randomProperty = row.randomProperty;
        fact.classMask = row.classMask;
        fact.specRestricted = row.specRestricted;
        fact.specTab = row.specTab;
        fact.role = row.role;
        fact.kind = row.kind;
        fact.exclusiveMainhand = row.exclusiveMainhand;
        fact.armorSubtype = row.armorSubtype;
        fact.uniqueEquipped = row.uniqueEquipped;
        fact.maxCount = row.maxCount;
        fact.limitCategory = row.limitCategory;
        fact.categoryLimit = row.categoryLimit;
        fact.categoryHaveLimit = row.categoryHaveLimit;
        fact.setId = row.setId;
        fact.setPermitted = true;
        fact.setEvidenceKnown = row.setEvidenceKnown;
        fact.setBonusPresent = row.setBonusPresent;
        fact.scoreKnown = row.scoreKnown;
        fact.darkmoonCard = row.darkmoonCard;
        fact.darkmoonAvailabilityKnown = row.darkmoonAvailabilityKnown;
        fact.darkmoonProcKnown = row.darkmoonProcKnown;
        fact.requestForceBis = false;
        fact.sourceKnown = row.loaded;
        fact.metadataKnown = row.metadataKnown;
        fact.equipKnown = row.equipKnown;
        fact.equipAllowed = row.equipAllowed;
        fact.blockedOnlyByCurrentGear = row.blockedOnlyByCurrentGear;
        return fact;
    }

    inline void FillRetained(CandidateTemplate const& row, std::span<RetainedCount const> retained,
        ProvenSourceRow& proven)
    {
        proven.retainedKnown = true;
        proven.retainedOwned = 0;
        if (row.maxCount != 0)
        {
            RetainedCount const* held = FindEntryCount(retained, row.entry);
            std::uint32_t owned = 0;
            if (held == nullptr || !CountsKnown(*held) || !CountTotal(*held, owned))
                proven.retainedKnown = false;
            else
                proven.retainedOwned = owned;
        }
        if (row.categoryHaveLimit == 0)
            return;
        RetainedCount const* held = FindCategoryCount(retained, row.limitCategory);
        std::uint32_t owned = 0;
        if (held == nullptr || !CountsKnown(*held) || !CountTotal(*held, owned))
            proven.retainedKnown = false;
    }

    // Callable entry. Copies a bounded set of source rows, then asks
    // SelectFromFacts to plan them. It does not equip, destroy, or reselect.
    template <class Scorer>
    CollectorResult CollectCandidateFacts(CollectorRequest const& request, Scorer const& scorer)
    {
        CollectorResult result;
        result.deletesGear = false;
        CapInput capInput;
        capInput.playerContext = request.playerContext;
        capInput.progressionContext = request.progressionContext;
        capInput.mode = CapMode::Endgame;
        capInput.rawStage = request.rawStage;
        capInput.effectiveStage = request.effectiveStage;
        capInput.policy = ApprovedCapPolicy();
        capInput.scoreLimit = request.scoreLimit;
        CapResult const cap = ComposeCap(capInput);
        result.cap = cap.cap;

        bool const overBudget = request.rows.size() > request.rowBudget;
        result.truncated = overBudget || !request.sourceExhausted;
        std::size_t const limit = request.rows.size() < request.rowBudget ? request.rows.size() : request.rowBudget;
        bool incomplete = !cap.preserve && !request.qualityLimitKnown;
        if (!request.playerContext || !request.progressionContext || cap.preserve)
            incomplete = true;

        result.facts.reserve(limit);
        result.rows.reserve(limit);
        for (std::size_t index = 0; index < limit; ++index)
        {
            CandidateTemplate const& row = request.rows[index];
            ProvenSourceRow proven;
            proven.entry = row.entry;
            proven.randomProperty = row.randomProperty;
            proven.slot = row.slot;
            proven.classMask = row.classMask;
            proven.specRestricted = row.specRestricted;
            proven.specTab = row.specTab;
            proven.weapon = row.kind;
            proven.uniqueEquipped = row.uniqueEquipped;
            proven.maxCount = row.maxCount;
            proven.limitCategory = row.limitCategory;
            proven.categoryLimit = row.categoryLimit;
            proven.categoryHaveLimit = row.categoryHaveLimit;
            proven.setId = row.setId;
            proven.setEvidenceKnown = row.setEvidenceKnown;
            proven.setBonusPresent = row.setBonusPresent;
            proven.itemLevel = row.itemLevel;
            proven.quality = row.quality;
            FillRetained(row, request.retained, proven);
            if (!IdentityComplete(row))
            {
                proven.eligibility = FactDecision::Unknown;
                incomplete = true;
                result.rows.push_back(proven);
                continue;
            }
            proven.withinQuality = row.quality <= request.qualityLimit;
            proven.withinCap = cap.cap != 0 && row.itemLevel <= cap.cap;
            if (!proven.withinQuality || !proven.withinCap)
            {
                proven.eligibility = FactDecision::Unavailable;
                result.rows.push_back(proven);
                continue;
            }
            ItemFact fact = FactFromTemplate(row);
            FactDecision const decision = Classify(fact, request.bot, cap.cap);
            proven.eligibility = decision;
            bool const cardOpen = row.darkmoonCard && (!row.darkmoonAvailabilityKnown || !row.darkmoonProcKnown);
            if (decision == FactDecision::Unknown || decision == FactDecision::Invalid || cardOpen)
                incomplete = true;
            if (decision == FactDecision::Accept && !cardOpen)
            {
                proven.included = true;
                result.facts.push_back(fact);
            }
            result.rows.push_back(proven);
        }

        PlanInput input;
        input.cap = cap;
        input.bot = request.bot;
        input.facts = result.facts;
        input.eligibleMask = request.eligibleMask;
        input.requiredMask = request.requiredMask;
        input.completeMask = result.truncated || incomplete ? 0 : request.completeMask;
        input.retained = request.retained;
        input.bags = request.bags;
        input.authorityValid = request.authorityValid && !result.truncated && !incomplete && !cap.preserve;
        result.plan = SelectFromFacts(input, scorer);
        if (result.truncated || incomplete || cap.preserve)
        {
            result.action = GearAction::Preserve;
            result.preserve = true;
            result.claimedEmpty = false;
        }
        else
        {
            result.action = result.plan.outcome.action;
            result.preserve = result.action == GearAction::Preserve;
            result.claimedEmpty = result.action == GearAction::StripToBags;
        }
        result.deletesGear = false;
        return result;
    }
}

#endif
