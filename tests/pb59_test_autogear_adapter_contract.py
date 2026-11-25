#!/usr/bin/env python3
"""Source contract for the live AutoGear adapter (TrainerAction.cpp) and the random-bot factory hook.

The executor is tested with a fake inventory (pb59_autogear_capped_command.cpp). The live adapter
calls Core (CanUseItem, CanEquipNewItem, CanStoreItem, ...), which needs a running server, so these
checks pin what the adapter source may and may not do instead. They do not prove Core behaviour.
Each rule is also run against a deliberately broken copy (negative controls), so a checker that
stops matching fails this script.

CS01 coordination #52 Part 28, item 1: unique/equip-slot restrictions, no owned gear destruction,
Altbot protection, refuse before mutation. #52 7118592534 / 7118646677 (gates A03-A06): exact
all-or-nothing strips through the transaction runner, identity checks, no pointer kept across a
script hook, checked restores, success only when it is true, and no off-hand mailed by the factory.
"""
from pathlib import Path
import re
import sys


def between(source, start, end):
    if source.count(start) != 1:
        raise RuntimeError(f"expected exactly one: {start}")
    pos = source.index(start)
    stop = source.index(end, pos + len(start))
    return source[pos:stop]


def method(cls, signature):
    """Body of one method inside the adapter class, from its signature to the next blank-line method."""
    if cls.count(signature) != 1:
        raise RuntimeError(f"expected exactly one method: {signature}")
    pos = cls.index(signature)
    depth = 0
    started = False
    for i in range(cls.index("{", pos), len(cls)):
        if cls[i] == "{":
            depth += 1
            started = True
        elif cls[i] == "}":
            depth -= 1
            if started and depth == 0:
                return cls[pos:i + 1]
    raise RuntimeError(f"unterminated method: {signature}")


def section(text, start, end):
    """Like between(), but empty when the start is missing or repeated (an ordering rule reports that)."""
    if text.count(start) != 1 or end not in text[text.index(start) + len(start):]:
        return ""
    return between(text, start, end)


def ordered(text, *needles):
    """Every needle occurs, in this order."""
    pos = -1
    for needle in needles:
        found = text.find(needle, pos + 1)
        if found < 0:
            return False
        pos = found
    return True


def check(trainer, factory_cpp, factory_h):
    errors = []

    def rule(ok, text):
        if not ok:
            errors.append(text)

    cls = between(trainer, "class PlayerAutoGearInventory", "std::string RefusalText(")
    strip = method(cls, "PlayerbotsAutoGearExec::StripResult StripToBags(uint32 slotMask)")
    run_factory = method(cls, "bool RunFactory(uint32 quality, uint32 cap, bool incremental)")
    bis = method(cls, "bool ApplyBisOverlay(uint32 cap)")
    finishers = method(cls, "void ApplyFinishers(uint32 cap)")
    destroy = method(cls, "bool DestroyCreatedEquipped(uint8 slot, uint64 guid)")
    restore = method(cls, "bool RestoreToSlot(ObjectGuid guid, uint8 slot)")
    core = method(cls, "PlayerbotsAutoGearExec::StripResult RunCoreBatch(")
    at_destination = method(cls, "bool AllAtDestination(")

    # Nothing owned is destroyed: the only destroy call checks the exact created item's guid first.
    rule(cls.count("DestroyItem(") == 1 and "DestroyItem(" in destroy,
         "the adapter destroys items only in DestroyCreatedEquipped")
    rule(ordered(destroy, "item->GetGUID().GetRawValue() != guid", "return false;", "DestroyItem("),
         "DestroyCreatedEquipped checks the created guid before destroying")
    for banned in ("DestroyEquippedGear", "SwapItem(", "DestroyItemCount(", "DeleteFromDB", "RemoveItemFromUpdateQueue",
                   "HandleAutoStoreBagItemOpcode", "MoveItemFromInventory("):
        rule(banned not in cls, f"the adapter never calls {banned}")
    # Every move of an owned item goes through Core's executor: no public inventory helper anywhere in the
    # adapter (coordination #52 7130978738 Layer 2; Core PR74 contract).
    for helper in ("RemoveItem", "StoreItem", "EquipItem"):
        rule(re.search(r"(?<![A-Za-z_])" + helper + r"\(", cls) is None,
             f"the adapter never calls the public helper {helper}")
    rule("#if defined(COSMIC_PB59_INVENTORY_BATCH_API_VERSION) && COSMIC_PB59_INVENTORY_BATCH_API_VERSION == 1"
         in trainer, "the executor is used only with Core's batch API version 1")
    rule("CheckPB59InventoryBatchPlan(" not in trainer, "Core's read-only check is never used as permission")

    # Strips: hook-free exact planning, then one Core batch call; without the executor, a plain refusal.
    rule(ordered(strip, "if (_stripping)", "return StripResult::Refused;", "_stripping = true;"),
         "a strip refuses to start inside another strip (Grok F3)")
    rule(ordered(strip, "#if !PB59_AUTOGEAR_CORE_BATCH", "return StripResult::Refused;", "#else"),
         "without the executor the strip refuses: no fallback")
    rule(ordered(strip, "slots.push_back(EQUIPMENT_SLOT_OFFHAND);", "for (uint8 slot = EQUIPMENT_SLOT_START",
                 "CapturePB59InventorySlot(position)", "return StripResult::Refused;"),
         "the off-hand is planned first, from exact slot facts")
    rule(ordered(strip, "INVENTORY_SLOT_ITEM_START", "proto->BagFamily != 0", "return StripResult::NoSpace;",
                 "RunCoreBatch(plan, sources, destinations)"),
         "destinations are empty backpack or general-bag slots, then one Core batch")
    for hook in ("CanUnequipItem(", "CanStoreItem(", "CanEquipItem("):
        rule(hook not in strip, f"planning calls no script hook ({hook})")
    rule(ordered(core, "auto const before = Fingerprint();", "CapturePB59InventoryStamp()",
                 "return StripResult::Refused;", "GetPB59OwnerIncarnation()", "GetPB59SessionIncarnation()",
                 "BuildMoves(owner, plan, sources, destinations)", "ExecutePB59InventoryBatchPlan(",
                 "OutcomeFrom(result.outcome)", "observed.missing = result.recoveryCount;",
                 "observed.failureReported = result.failure != PB59::InventoryBatchFailure::None;",
                 "if (outcome == CoreBatchOutcome::RecoveryRequired)", "return StripResult::RecoveryRequired;",
                 "if (!SameContext(botGuid, session, ownerIncarnation, sessionIncarnation))",
                 "return StripResult::RecoveryRequired;", "auto const after = Fingerprint();",
                 "observed.unchangedSinceStart = after && *after == *before;",
                 "observed.everyMoveAtDestination = AllAtDestination(plan);", "ToStripResult(outcome, observed)"),
         "Core's result is read by name and believed only when Playerbots' own observation agrees")
    native_rr = section(core, "if (outcome == CoreBatchOutcome::RecoveryRequired)", "return StripResult::RecoveryRequired;")
    context_lost = section(core, "if (!SameContext(", "return StripResult::RecoveryRequired;")
    rule("_bot" not in native_rr and "Fingerprint" not in native_rr and "LOG_ERROR(" in native_rr,
         "after Core's recovery nothing is read from the bot (#52 7137340399)")
    rule("_bot" not in context_lost, "a lost context is reported without reading the bot")
    same_context = section(cls, "bool SameContext(", "\n        }\n")
    rule(ordered(same_context, "FindConnectedPlayer(guid)", "current == _bot &&", "GetPB59OwnerIncarnation()",
                 "GetPB59SessionIncarnation()") and
         "->" not in section(same_context, "{", "current == _bot"),
         "the bot is re-resolved by GUID and compared before it is read")
    rule(ordered(core, "recoveryGuids.data()", "if (outcome == CoreBatchOutcome::RecoveryRequired)", "LOG_ERROR(",
                 "Diagnostic(outcome, observed, guids)"),
         "a recovery is logged with Core's GUIDs")
    caller = section(trainer, "bool RunPB59AutoGearWorldPlan(", "\nbool AutoGearAction::Execute(")
    after_run = section(caller, "Execute(inventory, plan);", "if (!same(botId, bot,")
    rule(ordered(caller, "std::string const botText = botId.ToString();", "Execute(inventory, plan);",
                 "FindConnectedPlayer(id)", "current == expected &&", "if (!same(botId, bot,",
                 "!same(senderId, sender,", "ai->Whisper(") and
         not any(x in after_run for x in ("bot->", "sender->", "ai->")),
         "after the run both players are re-resolved before any read or reply")
    rule(all(x in at_destination for x in ("source->item", "destination->item->guid != move.itemGuid",
                                            "destination->bag != move.destinationBag")),
         "applied is checked against every planned destination")
    fingerprint = method(cls, "std::optional<InventoryFingerprint> Fingerprint() const")
    rule(all(x in fingerprint for x in ("INVENTORY_SLOT_ITEM_START", "INVENTORY_SLOT_BAG_START", "GetBagSize()",
                                         "PlayerLogout()", "return std::nullopt;")),
         "the fingerprint covers equipment, backpack, every bag, the session and logout, and fails closed")
    rule(ordered(restore, "#if !PB59_AUTOGEAR_CORE_BATCH", "return false;", "CapturePB59InventorySlot(from)",
                 "destination->item", "return false;",
                 "RunCoreBatch(plan, sources, destinations) == PlayerbotsAutoGearExec::StripResult::Done"),
         "a put-back goes through Core's executor into an empty slot, and counts only when done")

    # The factory: no second chance, the raid filter, and every owned-item move through the exact hooks.
    rule(re.search(r"/\*secondChance\*/\s*false", run_factory) is not None,
         "RunFactory disables the destructive second chance")
    rule("_sources" in run_factory, "RunFactory passes the raid source filter")
    rule(ordered(run_factory, "PlayerbotFactory::ExactMoves moves;", "return StripToBags(mask);",
                 "return RestoreToSlot(guid, slot);", "&moves", "return !moves.stopped;"),
         "the factory moves owned items only through the exact strip and reports a stop")

    # BiS rows: the cap, the raid gate and Core's use/equip checks come before any change.
    rule(ordered(bis, "AllowsRawItemLevel(", "_sources->Allows(", "CanUseItem(", "continue;"),
         "BiS rows pass the cap, the raid gate and CanUseItem before anything else")
    rule(ordered(bis, "CanEquipNewItem(slot, dest, entry, true)", "continue;", "DisplacesOffhand(_bot, proto)",
                 "StripToBags(mask)", "StripResult::RecoveryRequired", "return false;", "StripResult::Done",
                 "continue;", "CanEquipNewItem(slot, dest, entry, false)", "!InWorld()", "EquipNewItem(",
                 "!RestoreToSlot(previous, slot)", "!RestoreToSlot(previousOffhand, EQUIPMENT_SLOT_OFFHAND)",
                 "return false;"),
         "BiS moves the current item and a displaced off-hand in one exact strip, stops on an unproven strip, "
         "and puts both back or stops")
    rule(ordered(bis, "if (!_bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND))",
                 "_bot->AutoUnequipOffhandIfNeed();"),
         "BiS lets Core touch the off-hand only when it is empty (no store or mail)")
    rule("SetReputation" not in bis and "GiveReputation" not in bis, "BiS never grants reputation")
    rule("SetSourceFilter(_sources)" in finishers, "finishers (ammo, gems) use the raid source filter")
    rule(ordered(finishers, "SetKeepOwnedItems(true);", "InitAmmo();"), "finishers keep the bot's old ammo")

    # Legacy and queued commands must all preserve the owner's RNDBOT-only policy.
    for signature in ("bool BisGearAction::Execute(Event event)", "bool AutoGearAction::Execute(Event event)"):
        action = method(trainer, signature)
        guard = ("if (!bot->GetSession() || !PlayerbotsAutoGearSelection::IsConfirmedRandomBot("
                 "sRandomPlayerbotMgr.IsRandomBot(bot),"
                 "sPlayerbotAIConfig.IsInRandomAccountList(bot->GetSession()->GetAccountId())))")
        compact = re.sub(r"\s+", "", action)
        guard = re.sub(r"\s+", "", guard)
        guard_body = re.search(re.escape(guard) + r"\{[^{}]*\{\}[^{}]*returnfalse;\}", compact)
        rule(guard_body is not None and ordered(compact, guard, "returnfalse;","if(sPlayerbotAIConfig.AutoGearIPCapEnable)"),
             f"{signature}: unconditional random identity guard before dispatch")

    # The world plan: facts first, refusal decided by the plan before the executor touches anything.
    plan = between(trainer, "bool RunPB59AutoGearWorldPlan(", "\n}\n")
    rule(ordered(plan, "facts.sourcesAvailable =", "ResolveCommand(", "PlayerAutoGearInventory inventory(",
                 "Execute(inventory, plan)"),
         "the index state is a plan fact, decided before the executor runs")
    rule(plan.count("Execute(inventory, plan)") == 1, "the executor runs once")
    rule(ordered(plan, "facts.randomBot = sRandomPlayerbotMgr.IsRandomBot(bot);",
                 "facts.randomAccount = sPlayerbotAIConfig.IsInRandomAccountList(bot->GetSession()->GetAccountId());",
                 "ResolveCommand("),
         "queued command reads actual random-bot and session-account facts")
    for call in ("RunFactory(", "StripToBags(", "ApplyBisOverlay(", "DestroyEquippedGear", "EquipNewItem("):
        rule(call not in plan, f"the world plan never calls {call} directly")
    rule(ordered(plan, "if (!sPlayerbotAIConfig.AutoGearIPCapEnable", "return false;"),
         "the world plan returns before anything when the IP cap is off")
    rule(ordered(plan, "thread_local std::unordered_set<uint64> running;", "if (!running.insert(botGuid).second)",
                 "return false;", "running.erase(guid);", "ResolveCommand("),
         "one AutoGear command per bot at a time, released on every exit (Grok F3)")
    rule(ordered(plan, "Execute(inventory, plan)", "if (!outcome.Succeeded())", "return false;",
                 "\"autogear_ip_done_msg\""),
         "the world plan whispers success only after outcome.Succeeded()")
    refusals = between(trainer, "std::string RefusalText(", "\n}\n")
    for refusal in ("StripRefused", "StripRestored", "RecoveryRequired", "BagsFull"):
        rule(f"case Refusal::{refusal}:" in refusals, f"RefusalText explains {refusal}")
    rule("StripFailed" not in trainer, "the old StripFailed refusal is gone")

    # The factory under exact moves: the old item and a displaced off-hand leave through one exact strip, a
    # stop is final, and only then the ordinary (random-bot) path with its own off-hand guard.
    equip = between(factory_cpp, "void PlayerbotFactory::InitEquipment(bool incremental, bool second_chance)",
                    "bool PlayerbotFactory::IsDesiredReplacement(")
    rule(ordered(equip, "if (oldItem && exactMoves)", "exactMoves->strip(1u << slot)", "exactMoves->stopped = true;",
                 "else if (oldItem)", "HandleAutoStoreBagItemOpcode"),
         "the PvP trinket's old item leaves through the exact strip when exact moves are set")
    rule(ordered(equip, "if (exactMoves)", "DisplacesOffhand(bot, sObjectMgr->GetItemTemplate(bestItemForSlot))",
                 "exactMoves->strip(mask)", "StripResult::RecoveryRequired", "exactMoves->stopped = true;",
                 "return;", "StripResult::Done", "continue;", "!CanEquipUnseenItem(slot, dest, bestItemForSlot)",
                 "exactMoves->restore(oldItemGuid, slot)", "exactMoves->restore(offhandGuid, EQUIPMENT_SLOT_OFFHAND)",
                 "if (!bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND))",
                 "AutoUnequipOffhandIfNeed();", "continue;", "if (oldItem)", "HandleAutoStoreBagItemOpcode"),
         "with exact moves the factory strips exactly, puts back on failure, stops on recovery, and never "
         "reaches the public-helper path")
    rule("if (second_chance && !exactMoves)" in equip, "no destructive second chance under exact moves")
    rule(ordered(equip, "// fail to store in bag", "!oldItemGuid.IsEmpty() && !CanEquipUnseenItem(slot, dest, bestItemForSlot)",
                 "CanEquipItem(slot, back, stored, false) == EQUIP_ERR_OK", "EquipItem(back, stored, true)", "continue;",
                 "EquipNewItem(dest, bestItemForSlot, true)"),
         "the ordinary path re-checks the replacement after moving the old item and puts it back otherwise")
    rule("if (!keepOwnedItems)\n        DestroyObsoleteAmmo(bot, ammoProto);" in factory_cpp,
         "old ammo is destroyed only outside the capped command")
    mirror = between(factory_cpp, "bool PlayerbotFactory::DisplacesOffhand(", "\n}\n")
    rule(all(x in mirror for x in ("!bot->CanDualWield()", "ITEM_SUBCLASS_WEAPON_POLEARM", "ITEM_SUBCLASS_WEAPON_STAFF",
                                    "ITEM_SUBCLASS_WEAPON_FISHING_POLE",
                                    "bool const twoHandUsed = mainhand->InventoryType == INVTYPE_2HWEAPON && !bot->CanTitanGrip();",
                                    "return !(bot->CanTitanGrip() || (offProto->InventoryType != INVTYPE_2HWEAPON && !twoHandUsed));")),
         "the off-hand rule mirrors Player::AutoUnequipOffhandIfNeed")
    rule(ordered(factory_cpp, "factory.SetExactMoves(exactMoves);", "factory.SetKeepOwnedItems(exactMoves != nullptr);",
                 "factory.InitEquipment(incremental, secondChance);"),
         "AutoGear hands the exact moves to the factory")

    # Random-bot gate: only random bots, only with its option and IP, never for alt bots or humans,
    # and a commanded filter takes precedence.
    hook = between(factory_cpp, "void PlayerbotFactory::InitRandomBotSourceFilter()", "\n}\n")
    rule(ordered(hook, "!sPlayerbotAIConfig.RandomBotGearRaidSourceGate", "IndividualProgression.Enable",
                 "!sRandomPlayerbotMgr.IsRandomBot(bot)", "return;"),
         "the random-bot filter needs its option, IP and a random bot")
    rule("GetQuestStatus(" in hook and "TryReadPB59AutoGearProgression" not in hook,
         "the random-bot stage comes from the bot's own marker quests, not the human provider")
    rule(re.search(r"sourceFilter\s*\?\s*sourceFilter\s*:", factory_h) is not None,
         "a commanded source filter takes precedence over the random-bot filter")
    rule("InitRandomBotSourceFilter();" in between(factory_cpp, "PlayerbotFactory::PlayerbotFactory(", "\n}\n"),
         "the factory constructor sets up the random-bot filter")
    return errors


def mutants(trainer, factory_cpp, factory_h):
    """Each broken copy must fail at least one rule."""
    def t(old, new):
        assert old in trainer, old
        return trainer.replace(old, new, 1), factory_cpp, factory_h

    def f(old, new):
        assert old in factory_cpp, old
        return trainer, factory_cpp.replace(old, new, 1), factory_h

    guard = "if (!bot->GetSession() || !PlayerbotsAutoGearSelection::IsConfirmedRandomBot(\n            sRandomPlayerbotMgr.IsRandomBot(bot),\n            sPlayerbotAIConfig.IsInRandomAccountList(bot->GetSession()->GetAccountId())))"
    assert trainer.count(guard) == 2
    for index in range(2):
        pos = trainer.index(guard) if index == 0 else trainer.index(guard, trainer.index(guard) + len(guard))
        yield f"legacy alt guard {index} removed", trainer[:pos] + "if (false)" + trainer[pos+len(guard):], factory_cpp, factory_h
    yield "queued identity hardcoded", *t("facts.randomBot = sRandomPlayerbotMgr.IsRandomBot(bot);",
                                           "facts.randomBot = true;")
    yield "extra destroy", *t("PB59::InventoryBatchResult const result =",
                               "_bot->DestroyItem(0, 0, true);\n            PB59::InventoryBatchResult const result =")
    yield "second chance on", re.sub(r"/\*secondChance\*/\s*false", "/*secondChance*/ true", trainer, count=1), factory_cpp, factory_h
    yield "no CanUseItem", *t("_bot->CanUseItem(proto) != EQUIP_ERR_OK", "false")
    yield "no restore", *t("!RestoreToSlot(previous, slot)", "false")
    yield "no off-hand restore", *t("!RestoreToSlot(previousOffhand, EQUIPMENT_SLOT_OFFHAND)", "false")
    yield "public-helper fallback", *t("            (void)slotMask;\n            return StripResult::Refused;",
                                       "            _bot->RemoveItem(0, 0, true);\n            return StripResult::Refused;")
    yield "fallback claims done", *t("            (void)slotMask;\n            return StripResult::Refused;",
                                     "            (void)slotMask;\n            return StripResult::Done;")
    yield "applied without observation", *t("observed.everyMoveAtDestination = AllAtDestination(plan);",
                                            "observed.everyMoveAtDestination = true;")
    yield "recovery count ignored", *t("observed.missing = result.recoveryCount;", "observed.missing = 0;")
    yield "failure ignored", *t("observed.failureReported = result.failure != PB59::InventoryBatchFailure::None;",
                                "observed.failureReported = false;")
    yield "fingerprint ignored", *t("observed.unchangedSinceStart = after && *after == *before;",
                                    "observed.unchangedSinceStart = true;")
    yield "check bool as permission", *t("ExecutePB59InventoryBatchPlan(", "CheckPB59InventoryBatchPlan(")
    yield "hook in planning", *t("                if (!facts->item)\n                    continue;",
                                 "                _bot->CanUnequipItem(position, false);\n                if (!facts->item)\n                    continue;")
    yield "any bag family", *t("proto->BagFamily != 0", "false")
    yield "no recovery log", *t("LOG_ERROR(\"playerbots\", \"PB59 AutoGear bot={} {}{}\"", "LOG_DEBUG(\"playerbots\", \"PB59 AutoGear bot={} {}{}\"")
    yield "bot read after Core recovery", *t("                return StripResult::RecoveryRequired;\n            }\n            // Any other",
                                             "                (void)Fingerprint();\n                return StripResult::RecoveryRequired;\n            }\n            // Any other")
    yield "no context check", *t("if (!SameContext(botGuid, session, ownerIncarnation, sessionIncarnation))", "if (false)")
    yield "context check reads first", *t("return current == _bot &&", "return _bot->IsInWorld() && current == _bot &&")
    yield "reply without revalidation", *t("    if (!same(botId, bot, botSession, botIncarnation, botSessionIncarnation) ||",
                                           "    if (false && !same(botId, bot, botSession, botIncarnation, botSessionIncarnation) ||")
    yield "log reads the live bot", *t("             botText, senderText, request.rawStage",
                                       "             bot->GetGUID().ToString(), senderText, request.rawStage")
    yield "restore counts any result", *t("RunCoreBatch(plan, sources, destinations) == PlayerbotsAutoGearExec::StripResult::Done",
                                          "(RunCoreBatch(plan, sources, destinations), true)")
    yield "no nested-strip guard (F3)", *t("if (_stripping)\n                return StripResult::Refused;", "")
    yield "no command guard (F3)", *t("if (!running.insert(botGuid).second)", "if (false)")
    yield "BiS ignores unproven strip", *t(
        "if (strip == PlayerbotsAutoGearExec::StripResult::RecoveryRequired)\n                        return false;", "")
    yield "BiS lets Core move the off-hand", *t(
        "                if (!_bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND))\n                    _bot->AutoUnequipOffhandIfNeed();",
        "                _bot->AutoUnequipOffhandIfNeed();")
    yield "finishers destroy old ammo", *t("            factory.SetKeepOwnedItems(true);\n", "")
    yield "factory stop ignored", *t("            return !moves.stopped;", "            return true;")
    yield "success when ran", *t("if (!outcome.Succeeded())", "if (!outcome.ran)")
    yield "no recovery text", *t("case Refusal::RecoveryRequired:", "case Refusal::None:")
    yield "no source gate on BiS", *t("(_sources && !_sources->Allows(entry)) ||", "")
    yield "index after execute", *t("facts.sourcesAvailable =", "bool unusedSources =")
    yield "swap", *t("_bot->AutoUnequipOffhandIfNeed();", "_bot->SwapItem(0, 0);")
    yield "factory skips exact moves", *f("        if (exactMoves)\n        {\n            // The old item", "        if (false)\n        {\n            // The old item")
    yield "factory ignores a stop", *f(
        "                if (strip == PlayerbotsAutoGearExec::StripResult::RecoveryRequired)\n                {\n                    exactMoves->stopped = true;\n                    return;\n                }\n                if (strip != PlayerbotsAutoGearExec::StripResult::Done)\n                    continue;\n            }\n            // Equip only",
        "                if (strip != PlayerbotsAutoGearExec::StripResult::Done)\n                    continue;\n            }\n            // Equip only")
    yield "factory leaves the off-hand to Core", *f("DisplacesOffhand(bot, sObjectMgr->GetItemTemplate(bestItemForSlot))", "false")
    yield "factory no put-back", *f("exactMoves->restore(oldItemGuid, slot)", "true")
    yield "factory destroys old ammo", *f("if (!keepOwnedItems)\n        DestroyObsoleteAmmo", "if (true)\n        DestroyObsoleteAmmo")
    yield "second chance under exact moves", *f("if (second_chance && !exactMoves)", "if (second_chance)")
    yield "off-hand rule ignores Titan's Grip", *f(
        "bool const twoHandUsed = mainhand->InventoryType == INVTYPE_2HWEAPON && !bot->CanTitanGrip();",
        "bool const twoHandUsed = mainhand->InventoryType == INVTYPE_2HWEAPON;")
    yield "trinket via public helper", *f("            if (oldItem && exactMoves)", "            if (false)")
    yield "exact moves not handed over", *f("    factory.SetExactMoves(exactMoves);\n", "")
    yield "ordinary path mails off-hand", *f(
        "!oldItemGuid.IsEmpty() && !CanEquipUnseenItem(slot, dest, bestItemForSlot)", "false")
    hook = between(factory_cpp, "void PlayerbotFactory::InitRandomBotSourceFilter()", "\n}\n")
    yield "alt bots gated", trainer, factory_cpp.replace(
        hook, hook.replace("!sRandomPlayerbotMgr.IsRandomBot(bot)", "!bot", 1), 1), factory_h
    yield "human provider", trainer, factory_cpp.replace(
        hook, hook.replace("bot->GetQuestStatus(quest)", "sScriptMgr->TryReadPB59AutoGearProgression(bot)", 1), 1), factory_h
    yield "random filter wins", trainer, factory_cpp, re.sub(r"sourceFilter\s*\?\s*sourceFilter\s*:", "false ? sourceFilter :", factory_h, count=1)


def check_core_batch(header, trainer):
    """The Core batch preparation stays dormant and can never move items itself (#52 7132070192)."""
    errors = []
    for call in ("RemoveItem(", "StoreItem(", "EquipItem(", "SwapItem(", "DestroyItem(", "MoveItemFromInventory("):
        if call in header:
            errors.append(f"the Core batch header never calls {call}")
    if "#include \"AutoGearCoreBatchRequest.h\"" not in trainer or "ExecutePB59InventoryBatchPlan(" not in trainer:
        errors.append("the adapter moves owned items through the Core batch header and Core's executor")
    mapping = between(header, "constexpr PlayerbotsAutoGearExec::StripResult ToStripResult(", "\n}\n")
    for outcome in ("Refused", "Applied", "Restored", "RecoveryRequired"):
        if f"case CoreBatchOutcome::{outcome}:" not in mapping:
            errors.append(f"the mapping handles {outcome}")
    if "default:" in mapping or not mapping.rstrip().endswith("return StripResult::RecoveryRequired;"):
        errors.append("the mapping has no default case and fails closed to recovery")
    if "!observed.failureReported" not in mapping or "if (observed.missing)" not in mapping:
        errors.append("applied needs no failure reason, and missing GUIDs always mean recovery")
    native = between(header, "constexpr CoreBatchOutcome OutcomeFrom(NativeOutcome native)", "\n}\n")
    for outcome in ("Refused", "Applied", "Restored", "RecoveryRequired"):
        if f"case NativeOutcome::{outcome}:\n            return CoreBatchOutcome::{outcome};" not in native:
            errors.append(f"Core's {outcome} is read by name")
    return errors


def main():
    root = Path(__file__).resolve().parents[1]
    trainer = (root / "src/Ai/Base/Actions/TrainerAction.cpp").read_text(encoding="utf-8")
    factory_cpp = (root / "src/Bot/Factory/PlayerbotFactory.cpp").read_text(encoding="utf-8")
    factory_h = (root / "src/Bot/Factory/PlayerbotFactory.h").read_text(encoding="utf-8")

    failures = 0
    errors = check(trainer, factory_cpp, factory_h)
    for error in errors:
        print(f"FAIL {error}")
    failures += len(errors)

    caught = 0
    total = 0
    for name, t, fc, fh in mutants(trainer, factory_cpp, factory_h):
        total += 1
        if (t, fc, fh) == (trainer, factory_cpp, factory_h):
            print(f"FAIL negative control '{name}' did not change the source")
            failures += 1
            continue
        if check(t, fc, fh):
            caught += 1
        else:
            print(f"FAIL negative control '{name}' was not caught")
            failures += 1
    header = (root / "src/Bot/AutoGearCoreBatchRequest.h").read_text(encoding="utf-8")
    batch_errors = check_core_batch(header, trainer)
    for error in batch_errors:
        print(f"FAIL {error}")
    failures += len(batch_errors)
    errors = errors + batch_errors
    batch_mutants = [
        ("batch header moves items", header.replace("namespace PlayerbotsAutoGearCoreBatch\n{", "namespace PlayerbotsAutoGearCoreBatch\n{ // bot->StoreItem(dest, item, true);", 1), trainer),
        ("batch header not used", header, trainer.replace('#include "AutoGearCoreBatchRequest.h"', '', 1)),
        ("applied ignores the failure reason", header.replace("observed.everyMoveAtDestination && !observed.failureReported", "observed.everyMoveAtDestination", 1), trainer),
        ("native result read by position", header.replace("        case NativeOutcome::Applied:\n            return CoreBatchOutcome::Applied;", "        case NativeOutcome::Applied:\n            return CoreBatchOutcome::Refused;", 1), trainer),
        ("mapping default case", header.replace("        case CoreBatchOutcome::Restored:\n", "        default:\n", 1), trainer),
        ("mapping fails open", header.replace("    }\n    return StripResult::RecoveryRequired;\n}", "    }\n    return StripResult::Refused;\n}", 1), trainer),
    ]
    for name, h, t in batch_mutants:
        total += 1
        if (h, t) == (header, trainer):
            print(f"FAIL negative control '{name}' did not change the source")
            failures += 1
        elif check_core_batch(h, t):
            caught += 1
        else:
            print(f"FAIL negative control '{name}' was not caught")
            failures += 1
    print(f"contract rules pass on the real source: {not errors}; negative controls caught {caught}/{total}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
