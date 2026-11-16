/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_WHOLE_SET_SCORE_H
#define PLAYERBOTS_AUTOGEAR_WHOLE_SET_SCORE_H

#include "AutoGearSetBonusCompare.h"
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
    };

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
        std::uint8_t chosenSlots = 0;
    };

    // Every candidate in a slot is tried. A slot holds one item, or nothing when
    // that slot is optional. Overflow drops that sum and keeps searching.
    inline WholeSetOracle SearchLegalSet(std::span<LegalPiece const> pieces, std::span<SetDelta const> deltas,
        std::size_t slotBound, std::size_t itemBound)
    {
        WholeSetOracle oracle;
        if (slotBound == 0 || slotBound > 4 || itemBound == 0 || itemBound > 4)
        {
            oracle.status = WholeSetStatus::Bounded;
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
        for (std::size_t i = 0; i < pieces.size(); ++i)
        {
            for (std::size_t j = 0; j < i; ++j)
            {
                if (pieces[i].id != pieces[j].id)
                    continue;
                LegalPiece const& left = pieces[i];
                LegalPiece const& right = pieces[j];
                if (left.uniqueEquipped != right.uniqueEquipped || left.maxCount != right.maxCount ||
                    left.retained != right.retained || left.twoHand != right.twoHand || left.offHand != right.offHand)
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
                {
                    oracle.status = WholeSetStatus::Bounded;
                    return oracle;
                }
                index = slotCount;
                slots[slotCount++] = piece.slot;
            }
            std::uint8_t const at = static_cast<std::uint8_t>(index);
            if (counts[at] == itemBound)
            {
                oracle.status = WholeSetStatus::Bounded;
                return oracle;
            }
            if (counts[at] != 0 && optional[at] != piece.slotOptional)
            {
                oracle.status = WholeSetStatus::Bounded;
                return oracle;
            }
            optional[at] = piece.slotOptional;
            grouped[at][counts[at]++] = piece;
        }
        for (std::uint8_t slot = 0; slot < slotCount; ++slot)
        {
            std::sort(grouped[slot].begin(), grouped[slot].begin() + counts[slot],
                [](LegalPiece const& left, LegalPiece const& right) { return left.id < right.id; });
            if (!optional[slot])
                continue;
            if (counts[slot] == 5)
            {
                oracle.status = WholeSetStatus::Bounded;
                return oracle;
            }
            LegalPiece empty;
            empty.slot = slots[slot];
            empty.slotOptional = true;
            grouped[slot][counts[slot]++] = empty;
        }
        std::array<LegalPiece, 4> picked{};
        bool found = false;
        bool numericOverflow = false;
        auto smaller = [&](std::array<LegalPiece, 4> const& candidate) -> bool
        {
            for (std::uint8_t i = 0; i < slotCount; ++i)
            {
                if (candidate[i].id != oracle.chosen[i])
                    return candidate[i].id < oracle.chosen[i];
            }
            return false;
        };
        auto legal = [&]() -> bool
        {
            bool twoHand = false;
            bool offHand = false;
            for (std::uint8_t i = 0; i < slotCount; ++i)
            {
                if (picked[i].id == 0)
                    continue;
                if (picked[i].twoHand)
                    twoHand = true;
                if (picked[i].offHand)
                    offHand = true;
                std::uint32_t copies = 0;
                for (std::uint8_t j = 0; j < slotCount; ++j)
                {
                    if (picked[j].id == picked[i].id)
                        ++copies;
                }
                if (picked[i].uniqueEquipped && copies > 1)
                    return false;
                if (picked[i].maxCount == 0)
                    continue;
                if (copies > picked[i].maxCount || picked[i].retained > picked[i].maxCount - copies)
                    return false;
            }
            return !(twoHand && offHand);
        };
        auto consider = [&](auto&& self, std::uint8_t depth, std::uint32_t base) -> void
        {
            if (depth == slotCount)
            {
                if (!legal())
                    return;
                std::uint32_t bonus = 0;
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
                    if (bonus > 0xFFFFFFFFu - delta.bonus)
                    {
                        numericOverflow = true;
                        return;
                    }
                    bonus += delta.bonus;
                }
                if (base > 0xFFFFFFFFu - bonus)
                {
                    numericOverflow = true;
                    return;
                }
                std::uint32_t const total = base + bonus;
                if (!found || total > oracle.score || (total == oracle.score && smaller(picked)))
                {
                    found = true;
                    oracle.score = total;
                    oracle.additiveScore = base;
                    oracle.chosenSlots = slotCount;
                    for (std::uint8_t i = 0; i < slotCount; ++i)
                        oracle.chosen[i] = picked[i].id;
                }
                return;
            }
            for (std::uint8_t i = 0; i < counts[depth]; ++i)
            {
                picked[depth] = grouped[depth][i];
                std::uint32_t const added = picked[depth].id == 0 ? 0 : picked[depth].baseScore;
                if (base > 0xFFFFFFFFu - added)
                {
                    numericOverflow = true;
                    continue;
                }
                self(self, static_cast<std::uint8_t>(depth + 1), base + added);
            }
        };
        consider(consider, 0, 0);
        if (!found)
        {
            oracle.status = numericOverflow ? WholeSetStatus::Bounded : WholeSetStatus::Infeasible;
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
