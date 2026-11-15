#!/usr/bin/env python3
"""Compile exact pending and queued login extracts with synthetic dependencies."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def between(source, start, end):
    if source.count(start) != 1:
        raise RuntimeError(f"expected unique anchor: {start}")
    pos = source.index(start)
    return source[pos:source.index(end, pos)].rstrip() + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default="g++")
    parser.add_argument("--sanitizers", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    mgr = (root / "src/Bot/PlayerbotMgr.cpp").read_text(encoding="utf-8")
    ops = (root / "src/Script/WorldThr/PlayerbotOperations.h").read_text(encoding="utf-8")
    script = (root / "src/Script/Playerbots.cpp").read_text(encoding="utf-8")
    native = between(mgr, "void PlayerbotHolder::UpdatePendingLogins()", "bool PlayerbotHolder::IsAccountLinked") + "\n"
    native += between(mgr, "void PlayerbotHolder::HandlePlayerBotLoginCallback", "void PlayerbotHolder::UpdateSessions()") + "\n"
    native += between(mgr, "void PlayerbotHolder::OnBotLogin(Player* const bot,", "    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);") + "}\n"
    queued = between(ops, "class OnBotLoginOperation : public PlayerbotOperation", "class ArenaTeamAssignOperation")
    # Source contracts: AddPlayerBot and callback registration are not compiled
    # by this fixture; neither are the post-publication native initialization
    # callbacks or the full WorldSession/DB/world-thread integration.
    allocation = between(mgr, "void PlayerbotHolder::AddPlayerBot(", "void PlayerbotHolder::UpdatePendingLogins()")
    assert "std::make_unique<WorldSession>" in allocation
    assert "botLoading.try_emplace" in allocation and "std::move(session)" in allocation
    assert "AfterComplete([playerGuid, sessionId]" in allocation
    assert "owner.Matches(sessionId)" in allocation and "owner.Busy()" in allocation
    assert "masterPlayerIncarnation" in allocation and "masterSessionIncarnation" in allocation
    assert "botLoading.erase" not in allocation and "delete " not in allocation
    callback = between(mgr, "void PlayerbotHolder::HandlePlayerBotLoginCallback", "bool PlayerbotHolder::CompleteQueuedBotLogin")
    assert "QueueOperation(std::move(op))" in callback and "pending.owner.Fail()" in callback
    assert "botLoading.erase" not in callback
    assert native.index("playerBots.emplace(guid, bot)") < native.index("pendingOwner.ReleasePublished") < native.index("OnBotLoginInternal(bot)")
    shutdown = between(script, "    void OnShutdown() override", "class PlayerBotsBGScript")
    assert shutdown.index("StopPendingLogins") < shutdown.index("LogoutAllBots") < shutdown.index("ClearPendingLogins")
    with tempfile.TemporaryDirectory(prefix="pb59-pending-") as tmp:
        work = Path(tmp)
        (work / "pending_native.inc").write_text(native, encoding="utf-8")
        (work / "queued_native.inc").write_text(queued, encoding="utf-8")
        for name in ("pb59_pending_session_owner.cpp", "pb59_pending_native_body.cpp"):
            binary = work / Path(name).stem
            command = [args.cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-pthread",
                       "-I", str(root / "src/Bot"), "-I", str(work),
                       str(root / "tests" / name), "-o", str(binary)]
            if args.sanitizers:
                command.append("-fsanitize=address,undefined")
            subprocess.run(command, check=True)
            subprocess.run([str(binary)], check=True)
    print("PASS: pending owner, exact synthetic native pending/queue bodies, source contracts")


if __name__ == "__main__":
    main()
