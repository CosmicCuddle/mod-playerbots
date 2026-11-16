/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_WHOLE_SET_SCORE_H
#define PLAYERBOTS_AUTOGEAR_WHOLE_SET_SCORE_H

#include "AutoGearSetBonusCompare.h"
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
        Scored
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

    // Searches legal one-per-slot choices. A threshold delta is added only when
    // that many pieces of the set are chosen. Unknown set evidence does not score.
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
            if (piece.setId != 0 && !piece.evidenceKnown)
                return oracle;
        }
        std::array<std::uint8_t, 4> slots{};
        std::array<std::uint8_t, 4> counts{};
        std::uint8_t slotCount = 0;
        std::array<std::array<LegalPiece, 4>, 4> grouped{};
        for (LegalPiece const& piece : pieces)
        {
            int index = -1;
            for (std::uint8_t i = 0; i < slotCount; ++i)
            {
                if (slots[i] == piece.slot)
                    index = i;
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
            if (counts[static_cast<std::uint8_t>(index)] == itemBound)
            {
                oracle.status = WholeSetStatus::Bounded;
                return oracle;
            }
            grouped[static_cast<std::uint8_t>(index)][counts[static_cast<std::uint8_t>(index)]++] = piece;
        }
        std::array<LegalPiece, 4> picked{};
        bool found = false;
        auto consider = [&](auto&& self, std::uint8_t depth, std::uint32_t base) -> void
        {
            if (depth == slotCount)
            {
                std::uint32_t bonus = 0;
                for (SetDelta const& delta : deltas)
                {
                    std::uint32_t count = 0;
                    for (std::uint8_t i = 0; i < slotCount; ++i)
                    {
                        if (picked[i].setId == delta.setId && delta.setId != 0)
                            ++count;
                    }
                    if (count >= delta.threshold)
                    {
                        if (bonus > 0xFFFFFFFFu - delta.bonus)
                            return;
                        bonus += delta.bonus;
                    }
                }
                if (base > 0xFFFFFFFFu - bonus)
                    return;
                std::uint32_t const total = base + bonus;
                if (!found || total > oracle.score)
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
                if (base > 0xFFFFFFFFu - picked[depth].baseScore)
                    return;
                self(self, static_cast<std::uint8_t>(depth + 1), base + picked[depth].baseScore);
            }
        };
        consider(consider, 0, 0);
        if (!found && slotCount != 0)
        {
            oracle.status = WholeSetStatus::Bounded;
            return oracle;
        }
        oracle.status = WholeSetStatus::Scored;
        oracle.additiveSolverIsSetOptimal = false;
        return oracle;
    }
}

#endif
