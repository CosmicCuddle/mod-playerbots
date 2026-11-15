#!/usr/bin/env python3
"""Compile exact PB retirement bodies with synthetic native dependencies."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def between(source, start, end):
    if source.count(start) != 1:
        raise RuntimeError(f"expected unique method: {start}")
    pos = source.index(start)
    return source[pos:source.index(end, pos)].rstrip() + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", type=Path, required=True)
    parser.add_argument("--cxx", default="g++")
    parser.add_argument("--sanitizers", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    mgr = (root / "src/Bot/PlayerbotMgr.cpp").read_text(encoding="utf-8")
    random = (root / "src/Bot/RandomPlayerbotMgr.cpp").read_text(encoding="utf-8")
    body = between(mgr, "bool PlayerbotHolder::LogoutPlayerBot(ObjectGuid guid)",
                   "void PlayerbotHolder::RemoveFromPlayerbotsMap")
    bulk = between(mgr, "void PlayerbotHolder::LogoutAllBots()", "void PlayerbotMgr::CancelLogout()")
    # Source contract checks only: actual scheduling/DB behavior requires native tests.
    population = between(random, "uint32 RandomPlayerbotMgr::RemoveRandomBots()",
                         "// Population target for RandomBotCountMode 2")
    expiry = between(random, "bool RandomPlayerbotMgr::ProcessBot(uint32 bot)",
                     "bool RandomPlayerbotMgr::ProcessBot(Player* bot)")
    direct = between(random, "void RandomPlayerbotMgr::Remove(Player* bot)",
                     "CreatureData const* RandomPlayerbotMgr::GetCreatureDataByEntry")
    assert population.index("if (TryLogoutPlayerBot(guid, [&]") < population.index('SetEventValue(bot, "add", 0, 0)') < population.index("++removed;")
    assert population.count("currentBots.erase(bot)") == 1 and "return removed;" in population
    assert "return toLogout.size();" not in population
    assert expiry.index("TryLogoutPlayerBot(botGUID, [&]") < expiry.index("clearSchedule();")
    assert expiry.count('SetEventValue(bot, "add", 0, 0)') == 1
    assert direct.index("auto clearEvents =") < direct.index("TryLogoutPlayerBot(owner, clearEvents)")
    assert direct.index("retirement.MarkMutation()") < direct.index("clearEvents();") < direct.index("retirement.Complete()")
    assert "BotRetirementResult::RecoveryRequired" in mgr and "bot retirement incomplete; recovery required, do not retry" in mgr
    assert "}) == BotRetirementResult::Completed)" in population
    core = args.core.resolve() / "src/server/game/Entities/Player"
    fixtures = ["pb59_bot_retirement_body.cpp", "pb59_recovery_registry.cpp",
                "pb59_bulk_logout_body.cpp"]
    with tempfile.TemporaryDirectory(prefix="pb59-retirement-") as tmp:
        work = Path(tmp)
        (work / "retirement_native.inc").write_text(body, encoding="utf-8")
        (work / "LogoutAllBots.inc").write_text(bulk, encoding="utf-8")
        for name in fixtures:
            fixture = Path(__file__).with_name(name)
            binary = work / fixture.stem
            command = [args.cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-pthread",
                       "-I", str(core), "-I", str(root / "src/Bot"), "-I", str(work),
                       str(fixture), "-o", str(binary)]
            if args.sanitizers:
                command.append("-fsanitize=address,undefined")
            subprocess.run(command, check=True)
            subprocess.run([str(binary)], check=True)
    print("PASS: exact synthetic retirement and bulk bodies, journal, scheduler source contracts")


if __name__ == "__main__":
    main()
