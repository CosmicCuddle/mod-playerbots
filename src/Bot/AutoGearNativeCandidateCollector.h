/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_NATIVE_CANDIDATE_COLLECTOR_H
#define PLAYERBOTS_AUTOGEAR_NATIVE_CANDIDATE_COLLECTOR_H

#include "AutoGearCandidateCollector.h"
#include <array>
#include <vector>

namespace PlayerbotsAutoGearSelection
{
    // Class and subclass numbers are ItemTemplate.h at core 28df5c5f.
    inline PlayerbotsAutoGearWeapons::Kind KindFromTemplate(std::uint32_t itemClass, std::uint32_t subClass,
        std::uint32_t inventoryType)
    {
        using PlayerbotsAutoGearWeapons::Kind;
        if (itemClass == 2)
        {
            if (subClass == 2)
                return Kind::Bow;
            if (subClass == 3)
                return Kind::Gun;
            if (subClass == 18)
                return Kind::Crossbow;
            if (subClass == 19)
                return Kind::Wand;
            if (subClass == 16)
                return Kind::Thrown;
            if (subClass == 1 || subClass == 5 || subClass == 6 || subClass == 8 || subClass == 10 || subClass == 17 ||
                subClass == 20)
                return Kind::TwoHand;
            if (subClass == 0 || subClass == 4 || subClass == 7 || subClass == 13 || subClass == 15)
                return Kind::OneHand;
            return Kind::Count;
        }
        if (itemClass == 4)
        {
            if (subClass == 6 || inventoryType == 14)
                return Kind::Shield;
            if (inventoryType == 23)
                return Kind::Holdable;
            if (subClass >= 7 && subClass <= 10)
                return Kind::Relic;
        }
        return Kind::Count;
    }

    inline bool ExclusiveMainhand(std::uint32_t itemClass, std::uint32_t subClass)
    {
        return itemClass == 2 && (subClass == 6 || subClass == 10 || subClass == 17 || subClass == 20);
    }

    // Reads one ItemTemplate. Field names match core 28df5c5f. A random-property
    // group is not a rolled property, and this does not call CanEquipItem.
    template <class Proto>
    CandidateTemplate ReadItemTemplate(Proto const* proto, std::uint8_t slot)
    {
        CandidateTemplate row;
        row.slot = slot;
        row.slotKnown = slot < PlayerbotsAutoGearSet::Slots;
        if (proto == nullptr)
            return row;
        row.loaded = true;
        row.metadataKnown = true;
        row.entry = proto->ItemId;
        row.itemLevel = proto->ItemLevel;
        row.requiredLevel = proto->RequiredLevel;
        row.qualityKnown = true;
        row.quality = proto->Quality;
        std::uint32_t const mask = proto->AllowableClass;
        if (mask != 0 && mask != 0xFFFFFFFFu)
            row.classMask = mask;
        row.propertyGroup = proto->RandomProperty;
        row.suffixGroup = proto->RandomSuffix;
        row.groupsKnown = true;
        bool const grouped = row.propertyGroup != 0 || row.suffixGroup != 0;
        row.randomPropertyKnown = !grouped;
        if (proto->MaxCount > 0)
            row.maxCount = static_cast<std::uint32_t>(proto->MaxCount);
        row.uniqueEquipped = (static_cast<std::uint32_t>(proto->Flags) & 0x00080000u) != 0;
        row.limitCategory = proto->ItemLimitCategory;
        row.limitKnown = proto->ItemLimitCategory == 0;
        row.setId = proto->ItemSet;
        row.setEvidenceKnown = proto->ItemSet == 0;
        row.kind = KindFromTemplate(proto->Class, proto->SubClass, proto->InventoryType);
        row.exclusiveMainhand = ExclusiveMainhand(proto->Class, proto->SubClass);
        if (proto->Class == 4 && proto->SubClass <= 4)
            row.armorSubtype = static_cast<std::uint8_t>(proto->SubClass);
        return row;
    }

    struct NativeCollectQuery
    {
        CollectorRequest request{};
        std::uint32_t level = 0;
        std::uint8_t classId = 0;
    };

    template <class Inventory>
    inline Inventory ExactInventoryType(std::uint32_t raw)
    {
        return static_cast<Inventory>(raw);
    }

    inline void InventoryTypesForSlot(std::uint8_t slot, std::array<std::uint32_t, 4>& types, std::uint8_t& count)
    {
        count = 0;
        auto add = [&](std::uint32_t type)
        {
            if (count < types.size())
                types[count++] = type;
        };
        switch (slot)
        {
            case 0:
                add(1);
                break;
            case 1:
                add(2);
                break;
            case 2:
                add(3);
                break;
            case 4:
                add(5);
                add(20);
                break;
            case 5:
                add(6);
                break;
            case 6:
                add(7);
                break;
            case 7:
                add(8);
                break;
            case 8:
                add(9);
                break;
            case 9:
                add(10);
                break;
            case 10:
            case 11:
                add(11);
                break;
            case 12:
            case 13:
                add(12);
                break;
            case 14:
                add(16);
                break;
            case 15:
                add(13);
                add(17);
                add(21);
                break;
            case 16:
                add(13);
                add(14);
                add(22);
                add(23);
                break;
            case 17:
                add(15);
                add(25);
                add(26);
                add(28);
                break;
            default:
                break;
        }
    }

    // Walks RandomItemMgr::GetEquipment and GetEquipmentNew. Inventory is the
    // real InventoryType. The caches omit items above epic and invalid items,
    // and quest levels are normalized, so the walk is not a legal universe.
    template <class Inventory, class Source, class ItemLookup, class LimitLookup, class SetLookup, class Scorer>
    CollectorResult CollectNativeCandidateFacts(Source const& source, ItemLookup const& items,
        LimitLookup const& limits, SetLookup const& sets, NativeCollectQuery const& query, Scorer const& scorer)
    {
        struct Seen
        {
            CandidateTemplate row;
        };
        std::vector<Seen> seen;
        bool conflict = false;
        auto consider = [&](std::uint32_t entry, std::uint8_t slot)
        {
            CandidateTemplate row = ReadItemTemplate(items(entry), slot);
            if (row.limitCategory != 0)
                ApplyLimitEntry(row, limits(row.limitCategory));
            else
                row.limitKnown = true;
            if (row.setId != 0)
                ApplySetEntry(row, sets(row.setId));
            else
            {
                row.setEvidenceKnown = true;
                row.setBonusPresent = false;
            }
            for (Seen const& prior : seen)
            {
                if (prior.row.entry != entry || prior.row.slot != slot)
                    continue;
                if (!SameTemplateIdentity(prior.row, row))
                    conflict = true;
                return;
            }
            seen.push_back(Seen{row});
        };
        std::uint32_t const qualityCap = query.request.qualityLimit < 5 ? query.request.qualityLimit : 5;
        for (std::uint8_t slot = 0; slot < PlayerbotsAutoGearSet::Slots && !conflict; ++slot)
        {
            if ((query.request.eligibleMask & (std::uint32_t{1} << slot)) == 0)
                continue;
            for (std::uint32_t quality = 0; quality <= qualityCap && !conflict; ++quality)
            {
                auto const& listed = source.GetEquipment(query.level, query.classId, slot, quality);
                for (auto const entry : listed)
                    consider(entry, slot);
            }
            std::array<std::uint32_t, 4> types{};
            std::uint8_t typeCount = 0;
            InventoryTypesForSlot(slot, types, typeCount);
            for (std::uint8_t typeIndex = 0; typeIndex < typeCount && !conflict; ++typeIndex)
            {
                Inventory const inventoryType = ExactInventoryType<Inventory>(types[typeIndex]);
                for (std::uint32_t required = 0; required <= query.level && !conflict; ++required)
                {
                    auto const& listed = source.GetEquipmentNew(required, inventoryType);
                    for (auto const entry : listed)
                        consider(entry, slot);
                }
            }
        }
        bool const overBudget = seen.size() > query.request.rowBudget;
        std::size_t const limit = seen.size() < query.request.rowBudget ? seen.size() : query.request.rowBudget;
        std::vector<CandidateTemplate> gathered;
        gathered.reserve(limit);
        for (std::size_t index = 0; index < limit; ++index)
            gathered.push_back(seen[index].row);
        CollectorRequest request = query.request;
        request.rows = gathered;
        // The equipment caches are not the authoritative legal universe.
        request.exhaustiveLegalDomain = false;
        request.sourceExhausted = !overBudget && !conflict;
        request.completeMask = 0;
        if (overBudget)
            request.rowBudget = limit;
        CollectorResult result = CollectCandidateFacts(request, scorer);
        if (conflict)
            result.conflict = true;
        result.action = GearAction::Preserve;
        result.preserve = true;
        result.claimedEmpty = false;
        result.deletesGear = false;
        result.nativeEquipProven = false;
        if (overBudget)
            result.truncated = true;
        return result;
    }

    // Adapts StatsWeightCalculator::CalculateItem. The calculator still loads
    // its own template and applies its own set-bonus stats. This adds none.
    template <class Calculator>
    struct StatsWeightScore
    {
        Calculator* calculator = nullptr;

        std::uint32_t operator()(std::uint32_t itemId, std::int32_t randomProperty, std::uint32_t slot) const
        {
            if (calculator == nullptr)
                return 0;
            float const score = calculator->CalculateItem(itemId, randomProperty, static_cast<int>(slot));
            if (!(score > 0.f) || score >= 4294967295.f)
                return 0;
            return static_cast<std::uint32_t>(score);
        }
    };
}

#endif
