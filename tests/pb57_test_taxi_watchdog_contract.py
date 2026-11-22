#!/usr/bin/env python3
"""Source contract for the live taxi watchdog adapter (PlayerbotAI.cpp / PlayerbotAI.h).

The watchdog policy is tested as a pure model (pb57_taxi_watchdog.cpp). Recovery itself needs a
running server, so these checks pin what the adapter source does: who is eligible, when it samples,
how recovery cleans up, and what it never does. They do not prove Core behaviour in game. Each rule
is also run against deliberately broken copies (negative controls).

CS01 coordination #59 C2 and #52 Part 28 item 3: death, relog, missing route, bots only, humans
unchanged, no kill or revive.
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


def check(cpp, header, conf):
    errors = []

    def rule(ok, text):
        if not ok:
            errors.append(text)

    update_ai = between(cpp, "void PlayerbotAI::UpdateAI(uint32 elapsed, bool minimal)", "\n}\n")
    update = between(cpp, "void PlayerbotAI::UpdateTaxiWatchdog(uint32 elapsed)", "\n}\n")
    recover = between(cpp, "bool PlayerbotAI::RecoverStuckTaxi()", "\n}\n")

    # Sampled from the bot's own AI update, after its invalid-state return (logout, teleport, removal).
    rule(ordered(update_ai, "IsBeingTeleported()", "IsLoggingOut()", "return;", "UpdateTaxiWatchdog(elapsed);"),
         "the watchdog runs only after UpdateAI's invalid-state return")
    rule(update_ai.count("UpdateTaxiWatchdog(") == 1, "the watchdog is sampled once per AI update")

    # Bots only: PlayerbotAI exists only for bots; self-bots (a human's own character) and dead bots
    # are not eligible; the option gates everything.
    rule(re.search(r"sample\.eligible\s*=\s*config\.enabled\s*&&\s*!IsSelfBot\(bot\)\s*&&\s*bot->IsAlive\(\)", update)
         is not None, "eligibility needs the option, a non-self bot and a living bot")
    rule("config.enabled = sPlayerbotAIConfig.TaxiWatchdogEnable;" in update, "the option feeds the policy")
    rule(re.search(r"^Playerbots\.TaxiWatchdog\.Enable\s*=\s*0\s*$", conf, re.M) is not None,
         "the shipped default is off")

    # Relog: the state lives in this PlayerbotAI instance (a new login gets a new one), never static.
    rule(re.search(r"^\s*PlayerbotsTaxiWatchdog::Watchdog taxiWatchdog;", header, re.M) is not None,
         "the watchdog state is a PlayerbotAI member")
    rule(re.search(r"static\s+PlayerbotsTaxiWatchdog::Watchdog", header + cpp) is None,
         "the watchdog state is never static")

    # Only the policy's Recover runs a recovery, and the policy is told about it.
    rule(ordered(update, "case PlayerbotsTaxiWatchdog::Action::Recover:", "RecoverStuckTaxi()",
                 "taxiWatchdog.Recovered("),
         "Recover runs RecoverStuckTaxi and reports it")
    rule(update.count("RecoverStuckTaxi()") == 1, "recovery is called from one place")

    # Recovery: read the route first (owner decision: the final flight master), clean up exactly as a
    # landing does, then teleport to a validated node, else homebind. A missing route falls through.
    rule(ordered(recover, "RecoveryNodes(bot->m_taxi.GetPath())", "MovementExpired()",
                 "ClearUnitState(UNIT_STATE_IN_FLIGHT)", "CleanupAfterTaxiFlight()", "StopMoving()"),
         "recovery reads the route, then clears flight state like a landing")
    rule(ordered(recover, "CleanupAfterTaxiFlight()", "IsValidMapCoord(entry->map_id", "TeleportTo(entry->map_id"),
         "a node teleport is validated and happens after cleanup")
    rule(ordered(recover, "TeleportTo(entry->map_id", "m_homebindMapId", "TeleportTo(bot->m_homebindMapId"),
         "homebind is the fallback when no node can be used")
    for banned in ("Kill(", "DealDamage(", "setDeathState(", "ResurrectPlayer(", "SetHealth(", "DestroyItem("):
        rule(banned not in recover and banned not in update, f"the watchdog never calls {banned}")
    return errors


def mutants(cpp, header, conf):
    update = between(cpp, "void PlayerbotAI::UpdateTaxiWatchdog(uint32 elapsed)", "\n}\n")
    recover = between(cpp, "bool PlayerbotAI::RecoverStuckTaxi()", "\n}\n")

    def in_update(old, new):
        return cpp.replace(update, update.replace(old, new, 1), 1)

    def in_recover(old, new):
        return cpp.replace(recover, recover.replace(old, new, 1), 1)

    yield "dead bots eligible", in_update(" && bot->IsAlive()", ""), header, conf
    yield "self bots eligible", in_update("!IsSelfBot(bot) && ", ""), header, conf
    yield "default on", cpp, header, conf.replace("Playerbots.TaxiWatchdog.Enable = 0", "Playerbots.TaxiWatchdog.Enable = 1", 1)
    yield "static state", cpp, header.replace("PlayerbotsTaxiWatchdog::Watchdog taxiWatchdog;",
                                              "static PlayerbotsTaxiWatchdog::Watchdog taxiWatchdog;", 1), conf
    yield "route read after cleanup", in_recover(
        "std::array<uint32, 3> const nodes = PlayerbotsTaxiWatchdog::RecoveryNodes(bot->m_taxi.GetPath());", ""), header, conf
    yield "no cleanup", in_recover("bot->CleanupAfterTaxiFlight();", ""), header, conf
    yield "unvalidated teleport", in_recover("MapMgr::IsValidMapCoord(entry->map_id, entry->x, entry->y, entry->z) &&", ""), header, conf
    yield "kill", in_recover("bot->StopMoving();", "bot->StopMoving(); bot->Kill(bot, bot);"), header, conf
    yield "not reported", in_update("taxiWatchdog.Recovered(taxiWatchdogClockMs);", ""), header, conf


def main():
    root = Path(__file__).resolve().parents[1]
    cpp = (root / "src/Bot/PlayerbotAI.cpp").read_text(encoding="utf-8")
    header = (root / "src/Bot/PlayerbotAI.h").read_text(encoding="utf-8")
    conf = (root / "conf/playerbots.conf.dist").read_text(encoding="utf-8")
    failures = 0
    errors = check(cpp, header, conf)
    for error in errors:
        print(f"FAIL {error}")
    failures += len(errors)
    caught = total = 0
    for name, c, h, f in mutants(cpp, header, conf):
        total += 1
        if (c, h, f) == (cpp, header, conf):
            print(f"FAIL negative control '{name}' did not change the source")
            failures += 1
        elif check(c, h, f):
            caught += 1
        else:
            print(f"FAIL negative control '{name}' was not caught")
            failures += 1
    print(f"contract rules pass on the real source: {not errors}; negative controls caught {caught}/{total}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
