/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_CANDIDATE_COLLECTOR_H
#define PLAYERBOTS_AUTOGEAR_CANDIDATE_COLLECTOR_H

#include "AutoGearSelectionPlan.h"
#include "AutoGearVariantEnumeration.h"
#include <array>
#include <vector>

namespace PlayerbotsAutoGearSelection
{
    // Vanilla Darkmoon trinkets whose tooltip metadata and procs disagree.
    // They stay unknown and are never forced.
    inline bool ConflictingDarkmoonCard(std::uint32_t entry)
    {
        return entry >= 19287 && entry <= 19290;
    }

    enum class CountPopulation : std::uint8_t
    {
        Unknown,
        AltBot,
        RandomBot
    };

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
        // Actual CanEquip / whole-set proof. The template reader never sets this.
        bool equipKnown = false;
        bool equipAllowed = false;
        bool blockedOnlyByCurrentGear = false;
        bool scoreKnown = true;
        // Template random-property group and random-suffix group. Zero means none.
        std::int32_t propertyGroup = 0;
        std::int32_t suffixGroup = 0;
        bool groupsKnown = false;
        std::span<std::uint32_t const> enchantPool{};
        bool poolPresent = false;
        std::span<std::uint8_t const> membership{};
        bool membershipKnown = false;
        std::span<std::uint32_t const> propertyDbc{};
        std::span<std::uint32_t const> suffixDbc{};
        bool dbcKnown = false;
        std::size_t variantBudget = 8;
    };

    // Caller-verified identity. Unlisted entries stay unknown.
    struct VerifiedItemIdentity
    {
        std::uint32_t entry = 0;
        bool verified = false;
        bool darkmoonCard = false;
        bool availabilityKnown = false;
        bool procKnown = false;
    };

    // Exact original that a verified random bot's disposition will retire.
    struct ApprovedOriginal
    {
        std::uint32_t entryId = 0;
        std::int32_t randomProperty = 0;
        bool propertyKnown = false;
        std::uint8_t slot = 0;
        bool approved = false;
        std::uint32_t limitCategory = 0;
        std::uint64_t guid = 0;
        bool guidKnown = false;
    };

    // Temporary custody. It is not part of the final projected count.
    struct StagingPiece
    {
        std::uint32_t entryId = 0;
        std::int32_t randomProperty = 0;
        std::uint32_t count = 0;
    };

    struct ProvenSourceRow
    {
        std::uint32_t entry = 0;
        std::int32_t randomProperty = 0;
        std::uint8_t slot = 0;
        FactDecision eligibility = FactDecision::Unknown;
        SourceEligibility staticEligibility = SourceEligibility::Unknown;
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
        bool equipProven = false;
    };

    struct CollectorRequest
    {
        // Already resolved for this command, including match, leveling, and numeric.
        bool capSupplied = false;
        CapResult resolvedCap{};
        CapMode resolvedMode = CapMode::Endgame;
        bool bis = false;
        bool qualityLimitKnown = false;
        std::uint32_t qualityLimit = 0;
        BotView bot{};
        std::span<CandidateTemplate const> rows{};
        std::span<VerifiedItemIdentity const> identities{};
        std::span<RetainedCount const> retained{};
        std::span<ApprovedOriginal const> approvedOriginals{};
        std::span<StagingPiece const> staging{};
        CountPopulation population = CountPopulation::Unknown;
        bool populationVerified = false;
        std::uint32_t eligibleMask = 0;
        std::uint32_t requiredMask = 0;
        std::uint32_t completeMask = 0;
        bool sourceExhausted = false;
        // True only when the caller proved this list is the whole legal universe.
        // A cache walk must leave it false.
        bool exhaustiveLegalDomain = false;
        std::size_t rowBudget = 64;
        // Enforced before candidate allocation and before scoring.
        std::size_t sourceBudget = 64;
        std::size_t expansionBudget = 64;
        std::uint64_t sourceRevision = 0;
        BagSpace bags = BagSpace::Unknown;
        bool authorityValid = false;
    };

    struct CollectorResult
    {
        bool preserve = true;
        bool truncated = false;
        bool conflict = false;
        bool projectionKnown = false;
        // True only for a justified exhaustive domain with no legal row.
        bool claimedEmpty = false;
        bool deletesGear = false;
        bool nativeEquipProven = false;
        bool nativeSourceUsed = false;
        std::uint32_t cap = 0;
        CapMode mode = CapMode::Endgame;
        bool bis = false;
        GearAction action = GearAction::Preserve;
        Plan plan{};
        std::vector<ItemFact> facts{};
        std::vector<ProvenSourceRow> rows{};
        std::vector<SourceRow> nativeRows{};
        std::array<std::vector<ProvenanceRow>, PlayerbotsAutoGearSet::Slots> provenance{};
        std::vector<RetainedCount> physicalCounts{};
        std::vector<RetainedCount> projectedCounts{};
        std::vector<StagingPiece> staging{};
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

    inline void ApplyVerifiedIdentity(CandidateTemplate& row, std::span<VerifiedItemIdentity const> identities)
    {
        if (ConflictingDarkmoonCard(row.entry))
        {
            row.darkmoonIdentityKnown = true;
            row.darkmoonCard = true;
            row.darkmoonAvailabilityKnown = false;
            row.darkmoonProcKnown = false;
            return;
        }
        VerifiedItemIdentity const* found = nullptr;
        for (VerifiedItemIdentity const& identity : identities)
        {
            if (identity.entry != row.entry)
                continue;
            if (found != nullptr || !identity.verified)
            {
                row.darkmoonIdentityKnown = false;
                return;
            }
            found = &identity;
        }
        if (found == nullptr)
        {
            row.darkmoonIdentityKnown = false;
            return;
        }
        row.darkmoonIdentityKnown = true;
        row.darkmoonCard = found->darkmoonCard;
        row.darkmoonAvailabilityKnown = found->availabilityKnown;
        row.darkmoonProcKnown = found->procKnown;
    }

    inline bool SameTemplateIdentity(CandidateTemplate const& left, CandidateTemplate const& right)
    {
        return left.entry == right.entry && left.slot == right.slot && left.randomProperty == right.randomProperty &&
               left.itemLevel == right.itemLevel && left.quality == right.quality && left.setId == right.setId &&
               left.limitCategory == right.limitCategory && left.maxCount == right.maxCount &&
               left.loaded == right.loaded && left.propertyGroup == right.propertyGroup &&
               left.suffixGroup == right.suffixGroup && left.groupsKnown == right.groupsKnown &&
               left.classMask == right.classMask && left.armorSubtype == right.armorSubtype &&
               left.kind == right.kind && left.uniqueEquipped == right.uniqueEquipped &&
               left.specRestricted == right.specRestricted && left.equipKnown == right.equipKnown &&
               left.equipAllowed == right.equipAllowed;
    }

    inline bool ConflictingTemplates(CandidateTemplate const& left, CandidateTemplate const& right)
    {
        if (left.entry != right.entry || left.slot != right.slot)
            return false;
        if (left.randomProperty != right.randomProperty)
            return false;
        return !SameTemplateIdentity(left, right);
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

    // mode 0 is HAVE. mode 1 is EQUIP. Any other mode stays unknown.
    template <class Entry>
    void ApplyLimitEntry(CandidateTemplate& row, Entry const* entry)
    {
        if (row.limitCategory == 0)
        {
            row.limitKnown = true;
            row.categoryLimit = 0;
            row.categoryHaveLimit = 0;
            return;
        }
        if (entry == nullptr || entry->ID != row.limitCategory || entry->maxCount == 0)
        {
            row.limitKnown = false;
            return;
        }
        if (entry->mode == 0)
        {
            row.categoryHaveLimit = entry->maxCount;
            row.categoryLimit = 0;
            row.limitKnown = true;
            return;
        }
        if (entry->mode == 1)
        {
            row.categoryLimit = entry->maxCount;
            row.categoryHaveLimit = 0;
            row.limitKnown = true;
            return;
        }
        row.limitKnown = false;
    }

    template <class Entry>
    void ApplySetEntry(CandidateTemplate& row, Entry const* entry)
    {
        if (row.setId == 0)
        {
            row.setEvidenceKnown = true;
            row.setBonusPresent = false;
            return;
        }
        if (entry == nullptr)
        {
            row.setEvidenceKnown = false;
            row.setBonusPresent = false;
            return;
        }
        row.setEvidenceKnown = true;
        row.setBonusPresent = false;
        for (std::size_t spell = 0; spell < 8; ++spell)
        {
            if (entry->spells[spell] != 0 && entry->items_to_triggerspell[spell] != 0)
                row.setBonusPresent = true;
        }
    }

    inline bool CombatDispositionSlot(std::uint8_t slot)
    {
        return slot < PlayerbotsAutoGearSet::Slots && slot != 3 && slot != 18;
    }

    // Random bots drop only exact approved originals from the final count.
    // Staging and unrelated rows stay. Alt bots keep the physical count.
    inline bool ProjectFinalCounts(CollectorRequest const& request, CollectorResult& result)
    {
        result.physicalCounts.assign(request.retained.begin(), request.retained.end());
        result.staging.assign(request.staging.begin(), request.staging.end());
        result.projectedCounts = result.physicalCounts;
        result.projectionKnown = true;
        bool const randomBot = request.populationVerified && request.population == CountPopulation::RandomBot;
        if (!randomBot)
            return true;
        for (ApprovedOriginal const& original : request.approvedOriginals)
        {
            if (!original.approved)
                continue;
            if (!original.propertyKnown || !original.guidKnown || !CombatDispositionSlot(original.slot))
            {
                result.projectionKnown = false;
                return false;
            }
            for (ApprovedOriginal const& earlier : request.approvedOriginals)
            {
                if (&earlier == &original || !earlier.approved)
                    continue;
                bool const sameSlot = earlier.slot == original.slot;
                bool const sameGuid = earlier.guidKnown && earlier.guid == original.guid;
                if (sameSlot || sameGuid)
                {
                    result.projectionKnown = false;
                    return false;
                }
            }
            bool matched = false;
            for (RetainedCount& row : result.projectedCounts)
            {
                if (row.entryId != original.entryId || row.limitCategory != 0)
                    continue;
                if (!row.equippedKnown || row.equipped == 0)
                {
                    result.projectionKnown = false;
                    return false;
                }
                --row.equipped;
                matched = true;
                break;
            }
            if (!matched)
            {
                result.projectionKnown = false;
                return false;
            }
            if (original.limitCategory == 0)
                continue;
            bool categoryMatched = false;
            for (RetainedCount& row : result.projectedCounts)
            {
                if (row.entryId != 0 || row.limitCategory != original.limitCategory)
                    continue;
                if (!row.equippedKnown || row.equipped == 0)
                {
                    result.projectionKnown = false;
                    return false;
                }
                --row.equipped;
                categoryMatched = true;
                break;
            }
            if (!categoryMatched)
            {
                result.projectionKnown = false;
                return false;
            }
        }
        return true;
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

    inline FactDecision StaticEligibility(ItemFact const& fact, BotView const& bot, std::uint32_t cap)
    {
        ItemFact staticFact = fact;
        staticFact.equipKnown = true;
        staticFact.equipAllowed = true;
        return Classify(staticFact, bot, cap);
    }

    // Callable entry. Copies a bounded set of source rows, then asks
    // SelectFromFacts to plan them. It does not equip, destroy, or reselect.
    template <class Scorer>
    CollectorResult CollectCandidateFacts(CollectorRequest const& request, Scorer const& scorer)
    {
        CollectorResult result;
        result.deletesGear = false;
        result.nativeEquipProven = false;
        result.mode = request.resolvedMode;
        result.bis = request.bis;
        if (request.rows.size() > request.sourceBudget)
        {
            result.truncated = true;
            result.preserve = true;
            result.claimedEmpty = false;
            result.action = GearAction::Preserve;
            return result;
        }
        std::vector<CandidateTemplate> expanded;
        bool variantGap = false;
        for (CandidateTemplate const& sourceRow : request.rows)
        {
            if (expanded.size() >= request.expansionBudget)
            {
                result.truncated = true;
                result.preserve = true;
                result.claimedEmpty = false;
                result.action = GearAction::Preserve;
                return result;
            }
            if (!sourceRow.groupsKnown)
            {
                expanded.push_back(sourceRow);
                continue;
            }
            if (sourceRow.propertyGroup == 0 && sourceRow.suffixGroup == 0)
            {
                CandidateTemplate plain = sourceRow;
                plain.randomProperty = 0;
                plain.randomPropertyKnown = true;
                expanded.push_back(plain);
                continue;
            }
            VariantRequest variants;
            variants.randomProperty = sourceRow.propertyGroup;
            variants.randomSuffix = sourceRow.suffixGroup;
            variants.groupsKnown = true;
            variants.pool = sourceRow.enchantPool;
            variants.poolPresent = sourceRow.poolPresent;
            variants.membership = sourceRow.membership;
            variants.membershipKnown = sourceRow.membershipKnown;
            variants.propertyDbc = sourceRow.propertyDbc;
            variants.suffixDbc = sourceRow.suffixDbc;
            variants.dbcKnown = sourceRow.dbcKnown;
            variants.budget = sourceRow.variantBudget;
            VariantResult const listed = EnumerateVariants(variants);
            if (!listed.complete)
            {
                variantGap = true;
                if (expanded.size() >= request.expansionBudget)
                {
                    result.truncated = true;
                    result.preserve = true;
                    result.claimedEmpty = false;
                    result.action = GearAction::Preserve;
                    return result;
                }
                CandidateTemplate unknown = sourceRow;
                unknown.randomPropertyKnown = false;
                expanded.push_back(unknown);
                continue;
            }
            if (expanded.size() + listed.variants.size() > request.expansionBudget)
            {
                result.truncated = true;
                result.preserve = true;
                result.claimedEmpty = false;
                result.action = GearAction::Preserve;
                return result;
            }
            for (VariantId const& variant : listed.variants)
            {
                CandidateTemplate copy = sourceRow;
                copy.randomProperty = variant.signedId;
                copy.randomPropertyKnown = true;
                expanded.push_back(copy);
            }
        }
        std::span<CandidateTemplate const> rows{expanded};
        bool const capReady = request.capSupplied && !request.resolvedCap.preserve && request.resolvedCap.cap != 0;
        result.cap = capReady ? request.resolvedCap.cap : 0;
        bool projectionOk = ProjectFinalCounts(request, result);
        bool conflict = false;
        for (std::size_t i = 0; i < rows.size() && !conflict; ++i)
        {
            for (std::size_t j = 0; j < i; ++j)
            {
                if (ConflictingTemplates(rows[i], rows[j]))
                    conflict = true;
            }
        }
        result.conflict = conflict;
        bool const overBudget = rows.size() > request.rowBudget;
        result.truncated = overBudget || !request.sourceExhausted || conflict || variantGap;
        std::size_t const limit = rows.size() < request.rowBudget ? rows.size() : request.rowBudget;
        bool incomplete = !capReady || !request.qualityLimitKnown || !projectionOk || conflict || variantGap;
        result.facts.reserve(limit);
        result.rows.reserve(limit);
        for (std::size_t index = 0; index < limit; ++index)
        {
            CandidateTemplate row = rows[index];
            ApplyVerifiedIdentity(row, request.identities);
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
            proven.equipProven = row.equipKnown && row.equipAllowed;
            FillRetained(row, result.projectedCounts, proven);
            if (!proven.retainedKnown)
                incomplete = true;
            SourceRow source;
            source.entryId = row.entry;
            source.randomProperty = row.randomProperty;
            source.itemLevel = row.itemLevel;
            source.slot = row.slot;
            source.templateLoaded = row.loaded && row.metadataKnown;
            ProvenanceRow provenance;
            provenance.entryId = row.entry;
            provenance.randomProperty = row.randomProperty;
            provenance.itemLevel = row.itemLevel;
            provenance.setId = row.setId;
            provenance.setEvidenceKnown = row.setEvidenceKnown;
            provenance.setBonusPresent = row.setBonusPresent;
            provenance.revision = request.sourceRevision;
            if (!IdentityComplete(row) || !capReady)
            {
                proven.eligibility = FactDecision::Unknown;
                proven.staticEligibility = SourceEligibility::Unknown;
                source.eligibility = SourceEligibility::Unknown;
                provenance.state = RowState::Unknown;
                incomplete = true;
            }
            else
            {
                proven.withinQuality = row.quality <= request.qualityLimit;
                proven.withinCap = row.itemLevel <= result.cap;
                ItemFact fact = FactFromTemplate(row);
                FactDecision const staticDecision = StaticEligibility(fact, request.bot, result.cap);
                FactDecision const decision = Classify(fact, request.bot, result.cap);
                proven.eligibility = decision;
                bool const cardOpen = row.darkmoonCard && (!row.darkmoonAvailabilityKnown || !row.darkmoonProcKnown);
                if (!proven.withinQuality || !proven.withinCap || staticDecision == FactDecision::Unavailable)
                {
                    proven.staticEligibility = SourceEligibility::KnownUnavailable;
                    source.eligibility = SourceEligibility::KnownUnavailable;
                    source.staticCapabilityKnown = true;
                    source.staticCapable = false;
                    provenance.state = RowState::KnownUnavailable;
                }
                else if (staticDecision == FactDecision::Accept)
                {
                    proven.staticEligibility = SourceEligibility::KnownEligible;
                    source.eligibility = SourceEligibility::KnownEligible;
                    source.staticCapabilityKnown = true;
                    source.staticCapable = true;
                    provenance.state = RowState::KnownPresent;
                    if (decision == FactDecision::Unknown || decision == FactDecision::Invalid || cardOpen ||
                        !proven.retainedKnown)
                        incomplete = true;
                    if (decision == FactDecision::Accept && !cardOpen && proven.retainedKnown)
                    {
                        proven.included = true;
                        result.facts.push_back(fact);
                    }
                }
                else
                {
                    proven.staticEligibility = SourceEligibility::Unknown;
                    source.eligibility = SourceEligibility::Unknown;
                    provenance.state = RowState::Unknown;
                    incomplete = true;
                }
            }
            if (row.slot < PlayerbotsAutoGearSet::Slots)
                result.provenance[row.slot].push_back(provenance);
            result.nativeRows.push_back(source);
            result.rows.push_back(proven);
        }

        bool splitVariant = false;
        for (std::size_t i = 0; i < result.nativeRows.size() && !splitVariant; ++i)
        {
            for (std::size_t j = 0; j < i; ++j)
            {
                if (result.nativeRows[i].entryId == result.nativeRows[j].entryId &&
                    result.nativeRows[i].randomProperty != result.nativeRows[j].randomProperty)
                    splitVariant = true;
            }
        }
        bool const justified = request.exhaustiveLegalDomain && request.sourceExhausted && !result.truncated &&
                               !incomplete && !conflict && capReady && projectionOk;
        PlanInput input;
        input.cap = request.resolvedCap;
        input.cap.cap = result.cap;
        input.cap.preserve = !capReady;
        input.bot = request.bot;
        input.facts = result.facts;
        input.eligibleMask = request.eligibleMask;
        input.requiredMask = request.requiredMask;
        input.completeMask = justified ? request.completeMask : 0;
        input.retained = result.projectedCounts;
        input.bags = request.bags;
        input.authorityValid = request.authorityValid && justified;
        input.sourceRevision = request.sourceRevision;
        input.sourceBudget = request.rowBudget;
        input.hasNativeSource = justified && request.sourceRevision != 0 && !splitVariant;
        if (input.hasNativeSource)
        {
            input.nativeRows = result.nativeRows;
            for (unsigned slot = 0; slot < PlayerbotsAutoGearSet::Slots; ++slot)
            {
                input.nativeProofs[slot].authoritative = true;
                input.nativeProofs[slot].rows = result.provenance[slot];
                input.nativeProofs[slot].rowsDeclared = true;
                input.nativeProofs[slot].declaredRows = result.provenance[slot].size();
            }
        }
        result.nativeSourceUsed = input.hasNativeSource;
        result.plan = SelectFromFacts(input, scorer);
        result.nativeEquipProven = false;
        if (!justified)
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
