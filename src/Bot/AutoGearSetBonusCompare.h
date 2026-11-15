/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_SET_BONUS_COMPARE_H
#define PLAYERBOTS_AUTOGEAR_SET_BONUS_COMPARE_H

#include <array>
#include <cstdint>
#include <span>

namespace PlayerbotsAutoGearSelection
{
    // Independent of the shared set solver. Base scores are not rewritten.
    struct BonusPiece
    {
        std::uint32_t id = 0;
        std::uint8_t slot = 0;
        std::uint32_t baseScore = 0;
        std::uint32_t setId = 0;
        std::uint32_t itemLevel = 0;
    };

    struct BonusRule
    {
        std::uint32_t setId = 0;
        std::uint32_t pieces = 0;
        std::uint32_t bonus = 0;
    };

    struct BonusCompare
    {
        bool bounded = true;
        std::uint32_t greedyScore = 0;
        std::uint32_t jointScore = 0;
        std::array<std::uint32_t, 4> greedyIds{};
        std::array<std::uint32_t, 4> jointIds{};
    };

    inline constexpr std::size_t kBonusSlotCap = 4;
    inline constexpr std::size_t kBonusItemCap = 4;

    inline std::uint32_t SetBonusFor(std::span<BonusPiece const> chosen, std::span<BonusRule const> rules)
    {
        std::uint32_t total = 0;
        for (BonusRule const& rule : rules)
        {
            if (rule.setId == 0 || rule.pieces == 0)
                continue;
            std::uint32_t count = 0;
            for (BonusPiece const& piece : chosen)
            {
                if (piece.setId == rule.setId)
                    ++count;
            }
            if (count >= rule.pieces)
                total += rule.bonus;
        }
        return total;
    }

    // Greedy picks the highest base score in each slot. Joint adds the set
    // bonus only after a full combination is chosen. Over-cap pieces are
    // excluded from both. Past the bound, neither score is a result.
    inline BonusCompare CompareSetBonus(std::span<BonusPiece const> pieces, std::span<BonusRule const> rules,
        std::uint32_t cap)
    {
        BonusCompare compared;
        std::array<std::array<BonusPiece, kBonusItemCap>, kBonusSlotCap> slots{};
        std::array<std::uint8_t, kBonusSlotCap> counts{};
        std::uint8_t usedSlots = 0;
        for (BonusPiece const& piece : pieces)
        {
            if (piece.id == 0 || piece.itemLevel == 0 || piece.itemLevel > cap || piece.slot >= kBonusSlotCap)
                continue;
            std::uint8_t& count = counts[piece.slot];
            if (count == 0)
                ++usedSlots;
            if (count >= kBonusItemCap || usedSlots > kBonusSlotCap)
            {
                compared.bounded = true;
                return compared;
            }
            slots[piece.slot][count] = piece;
            ++count;
        }
        if (usedSlots == 0 || usedSlots > kBonusSlotCap)
            return compared;

        std::array<std::uint8_t, kBonusSlotCap> order{};
        std::uint8_t n = 0;
        for (std::uint8_t slot = 0; slot < kBonusSlotCap; ++slot)
        {
            if (counts[slot] != 0)
                order[n++] = slot;
        }

        for (std::uint8_t i = 0; i < n; ++i)
        {
            BonusPiece best = slots[order[i]][0];
            for (std::uint8_t k = 1; k < counts[order[i]]; ++k)
            {
                BonusPiece const& candidate = slots[order[i]][k];
                if (candidate.baseScore > best.baseScore ||
                    (candidate.baseScore == best.baseScore && candidate.id < best.id))
                    best = candidate;
            }
            compared.greedyIds[i] = best.id;
            compared.greedyScore += best.baseScore;
        }
        std::array<BonusPiece, kBonusSlotCap> greedyChosen{};
        for (std::uint8_t i = 0; i < n; ++i)
        {
            for (std::uint8_t k = 0; k < counts[order[i]]; ++k)
            {
                if (slots[order[i]][k].id == compared.greedyIds[i])
                    greedyChosen[i] = slots[order[i]][k];
            }
        }
        compared.greedyScore += SetBonusFor(std::span<BonusPiece const>(greedyChosen.data(), n), rules);

        std::array<std::uint8_t, kBonusSlotCap> pick{};
        std::uint32_t bestJoint = 0;
        std::array<std::uint32_t, 4> bestIds{};
        bool found = false;
        auto consider = [&](auto&& self, std::uint8_t depth, std::uint32_t score) -> void
        {
            if (depth == n)
            {
                std::array<BonusPiece, kBonusSlotCap> chosen{};
                for (std::uint8_t i = 0; i < n; ++i)
                    chosen[i] = slots[order[i]][pick[i]];
                score += SetBonusFor(std::span<BonusPiece const>(chosen.data(), n), rules);
                bool const better = !found || score > bestJoint;
                bool tie = found && score == bestJoint;
                if (tie)
                {
                    for (std::uint8_t i = 0; i < n; ++i)
                    {
                        std::uint32_t const id = chosen[i].id;
                        if (id != bestIds[i])
                        {
                            tie = id < bestIds[i];
                            break;
                        }
                    }
                }
                if (better || tie)
                {
                    found = true;
                    bestJoint = score;
                    for (std::uint8_t i = 0; i < n; ++i)
                        bestIds[i] = chosen[i].id;
                }
                return;
            }
            for (std::uint8_t k = 0; k < counts[order[depth]]; ++k)
            {
                pick[depth] = k;
                self(self, static_cast<std::uint8_t>(depth + 1), score + slots[order[depth]][k].baseScore);
            }
        };
        consider(consider, 0, 0);
        compared.bounded = false;
        compared.jointScore = bestJoint;
        compared.jointIds = bestIds;
        return compared;
    }
}

#endif
