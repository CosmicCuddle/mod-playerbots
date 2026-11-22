/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOGEAR_MASTER_SOURCE_INDEX_H
#define PLAYERBOTS_AUTOGEAR_MASTER_SOURCE_INDEX_H

// CP40 (owner approval 20 Nov 2026 11:50 UK, CP39 item 1). A fail-closed
// lookup from an item entry to the raid, size and difficulty that drop it,
// built from world-database loot rows, plus the adapter that binds a
// commander equipment capture to TierFacts (AutoGearMasterTier.h).
//
// An entry is raid proof only when every loot record of it resolves to
// exactly one (raid, size, difficulty) and it has no vendor or quest source.
// Anything else is Unknown, and Unknown never raises the tier. Nothing is
// inferred from item level, the map the master stands in, or a lockout:
// difficulty comes only from the loot record (the creature difficulty
// clone or the chest spawn mask). Individual Progression runs its Vanilla
// raids (Naxxramas 40, Onyxia 40) in 10-man Heroic on the WotLK map, so
// that bucket is Vanilla, never Wrath Heroic.
//
// Pure logic: the caller supplies the rows. No server headers are needed
// except for the optional PB59::EquipmentFacts overload at the bottom.

#include "AutoGearMasterTier.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <span>
#include <vector>

namespace PlayerbotsAutoGearMasterSource
{
    using PlayerbotsAutoGearSelection::MasterItemSource;
    using PlayerbotsAutoGearSelection::RaidPoint;
    using PlayerbotsAutoGearSelection::RaidSize;
    using PlayerbotsAutoGearSelection::SenderSnapshot;
    using PlayerbotsAutoGearSelection::TierFacts;

    // Core spawn modes: 0 10N, 1 25N, 2 10H, 3 25H (Map::GetSpawnMode).
    constexpr std::uint8_t DifficultyCount = 4;

    enum class Verdict : std::uint8_t
    {
        Unknown,      // not found, shared, vendor, quest, unmapped or ambiguous
        VanillaRaid,  // one Vanilla raid bucket: no TBC or Wrath tier evidence
        Raid          // one TBC or Wrath raid bucket: tier evidence
    };

    struct RaidBucket
    {
        Verdict verdict = Verdict::Unknown;
        RaidPoint point = RaidPoint::None;
        RaidSize size = RaidSize::Unknown;
        bool heroic = false;
    };

    // Explicit (map, difficulty) table. A pair not listed is Unknown. TBC
    // raids have only difficulty 0. Wrath Naxxramas and Onyxia have no 10H
    // in Wrath: on these maps 10H is the IP Vanilla version. Vault of
    // Archavon spans tiers and is left Unknown.
    constexpr RaidBucket ClassifyBucket(std::uint32_t map, std::uint8_t difficulty)
    {
        auto raid = [](RaidPoint point, RaidSize size, bool heroic) {
            return RaidBucket{Verdict::Raid, point, size, heroic};
        };
        auto wrath = [&](RaidPoint point, bool hasHeroic) -> RaidBucket {
            switch (difficulty)
            {
                case 0: return raid(point, RaidSize::Ten, false);
                case 1: return raid(point, RaidSize::TwentyFive, false);
                case 2: return hasHeroic ? raid(point, RaidSize::Ten, true) : RaidBucket{};
                case 3: return hasHeroic ? raid(point, RaidSize::TwentyFive, true) : RaidBucket{};
                default: return RaidBucket{};
            }
        };
        RaidBucket const vanilla{Verdict::VanillaRaid, RaidPoint::None, RaidSize::Unknown, false};
        switch (map)
        {
            case 309: case 409: case 469: case 509: case 531:  // ZG, MC, BWL, AQ20, AQ40
                return difficulty == 0 ? vanilla : RaidBucket{};
            case 532:  // Karazhan
                return difficulty == 0 ? raid(RaidPoint::TbcT4, RaidSize::Ten, false) : RaidBucket{};
            case 565: case 544:  // Gruul, Magtheridon
                return difficulty == 0 ? raid(RaidPoint::TbcT4, RaidSize::TwentyFive, false) : RaidBucket{};
            case 548: case 550:  // Serpentshrine, Tempest Keep
                return difficulty == 0 ? raid(RaidPoint::TbcT5, RaidSize::TwentyFive, false) : RaidBucket{};
            case 568:  // Zul'Aman
                return difficulty == 0 ? raid(RaidPoint::TbcT5, RaidSize::Ten, false) : RaidBucket{};
            case 534: case 564:  // Hyjal, Black Temple
                return difficulty == 0 ? raid(RaidPoint::TbcT6, RaidSize::TwentyFive, false) : RaidBucket{};
            case 580:  // Sunwell
                return difficulty == 0 ? raid(RaidPoint::TbcSunwell, RaidSize::TwentyFive, false) : RaidBucket{};
            case 533: case 249:  // Naxxramas, Onyxia: 10H is the IP 40-man Vanilla version
                if (difficulty == 2)
                    return vanilla;
                return wrath(map == 533 ? RaidPoint::WrathT7 : RaidPoint::WrathT9, false);
            case 615: case 616:  // Obsidian Sanctum, Eye of Eternity
                return wrath(RaidPoint::WrathT7, false);
            case 603:  // Ulduar
                return wrath(RaidPoint::WrathT8, false);
            case 649:  // Trial of the Crusader
                return wrath(RaidPoint::WrathT9, true);
            case 631:  // Icecrown Citadel
                return wrath(RaidPoint::WrathT10, true);
            case 724:  // Ruby Sanctum
                return wrath(RaidPoint::WrathRubySanctum, true);
            default:
                return RaidBucket{};
        }
    }

    // ---- World rows (as read from the world database) ------------------
    struct LootRow                // creature_ / gameobject_ / reference_ / item_loot_template
    {
        std::uint32_t entry = 0;
        std::uint32_t item = 0;
        std::uint32_t reference = 0;  // non-zero: expand reference_loot_template
    };

    struct CreatureTemplateRow
    {
        std::uint32_t entry = 0;
        std::array<std::uint32_t, 3> difficultyEntry{};  // difficulty_entry_1..3
        std::uint32_t lootId = 0;
    };

    struct SpawnRow               // creature (id1) or gameobject (id)
    {
        std::uint32_t id = 0;
        std::uint32_t map = 0;
        std::uint8_t spawnMask = 0;
    };

    struct ChestTemplateRow       // gameobject_template type 3 (chest), Data1
    {
        std::uint32_t entry = 0;
        std::uint32_t lootId = 0;
    };

    // quest_template: what a quest rewards and what completing it needs.
    struct QuestRow
    {
        std::uint32_t id = 0;
        std::array<std::uint32_t, 10> rewards{};  // RewardItem1-4, RewardChoiceItemID1-6
        std::array<std::uint32_t, 7> items{};     // RequiredItemId1-6, StartItem
        std::array<std::int32_t, 4> npcOrGo{};    // RequiredNpcOrGo1-4: > 0 creature, < 0 gameobject
    };

    // quest_template_addon.PrevQuestID: the quest that must come first (negative: must be active).
    struct QuestChainRow
    {
        std::uint32_t id = 0;
        std::int32_t prev = 0;
    };

    struct WorldRows
    {
        std::span<LootRow const> creatureLoot{};
        std::span<LootRow const> gameobjectLoot{};
        std::span<LootRow const> referenceLoot{};
        std::span<LootRow const> itemLoot{};          // container contents: not a raid record
        std::span<CreatureTemplateRow const> creatures{};
        std::span<SpawnRow const> creatureSpawns{};
        std::span<ChestTemplateRow const> chests{};
        std::span<SpawnRow const> chestSpawns{};
        std::span<std::uint32_t const> vendorItems{};  // npc_vendor.item (any ExtendedCost)
        std::span<std::uint32_t const> questRewardItems{};  // RewardItem*, RewardChoiceItemID*
        std::span<QuestRow const> quests{};                 // the same rewards with their requirements
        std::span<QuestChainRow const> questChains{};
    };

    struct SourceEntry
    {
        Verdict verdict = Verdict::Unknown;
        RaidPoint point = RaidPoint::None;
        RaidSize size = RaidSize::Unknown;
        bool heroic = false;
        // The one (map, difficulty) bucket that proved the verdict. Zero when Unknown.
        std::uint32_t map = 0;
        std::uint8_t difficulty = 0;
    };

    // Every source the world rows show for one item, for gates that ask whether the item can be
    // obtained anywhere the player may go (AutoGearSourceAccess.h), rather than "which one raid".
    // Spawned loot is (map, difficulty). Loot reached only by unspawned or script-summoned templates,
    // container contents and vendors are flags. Quest rewards keep the rewarding quests, so a gate can
    // ask what completing them needs.
    struct AccessEntry
    {
        bool known = false;  // any loot, vendor or quest row names the item
        std::vector<std::pair<std::uint32_t, std::uint8_t>> spawned{};
        bool unattributed = false;
        bool container = false;
        bool vendor = false;
        std::vector<std::uint32_t> quests{};  // quests that reward it
        bool questUnstructured = false;       // a reward row without requirements (older callers)
    };

    // What completing one quest needs.
    struct QuestFacts
    {
        std::array<std::uint32_t, 7> items{};   // hand-ins and the start item
        std::array<std::int32_t, 4> npcOrGo{};  // kill or use targets
        std::int32_t prev = 0;                  // quest that must come first
    };

    class SourceIndex
    {
    public:
        // Rows are copied into the index; the spans may die afterwards.
        static SourceIndex Build(WorldRows const& rows)
        {
            SourceIndex index;
            Builder builder(rows);
            builder.Run(index._entries, index._access);
            for (QuestRow const& quest : rows.quests)
                index._quests[quest.id] = QuestFacts{quest.items, quest.npcOrGo, 0};
            for (QuestChainRow const& chain : rows.questChains)
                if (auto const found = index._quests.find(chain.id); found != index._quests.end())
                    found->second.prev = chain.prev;
            for (SpawnRow const& spawn : rows.creatureSpawns)
                for (std::uint8_t diff = 0; diff < DifficultyCount; ++diff)
                    if (spawn.spawnMask & (1u << diff))
                        index._creatureSpawns[spawn.id].emplace_back(spawn.map, diff);
            index._built = true;
            return index;
        }

        bool Built() const { return _built; }

        SourceEntry Lookup(std::uint32_t item) const
        {
            if (!_built || item == 0)
                return {};
            auto const found = _entries.find(item);
            return found == _entries.end() ? SourceEntry{} : found->second;
        }

        // The same without a copy, for per-candidate checks; nullptr when no row names the item.
        AccessEntry const* FindAccess(std::uint32_t item) const
        {
            if (!_built || item == 0)
                return nullptr;
            auto const found = _access.find(item);
            return found == _access.end() ? nullptr : &found->second;
        }

        QuestFacts const* Quest(std::uint32_t quest) const
        {
            auto const found = _quests.find(quest);
            return found == _quests.end() ? nullptr : &found->second;
        }

        // Every (map, difficulty) a creature template is spawned in; nullptr when it has no spawn.
        std::vector<std::pair<std::uint32_t, std::uint8_t>> const* CreatureSpawns(std::uint32_t entry) const
        {
            auto const found = _creatureSpawns.find(entry);
            return found == _creatureSpawns.end() ? nullptr : &found->second;
        }

        template <class Visit>
        void ForEachAccessItem(Visit&& visit) const
        {
            for (auto const& [item, entry] : _access)
                visit(item);
        }

        AccessEntry Access(std::uint32_t item) const
        {
            if (!_built || item == 0)
                return {};
            auto const found = _access.find(item);
            return found == _access.end() ? AccessEntry{} : found->second;
        }

        std::size_t RaidEntries() const
        {
            return static_cast<std::size_t>(std::count_if(_entries.begin(), _entries.end(),
                [](auto const& pair) { return pair.second.verdict == Verdict::Raid; }));
        }

    private:
        using Bucket = std::pair<std::uint32_t, std::uint8_t>;  // map, difficulty
        static constexpr Bucket Unattributed{std::numeric_limits<std::uint32_t>::max(), 0};

        struct Builder
        {
            WorldRows const& rows;
            std::multimap<std::uint32_t, LootRow> creatureLoot, gameobjectLoot, referenceLoot;
            std::map<std::uint32_t, CreatureTemplateRow> creatures;
            std::map<std::uint32_t, std::uint32_t> chests;
            std::map<std::uint32_t, std::set<Bucket>> buckets;
            std::set<std::uint32_t> reachedCreatureLoot, reachedChestLoot;
            std::set<std::uint32_t> reachedCreatureTemplates; // selected exact template entries, not merely shared loot ids
            std::set<std::uint32_t> poisoned;  // vendor, quest, container

            explicit Builder(WorldRows const& source) : rows(source)
            {
                for (LootRow const& row : rows.creatureLoot)
                    creatureLoot.emplace(row.entry, row);
                for (LootRow const& row : rows.gameobjectLoot)
                    gameobjectLoot.emplace(row.entry, row);
                for (LootRow const& row : rows.referenceLoot)
                    referenceLoot.emplace(row.entry, row);
                for (CreatureTemplateRow const& row : rows.creatures)
                    creatures[row.entry] = row;
                for (ChestTemplateRow const& row : rows.chests)
                    chests[row.entry] = row.lootId;
            }

            void Expand(std::multimap<std::uint32_t, LootRow> const& store, std::uint32_t lootId,
                std::set<std::uint32_t>& seen, std::set<std::uint32_t>& out) const
            {
                auto range = store.equal_range(lootId);
                for (auto it = range.first; it != range.second; ++it)
                {
                    LootRow const& row = it->second;
                    if (row.reference)
                    {
                        if (seen.insert(row.reference).second)
                            Expand(referenceLoot, row.reference, seen, out);
                    }
                    else if (row.item)
                        out.insert(row.item);
                }
            }

            void Record(std::multimap<std::uint32_t, LootRow> const& store, std::uint32_t lootId, Bucket bucket)
            {
                std::set<std::uint32_t> seen, items;
                Expand(store, lootId, seen, items);
                for (std::uint32_t item : items)
                    buckets[item].insert(bucket);
            }

            // Core Creature::InitEntry: Heroic may select a normal loot
            // template on fallback. Preserve both the selected template and
            // the source difficulty; a normal table is not Heroic item proof.
            struct SelectedTemplate
            {
                std::uint32_t entry = 0;
                std::uint8_t sourceDifficulty = 0;
            };

            SelectedTemplate TemplateFor(std::uint32_t base, std::uint8_t difficulty) const
            {
                auto const baseIt = creatures.find(base);
                if (baseIt == creatures.end())
                    return {};
                for (std::uint8_t diff = difficulty; diff > 0;)
                {
                    std::uint32_t const clone = baseIt->second.difficultyEntry[diff - 1];
                    if (clone && creatures.count(clone))
                        return {clone, diff};
                    diff = diff >= 2 ? diff - 2 : diff - 1;
                }
                return {base, 0};
            }

            void Run(std::map<std::uint32_t, SourceEntry>& entries, std::map<std::uint32_t, AccessEntry>& access)
            {
                for (SpawnRow const& spawn : rows.creatureSpawns)
                    for (std::uint8_t diff = 0; diff < DifficultyCount; ++diff)
                    {
                        if (!(spawn.spawnMask & (1u << diff)))
                            continue;
                        SelectedTemplate const selected = TemplateFor(spawn.id, diff);
                        auto const it = creatures.find(selected.entry);
                        if (it == creatures.end() || !it->second.lootId)
                            continue;
                        reachedCreatureTemplates.insert(it->first);
                        reachedCreatureLoot.insert(it->second.lootId);
                        Bucket bucket{spawn.map, diff};
                        RaidBucket const raid = ClassifyBucket(spawn.map, diff);
                        // When a Heroic mode falls back to a lower-difficulty
                        // creature's normal loot table, do not lift Heroic BiS
                        // from that table. Its acquisition context alone does
                        // not prove a Heroic-specific item source.
                        if (raid.verdict == Verdict::Raid && raid.heroic &&
                            selected.sourceDifficulty != diff)
                            bucket = Unattributed;
                        Record(creatureLoot, it->second.lootId, bucket);
                    }
                for (SpawnRow const& spawn : rows.chestSpawns)
                {
                    auto const chest = chests.find(spawn.id);
                    if (chest == chests.end() || !chest->second)
                        continue;
                    for (std::uint8_t diff = 0; diff < DifficultyCount; ++diff)
                        if (spawn.spawnMask & (1u << diff))
                        {
                            reachedChestLoot.insert(chest->second);
                            Record(gameobjectLoot, chest->second, {spawn.map, diff});
                        }
                }
                // A second creature template can reuse a loot id reached by a
                // mapped raid spawn, while that second template has no static
                // spawn. Script-summoned uses of that template remain unknown:
                // loot-id reachability alone must not hide the alternate source.
                for (auto const& [templateEntry, info] : creatures)
                    if (info.lootId && !reachedCreatureTemplates.count(templateEntry) &&
                        reachedCreatureLoot.count(info.lootId))
                        Record(creatureLoot, info.lootId, Unattributed);
                // Loot no spawn reaches (script-summoned bosses and chests,
                // unused templates) proves nothing and spoils the entry.
                for (auto it = creatureLoot.begin(); it != creatureLoot.end(); it = creatureLoot.upper_bound(it->first))
                    if (!reachedCreatureLoot.count(it->first))
                        Record(creatureLoot, it->first, Unattributed);
                for (auto it = gameobjectLoot.begin(); it != gameobjectLoot.end(); it = gameobjectLoot.upper_bound(it->first))
                    if (!reachedChestLoot.count(it->first))
                        Record(gameobjectLoot, it->first, Unattributed);
                std::multimap<std::uint32_t, LootRow> itemLoot;
                for (LootRow const& row : rows.itemLoot)
                    itemLoot.emplace(row.entry, row);
                std::set<std::uint32_t> containerItems;
                for (auto it = itemLoot.begin(); it != itemLoot.end(); it = itemLoot.upper_bound(it->first))
                {
                    std::set<std::uint32_t> seen, items;
                    Expand(itemLoot, it->first, seen, items);
                    poisoned.insert(items.begin(), items.end());
                    containerItems.insert(items.begin(), items.end());
                }
                poisoned.insert(rows.vendorItems.begin(), rows.vendorItems.end());
                poisoned.insert(rows.questRewardItems.begin(), rows.questRewardItems.end());
                for (QuestRow const& quest : rows.quests)
                    for (std::uint32_t item : quest.rewards)
                        if (item)
                            poisoned.insert(item);

                for (auto const& [item, set] : buckets)
                {
                    if (set.size() != 1 || poisoned.count(item))
                        continue;
                    Bucket const bucket = *set.begin();
                    if (bucket == Unattributed)
                        continue;
                    RaidBucket const raid = ClassifyBucket(bucket.first, bucket.second);
                    if (raid.verdict == Verdict::Unknown)
                        continue;
                    entries[item] =
                        SourceEntry{raid.verdict, raid.point, raid.size, raid.heroic, bucket.first, bucket.second};
                }

                for (auto const& [item, set] : buckets)
                {
                    AccessEntry& entry = access[item];
                    entry.known = true;
                    for (Bucket const& bucket : set)
                    {
                        if (bucket == Unattributed)
                            entry.unattributed = true;
                        else
                            entry.spawned.push_back(bucket);
                    }
                }
                for (std::uint32_t item : containerItems)
                {
                    access[item].known = true;
                    access[item].container = true;
                }
                for (std::uint32_t item : rows.vendorItems)
                    if (item)
                    {
                        access[item].known = true;
                        access[item].vendor = true;
                    }
                std::set<std::uint32_t> structured;
                for (QuestRow const& quest : rows.quests)
                    for (std::uint32_t item : quest.rewards)
                        if (item)
                        {
                            AccessEntry& entry = access[item];
                            entry.known = true;
                            if (entry.quests.empty() || entry.quests.back() != quest.id)
                                entry.quests.push_back(quest.id);
                            structured.insert(item);
                        }
                for (std::uint32_t item : rows.questRewardItems)
                    if (item && !structured.count(item))
                    {
                        access[item].known = true;
                        access[item].questUnstructured = true;
                    }
            }
        };

        std::map<std::uint32_t, SourceEntry> _entries;
        std::map<std::uint32_t, AccessEntry> _access;
        std::map<std::uint32_t, QuestFacts> _quests;
        std::map<std::uint32_t, std::vector<std::pair<std::uint32_t, std::uint8_t>>> _creatureSpawns;
        bool _built = false;
    };

    // ---- Adapter: one commander capture -> slots + TierFacts -----------
    // CP40b (owner 20 Nov 2026 12:16 UK): 64-bit session and revision as in
    // Core's PB59 capture stamp, the sender GUID, and the same 19-slot item
    // GUID / entry / item-level sample. Nothing is narrowed or invented.
    using PlayerbotsAutoGearSelection::SlotIdentity;

    constexpr std::size_t EquipmentSlots = 19;
    constexpr std::size_t BodySlot = 3;
    constexpr std::size_t TabardSlot = 18;
    constexpr std::uint32_t HeirloomQuality = 7;

    struct CapturedSlot
    {
        std::uint64_t itemGuid = 0;
        std::uint32_t entry = 0;
        std::uint32_t itemLevel = 0;
        std::uint32_t quality = 0;
        std::uint32_t scalingDistribution = 0;
        std::uint32_t scalingValue = 0;
        bool operator==(CapturedSlot const&) const = default;
    };

    struct CapturedEquipment
    {
        std::uint64_t guid = 0;
        std::uint64_t session = 0;
        std::uint64_t revision = 0;
        std::array<CapturedSlot, EquipmentSlots> slots{};
    };

    enum class BindResult : std::uint8_t
    {
        Bound,
        IndexNotBuilt,
        MissingIdentity,     // guid, session or revision is zero
        IncompleteSlot       // an occupied slot the sample cannot count
    };

    // The counted 19-slot sample of one capture, as the route sees it.
    // Mirrors the TrainerAction sender sample: body and tabard are not
    // counted; an occupied slot without entry or item level, an heirloom or
    // a scaling item, or an empty slot with stray data, makes the whole
    // capture unusable.
    class CaptureSample
    {
    public:
        BindResult result = BindResult::MissingIdentity;
        std::uint64_t senderGuid = 0;
        std::uint64_t captureSession = 0;
        std::uint64_t equipmentRevision = 0;
        std::array<std::uint32_t, EquipmentSlots> levels{};
        std::array<SlotIdentity, EquipmentSlots> identities{};

        bool Usable() const { return result == BindResult::Bound; }

        // Points into this object: call it on the object that outlives the route call.
        SenderSnapshot Snapshot() const
        {
            if (!Usable())
                return {};
            return SenderSnapshot{senderGuid, captureSession, equipmentRevision,
                std::span<SlotIdentity const>(identities.data(), identities.size())};
        }

        bool SameCapture(CaptureSample const& other) const
        {
            return Usable() && other.Usable() && senderGuid == other.senderGuid &&
                captureSession == other.captureSession && equipmentRevision == other.equipmentRevision &&
                levels == other.levels && identities == other.identities;
        }
    };

    inline CaptureSample SampleCapture(CapturedEquipment const& capture)
    {
        CaptureSample sample;
        if (!capture.guid || !capture.session || !capture.revision)
            return sample;
        for (std::size_t i = 0; i < EquipmentSlots; ++i)
        {
            if (i == BodySlot || i == TabardSlot)
                continue;
            CapturedSlot const& slot = capture.slots[i];
            if (!slot.itemGuid)
            {
                if (!(slot == CapturedSlot{}))
                    return CaptureSample{BindResult::IncompleteSlot};
                continue;
            }
            if (!slot.entry || !slot.itemLevel || slot.quality == HeirloomQuality ||
                slot.scalingDistribution || slot.scalingValue)
                return CaptureSample{BindResult::IncompleteSlot};
            sample.levels[i] = slot.itemLevel;
            sample.identities[i] = SlotIdentity{slot.itemGuid, slot.entry, slot.itemLevel};
        }
        sample.senderGuid = capture.guid;
        sample.captureSession = capture.session;
        sample.equipmentRevision = capture.revision;
        sample.result = BindResult::Bound;
        return sample;
    }

    // Owns the source array; Facts() points into it.
    class BoundMasterSources
    {
    public:
        BindResult result = BindResult::IndexNotBuilt;
        CaptureSample sample{};
        std::array<MasterItemSource, EquipmentSlots> sources{};

        bool Bound() const { return result == BindResult::Bound; }
        std::array<std::uint32_t, EquipmentSlots> const& Slots() const { return sample.levels; }

        TierFacts Facts() const
        {
            TierFacts facts;
            if (!Bound())
                return facts;  // unverified: no tier lift
            facts.sourcesVerified = true;
            facts.sources = std::span<MasterItemSource const>(sources.data(), sources.size());
            facts.senderGuid = sample.senderGuid;
            facts.captureSession = sample.captureSession;
            facts.equipmentRevision = sample.equipmentRevision;
            return facts;
        }

        // Freshness: the facts are handed out only while a current capture
        // of the commander is the very capture they were bound to (same
        // sender, session, revision and 19-slot sample). Stale, mismatched
        // or unusable: unverified facts, so no tier lift.
        TierFacts FreshFacts(CapturedEquipment const& current) const
        {
            if (!Bound() || !sample.SameCapture(SampleCapture(current)))
                return TierFacts{};
            return Facts();
        }
    };

    inline BoundMasterSources BindMasterSources(SourceIndex const& index, CapturedEquipment const& capture)
    {
        BoundMasterSources bound;
        if (!index.Built())
            return bound;
        bound.sample = SampleCapture(capture);
        if (!bound.sample.Usable())
        {
            bound.result = bound.sample.result;
            bound.sample = CaptureSample{bound.result};
            return bound;
        }
        for (std::size_t i = 0; i < EquipmentSlots; ++i)
        {
            SlotIdentity const& identity = bound.sample.identities[i];
            if (!identity.itemGuid)
                continue;
            MasterItemSource& source = bound.sources[i];
            source.itemLevel = identity.itemLevel;
            source.itemGuid = identity.itemGuid;
            source.entry = identity.entry;
            // Entry-level "can drop only from X" evidence, never proof that
            // this item instance came from X.
            SourceEntry const found = index.Lookup(identity.entry);
            if (found.verdict == Verdict::Raid)
            {
                source.raidSourceKnown = true;
                source.point = found.point;
                source.size = found.size;
                source.heroic = found.heroic;
                source.heroicKnown = true;  // from the loot record, never the map
            }
        }
        bound.result = BindResult::Bound;
        return bound;
    }
}

// Core seam: PB59::EquipmentFacts lives in CS01's unmerged Core header. It
// is only included in an integrated build that carries that header; the
// stamp's session and revision are copied at full 64-bit width.
#if __has_include("EquipmentCaptureState.h")
#include "EquipmentCaptureState.h"
namespace PlayerbotsAutoGearMasterSource
{
    inline CapturedEquipment FromEquipmentFacts(PB59::EquipmentFacts const& facts)
    {
        CapturedEquipment capture;
        capture.guid = facts.guid;
        capture.session = facts.stamp.session;
        capture.revision = facts.stamp.revision;
        for (std::size_t i = 0; i < EquipmentSlots && i < facts.slots.size(); ++i)
        {
            auto const& slot = facts.slots[i];
            capture.slots[i] = CapturedSlot{slot.itemGuid, slot.entry, slot.itemLevel, slot.quality,
                slot.scalingDistribution, slot.scalingValue};
        }
        return capture;
    }
}
#endif

#endif
