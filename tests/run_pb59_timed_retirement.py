#!/usr/bin/env python3
"""Compile exact timer dispatch/readiness extracts with synthetic dependencies."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def between(source, start, end):
    if source.count(start) != 1:
        raise RuntimeError(f"expected unique anchor: {start}")
    pos = source.index(start)
    return source[pos:source.index(end, pos + len(start))].rstrip() + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default="g++")
    parser.add_argument("--core", type=Path, required=True, help="Paired Core checkout with PB59 login admission guards")
    parser.add_argument("--sanitizers", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    mgr = (root / "src/Bot/PlayerbotMgr.cpp").read_text(encoding="utf-8")
    ai = (root / "src/Bot/PlayerbotAI.cpp").read_text(encoding="utf-8")
    script = (root / "src/Script/Playerbots.cpp").read_text(encoding="utf-8")
    secure = (root / "src/Script/PlayerbotsSecureLogin.cpp").read_text(encoding="utf-8")
    core = (args.core / "src/server/game/Handlers/CharacterHandler.cpp").read_text(encoding="utf-8")
    native = between(mgr, "PlayerbotHolder::~PlayerbotHolder()", "class PlayerbotLoginQueryHolder")
    ready = between(ai, "bool PlayerbotAI::IsTimedLogoutReady()", "void PlayerbotAI::UpdateAIInternal(")
    outer = between(ai, "void PlayerbotAI::UpdateAI(uint32 elapsed, bool minimal)", "    // Bots send no movement opcodes")
    secure_body = between(secure, "class PlayerbotsSecureLoginServerScript", "void AddPlayerbotsSecureLoginScripts()")
    admission = between(core, "bool WorldSession::CanPB59RetireBotForClientLogin", "void WorldSession::HandlePlayerLoginOpcode")
    load = between(core, "void WorldSession::HandlePlayerLoginFromDB(LoginQueryHolder const& holder)", "    Player* pCurrChar = new Player(this);")
    assert outer.index("IsLoggingOut()") < outer.index("RequestTimedBotLogout(bot)") < outer.index("IsInWorld()")
    assert "CanPB59RetireBotForClientLogin(loginGuid)" in secure_body and "RetireBotForClientLogin(existing)" in secure_body
    assert "IsLegitCharacterForAccount(guid)" in admission and "!sToCloud9Sidecar->ClusterModeEnabled()" in admission
    assert "FindConnectedPlayer(playerGuid)" in load and "FindOfflineSessionForCharacterGUID" in load
    timer = between(ai, "    // Never destroy a headless session", "    botOutgoingPacketHandlers.Handle(helper);")
    assert "RequestTimedBotLogout(bot)" in timer and "LogoutPlayerBot(" not in timer
    logout = between(mgr, "BotRetirementResult PlayerbotHolder::TryLogoutPlayerBot(", "bool PlayerbotHolder::DisablePlayerBot(")
    disable = between(mgr, "BotRetirementResult PlayerbotHolder::TryDisablePlayerBot(", "void PlayerbotHolder::RemoveFromPlayerbotsMap")
    assert "botWorldSessionPtr->PlayerLogout()" in logout and "timedLogout ? !botAI->IsTimedLogoutReady()" in logout
    assert "timedLogout ? botWorldSessionPtr->IsLoggingOut()" in logout
    for body in (logout, disable):
        assert "GetPB59SessionIncarnation() == sessionIncarnation" in body
        assert body.index("ForgetTimedBot") < body.index("RemoveFromPlayerbotsMap")
    publish = between(mgr, "bool PlayerbotHolder::OnBotLogin(Player* const bot,", "    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);")
    assert publish.index("playerBots.emplace") < publish.index("RegisterTimedBot") < publish.index("publication.Publish") < publish.index("OnBotLoginInternal")
    assert logout.index("TryReserveRetirement") < logout.index("MarkMutation") < logout.index("TakeForRecovery") < logout.index("RemoveFromPlayerbotsMap") < logout.index("DeleteDetached")
    assert "delete botWorldSessionPtr" not in logout
    assert script.index("BindOwnerThread") < script.index("ProcessTimedBotLogouts")
    assert script.index("ProcessTimedBotLogouts") < script.index("UpdatePendingLogins") < script.index("PlayerbotWorldThreadProcessor::instance().Update(diff)")
    shutdown = between(script, "    void OnShutdown() override", "class PlayerBotsBGScript")
    assert shutdown.index("StopTimedBotLogouts") < shutdown.index("StopPendingLogins") < shutdown.index("LogoutAllBots")
    with tempfile.TemporaryDirectory(prefix="pb59-timed-") as tmp:
        work = Path(tmp)
        (work / "timed_native.inc").write_text(native, encoding="utf-8")
        (work / "ready_native.inc").write_text(ready, encoding="utf-8")
        for name, body in (("outer", outer), ("secure", secure_body), ("admission", admission), ("load", load)):
            (work / (name + "_native.inc")).write_text(body, encoding="utf-8")
        for name in ("pb59_timed_retirement_mailbox.cpp", "pb59_timed_native_body.cpp"):
            binary = work / Path(name).stem
            command = [args.cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-pthread",
                       "-I", str(root / "src/Bot"), "-I", str(work),
                       str(root / "tests" / name), "-o", str(binary)]
            if args.sanitizers:
                command.append("-fsanitize=address,undefined")
            subprocess.run(command, check=True)
            subprocess.run([str(binary)], check=True)
    print("PASS: mailbox, exact synthetic timer entry/dispatch, secure-login and paired Core admission/load guards, source contracts")


if __name__ == "__main__":
    main()
