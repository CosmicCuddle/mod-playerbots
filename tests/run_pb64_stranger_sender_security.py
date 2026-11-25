#!/usr/bin/env python3
"""Stranger-sender security seam: compile the exact PlayerbotSecurity source with mocks and pin the
HandleCommand admission gates and queued silent recheck as text. No database, server or Core tree needed.
Not native proof: HandleCommand, chat hooks and packet delivery are not compiled or run."""
import argparse
import re
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--cxx", default="g++")
parser.add_argument("--sanitize", action="store_true")
parser.add_argument("--pb", type=Path, default=Path(__file__).resolve().parent.parent)
args = parser.parse_args()
pb = args.pb
h = (pb / "src/Mgr/Security/PlayerbotSecurity.h").read_text()
c = (pb / "src/Mgr/Security/PlayerbotSecurity.cpp").read_text()
ai = (pb / "src/Bot/PlayerbotAI.cpp").read_text()
header = h[h.index("enum PlayerbotSecurityLevel"):h.rindex("#endif")]
source = c[c.index("PlayerbotSecurity::PlayerbotSecurity(Player* const bot)"):]
compact = re.sub(r"\s+", "", ai)
pins = {
    "ref overload gate1 INVITE": "if(!IsAllowedCommand(filtered)&&!GetSecurity()->CheckLevelFor(PlayerbotSecurityLevel::PLAYERBOT_SECURITY_INVITE,type!=CHAT_MSG_WHISPER,&fromPlayer))return;",
    "ref overload gate2 ALLOW_ALL": "if(!IsAllowedCommand(filtered)&&!GetSecurity()->CheckLevelFor(PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL,type!=CHAT_MSG_WHISPER,&fromPlayer))return;",
    "ptr overload gate1 INVITE": "if(!GetSecurity()->CheckLevelFor(PLAYERBOT_SECURITY_INVITE,type!=CHAT_MSG_WHISPER,fromPlayer))return;",
    "ptr overload gate2 ALLOW_ALL": "if(!IsAllowedCommand(filtered)&&(!GetSecurity()->CheckLevelFor(PLAYERBOT_SECURITY_ALLOW_ALL,type!=CHAT_MSG_WHISPER,fromPlayer)))return;",
    "queued op silent recheck": "if(!ai||!ai->GetSecurity()->CheckLevelFor(_requiredLevel,true,sender))returnfalse;",
}
failed = [name for name, text in pins.items() if compact.count(text) != 1]
for name in pins:
    print(("PIN FAIL " if name in failed else "PIN ok   ") + name)
unsecured = re.findall(r'unsecuredCommands\.insert\("([^"]+)"\)', ai)
if not unsecured or any("autogear".startswith(u) for u in unsecured):
    failed.append("autogear unsecured")
    print("FAIL: autogear unsecured-prefix check")
with tempfile.TemporaryDirectory() as directory:
    d = Path(directory)
    (d / "security_h.inc").write_text(header)
    (d / "security_cpp.inc").write_text(source)
    shutil.copyfile(Path(__file__).resolve().parent / "pb64_stranger_sender_security.cpp", d / "fixture.cpp")
    flags = ["-std=c++20", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", f"-I{d}"]
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
    subprocess.run([args.cxx, *flags, str(d / "fixture.cpp"), "-o", str(d / "test")], check=True)
    rc = subprocess.run([str(d / "test")]).returncode
raise SystemExit(rc or (1 if failed else 0))
