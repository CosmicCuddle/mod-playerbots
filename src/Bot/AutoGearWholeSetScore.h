/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_WHOLE_SET_SCORE_H
#define PLAYERBOTS_AUTOGEAR_WHOLE_SET_SCORE_H

#include "AutoGearSetBonusCompare.h"
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
}

#endif
