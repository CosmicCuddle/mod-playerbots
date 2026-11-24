/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "TrainerAction.h"
#include "AiFactory.h"
#include "AutoGearCapCommand.h"
#include "AutoGearCommandRoute.h"
#include "AutoGearProgressionCap.h"
#include "AutoGearSourceAccess.h"
#include "AutoGearSourceIndexStore.h"
#include "AutoGearStorageTransaction.h"
#include "AutoGearSlotPolicy.h"
#include "BisListMgr.h"
#include "BudgetValues.h"
#include "Config.h"
#include "Event.h"
#include "PlayerbotFactory.h"
#include "PlayerbotTextMgr.h"
#include "Playerbots.h"
#include "ReputationMgr.h"
#include "ScriptMgr.h"
#include "WorldSession.h"
#include "Trainer.h"
#include <limits>
#include <optional>
#include <unordered_set>
#include <vector>

namespace
{
    bool ResolveCommandSenderAutoGearCap(Event const& /*event*/, uint32& hardCap)
    {
        hardCap = 0;
        // A foreign-map sender cannot be dereferenced from the bot's map thread.
        // Until IP publishes a cache-only, GUID-keyed snapshot, enabling this
        // option refuses commands before any gear mutation.
        return !sPlayerbotAIConfig.AutoGearIPCapEnable;
    }
}

bool TrainerAction::Execute(Event event)
{
    std::string const param = event.getParam();

    Creature* target = GetCreatureTarget();
    if (!target)
        return false;

    Trainer::Trainer* trainer = sObjectMgr->GetTrainer(target->GetEntry());
    if (!trainer)
        return false;

    // NOTE: Original version uses SpellIds here, but occasionally only inserts
    // a single spell ID value from parameters. If someone wants to impl multiple
    // spells as parameters, check SkipSpellsListAction::parseIds as an example.
    uint32 spellId = chat->parseSpell(param);

    bool learnSpells = param.find("learn") != std::string::npos || sRandomPlayerbotMgr.IsRandomBot(bot) ||
                       (sPlayerbotAIConfig.AllowLearnTrainerSpells &&
                        // TODO: Rewrite to only exclude start primary profession skills and make config dependent.
                        (trainer->GetTrainerType() != Trainer::Type::Tradeskill || !IsRealPlayer(botAI->GetMaster())));

    Iterate(target, learnSpells, spellId);

    return true;
}

bool TrainerAction::isUseful()
{
    Creature* target = GetCreatureTarget();
    if (!target)
        return false;

    if (!target->IsInWorld() || target->IsDuringRemoveFromWorld() || !target->IsAlive())
        return false;

    return target->IsTrainer();
}

bool TrainerAction::isPossible()
{
    Creature* target = GetCreatureTarget();
    if (!target)
        return false;

    Trainer::Trainer* trainer = sObjectMgr->GetTrainer(target->GetEntry());
    if (!trainer)
        return false;

    if (!trainer->IsTrainerValidForPlayer(bot))
        return false;

    for (Trainer::Spell const& spell : trainer->GetSpells())
    {
        Trainer::Spell const* trainerSpell = trainer->GetSpell(spell.SpellId);
        if (!trainerSpell)
            continue;

        if (!PlayerbotFactory::IsTrainerSpellAllowedForBot(bot, trainer, trainerSpell))
            continue;

        if (trainer->CanTeachSpell(bot, trainerSpell))
            return true;
    }

    return false;
}

Unit* TrainerAction::GetTarget()
{
    // There are just two scenarios: the bot has a master or it doesn't. If the
    // bot has a master, the master should target a unit; otherwise, the bot
    // should target the unit itself.
    if (Player* master = GetMaster())
        return master->GetSelectedUnit();

    return bot->GetSelectedUnit();
}

Creature* TrainerAction::GetCreatureTarget()
{
    Unit* target = GetTarget();
    return target ? target->ToCreature() : nullptr;
}

void TrainerAction::Iterate(Creature* creature, bool learnSpells, uint32 spellId)
{
    TellHeader(creature);

    Trainer::Trainer* trainer = sObjectMgr->GetTrainer(creature->GetEntry());
    if (!trainer)
        return;

    float reputationDiscount = bot->GetReputationPriceDiscount(creature);
    uint32 totalCost = 0;

    for (auto& spell : trainer->GetSpells())
    {
        // simplified version of Trainer::TeachSpell method

        Trainer::Spell const* trainerSpell = trainer->GetSpell(spell.SpellId);
        if (!trainerSpell)
            continue;

        if (!PlayerbotFactory::IsTrainerSpellAllowedForBot(bot, trainer, trainerSpell))
            continue;

        if (!trainer->CanTeachSpell(bot, trainerSpell))
            continue;

        if (spellId && trainerSpell->SpellId != spellId)
            continue;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(trainerSpell->SpellId);
        if (!spellInfo)
            continue;

        uint32 cost = static_cast<uint32>(floor(trainerSpell->MoneyCost * reputationDiscount));
        totalCost += cost;

        std::ostringstream out;
        out << chat->FormatSpell(spellInfo) << chat->formatMoney(cost);

        if (learnSpells)
            Learn(spellInfo, cost, out);

        botAI->TellMaster(out);
    }

    TellFooter(totalCost);
}

void TrainerAction::Learn(SpellInfo const* spellInfo, uint32 cost, std::ostringstream& out)
{
    if (!botAI->HasCheat(BotCheatMask::gold))
    {
        if (AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::spells) < cost)
        {
            out << " - too expensive";
            return;
        }

        bot->ModifyMoney(-static_cast<int32>(cost));
    }

    if (spellInfo->HasEffect(SPELL_EFFECT_LEARN_SPELL))
        bot->CastSpell(bot, spellInfo->Id, true);
    else
        bot->learnSpell(spellInfo->Id, false);

    out << " - learned";
}

void TrainerAction::TellHeader(Creature* creature)
{
    std::ostringstream out;
    out << "--- Can learn from " << creature->GetName() << " ---";
    botAI->TellMaster(out);
}

void TrainerAction::TellFooter(uint32 totalCost)
{
    if (totalCost)
    {
        std::ostringstream out;
        out << "Total cost: " << chat->formatMoney(totalCost);
        botAI->TellMaster(out);
    }
}

bool MaintenanceAction::Execute(Event /*event*/)
{
    if (!sPlayerbotAIConfig.MaintenanceCommand)
    {
        botAI->TellError("maintenance command is not allowed, please check the configuration.");
        return false;
    }

    botAI->TellMaster("I'm maintaining");
    PlayerbotFactory factory(bot, bot->GetLevel());

    if (!botAI->IsAltBot())
    {
        factory.InitAttunementQuests();
        factory.InitBags(false);
        factory.InitAmmo();
        factory.InitFood();
        factory.InitReagents();
        factory.InitConsumables();
        factory.InitPotions();
        factory.InitTalentsTree(true);
        factory.InitPet();
        factory.InitPetTalents();
        factory.InitSkills();
        factory.InitClassSpells();
        factory.InitAvailableSpells();
        factory.InitReputation();
        factory.InitSpecialSpells();
        factory.InitMounts();
        factory.InitGlyphs(false);
        factory.InitKeyring();
        if (bot->GetLevel() >= sPlayerbotAIConfig.MinEnchantingBotLevel)
            factory.ApplyEnchantAndGemsNew();
    }
    else
    {
        if (sPlayerbotAIConfig.AltMaintenanceAttunementQs)
            factory.InitAttunementQuests();

        if (sPlayerbotAIConfig.AltMaintenanceBags)
            factory.InitBags(false);

        if (sPlayerbotAIConfig.AltMaintenanceAmmo)
            factory.InitAmmo();

        if (sPlayerbotAIConfig.AltMaintenanceFood)
            factory.InitFood();

        if (sPlayerbotAIConfig.AltMaintenanceReagents)
            factory.InitReagents();

        if (sPlayerbotAIConfig.AltMaintenanceConsumables)
            factory.InitConsumables();

        if (sPlayerbotAIConfig.AltMaintenancePotions)
            factory.InitPotions();

        if (sPlayerbotAIConfig.AltMaintenanceTalentTree)
            factory.InitTalentsTree(true);

        if (sPlayerbotAIConfig.AltMaintenancePet)
            factory.InitPet();

        if (sPlayerbotAIConfig.AltMaintenancePetTalents)
            factory.InitPetTalents();

        if (sPlayerbotAIConfig.AltMaintenanceSkills)
            factory.InitSkills();

        if (sPlayerbotAIConfig.AltMaintenanceClassSpells)
            factory.InitClassSpells();

        if (sPlayerbotAIConfig.AltMaintenanceAvailableSpells)
            factory.InitAvailableSpells();

        if (sPlayerbotAIConfig.AltMaintenanceReputation)
            factory.InitReputation();

        if (sPlayerbotAIConfig.AltMaintenanceSpecialSpells)
            factory.InitSpecialSpells();

        if (sPlayerbotAIConfig.AltMaintenanceMounts)
            factory.InitMounts();

        if (sPlayerbotAIConfig.AltMaintenanceGlyphs)
            factory.InitGlyphs(false);

        if (sPlayerbotAIConfig.AltMaintenanceKeyring)
            factory.InitKeyring();

        if (sPlayerbotAIConfig.AltMaintenanceGemsEnchants &&
            bot->GetLevel() >= sPlayerbotAIConfig.MinEnchantingBotLevel)
            factory.ApplyEnchantAndGemsNew();
    }

    bot->DurabilityRepairAll(false, 1.0f, false);
    bot->SendTalentsInfoData(false);

    return true;
}

bool BisGearAction::RunAutogearFallback(uint16 effectiveIlvl, uint32 hardCap)
{
    if (!sPlayerbotAIConfig.AutoGearCommand)
    {
        botAI->TellError(PlayerbotTextMgr::instance().GetBotTextOrDefault(
            "bis_autogear_unavailable_error",
            "autogear command is not allowed, please check the configuration.", {}));
        return false;
    }

    botAI->TellMaster(PlayerbotTextMgr::instance().GetBotTextOrDefault(
        "bis_no_rows_fallback",
        "No BiS for your tier/spec/level, check cfg, running autogear instead", {}));

    // Wipe all equipped slots so autogear gears from scratch at the requested ilvl
    // (avoids old high-tier items surviving the incremental 1.2x threshold).
    PlayerbotFactory::DestroyEquippedGear(bot);

    PlayerbotFactory::AutoGear(bot, sPlayerbotAIConfig.AutoGearQualityLimit, effectiveIlvl, /*incremental*/ false,
                               sPlayerbotAIConfig.TwoRoundsGearInit, /*applyFinishers*/ true, hardCap);
    return true;
}

static bool PreserveAutoGearPlan(Player* bot, PlayerbotAI* botAI, Event const& event, bool bis);

bool BisGearAction::Execute(Event event)
{
    if (!sPlayerbotAIConfig.AutoGearBisCommand)
    {
        botAI->TellError(PlayerbotTextMgr::instance().GetBotTextOrDefault(
            "bis_command_unavailable_error",
            "bis command is not allowed, please check the configuration.", {}));
        return false;
    }

    if (!sPlayerbotAIConfig.AutoGearCommandAltBots &&
        !sPlayerbotAIConfig.IsInRandomAccountList(bot->GetSession()->GetAccountId()))
    {
        botAI->TellError(PlayerbotTextMgr::instance().GetBotTextOrDefault(
            "bis_altbot_refused_error", "You cannot use bis on alt bots.", {}));
        return false;
    }

    if (sPlayerbotAIConfig.AutoGearQualityLimit < 4)
    {
        botAI->TellError(PlayerbotTextMgr::instance().GetBotTextOrDefault(
            "bis_quality_floor_error", "AutoGearQualityLimit must be 4 for BiS.", {}));
        return false;
    }

    if (sRandomPlayerbotMgr.IsSpecPvp(bot->GetGUID().GetCounter(), bot->getClass()))
    {
        botAI->TellError(PlayerbotTextMgr::instance().GetBotTextOrDefault(
            "bis_pvp_refused_error", "bis is PvE only, bot is configured as PvP.", {}));
        return false;
    }

    if (sPlayerbotAIConfig.AutoGearIPCapEnable)
        return PreserveAutoGearPlan(bot, botAI, event, true);

    uint32 hardCap = 0;
    if (!ResolveCommandSenderAutoGearCap(event, hardCap))
    {
        botAI->TellError(PlayerbotTextMgr::instance().GetBotTextOrDefault(
            "bis_command_unavailable_error", "bis command is not allowed, please check the configuration.", {}));
        return false;
    }
    uint16 ilvl = hardCap
        ? static_cast<uint16>(std::min<uint32>(hardCap, std::numeric_limits<uint16>::max()))
        : static_cast<uint16>(sPlayerbotAIConfig.AutoGearScoreLimit);

    // Optional explicit ilvl override: `/p autogear bis 55`.
    // Garbage or out-of-range args are hard-rejected: no autogear fallback, no gear change.
    std::string const param = event.getParam();
    if (!param.empty())
    {
        unsigned long parsed = 0;
        size_t pos = 0;
        bool valid = false;
        try
        {
            parsed = std::stoul(param, &pos);
            valid = (parsed > 0 && pos == param.size() && parsed <= 0xFFFFu);
        }
        catch (...)
        {
            valid = false;
        }

        if (!valid)
        {
            std::map<std::string, std::string> phs;
            phs["%param"] = param;
            botAI->TellError(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                "bis_invalid_arg_error",
                "Invalid BiS ilvl argument '%param'. Use a positive integer.", phs));
            return false;
        }
        if (hardCap ? parsed > ilvl : parsed > static_cast<unsigned long>(sPlayerbotAIConfig.AutoGearScoreLimit))
        {
            std::map<std::string, std::string> phs;
            phs["%requested"] = std::to_string(parsed);
            phs["%limit"] = std::to_string(hardCap ? ilvl : sPlayerbotAIConfig.AutoGearScoreLimit);
            botAI->TellError(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                "bis_arg_above_limit_error",
                "BiS ilvl %requested exceeds AutoGearScoreLimit %limit, refusing", phs));
            return false;
        }
        ilvl = static_cast<uint16>(parsed);
    }
    uint32 const itemCap = hardCap ? ilvl : 0;
    uint8 cls = bot->getClass();
    uint8 tab = AiFactory::GetPlayerSpecTab(bot);
    uint8 faction = bot->GetTeamId() == TEAM_ALLIANCE ? 1 : 2;

    // Druid Bear (Feral Tank) shares tab 1 with Cat. Use sentinel tab 10 when tank strategy active.
    constexpr uint8 BIS_TAB_DRUID_BEAR = 10;
    constexpr uint16 BIS_ILVL_FALLBACK_WINDOW = 20;
    uint16 resolvedIlvl = 0;
    std::map<uint8, uint32> bisMap;
    if (cls == CLASS_DRUID && tab == DRUID_TAB_FERAL && PlayerbotAI::IsTank(bot))
        bisMap = sBisListMgr->GetBisForNearest(ilvl, BIS_ILVL_FALLBACK_WINDOW, cls, BIS_TAB_DRUID_BEAR, faction,
                                               &resolvedIlvl);
    if (bisMap.empty())
        bisMap = sBisListMgr->GetBisForNearest(ilvl, BIS_ILVL_FALLBACK_WINDOW, cls, tab, faction, &resolvedIlvl);

    // No rows within fallback window -> full autogear fallback at the effective ilvl.
    if (bisMap.empty())
    {
        std::map<std::string, std::string> phs;
        phs["%ilvl"] = std::to_string(ilvl);
        botAI->TellMaster(PlayerbotTextMgr::instance().GetBotTextOrDefault(
            "bis_no_rows_autogear_msg",
            "No BiS at ilvl %ilvl, using Autogear %ilvl instead", phs));
        return RunAutogearFallback(ilvl, itemCap);
    }

    // BiS row level is not an item-level guarantee. Filter raw item templates
    // before any equipped or bag item is destroyed.
    if (hardCap)
    {
        for (auto it = bisMap.begin(); it != bisMap.end();)
        {
            ItemTemplate const* item = sObjectMgr->GetItemTemplate(it->second);
            if (!item || !PlayerbotsAutoGearCap::AllowsRawItemLevel(item->ItemLevel, itemCap))
                it = bisMap.erase(it);
            else
                ++it;
        }
        if (bisMap.empty())
            return RunAutogearFallback(ilvl, itemCap);
    }

    if (resolvedIlvl != ilvl)
    {
        std::map<std::string, std::string> phs;
        phs["%requested"] = std::to_string(ilvl);
        phs["%resolved"] = std::to_string(resolvedIlvl);
        botAI->TellMaster(PlayerbotTextMgr::instance().GetBotTextOrDefault(
            "bis_closest_match_msg",
            "No BiS at ilvl %requested, using closest match at ilvl %resolved", phs));
    }

    botAI->TellMaster(PlayerbotTextMgr::instance().GetBotTextOrDefault(
        "bis_applying_msg", "Applying BiS gear", {}));

    // 1. Wipe everything currently equipped so autogear starts from a clean slate.
    //    Old items linger in inventory otherwise and autogear leaves slots empty on bag conflicts.
    PlayerbotFactory::DestroyEquippedGear(bot);

    // Wipe equippable items from bags too. Autogear can shove old equipped items into bags
    // (HandleAutoStoreBagItemOpcode), and a unique-equipped duplicate stuck in a bag blocks
    // CanEquipNewItem on subsequent BiS runs. Spare consumables/reagents.
    auto destroyIfEquippable = [&](uint8 bag, uint8 slot)
    {
        Item* item = bot->GetItemByPos(bag, slot);
        if (!item)
            return;
        ItemTemplate const* tmpl = item->GetTemplate();
        if (!tmpl)
            return;
        if (tmpl->Class == ITEM_CLASS_WEAPON || tmpl->Class == ITEM_CLASS_ARMOR)
            bot->DestroyItem(bag, slot, true);
    };
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        destroyIfEquippable(INVENTORY_SLOT_BAG_0, slot);
    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
    {
        if (Bag* container = bot->GetBagByPos(bag))
            for (uint32 slot = 0; slot < container->GetBagSize(); ++slot)
                destroyIfEquippable(bag, slot);
    }

    // 2. Run full autogear on the empty bot so every slot gets a best-available pick.
    //    Uncovered slots will keep the autogear pick; BiS overwrites the rest below.
    if (sPlayerbotAIConfig.AutoGearCommand)
        PlayerbotFactory::AutoGear(bot, sPlayerbotAIConfig.AutoGearQualityLimit, ilvl, /*incremental*/ false,
                                   sPlayerbotAIConfig.TwoRoundsGearInit, /*applyFinishers*/ false, itemCap);

    // Autogear may have placed the exact item BiS wants into trinket2/finger2 (or vice versa);
    // unique-equipped enforcement would then make BiS's equip silently drop one copy.
    std::set<uint32> bisEntries;
    for (auto const& kv : bisMap)
        bisEntries.insert(kv.second);
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            if (bisEntries.count(item->GetEntry()))
                bot->DestroyItem(INVENTORY_SLOT_BAG_0, slot, true);
    }

    // 3. Apply BiS: only touch slots where the bot can actually equip the BiS item.
    //    If item requires reputation, grant the required rank first. If CanUseItem still
    //    fails (class/race/skill/level), keep autogear's pick for that slot.
    for (auto const& kv : bisMap)
    {
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(kv.second);
        if (!proto)
            continue;

        // Grant required reputation rank if the item gates on it.
        if (proto->RequiredReputationFaction && proto->RequiredReputationRank > 0)
        {
            if (FactionEntry const* fac = sFactionStore.LookupEntry(proto->RequiredReputationFaction))
            {
                ReputationRank requiredRank = static_cast<ReputationRank>(proto->RequiredReputationRank);
                if (bot->GetReputationRank(proto->RequiredReputationFaction) < requiredRank)
                {
                    int32 standing = ReputationMgr::ReputationRankToStanding(
                                         static_cast<ReputationRank>(requiredRank - 1)) + 1;
                    bot->GetReputationMgr().SetReputation(fac, standing);
                }
            }
        }

        if (bot->CanUseItem(proto) != EQUIP_ERR_OK)
            continue;

        uint8 slot = kv.first;
        if (bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            bot->DestroyItem(INVENTORY_SLOT_BAG_0, slot, true);

        uint16 dest = 0;
        InventoryResult eqResult = bot->CanEquipNewItem(slot, dest, kv.second, false);

        // Paired slots (finger 10<->11, trinket 12<->13): destroy paired slot and retry once
        // when unique-equipped or autogear residue blocks the first attempt.
        if (eqResult != EQUIP_ERR_OK)
        {
            uint8 pairedSlot = 0xFF;
            if (slot == EQUIPMENT_SLOT_FINGER1)        pairedSlot = EQUIPMENT_SLOT_FINGER2;
            else if (slot == EQUIPMENT_SLOT_FINGER2)   pairedSlot = EQUIPMENT_SLOT_FINGER1;
            else if (slot == EQUIPMENT_SLOT_TRINKET1)  pairedSlot = EQUIPMENT_SLOT_TRINKET2;
            else if (slot == EQUIPMENT_SLOT_TRINKET2)  pairedSlot = EQUIPMENT_SLOT_TRINKET1;

            if (pairedSlot != 0xFF)
            {
                if (bot->GetItemByPos(INVENTORY_SLOT_BAG_0, pairedSlot))
                    bot->DestroyItem(INVENTORY_SLOT_BAG_0, pairedSlot, true);
                eqResult = bot->CanEquipNewItem(slot, dest, kv.second, false);
            }
        }

        if (eqResult == EQUIP_ERR_OK)
        {
            bot->EquipNewItem(dest, kv.second, true);
            bot->AutoUnequipOffhandIfNeed();
        }
    }

    PlayerbotFactory factory(bot, bot->GetLevel(), ITEM_QUALITY_EPIC, 0, itemCap);
    factory.InitAmmo();
    if (bot->GetLevel() >= sPlayerbotAIConfig.MinEnchantingBotLevel)
        factory.ApplyEnchantAndGemsNew();

    bot->DurabilityRepairAll(false, 1.0f, false);

    botAI->TellMaster(PlayerbotTextMgr::instance().GetBotTextOrDefault(
        "bis_applied_msg", "BiS applied", {}));
    return true;
}

bool RemoveGlyphAction::Execute(Event /*event*/)
{
    for (uint32 slotIndex = 0; slotIndex < MAX_GLYPH_SLOT_INDEX; ++slotIndex)
    {
        bot->SetGlyph(slotIndex, 0, true);
    }
    bot->SendTalentsInfoData(false);
    return true;
}

static uint32 GetWornItemLevelAverage(Player* player)
{
    uint32 sumLevel = 0;
    uint32 count = 0;
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        if (slot == EQUIPMENT_SLOT_BODY || slot == EQUIPMENT_SLOT_TABARD)
            continue;

        if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
        {
            sumLevel += item->GetTemplate()->ItemLevel;
            ++count;
        }
    }

    return count ? sumLevel / count : 0;
}

// Direct action invocations without the original queued sender ticket cannot
// enter the world-authority path. Never sample foreign-map live pointers here.
static bool PreserveAutoGearPlan(Player* /*bot*/, PlayerbotAI* /*botAI*/, Event const& /*event*/, bool /*bis*/)
{
    LOG_DEBUG("playerbots", "PB59 AutoGear: no verified world command ticket; preserving equipment");
    return false;
}

namespace
{
    constexpr PlayerbotsAutoGearSlots::Layout CommandSlotLayout = {
        EQUIPMENT_SLOT_HEAD, EQUIPMENT_SLOT_NECK, EQUIPMENT_SLOT_BODY, EQUIPMENT_SLOT_CHEST,
        EQUIPMENT_SLOT_LEGS, EQUIPMENT_SLOT_FEET, EQUIPMENT_SLOT_FINGER1, EQUIPMENT_SLOT_FINGER2,
        EQUIPMENT_SLOT_TRINKET1, EQUIPMENT_SLOT_TRINKET2, EQUIPMENT_SLOT_MAINHAND, EQUIPMENT_SLOT_OFFHAND,
        EQUIPMENT_SLOT_RANGED, EQUIPMENT_SLOT_TABARD};
    constexpr uint16 BIS_ILVL_FALLBACK_WINDOW = 20;
    constexpr uint8 BIS_TAB_DRUID_BEAR = 10;

    static_assert(EQUIPMENT_SLOT_END == PlayerbotsAutoGearExec::EquipmentSlots);
    static_assert(EQUIPMENT_SLOT_BODY == PlayerbotsAutoGearExec::SlotBody);
    static_assert(EQUIPMENT_SLOT_TABARD == PlayerbotsAutoGearExec::SlotTabard);

    // Live inventory for PlayerbotsAutoGearExec::Execute. Runs on the world thread only, after the
    // queued command re-resolved both players. Nothing here destroys an item the bot owned before.
    class PlayerAutoGearInventory
    {
    public:
        PlayerAutoGearInventory(Player* bot, uint32 quality, PlayerbotsAutoGearSourceAccess::Filter const* sources)
            : _bot(bot), _quality(quality), _sources(sources)
        {
        }

        PlayerbotsAutoGearExec::Snapshot Capture() const
        {
            PlayerbotsAutoGearExec::Snapshot snapshot;
            for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
            {
                Item* item = _bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
                if (!item)
                    continue;
                ItemTemplate const* proto = item->GetTemplate();
                snapshot.equipped[slot] = {item->GetGUID().GetRawValue(), item->GetEntry(), proto ? proto->ItemLevel : 0};
                snapshot.carried.push_back(item->GetGUID().GetRawValue());
            }
            for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            {
                if (Item* item = _bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                    snapshot.carried.push_back(item->GetGUID().GetRawValue());
                else
                    ++snapshot.freeBagSlots;
            }
            for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
            {
                Bag* bag = _bot->GetBagByPos(bagSlot);
                if (!bag)
                    continue;
                // Equipment only fits general-purpose containers.
                bool const general = bag->GetTemplate() && bag->GetTemplate()->BagFamily == 0;
                for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                {
                    if (Item* item = bag->GetItemByPos(slot))
                        snapshot.carried.push_back(item->GetGUID().GetRawValue());
                    else if (general)
                        ++snapshot.freeBagSlots;
                }
            }
            return snapshot;
        }

        // Every masked equipped item to its own reserved empty bag slot, or none (coordination #52
        // 7118646677, gates A03/A04). The plan comes from PlayerbotFactory::PrepareEquipmentStorage
        // (exact destinations and bag GUIDs, re-checked after every script hook, off-hand first); the
        // moves run through PlayerbotsAutoGearStorage::Execute, which checks exact slot facts before and
        // after every move and undoes earlier moves in reverse order if one fails. Equipment never
        // stacks, so Core's public RemoveItem/StoreItem/EquipItem to one pre-checked position is an exact
        // move; the returned item must be the same object.
        PlayerbotsAutoGearExec::StripResult StripToBags(uint32 slotMask)
        {
            using PlayerbotsAutoGearExec::StripResult;
#if !__has_include("PB59InventorySlotFacts.h")
            (void)slotMask;
            return StripResult::Refused;
#else
            // A script hook could start another strip of this bot while one is running.
            if (_stripping)
                return StripResult::Refused;
            _stripping = true;
            struct Reset
            {
                bool& flag;
                ~Reset() { flag = false; }
            } const reset{_stripping};

            // Planning and validation run CanUnequipItem, whose script hook can change anything. Nothing of
            // ours has moved before the transaction, so "nothing changed" (Refused, NoSpace) is reported only
            // if the whole inventory and the bot's context are proven unchanged; otherwise recovery is needed
            // (coordination #52 7130720231 point 2, Grok F1). A hook change that leaves the plan valid still
            // stops the strip: nothing is moved on top of changes we did not make.
            auto const before = Fingerprint();
            if (!before)
                return StripResult::Refused;
            auto unchanged = [&]()
            {
                auto const after = Fingerprint();
                return after && *after == *before;
            };
            std::vector<PlayerbotFactory::EquipmentStorageMove> plan;
            InventoryResult const prepared = PlayerbotFactory::PrepareEquipmentStorage(_bot, plan, slotMask);
            if (!unchanged())
                return StripResult::RecoveryRequired;
            if (prepared == EQUIP_ERR_INVENTORY_FULL)
                return StripResult::NoSpace;
            if (prepared != EQUIP_ERR_OK || plan.empty())
                return StripResult::Refused;

            std::vector<PlayerbotsAutoGearStorage::ExactMove> moves;
            for (auto const& move : plan)
                moves.push_back({{move.itemGuid.GetRawValue(), move.source, move.destination},
                                 move.count,
                                 move.destinationBagGuid.GetRawValue()});

            WorldSession* const session = _bot->GetSession();
            auto inspect = [&](uint16 position) -> std::optional<PlayerbotsAutoGearStorage::ExactSlot>
            {
                auto facts = _bot->CapturePB59InventorySlot(position);
                if (!facts)
                    return std::nullopt;
                return PlayerbotsAutoGearStorage::ExactSlot{facts->item ? facts->item->guid : 0, facts->bag,
                                                            facts->item ? facts->item->count : 0};
            };
            auto contextValid = [&]()
            { return _bot->IsInWorld() && _bot->GetSession() == session && session && !session->PlayerLogout(); };
            auto moveExact = [&](PlayerbotsAutoGearStorage::ExactMove const& move, bool restore)
            {
                uint16 const from = restore ? move.position.destination : move.position.source;
                uint16 const to = restore ? move.position.source : move.position.destination;
                // The exact item at `from`, re-read each time: CanEquipItem and CanUnequipItem run script hooks,
                // so no pointer is kept across them.
                auto current = [&]() -> Item*
                {
                    Item* item = _bot->GetItemByPos(from);
                    return item && item->GetGUID().GetRawValue() == move.position.itemGuid &&
                                   item->GetCount() == move.count
                               ? item
                               : nullptr;
                };
                Item* item = current();
                if (!item)
                    return false;
                uint16 equipDest = 0;
                ItemPosCountVec storeDest;
                if (restore)
                {
                    if (_bot->CanEquipItem(to & 255, equipDest, item, false) != EQUIP_ERR_OK || equipDest != to)
                        return false;
                }
                else if (_bot->CanUnequipItem(from, false) != EQUIP_ERR_OK)
                    return false;
                // After the hook and before any write: the bot is still here and the item is still in place
                // (Grok F2). The store check itself runs no script.
                item = current();
                if (!item || !contextValid())
                    return false;
                if (!restore &&
                    (_bot->CanStoreItem(to >> 8, to & 255, storeDest, item, false) != EQUIP_ERR_OK ||
                     storeDest.size() != 1 || storeDest.front().pos != to || storeDest.front().count != move.count))
                    return false;
                _bot->RemoveItem(from >> 8, from & 255, true);
                // Never link an item that is still linked somewhere (Grok F4): it must have left `from`.
                if (!Detached(item, from))
                    return false;
                return restore ? _bot->EquipItem(equipDest, item, true) == item
                               : _bot->StoreItem(storeDest, item, true) == item;
            };

            switch (
                PlayerbotsAutoGearStorage::Execute(moves, /*admittedFinalB*/ true, inspect, contextValid, moveExact))
            {
                case PlayerbotsAutoGearStorage::TransactionStatus::Committed:
                    return StripResult::Done;
                case PlayerbotsAutoGearStorage::TransactionStatus::RolledBack:
                    return StripResult::Restored;
                case PlayerbotsAutoGearStorage::TransactionStatus::RecoveryRequired:
                    return StripResult::RecoveryRequired;
                case PlayerbotsAutoGearStorage::TransactionStatus::Rejected:
                    break;
            }
            return unchanged() ? StripResult::Refused : StripResult::RecoveryRequired;
#endif
        }

        void RunFactory(uint32 quality, uint32 cap, bool incremental)
        {
            // No second chance: that pass destroys the equipped item before searching.
            PlayerbotFactory::AutoGear(_bot, quality, cap, incremental, /*secondChance*/ false,
                                       /*applyFinishers*/ false, cap, _sources);
        }

        // False when the run must stop for recovery: a replaced item could not be put back (it stays in the
        // bags), or a one-slot strip could not be proven to have changed nothing.
        bool ApplyBisOverlay(uint32 cap)
        {
            uint8 const cls = _bot->getClass();
            uint8 const tab = AiFactory::GetPlayerSpecTab(_bot);
            uint8 const faction = _bot->GetTeamId() == TEAM_ALLIANCE ? 1 : 2;
            uint16 const target = static_cast<uint16>(std::min<uint32>(cap, std::numeric_limits<uint16>::max()));
            std::map<uint8, uint32> bisMap;
            if (cls == CLASS_DRUID && tab == DRUID_TAB_FERAL && PlayerbotAI::IsTank(_bot))
                bisMap = sBisListMgr->GetBisForNearest(target, BIS_ILVL_FALLBACK_WINDOW, cls, BIS_TAB_DRUID_BEAR,
                                                       faction);
            if (bisMap.empty())
                bisMap = sBisListMgr->GetBisForNearest(target, BIS_ILVL_FALLBACK_WINDOW, cls, tab, faction);

            for (auto const& [slot, entry] : bisMap)
            {
                ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry);
                // Reputation is never granted here; an item the bot cannot use keeps the factory pick.
                if (slot >= EQUIPMENT_SLOT_END || !proto ||
                    !PlayerbotsAutoGearCap::AllowsRawItemLevel(proto->ItemLevel, cap) ||
                    (_sources && !_sources->Allows(entry)) || _bot->CanUseItem(proto) != EQUIP_ERR_OK)
                    continue;
                Item* current = _bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
                if (current && current->GetEntry() == entry)
                    continue;
                uint16 dest = 0;
                if (_bot->CanEquipNewItem(slot, dest, entry, true) != EQUIP_ERR_OK)
                    continue;
                ObjectGuid const previous = current ? current->GetGUID() : ObjectGuid::Empty;
                if (current)
                {
                    // Nothing changed (refused, no room, rolled back): keep the factory pick. Anything we
                    // cannot prove unchanged stops the run as a recovery.
                    PlayerbotsAutoGearExec::StripResult const strip = StripToBags(1u << slot);
                    if (strip == PlayerbotsAutoGearExec::StripResult::RecoveryRequired)
                        return false;
                    if (strip != PlayerbotsAutoGearExec::StripResult::Done)
                        continue;
                }
                if (_bot->CanEquipNewItem(slot, dest, entry, false) != EQUIP_ERR_OK || !InWorld() ||
                    _bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot) || !_bot->EquipNewItem(dest, entry, true))
                {
                    if (!previous.IsEmpty() && !RestoreToSlot(previous, slot))
                        return false;
                    continue;
                }
                // CanEquipNewItem already required room for a displaced off-hand, so this stores it.
                _bot->AutoUnequipOffhandIfNeed();
            }
            return true;
        }

        void ApplyFinishers(uint32 cap)
        {
            PlayerbotFactory factory(_bot, _bot->GetLevel(), _quality, 0, cap);
            factory.SetSourceFilter(_sources);
            factory.InitAmmo();
            if (_bot->GetLevel() >= sPlayerbotAIConfig.MinEnchantingBotLevel)
                factory.ApplyEnchantAndGemsNew();
            _bot->DurabilityRepairAll(false, 1.0f, false);
        }

        bool DestroyCreatedEquipped(uint8 slot, uint64 guid)
        {
            Item* item = _bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            if (!item || item->GetGUID().GetRawValue() != guid)
                return false;
            _bot->DestroyItem(INVENTORY_SLOT_BAG_0, slot, true);
            return !_bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        }

    private:
        // True only if exactly that item is back in the slot.
        bool RestoreToSlot(ObjectGuid guid, uint8 slot)
        {
            Item* item = _bot->GetItemByGuid(guid);
            uint16 dest = 0;
            if (!item || _bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                return false;
            uint16 const from = item->GetPos();
            if (_bot->CanEquipItem(slot, dest, item, false) != EQUIP_ERR_OK)
                return false;
            // CanEquipItem ran a script hook: the item must still be where it was, and the bot still here.
            item = _bot->GetItemByPos(from);
            if (!item || item->GetGUID() != guid || _bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot) || !InWorld())
                return false;
            _bot->RemoveItem(from >> 8, from & 255, true);
            if (!Detached(item, from) || _bot->EquipItem(dest, item, true) != item)
                return false;
            Item* restored = _bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            return restored && restored->GetGUID() == guid;
        }

        bool InWorld() const
        {
            WorldSession* session = _bot->GetSession();
            return _bot->IsInWorld() && session && !session->PlayerLogout();
        }

        // RemoveItem took the item out of `from` and out of every container.
        bool Detached(Item* item, uint16 from) const
        {
            return !_bot->GetItemByPos(from) && item->GetSlot() == NULL_SLOT &&
                   item->GetGuidValue(ITEM_FIELD_CONTAINED).IsEmpty();
        }

#if __has_include("PB59InventorySlotFacts.h")
        // Everything a script hook could change: every inventory position the bot has, with the exact copied
        // item instance, plus the session it is in. Unknown when any position is unreadable.
        struct InventoryFingerprint
        {
            WorldSession* session = nullptr;
            bool inWorld = false;
            bool alive = false;
            bool logout = false;
            std::vector<PB59::InventorySlotFacts> slots;
            std::vector<uint64> others;  // shirt, tabard and bag items, which slot facts do not cover
            bool operator==(InventoryFingerprint const&) const = default;
        };

        std::optional<InventoryFingerprint> Fingerprint() const
        {
            InventoryFingerprint print;
            print.session = _bot->GetSession();
            print.inWorld = _bot->IsInWorld();
            print.alive = _bot->IsAlive();
            print.logout = !print.session || print.session->PlayerLogout();
            auto add = [&](uint8 bag, uint8 slot)
            {
                auto facts = _bot->CapturePB59InventorySlot((uint16(bag) << 8) | slot);
                if (facts)
                    print.slots.push_back(std::move(*facts));
                return facts.has_value();
            };
            auto other = [&](uint8 slot)
            {
                Item* item = _bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
                print.others.push_back(item ? item->GetGUID().GetRawValue() : 0);
            };
            for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
            {
                if (slot == EQUIPMENT_SLOT_BODY || slot == EQUIPMENT_SLOT_TABARD)
                    other(slot);
                else if (!add(INVENTORY_SLOT_BAG_0, slot))
                    return std::nullopt;
            }
            for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
                if (!add(INVENTORY_SLOT_BAG_0, slot))
                    return std::nullopt;
            for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
            {
                other(bag);
                if (Bag* container = _bot->GetBagByPos(bag))
                    for (uint32 slot = 0; slot < container->GetBagSize(); ++slot)
                        if (!add(bag, static_cast<uint8>(slot)))
                            return std::nullopt;
            }
            return print;
        }
#endif

        Player* _bot;
        uint32 _quality;
        PlayerbotsAutoGearSourceAccess::Filter const* _sources;
        bool _stripping = false;
    };

    std::string RefusalText(PlayerbotsAutoGearExec::Refusal refusal)
    {
        using PlayerbotsAutoGearExec::Refusal;
        switch (refusal)
        {
            case Refusal::Rejected:
                return PlayerbotTextMgr::instance().GetBotTextOrDefault(
                    "autogear_ip_rejected_error", "That autogear command is not allowed for me.", {});
            case Refusal::UnknownPopulation:
                return PlayerbotTextMgr::instance().GetBotTextOrDefault(
                    "autogear_ip_population_error", "I can't confirm what kind of bot I am, so I won't change gear.",
                    {});
            case Refusal::ResetNotAllowed:
                return PlayerbotTextMgr::instance().GetBotTextOrDefault(
                    "autogear_ip_reset_error", "Reset is only for random bots. Use autogear to upgrade instead.", {});
            case Refusal::BagsFull:
                return PlayerbotTextMgr::instance().GetBotTextOrDefault(
                    "autogear_ip_bags_full_error", "My bags are too full to take off my gear first.", {});
            case Refusal::StripRefused:
                return PlayerbotTextMgr::instance().GetBotTextOrDefault(
                    "autogear_ip_strip_refused_error",
                    "Something about my gear or bags changed, so I didn't touch anything.", {});
            case Refusal::StripRestored:
                return PlayerbotTextMgr::instance().GetBotTextOrDefault(
                    "autogear_ip_strip_restored_error",
                    "I couldn't take off all my gear, so I put everything back as it was.", {});
            case Refusal::RecoveryRequired:
                return PlayerbotTextMgr::instance().GetBotTextOrDefault(
                    "autogear_ip_recovery_error",
                    "Something went wrong with my gear and not everything is where it was. Nothing was destroyed; "
                    "please check my bags and equipment.",
                    {});
            case Refusal::SourcesUnavailable:
                return PlayerbotTextMgr::instance().GetBotTextOrDefault(
                    "autogear_ip_sources_error",
                    "I can't check where gear comes from right now, so I'm keeping my current gear.", {});
            case Refusal::NotVerified:
            case Refusal::None:
                break;
        }
        return PlayerbotTextMgr::instance().GetBotTextOrDefault(
            "autogear_ip_unverified_error",
            "I can't confirm your progression right now, so I'm keeping my current gear.", {});
    }
}

bool RunPB59AutoGearWorldPlan(Player* bot, PlayerbotAI* ai, Player* sender, std::string_view argument, bool bis)
{
    if (!sPlayerbotAIConfig.AutoGearIPCapEnable || !bot || !ai || !sender ||
        !bot->IsInWorld() || !sender->IsInWorld() || !bot->GetSession() || !sender->GetSession())
        return false;
    // One AutoGear command per bot at a time (Grok F3): a script hook inside a running command could
    // otherwise start a nested one for the same bot. The command runs on the world thread.
    thread_local std::unordered_set<uint64> running;
    uint64 const botGuid = bot->GetGUID().GetRawValue();
    if (!running.insert(botGuid).second)
    {
        ai->Whisper(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                        "autogear_ip_busy_error", "I'm already changing my gear. Nothing else was changed.", {}),
                    sender->GetName());
        return false;
    }
    struct Release
    {
        uint64 guid;
        ~Release() { running.erase(guid); }
    } const release{botGuid};
    PlayerbotsAutoGearExec::CommandFacts facts;
    facts.bis = bis;
    facts.argument = argument;
    facts.commandEnabled = bis ? sPlayerbotAIConfig.AutoGearBisCommand : sPlayerbotAIConfig.AutoGearCommand;
    facts.bisPvpSpec = bis && sRandomPlayerbotMgr.IsSpecPvp(bot->GetGUID().GetCounter(), bot->getClass());
    facts.altCommandAllowed = sPlayerbotAIConfig.AutoGearCommandAltBots != 0;
    facts.qualityLimit = static_cast<uint32>(sPlayerbotAIConfig.AutoGearQualityLimit);
    facts.scoreLimit = static_cast<uint32>(sPlayerbotAIConfig.AutoGearScoreLimit);
    facts.useConfigCaps = true;
    facts.configCompletionCaps = sPlayerbotAIConfig.AutoGearIPCompletionCaps;
    facts.configEraCeilings = sPlayerbotAIConfig.AutoGearIPEraCeilings;
    facts.randomBot = sRandomPlayerbotMgr.IsRandomBot(bot);
    facts.randomAccount = sPlayerbotAIConfig.IsInRandomAccountList(bot->GetSession()->GetAccountId());
#if defined(COSMIC_PB59_AUTOGEAR_PROGRESSION_API)
    facts.progressionKnown = sScriptMgr->TryReadPB59AutoGearProgression(sender, facts.rawStage, facts.effectiveStage);
#endif
    facts.senderLevel = sender->GetLevel();
    // Lives until the route call returns: the facts point into it.
    PlayerbotsAutoGearMasterSource::BoundMasterSources tierSources;
#if __has_include("EquipmentCaptureState.h")
    if (auto equipment = sender->CapturePB59Equipment())
    {
        if (auto const* index = PlayerbotsAutoGearMasterSource::SourceIndexStore::Get())
        {
            tierSources = PlayerbotsAutoGearMasterSource::BindMasterSources(
                *index, PlayerbotsAutoGearMasterSource::FromEquipmentFacts(*equipment));
            facts.tierSnapshot = tierSources.sample.Snapshot();
            facts.tierFacts = tierSources.Facts();
        }
        bool complete = sender->IsPB59EquipmentCurrent(equipment->stamp);
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        {
            if (slot == EQUIPMENT_SLOT_BODY || slot == EQUIPMENT_SLOT_TABARD)
                continue;
            auto const& item = equipment->slots[slot];
            if (!item.itemGuid)
                continue;
            if (!item.entry || !item.itemLevel || item.quality == ITEM_QUALITY_HEIRLOOM ||
                item.scalingDistribution || item.scalingValue)
                complete = false;
            else
                facts.senderSlots[slot] = item.itemLevel;
        }
        facts.senderSlotsComplete = complete;
        facts.senderIsMaster = ai->GetMaster() == sender;
    }
#endif
    facts.sourcesAvailable = PlayerbotsAutoGearMasterSource::SourceIndexStore::Get() != nullptr;
    PlayerbotsAutoGearExec::ResolvedCommand const resolved = PlayerbotsAutoGearExec::ResolveCommand(facts,
        [](uint32 level, uint8 slot) { return PlayerbotsAutoGearSlots::SearchAtLevel(level, slot, CommandSlotLayout); });
    auto const& request = resolved.request;
    auto const& decision = resolved.decision;
    PlayerbotsAutoGearExec::Plan const& plan = resolved.plan;

    // C1-B: offer no item whose every source is a raid the commander cannot enter yet, using IP's own
    // entry gates at the commander's effective stage. A missing index was refused by the plan
    // (Refusal::SourcesUnavailable) before anything changed.
    PlayerbotsAutoGearSourceAccess::Filter sources;
    sources.index = PlayerbotsAutoGearMasterSource::SourceIndexStore::Get();
    sources.effectiveStage = request.effectiveStage;
    sources.gates.zulGurub = sConfigMgr->GetOption<uint8>("IndividualProgression.RequiredZulGurubProgression", 3, false);
    sources.gates.zulAman = sConfigMgr->GetOption<uint8>("IndividualProgression.RequiredZulAmanProgression", 12, false);
    if (plan.refusal == PlayerbotsAutoGearExec::Refusal::SourcesUnavailable)
        LOG_ERROR("playerbots", "PB59 AutoGear bot={} sender={} refused: the raid source index is not loaded",
            bot->GetGUID().ToString(), sender->GetGUID().ToString());

    PlayerAutoGearInventory inventory(bot, plan.quality, &sources);
    PlayerbotsAutoGearExec::Outcome const outcome = PlayerbotsAutoGearExec::Execute(inventory, plan);

    LOG_INFO("playerbots",
             "PB59 AutoGear bot={} sender={} raw={} effective={} authority={} cap={} form={} refusal={} ran={} "
             "stripped={} new={} revoked={} capHeld={} bisRestoreFailed={} ownedMissing={}",
             bot->GetGUID().ToString(), sender->GetGUID().ToString(), request.rawStage, request.effectiveStage,
             request.progressionContextVerified, decision.cap, static_cast<uint32>(decision.form),
             static_cast<uint32>(outcome.refusal), outcome.ran, outcome.stripped, outcome.newItems, outcome.revoked,
             outcome.capHeld, outcome.bisRestoreFailed, outcome.ownedMissing);
    if (outcome.ran && !outcome.capHeld)
        LOG_ERROR("playerbots", "PB59 AutoGear bot={} kept a new item above cap {}", bot->GetGUID().ToString(),
            plan.cap);
    if (outcome.refusal == PlayerbotsAutoGearExec::Refusal::RecoveryRequired)
        LOG_ERROR("playerbots",
                  "PB59 AutoGear bot={} needs recovery: stripRecovery={} bisRestoreFailed={} ownedMissing={}",
                  bot->GetGUID().ToString(), !outcome.ran, outcome.bisRestoreFailed, outcome.ownedMissing);

    // Success is whispered only when gearing ran, the cap held and nothing needs recovery
    // (coordination #52 7118646677, gate A06).
    if (!outcome.Succeeded())
    {
        if (outcome.ran && outcome.refusal == PlayerbotsAutoGearExec::Refusal::None)
            ai->Whisper(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                            "autogear_ip_cap_error",
                            "I couldn't keep my new gear under your progression limit. Please check my gear.", {}),
                        sender->GetName());
        else
            ai->Whisper(RefusalText(outcome.refusal), sender->GetName());
        return false;
    }
    std::map<std::string, std::string> phs;
    phs["%ilvl"] = std::to_string(plan.cap);
    phs["%count"] = std::to_string(outcome.newItems - outcome.revoked);
    ai->Whisper(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                    "autogear_ip_done_msg", "Geared up to item level %ilvl from your progression (%count new items).",
                    phs),
        sender->GetName());
    return true;
}

bool AutoGearAction::Execute(Event event)
{
    if (!sPlayerbotAIConfig.AutoGearCommand)
    {
        botAI->TellError(PlayerbotTextMgr::instance().GetBotTextOrDefault(
            "autogear_command_disabled_error", "autogear command is not allowed, please check the configuration.", {}));
        return false;
    }

    if (!sPlayerbotAIConfig.AutoGearCommandAltBots &&
        !sPlayerbotAIConfig.IsInRandomAccountList(bot->GetSession()->GetAccountId()))
    {
        botAI->TellError(PlayerbotTextMgr::instance().GetBotTextOrDefault("autogear_altbot_refused_error",
                                                                          "You cannot use autogear on alt bots.", {}));
        return false;
    }

    if (sPlayerbotAIConfig.AutoGearIPCapEnable)
        return PreserveAutoGearPlan(bot, botAI, event, false);

    uint32 const qualityCap = static_cast<uint32>(sPlayerbotAIConfig.AutoGearQualityLimit);
    uint32 hardCap = 0;
    if (!ResolveCommandSenderAutoGearCap(event, hardCap))
    {
        botAI->TellError(PlayerbotTextMgr::instance().GetBotTextOrDefault(
            "autogear_command_disabled_error", "autogear command is not allowed, please check the configuration.", {}));
        return false;
    }
    uint32 const ilvlCap = hardCap ? hardCap : static_cast<uint32>(sPlayerbotAIConfig.AutoGearScoreLimit);

    uint32 quality = qualityCap;
    uint32 ilvl = ilvlCap;

    // get optional chat quality
    std::string param = event.getParam();
    std::transform(param.begin(), param.end(), param.begin(), [](unsigned char c) { return std::tolower(c); });
    size_t const first = param.find_first_not_of(" \t");
    if (first == std::string::npos)
        param.clear();
    else
        param = param.substr(first, param.find_last_not_of(" \t") - first + 1);

    // 'reset' erases all equipped gear and regears from scratch instead of upgrading incrementally.
    // It can be combined with a quality/ilvl argument: 'autogear reset green', 'autogear reset 200'.
    bool reset = false;
    if (param == "reset" || param.rfind("reset ", 0) == 0)
    {
        reset = true;
        param = param.size() > 5 ? param.substr(6) : "";
        size_t const start = param.find_first_not_of(" \t");
        param = start == std::string::npos ? "" : param.substr(start);
    }

    if (!param.empty())
    {
        if (param == "match")
        {
            Player* master = GetMaster();
            if (!master)
            {
                botAI->TellError(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                    "autogear_no_master_error", "I have no master to match gear with.", {}));
                return false;
            }

            uint32 const requestedIlvl = GetWornItemLevelAverage(master);
            if (requestedIlvl == 0)
            {
                botAI->TellError(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                    "autogear_master_no_gear_error", "You have no gear equipped for me to match.", {}));
                return false;
            }

            ilvl = (ilvlCap == 0) ? requestedIlvl : std::min(requestedIlvl, ilvlCap);
            if (ilvlCap != 0 && requestedIlvl > ilvlCap)
            {
                std::map<std::string, std::string> phs;
                phs["%limit"] = std::to_string(ilvlCap);
                phs["%requested"] = std::to_string(requestedIlvl);
                botAI->TellMaster(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                    "autogear_ilvl_capped_msg",
                    "Config caps item level at %limit, gearing to that instead of %requested.", phs));
            }
        }
        else if (ChatHelper::parseItemQuality(param) != MAX_ITEM_QUALITY)
        {

            uint32 const requestedQuality = std::max<uint32>(ChatHelper::parseItemQuality(param), ITEM_QUALITY_NORMAL);

            quality = std::min(requestedQuality, qualityCap);
            if (requestedQuality > qualityCap)
            {
                std::map<std::string, std::string> phs;
                phs["%quality"] = ChatHelper::FormatItemQuality(qualityCap);
                botAI->TellMaster(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                    "autogear_quality_capped_msg", "Config limits me to %quality quality, gearing to that.", phs));
            }
        }
        else
        {
            // Handle negative numbers, garbage, and numbers that are too large to fit in a uint32.
            constexpr size_t MAX_ILVL_DIGITS = 5;
            bool const digitsOnly = !param.empty() && param.size() <= MAX_ILVL_DIGITS &&
                                    param.find_first_not_of("0123456789") == std::string::npos;
            uint32 const requestedIlvl = digitsOnly ? static_cast<uint32>(std::stoul(param)) : 0;

            if (!requestedIlvl)
            {
                std::map<std::string, std::string> phs;
                phs["%param"] = param;
                botAI->TellError(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                    "autogear_invalid_arg_error",
                    "Unknown autogear option '%param'. Use bis, reset, match, a quality "
                    "(white, green, blue, purple, orange) or a positive item level number.",
                    phs));
                return false;
            }

            if (requestedIlvl <= ITEM_QUALITY_LEGENDARY)
            {
                std::map<std::string, std::string> phs;
                phs["%param"] = param;
                botAI->TellError(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                    "autogear_quality_as_number_error",
                    "'%param' looks like a quality. Numbers are item levels (e.g. 200); use a colour word "
                    "for quality: white, green, blue, purple, orange.",
                    phs));
                return false;
            }

            ilvl = (ilvlCap == 0) ? requestedIlvl : std::min(requestedIlvl, ilvlCap);
            if (ilvlCap != 0 && requestedIlvl > ilvlCap)
            {
                std::map<std::string, std::string> phs;
                phs["%limit"] = std::to_string(ilvlCap);
                phs["%requested"] = std::to_string(requestedIlvl);
                botAI->TellMaster(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                    "autogear_ilvl_capped_msg",
                    "Config caps item level at %limit, gearing to that instead of %requested.", phs));
            }
        }
    }

    std::map<std::string, std::string> announcePhs;
    announcePhs["%quality"] = ChatHelper::FormatItemQuality(quality);
    announcePhs["%ilvl"] = std::to_string(ilvl);
    if (reset)
        botAI->TellMaster(
            ilvl != 0
                ? PlayerbotTextMgr::instance().GetBotTextOrDefault(
                      "autogear_reset_start_msg",
                      "I'm erasing my gear and regearing from scratch (%quality, up to item level %ilvl).", announcePhs)
                : PlayerbotTextMgr::instance().GetBotTextOrDefault(
                      "autogear_reset_start_any_ilvl_msg", "I'm erasing my gear and regearing from scratch (%quality).",
                      announcePhs));
    else
        botAI->TellMaster(
            ilvl != 0 ? PlayerbotTextMgr::instance().GetBotTextOrDefault(
                            "autogear_start_msg", "I'm auto gearing (%quality, up to item level %ilvl).", announcePhs)
                      : PlayerbotTextMgr::instance().GetBotTextOrDefault("autogear_start_any_ilvl_msg",
                                                                         "I'm auto gearing (%quality).", announcePhs));

    if (reset)
    {
        // Wipe everything equipped first so old high-tier items can't survive; then gear every
        // slot from scratch instead of only upgrading past the incremental threshold.
        PlayerbotFactory::DestroyEquippedGear(bot);
        PlayerbotFactory::AutoGear(bot, quality, ilvl, /*incremental*/ false,
                                   sPlayerbotAIConfig.TwoRoundsGearInit, /*applyFinishers*/ true,
                                   hardCap ? ilvl : 0);
    }
    else
        PlayerbotFactory::AutoGear(bot, quality, ilvl, /*incremental*/ true,
                                   /*twoRounds*/ false, /*applyFinishers*/ true, hardCap ? ilvl : 0);

    return true;
}
