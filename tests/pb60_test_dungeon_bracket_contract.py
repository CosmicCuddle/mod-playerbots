#!/usr/bin/env python3
"""Source contract for the live dungeon bracket hooks (src/Script/Playerbots.cpp).

The bracket rules are tested as pure code (pb60_dungeon_bracket_runtime.cpp). The hooks need a running
server, so these checks pin what the hook source does: bots only, default off, the LFG selection read
only where Core reads it, the destination handed from BeforeTeleport to CanEnterMap and consumed once,
no live master pointer used off the world thread, and no bypass of Core's own checks. They do not prove
Core behaviour in game. Each rule also runs against deliberately broken copies (negative controls).

CS01 coordination #59 C3 and #52 Part 28 item 3.
"""
from pathlib import Path
import re
import sys


def between(source, start, end):
    if source.count(start) != 1:
        raise RuntimeError(f"expected exactly one: {start}")
    pos = source.index(start)
    return source[pos:source.index(end, pos + len(start))]


def ordered(text, *needles):
    pos = -1
    for needle in needles:
        found = text.find(needle, pos + 1)
        if found < 0:
            return False
        pos = found
    return True


def check(cpp, conf):
    errors = []

    def rule(ok, text):
        if not ok:
            errors.append(text)

    gate = between(cpp, "bool CanBotEnterDungeon(Player* player, MapEntry const* entry, bool lfgIdentity,",
                   "\n    }\n}\n")
    can_enter = between(cpp, "bool OnPlayerCanEnterMap(Player* player, MapEntry const* entry,", "\n    }\n")
    before = between(cpp, "bool OnPlayerBeforeTeleport(Player* player, uint32 mapid,", "/* for now commmented out")
    take = between(cpp, "PlayerbotsDungeonBracket::Destination TakePendingTeleport(Player* player, uint32 mapId)",
                   "\n    }\n")

    # Bots only, non-raid dungeons only, option off by default; everything else passes untouched.
    rule(ordered(gate, "!sPlayerbotAIConfig.EnforceDungeonLevelBracket", "!player->GetSession()->IsHeadless()",
                 "!entry->IsNonRaidDungeon()", "return true;"),
         "the gate passes humans, raids and the disabled option before anything else")
    rule(re.search(r"^Playerbots\.EnforceDungeonLevelBracket\s*=\s*0\s*$", conf, re.M) is not None,
         "the shipped default is off")
    rule(re.search(r'^Playerbots\.DungeonLevelBracket\.Overrides\s*=\s*""\s*$', conf, re.M) is not None,
         "no reviewed override ships by default")

    # LFG identity only on the CanEnterMap path and only for LFG groups (where Core reads it too).
    rule(ordered(gate, "if (lfgIdentity)", "group->isLFGGroup()", "sLFGMgr->GetDungeon("),
         "the LFG selection is read only for LFG groups on the CanEnterMap path")
    rule(gate.count("sLFGMgr->GetDungeon(") == 1, "the LFG selection is read in one place")
    rule("CanBotEnterDungeon(player, entry, true, destination)" in can_enter,
         "CanEnterMap passes LFG identity")
    rule("CanBotEnterDungeon(player, sMapStore.LookupEntry(mapid), false," in before,
         "BeforeTeleport never reads the LFG selection")

    # Allowed and the holiday exception return true: Core then runs its own checks (no bypass).
    rule(ordered(gate, "Result::Allowed", "Result::SeasonalException", "return true;"),
         "only Allowed and the seasonal exception pass")
    rule("return true;" not in gate.split("SeasonalException")[1].split("ShouldSendBracketNotice")[0].replace(
        "return true;", "", 1), "nothing else returns true after the decision")

    # Destination hand-off: stored only for cross-map bot teleports with the option on, consumed once
    # and bound to bot, map and time.
    rule(ordered(before, "sPlayerbotAIConfig.EnforceDungeonLevelBracket", "player->GetMapId() != mapid",
                 "IsHeadless()", "pendingTeleport ="),
         "the destination is stored only for cross-map bot teleports with the option on")
    rule(re.search(r"thread_local\s+PendingTeleport\s+pendingTeleport;", cpp) is not None,
         "the hand-off is thread_local (TeleportTo calls both hooks on one thread)")
    rule(ordered(take, "pendingTeleport = PendingTeleport{};", "pending.bot != player->GetGUID()",
                 "pending.mapId != mapId", "PENDING_TELEPORT_MS", "return {};"),
         "the destination is cleared on read and checked against bot, map and time")
    rule(ordered(can_enter, "if (loginCheck && player->GetMapId() == entry->MapID)", "else if (!loginCheck)",
                 "TakePendingTeleport("),
         "login uses the saved position; only teleports take the stored destination")

    # Same-map teleports are not re-judged; GM-mode cross-map teleports are (they skip PlayerCannotEnter).
    rule("TeleportNeedsBracketCheck(" in before and "player->GetMapId() == mapid" in before,
         "BeforeTeleport re-checks only GM-mode cross-map teleports")

    # The master's notice goes through the world-thread processor by GUID, rate limited.
    rule(ordered(gate, "ShouldSendBracketNotice(player->GetGUID())", "return false;", "QueueOperation(",
                 "master->GetGUID()"),
         "the denial notice is rate limited and queued to the world thread by GUID")
    rule("SendSysMessage" not in gate and "PSendSysMessage" not in gate,
         "the gate never messages the master from the map thread")
    for banned in ("TeleportTo(", "RemoveFromGroup(", "Kill(", "SetLevel(", "GiveLevel("):
        rule(banned not in gate and banned not in before.split("return false;")[0],
             f"the bracket never calls {banned}")
    return errors


def mutants(cpp, conf):
    gate = between(cpp, "bool CanBotEnterDungeon(Player* player, MapEntry const* entry, bool lfgIdentity,",
                   "\n    }\n}\n")
    before = between(cpp, "bool OnPlayerBeforeTeleport(Player* player, uint32 mapid,", "/* for now commmented out")
    take = between(cpp, "PlayerbotsDungeonBracket::Destination TakePendingTeleport(Player* player, uint32 mapId)",
                   "\n    }\n")

    def swap(part, old, new):
        return cpp.replace(part, part.replace(old, new, 1), 1)

    yield "humans gated", swap(gate, "!player->GetSession()->IsHeadless() ||", ""), conf
    yield "default on", cpp, conf.replace("Playerbots.EnforceDungeonLevelBracket = 0",
                                          "Playerbots.EnforceDungeonLevelBracket = 1", 1)
    yield "LFG read for any group", swap(gate, "group && group->isLFGGroup()", "group"), conf
    yield "LFG read on teleport", swap(before, "sMapStore.LookupEntry(mapid), false,", "sMapStore.LookupEntry(mapid), true,"), conf
    yield "stash not cleared", swap(take, "pendingTeleport = PendingTeleport{};", ""), conf
    yield "stash without time bound", swap(take, "GetMSTimeDiffToNow(pending.atMs) > PENDING_TELEPORT_MS", "false"), conf
    yield "stash on same map", swap(before, "player->GetMapId() != mapid &&", ""), conf
    yield "not thread_local", cpp.replace("thread_local PendingTeleport pendingTeleport;", "PendingTeleport pendingTeleport;", 1), conf
    yield "direct master message", swap(gate, "PlayerbotWorldThreadProcessor::instance().QueueOperation(",
                                        "ChatHandler(master->GetSession()).SendSysMessage(\"x\"); PlayerbotWorldThreadProcessor::instance().QueueOperation("), conf
    yield "no rate limit", swap(gate, "if (!ShouldSendBracketNotice(player->GetGUID()))\n            return false;", ""), conf
    yield "login takes stash", cpp.replace(
        "else if (!loginCheck)\n            destination = TakePendingTeleport(player, entry->MapID);",
        "else\n            destination = TakePendingTeleport(player, entry->MapID);", 1), conf


def main():
    root = Path(__file__).resolve().parents[1]
    cpp = (root / "src/Script/Playerbots.cpp").read_text(encoding="utf-8")
    conf = (root / "conf/playerbots.conf.dist").read_text(encoding="utf-8")
    failures = 0
    errors = check(cpp, conf)
    for error in errors:
        print(f"FAIL {error}")
    failures += len(errors)
    caught = total = 0
    for name, c, f in mutants(cpp, conf):
        total += 1
        if (c, f) == (cpp, conf):
            print(f"FAIL negative control '{name}' did not change the source")
            failures += 1
        elif check(c, f):
            caught += 1
        else:
            print(f"FAIL negative control '{name}' was not caught")
            failures += 1
    print(f"contract rules pass on the real source: {not errors}; negative controls caught {caught}/{total}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
