/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_SELECTION_PLAN_H
#define PLAYERBOTS_AUTOGEAR_SELECTION_PLAN_H

#include "AutoGearCandidateDomain.h"
#include "AutoGearConstraintFill.h"
#include "AutoGearSelectionCap.h"
#include "AutoGearSelectionOutcome.h"
#include "AutoGearSlotEnumeration.h"
#include "AutoGearVariantSolver.h"
#include "AutoGearVariantSource.h"
#include <algorithm>
#include <array>
#include <new>
#include <vector>

namespace PlayerbotsAutoGearSelection
{
    struct PlanInput
    {
        CapResult cap{};
        BotView bot{};
        std::span<ItemFact const> facts{};
        std::uint32_t eligibleMask = 0;
        std::uint32_t requiredMask = 0;
        std::uint32_t completeMask = 0;
        std::size_t candidateBudget = 1000;
        std::size_t nodeBudget = 100000;
        bool authorityValid = false;
        // Unknown until a real storage preflight says otherwise.
        BagSpace bags = BagSpace::Unknown;
        // When set, native rows are copied after facts are bounded and validated.
        bool hasNativeSource = false;
        std::span<SourceRow const> nativeRows{};
        std::size_t sourceBudget = 0;
        // Per-slot proof. Unknown is the default. completeMask is not this proof.
        enum class SlotListState : std::uint8_t
        {
            Unknown,
            Incomplete,
            Complete
        };
        std::array<SlotListState, PlayerbotsAutoGearSet::Slots> slotLists{};
        std::span<RetainedCount const> retained{};
        // Native mode requires these rows. They are not a caller-supplied verdict.
        std::uint64_t sourceRevision = 0;
        // Counts unavailable rows as well. The quadratic proof scan does not start past this.
        std::size_t proofRowBudget = 32;
        struct NativeSlotProof
        {
            bool authoritative = false;
            std::span<ProvenanceRow const> rows{};
            bool rowsDeclared = false;
            std::size_t declaredRows = 0;
        };
        std::uint64_t senderEpoch = 0;
        std::uint64_t senderIpRevision = 0;
        std::uint64_t senderEquipmentRevision = 0;
        std::uint64_t botEpoch = 0;
        std::uint64_t botEquipmentRevision = 0;
        std::uint64_t sessionIncarnation = 0;
        std::uint64_t botIncarnation = 0;
        std::uint64_t policyRevision = 0;
        bool freshnessRequired = false;
        std::uint64_t publishedSenderEpoch = 0;
        std::uint64_t publishedBotEpoch = 0;
        std::uint64_t publishedPolicyRevision = 0;
        std::uint64_t publishedSourceRevision = 0;
        std::array<NativeSlotProof, PlayerbotsAutoGearSet::Slots> nativeProofs{};
        // Fixture hook. Production callers leave this at None.
        enum class AllocationPoint : std::uint8_t
        {
            None,
            Scoring,
            Domain,
            Solve
        };
        AllocationPoint allocationFault = AllocationPoint::None;
    };

    inline bool SameNativeFact(SourceRow const& row, ItemFact const& fact)
    {
        return row.entryId == fact.id && row.randomProperty == fact.randomProperty && row.itemLevel == fact.itemLevel &&
               row.slot == fact.slot;
    }

    // Unknown possession preserves the plan. A known HAVE or MaxCount limit
    // only tells the shared solver how many copies it is allowed to see.
    enum class PossessionGate : std::uint8_t
    {
        Ok,
        Unknown,
        Omit
    };

    inline PossessionGate GatePossession(ItemFact const& fact, std::span<RetainedCount const> retained,
        std::uint32_t& room)
    {
        room = 0xFFFFFFFFu;
        if (fact.maxCount == 0 && fact.categoryHaveLimit == 0)
            return PossessionGate::Ok;
        if (fact.maxCount != 0)
        {
            RetainedCount const* row = FindEntryCount(retained, fact.id);
            std::uint32_t owned = 0;
            if (!row || !CountsKnown(*row) || !CountTotal(*row, owned))
                return PossessionGate::Unknown;
            if (owned >= fact.maxCount)
                return PossessionGate::Omit;
            room = fact.maxCount - owned;
        }
        if (fact.categoryHaveLimit != 0)
        {
            RetainedCount const* row = FindCategoryCount(retained, fact.limitCategory);
            std::uint32_t owned = 0;
            if (!row || !CountsKnown(*row) || !CountTotal(*row, owned))
                return PossessionGate::Unknown;
            if (owned >= fact.categoryHaveLimit)
                return PossessionGate::Omit;
            std::uint32_t const haveRoom = fact.categoryHaveLimit - owned;
            if (haveRoom < room)
                room = haveRoom;
        }
        return PossessionGate::Ok;
    }

    struct SelectedPiece
    {
        std::uint8_t slot = 0;
        std::uint32_t entryId = 0;
        std::int32_t randomProperty = 0;
    };

    // Planning handoff only. It does not call the storage transaction.
    // Retained gear that moves into bags is not deleted.
    struct StorageRequest
    {
        bool ready = false;
        bool deletesRetainedGear = false;
        std::array<SelectedPiece, PlayerbotsAutoGearSet::Slots> pieces{};
        std::uint64_t senderEpoch = 0;
        std::uint64_t senderIpRevision = 0;
        std::uint64_t senderEquipmentRevision = 0;
        std::uint64_t botEpoch = 0;
        std::uint64_t botEquipmentRevision = 0;
        std::uint64_t sessionIncarnation = 0;
        std::uint64_t botIncarnation = 0;
        std::uint64_t policyRevision = 0;
        std::uint64_t sourceRevision = 0;
    };

    struct Plan
    {
        Outcome outcome{};
        PlayerbotsAutoGearSet::Result solved{};
        std::array<SelectedPiece, PlayerbotsAutoGearSet::Slots> selected{};
        StorageRequest storage{};
        std::uint32_t cap = 0;
        std::size_t factsSupplied = 0;
        std::size_t factsAccepted = 0;
        bool sourceComplete = false;
        bool scoresTrustworthy = true;
    };

    template <class Scorer>
    Plan SelectFromFacts(PlanInput const& in, Scorer const& scorer)
    {
        using PlayerbotsAutoGearCommand::PreparationResult;
        Plan plan;
        plan.factsSupplied = in.facts.size();
        auto finish = [&](bool authority, PreparationResult prepared, bool searchComplete)
        {
            plan.sourceComplete = false;
            plan.outcome = MapOutcome(authority, prepared, searchComplete, in.bags);
            return plan;
        };
        if (!in.authorityValid || in.cap.preserve || in.cap.cap == 0)
            return finish(false, PreparationResult::InvalidPolicy, false);
        if (in.requiredMask == 0 || (in.requiredMask & ~in.eligibleMask) != 0)
            return finish(true, PreparationResult::InvalidPolicy, false);
        if (!in.bot.levelKnown || !in.bot.classKnown || !in.bot.specKnown || !in.bot.roleKnown)
            return finish(true, PreparationResult::InvalidPolicy, false);
        if ((in.eligibleMask & PlayerbotsAutoGearSet::WeaponMask) != 0 && !in.bot.weaponsKnown)
            return finish(true, PreparationResult::InvalidPolicy, false);
        if ((in.eligibleMask & ~PlayerbotsAutoGearSet::CombatMask) != 0 || (in.completeMask & ~in.eligibleMask) != 0)
            return finish(true, PreparationResult::InvalidPolicy, false);
        // A scheduled skip is not an empty slot. Leave the equipped item in place.
        if (UnsearchedEligibleSlot(in.bot, in.eligibleMask))
            return finish(true, PreparationResult::NotPrepared, false);
        // Bound and validate facts before they are matched to a native row.
        if (in.facts.size() > in.candidateBudget)
            return finish(true, PreparationResult::NotPrepared, false);
        for (ItemFact const& fact : in.facts)
        {
            FactDecision const decision = Classify(fact, in.bot, in.cap.cap);
            if (decision == FactDecision::Unknown || decision == FactDecision::Invalid)
                return finish(true, PreparationResult::InvalidPolicy, false);
        }
        bool needsVariant = false;
        for (ItemFact const& fact : in.facts)
        {
            if (fact.slot >= PlayerbotsAutoGearSet::Slots)
                return finish(true, PreparationResult::InvalidPolicy, false);
            std::uint32_t room = 0;
            if (GatePossession(fact, in.retained, room) == PossessionGate::Unknown)
                return finish(true, PreparationResult::NotPrepared, false);
            if (fact.maxCount != 0 || fact.categoryHaveLimit != 0 || fact.categoryLimit != 0 ||
                fact.randomProperty != 0)
                needsVariant = true;
        }
        if (!needsVariant)
        {
            for (std::size_t i = 0; i < in.facts.size(); ++i)
            {
                for (std::size_t j = 0; j < i; ++j)
                {
                    if (in.facts[i].id == in.facts[j].id && in.facts[i].randomProperty != in.facts[j].randomProperty)
                        needsVariant = true;
                }
            }
        }
        std::array<ListAssessment, PlayerbotsAutoGearSet::Slots> nativeAssessments{};
        if (in.hasNativeSource)
        {
            try
            {
                // Only the signed-key solver may consume distinct rolled variants.
                // Exact per-slot provenance validation below remains mandatory.
                CopiedSource const copied = CopyBoundedSource(in.nativeRows, in.sourceBudget, needsVariant);
                if (!copied.sourceComplete || copied.status != CopyStatus::Copied)
                    return finish(true, PreparationResult::NotPrepared, false);
                for (ItemFact const& fact : in.facts)
                {
                    bool found = false;
                    for (SourceRow const& row : copied.rows)
                    {
                        if (SameNativeFact(row, fact))
                            found = true;
                    }
                    if (!found)
                        return finish(true, PreparationResult::NotPrepared, false);
                }
                for (SourceRow const& row : copied.rows)
                {
                    bool found = false;
                    for (ItemFact const& fact : in.facts)
                    {
                        if (SameNativeFact(row, fact))
                            found = true;
                    }
                    if (!found)
                        return finish(true, PreparationResult::NotPrepared, false);
                }
                if (in.sourceRevision == 0)
                    return finish(true, PreparationResult::NotPrepared, false);
                std::size_t proofRows = 0;
                for (unsigned slot = 0; slot < PlayerbotsAutoGearSet::Slots; ++slot)
                {
                    if ((in.eligibleMask & (std::uint32_t{1} << slot)) == 0)
                        continue;
                    PlanInput::NativeSlotProof const& proof = in.nativeProofs[slot];
                    if (proof.rowsDeclared && proof.declaredRows != proof.rows.size())
                        return finish(true, PreparationResult::NotPrepared, false);
                    if (!AddProofCount(proofRows, proof.rows.size()))
                        return finish(true, PreparationResult::NotPrepared, false);
                }
                if (proofRows > in.proofRowBudget)
                    return finish(true, PreparationResult::NotPrepared, false);
                for (unsigned slot = 0; slot < PlayerbotsAutoGearSet::Slots; ++slot)
                {
                    if ((in.eligibleMask & (std::uint32_t{1} << slot)) == 0)
                        continue;
                    PlanInput::NativeSlotProof const& proof = in.nativeProofs[slot];
                    nativeAssessments[slot] =
                        AssessSlotList(proof.rows, in.sourceRevision, proof.authoritative, in.proofRowBudget);
                    if (nativeAssessments[slot].verdict == ListVerdict::Invalid ||
                        nativeAssessments[slot].verdict == ListVerdict::Incomplete)
                        return finish(true, PreparationResult::NotPrepared, false);
                    std::size_t present = 0;
                    for (ProvenanceRow const& row : proof.rows)
                    {
                        if (row.state != RowState::KnownPresent)
                            continue;
                        ++present;
                        bool inSource = false;
                        for (SourceRow const& source : copied.rows)
                        {
                            if (source.slot == slot && source.entryId == row.entryId &&
                                source.randomProperty == row.randomProperty && source.itemLevel == row.itemLevel)
                                inSource = true;
                        }
                        bool inFacts = false;
                        for (ItemFact const& fact : in.facts)
                        {
                            bool const sameFact = fact.slot == slot && fact.id == row.entryId &&
                                                      fact.randomProperty == row.randomProperty &&
                                                      fact.itemLevel == row.itemLevel && fact.setId == row.setId &&
                                                      fact.setEvidenceKnown == row.setEvidenceKnown &&
                                                      fact.setBonusPresent == row.setBonusPresent;
                            if (sameFact && Classify(fact, in.bot, in.cap.cap) == FactDecision::Accept)
                                inFacts = true;
                        }
                        if (!inSource || !inFacts)
                            return finish(true, PreparationResult::NotPrepared, false);
                    }
                    std::size_t copiedHere = 0;
                    for (SourceRow const& source : copied.rows)
                    {
                        if (source.slot == slot)
                            ++copiedHere;
                    }
                    std::size_t acceptedHere = 0;
                    for (ItemFact const& fact : in.facts)
                    {
                        if (fact.slot == slot && Classify(fact, in.bot, in.cap.cap) == FactDecision::Accept)
                            ++acceptedHere;
                    }
                    if (present != copiedHere || present != acceptedHere)
                        return finish(true, PreparationResult::NotPrepared, false);
                }
            }
            catch (...)
            {
                return finish(true, PreparationResult::NotPrepared, false);
            }
        }

        std::vector<AcceptedCandidate> accepted;
        DomainSet domains;
        try
        {
            if (in.allocationFault == PlanInput::AllocationPoint::Scoring)
                throw std::bad_alloc();
            accepted.reserve(in.facts.size());
            for (ItemFact const& fact : in.facts)
            {
                PreparedFact const prepared = PrepareFact(fact, in.bot, in.cap.cap, scorer);
                if (prepared.decision != FactDecision::Accept)
                    continue;
                AcceptedCandidate chosen;
                chosen.slot = fact.slot;
                chosen.candidate = prepared.candidate;
                chosen.randomProperty = fact.randomProperty;
                chosen.score = prepared.candidate.score;
                chosen.scoreKnown = fact.scoreKnown;
                chosen.setId = fact.setId;
                chosen.setEvidenceKnown = fact.setEvidenceKnown;
                chosen.setBonusPresent = fact.setBonusPresent;
                chosen.maxCount = fact.maxCount;
                chosen.categoryHaveLimit = fact.categoryHaveLimit;
                chosen.uniqueEquipped = fact.uniqueEquipped;
                chosen.exclusiveMainhand = fact.exclusiveMainhand;
                chosen.kind = fact.kind;
                accepted.push_back(chosen);
            }
        }
        catch (...)
        {
            return finish(true, PreparationResult::NotPrepared, false);
        }
        plan.factsAccepted = accepted.size();
        for (AcceptedCandidate const& chosen : accepted)
        {
            if (!chosen.scoreKnown || (chosen.setId != 0 && !chosen.setEvidenceKnown))
                plan.scoresTrustworthy = false;
        }
        for (std::size_t i = 0; i < accepted.size(); ++i)
        {
            for (std::size_t j = 0; j < i; ++j)
            {
                bool const same = accepted[i].slot == accepted[j].slot &&
                                  accepted[i].candidate.id == accepted[j].candidate.id &&
                                  accepted[i].randomProperty == accepted[j].randomProperty;
                if (same && accepted[i].score != accepted[j].score)
                    plan.scoresTrustworthy = false;
            }
        }

        std::array<PlayerbotsAutoGearSet::Domain, PlayerbotsAutoGearSet::Slots> views{};
        bool searchComplete = true;
        if (!needsVariant)
        {
            try
            {
                if (in.allocationFault == PlanInput::AllocationPoint::Domain)
                    throw std::bad_alloc();
                domains = CollectDomains(accepted, in.eligibleMask, in.completeMask, in.candidateBudget);
            }
            catch (...)
            {
                return finish(true, PreparationResult::NotPrepared, false);
            }
            if (domains.status == DomainSet::Status::Bounded)
                return finish(true, PreparationResult::NotPrepared, false);
            if (domains.status != DomainSet::Status::Ok)
                return finish(true, PreparationResult::InvalidPolicy, false);
            for (unsigned slot = 0; slot < PlayerbotsAutoGearSet::Slots; ++slot)
            {
                if ((in.eligibleMask & (std::uint32_t{1} << slot)) == 0)
                    continue;
                views[slot] = {domains.slots[slot], domains.complete[slot]};
                if (!domains.complete[slot])
                    searchComplete = false;
            }
        }
        else if (in.allocationFault == PlanInput::AllocationPoint::Domain)
        {
            return finish(true, PreparationResult::NotPrepared, false);
        }

        PlayerbotsAutoGearSet::Policy policy;
        policy.cap = in.cap.cap;
        policy.eligibleMask = in.eligibleMask;
        policy.requiredMask = in.requiredMask & ~PlayerbotsAutoGearSet::WeaponMask;
        policy.weapons = in.bot.weapons;
        if (in.requiredMask & (std::uint32_t{1} << PlayerbotsAutoGearSet::Main))
            policy.weapons.mainhandRequired = true;
        if (in.requiredMask & (std::uint32_t{1} << PlayerbotsAutoGearSet::Off))
            policy.weapons.offhandRequired = true;
        if (in.requiredMask & (std::uint32_t{1} << PlayerbotsAutoGearSet::Ranged))
            policy.weapons.rangedRequired = true;
        policy.known = true;

        plan.cap = in.cap.cap;
        try
        {
            if (in.allocationFault == PlanInput::AllocationPoint::Solve)
                throw std::bad_alloc();
            if (!needsVariant)
            {
                plan.solved = PlayerbotsAutoGearSet::Solve(views, policy, in.candidateBudget, in.nodeBudget);
                for (unsigned slot = 0; slot < PlayerbotsAutoGearSet::Slots; ++slot)
                {
                    plan.selected[slot].slot = static_cast<std::uint8_t>(slot);
                    plan.selected[slot].entryId = plan.solved.items[slot];
                }
            }
            else
            {
                std::array<std::vector<PlayerbotsAutoGearVariant::Candidate>, PlayerbotsAutoGearSet::Slots> lists;
                for (AcceptedCandidate const& chosen : accepted)
                {
                    PlayerbotsAutoGearVariant::Candidate row;
                    row.entryId = chosen.candidate.id;
                    row.randomProperty = chosen.randomProperty;
                    row.itemLevel = chosen.candidate.itemLevel;
                    row.score = chosen.score;
                    row.limitCategory = chosen.candidate.limitCategory;
                    row.categoryLimit = chosen.candidate.categoryLimit;
                    row.categoryHaveLimit = chosen.categoryHaveLimit;
                    row.maxCount = chosen.maxCount;
                    row.kind = chosen.kind;
                    row.uniqueEquipped = chosen.uniqueEquipped;
                    row.exclusiveMainhand = chosen.exclusiveMainhand;
                    row.metadataKnown = true;
                    row.eligible = true;
                    lists[chosen.slot].push_back(row);
                }
                std::array<PlayerbotsAutoGearVariant::Domain, PlayerbotsAutoGearSet::Slots> variantDomains{};
                for (unsigned slot = 0; slot < PlayerbotsAutoGearSet::Slots; ++slot)
                {
                    std::sort(lists[slot].begin(), lists[slot].end(),
                        [](PlayerbotsAutoGearVariant::Candidate const& left,
                            PlayerbotsAutoGearVariant::Candidate const& right)
                        {
                            return left.entryId < right.entryId ||
                                   (left.entryId == right.entryId && left.randomProperty < right.randomProperty);
                        });
                    if ((in.eligibleMask & (std::uint32_t{1} << slot)) == 0)
                        continue;
                    bool const complete = (in.completeMask & (std::uint32_t{1} << slot)) != 0;
                    variantDomains[slot] = {lists[slot], complete};
                }
                PlayerbotsAutoGearVariant::Policy variantPolicy;
                variantPolicy.cap = in.cap.cap;
                variantPolicy.eligibleMask = in.eligibleMask;
                variantPolicy.requiredMask = policy.requiredMask;
                variantPolicy.weapons = policy.weapons;
                variantPolicy.known = true;
                variantPolicy.entryHeld = in.retained;
                variantPolicy.categoryHeld = in.retained;
                PlayerbotsAutoGearVariant::Result const variant =
                    PlayerbotsAutoGearVariant::Solve(variantDomains, variantPolicy, in.candidateBudget, in.nodeBudget);
                plan.solved.score = variant.score;
                plan.solved.visited = variant.visited;
                plan.solved.items = variant.entries;
                for (unsigned slot = 0; slot < PlayerbotsAutoGearSet::Slots; ++slot)
                {
                    plan.selected[slot].slot = static_cast<std::uint8_t>(slot);
                    plan.selected[slot].entryId = variant.entries[slot];
                    plan.selected[slot].randomProperty = variant.properties[slot];
                }
                switch (variant.status)
                {
                    case PlayerbotsAutoGearVariant::Status::Invalid:
                        plan.solved.status = PlayerbotsAutoGearSet::Status::Invalid;
                        break;
                    case PlayerbotsAutoGearVariant::Status::Incomplete:
                        plan.solved.status = PlayerbotsAutoGearSet::Status::Incomplete;
                        break;
                    case PlayerbotsAutoGearVariant::Status::NoSet:
                        plan.solved.status = PlayerbotsAutoGearSet::Status::NoSet;
                        break;
                    case PlayerbotsAutoGearVariant::Status::SuitableSet:
                        plan.solved.status = PlayerbotsAutoGearSet::Status::SuitableSet;
                        break;
                }
            }
        }
        catch (...)
        {
            plan.sourceComplete = false;
            return finish(true, PreparationResult::NotPrepared, false);
        }
        if (plan.solved.status == PlayerbotsAutoGearSet::Status::Incomplete ||
            plan.solved.status == PlayerbotsAutoGearSet::Status::Invalid || !plan.scoresTrustworthy)
            searchComplete = false;
        // A caller-supplied completeMask (even with a Complete slotLists hint)
        // is NOT an authoritative empty-universe certificate. A no-set from
        // the direct facts-only path must never authorize StripToBags.
        // Native mode separately requires ProvenEmpty for every eligible slot.
        if (plan.solved.status == PlayerbotsAutoGearSet::Status::NoSet && !in.hasNativeSource)
            searchComplete = false;
        if (in.hasNativeSource)
        {
            bool provenEmptyUniverse = true;
            for (unsigned slot = 0; slot < PlayerbotsAutoGearSet::Slots; ++slot)
            {
                if ((in.eligibleMask & (std::uint32_t{1} << slot)) == 0)
                    continue;
                ListAssessment const& proof = nativeAssessments[slot];
                bool const required = (in.requiredMask & (std::uint32_t{1} << slot)) != 0;
                bool const empty = proof.verdict == ListVerdict::ProvenEmpty && proof.setBonusProvenAbsent;
                if (!empty)
                    provenEmptyUniverse = false;
                if (proof.verdict == ListVerdict::Invalid || proof.verdict == ListVerdict::Incomplete)
                    searchComplete = false;
                if (plan.solved.status == PlayerbotsAutoGearSet::Status::NoSet && !empty)
                    searchComplete = false;
                if (plan.solved.items[slot] != 0 && proof.verdict != ListVerdict::ProvenCandidates)
                    searchComplete = false;
                if (required && plan.solved.items[slot] == 0 && proof.verdict == ListVerdict::ProvenCandidates)
                    searchComplete = false;
            }
            if (plan.solved.status == PlayerbotsAutoGearSet::Status::NoSet && !provenEmptyUniverse)
                searchComplete = false;
        }
        bool const finished = plan.solved.status == PlayerbotsAutoGearSet::Status::SuitableSet ||
                              plan.solved.status == PlayerbotsAutoGearSet::Status::NoSet;
        plan.sourceComplete = searchComplete && finished;

        PreparationResult prepared = PreparationResult::InvalidPolicy;
        switch (plan.solved.status)
        {
            case PlayerbotsAutoGearSet::Status::SuitableSet:
                prepared = PreparationResult::SuitableSet;
                break;
            case PlayerbotsAutoGearSet::Status::NoSet:
                prepared = PreparationResult::NoSuitableSet;
                break;
            case PlayerbotsAutoGearSet::Status::Incomplete:
                prepared = PreparationResult::NotPrepared;
                break;
            case PlayerbotsAutoGearSet::Status::Invalid:
                prepared = PreparationResult::InvalidPolicy;
                break;
        }
        if (!plan.scoresTrustworthy)
        {
            searchComplete = false;
            prepared = PreparationResult::NotPrepared;
        }
        plan.outcome = MapOutcome(true, prepared, searchComplete, in.bags);
        plan.storage.pieces = plan.selected;
        plan.storage.senderEpoch = in.senderEpoch;
        plan.storage.senderIpRevision = in.senderIpRevision;
        plan.storage.senderEquipmentRevision = in.senderEquipmentRevision;
        plan.storage.botEpoch = in.botEpoch;
        plan.storage.botEquipmentRevision = in.botEquipmentRevision;
        plan.storage.sessionIncarnation = in.sessionIncarnation;
        plan.storage.botIncarnation = in.botIncarnation;
        plan.storage.policyRevision = in.policyRevision;
        plan.storage.sourceRevision = in.sourceRevision;
        bool const revisionsKnown = in.senderEpoch != 0 && in.senderIpRevision != 0 &&
                                    in.senderEquipmentRevision != 0 && in.botEpoch != 0 &&
                                    in.botEquipmentRevision != 0 && in.sessionIncarnation != 0 &&
                                    in.botIncarnation != 0 && in.policyRevision != 0 &&
                                    (!in.hasNativeSource || in.sourceRevision != 0);
        bool const fresh = !in.freshnessRequired || (in.publishedSenderEpoch == in.senderEpoch &&
                                                        in.publishedBotEpoch == in.botEpoch &&
                                                        in.publishedPolicyRevision == in.policyRevision &&
                                                        in.publishedSourceRevision == in.sourceRevision);
        plan.storage.ready = plan.outcome.disposition == Disposition::Commit && revisionsKnown && fresh &&
                             !plan.storage.deletesRetainedGear;
        if (in.freshnessRequired && !fresh)
        {
            plan.storage.ready = false;
            plan.sourceComplete = false;
            plan.outcome = {};
        }
        return plan;
    }
}

#endif
