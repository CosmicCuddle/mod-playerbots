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
    run_factory = method(cls, "void RunFactory(uint32 quality, uint32 cap, bool incremental)")
    bis = method(cls, "bool ApplyBisOverlay(uint32 cap)")
    finishers = method(cls, "void ApplyFinishers(uint32 cap)")
    destroy = method(cls, "bool DestroyCreatedEquipped(uint8 slot, uint64 guid)")
    restore = method(cls, "bool RestoreToSlot(ObjectGuid guid, uint8 slot)")

    # Nothing owned is destroyed: the only destroy call checks the exact created item's guid first.
    rule(cls.count("DestroyItem(") == 1 and "DestroyItem(" in destroy,
         "the adapter destroys items only in DestroyCreatedEquipped")
    rule(ordered(destroy, "item->GetGUID().GetRawValue() != guid", "return false;", "DestroyItem("),
         "DestroyCreatedEquipped checks the created guid before destroying")
    for banned in ("DestroyEquippedGear", "SwapItem(", "DestroyItemCount(", "DeleteFromDB", "RemoveItemFromUpdateQueue"):
        rule(banned not in cls, f"the adapter never calls {banned}")

    # Strips: one exact plan (own empty bag slot per item, off-hand first) run all-or-nothing by the
    # transaction runner, which checks slot facts around every move and undoes earlier moves.
    rule(ordered(strip, "PrepareEquipmentStorage(_bot, plan, slotMask)", "EQUIP_ERR_INVENTORY_FULL",
                 "return StripResult::NoSpace;", "PlayerbotsAutoGearStorage::Execute("),
         "StripToBags plans exact destinations before the transaction runs")
    rule(ordered(strip, "CapturePB59InventorySlot(position)", "return std::nullopt;"),
         "StripToBags inspects exact slot facts and reports unknown slots as unknown")
    rule(strip.count("item->GetGUID().GetRawValue() != move.position.itemGuid") >= 2,
         "StripToBags checks the item identity before and after the unequip hook")
    rule(ordered(strip, "CanUnequipItem(from, false)", "return false;", "item = _bot->GetItemByPos(from);",
                 "CanStoreItem(to >> 8, to & 255, dest, item, false)", "dest.front().pos != to",
                 "RemoveItem(from >> 8, from & 255, true)", "StoreItem(dest, item, true) == item"),
         "a forward move re-reads the item after the hook and stores it at exactly the planned slot")
    rule(ordered(strip, "if (restore)", "CanEquipItem(to & 255, dest, item, false)", "dest != to", "return false;",
                 "RemoveItem(", "EquipItem(dest, item, true) == item"),
         "a restore move checks the exact original slot and the returned item")
    rule(ordered(strip, "TransactionStatus::Committed:", "return StripResult::Done;",
                 "TransactionStatus::RolledBack:", "return StripResult::Restored;",
                 "TransactionStatus::RecoveryRequired:", "return StripResult::RecoveryRequired;"),
         "only a committed transaction is a finished strip")
    rule(len(re.findall(r"(?<!Can)StoreItem\(", strip)) == 1 and "SwapItem(" not in strip,
         "StripToBags has no other store or swap path")

    # The factory runs without the second-chance pass (which destroys the equipped item first).
    rule(re.search(r"/\*secondChance\*/\s*false", run_factory) is not None,
         "RunFactory disables the destructive second chance")
    rule("_sources" in run_factory, "RunFactory passes the raid source filter")

    # BiS rows: the cap, the raid gate and Core's use/equip checks (unique, slot, class) come before
    # any change; a failed equip restores the previous item.
    rule(ordered(bis, "AllowsRawItemLevel(", "_sources->Allows(", "CanUseItem(", "continue;"),
         "BiS rows pass the cap, the raid gate and CanUseItem before anything else")
    rule(ordered(bis, "CanEquipNewItem(slot, dest, entry, true)", "continue;", "StripToBags(1u << slot)"),
         "BiS checks CanEquipNewItem before moving the current item")
    rule(ordered(bis, "StripToBags(1u << slot) != PlayerbotsAutoGearExec::StripResult::Done", "continue;",
                 "CanEquipNewItem(slot, dest, entry, false)", "EquipNewItem(", "!RestoreToSlot(previous, slot)",
                 "return false;"),
         "BiS moves only after an exact strip, re-checks, and stops when the restore fails")
    rule("SetReputation" not in bis and "GiveReputation" not in bis, "BiS never grants reputation")
    rule(ordered(restore, "CanEquipItem(", "return false;", "RemoveItem(", "EquipItem(dest, item, true) != item",
                 "return false;", "restored->GetGUID() == guid"),
         "RestoreToSlot checks CanEquipItem first and reports whether exactly that item is back")
    rule("SetSourceFilter(_sources)" in finishers, "finishers (ammo, gems) use the raid source filter")

    # The world plan: facts first, refusal decided by the plan before the executor touches anything.
    plan = between(trainer, "bool RunPB59AutoGearWorldPlan(", "\n}\n")
    rule(ordered(plan, "facts.sourcesAvailable =", "ResolveCommand(", "PlayerAutoGearInventory inventory(",
                 "Execute(inventory, plan)"),
         "the index state is a plan fact, decided before the executor runs")
    rule(plan.count("Execute(inventory, plan)") == 1, "the executor runs once")
    for call in ("RunFactory(", "StripToBags(", "ApplyBisOverlay(", "DestroyEquippedGear", "EquipNewItem("):
        rule(call not in plan, f"the world plan never calls {call} directly")
    rule(ordered(plan, "if (!sPlayerbotAIConfig.AutoGearIPCapEnable", "return false;"),
         "the world plan returns before anything when the IP cap is off")
    rule(ordered(plan, "Execute(inventory, plan)", "if (!outcome.Succeeded())", "return false;",
                 "\"autogear_ip_done_msg\""),
         "the world plan whispers success only after outcome.Succeeded()")
    refusals = between(trainer, "std::string RefusalText(", "\n}\n")
    for refusal in ("StripRefused", "StripRestored", "RecoveryRequired", "BagsFull"):
        rule(f"case Refusal::{refusal}:" in refusals, f"RefusalText explains {refusal}")
    rule("StripFailed" not in trainer, "the old StripFailed refusal is gone")

    # The factory re-checks a replacement after the old item took a bag slot, so a displaced off-hand
    # is never mailed; otherwise the old item goes back.
    equip = between(factory_cpp, "void PlayerbotFactory::InitEquipment(bool incremental, bool second_chance)",
                    "bool PlayerbotFactory::IsDesiredReplacement(")
    rule(ordered(equip, "// fail to store in bag", "!oldItemGuid.IsEmpty() && !CanEquipUnseenItem(slot, dest, bestItemForSlot)",
                 "CanEquipItem(slot, back, stored, false) == EQUIP_ERR_OK", "EquipItem(back, stored, true)", "continue;",
                 "EquipNewItem(dest, bestItemForSlot, true)"),
         "the factory re-checks the replacement after moving the old item and puts it back otherwise")

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
    yield "extra destroy", trainer.replace("_bot->RemoveItem(from >> 8, from & 255, true);",
                                           "_bot->DestroyItem(from >> 8, from & 255, true);", 1), factory_cpp, factory_h
    yield "second chance on", re.sub(r"/\*secondChance\*/\s*false", "/*secondChance*/ true", trainer, count=1), factory_cpp, factory_h
    yield "no CanUseItem", trainer.replace("_bot->CanUseItem(proto) != EQUIP_ERR_OK", "false", 1), factory_cpp, factory_h
    yield "no restore", trainer.replace("!RestoreToSlot(previous, slot)", "false", 1), factory_cpp, factory_h
    yield "restore unchecked", trainer.replace("if (_bot->EquipItem(dest, item, true) != item)", "if (false)", 1), factory_cpp, factory_h
    yield "rollback as done", trainer.replace("return StripResult::Restored;", "return StripResult::Done;", 1), factory_cpp, factory_h
    yield "store unchecked", trainer.replace("return _bot->StoreItem(dest, item, true) == item;",
                                             "_bot->StoreItem(dest, item, true);\n                return true;", 1), factory_cpp, factory_h
    yield "pointer across hook", trainer.replace(
        "return false;\n                item = _bot->GetItemByPos(from);", "return false;", 1), factory_cpp, factory_h
    yield "any store slot", trainer.replace("dest.front().pos != to ||", "", 1), factory_cpp, factory_h
    yield "success when ran", trainer.replace("if (!outcome.Succeeded())", "if (!outcome.ran)", 1), factory_cpp, factory_h
    yield "no recovery text", trainer.replace("case Refusal::RecoveryRequired:", "case Refusal::None:", 1), factory_cpp, factory_h
    yield "factory mails off-hand", trainer, factory_cpp.replace(
        "!oldItemGuid.IsEmpty() && !CanEquipUnseenItem(slot, dest, bestItemForSlot)", "false", 1), factory_h
    yield "no source gate on BiS", trainer.replace("(_sources && !_sources->Allows(entry)) ||", "", 1), factory_cpp, factory_h
    yield "index after execute", trainer.replace("facts.sourcesAvailable =", "bool unusedSources =", 1), factory_cpp, factory_h
    hook = between(factory_cpp, "void PlayerbotFactory::InitRandomBotSourceFilter()", "\n}\n")
    yield "alt bots gated", trainer, factory_cpp.replace(
        hook, hook.replace("!sRandomPlayerbotMgr.IsRandomBot(bot)", "!bot", 1), 1), factory_h
    yield "human provider", trainer, factory_cpp.replace(
        hook, hook.replace("bot->GetQuestStatus(quest)", "sScriptMgr->TryReadPB59AutoGearProgression(bot)", 1), 1), factory_h
    yield "random filter wins", trainer, factory_cpp, re.sub(r"sourceFilter\s*\?\s*sourceFilter\s*:", "false ? sourceFilter :", factory_h, count=1)
    yield "swap", trainer.replace("_bot->AutoUnequipOffhandIfNeed();", "_bot->SwapItem(0, 0);", 1), factory_cpp, factory_h


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
    print(f"contract rules pass on the real source: {not errors}; negative controls caught {caught}/{total}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
