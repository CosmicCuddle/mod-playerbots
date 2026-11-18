/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_MASTER_TIER_H
#define PLAYERBOTS_AUTOGEAR_MASTER_TIER_H

#include "AutoGearCommandRoute.h"
#include "AutoGearProgressionCap.h"
#include <array>
#include <cstdint>
#include <span>

namespace PlayerbotsAutoGearSelection
{
    // Owner decision 18 Nov 2026 14:49 UK. At TBC and Wrath max level, normal
    // autogear follows the master's average but never passes the raid tier
    // the master is actually wearing. BiS is unchanged. Pure logic: the
    // caller supplies each equipped item's raid source.
    enum class RaidPoint : std::uint8_t
    {
        None,       // quest, dungeon, crafted, vendor, PvP or unknown source
        TbcT4,      // Karazhan, Gruul, Magtheridon
        TbcT5,      // Serpentshrine, Tempest Keep (and disabled Zul'Aman)
        TbcT6,      // Hyjal, Black Temple
        TbcSunwell, // Sunwell Plateau
        WrathT7,    // Naxxramas, Obsidian Sanctum, Eye of Eternity
        WrathT8,    // Ulduar
        WrathT9,    // Trial of the Crusader, Onyxia 80
        WrathT10,   // Icecrown Citadel, Ruby Sanctum
        Ambiguous   // cross-phase sources such as Vault of Archavon
    };

    enum class RaidSize : std::uint8_t
    {
        Unknown,
        Ten,
        TwentyFive
    };

    // One occupied combat slot of the master. raidSourceKnown false or
    // RaidPoint::None gives no tier evidence.
    struct MasterItemSource
    {
        std::uint32_t itemLevel = 0;
        bool raidSourceKnown = false;
        RaidPoint point = RaidPoint::None;
        RaidSize size = RaidSize::Unknown;
        bool heroic = false;
    };

    enum class TierEra : std::uint8_t
    {
        None,
        Tbc,
        Wrath
    };

    constexpr TierEra EraForStage(std::uint8_t rawStage)
    {
        if (rawStage < PlayerbotsAutoGearCap::TbcEntryStage || rawStage > PlayerbotsAutoGearCap::LastSupportedStage)
            return TierEra::None;
        return rawStage < PlayerbotsAutoGearCap::WrathEntryStage ? TierEra::Tbc : TierEra::Wrath;
    }

    constexpr TierEra EraForPoint(RaidPoint point)
    {
        switch (point)
        {
            case RaidPoint::TbcT4:
            case RaidPoint::TbcT5:
            case RaidPoint::TbcT6:
            case RaidPoint::TbcSunwell:
                return TierEra::Tbc;
            case RaidPoint::WrathT7:
            case RaidPoint::WrathT8:
            case RaidPoint::WrathT9:
            case RaidPoint::WrathT10:
                return TierEra::Wrath;
            default:
                return TierEra::None;
        }
    }

    // Normal-mode item ceilings from the 18 Nov item-database research
    // (/tmp/ilvl report); the owner delegated these to the item records on
    // 18 Nov 16:08 UK. Ulduar 25 is its highest normal drop (Val'anyr stays
    // under the stage 15 cap); Sunwell is its highest actual drop. Wrath
    // unknown size uses the 10-man value. Heroic
    // sources count as normal until the planned master-Heroic check exists.
    constexpr std::uint32_t RaidPointCeiling(RaidPoint point, RaidSize size)
    {
        bool const big = size == RaidSize::TwentyFive;
        switch (point)
        {
            case RaidPoint::TbcT4:
                return 125;
            case RaidPoint::TbcT5:
                return 141;
            case RaidPoint::TbcT6:
                return 156;
            case RaidPoint::TbcSunwell:
                return 159;  // highest actual Sunwell drop (owner, 18 Nov 16:08 UK)
            case RaidPoint::WrathT7:
                return big ? 226 : 213;
            case RaidPoint::WrathT8:
                return big ? 239 : 232;
            case RaidPoint::WrathT9:
                return big ? 245 : 232;
            case RaidPoint::WrathT10:
                return big ? 271 : 264;
            default:
                return 0;
        }
    }

    // The lowest tier of the era: the fail-closed ceiling.
    constexpr std::uint32_t EraFloorCeiling(TierEra era)
    {
        if (era == TierEra::Tbc)
            return RaidPointCeiling(RaidPoint::TbcT4, RaidSize::Unknown);
        if (era == TierEra::Wrath)
            return RaidPointCeiling(RaidPoint::WrathT7, RaidSize::Ten);
        return 0;
    }

    struct MasterTier
    {
        bool known = false;          // false means the era floor was used
        std::uint32_t ceiling = 0;   // 0 only when the era is None
        std::uint32_t raidPieces = 0;
    };

    // The wearing tier is the highest ceiling C held by at least two raid
    // pieces at C or above, and by at least half of all raid pieces from this
    // era. Anything less (one stray piece, an even spread, other-era or
    // ambiguous sources, bad input) falls to the era's lowest tier.
    constexpr MasterTier ResolveMasterTier(TierEra era, std::span<MasterItemSource const> items)
    {
        MasterTier tier;
        if (era == TierEra::None)
            return tier;
        tier.ceiling = EraFloorCeiling(era);
        std::uint32_t pieces = 0;
        for (MasterItemSource const& item : items)
        {
            if (item.itemLevel == 0)
                return tier;
            if (!item.raidSourceKnown || item.point == RaidPoint::None)
                continue;
            if (item.point == RaidPoint::Ambiguous || EraForPoint(item.point) != era)
                continue;
            ++pieces;
        }
        tier.raidPieces = pieces;
        if (pieces < 2)
            return tier;
        std::uint32_t best = 0;
        for (MasterItemSource const& candidate : items)
        {
            if (!candidate.raidSourceKnown || EraForPoint(candidate.point) != era)
                continue;
            std::uint32_t const ceiling = RaidPointCeiling(candidate.point, candidate.size);
            if (ceiling <= best)
                continue;
            std::uint32_t atOrAbove = 0;
            for (MasterItemSource const& item : items)
            {
                if (item.raidSourceKnown && EraForPoint(item.point) == era &&
                    RaidPointCeiling(item.point, item.size) >= ceiling)
                    ++atOrAbove;
            }
            if (atOrAbove >= 2 && atOrAbove * 2 >= pieces)
                best = ceiling;
        }
        if (best == 0)
            return tier;
        tier.known = true;
        tier.ceiling = best;
        return tier;
    }

    // min(master average, tier ceiling). Zero average stays zero: preserve.
    constexpr std::uint32_t TierBoundedAverage(std::uint32_t average, MasterTier const& tier)
    {
        if (average == 0 || tier.ceiling == 0)
            return 0;
        return average < tier.ceiling ? average : tier.ceiling;
    }

    // Facts the route does not carry: the raid source of each slot of the
    // commanding sender, in the same order and length as
    // commandingSenderSlots (empty slots read itemLevel 0, no raid source).
    // The list is bound to the snapshot it was read from; see TierSourcesBound.
    struct TierFacts
    {
        bool sourcesVerified = false;
        std::span<MasterItemSource const> sources{};
        std::uint64_t senderGuid = 0;          // whose equipment was read
        std::uint32_t equipmentRevision = 0;   // equipment revision it was read at
    };

    // The commander snapshot the route's commandingSenderSlots came from.
    struct SenderSnapshot
    {
        std::uint64_t senderGuid = 0;
        std::uint32_t equipmentRevision = 0;
    };

    // G7 (CS01 #59 comment 6984350322): a source list may lift the tier only
    // when it describes exactly the sample the route averages. A missing or
    // extra entry, a slot whose item level differs from the equipped sample,
    // another sender, or another (stale or reused) equipment revision means
    // the list proves nothing, even when it is marked verified.
    constexpr bool TierSourcesBound(std::span<std::uint32_t const> slots, SenderSnapshot const& snapshot,
        TierFacts const& facts)
    {
        if (!facts.sourcesVerified || facts.sources.empty())
            return false;
        if (snapshot.senderGuid == 0 || snapshot.equipmentRevision == 0)
            return false;
        if (facts.senderGuid != snapshot.senderGuid || facts.equipmentRevision != snapshot.equipmentRevision)
            return false;
        if (facts.sources.size() != slots.size())
            return false;
        bool occupied = false;
        for (std::size_t i = 0; i < slots.size(); ++i)
        {
            MasterItemSource const& source = facts.sources[i];
            if (source.itemLevel != slots[i])
                return false;
            if (slots[i] == 0)
            {
                if (source.raidSourceKnown || source.point != RaidPoint::None)
                    return false;  // an empty slot cannot carry a raid source
                continue;
            }
            occupied = true;
        }
        return occupied;
    }

    // Tier of a bound list; empty slots are skipped. Unbound gives the era
    // floor (no lift).
    constexpr MasterTier ResolveBoundMasterTier(TierEra era, std::span<std::uint32_t const> slots,
        SenderSnapshot const& snapshot, TierFacts const& facts)
    {
        if (!TierSourcesBound(slots, snapshot, facts))
            return ResolveMasterTier(era, {});
        std::array<MasterItemSource, 32> occupied{};
        std::size_t count = 0;
        for (MasterItemSource const& source : facts.sources)
        {
            if (source.itemLevel == 0)
                continue;
            if (count == occupied.size())
                return ResolveMasterTier(era, {});
            occupied[count++] = source;
        }
        return ResolveMasterTier(era, std::span<MasterItemSource const>(occupied.data(), count));
    }

    // Wraps ResolveAutoGearRoute without changing it. At a TBC or Wrath stage
    // with a max-level commander, normal forms take the route's existing
    // post-leveling average path with the average lowered to the worn tier,
    // so the cap is min(average, tier ceiling, stage cap). At Vanilla stages
    // 1-7 with a max-level commander, normal forms take the same path with
    // no tier bound: min(average, stage cap) (owner, 18 Nov 16:08 UK). BiS
    // still takes the stage cap and match its own sample. Everything else
    // (stage 0, leveling, Unknown, tier sync) passes through.
    template <class Scorer>
    RouteDecision ResolveTieredAutoGearRoute(RouteRequest const& request, SenderSnapshot const& snapshot,
        TierFacts const& facts, Scorer const& scorer)
    {
        TierEra const era = EraForStage(request.rawStage);
        bool const vanillaLater = request.rawStage >= 1 && request.rawStage < PlayerbotsAutoGearCap::TbcEntryStage;
        if (request.tierSync || (era == TierEra::None && !vanillaLater))
            return ResolveAutoGearRoute(request, scorer);
        bool leveling = request.senderIsLeveling;
        if (request.senderLevelKnown)
        {
            SenderPhase const phase =
                SenderPhaseForStage(true, request.senderLevel, request.progressionContextVerified, request.rawStage);
            if (phase == SenderPhase::Unknown)
                return ResolveAutoGearRoute(request, scorer);  // the route preserves on Unknown
            leveling = phase == SenderPhase::Leveling;
        }
        if (leveling)
            return ResolveAutoGearRoute(request, scorer);
        if (era == TierEra::None)
        {
            RouteRequest averaged = request;
            averaged.firstStageAfterLeveling = true;
            return ResolveAutoGearRoute(averaged, scorer);
        }
        std::uint32_t const average =
            request.commandingSenderVerified ? OccupiedAverage(request.commandingSenderSlots) : 0;
        MasterTier const tier = request.commandingSenderVerified
            ? ResolveBoundMasterTier(era, request.commandingSenderSlots, snapshot, facts)
            : ResolveMasterTier(era, {});
        std::array<std::uint32_t, 1> const bounded{{TierBoundedAverage(average, tier)}};
        RouteRequest tiered = request;
        tiered.firstStageAfterLeveling = true;
        tiered.commandingSenderVerified = bounded[0] != 0;
        tiered.commandingSenderSlots = bounded;
        return ResolveAutoGearRoute(tiered, scorer);
    }

    // Without a commander snapshot no source list can be bound: no tier lift.
    template <class Scorer>
    RouteDecision ResolveTieredAutoGearRoute(RouteRequest const& request, TierFacts const& facts, Scorer const& scorer)
    {
        return ResolveTieredAutoGearRoute(request, SenderSnapshot{}, facts, scorer);
    }
}

#endif
