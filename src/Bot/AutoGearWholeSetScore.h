/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_WHOLE_SET_SCORE_H
#define PLAYERBOTS_AUTOGEAR_WHOLE_SET_SCORE_H

#include "AutoGearSetBonusCompare.h"
#include "AutoGearWeaponDomain.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

namespace PlayerbotsAutoGearSelection
{
    // Unknown evidence is not a score. The additive variant solver is not
    // set-optimal; this model is a bounded check of explicit synthetic bonuses.
    enum class WholeSetStatus : std::uint8_t
    {
        Unknown,
        Bounded,
        Scored,
        Infeasible
    };

    struct WholeSetScore
    {
        WholeSetStatus status = WholeSetStatus::Unknown;
        std::uint32_t score = 0;
        std::uint32_t upperBound = 0;
        bool additiveSolverIsSetOptimal = false;
    };

    inline WholeSetScore ScoreWholeSet(std::span<BonusPiece const> pieces, std::span<BonusRule const> rules,
        bool evidenceKnown, std::size_t pieceBound, std::size_t ruleBound)
    {
        WholeSetScore scored;
        if (!evidenceKnown)
            return scored;
        if (pieces.size() > pieceBound || rules.size() > ruleBound)
        {
            scored.status = WholeSetStatus::Bounded;
            return scored;
        }
        std::uint32_t bases = 0;
        std::uint32_t bonuses = 0;
        for (BonusPiece const& piece : pieces)
        {
            if (bases > 0xFFFFFFFFu - piece.baseScore)
            {
                scored.status = WholeSetStatus::Bounded;
                return scored;
            }
            bases += piece.baseScore;
        }
        for (BonusRule const& rule : rules)
        {
            if (bonuses > 0xFFFFFFFFu - rule.bonus)
            {
                scored.status = WholeSetStatus::Bounded;
                return scored;
            }
            bonuses += rule.bonus;
        }
        if (bases > 0xFFFFFFFFu - bonuses)
        {
            scored.status = WholeSetStatus::Bounded;
            return scored;
        }
        scored.upperBound = bases + bonuses;
        BonusCompare const compared = CompareSetBonus(pieces, rules, 0xFFFFFFFFu);
        if (compared.bounded || compared.jointScore > scored.upperBound)
        {
            scored.status = WholeSetStatus::Bounded;
            scored.score = 0;
            return scored;
        }
        scored.status = WholeSetStatus::Scored;
        scored.score = compared.jointScore;
        return scored;
    }

    struct LegalPiece
    {
        std::uint32_t id = 0;
        std::uint8_t slot = 0;
        std::uint32_t baseScore = 0;
        std::uint32_t setId = 0;
        bool evidenceKnown = true;
        bool uniqueEquipped = false;
        std::uint32_t maxCount = 0;
        std::uint32_t retained = 0;
        bool twoHand = false;
        bool offHand = false;
        bool slotOptional = false;
        std::int32_t randomProperty = 0;
        std::uint32_t allowedSlots = 0;
        std::uint32_t limitCategory = 0;
        std::uint32_t categoryLimit = 0;
        std::uint32_t categoryRetained = 0;
        bool slotMaskKnown = false;
        bool weaponKnown = false;
        PlayerbotsAutoGearWeapons::Kind weapon = PlayerbotsAutoGearWeapons::Kind::Count;
    };

    // Equipment slots used by the model check. This is not the game's optimizer.
    inline constexpr std::uint8_t kModelSlotCount = 19;
    inline constexpr std::uint8_t kModelMainSlot = 15;
    inline constexpr std::uint8_t kModelOffSlot = 16;

    // The shift happens only after the index is inside the domain.
    inline bool SlotBit(std::uint8_t slot, std::uint32_t& bit)
    {
        if (slot >= 32 || slot >= kModelSlotCount)
            return false;
        bit = std::uint32_t{1} << slot;
        return true;
    }

    struct SetDelta
    {
        std::uint32_t setId = 0;
        std::uint32_t threshold = 0;
        std::uint32_t bonus = 0;
    };

    struct WholeSetOracle
    {
        WholeSetStatus status = WholeSetStatus::Unknown;
        std::uint32_t score = 0;
        std::uint32_t additiveScore = 0;
        bool additiveSolverIsSetOptimal = false;
        std::array<std::uint32_t, 4> chosen{};
        std::array<std::int32_t, 4> chosenProperty{};
        std::uint8_t chosenSlots = 0;
    };

    // Bounded model check. It is not the game's gear optimizer. Past the bound the
    // result is Unknown. A legal sum that does not fit is Unknown for the whole
    // search. Infeasible is a confirmed empty result, not that safety result.
    inline WholeSetOracle SearchLegalSet(std::span<LegalPiece const> pieces, std::span<SetDelta const> deltas,
        std::size_t slotBound, std::size_t itemBound, bool domainComplete = false, std::uint32_t requiredSlotMask = 0)
    {
        WholeSetOracle oracle;
        if (slotBound == 0 || slotBound > 4 || itemBound == 0 || itemBound > 4)
            return oracle;
        std::uint32_t const domainMask = (std::uint32_t{1} << kModelSlotCount) - 1u;
        if ((requiredSlotMask & ~domainMask) != 0)
        {
            oracle.status = WholeSetStatus::Bounded;
            return oracle;
        }
        std::uint32_t requiredCount = 0;
        for (std::uint8_t slot = 0; slot < kModelSlotCount; ++slot)
        {
            std::uint32_t bit = 0;
            if (!SlotBit(slot, bit))
            {
                oracle.status = WholeSetStatus::Bounded;
                return oracle;
            }
            if ((requiredSlotMask & bit) != 0)
                ++requiredCount;
        }
        if (requiredCount > slotBound)
            return oracle;
        if (pieces.empty())
        {
            if (domainComplete)
                oracle.status = WholeSetStatus::Infeasible;
            return oracle;
        }
        for (LegalPiece const& piece : pieces)
        {
            if (piece.id == 0 || !piece.evidenceKnown)
                return oracle;
        }
        for (std::size_t i = 0; i < deltas.size(); ++i)
        {
            if (deltas[i].setId == 0 || deltas[i].threshold == 0)
            {
                oracle.status = WholeSetStatus::Bounded;
                return oracle;
            }
            for (std::size_t j = 0; j < i; ++j)
            {
                if (deltas[j].setId == deltas[i].setId && deltas[j].threshold == deltas[i].threshold)
                {
                    oracle.status = WholeSetStatus::Bounded;
                    return oracle;
                }
            }
        }
        for (LegalPiece const& piece : pieces)
        {
            std::uint32_t slotBit = 0;
            if (!SlotBit(piece.slot, slotBit))
            {
                oracle.status = WholeSetStatus::Bounded;
                return oracle;
            }
            if (!piece.slotMaskKnown || !piece.weaponKnown)
                return oracle;
            if (piece.allowedSlots == 0 || (piece.allowedSlots & slotBit) == 0)
            {
                oracle.status = WholeSetStatus::Bounded;
                return oracle;
            }
            if (piece.weapon == PlayerbotsAutoGearWeapons::Kind::TwoHand && piece.slot != kModelMainSlot)
            {
                oracle.status = WholeSetStatus::Bounded;
                return oracle;
            }
            if (piece.limitCategory != 0 && piece.categoryLimit == 0)
            {
                oracle.status = WholeSetStatus::Bounded;
                return oracle;
            }
        }
        for (std::size_t i = 0; i < pieces.size(); ++i)
        {
            for (std::size_t j = 0; j < i; ++j)
            {
                LegalPiece const& left = pieces[i];
                LegalPiece const& right = pieces[j];
                bool const sameEntry = left.id == right.id;
                bool const sameIdentity = sameEntry && left.randomProperty == right.randomProperty;
                if (sameEntry &&
                    (left.uniqueEquipped != right.uniqueEquipped || left.maxCount != right.maxCount ||
                        left.retained != right.retained || left.limitCategory != right.limitCategory ||
                        left.categoryLimit != right.categoryLimit || left.categoryRetained != right.categoryRetained ||
                        left.weapon != right.weapon || left.allowedSlots != right.allowedSlots))
                {
                    oracle.status = WholeSetStatus::Bounded;
                    return oracle;
                }
                std::uint32_t rightBit = 0;
                if (!SlotBit(right.slot, rightBit))
                {
                    oracle.status = WholeSetStatus::Bounded;
                    return oracle;
                }
                if (sameIdentity &&
                    (left.baseScore != right.baseScore || left.setId != right.setId ||
                        (left.allowedSlots & rightBit) == 0))
                {
                    oracle.status = WholeSetStatus::Bounded;
                    return oracle;
                }
            }
        }
        std::array<std::uint8_t, 4> slots{};
        std::array<std::uint8_t, 4> counts{};
        std::array<bool, 4> optional{};
        std::uint8_t slotCount = 0;
        std::array<std::array<LegalPiece, 5>, 4> grouped{};
        for (LegalPiece const& piece : pieces)
        {
            int index = -1;
            for (std::uint8_t i = 0; i < slotCount; ++i)
            {
                if (slots[i] == piece.slot)
                    index = static_cast<int>(i);
            }
            if (index < 0)
            {
                if (slotCount == slotBound)
                    return oracle;
                index = slotCount;
                slots[slotCount++] = piece.slot;
            }
            std::uint8_t const at = static_cast<std::uint8_t>(index);
            if (counts[at] == itemBound)
                return oracle;
            if (counts[at] != 0 && optional[at] != piece.slotOptional)
            {
                oracle.status = WholeSetStatus::Bounded;
                return oracle;
            }
            optional[at] = piece.slotOptional;
            grouped[at][counts[at]++] = piece;
        }
        for (std::uint8_t slot = 0; slot < kModelSlotCount; ++slot)
        {
            std::uint32_t bit = 0;
            if (!SlotBit(slot, bit) || (requiredSlotMask & bit) == 0)
                continue;
            bool present = false;
            for (std::uint8_t i = 0; i < slotCount; ++i)
            {
                if (slots[i] == slot)
                    present = true;
            }
            if (present)
                continue;
            if (!domainComplete)
                return oracle;
            oracle.status = WholeSetStatus::Infeasible;
            return oracle;
        }
        for (std::uint8_t slot = 0; slot < slotCount; ++slot)
        {
            std::sort(grouped[slot].begin(), grouped[slot].begin() + counts[slot],
                [](LegalPiece const& left, LegalPiece const& right)
                {
                    return left.id < right.id || (left.id == right.id && left.randomProperty < right.randomProperty);
                });
            if (!optional[slot])
                continue;
            if (counts[slot] == 5)
                return oracle;
            LegalPiece empty;
            empty.slot = slots[slot];
            empty.slotOptional = true;
            grouped[slot][counts[slot]++] = empty;
        }
        std::array<std::uint8_t, 4> order{};
        for (std::uint8_t i = 0; i < slotCount; ++i)
            order[i] = i;
        std::sort(order.begin(), order.begin() + slotCount,
            [&](std::uint8_t left, std::uint8_t right) { return slots[left] < slots[right]; });
        std::array<std::uint8_t, 4> sortedSlots{};
        std::array<std::uint8_t, 4> sortedCounts{};
        std::array<std::array<LegalPiece, 5>, 4> sortedGrouped{};
        for (std::uint8_t i = 0; i < slotCount; ++i)
        {
            sortedSlots[i] = slots[order[i]];
            sortedCounts[i] = counts[order[i]];
            sortedGrouped[i] = grouped[order[i]];
        }
        slots = sortedSlots;
        counts = sortedCounts;
        grouped = sortedGrouped;
        std::array<LegalPiece, 4> picked{};
        bool found = false;
        bool legalOverflow = false;
        auto smaller = [&](std::array<LegalPiece, 4> const& candidate) -> bool
        {
            for (std::uint8_t i = 0; i < slotCount; ++i)
            {
                if (candidate[i].id != oracle.chosen[i])
                    return candidate[i].id < oracle.chosen[i];
                if (candidate[i].randomProperty != oracle.chosenProperty[i])
                    return candidate[i].randomProperty < oracle.chosenProperty[i];
            }
            return false;
        };
        auto legal = [&]() -> bool
        {
            bool twoHand = false;
            bool offOccupied = false;
            for (std::uint8_t i = 0; i < slotCount; ++i)
            {
                if (picked[i].id == 0)
                    continue;
                if (picked[i].weapon == PlayerbotsAutoGearWeapons::Kind::TwoHand && picked[i].slot == kModelMainSlot)
                    twoHand = true;
                if (picked[i].slot == kModelOffSlot)
                    offOccupied = true;
                std::uint32_t copies = 0;
                std::uint32_t categoryCopies = 0;
                for (std::uint8_t j = 0; j < slotCount; ++j)
                {
                    if (picked[j].id == 0)
                        continue;
                    if (picked[j].id == picked[i].id)
                        ++copies;
                    if (picked[i].limitCategory != 0 && picked[j].limitCategory == picked[i].limitCategory)
                        ++categoryCopies;
                }
                if (picked[i].uniqueEquipped && copies > 1)
                    return false;
                if (picked[i].limitCategory != 0 &&
                    (picked[i].categoryRetained > picked[i].categoryLimit ||
                        categoryCopies > picked[i].categoryLimit - picked[i].categoryRetained))
                    return false;
                if (picked[i].maxCount == 0)
                    continue;
                if (copies > picked[i].maxCount || picked[i].retained > picked[i].maxCount - copies)
                    return false;
            }
            return !(twoHand && offOccupied);
        };
        auto consider = [&](auto&& self, std::uint8_t depth, std::uint64_t base) -> void
        {
            if (depth == slotCount)
            {
                if (!legal())
                    return;
                std::uint64_t bonus = 0;
                for (SetDelta const& delta : deltas)
                {
                    std::uint32_t count = 0;
                    for (std::uint8_t i = 0; i < slotCount; ++i)
                    {
                        if (picked[i].id != 0 && picked[i].setId == delta.setId)
                            ++count;
                    }
                    if (count < delta.threshold)
                        continue;
                    if (bonus > ~std::uint64_t{0} - delta.bonus)
                    {
                        legalOverflow = true;
                        return;
                    }
                    bonus += delta.bonus;
                }
                if (base > ~std::uint64_t{0} - bonus || base + bonus > 0xFFFFFFFFu)
                {
                    legalOverflow = true;
                    return;
                }
                std::uint32_t const total = static_cast<std::uint32_t>(base + bonus);
                if (!found || total > oracle.score || (total == oracle.score && smaller(picked)))
                {
                    found = true;
                    oracle.score = total;
                    oracle.additiveScore = static_cast<std::uint32_t>(base);
                    oracle.chosenSlots = slotCount;
                    for (std::uint8_t i = 0; i < slotCount; ++i)
                    {
                        oracle.chosen[i] = picked[i].id;
                        oracle.chosenProperty[i] = picked[i].randomProperty;
                    }
                }
                return;
            }
            for (std::uint8_t i = 0; i < counts[depth]; ++i)
            {
                picked[depth] = grouped[depth][i];
                std::uint64_t const added = picked[depth].id == 0 ? 0 : picked[depth].baseScore;
                self(self, static_cast<std::uint8_t>(depth + 1), base + added);
            }
        };
        consider(consider, 0, 0);
        if (legalOverflow)
        {
            oracle = {};
            oracle.status = WholeSetStatus::Unknown;
            return oracle;
        }
        if (!found)
        {
            oracle.status = WholeSetStatus::Infeasible;
            oracle.score = 0;
            oracle.additiveScore = 0;
            return oracle;
        }
        oracle.status = WholeSetStatus::Scored;
        oracle.additiveSolverIsSetOptimal = false;
        return oracle;
    }
}

#endif
